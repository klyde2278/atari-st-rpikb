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
// handle_keyboard() — main keyboard processing loop.
// ---------------------------------------------------------------------------
void HidInput::handle_keyboard()
{
    for (auto& it : device) {
        if (tuh_hid_get_type(it.first) != HID_KEYBOARD) continue;
        if (!tuh_hid_is_mounted(it.first) || tuh_hid_is_busy(it.first)) continue;

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

        // --- Caps Lock: toggle state and send a single pulse to the Atari ---
        static bool last_capslock_hw  = false;
        static bool capslock_on       = false;
        static bool capslock_pulse    = false;
        {
            bool pressed = is_key_pressed(kb, KEY_CAPS_LOCK);
            if (pressed && !last_capslock_hw) {
                capslock_on      = !capslock_on;
                capslock_pulse   = true;
                last_capslock_hw = true;
                // Reflect Caps Lock state in the keyboard LED (bit 1), keep NumLock (bit 0).
                current_led_state = (capslock_on ? 0x02 : 0x00) | 0x01;
                bool sent = false;
                for (uint8_t idx = 0; idx < 3 && !sent; idx++) {
                    sent = tuh_hid_set_report(it.first, idx, 0,
                        HID_REPORT_TYPE_OUTPUT, &current_led_state,
                        sizeof(current_led_state));
                }
            } else if (!pressed) {
                last_capslock_hw = false;
                capslock_pulse   = false;
            }
        }

        // -----------------------------------------------------------------------
        // Translate USB HID keycodes to Atari ST scancodes via the active layout
        // lookup table. System shortcuts that have already been handled above are
        // suppressed here so they do not reach the Atari.
        // -----------------------------------------------------------------------
        uint8_t st_keys[6] = {};

        for (int i = 0; i < 6; ++i) {
            const uint8_t hk = kb->keycode[i];
            if (hk == 0 || hk >= 128) {
                st_keys[i] = 0;
                continue;
            }

            // Suppress Alt+Keypad+/- (clock speed shortcuts).
            if (alt_down && (hk == KEY_KP_ADD || hk == KEY_KP_SUBTRACT)) {
                st_keys[i] = 0;
                continue;
            }

            // Suppress Ctrl+F9/F10/F11/F12 (system shortcuts).
            if (ctrl_down && (hk == KEY_TOGGLE_MOUSE ||
                              hk == KEY_XRESET       ||
                              hk == KEY_JOY0_TOGGLE  ||
                              hk == KEY_JOY1_TOGGLE)) {
                st_keys[i] = 0;
                continue;
            }

            // Normal key: look up the Atari ST scancode for the active layout.
            st_keys[i] = s_current_lookup[hk];
        }

        // -----------------------------------------------------------------------
        // Update key_states[] from the translated scancodes.
        // -----------------------------------------------------------------------
        for (size_t i = 1; i < key_states.size(); ++i) {
            if (static_cast<int>(i) == ATARI_CAPSLOCK) {
                // Caps Lock is driven by the single-pulse flag, not the key state.
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

        // Modifier keys are sourced directly from the HID modifier byte.
        key_states[ATARI_LSHIFT] = (kb->modifier & MOD_LSHIFT) ? 1 : 0;
        key_states[ATARI_RSHIFT] = (kb->modifier & MOD_RSHIFT) ? 1 : 0;
        key_states[ATARI_CTRL]   = ctrl_down ? 1 : 0;
        key_states[ATARI_ALT]    = alt_down  ? 1 : 0;

        // Keep the NumLock LED active after each report cycle.
        for (uint8_t idx = 0; idx < 3; idx++) {
            tuh_hid_set_report(it.first, idx, 0,
                HID_REPORT_TYPE_OUTPUT, &current_led_state,
                sizeof(current_led_state));
        }

        hid_app_request_report(it.first, it.second);
    }
}
