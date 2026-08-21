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

    if (required < output->size) exit(1);
    if (required > output->capacity) {
        size_t capacity = output->capacity ? output->capacity : 4096;
        unsigned char *replacement;

        while (capacity < required) {
            if (capacity > SIZE_MAX / 2) exit(1);
            capacity *= 2;
        }
        replacement = realloc(output->data, capacity);
        if (!replacement) exit(1);
        output->data = replacement;
        output->capacity = capacity;
    }
    if (size) memcpy(output->data + output->size, data, size);
    output->size = required;
}

static void append_u32(struct bytes *output, uint32_t value)
{
    unsigned char data[4] = {
        (unsigned char)(value >> 24), (unsigned char)(value >> 16),
        (unsigned char)(value >> 8), (unsigned char)value,
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
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                                (uint32_t)-(int32_t)(crc & 1));
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

    if (size > UINT32_MAX) exit(1);
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
    static const unsigned char signature[8] =
        {137, 80, 78, 71, 13, 10, 26, 10};
    struct bytes raw = {0}, compressed = {0};
    unsigned char header[13] = {0};
    unsigned char rgba[256][4], palette[256 * 3], transparency[256];
    unsigned char *indices, *packed;
    uint32_t palette_count = 0, row, checksum;
    unsigned int bit_depth;
    size_t pixel_count, pixel_index, row_size;
    size_t offset;

    if (!width || !height || (size_t)width > SIZE_MAX / height)
        exit(1);
    pixel_count = (size_t)width * height;
    indices = malloc(pixel_count);
    if (!indices) exit(1);
    for (pixel_index = 0; pixel_index < pixel_count; ++pixel_index) {
        const unsigned char *pixel = pixels + pixel_index * 4;
        uint32_t palette_index;

        for (palette_index = 0; palette_index < palette_count;
             ++palette_index)
            if (!memcmp(rgba[palette_index], pixel, 4))
                break;
        if (palette_index == palette_count) {
            if (palette_count == 256) exit(1);
            memcpy(rgba[palette_count], pixel, 4);
            palette[palette_count * 3] = pixel[0];
            palette[palette_count * 3 + 1] = pixel[1];
            palette[palette_count * 3 + 2] = pixel[2];
            transparency[palette_count] = pixel[3];
            ++palette_count;
        }
        indices[pixel_index] = (unsigned char)palette_index;
    }
    bit_depth = palette_count <= 2 ? 1 : palette_count <= 4 ? 2 :
                palette_count <= 16 ? 4 : 8;
    if ((size_t)width > (SIZE_MAX - 7) / bit_depth) exit(1);
    row_size = ((size_t)width * bit_depth + 7) / 8;
    packed = calloc(row_size, 1);
    if (!packed) exit(1);

    append(png, signature, sizeof(signature));
    header[0] = (unsigned char)(width >> 24);
    header[1] = (unsigned char)(width >> 16);
    header[2] = (unsigned char)(width >> 8);
    header[3] = (unsigned char)width;
    header[4] = (unsigned char)(height >> 24);
    header[5] = (unsigned char)(height >> 16);
    header[6] = (unsigned char)(height >> 8);
    header[7] = (unsigned char)height;
    header[8] = (unsigned char)bit_depth;
    header[9] = 3;
    chunk(png, "IHDR", header, sizeof(header));
    chunk(png, "PLTE", palette, (size_t)palette_count * 3);
    chunk(png, "tRNS", transparency, palette_count);
    for (row = 0; row < height; ++row) {
        const unsigned char filter = 0;
        uint32_t column;

        memset(packed, 0, row_size);
        for (column = 0; column < width; ++column) {
            size_t bit = (size_t)column * bit_depth;
            unsigned int shift = 8U - bit_depth - (unsigned int)(bit % 8);

            packed[bit / 8] |= (unsigned char)(
                indices[(size_t)row * width + column] << shift);
        }
        append(&raw, &filter, 1);
        append(&raw, packed, row_size);
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
            (unsigned char)(remaining <= 65535),
            (unsigned char)length, (unsigned char)(length >> 8),
            (unsigned char)inverse, (unsigned char)(inverse >> 8),
        };

        append(&compressed, block, sizeof(block));
        append(&compressed, raw.data + offset, length);
        offset += length;
    }
    checksum = adler32(raw.data, raw.size);
    append_u32(&compressed, checksum);
    chunk(png, "IDAT", compressed.data, compressed.size);
    chunk(png, "IEND", 0, 0);
    free(packed);
    free(indices);
    free(compressed.data);
    free(raw.data);
}

static unsigned char channel(float value)
{
    if (value <= 0.0f) return 0;
    if (value >= 1.0f) return 255;
    return (unsigned char)(value * 255.0f + 0.5f);
}

int main(int argc, char **argv)
{
    const uint32_t width = FRAME_PACER_HUD_WIDTH_MAX;
    const uint32_t height = FRAME_PACER_HUD_HEIGHT_MAX;
    struct frame_pacer_metrics_snapshot metrics = {
        .available = FRAME_PACER_METRIC_GPU_USE |
                     FRAME_PACER_METRIC_GPU_TEMP |
                     FRAME_PACER_METRIC_CPU_USE |
                     FRAME_PACER_METRIC_CPU_TEMP |
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
    unsigned char *pixels;
    FILE *file;
    uint32_t index;

    if (argc != 2) return 64;
    pixels = calloc((size_t)width * height, 4);
    vertices = calloc(1, sizeof(*vertices));
    if (!pixels || !vertices) return 1;
    frame_pacer_hud_text_format(&text, &metrics, true, 999, 999, true, true,
                                50);
    if (!frame_pacer_hud_vertices_build(vertices, &text)) return 1;
    for (index = 0; index < vertices->count; index += 6) {
        const struct frame_pacer_hud_vertex *quad = &vertices->data[index];
        uint32_t x0 = (uint32_t)quad[0].position[0];
        uint32_t y0 = (uint32_t)quad[0].position[1];
        uint32_t x1 = (uint32_t)quad[2].position[0];
        uint32_t y1 = (uint32_t)quad[2].position[1];
        uint32_t y, x;

        if (x1 > width || y1 > height || x0 > x1 || y0 > y1) return 1;
        for (y = y0; y < y1; ++y)
            for (x = x0; x < x1; ++x) {
                unsigned char *pixel = &pixels[((size_t)y * width + x) * 4];

                pixel[0] = channel(quad[0].color[0]);
                pixel[1] = channel(quad[0].color[1]);
                pixel[2] = channel(quad[0].color[2]);
                pixel[3] = 255;
            }
    }
    encode_png(&png, pixels, width, height);
    file = fopen(argv[1], "wb");
    if (!file) return 1;
    if (fwrite(png.data, 1, png.size, file) != png.size) {
        (void)fclose(file);
        return 1;
    }
    if (fclose(file)) return 1;
    free(png.data);
    free(vertices);
    free(pixels);
    return 0;
}
