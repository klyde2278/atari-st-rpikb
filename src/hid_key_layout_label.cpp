/*
 * Atari ST RP2040 IKBD Emulator
 * Copyright (C) 2026 Emmanuel Barraud
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

// Direct USB HID keycode -> physical key label per layout.
// Completely independent of the Atari ST scancode system.
//
// All 16 layouts share the same HID->ST position mapping (verified from
// st_key_lookup_hid_*.cpp files). Layout differences are expressed here as
// sparse override tables indexed by HID keycode, falling back to the EN-US
// baseline for any code not listed.
//
// Arrow-key labels reuse the custom font glyphs already used elsewhere in
// the UI: 0x83=up  0x84=down  0x85=left  0x86=right.
//
// USB HID digit row (0x1E..0x27) -> ST scancodes 2..11 (same in all layouts).
// HID letter positions (0x04..0x1D) -> same ST positions as EN-US in every
// layout; AZERTY/QWERTZ label differences are captured in the override tables.

#include "hid_key_layout_label.h"
#include <stddef.h>

struct HidLabelEntry {
    uint8_t     hid;
    const char* label;
};

// ---------------------------------------------------------------------------
// EN-US baseline — covers every HID code that can appear in the remap UI.
// Custom font arrow glyphs: 0x83=up 0x84=down 0x85=left 0x86=right.
// ---------------------------------------------------------------------------
static const char* hid_en_us_label(uint8_t hid) {
    switch (hid) {
        // Letters
        case 0x04: return "A";   case 0x05: return "B";   case 0x06: return "C";
        case 0x07: return "D";   case 0x08: return "E";   case 0x09: return "F";
        case 0x0A: return "G";   case 0x0B: return "H";   case 0x0C: return "I";
        case 0x0D: return "J";   case 0x0E: return "K";   case 0x0F: return "L";
        case 0x10: return "M";   case 0x11: return "N";   case 0x12: return "O";
        case 0x13: return "P";   case 0x14: return "Q";   case 0x15: return "R";
        case 0x16: return "S";   case 0x17: return "T";   case 0x18: return "U";
        case 0x19: return "V";   case 0x1A: return "W";   case 0x1B: return "X";
        case 0x1C: return "Y";   case 0x1D: return "Z";
        // Digits
        case 0x1E: return "1";   case 0x1F: return "2";   case 0x20: return "3";
        case 0x21: return "4";   case 0x22: return "5";   case 0x23: return "6";
        case 0x24: return "7";   case 0x25: return "8";   case 0x26: return "9";
        case 0x27: return "0";
        // Structural keys (labeled but never remappable)
        case 0x28: return "Ent"; case 0x29: return "Esc"; case 0x2A: return "BSp";
        case 0x2B: return "Tab";
        // Punctuation / symbols
        case 0x2C: return "Sp";
        case 0x2D: return "-";   case 0x2E: return "=";
        case 0x2F: return "[";   case 0x30: return "]";
        case 0x31: return "\\";  case 0x32: return "#";
        case 0x33: return ";";   case 0x34: return "'";
        case 0x35: return "`";   case 0x36: return ",";
        case 0x37: return ".";   case 0x38: return "/";
        // CapsLock / F-keys
        case 0x39: return "CLk";
        case 0x3A: return "F1";  case 0x3B: return "F2";  case 0x3C: return "F3";
        case 0x3D: return "F4";  case 0x3E: return "F5";  case 0x3F: return "F6";
        case 0x40: return "F7";  case 0x41: return "F8";
        case 0x42: return "F9";  case 0x43: return "F10";
        case 0x44: return "F11"; case 0x45: return "F12";
        // PrintScreen / ScrollLock (mapped to Help/Undo in firmware)
        case 0x46: return "Prt"; case 0x47: return "SLk";
        // Navigation cluster
        case 0x49: return "Ins"; case 0x4A: return "Hom";
        case 0x4B: return "PgU"; case 0x4C: return "Del";
        case 0x4D: return "End"; case 0x4E: return "PgD";
        // Arrow keys — custom font glyphs
        case 0x4F: return "\x86"; // right
        case 0x50: return "\x85"; // left
        case 0x51: return "\x84"; // down
        case 0x52: return "\x83"; // up
        // Numpad
        case 0x53: return "NLk";
        case 0x54: return "K/";  case 0x55: return "K*";
        case 0x56: return "K-";  case 0x57: return "K+";
        case 0x58: return "KEn";
        case 0x59: return "K1";  case 0x5A: return "K2";  case 0x5B: return "K3";
        case 0x5C: return "K4";  case 0x5D: return "K5";  case 0x5E: return "K6";
        case 0x5F: return "K7";  case 0x60: return "K8";  case 0x61: return "K9";
        case 0x62: return "K0";  case 0x63: return "K.";
        // ISO extra key (left of Z on ISO keyboards)
        case 0x64: return "<";
        default:   return "?";
    }
}

// ---------------------------------------------------------------------------
// Per-layout sparse override tables (entries differing from EN-US baseline).
// HID code derivation: all layouts share the same HID->ST position mapping
// as EN-US, so each override {hid, label} matches the EN-US HID position
// for that physical key.
// ---------------------------------------------------------------------------

/*
   ²      1      2      3      4      5      6      7      8      9      0      -      =    Backspace
| 0x35 | 0x1E | 0x1F | 0x20 | 0x21 | 0x22 | 0x23 | 0x24 | 0x25 | 0x26 | 0x27 | 0x2D | 0x2E | 0x2A | 

   Tab    Q      W      E      R      T      Y      U      I      O      P      [      ]      \ 
| 0x2B | 0x14 | 0x1A | 0x08 | 0x15 | 0x17 | 0x1C | 0x18 | 0x0C | 0x12 | 0x13 | 0x2F | 0x30 | 0x31 |

  Caps    A     S      D      F      G      H      J      K      L      ;      '      #    Enter
| 0x39| 0x04 | 0x16 | 0x07 | 0x09 | 0x0A | 0x0B | 0x0D | 0x0E | 0x0F | 0x33 | 0x34 | 0x32 | 0x28 |

 LShift  <  >    Z      X      C      V      B      N      M      ,      .     /     RShift
| 0xE1 | 0x64 | 0x1D | 0x1B | 0x06 | 0x19 | 0x05 | 0x11 | 0x10 | 0x36 | 0x37 | 0x38 | 0xE5| 

  LCtrl  LGUI   LAlt   Space  RAlt  Menu   RCtrl
| 0xE0 | 0xE3 | 0xE2 | 0x2C | 0xE6| 0x65 | 0xE4 |

*/

