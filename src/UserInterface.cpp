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

#include "UserInterface.h"
#include "UserInterface_i18n.h"
#include "version.h"
#include "pico/stdlib.h"
#include "hid_app_host.h"
#include "config.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include "hardware/vreg.h"
#include "HidInput.h"
#include "ssd1306_key.h"
#include "st_key_layout_label.h"
#include "hid_key_layout_label.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Key remap group definitions — arrays of Atari ST scancodes included in
// each remapping group.  Groups intentionally exclude non-remappable keys
// (Esc, Tab, Ctrl, Shift, Alt, CapsLock, Return, Backspace, Delete, F1-F10).
// ---------------------------------------------------------------------------
static const uint8_t kRemapGroup1to9[] = {
    // Digit row: 1-0 (ST 2-11), minus (12), equals (13), backtick/~ (41)
    2,3,4,5,6,7,8,9,10,11,12,13,41
};
static const uint8_t kRemapGroupAtoZ[] = {
    // Q-row: Q-] (ST 16-27)
    16,17,18,19,20,21,22,23,24,25,26,27,
    // A-row: A-' (ST 30-40), backslash (43)
    30,31,32,33,34,35,36,37,38,39,40,43,
    // Z-row: ISO extra (96), Z-/ (ST 44-53), Space (57)
    96,44,45,46,47,48,49,50,51,52,53,57
};
static const uint8_t kRemapGroupSpec[] = {
    // Help(98), Undo(97), Insert(82), ClrHome(71),
    // Up(72), Down(80), Left(75), Right(77)
    98,97,82,71,72,80,75,77
};
static const uint8_t kRemapGroupPad[] = {
    // KP( (99), KP) (100), KP/ (101), KP* (102), KP- (74), KP+ (78), 
    // KP7-9 (103-105), KP4-6 (106-108), KP1-3 (109-111), KP0 (112), KP. (113)
    99,100,101,102,74,78,103,104,105,106,107,108,109,110,111,112,113
};


#define DEBOUNCE_COUNT  10   // consecutive low samples required before first fire
#define REPEAT_DELAY    25   // extra cycles after first fire before auto-repeat starts
#define REPEAT_RATE      8   // cycles between successive auto-repeat fires

// ---------------------------------------------------------------------------------------------------
// Deadzone and mouse debug periodic timer — fires every 50 ms to request a joystick or mouse HID poll
// and a UI redraw while the deadzone page or mouse debug page is active.
// Written from an IRQ context; read from the main loop. volatile is sufficient
// on the RP2040 Cortex-M0+ (no hardware reordering on a single core).
// ----------------------------------------------------------------------------------------------------
static repeating_timer_t dz_timer;
static volatile bool dz_dirty_requested        = false;
static volatile bool mouse_dbg_dirty_requested = false;

// Only requests a redraw of the live debug/deadzone pages. The HID poll is no
// longer triggered here: the 10 ms main loop already calls handle_mouse() and
// handle_joystick() every cycle, so an extra 50 ms poll was pure duplication.
static bool deadzone_timer_cb(repeating_timer_t* rt) {
    dz_dirty_requested        = true;
    mouse_dbg_dirty_requested = true;
    return true; // keep repeating
}

static int lang_idx = 0;

// ---------------------------------------------------------------------------
// Keyboard layout names (5-char format, fixed list — must match the order
// of KeyboardLayout enum and s_layout_map[] in HidInput_common.cpp).
// ---------------------------------------------------------------------------
static const char* kLayouts[] = {
    "CZ-CZ", // 0
    "CH-DE", // 1
	"CH-FR", // 2
    "DE-DE", // 3
    "DK-DK", // 4
    "EN-UK", // 5
    "EN-US", // 6
    "ES-ES", // 7
    "FI-FI", // 8
    "FR-FR", // 9
    "HU-HU", // 10
    "IT-IT", // 11
    "NL-NL", // 12
    "NO-NO", // 13
    "PL-PL", // 14
    "SE-SE", // 15
};
static const int NUM_LAYOUTS = sizeof(kLayouts) / sizeof(kLayouts[0]);

// ---------------------------------------------------------------------------
// Screen sleep timeout presets (seconds). 0 = sleep disabled ("OFF").
// LEFT/RIGHT on PAGE_SCREEN cycle through this table, wrapping at both ends.
// ---------------------------------------------------------------------------
static const uint16_t kSleepPresets[] = { 0, 5, 10, 15, 20, 30, 60, 120, 300 };
static const int SLEEP_PRESET_COUNT = sizeof(kSleepPresets) / sizeof(kSleepPresets[0]);

// Map a brightness step (0..BRIGHT_MAX) to an SSD1306 contrast value (0..255).
static inline uint8_t brightness_to_contrast(uint8_t step) {
    return (uint8_t)(((int)step * 255) / BRIGHT_MAX);
}

enum BUTTONS {
    BUTTON_LEFT,
    BUTTON_MIDDLE,
    BUTTON_RIGHT
};

UserInterface::UserInterface() {
}

ssd1306_t disp;

void UserInterface::init() {
    // Set up the I2C interface to the SSD1306 display.
    i2c_init(SSD1306_I2C, 400000);
    gpio_set_function(SSD1306_SDA, GPIO_FUNC_I2C);
    gpio_set_function(SSD1306_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SSD1306_SDA);
    gpio_pull_up(SSD1306_SCL);

    ssd1306_init(&disp, SSD1306_WIDTH, SSD1306_HEIGHT, SSD1306_ADDR, SSD1306_I2C);

    // Set up GPIO for the three UI buttons.
    btn_gpio[0] = GPIO_BUTTON_LEFT;
    btn_gpio[1] = GPIO_BUTTON_MIDDLE;
    btn_gpio[2] = GPIO_BUTTON_RIGHT;
    for (int i = 0; i < 3; ++i) {
        gpio_init(btn_gpio[i]);
        gpio_set_dir(btn_gpio[i], GPIO_IN);
        gpio_pull_up(btn_gpio[i]);
    }

    // Restore mouse speed from NV storage; clamp to valid range.
    int mouse_speed = settings.get_settings().mouse_speed;
    if (mouse_speed < MOUSE_MIN) {
        mouse_speed = MOUSE_MIN;
        settings.get_settings().mouse_speed = mouse_speed;
    }
    if (mouse_speed > MOUSE_MAX) {
        mouse_speed = MOUSE_MAX;
        settings.get_settings().mouse_speed = mouse_speed;
    }

    // Restore UI language from NV storage.
    lang_idx = settings.get_settings().language_index;

    // Restore keyboard layout from NV storage; default to index 0 if out of range.
    auto& st = settings.get_settings();
    if (st.keyboard_layout_index >= NUM_LAYOUTS) {
        st.keyboard_layout_index = 0;
        // Boot-time migration: write immediately (core 1 not launched yet).
        settings.write();
    }
    HidInput::instance().set_layout_from_index(st.keyboard_layout_index);

    // Initialise remap table: if key_remap[] was never populated (all zeros after
    // flash migration), copy the active layout table now and persist.
    if (st.key_remap[4] == 0) {
        const uint8_t* tbl = HidInput::get_layout_table(st.keyboard_layout_index);
        memcpy(st.key_remap, tbl, 128);
        // Boot-time migration: write immediately (core 1 not launched yet).
        settings.write();
    }
    HidInput::instance().set_remap_table(st.key_remap);

    // Restore joystick dead zone from NV storage; clamp to valid range.
    uint8_t joystick_dead_zone = settings.get_settings().joystick_dead_zone;
    if (joystick_dead_zone == 0xFF || joystick_dead_zone < JOY_DZ_MIN) {
        joystick_dead_zone = JOY_DZ_MIN;
        settings.get_settings().joystick_dead_zone = joystick_dead_zone;
    }
    if (joystick_dead_zone > JOY_DZ_MAX) {
        joystick_dead_zone = JOY_DZ_MAX;
        settings.get_settings().joystick_dead_zone = joystick_dead_zone;
    }

    // Restore screen settings from NV storage; clamp out-of-range values
    // (0xFF after a flash migration) to safe defaults, then apply brightness.
    if (st.screen_sleep_idx >= SLEEP_PRESET_COUNT) st.screen_sleep_idx = 0; // OFF
    if (st.screen_brightness > BRIGHT_MAX) st.screen_brightness = BRIGHT_DEFAULT;
    ssd1306_contrast(&disp, brightness_to_contrast(st.screen_brightness));

    last_activity_tm = get_absolute_time();

    serial_tm  = get_absolute_time();
    splash_tm  = get_absolute_time();
    splash_done = false;

    // Cross-core serial log queue. Must be initialised before core 1 starts
    // (init() is called from main() before multicore_launch_core1()).
    // 64 entries is ample: at 7812 baud both directions combined produce at
    // most ~16 bytes per 10 ms main-loop poll.
    queue_init(&serial_log_q, sizeof(uint16_t), 64);

    // Start the 50 ms periodic timer used to refresh the deadzone and mouse debug page.
    add_repeating_timer_ms(50, deadzone_timer_cb, this, &dz_timer);

    dirty = true;
}

