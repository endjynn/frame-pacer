#include "hud_font.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    unsigned char pixels[32 * 14];
    unsigned int character;
    unsigned int densest = 0;

    for (character = 0; character < 256; ++character) {
        unsigned int lit = 0;
        unsigned int x, y;

        for (y = 0; y < FRAME_PACER_FONT_HEIGHT; ++y)
            for (x = 0; x < FRAME_PACER_FONT_WIDTH; ++x)
                if (frame_pacer_font_pixel((unsigned char)character, x, y))
                    ++lit;
        assert(lit <= FRAME_PACER_FONT_LIT_PIXELS_MAX);
        if (lit > densest)
            densest = lit;
    }
    assert(densest == FRAME_PACER_FONT_LIT_PIXELS_MAX);
    assert(frame_pacer_font_pixel('f', 2, 0));
    assert(!frame_pacer_font_pixel('f', 0, 0));
    assert(frame_pacer_font_pixel('8', 1, 0));
    assert(frame_pacer_font_pixel('L', 0, 0));
    assert(frame_pacer_font_pixel('M', 0, 0));
    assert(frame_pacer_font_pixel('T', 0, 0));
    assert(frame_pacer_font_pixel('H', 0, 0));
    assert(frame_pacer_font_pixel('R', 0, 0));
    assert(!frame_pacer_font_pixel('?', 0, 0));
    assert(frame_pacer_font_pixel('/', 2, 2));
    assert(frame_pacer_font_pixel(126, 1, 0));
    assert(frame_pacer_font_pixel(127, 1, 0));
    assert(!frame_pacer_font_pixel(128, 0, 0));
    assert(!frame_pacer_font_pixel(255, 0, 0));
    memset(pixels, 0, sizeof(pixels));
    frame_pacer_font_rasterize("FPS", pixels, 32, 2);
    assert(pixels[0] == 255);
    assert(pixels[8] == 255);
}
