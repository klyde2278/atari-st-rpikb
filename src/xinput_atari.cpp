/*
 * Xbox Controller to Atari ST Joystick Mapper
 * Maps XInput gamepad data to Atari ST joystick format.
 */

#include <stdint.h>
#include <stdbool.h>

// Use the real driver header as the single source of truth for the XInput
// types and button masks. Previously this file re-declared xinputh_interface_t
// by hand ("MUST match xinput_host.h"), which silently broke whenever the
// driver struct changed. tusb.h is included first to set up the TinyUSB
// context that xinput_host.h depends on.
#include "tusb.h"
#include "xinput_host.h"

// Storage for connected Xbox controllers, indexed by USB device address (1..7).
static const xinputh_interface_t* xbox_controllers[8] = {0};

// Map one Xbox pad's state into the Atari axis/fire bitfields.
static void map_pad_to_atari(const xinput_gamepad_t* pad, uint8_t* axis,
                             uint8_t* button, uint8_t dead_zone) {
    *axis   = 0;
    *button = 0;

    // D-Pad takes priority.
    if (pad->wButtons & XINPUT_GAMEPAD_DPAD_UP)    *axis |= 0x01;
    if (pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)  *axis |= 0x02;
    if (pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)  *axis |= 0x04;
    if (pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) *axis |= 0x08;

    // Left stick as fallback when the D-Pad is centred.
    if (*axis == 0) {
        // Scale the UI setting (0..16) to the signed 16-bit half-range, matching
        // deadzone_threshold_s16() in HidInput_joystick.cpp.
        const int32_t DEADZONE = (int32_t)dead_zone * 32767 / 0x10;

        if (pad->sThumbLX < -DEADZONE) *axis |= 0x04;  // Left
        if (pad->sThumbLX >  DEADZONE) *axis |= 0x08;  // Right
        // Xbox Y axis is inverted relative to the Atari convention.
        if (pad->sThumbLY >  DEADZONE) *axis |= 0x01;  // Up
        if (pad->sThumbLY < -DEADZONE) *axis |= 0x02;  // Down
    }

    // Fire: A button, or right trigger past 50%.
    if (pad->wButtons & XINPUT_GAMEPAD_A) *button = 1;
    else if (pad->bRightTrigger > 128)    *button = 1;
}

extern "C" {

// Register Xbox controller when mounted.
void xinput_register_controller(uint8_t dev_addr, const xinputh_interface_t* xid_itf) {
    if (dev_addr < 8) {
        xbox_controllers[dev_addr] = xid_itf;
    }
}

// Unregister Xbox controller when unmounted.
void xinput_unregister_controller(uint8_t dev_addr) {
    if (dev_addr < 8) {
        xbox_controllers[dev_addr] = NULL;
    }
}

// Convert the Nth connected Xbox controller to Atari joystick format.
//
// Port convention matches the PS4 / USB-HID readers: the 1st connected pad
// drives Joy1, the 2nd drives Joy0. This lets two pads control the two Atari
// ports independently; previously joystick_num was ignored, so a single pad
// fed both ports and a second pad was never used.
bool xinput_to_atari_joystick(int joystick_num, uint8_t* axis,
                              uint8_t* button, uint8_t dead_zone) {
    const int want = (joystick_num == 1) ? 0 : 1;  // Joy1 = first pad, Joy0 = second
    int seen = 0;

    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        const xinputh_interface_t* xbox = xbox_controllers[dev_addr];
        if (!xbox || !xbox->connected) continue;

        if (seen == want) {
            map_pad_to_atari(&xbox->pad, axis, button, dead_zone);
            return true;
        }
        ++seen;
    }

    return false;
}

}  // extern "C"
