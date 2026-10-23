# tests/test_rift_ui_glyphs.py — fail when RIFT BOARD UI copy drifts from
# the tracked charset (the subset fonts are generated from that charset).
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    ROOT / "main" / "rift_app.c",
    ROOT / "main" / "rift_view.c",
    ROOT / "main" / "rift_model.c",
    ROOT / "main" / "rift_champs.c",
]
CHARSET = ROOT / "assets" / "fonts" / "rift_charset.txt"

# Mirror tools/gen_rift_charset.py: every non-ASCII codepoint in the UI
# sources must have a glyph. A hand-picked CJK class once missed "·"
# (U+00B7) and shipped boxes on every page while this test stayed green.
NON_ASCII = re.compile(r"[^\x00-\x7f]")

used = set()
for src in SOURCES:
    used.update(NON_ASCII.findall(src.read_text(encoding="utf-8")))

have = set(CHARSET.read_text(encoding="utf-8").strip().split("\n"))
have = {c for c in have if c and not c.startswith("#")}
have = {ch for c in have for ch in c}

missing = sorted(used - have)
if missing:
    print("missing glyphs in rift_charset.txt:",
          " ".join(missing), file=sys.stderr)
    print("run: python tools/gen_rift_charset.py && bash tools/gen_rift_fonts.sh",
          file=sys.stderr)
    sys.exit(1)
print(f"test_rift_ui_glyphs: {len(used)} codepoints covered")
