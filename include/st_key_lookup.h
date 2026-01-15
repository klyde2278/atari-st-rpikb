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
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ST_KEY_LOOKUP_SIZE 128

  extern const uint8_t st_key_lookup_hid_cz_cz[ST_KEY_LOOKUP_SIZE];
  extern const uint8_t st_key_lookup_hid_de_ch[ST_KEY_LOOKUP_SIZE];
  extern const uint8_t st_key_lookup_hid_de_de[ST_KEY_LOOKUP_SIZE];
  extern const uint8_t st_key_lookup_hid_dk_dk[ST_KEY_LOOKUP_SIZE];
  extern const uint8_t st_key_lookup_hid_en_uk[ST_KEY_LOOKUP_SIZE];
  extern const uint8_t st_key_lookup_hid_en_us[ST_KEY_LOOKUP_SIZE];
  extern const uint8_t st_key_lookup_hid_es_es[ST_KEY_LOOKUP_SIZE];
  extern const uint8_t st_key_lookup_hid_fi_fi[ST_KEY_LOOKUP_SIZE]; 
  extern const uint8_t st_key_lookup_hid_fr_ch[ST_KEY_LOOKUP_SIZE];
  extern const uint8_t st_key_lookup_hid_fr_fr[ST_KEY_LOOKUP_SIZE];
  extern const uint8_t st_key_lookup_hid_hu_hu[ST_KEY_LOOKUP_SIZE];
  extern const uint8_t st_key_lookup_hid_it_it[ST_KEY_LOOKUP_SIZE];
  extern const uint8_t st_key_lookup_hid_nl_nl[ST_KEY_LOOKUP_SIZE];
  extern const uint8_t st_key_lookup_hid_no_no[ST_KEY_LOOKUP_SIZE];
  extern const uint8_t st_key_lookup_hid_pl_pl[ST_KEY_LOOKUP_SIZE];
  extern const uint8_t st_key_lookup_hid_se_se[ST_KEY_LOOKUP_SIZE];

#ifdef __cplusplus
}
#endif
