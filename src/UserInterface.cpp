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
#include "UserInterface.h"
#include "UserInterface_i18n.h"
#include "version.h"
#include "pico/stdlib.h"
#include "hid_app_host.h"
#include "config.h"
#include "hardware/clocks.h"

#define DEBOUNCE_COUNT 10


static int lang_idx = 0;

enum BUTTONS {
    BUTTON_LEFT,
    BUTTON_MIDDLE,
    BUTTON_RIGHT
};

UserInterface::UserInterface() {
}

ssd1306_t   disp;

void UserInterface::init() {
    // Setup the I2C interface to the display
    i2c_init(SSD1306_I2C, 400000);
    gpio_set_function(SSD1306_SDA, GPIO_FUNC_I2C);
    gpio_set_function(SSD1306_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SSD1306_SDA);
    gpio_pull_up(SSD1306_SCL);

    // Initialise the display library
    ssd1306_init(&disp, SSD1306_WIDTH, SSD1306_HEIGHT, SSD1306_ADDR, SSD1306_I2C);

    // Setup GPIO for buttons
    btn_gpio[0] = GPIO_BUTTON_LEFT;
    btn_gpio[1] = GPIO_BUTTON_MIDDLE;
    btn_gpio[2] = GPIO_BUTTON_RIGHT;
    for (int i = 0; i < 3; ++i) {
        gpio_init(btn_gpio[i]);
        gpio_set_dir(btn_gpio[i], GPIO_IN);
        gpio_pull_up(btn_gpio[i]);
    }

    // Read from NV storage
    int mouse_speed = settings.get_settings().mouse_speed;
    if (mouse_speed < MOUSE_MIN) {
        mouse_speed = MOUSE_MIN;
        settings.get_settings().mouse_speed = mouse_speed;
    }
    if (mouse_speed > MOUSE_MAX) {
        mouse_speed = MOUSE_MAX;
        settings.get_settings().mouse_speed = mouse_speed;
    }

	lang_idx = settings.get_settings().language_index;

    serial_tm = get_absolute_time();
	debug_usb_tm = get_absolute_time();

}

void UserInterface::usb_connect_state(int kb, int mouse, int joy) {
    if ((num_kb != kb) || (num_mouse != mouse) || (num_joy != joy)) {
        dirty = true;
    }
    num_kb = kb;
    num_mouse = mouse;
    num_joy = joy;
}

int8_t UserInterface::get_mouse_speed() {
    return settings.get_settings().mouse_speed;
}

uint8_t UserInterface::get_joystick() {
    return settings.get_settings().joy_device;
}

uint8_t UserInterface::get_mouse_enabled() {
    return settings.get_settings().mouse_enabled;
}

void UserInterface::set_mouse_enabled(uint8_t en) {
    settings.get_settings().mouse_enabled = en;
    settings.write();
    dirty = true;
}

void UserInterface::update_serial() {
    uint8_t y = 0;
    ssd1306_clear(&disp);
    for (auto it : serial_lines) {
        ssd1306_draw_string(&disp, 0, y, 1, (char*)it.c_str());
        y += 9;
    }
    ssd1306_draw_string(&disp, 24, 27, 1, (char*)"ST <-> Kbd");

	ssd1306_draw_string(&disp, 34, 0, 1, (char*)"V " PROJECT_VERSION_STRING);	    
}

