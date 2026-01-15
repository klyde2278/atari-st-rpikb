/*
 * Atari ST RP2040 IKBD Emulator
 * Copyright (C) 2025 Emmanuel Barraud
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


/*
 * USB HID (EN_US) -> Atari ST IKBD scancode lookup (128 entries).
 * Includes generic mappings:
 *  - PrintScreen (0x46) -> Help (98)
 *  - ScrollLock  (0x47) -> Undo (97)
 *  - PageUp      (0x4B) -> keypad '(' (99)
 *  - PageDown    (0x4E) -> keypad ')' (100)
 *
 * References:
 * - TOS/IKBD scancodes: https://freemint.github.io/tos.hyp/en/scancode.html
 * - USB HID Keyboard/Keypad usage page: see Microchip HID codes list.
 */

#include <stdint.h>
#include "st_key_lookup.h"

extern "C" {
const uint8_t st_key_lookup_hid_dk_dk[ST_KEY_LOOKUP_SIZE] = {
  /* 0x00..0x03 */ 0,0,0,0,

  /* Letters 0x04..0x1D (QWERTY positions -> ST scancodes) */
  30, /* 0x04 'A' */
  48, /* 0x05 'B' */
  46, /* 0x06 'C' */
  32, /* 0x07 'D' */
  18, /* 0x08 'E' */
  33, /* 0x09 'F' */
  34, /* 0x0A 'G' */
  35, /* 0x0B 'H' */
  23, /* 0x0C 'I' */
  36, /* 0x0D 'J' */
  37, /* 0x0E 'K' */
  38, /* 0x0F 'L' */
  50, /* 0x10 'M' */
  49, /* 0x11 'N' */
  24, /* 0x12 'O' */
  25, /* 0x13 'P' */
  16, /* 0x14 'Q' */
  19, /* 0x15 'R' */
  31, /* 0x16 'S' */
  20, /* 0x17 'T' */
  22, /* 0x18 'U' */
  47, /* 0x19 'V' */
  17, /* 0x1A 'W' */
  45, /* 0x1B 'X' */
  21, /* 0x1C 'Y' */
  44, /* 0x1D 'Z' */

  /* 0x1E..0x27 digits row '1'..'0' */
  2,3,4,5,6,7,8,9,10,11,

  /* 0x28..0x2B Enter, Esc, Backspace, Tab */
  28,1,14,15,

  /* 0x2C Space */
  57,

  /* 0x2D '-' and '_' */
  12,

  /* 0x2E '=' and '+' */
  13,

  /* 0x2F '[' and '{' */
  26,

  /* 0x30 ']' and '}' */
  27,

  /* 0x31 '\' and '|' (US backslash position) */
  43,

  /* 0x32 Non-US # and ~ (ISO key) */
  41,

  /* 0x33 ';' and ':' */
  39,

  /* 0x34 '\'' and '"' */
  40,

  /* 0x35 '`' and '~' (often mapped to ST 41 or 43 depending on ROM; use 41 here) */
  41,

  /* 0x36 ',' */
  51,

  /* 0x37 '.' */
  52,

  /* 0x38 '/' and '?' */
  53,

  /* 0x39 CapsLock */
  58,

  /* 0x3A..0x45 F1..F12 */
  59,60,61,62,63,64,65,66,67,68,99,100,

  /* 0x46 PrintScreen -> Help */
  98,

  /* 0x47 ScrollLock -> Undo */
  97,

  /* 0x48 Pause */
  0,

  /* 0x49..0x4C Insert, Home, PageUp, Delete */
  82, 71, 99, 83,   /* PageUp -> keypad '(' (99) */

  /* 0x4D..0x52 End, PageDown, Right, Left, Down, Up */
  0, 100, 77, 75, 80, 72, /* PageDown -> keypad ')' (100) */

  /* 0x53 NumLock */
  0,

  /* 0x54..0x58 keypad '/', '*', '-', '+', Enter */
  101, 102, 74, 78, 114,

  /* 0x59..0x63 keypad 1..9,0,. */
  109,110,111,106,107,108,103,104,105,112,113,

  /* 0x64 ISO 102nd key (< >) -> ST 96 */
  96,

  /* 0x65..0x67 Application, Power, Keypad '=' (unused) */
  0,0,0,

  /* 0x68..0x73 F13..F24 (unused) */
  0,0,0,0,0,0,0,0,0,0,0,0,

  /* 0x74..0x7F reserved */
  0,0,0,0,0,0,0,0,0,0,0,0
};
}
