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

// HidInput — joystick handling (USB HID, PS4, Xbox/XInput)
#include "HidInput.h"
#include "hid_app_host.h"
#include "ps4_controller.h"
#include "xinput_host.h"
#include "config.h"
#include <map>
#include <stdint.h>

extern std::map<int, uint8_t*> device;
extern UserInterface* ui_;

// ---------------------------------------------------------------------------
// Persistent joystick slot table.
//
// Atari ST convention: Joy1 is the primary port (games), Joy0 shares with
// the mouse. Up to two USB joysticks/pads are supported simultaneously.
//
//   slot[0] -> Joy1 (first device to connect)
//   slot[1] -> Joy0 (second device to connect)
//
// Slots are assigned on mount and released on unmount, so unplugging one
// device never disturbs the slot of the other.
// A value of -1 means the slot is empty.
// ---------------------------------------------------------------------------
static int s_joystick_slots[2] = { -1, -1 };

// Called from tuh_hid_mounted_cb() when a joystick device is detected.
void hid_joystick_assign_slot(int addr)
{
    // Do not double-assign an already-tracked address.
    for (int i = 0; i < 2; ++i) {
        if (s_joystick_slots[i] == addr) return;
    }
    // Fill the first empty slot.
    for (int i = 0; i < 2; ++i) {
        if (s_joystick_slots[i] == -1) {
            s_joystick_slots[i] = addr;
            return;
        }
    }
    // Both slots already occupied — third device ignored.
}

