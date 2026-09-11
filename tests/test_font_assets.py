#!/usr/bin/env python3
"""Check embedded font inputs without FreeType, Pillow, or network access."""

import hashlib
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets/fonts/hud"


class FontAssetsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = (ASSETS / "hud-atlas.bin").read_bytes()
        cls.metadata = json.loads((ASSETS / "hud-atlas.json").read_text())
        cls.manifest = json.loads((ASSETS / "manifest.json").read_text())

    def test_provenance(self):
        self.assertEqual(self.manifest["font"], "JetBrains Mono Medium")
        self.assertEqual(self.manifest["font_version"], "2.304")
        self.assertEqual(self.manifest["freetype_version"], "2.14.3")
        for file, key in (("hud-atlas.bin", "atlas_sha256"), ("hud-atlas.json", "metadata_sha256"),
                          ("hud-atlas.inc", "c_metadata_sha256")):
            self.assertEqual(hashlib.sha256((ASSETS / file).read_bytes()).hexdigest(), self.manifest[key])
        font = ROOT / "assets/fonts/jetbrains-mono/JetBrainsMono-Medium.ttf"
        self.assertEqual(hashlib.sha256(font.read_bytes()).hexdigest(), self.manifest["font_sha256"])
        self.assertIn("SIL OPEN FONT LICENSE Version 1.1", (font.parent / "OFL.txt").read_text())

    def test_sizes_glyphs_and_padding(self):
        entries = self.metadata["sizes"]
        self.assertEqual([entry["size"] for entry in entries], list(range(8, 65)))
        expected = set(b" %/0123456789ACFGHNOPRSTU\x7e\x7f")
        offset = 0
        for entry in entries:
            with self.subTest(size=entry["size"]):
                self.assertEqual(entry["offset"], offset)
                # JetBrains Mono v2.304 has a 600/1000-em fixed advance.
                # Ink extents (notably %) must never widen that native spacing.
                self.assertEqual(entry["advance"], (entry["size"] * 3 + 2) // 5)
                self.assertGreater(entry["line_height"], 0)
                width, height = entry["width"], entry["height"]
                atlas = self.data[offset:offset + width * height]
                self.assertEqual(len(atlas), width * height)
                self.assertEqual({glyph[0] for glyph in entry["glyphs"]}, expected)
                self.assertEqual(len(entry["glyphs"]), len(expected))
                occupied = set()
                for character, x, y, w, h, left, top in entry["glyphs"]:
                    self.assertGreater(x, 0)
                    self.assertGreater(y, 0)
                    self.assertLess(x + w, width)
                    self.assertLess(y + h, height)
                    self.assertGreaterEqual(left, entry["ink_left"])
                    self.assertLessEqual(left + w, entry["ink_right"])
                    self.assertLessEqual(top, entry["ascent"])
                    self.assertLessEqual(h - top, entry["descent"])
                    coverage = []
                    for row in range(y - 1, y + h + 1):
                        for column in range(x - 1, x + w + 1):
                            value = atlas[row * width + column]
                            if row in (y - 1, y + h) or column in (x - 1, x + w):
                                self.assertEqual(value, 0, "nonzero sampling gutter")
                            else:
                                position = row * width + column
                                self.assertNotIn(position, occupied)
                                occupied.add(position)
                                coverage.append(value)
                    if character != 32:
                        self.assertTrue(any(coverage))
                        self.assertTrue(any(0 < value < 255 for value in coverage), "no grayscale antialiasing")
                offset += width * height
        self.assertEqual(offset, len(self.data))

    def test_percent_overhang_does_not_change_advance(self):
        entry = next(item for item in self.metadata["sizes"] if item["size"] == 14)
        percent = next(glyph for glyph in entry["glyphs"] if glyph[0] == ord("%"))
        self.assertEqual(entry["advance"], 8)
        self.assertEqual(percent[5] + percent[3], 9)
        self.assertGreaterEqual(entry["ink_right"], 9)


if __name__ == "__main__":
    unittest.main()
