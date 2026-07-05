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

#include "st_key_lookup_registry.h"
#include "st_key_lookup.h"

extern "C" {
    extern const uint8_t st_key_lookup_hid[ST_KEY_LOOKUP_SIZE];
}

static uint8_t overlay_none[ST_KEY_LOOKUP_SIZE] = {0};

static const LookupEntry g_registry[] = {
  { KeyboardLayout::EN_US, st_key_lookup_hid, overlay_none },
};

static const LookupEntry* g_default = &g_registry[0];

const LookupEntry* find_lookup(KeyboardLayout) {
  return g_default;
}

void registry_set_overlay(KeyboardLayout, const uint8_t*) {
  // No-op: single table, no per-layout overlays.
}

