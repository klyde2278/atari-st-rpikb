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
#include "pico/util/queue.h"
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
// Serial RX: bytes from the ST cross from core 0 to core 1 through a
// lock-free SPSC queue (pico/util/queue).
//
// hd6301_receive_byte() performs read-modify-write on iram[TRCSR], which the
// emulation on core 1 also modifies (rdr_getb, trcsr_putb, hd6301_tx_empty).
// Feeding bytes directly from core 0 raced those RMW sequences and could lose
// RDRF/ORFE flags. Instead core 0 only drains the UART FIFO into the queue;
// core 1 pops bytes and updates the SCI registers itself.
// At 7812 baud, one byte arrives every ~1.28 ms; core 1 polls every 250 us.
// ---------------------------------------------------------------------------
#define ST_RX_QUEUE_DEPTH 32

static queue_t st_rx_queue;

// Core 0: move UART bytes into the cross-core queue. Must stay fast.
static void handle_rx_from_st()
{
    unsigned char data;
    while (!queue_is_full(&st_rx_queue) && SerialPort::instance().recv(data)) {
        queue_try_add(&st_rx_queue, &data);
    }
}

// Core 1: feed queued bytes to the HD6301 SCI. While RDR is still full the
// byte stays in the queue, mirroring the previous UART-FIFO holding behaviour.
static void feed_hd6301_from_queue()
{
    unsigned char data;
    while (!hd6301_sci_busy() && queue_try_remove(&st_rx_queue, &data)) {
        hd6301_receive_byte(data);
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
    // Allow core 0 to park this core during flash writes (NVSettings::write):
    // executing from XIP while the flash is being erased/programmed would
    // hard-fault. The lockout handler runs from RAM.
    multicore_lockout_victim_init();

    setup_hd6301();
    hd6301_reset(1);

    absolute_time_t tm = get_absolute_time();
    while (true) {
        feed_hd6301_from_queue();
        hd6301_tx_empty(SerialPort::instance().send_buf_empty() ? 1 : 0);
        hd6301_run_clocks(CYCLES_PER_LOOP);
        tm = delayed_by_us(tm, CYCLES_PER_LOOP);
        sleep_until(tm);
    }
}

int main()
{
    // Set the system clock first. clk_sys feeds the I2C and UART peripherals,
    // so fixing it before ui.init() / SerialPort::open() lets their baud rates
    // be computed for the final 150 MHz. Previously i2c_init() ran at the
    // 125 MHz boot clock, leaving SSD1306 SCL ~20% over spec after this call.
    set_sys_clock_khz(150000, true);

    // TinyUSB must be initialised before any other peripheral setup.
    // tusb_init() with no args is deprecated since TinyUSB 0.18.
    // Use tuh_init() directly for host-only mode on native USB port (rhport 0).
    if (!tuh_init(0)) {
        watchdog_reboot(0, 0, 0);
    }

    UserInterface ui;
    ui.init();
    ui.update();

    SerialPort::instance().open();
    SerialPort::instance().set_ui(ui);

    HidInput::instance().reset();
    HidInput::instance().set_ui(ui);

    // Small delay to let USB devices enumerate before core 1 starts.
    sleep_ms(100);

    queue_init(&st_rx_queue, sizeof(unsigned char), ST_RX_QUEUE_DEPTH);

    multicore_launch_core1(core1_entry);

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
