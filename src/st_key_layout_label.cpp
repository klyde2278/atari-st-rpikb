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

#include "st_key_layout_label.h"
#include "HidInput.h"
#include <stddef.h>

// Sparse override table: maps ST scancodes to display labels for a given layout.
// Entries are sorted by scancode for readability; NULL label terminates the table.
// the EN-US fallback from st_scancode_name().
/* Atari ST keyboard decimal scancodes by rows
   / 59 / 60 / 61 / 62 / 63 / 64 / 65 / 66 / 67 / 68 /
   
   | 01 | 02 | 03 | 04 | 05 | 06 | 07 | 08 | 09 | 10 | 11 | 12 | 13 | 41 |  14  |
   |  15  | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 | 25 | 26 | 27 |    | 83 |
   |   29   | 30 | 31 | 32 | 33 | 34 | 35 | 36 | 37 | 38 | 39 | 40  |  28  | 43 |
   |  42  | 96 | 44 | 45 | 46 | 47 | 48 | 49 | 50 | 51 | 52 | 53 |   54   |
            |  56  |                  57                   |  58  |
*/ 

struct LabelEntry {
    uint8_t     sc;
    const char* label;
};
// ---------------------------------------------------------------------------
// DE-DE (QWERTZ) — layout index 2
// ---------------------------------------------------------------------------
static const LabelEntry k_de_de[] = {
    // { 12, "\xDF" }, // ß
    { 21, "Z"    }, { 26, "ü"    }, { 27, "+"    },
    { 39, "ö"    }, { 40, "ä"    }, { 43, "~"    },
    { 96, "<"    }, { 44, "Y"    }, { 53, "-"    },
    {  0, NULL   }
};

// ---------------------------------------------------------------------------
// FR-FR (AZERTY) — layout index 9
// ---------------------------------------------------------------------------
static const LabelEntry k_fr_fr[] = {
    {  2, "&"    }, {  3, "é"    }, {  4, "\""  }, {  5, "'"   },
	{  6, "("    }, {  7, "§"    }, {  8, "è"   }, {  9, "!"   },
    { 10, "ç"    }, { 11, "à"    }, { 12, ")"   }, { 13, "-"   }, { 41, "`"    },
    { 16, "A"    }, { 17, "Z"    }, { 26, "^"   }, { 27, "$"   },
    { 30, "Q"    }, { 39, "M"    }, { 40, "ù"   }, { 43, "#"   },
    { 44, "W"    }, { 50, ","    }, { 51, ";"   }, { 52, ":"   }, { 53, "="    },
	{ 96, "<"    },
    {  0, NULL   }
};

// ---------------------------------------------------------------------------
// EN-UK (United Kingdom, QWERTY) — layout index 4
// ---------------------------------------------------------------------------
static const LabelEntry k_en_uk[] = {
    { 43, "#"    },
	{ 96, "\x5C" },
    {  0, NULL   }
};

// ---------------------------------------------------------------------------
// ES-ES (Spanish) — layout index 6
// ---------------------------------------------------------------------------
static const LabelEntry k_es_es[] = {
    { 41, "ç"    },
	{ 26, "'"    }, { 27, "`" },
	{ 39, "ñ"    }, { 40, ";" },
	{ 96, "<"    }, { 53, "°" },
    {  0, NULL   }
};

// ---------------------------------------------------------------------------
// IT-IT (Spanish) — layout index 11
// ---------------------------------------------------------------------------
static const LabelEntry k_it_it[] = {
    { 12, "'"    }, { 13, "ì"    }, { 41, "ù"    },
    { 26, "è"    }, { 27, "+"    },
	{ 39, "ò"    }, { 40, "à"    },
	{ 96, "<"    }, { 53, "-"    },
    {  0, NULL   }
};

// ---------------------------------------------------------------------------
// SE-SE (Swedish) and FI-FI (Finnish) — layout indices 15 and 7
// ---------------------------------------------------------------------------
static const LabelEntry k_se_fi[] = {
    { 12, "+"    }, { 13, "é"    }, { 41, "'"    },
    { 26, "å"    }, { 27, "ü"    },
    { 39, "ö"    }, { 40, "ä"    },
	{ 96, "<"    }, { 53, "-"    },
    {  0, NULL   }
};

