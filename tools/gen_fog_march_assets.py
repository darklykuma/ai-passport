#!/usr/bin/env python3
"""Generate FOG MARCH sprite assets from hand-authored ASCII pixel maps.

Source of truth: the SPRITES tables below (26x26 character grids, one char per
pixel). The script renders each grid with its palette into:

  main/assets/fog_<name>.rgb565      opaque tiles, raw little-endian RGB565
  main/assets/fog_<name>.rgb565a8     units with alpha, RGB565 plane + A8 plane
  assets/fog-march/preview/*.png      per-sprite previews (units over a tile)
  assets/fog-march/preview/fog_march_contact_sheet.png

The firmware embeds the raw blobs via EMBED_FILES and wraps them in
lv_image_dsc_t (LV_COLOR_FORMAT_RGB565 / RGB565A8), the same pattern as
demo/rock-paper-scissors. Output is deterministic: no timestamps, fixed zlib
level, identical bytes for identical input.

Regenerate after editing any map:

    python3 tools/gen_fog_march_assets.py
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAW_DIR = ROOT / "main" / "assets"
PREVIEW_DIR = ROOT / "assets" / "fog-march" / "preview"

SIZE = 26

# ---------------------------------------------------------------------------
# Palettes. Terrain sprites pick the chars they use; unit sprites share the
# side palettes below (blue = player, red = enemy).
# ---------------------------------------------------------------------------

TERRAIN_COLORS = {
    # ground (plain)
    "b": (63, 72, 57),      # base moss gray
    "d": (52, 60, 47),      # dark speck
    "l": (74, 84, 66),      # light speck
    # rock
    "m": (140, 123, 100),   # rock mid
    "s": (101, 87, 65),     # rock shadow face
    "h": (169, 152, 120),   # rock lit face
    "c": (232, 230, 223),   # snow
    # forest
    "q": (34, 51, 31),      # forest ground base
    "r": (27, 41, 23),      # forest ground speck
    "G": (46, 123, 58),     # canopy mid
    "g": (63, 156, 76),     # canopy lit
    "e": (26, 78, 38),      # canopy under-shade
    "t": (92, 70, 48),      # trunk
    "T": (66, 50, 34),      # trunk dark base
    # water
    "u": (30, 62, 117),     # river base
    "v": (59, 111, 180),    # wave
    "x": (143, 180, 232),   # sparkle
    "U": (22, 48, 92),      # deep patch
    # city wall
    "y": (192, 145, 63),    # ochre base
    "Y": (216, 172, 85),    # ochre lit
    "z": (150, 112, 44),    # ochre mortar/joint
    "n": (36, 26, 16),      # gate void
    "k": (20, 16, 12),      # outline
}

UNIT_COMMON = {
    "k": (20, 16, 12),      # outline
    "f": (232, 195, 154),   # skin
    "w": (201, 206, 214),   # steel
    "W": (130, 137, 154),   # steel dark
    "t": (122, 90, 51),     # wood
    "T": (87, 64, 31),      # wood dark
    "o": (224, 178, 60),    # gold
    "H": (138, 90, 46),     # horse body
    "J": (107, 67, 31),     # horse dark
}

SIDE_BLUE = {
    "A": (61, 125, 216),    # armor main
    "C": (111, 168, 242),   # armor lit
    "E": (40, 86, 155),     # armor dark
}

SIDE_RED = {
    "A": (216, 72, 58),
    "C": (242, 128, 110),
    "E": (155, 44, 36),
}

# ---------------------------------------------------------------------------
# Terrain maps. 26 rows x 26 cols; every char must exist in TERRAIN_COLORS.
# ---------------------------------------------------------------------------

PLAIN = [
    "bbbbdbbbbbbbbbbbbbbblbbbbb",
    "bbbbbbbbblbbbbbbbbbbbbdbbb",
    "bbbbbbbbbbbbblbbbbbbbbbbbb",
    "bbdbbbbbbbbbbbbbbbbblbbbbb",
    "bbbbbbbbbbdbbbbbbbbbbbbbbb",
    "blbbbbbbbbbbbbbbdbbbbbbbbb",
    "bbbbbbbbbbbbbbbbbbbbblbbbb",
    "bbbbbbblbbbbbbbbbbbbbbbbdb",
    "bbbbbbbbbbbbdbbbbbbbbbbbbb",
    "bdbbbbbbbbbbbbbbbbblbbbbbb",
    "bbbbbbbbbbbbbbbbbbbbbdbbbb",
    "bbbblbbbbbbbbdbbbbbbbbbbbb",
    "bbbbbbbbbbbbbbbbbbbbbbbbbb",
    "bbbbbbbbdbbbbbbbbbbbblbbbb",
    "blbbbbbbbbbbbbbdbbbbbbbbbb",
    "bbbbbbbbbbbbbbbbbbbbbbbbbb",
    "bbbbdbbbbbbbbbbbbbbblbbbbb",
    "bbbbbbbbbblbbbbbbbbbbbbbbb",
    "bbbbbblbbbbbbbbbbbbbbdbbbb",
    "bbbbbbbbbbbbbbbbbbbbbbbbbb",
    "bdbbbbbbbbbbbbblbbbbbbbbbb",
    "bbbbbbbbbbbbbbbbbbbbbbdbbb",
    "bbbblbbbbbbdbbbbbbbbbbbbbb",
    "bbbbbbbbbbbbbbbbblbbbbbbbb",
    "bbbbbbbbbblbbbbbbbbbbbbbbb",
    "bbbbbbbbbbbbbbbbbbbbbbbbbb",
]

MOUNTAIN = [
    "bbbbbbbbbbbbbbbbbbbbbbbbbb",
    "bbbbbbbbbbblbbbbbbbbbbbbbb",
    "bbbbbbbbbbbbkckbbbbbbbbbbb",
    "bbbbbbbbbbbkccckbbbbbbbbbb",
    "bbbbbbbbbbkhccsskbbbbbbbbb",
    "bbbbbbbbbkhhccssskbbbbbbbb",
    "bbbbbbbbbkhhhmmmssskbbbbbb",
    "bbbbbbbkhhmmmmmsssskbbbbbb",
    "bbbbbbkhhhmmmmmmmsssskbbbb",
    "bbbbbkhhmmmmmmmmmssssskbbb",
    "bbbbkhhmmmmmmmmmmmssssskbb",
    "bbbkhhmmmmmmmmmmmmmssssskb",
    "bbkhhmmmmmmmmmmmmmmmsssskb",
    "bkhhmmmmmmmmmmmmmmmmsssskb",
    "bkmmmmmmmmmmmmmmmmmssssskb",
    "bkmmmmmmmmmmmmmmmmsssssskb",
    "bkmmmmmmmmmmmmmmmmsssssskb",
    "bkmmmmmmmmmmmmmmmssssssskb",
    "bkmmmmmmmmmmmmmmsssssssskb",
    "bkmmmmmmmmmmmmsssssssssskb",
    "bkmmmmmmmmmmsssssssssssskb",
    "bkmmmmmmmmsssssssssssssskb",
    "bbdbbbbbbbbbbbbbbbbbbbdbbb",
    "bbbbbbbbblbbbbbbbbbbbbbbbb",
    "bbbbbbbbbbbbbbbbdbbbbbbbbb",
    "bbblbbbbbbbbbbbbbbbbbbbbbb",
]

FOREST = [
    "qqqqqqqqqqqqqqqqqqqqqqqqqq",
    "qqqqqqqqqqqqqqqqqqqqqqqqqq",
    "qqqqqqkgkqqqqqqqqqqqqqqqqq",
    "qqqqqkgggkqqqqqqqqqqqqqqqq",
    "qqqqqkGgGkqqqqqqqqqqqqqqqq",
    "qqqqkggggggkqqqqqqqqqqqqqq",
    "qqqqkGgGgGkqqqqqqqqqqqqqqq",
    "qqqkgggggggggkqqqqqqqqqqqq",
    "qqqkGggGggGkqqqqqqqqqqqqqq",
    "qqkgggggggggggkqqqqqqqqqqq",
    "qqkGggGggGggkqqqqqqqqqqqqq",
    "qkgggggggggggggkqqqqqqqqqq",
    "qkGgGgggGggGkqqqqqqqqqqqqq",
    "qkgggggggggggkqqqqqqqqqqqq",
    "qqkGeGGGGGGekqqqqqqqqqqqqq",
    "qqqqqkttkqqqqqqqqqqqqqqqqq",
    "qqqqqkttkqqqqqqqqqqqqqqqqq",
    "qqqqqkttkqqqqqqqqqqqqqqqqq",
    "qqqqqkttkqqqqqqqqqqqkgkqqq",
    "qqqqqkttkqqqqqqqqqqkgggkqq",
    "qqqqqkttkqqqqqqqqqqqkGgGkq",
    "qqqqqkttkqqqqqqqqqqkgggggk",
    "qqqqqkTtkqqqqqqqqqqqkGgGgk",
    "qqqqrkttkqrqqqqqqqqqkggggk",
    "qqqqqqttqqqqqqqqqqqqqkGeGk",
    "qqrqqqqqqqqqqqqrqqqqqqkttk",
]

RIVER = [
    "uuuuuuuuuuuuuuuuuuuuuuuuuu",
    "uuuuuuuuvvvvvvuuuuuuuuuuuu",
    "uuuuuuuuuuuuuuuuvvvvvvuuuu",
    "uuuuuuuuuuuuxuuuuuuuuuuuuu",
    "uuuuuuuuuuuuuuuuuuuuuuuuuu",
    "uuuvvvvvvuuuuuuuuuuuuuuuuu",
    "uuuuuuuuuuvvvvvvuuuuxuuuuu",
    "uuuuuuuuuuuuuuuuuuuuuuuuuu",
    "uuuuuuuuuuuuuuuuuuUUUUUuuu",
    "uuuuuuuuuuuuuuvvvvvvuuuuuu",
    "uuuuuxuuuuuuuuuuuuuuuuuuuu",
    "uuvvvvvvuuuuuuuuuuuuuuuuuu",
    "uuuuuuuuuuuuuuuuuuuuvvvvvv",
    "uuuuuuuuuUUUUUuuuuuuuuuuuu",
    "uuuuvvvvvvuuuuuuuuxuuuuuuu",
    "uuuuuuuuuuuuuuuuuuuuuuuuuu",
    "uuuuuuuuuuuuvvvvvvuuuuuuuu",
    "uuuxuuuuuuuuuuuuUUUUUuuuuu",
    "uuuuuuuuvvvvvvuuuuuuuuuuuu",
    "uuuuuuuuuuuuuuuuuuuuuuuuuu",
    "uuuuvvvvvvuuuuuuuuuuuuuuuu",
    "uuuuuuuuuuuuuuuuuuuuuxuuuu",
    "uuuuuuuuuuuuuuuuuuvvvvvvuu",
    "uuuuuuuuuuuuuuuuuuuuuuuuuu",
    "uuuuuuvvvvvvuuuuuuuuuuuuuu",
    "uuuuuuuuuuuuuuuuuuuuuuuuuu",
]

CITY = [
    "kkkkkkkkkkkkkkkkkkkkkkkkkk",
    "yyyynnnnyyyynnnnyyyynnnnyy",
    "yyyynnnnyyyynnnnyyyynnnnyy",
    "kYYYYYYYYYYYYYYYYYYYYYYYYk",
    "kyyyyyyyzyyyyyyyyzyyyyyyek".replace("e", "y"),
    "kyyyyyyyyyyyyyyyyyyyyyyyyk",
    "kyyyyyyyyyyyyzyyyyyyyzyyyk",
    "kzzzzzzzzzzzzzzzzzzzzzzzzk",
    "kyyyyzyyyyyyyyyzyyyyyyyyyk",
    "kyyyyyyyyyyyyyyyyyyyyyyyyk",
    "kyyyyyyyyyyyyyyyyyyyyyyyyk",
    "kyYYYYYYYknnnnnnnnkYYYYYYk".replace("Y", "y"),
    "kyyyyyyyyknnnnnnnnkyyyyyyk",
    "kyyyyyyyyknnnnnnnnkyyyyyyk",
    "kyyyyyyyyknnnnnnnnkyyyyyyk",
    "kzzzzzzzzknnnnnnnnkzzzzzzk",
    "kyyyyyyyyknnnnnnnnkyyyyyyk",
    "kyyyyzyyyknnnnnnnnkyyzyyyk",
    "kyyyyyyyyknnnnnnnnkyyyyyyk",
    "kyyyyyyyyknnnnnnnnkyyyyyyk",
    "kyyyyyyyyknnnnnnnnkyyyyyyk",
    "kyyyyzyyyknnnnnnnnkyyyyzyk",
    "kyyyyyyyyknnnnnnnnkyyyyyyk",
    "kyyyyyyyyknnnnnnnnkyyyyyyk",
    "kzzzzzzzzzzzzzzzzzzzzzzzzk",
    "kkkkkkkkkkkkkkkkkkkkkkkkkk",
]

# ---------------------------------------------------------------------------
# Unit maps. '.' = transparent. Rendered once per side palette.
# ---------------------------------------------------------------------------

SPEARMAN = [
    "..........................",
    "........w.................",
    ".......kwk................",
    ".......kWk................",
    "........t.................",
    "........t...kAAAk.........",
    "........t...kfffk.........",
    "........t...kfffk.........",
    "........t..kAAAAAk........",
    "........t.kACAAAAEk.......",
    "........tAkACAAAAEk.......",
    "........tfkAATTTAAk.......",
    "........t.kACAAAAEk.......",
    "........t..kAAAAAk........",
    "........t.kEE.EEk.........",
    "........t.kEE.EEk.........",
    "........t.kEE.EEk.........",
    "........t.kEE.EEk.........",
    "........t.kTT.TTk.........",
    "........t.................",
    "........t.................",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
]

ARCHER = [
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "..........kAAAk...........",
    "..........kAffk...........",
    ".....wW...kAAAk....t......",
    ".....TT..kACAAEk..wt......",
    ".....TT..kACAAEk..wt......",
    ".....TT..kACAAEkAfw.t.....",
    ".....TT..kACAAEkAfw.t.....",
    ".........kACAAEk..wt......",
    "..........kAAAk...wt......",
    ".........kEE.EEk..wt......",
    ".........kEE.EEk..t.......",
    ".........kEE.EEk..........",
    ".........kEE.EEk..........",
    ".........kTT.TTk..........",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
]

CAVALRY = [
    "..........................",
    "..........................",
    "...........kfk............",
    "...........kfk..........k.",
    "..........kACAAk.....kHHHk",
    "..........kACAAk.....kHHHk",
    "..........kACAAkAA...kHHHk",
    "..........kACAAkAA...kHHHk",
    "..........kACAAk...kHHHHHk",
    ".........AAAAA...kHHHHHHHk",
    "....JkkkkkkEEkkkkkkkkkkkk.",
    "...JkHHHHHHEEHHHHHHHHHHH..",
    "...JkHHHHHHEEHHHHHHHHHHH..",
    "..JkkkkkkkTTkkkkkkkkkkkk..",
    "......HH..HH....HH..HH....",
    "......HH..HH....HH..HH....",
    "......HH..HH....HH..HH....",
    "......HH..HH....HH..HH....",
    "......TT..TT....TT..TT....",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
]

GENERAL = [
    "..........................",
    "..........................",
    "......tkAAAAAAAAk.........",
    "......tkACAAAACAk.........",
    "......tkACAAAACAk.........",
    "......tkAACAAACAk.........",
    "......tkAAACAAACk.........",
    "......tkAAAA.AAAk.........",
    "......tkkkkkkkkkk.........",
    "......t......C............",
    "......t....kAAAk..........",
    "......t....kfffk..........",
    "......t....kfffk..........",
    "......t...kAoAAAokoo......",
    "......t...kACooAEk.w......",
    "......t...kACooAEk.w......",
    "......t....kAAAAAk.w......",
    "......t...kEE.EEk.w.......",
    "......t...kEE.EEk.w.......",
    "......t...kTT.TTk.........",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
    "..........................",
]

UNITS = {
    "spear": SPEARMAN,
    "archer": ARCHER,
    "cavalry": CAVALRY,
    "general": GENERAL,
}

TERRAIN = {
    "plain": PLAIN,
    "mountain": MOUNTAIN,
    "forest": FOREST,
    "river": RIVER,
    "city": CITY,
}

# ---------------------------------------------------------------------------
# Rendering helpers
# ---------------------------------------------------------------------------


def check_map(name: str, rows: list[str]) -> None:
    if len(rows) != SIZE:
        raise SystemExit(f"{name}: expected {SIZE} rows, got {len(rows)}")
    for y, row in enumerate(rows):
        if len(row) != SIZE:
            raise SystemExit(f"{name}: row {y} has {len(row)} chars, expected {SIZE}")


def render(rows: list[str], palette: dict[str, tuple[int, int, int]], name: str):
    """Return (pixels, alpha): pixels = list of 26 bytearrays (RGB), alpha 0/255."""
    pixels, alpha = [], []
    for y, row in enumerate(rows):
        line = bytearray()
        arow = bytearray()
        for x, ch in enumerate(row):
            if ch == ".":
                line += b"\x00\x00\x00"
                arow.append(0)
                continue
            try:
                r, g, b = palette[ch]
            except KeyError:
                raise SystemExit(f"{name}: unknown char {ch!r} at ({x},{y})") from None
            line += bytes((r, g, b))
            arow.append(255)
        pixels.append(line)
        alpha.append(arow)
    return pixels, alpha


def dim(rgb: tuple[int, int, int]) -> tuple[int, int, int]:
    """Explored-fog variant: desaturate heavily and darken."""
    lum = 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]
    return tuple(int(lum * 0.74 + c * 0.26) // 2 for c in rgb)  # type: ignore[return-value]


def rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def rgb565_le_rows(pixels) -> bytes:
    out = bytearray()
    for line in pixels:
        for i in range(0, len(line), 3):
            r, g, b = line[i], line[i + 1], line[i + 2]
            out += struct.pack("<H", rgb565(r, g, b))
    return bytes(out)


def png_chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def write_png(path: Path, pixels) -> None:
    """Minimal deterministic PNG writer (8-bit RGB, no ancillary chunks)."""
    h = len(pixels)
    w = len(pixels[0]) // 3
    raw = b"".join(b"\x00" + bytes(line) for line in pixels)
    header = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    data = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(raw, 9))
        + png_chunk(b"IEND", b"")
    )
    path.write_bytes(data)


def composite(dst, src, alpha, ox: int, oy: int) -> None:
    """alpha=None pastes opaquely (used by the scaled contact sheet)."""
    for y, line in enumerate(src):
        if oy + y < 0 or oy + y >= len(dst):
            continue
        dline = dst[oy + y]
        for x in range(0, len(line), 3):
            if alpha is not None and alpha[y][x // 3] == 0:
                continue
            dx = ox + x
            if 0 <= dx < len(dline):
                dline[dx : dx + 3] = line[x : x + 3]


def scale(pixels, factor: int):
    out = []
    for line in pixels:
        row = bytearray()
        for i in range(0, len(line), 3):
            row += line[i : i + 3] * factor
        out.extend([bytes(row)] * factor)
    return out


def main() -> None:
    RAW_DIR.mkdir(parents=True, exist_ok=True)
    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)

    for rows in list(TERRAIN.values()) + list(UNITS.values()):
        pass  # per-map checks below keep names for error messages

    terrain_rendered: dict[str, object] = {}
    for name, rows in TERRAIN.items():
        check_map(name, rows)
        terrain_rendered[name] = render(rows, TERRAIN_COLORS, name)
        pixels, _ = terrain_rendered[name]
        (RAW_DIR / f"fog_terrain_{name}.rgb565").write_bytes(rgb565_le_rows(pixels))
        dim_palette = {ch: dim(c) for ch, c in TERRAIN_COLORS.items()}
        pixels_dim, _ = render(rows, dim_palette, name + "_dim")
        (RAW_DIR / f"fog_terrain_{name}_dim.rgb565").write_bytes(
            rgb565_le_rows(pixels_dim)
        )
        write_png(PREVIEW_DIR / f"fog_terrain_{name}.png", pixels)
        write_png(PREVIEW_DIR / f"fog_terrain_{name}_dim.png", pixels_dim)

    plain_bg = terrain_rendered["plain"][0]
    for name, rows in UNITS.items():
        check_map(name, rows)
        for side, side_palette in (("blue", SIDE_BLUE), ("red", SIDE_RED)):
            palette = dict(UNIT_COMMON)
            palette.update(side_palette)
            pixels, alpha = render(rows, palette, f"{name}_{side}")
            blob = rgb565_le_rows(pixels) + b"".join(bytes(a) for a in alpha)
            (RAW_DIR / f"fog_unit_{name}_{side}.rgb565a8").write_bytes(blob)
            over = [bytearray(line) for line in plain_bg]
            composite(over, pixels, alpha, 0, 0)
            write_png(PREVIEW_DIR / f"fog_unit_{name}_{side}.png", over)

    # Contact sheet: terrain (visible), terrain (dim), blue units, red units.
    gap, margin, scalef = 4, 6, 6
    terrain_names = list(TERRAIN)
    unit_names = list(UNITS)
    tile = SIZE * scalef + gap
    width = margin * 2 + max(len(terrain_names), len(unit_names)) * tile
    height = margin * 2 + 4 * tile + 3 * gap
    sheet = [bytearray(b"\x10\x14\x18" * width) for _ in range(height)]

    def stamp(pixels, col, row):
        composite(sheet, scale(pixels, scalef), None, margin + col * tile, margin + row * tile)

    dim_palette = {ch: dim(c) for ch, c in TERRAIN_COLORS.items()}
    for i, name in enumerate(terrain_names):
        stamp(terrain_rendered[name][0], i, 0)
        stamp(render(TERRAIN[name], dim_palette, name)[0], i, 1)
    for row, (side, side_palette) in enumerate((("blue", SIDE_BLUE), ("red", SIDE_RED))):
        for i, name in enumerate(unit_names):
            palette = dict(UNIT_COMMON)
            palette.update(side_palette)
            pixels, alpha = render(UNITS[name], palette, f"{name}_{side}")
            over = [bytearray(line) for line in plain_bg]
            composite(over, pixels, alpha, 0, 0)
            stamp(over, i, 2 + row)

    write_png(PREVIEW_DIR / "fog_march_contact_sheet.png", sheet)

    total = sum(p.stat().st_size for p in RAW_DIR.glob("fog_*"))
    print(f"FOG MARCH assets: {len(list(RAW_DIR.glob('fog_*')))} files, {total} bytes in {RAW_DIR}")
    print(f"Previews: {PREVIEW_DIR}")


if __name__ == "__main__":
    main()
