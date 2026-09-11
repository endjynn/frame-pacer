#!/usr/bin/env python3
"""Render generated HUD glyphs at native size using only the Python standard library."""

import argparse
import json
from pathlib import Path
import struct
import zlib


def png(path, width, height, pixels):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assets", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    atlas = (args.assets / "hud-atlas.bin").read_bytes()
    sizes = {size["size"]: size for size in json.loads((args.assets / "hud-atlas.json").read_text())["sizes"]}
    width, height = 640, 650
    pixels = bytearray([24, 28, 32] * width * height)
    cursor_y = 12
    colors = [(0, 230, 153), (0, 166, 255), (191, 89, 255), (255, 51, 51)]
    lines = ["GPU  27%  62\x7f", "CPU   9%  81\x7f", "THR  16%  50%", "FPS  60\x7e  60\x7e"]
    for size in (14, 16, 24, 32):
        entry = sizes[size]
        glyphs = {glyph[0]: glyph for glyph in entry["glyphs"]}
        line_height = entry["line_height"]
        for line_index, line in enumerate(lines):
            for column, character in enumerate(line):
                _, gx, gy, gw, gh, left, top = glyphs[ord(character)]
                color = colors[line_index] if column < 3 else (255, 255, 255)
                origin_x = 12 - min(0, entry["ink_left"]) + column * entry["advance"] + left
                origin_y = cursor_y + line_index * line_height + entry["font_ascent"] - top
                for y in range(gh):
                    for x in range(gw):
                        coverage = atlas[entry["offset"] + (gy + y) * entry["width"] + gx + x]
                        offset = ((origin_y + y) * width + origin_x + x) * 3
                        for channel in range(3):
                            pixels[offset + channel] = (color[channel] * coverage + pixels[offset + channel] * (255 - coverage) + 127) // 255
        cursor_y += len(lines) * line_height + 24
    png(args.output, width, height, pixels)


if __name__ == "__main__":
    main()
