# -*- coding: utf-8 -*-
"""Render pixel-accurate MAFA CHRONICLE page mockups (240x320, x2 upscale).

Geometry and colors mirror main/mafa_view.c exactly; content reflects the
skills-2.0 logic (fbe98bd): 5 skills per class, skill books in the store,
multi-mob battles, the taoist pet, and death penalties. Fonts approximate
the device's Source Han Sans subsets with Microsoft YaHei at 16/20px."""
from PIL import Image, ImageDraw, ImageFont

W, H = 240, 320
BG        = (0x0B, 0x0D, 0x10)
HEADER_BG = (0x10, 0x15, 0x1C)
STRIP_BG  = (0x0D, 0x11, 0x16)
PANEL_BG  = (0x0E, 0x13, 0x19)
PANEL_EDGE= (0x2C, 0x3A, 0x48)
GOLD_EDGE = (0x8A, 0x6A, 0x30)
TRACK     = (0x1A, 0x21, 0x29)
HP_GREEN  = (0x5F, 0xC8, 0x5F)
MP_BLUE   = (0x4F, 0xA8, 0xF2)
ENEMY_RED = (0xE0, 0x5A, 0x48)
MAIN      = (0xE8, 0xE4, 0xD8)
DIM       = (0x9A, 0xA3, 0xA8)
GOLD      = (0xF0, 0xC0, 0x4A)
RED       = (0xE0, 0x5A, 0x48)
GREEN     = (0x5F, 0xC8, 0x5F)
QUAL = {"white": (0xC8,0xC8,0xC8), "green": (0x5F,0xC8,0x5F), "blue": (0x4F,0xA8,0xF2), "purple": (0xB0,0x6C,0xF0)}

F16 = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 16)
F20 = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 20)

LH16, LH20 = 19, 25

def new_page():
    img = Image.new("RGB", (W, H), BG)
    return img, ImageDraw.Draw(img)

def panel(d, x, y, w, h, bg, edge, radius=4, top_border=False, bottom_only=False, bw=1):
    d.rounded_rectangle([x, y, x+w-1, y+h-1], radius=radius, fill=bg,
                        outline=edge, width=bw)
    if bottom_only:
        d.rectangle([x+1, y+1, x+w-2, y+h-2], fill=bg)
        d.rectangle([x, y+h-bw, x+w-1, y+h-1], fill=edge)
    if top_border:
        d.rectangle([x, y, x+w-1, y+bw-1], fill=edge)
        d.rectangle([x, y+bw, x+w-1, y+h-1], fill=bg)

def text(d, xy, s, font, color):
    d.text(xy, s, font=font, fill=color)

def text_r(d, right, y, s, font, color):
    w = d.textlength(s, font=font)
    d.text((right - w, y), s, font=font, fill=color)

def text_c(d, cx, y, s, font, color):
    w = d.textlength(s, font=font)
    d.text((cx - w/2, y), s, font=font, fill=color)

def bar(d, x, y, w, h, ratio, fill):
    d.rounded_rectangle([x, y, x+w-1, y+h-1], radius=2, fill=TRACK)
    if ratio > 0:
        iw = max(2, int((w-2) * ratio))
        d.rounded_rectangle([x+1, y+1, x+1+iw, y+h-2], radius=1, fill=fill)

def title_band(d, s):
    panel(d, 0, 0, W, 34, HEADER_BG, GOLD_EDGE, radius=0, bottom_only=True)
    text_c(d, W/2, 6, s, F20, GOLD)

def save(img, name):
    img.resize((W*2, H*2), Image.NEAREST).save(
        rf"E:\xx-code\ai-passport\docs\design\mafa\{name}.png")
    print(name)

def main_chrome(d):
    panel(d, 0, 0, W, 62, HEADER_BG, GOLD_EDGE, radius=0, bottom_only=True)
    panel(d, 0, 62, W, 36, STRIP_BG, PANEL_EDGE, radius=0, bottom_only=True)
    d.rectangle([14, 88, 50, 105], fill=BG)      # caption punches the border
    panel(d, 4, 98, 232, 140, PANEL_BG, PANEL_EDGE)
    text_c(d, 32, 90, "战况", F16, GOLD)
    panel(d, 0, 238, W, 82, (0x0C, 0x0F, 0x13), GOLD_EDGE, radius=0, top_border=True, bw=2)