void UserInterface::update_status() {
    char buf[32];
    // stdio_init_all();
    // Get the current CPU frequency
    uint32_t cpu_freq = clock_get_hz(clk_sys);
    //uint32_t cpu_freq = 123;
    ssd1306_clear(&disp);
    sprintf(buf, "%s %d", get_translation(KEY_USB_KEYBOARD, lang_idx), num_kb);
    ssd1306_draw_string(&disp, 0, 0, 1,  buf);
    sprintf(buf, "%s %d", get_translation(KEY_USB_MOUSE, lang_idx), num_mouse);
    ssd1306_draw_string(&disp, 0, 9, 1,  buf);
    sprintf(buf, "%s %d", get_translation(KEY_USB_JOYSTICK, lang_idx), num_joy);
    ssd1306_draw_string(&disp, 0, 18, 1, buf);
    sprintf(buf, "%s", get_translation(settings.get_settings().mouse_enabled ? KEY_MOUSE_ENABLED : KEY_JOY0_ENABLED, lang_idx));
    ssd1306_draw_string(&disp, 0, 27, 1, buf);
    sprintf(buf, "CPU: %.2f MHz", static_cast<double>(cpu_freq) / 1000000.0);
    ssd1306_draw_string(&disp, 0, 36, 1, buf);
}

void UserInterface::set_cpu_speed(uint32_t khz) {
    set_sys_clock_khz(khz, false);
    dirty = true;
}

void UserInterface::update_mouse() {
    char buf[32];
    ssd1306_draw_string(&disp, 0, 45, 1, get_translation(KEY_MOUSE_SPEED, lang_idx));
    sprintf(buf, "[==============]");
    buf[settings.get_settings().mouse_speed - MOUSE_MIN] = '*';
    ssd1306_draw_string(&disp, 0, 54, 1, buf);
}

void UserInterface::update_joy(int index) {
    char buf[32];
    sprintf(buf, "Joy %d: %s", index, (settings.get_settings().joy_device & (1 << index)) ? "DSub" : "USB");
    ssd1306_draw_string(&disp, 0, 54, 1, buf);
}

void UserInterface::update_splash() {
    ssd1306_clear(&disp);

    // ATARI text (centered)
    ssd1306_draw_string(&disp, 22, 0, 2, (char*)"EIFFEL");
    ssd1306_draw_string(&disp, 28, 25, 1, (char*)"Pico - USB");
    ssd1306_draw_string(&disp, 40, 40, 1, (char*)"Adapter");

    // Version number at bottom
    ssd1306_draw_string(&disp, 42, 55, 1, (char*)"v" PROJECT_VERSION_STRING);
}

void UserInterface::update_usb_debug() {
    char buf[32];
    ssd1306_clear(&disp);
    
    // Title
    ssd1306_draw_string(&disp, 0, 0, 1, (char*)"USB Debug Info");
    
    // Device counts
    sprintf(buf, "KB:%d Ms:%d Joy:%d", num_kb, num_mouse, num_joy);
    ssd1306_draw_string(&disp, 0, 12, 1, buf);
    
    // Mount and active device tracking
    uint32_t addr_inst = hid_debug_get_last_addr_inst();
    sprintf(buf, "Mnts:%lu Active:%lu", 
        hid_debug_get_mount_calls(),
        hid_debug_get_active_devices());
    ssd1306_draw_string(&disp, 0, 24, 1, buf);
    
    // Last device address and instance
    sprintf(buf, "Last:Ad:%d Inst:%d", 
        (addr_inst >> 8) & 0xFF,
        addr_inst & 0xFF);
    ssd1306_draw_string(&disp, 0, 36, 1, buf);
    
    // Report statistics
    sprintf(buf, "Reports Rx:%lu", hid_debug_get_report_calls());
    ssd1306_draw_string(&disp, 0, 48, 1, buf);
    
    sprintf(buf, "Reports Copy:%lu", hid_debug_get_report_copied());
    ssd1306_draw_string(&disp, 0, 56, 1, buf);
}

void UserInterface::update_language() {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, 0, 1, get_translation(KEY_LANGUAGE, lang_idx));
    const char* lang = languages[lang_idx];
    ssd1306_draw_string(&disp, 0, 30, 2, (char*)lang);
}

void UserInterface::handle_buttons() {
    for (int i = 0; i < 3; ++i) {
        bool state = gpio_get(btn_gpio[i]);
        if (!state) {
            // The <= means we go one past DEBOUNCE_COUNT and latch there until the button is released.
            if (btn_count[i] <= DEBOUNCE_COUNT) {
                if (++btn_count[i] == DEBOUNCE_COUNT) {
                    on_button_down(i);
                }
            }
        }
        else {
            btn_count[i] = 0;
        }
    }
}