void UserInterface::usb_connect_state(int kb, int mouse, int joy) {
    if ((num_kb != kb) || (num_mouse != mouse) || (num_joy != joy)) {
        dirty = true;
    }
    num_kb    = kb;
    num_mouse = mouse;
    num_joy   = joy;
}

int8_t  UserInterface::get_mouse_speed()   { return settings.get_settings().mouse_speed;         }
uint8_t UserInterface::get_mouse_enabled() { return settings.get_settings().mouse_enabled;       }
uint8_t UserInterface::get_joystick()      { return settings.get_settings().joy_device;          }
uint8_t UserInterface::get_dead_zone()     { return settings.get_settings().joystick_dead_zone;  }

uint8_t UserInterface::get_autofire_mode(int joy) {
    return (joy == 0) ? settings.get_settings().autofire_mode_joy0
                      : settings.get_settings().autofire_mode_joy1;
}

uint8_t UserInterface::get_autofire_rate(int joy) {
    uint8_t r = (joy == 0) ? settings.get_settings().autofire_rate_joy0
                           : settings.get_settings().autofire_rate_joy1;
    if (r < AUTOFIRE_MIN) r = AUTOFIRE_MIN;
    if (r > AUTOFIRE_MAX) r = AUTOFIRE_MAX;
    return r;
}

void UserInterface::set_mouse_enabled(uint8_t en) {
    settings.get_settings().mouse_enabled = en;
    schedule_settings_write();
    dirty = true;
}

// ---------------------------------------------------------------------------
// update_serial
// Renders the raw serial byte log.  Refreshed at most every 500 ms.
// ---------------------------------------------------------------------------
void UserInterface::update_serial() {
    uint8_t y = 0;
    ssd1306_clear(&disp);
    for (const auto& it : serial_lines) {
        ssd1306_draw_string(&disp, 0, y, 1, (char*)it.c_str());
        y += 9;
    }
    ssd1306_draw_string(&disp, 29, 27, 1, (char*)"ST<->Kbd");
}

// ---------------------------------------------------------------------------
// update_status
// Renders the shared status block (rows 0-36): connected device counts,
// mouse/joy mode, and current CPU frequency.
// Used as the upper half of PAGE_MOUSE and PAGE_JOY.
// ---------------------------------------------------------------------------
void UserInterface::update_status() {
    char buf[32];

    uint32_t cpu_freq = clock_get_hz(clk_sys);

    ssd1306_clear(&disp);
    sprintf(buf, "%s %d", get_translation(KEY_USB_KEYBOARD, lang_idx), num_kb);
    ssd1306_draw_utf8_string(&disp, 0,  0, 1, buf);
    sprintf(buf, "%s %d", get_translation(KEY_USB_MOUSE, lang_idx), num_mouse);
    ssd1306_draw_utf8_string(&disp, 0,  9, 1, buf);
    sprintf(buf, "%s %d", get_translation(KEY_USB_JOYSTICK, lang_idx), num_joy);
    ssd1306_draw_utf8_string(&disp, 0, 18, 1, buf);
    sprintf(buf, "%s", get_translation(
        settings.get_settings().mouse_enabled ? KEY_MOUSE_ENABLED : KEY_JOY0_ENABLED,
        lang_idx));
    ssd1306_draw_utf8_string(&disp, 0, 27, 1, buf);
    sprintf(buf, "CPU: %.2f MHz", static_cast<double>(cpu_freq) / 1000000.0);
    ssd1306_draw_utf8_string(&disp, 0, 36, 1, buf);
}

void UserInterface::set_cpu_speed(uint32_t khz) {
    // Raise core voltage before overclocking: the default 1.10 V is marginal
    // above ~250 MHz. Restore it when dropping back to the safe clock.
    if (khz > DEFAULT_CPU_CLOCK_KHZ) {
        vreg_set_voltage(VREG_VOLTAGE_1_20);
        sleep_ms(2);  // let the regulator settle before raising the clock
    }

    set_sys_clock_khz(khz, false);

    // clk_sys clocks the I2C block, so SCL scales with the CPU frequency.
    // Re-program the SSD1306 baud rate to keep it at 400 kHz (was drifting to
    // ~480 kHz at 150 MHz and ~864 kHz at 270 MHz, out of the panel's spec).
    i2c_set_baudrate(SSD1306_I2C, 400000);

    if (khz <= DEFAULT_CPU_CLOCK_KHZ) {
        vreg_set_voltage(VREG_VOLTAGE_1_10);
    }

    dirty = true;
}

// ---------------------------------------------------------------------------
// update_mouse
// Renders the mouse speed slider on rows 45-54 (below the status block).
// FIX (bug 1): cursor index is clamped to prevent writing beyond the slider.
// ---------------------------------------------------------------------------
void UserInterface::update_mouse() {
    char buf[32];
    ssd1306_draw_utf8_string(&disp, 0, 45, 1, get_translation(KEY_MOUSE_SPEED, lang_idx));
    sprintf(buf, "\x80==============\x82"); // 0x80=left-end, 0x82=right-end, 0x81=cursor
    int cursor = settings.get_settings().mouse_speed - MOUSE_MIN;
    if (cursor < 0)                 cursor = 0;
    if (cursor >= MOUSE_SLIDER_LEN) cursor = MOUSE_SLIDER_LEN;
    buf[cursor] = (char)0x81;
    ssd1306_draw_string(&disp, 0, 54, 1, buf);
}

// ---------------------------------------------------------------------------
// update_joy
// Renders one joystick row on PAGE_JOY (below the status block).
//   index    : 0 or 1
//   selected : currently highlighted joystick (0 or 1).
//              Draws the UI_CURSOR_GLYPH in front of the selected row,
//              and a space in front of the other.
// ---------------------------------------------------------------------------
void UserInterface::update_joy(int index, int selected) {
    char buf[32];
    uint8_t y = (index == 0) ? 45 : 54;

    char cursor_buf[2] = { (selected == index) ? UI_CURSOR_GLYPH : ' ', 0 };
    ssd1306_draw_string(&disp, 0, y, 1, cursor_buf);

    sprintf(buf, "Joy %d: %s", index,
        (settings.get_settings().joy_device & (1 << index)) ? "D-Sub" : "USB");
    ssd1306_draw_utf8_string(&disp, 12, y, 1, buf);
}

// ---------------------------------------------------------------------------
// update_menu1
// Renders PAGE_MENU_1 (main navigation menu).
// Entries: Back | Settings | Help | Debug
// The UI_CURSOR_GLYPH marks the currently selected entry (menu1_selected).
// Left / Right buttons cycle through entries; Middle button activates.
// ---------------------------------------------------------------------------
void UserInterface::update_menu1() {
    const char* entries[MENU1_COUNT] = {
        get_translation(KEY_BACK,     lang_idx),
		get_translation(KEY_AUTOFIRE, lang_idx),
        get_translation(KEY_SETTINGS, lang_idx),
        get_translation(KEY_HELP,     lang_idx),
        get_translation(KEY_DEBUG,    lang_idx)
    };
    ssd1306_clear(&disp);
    for (int i = 0; i < MENU1_COUNT; ++i) {
        char cur[2] = { (menu1_selected == i) ? UI_CURSOR_GLYPH : ' ', 0 };
        ssd1306_draw_string(&disp, 0,  i * MENU1_LINE_H, 1, cur);
        ssd1306_draw_utf8_string(&disp, 10, i * MENU1_LINE_H, 1, (char*)entries[i]);
    }
}

