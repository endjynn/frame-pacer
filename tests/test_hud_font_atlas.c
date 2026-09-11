#include "hud_font_atlas.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void)
{
    assert(!frame_pacer_font_atlas_at_size(7));
    assert(!frame_pacer_font_atlas_at_size(65));
    assert(!frame_pacer_font_atlas_for_extent(0, 900));
    assert(!frame_pacer_font_atlas_for_extent(1440, 0));
    assert(!frame_pacer_font_atlas_for_extent(1, 1));
    assert(!frame_pacer_font_atlas_pixels(NULL));
    assert(!frame_pacer_font_panel_width(NULL, 13));
    assert(!frame_pacer_font_panel_height(NULL, 4));
    assert(frame_pacer_font_padding(frame_pacer_font_atlas_at_size(24)) == 10);
    assert(frame_pacer_font_padding(frame_pacer_font_atlas_at_size(14)) == 6);
    assert(frame_pacer_font_padding(frame_pacer_font_atlas_at_size(32)) == 14);

    const struct {
        uint32_t width, height, size;
    } extents[] = {
        {1280, 720, 14},  {1440, 900, 14},  {1920, 1080, 16},
        {2560, 1440, 21}, {2560, 1600, 23}, {3840, 2160, 31},
        {3440, 1440, 21}, {7680, 4320, 60}, {UINT32_MAX, UINT32_MAX, 60}};
    for (size_t i = 0; i < sizeof(extents) / sizeof(extents[0]); ++i) {
        const struct frame_pacer_font_atlas *atlas =
            frame_pacer_font_atlas_for_extent(extents[i].width,
                                              extents[i].height);
        assert(atlas && atlas->pixel_size == extents[i].size);
        assert(frame_pacer_font_panel_width(atlas, 13) <= extents[i].width);
        assert(frame_pacer_font_panel_height(atlas, 4) <= extents[i].height);
    }
    const unsigned char *previous = NULL;
    size_t previous_bytes = 0;
    for (unsigned int size = 8; size <= 64; ++size) {
        const struct frame_pacer_font_atlas *atlas =
            frame_pacer_font_atlas_at_size(size);
        const unsigned char *pixels = frame_pacer_font_atlas_pixels(atlas);
        assert(atlas->pixel_size == size);
        assert(atlas->advance == (size * 3 + 2) / 5);
        if (previous)
            assert(pixels == previous + previous_bytes);
        previous = pixels;
        previous_bytes = (size_t)atlas->width * atlas->height;
        assert(frame_pacer_font_panel_height(atlas, 4) -
                   frame_pacer_font_panel_height(atlas, 3) ==
               (uint32_t)atlas->line_height);
        assert(frame_pacer_font_panel_width(atlas, 13) -
                   frame_pacer_font_panel_width(atlas, 12) ==
               atlas->advance);
        assert(!frame_pacer_font_panel_height(atlas, 0));
        assert(!frame_pacer_font_panel_height(atlas, 5));
        assert(!frame_pacer_font_panel_width(atlas, 14));
        for (unsigned int character = 0; character < 128; ++character) {
            const struct frame_pacer_font_glyph *glyph =
                &atlas->glyphs[character];
            if (!glyph->present)
                continue;
            assert(glyph->x + glyph->width < atlas->width);
            assert(glyph->y + glyph->height < atlas->height);
            assert(glyph->top <= atlas->ascent);
            assert((int)glyph->height - glyph->top <= atlas->descent);
        }
        uint32_t width = frame_pacer_font_panel_width(atlas, 13);
        uint32_t height = frame_pacer_font_panel_height(atlas, 4);
        const struct frame_pacer_font_atlas *fit =
            frame_pacer_font_atlas_for_extent(width, height);
        assert(fit && frame_pacer_font_panel_width(fit, 13) <= width);
        assert(frame_pacer_font_panel_height(fit, 4) <= height);
    }
    const struct frame_pacer_font_atlas *small =
        frame_pacer_font_atlas_at_size(14);
    assert(small->advance == 8);
    assert(small->glyphs['%'].left + small->glyphs['%'].width == 9);
    return 0;
}