// Called from tuh_hid_unmounted_cb() when a joystick device disconnects.
void hid_joystick_release_slot(int addr)
{
    for (int i = 0; i < 2; ++i) {
        if (s_joystick_slots[i] == addr) {
            s_joystick_slots[i] = -1;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Dead zone helpers.
//
// Two axis conventions are handled:
//   - Unsigned 8-bit,  centred on 0x80 (standard USB HID joystick)
//   - Signed 16-bit,   centred on 0x00 (XInput-style generic HID device)
//
// The threshold is read live from the UI settings so the user's adjustment
// takes effect immediately without a reboot.
//
// dz_setting range : JOY_DZ_MIN(0) .. JOY_DZ_MAX(0x10)
// ---------------------------------------------------------------------------
static inline int deadzone_threshold_u8(uint8_t dz_setting)
{
    return (int)dz_setting;
}

static inline int32_t deadzone_threshold_s16(uint8_t dz_setting)
{
    // Scale 0..0x10 into the signed 16-bit half-range 0..32767.
    return (int32_t)dz_setting * 32767 / 0x10;
}

static inline bool in_deadzone_u8(uint32_t value, int threshold)
{
    int v = (int)value - 0x80;
    return (v > -threshold && v < threshold);
}

static inline bool in_deadzone_s16(int32_t value, int32_t threshold)
{
    return (value > -threshold && value < threshold);
}

// ---------------------------------------------------------------------------
// USB HID generic joystick reader.
//
// Axis convention is determined solely from BitSize, never from the current
// value. Testing the sign bit of the value to infer signedness is wrong
// because it depends on stick position and causes "top-left" artefacts when
// the stick is centred or displaced toward low values.
//
//   BitSize == 16 → signed, centred on 0   (XInput-style HID device)
//   BitSize <= 8  → unsigned, centred on 0x80 (standard HID joystick)
//
// Dead zone is read live from UI settings.
// ---------------------------------------------------------------------------
#define GET_I32_VALUE(item) \
    (int32_t)((item)->Value | \
    (((item)->Value & (1u << ((item)->Attributes.BitSize - 1))) \
        ? ~((1u << (item)->Attributes.BitSize) - 1) : 0))

bool HidInput::get_usb_joystick(int addr, uint8_t& axis, uint8_t& button)
{
    if (!tuh_hid_is_mounted(addr) || tuh_hid_is_busy(addr)) return false;

    const uint8_t  dz_setting    = ui_ ? ui_->get_dead_zone() : 0x08;
    const int      threshold_u8  = deadzone_threshold_u8(dz_setting);
    const int32_t  threshold_s16 = deadzone_threshold_s16(dz_setting);

    const uint8_t*    js   = device[addr];
    HID_ReportInfo_t* info = tuh_hid_get_report_info(addr);

    if (info) {
        for (uint8_t i = 0; i < info->TotalReportItems; ++i) {
            HID_ReportItem_t* item = &info->ReportItems[i];
            if (!USB_GetHIDReportItemInfo((const uint8_t*)js, item)) continue;
            if (item->ItemType != HID_REPORT_ITEM_In) continue;

            // --- Button ---
            if (item->Attributes.Usage.Page == USAGE_PAGE_BUTTON) {
                if (item->Value) button = 1;
            }
            // --- Axis ---
            else if (item->Attributes.Usage.Page == USAGE_PAGE_GENERIC_DCTRL &&
                     (item->Attributes.Usage.Usage == USAGE_X ||
                      item->Attributes.Usage.Usage == USAGE_Y)) {

                // X -> bits 2-3 (LEFT/RIGHT),  Y -> bits 0-1 (UP/DOWN)
                int bit = (item->Attributes.Usage.Usage == USAGE_X) ? 2 : 0;
                axis &= ~(0x3 << bit);  // clear previous state for this axis

                if (item->Attributes.BitSize == 16) {
                    // Signed 16-bit axis centred on 0 (XInput-style HID device).
                    int32_t v = GET_I32_VALUE(item);
                    if (!in_deadzone_s16(v, threshold_s16)) {
                        if (v < 0) axis |= (1 << bit);        // UP or LEFT
                        else       axis |= (1 << (bit + 1));  // DOWN or RIGHT
                    }
                } else {
                    // Unsigned 8-bit axis centred on 0x80 (standard HID joystick).
                    if (!in_deadzone_u8(item->Value, threshold_u8)) {
                        if (item->Value < 0x80) axis |= (1 << bit);        // UP or LEFT
                        else                    axis |= (1 << (bit + 1));   // DOWN or RIGHT
                    }
                }
            }
        }
    }

    hid_app_request_report(addr, device[addr]);
    return true;
}

// ---------------------------------------------------------------------------
// PS4 joystick reader.
// Passes the dead zone setting through to ps4_to_atari() so the per-axis
// filtering uses the same threshold as the USB HID path.
// ---------------------------------------------------------------------------
bool HidInput::get_ps4_joystick(int joystick_num, uint8_t& axis, uint8_t& button)
{
    const uint8_t dz_setting = ui_ ? ui_->get_dead_zone() : 0x08;

    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        ps4_controller_t* ps4 = ps4_get_controller(dev_addr);
        if (ps4 && ps4->connected) {
            // Pass dz_setting so ps4_to_atari() can apply the correct threshold.
            ps4_to_atari(ps4, joystick_num, &axis, &button, dz_setting);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Xbox / XInput joystick reader.
// Passes the dead zone setting through to xinput_to_atari_joystick().
// ---------------------------------------------------------------------------
extern "C" bool xinput_to_atari_joystick(int joystick_num, uint8_t* axis,
                                          uint8_t* button, uint8_t dead_zone);

bool HidInput::get_xbox_joystick(int joystick_num, uint8_t& axis, uint8_t& button)
{
    const uint8_t dz_setting = ui_ ? ui_->get_dead_zone() : 0x08;
    return xinput_to_atari_joystick(joystick_num, &axis, &button, dz_setting);
}

// ---------------------------------------------------------------------------
// handle_joystick() — main joystick processing.
//
// Slot mapping (Atari ST convention):
//   s_joystick_slots[0] -> Joy1 (primary, used by games)
//   s_joystick_slots[1] -> Joy0 (secondary, shares port with mouse)
//
// mouse_state bit layout (shared with HidInput_mouse.cpp):
//   bit 0 : Joy1 fire  / right mouse button
//   bit 1 : Joy0 fire
//   bit 2 : left mouse button
//
// When the USB mouse is enabled, bit 0 serves double duty as both the right
// mouse button AND the Joy1 fire signal — this is the correct Atari ST
// behaviour (right-click = Joy1 fire). Therefore:
//   - Joy1 fire must ALWAYS update bit 0, regardless of Joy1's port mode
//     (D-Sub or USB), as long as mouse_enabled is true.
//   - Joy0 fire only touches bit 1 when mouse is disabled (Joy0 = joystick).
// ---------------------------------------------------------------------------
void HidInput::handle_joystick()
{
    for (int joystick = 1; joystick >= 0; --joystick) {
        uint8_t axis   = 0;
        uint8_t button = 0;

        if (ui_->get_joystick() & (1 << joystick)) {
            // --- D-Sub physical port ---
            if (joystick == 1) {
                // Directions are always read from D-Sub GPIO.
                axis |= gpio_get(JOY1_UP)    ? 0 : 1;
                axis |= gpio_get(JOY1_DOWN)  ? 0 : 2;
                axis |= gpio_get(JOY1_LEFT)  ? 0 : 4;
                axis |= gpio_get(JOY1_RIGHT) ? 0 : 8;
                joystick_state = (joystick_state & ~0xF0) | (axis << 4);
                // bit 0 = Joy1 fire = right mouse button on Atari ST.
                // Always write bit 0 regardless of mouse_enabled, mirroring the USB path:
                // on the Atari ST, Joy1 fire and the right mouse button are the same signal.
                mouse_state = (mouse_state & 0xFE) | (gpio_get(JOY1_FIRE) ? 0 : 1);
            }
            else {
                // Joy0 D-Sub only active when the mouse is not using Joy0 port.
                if (!ui_->get_mouse_enabled()) {
                    mouse_state    = (mouse_state & 0xFD) | (gpio_get(JOY0_FIRE) ? 0 : 2);
                    axis |= gpio_get(JOY0_UP)    ? 0 : 1;
                    axis |= gpio_get(JOY0_DOWN)  ? 0 : 2;
                    axis |= gpio_get(JOY0_LEFT)  ? 0 : 4;
                    axis |= gpio_get(JOY0_RIGHT) ? 0 : 8;
                    joystick_state = (joystick_state & ~0x0F) | axis;
                }
            }
        }
        else {
            // --- USB / wireless device ---
            // Slot index: Joy1 uses slot[0], Joy0 uses slot[1].
            int slot_idx = (joystick == 1) ? 0 : 1;
            int addr     = s_joystick_slots[slot_idx];

            bool got_input = false;

            // Try the persistent slot first (generic USB HID joystick).
            if (addr != -1) {
                got_input = get_usb_joystick(addr, axis, button);
            }

            // Fall back to PS4 then Xbox if the HID slot is empty or inactive.
            if (!got_input) {
                if (get_ps4_joystick(joystick, axis, button)) {
                    got_input = true;
                } else if (get_xbox_joystick(joystick, axis, button)) {
                    got_input = true;
                }
            }

            if (got_input) {
                if (joystick == 1) {
                    // Joy1 fire = bit 0, always written (right mouse button when
                    // mouse is enabled — correct Atari ST behaviour).
                    mouse_state    = (mouse_state & 0xFE) | (button ? 1 : 0);
                    joystick_state = (joystick_state & ~0xF0) | (axis << 4);
                }
                else {
                    // Joy0 fire = bit 1, only when mouse is not active on Joy0.
                    if (!ui_->get_mouse_enabled()) {
                        mouse_state    = (mouse_state & 0xFD) | (button ? 2 : 0);
                        joystick_state = (joystick_state & ~0x0F) | axis;
                    }
                }
            }
        }
    }
}
