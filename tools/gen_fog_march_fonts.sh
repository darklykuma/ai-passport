#!/usr/bin/env bash
# Regenerate the FOG MARCH application CJK subset fonts (PRD_FOG_MARCH 10.4,
# Plan B: application-owned subset because the LVGL built-in Source Han Sans
# CJK subset misses 56 of the 132 PRD inventory glyphs).
#
# Inputs (this script is the version-controlled record of the conversion):
#   assets/fonts/SourceHanSansSC-Regular.otf  (NOT tracked; see download below)
#   assets/fonts/fog_march_charset.txt        (tracked; PRD 10.4 inventory)
#
# Font source (SIL Open Font License 1.1, Adobe / Google Source Han Sans SC):
#   https://github.com/adobe-fonts/source-han-sans/releases
#   File: SubsetOTF/CN/SourceHanSansSC-Regular.otf
#   SHA256: 84bbd4ace91d327b3ad1a581c688196278a4e41308520176f419180064e4af2b
#
# Outputs (tracked, compiled by main/CMakeLists.txt):
#   assets/fonts/fog_font_16.c  -- body text
#   assets/fonts/fog_font_20.c  -- titles
#
# Requirements: Node.js (npx) and network access to the npm registry.
# Usage: bash tools/gen_fog_march_fonts.sh
set -euo pipefail
cd "$(dirname "$0")/.."

FONT=assets/fonts/SourceHanSansSC-Regular.otf
CHARSET=assets/fonts/fog_march_charset.txt
# Pinned converter version (docs/development/engineering/lvgl-chinese-fonts.md
# requires recording it).
LV_FONT_CONV_VERSION=1.5.3

test -f "$FONT" || {
    echo "missing $FONT" >&2
    echo "download it from the URL recorded in this script's header" >&2
    exit 1
}

# Printable ASCII always rides along; the CJK part is the PRD inventory.
# Comment lines (leading #) are stripped before the whitespace squeeze.
SYMBOLS=$(grep -v '^[[:space:]]*#' "$CHARSET" | tr -d ' \t\r\n')

for size in 16 20; do
    npx --yes "lv_font_conv@${LV_FONT_CONV_VERSION}" \
        --font "$FONT" \
        --size "$size" --bpp 4 --format lvgl --no-compress \
        --range 0x20-0x7E \
        --symbols "$SYMBOLS" \
        --lv-font-name "fog_font_${size}" --lv-include lvgl.h \
        --output "assets/fonts/fog_font_${size}.c"
    echo "wrote assets/fonts/fog_font_${size}.c"
done
