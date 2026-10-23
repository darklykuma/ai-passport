# -*- coding: utf-8 -*-
"""Render pixel-accurate RIFT BOARD (S16 赛场看板, name placeholder) page
mockups (240x320, x2 upscale) for a LoL Worlds 2026 scoreboard app on the
same 240x320 / 3-button (UP-DOWN-OK, long-press-OK = back) device as MAFA
CHRONICLE and WILD TRACKS.

Visual identity is deliberately distinct from both: a cool indigo "night
arena" base with a hextech-cyan interactive accent; gold is ceremonial only
(title band, winners, trophy). Blue/red side colors carry the BP/KDA pages.

Data provenance (user fact-checks; do not present invented numbers as real):
- All Worlds 2026 rows shown are the REAL probed schedule (2026-10-23,
  Beijing grouping): the six completed Play-Ins BO5s (KC 3:2 C9, MVK 3:0
  FUR, KC 3:0 MVK, FUR 3:1 C9, MVK 3:1 FUR, KC 3:0 MVK) and the Swiss
  round-1 pairings of 10-24 (GEN-IG, HLE-TES, BLG-MKOI, AL-KC, G2-DK,
  TLAW-CFO, LOS-T1, TSW-LYON). Later Swiss/QF days are TBD in the feed.
- G1 of KC 3-0 MVK (2026-10-17 02:00 BJ, Play-Ins BO5): roster, champions,
  final K/D/A, CS, and team kills/gold are the real probed window-feed
  values. Per-player gold on the KDA page is approximated.
- The feed has no ban data, so the BP page shows picks only (no ban strip).
- Champion names render as zh_CN proper names (杰斯, 李青…) via
  champions_zh.json, built from Data Dragon 16.21.1 zh_CN champion.json
  (upstream stores the epithet in "name" and the proper name in "title";
  normalized there; compound names keep only the first segment, e.g.
  烈娜塔, 努努). The device font subset must cover the 191 unique CJK
  glyphs these names need.
- Game 2/3 durations on the match page, the LIVE board scenario (GEN vs IG
  mid-game stats and both rosters), and Wi-Fi name/IP are illustrative
  placeholders. Rest day (10-22/23), TBD and unpublished rows are real.

Fonts approximate the device's Source Han Sans subsets with Microsoft
YaHei at 16/20px, same as the MAFA and WILD TRACKS mockups."""

import json
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 240, 320
BAND, FOOT = 34, 32           # title band / footer hint strip heights

# --- palette: indigo night arena + hextech cyan (distinct from MAFA/WT) ---
BG        = (0x0A, 0x0C, 0x16)
HEADER_BG = (0x11, 0x17, 0x2A)
STRIP_BG  = (0x0D, 0x12, 0x22)
PANEL_BG  = (0x0F, 0x14, 0x26)
PANEL_BG2 = (0x0D, 0x11, 0x1F)
EDGE      = (0x2A, 0x38, 0x58)
GOLD_EDGE = (0x7A, 0x62, 0x2E)
TRACK     = (0x18, 0x1F, 0x33)
GOLD      = (0xE6, 0xC0, 0x5E)   # title band / winners / trophy only
CYAN      = (0x46, 0xC8, 0xE8)   # interactive: cursor / tabs / actions
MAIN      = (0xDA, 0xE0, 0xEA)
DIM       = (0x84, 0x90, 0xA6)
BLUE      = (0x54, 0x96, 0xF2)
RED       = (0xE0, 0x58, 0x44)
LIVE_RED  = (0xF2, 0x5A, 0x40)
GREEN     = (0x5F, 0xC8, 0x6A)
RED_EDGE  = (0x6E, 0x30, 0x28)

F16 = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 16)
F20 = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 20)
LH16, LH20 = 19, 25

OUT = os.path.dirname(os.path.abspath(__file__))

with open(os.path.join(OUT, "champions_zh.json"), encoding="utf-8") as _f:
    CHAMPS = json.load(_f)   # ddragon zh_CN 16.21.1; "name" = proper name

def champ_zh(champ_id):
    return CHAMPS["champions"].get(champ_id, {}).get("name", champ_id)

def new_page():
    img = Image.new("RGB", (W, H), BG)
    return img, ImageDraw.Draw(img)

def text(d, xy, s, font, color):
    d.text(xy, s, font=font, fill=color)