def header(d, mapname, level, bat, gold, hp, hpmax, mp, mpmax):
    text(d, (10, 6), mapname, F20, GOLD)
    # info_label y=6, line height 19, line_space 0 -> line2 top = 25
    text_r(d, 234, 6, f"Lv.{level} {bat}%", F16, DIM)
    text_r(d, 234, 25, f"金{gold}", F16, GOLD)
    bar(d, 10, 33, 130, 10, hp/hpmax, HP_GREEN)
    text_r(d, 136, 28, str(hp), F16, MAIN)
    bar(d, 10, 48, 130, 7, mp/mpmax, MP_BLUE)
    text_r(d, 136, 44, str(mp), F16, MAIN)

def enemy(d, s, color, ratio=None):
    if s.startswith("▶"):   # YaHei lacks U+25B6; draw the triangle instead
        d.polygon([(10, 73), (10, 87), (22, 80)], fill=color)
        s = s[1:]
        x = 26
    else:
        x = 10
    text(d, (x, 71), s, F16, color)
    if ratio is not None:
        bar(d, 104, 75, 128, 9, ratio, ENEMY_RED)

def loglines(d, lines):
    y = 107                                      # log_label abs (14,107)
    for s, c in lines:
        text(d, (14, y), s, F16, c)
        y += LH16 + 2

def action_bar(d, cur):
    # Code (034bddb): one recolored label — selected cell gold, rest dim;
    # columns pad to a fixed pitch with two ASCII spaces.
    cells = ["背包", "装备", "商店", "地图", "设置", "加速4x"]
    gap = d.textlength(" ", font=F20)
    for row in (0, 1):
        x = 12.0
        y = 250 if row == 0 else 282             # 250 + 25 line + 7 space
        for col in range(3):
            i = row * 3 + col
            s = ("＞" if i == cur else "　") + cells[i]
            text(d, (x, y), s, F20, GOLD if i == cur else DIM)
            x += d.textlength(s, font=F20) + gap * 2

# ---- 1. menu ---------------------------------------------------------------
img, d = new_page()
panel(d, 30, 44, 180, 96, PANEL_BG, GOLD_EDGE, radius=8, bw=2)
text_c(d, 120, 64, "玛法战纪", F20, GOLD)
text_c(d, 120, 100, "「文字传奇」", F16, DIM)
text(d, (80, 180), "＞继续游戏", F16, MAIN)
text(d, (80, 205), "  新游戏", F16, MAIN)
save(img, "01-menu")

# ---- 2. class (skills-2.0 blurbs) ------------------------------------------
img, d = new_page()
title_band(d, "选择职业")
panel(d, 6, 42, 228, 150, PANEL_BG, PANEL_EDGE)
for i, nm in enumerate(["战士  高血高防", "法师  高攻脆皮", "道士  攻守兼备"]):
    mark = "＞" if i == 0 else "　"
    text(d, (18, 52 + i * (LH16 + 6)), mark + nm, F16, GOLD if i == 0 else MAIN)
panel(d, 6, 200, 228, 112, PANEL_BG, PANEL_EDGE)
text(d, (18, 210), "基础/攻杀/刺杀/半月/烈火", F16, DIM)
text(d, (18, 235), "Lv1 起步,技能书解锁进阶", F16, DIM)
save(img, "02-class")

# ---- 3. main idle ----------------------------------------------------------
img, d = new_page()
main_chrome(d)
header(d, "比奇森林", 8, 87, 1234, 187, 210, 46, 60)
enemy(d, "挂机中 17/25", DIM)
loglines(d, [
    ("遭遇 骸骨兵 等 3 只!", MAIN),
    ("你造成 14 点伤害", MAIN),
    ("【攻杀剑术】造成 28 伤害!", MAIN),
    ("骸骨兵 反击,你受 9 伤害", RED),
    ("骸骨兵 倒下!经验+18", GOLD),
    ("遭遇 暗鼠!", MAIN),
])
action_bar(d, 0)
save(img, "03-main-idle")

