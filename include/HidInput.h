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
#include "pico/time.h"

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

    /**
     * Return a pointer to the raw HID->ST scancode table for the given layout index.
     * Returns the EN-US fallback for out-of-range indices.
     * Used by UserInterface to initialise or reset key_remap[].
     */
    static const uint8_t* get_layout_table(unsigned ui_index);

    // --- Key remap ---

    /** Set the active HID->ST scancode remap table (pointer to Settings::key_remap[]).
     *  Pass nullptr to fall back to the standard layout lookup. */
    void set_remap_table(const uint8_t* tbl);

    /** Enable / disable USB key capture mode.
     *  While active, handle_keyboard() captures the pressed HID keycode into
     *  captured_hid_keycode and updates last_key_label, but does NOT update
     *  key_states[] (preventing spurious Atari keypresses during remap UI). */
    void set_capture_mode(bool active);

    /** Return the last HID keycode captured while in capture mode.
     *  Returns 0 when no key is pressed or capture mode is off. */
    uint8_t get_captured_hid_keycode() const;

    /** Return a short display label for a USB HID keycode (QWERTY convention).
     *  Thread-safe from main loop context only (static internal buffer). */
    static const char* get_hid_label(uint8_t hk);

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
// --- State accessors ---
    unsigned char keydown(const unsigned char code) const;
    int           mouse_buttons() const;          // Atari-side button state (mouse_state)
    int           usb_mouse_buttons_raw() const   // Raw USB HID button state (bit1=L, bit0=R)
                      { return usb_mouse_buttons; }
    unsigned char joystick() const;
    bool          mouse_enabled() const;

    /**
     * Return the short display label for the last pressed USB key.
     * Empty string when no key is held. Updated by handle_keyboard().
     * Used by UserInterface::update_mouse_debug() to draw the key widget.
     */
    const char* get_last_key_label() const { return last_key_label; }

    /**
     * Return true if autofire is currently active (firing) for the given joystick.
     * @param joy  0 or 1
     */
    bool is_autofire_active(int joy) const {
        return (joy >= 0 && joy < 2) && autofire_active[joy];
    }

private:
    bool get_usb_joystick(int addr, uint8_t& axis, uint8_t& button);
    bool get_xbox_joystick(int joystick_num, uint8_t& axis, uint8_t& button);
    bool get_ps4_joystick(int joystick_num, uint8_t& axis, uint8_t& button);

    // Applies autofire logic for the given joystick port.
    // Returns the effective fire state (raw fire OR autofire pulse).
    bool process_autofire(int joy, bool raw_fire);

private:
    std::vector<unsigned char> key_states;
    int            keyboard_handle    =-1;
    int            mouse_handle       =-1;
    int            joystick_handle    =-1;
    int            mouse_state        = 0;
    int            usb_mouse_buttons  = 0;  // last known USB mouse button state (persists between reports)
    uint8_t        usb_joy_fire       = 0;  // effective USB joystick fire (after autofire): bit0=Joy1, bit1=Joy0
    uint8_t        usb_joy_raw_fire   = 0;  // last known raw USB joystick fire (before autofire): bit0=Joy1, bit1=Joy0
	unsigned char  joystick_state     = 0;
    int            mouse_overlay_fire = 0;
    bool           mouse_en           = true;

    // Short label (≤ 7 chars + NUL) of the last pressed key.
    // Cleared to "" when no key is held. Written by handle_keyboard(),
    // read by UserInterface::update_mouse_debug() via get_last_key_label().
    char last_key_label[8] = {};

    // --- Remap / capture state ---
    bool    capture_mode_active  = false;  // true = suppress key_states updates
    uint8_t captured_hid_keycode = 0;      // last non-zero HID code seen in capture mode

    // --- Autofire runtime state (not persisted) ---
    // autofire_active: true = autofire currently pulsing
    bool            autofire_active[2]        = {};
    // autofire_hold_tracking: true = fire button held, measuring duration
    bool            autofire_hold_tracking[2] = {};
    // autofire_wait_release: true = waiting for button release after a mode toggle
    bool            autofire_wait_release[2]  = {};
    // Time at which the current hold started
    absolute_time_t autofire_hold_start[2]    = {};
    // Last phase-change timestamp for the pulse generator
    absolute_time_t autofire_phase_tm[2]      = {};
    // Current pulse generator output (true = fire ON)
    bool            autofire_phase_state[2]   = {};
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

#ifdef __cplusplus
}
#endif