// CZ-CZ (QWERTZ) — layout index 0
static const HidLabelEntry k_hid_cz_cz[] = {
    // Digit row
    { 0x35, ";"    }, { 0x1E, "+"    }, { 0x1F, "\x89" }, { 0x20, "\x8A" }, { 0x21, "\x8B" },
    { 0x22, "\x8C" }, { 0x23, "\x8D" }, { 0x24, "ý"    }, { 0x25, "á"    },{ 0x26, "í"    }, { 0x27, "é"    },
    // Special
    { 0x2D, "="    }, { 0x2E, "\xB4"    },
    // QWERTZ Y/Z swap
    { 0x1C, "Z"    },
    // Brackets / punctuation row
    { 0x2F, "\xFA" }, { 0x30, ")" },
    { 0x31, "\x8E" }, { 0x33, "\x91" }, { 0x34, "\xA7" }, { 0x32, "\xA8" }, 
    { 0x64, "\x5C" }, { 0x1D, "Y"    }, { 0x38, "-"    }, 
    { 0x00, NULL   }
};

// CH-DE (Swiss German, QWERTZ) — layout index 1
static const HidLabelEntry k_hid_ch_de[] = {
    { 0x35, "\xA7" }, { 0x2D, "'"    }, { 0x2E, "^"    },
    { 0x1C, "Z"    }, 
    { 0x2F, "\xFC" }, { 0x30, "\xA8" }, // ü ¨
    { 0x33, "\xF6" }, { 0x34, "\xE4" }, { 0x32, "$"    },  // ö ä $ 
    { 0x64, "<"    }, { 0x1D, "Y"    }, { 0x38, "-"    }, 
    { 0x00, NULL   }
};

// DE-DE (German, QWERTZ) — layout index 2
// Adds ß and ´ which are absent from the ST label table.
static const HidLabelEntry k_hid_de_de[] = {
    { 0x35, "^"    }, { 0x2D, "\xDF" }, { 0x2E, "\xB4" }, // ß ´
    { 0x1C, "Z"    }, 
    { 0x2F, "ü"    }, { 0x30, "+"    }, // ü +
    { 0x33, "\xF6" }, { 0x34, "\xE4" }, { 0x32, "#"    }, // ö ä #
    { 0x64, "<"    }, { 0x1D, "Y"    }, { 0x38, "-"    }, 
    { 0x00, NULL   }
};

