/*
 * Atari ST RP2040 IKBD Emulator - PS4 DualShock 4 Support
 * Copyright (C) 2025
 *
 * PS4 DualShock 4 controller support for Atari ST joystick emulation.
 * Based on TinyUSB HID controller example.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef PS4_CONTROLLER_H
#define PS4_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// PS4 DualShock 4 identification
// ---------------------------------------------------------------------------
#define PS4_VENDOR_ID       0x054C  // Sony

#define PS4_DS4_PID_V1      0x05C4  // DualShock 4 v1
#define PS4_DS4_PID_V2      0x09CC  // DualShock 4 v2
#define PS4_DS4_PID_DONGLE  0x0BA0  // DualShock 4 USB Wireless Adaptor

// ---------------------------------------------------------------------------
// PS4 DualShock 4 input report (Sony-specific HID format)
// Analog sticks: 0-255, centred on 128.
// ---------------------------------------------------------------------------
typedef struct TU_ATTR_PACKED {
    uint8_t x, y, z, rz;       // Left stick X/Y, right stick X/Y

    uint8_t dpad     : 4;       // D-Pad (0-7 = directions, 8 = centre)
    uint8_t square   : 1;
    uint8_t cross    : 1;       // Cross (X) — primary fire button
    uint8_t circle   : 1;
    uint8_t triangle : 1;

    uint8_t l1      : 1;
    uint8_t r1      : 1;
    uint8_t l2      : 1;
    uint8_t r2      : 1;
    uint8_t share   : 1;
    uint8_t options : 1;
    uint8_t l3      : 1;
    uint8_t r3      : 1;

    uint8_t ps      : 1;
    uint8_t tpad    : 1;
    uint8_t counter : 6;

    uint8_t l2_trigger;         // L2 analog (0-255)
    uint8_t r2_trigger;         // R2 analog (0-255) — secondary fire if > 128
} ps4_report_t;

// D-Pad direction values
#define PS4_DPAD_UP         0
#define PS4_DPAD_UP_RIGHT   1
#define PS4_DPAD_RIGHT      2
#define PS4_DPAD_DOWN_RIGHT 3
#define PS4_DPAD_DOWN       4
#define PS4_DPAD_DOWN_LEFT  5
#define PS4_DPAD_LEFT       6
#define PS4_DPAD_UP_LEFT    7
#define PS4_DPAD_CENTER     8

// ---------------------------------------------------------------------------
// PS4 controller state.
// The dead zone is no longer stored here — it is passed at call time from
// the UI settings so that a single shared value applies to all device types.
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t      dev_addr;  // USB device address
    bool         connected;
    ps4_report_t report;    // Latest decoded input report
} ps4_controller_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/** Return true if vid/pid identifies a DualShock 4. */
bool ps4_is_dualshock4(uint16_t vid, uint16_t pid);

/** Decode a raw HID report into the controller's ps4_report_t. */
bool ps4_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len);

/** Return the controller state for dev_addr, or NULL if not connected. */
ps4_controller_t* ps4_get_controller(uint8_t dev_addr);

/**
 * Convert PS4 input to Atari ST joystick format.
 *
 * @param ps4          Controller state (must not be NULL).
 * @param joystick_num Target Atari joystick number (0 or 1, reserved for future use).
 * @param direction    Output: direction bitfield (bit0=Up, bit1=Down, bit2=Left, bit3=Right).
 * @param fire         Output: fire button state (0 or 1).
 * @param dead_zone    Dead zone threshold from UI settings (0..JOY_DZ_MAX = 0x10).
 *                     Scaled internally to the PS4 stick half-range (0..127).
 */
void ps4_to_atari(const ps4_controller_t* ps4, uint8_t joystick_num,
                  uint8_t* direction, uint8_t* fire, uint8_t dead_zone);

/** Called when a PS4 controller is mounted. */
void ps4_mount_cb(uint8_t dev_addr);

/** Called when a PS4 controller is unmounted. */
void ps4_unmount_cb(uint8_t dev_addr);

#ifdef __cplusplus
}
#endif

#endif /* PS4_CONTROLLER_H */
