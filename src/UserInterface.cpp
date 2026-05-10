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
#include "HidInput.h"
#include "ssd1306_key.h"

#define DEBOUNCE_COUNT 10

// ---------------------------------------------------------------------------------------------------
// Deadzone and mouse debug periodic timer — fires every 50 ms to request a joystick or mouse HID poll
// and a UI redraw while the deadzone page or mouse debug page is active.
// Written from an IRQ context; read from the main loop. volatile is sufficient
// on the RP2040 Cortex-M0+ (no hardware reordering on a single core).
// ----------------------------------------------------------------------------------------------------
static repeating_timer_t dz_timer;
static volatile bool dz_poll_needed            = false;
static volatile bool dz_dirty_requested        = false;
static volatile bool mouse_dbg_poll_needed     = false;
static volatile bool mouse_dbg_dirty_requested = false;

static bool deadzone_timer_cb(repeating_timer_t* rt) {
    dz_poll_needed            = true;
    dz_dirty_requested        = true;
    mouse_dbg_poll_needed     = true;
    mouse_dbg_dirty_requested = true;
    return true; // keep repeating
}

static int lang_idx = 0;

// ---------------------------------------------------------------------------
// Keyboard layout names (5-char format, fixed list — must match the order
// of KeyboardLayout enum and s_layout_map[] in HidInput_common.cpp).
// ---------------------------------------------------------------------------
static const char* kLayouts[] = {
    "CZ-CZ",
    "DE-CH",
    "DE-DE",
    "DK-DK",
    "EN-UK",
    "EN-US",
    "ES-ES",
    "FI-FI",
    "FR-CH",
    "FR-FR",
    "HU-HU",
    "IT-IT",
    "NL-NL",
    "NO-NO",
    "PL-PL",
    "SE-SE",
};
static const int NUM_LAYOUTS = sizeof(kLayouts) / sizeof(kLayouts[0]);

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
        settings.write();
    }
    HidInput::instance().set_layout_from_index(st.keyboard_layout_index);

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

    serial_tm  = get_absolute_time();
    splash_tm  = get_absolute_time();
    splash_done = false;

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

int8_t  UserInterface::get_mouse_speed()   { return settings.get_settings().mouse_speed;          }
uint8_t UserInterface::get_mouse_enabled() { return settings.get_settings().mouse_enabled;         }
uint8_t UserInterface::get_joystick()      { return settings.get_settings().joy_device;            }
uint8_t UserInterface::get_dead_zone()     { return settings.get_settings().joystick_dead_zone;    }

void UserInterface::set_mouse_enabled(uint8_t en) {
    settings.get_settings().mouse_enabled = en;
    settings.write();
    dirty = true;
}

// ---------------------------------------------------------------------------
// update_serial
// Renders the raw serial byte log.  Refreshed at most every 500 ms.
// ---------------------------------------------------------------------------
void UserInterface::update_serial() {
    uint8_t y = 0;
    ssd1306_clear(&disp);
    for (auto it : serial_lines) {
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
    set_sys_clock_khz(khz, false);
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
        get_translation(KEY_BACK, lang_idx),
        get_translation(KEY_SETTINGS, lang_idx),
        get_translation(KEY_HELP, lang_idx),
        get_translation(KEY_DEBUG, lang_idx)
    };
    ssd1306_clear(&disp);
    for (int i = 0; i < MENU1_COUNT; ++i) {
        char cur[2] = { (menu1_selected == i) ? UI_CURSOR_GLYPH : ' ', 0 };
        ssd1306_draw_string(&disp, 0,  i * MENU_LINE_H, 1, cur);
        ssd1306_draw_utf8_string(&disp, 10, i * MENU_LINE_H, 1, (char*)entries[i]);
    }
}

// ---------------------------------------------------------------------------
// update_menu2
// Renders PAGE_MENU_2 (settings sub-menu).
// Entries: Back | Language | Kbd Layout | Deadzone
// The UI_CURSOR_GLYPH marks the currently selected entry (menu2_selected).
// Left / Right buttons cycle through entries; Middle button activates.
// ---------------------------------------------------------------------------
void UserInterface::update_menu2() {
    const char* entries[MENU2_COUNT] = {
        get_translation(KEY_BACK, lang_idx),
        get_translation(KEY_LANGUAGE, lang_idx),
        get_translation(KEY_LAYOUT, lang_idx),
        get_translation(KEY_DEAD_ZONE, lang_idx)
    };
    ssd1306_clear(&disp);
    for (int i = 0; i < MENU2_COUNT; ++i) {
        char cur[2] = { (menu2_selected == i) ? UI_CURSOR_GLYPH : ' ', 0 };
        ssd1306_draw_string(&disp, 0,  i * MENU_LINE_H, 1, cur);
        ssd1306_draw_utf8_string(&disp, 10, i * MENU_LINE_H, 1, (char*)entries[i]);
    }
}

