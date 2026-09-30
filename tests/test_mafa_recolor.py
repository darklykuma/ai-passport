#!/usr/bin/env python3
"""MAFA CHRONICLE recolor gate: LVGL's lv_text_is_cmd consumes everything
after #RRGGBB up to the first ASCII space, so a color code not followed by
a space makes the whole colored segment disappear (and leaks following hex
into the text). Every #RRGGBB in a UI string literal must be followed by a
space, and every opened color must close with a # in the same literal."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

CODE = re.compile(r"#[0-9A-Fa-f]{6}")
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')

UI_SOURCES = (
    ROOT / "main" / "mafa_app.c",
    ROOT / "main" / "mafa_view.c",
    ROOT / "main" / "mafa_model.c",
)


class MafaRecolor(unittest.TestCase):
    def test_literal_recolor_syntax(self) -> None:
        for path in UI_SOURCES:
            src = path.read_text(encoding="utf-8")
            for ln, line in enumerate(src.splitlines(), 1):
                for m in LITERAL.finditer(line):
                    s = m.group(1)
                    if not CODE.search(s):
                        continue
                    if CODE.fullmatch(s.rstrip()):
                        continue    # bare color constant (the menu tones
                                    # carry the parser-consumed space); the
                                    # closing # lives at the format site
                    for c in CODE.finditer(s):
                        self.assertEqual(
                            s[c.end():c.end() + 1], " ",
                            f"{path.name}:{ln}: color {c.group(0)} not "
                            f"followed by a space in {s!r} - LVGL would "
                            f"swallow the whole segment")
                    self.assertEqual(
                        s.count("#") % 2, 0,
                        f"{path.name}:{ln}: unbalanced # in {s!r} - "
                        f"the color would bleed into following text")

    def test_runtime_color_splices_have_the_space(self) -> None:
        # Q_COLOR is spliced into a format at runtime (drop / sell lines,
        # gear and backpack rows): the literal itself carries no #, so the
        # literal rule cannot see it. Pin every splice site's format, then
        # assert the site count so a new Q_COLOR call cannot skip the gate.
        app = (ROOT / "main" / "mafa_app.c").read_text(encoding="utf-8")
        pins = (
            ('log_line("【掉落】%s %s%s!"',
             "the drop line must keep a space after the spliced "
             "color code, or the whole drop text disappears"),
            ('log_line("售出 %s %s+%d 金#"',
             "the sell line must keep a space after the spliced "
             "color code, or the price text disappears"),
            ('"%s%s %s %s#\\n"',
             "the gear row must keep a space after the spliced "
             "color code, or the worn item's name disappears"),
            ('"%s %s%d.%s x%d#"',
             "the backpack row must keep a space after the spliced "
             "color code, or the item name disappears"),
        )
        for fragment, why in pins:
            self.assertIn(fragment, app, why)
        sites = len(re.findall(r"Q_COLOR\[", app))
        self.assertEqual(
            sites, len(pins) + 1,  # +1: the Q_COLOR definition itself
            f"found {sites} Q_COLOR[ occurrences; a splice site was "
            f"added or removed - extend the pin list in "
            f"{Path(__file__).name} with its format string")
        menu = re.search(r'const char \*tone = [^;]+;', app)
        self.assertIsNotNone(menu, "menu tone strings missing")
        for tone in CODE.findall(menu.group(0)):
            self.assertTrue(tone.endswith(" ") or
                            menu.group(0).split(tone, 1)[1].startswith(" "),
                            f"menu tone {tone!r} must carry the trailing "
                            f"space consumed by the recolor parser")


if __name__ == "__main__":
    unittest.main()
