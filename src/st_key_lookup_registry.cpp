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
// Base per-locale tables you already compile:
extern const uint8_t st_key_lookup_hid_en_us[ST_KEY_LOOKUP_SIZE];
extern const uint8_t st_key_lookup_hid_en_uk[ST_KEY_LOOKUP_SIZE];
extern const uint8_t st_key_lookup_hid_fr_fr[ST_KEY_LOOKUP_SIZE];
}

// ---------- Global overlay (applied to ALL layouts) ----------
static uint8_t overlay_global[ST_KEY_LOOKUP_SIZE] = {0};

// ---------- Per-layout overlays (optional) ----------
static uint8_t overlay_none[ST_KEY_LOOKUP_SIZE] = {0};
static uint8_t overlay_en_us[ST_KEY_LOOKUP_SIZE] = {0};
static uint8_t overlay_en_uk[ST_KEY_LOOKUP_SIZE] = {0};
static uint8_t overlay_fr_fr[ST_KEY_LOOKUP_SIZE] = {0};

// Registry: base table + per-layout overlay
static const LookupEntry g_registry[] = {
  { KeyboardLayout::EN_US, st_key_lookup_hid_en_uk, overlay_en_us },
  { KeyboardLayout::EN_UK, st_key_lookup_hid_en_us, overlay_en_uk },
  { KeyboardLayout::FR_FR, st_key_lookup_hid_fr_fr, overlay_fr_fr },
  // add more layouts here...
};

static const LookupEntry* g_default = &g_registry[0]; // default to EN_UK (or EN_US if you prefer)

const LookupEntry* find_lookup(KeyboardLayout layout) {
  for (auto& it : g_registry)
    if (it.layout == layout) return &it;
  return g_default;
}

void registry_set_overlay(KeyboardLayout layout, const uint8_t* overlay128) {
  // Optional runtime hook if you want to change per-layout overlay on the fly.
  // For simplicity we do not copy; we assume overlay128 points to a stable 128-byte buffer.
  for (auto& it : g_registry) {
    if (it.layout == layout) {
      // const_cast: we know it's a static table, adjust pointer here
      const_cast<uint8_t*&>(it.overlay) = const_cast<uint8_t*>(overlay128);
      return;
    }
  }
}

// Initialize overlays once
static struct OverlayInit {
  OverlayInit() {
    // ---------- Global overlay (for ALL layouts) ----------
    // Map HID PrintScreen (0x46) -> ST Help (98)
    overlay_global[0x46] = 98;
    // Map HID ScrollLock (0x47) -> ST Undo (97)
    overlay_global[0x47] = 97;

    // ---------- Per-layout examples ----------
    // EN_UK: PageUp -> keypad '(' (99), PageDown -> keypad ')' (100)
    overlay_en_uk[0x4B] = 99;
    overlay_en_uk[0x4E] = 100;

    // EN_US: same as EN_UK
    overlay_en_us[0x4B] = 99;
    overlay_en_us[0x4E] = 100;

    // FR_FR: same PageUp/Down + any FR-specific tweaks you want
    overlay_fr_fr[0x4B] = 99;
    overlay_fr_fr[0x4E] = 100;

    // NOTE:
    // - FR_FR specific '-' mapping stays in st_key_lookup_hid_fr_fr.cpp (0x2D -> 53) because it's layout-specific.
    // - Global overlay never sets 0x2D: we keep '-' layout-dependent.
  }
} _overlay_init_;

