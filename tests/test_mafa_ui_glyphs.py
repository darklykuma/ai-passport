#!/usr/bin/env python3
"""MAFA CHRONICLE glyph coverage gate: every CJK character and UI symbol
(…, ▶) used by the UI sources must be in the tracked charset (which drives
the subset fonts), and the tracked generated fonts must cover every
codepoint of that charset. Regenerate with tools/gen_mafa_charset.py +
tools/gen_mafa_fonts.sh. Keep this regex in sync with the one in
tools/gen_mafa_charset.py."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

CHARSET_FILE = ROOT / "assets" / "fonts" / "mafa_charset.txt"
FONT_SOURCES = (
    ROOT / "assets" / "fonts" / "mafa_font_16.c",
    ROOT / "assets" / "fonts" / "mafa_font_20.c",
)

CJK = re.compile(
    r"[\u2026\u25a0-\u25ff\u3000-\u303f\u3400-\u4dbf\u4e00-\u9fff\uff01-\uffee]"
)

UI_SOURCES = (
    ROOT / "main" / "mafa_app.c",
    ROOT / "main" / "mafa_view.c",
    ROOT / "main" / "mafa_model.c",
)


class MafaUiGlyphs(unittest.TestCase):
    def test_charset_matches_ui_sources(self) -> None:
        used = set()
        for path in UI_SOURCES:
            used.update(CJK.findall(path.read_text(encoding="utf-8")))
        charset = set(CJK.findall(CHARSET_FILE.read_text(encoding="utf-8")))
        self.assertFalse(
            used - charset,
            "UI sources use characters missing from mafa_charset.txt; "
            "run tools/gen_mafa_charset.py and regenerate the fonts: "
            f"{''.join(sorted(used - charset))}",
        )
        self.assertFalse(
            charset - used,
            "mafa_charset.txt carries characters no UI source uses; "
            "run tools/gen_mafa_charset.py to prune: "
            f"{''.join(sorted(charset - used))}",
        )

    def test_generated_fonts_cover_charset(self) -> None:
        charset = set(CJK.findall(CHARSET_FILE.read_text(encoding="utf-8")))
        self.assertGreater(len(charset), 100)
        for path in FONT_SOURCES:
            self.assertTrue(
                path.exists(),
                f"{path.name} missing; run tools/gen_mafa_fonts.sh",
            )
            covered = {
                int(m, 16)
                for m in re.findall(
                    r"U\+([0-9A-Fa-f]{4,5})",
                    path.read_text(encoding="utf-8"),
                )
            }
            missing = [
                f"U+{ord(ch):04X}({ch})"
                for ch in sorted(charset)
                if ord(ch) not in covered
            ]
            self.assertFalse(
                missing,
                f"{path.name} does not cover mafa_charset.txt: "
                f"{', '.join(missing)}; regenerate with tools/gen_mafa_fonts.sh",
            )


if __name__ == "__main__":
    unittest.main()
