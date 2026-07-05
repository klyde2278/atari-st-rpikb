#ifndef _inc_ssd1306_key
#define _inc_ssd1306_key

#include "ssd1306.h"
#include <string.h>

/*
 * ssd1306_draw_key
 *
 * Draws a keyboard key centered in a KEY_SIZE-wide reference cell at (x, y).
 * The actual key width adapts to the label length (1–3 chars).
 *
 * The key style mimics a physical keyboard key on a black-background OLED:
 *   - Solid white outer rectangle (border area)
 *   - Black interior cleared via direct buffer access
 *   - White label centered on the black face, drawn at scale 2
 *
 * This approach avoids ssd1306_draw_line() entirely for the border, which is
 * necessary because the draw_line() implementation divides by (x2-x1) and
 * produces undefined behaviour for vertical lines (x1 == x2).
 *
 * x, y define the top-left corner of the KEY_SIZE × KEY_SIZE bounding cell.
 * The actual key is centered horizontally within that cell.
 * Caller must ensure x >= KEY_BORDER and y >= KEY_BORDER.
 *
 * API reminder (ssd1306.c):
 *   draw_square(x, y, w, h) : fills w×h pixels starting at (x, y)  [pixel count]
 *
 * Parameters:
 *   p     : pointer to ssd1306 instance
 *   x     : left edge of the KEY_SIZE-wide reference cell (>= KEY_BORDER)
 *   y     : top edge of the key face (>= KEY_BORDER)
 *   label : string to display at the center of the key (ASCII or custom glyphs)
 */

#define KEY_SIZE    24   /* Key face height, and reference cell width (px)            */
#define KEY_BORDER   2   /* Border thickness: outer square extends KEY_BORDER px      */
                         /* outside the interior on all four sides                    */
#define KEY_H_PAD    4   /* Horizontal padding on each side of the label              */

#define KEY_FONT_H  (8 * 2)                 /* Glyph height at scale 2     */
#define KEY_CHAR_W  (5 * 2 + CHAR_KERNING)  /* Pixel advance per character */

static inline void ssd1306_draw_utf8_key(ssd1306_t *p, uint32_t x, uint32_t y, const char *label) {

    uint32_t len    = ssd1306_utf8_charlen(label);
    uint32_t text_w = len * KEY_CHAR_W - CHAR_KERNING;
    uint32_t key_w  = text_w + 2u * KEY_H_PAD;
    uint32_t kx     = x + (KEY_SIZE > key_w ? (KEY_SIZE - key_w) / 2u : 0u);

    ssd1306_draw_square(p,
        kx  - KEY_BORDER,
        y   - KEY_BORDER,
        key_w  + 1u + 2u * KEY_BORDER,
        KEY_SIZE + 1u + 2u * KEY_BORDER);

    for (uint32_t cx = kx; cx <= kx + key_w; ++cx) {
        for (uint32_t cy = y; cy <= y + KEY_SIZE; ++cy) {
            if (cx < p->width && cy < p->height) {
                p->buffer[cx + p->width * (cy >> 3)] &= ~(1u << (cy & 7));
            }
        }
    }

    uint32_t text_x = kx + (key_w    > text_w    ? (key_w    - text_w)    / 2u : 0u);
    uint32_t text_y = y  + (KEY_SIZE > KEY_FONT_H ? (KEY_SIZE - KEY_FONT_H) / 2u : 0u);

    ssd1306_draw_utf8_string(p, text_x, text_y, 2, label);
}

static inline void ssd1306_draw_key(ssd1306_t *p, uint32_t x, uint32_t y, const char *label) {

    /* Label pixel width (byte count × char advance, minus trailing kerning).
     * All labels from hid_keycode_to_label() are ASCII or custom single-byte
     * glyphs, so strlen() gives the correct character count. */
    uint32_t len    = (uint32_t)strlen(label);
    uint32_t text_w = len * KEY_CHAR_W - CHAR_KERNING;

    /* Key face width adapts to the label, padded symmetrically. */
    uint32_t key_w = text_w + 2u * KEY_H_PAD;

    /* Center the key horizontally within the KEY_SIZE reference cell.
     * If key_w > KEY_SIZE (wide 3-char labels) the key starts at x. */
    uint32_t kx = x + (KEY_SIZE > key_w ? (KEY_SIZE - key_w) / 2u : 0u);

    /* --- Border: filled outer square + cleared interior -------------------
     *
     * 1) draw_square fills the full outer rectangle (face + border ring):
     *      top-left  : (kx - KEY_BORDER,  y - KEY_BORDER)
     *      pixel count: (key_w + 1 + 2*KEY_BORDER) × (KEY_SIZE + 1 + 2*KEY_BORDER)
     *    The +1 makes the rect span from kx to kx+key_w inclusive (key_w+1 columns).
     *
     * 2) The face interior (kx, y) → (kx+key_w, y+KEY_SIZE) is cleared to black
     *    via direct buffer manipulation — identical to the draw_pixel formula in
     *    ssd1306.c: buffer[x + width*(y>>3)] bit (y&7).
     *
     * Result: solid whiteã  border (KEY_BORDER px thick), black face interior. */

    ssd1306_draw_square(p,
        kx  - KEY_BORDER,
        y   - KEY_BORDER,
        key_w  + 1u + 2u * KEY_BORDER,
        KEY_SIZE + 1u + 2u * KEY_BORDER);

    for (uint32_t cx = kx; cx <= kx + key_w; ++cx) {
        for (uint32_t cy = y; cy <= y + KEY_SIZE; ++cy) {
            if (cx < p->width && cy < p->height) {
                p->buffer[cx + p->width * (cy >> 3)] &= ~(1u << (cy & 7));
            }
        }
    }

    /* --- Label: centered on the black face -------------------------------- */
    uint32_t text_x = kx + (key_w    > text_w    ? (key_w    - text_w)    / 2u : 0u);
    uint32_t text_y = y  + (KEY_SIZE > KEY_FONT_H ? (KEY_SIZE - KEY_FONT_H) / 2u : 0u);

    ssd1306_draw_string(p, text_x, text_y, 2, label);
}

#endif /* _inc_ssd1306_key */