// ---------------------------------------------------------------------------
// FR-CH (Swiss French, QWERTZ) — layout index 8
// ---------------------------------------------------------------------------
static const LabelEntry k_ch_fr[] = {
    { 12, "'"    }, { 13, "^"    }, { 41, "§"    },
	{ 21, "Z"    }, { 26, "è"    }, { 27, "¨"    },
    { 39, "é"    }, { 40, "à"    }, { 43, "$"    },
    { 96, "<"    }, { 44, "Y"    }, { 53, "-"    },
    {  0, NULL   }
};

// ---------------------------------------------------------------------------
// DE-CH (Swiss German, QWERTZ) — layout index 1
// ---------------------------------------------------------------------------
static const LabelEntry k_ch_de[] = {
    { 12, "'"    }, { 13, "^"    }, { 41, "§"    },
	{ 21, "Z"    }, { 26, "ü"    }, { 27, "¨"    },
    { 39, "ö"    }, { 40, "ä"    }, { 43, "$"    },
    { 96, "<"    }, { 44, "Y"    }, { 53, "-"    },
    {  0, NULL   }
};

// ---------------------------------------------------------------------------
// NO-NO (Norwegian) layout index 13
// ---------------------------------------------------------------------------
static const LabelEntry k_no_no[] = {
    { 12, "+"    }, { 13, "é"    }, { 41, "'"   },
    { 26, "å"    }, { 27, "ü"    },
    { 39, "ø"    }, { 40, "æ"    },
	{ 96, "<"    }, { 53, "-"    },
    {  0, NULL   }
};

// ---------------------------------------------------------------------------
// DK-DK (Danish) — layout index 3
// ---------------------------------------------------------------------------
static const LabelEntry k_dk_dk[] = {
    { 12, "+"    }, { 13, "´"    }, { 41, "é"    },
    { 26, "å"    }, { 27, "*"    },
    { 39, "æ"    }, { 40, "ø"    }, { 43, "#"    },
	{ 96, "<"    }, { 53, "-"    },
    {  0, NULL   }
};

// ---------------------------------------------------------------------------
// NL-NL (Deutch) — layout index 12
// ---------------------------------------------------------------------------
static const LabelEntry k_nl_nl[] = {
	{ 43, "#"    },
	{ 96, "\x5C" },
    {  0, NULL   }
};

// ---------------------------------------------------------------------------
// CZ-CZ (Czech) — layout index 0
// ---------------------------------------------------------------------------
static const LabelEntry k_cz_cz[] = {
	{  2, "ó"    }, {  3, "\x88" }, {  4, "\x89" }, {  5, "\x8A" },
    {  6, "\x8B" }, {  7, "\x8C" }, {  8, "ý"    }, {  9, "á"    },
	{ 10, "í"    }, { 11, "é"    }, { 12, "="    }, { 13, "'"    }, { 41, "#"    }, 
	{ 21, "Z"    }, { 26, "\xFA" }, { 27, "\x8D" },
	{ 39, "\x90" }, { 40, "\x8F" }, { 43, "\x8E" },
	{ 96, "<"    }, { 44, "Y"    }, { 53, "-"    },
    {  0, NULL  }
};

// ---------------------------------------------------------------------------
// HU-HU (Hungarian, QWERTZ) — layout index 10
// ---------------------------------------------------------------------------
static const LabelEntry k_hu_hu[] = {
    { 12, "ö"    }, { 13, "ü"    }, { 41, "ó"    },
    { 21, "Z"    }, { 26, "ð"    }, { 27, "ú"    },
	{ 39, "é"    }, { 40, "á"    }, { 43, "\x91" },
	{ 96, "í"    }, { 44, "Y"    }, { 53, "-"    },
    {  0, NULL   }
};

// ---------------------------------------------------------------------------
// Master table indexed by layout index.
// 0=CZ-CZ, 1=DE-CH, 2=DE-DE, 3=DK-DK, 4=EN-UK, 5=EN-US,
// 6=ES-ES, 7=FI-FI, 8=FR-CH, 9=FR-FR, 10=HU-HU, 11=IT-IT,
// 12=NL-NL, 13=NO-NO, 14=PL-PL, 15=SE-SE
// ---------------------------------------------------------------------------
static const LabelEntry* const k_overrides[16] = {
    k_cz_cz,  // 0  CZ-CZ
    k_ch_de,  // 1  DE-CH
	k_ch_fr,  // 2  FR-CH
    k_de_de,  // 3  DE-DE
    k_dk_dk,  // 4  DK-DK
    k_en_uk,  // 5  EN-UK
    NULL,         // 6  EN-US  (baseline only)
    k_es_es,  // 7  ES-ES
    k_se_fi,  // 8  FI-FI
    k_fr_fr,  // 9  FR-FR
    k_hu_hu,  // 10 HU-HU
    k_it_it,  // 11 IT-IT
    k_nl_nl,  // 12 NL-NL
    k_no_no,  // 13 NO-NO
    NULL,         // 14 PL-PL  (ANSI, same as EN-US)
    k_se_fi,  // 15 SE-SE
};

