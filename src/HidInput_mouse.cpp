/*
* Atari ST RP2040 IKDB Emulator
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
extern int mouse_state; // note: mouse_state is a member variable of HidInput in header; use member in method

#define GET_I32_VALUE(item)     (int32_t)(item->Value | ((item->Value & (1 << (item->Attributes.BitSize-1))) ? ~((1 << item->Attributes.BitSize) - 1) : 0))

void HidInput::handle_mouse(const int64_t cpu_cycles) {
    int32_t x = 0;
    int32_t y = 0;
    
    for (auto it : device) {
        uint8_t actual_addr = (it.first >= 128) ? (it.first - 128) : it.first;
        
        if (tuh_hid_get_type(it.first) != HID_MOUSE) {
            continue;
        }
        
        if (tuh_hid_is_mounted(it.first) && !tuh_hid_is_busy(it.first)) {
            hid_mouse_report_t* mouse = (hid_mouse_report_t*)it.second;
            const uint8_t* js = it.second;
            HID_ReportInfo_t* info = tuh_hid_get_report_info(it.first);
            
            bool is_multi_interface_mouse = (it.first >= 128);
            
            if (is_multi_interface_mouse) {
                int8_t buttons = js[0];
                int8_t dx = (int8_t)js[1];
                int8_t dy = (int8_t)js[2];
                
                if (js[2] == 0xFF && js[1] == 0x00) {
                    dy = 0;
                }
                
                x = dx;
                y = dy;
                
                mouse_state = (mouse_state & 0xfd) | ((buttons & 0x01) ? 2 : 0);
                mouse_state = (mouse_state & 0xfe) | ((buttons & 0x02) ? 1 : 0);
            }
            else if (info) {
                int8_t buttons = 0;
                for (uint8_t i = 0; i < info->TotalReportItems; ++i) {
                    HID_ReportItem_t* item = &info->ReportItems[i];
                    if (!(USB_GetHIDReportItemInfo((const uint8_t*)js, item)))
                        continue;
                    if ((item->Attributes.Usage.Page == USAGE_PAGE_BUTTON) && (item->ItemType == HID_REPORT_ITEM_In)) {
                        buttons |= (item->Value ? 1 : 0) << (item->Attributes.Usage.Usage - 1);
                    }
                    else if ((item->Attributes.Usage.Page == USAGE_PAGE_GENERIC_DCTRL) &&
                                ((item->Attributes.Usage.Usage == USAGE_X) ||
                                 (item->Attributes.Usage.Usage == USAGE_Y)) &&
                                 (item->ItemType == HID_REPORT_ITEM_In)) {
                        if (item->Attributes.Usage.Usage == USAGE_X) {
                            x = GET_I32_VALUE(item);
                        }
                        else {
                            y = GET_I32_VALUE(item);
                        }
                    }
                }
                mouse_state = (mouse_state & 0xfd) | ((buttons & MOUSE_BUTTON_LEFT) ? 2 : 0);
                mouse_state = (mouse_state & 0xfe) | ((buttons & MOUSE_BUTTON_RIGHT) ? 1 : 0);
            }
            // Next report
            hid_app_request_report(it.first, it.second);
        }
    }
    double accel = 1.0 + ((double)ui_->get_mouse_speed() * 0.1);
    AtariSTMouse::instance().set_speed((int)((double)x * accel), (int)((double)y * accel));
}