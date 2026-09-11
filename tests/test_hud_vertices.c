#include "hud_vertices.h"

#include <assert.h>
#include <string.h>

static int near(float a, float b)
{
    float difference = a - b;
    return difference > -0.00001f && difference < 0.00001f;
}

static void verify(const struct frame_pacer_hud_vertices *vertices,
                   const struct frame_pacer_hud_text *text)
{
    const struct frame_pacer_font_atlas *atlas = vertices->atlas;
    unsigned int expected = 6;
    float width = vertices->data[1].position[0];
    float height = vertices->data[2].position[1];
    assert(atlas && vertices->count <= FRAME_PACER_HUD_MAX_VERTICES);
    float min_x = width, min_y = height, max_x = 0, max_y = 0;
    const struct frame_pacer_font_glyph *first =
        &atlas->glyphs[(unsigned char)text->lines[0][0]];
    float origin = vertices->data[6].position[0] - first->left;
    float baseline = vertices->data[6].position[1] + first->top;
    for (unsigned int row = 0; row < text->line_count; ++row) {
        for (unsigned int column = 0; text->lines[row][column]; ++column) {
            const struct frame_pacer_font_glyph *glyph =
                &atlas->glyphs[(unsigned char)text->lines[row][column]];
            if (!glyph->width || !glyph->height)
                continue;
            const struct frame_pacer_hud_vertex *v = &vertices->data[expected];
            assert(v[1].position[0] - v[0].position[0] == glyph->width);
            assert(v[2].position[1] - v[0].position[1] == glyph->height);
            assert(near(v[0].uv[0], (float)glyph->x / atlas->width));
            assert(near(v[0].uv[1], (float)glyph->y / atlas->height));
            assert(
                v[0].position[0] ==
                (float)(origin + (int)column * atlas->advance + glyph->left));
            assert(
                v[0].position[1] ==
                (float)(baseline + (int)row * atlas->line_height - glyph->top));
            if (v[0].position[0] < min_x)
                min_x = v[0].position[0];
            if (v[0].position[1] < min_y)
                min_y = v[0].position[1];
            if (v[2].position[0] > max_x)
                max_x = v[2].position[0];
            if (v[2].position[1] > max_y)
                max_y = v[2].position[1];
            if (column >= 3)
                assert(v[0].color[0] == 1 && v[0].color[1] == 1 &&
                       v[0].color[2] == 1);
            expected += 6;
        }
    }
    assert(vertices->count == expected);
    float padding = (float)frame_pacer_font_padding(atlas);
    assert(min_x == padding && min_y == padding);
    assert(width - max_x == padding && height - max_y == padding);
    for (unsigned int i = 0; i < vertices->count; ++i) {
        const struct frame_pacer_hud_vertex *v = &vertices->data[i];
        assert(v->position[0] >= 0 && v->position[0] <= width);
        assert(v->position[1] >= 0 && v->position[1] <= height);
        assert(v->position[0] == (float)(unsigned int)v->position[0]);
        assert(v->position[1] == (float)(unsigned int)v->position[1]);
        if (i < 6) {
            assert(v->uv[0] == -1 && v->uv[1] == -1);
            assert(v->color[3] > 0.299f && v->color[3] < 0.301f);
        } else {
            assert(v->uv[0] >= 0 && v->uv[0] <= 1);
            assert(v->uv[1] >= 0 && v->uv[1] <= 1);
        }
    }
}

int main(void)
{
    struct frame_pacer_hud_text text = {{"GPU  18%  56\x7f", "CPU N/A N/A",
                                         "THR  16%  50%",
                                         "FPS 999\x7e 999\x7e"},
                                        4};
    struct frame_pacer_hud_vertices vertices;
    const uint32_t extents[][2] = {
        {1280, 720},  {1440, 900},  {1920, 1080}, {2560, 1440}, {2560, 1600},
        {3840, 2160}, {3440, 1440}, {87, 400},    {100, 100},   {7680, 4320}};
    for (unsigned int i = 0; i < sizeof(extents) / sizeof(extents[0]); ++i) {
        assert(frame_pacer_hud_vertices_build_for_extent(
            &vertices, &text, extents[i][0], extents[i][1]));
        verify(&vertices, &text);
        assert(vertices.data[1].position[0] <= extents[i][0]);
        assert(vertices.data[2].position[1] <= extents[i][1]);
    }
    memcpy(text.lines[2], text.lines[3], sizeof(text.lines[2]));
    text.line_count = 3;
    assert(
        frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 1440, 900));
    verify(&vertices, &text);
    assert(vertices.data[6].color[1] > 0.899f &&
           vertices.data[6].color[1] < 0.901f);
    for (unsigned int line = 0; line < 4; ++line) {
        memset(text.lines[line], '0', 13);
        text.lines[line][13] = 0;
    }
    text.line_count = 4;
    assert(
        frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 1440, 900));
    assert(vertices.count == FRAME_PACER_HUD_MAX_VERTICES);
    verify(&vertices, &text);
    text.lines[0][0] = 'Z';
    assert(!frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 1440,
                                                      900));
    assert(vertices.count == 0 && !vertices.atlas);
    memset(&text, '8', sizeof(text));
    text.line_count = 4;
    assert(!frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 1440,
                                                      900));
    assert(!frame_pacer_hud_vertices_build_for_extent(&vertices, 0, 1440, 900));
    assert(!frame_pacer_hud_vertices_build_for_extent(0, &text, 1440, 900));
    assert(!frame_pacer_hud_vertices_build_for_extent(&vertices, &text, 1, 1));
    return 0;
}
