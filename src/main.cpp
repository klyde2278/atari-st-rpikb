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

#define ROMBASE 256
#define CYCLES_PER_LOOP 250

extern unsigned char rom_HD6301V1ST_img[];
extern unsigned int rom_HD6301V1ST_img_len;

static void handle_rx_from_st() {
    unsigned char data;
    while (SerialPort::instance().recv(data)) {
        if (!hd6301_sci_busy()) {
            hd6301_receive_byte(data);
        } else {
            break;
        }
    }
}

void setup_hd6301() {
    BYTE* pram = hd6301_init();
    if (!pram) {
        printf("Failed to initialise HD6301\n");
        watchdog_reboot(0, 0, 0);
    }
    const size_t PRAM_SIZE = 8192;
    if (ROMBASE + rom_HD6301V1ST_img_len > PRAM_SIZE) {
        printf("ROM too large for PRAM!\n");
        watchdog_reboot(0, 0, 0);
    }
    memcpy(pram + ROMBASE, rom_HD6301V1ST_img, rom_HD6301V1ST_img_len);
    printf("HD6301 initialized\r\n");
}

void core1_entry() {
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

int main() {

    printf("Starting system at 150 MHz...\r\n");

    if (!tusb_init()) {
        printf("TinyUSB init failed!\r\n");
        watchdog_reboot(0, 0, 0);
    }

    UserInterface ui;
    ui.init();
    ui.update();
    printf("User interface initialized\r\n");

    if (!set_sys_clock_khz(DEFAULT_CPU_CLOCK_KHZ, false)) {
        printf("Clock set failed, using default.\r\n");
    } else {
        printf("System clock set to %d MHz\r\n", DEFAULT_CPU_CLOCK_KHZ / 1000);
    }

    SerialPort::instance().open();
    SerialPort::instance().set_ui(ui);
    printf("Serial port initialized\r\n");

    HidInput::instance().reset();
    HidInput::instance().set_ui(ui);
    printf("HID input initialized\r\n");

    sleep_ms(100);
	
    multicore_launch_core1(core1_entry);
    printf("Multicore initialized\r\n");

    // Overclock to 250 MHz after init
    printf("Boosting clock to 250 MHz...\r\n");
    if (!set_sys_clock_khz(250000, true)) {
        printf("Overclock failed, keeping 150 MHz\r\n");
    } else {
        printf("System clock boosted to 250 MHz\r\n");
    }
	
    // Reinitialize peripherals after overclock
    i2c_init(SSD1306_I2C, 400000);
    uart_init(UART_DEVICE, 115200);
    tusb_init(); // Reset TinyUSB for stability
    printf("Peripherals reinitialized after overclock\r\n");
	
    stdio_init_all();

    HidInput::instance().force_usb_mouse();

    absolute_time_t ten_ms = get_absolute_time();
    watchdog_enable(5000, true);

    while (true) {
        watchdog_update();
        absolute_time_t tm = get_absolute_time();
        handle_rx_from_st();
        AtariSTMouse::instance().update();

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