// ---------------------------------------------------------------------------
// update_menu2
// Renders PAGE_MENU_2 (settings sub-menu).
// Entries: Back | Language | Kbd Layout | Deadzone | Remapping | Screen
// The UI_CURSOR_GLYPH marks the currently selected entry (menu2_selected).
// Left / Right buttons cycle through entries; Middle button activates.
// ---------------------------------------------------------------------------
void UserInterface::update_menu2() {
    const char* entries[MENU2_COUNT] = {
        get_translation(KEY_BACK, lang_idx),
        get_translation(KEY_LANGUAGE, lang_idx),
        get_translation(KEY_LAYOUT, lang_idx),
        get_translation(KEY_DEAD_ZONE, lang_idx),
        get_translation(KEY_REMAP, lang_idx),
        get_translation(KEY_SCREEN, lang_idx)
    };
    ssd1306_clear(&disp);
    for (int i = 0; i < MENU2_COUNT; ++i) {
        char cur[2] = { (menu2_selected == i) ? UI_CURSOR_GLYPH : ' ', 0 };
        ssd1306_draw_string(&disp, 0,  i * MENU2_LINE_H, 1, cur);
        ssd1306_draw_utf8_string(&disp, 10, i * MENU2_LINE_H, 1, (char*)entries[i]);
    }
}

void UserInterface::update_splash() {
    ssd1306_clear(&disp);
    ssd1306_draw_string_inverse(&disp, 0,  0, 2, (char*)"  EIFFEL   ");
    ssd1306_draw_string(&disp, 30, 25, 1, (char*)"Pico - USB");
    ssd1306_draw_string(&disp, 40, 36, 1, (char*)"Adapter");
    ssd1306_draw_string(&disp, 44, 55, 1, (char*)"v" PROJECT_VERSION_STRING);
}

void UserInterface::show_usb_debug_page() {
    page  = PAGE_USB_DEBUG;
    dirty = true;
}

void UserInterface::update_usb_debug() {
    char buf[32];
    ssd1306_clear(&disp);

    sprintf(buf, "Kb:%d Ms:%d Joy:%d", num_kb, num_mouse, num_joy);
    ssd1306_draw_string(&disp, 0,  0, 1, buf);

    uint32_t addr_inst = hid_debug_get_last_addr_inst();
    sprintf(buf, "Mounts:%lu Act:%lu",
        hid_debug_get_mount_calls(),
        hid_debug_get_active_devices());
    ssd1306_draw_string(&disp, 0, 10, 1, buf);

    sprintf(buf, "Last: Ad:%d In:%d",
        (addr_inst >> 8) & 0xFF,
        addr_inst & 0xFF);
    ssd1306_draw_string(&disp, 0, 20, 1, buf);

    sprintf(buf, "Rx:%lu Cp:%lu",
        hid_debug_get_report_calls(),
        hid_debug_get_report_copied());
    ssd1306_draw_string(&disp, 0, 30, 1, buf);

    // Last descriptor length and detected device type.
    // Helps diagnose receivers whose descriptor exceeds the parsing limit
    // (e.g. Logitech Unifying, which can exceed 512 bytes).
    uint16_t  dlen = hid_debug_get_last_desc_len();
    HID_TYPE  dtype = hid_debug_get_last_hid_type();
    const char* type_str = (dtype == HID_KEYBOARD) ? "KB"    :
                           (dtype == HID_MOUSE)     ? "MOUSE" :
                           (dtype == HID_JOYSTICK)  ? "JOY"   : "UNKN";
    sprintf(buf, "Desc:%u %s", (unsigned)dlen, type_str);
    ssd1306_draw_string(&disp, 0, 40, 1, buf);

    sprintf(buf, "Unmounts:%lu", hid_debug_get_unmount_calls());
    ssd1306_draw_string(&disp, 0, 50, 1, buf);
}

// ---------------------------------------------------------------------------
// update_language
// Standalone language selection page: label at the top, current value below.
// Left / Right changes the language; Middle returns to PAGE_MENU_2.
// ---------------------------------------------------------------------------
void UserInterface::update_language() {
    ssd1306_draw_utf8_string_inverse(&disp, 0,  0, 1, get_translation(KEY_LANGUAGE, lang_idx));
    ssd1306_draw_string     (&disp, 0, 14, 2, (char*)languages[lang_idx]);
}

// ---------------------------------------------------------------------------
// update_layout
// Standalone keyboard layout selection page: label at the top, value below.
// Left / Right changes the layout; Middle returns to PAGE_MENU_2.
// ---------------------------------------------------------------------------
void UserInterface::update_layout() {
    const char* layout = kLayouts[settings.get_settings().keyboard_layout_index % NUM_LAYOUTS];
    ssd1306_draw_utf8_string_inverse(&disp, 0,  0, 1, get_translation(KEY_LAYOUT, lang_idx));
    ssd1306_draw_utf8_string(&disp, 0, 14, 2, (char*)layout);
}

void UserInterface::update_help_1() {
    ssd1306_clear(&disp);
    ssd1306_draw_string     (&disp, 0,  0, 1, (char*)"Ctrl + F12:");
    ssd1306_draw_utf8_string(&disp, 0, 10, 1, get_translation(KEY_HELP_TOGGLE_MOUSE, lang_idx));
    ssd1306_draw_string     (&disp, 0, 20, 1, (char*)"Ctrl + F11:");
    ssd1306_draw_utf8_string(&disp, 0, 30, 1, get_translation(KEY_HELP_RESET, lang_idx));
    // FIX (bug 7): Ctrl+F10 toggles Joy1 (not Joy0). Label corrected.
    ssd1306_draw_string     (&disp, 0, 40, 1, (char*)"Ctrl + F10:");
    ssd1306_draw_string     (&disp, 0, 50, 1, (char*)"Joy1 D-sub<->USB");
}

void UserInterface::update_help_2() {
    ssd1306_clear(&disp);
    ssd1306_draw_string     (&disp, 0,  0, 1, (char*)"Ctrl + F9:");
    ssd1306_draw_string     (&disp, 0, 10, 1, (char*)"Joy0 D-sub<->USB");
    ssd1306_draw_string     (&disp, 0, 20, 1, (char*)"Alt + NumPad '+':");
    ssd1306_draw_utf8_string(&disp, 0, 30, 1, get_translation(KEY_HELP_SET_270, lang_idx));
    ssd1306_draw_string     (&disp, 0, 40, 1, (char*)"Alt + NumPad '-':");
    ssd1306_draw_utf8_string(&disp, 0, 50, 1, get_translation(KEY_HELP_SET_150, lang_idx));
}

// ---------------------------------------------------------------------------
// update_screen
// PAGE_SCREEN layout (128 x 64 px, scale 1):
//   y= 0  Title "Screen" (inverse)
//   y=16  [cur] Sleep timeout ("OFF" or seconds)
//   y=36  [cur] Brightness label
//   y=48        Brightness slider (8 steps)
//
// screen_selected: 0 = sleep timeout, 1 = brightness
// MIDDLE advances cursor; on last item exits to PAGE_MENU_2.
// LEFT/RIGHT: cycle sleep presets (wraps), or decrease/increase brightness.
// ---------------------------------------------------------------------------
void UserInterface::update_screen() {
    char buf[32];

    ssd1306_draw_utf8_string_inverse(&disp, 0, 0, 1, get_translation(KEY_SCREEN, lang_idx));

    auto& st = settings.get_settings();

    // --- Sleep timeout line ---
    char cur[2] = { (screen_selected == 0) ? UI_CURSOR_GLYPH : ' ', 0 };
    ssd1306_draw_string(&disp, 0, 16, 1, cur);
    uint16_t secs = kSleepPresets[st.screen_sleep_idx];
    if (secs == 0) {
        sprintf(buf, "%s: OFF", get_translation(KEY_SLEEP, lang_idx));
    } else {
        sprintf(buf, "%s: %us", get_translation(KEY_SLEEP, lang_idx), (unsigned)secs);
    }
    ssd1306_draw_utf8_string(&disp, 10, 16, 1, buf);

    // --- Brightness line + slider ---
    cur[0] = (screen_selected == 1) ? UI_CURSOR_GLYPH : ' ';
    ssd1306_draw_string(&disp, 0, 36, 1, cur);
    ssd1306_draw_utf8_string(&disp, 10, 36, 1, get_translation(KEY_BRIGHTNESS, lang_idx));

    sprintf(buf, "\x80======\x82"); // 0x80=left-end, 0x82=right-end, 0x81=cursor
    int cursor = st.screen_brightness;
    if (cursor < BRIGHT_MIN) cursor = BRIGHT_MIN;
    if (cursor > BRIGHT_MAX) cursor = BRIGHT_MAX;
    buf[cursor] = (char)0x81;
    ssd1306_draw_string(&disp, 10, 48, 1, buf);
}

