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

#include "NVSettings.h"
#include "pico/multicore.h"
#include <hardware/sync.h>
#include <string.h>

// The PICO has 2Mb of flash storage. We will assume the code will not be taking
// all of this and carve 4K out near the end
#define FLASH_LOCATION  (0x200000-FLASH_SECTOR_SIZE)

static union {
    Settings settings;
    uint8_t  raw[FLASH_SECTOR_SIZE];
} storage;

NVSettings::NVSettings() {
    read();
}

Settings& NVSettings::get_settings() {
    return storage.settings;
}

void NVSettings::write() {
    // While the sector is erased/programmed the flash is unreadable, so no
    // code may execute from XIP. Core 1 runs the HD6301 emulation from
    // flash: park it in its multicore-lockout RAM handler for the duration.
    // Boot-time migration writes happen before core 1 is launched; the
    // victim check makes them skip the (otherwise blocking) lockout.
    // NOTE: the write costs ~50 ms with interrupts off on this core; callers
    // should defer/coalesce writes (see UserInterface::schedule_settings_write).
    bool lockout = multicore_lockout_victim_is_initialized(1);
    if (lockout) multicore_lockout_start_blocking();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_LOCATION, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_LOCATION, &storage.raw[0], FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
    if (lockout) multicore_lockout_end_blocking();
}

void NVSettings::read() {
    // Copy from flash to our structure
    memcpy(&storage.raw[0], (void*)(XIP_BASE + FLASH_LOCATION), FLASH_SECTOR_SIZE);
    // Make sure flash contained a valid structure
    if (storage.settings.version != 1) {
        memset(&storage.raw[0], 0, FLASH_SECTOR_SIZE);
        storage.settings.version = 1;
        storage.settings.mouse_enabled = 1;         // Default: USB mouse enabled
		storage.settings.language_index = 0;        // Default language (EN)
		storage.settings.keyboard_layout_index = 0; // Default keyboard layout (CZ)
        storage.settings.joystick_dead_zone = 0x08; // Default 0x08
        write();
    }
    else {
		 // MIGRATION / SANITY: if the new field is uninitialized (0xFF), set a safe default
		 // Note: on previously-written v1 settings, 'keyboard_layout_index' didn't exist yet
		 // and may read back as 0xFF from flash. Fix it once and persist.
		 if (storage.settings.keyboard_layout_index == 0xFF) {
		   storage.settings.keyboard_layout_index = 0; // CZ by default
		   write();
		 }
		// MIGRATION / SANITY: if the new field is uninitialized (0xFF), set a safe default
		if (storage.settings.joystick_dead_zone == 0xFF) {
			storage.settings.joystick_dead_zone = 0x08;
			write();
		}

		// MIGRATION: autofire fields — default to STANDBY OFF, 8 Hz
		bool af_dirty = false;
		if (storage.settings.autofire_mode_joy0 == 0xFF) {
			storage.settings.autofire_mode_joy0 = 0; af_dirty = true;
		}
		if (storage.settings.autofire_mode_joy1 == 0xFF) {
			storage.settings.autofire_mode_joy1 = 0; af_dirty = true;
		}
		if (storage.settings.autofire_rate_joy0 == 0 || storage.settings.autofire_rate_joy0 == 0xFF) {
			storage.settings.autofire_rate_joy0 = 8; af_dirty = true;
		}
		if (storage.settings.autofire_rate_joy1 == 0 || storage.settings.autofire_rate_joy1 == 0xFF) {
			storage.settings.autofire_rate_joy1 = 8; af_dirty = true;
		}
		if (af_dirty) write();

		// MIGRATION: key_remap — zero-fill if uninitialized (0xFF from blank flash).
		// UserInterface::init() will copy the active layout table on first valid boot.
		if (storage.settings.key_remap[4] == 0xFF) {
			memset(storage.settings.key_remap, 0, sizeof(storage.settings.key_remap));
			write();
		}
   }
}