# ---- 4. main battle: multi-mob ---------------------------------------------
img, d = new_page()
main_chrome(d)
header(d, "废矿洞", 10, 73, 2380, 195, 230, 40, 70)
enemy(d, "▶骸骨兵×3", MAIN, 0.42)
loglines(d, [
    ("你造成 14 点伤害", MAIN),
    ("【半月弯刀】造成 13 伤害!", MAIN),
    ("骸骨兵【重击】你受 15 伤害!", RED),
    ("红药 +50", GREEN),
    ("暗鼠 受 5 持续伤害", RED),
    ("骷髅 攻击骸骨兵,造成 8 伤害", DIM),
])
action_bar(d, 4)
save(img, "04-main-battle")

# ---- 5. main boss (taoist + pet) -------------------------------------------
img, d = new_page()
main_chrome(d)
header(d, "祖玛寺庙", 13, 64, 876, 233, 260, 58, 90)
enemy(d, "▶尸王!", GOLD, 0.71)
loglines(d, [
    ("【Boss】尸王 出现了!", GOLD),
    ("神兽 出现!", GREEN),
    ("【灵魂火符】造成 45 伤害!", MAIN),
    ("神兽 替你挡下 12 伤害", DIM),
    ("尸王 反击,你受 18 伤害", RED),
    ("治愈 +78", GREEN),
])
action_bar(d, 5)
save(img, "05-main-boss")

# ---- 6. modal (boss prompt) ------------------------------------------------
img, d = new_page()
main_chrome(d)
header(d, "祖玛寺庙", 13, 64, 876, 233, 260, 58, 90)
enemy(d, "挂机中 24/25", DIM)
action_bar(d, 0)
ov = Image.new("RGB", (W, H), (0, 0, 0))
img = Image.blend(img, ov, 0.45)
d = ImageDraw.Draw(img)
panel(d, 8, 85, 224, 150, (0x10, 0x18, 0x20), GOLD_EDGE, radius=6)
text_c(d, 120, 100, "【Boss】尸王 出现了!", F16, GOLD)
text(d, (60, 140), "＞迎战", F16, GOLD)
text(d, (60, 170), "  回避", F16, MAIN)
save(img, "06-modal-boss")

# ---- 7. status (gear) ------------------------------------------------------
img, d = new_page()
title_band(d, "装备")
panel(d, 6, 42, 228, 270, PANEL_BG, PANEL_EDGE)
rows = [
    ("＞武器:修罗 攻+12", MAIN),
    ("  衣服:天魔神甲 防+8 血+40", MAIN),
    ("  首饰:绿宝石戒指 攻+4 防+2", MAIN),
    ("", DIM),
    ("攻 28  防 10", DIM),
    ("血 233/260  蓝 58/90", DIM),
]
y = 52
for s, c in rows:
    if s: text(d, (18, y), s, F16, c)
    y += LH16 + 6
save(img, "07-status")

# ---- 8. backpack -----------------------------------------------------------
# The device font's real line height is 20 (LH16 here approximates 19), so
# the list panel mirrors the code's 208px: 8 rows at pitch 24 fit with a
# symmetric 10px pad instead of riding the bottom border.
img, d = new_page()
title_band(d, "背包")
panel(d, 6, 42, 228, 208, PANEL_BG, PANEL_EDGE)
items = [("＞1.修罗 x1", None), ("  2.天魔神甲 x1", None), ("  3.红药 x3", None),
         ("  4.蓝药 x2", None), ("  5.绿宝石戒指 x1", "green"), ("  6.灵魂项链 x1", "blue"),
         ("  7.空", DIM), ("  8.空", DIM)]
y = 50
for s, q in items:
    color = q if isinstance(q, tuple) else QUAL.get(q, MAIN)
    text(d, (18, y), s, F16, color)
    y += LH16 + 4
