#pragma once
#include <stdint.h>

// layout_idx matches kLayouts[] in UserInterface.cpp:
//   0=CZ-CZ, 1=DE-CH, 2=DE-DE, 3=DK-DK, 4=EN-UK, 5=EN-US,
//   6=ES-ES, 7=FI-FI, 8=FR-CH, 9=FR-FR, 10=HU-HU, 11=IT-IT,
//   12=NL-NL, 13=NO-NO, 14=PL-PL, 15=SE-SE

// EN-US fallback label for an Atari ST scancode (e.g. SC16->"Q", SC30->"A").
// Returns "?" for unknown scancodes.
const char* st_scancode_name(uint8_t sc);

// Layout-aware label for an Atari ST scancode.
// Returns the override for the given layout, or NULL → caller uses st_scancode_name().
const char* get_layout_st_label(uint8_t sc, int layout_idx);

// Layout-aware label for a USB HID keycode.
// Derives the physical key label by mapping hid→ST scancode (default layout table)
// then applying get_layout_st_label(). Falls back to HidInput::get_hid_label().
const char* get_layout_hid_label(uint8_t hid, int layout_idx);

// Returns true if sc is an ISO-only key absent from ANSI keyboards (sc == 96).
bool sc_is_iso_key(uint8_t sc);

// Returns false for ANSI layouts that have no ISO extra key (EN-US=5, PL-PL=14).
bool layout_has_iso_key(int layout_idx);
