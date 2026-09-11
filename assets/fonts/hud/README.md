# HUD coverage atlases

Generated from unmodified JetBrains Mono Medium v2.304 using FreeType 2.14.3.
Font copyright and SIL Open Font License 1.1 are in
[../jetbrains-mono/OFL.txt](../jetbrains-mono/OFL.txt). These font-derived assets
remain separately identified from frame-pacer's MIT-licensed application code.

`hud-atlas.bin` contains consecutive uncompressed 8-bit grayscale coverage
atlases, one per integer font size from 8 through 64 pixels. `hud-atlas.json`
records each atlas offset, extent, native fixed character advance, native font
line metrics, and glyph ink bounds. Glyph records are:

```text
[character_id, atlas_x, atlas_y, width, height, bitmap_left, bitmap_top]
```

`hud-atlas.inc` contains the generated C metrics used by the renderer.
`hud_font_data.S` embeds the raw bytes in read-only storage; normal builds do
not run the generator. The distributed payload includes the complete font
copyright and license as `LICENSE-JetBrainsMono.txt` beside the runtime files.

Character IDs 126 and 127 retain the HUD's existing internal frame-unit and
degree markers. The frame-unit marker uses an elevated smaller F; it occupies
the same full character advance as other glyphs. The degree marker maps to
Unicode U+00B0. All other glyphs use their ordinary Unicode mappings and font
size. Grayscale coverage is not RGB subpixel rendering.

Glyph ink may extend past its advance (for example `%` at some sizes). Preserve
native advances and line metrics; grow panel bounds/padding to contain ink.
Do not stretch glyphs or widen the character grid to fit their ink bounds.

Regenerate from the repository root:

```sh
python3 tools/generate_hud_font.py --weight Medium --output assets/fonts/hud
python3 tools/generate_hud_font.py --weight Medium --output assets/fonts/hud --check
python3 tests/test_font_assets.py
```

The generator verifies the font and FreeType archive SHA-256 values, builds a
static FreeType in `build/font-tools`, and generates twice to verify identical
output. The first run downloads the pinned FreeType source if absent; later
runs can use that verified archive offline. No system installation occurs.
Normal frame-pacer builds and the asset integrity tests do not invoke FreeType
or access the network. `manifest.json` records provenance and output hashes.

To inspect the native-size HUD samples without additional Python packages:

```sh
python3 tools/preview_hud_font.py assets/fonts/hud build/hud-font-preview.png
```

The preview uses sizes 14, 16, 24, and 32.
