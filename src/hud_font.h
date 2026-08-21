#ifndef FRAME_PACER_HUD_FONT_H
#define FRAME_PACER_HUD_FONT_H

#include <stdbool.h>
#include <stdint.h>

#define FRAME_PACER_FONT_WIDTH 5U
#define FRAME_PACER_FONT_HEIGHT 7U
#define FRAME_PACER_FONT_LIT_PIXELS_MAX 19U

#if defined(__GNUC__)
#define FRAME_PACER_HUD_FONT_INTERNAL __attribute__((visibility("hidden")))
#else
#define FRAME_PACER_HUD_FONT_INTERNAL
#endif

FRAME_PACER_HUD_FONT_INTERNAL uint8_t frame_pacer_font_row(
    unsigned char character, unsigned int y);
bool frame_pacer_font_pixel(unsigned char character, unsigned int x, unsigned int y);
void frame_pacer_font_rasterize(const char *text, uint8_t *pixels,
                                unsigned int stride, unsigned int scale);

#endif