void UserInterface::update_deadzone() {
    char buf[32];
    ssd1306_draw_utf8_string_inverse(&disp, 0, 0, 1, get_translation(KEY_DEAD_ZONE, lang_idx));

    {
        char dz_str[32];
        uint8_t dz = settings.get_settings().joystick_dead_zone;
        sprintf(dz_str, "%u", dz);
        ssd1306_draw_utf8_string(&disp, 0, 20, 2, dz_str);
    }

    // Clamp cursor so it never overwrites the 0x82 end-marker
    // when the dead zone is at maximum.
    sprintf(buf, "\x80==============\x82"); // 0x80=left-end, 0x82=right-end, 0x81=cursor
    int cursor = (int)settings.get_settings().joystick_dead_zone - JOY_DZ_MIN;
    if (cursor < 0)                  cursor = 0;
    if (cursor >= JOY_DZ_MAX)  cursor = JOY_DZ_MAX;
    buf[cursor] = (char)0x81;
    ssd1306_draw_string(&disp, 0, 54, 1, buf);

    // Always display Joy1 state — it is the primary Atari ST port.
    // handle_joystick() writes joystick_state bits 4-7 and mouse_state bit 0
    // for Joy1 regardless of port mode (D-Sub, USB HID, PS4, Xbox).
    uint8_t js   = HidInput::instance().joystick();
    uint8_t axis = (js >> 4) & 0x0F;  // Joy1 = bits 4-7

    uint8_t mb   = HidInput::instance().mouse_buttons();
    bool    fire = (mb & 0x01) != 0;  // bit 0 = Joy1 fire / right mouse button

    // Direction glyphs (custom font: up=0x83, down=0x84, left=0x85, right=0x86, fire=0x87)
    const int x_center = 100;
    const int y_top    =   8;
    const int y_mid    =  22;
    const int y_bot    =  36;

    char glyph[2] = {0, 0};

    glyph[0] = (axis & 0x01) ? (char)0x83 : ' '; // UP
    ssd1306_draw_string(&disp, x_center,      y_top, 2, glyph);

    glyph[0] = (axis & 0x04) ? (char)0x85 : ' '; // LEFT
    ssd1306_draw_string(&disp, x_center - 12, y_mid, 2, glyph);

    glyph[0] = (axis & 0x08) ? (char)0x86 : ' '; // RIGHT
    ssd1306_draw_string(&disp, x_center + 12, y_mid, 2, glyph);

    glyph[0] = (axis & 0x02) ? (char)0x84 : ' '; // DOWN
    ssd1306_draw_string(&disp, x_center,      y_bot, 2, glyph);

    glyph[0] = fire ? (char)0x87 : ' ';          // FIRE
    ssd1306_draw_string(&disp, x_center,      y_mid, 2, glyph);
}

// ---------------------------------------------------------------------------
// update_autofire
// PAGE_AUTOFIRE layout (128 × 64 px, scale 1):
//   y= 0  Title "Autofire"
//   y= 9  [cur] Joy1 mode  (OFF / STBY / ON!)
//   y=18  [cur] Joy1 rate  (1..15 Hz)
//   y=27        Joy1 rate slider
//   y=36  [cur] Joy0 mode
//   y=45  [cur] Joy0 rate
//   y=54        Joy0 rate slider
//
// autofire_selected: 0=J0 mode, 1=J0 rate, 2=J1 mode, 3=J1 rate
// MIDDLE advances cursor; on last item exits to PAGE_MENU_1.
// LEFT/RIGHT: toggle mode (0=OFF<->1=STBY), or decrease/increase rate.
// ---------------------------------------------------------------------------
void UserInterface::update_autofire() {
    char buf[32];

    ssd1306_draw_string_inverse(&disp, 0, 0, 1, get_translation(KEY_AUTOFIRE, lang_idx));

	for (int d = 0; d < 2; ++d) {
		int joy = 1 - d;               // d=0 → Joy1 (top), d=1 → Joy0 (bottom)
		int y_mode   = (d == 0) ?  9 : 36;
		int y_rate   = (d == 0) ? 18 : 45;
		int y_slider = (d == 0) ? 27 : 54;
		int item_mode = d * 2;         // 0 or 2  (display order)
		int item_rate = d * 2 + 1;     // 1 or 3  (display order)

        // --- Mode line ---
        uint8_t mode = (joy == 0) ? settings.get_settings().autofire_mode_joy0
                                  : settings.get_settings().autofire_mode_joy1;
        bool active = HidInput::instance().is_autofire_active(joy);
        const char* mode_str = (mode == 0) ? "OFF " : (active ? " ON!" : "STAND BY");

        char cur[2] = { (autofire_selected == item_mode) ? UI_CURSOR_GLYPH : ' ', 0 };
        ssd1306_draw_string(&disp, 0, y_mode, 1, cur);
        sprintf(buf, "Joy%d: %s", joy, mode_str);
        ssd1306_draw_string(&disp, 10, y_mode, 1, buf);

        // --- Rate line ---
        uint8_t rate = (joy == 0) ? settings.get_settings().autofire_rate_joy0
                                  : settings.get_settings().autofire_rate_joy1;

        cur[0] = (autofire_selected == item_rate) ? UI_CURSOR_GLYPH : ' ';
        ssd1306_draw_string(&disp, 0, y_rate, 1, cur);
        sprintf(buf, "Joy%d:%3dHz", joy, (int)rate);
        ssd1306_draw_string(&disp, 10, y_rate, 1, buf);

        // --- Rate slider ---
        sprintf(buf, "\x80=============\x82");
        int cursor = (int)rate - AUTOFIRE_MIN;   // 0..14
        if (cursor < 0)                  cursor = 0;
        if (cursor >= AUTOFIRE_MAX)  cursor = AUTOFIRE_MAX;
        buf[cursor] = (char)0x81;
        ssd1306_draw_string(&disp, 10, y_slider, 1, buf);
    }
}

// ---------------------------------------------------------------------------
// update_remap_group
// PAGE_REMAP_GROUP: menu with 6 entries.
//   0=Back  1=1-9  2=A-Z  3=Spec.  4=Pad  5=Clear all
// ---------------------------------------------------------------------------
void UserInterface::update_remap_group() {
    ssd1306_clear(&disp);

    if (remap_clear_confirm) {
        ssd1306_draw_utf8_string_inverse(&disp,  0,  4, 1, get_translation(KEY_CLEAR_CONFIRM, lang_idx));
        ssd1306_draw_utf8_string        (&disp,  0, 24, 1, get_translation(KEY_OK_CONFIRM, lang_idx));
        ssd1306_draw_utf8_string        (&disp,  0, 36, 1, get_translation(KEY_LR_CANCEL, lang_idx));
        return;
    }

    const char* entries[REMAP_GROUP_COUNT] = {
        get_translation(KEY_BACK, lang_idx),
        " 1-9",
        " A-Z",
        " Special",
        " Key Pad",
        get_translation(KEY_REMAP_CLEAR, lang_idx)
    };
    for (int i = 0; i < REMAP_GROUP_COUNT; ++i) {
        char cur[2] = { (remap_group_selected == i) ? UI_CURSOR_GLYPH : ' ', 0 };
        ssd1306_draw_string    (&disp,  0, i * REMAP_GROUP_LINE_H, 1, cur);
        ssd1306_draw_utf8_string(&disp, 10, i * REMAP_GROUP_LINE_H, 1, (char*)entries[i]);
    }
}

