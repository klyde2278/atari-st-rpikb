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
#pragma once

#include "ssd1306.h"
#include "NVSettings.h"
#include <string>
#include <deque>

// Slider bar length in characters (must match the string literal in
// update_mouse / update_deadzone). Used as the upper bound for the cursor.
#define MOUSE_SLIDER_LEN   15   // "===============" between 0x80 and 0x82
#define MOUSE_MIN     -7
#define MOUSE_MAX      8
#define JOY_DZ_MIN     0
#define JOY_DZ_MAX    15
#define JOY_DZ_DEFAULT 8

// Autofire rate range (Hz). Stored directly as 1..AUTOFIRE_MAX.
// 15 steps match the 15-position slider exactly.
#define AUTOFIRE_MIN      1
#define AUTOFIRE_MAX     15
#define AUTOFIRE_DEFAULT  8

// Custom cursor glyph redefined in the font at address 0x88.
// Used in front of the currently selected item on any menu or list page.
#define UI_CURSOR_GLYPH  ((char)0x88)

// Number of entries in each menu (must stay in sync with the string arrays
// defined in update_menu1 / update_menu2).
#define MENU1_COUNT  5   // Back | Autofire | Settings   | Help    | Debug
#define MENU2_COUNT  4   // Back | Language | Kbd Layout | Deadzone

// Vertical spacing (pixels) between menu entries.
// MENU1 has 4 entries: 4 x 14 = 56 px < 64 px display height.
// MENU2 has 5 entries: 5 x 12 = 60 px < 64 px.
#define MENU1_LINE_H 12
#define MENU2_LINE_H 14

class UserInterface {
public:
    UserInterface();

    enum PAGE {
        PAGE_SPLASH,

        // Main operational pages
        PAGE_MOUSE,
        PAGE_JOY,

        // Top-level navigation menu
        PAGE_MENU_1,

        // Settings sub-menu
        PAGE_MENU_2,

        // Settings pages (reached from PAGE_MENU_2)
        PAGE_LANGUAGE,
        PAGE_LAYOUT,
        PAGE_DEADZONE,

        // Autofire settings page (reached from PAGE_MENU_1)
        PAGE_AUTOFIRE,

        // Help pages (reached from PAGE_MENU_1)
        PAGE_HELP_1,
        PAGE_HELP_2,

        // Debug pages (reached from PAGE_MENU_1)
        PAGE_MOUSE_DEBUG,
        PAGE_SERIAL,
        PAGE_USB_DEBUG,

        PAGE_MAX
    };

    void init();

    /**
     * Update the user interface with the current USB connection state.
     */
    void usb_connect_state(int kb, int mouse, int joy);

    /**
     * Get the user-specified mouse speed.
     * 0 = standard; negative = slower; positive = faster.
     */
    int8_t get_mouse_speed();

    /**
     * Get the joystick hardware assignment.
     * Bitfield: 1 = D-Sub joystick, 0 = USB.
     *   Bit 0 = Joystick 0
     *   Bit 1 = Joystick 1
     */
    uint8_t get_joystick();

    /**
     * Get the user-specified joystick dead zone.
     */
    uint8_t get_dead_zone();

    /**
     * Get the autofire mode for the given joystick (0 = OFF, 1 = STANDBY).
     * @param joy  0 or 1
     */
    uint8_t get_autofire_mode(int joy);

    /**
     * Get the autofire rate (Hz) for the given joystick (AUTOFIRE_MIN..AUTOFIRE_MAX).
     * @param joy  0 or 1
     */
    uint8_t get_autofire_rate(int joy);

    /**
     * Returns true if mouse is enabled, false if joystick 0 is enabled.
     */
    uint8_t get_mouse_enabled();
    void set_mouse_enabled(uint8_t en);

    /**
     * Toggle joystick source between D-SUB and USB.
     * @param joystick_num  0 or 1
     */
    void toggle_joystick_source(uint8_t joystick_num);

    /**
     * Switch to a different CPU clock speed.
     */
    void set_cpu_speed(uint32_t khz);

    /**
     * Update the display if necessary. Call this from the main loop.
     */
    void update();

    /**
     * Log a serial byte to the serial debug page.
     */
    void serial(bool send, uint8_t data);

    /**
     * Render the language selection page content.
     */
    void update_language();

    /**
     * Render the keyboard layout selection page content.
     */
    void update_layout();

    /**
     * Render help page 1.
     */
    void update_help_1();

    /**
     * Render help page 2.
     */
    void update_help_2();

    /**
     * Render the autofire settings page.
     */
    void update_autofire();

    /**
     * Force a jump to the USB debug page (e.g. via Ctrl+F8).
     */
    void show_usb_debug_page();

    /**
     * Accumulate mouse delta for the mouse debug page.
     * Called from handle_mouse() on every HID poll cycle.
     * Non-zero values are OR-latched so a fast movement is not missed between
     * two render frames.
     */
    void set_mouse_debug_delta(int dx, int dy);

private:
    void update_serial();
    void update_status();
    void update_mouse();
    void update_deadzone();
    void update_mouse_debug();
    // index = 0 or 1 ; selected = currently highlighted joystick on PAGE_JOY
    void update_joy(int index, int selected);
    void update_usb_debug();
    void update_splash();
    // Render PAGE_MENU_1 (main menu)
    void update_menu1();
    // Render PAGE_MENU_2 (settings sub-menu)
    void update_menu2();
    void handle_buttons();
    void on_button_down(int i);

private:
    PAGE        page = PAGE_SPLASH;
    NVSettings  settings;
    bool        dirty = true;
    int         num_kb = 0;
    int         num_mouse = 0;
    int         num_joy = 0;
    std::deque<std::string> serial_lines;
    absolute_time_t serial_tm;
    uint        btn_gpio[3];
    int         btn_count[3];
    absolute_time_t splash_tm;
    bool        splash_done = false;

    // Currently highlighted joystick on PAGE_JOY (0 = Joy0, 1 = Joy1)
    int         joy_selected   = 0;
    // Currently highlighted entry on PAGE_MENU_1 (0=Back,1=Settings,2=Help,3=Debug)
    int         menu1_selected = 0;
    // Currently highlighted entry on PAGE_MENU_2 (0=Back,1=Language,2=Kbd Layout,3=Deadzone,4=Autofire)
    int         menu2_selected = 0;
    // Currently selected item on PAGE_AUTOFIRE (0=J1 mode,1=J1 rate,2=J0 mode,3=J0 rate)
    int         autofire_selected = 0;

    // Mouse movement delta accumulated between two render frames (PAGE_MOUSE_DEBUG).
    // Set by set_mouse_debug_delta(), cleared after each call to update_mouse_debug().
    int         mouse_debug_dx = 0;
    int         mouse_debug_dy = 0;
};
