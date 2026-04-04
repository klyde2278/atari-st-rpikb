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

// Common implementation for HidInput: shared globals, TinyUSB callbacks, layout management.
#include "HidInput.h"
#include "st_key_lookup.h"
#include "AtariSTMouse.h"
#include "tusb.h"
#include "hid_app_host.h"
#include "config.h"
#include "hardware/clocks.h"
#include "6301.h"
#include "ssd1306.h"
#include <map>
#include <stdint.h>

extern ssd1306_t disp;

// ---------------------------------------------------------------------------
// Shared globals (single definition; declared extern in other translation units)
// ---------------------------------------------------------------------------
std::map<int, uint8_t*> device;
UserInterface* ui_         = nullptr;
int kb_count               = 0;
int mouse_count            = 0;
int joy_count              = 0;
uint8_t current_led_state  = 0x01;   // NumLock ON by default
const uint8_t* s_current_lookup = nullptr;

// ---------------------------------------------------------------------------
// Forward-declare every layout table.
// To add a new layout: add its extern here and one entry in s_layout_map[].
// ---------------------------------------------------------------------------
extern "C" {
    extern const uint8_t st_key_lookup_hid_cz_cz[ST_KEY_LOOKUP_SIZE];
    extern const uint8_t st_key_lookup_hid_de_ch[ST_KEY_LOOKUP_SIZE];
    extern const uint8_t st_key_lookup_hid_de_de[ST_KEY_LOOKUP_SIZE];
    extern const uint8_t st_key_lookup_hid_dk_dk[ST_KEY_LOOKUP_SIZE];
    extern const uint8_t st_key_lookup_hid_en_uk[ST_KEY_LOOKUP_SIZE];
    extern const uint8_t st_key_lookup_hid_en_us[ST_KEY_LOOKUP_SIZE];
    extern const uint8_t st_key_lookup_hid_es_es[ST_KEY_LOOKUP_SIZE];
    extern const uint8_t st_key_lookup_hid_fi_fi[ST_KEY_LOOKUP_SIZE];
    extern const uint8_t st_key_lookup_hid_fr_ch[ST_KEY_LOOKUP_SIZE];
    extern const uint8_t st_key_lookup_hid_fr_fr[ST_KEY_LOOKUP_SIZE];
    extern const uint8_t st_key_lookup_hid_hu_hu[ST_KEY_LOOKUP_SIZE];
    extern const uint8_t st_key_lookup_hid_it_it[ST_KEY_LOOKUP_SIZE];
    extern const uint8_t st_key_lookup_hid_nl_nl[ST_KEY_LOOKUP_SIZE];
    extern const uint8_t st_key_lookup_hid_no_no[ST_KEY_LOOKUP_SIZE];
    extern const uint8_t st_key_lookup_hid_pl_pl[ST_KEY_LOOKUP_SIZE];
    extern const uint8_t st_key_lookup_hid_se_se[ST_KEY_LOOKUP_SIZE];
}

// ---------------------------------------------------------------------------
// Table-driven layout registry.
//
// Each entry maps a KeyboardLayout enum value to its HID->ST scancode lookup
// table and a short display name for the UI.
//
// Improvement over the original code: adding a new language requires only a
// new extern above and one new line here. The previous approach required
// changes in three separate places: the extern block, map_index_to_layout(),
// and the switch statement in set_layout().
// ---------------------------------------------------------------------------
struct LayoutEntry {
    KeyboardLayout  layout;
    const uint8_t*  table;
    const char*     name;   // Short label shown in the UI
};