// DK-DK (Danish) — layout index 3
static const HidLabelEntry k_hid_dk_dk[] = {
    { 0x35, "\xBD" }, { 0x2D, "+"    }, { 0x2E, "\xB4" }, // ½ + ´
    { 0x2F, "\xE5" }, { 0x30, "¨"    }, // å
    { 0x33, "\xE6" }, { 0x34, "\xF8" }, { 0x32, "'"    }, // æ ø
    { 0x64, "<"    }, { 0x38, "-"    },
    { 0x00, NULL   }
};

// EN-UK (United Kingdom) — layout index 4
static const HidLabelEntry k_hid_en_uk[] = {
    { 0x35, "\x60" }, // `
    { 0x64, "\x5C" }, // "\"
    { 0x00, NULL   }
};

// CH-FR (Swiss French, QWERTZ) — layout index 8
static const HidLabelEntry k_hid_ch_fr[] = {
    { 0x35, "\xA7" },{ 0x2D, "'"    }, { 0x2E, "^"    }, // § ' ^ 
    { 0x1C, "Z"    }, 
    { 0x2F, "\xE8" }, { 0x30, "\xA8" }, // è ¨
    { 0x33, "\xE9" }, { 0x34, "\xE0" }, { 0x32, "$"    }, // é à $
    { 0x64, "<"    }, { 0x1D, "Y"    }, { 0x38, "-"    }, 
    { 0x00, NULL   }
};

// FR-FR (AZERTY) — layout index 9
// Letters: Q/A, W/Z swapped; M at ; position; comma at M position.
static const HidLabelEntry k_hid_fr_fr[] = {
    // AZERTY letter remapping
    { 0x04, "Q"    }, { 0x14, "A"    }, // A-pos->Q, Q-pos->A
    { 0x1A, "Z"    }, { 0x1D, "W"    }, // W-pos->Z, Z-pos->W
    { 0x10, ","    }, { 0x33, "M"    }, // M-pos->,, ;-pos->M
    // Digit row
    { 0x35, "\xB2" }, { 0x1E, "&"    }, { 0x1F, "é"    }, { 0x20, "\""   }, { 0x21, "'"    },
    { 0x22, "("    }, { 0x23, "-"    }, { 0x24, "è"    }, { 0x25, "_"    }, { 0x26, "ç"    },
	{ 0x27, "à"    }, { 0x2D, ")"    }, { 0x2E, "="    },
    // Special
    { 0x2F, "^"    }, { 0x30, "$"    },
    { 0x34, "ù"    }, { 0x32, "*"    }, 
    { 0x64, "<"    }, { 0x36, ";"    }, { 0x37, ":"    }, { 0x38, "!"    },
    { 0x00, NULL   }
};

// ES-ES (Spanish) — layout index 6
static const HidLabelEntry k_hid_es_es[] = {
    { 0x35, "\xB0" }, { 0x2D, "'"    }, { 0x2E, "\xA1" }, // ° ' ¡
    { 0x2F, "\x60" }, { 0x30, "+"    }, // `
    { 0x33, "\xF1" }, { 0x34, "\xB4" }, { 0x32, "ç"    }, // ñ ´ ç
    { 0x64, "<"    }, { 0x38, "-"    }, 
    { 0x00, NULL   }
};

// FI-FI (Finnish) and SE-SE (Swedish) share the same override table.
// Layout index 8 & 15
static const HidLabelEntry k_hid_se_fi[] = {
    { 0x35, "\xA7" }, { 0x2D, "+"    }, { 0x2E, "\xB4" }, // § + ´
    { 0x2F, "\xE5" }, { 0x30, "¨"    }, // å ¨
    { 0x33, "\xF6" }, { 0x34, "\xE4" }, { 0x32, "'"    }, // ö ä '
    { 0x64, "<"    }, { 0x38, "-"    }, 
    { 0x00, NULL   }
};

// HU-HU (Hungarian, QWERTZ) — layout index 10
static const HidLabelEntry k_hid_hu_hu[] = {
    { 0x35, "0"    }, { 0x27, "\xF6" }, { 0x2D, "\xFC" }, { 0x2E, "\xF3" }, // ö ü ó
    { 0x1C, "Z"    }, { 0x2F, "\xF0" }, { 0x30, "\xFA" }, // ð ú
    { 0x33, "é"    }, { 0x34, "\xE1" }, { 0x32, "ü"    }, // é á ü
    { 0x64, "\xED" }, { 0x1D, "Y"    }, { 0x38, "-"    }, // í at ISO key
    { 0x00, NULL   }
};

