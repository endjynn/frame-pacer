#ifndef FRAME_PACER_HUD_VERTICES_H
#define FRAME_PACER_HUD_VERTICES_H

#include "hud_text.h"
#include "hud_font_atlas.h"

#include <stdbool.h>
#include <stdint.h>

#define FRAME_PACER_HUD_MAX_VERTICES                                           \
    (6U * (1U + FRAME_PACER_HUD_LINE_COUNT_MAX *                               \
                    FRAME_PACER_HUD_LINE_CHARACTERS_MAX))

struct frame_pacer_hud_vertex {
    float position[2];
    float color[4];
    float uv[2];
};

struct frame_pacer_hud_vertices {
    struct frame_pacer_hud_vertex data[FRAME_PACER_HUD_MAX_VERTICES];
    uint32_t count;
    const struct frame_pacer_font_atlas *atlas;
};

#if defined(__GNUC__)
#define FRAME_PACER_HUD_VERTICES_INTERNAL __attribute__((visibility("hidden")))
#else
#define FRAME_PACER_HUD_VERTICES_INTERNAL
#endif

/* Builds pixel-aligned native-size glyph quads without allocation. */
FRAME_PACER_HUD_VERTICES_INTERNAL bool
frame_pacer_hud_vertices_build_for_extent(struct frame_pacer_hud_vertices *,
                                          const struct frame_pacer_hud_text *,
                                          uint32_t drawable_width,
                                          uint32_t drawable_height);

#endif