def text_r(d, right, y, s, font, color):
    d.text((right - d.textlength(s, font=font), y), s, font=font, fill=color)

def text_c(d, cx, y, s, font, color):
    d.text((cx - d.textlength(s, font=font) / 2, y), s, font=font, fill=color)

def bar(d, x, y, w, h, ratio, fill):
    d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=2, fill=TRACK)
    if ratio > 0:
        iw = max(2, int((w - 2) * ratio))
        d.rounded_rectangle([x + 1, y + 1, x + 1 + iw, y + h - 2], radius=1, fill=fill)

def tri_l(d, x, y, s, color):       # ‹ chevron; symbol glyphs are drawn, not
    d.polygon([(x + s, y), (x, y + s // 2), (x + s, y + s)], fill=color)

def tri_r(d, x, y, s, color):       # › chevron
    d.polygon([(x, y), (x + s, y + s // 2), (x, y + s)], fill=color)

def dot(d, x, y, r, color):
    d.ellipse([x - r, y - r, x + r, y + r], fill=color)

def check(d, x, y, color):          # small check mark, drawn
    d.line([(x, y + 5), (x + 4, y + 9), (x + 11, y)], fill=color, width=2)

def lock(d, x, y, color):           # unpublished glyph, drawn
    d.rounded_rectangle([x, y + 6, x + 10, y + 15], radius=2, outline=color, width=1)
    d.arc([x + 2, y, x + 8, y + 8], 180, 360, fill=color, width=1)

def slash_circle(d, x, y, color):   # offline glyph, drawn
    d.ellipse([x, y, x + 13, y + 13], outline=color, width=1)
    d.line([(x + 2, y + 11), (x + 11, y + 2)], fill=color, width=1)

def dim_page(img):
    """Dim the page behind a modal."""
    base = img.convert("RGBA")
    ov = Image.new("RGBA", (W, H), (5, 7, 14, 175))
    img2 = Image.alpha_composite(base, ov).convert("RGB")
    return img2, ImageDraw.Draw(img2)

def band(d, title, right=None, right_color=DIM, edge=GOLD_EDGE, f=None):
    d.rectangle([0, 0, W - 1, BAND - 1], fill=HEADER_BG)
    d.rectangle([0, BAND - 2, W - 1, BAND - 1], fill=edge)
    text(d, (10, 5 if f is F20 else 7), title, f or F20, GOLD)
    if right:
        text_r(d, 230, 8, right, F16, right_color)

def footer(d, s):
    d.rectangle([0, H - FOOT, W - 1, H - 1], fill=PANEL_BG2)
    d.rectangle([0, H - FOOT, W - 1, H - FOOT], fill=EDGE)
    text(d, (10, 295), s, F16, DIM)

def save(img, name):
    img.resize((W * 2, H * 2), Image.NEAREST).save(
        os.path.join(OUT, name + ".png"))
    print(name)

# ---------------------------------------------------------------- 01 boot
def p01_boot():
    img, d = new_page()
    band(d, "RIFT BOARD", right="v1.0")
    # trophy silhouette, flat polygons/arcs (LVGL-drawable on device)
    cx = W // 2
    d.polygon([(cx - 26, 74), (cx + 26, 74), (cx + 18, 104), (cx - 18, 104)], fill=GOLD)
    d.rectangle([cx - 3, 104, cx + 3, 116], fill=GOLD)
    d.rectangle([cx - 16, 116, cx + 16, 122], fill=GOLD)
    d.arc([cx - 46, 74, cx - 14, 106], 270, 90, fill=GOLD, width=3)
    d.arc([cx + 14, 74, cx + 46, 106], 90, 270, fill=GOLD, width=3)
    text_c(d, cx, 136, "S16 赛场看板", F20, MAIN)
    text_c(d, cx, 162, "2026 全球总决赛 · 实时赛况", F16, DIM)
    rows = [
        ("已连接 · HOME-5G", GREEN),
        ("赛程缓存 108 场 · 20:41", DIM),
        ("正在同步今日赛程…", CYAN),
    ]
    y = 196
    for s, c in rows:
        if c is GREEN:
            check(d, 34, y + 2, c)
            text(d, (54, y), s, F16, MAIN)
        else:
            text(d, (34, y), s, F16, c)
        y += 24
    bar(d, 34, 276, 148, 8, 0.45, CYAN)
    text_r(d, 232, 272, "45%", F16, DIM)
    footer(d, "数据源 lolesports · 非官方接口")
    return img

# ---------------------------------------------------------------- 02 home
def p02_home():
    img, d = new_page()
    band(d, "S16 赛场看板", right="缓存 21:14")
    # date bar: UP/DOWN steps days when focused, OK opens the date list
    d.rectangle([0, BAND, W - 1, BAND + 29], fill=STRIP_BG)
    d.rectangle([0, BAND + 29, W - 1, BAND + 29], fill=EDGE)
    tri_l(d, 12, BAND + 7, 9, DIM)
    tri_r(d, 219, BAND + 7, 9, DIM)
    text_c(d, W // 2, BAND + 6, "10月16日 周五 · 入围赛", F16, MAIN)

    # row A: real Play-Ins result (KC 3:2 C9, 2026-10-16 02:00 BJ)
    y = BAND + 34
    d.rounded_rectangle([4, y, 235, y + 53], radius=4,
                        fill=PANEL_BG, outline=EDGE, width=1)
    text(d, (40, y + 3), "KC", F20, MAIN)
    text_c(d, 122, y + 3, "3:2", F20, GOLD)
    text_r(d, 200, y + 3, "C9", F20, GOLD)
    d.rounded_rectangle([206, y + 5, 232, y + 25], radius=3, outline=EDGE, width=1)
    text_c(d, 219, y + 7, "完", F16, DIM)
    text(d, (14, y + 31), "02:00 · 入围赛 BO5 · 已结束", F16, DIM)

    # row B: real Play-Ins result (MVK 3:0 FUR, 2026-10-16 07:00 BJ)
    y = BAND + 92
    d.rounded_rectangle([4, y, 235, y + 53], radius=4,
                        fill=PANEL_BG, outline=EDGE, width=1)
    text(d, (40, y + 3), "MVK", F20, MAIN)
    text_c(d, 122, y + 3, "3:0", F20, GOLD)
    text_r(d, 200, y + 3, "FUR", F20, GOLD)
    d.rounded_rectangle([206, y + 5, 232, y + 25], radius=3, outline=EDGE, width=1)
    text_c(d, 219, y + 7, "完", F16, DIM)
    text(d, (14, y + 31), "07:00 · 入围赛 BO5 · 已结束", F16, DIM)

    # teaser row: next day's first match (real pairing, 10-17 02:00 BJ)
    y = BAND + 150
    d.rounded_rectangle([4, y, 235, y + 29], radius=4,
                        fill=PANEL_BG2, outline=EDGE, width=1)
    for gx in range(10, 230, 12):   # dashed bottom edge = not today
        d.rectangle([gx, y + 28, min(gx + 6, 230), y + 28], fill=EDGE)
    text(d, (14, y + 6), "明日 02:00 · KC vs MVK", F16, DIM)
    tri_r(d, 221, y + 10, 8, DIM)
    footer(d, "OK进入 日期行切日 长按=数据页")
    return img

# ---------------------------------------------------------------- 03 date list modal
def p03_dates():
    img, d = new_page()
    band(d, "S16 赛场看板", right="缓存 21:14")
    d.rectangle([0, BAND, W - 1, BAND + 29], fill=STRIP_BG)
    text_c(d, W // 2, BAND + 6, "10月23日 周五 · 瑞士轮", F16, DIM)
    img, d = dim_page(img)
    d.rounded_rectangle([10, 46, 229, 284], radius=6, fill=PANEL_BG,
                        outline=CYAN, width=1)
    text_c(d, W // 2, 54, "切换日期", F20, GOLD)
    text_c(d, W // 2, 82, "待定=对阵未出 · 灰行=未公布", F16, DIM)
    d.rectangle([10, 104, 229, 104], fill=EDGE)
    rows = [
        ("回到今天 · 10月23日", CYAN, None),
        ("10月24日 六 · 未开8场", MAIN, None),
        ("10月25日 日 · 待定8场", MAIN, None),
        ("10月26日 一 · 待定4场", MAIN, None),
        ("10月27日 二 · 待定4场", MAIN, None),
        ("10月29日 四 · 待定2场", MAIN, None),
        ("11月4日 三 · 淘汰赛 待定", MAIN, None),
        ("… 11月15日 决赛 · 未公布", DIM, "lock"),
    ]
    y = 110
    for i, (s, c, tag) in enumerate(rows):
        if i == 3:  # cursor on 10-24
            d.rounded_rectangle([16, y - 3, 218, y + 20], radius=3,
                                fill=TRACK, outline=CYAN, width=1)
            tri_r(d, 204, y + 2, 8, CYAN)
        text(d, (26, y), s, F16, c)
        if tag == "done":
            check(d, 210, y + 2, GREEN)
        elif tag == "today":
            dot(d, 214, y + 9, 3, GOLD)
        elif tag == "lock":
            lock(d, 210, y + 1, DIM)
        y += 22
    # scroll window: more unpublished days below, to the 11-14 finals
    d.rectangle([222, 110, 224, 281], fill=TRACK)
    d.rectangle([222, 110, 224, 140], fill=DIM)
    footer(d, "OK 选定 · 长按取消")
    return img

# ---------------------------------------------------------------- 04 match detail
def p04_match():
    img, d = new_page()
    band(d, "对局 · 入围赛 10月17日")
    # series header (real: KC 3-0 MVK, Play-Ins BO5)
    d.rounded_rectangle([4, BAND + 4, 235, BAND + 76], radius=4,
                        fill=PANEL_BG, outline=EDGE, width=1)
    text(d, (20, BAND + 6), "KC", F20, GOLD)
    text_c(d, W // 2, BAND + 6, "3 : 0", F20, GOLD)
    text_r(d, 220, BAND + 6, "MVK", F20, MAIN)
    text(d, (20, BAND + 32), "Karmine Corp", F16, DIM)
    text_r(d, 230, BAND + 32, "MVK Esports", F16, DIM)
    text_c(d, W // 2, BAND + 53, "BO5 · 已结束", F16, DIM)
    # game rows: G1 stats real; G2/G3 durations illustrative
    games = [
        ("G1", "KC 胜", "32:32", True, True),
        ("G2", "KC 胜", "28:14", True, False),
        ("G3", "KC 胜", "35:02", True, False),
        ("G4", "—", "", False, False),
        ("G5", "—", "", False, False),
    ]
    y = BAND + 84
    for g, res, dur, played, cur in games:
        d.rounded_rectangle([4, y, 235, y + 30], radius=3,
                            fill=PANEL_BG if played else PANEL_BG2,
                            outline=CYAN if cur else EDGE, width=1)
        if cur:
            d.rectangle([4, y, 7, y + 30], fill=CYAN)
            tri_r(d, 216, y + 9, 8, CYAN)
        text(d, (16, y + 5), g, F16, DIM)
        text(d, (48, y + 5), res, F16, GOLD if played else DIM)
        text_r(d, 200, y + 5, dur, F16, DIM)
        y += 34
    footer(d, "OK 小场详情 · 长按OK 返回")
    return img

# ---------------------------------------------------------------- 05 game BP
def p05_bp():
    img, d = new_page()
    band(d, "第1场 KC胜 · 32:32", f=F16)
    # view toggle rides the band (BP active); OK flips BP<->KDA
    d.rounded_rectangle([152, 6, 186, 26], radius=3, fill=CYAN)
    text_c(d, 169, 8, "BP", F16, BG)
    d.rounded_rectangle([192, 6, 230, 26], radius=3, outline=EDGE, width=1)
    text_c(d, 211, 8, "KDA", F16, DIM)
    # picks: real G1 window-feed roster/champions, single full-width column;
    # no ban strip — the feed has no ban data (v1 shows picks only)
    blue = [("上", "Canna", "Jayce"), ("野", "Yike", "LeeSin"),
            ("中", "kyeahoo", "Cassiopeia"), ("下", "Caliste", "Zeri"),
            ("辅", "Busio", "Yuumi")]
    red = [("上", "Kratos", "Yorick"), ("野", "Gury", "JarvanIV"),
           ("中", "Chika", "Akali"), ("下", "Harky", "Ashe"),
           ("辅", "SiuLoong", "Seraphine")]
    text(d, (14, 38), "KC · 蓝方", F16, BLUE)
    y = 60
    d.rectangle([4, y, 6, y + 99], fill=BLUE)     # blue group bar
    for role, name, champ in blue:
        text(d, (12, y), role, F16, BLUE)
        text(d, (32, y), name, F16, MAIN)
        text_r(d, 232, y, champ_zh(champ), F16, BLUE)
        y += 20
    y += 2
    text(d, (14, y), "MVK · 红方", F16, RED)
    y += 22
    d.rectangle([4, y, 6, y + 99], fill=RED)      # red group bar
    for role, name, champ in red:
        text(d, (12, y), role, F16, RED)
        text(d, (32, y), name, F16, MAIN)
        text_r(d, 232, y, champ_zh(champ), F16, RED)
        y += 20
    footer(d, "OK 切换KDA · 长按OK 返回")
    return img

# ---------------------------------------------------------------- 06 game KDA
def p06_kda():
    img, d = new_page()
    band(d, "第1场 KC胜 · 32:32", f=F16)
    d.rounded_rectangle([152, 6, 186, 26], radius=3, outline=EDGE, width=1)
    text_c(d, 169, 8, "BP", F16, DIM)
    d.rounded_rectangle([192, 6, 230, 26], radius=3, fill=CYAN)
    text_c(d, 211, 8, "KDA", F16, BG)
    y = 38
    # team subtotal heads are real (kills 13-2, gold 58.5k-47.1k)
    blue = [("Canna", "1/0/6", "301", "8.6k"), ("Yike", "2/0/5", "220", "7.9k"),
            ("kyeahoo", "4/1/2", "284", "11.2k"), ("Caliste", "6/1/1", "311", "14.8k"),
            ("Busio", "0/0/10", "8", "1.1k")]   # gold values illustrative
    red = [("Kratos", "0/4/0", "280", "9.1k"), ("Gury", "1/2/0", "179", "6.8k"),
           ("Chika", "0/4/0", "217", "8.4k"), ("Harky", "1/1/1", "319", "10.9k"),
           ("SiuLoong", "0/2/0", "40", "1.5k")]
    text(d, (14, y), "KC  13杀 · 58.5k", F16, BLUE)
    y += 22
    for name, kda, cs, gold in blue:
        text(d, (14, y), name, F16, MAIN)
        text_c(d, 122, y, kda, F16, MAIN)
        text_c(d, 170, y, cs, F16, DIM)
        text_r(d, 232, y, gold, F16, DIM)
        y += 19
    y += 4
    text(d, (14, y), "MVK  2杀 · 47.1k", F16, RED)
    y += 22
    for name, kda, cs, gold in red:
        text(d, (14, y), name, F16, MAIN)
        text_c(d, 122, y, kda, F16, MAIN)
        text_c(d, 170, y, cs, F16, DIM)
        text_r(d, 232, y, gold, F16, DIM)
        y += 19
    footer(d, "OK 切换BP · 长按OK 返回")
    return img

# ---------------------------------------------------------------- 07 live board
def p07_live():
    img, d = new_page()
    band(d, "LIVE 10月24日", right="24:37", edge=RED_EDGE)
    # scenario: GEN vs IG G1 mid-game (pairing real; stats/rosters placeholder)
    d.rounded_rectangle([4, BAND + 4, 235, BAND + 58], radius=4,
                        fill=PANEL_BG, outline=RED_EDGE, width=1)
    dot(d, 18, BAND + 16, 4, LIVE_RED)
    text(d, (28, BAND + 8), "LIVE", F16, LIVE_RED)
    text_r(d, 226, BAND + 8, "BO1 · 第1场", F16, DIM)
    text(d, (20, BAND + 28), "GEN", F20, MAIN)
    text_c(d, W // 2, BAND + 28, "12 : 8", F20, GOLD)
    text_r(d, 220, BAND + 28, "IG", F20, MAIN)

    def bar_rev(x, y, w, h, ratio, fill):   # right-side share grows leftward
        d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=2, fill=TRACK)
        if ratio > 0:
            iw = max(2, int((w - 2) * ratio))
            d.rounded_rectangle([x + w - 1 - iw, y + 1, x + w - 2, y + h - 2],
                                radius=1, fill=fill)

    def compare(y, label, lv, rv, ratio):
        text_c(d, W // 2, y, label, F16, DIM)
        text(d, (10, y), str(lv), F16, BLUE)
        text_r(d, 230, y, str(rv), F16, RED)
        bar(d, 58, y + 20, 56, 7, ratio, BLUE)
        bar_rev(126, y + 20, 56, 7, 1 - ratio, RED)

    compare(BAND + 66, "击杀", 12, 8, 0.6)
    compare(BAND + 96, "经济", "54.2k", "47.1k", 0.535)
    compare(BAND + 126, "防御塔", 7, 3, 0.7)
    # compact roster KDA (placeholder players)
    gen = [("Kiin", "2/1/4"), ("Canyon", "1/2/8"), ("Chovy", "5/0/3"),
           ("Peyz", "4/1/2"), ("Lehends", "0/4/9")]
    ig = [("TheShy", "1/3/1"), ("Jiejie", "0/4/2"), ("Rookie", "3/2/1"),
          ("GALA", "3/3/0"), ("Meiko", "1/5/2")]
    y = BAND + 157
    for (gn, gk), (rn, rk) in zip(gen, ig):
        text(d, (12, y), gn, F16, MAIN)
        text_r(d, 122, y, gk, F16, DIM)
        text(d, (126, y), rn, F16, MAIN)
        text_r(d, 230, y, rk, F16, DIM)
        y += 19
    footer(d, "OK 立即刷新 · 每30秒自动更新")
    return img

# ---------------------------------------------------------------- 08 data page
def p08_data():
    img, d = new_page()
    band(d, "数据与网络")
    rows = [
        ("Wi-Fi", "已连接 · HOME-5G", GREEN, False),
        ("数据缓存", "21:14 · 赛程108场 · 小场96场", CYAN, True),
        ("时区", "北京时间 UTC+8", DIM, False),
        ("数据源", "lolesports 非官方接口", DIM, False),
    ]
    y = BAND + 6
    for title, sub, c, cur in rows:
        d.rounded_rectangle([4, y, 235, y + 46], radius=4,
                            fill=PANEL_BG, outline=CYAN if cur else EDGE,
                            width=1)
        if cur:
            d.rectangle([4, y, 7, y + 46], fill=CYAN)
            tri_r(d, 216, y + 16, 8, CYAN)
        text(d, (16, y + 4), title, F16, MAIN)
        if title == "Wi-Fi":
            check(d, 82, y + 5, GREEN)
        text(d, (16, y + 24), sub, F16, c if cur else DIM)
        y += 52
    d.rounded_rectangle([4, y, 235, y + 30], radius=4, fill=PANEL_BG2,
                        outline=EDGE, width=1)
    text(d, (14, y + 6), "离线时可浏览全部已缓存内容", F16, DIM)
    footer(d, "OK 刷新数据 · 长按OK 返回")
    return img

# ---------------------------------------------------------------- 09 edge states
def p09_states():
    img, d = new_page()
    band(d, "兜底状态")
    states = [
        ("slash_circle", "今日休赛", "10月22日 无比赛", "下一场 10-24 01:00 GEN-IG"),
        ("dot", "对阵待定", "10月25日 · 8场已排", "瑞士轮对阵逐轮公布"),
        ("lock", "赛程未公布", "11月4日 · 四分之一决赛", "淘汰赛对阵待瑞士轮结束"),
        ("wifi", "离线模式", "Wi-Fi 未连接", "显示 21:14 缓存 · OK重试"),
    ]
    y = BAND + 6
    for kind, title, l1, l2 in states:
        d.rounded_rectangle([4, y, 235, y + 54], radius=4, fill=PANEL_BG,
                            outline=EDGE, width=1)
        if kind == "slash_circle":
            slash_circle(d, 18, y + 8, DIM)
        elif kind == "dot":
            dot(d, 24, y + 15, 5, GOLD)
        elif kind == "lock":
            lock(d, 18, y + 8, DIM)
        else:
            d.arc([18, y + 6, 30, y + 18], 90, 360, fill=CYAN, width=2)
            dot(d, 24, y + 12, 3, DIM)
        text(d, (44, y + 4), title, F16, MAIN)
        text(d, (44, y + 22), l1, F16, DIM)
        text(d, (14, y + 38), l2, F16, DIM)
        y += 60
    footer(d, "OK 返回 · 自动定时重试刷新")
    return img

def main():
    save(p01_boot(), "01-boot")
    save(p02_home(), "02-home-today")
    save(p03_dates(), "03-date-list")
    save(p04_match(), "04-match")
    save(p05_bp(), "05-game-bp")
    save(p06_kda(), "06-game-kda")
    save(p07_live(), "07-live")
    save(p08_data(), "08-data")
    save(p09_states(), "09-states")

if __name__ == "__main__":
    main()
