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

#include <stdint.h>
#include <hardware/flash.h>

struct Settings {
    // Version - used to detect if this is the first time we have read from flash.
    // Should be 1.
    uint8_t     version;

    // The mouse speed ranges from -7 to +8 with 0 being the standard speed
    int8_t      mouse_speed;

    // 1 = mouse enabled, 0 = joystick 0 enabled
    uint8_t     mouse_enabled;

    // Bitfield where 1 = DSub joystick, 0 = USB.
    // Bit0 = Joystick 0
    // Bit1 = Joystick 1
    uint8_t     joy_device;
	
	// Language index: 0 = EN, 1 = FR, 2 = DE, 3 = SP, 4 = IT
    uint8_t     language_index;
	
	// Keyboard layout index (UI selection persisted in NV)
	uint8_t     keyboard_layout_index;
	
	// Joystick dead zone value
	uint8_t joystick_dead_zone;

	// Autofire mode per joystick: 0 = OFF, 1 = STANDBY
	uint8_t autofire_mode_joy0;
	uint8_t autofire_mode_joy1;

	// Autofire rate per joystick: 1..AUTOFIRE_MAX Hz
	uint8_t autofire_rate_joy0;
	uint8_t autofire_rate_joy1;

	// USB HID -> Atari ST scancode remap table.
	// Indexed by HID keycode (0-127). Value = ST scancode to produce.
	// Initialised from the active layout table on first boot after firmware update.
	// Reset to the active layout when the user changes layout or selects "Clear all".
	uint8_t key_remap[128];
};

class NVSettings {
public:
    NVSettings();
    Settings& get_settings();
    void write();
    void read();

};

