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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "config.h"
#include "6301.h"
#include "cpu.h"
#include "util.h"
#include "tusb.h"
#include "HidInput.h"
#include "SerialPort.h"
#include "AtariSTMouse.h"
#include "UserInterface.h"
#include "xinput_host.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define ROMBASE         256
#define CYCLES_PER_LOOP 250
#define PRAM_SIZE       8192

// Watchdog timeout (ms). Must be longer than the worst-case loop iteration.
#define WATCHDOG_TIMEOUT_MS 5000

extern unsigned char rom_HD6301V1ST_img[];
extern unsigned int  rom_HD6301V1ST_img_len;

// ---------------------------------------------------------------------------
// Serial RX: read all available bytes from the ST and feed them to the HD6301.
// Called every main loop iteration — must stay fast.
// At 7812 baud, one byte arrives every ~1.28 ms.
// ---------------------------------------------------------------------------
static void handle_rx_from_st()
{
    unsigned char data;
    while (SerialPort::instance().recv(data)) {
        if (!hd6301_sci_busy()) {
            hd6301_receive_byte(data);
        } else {
            // HD6301 RDR is full; the UART FIFO will hold this byte
            // until the next iteration.
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// HD6301 setup: initialise emulator and copy ROM image into PRAM.
// On failure, trigger an immediate watchdog reboot — exit() is not safe on
// the Pico without a proper C runtime teardown.
// ---------------------------------------------------------------------------
static void setup_hd6301()
{
    BYTE* pram = hd6301_init();
    if (!pram) {
        watchdog_reboot(0, 0, 0);
    }
    if (ROMBASE + rom_HD6301V1ST_img_len > PRAM_SIZE) {
        watchdog_reboot(0, 0, 0);
    }
    memcpy(pram + ROMBASE, rom_HD6301V1ST_img, rom_HD6301V1ST_img_len);
}

// ---------------------------------------------------------------------------
// Core 1 entry point: dedicated to HD6301 emulation.
// Runs independently from the USB/HID loop on core 0.
// ---------------------------------------------------------------------------
void core1_entry()
{
    setup_hd6301();
    hd6301_reset(1);

    absolute_time_t tm = get_absolute_time();
    while (true) {
        hd6301_tx_empty(SerialPort::instance().send_buf_empty() ? 1 : 0);
        hd6301_run_clocks(CYCLES_PER_LOOP);
        tm = delayed_by_us(tm, CYCLES_PER_LOOP);
        sleep_until(tm);
    }
}

int main()
{
    // TinyUSB must be initialised before any other peripheral setup.
    // tusb_init() with no args is deprecated since TinyUSB 0.18.
    // Use tuh_init() directly for host-only mode on native USB port (rhport 0).
    if (!tuh_init(0)) {
        watchdog_reboot(0, 0, 0);
    }

    UserInterface ui;
    ui.init();
    ui.update();

    // Run at 150 MHz — stable for USB host and HD6301 emulation timing.
    // No overclock: simpler peripheral management, no re-init needed.
    set_sys_clock_khz(150000, true);

    SerialPort::instance().open();
    SerialPort::instance().set_ui(ui);

    HidInput::instance().reset();
    HidInput::instance().set_ui(ui);

    // Small delay to let USB devices enumerate before core 1 starts.
    sleep_ms(100);

    multicore_launch_core1(core1_entry);

    HidInput::instance().force_usb_mouse();

    // Enable watchdog: reboot automatically if the main loop stalls.
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true);

    absolute_time_t ten_ms = get_absolute_time();
    while (true) {
        watchdog_update();

        absolute_time_t tm = get_absolute_time();

        handle_rx_from_st();
        AtariSTMouse::instance().update();

        // 10 ms poll: USB, keyboard, mouse, joystick, UI.
        if (absolute_time_diff_us(ten_ms, tm) >= 10000) {
            ten_ms = tm;
            tuh_task();
            HidInput::instance().handle_keyboard();
            HidInput::instance().handle_mouse(cpu.ncycles);
            HidInput::instance().handle_joystick();
            ui.update();
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// TinyUSB XInput vendor driver registration
// ---------------------------------------------------------------------------
usbh_class_driver_t const* usbh_app_driver_get_cb(uint8_t* driver_count)
{
    extern usbh_class_driver_t const usbh_xinput_driver;
    *driver_count = 1;
    return &usbh_xinput_driver;
}

// ---------------------------------------------------------------------------
// XInput callbacks (Xbox 360 wired/wireless, Xbox One, Xbox OG)
// ---------------------------------------------------------------------------
extern "C" {
    void xinput_register_controller(uint8_t dev_addr, const xinputh_interface_t* xid_itf);
    void xinput_unregister_controller(uint8_t dev_addr);
}

void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance,
                         const xinputh_interface_t* xinput_itf)
{
    xinput_register_controller(dev_addr, xinput_itf);

    // For Xbox 360 Wireless, wait for the controller to report as connected
    // before setting LEDs; sending LED commands to an unconnected receiver
    // has no effect and may confuse the wireless protocol.
    if (xinput_itf->type == XBOX360_WIRELESS && !xinput_itf->connected) {
        tuh_xinput_receive_report(dev_addr, instance);
        return;
    }

    tuh_xinput_set_led(dev_addr, instance, 0, true);
    tuh_xinput_set_rumble(dev_addr, instance, 0, 0, true);
    tuh_xinput_receive_report(dev_addr, instance);
}

void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    (void)instance;
    xinput_unregister_controller(dev_addr);
}

void tuh_xinput_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                   xinputh_interface_t const* xid_itf, uint16_t len)
{
    (void)len;
    xinput_register_controller(dev_addr, xid_itf);
    tuh_xinput_receive_report(dev_addr, instance);
}
