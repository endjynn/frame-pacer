#ifndef FRAME_PACER_HUD_VERTICES_H
#define FRAME_PACER_HUD_VERTICES_H

#include "hud_text.h"
#include "hud_font.h"

#include <stdbool.h>
#include <stdint.h>

#define FRAME_PACER_HUD_REFERENCE_VIEWPORT_WIDTH 2560U
#define FRAME_PACER_HUD_REFERENCE_VIEWPORT_HEIGHT 1600U
#define FRAME_PACER_HUD_REFERENCE_PIXEL_SIZE 3U
#define FRAME_PACER_HUD_CHARACTER_ADVANCE_UNITS 6U
#define FRAME_PACER_HUD_LINE_ADVANCE_UNITS 10U
#define FRAME_PACER_HUD_TEXT_X_UNITS 6U
#define FRAME_PACER_HUD_TEXT_Y_UNITS 6U
#define FRAME_PACER_HUD_PANEL_RIGHT_PADDING_UNITS 4U
#define FRAME_PACER_HUD_PANEL_BOTTOM_PADDING_UNITS 3U
#define FRAME_PACER_HUD_WIDTH_UNITS                                      \
    (FRAME_PACER_HUD_TEXT_X_UNITS +                                     \
     FRAME_PACER_HUD_LINE_CHARACTERS_MAX *                              \
         FRAME_PACER_HUD_CHARACTER_ADVANCE_UNITS +                      \
     FRAME_PACER_HUD_PANEL_RIGHT_PADDING_UNITS)
#define FRAME_PACER_HUD_HEIGHT_UNITS                                    \
    (FRAME_PACER_HUD_TEXT_Y_UNITS +                                    \
     FRAME_PACER_HUD_LINE_COUNT_MAX *                                  \
         FRAME_PACER_HUD_LINE_ADVANCE_UNITS +                          \
     FRAME_PACER_HUD_PANEL_BOTTOM_PADDING_UNITS)
#define FRAME_PACER_HUD_REFERENCE_WIDTH                                 \
    (FRAME_PACER_HUD_WIDTH_UNITS * FRAME_PACER_HUD_REFERENCE_PIXEL_SIZE)
#define FRAME_PACER_HUD_REFERENCE_HEIGHT                                \
    (FRAME_PACER_HUD_HEIGHT_UNITS * FRAME_PACER_HUD_REFERENCE_PIXEL_SIZE)
#define FRAME_PACER_HUD_MAX_VERTICES                                      \
    (6U * (1U + FRAME_PACER_HUD_LINE_COUNT_MAX *                          \
                 FRAME_PACER_HUD_LINE_CHARACTERS_MAX *                    \
                 FRAME_PACER_FONT_LIT_PIXELS_MAX))

struct frame_pacer_hud_vertex {
    float position[2];
    float color[4];
};

struct frame_pacer_hud_vertices {
    struct frame_pacer_hud_vertex data[FRAME_PACER_HUD_MAX_VERTICES];
    uint32_t count;
};

/* Produces reference-size triangles for every lit font pixel without
 * allocation.  Kept as the stable geometry-builder ABI. */
bool frame_pacer_hud_vertices_build(struct frame_pacer_hud_vertices *,
                                    const struct frame_pacer_hud_text *);

#if defined(__GNUC__)
#define FRAME_PACER_HUD_VERTICES_INTERNAL __attribute__((visibility("hidden")))
#else
#define FRAME_PACER_HUD_VERTICES_INTERNAL
#endif

/* The presentation backends supply their current extent so the nearest crisp
 * integer scale can be selected relative to the 2560x1600 reference. */
FRAME_PACER_HUD_VERTICES_INTERNAL bool
frame_pacer_hud_vertices_build_for_extent(
    struct frame_pacer_hud_vertices *, const struct frame_pacer_hud_text *,
    uint32_t drawable_width, uint32_t drawable_height);

#endif
