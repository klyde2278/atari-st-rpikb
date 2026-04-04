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

#ifdef __cplusplus
#include <stdexcept>
#include <vector>
#include <string>
#include <cstddef>
#include "UserInterface.h"

enum class KeyboardLayout {
    CZ_CZ,
    DE_CH,
    DE_DE,
    DK_DK,
    EN_UK,
    EN_US,
    ES_ES,
    FI_FI,
    FR_CH,
    FR_FR,
    HU_HU,
    IT_IT,
    NL_NL,
    NO_NO,
    PL_PL,
    SE_SE
};

class HidInputException : public std::runtime_error {
public:
    HidInputException(const std::string& what) : std::runtime_error(what) {}
};

class HidInput {
private:
    HidInput();

public:
    static HidInput& instance();

    void set_ui(UserInterface& ui);

    // --- Layout management ---

    /** Switch the active HID->IKBD lookup table by KeyboardLayout enum value. */
    void set_layout(KeyboardLayout);

    /** Switch the active layout by UI list index (matches s_layout_map[] order). */
    void set_layout_from_index(unsigned ui_index);

    /** Return the total number of available keyboard layouts. */
    static size_t get_layout_count();

    /**
     * Return the short display name for layout at ui_index (e.g. "FR-FR").
     * Returns "??" for out-of-range indices.
     */
    static const char* get_layout_name(unsigned ui_index);

    // --- Device handling ---

    /** Open input devices (no-op on RP2040 — USB callbacks handle enumeration). */
    void open(const std::string& kbdev, const std::string& mousedev,
              const std::string joystickdev = "");

    void force_usb_mouse();
    void handle_keyboard();
    void handle_mouse(const int64_t cpu_cycles);
    void handle_joystick();
    void reset();

    // --- State accessors ---
    unsigned char keydown(const unsigned char code) const;
    int           mouse_buttons() const;
    unsigned char joystick() const;
    bool          mouse_enabled() const;

private:
    bool get_usb_joystick(int addr, uint8_t& axis, uint8_t& button);
    bool get_xbox_joystick(int joystick_num, uint8_t& axis, uint8_t& button);
    bool get_ps4_joystick(int joystick_num, uint8_t& axis, uint8_t& button);

private:
    int keyboard_handle  = -1;
    int mouse_handle     = -1;
    int joystick_handle  = -1;
    std::vector<unsigned char> key_states;
    int           mouse_state         = 0;
    unsigned char joystick_state      = 0;
    int           mouse_overlay_fire  = 0;
    bool          mouse_en            = true;
};

// ---------------------------------------------------------------------------
// Joystick slot management — implemented in HidInput_joystick.cpp.
// Called from tuh_hid_mounted_cb / tuh_hid_unmounted_cb in HidInput_common.cpp
// to maintain stable Joy1/Joy0 assignment across plug/unplug cycles.
// ---------------------------------------------------------------------------
void hid_joystick_assign_slot(int addr);
void hid_joystick_release_slot(int addr);

extern "C" {
#endif

/** Return the key state for the given Atari ST scancode (0-127). */
unsigned char st_keydown(const unsigned char code);

/** Return the current mouse button state. */
int st_mouse_buttons();

/** Return a bitfield representing the joystick state. */
unsigned char st_joystick();

/** Return 1 if the mouse is enabled, 0 if the joystick is active on Joy0. */
int st_mouse_enabled();

/** Update joystick state on-demand (called from the HD6301 emulator). */
void update_joystick_state();

#ifdef __cplusplus
}
#endif
