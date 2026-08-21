#include "hud_font.h"

#include <string.h>

/* Project-owned compact 5x7 glyphs. The fixed HUD has no general font need. */
static const uint8_t glyphs[128][FRAME_PACER_FONT_HEIGHT] = {
    ['-'] = {0, 0, 0, 31, 0, 0, 0},
    ['/'] = {1, 2, 4, 8, 16, 0, 0},
    ['%'] = {17, 2, 4, 8, 17, 0, 0},
    ['0'] = {14, 17, 19, 21, 25, 17, 14},
    ['1'] = {4, 12, 4, 4, 4, 4, 14},
    ['2'] = {14, 17, 1, 2, 4, 8, 31},
    ['3'] = {30, 1, 1, 14, 1, 1, 30},
    ['4'] = {2, 6, 10, 18, 31, 2, 2},
    ['5'] = {31, 16, 16, 30, 1, 1, 30},
    ['6'] = {6, 8, 16, 30, 17, 17, 14},
    ['7'] = {31, 1, 2, 4, 8, 8, 8},
    ['8'] = {14, 17, 17, 14, 17, 17, 14},
    ['9'] = {14, 17, 17, 15, 1, 2, 12},
    ['A'] = {14, 17, 17, 31, 17, 17, 17},
    ['C'] = {14, 17, 16, 16, 16, 17, 14},
    ['F'] = {31, 16, 16, 30, 16, 16, 16},
    ['G'] = {14, 17, 16, 23, 17, 17, 15},
    ['H'] = {17, 17, 17, 31, 17, 17, 17},
    ['L'] = {16, 16, 16, 16, 16, 16, 31},
    ['M'] = {17, 27, 21, 21, 17, 17, 17},
    ['N'] = {17, 25, 21, 19, 17, 17, 17},
    ['P'] = {30, 17, 17, 30, 16, 16, 16},
    ['R'] = {30, 17, 17, 30, 20, 18, 17},
    ['S'] = {15, 16, 16, 14, 1, 1, 30},
    ['T'] = {31, 4, 4, 4, 4, 4, 4},
    ['U'] = {17, 17, 17, 17, 17, 17, 14},
    ['a'] = {0, 0, 14, 1, 15, 17, 15},
    ['c'] = {0, 0, 14, 16, 16, 17, 14},
    ['e'] = {0, 0, 14, 17, 31, 16, 14},
    ['f'] = {6, 8, 8, 30, 8, 8, 8},
    ['m'] = {0, 0, 26, 21, 21, 21, 21},
    ['p'] = {0, 0, 30, 17, 17, 30, 16},
    ['r'] = {0, 0, 22, 25, 16, 16, 16},
    [126] = {15, 8, 14, 8, 0, 0, 0}, /* Internal compact frame glyph. */
    [127] = {14, 17, 17, 14, 0, 0, 0}, /* Internal degree glyph. */
};

uint8_t frame_pacer_font_row(unsigned char character, unsigned int y)
{
    if (character >= sizeof(glyphs) / sizeof(glyphs[0]) ||
        y >= FRAME_PACER_FONT_HEIGHT)
        return 0;
    return glyphs[character][y];
}

bool frame_pacer_font_pixel(unsigned char character, unsigned int x, unsigned int y)
{
    return x < FRAME_PACER_FONT_WIDTH &&
           (frame_pacer_font_row(character, y) &
            (1U << (FRAME_PACER_FONT_WIDTH - 1U - x))) != 0;
}

void frame_pacer_font_rasterize(const char *text, uint8_t *pixels,
                                unsigned int stride, unsigned int scale)
{
    unsigned int glyph;

    if (!text || !pixels || !scale ||
        stride < (FRAME_PACER_FONT_WIDTH + 1U) * scale)
        return;

    for (glyph = 0; text[glyph]; ++glyph) {
        unsigned int y;

        for (y = 0; y < FRAME_PACER_FONT_HEIGHT; ++y) {
            unsigned int x;

            for (x = 0; x < FRAME_PACER_FONT_WIDTH; ++x) {
                unsigned int sy;

                if (!frame_pacer_font_pixel((unsigned char)text[glyph], x, y))
                    continue;
                for (sy = 0; sy < scale; ++sy) {
                    unsigned int sx;

                    for (sx = 0; sx < scale; ++sx) {
                        size_t offset = (y * scale + sy) * stride +
                                        glyph * (FRAME_PACER_FONT_WIDTH + 1U) * scale +
                                        x * scale + sx;

                        pixels[offset] = 255;
                    }
                }
            }
        }
    }
}