static const LayoutEntry s_layout_map[] = {
    { KeyboardLayout::CZ_CZ, st_key_lookup_hid_cz_cz, "CZ"   },
    { KeyboardLayout::DE_CH, st_key_lookup_hid_de_ch, "DE-CH" },
    { KeyboardLayout::DE_DE, st_key_lookup_hid_de_de, "DE"    },
    { KeyboardLayout::DK_DK, st_key_lookup_hid_dk_dk, "DK"    },
    { KeyboardLayout::EN_UK, st_key_lookup_hid_en_uk, "EN-UK" },
    { KeyboardLayout::EN_US, st_key_lookup_hid_en_us, "EN-US" },
    { KeyboardLayout::ES_ES, st_key_lookup_hid_es_es, "ES"    },
    { KeyboardLayout::FI_FI, st_key_lookup_hid_fi_fi, "FI"    },
    { KeyboardLayout::FR_CH, st_key_lookup_hid_fr_ch, "FR-CH" },
    { KeyboardLayout::FR_FR, st_key_lookup_hid_fr_fr, "FR"    },
    { KeyboardLayout::HU_HU, st_key_lookup_hid_hu_hu, "HU"    },
    { KeyboardLayout::IT_IT, st_key_lookup_hid_it_it, "IT"    },
    { KeyboardLayout::NL_NL, st_key_lookup_hid_nl_nl, "NL"    },
    { KeyboardLayout::NO_NO, st_key_lookup_hid_no_no, "NO"    },
    { KeyboardLayout::PL_PL, st_key_lookup_hid_pl_pl, "PL"    },
    { KeyboardLayout::SE_SE, st_key_lookup_hid_se_se, "SE"    },
};
static constexpr size_t s_layout_count =
    sizeof(s_layout_map) / sizeof(s_layout_map[0]);

// Fallback used when a layout value is not found in the table.
static const uint8_t* s_default_lookup = st_key_lookup_hid_en_us;

// ---------------------------------------------------------------------------
// set_layout() — select the active scancode lookup table.
//
// Replaces the original switch/case with a linear search over s_layout_map[].
// Falls back to EN-US for unknown values.
// ---------------------------------------------------------------------------
void HidInput::set_layout(KeyboardLayout l)
{
    for (size_t i = 0; i < s_layout_count; ++i) {
        if (s_layout_map[i].layout == l) {
            s_current_lookup = s_layout_map[i].table;
            return;
        }
    }
    s_current_lookup = s_default_lookup;
}

// ---------------------------------------------------------------------------
// set_layout_from_index() — select layout by UI list index.
//
// The index directly addresses s_layout_map[], so the UI order and the
// internal order are always in sync without a separate mapping function.
// ---------------------------------------------------------------------------
void HidInput::set_layout_from_index(unsigned ui_index)
{
    if (ui_index < s_layout_count) {
        set_layout(s_layout_map[ui_index].layout);
    } else {
        set_layout(KeyboardLayout::EN_US);
    }
}

// ---------------------------------------------------------------------------
// Layout metadata accessors.
// Allow the UI to populate its list without duplicating layout names
// or coupling to the internal enum order.
// ---------------------------------------------------------------------------
size_t HidInput::get_layout_count()
{
    return s_layout_count;
}

const char* HidInput::get_layout_name(unsigned ui_index)
{
    if (ui_index < s_layout_count) {
        return s_layout_map[ui_index].name;
    }
    return "??";
}

