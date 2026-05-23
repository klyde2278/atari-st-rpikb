/*
 * Atari ST RP2040 IKBD Emulator
 * Copyright (C) 2021 Roy Hopkins
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

// HidInput — keyboard report handling.
#include "HidInput.h"
#include "st_key_lookup.h"
#include "tusb.h"
#include "hid_app_host.h"
#include "config.h"
#include "hardware/clocks.h"
#include "6301.h"
#include "ssd1306.h"
#include <map>
#include <stdint.h>

// Shared globals defined in HidInput_common.cpp.
extern std::map<int, uint8_t*> device;
extern UserInterface* ui_;
extern uint8_t current_led_state;
extern const uint8_t* s_current_lookup;

// Per-device last LED byte successfully queued (0xFF = never sent to this device).
// A std::map is used so that newly plugged keyboards automatically get 0xFF
// (treated as "unknown") the first time handle_keyboard() sees their address.
static std::map<int, uint8_t> dev_last_led;

extern ssd1306_t disp;

// ---------------------------------------------------------------------------
// Atari ST scancodes used in this file
// ---------------------------------------------------------------------------
#define ATARI_LSHIFT    42
#define ATARI_RSHIFT    54
#define ATARI_ALT       56
#define ATARI_CTRL      29
#define ATARI_CAPSLOCK  58

// ---------------------------------------------------------------------------
// HID modifier bit masks
// ---------------------------------------------------------------------------
#define MOD_LCTRL   (1 << 0)
#define MOD_RCTRL   (1 << 4)
#define MOD_LALT    (1 << 2)
#define MOD_RALT    (1 << 6)
#define MOD_LSHIFT  (1 << 1)
#define MOD_RSHIFT  (1 << 5)

// ---------------------------------------------------------------------------
// System shortcut keycodes (HID usage page 0x07).
// Centralised here so they are easy to find and change.
// ---------------------------------------------------------------------------
static constexpr uint8_t KEY_TOGGLE_MOUSE  = 0x45;  // F12
static constexpr uint8_t KEY_XRESET       = 0x44;  // F11
static constexpr uint8_t KEY_JOY0_TOGGLE  = 0x42;  // F9
static constexpr uint8_t KEY_JOY1_TOGGLE  = 0x43;  // F10
static constexpr uint8_t KEY_KP_ADD       = 0x57;  // Keypad +
static constexpr uint8_t KEY_KP_SUBTRACT  = 0x56;  // Keypad -
static constexpr uint8_t KEY_CAPS_LOCK    = 0x39;

// ---------------------------------------------------------------------------
// is_key_pressed() — check whether a keycode appears in the 6-slot report.
//
// Replaces the repeated "for (int i = 0; i < 6; ++i)" blocks that were
// copy-pasted throughout the original handle_keyboard() function.
// ---------------------------------------------------------------------------
static inline bool is_key_pressed(const hid_keyboard_report_t* kb, uint8_t keycode)
{
    for (int i = 0; i < 6; ++i) {
        if (kb->keycode[i] == keycode) return true;
    }
    return false;
}

// Convenience helpers for the two modifier groups used most often.
static inline bool ctrl_held(const hid_keyboard_report_t* kb) {
    return (kb->modifier & MOD_LCTRL) || (kb->modifier & MOD_RCTRL);
}
static inline bool alt_held(const hid_keyboard_report_t* kb) {
    return (kb->modifier & MOD_LALT) || (kb->modifier & MOD_RALT);
}

// ---------------------------------------------------------------------------
// one_shot() — generic helper for "trigger action on first press" shortcuts.
//
// Replaces the repeated pattern:
//   static bool last_xxx = false;
//   if (condition) { if (!last_xxx) { action(); last_xxx = true; } }
//   else           { last_xxx = false; }
//
// Usage: one_shot(condition, last_state, [&]{ action(); });
// ---------------------------------------------------------------------------
template<typename Fn>
static void one_shot(bool condition, bool& last_state, Fn&& action)
{
    if (condition) {
        if (!last_state) {
            action();
            last_state = true;
        }
    } else {
        last_state = false;
    }
}

// ---------------------------------------------------------------------------
// hid_keycode_to_label
// Returns a short display label (≤ 3 chars + NUL) for a USB HID keycode.
// Labels follow the standard HID Usage Table (QWERTY convention) and are
// independent of the active keyboard layout.  They identify the physical key
// position rather than the character it produces under a given layout.
//
// A static single-char buffer is used for letter and digit keys; all other
// labels are string literals.  The function is only called from the main
// loop (not from an IRQ), so the static buffer is safe on RP2040.
// ---------------------------------------------------------------------------
static const char* hid_keycode_to_label(uint8_t hk) {
    // Letters A–Z: HID 0x04–0x1D in QWERTY order.
    static const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static char single[2] = {0, 0};

    if (hk >= 0x04 && hk <= 0x1D) {
        single[0] = letters[hk - 0x04];
        return single;
    }
    // Digits 1–9.
    if (hk >= 0x1E && hk <= 0x26) {
        single[0] = (char)('1' + (hk - 0x1E));
        return single;
    }

    switch (hk) {
        case 0x27: return "0";
        case 0x28: return "Ent";
        case 0x29: return "Esc";
        case 0x2A: return "Bs";
        case 0x2B: return "Tab";
        case 0x2C: return "Sp";
        // Punctuation — show the unshifted character where unambiguous.
        case 0x2D: return "-";
        case 0x2E: return "=";
        case 0x2F: return "[";
        case 0x30: return "]";
        case 0x31: return "\\";
        case 0x32: return "#";
        case 0x33: return ";";
        case 0x34: return "'";
        case 0x35: return "`";
        case 0x36: return ",";
        case 0x37: return ".";
        case 0x38: return "/";
        case 0x39: return "Cap";
        // Function keys.
        case 0x3A: return "F1";
        case 0x3B: return "F2";
        case 0x3C: return "F3";
        case 0x3D: return "F4";
        case 0x3E: return "F5";
        case 0x3F: return "F6";
        case 0x40: return "F7";
        case 0x41: return "F8";
        case 0x42: return "F9";
        case 0x43: return "F10";
        case 0x44: return "F11";
        case 0x45: return "F12";
        // Navigation cluster.
        case 0x46: return "Prt";
        case 0x47: return "Scr";
        case 0x48: return "Pau";
        case 0x49: return "Ins";
        case 0x4A: return "Hm";
        case 0x4B: return "PU";
        case 0x4C: return "Del";
        case 0x4D: return "End";
        case 0x4E: return "PD";
        // Arrow keys: reuse the custom display glyphs already in the font.
        case 0x4F: return "\x86"; // right
        case 0x50: return "\x85"; // left
        case 0x51: return "\x84"; // down
        case 0x52: return "\x83"; // up
        // Keypad.
        case 0x53: return "NL";
        case 0x54: return "K/";
        case 0x55: return "K*";
        case 0x56: return "K-";
        case 0x57: return "K+";
        case 0x58: return "KE";
        case 0x59: return "K1";
        case 0x5A: return "K2";
        case 0x5B: return "K3";
        case 0x5C: return "K4";
        case 0x5D: return "K5";
        case 0x5E: return "K6";
        case 0x5F: return "K7";
        case 0x60: return "K8";
        case 0x61: return "K9";
        case 0x62: return "K0";
        case 0x63: return "K.";
        default:   return "?";
    }
}


// Called from tuh_hid_unmounted_cb (HidInput_common.cpp) on keyboard unplug.
// Clears per-device LED state so the next keyboard at the same address
// gets a fresh send on its first report cycle.
void hid_keyboard_on_unmount(int dev_addr) {
    dev_last_led.erase(dev_addr);
}

void HidInput::handle_keyboard()
{
    bool any_polled    = false;
    bool any_key_found = false;

    for (auto& it : device) {
        if (tuh_hid_get_type(it.first) != HID_KEYBOARD) continue;
        if (!tuh_hid_is_mounted(it.first) || tuh_hid_is_busy(it.first)) continue;

        any_polled = true;

        auto* kb = reinterpret_cast<hid_keyboard_report_t*>(it.second);
        const bool ctrl_down = ctrl_held(kb);
        const bool alt_down  = alt_held(kb);

        // --- Ctrl+F12: toggle mouse / joystick 0 ---
        {
            static bool last = false;
            one_shot(ctrl_down && is_key_pressed(kb, KEY_TOGGLE_MOUSE), last, [&]{
                ui_->set_mouse_enabled(!ui_->get_mouse_enabled());
            });
        }

        // --- Ctrl+F11: trigger Atari ST reset ---
        {
            static bool last = false;
            one_shot(ctrl_down && is_key_pressed(kb, KEY_XRESET), last, [&]{
                ssd1306_clear(&disp);
                ssd1306_draw_string(&disp, 30, 20, 2, (char*)"RESET");
                ssd1306_draw_string(&disp, 20, 45, 1, (char*)"Ctrl + F11");
                ssd1306_show(&disp);
                sleep_ms(500);
                hd6301_trigger_reset();
            });
        }

        // --- Ctrl+F9: toggle joystick 0 source ---
        {
            static bool last = false;
            one_shot(ctrl_down && is_key_pressed(kb, KEY_JOY0_TOGGLE), last, [&]{
                ui_->toggle_joystick_source(0);
            });
        }

        // --- Ctrl+F10: toggle joystick 1 source ---
        {
            static bool last = false;
            one_shot(ctrl_down && is_key_pressed(kb, KEY_JOY1_TOGGLE), last, [&]{
                ui_->toggle_joystick_source(1);
            });
        }

        // --- Alt+Keypad+: overclock to 270 MHz ---
        {
            static bool last = false;
            one_shot(alt_down && is_key_pressed(kb, KEY_KP_ADD), last, [&]{
                set_sys_clock_khz(270000, false);
                ui_->set_cpu_speed(270000);
            });
        }

        // --- Alt+Keypad-: reduce clock to 150 MHz ---
        {
            static bool last = false;
            one_shot(alt_down && is_key_pressed(kb, KEY_KP_SUBTRACT), last, [&]{
                set_sys_clock_khz(150000, false);
                ui_->set_cpu_speed(150000);
            });
        }

        // --- Caps Lock ---
        static bool last_capslock_hw  = false;
        static bool capslock_on       = false;
        static bool capslock_pulse    = false;
        {
            bool pressed = is_key_pressed(kb, KEY_CAPS_LOCK);
            if (pressed && !last_capslock_hw) {
                capslock_on      = !capslock_on;
                capslock_pulse   = true;
                last_capslock_hw = true;
                current_led_state = (capslock_on ? 0x02 : 0x00) | 0x01;
            } else if (!pressed) {
                last_capslock_hw = false;
                capslock_pulse   = false;
            }
        }

        // -----------------------------------------------------------------------
        // Translate USB HID keycodes to Atari ST scancodes.
        // -----------------------------------------------------------------------
        uint8_t st_keys[6] = {};

        for (int i = 0; i < 6; ++i) {
            const uint8_t hk = kb->keycode[i];
            if (hk == 0 || hk >= 128) {
                st_keys[i] = 0;
                continue;
            }
            if (alt_down && (hk == KEY_KP_ADD || hk == KEY_KP_SUBTRACT)) {
                st_keys[i] = 0;
                continue;
            }
            if (ctrl_down && (hk == KEY_TOGGLE_MOUSE ||
                              hk == KEY_XRESET       ||
                              hk == KEY_JOY0_TOGGLE  ||
                              hk == KEY_JOY1_TOGGLE)) {
                st_keys[i] = 0;
                continue;
            }
            st_keys[i] = s_current_lookup[hk];
        }

        // -----------------------------------------------------------------------
        // Update key_states[].
        // -----------------------------------------------------------------------
        for (size_t i = 1; i < key_states.size(); ++i) {
            if (static_cast<int>(i) == ATARI_CAPSLOCK) {
                key_states[i] = capslock_pulse ? 1 : 0;
                continue;
            }
            bool down = false;
            for (int j = 0; j < 6; ++j) {
                if (st_keys[j] == static_cast<uint8_t>(i)) {
                    down = true;
                    break;
                }
            }
            key_states[i] = down ? 1 : 0;
        }

        key_states[ATARI_LSHIFT] = (kb->modifier & MOD_LSHIFT) ? 1 : 0;
        key_states[ATARI_RSHIFT] = (kb->modifier & MOD_RSHIFT) ? 1 : 0;
        key_states[ATARI_CTRL]   = ctrl_down ? 1 : 0;
        key_states[ATARI_ALT]    = alt_down  ? 1 : 0;

        // Send LED update when state changed or not yet sent to this device.
        // Per-device tracking (dev_last_led) fixes the multi-keyboard case where
        // a single shared static would suppress the LED send for the second keyboard.
        // hid_app_get_kb_led_rid() returns the report ID confirmed by the probe in
        // tuh_hid_set_protocol_complete_cb(); defaults to 0 while probing.
        {
            auto it_led = dev_last_led.find(it.first);
            uint8_t last_led = (it_led != dev_last_led.end()) ? it_led->second : 0xFF;

            if (current_led_state != last_led) {
                uint8_t instance = tuh_hid_get_instance((uint8_t)it.first);
                uint8_t rid = hid_app_get_kb_led_rid((uint8_t)it.first);
                if (rid == 0xFF) rid = 0;  // probe not done yet: try default

                if (tuh_hid_set_report((uint8_t)it.first, instance, rid,
                        HID_REPORT_TYPE_OUTPUT,
                        &current_led_state, sizeof(current_led_state))) {
                    dev_last_led[it.first] = current_led_state;
                }
            }
        }

        // Update the debug key label.
        for (int i = 0; i < 6; ++i) {
            if (kb->keycode[i] != 0) {
                any_key_found = true;
                if (last_key_label[0] == '\0') {
                    const char* lbl = hid_keycode_to_label(kb->keycode[i]);
                    strncpy(last_key_label, lbl, sizeof(last_key_label) - 1);
                    last_key_label[sizeof(last_key_label) - 1] = '\0';
                }
                break;
            }
        }

        hid_app_request_report(it.first, it.second);
    }

    if (any_polled && !any_key_found) {
        last_key_label[0] = '\0';
    }
}