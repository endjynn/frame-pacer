#include "hud_text.h"
#include "hud_vertices.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct bytes {
    unsigned char *data;
    size_t size;
    size_t capacity;
};

static void append(struct bytes *output, const void *data, size_t size)
{
    size_t required = output->size + size;

    if (required < output->size)
        exit(1);
    if (required > output->capacity) {
        size_t capacity = output->capacity ? output->capacity : 4096;
        unsigned char *replacement;

        while (capacity < required) {
            if (capacity > SIZE_MAX / 2)
                exit(1);
            capacity *= 2;
        }
        replacement = realloc(output->data, capacity);
        if (!replacement)
            exit(1);
        output->data = replacement;
        output->capacity = capacity;
    }
    if (size)
        memcpy(output->data + output->size, data, size);
    output->size = required;
}

static void append_u32(struct bytes *output, uint32_t value)
{
    unsigned char data[4] = {
        (unsigned char)(value >> 24),
        (unsigned char)(value >> 16),
        (unsigned char)(value >> 8),
        (unsigned char)value,
    };

    append(output, data, sizeof(data));
}

static uint32_t crc32(const unsigned char *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    size_t index;

    for (index = 0; index < size; ++index) {
        unsigned int bit;

        crc ^= data[index];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^
                  (UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1));
    }
    return ~crc;
}

static uint32_t adler32(const unsigned char *data, size_t size)
{
    uint32_t a = 1, b = 0;
    size_t index;

    for (index = 0; index < size; ++index) {
        a = (a + data[index]) % 65521U;
        b = (b + a) % 65521U;
    }
    return (b << 16) | a;
}

static void chunk(struct bytes *png, const char type[4],
                  const unsigned char *data, size_t size)
{
    struct bytes checksum = {0};

    if (size > UINT32_MAX)
        exit(1);
    append_u32(png, (uint32_t)size);
    append(png, type, 4);
    append(png, data, size);
    append(&checksum, type, 4);
    append(&checksum, data, size);
    append_u32(png, crc32(checksum.data, checksum.size));
    free(checksum.data);
}

static void encode_png(struct bytes *png, const unsigned char *pixels,
                       uint32_t width, uint32_t height)
{
    static const unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    struct bytes raw = {0}, compressed = {0};
    unsigned char header[13] = {0};
    uint32_t row, checksum;
    size_t offset;
    if (!width || !height || (size_t)width > SIZE_MAX / height / 4)
        exit(1);
    append(png, signature, sizeof(signature));
    header[0] = (unsigned char)(width >> 24);
    header[1] = (unsigned char)(width >> 16);
    header[2] = (unsigned char)(width >> 8);
    header[3] = (unsigned char)width;
    header[4] = (unsigned char)(height >> 24);
    header[5] = (unsigned char)(height >> 16);
    header[6] = (unsigned char)(height >> 8);
    header[7] = (unsigned char)height;
    header[8] = 8;
    header[9] = 6;
    chunk(png, "IHDR", header, sizeof(header));
    for (row = 0; row < height; ++row) {
        const unsigned char filter = 0;
        append(&raw, &filter, 1);
        append(&raw, pixels + (size_t)row * width * 4, (size_t)width * 4);
    }
    {
        const unsigned char zlib_header[2] = {0x78, 0x01};

        append(&compressed, zlib_header, sizeof(zlib_header));
    }
    for (offset = 0; offset < raw.size;) {
        size_t remaining = raw.size - offset;
        uint16_t length = (uint16_t)(remaining > 65535 ? 65535 : remaining);
        uint16_t inverse = (uint16_t)~length;
        unsigned char block[5] = {
            (unsigned char)(remaining <= 65535), (unsigned char)length,
            (unsigned char)(length >> 8),        (unsigned char)inverse,
            (unsigned char)(inverse >> 8),
        };

        append(&compressed, block, sizeof(block));
        append(&compressed, raw.data + offset, length);
        offset += length;
    }
    checksum = adler32(raw.data, raw.size);
    append_u32(&compressed, checksum);
    chunk(png, "IDAT", compressed.data, compressed.size);
    chunk(png, "IEND", 0, 0);
    free(compressed.data);
    free(raw.data);
}

