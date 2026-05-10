/*

MIT License

Copyright (c) 2021 David Schramm

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include <pico/stdlib.h>
#include <hardware/i2c.h>
#include <pico/binary_info.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "ssd1306.h"
#include "ssd1306_key.h"
#include "font.h"

inline static void fancy_write(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, char *name) {
    switch(i2c_write_blocking(i2c, addr, src, len, false)) {
    case PICO_ERROR_GENERIC:
        printf("[%s] addr not acknowledged!\n", name);
        break;
    case PICO_ERROR_TIMEOUT:
        printf("[%s] timeout!\n", name);
        break;
    default:
        //printf("[%s] wrote successfully %lu bytes!\n", name, len);
        break;
    }
}

inline static void ssd1306_write(ssd1306_t *p, uint8_t val) {
    uint8_t d[2]= {0x00, val};
    fancy_write(p->i2c_i, p->address, d, 2, "ssd1306_write");
}

bool ssd1306_init(ssd1306_t *p, uint16_t width, uint16_t height, uint8_t address, i2c_inst_t *i2c_instance) {
    p->width=width;
    p->height=height;
    p->pages=height/8;
    p->address=address;

    p->i2c_i=i2c_instance;


    p->bufsize=(p->pages)*(p->width);
    if((p->buffer=malloc(p->bufsize+1))==NULL) {
        p->bufsize=0;
        return false;
    }

    ++(p->buffer);

	// from https://github.com/makerportal/rpi-pico-ssd1306
    int8_t cmds[]= {
        SET_DISP | 0x00,  // off
        // address setting
        SET_MEM_ADDR,
        0x00,  // horizontal
        // resolution and layout
        SET_DISP_START_LINE | 0x00,
        SET_SEG_REMAP | 0x01,  // column addr 127 mapped to SEG0
        SET_MUX_RATIO,
        height - 1,
        SET_COM_OUT_DIR | 0x08,  // scan from COM[N] to COM0
        SET_DISP_OFFSET,
        0x00,
        SET_COM_PIN_CFG,
        width>2*height?0x02:0x12,
        // timing and driving scheme
        SET_DISP_CLK_DIV,
        0x80,
        SET_PRECHARGE,
        p->external_vcc?0x22:0xF1,
        SET_VCOM_DESEL,
        0x30,  // 0.83*Vcc
        // display
        SET_CONTRAST,
        0xFF,  // maximum
        SET_ENTIRE_ON,  // output follows RAM contents
        SET_NORM_INV,  // not inverted
        // charge pump
        SET_CHARGE_PUMP,
        p->external_vcc?0x10:0x14,
        SET_DISP | 0x01
    };

    for(size_t i=0; i<sizeof(cmds); ++i)
        ssd1306_write(p, cmds[i]);

    return true;
}

inline void ssd1306_deinit(ssd1306_t *p) {
    free(p->buffer-1);
}

inline void ssd1306_poweroff(ssd1306_t *p) {
    ssd1306_write(p, SET_DISP|0x00);
}

inline void ssd1306_poweron(ssd1306_t *p) {
    ssd1306_write(p, SET_DISP|0x01);
}

inline void ssd1306_contrast(ssd1306_t *p, uint8_t val) {
    ssd1306_write(p, SET_CONTRAST);
    ssd1306_write(p, val);
}

inline void ssd1306_invert(ssd1306_t *p, uint8_t inv) {
    ssd1306_write(p, SET_NORM_INV | (inv & 1));
}

inline void ssd1306_clear(ssd1306_t *p) {
    memset(p->buffer, 0, p->bufsize);
}

void ssd1306_draw_pixel(ssd1306_t *p, uint32_t x, uint32_t y) {
    if (x >= p->width || y >= p->height) return;
    uint8_t *byte = &p->buffer[x + p->width * (y >> 3)];
    uint8_t  mask = (uint8_t)(1u << (y & 7));
    *byte |= mask;  // set bit without clobbering other bits
}

void ssd1306_draw_line(ssd1306_t *p, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    if(x1>x2) {
        uint32_t t=x1;
        x1=x2;
        x2=t;
        t=y1;
        y1=y2;
        y2=t;
    }

    float m=(float) (y2-y1) / (float) (x2-x1);

    for(int32_t i=x1; i<=x2; ++i) {
        float y=m*(float) (i-x1)+(float) y1;
        ssd1306_draw_pixel(p, (uint32_t) i, (uint32_t) y);
    }
}

void ssd1306_draw_square(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t width, uint32_t height){
	for(uint32_t i=0;i<width;++i)
		for(uint32_t j=0;j<height;++j)
			ssd1306_draw_pixel(p, x+i, y+j);

}

void ssd1306_draw_empty_square(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t width, uint32_t height){
	ssd1306_draw_line(p, x, y, x+width, y);
	ssd1306_draw_line(p, x, y+height, x+width, y+height);
	ssd1306_draw_line(p, x, y, x, y+height);
	ssd1306_draw_line(p, x+width, y, x+width, y+height);
}


void ssd1306_draw_char_with_font(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, const uint8_t *font, char c) {
    unsigned char uc = (unsigned char)c;
    if (uc < 0x20 || uc > 0xFF) return; // Out of table (ACCEPT Latin‑1)
    for (uint8_t i = 0; i < font[1]; ++i) {
        uint8_t line = (uint8_t)(font[(uc - 0x20) * font[1] + i + 2]);
        for (int8_t j = 0; j < font[0]; ++j, line >>= 1) {
            if ((line & 1) == 1)
                ssd1306_draw_square(p, x + i * scale, y + j * scale, scale, scale);
        }
    }
}

void ssd1306_draw_string_with_font(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, const uint8_t *font, const char *s) {
    for(int32_t x_n = x; *s; x_n += font[1] * scale + CHAR_KERNING) {
        ssd1306_draw_char_with_font(p, x_n, y, scale, font, *(s++));
    }
}

void ssd1306_draw_char(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, char c) {
    ssd1306_draw_char_with_font(p, x, y, scale, font_8x5, c);
}

void ssd1306_draw_string(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, const char *s) {
    ssd1306_draw_string_with_font(p, x, y, scale, font_8x5, s);
}

// --- UTF-8 decoder ---
static uint32_t utf8_next(const char **ps) {
    const unsigned char *s = (const unsigned char *)(*ps);
    unsigned char c = *s;

    if (c == 0) return 0;                // End of string
    if (c < 0x80) {                      // 1-byte ASCII
        (*ps)++;
        return c;
    }
    // 2-byte sequence: 110xxxxx 10xxxxxx
    if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
        (*ps) += 2;
        return cp;
    }
    // 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
    if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(c & 0x0F) << 12)
                    | ((uint32_t)(s[1] & 0x3F) << 6)
                    | (uint32_t)(s[2] & 0x3F);
        (*ps) += 3;
        return cp;
    }
    // 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if ((c & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(c & 0x07) << 18)
                    | ((uint32_t)(s[1] & 0x3F) << 12)
                    | ((uint32_t)(s[2] & 0x3F) << 6)
                    | (uint32_t)(s[3] & 0x3F);
        (*ps) += 4;
        return cp;
    }
    // Invalid byte: advance by 1 and return replacement
    (*ps)++;
    return 0xFFFD; // Unicode replacement character
}

// Map a Unicode code point to a single-byte glyph in font_8x5
// Returns 0 if no direct glyph available (caller may fallback)
static uint8_t unicode_to_font_byte(uint32_t cp) {
    // Displayable ASCII
    if (cp >= 0x20 && cp <= 0x7F) return (uint8_t)cp;

    // Latin-1 Supplement (U+00A0..U+00FF) -> 0xA0..0xFF
    if (cp >= 0x00A0 && cp <= 0x00FF) return (uint8_t)cp;

    // No direct glyph
    return 0;
}

// UTF-8 aware string rendering with given font
void ssd1306_draw_utf8_string_with_font(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, const uint8_t *font, const char *s) {
    int32_t x_n = x;
    while (s && *s) {
        uint32_t cp = utf8_next(&s);

        uint8_t b = unicode_to_font_byte(cp);

        if (b) {
            // Cast unsigned char
            ssd1306_draw_char_with_font(p, x_n, y, scale, font, (char)b);
            x_n += font[1] * scale + CHAR_KERNING;
        } else {
            // Fallback: '?'
            ssd1306_draw_char_with_font(p, x_n, y, scale, font, '?');
            x_n += font[1] * scale + CHAR_KERNING;
        }
    }
}

// Convenience wrapper with builtin font
void ssd1306_draw_utf8_string(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, const char *s) {
    ssd1306_draw_utf8_string_with_font(p, x, y, scale, font_8x5, s);
}

void ssd1306_show(ssd1306_t *p) {
    uint8_t payload[]= {SET_COL_ADDR, 0, p->width-1, SET_PAGE_ADDR, 0, p->pages-1};
    if(p->width==64) {
        payload[1]+=32;
        payload[2]+=32;
    }

    for(size_t i=0; i<sizeof(payload); ++i)
        ssd1306_write(p, payload[i]);

    *(p->buffer-1)=0x40;

    fancy_write(p->i2c_i, p->address, p->buffer-1, p->bufsize+1, "ssd1306_show");
}
