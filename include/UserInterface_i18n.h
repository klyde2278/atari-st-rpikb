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

extern const char* languages[];
extern const int NUM_LANGUAGES;

enum TranslationKey {
    KEY_USB_KEYBOARD,
    KEY_USB_MOUSE,
    KEY_USB_JOYSTICK,
    KEY_MOUSE_ENABLED,
    KEY_JOY0_ENABLED,
    KEY_MOUSE_SPEED,
    KEY_LANGUAGE,
	KEY_LAYOUT,
	KEY_HELP,
    KEY_HELP_TOGGLE_MOUSE,  // "Toggles between USB mouse and Atari mouse"
    KEY_HELP_RESET,         // "Resets the emulation"
    KEY_HELP_TOGGLE_JOY1,   // "Switches Joy1 D-sub <-> USB"
    KEY_HELP_TOGGLE_JOY0,   // "Switches Joy0 D-sub <-> USB"
    KEY_HELP_SET_270,       // "Set 270MHz"
    KEY_HELP_SET_150,		// "Set 150MHz"
	KEY_DEAD_ZONE,          // "Joystick dead zone"
	KEY_BACK,               // "Back"
	KEY_SETTINGS,           // "Settings"
	KEY_DEBUG,              // "Debug"
	KEY_AUTOFIRE,           // "Autofire"
    KEY_REMAP,              // "Remapping" — menu entry for key remap feature
    KEY_REMAP_CLEAR,        // "Clear all"  — reset remap to default layout
	KEY_QUIT_REMAPPING,     // "Quit remapping?"
	KEY_OK_CONFIRM,         // "OK to confirm exit"
	KEY_LR_CANCEL,          // "LEFT or RIGHT to cancel"
	KEY_CLEAR_CONFIRM,      // "Clear all mappings?"
    KEY_SCREEN,             // "Screen" — menu entry for screen settings page
    KEY_SLEEP,              // "Sleep" — screen sleep timeout label
    KEY_BRIGHTNESS,         // "Brightness"
    KEY_COUNT // Number of strings. Keep last to count.
};

const char* get_translation(TranslationKey key, int lang_idx);