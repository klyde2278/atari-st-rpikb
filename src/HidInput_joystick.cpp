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
// USB joystick address list.
//
// At most 2 USB joysticks are supported simultaneously.
//   s_usb_joy[0] -> Joy1 (first device to connect)
//   s_usb_joy[1] -> Joy0 (second device to connect)
//
// On disconnect the list is compacted so Joy1 is always populated first.
// A value of -1 means the slot is empty.
// ---------------------------------------------------------------------------
static int s_usb_joy[2] = { -1, -1 };

void hid_joystick_assign_slot(int addr)
{
    // Do not double-assign an already-tracked address.
    for (int i = 0; i < 2; ++i) {
        if (s_usb_joy[i] == addr) return;
    }
    // 1st joystick -> joy1 (index 0), 2nd -> joy0 (index 1).
    for (int i = 0; i < 2; ++i) {
        if (s_usb_joy[i] == -1) {
            s_usb_joy[i] = addr;
            return;
        }
    }
    // Both slots full — third device ignored.
}

void hid_joystick_release_slot(int addr)
{
    for (int i = 0; i < 2; ++i) {
        if (s_usb_joy[i] == addr) {
            // Compact: shift remaining entries down so joy1 is always filled first.
            s_usb_joy[i] = -1;
            for (int j = i; j < 1; ++j) {
                s_usb_joy[j] = s_usb_joy[j + 1];
            }
            s_usb_joy[1] = -1;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Dead zone helpers.
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
//   BitSize == 16 -> signed, centred on 0   (XInput-style HID device)
//   BitSize <= 8  -> unsigned, centred on 0x80 (standard HID joystick)
// ---------------------------------------------------------------------------
#define GET_I32_VALUE(item) \
    (int32_t)((item)->Value | \
    (((item)->Value & (1u << ((item)->Attributes.BitSize - 1))) \
        ? ~((1u << (item)->Attributes.BitSize) - 1) : 0))

bool HidInput::get_usb_joystick(int addr, uint8_t& axis, uint8_t& button)
{
    auto it = device.find(addr);
    if (it == device.end()) return false;
    if (!tuh_hid_is_mounted(addr) || tuh_hid_is_busy(addr)) return false;

    HID_ReportInfo_t* info = tuh_hid_get_report_info(addr);
    // NULL means the device was not parsed or has no report items — skip it so
    // that the caller can fall through to PS4/XInput detection.
    if (!info || info->TotalReportItems == 0) return false;

    const uint8_t  dz_setting    = ui_ ? ui_->get_dead_zone() : 0x08;
    const int      threshold_u8  = deadzone_threshold_u8(dz_setting);
    const int32_t  threshold_s16 = deadzone_threshold_s16(dz_setting);
    const uint8_t* js            = it->second;

    for (uint8_t i = 0; i < info->TotalReportItems; ++i) {
        HID_ReportItem_t* item = &info->ReportItems[i];
        if (!USB_GetHIDReportItemInfo(js, item)) continue;
        if (item->ItemType != HID_REPORT_ITEM_In) continue;

        if (item->Attributes.Usage.Page == USAGE_PAGE_BUTTON) {
            if (item->Value) button = 1;
        }
        else if (item->Attributes.Usage.Page == USAGE_PAGE_GENERIC_DCTRL &&
                 (item->Attributes.Usage.Usage == USAGE_X ||
                  item->Attributes.Usage.Usage == USAGE_Y)) {

            int bit = (item->Attributes.Usage.Usage == USAGE_X) ? 2 : 0;
            axis &= ~(0x3 << bit);

            if (item->Attributes.BitSize == 16) {
                int32_t v = GET_I32_VALUE(item);
                if (!in_deadzone_s16(v, threshold_s16)) {
                    if (v < 0) axis |= (1 << bit);
                    else       axis |= (1 << (bit + 1));
                }
            } else {
                if (!in_deadzone_u8(item->Value, threshold_u8)) {
                    if (item->Value < 0x80) axis |= (1 << bit);
                    else                    axis |= (1 << (bit + 1));
                }
            }
        }
    }

    hid_app_request_report(addr, it->second);
    return true;
}

// ---------------------------------------------------------------------------
// PS4 joystick reader.
// ---------------------------------------------------------------------------
bool HidInput::get_ps4_joystick(int joystick_num, uint8_t& axis, uint8_t& button)
{
    const uint8_t dz_setting = ui_ ? ui_->get_dead_zone() : 0x08;

    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        ps4_controller_t* ps4 = ps4_get_controller(dev_addr);
        if (ps4 && ps4->connected) {
            ps4_to_atari(ps4, joystick_num, &axis, &button, dz_setting);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Xbox / XInput joystick reader.
// ---------------------------------------------------------------------------
extern "C" bool xinput_to_atari_joystick(int joystick_num, uint8_t* axis,
                                          uint8_t* button, uint8_t dead_zone);

bool HidInput::get_xbox_joystick(int joystick_num, uint8_t& axis, uint8_t& button)
{
    const uint8_t dz_setting = ui_ ? ui_->get_dead_zone() : 0x08;
    return xinput_to_atari_joystick(joystick_num, &axis, &button, dz_setting);
}

// ---------------------------------------------------------------------------
// process_autofire — autofire state machine for one joystick port.
//
// Called every handle_joystick() cycle (~10 ms) with the raw fire button state.
// Returns the effective fire state: raw OR autofire pulse.
//
// Mode (from NVSettings, set via UI):
//   0 = OFF    — autofire fully disabled; raw fire passes through unchanged.
//   1 = STANDBY — autofire ready; hold fire 3 s to toggle ON/OFF.
//
// When active (ON):
//   Generates a 50% duty-cycle pulse at the configured rate (1–15 Hz).
//   Effective fire = raw_fire OR pulse.
//
// Hold state machine (per joy):
//   IDLE          — waiting for button press.
//   HOLD_TRACKING — button held; measuring elapsed time.
//   WAIT_RELEASE  — toggle just happened; ignore button until released.
// ---------------------------------------------------------------------------
bool HidInput::process_autofire(int joy, bool raw_fire)
{
    if (!ui_) return raw_fire;

    uint8_t mode = ui_->get_autofire_mode(joy);

    // If autofire is OFF, reset any active state and pass raw fire through.
    if (mode == 0) {
        autofire_active[joy]        = false;
        autofire_hold_tracking[joy] = false;
        autofire_wait_release[joy]  = false;
        return raw_fire;
    }

    // --- Hold detection (STANDBY mode) ---
    if (autofire_wait_release[joy]) {
        // Wait for button release before starting a new hold measurement.
        if (!raw_fire) autofire_wait_release[joy] = false;
    } else if (raw_fire) {
        if (!autofire_hold_tracking[joy]) {
            autofire_hold_tracking[joy] = true;
            autofire_hold_start[joy]    = get_absolute_time();
        } else {
            int64_t held_us = absolute_time_diff_us(autofire_hold_start[joy],
                                                    get_absolute_time());
            if (held_us >= 3000000LL) {
                if (!autofire_active[joy]) {
                    // Activate: reset pulse generator so first pulse fires immediately.
                    autofire_active[joy]      = true;
                    autofire_phase_state[joy] = true;
                    autofire_phase_tm[joy]    = get_absolute_time();
                } else {
                    autofire_active[joy] = false;
                }
                autofire_hold_tracking[joy] = false;
                autofire_wait_release[joy]  = true;
            }
        }
    } else {
        autofire_hold_tracking[joy] = false;
    }

    // --- Pulse generator ---
    bool pulse = false;
    if (autofire_active[joy]) {
        uint8_t rate_hz = ui_->get_autofire_rate(joy);
        if (rate_hz < 1) rate_hz = 1;
        // Half-period in microseconds (50 % duty cycle).
        uint32_t half_period_us = 1000000u / (2u * (uint32_t)rate_hz);
        int64_t elapsed_us = absolute_time_diff_us(autofire_phase_tm[joy],
                                                   get_absolute_time());
        if (elapsed_us >= (int64_t)half_period_us) {
            autofire_phase_state[joy] = !autofire_phase_state[joy];
            autofire_phase_tm[joy]    = get_absolute_time();
        }
        pulse = autofire_phase_state[joy];
    }

    return raw_fire || pulse;
}

// ---------------------------------------------------------------------------
// handle_joystick() — main joystick processing.
//
// Port mode (set via UI, stored in get_joystick() bits):
//   bit 1 set -> Joy1 D-Sub   bit 1 clear -> Joy1 USB
//   bit 0 set -> Joy0 D-Sub   bit 0 clear -> Joy0 USB
//
// Isolation rules:
//   D-Sub mode -> GPIO signals active,  USB joystick ignored for that port.
//   USB mode   -> USB joystick active,  GPIO signals ignored for that port.
//
// mouse_state bits:
//   bit 0 : right mouse button (also Joy1 fire)
//   bit 1 : left  mouse button (also Joy0 fire)
//
// USB mouse buttons are seeded first and are independent of joystick state.
// Joystick fire (D-Sub GPIO or USB) is then OR'd on top so that either
// source can assert a button without the other source clearing it.
//
// Joy0 fire GPIO is always read in D-Sub mode (even with mouse enabled) to
// support an Atari quadrature mouse whose left button is wired to Joy0 fire.
// Joy0 directions are only written when mouse is disabled.
// ---------------------------------------------------------------------------
void HidInput::handle_joystick()
{
    // Seed mouse_state button bits from USB mouse buttons only.
    // Joystick fire sources (D-Sub GPIO or USB) are OR'd in below so that
    // they cannot override or suppress the USB mouse button state.
    mouse_state = (mouse_state & ~0x03) | usb_mouse_buttons;

    for (int joystick = 1; joystick >= 0; --joystick) {
        uint8_t axis   = 0;
        uint8_t button = 0;

        if (ui_->get_joystick() & (1 << joystick)) {
            // --- D-Sub mode: read GPIO, ignore USB ---
            // Clear USB fire state for this port to avoid ghost state on mode switch.
            if (joystick == 1) {
                usb_joy_fire     &= ~0x01;
                usb_joy_raw_fire &= ~0x01;
                axis |= gpio_get(JOY1_UP)    ? 0 : 1;
                axis |= gpio_get(JOY1_DOWN)  ? 0 : 2;
                axis |= gpio_get(JOY1_LEFT)  ? 0 : 4;
                axis |= gpio_get(JOY1_RIGHT) ? 0 : 8;
                joystick_state = (joystick_state & ~0xF0) | (axis << 4);
                bool fire1 = process_autofire(1, !gpio_get(JOY1_FIRE));
                if (fire1) mouse_state |= 0x01;
            }
            else {
                usb_joy_fire     &= ~0x02;
                usb_joy_raw_fire &= ~0x02;
                // Joy0 fire always read: covers Atari quadrature mouse left button.
                bool fire0 = process_autofire(0, !gpio_get(JOY0_FIRE));
                if (fire0) mouse_state |= 0x02;
                if (!ui_->get_mouse_enabled()) {
                    axis |= gpio_get(JOY0_UP)    ? 0 : 1;
                    axis |= gpio_get(JOY0_DOWN)  ? 0 : 2;
                    axis |= gpio_get(JOY0_LEFT)  ? 0 : 4;
                    axis |= gpio_get(JOY0_RIGHT) ? 0 : 8;
                    joystick_state = (joystick_state & ~0x0F) | axis;
                }
            }
        }
        else {
            // --- USB mode: read USB joystick, ignore GPIO ---
            // Joy0 USB is skipped when the USB mouse is enabled (exclusive port).
            if (joystick == 0 && ui_->get_mouse_enabled()) continue;

            int addr = s_usb_joy[(joystick == 1) ? 0 : 1];
            bool got_input = false;

            if (addr != -1) {
                got_input = get_usb_joystick(addr, axis, button);
            }
            if (!got_input) {
                if (get_ps4_joystick(joystick, axis, button))       got_input = true;
                else if (get_xbox_joystick(joystick, axis, button)) got_input = true;
            }

            if (got_input) {
                // New report arrived: update last known raw fire state and directions.
                if (joystick == 1) {
                    if (button) usb_joy_raw_fire |= 0x01; else usb_joy_raw_fire &= ~0x01;
                    joystick_state = (joystick_state & ~0xF0) | (axis << 4);
                } else {
                    if (button) usb_joy_raw_fire |= 0x02; else usb_joy_raw_fire &= ~0x02;
                    joystick_state = (joystick_state & ~0x0F) | axis;
                }
            }

            // Always call process_autofire with the last known raw fire state,
            // mirroring D-Sub GPIO behaviour: the hold timer advances every cycle
            // regardless of whether a fresh USB report has arrived.
            // For joy1, JOY1_FIRE GPIO is also included: the right button of an
            // Atari quadrature mouse is wired to that pin and must work regardless
            // of whether joy1 is in USB or D-Sub mode.
            if (joystick == 1) {
                bool raw = (bool)(usb_joy_raw_fire & 0x01) || !gpio_get(JOY1_FIRE);
                bool eff_fire = process_autofire(1, raw);
                if (eff_fire) usb_joy_fire |= 0x01; else usb_joy_fire &= ~0x01;
            } else {
                bool eff_fire = process_autofire(0, (bool)(usb_joy_raw_fire & 0x02));
                if (eff_fire) usb_joy_fire |= 0x02; else usb_joy_fire &= ~0x02;
            }

            // OR effective fire into mouse_state.
            if (joystick == 1) {
                if (usb_joy_fire & 0x01) mouse_state |= 0x01;
            } else {
                if (usb_joy_fire & 0x02) mouse_state |= 0x02;
            }
        }
    }
}