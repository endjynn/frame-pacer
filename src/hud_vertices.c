#include "hud_vertices.h"

#include <string.h>
#include <limits.h>

static void add_quad(struct frame_pacer_hud_vertices *vertices, float x,
                     float y, float width, float height, const float color[4],
                     const struct frame_pacer_font_glyph *glyph)
{
    static const unsigned int corners[6][2] = {{0, 0}, {1, 0}, {1, 1},
                                               {0, 0}, {1, 1}, {0, 1}};
    for (unsigned int i = 0; i < 6; ++i) {
        struct frame_pacer_hud_vertex *output =
            &vertices->data[vertices->count++];
        output->position[0] = x + (float)corners[i][0] * width;
        output->position[1] = y + (float)corners[i][1] * height;
        memcpy(output->color, color, sizeof(output->color));
        output->uv[0] = glyph
                            ? (float)(glyph->x + corners[i][0] * glyph->width) /
                                  (float)vertices->atlas->width
                            : -1.0f;
        output->uv[1] =
            glyph ? (float)(glyph->y + corners[i][1] * glyph->height) /
                        (float)vertices->atlas->height
                  : -1.0f;
    }
}

static const float *label_color(const char *text, unsigned int line,
                                const float colors[4][4])
{
    if (!strncmp(text, "THR", 3))
        return colors[2];
    if (!strncmp(text, "FPS", 3))
        return colors[3];
    return colors[line];
}

bool frame_pacer_hud_vertices_build_for_extent(
    struct frame_pacer_hud_vertices *vertices,
    const struct frame_pacer_hud_text *text, uint32_t drawable_width,
    uint32_t drawable_height)
{
    static const float panel_color[4] = {0.0f, 0.0f, 0.0f, 0.30f};
    static const float text_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const float label_colors[4][4] = {{0.0f, 0.9f, 0.6f, 1.0f},
                                             {0.0f, 0.65f, 1.0f, 1.0f},
                                             {0.75f, 0.35f, 1.0f, 1.0f},
                                             {1.0f, 0.2f, 0.2f, 1.0f}};
    int left = INT_MAX, top = INT_MAX, right = INT_MIN, bottom = INT_MIN;
    if (!vertices)
        return false;
    vertices->count = 0;
    vertices->atlas = 0;
    if (!text || text->line_count < 3 ||
        text->line_count > FRAME_PACER_HUD_LINE_COUNT_MAX)
        return false;
    const struct frame_pacer_font_atlas *atlas =
        frame_pacer_font_atlas_for_extent(drawable_width, drawable_height);
    if (!atlas)
        return false;
    for (unsigned int line = 0; line < text->line_count; ++line) {
        unsigned int length = 0;
        while (length < sizeof(text->lines[line]) &&
               text->lines[line][length]) {
            unsigned char character =
                (unsigned char)text->lines[line][length++];
            if (character >= 128 || !atlas->glyphs[character].present)
                return false;
            const struct frame_pacer_font_glyph *glyph =
                &atlas->glyphs[character];
            if (glyph->width && glyph->height) {
                int x = (int)(length - 1U) * atlas->advance + glyph->left;
                int y = (int)line * atlas->line_height - glyph->top;
                if (x < left)
                    left = x;
                if (y < top)
                    top = y;
                if (x + glyph->width > right)
                    right = x + glyph->width;
                if (y + glyph->height > bottom)
                    bottom = y + glyph->height;
            }
        }
        if (length > FRAME_PACER_HUD_LINE_CHARACTERS_MAX)
            return false;
    }
    if (left == INT_MAX)
        return false;
    vertices->atlas = atlas;
    int padding = (int)frame_pacer_font_padding(atlas);
    /* Fit the visible ink, not the font's unused ascent/descent space.
     * Native character advances and baseline distances remain unchanged. */
    add_quad(vertices, 0, 0, (float)(right - left + 2 * padding),
             (float)(bottom - top + 2 * padding), panel_color, 0);
    int origin = padding - left;
    for (unsigned int line = 0; line < text->line_count; ++line) {
        int baseline = padding - top + (int)line * atlas->line_height;
        for (unsigned int column = 0; text->lines[line][column]; ++column) {
            const struct frame_pacer_font_glyph *glyph =
                &atlas->glyphs[(unsigned char)text->lines[line][column]];
            if (!glyph->width || !glyph->height)
                continue;
            const float *color =
                column < 3 ? label_color(text->lines[line], line, label_colors)
                           : text_color;
            add_quad(
                vertices,
                (float)(origin + (int)column * atlas->advance + glyph->left),
                (float)(baseline - glyph->top), glyph->width, glyph->height,
                color, glyph);
        }
    }
    return true;
}