// ---------------------------------------------------------------------------
// update_remap_list
// PAGE_REMAP_LIST: shows the ST scancode for the selected group entry (left)
// and the USB key currently mapped to it (right), as key-cap widgets.
// The user presses a USB key to set a new mapping, then MIDDLE to confirm.
// ---------------------------------------------------------------------------
void UserInterface::update_remap_list() {
    if (!remap_group_ptr || remap_group_size == 0) return;

    ssd1306_clear(&disp);

    if (remap_exit_confirm) {
        ssd1306_draw_utf8_string_inverse(&disp,  0,  4, 1, get_translation(KEY_QUIT_REMAPPING, lang_idx));
        ssd1306_draw_utf8_string        (&disp,  0, 24, 1, get_translation(KEY_OK_CONFIRM, lang_idx));
        ssd1306_draw_utf8_string        (&disp,  0, 36, 1, get_translation(KEY_LR_CANCEL, lang_idx));
        return;
    }

    // Title bar: group name and counter
    char title[22];
    const char* grp_names[] = { "", "1-9", "A-Z", "Special", "Key Pad", "" };
    int grp_idx = (remap_group_selected >= 1 && remap_group_selected <= 4) ? remap_group_selected : 0;
    ssd1306_draw_string(&disp, 0, 0, 1, grp_names[grp_idx]);

    // Vertical separator
    ssd1306_draw_square(&disp, 63, 9, 1, 55);

    // Centre each label in its half: advance per char at scale 1 = 5 (font_8x5 width) + CHAR_KERNING;
    // visual width = len*(5+CHAR_KERNING)-CHAR_KERNING; left half = 63 px, right half = 64 px at x=64.
    int st_w  = (int)strlen("ST")  * (5 + CHAR_KERNING) - CHAR_KERNING;
    int usb_w = (int)strlen("USB") * (5 + CHAR_KERNING) - CHAR_KERNING;
    ssd1306_draw_string(&disp, (63 - st_w)  / 2,      10, 1, (char*)"ST");
    ssd1306_draw_string(&disp, 64 + (64 - usb_w) / 2, 10, 1, (char*)"USB");

    int layout_idx = settings.get_settings().keyboard_layout_index;
    uint8_t st_sc = remap_group_ptr[remap_list_index];

    // ISO-only key on an ANSI layout: show "N/A" and skip the remap widgets.
    if (sc_is_iso_key(st_sc) && !layout_has_iso_key(layout_idx)) {
        ssd1306_draw_string(&disp, 20, 28, 1, (char*)"N/A (ANSI kbd)");
        char hint[16];
        sprintf(hint, "%d/%d", remap_list_index + 1, remap_group_size);
        ssd1306_draw_string(&disp, 0, 55, 1, hint);
        return;
    }

    const char* st_label = get_layout_st_label(st_sc, layout_idx);
    if (!st_label) st_label = st_scancode_name(st_sc);

    // Find the USB (HID) key currently mapped to this ST scancode
    const uint8_t* remap = settings.get_settings().key_remap;
    uint8_t cur_hid = 0;
    for (int h = 1; h < 128; ++h) {
        if (remap[h] == st_sc) { cur_hid = h; break; }
    }

    // Show pending key if set (latched after release), otherwise current saved mapping.
    // Uses the USB-native HID label table, independent of the ST scancode system.
    // "N/A" when no HID key produces this ST scancode (orphaned after a reassignment).
    const char* usb_label;
    if (pending_remap_hid != 0) {
        usb_label = get_hid_layout_label(pending_remap_hid, layout_idx);
    } else {
        usb_label = (cur_hid != 0) ? get_hid_layout_label(cur_hid, layout_idx) : "N/A";
    }

    // Draw key-cap widgets: ST on left (x=16,y=22), USB on right (x=80,y=22)
    ssd1306_draw_utf8_key(&disp, 16, 22, st_label);
    ssd1306_draw_utf8_key(&disp, 80, 22, usb_label);

    // Bottom counter hint
    char hint[16];
    sprintf(hint, "%d/%d", remap_list_index + 1, remap_group_size);
    ssd1306_draw_string(&disp, 0, 55, 1, hint);
}

// ---------------------------------------------------------------------------
// update_mouse_debug
// Screen layout (128 x 64):
//
//   [0..62]  Left half  — two header lines, then USB key cap (ssd1306_draw_key).
//            KEY_SIZE = 30 px; centered in the 63 px left area: x = 16, y = 20.
//            Drawn only while a key is held; cleared by ssd1306_clear otherwise.
//
//   [63]     1-px vertical separator.
//
//   [64..127] Right half — mouse movement arrows (custom glyphs, scale 2)
//             and L / R button indicators (scale 2, bottom row, latched for
//             at least BTN_HOLD_FRAMES × 50 ms to absorb HID polling gaps).
//
// mouse_debug_dx/dy are cleared after each frame so arrows flash on movement
// impulse; button indicators latch until the button is confirmed released.
// ---------------------------------------------------------------------------
void UserInterface::update_mouse_debug() {
    ssd1306_clear(&disp);

    // --- Header lines --------------------------------------------------------
    ssd1306_draw_string_inverse(&disp,  0,  0, 1, (char*)"Kb test");
    ssd1306_draw_string(&disp,  0, 10, 1, (char*)"(en-US)");
    ssd1306_draw_string_inverse(&disp, 68,  0, 1, (char*)"Mse test");

    // --- Left half: pressed USB key cap --------------------------------------
    // Label is supplied by HidInput::handle_keyboard() via last_key_label.
    // Empty string means no key is currently held.
    // KEY_SIZE = 30; centered in 63 px left area: x = (63-30)/2 = 16.
    // y = 20: leaves a clean gap after the two header lines (rows 0-17).
    const char* label = HidInput::instance().get_last_key_label();
    if (label && label[0] != '\0') {
        ssd1306_draw_key(&disp, 16, 24, label);
    }

    // --- Vertical separator --------------------------------------------------
    ssd1306_draw_square(&disp, 63, 0, 1, 64);

    // --- Right half: mouse direction arrows ----------------------------------
    // Custom glyphs: 0x83=up  0x84=down  0x85=left  0x86=right  (scale 2)
    const int xc = 90;
    const int yt = 10, ym = 26, yb = 42;
    char g[2] = {0, 0};

    g[0] = (mouse_debug_dy < 0) ? (char)0x83 : ' '; // up
    ssd1306_draw_string(&disp, xc,      yt, 2, g);

    g[0] = (mouse_debug_dx < 0) ? (char)0x85 : ' '; // left
    ssd1306_draw_string(&disp, xc - 16, ym, 2, g);

    g[0] = (mouse_debug_dx > 0) ? (char)0x86 : ' '; // right
    ssd1306_draw_string(&disp, xc + 16, ym, 2, g);

    g[0] = (mouse_debug_dy > 0) ? (char)0x84 : ' '; // down
    ssd1306_draw_string(&disp, xc,      yb, 2, g);

    // --- Button indicators (L / R) -------------------------------------------

        int buttons = HidInput::instance().usb_mouse_buttons_raw();
		if (buttons & 0x02) { ssd1306_draw_string(&disp, xc, ym, 2, (char*)"L"); }
        if (buttons & 0x01) { ssd1306_draw_string(&disp, xc, ym, 2, (char*)"R"); }

    // Clear deltas so arrows flash on impulse rather than latching.
    mouse_debug_dx = 0;
    mouse_debug_dy = 0;
}

void UserInterface::set_mouse_debug_delta(int dx, int dy) {
    if (dx != 0) mouse_debug_dx = dx;
    if (dy != 0) mouse_debug_dy = dy;
}

// Apply pending_remap_hid to key_remap[] and schedule a deferred flash write.
// Safe to call when pending_remap_hid == 0 (no-op). Must be called while
// remap_list_index still points to the entry being confirmed.
void UserInterface::commit_pending_remap() {
    if (pending_remap_hid == 0) return;
    uint8_t  st_sc = remap_group_ptr[remap_list_index];
    uint8_t* remap = settings.get_settings().key_remap;
    if (remap[pending_remap_hid] != st_sc) {
        remap[pending_remap_hid] = st_sc;
        schedule_settings_write();
        HidInput::instance().set_remap_table(remap);
    }
    pending_remap_hid = 0;
}

void UserInterface::handle_buttons() {
    for (int i = 0; i < 3; ++i) {
        bool state = gpio_get(btn_gpio[i]);
        if (!state) {
            ++btn_count[i];
            if (btn_count[i] == DEBOUNCE_COUNT) {
                // Initial fire on debounce threshold.
                on_button_down(i);
            } else if (btn_count[i] > DEBOUNCE_COUNT + REPEAT_DELAY) {
                // Auto-repeat: fire every REPEAT_RATE cycles while held.
                int phase = (btn_count[i] - DEBOUNCE_COUNT - REPEAT_DELAY) % REPEAT_RATE;
                if (phase == 0) {
                    on_button_down(i);
                }
            }
        } else {
            btn_count[i] = 0;
        }
    }
}

