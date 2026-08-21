#ifndef FRAME_PACER_HUD_VERTICES_H
#define FRAME_PACER_HUD_VERTICES_H

#include "hud_text.h"
#include "hud_font.h"

#include <stdbool.h>
#include <stdint.h>

#define FRAME_PACER_HUD_PIXEL_SIZE 4U
#define FRAME_PACER_HUD_CHARACTER_ADVANCE 24U
#define FRAME_PACER_HUD_LINE_ADVANCE 40U
#define FRAME_PACER_HUD_TEXT_X 24U
#define FRAME_PACER_HUD_TEXT_Y 24U
#define FRAME_PACER_HUD_PANEL_RIGHT_PADDING 16U
#define FRAME_PACER_HUD_PANEL_BOTTOM_PADDING 12U
#define FRAME_PACER_HUD_WIDTH_MAX                                        \
    (FRAME_PACER_HUD_TEXT_X +                                           \
     FRAME_PACER_HUD_LINE_CHARACTERS_MAX *                              \
         FRAME_PACER_HUD_CHARACTER_ADVANCE +                            \
     FRAME_PACER_HUD_PANEL_RIGHT_PADDING)
#define FRAME_PACER_HUD_HEIGHT_MAX                                      \
    (FRAME_PACER_HUD_TEXT_Y +                                          \
     FRAME_PACER_HUD_LINE_COUNT_MAX * FRAME_PACER_HUD_LINE_ADVANCE +   \
     FRAME_PACER_HUD_PANEL_BOTTOM_PADDING)
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

/* Produces two triangles for every lit font pixel without allocation. */
bool frame_pacer_hud_vertices_build(struct frame_pacer_hud_vertices *,
                                    const struct frame_pacer_hud_text *);

#endif
