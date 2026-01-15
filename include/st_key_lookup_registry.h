/*
 * Atari ST RP2040 IKBD Emulator
 * Copyright (C) 2025 Emmanuel Barraud
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


// st_key_lookup_registry.h
#pragma once
#include <cstdint>
#include "HidInput.h"

// Each table is a 128-byte lookup: HID usage [0..127] -> Atari ST scancode [0..127] (0 = unmapped)
struct LookupEntry {
  KeyboardLayout layout;
  const uint8_t* table;       // base table: st_key_lookup_hid_<locale>[128]
  const uint8_t* overlay;     // per-layout overlay: can be nullptr or 128 bytes (0 means "no override")
};

const LookupEntry* find_lookup(KeyboardLayout layout);

// Optional helpers to set up or tweak overlays at runtime if needed
void registry_set_overlay(KeyboardLayout layout, const uint8_t* overlay128);
