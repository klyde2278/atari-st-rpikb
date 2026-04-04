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

/*
 * USB HID (FR_FR, AZERTY) -> Atari ST IKBD scancode lookup (128 entries).
 *
 * HOW THIS TABLE WORKS
 * =====================
 * The Atari ST keyboard controller sends positional scancodes: each scancode
 * represents a physical key location on the Atari ST keyboard, not a character.
 * TOS/GEM then maps scancodes to characters using its own keymap tables.
 *
 * This table maps each USB HID keycode received from a French AZERTY keyboard
 * to the Atari ST scancode for the physically equivalent key on the French
 * Atari ST keyboard.
 *
 * AZERTY-SPECIFIC REMAPPINGS
 * ===========================
 * The physical positions of several keys differ between a PC AZERTY keyboard
 * and a QWERTY one. The following entries account for those differences:
 *
 *   HID 0x04 (physical A on AZERTY) -> scancode 30 (Q position on Atari)
 *   HID 0x14 (physical Q on AZERTY) -> scancode 16 (A position on Atari)
 *   HID 0x1A (physical W on AZERTY) -> scancode 17 (Z position on Atari)
 *   HID 0x1D (physical Z on AZERTY) -> scancode 44 (W position on Atari)
 *   HID 0x2D (- and _)              -> scancode 12 (! and section sign on Atari AZERTY)
 *   HID 0x35 (` and ~)              -> scancode 43 (superscript 2 on Atari AZERTY)
 *
 * KEYS REMAPPED TO ATARI-ONLY POSITIONS
 * =======================================
 * Some Atari ST keys have no direct equivalent on a modern PC keyboard.
 * The following PC keys are redirected to reach them:
 *
 *   HID 0x46 (Print Screen) -> scancode 98  (Help)
 *   HID 0x47 (Scroll Lock)  -> scancode 97  (Undo)
 *   HID 0x4B (Page Up)      -> scancode 99  (Keypad open-paren)
 *   HID 0x4E (Page Down)    -> scancode 100 (Keypad close-paren)
 *
 * KEYS HANDLED AS SYSTEM SHORTCUTS (NOT PASSED TO THE ATARI)
 * ============================================================
 * F11 (Ctrl+F11 = Atari reset) and F12 (Ctrl+F12 = toggle mouse mode)
 * are intercepted in HidInput_keyboard.cpp before the lookup table is used,
 * so their entries here are set to 0.
 *
 * HOW TO CREATE A NEW LAYOUT
 * ===========================
 *  1. Copy this file to  src/st_key_lookup_hid_XX_XX.cpp
 *  2. Adjust the entries that differ from the EN-US layout
 *     (refer to USB HID Usage Tables section 10 and the Atari ST hardware manual).
 *  3. In HidInput_common.cpp: add an extern declaration and a new row in s_layout_map[].
 *  4. In HidInput.h: add the new value to the KeyboardLayout enum.
 */

#include <stdint.h>
#include "st_key_lookup.h"

