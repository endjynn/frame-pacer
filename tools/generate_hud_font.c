/* Offline only. FreeType is never linked into frame-pacer. */
#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ATLAS_WIDTH 256U
#define ATLAS_HEIGHT_MAX 1024U

static const unsigned char characters[] = " %/0123456789ACFGHNOPRSTU\176\177";

static void require(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv)
{
    FT_Library library;
    FT_Face face;
    FT_Int major, minor, patch;
    uint8_t *pixels = calloc(ATLAS_WIDTH, ATLAS_HEIGHT_MAX);
    unsigned long offset = 0;
    FILE *binary;
    FILE *metadata;

    require(argc == 4, "usage: generate-hud-font FONT OUTPUT.bin OUTPUT.json");
    require(pixels != NULL, "out of memory");
    require(!FT_Init_FreeType(&library), "FreeType initialization failed");
    FT_Library_Version(library, &major, &minor, &patch);
    require(major == 2 && minor == 14 && patch == 3,
            "asset generation requires FreeType 2.14.3");
    require(!FT_New_Face(library, argv[1], 0, &face), "font load failed");
    require(!FT_Select_Charmap(face, FT_ENCODING_UNICODE),
            "Unicode map missing");
    binary = fopen(argv[2], "wb");
    metadata = fopen(argv[3], "w");
    require(binary && metadata, "cannot create output");
    fputs("{\"freetype\":\"2.14.3\",\"sizes\":[\n", metadata);
    for (unsigned int size = 8; size <= 64; ++size) {
        unsigned int x = 1, y = 1, row_height = 0;
        int ascent = 0, descent = 0, advance = 0, leftmost = 0, rightmost = 0;
        memset(pixels, 0, ATLAS_WIDTH * ATLAS_HEIGHT_MAX);
        require(!FT_Set_Pixel_Sizes(face, 0, size), "cannot set font size");
        require(
            !FT_Load_Char(face, '0', FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL),
            "cannot render zero");
        advance = (int)((face->glyph->advance.x + 32) / 64);
        FT_Pos native_advance = face->glyph->advance.x;
        int font_ascent = (int)((face->size->metrics.ascender + 63) / 64);
        int font_descent = (int)((-face->size->metrics.descender + 63) / 64);
        int line_height = (int)((face->size->metrics.height + 32) / 64);
        fprintf(metadata,
                "%s{\"size\":%u,\"offset\":%lu,\"width\":%u,"
                "\"glyphs\":[",
                size == 8 ? "" : ",\n", size, offset, ATLAS_WIDTH);
        for (unsigned int index = 0; index < sizeof(characters) - 1; ++index) {
            unsigned int character = characters[index];
            FT_ULong codepoint = character == 127   ? 0xb0UL
                                 : character == 126 ? 'F'
                                                    : character;
            /* Keep the existing compact, elevated frame-unit marker. */
            unsigned int glyph_size =
                character == 126 ? (size * 2U + 1U) / 3U : size;
            require(!FT_Set_Pixel_Sizes(face, 0, glyph_size),
                    "cannot set glyph size");
            require(FT_Get_Char_Index(face, codepoint) != 0,
                    "required glyph missing");
            require(!FT_Load_Char(face, codepoint,
                                  FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL),
                    "glyph rasterization failed");
            FT_GlyphSlot glyph = face->glyph;
            require(character == 126 || glyph->advance.x == native_advance,
                    "font does not have a uniform character advance");
            FT_Bitmap *bitmap = &glyph->bitmap;
            unsigned int width = bitmap->width, height = bitmap->rows;
            int top = glyph->bitmap_top;
            if (character == 126)
                top += (int)(size - glyph_size);
            require(bitmap->pixel_mode == FT_PIXEL_MODE_GRAY &&
                        bitmap->pitch >= 0,
                    "unexpected bitmap format");
            require(width + 2 < ATLAS_WIDTH, "glyph too wide");
            if (x + width + 1 > ATLAS_WIDTH) {
                x = 1;
                y += row_height + 2;
                row_height = 0;
            }
            require(y + height + 1 <= ATLAS_HEIGHT_MAX, "atlas overflow");
            for (unsigned int row = 0; row < height; ++row)
                memcpy(pixels + (y + row) * ATLAS_WIDTH + x,
                       bitmap->buffer + row * (unsigned int)bitmap->pitch,
                       width);
            if (top > ascent)
                ascent = top;
            if ((int)height - top > descent)
                descent = (int)height - top;
            if (glyph->bitmap_left < leftmost)
                leftmost = glyph->bitmap_left;
            if (glyph->bitmap_left + (int)width > rightmost)
                rightmost = glyph->bitmap_left + (int)width;
            fprintf(metadata, "%s[%u,%u,%u,%u,%u,%d,%d]", index ? "," : "",
                    character, x, y, width, height, glyph->bitmap_left, top);
            x += width + 2;
            if (height > row_height)
                row_height = height;
        }
        unsigned int height = y + row_height + 1;
        unsigned int bytes = ATLAS_WIDTH * height;
        fprintf(metadata,
                "],\"height\":%u,\"ascent\":%d,\"descent\":%d,"
                "\"advance\":%d,\"ink_left\":%d,\"ink_right\":%d,"
                "\"font_ascent\":%d,\"font_descent\":%d,\"line_height\":%d}",
                height, ascent, descent, advance, leftmost, rightmost,
                font_ascent, font_descent, line_height);
        require(fwrite(pixels, 1, bytes, binary) == bytes,
                "atlas write failed");
        offset += bytes;
    }
    fputs("\n]}\n", metadata);
    require(!fclose(binary), "atlas close failed");
    require(!fclose(metadata), "metadata close failed");
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    free(pixels);
    return 0;
}
