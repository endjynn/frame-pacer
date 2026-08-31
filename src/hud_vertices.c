#include "hud_vertices.h"

#include "hud_font.h"

#include <string.h>

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

static uint32_t pixel_size_for(uint32_t width, uint32_t height)
{
    uint64_t width_size, height_size;
    uint32_t size, fit_width, fit_height, fit;

    if (!width || !height) return 0;
    /* Round the limiting axis to the nearest whole font pixel.  Integer
     * geometry keeps the 5x7 bitmap font sharp at every selected size. */
    width_size = ((uint64_t)width * FRAME_PACER_HUD_REFERENCE_PIXEL_SIZE +
                  FRAME_PACER_HUD_REFERENCE_VIEWPORT_WIDTH / 2U) /
                 FRAME_PACER_HUD_REFERENCE_VIEWPORT_WIDTH;
    height_size = ((uint64_t)height * FRAME_PACER_HUD_REFERENCE_PIXEL_SIZE +
                   FRAME_PACER_HUD_REFERENCE_VIEWPORT_HEIGHT / 2U) /
                  FRAME_PACER_HUD_REFERENCE_VIEWPORT_HEIGHT;
    size = (uint32_t)(width_size < height_size ? width_size : height_size);
    if (!size) size = 1;

    /* Even unusual tiny drawables must never receive geometry outside their
     * bounds.  Account for the optional fourth row so toggling it cannot
     * silently overflow or change the selected scale. */
    fit_width = width / FRAME_PACER_HUD_WIDTH_UNITS;
    fit_height = height / FRAME_PACER_HUD_HEIGHT_UNITS;
    fit = fit_width < fit_height ? fit_width : fit_height;
    return size < fit ? size : fit;
}

bool frame_pacer_hud_vertices_build_for_extent(
    struct frame_pacer_hud_vertices *vertices,
    const struct frame_pacer_hud_text *text, uint32_t drawable_width,
    uint32_t drawable_height)
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
    uint32_t pixel_size;
    unsigned int line, longest = 0;

    if (!vertices || !text)
        return false;
    vertices->count = 0;
    pixel_size = pixel_size_for(drawable_width, drawable_height);
    if (!pixel_size) return false;
    if (text->line_count < 3 || text->line_count > FRAME_PACER_HUD_LINE_COUNT_MAX)
        return false;
    for (line = 0; line < text->line_count; ++line) {
        size_t length = 0;

        while (length < sizeof(text->lines[line]) && text->lines[line][length]) ++length;
        if (length == sizeof(text->lines[line]) ||
            length > FRAME_PACER_HUD_LINE_CHARACTERS_MAX)
            return false;
        if (length > longest) longest = (unsigned int)length;
    }
    /* The background follows the actual row count and widest rendered metric. */
    add_quad(vertices, 0.0f, 0.0f,
             (float)(pixel_size *
                     (FRAME_PACER_HUD_TEXT_X_UNITS +
                      longest * FRAME_PACER_HUD_CHARACTER_ADVANCE_UNITS +
                      FRAME_PACER_HUD_PANEL_RIGHT_PADDING_UNITS)),
             (float)(pixel_size *
                     (FRAME_PACER_HUD_TEXT_Y_UNITS +
                      text->line_count * FRAME_PACER_HUD_LINE_ADVANCE_UNITS +
                      FRAME_PACER_HUD_PANEL_BOTTOM_PADDING_UNITS)), panel_color);
    for (line = 0; line < text->line_count; ++line) {
        unsigned int character;

        for (character = 0; text->lines[line][character]; ++character) {
            const float *color =
                character < 3 ? label_color(text->lines[line], line, label_colors) : text_color;
            unsigned int y;

            for (y = 0; y < FRAME_PACER_FONT_HEIGHT; ++y) {
                uint8_t pixels = frame_pacer_font_row(
                    (unsigned char)text->lines[line][character], y);
                unsigned int x;

                for (x = 0; x < FRAME_PACER_FONT_WIDTH; ++x) {
                    if (pixels &
                        (1U << (FRAME_PACER_FONT_WIDTH - 1U - x))) {
                        float pixel_x;
                        float pixel_y;

                        if (vertices->count >
                            FRAME_PACER_HUD_MAX_VERTICES - 6) {
                            vertices->count = 0;
                            return false;
                        }
                        pixel_x = (float)(pixel_size *
                            (FRAME_PACER_HUD_TEXT_X_UNITS +
                             character * FRAME_PACER_HUD_CHARACTER_ADVANCE_UNITS +
                             x));
                        pixel_y = (float)(pixel_size *
                            (FRAME_PACER_HUD_TEXT_Y_UNITS +
                             line * FRAME_PACER_HUD_LINE_ADVANCE_UNITS + y));
                        add_quad(vertices, pixel_x, pixel_y,
                                 (float)pixel_size, (float)pixel_size, color);
                    }
                }
            }
        }
    }
    return true;
}

bool frame_pacer_hud_vertices_build(struct frame_pacer_hud_vertices *vertices,
                                    const struct frame_pacer_hud_text *text)
{
    return frame_pacer_hud_vertices_build_for_extent(
        vertices, text, FRAME_PACER_HUD_REFERENCE_VIEWPORT_WIDTH,
        FRAME_PACER_HUD_REFERENCE_VIEWPORT_HEIGHT);
}
