#ifndef FRAME_PACER_HUD_VERTICES_H
#define FRAME_PACER_HUD_VERTICES_H

#include "hud_text.h"

#include <stdbool.h>
#include <stdint.h>

#define FRAME_PACER_HUD_MAX_VERTICES 4096U

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
