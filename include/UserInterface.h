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

#define MOUSE_MIN -7
#define MOUSE_MAX 8

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

        // Help pages (reached from PAGE_MENU_1)
        PAGE_HELP_1,
        PAGE_HELP_2,

        // Debug pages (reached from PAGE_MENU_1)
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
     * Force a jump to the USB debug page (e.g. via Ctrl+F8).
     */
    void show_usb_debug_page();

private:
    void update_serial();
    void update_status();
    void update_mouse();
    void update_deadzone();
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
    // Currently highlighted entry on PAGE_MENU_1 (0 = Back, 1 = Settings, 2 = Help, 3 = Debug)
    int         menu1_selected = 0;
    // Currently highlighted entry on PAGE_MENU_2 (0 = Back, 1 = Language, 2 = Kbd Layout, 3 = Deadzone)
    int         menu2_selected = 0;
};