void UserInterface::toggle_joystick_source(uint8_t joystick_num) {
    if (joystick_num > 1) return;
    settings.get_settings().joy_device ^= (1 << joystick_num);
    schedule_settings_write();
    dirty = true;
}

// ---------------------------------------------------------------------------
// on_button_down
// Implements the full navigation state machine.
//
// Navigation overview
// -------------------
//   SPLASH  ──(auto 3s)──► MOUSE
//   MOUSE   ──(mid)──► JOY
//   JOY     ──(mid, joy0)──► joy_selected=1
//           ──(mid, joy1)──► MENU_1
//   MENU_1  ──(left/right)── cycle entries
//           ──(mid, Back)──► MOUSE
//           ──(mid, Autofire)──► AUTOFIRE
//           ──(mid, Settings)──► MENU_2
//           ──(mid, Help)──► HELP_1
//           ──(mid, Debug)──► SERIAL
//   MENU_2  ──(left/right)── cycle entries
//           ──(mid, Back)──► MENU_1
//           ──(mid, Language)──► LANGUAGE
//           ──(mid, Kbd Layout)──► LAYOUT
//           ──(mid, Deadzone)──► DEADZONE
//           ──(mid, Screen)──► SCREEN
//   LANGUAGE, LAYOUT, DEADZONE ──(mid)──► MENU_2
//   SCREEN  ──(mid)── cycle items; on last item ──► MENU_2
//   MENU_2  ──(mid, Remapping)──► REMAP_GROUP
//   REMAP_GROUP ──(mid, Back)──► MENU_2
//           ──(mid, group)──► REMAP_LIST (capture mode ON)
//           ──(mid, Clear)── reload layout, stay
//   REMAP_LIST ──(mid, pending)── save mapping, stay
//              ──(mid, no pending)── show quit-confirm overlay
//              ──(mid, overlay shown)── capture mode OFF, ──► REMAP_GROUP
//              ──(left/right, overlay shown)── dismiss overlay, stay
//   AUTOFIRE ──(mid)──► MENU_1
//   HELP_1  ──(mid)──► HELP_2
//   HELP_2  ──(mid)──► MENU_1
//   MOUSE_DEBUG  ──(mid)──► SERIAL
//   SERIAL  ──(mid)──► USB_DEBUG
//   USB_DEBUG ──(mid)──► MENU_1
// ---------------------------------------------------------------------------
void UserInterface::on_button_down(int i) {

    // While the screen is asleep, any button only wakes it up: the waking
    // press is consumed so the user never navigates or edits blind.
    if (screen_asleep) {
        screen_asleep = false;
        ssd1306_poweron(&disp);
        last_activity_tm = get_absolute_time();
        dirty = true;
        return;
    }
    last_activity_tm = get_absolute_time();

    if (i == BUTTON_MIDDLE) {
        switch (page) {
		
			case PAGE_SPLASH:
				page = PAGE_MOUSE;
				dirty = true;
				break;

            case PAGE_MOUSE:
                // Middle from the mouse page enters the joystick page.
                joy_selected = 0;
                page  = PAGE_JOY;
                dirty = true;
                break;

            case PAGE_JOY:
                if (joy_selected == 0) {
                    // First press: move selection from Joy0 to Joy1.
                    joy_selected = 1;
                    dirty = true;
                } else {
                    // Second press: leave the page and open the main menu.
                    joy_selected = 0;
                    page  = PAGE_MENU_1;
                    dirty = true;
                }
                break;

            case PAGE_MENU_1:
                switch (menu1_selected) {
                    case 0: page = PAGE_MOUSE;       break;  // Back
                    case 1:                                  // Autofire
                        autofire_selected = 0;
                        page = PAGE_AUTOFIRE;
                        break;
					case 2: page = PAGE_MENU_2;      break;  // Settings
                    case 3: page = PAGE_HELP_1;      break;  // Help
                    case 4: page = PAGE_MOUSE_DEBUG; break;  // Debug
                }
                dirty = true;
                break;

            case PAGE_MENU_2:
                switch (menu2_selected) {
                    case 0: page = PAGE_MENU_1;   break;  // Back
                    case 1: page = PAGE_LANGUAGE; break;  // Language
                    case 2: page = PAGE_LAYOUT;   break;  // Keyboard layout
                    case 3: page = PAGE_DEADZONE; break;  // Joystick deadzone
                    case 4:                                // Key remapping
                        remap_group_selected = 0;
                        page = PAGE_REMAP_GROUP;
                        break;
                    case 5:                                // Screen settings
                        screen_selected = 0;
                        page = PAGE_SCREEN;
                        break;
                }
                dirty = true;
                break;

            case PAGE_LANGUAGE:
                page  = PAGE_MENU_2;
                dirty = true;
                break;

            case PAGE_LAYOUT:
                page  = PAGE_MENU_2;
                dirty = true;
                break;

            case PAGE_DEADZONE:
                page  = PAGE_MENU_2;
                dirty = true;
                break;

            case PAGE_SCREEN:
                // MIDDLE advances the cursor through items; on the last item exits.
                if (screen_selected == 0) {
                    screen_selected = 1;
                } else {
                    screen_selected = 0;
                    page = PAGE_MENU_2;
                }
                dirty = true;
                break;

            case PAGE_REMAP_GROUP:
                if (remap_clear_confirm) {
                    // Second MIDDLE on confirm overlay: perform the reset.
                    remap_clear_confirm = false;
                    auto& st = settings.get_settings();
                    memcpy(st.key_remap,
                           HidInput::get_layout_table(st.keyboard_layout_index), 128);
                    schedule_settings_write();
                    HidInput::instance().set_remap_table(st.key_remap);
                    dirty = true;
                    break;
                }
                switch (remap_group_selected) {
                    case 0:  // Back
                        page = PAGE_MENU_2;
                        break;
                    case 1:  // 1-9
                        remap_group_ptr  = kRemapGroup1to9;
                        remap_group_size = (int)(sizeof(kRemapGroup1to9));
                        remap_list_index = 0;
                        pending_remap_hid = 0;
                        HidInput::instance().set_capture_mode(true);
                        page = PAGE_REMAP_LIST;
                        break;
                    case 2:  // A-Z
                        remap_group_ptr  = kRemapGroupAtoZ;
                        remap_group_size = (int)(sizeof(kRemapGroupAtoZ));
                        remap_list_index = 0;
                        pending_remap_hid = 0;
                        HidInput::instance().set_capture_mode(true);
                        page = PAGE_REMAP_LIST;
                        break;
                    case 3:  // Spec.
                        remap_group_ptr  = kRemapGroupSpec;
                        remap_group_size = (int)(sizeof(kRemapGroupSpec));
                        remap_list_index = 0;
                        pending_remap_hid = 0;
                        HidInput::instance().set_capture_mode(true);
                        page = PAGE_REMAP_LIST;
                        break;
                    case 4:  // Pad
                        remap_group_ptr  = kRemapGroupPad;
                        remap_group_size = (int)(sizeof(kRemapGroupPad));
                        remap_list_index = 0;
                        pending_remap_hid = 0;
                        HidInput::instance().set_capture_mode(true);
                        page = PAGE_REMAP_LIST;
                        break;
                    case 5:  // Clear all — ask for confirmation first
                        remap_clear_confirm = true;
                        break;
                    default: break;
                }
                dirty = true;
                break;

            case PAGE_REMAP_LIST: {
                if (remap_exit_confirm) {
                    // Second MIDDLE on confirm overlay: exit to group.
                    remap_exit_confirm = false;
                    pending_remap_hid  = 0;
                    HidInput::instance().set_capture_mode(false);
                    page = PAGE_REMAP_GROUP;
                } else if (pending_remap_hid != 0) {
                    commit_pending_remap();
                    // Stay on PAGE_REMAP_LIST — capture mode remains active.
                } else {
                    // Nothing pending: show quit-confirm overlay.
                    remap_exit_confirm = true;
                }
                dirty = true;
                break;
            }

            case PAGE_AUTOFIRE:
                // MIDDLE advances the cursor through items; on the last item exits.
                if (autofire_selected < 3) {
                    ++autofire_selected;
                } else {
                    autofire_selected = 0;
                    page = PAGE_MENU_1;
                }
                dirty = true;
                break;

            case PAGE_HELP_1:
                page  = PAGE_HELP_2;
                dirty = true;
                break;

            case PAGE_HELP_2:
                page  = PAGE_MENU_1;
                dirty = true;
                break;

            case PAGE_MOUSE_DEBUG:
                page  = PAGE_SERIAL;
                dirty = true;
                break;

            case PAGE_SERIAL:
                page  = PAGE_USB_DEBUG;
                dirty = true;
                break;

            case PAGE_USB_DEBUG:
                page  = PAGE_MENU_1;
                dirty = true;
                break;

            default:
                break;
        }
    }
    else if (i == BUTTON_LEFT) {
        switch (page) {

            case PAGE_MOUSE:
                if (settings.get_settings().mouse_speed > MOUSE_MIN) {
                    --settings.get_settings().mouse_speed;
                    schedule_settings_write();
                    dirty = true;
                }
                break;

            case PAGE_JOY:
                // Toggle D-Sub <-> USB for the currently selected joystick.
                settings.get_settings().joy_device ^= (1 << joy_selected);
                schedule_settings_write();
                dirty = true;
                break;

            case PAGE_MENU_1:
                menu1_selected = (menu1_selected - 1 + MENU1_COUNT) % MENU1_COUNT;
                dirty = true;
                break;

            case PAGE_AUTOFIRE: {
                auto& st = settings.get_settings();
                if (autofire_selected == 0) {
                    // Joy1 mode: toggle OFF <-> STANDBY
                    st.autofire_mode_joy1 = st.autofire_mode_joy1 ? 0 : 1;
                    schedule_settings_write();
                    dirty = true;
                } else if (autofire_selected == 1) {
                    // Joy1 rate: LEFT = decrease
                    if (st.autofire_rate_joy1 > AUTOFIRE_MIN) {
                        --st.autofire_rate_joy1;
                        schedule_settings_write();
                        dirty = true;
                    }
                } else if (autofire_selected == 2) {
                    // Joy0 mode: toggle OFF <-> STANDBY
                    st.autofire_mode_joy0 = st.autofire_mode_joy0 ? 0 : 1;
                    schedule_settings_write();
                    dirty = true;
                } else {
                    // Joy0 rate: LEFT = decrease
                    if (st.autofire_rate_joy0 > AUTOFIRE_MIN) {
                        --st.autofire_rate_joy0;
                        schedule_settings_write();
                        dirty = true;
                    }
                }
                break;
            }

            case PAGE_MENU_2:
                // Cycle backwards through menu entries (wraps around).
                menu2_selected = (menu2_selected - 1 + MENU2_COUNT) % MENU2_COUNT;
                dirty = true;
                break;

            case PAGE_REMAP_GROUP:
                if (remap_clear_confirm) {
                    remap_clear_confirm = false; // cancel, stay on page
                    dirty = true;
                    break;
                }
                remap_group_selected =
                    (remap_group_selected - 1 + REMAP_GROUP_COUNT) % REMAP_GROUP_COUNT;
                dirty = true;
                break;

            case PAGE_REMAP_LIST:
                if (remap_exit_confirm) {
                    remap_exit_confirm = false; // cancel exit confirm, stay on page
                    dirty = true;
                    break;
                }
                commit_pending_remap();
                if (remap_group_size > 0) {
                    int layout_idx = settings.get_settings().keyboard_layout_index;
                    int steps = remap_group_size; // safety: at most one full wrap
                    do {
                        remap_list_index =
                            (remap_list_index - 1 + remap_group_size) % remap_group_size;
                        --steps;
                    } while (steps > 0 &&
                             sc_is_iso_key(remap_group_ptr[remap_list_index]) &&
                             !layout_has_iso_key(layout_idx));
                }
                pending_remap_hid = 0;
                dirty = true;
                break;

            case PAGE_LANGUAGE:
                lang_idx = (lang_idx - 1 + NUM_LANGUAGES) % NUM_LANGUAGES;
                settings.get_settings().language_index = lang_idx;
                schedule_settings_write();
                dirty = true;
                break;

            case PAGE_LAYOUT: {
                auto& st = settings.get_settings();
                // FIX (bug 5): cast to int before subtracting to avoid uint8_t underflow.
                int idx = (int)st.keyboard_layout_index - 1;
                if (idx < 0) idx = NUM_LAYOUTS - 1;
                st.keyboard_layout_index = (uint8_t)idx;
                memcpy(st.key_remap, HidInput::get_layout_table(st.keyboard_layout_index), 128);
                schedule_settings_write();
                HidInput::instance().set_layout_from_index(st.keyboard_layout_index);
                HidInput::instance().set_remap_table(st.key_remap);
                dirty = true;
                break;
            }

            case PAGE_DEADZONE:
                if (settings.get_settings().joystick_dead_zone > JOY_DZ_MIN) {
                    --settings.get_settings().joystick_dead_zone;
                    schedule_settings_write();
                    dirty = true;
                }
                break;

            case PAGE_SCREEN: {
                auto& st = settings.get_settings();
                if (screen_selected == 0) {
                    // Sleep timeout: cycle presets backwards, wrap OFF <- 300s.
                    st.screen_sleep_idx =
                        (st.screen_sleep_idx + SLEEP_PRESET_COUNT - 1) % SLEEP_PRESET_COUNT;
                    schedule_settings_write();
                    dirty = true;
                } else if (st.screen_brightness > BRIGHT_MIN) {
                    // Brightness: LEFT = decrease, applied live.
                    --st.screen_brightness;
                    ssd1306_contrast(&disp, brightness_to_contrast(st.screen_brightness));
                    schedule_settings_write();
                    dirty = true;
                }
                break;
            }

            default:
                break;
        }
    }
    else if (i == BUTTON_RIGHT) {
        switch (page) {

            case PAGE_MOUSE:
                if (settings.get_settings().mouse_speed < MOUSE_MAX) {
                    ++settings.get_settings().mouse_speed;
                    schedule_settings_write();
                    dirty = true;
                }
                break;

            case PAGE_JOY:
                // Toggle D-Sub <-> USB for the currently selected joystick.
                settings.get_settings().joy_device ^= (1 << joy_selected);
                schedule_settings_write();
                dirty = true;
                break;

            case PAGE_MENU_1:
                menu1_selected = (menu1_selected + 1) % MENU1_COUNT;
                dirty = true;
                break;

            case PAGE_AUTOFIRE: {
                auto& st = settings.get_settings();
                if (autofire_selected == 0) {
                    // Joy1 mode: toggle OFF <-> STANDBY
                    st.autofire_mode_joy1 = st.autofire_mode_joy1 ? 0 : 1;
                    schedule_settings_write();
                    dirty = true;
                } else if (autofire_selected == 1) {
                    // Joy1 rate: RIGHT = increase
                    if (st.autofire_rate_joy1 < AUTOFIRE_MAX) {
                        ++st.autofire_rate_joy1;
                        schedule_settings_write();
                        dirty = true;
                    }
                } else if (autofire_selected == 2) {
                    // Joy0 mode: toggle OFF <-> STANDBY
                    st.autofire_mode_joy0 = st.autofire_mode_joy0 ? 0 : 1;
                    schedule_settings_write();
                    dirty = true;
                } else {
                    // Joy0 rate: RIGHT = increase
                    if (st.autofire_rate_joy0 < AUTOFIRE_MAX) {
                        ++st.autofire_rate_joy0;
                        schedule_settings_write();
                        dirty = true;
                    }
                }
                break;
            }

            case PAGE_MENU_2:
                // Cycle forwards through menu entries (wraps around).
                menu2_selected = (menu2_selected + 1) % MENU2_COUNT;
                dirty = true;
                break;

            case PAGE_REMAP_GROUP:
                if (remap_clear_confirm) {
                    remap_clear_confirm = false; // cancel, stay on page
                    dirty = true;
                    break;
                }
                remap_group_selected =
                    (remap_group_selected + 1) % REMAP_GROUP_COUNT;
                dirty = true;
                break;

            case PAGE_REMAP_LIST:
                if (remap_exit_confirm) {
                    remap_exit_confirm = false; // cancel exit confirm, stay on page
                    dirty = true;
                    break;
                }
                commit_pending_remap();
                if (remap_group_size > 0) {
                    int layout_idx = settings.get_settings().keyboard_layout_index;
                    int steps = remap_group_size; // safety: at most one full wrap
                    do {
                        remap_list_index =
                            (remap_list_index + 1) % remap_group_size;
                        --steps;
                    } while (steps > 0 &&
                             sc_is_iso_key(remap_group_ptr[remap_list_index]) &&
                             !layout_has_iso_key(layout_idx));
                }
                pending_remap_hid = 0;
                dirty = true;
                break;

            case PAGE_LANGUAGE:
                lang_idx = (lang_idx + 1) % NUM_LANGUAGES;
                settings.get_settings().language_index = lang_idx;
                schedule_settings_write();
                dirty = true;
                break;

            case PAGE_LAYOUT: {
                auto& st = settings.get_settings();
                int idx = ((int)st.keyboard_layout_index + 1) % NUM_LAYOUTS;
                st.keyboard_layout_index = (uint8_t)idx;
                memcpy(st.key_remap, HidInput::get_layout_table(st.keyboard_layout_index), 128);
                schedule_settings_write();
                HidInput::instance().set_layout_from_index(st.keyboard_layout_index);
                HidInput::instance().set_remap_table(st.key_remap);
                dirty = true;
                break;
            }

            case PAGE_DEADZONE:
                if (settings.get_settings().joystick_dead_zone < JOY_DZ_MAX) {
                    ++settings.get_settings().joystick_dead_zone;
                    schedule_settings_write();
                    dirty = true;
                }
                break;

            case PAGE_SCREEN: {
                auto& st = settings.get_settings();
                if (screen_selected == 0) {
                    // Sleep timeout: cycle presets forwards, wrap 300s -> OFF.
                    st.screen_sleep_idx =
                        (st.screen_sleep_idx + 1) % SLEEP_PRESET_COUNT;
                    schedule_settings_write();
                    dirty = true;
                } else if (st.screen_brightness < BRIGHT_MAX) {
                    // Brightness: RIGHT = increase, applied live.
                    ++st.screen_brightness;
                    ssd1306_contrast(&disp, brightness_to_contrast(st.screen_brightness));
                    schedule_settings_write();
                    dirty = true;
                }
                break;
            }

            default:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// update
// Main rendering dispatcher. Called every iteration of the main loop.
// Only redraws when dirty == true, except for time-gated pages (SERIAL,
// USB_DEBUG, SPLASH) which manage their own refresh cadence.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Deferred NV settings write.
// Each slider step / toggle only marks the settings dirty; the flash write
// (sector erase + program, ~50 ms with interrupts off and core 1 parked)
// happens once, after no change occurred for SETTINGS_WRITE_DELAY_MS.
// ---------------------------------------------------------------------------
#define SETTINGS_WRITE_DELAY_MS 1500

void UserInterface::schedule_settings_write() {
    settings_dirty    = true;
    settings_dirty_tm = get_absolute_time();
}

void UserInterface::update() {
    handle_buttons();

    // Format pending serial log events on core 0, outside the emulation path.
    drain_serial_log();

    // Flush deferred settings once the last change has settled.
    if (settings_dirty &&
        absolute_time_diff_us(settings_dirty_tm, get_absolute_time()) >=
            (int64_t)SETTINGS_WRITE_DELAY_MS * 1000) {
        settings_dirty = false;
        settings.write();
    }

    // Screen sleep: power the panel off after the configured inactivity
    // timeout (button presses only). Never sleeps on the splash screen.
    // While asleep, skip all rendering to save I2C bandwidth; wake-up is
    // handled at the top of on_button_down().
    if (!screen_asleep) {
        uint16_t sleep_s = kSleepPresets[settings.get_settings().screen_sleep_idx];
        if (sleep_s > 0 && page != PAGE_SPLASH &&
            absolute_time_diff_us(last_activity_tm, get_absolute_time()) >=
                (int64_t)sleep_s * 1000000) {
            screen_asleep = true;
            ssd1306_poweroff(&disp);
        }
    }
    if (screen_asleep) return;

    // Mouse and joystick are polled by the 10 ms main loop, not here: the
    // periodic timer only requests redraws (below) for the live debug pages.

    if (dz_dirty_requested) {
        dz_dirty_requested = false;
        if (page == PAGE_DEADZONE || page == PAGE_AUTOFIRE) dirty = true;
    }
    if (mouse_dbg_dirty_requested) {
        mouse_dbg_dirty_requested = false;
        if (page == PAGE_MOUSE_DEBUG) dirty = true;
    }

    // On PAGE_REMAP_LIST, trigger a redraw when the captured USB key changes.
    // This avoids forcing dirty every loop cycle while still reacting immediately
    // to a key press. remap_list_index changes are already handled by on_button_down().
    if (page == PAGE_REMAP_LIST) {
        uint8_t cap = HidInput::instance().get_captured_hid_keycode();
        if (cap != last_captured_hid) {
            last_captured_hid = cap;
            // Latch after release; reject system-reserved keys.
            if (cap != 0 && !hid_is_system_reserved(cap)) pending_remap_hid = cap;
            dirty = true;
        }
    } else {
        last_captured_hid = 0xFF; // reset sentinel when leaving the page
    }

	if (!dirty) return;
	dirty = false;

    switch (page) {

        case PAGE_MOUSE:
            update_status();
            update_mouse();
            break;

        case PAGE_JOY:
            update_status();
            update_joy(0, joy_selected);
            update_joy(1, joy_selected);
            break;

        case PAGE_MENU_1:
            update_menu1();
            break;

        case PAGE_MENU_2:
            update_menu2();
            break;

        case PAGE_LANGUAGE:
            ssd1306_clear(&disp);
            update_language();
            break;

        case PAGE_LAYOUT:
            ssd1306_clear(&disp);
            update_layout();
            break;

        case PAGE_DEADZONE:
            ssd1306_clear(&disp);
            update_deadzone();
            break;

        case PAGE_SCREEN:
            ssd1306_clear(&disp);
            update_screen();
            break;

        case PAGE_AUTOFIRE:
            ssd1306_clear(&disp);
            update_autofire();
            break;

        case PAGE_REMAP_GROUP:
            update_remap_group();
            break;

        case PAGE_REMAP_LIST:
            update_remap_list();
            break;

        case PAGE_HELP_1:
            update_help_1();
            break;

        case PAGE_HELP_2:
            update_help_2();
            break;

        case PAGE_MOUSE_DEBUG: {
			update_mouse_debug();
            break;
        }

        case PAGE_SERIAL: {
            // Refresh at most every 500 ms to avoid flooding the display bus.
            absolute_time_t tm = get_absolute_time();
            if (absolute_time_diff_us(serial_tm, tm) >= (500 * 1000)) {
                serial_tm = tm;
                update_serial();
            } else {
				dirty = true;
                return;
            }
            break;
        }

        case PAGE_SPLASH: {
            absolute_time_t tm = get_absolute_time();
            if (absolute_time_diff_us(splash_tm, tm) >= (3 * 1000 * 1000)) {
                splash_done = true;
                page  = PAGE_MOUSE;
                update_status();
                update_mouse();
            } else {
                update_splash();
                ssd1306_show(&disp);
                return;
            }
            break;
        }

        case PAGE_USB_DEBUG: {
            // Refresh at most every 100 ms.
            static absolute_time_t usb_debug_tm = get_absolute_time();
            absolute_time_t tm = get_absolute_time();
            if (absolute_time_diff_us(usb_debug_tm, tm) >= (100 * 1000)) {
                usb_debug_tm = tm;
                update_usb_debug();
            } else {
                dirty = true;
                return;
            }
            break;
        }

        default:
            break;
    }

    ssd1306_show(&disp);
}

void UserInterface::serial(bool send, uint8_t data) {
    // Called from core 1 (HD6301 TX via tdr_putb -> serial_send) and from
    // core 0 (UART RX). No formatting, no heap allocation, no member writes
    // here: just push the raw event. Dropped silently when the queue is full
    // (this is a debug log; losing entries is preferable to blocking the
    // emulation hot path).
    uint16_t event = (uint16_t)data | (send ? 0x100u : 0u);
    queue_try_add(&serial_log_q, &event);
}

void UserInterface::drain_serial_log() {
    uint16_t event;
    bool got_event = false;
    while (queue_try_remove(&serial_log_q, &event)) {
        char buf[32];
        sprintf(buf, "%s%02X", (event & 0x100) ? "              " : "",
                (unsigned)(event & 0xFF));
        serial_lines.push_back(std::string(buf));
        while (serial_lines.size() > 7) {
            serial_lines.pop_front();
        }
        got_event = true;
    }
    if (got_event && page == PAGE_SERIAL) {
        dirty = true;
    }
}
