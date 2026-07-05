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
// Active remap table: pointer to Settings::key_remap[]. When non-null, used
// in place of s_current_lookup for HID->ST scancode translation.
const uint8_t* s_remap_table = nullptr;

// ---------------------------------------------------------------------------
// Snapshots for core 1 (HD6301 emulation).
// The emulator polls joystick/mouse state from dr2_getb/dr4_getb on core 1.
// It must never call into HidInput/TinyUSB itself: the device map, report
// buffers and autofire state are owned by core 0 (main loop + 50 ms timer),
// and a USB mount/unmount during such a call would corrupt them.
// Instead core 0 publishes these single-byte snapshots at the end of every
// handle_joystick() cycle; core 1 only reads them (atomic byte accesses).
// ---------------------------------------------------------------------------
volatile uint8_t g_joystick_snapshot      = 0;
volatile uint8_t g_mouse_buttons_snapshot = 0;
volatile uint8_t g_mouse_enabled_snapshot = 1;

// ---------------------------------------------------------------------------
// Single HID->ST scancode lookup table (position-based, layout-independent).
// ---------------------------------------------------------------------------
extern "C" {
    extern const uint8_t st_key_lookup_hid[ST_KEY_LOOKUP_SIZE];
}

static const uint8_t* s_default_lookup = st_key_lookup_hid;

void HidInput::set_layout(KeyboardLayout)
{
    s_current_lookup = st_key_lookup_hid;
}

void HidInput::set_layout_from_index(unsigned)
{
    s_current_lookup = st_key_lookup_hid;
}

size_t HidInput::get_layout_count()
{
    return 1;
}

const char* HidInput::get_layout_name(unsigned ui_index)
{
    return (ui_index == 0) ? "HID" : "??";
}

const uint8_t* HidInput::get_layout_table(unsigned)
{
    return st_key_lookup_hid;
}

void HidInput::set_remap_table(const uint8_t* tbl)
{
    s_remap_table = tbl;
}

void HidInput::set_capture_mode(bool active)
{
    capture_mode_active  = active;
    captured_hid_keycode = 0;
    if (!active) last_key_label[0] = '\0';
}

uint8_t HidInput::get_captured_hid_keycode() const
{
    return captured_hid_keycode;
}

// ---------------------------------------------------------------------------
// TinyUSB host callbacks
// ---------------------------------------------------------------------------
// Defined in HidInput_keyboard.cpp — clears per-device LED tracking on unplug.
extern void hid_keyboard_on_unmount(int dev_addr);

// All report buffers share this size: tuh_hid_report_received_cb() in
// hid_app_host.c copies up to 64 bytes into the destination buffer regardless
// of the parsed report size, so smaller allocations (e.g.
// sizeof(hid_keyboard_report_t)) could be overflowed by devices whose
// transfers exceed the boot-protocol layout.
#define HID_REPORT_BUF_LEN 64

// HID type of each device-map entry, recorded at mount time. Used on unmount
// to decrement the right counter: tuh_hid_get_type() cannot be used there
// because it only reflects the last interface still mounted, which is wrong
// for combo keyboard+mouse devices.
static std::map<int, HID_TYPE> device_type;

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
            device[actual_addr] = new uint8_t[HID_REPORT_BUF_LEN];
            device_type[actual_addr] = HID_KEYBOARD;
            hid_app_request_report(actual_addr, device[actual_addr]);
            ++kb_count;
            // NumLock LED is set in tuh_hid_set_protocol_complete_cb() (hid_app_host.c),
            // called by TinyUSB once SET_PROTOCOL finishes and EP0 is free.
            // Any attempt here would fail: SET_PROTOCOL is still pending inside tuh_task().
        }
    }
    else if (tp == HID_MOUSE) {
        if (device.find(actual_addr) == device.end()) {
            device[actual_addr] = new uint8_t[HID_REPORT_BUF_LEN];
            device_type[actual_addr] = HID_MOUSE;
            hid_app_request_report(actual_addr, device[actual_addr]);
            ++mouse_count;
        } else {
            // Second mouse or dual-interface device: use addr+128 as the map key.
            int mouse_key = actual_addr + 128;
            if (device.find(mouse_key) == device.end()) {
                device[mouse_key] = new uint8_t[HID_REPORT_BUF_LEN];
                device_type[mouse_key] = HID_MOUSE;
                hid_app_request_report(mouse_key, device[mouse_key]);
                ++mouse_count;
            }
        }
    }
    else if (tp == HID_JOYSTICK) {
        if (device.find(actual_addr) == device.end()) {
            device[actual_addr] = new uint8_t[HID_REPORT_BUF_LEN];
            device_type[actual_addr] = HID_JOYSTICK;
            hid_app_request_report(actual_addr, device[actual_addr]);
            hid_joystick_assign_slot(actual_addr);   // assign to joy1 or joy0
            ++joy_count;
        }
    }

    if (ui_) {
        ui_->usb_connect_state(kb_count, mouse_count, joy_count);
    }
}

void tuh_hid_unmounted_cb(uint8_t dev_addr) {
    // Release both possible map entries for this address: the plain key and
    // the +128 key used for the mouse interface of combo keyboard+mouse
    // devices. Counters are decremented from the type recorded at mount time.
    const int keys[2] = { dev_addr, dev_addr + 128 };
    for (int key : keys) {
        auto it = device.find(key);
        if (it == device.end()) continue;

        auto type_it = device_type.find(key);
        HID_TYPE tp = (type_it != device_type.end()) ? type_it->second
                                                     : HID_UNDEFINED;
        if (tp == HID_KEYBOARD) {
            hid_keyboard_on_unmount(dev_addr);
            --kb_count;
        }
        else if (tp == HID_MOUSE) {
            --mouse_count;
        }
        else if (tp == HID_JOYSTICK) {
            hid_joystick_release_slot(dev_addr);
            --joy_count;
        }

        delete[] it->second;
        device.erase(it);
        if (type_it != device_type.end()) {
            device_type.erase(type_it);
        }
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
// C wrappers — called from the HD6301 emulation on core 1.
// They only read the snapshots published by core 0 (see comment at the top
// of this file); they must not call into HidInput/TinyUSB.
// ---------------------------------------------------------------------------
unsigned char st_keydown(const unsigned char code) {
    // Single-byte read from a fixed-size array updated by core 0 — atomic.
    return HidInput::instance().keydown(code);
}

int st_mouse_buttons() {
    return g_mouse_buttons_snapshot;
}

unsigned char st_joystick() {
    return g_joystick_snapshot;
}

int st_mouse_enabled() {
    return g_mouse_enabled_snapshot;
}
