/*
 * Atari ST RP2040 IKBD Emulator - PS4 DualShock 4 Support
 * Copyright (C) 2025
 *
 * PS4 DualShock 4 controller implementation.
 * Based on TinyUSB HID controller example.
 */

#include "ps4_controller.h"
#include "tusb.h"
#include "ssd1306.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Controller storage (max 2 simultaneous PS4 controllers)
// ---------------------------------------------------------------------------
#define MAX_PS4_CONTROLLERS 2

static ps4_controller_t controllers[MAX_PS4_CONTROLLERS];
static uint8_t          controller_count = 0;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
static ps4_controller_t* find_controller_by_addr(uint8_t dev_addr)
{
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].dev_addr == dev_addr && controllers[i].connected) {
            return &controllers[i];
        }
    }
    return NULL;
}

static ps4_controller_t* allocate_controller(uint8_t dev_addr)
{
    if (controller_count >= MAX_PS4_CONTROLLERS) return NULL;

    ps4_controller_t* ctrl = &controllers[controller_count++];
    memset(ctrl, 0, sizeof(ps4_controller_t));
    ctrl->dev_addr  = dev_addr;
    ctrl->connected = true;
    return ctrl;
}

static void free_controller(uint8_t dev_addr)
{
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].dev_addr == dev_addr) {
            // Shift remaining entries down.
            for (uint8_t j = i; j < controller_count - 1; j++) {
                controllers[j] = controllers[j + 1];
            }
            controller_count--;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool ps4_is_dualshock4(uint16_t vid, uint16_t pid)
{
    if (vid != PS4_VENDOR_ID) return false;
    switch (pid) {
        case PS4_DS4_PID_V1:
        case PS4_DS4_PID_V2:
        case PS4_DS4_PID_DONGLE:
            return true;
        default:
            return false;
    }
}

bool ps4_process_report(uint8_t dev_addr, const uint8_t* report, uint16_t len)
{
    ps4_controller_t* ctrl = find_controller_by_addr(dev_addr);
    if (!ctrl)       return false;
    if (len < 9)     return false;

    ps4_report_t* input = &ctrl->report;

    // Some PS4 devices prepend a report ID byte (0x01 or 0x11); skip it.
    uint8_t offset = (report[0] == 0x01 || report[0] == 0x11) ? 1 : 0;

    input->x  = report[offset + 0];
    input->y  = report[offset + 1];
    input->z  = report[offset + 2];
    input->rz = report[offset + 3];

    uint8_t buttons1   = report[offset + 4];
    input->dpad        = buttons1 & 0x0F;
    input->square      = (buttons1 >> 4) & 1;
    input->cross       = (buttons1 >> 5) & 1;
    input->circle      = (buttons1 >> 6) & 1;
    input->triangle    = (buttons1 >> 7) & 1;

    uint8_t buttons2   = report[offset + 5];
    input->l1          = (buttons2 >> 0) & 1;
    input->r1          = (buttons2 >> 1) & 1;
    input->l2          = (buttons2 >> 2) & 1;
    input->r2          = (buttons2 >> 3) & 1;
    input->share       = (buttons2 >> 4) & 1;
    input->options     = (buttons2 >> 5) & 1;
    input->l3          = (buttons2 >> 6) & 1;
    input->r3          = (buttons2 >> 7) & 1;

    if (len > (uint16_t)(offset + 6)) {
        uint8_t buttons3 = report[offset + 6];
        input->ps        = (buttons3 >> 0) & 1;
        input->tpad      = (buttons3 >> 1) & 1;
        input->counter   = (buttons3 >> 2) & 0x3F;
    }
    if (len > (uint16_t)(offset + 8)) {
        input->l2_trigger = report[offset + 7];
        input->r2_trigger = report[offset + 8];
    }

    return true;
}

ps4_controller_t* ps4_get_controller(uint8_t dev_addr)
{
    return find_controller_by_addr(dev_addr);
}

// ---------------------------------------------------------------------------
// ps4_to_atari() — convert PS4 state to Atari ST joystick format.
//
// dead_zone: 0..JOY_DZ_MAX (0x10), from the UI settings.
//            Scaled to the PS4 stick half-range (0..127) before use.
//            A value of 8 maps to ~64, matching the original hardcoded 50
//            while giving the user fine-grained control.
//
// The dead_zone field previously stored in ps4_controller_t has been removed;
// the value is now passed in directly from the UI at each call.
// ---------------------------------------------------------------------------
void ps4_to_atari(const ps4_controller_t* ps4, uint8_t joystick_num,
                  uint8_t* direction, uint8_t* fire, uint8_t dead_zone)
{
    if (!ps4 || !direction || !fire) return;

    (void)joystick_num; // Reserved for future per-joystick mapping.

    const ps4_report_t* input = &ps4->report;
    *direction = 0;
    *fire      = 0;

    // Scale dead_zone (0..16) to the signed 8-bit half-range (0..127).
    int8_t dz = (int8_t)((int)dead_zone * 127 / 16);

    // Left analog stick (centred on 128, converted to signed).
    int8_t stick_x = (int8_t)(input->x - 128);
    int8_t stick_y = (int8_t)(input->y - 128);

    if (stick_x < -dz || stick_x > dz ||
        stick_y < -dz || stick_y > dz) {
        if (stick_y < -dz) *direction |= 0x01;  // Up
        if (stick_y >  dz) *direction |= 0x02;  // Down
        if (stick_x < -dz) *direction |= 0x04;  // Left
        if (stick_x >  dz) *direction |= 0x08;  // Right
    }

    // D-Pad overrides the analog stick when not centred.
    if (input->dpad != PS4_DPAD_CENTER) {
        switch (input->dpad) {
            case PS4_DPAD_UP:         *direction = 0x01; break;
            case PS4_DPAD_UP_RIGHT:   *direction = 0x09; break;
            case PS4_DPAD_RIGHT:      *direction = 0x08; break;
            case PS4_DPAD_DOWN_RIGHT: *direction = 0x0A; break;
            case PS4_DPAD_DOWN:       *direction = 0x02; break;
            case PS4_DPAD_DOWN_LEFT:  *direction = 0x06; break;
            case PS4_DPAD_LEFT:       *direction = 0x04; break;
            case PS4_DPAD_UP_LEFT:    *direction = 0x05; break;
            default: break;
        }
    }

    // Fire: Cross (X) button, or R2 trigger pressed more than halfway.
    *fire = (input->cross || input->r2_trigger > 128) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Mount / unmount callbacks
// ---------------------------------------------------------------------------
void ps4_mount_cb(uint8_t dev_addr)
{
    extern ssd1306_t disp;

    ps4_controller_t* ctrl = allocate_controller(dev_addr);
    (void)ctrl;

    // Brief OLED notification — no blocking delay to keep core 0 responsive.
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 25, 10, 2, (char*)"PS4");
    ssd1306_draw_string(&disp, 10, 35, 1, (char*)"DualShock 4");
    ssd1306_draw_string(&disp, 20, 50, 1, (char*)"Connected");
    ssd1306_show(&disp);
}

void ps4_unmount_cb(uint8_t dev_addr)
{
    free_controller(dev_addr);
}