static unsigned char channel(float value)
{
    if (value <= 0.0f)
        return 0;
    if (value >= 1.0f)
        return 255;
    return (unsigned char)(value * 255.0f + 0.5f);
}

int main(int argc, char **argv)
{
    const struct frame_pacer_font_atlas *atlas;
    uint32_t width, height;
    struct frame_pacer_metrics_snapshot metrics = {
        .available = FRAME_PACER_METRIC_GPU_USE | FRAME_PACER_METRIC_GPU_TEMP |
                     FRAME_PACER_METRIC_CPU_USE | FRAME_PACER_METRIC_CPU_TEMP |
                     FRAME_PACER_METRIC_THREAD_CPU_USE,
        .gpu_use_percent = 100,
        .gpu_temp_celsius = 61,
        .cpu_use_percent = 83,
        .cpu_temp_celsius = 73,
        .thread_cpu_percent = 50,
    };
    struct frame_pacer_hud_text text;
    struct frame_pacer_hud_vertices *vertices;
    struct bytes png = {0};
    unsigned char *pixels = NULL;
    FILE *file;
    uint32_t index;
    int result = 1;

    if (argc != 2)
        return 64;
    vertices = calloc(1, sizeof(*vertices));
    if (!vertices)
        goto cleanup;
    frame_pacer_hud_text_format(&text, &metrics, true, 999, 999, true, true,
                                50);
    if (!frame_pacer_hud_vertices_build_for_extent(vertices, &text, 2560, 1600))
        goto cleanup;
    atlas = vertices->atlas;
    width = (uint32_t)vertices->data[1].position[0];
    height = (uint32_t)vertices->data[2].position[1];
    pixels = calloc((size_t)width * height, 4);
    if (!pixels)
        goto cleanup;
    for (index = 0; index < vertices->count; index += 6) {
        const struct frame_pacer_hud_vertex *quad = &vertices->data[index];
        uint32_t x0 = (uint32_t)quad[0].position[0];
        uint32_t y0 = (uint32_t)quad[0].position[1];
        uint32_t x1 = (uint32_t)quad[2].position[0];
        uint32_t y1 = (uint32_t)quad[2].position[1];
        uint32_t y, x;

        if ((float)x1 < quad[2].position[0])
            ++x1;
        if ((float)y1 < quad[2].position[1])
            ++y1;
        if (x1 > width || y1 > height || x0 > x1 || y0 > y1)
            goto cleanup;
        for (y = y0; y < y1; ++y)
            for (x = x0; x < x1; ++x) {
                unsigned char *pixel = &pixels[((size_t)y * width + x) * 4];

                float coverage = 1.0f;
                if (quad[0].uv[0] >= 0) {
                    unsigned int atlas_x =
                        (unsigned int)(quad[0].uv[0] * atlas->width + 0.5f) +
                        x - x0;
                    unsigned int atlas_y =
                        (unsigned int)(quad[0].uv[1] * atlas->height + 0.5f) +
                        y - y0;
                    coverage = frame_pacer_font_atlas_pixels(
                                   atlas)[atlas_y * atlas->width + atlas_x] /
                               255.0f;
                }
                for (unsigned int component = 0; component < 3; ++component)
                    pixel[component] =
                        channel(quad[0].color[component] * coverage +
                                pixel[component] / 255.0f * (1.0f - coverage));
                pixel[3] = 255;
            }
    }
    encode_png(&png, pixels, width, height);
    file = fopen(argv[1], "wb");
    if (!file)
        goto cleanup;
    if (fwrite(png.data, 1, png.size, file) != png.size) {
        (void)fclose(file);
        goto cleanup;
    }
    if (fclose(file))
        goto cleanup;
    result = 0;
cleanup:
    free(png.data);
    free(vertices);
    free(pixels);
    return result;
}