// IT-IT (Italian) — layout index 11
static const HidLabelEntry k_hid_it_it[] = {
    { 0x35, "\x5C" }, { 0x2D, "'"    }, { 0x2E, "\xEC" }, // ' ì ù
    { 0x2F, "\xE8" }, { 0x30, "+"    }, // è +
    { 0x33, "\xF2" }, { 0x34, "\xE0" }, { 0x32, "\xF9" }, // ò à ù
    { 0x64, "<"    }, { 0x38, "-"    }, 
    { 0x00, NULL   }
};

// NL-NL (Dutch) — layout index 12
static const HidLabelEntry k_hid_nl_nl[] = {
    { 0x35, "@"    }, { 0x2D, "/"    }, { 0x2E, "\xB0" }, // °
	{ 0x2F, "¨"    }, { 0x30, "*"    },
    { 0x33, "+"    }, { 0x34, "\xB4" }, { 0x32, "<"    }, // ´
    { 0x64, "]"    }, { 0x38, "-"    }, 
    { 0x00, NULL   }
};

// NO-NO (Norwegian) — layout index 13
static const HidLabelEntry k_hid_no_no[] = {
    { 0x35, "\x7C" }, { 0x2D, "+"    }, { 0x2E, "\x5C" }, // | + "\"
    { 0x2F, "\xE5" }, { 0x30, "¨"    }, // å ü
    { 0x33, "\xF8" }, { 0x34, "\xE6" }, { 0x32, "'"    }, // ø æ
    { 0x64, "<"    }, { 0x38, "-"    }, 
    { 0x00, NULL   }
};

// PL-PL (Polish) — layout index 14
static const HidLabelEntry k_hid_pl_pl[] = {
    { 0x35, "\x93" }, { 0x2D, "+"    }, { 0x2E, "'" }, // , 
    { 0x1C, "Z"    }, { 0x2F, "\x94" }, { 0x30, "\x95"    },
    { 0x33, "\x96" }, { 0x34, "\x97" }, { 0x32, "\xF3"    },
    { 0x64, "<"    }, { 0x38, "-"    }, 
    { 0x00, NULL   }
};

// ---------------------------------------------------------------------------
// Master override table indexed by layout_idx.
// ---------------------------------------------------------------------------
static const HidLabelEntry* const k_hid_overrides[16] = {
    k_hid_cz_cz,  // 0  CZ-CZ
    k_hid_ch_de,  // 1  DE-CH
	k_hid_ch_fr,  // 2  FR-CH
    k_hid_de_de,  // 3  DE-DE
    k_hid_dk_dk,  // 4  DK-DK
    k_hid_en_uk,  // 5  EN-UK
    NULL,         // 6  EN-US  (baseline only)
    k_hid_es_es,  // 7  ES-ES
    k_hid_se_fi,  // 8  FI-FI
    k_hid_fr_fr,  // 9  FR-FR
    k_hid_hu_hu,  // 10 HU-HU
    k_hid_it_it,  // 11 IT-IT
    k_hid_nl_nl,  // 12 NL-NL
    k_hid_no_no,  // 13 NO-NO
    k_hid_pl_pl,  // 14 PL-PL
    k_hid_se_fi,  // 15 SE-SE
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

const char* get_hid_layout_label(uint8_t hid, int layout_idx) {
    if (layout_idx >= 0 && layout_idx < 16) {
        const HidLabelEntry* tbl = k_hid_overrides[layout_idx];
        if (tbl) {
            for (; tbl->label; ++tbl) {
                if (tbl->hid == hid) return tbl->label;
            }
        }
    }
    return hid_en_us_label(hid);
}

bool hid_is_system_reserved(uint8_t hid) {
    switch (hid) {
        case 0x28: // Enter
        case 0x29: // Esc
        case 0x2A: // Backspace
        case 0x2B: // Tab
        case 0x39: // CapsLock
        case 0x42: // F9  — Ctrl+F9: Joy0 source toggle
        case 0x43: // F10 — Ctrl+F10: Joy1 source toggle
        case 0x44: // F11 — Ctrl+F11: IKBD reset
        case 0x45: // F12 — Ctrl+F12: mouse/joy mode toggle
        case 0x53: // NumLock
        case 0x56: // KP- — Alt+KP-: set 150 MHz
        case 0x57: // KP+ — Alt+KP+: set 270 MHz
            return true;
        default:
            return false;
    }
}
