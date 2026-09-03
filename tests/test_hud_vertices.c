#include "hud_vertices.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int near(float left, float right)
{
    float difference = left - right;

    if (difference < 0.0f)
        difference = -difference;
    return difference < 0.0001f;
}

static void assert_panel(const struct frame_pacer_hud_vertices *vertices,
                         float width, float height)
{
    assert(vertices->count >= 6);
    assert(vertices->data[0].position[0] == 0.0f);
    assert(vertices->data[0].position[1] == 0.0f);
    assert(vertices->data[1].position[0] == width);
    assert(vertices->data[2].position[1] == height);
}

int main(void)
{
    struct frame_pacer_hud_text text = {
        {"GPU  18%  56\x7f", "CPU N/A N/A", "FPS N/A   70", ""}, 3};
    struct frame_pacer_hud_vertices vertices;
    assert(frame_pacer_hud_vertices_build_for_extent(
        &vertices, &text, FRAME_PACER_HUD_REFERENCE_VIEWPORT_WIDTH,
        FRAME_PACER_HUD_REFERENCE_VIEWPORT_HEIGHT));
    assert(vertices.count && vertices.count % 6 == 0);
    assert_panel(&vertices, 220.0f, 97.5f);
    assert(vertices.data[6].position[0] == 17.5f);
    assert(vertices.data[6].position[1] == 15.0f);
    /* GPU label is teal, values begin white after the label and its space. */
    assert(near(vertices.data[0].color[3], 0.30f));
    assert(near(vertices.data[6].color[1], 0.9f));
    assert(near(vertices.data[6].color[2], 0.6f));
    {
        unsigned int i;
        int teal = 0, white = 0, red = 0;
        for (i = 0; i < vertices.count; ++i) {
            if (near(vertices.data[i].color[1], 0.9f) &&
                near(vertices.data[i].color[2], 0.6f))
                teal = 1;
            if (near(vertices.data[i].color[0], 1.0f) &&
                near(vertices.data[i].color[1], 1.0f))
                white = 1;
            if (near(vertices.data[i].color[0], 1.0f) &&
                near(vertices.data[i].color[1], 0.2f))
                red = 1;
        }
        assert(teal && white && red);
    }

    /* 1920x1200 is exactly 75% of the 2560x1600 reference extent.
     * Rebuilding the same text proves a live drawable resize changes only
     * geometry, not content or the crisp integer-pixel grid. */
    assert(frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 1920,
                                                     1200));
    assert_panel(&vertices, 165.0f, 73.125f);
    assert(vertices.data[6].position[0] == 13.125f);
    assert(vertices.data[6].position[1] == 11.25f);
    assert(frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 2560,
                                                     1600));
    assert_panel(&vertices, 220.0f, 97.5f);

    /* Scaling uses the limiting axis and remains proportional. */
    assert(frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 3840,
                                                     2400));
    assert_panel(&vertices, 330.0f, 146.25f);
    assert(frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 3440,
                                                     1440));
    assert_panel(&vertices, 198.0f, 87.75f);
    assert(frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 3840,
                                                     1200));
    assert_panel(&vertices, 165.0f, 73.125f);
    assert(
        frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 640, 400));
    assert_panel(&vertices, 88.0f, 39.0f);
    assert(
        frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 87, 400));
    assert(near(vertices.data[1].position[0], 87.0f));
    assert(vertices.data[2].position[1] < 400.0f);
    assert(
        !frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 640, 0));

    (void)snprintf(text.lines[2], sizeof(text.lines[2]), "THR 10000%%");
    (void)snprintf(text.lines[3], sizeof(text.lines[3]), "FPS 999\x7e 999\x7e");
    text.line_count = 4;
    assert(frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 2560,
                                                     1600));
    assert_panel(&vertices, (float)FRAME_PACER_HUD_REFERENCE_WIDTH, 122.5f);
    assert(frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 1920,
                                                     1200));
    assert_panel(&vertices, 165.0f, 91.875f);
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
    assert(frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 2560,
                                                     1600));
    assert(vertices.count <= FRAME_PACER_HUD_MAX_VERTICES);
    memset(&text, '8', sizeof(text));
    text.line_count = 4;
    assert(!frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 2560,
                                                      1600));
}