void UserInterface::update_splash() {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 29,  0, 2, (char*)"EIFFEL");
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
    ssd1306_draw_utf8_string(&disp, 0,  0, 1, get_translation(KEY_LANGUAGE, lang_idx));
    ssd1306_draw_string     (&disp, 0, 14, 2, (char*)languages[lang_idx]);
}

// ---------------------------------------------------------------------------
// update_layout
// Standalone keyboard layout selection page: label at the top, value below.
// Left / Right changes the layout; Middle returns to PAGE_MENU_2.
// ---------------------------------------------------------------------------
void UserInterface::update_layout() {
    const char* layout = kLayouts[settings.get_settings().keyboard_layout_index % NUM_LAYOUTS];
    ssd1306_draw_utf8_string(&disp, 0,  0, 1, get_translation(KEY_LAYOUT, lang_idx));
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
    // FIX (bug 7): Ctrl+F9 toggles Joy0 (not Joy1). Label corrected.
    ssd1306_draw_string     (&disp, 0,  0, 1, (char*)"Ctrl + F9:");
    ssd1306_draw_string     (&disp, 0, 10, 1, (char*)"Joy0 D-sub<->USB");
    ssd1306_draw_string     (&disp, 0, 20, 1, (char*)"Alt + NumPad '+':");
    ssd1306_draw_utf8_string(&disp, 0, 30, 1, get_translation(KEY_HELP_SET_270, lang_idx));
    ssd1306_draw_string     (&disp, 0, 40, 1, (char*)"Alt + NumPad '-':");
    ssd1306_draw_utf8_string(&disp, 0, 50, 1, get_translation(KEY_HELP_SET_150, lang_idx));
}

