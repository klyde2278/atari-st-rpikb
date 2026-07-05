#pragma once
#include <stdint.h>

// layout_idx matches kLayouts[] in UserInterface.cpp:
//   0=CZ-CZ, 1=DE-CH, 2=DE-DE, 3=DK-DK, 4=EN-UK, 5=EN-US,
//   6=ES-ES, 7=FI-FI, 8=FR-CH, 9=FR-FR, 10=HU-HU, 11=IT-IT,
//   12=NL-NL, 13=NO-NO, 14=PL-PL, 15=SE-SE

// Physical key label for a USB HID keycode in the given layout.
// Returns the layout-specific override if one exists, otherwise the EN-US
// baseline label. Independent of Atari ST scancodes.
const char* get_hid_layout_label(uint8_t hid, int layout_idx);

// Returns true if the HID keycode is reserved for a system hotkey or is a
// non-remappable structural key, and must not be used as a remap target:
//   Ctrl+F9/F10/F11/F12 — joystick/mouse/reset hotkeys
//   Alt+KP-/KP+         — CPU clock hotkeys
//   Esc, Tab, CapsLock, NumLock, Enter, Backspace
bool hid_is_system_reserved(uint8_t hid);