panel(d, 6, 256, 228, 56, PANEL_BG, PANEL_EDGE)
text(d, (18, 264), "攻+12 防+8 血+40", F16, DIM)
text(d, (18, 288), "金币 2380", F16, DIM)
save(img, "08-backpack")

# ---- 9. store: potions + the two class books --------------------------------
img, d = new_page()
title_band(d, "药店")
panel(d, 6, 42, 228, 270, PANEL_BG, PANEL_EDGE)
store = [
    ("＞红药 50金", MAIN),
    ("  蓝药 40金", MAIN),
    ("  攻杀剑术 300金 已学", DIM),
    ("  刺杀剑术 800金", MAIN),
    ("", DIM),
    ("金币 2380", GOLD),
]
y = 52
for s, c in store:
    if s: text(d, (18, y), s, F16, c)
    y += LH16 + 6
save(img, "09-store")

# ---- 10. maps (0 = safe-zone hub first, combat maps 1-3) -------------------
img, d = new_page()
title_band(d, "地图")
panel(d, 6, 42, 228, 150, PANEL_BG, PANEL_EDGE)
maps = [("  0.安全区", MAIN), ("＞1.比奇森林", MAIN),
        ("  2.废矿洞", MAIN), ("  3.祖玛寺庙 锁定", DIM)]
for i, (s, c) in enumerate(maps):
    text(d, (18, 52 + i * (LH16 + 6)), s, F16, c)
panel(d, 6, 200, 228, 112, PANEL_BG, PANEL_EDGE)
text(d, (18, 210), "当前:比奇森林", F16, DIM)
text(d, (18, 235), "击杀 40 触发 Boss", F16, DIM)
text(d, (18, 260), "安全区:无怪,休息回血", F16, DIM)
save(img, "10-maps")

# ---- 10b. main page while resting in the safe zone --------------------------
img, d = new_page()
main_chrome(d)
header(d, "安全区", 10, 96, 2380, 230, 230, 70, 70)
enemy(d, "休息中,请选地图", DIM)
loglines(d, [
    ("【安全区】休息中", GOLD),
    ("你被 尸王 杀死了…", RED),
    ("满血回到安全区", DIM),
    ("失去 修罗", RED),
    ("损失 120 金", RED),
    ("", MAIN),
])
action_bar(d, 2)
save(img, "13-main-safe")

# ---- 11. settings -----------------------------------------------------------
img, d = new_page()
title_band(d, "设置")
panel(d, 6, 42, 228, 270, PANEL_BG, PANEL_EDGE)
sets = [("＞自动喝药:开", MAIN), ("  自动卖白:开", MAIN), ("  自动Boss:关", MAIN)]
for i, (s, c) in enumerate(sets):
    text(d, (18, 52 + i * (LH16 + 6)), s, F16, c)
save(img, "11-settings")

# ---- 12. PROPOSAL: skills listed on the gear page ---------------------------
# Not implemented yet: 5 skills have no viewing surface outside the store's
# 已学 marks. The gear panel has ~170px free below the summary — room for a
# divider + 5 skill rows showing book state (已学/书店/精英/Boss).
img, d = new_page()
title_band(d, "装备")
panel(d, 6, 42, 228, 270, PANEL_BG, PANEL_EDGE)
rows = [
    ("＞武器:修罗 攻+12", MAIN),
    ("  衣服:天魔神甲 防+8 血+40", MAIN),
    ("  首饰:绿宝石戒指 攻+4 防+2", MAIN),
    ("攻28 防10  血233/260 蓝58/90", DIM),
    ("· · · · · · · · · · · · · · ·", PANEL_EDGE),
    ("  基础剑术      已学", DIM),
    ("  攻杀剑术      已学", DIM),
    ("  刺杀剑术      书店 800金", GOLD),
    ("  半月弯刀      Lv9·精英", DIM),
    ("  烈火剑法      Lv12·Boss", DIM),
]
y = 50
for s, c in rows:
    if s: text(d, (18, y), s, F16, c)
    y += LH16 + 3
save(img, "12-skills-proposal")

print("all mockups rendered")