// ---------------------------------------------------------------------------
// TinyUSB host callbacks
// ---------------------------------------------------------------------------
extern "C" {

void tuh_hid_mounted_cb(uint8_t dev_addr) {
    bool is_marked_mouse = (dev_addr & 0x80) != 0;
    uint8_t actual_addr  = dev_addr & 0x7F;

    HID_TYPE tp;
    if (is_marked_mouse) {
        tp = HID_MOUSE;
    } else {
        tp = tuh_hid_get_type(actual_addr);
    }

    if (tp == HID_KEYBOARD) {
        if (device.find(actual_addr) == device.end()) {
            device[actual_addr] = new uint8_t[sizeof(hid_keyboard_report_t)];
            hid_app_request_report(actual_addr, device[actual_addr]);
            ++kb_count;
            // Brief delay then clear all LEDs before applying the default state.
            sleep_ms(100);
            uint8_t led_off = 0x00;
            for (uint8_t idx = 0; idx < 3; idx++) {
                tuh_hid_set_report(actual_addr, idx, 0,
                    HID_REPORT_TYPE_OUTPUT, &led_off, sizeof(led_off));
            }
            sleep_ms(50);
            // Enable NumLock LED only.
            uint8_t led_on = 0x01;
            for (uint8_t idx = 0; idx < 3; idx++) {
                if (tuh_hid_set_report(actual_addr, idx, 0,
                        HID_REPORT_TYPE_OUTPUT, &led_on, sizeof(led_on))) {
                    break;
                }
            }
        }
    }
    else if (tp == HID_MOUSE) {
        if (device.find(actual_addr) == device.end()) {
            device[actual_addr] = new uint8_t[tuh_hid_get_report_size(actual_addr)];
            hid_app_request_report(actual_addr, device[actual_addr]);
            ++mouse_count;
        } else {
            // Second mouse or dual-interface device: use addr+128 as the map key.
            int mouse_key = actual_addr + 128;
            device[mouse_key] = new uint8_t[tuh_hid_get_report_size(actual_addr)];
            hid_app_request_report(mouse_key, device[mouse_key]);
            ++mouse_count;
        }
    }
    else if (tp == HID_JOYSTICK) {
        device[actual_addr] = new uint8_t[tuh_hid_get_report_size(actual_addr)];
        hid_app_request_report(actual_addr, device[actual_addr]);

		// The joystick is assigned and remains attached to Joy1 (first) or Joy0 (second)
		hid_joystick_assign_slot(actual_addr);
        ++joy_count;
    }

    if (ui_) {
        ui_->usb_connect_state(kb_count, mouse_count, joy_count);
    }
}

void tuh_hid_unmounted_cb(uint8_t dev_addr) {
    HID_TYPE tp = tuh_hid_get_type(dev_addr);
    if      (tp == HID_KEYBOARD) --kb_count;
    else if (tp == HID_MOUSE)    --mouse_count;
    else if (tp == HID_JOYSTICK) {
        --joy_count;
        hid_joystick_release_slot(dev_addr);
    }
    auto it = device.find(dev_addr);
    if (it != device.end()) {
        delete[] it->second;
        device.erase(it);
    }
    if (ui_) {
        ui_->usb_connect_state(kb_count, mouse_count, joy_count);
    }
}

void tuh_hid_isr(uint8_t dev_addr, xfer_result_t event) {
    (void)dev_addr;
    (void)event;
}

} // extern "C"

// ---------------------------------------------------------------------------
// HidInput class
// ---------------------------------------------------------------------------
#define JOY_GPIO_INIT(io) \
    gpio_init(io); gpio_set_dir(io, GPIO_IN); gpio_pull_up(io);

HidInput::HidInput() {
    key_states.resize(128);
    std::fill(key_states.begin(), key_states.end(), 0);

    JOY_GPIO_INIT(JOY1_UP);
    JOY_GPIO_INIT(JOY1_DOWN);
    JOY_GPIO_INIT(JOY1_LEFT);
    JOY_GPIO_INIT(JOY1_RIGHT);
    JOY_GPIO_INIT(JOY1_FIRE);
    JOY_GPIO_INIT(JOY0_UP);
    JOY_GPIO_INIT(JOY0_DOWN);
    JOY_GPIO_INIT(JOY0_LEFT);
    JOY_GPIO_INIT(JOY0_RIGHT);
    JOY_GPIO_INIT(JOY0_FIRE);

    // Start with the default layout; the UI will call set_layout_from_index()
    // once the user's preference is known.
    s_current_lookup = s_default_lookup;
}

HidInput& HidInput::instance() {
    static HidInput hid;
    return hid;
}

void HidInput::set_ui(UserInterface& ui) {
    ui_ = &ui;
}

void HidInput::open(const std::string& kbdev, const std::string& mousedev,
                    const std::string joystickdev) {
    (void)kbdev; (void)mousedev; (void)joystickdev;
}

void HidInput::force_usb_mouse() {
    ui_->set_mouse_enabled(true);
}

void HidInput::reset() {
    std::fill(key_states.begin(), key_states.end(), 0);
}

unsigned char HidInput::keydown(const unsigned char code) const {
    return (code < 128) ? key_states[code] : 0;
}

int HidInput::mouse_buttons() const {
    return mouse_state;
}

unsigned char HidInput::joystick() const {
    return joystick_state;
}

bool HidInput::mouse_enabled() const {
    return ui_->get_mouse_enabled();
}

// ---------------------------------------------------------------------------
// C wrappers
// ---------------------------------------------------------------------------
unsigned char st_keydown(const unsigned char code) {
    return HidInput::instance().keydown(code);
}

int st_mouse_buttons() {
    return HidInput::instance().mouse_buttons();
}

unsigned char st_joystick() {
    return HidInput::instance().joystick();
}

int st_mouse_enabled() {
    return HidInput::instance().mouse_enabled() ? 1 : 0;
}

void update_joystick_state() {
    HidInput::instance().handle_joystick();
}
