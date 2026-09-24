<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# FOG MARCH Sprites

Pixel-art sprites for the FOG MARCH tactics application (`feature/fog-march`,
see `docs/PRD_FOG_MARCH.md` section 10.7). All art is authored in this
repository as ASCII pixel maps inside the generator script; no external image
sources are used. License: MIT, same as the repository.

## Inventory

18 sprites, each 26 × 26 (one battlefield cell):

| Group | Files | Format |
| --- | --- | --- |
| Terrain, visible | `fog_terrain_{plain,mountain,forest,river,city}.rgb565` | raw little-endian RGB565, opaque |
| Terrain, explored-fog variant | same names with `_dim` suffix | same; desaturated + darkened palette |
| Units, player (blue) | `fog_unit_{spear,archer,cavalry,general}_blue.rgb565a8` | RGB565 plane followed by A8 alpha plane |
| Units, enemy (red) | same names with `_red` suffix | same |

Total footprint: 29,744 bytes of Flash (10 tiles × 1,352 B + 8 units × 2,028 B).

The raw blobs live in `main/assets/` (same layout as the
`demo/rock-paper-scissors` branch) and are embedded into firmware via CMake
`EMBED_FILES`, then wrapped in `lv_image_dsc_t` descriptors
(`LV_COLOR_FORMAT_RGB565` / `LV_COLOR_FORMAT_RGB565A8`).

## Regeneration

```bash
python3 tools/gen_fog_march_assets.py
```

The ASCII maps in `tools/gen_fog_march_assets.py` are the source of truth.
Edit the maps or palettes there, rerun the generator, and commit the refreshed
`main/assets/` blobs together with the script. `tests/test_fog_assets.py`
(wired into `tools/validate.sh --static`) fails when the committed blobs or
previews no longer match a clean regeneration.

## Previews

`preview/` holds one PNG per sprite (units composited over the plain tile) and
`fog_march_contact_sheet.png` (terrain visible/dim rows, blue and red unit
rows) for visual review. Previews are generated artifacts, not edit sources.