extern "C" {
const uint8_t st_key_lookup_hid_fr_fr[ST_KEY_LOOKUP_SIZE] = {
//  SC    HID    Key (French AZERTY keyboard)
    0,   // 0x00  No key pressed
    0,   // 0x01  Error Roll Over
    0,   // 0x02  (reserved)
    0,   // 0x03  (reserved)
    // --- Letter row (AZERTY physical positions) ---
    30,  // 0x04  A (physical) -> Q (Atari scancode)
    48,  // 0x05  B
    46,  // 0x06  C
    32,  // 0x07  D
    18,  // 0x08  E
    33,  // 0x09  F
    34,  // 0x0A  G
    35,  // 0x0B  H
    23,  // 0x0C  I
    36,  // 0x0D  J
    37,  // 0x0E  K
    38,  // 0x0F  L
    50,  // 0x10  M
    49,  // 0x11  N
    24,  // 0x12  O
    25,  // 0x13  P
    16,  // 0x14  Q (physical) -> A (Atari scancode)
    19,  // 0x15  R
    31,  // 0x16  S
    20,  // 0x17  T
    22,  // 0x18  U
    47,  // 0x19  V
    17,  // 0x1A  W (physical) -> Z (Atari scancode)
    45,  // 0x1B  X
    21,  // 0x1C  Y
    44,  // 0x1D  Z (physical) -> W (Atari scancode)
    // --- Number row ---
    2,   // 0x1E  1 and !
    3,   // 0x1F  2 and @
    4,   // 0x20  3 and #
    5,   // 0x21  4 and $
    6,   // 0x22  5 and %
    7,   // 0x23  6 and ^
    8,   // 0x24  7 and &
    9,   // 0x25  8 and *
    10,  // 0x26  9 and (
    11,  // 0x27  0 and )
    // --- Control keys ---
    28,  // 0x28  Return
    1,   // 0x29  Escape
    14,  // 0x2A  Backspace
    15,  // 0x2B  Tab
    57,  // 0x2C  Spacebar
    // --- Punctuation / symbols ---
    12,  // 0x2D  - and _  -> redirected to ! and section sign (Atari AZERTY SC 12)
    53,  // 0x2E  = and +
    26,  // 0x2F  [ and {
    27,  // 0x30  ] and }
    0,   // 0x31  \ and |  (absent from the French Atari ST keyboard)
    41,  // 0x32  Non-US # and ~
    39,  // 0x33  ; and :
    40,  // 0x34  ' and "
    43,  // 0x35  ` and ~  -> redirected to superscript 2 (Atari AZERTY)
    51,  // 0x36  , and <
    52,  // 0x37  . and >
    13,  // 0x38  / and ?
    // --- Special keys ---
    58,  // 0x39  Caps Lock
    59,  // 0x3A  F1
    60,  // 0x3B  F2
    61,  // 0x3C  F3
    62,  // 0x3D  F4
    63,  // 0x3E  F5
    64,  // 0x3F  F6
    65,  // 0x40  F7
    66,  // 0x41  F8
    67,  // 0x42  F9
    68,  // 0x43  F10
    0,   // 0x44  F11 (intercepted: Ctrl+F11 = Atari reset)
    0,   // 0x45  F12 (intercepted: Ctrl+F12 = toggle mouse mode)
    98,  // 0x46  Print Screen -> Help
    97,  // 0x47  Scroll Lock  -> Undo
    0,   // 0x48  Pause
    82,  // 0x49  Insert
    71,  // 0x4A  Home
    99,  // 0x4B  Page Up      -> Keypad (
    83,  // 0x4C  Delete Forward
    0,   // 0x4D  End
    100, // 0x4E  Page Down    -> Keypad )
    // --- Arrow keys ---
    77,  // 0x4F  Right
    75,  // 0x50  Left
    80,  // 0x51  Down
    72,  // 0x52  Up
    // --- Numeric keypad ---
    0,   // 0x53  Num Lock (managed by firmware, not forwarded to the Atari)
    101, // 0x54  Keypad /
    102, // 0x55  Keypad *
    74,  // 0x56  Keypad -
    78,  // 0x57  Keypad +
    114, // 0x58  Keypad Enter
    109, // 0x59  Keypad 1 / End
    110, // 0x5A  Keypad 2 / Down
    111, // 0x5B  Keypad 3 / Page Down
    106, // 0x5C  Keypad 4 / Left
    107, // 0x5D  Keypad 5
    108, // 0x5E  Keypad 6 / Right
    103, // 0x5F  Keypad 7 / Home
    104, // 0x60  Keypad 8 / Up
    105, // 0x61  Keypad 9 / Page Up
    112, // 0x62  Keypad 0 / Insert
    113, // 0x63  Keypad . / Delete
    96,  // 0x64  Non-US \ and |
    0,   // 0x65  Application
    0,   // 0x66  Power
    0,   // 0x67  Keypad =
    // --- F13-F24: not supported by the Atari ST ---
    0,   // 0x68  F13
    0,   // 0x69  F14
    0,   // 0x6A  F15
    0,   // 0x6B  F16
    0,   // 0x6C  F17
    0,   // 0x6D  F18
    0,   // 0x6E  F19
    0,   // 0x6F  F20
    0,   // 0x70  F21
    0,   // 0x71  F22
    0,   // 0x72  F23
    0,   // 0x73  F24
    // --- Reserved / unused ---
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0x74-0x7F
};
} // extern "C"
