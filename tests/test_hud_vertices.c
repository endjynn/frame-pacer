#include "hud_vertices.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int near(float left, float right)
{
    float difference = left - right;

    if (difference < 0.0f) difference = -difference;
    return difference < 0.0001f;
}

int main(void)
{
    struct frame_pacer_hud_text text = {{"GPU  18%  56\x7f", "CPU N/A N/A", "FPS N/A   70", ""}, 3};
    struct frame_pacer_hud_vertices vertices;
    assert(frame_pacer_hud_vertices_build(&vertices, &text));
    assert(vertices.count && vertices.count % 6 == 0);
    assert(vertices.data[0].position[0] == 0.0f);
    assert(vertices.data[0].position[1] == 0.0f);
    assert(vertices.data[1].position[0] == 352.0f);
    assert(vertices.data[2].position[1] == 156.0f);
    assert(vertices.data[6].position[0] == 28.0f);
    assert(vertices.data[6].position[1] == 24.0f);
    /* GPU label is teal, values begin white after the label and its space. */
    assert(near(vertices.data[0].color[3], 0.30f));
    assert(near(vertices.data[6].color[1], 0.9f));
    assert(near(vertices.data[6].color[2], 0.6f));
    {
        unsigned int i; int teal = 0, white = 0, red = 0;
        for (i = 0; i < vertices.count; ++i) {
            if (near(vertices.data[i].color[1], 0.9f) &&
                near(vertices.data[i].color[2], 0.6f)) teal = 1;
            if (near(vertices.data[i].color[0], 1.0f) &&
                near(vertices.data[i].color[1], 1.0f)) white = 1;
            if (near(vertices.data[i].color[0], 1.0f) &&
                near(vertices.data[i].color[1], 0.2f)) red = 1;
        }
        assert(teal && white && red);
    }
    (void)snprintf(text.lines[2], sizeof(text.lines[2]), "THR 10000%%");
    (void)snprintf(text.lines[3], sizeof(text.lines[3]),
                   "FPS 999\x7e 999\x7e");
    text.line_count = 4;
    assert(frame_pacer_hud_vertices_build(&vertices, &text));
    assert(vertices.data[1].position[0] == (float)FRAME_PACER_HUD_WIDTH_MAX);
    assert(vertices.data[2].position[1] == (float)FRAME_PACER_HUD_HEIGHT_MAX);
    /* The optional THR label has its own purple, matching the HUD palette. */
    {
        unsigned int i;
        int purple = 0;

        for (i = 0; i < vertices.count; ++i)
            if (near(vertices.data[i].color[0], 0.75f) &&
                near(vertices.data[i].color[1], 0.35f) &&
                near(vertices.data[i].color[2], 1.0f))
                purple = 1;
        assert(purple);
    }
    memset(&text, 0, sizeof(text));
    for (unsigned int line = 0; line < FRAME_PACER_HUD_LINE_COUNT_MAX; ++line)
        memset(text.lines[line], '0', FRAME_PACER_HUD_LINE_CHARACTERS_MAX);
    text.line_count = FRAME_PACER_HUD_LINE_COUNT_MAX;
    assert(frame_pacer_hud_vertices_build(&vertices, &text));
    assert(vertices.count <= FRAME_PACER_HUD_MAX_VERTICES);
    memset(&text, '8', sizeof(text));
    text.line_count = 4;
    assert(!frame_pacer_hud_vertices_build(&vertices, &text));
}