void UserInterface::update_deadzone() {
    char buf[32];
    ssd1306_draw_utf8_string(&disp, 0, 0, 1, get_translation(KEY_DEAD_ZONE, lang_idx));

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
    ssd1306_draw_string(&disp,  0,  0, 1, (char*)"Kb test");
    ssd1306_draw_string(&disp,  0, 10, 1, (char*)"(en-US)");
    ssd1306_draw_string(&disp, 68,  0, 1, (char*)"Mse test");

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

void UserInterface::handle_buttons() {
    for (int i = 0; i < 3; ++i) {
        bool state = gpio_get(btn_gpio[i]);
        if (!state) {
            // Latch at DEBOUNCE_COUNT until the button is released.
            if (btn_count[i] <= DEBOUNCE_COUNT) {
                if (++btn_count[i] == DEBOUNCE_COUNT) {
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
    settings.write();
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
//           ──(mid, Settings)──► MENU_2
//           ──(mid, Help)──► HELP_1
//           ──(mid, Debug)──► SERIAL
//   MENU_2  ──(left/right)── cycle entries
//           ──(mid, Back)──► MENU_1
//           ──(mid, Language)──► LANGUAGE
//           ──(mid, Kbd Layout)──► LAYOUT
//           ──(mid, Deadzone)──► DEADZONE
//   LANGUAGE, LAYOUT, DEADZONE ──(mid)──► MENU_2
//   HELP_1  ──(mid)──► HELP_2
//   HELP_2  ──(mid)──► MENU_1
//   MOUSE_DEBUG  ──(mid)──► SERIAL
//   SERIAL  ──(mid)──► USB_DEBUG
//   USB_DEBUG ──(mid)──► MENU_1
// ---------------------------------------------------------------------------
void UserInterface::on_button_down(int i) {

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
                    case 0: page = PAGE_MOUSE;  break;  // Back
                    case 1: page = PAGE_MENU_2; break;  // Settings
                    case 2: page = PAGE_HELP_1; break;  // Help
                    case 3: page = PAGE_MOUSE_DEBUG; break;  // Debug
                }
                dirty = true;
                break;

            case PAGE_MENU_2:
                switch (menu2_selected) {
                    case 0: page = PAGE_MENU_1;  break;  // Back
                    case 1: page = PAGE_LANGUAGE; break; // Language
                    case 2: page = PAGE_LAYOUT;   break; // Keyboard layout
                    case 3: page = PAGE_DEADZONE; break; // Joystick deadzone
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
                    settings.write();
                    dirty = true;
                }
                break;

            case PAGE_JOY:
                // Toggle D-Sub <-> USB for the currently selected joystick.
                settings.get_settings().joy_device ^= (1 << joy_selected);
                settings.write();
                dirty = true;
                break;

            case PAGE_MENU_1:
                // Cycle backwards through menu entries (wraps around).
                menu1_selected = (menu1_selected - 1 + MENU1_COUNT) % MENU1_COUNT;
                dirty = true;
                break;

            case PAGE_MENU_2:
                // Cycle backwards through menu entries (wraps around).
                menu2_selected = (menu2_selected - 1 + MENU2_COUNT) % MENU2_COUNT;
                dirty = true;
                break;

            case PAGE_LANGUAGE:
                lang_idx = (lang_idx - 1 + NUM_LANGUAGES) % NUM_LANGUAGES;
                settings.get_settings().language_index = lang_idx;
                settings.write();
                dirty = true;
                break;

            case PAGE_LAYOUT: {
                auto& st = settings.get_settings();
                // FIX (bug 5): cast to int before subtracting to avoid uint8_t underflow.
                int idx = (int)st.keyboard_layout_index - 1;
                if (idx < 0) idx = NUM_LAYOUTS - 1;
                st.keyboard_layout_index = (uint8_t)idx;
                settings.write();
                HidInput::instance().set_layout_from_index(st.keyboard_layout_index);
                dirty = true;
                break;
            }

            case PAGE_DEADZONE:
                if (settings.get_settings().joystick_dead_zone > JOY_DZ_MIN) {
                    --settings.get_settings().joystick_dead_zone;
                    settings.write();
                    dirty = true;
                }
                break;

            default:
                break;
        }
    }
    else if (i == BUTTON_RIGHT) {
        switch (page) {

            case PAGE_MOUSE:
                if (settings.get_settings().mouse_speed < MOUSE_MAX) {
                    ++settings.get_settings().mouse_speed;
                    settings.write();
                    dirty = true;
                }
                break;

            case PAGE_JOY:
                // Toggle D-Sub <-> USB for the currently selected joystick.
                settings.get_settings().joy_device ^= (1 << joy_selected);
                settings.write();
                dirty = true;
                break;

            case PAGE_MENU_1:
                // Cycle forwards through menu entries (wraps around).
                menu1_selected = (menu1_selected + 1) % MENU1_COUNT;
                dirty = true;
                break;

            case PAGE_MENU_2:
                // Cycle forwards through menu entries (wraps around).
                menu2_selected = (menu2_selected + 1) % MENU2_COUNT;
                dirty = true;
                break;

            case PAGE_LANGUAGE:
                lang_idx = (lang_idx + 1) % NUM_LANGUAGES;
                settings.get_settings().language_index = lang_idx;
                settings.write();
                dirty = true;
                break;

            case PAGE_LAYOUT: {
                auto& st = settings.get_settings();
                // FIX (bug 5): cast to int to ensure modulo is applied on a
                // well-defined value even if the stored index is corrupted.
                int idx = ((int)st.keyboard_layout_index + 1) % NUM_LAYOUTS;
                st.keyboard_layout_index = (uint8_t)idx;
                settings.write();
                HidInput::instance().set_layout_from_index(st.keyboard_layout_index);
                dirty = true;
                break;
            }

            case PAGE_DEADZONE:
                if (settings.get_settings().joystick_dead_zone < JOY_DZ_MAX) {
                    ++settings.get_settings().joystick_dead_zone;
                    settings.write();
                    dirty = true;
                }
                break;

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
void UserInterface::update() {
       handle_buttons();

    // Handle mouse BEFORE joystick so that usb_mouse_buttons is up-to-date
    // when handle_joystick() seeds mouse_state from it.
    // Both flags are set by the same 50 ms timer callback, so both run every cycle.
    if (mouse_dbg_poll_needed) {
        mouse_dbg_poll_needed = false;
        HidInput::instance().handle_mouse(0);
    }
    if (dz_poll_needed) {
        dz_poll_needed = false;
        HidInput::instance().handle_joystick();
    }

    if (dz_dirty_requested) {
        dz_dirty_requested = false;
        if (page == PAGE_DEADZONE) dirty = true;
    }
    if (mouse_dbg_dirty_requested) {
        mouse_dbg_dirty_requested = false;
        if (page == PAGE_MOUSE_DEBUG) dirty = true;
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
                // FIX (bug 4): render the new page immediately so ssd1306_show()
                // is called at the end of this same update() call.
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
    char buf[32];
    sprintf(buf, "%s%02X", send ? "              " : "", data);
    serial_lines.push_back(std::string(buf));
    while (serial_lines.size() > 7) {
        serial_lines.pop_front();
    }
    if (page == PAGE_SERIAL) {
        dirty = true;
    }
}
