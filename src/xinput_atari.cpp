/*
 * Atari ST RP2040 IKBD Emulator - Xbox / XInput Controller Mapper
 * Maps XInput gamepad data to Atari ST joystick format.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

// Forward-declare only what is needed from xinput_host.h to avoid
// pulling in the full TinyUSB header tree in a C++ translation unit.
extern "C" {

#define XINPUT_GAMEPAD_DPAD_UP    0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN  0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT  0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT 0x0008
#define XINPUT_GAMEPAD_A          0x1000

#define CFG_TUH_XINPUT_EPIN_BUFSIZE  64
#define CFG_TUH_XINPUT_EPOUT_BUFSIZE 64

typedef enum {
    XINPUT_UNKNOWN = 0,
    XBOXONE,
    XBOX360_WIRELESS,
    XBOX360_WIRED,
    XBOXOG
} xinput_type_t;

typedef struct {
    uint16_t wButtons;
    uint8_t  bLeftTrigger;
    uint8_t  bRightTrigger;
    int16_t  sThumbLX;
    int16_t  sThumbLY;
    int16_t  sThumbRX;
    int16_t  sThumbRY;
} xinput_gamepad_t;

// Must match xinput_host.h exactly.
typedef struct {
    xinput_type_t    type;
    xinput_gamepad_t pad;
    uint8_t          connected;
    uint8_t          new_pad_data;
    uint8_t          itf_num;
    uint8_t          ep_in;
    uint8_t          ep_out;
    uint16_t         epin_size;
    uint16_t         epout_size;
    uint8_t          epin_buf[CFG_TUH_XINPUT_EPIN_BUFSIZE];
    uint8_t          epout_buf[CFG_TUH_XINPUT_EPOUT_BUFSIZE];
    int              last_xfer_result;
    uint32_t         last_xferred_bytes;
} xinputh_interface_t;

} // extern "C"

// ---------------------------------------------------------------------------
// Controller registry — indexed by TinyUSB dev_addr (1..7).
// ---------------------------------------------------------------------------
static const xinputh_interface_t* xbox_controllers[8] = {};

extern "C" {

void xinput_register_controller(uint8_t dev_addr, const xinputh_interface_t* xid_itf)
{
    if (dev_addr < 8) xbox_controllers[dev_addr] = xid_itf;
}

void xinput_unregister_controller(uint8_t dev_addr)
{
    if (dev_addr < 8) xbox_controllers[dev_addr] = NULL;
}

// ---------------------------------------------------------------------------
// xinput_to_atari_joystick()
//
// joystick_num : 0 = Joy0 (secondary), 1 = Joy1 (primary).
//                Maps to the Nth connected Xbox controller found, preserving
//                the Atari ST convention where Joy1 is the primary port.
//
// dead_zone    : 0..16 (JOY_DZ_MIN..JOY_DZ_MAX), from the UI settings.
//                Scaled to the XInput signed 16-bit half-range (0..32767).
//                A value of 8 maps to ~16383 (~25%), matching the original
//                hardcoded value of 8000 out of 32767.
//
// D-Pad inputs are always digital and bypass the dead zone.
// The left analog stick is used as fallback when the D-Pad is centred.
// Left stick Y axis is intentionally inverted to match Atari ST convention
// (positive Y = Down on Atari, opposite of XInput).
// ---------------------------------------------------------------------------
bool xinput_to_atari_joystick(int joystick_num, uint8_t* axis,
                               uint8_t* button, uint8_t dead_zone)
{
    // Scale dead_zone (0..16) to XInput signed half-range (0..32767).
    int32_t dz = (int32_t)dead_zone * 32767 / 16;

    int found = 0;
    for (uint8_t dev_addr = 1; dev_addr < 8; dev_addr++) {
        const xinputh_interface_t* xbox = xbox_controllers[dev_addr];
        if (!xbox || !xbox->connected) continue;

        // Skip controllers until we reach the one matching joystick_num.
        // joystick_num 1 (Joy1) gets the first controller found,
        // joystick_num 0 (Joy0) gets the second.
        if (found != joystick_num) {
            ++found;
            continue;
        }

        const xinput_gamepad_t* pad = &xbox->pad;
        *axis   = 0;
        *button = 0;

        // D-Pad — always digital, no dead zone required.
        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_UP)    *axis |= 0x01;
        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)  *axis |= 0x02;
        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)  *axis |= 0x04;
        if (pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) *axis |= 0x08;

        // Left stick fallback when D-Pad is not pressed.
        if (*axis == 0) {
            if (pad->sThumbLX < -dz) *axis |= 0x04;  // Left
            if (pad->sThumbLX >  dz) *axis |= 0x08;  // Right
            // Y axis is inverted vs Atari ST (XInput +Y = up, Atari +Y = down).
            if (pad->sThumbLY >  dz) *axis |= 0x01;  // Up
            if (pad->sThumbLY < -dz) *axis |= 0x02;  // Down
        }

        // Fire: A button or R2 trigger pressed more than halfway.
        *button = ((pad->wButtons & XINPUT_GAMEPAD_A) ||
                   (pad->bRightTrigger > 128)) ? 1 : 0;

        return true;
    }

    return false;
}

} // extern "C"
