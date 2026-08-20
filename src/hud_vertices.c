#include "hud_vertices.h"

#include "hud_font.h"

#include <string.h>

#define FRAME_PACER_HUD_PIXEL_SIZE 4U
#define FRAME_PACER_HUD_CHARACTER_ADVANCE 24U
#define FRAME_PACER_HUD_LINE_ADVANCE 40U
#define FRAME_PACER_HUD_TEXT_X 24U
#define FRAME_PACER_HUD_TEXT_Y 24U
#define FRAME_PACER_HUD_PANEL_RIGHT_PADDING 16U
#define FRAME_PACER_HUD_PANEL_BOTTOM_PADDING 12U

static void add_quad(struct frame_pacer_hud_vertices *vertices, float x, float y,
                     float width, float height, const float color[4])
{
    const float points[6][2] = {
        {x, y},
        {x + width, y},
        {x + width, y + height},
        {x, y},
        {x + width, y + height},
        {x, y + height},
    };
    struct frame_pacer_hud_vertex *output = &vertices->data[vertices->count];
    unsigned int index;

    for (index = 0; index < 6; ++index) {
        output[index].position[0] = points[index][0];
        output[index].position[1] = points[index][1];
        memcpy(output[index].color, color, sizeof(output[index].color));
    }
    vertices->count += 6;
}

static const float *label_color(const char *text, unsigned int line,
                                const float colors[4][4])
{
    if (!strncmp(text, "THR", 3)) return colors[2];
    if (!strncmp(text, "FPS", 3)) return colors[3];
    return colors[line];
}

bool frame_pacer_hud_vertices_build(struct frame_pacer_hud_vertices *vertices,
                                    const struct frame_pacer_hud_text *text)
{
    static const float panel_color[4] = {0.0f, 0.0f, 0.0f, 0.30f};
    static const float text_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    static const float label_colors[4][4] = {
        {0.0f, 0.9f, 0.6f, 1.0f},
        {0.0f, 0.65f, 1.0f, 1.0f},
        /* Purple is distinct while retaining the HUD's saturated palette. */
        {0.75f, 0.35f, 1.0f, 1.0f},
        {1.0f, 0.2f, 0.2f, 1.0f},
    };
    unsigned int line, longest = 0;

    if (!vertices || !text)
        return false;
    vertices->count = 0;
    if (text->line_count < 3 || text->line_count > 4) return false;
    for (line = 0; line < text->line_count; ++line) {
        size_t length = 0;

        while (length < sizeof(text->lines[line]) && text->lines[line][length]) ++length;
        if (length == sizeof(text->lines[line])) return false;
        if (length > longest) longest = (unsigned int)length;
    }
    /* The background follows the actual row count and widest rendered metric. */
    add_quad(vertices, 0.0f, 0.0f,
             (float)(FRAME_PACER_HUD_TEXT_X +
                     longest * FRAME_PACER_HUD_CHARACTER_ADVANCE +
                     FRAME_PACER_HUD_PANEL_RIGHT_PADDING),
             (float)(FRAME_PACER_HUD_TEXT_Y +
                     text->line_count * FRAME_PACER_HUD_LINE_ADVANCE +
                     FRAME_PACER_HUD_PANEL_BOTTOM_PADDING), panel_color);
    for (line = 0; line < text->line_count; ++line) {
        unsigned int character;

        for (character = 0; text->lines[line][character]; ++character) {
            const float *color =
                character < 3 ? label_color(text->lines[line], line, label_colors) : text_color;
            unsigned int y;

            for (y = 0; y < FRAME_PACER_FONT_HEIGHT; ++y) {
                unsigned int x;

                for (x = 0; x < FRAME_PACER_FONT_WIDTH; ++x) {
                    if (frame_pacer_font_pixel(
                            (unsigned char)text->lines[line][character], x, y)) {
                        float pixel_x;
                        float pixel_y;

                        if (vertices->count >
                            FRAME_PACER_HUD_MAX_VERTICES - 6) {
                            vertices->count = 0;
                            return false;
                        }
                        pixel_x = (float)FRAME_PACER_HUD_TEXT_X +
                                  (float)(character *
                                              FRAME_PACER_HUD_CHARACTER_ADVANCE +
                                          x * FRAME_PACER_HUD_PIXEL_SIZE);
                        pixel_y = (float)FRAME_PACER_HUD_TEXT_Y +
                                  (float)(line * FRAME_PACER_HUD_LINE_ADVANCE +
                                          y * FRAME_PACER_HUD_PIXEL_SIZE);
                        add_quad(vertices, pixel_x, pixel_y,
                                 (float)FRAME_PACER_HUD_PIXEL_SIZE,
                                 (float)FRAME_PACER_HUD_PIXEL_SIZE, color);
                    }
                }
            }
        }
    }
    return true;
}
