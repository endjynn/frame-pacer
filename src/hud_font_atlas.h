#ifndef FRAME_PACER_HUD_FONT_ATLAS_H
#define FRAME_PACER_HUD_FONT_ATLAS_H

#include <stdbool.h>
#include <stdint.h>

#define FRAME_PACER_FONT_SIZE_MIN 8U
#define FRAME_PACER_FONT_SIZE_MAX 64U
#define FRAME_PACER_FONT_REFERENCE_SIZE 23U
#define FRAME_PACER_FONT_PREFERRED_MIN 14U
#define FRAME_PACER_FONT_PREFERRED_MAX 60U
#define FRAME_PACER_FONT_REFERENCE_HEIGHT 1600U

struct frame_pacer_font_glyph {
    uint16_t x, y, width, height;
    int16_t left, top;
    bool present;
};

struct frame_pacer_font_atlas {
    uint16_t pixel_size, width, height, advance;
    int16_t ascent, descent, line_height, ink_left, ink_right;
    uint32_t offset;
    struct frame_pacer_font_glyph glyphs[128];
};

#define FRAME_PACER_FONT_INTERNAL __attribute__((visibility("hidden")))
FRAME_PACER_FONT_INTERNAL const struct frame_pacer_font_atlas *
frame_pacer_font_atlas_at_size(unsigned int);
FRAME_PACER_FONT_INTERNAL const struct frame_pacer_font_atlas *
frame_pacer_font_atlas_for_extent(uint32_t, uint32_t);
FRAME_PACER_FONT_INTERNAL const unsigned char *
frame_pacer_font_atlas_pixels(const struct frame_pacer_font_atlas *);
FRAME_PACER_FONT_INTERNAL uint32_t frame_pacer_font_panel_width(
    const struct frame_pacer_font_atlas *, unsigned int characters);
FRAME_PACER_FONT_INTERNAL uint32_t frame_pacer_font_panel_height(
    const struct frame_pacer_font_atlas *, unsigned int rows);
FRAME_PACER_FONT_INTERNAL uint32_t
frame_pacer_font_padding(const struct frame_pacer_font_atlas *);

#endif
