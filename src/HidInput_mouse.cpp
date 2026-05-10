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

// HidInput mouse handling
#include "HidInput.h"
#include "AtariSTMouse.h"
#include "tusb.h"
#include "hid_app_host.h"
#include "config.h"
#include "hardware/clocks.h"
#include "ssd1306.h"
#include <map>
#include <stdint.h>

extern ssd1306_t disp;
extern std::map<int, uint8_t*> device;
extern UserInterface* ui_;

#define GET_I32_VALUE(item) \
    (int32_t)((item)->Value | \
    (((item)->Value & (1u << ((item)->Attributes.BitSize - 1))) \
        ? ~((1u << (item)->Attributes.BitSize) - 1) : 0))

// ---------------------------------------------------------------------------
// mouse_accel
//
// Maps a mouse_speed value (MOUSE_MIN..-1 / 0 / 1..MOUSE_MAX) to a strictly
// positive acceleration multiplier.
//
// Two-segment linear scale so that every slider step produces a distinct,
// usable speed and the multiplier never reaches zero or goes negative:
//
//   speed in [MOUSE_MIN .. 0]  ->  accel in [MOUSE_ACCEL_MIN .. 1.0]
//   speed in [0 .. MOUSE_MAX]  ->  accel in [1.0 .. MOUSE_ACCEL_MAX]
//
// At speed == 0 both segments meet at exactly 1.0 (normal speed).
// ---------------------------------------------------------------------------
#define MOUSE_ACCEL_MIN  0.2   // multiplier at the slowest slider position
#define MOUSE_ACCEL_MAX  3.0   // multiplier at the fastest slider position

static double mouse_accel(int8_t speed)
{
    if (speed <= 0) {
        // Linear interpolation: MOUSE_MIN -> MOUSE_ACCEL_MIN, 0 -> 1.0
        double t = (double)(speed - MOUSE_MIN) / (double)(0 - MOUSE_MIN);
        return MOUSE_ACCEL_MIN + t * (1.0 - MOUSE_ACCEL_MIN);
    } else {
        // Keep the original formula for positive speeds so upper range is unchanged.
        return 1.0 + (double)speed * (MOUSE_ACCEL_MAX / 15.0);
    }
}

void HidInput::handle_mouse(const int64_t cpu_cycles) {
    int32_t x = 0;
    int32_t y = 0;

    for (auto it : device) {
        uint8_t actual_addr = (it.first >= 128) ? (uint8_t)(it.first - 128)
                                                 : (uint8_t)it.first;

        if (tuh_hid_get_type(it.first) != HID_MOUSE) {
            continue;
        }

        if (tuh_hid_is_mounted(actual_addr) && !tuh_hid_is_busy(actual_addr)) {
            uint8_t* js = it.second;
            HID_ReportInfo_t* info = tuh_hid_get_report_info(it.first);
            bool is_multi_interface_mouse = (it.first >= 128);

            if (is_multi_interface_mouse && !info) {
                int8_t buttons = (int8_t)js[0];
                int8_t dx      = (int8_t)js[1];
                int8_t dy      = (int8_t)js[2];

                if (js[2] == 0xFF && js[1] == 0x00) {
                    dy = 0;
                }

                x = dx;
                y = dy;
                js[1] = 0;
                js[2] = 0;

                // Update persistent USB button state only when a new report arrives.
                usb_mouse_buttons = (usb_mouse_buttons & 0xfd) | ((buttons & 0x01) ? 2 : 0);
                usb_mouse_buttons = (usb_mouse_buttons & 0xfe) | ((buttons & 0x02) ? 1 : 0);
            }
            else if (info) {
                int8_t buttons = 0;
                for (uint8_t i = 0; i < info->TotalReportItems; ++i) {
                    HID_ReportItem_t* item = &info->ReportItems[i];
                    if (!USB_GetHIDReportItemInfo((const uint8_t*)js, item))
                        continue;
                    if ((item->Attributes.Usage.Page == USAGE_PAGE_BUTTON) &&
                        (item->ItemType == HID_REPORT_ITEM_In)) {
                        buttons |= (item->Value ? 1 : 0) << (item->Attributes.Usage.Usage - 1);
                    }
                    else if ((item->Attributes.Usage.Page == USAGE_PAGE_GENERIC_DCTRL) &&
                             ((item->Attributes.Usage.Usage == USAGE_X) ||
                              (item->Attributes.Usage.Usage == USAGE_Y)) &&
                             (item->ItemType == HID_REPORT_ITEM_In)) {
                        if (item->Attributes.Usage.Usage == USAGE_X)
                            x = GET_I32_VALUE(item);
                        else
                            y = GET_I32_VALUE(item);
                    }
                }
                // Update persistent USB button state only when a new report arrives.
                usb_mouse_buttons = (usb_mouse_buttons & 0xfd) | ((buttons & MOUSE_BUTTON_LEFT)  ? 2 : 0);
                usb_mouse_buttons = (usb_mouse_buttons & 0xfe) | ((buttons & MOUSE_BUTTON_RIGHT) ? 1 : 0);
            }

            hid_app_request_report(actual_addr, it.second);
        }
    }

    double accel = mouse_accel(ui_->get_mouse_speed());
    if (ui_) {
        ui_->set_mouse_debug_delta(x, y);
    }
    AtariSTMouse::instance().set_speed((int)((double)x * accel), (int)((double)y * accel));
}