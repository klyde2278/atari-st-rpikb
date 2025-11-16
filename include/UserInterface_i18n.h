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
    KEY_COUNT // Number of strings. Keep last to count.
};

const char* get_translation(TranslationKey key, int lang_idx);