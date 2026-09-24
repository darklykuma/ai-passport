#!/usr/bin/env python3
"""Every CJK character used by the FOG MARCH UI sources must be in the PRD's
verified glyph inventory (PRD_FOG_MARCH 10.4). Plan B (application-owned
subset fonts) serves exactly this inventory: the generator input
assets/fonts/fog_march_charset.txt must match the PRD list, and the tracked
generated fonts must cover every code point of it.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# PRD_FOG_MARCH.zh_CN.md section 10.4 character inventory (verbatim, spaces
# removed). Kept here so the check runs without parsing the document.
INVENTORY = set(
    "迷雾三国枪兵骑弓主将兵力行动移动攻击待机结束回合视野侦察"
    "山地林地河流城池要地胜利失败平局我方敌方统计设置新游戏"
    "继续退出暂停重新开始战斗结算击破命中伤害恢复跳过确认取消"
    "目标距离地形隐蔽防御加成剩余总览战绩连胜最高难度简单普通困难"
    "关于存档已重置不可用再来一局行动中电量"
)

CHARSET_FILE = ROOT / "assets" / "fonts" / "fog_march_charset.txt"
FONT_SOURCES = (
    ROOT / "assets" / "fonts" / "fog_font_16.c",
    ROOT / "assets" / "fonts" / "fog_font_20.c",
)

CJK = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")

UI_SOURCES = (
    ROOT / "main" / "fog_app.c",
    ROOT / "main" / "fog_view.c",
)


class FogUiGlyphs(unittest.TestCase):
    def test_ui_cjk_subset_of_inventory(self) -> None:
        for path in UI_SOURCES:
            used = set(CJK.findall(path.read_text(encoding="utf-8")))
            missing = used - INVENTORY
            self.assertFalse(
                missing,
                f"{path.name} uses CJK characters outside the PRD 10.4 "
                f"inventory (no glyph-coverage guarantee): "
                f"{''.join(sorted(missing))}",
            )

    def test_inventory_sanity(self) -> None:
        self.assertGreater(len(INVENTORY), 100)
        # The three class names and the core menu items must be covered.
        for word in ("主将", "枪兵", "弓兵", "新游戏", "关于", "待机", "胜利", "AI 行动中".replace("AI ", "")):
            for ch in word:
                self.assertIn(ch, INVENTORY)

    def test_charset_file_matches_inventory(self) -> None:
        charset = set(CJK.findall(CHARSET_FILE.read_text(encoding="utf-8")))
        self.assertEqual(
            charset,
            INVENTORY,
            "assets/fonts/fog_march_charset.txt drifted from the PRD 10.4 "
            "inventory; the generated subset fonts would not serve the PRD",
        )

    def test_generated_fonts_cover_inventory(self) -> None:
        for path in FONT_SOURCES:
            self.assertTrue(path.exists(), f"{path.name} missing; run "
                                           f"tools/gen_fog_march_fonts.sh")
            covered = {int(m, 16) for m in
                       re.findall(r"U\+([0-9A-Fa-f]{4,5})", path.read_text(encoding="utf-8"))}
            missing = [f"U+{ord(ch):04X}({ch})" for ch in INVENTORY
                       if ord(ch) not in covered]
            self.assertFalse(
                missing,
                f"{path.name} does not cover the PRD 10.4 inventory: "
                f"{', '.join(missing)}; regenerate with "
                f"tools/gen_fog_march_fonts.sh",
            )


if __name__ == "__main__":
    unittest.main()