const char* get_layout_st_label(uint8_t sc, int layout_idx) {
    if (layout_idx < 0 || layout_idx >= 16)
        return NULL;
    const LabelEntry* tbl = k_overrides[layout_idx];
    if (!tbl)
        return NULL;
    for (; tbl->label; ++tbl) {
        if (tbl->sc == sc)
            return tbl->label;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// st_scancode_name
// EN-US fallback label for an Atari ST scancode.
// ---------------------------------------------------------------------------
const char* st_scancode_name(uint8_t sc) {
    switch (sc) {
        // Digit row
        case  2: return "1";    case  3: return "2";    case  4: return "3";
        case  5: return "4";    case  6: return "5";    case  7: return "6";
        case  8: return "7";    case  9: return "8";    case 10: return "9";
        case 11: return "0";    case 12: return "-";    case 13: return "=";
        case 41: return "`";
        // Q-row
        case 16: return "Q";    case 17: return "W";    case 18: return "E";
        case 19: return "R";    case 20: return "T";    case 21: return "Y";
        case 22: return "U";    case 23: return "I";    case 24: return "O";
        case 25: return "P";    case 26: return "[";    case 27: return "]";
        // A-row
        case 30: return "A";    case 31: return "S";    case 32: return "D";
        case 33: return "F";    case 34: return "G";    case 35: return "H";
        case 36: return "J";    case 37: return "K";    case 38: return "L";
        case 39: return ";";    case 40: return "'";
        // Z-row + space
        case 43: return "\\";   case 44: return "Z";    case 45: return "X";
        case 46: return "C";    case 47: return "V";    case 48: return "B";
        case 49: return "N";    case 50: return "M";    case 51: return ",";
        case 52: return ".";    case 53: return "/";    case 57: return "Sp";
        // ISO extra key
        case 96: return "<";
        // Special cluster
        case 98: return "Hlp";  case 97: return "Und";  case 82: return "Ins";
        case 71: return "ClH";
        case 72: return "\x83"; case 80: return "\x84";
        case 75: return "\x85"; case 77: return "\x86";
        // Keypad
        case  74: return "K-";  case  78: return "K+";
		case  99: return "K(";  case 100: return "K)";
        case 101: return "K/";  case 102: return "K*";
        case 103: return "K7";  case 104: return "K8";  case 105: return "K9";
        case 106: return "K4";  case 107: return "K5";  case 108: return "K6";
        case 109: return "K1";  case 110: return "K2";  case 111: return "K3";
        case 112: return "K0";  case 113: return "K.";
        default:  return "?";
    }
}

// ---------------------------------------------------------------------------
// get_layout_label
// Returns the physical key label for a USB HID code in the given layout.
// Derives the label by looking up which ST scancode the HID key maps to by
// default (via HidInput::get_layout_table), then calling get_layout_st_label()
// / st_scancode_name(). Falls back to HidInput::get_label() for keys that
// have no ST mapping (function keys, arrows, keypad…).
// ---------------------------------------------------------------------------
bool sc_is_iso_key(uint8_t sc) {
    return sc == 96;
}

bool layout_has_iso_key(int layout_idx) {
    // EN-US (5) and PL-PL (14) are ANSI layouts with no ISO extra key.
    return layout_idx != 5 && layout_idx != 14;
}

const char* get_layout_label(uint8_t hid, int layout_idx) {
    if (hid == 0 || hid >= 128)
        return HidInput::get_hid_label(hid);

    const uint8_t* tbl = HidInput::get_layout_table((unsigned)layout_idx);
    if (!tbl)
        return HidInput::get_hid_label(hid);

    uint8_t st_sc = tbl[hid];
    if (st_sc == 0)
        return HidInput::get_hid_label(hid);

    // Try layout-specific override first, then EN-US fallback.
    const char* lbl = get_layout_st_label(st_sc, layout_idx);
    if (lbl)
        return lbl;

    const char* fb = st_scancode_name(st_sc);
    if (fb[0] != '?')
        return fb;

    return HidInput::get_hid_label(hid);
}
