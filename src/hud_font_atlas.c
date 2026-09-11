#include "hud_font_atlas.h"

#include "hud_text.h"

#include "../assets/fonts/hud/hud-atlas.inc"

extern const unsigned char frame_pacer_font_pixels[]
    __attribute__((visibility("hidden")));

const struct frame_pacer_font_atlas *
frame_pacer_font_atlas_at_size(unsigned int size)
{
    if (size < FRAME_PACER_FONT_SIZE_MIN || size > FRAME_PACER_FONT_SIZE_MAX)
        return 0;
    return &font_atlases[size - FRAME_PACER_FONT_SIZE_MIN];
}

uint32_t frame_pacer_font_padding(const struct frame_pacer_font_atlas *atlas)
{
    return atlas ? ((uint32_t)atlas->pixel_size * 5U + 11U) / 12U : 0;
}

uint32_t
frame_pacer_font_panel_width(const struct frame_pacer_font_atlas *atlas,
                             unsigned int characters)
{
    if (!atlas || characters > FRAME_PACER_HUD_LINE_CHARACTERS_MAX)
        return 0;
    uint32_t left = atlas->ink_left < 0 ? (uint32_t)-atlas->ink_left : 0;
    uint32_t right = atlas->ink_right > atlas->advance
                         ? (uint32_t)(atlas->ink_right - atlas->advance)
                         : 0;
    return frame_pacer_font_padding(atlas) * 2U + left +
           characters * atlas->advance + right;
}

uint32_t
frame_pacer_font_panel_height(const struct frame_pacer_font_atlas *atlas,
                              unsigned int rows)
{
    if (!atlas || !rows || rows > FRAME_PACER_HUD_LINE_COUNT_MAX)
        return 0;
    return frame_pacer_font_padding(atlas) * 2U +
           (rows - 1U) * (uint32_t)atlas->line_height +
           (uint32_t)atlas->ascent + (uint32_t)atlas->descent;
}

const struct frame_pacer_font_atlas *
frame_pacer_font_atlas_for_extent(uint32_t width, uint32_t height)
{
    if (!width || !height)
        return 0;
    uint64_t desired = ((uint64_t)height * FRAME_PACER_FONT_REFERENCE_SIZE +
                        FRAME_PACER_FONT_REFERENCE_HEIGHT / 2U) /
                       FRAME_PACER_FONT_REFERENCE_HEIGHT;
    if (desired < FRAME_PACER_FONT_PREFERRED_MIN)
        desired = FRAME_PACER_FONT_PREFERRED_MIN;
    if (desired > FRAME_PACER_FONT_PREFERRED_MAX)
        desired = FRAME_PACER_FONT_PREFERRED_MAX;
    /* Narrow/tiny windows choose a smaller native size, never distorted ink.
     * Reserve all four rows so toggling THR does not change the font size. */
    for (unsigned int size = (unsigned int)desired;
         size >= FRAME_PACER_FONT_SIZE_MIN; --size) {
        const struct frame_pacer_font_atlas *atlas =
            frame_pacer_font_atlas_at_size(size);
        if (frame_pacer_font_panel_width(
                atlas, FRAME_PACER_HUD_LINE_CHARACTERS_MAX) <= width &&
            frame_pacer_font_panel_height(
                atlas, FRAME_PACER_HUD_LINE_COUNT_MAX) <= height)
            return atlas;
    }
    return 0;
}

const unsigned char *
frame_pacer_font_atlas_pixels(const struct frame_pacer_font_atlas *atlas)
{
    return atlas ? frame_pacer_font_pixels + atlas->offset : 0;
}