void UserInterface::toggle_joystick_source(uint8_t joystick_num) {
    if (joystick_num > 1) return;  // Only support Joy0 and Joy1
    
    // Toggle the joystick device bit (D-SUB <-> USB)
    settings.get_settings().joy_device ^= (1 << joystick_num);
    settings.write();
    dirty = true;
}

void UserInterface::on_button_down(int i) {
    // Middle button changes page
    if (i == BUTTON_MIDDLE) {
        int pg = (int)page;
        pg = ((pg + 1) % (PAGE_USB_DEBUG + 1)); // Mention here what should be the last PAGE of the Enum
        page = (PAGE)pg;
        dirty = true;
    }
    else if (i == BUTTON_LEFT) {
        if (page == PAGE_MOUSE) {
            if (settings.get_settings().mouse_speed > MOUSE_MIN) {
                --settings.get_settings().mouse_speed;
                settings.write();
                dirty = true;
            }
        }
        else if ((page == PAGE_JOY0) || (page == PAGE_JOY1)) {
            settings.get_settings().joy_device ^= (1 << (page - PAGE_JOY0));
            settings.write();
            dirty = true;
        }
		else if (page == PAGE_LANGUAGE) {
			lang_idx = (lang_idx - 1 + NUM_LANGUAGES) % NUM_LANGUAGES;
			settings.get_settings().language_index = lang_idx;
			settings.write();
			dirty = true;
		}
    }
    else if (i == BUTTON_RIGHT) {
        if (page == PAGE_MOUSE) {
            if (settings.get_settings().mouse_speed < MOUSE_MAX) {
                ++settings.get_settings().mouse_speed;
                settings.write();
                dirty = true;
            }
        }
        else if ((page == PAGE_JOY0) || (page == PAGE_JOY1)) {
            settings.get_settings().joy_device ^= (1 << (page - PAGE_JOY0));
            settings.write();
            dirty = true;
        }
		else if (page == PAGE_LANGUAGE) {
			lang_idx = (lang_idx + 1) % NUM_LANGUAGES;
			settings.get_settings().language_index = lang_idx;
			settings.write();
			dirty = true;
		}
    }
}

void UserInterface::update() {
    handle_buttons();

    if (dirty) {
        dirty = false;

        if (page == PAGE_MOUSE) {
            update_status();
            update_mouse();
        }
        else if (page == PAGE_JOY0) {
            update_status();
            update_joy(0);
        }
        else if (page == PAGE_JOY1) {
            update_status();
            update_joy(1);
        }
        else if (page == PAGE_SERIAL) {
            absolute_time_t tm = get_absolute_time();
            if (absolute_time_diff_us(serial_tm, tm) >= (500 * 1000)) {
                serial_tm = tm;
                update_serial();
            }
            else {
                // Not time yet
                dirty = true;
            }
        }
        else if (page == PAGE_SPLASH) {
            update_splash();
        }
        else if (page == PAGE_USB_DEBUG) {
			absolute_time_t tm = get_absolute_time();
            if (absolute_time_diff_us(debug_usb_tm, tm) >= (500 * 1000)) {
                debug_usb_tm = tm;
                update_usb_debug();
				ssd1306_show(&disp);
				// Keep refreshing debug page every 500ms
				dirty=true;
			}
        }
		else if (page == PAGE_LANGUAGE) {
			update_language();
		}		
        if (!dirty) {
            ssd1306_show(&disp);
        }
    }
}

void UserInterface::serial(bool send, uint8_t data) {
    char buf[32];
    sprintf(buf, "%s%02X", send ? "              " : "", data);

    serial_lines.push_back(std::string(buf));
    while (serial_lines.size() > 7) {
        serial_lines.pop_front();
    }
    if (page == PAGE_SERIAL) {
        dirty = true;
    }
}
