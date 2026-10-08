# -*- coding: utf-8 -*-
"""Render pixel-accurate MAFA CHRONICLE page mockups (240x320, x2 upscale).

Geometry and colors mirror main/mafa_view.c exactly; content reflects the
1.76-alignment plan plus the v1.8 endgame (2026-10-01): level cap 45, 7
skills per class at real 1.76 learn levels, 9 combat maps with 36 floor
bosses (the 8-row map list scrolls, cursor-anchored), the 8-slot paper-doll
gear page, a dedicated skill page, and level-gated store books. Fonts
approximate the device's Source Han Sans subsets with Microsoft YaHei at
16/20px."""
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
    panel(d, 4, 98, 232, 140, PANEL_BG, PANEL_EDGE)
    # Caption bg rides both borders like the device's opaque label (pad 4):
    # drawn after the log panel, covering the strip/log border lines.
    d.rectangle([12, 88, 52, 108], fill=BG)
    text_c(d, 32, 90, "战况", F16, GOLD)
    panel(d, 0, 238, W, 82, (0x0C, 0x0F, 0x13), GOLD_EDGE, radius=0, top_border=True, bw=2)

def header(d, mapname, level, xp_pct, gold, hp, hpmax, mp, mpmax):
    text(d, (10, 6), mapname, F20, GOLD)
    # info_label y=6, line height 19, line_space 0 -> line2 top = 25.
    # The percent is XP progress toward the next level (battery readout
    # moved to the settings page); at the cap the line is bare "Lv.40".
    text_r(d, 234, 6, f"Lv.{level} {xp_pct}%", F16, DIM)
    text_r(d, 234, 25, f"金{gold}", F16, GOLD)
    bar(d, 10, 33, 130, 10, hp/hpmax, HP_GREEN)
    text_r(d, 178, 28, str(hp), F16, MAIN)
    bar(d, 10, 48, 130, 7, mp/mpmax, MP_BLUE)
    text_r(d, 178, 44, str(mp), F16, MAIN)

def enemy(d, s, color, ratio=None):
    if s.startswith("▶"):   # YaHei lacks U+25B6; draw the triangle instead
        d.polygon([(10, 73), (10, 87), (22, 80)], fill=color)
        s = s[1:]
        x = 26
    else:
        x = 10
    text(d, (x, 71), s, F16, color)
    if ratio is not None:
        bar(d, 134, 75, 98, 9, ratio, ENEMY_RED)

def loglines(d, lines):
    y = 107                                      # log_label abs (14,107)
    for s, c in lines:
        text(d, (14, y), s, F16, c)
        y += LH16 + 2

def action_bar(d, cur):
    # 1.76 plan: 7 cells (技能 joins the bar) in a 2x4 grid, 8th cell blank
    # (reserved for a future pet/codex page). Four F20 columns cannot fit
    # 240px (4 x (20 marker + 40 name) alone = 240), so the bar drops to F16
    # — the size every list page already uses — and the log keeps all six
    # lines; the alternative (3 F20 rows) would have cost a log line.
    # Columns pad to a fixed pitch with two ASCII spaces; every prefix is one
    # full-width glyph (＞ marker / 　 filler) so columns stay aligned.
    cells = ["背包", "装备", "技能", "商店", "地图", "设置", "加速4x"]
    gap = d.textlength(" ", font=F16)
    for row in (0, 1):
        x = 12.0
        y = 251 if row == 0 else 282             # bar 238..320, F16 rows
        for col in range(4):
            i = row * 4 + col
            if i >= len(cells):
                break
            s = ("＞" if i == cur else "　") + cells[i]
            text(d, (x, y), s, F16, GOLD if i == cur else DIM)
            x += d.textlength(s, font=F16) + gap * 2

# ---- 1. menu ---------------------------------------------------------------
img, d = new_page()
panel(d, 30, 44, 180, 96, PANEL_BG, GOLD_EDGE, radius=8, bw=2)
text_c(d, 120, 64, "玛法战纪", F20, GOLD)
text_c(d, 120, 100, "「文字传奇」", F16, DIM)
text(d, (80, 180), "＞继续游戏", F16, MAIN)
text(d, (80, 205), "　新游戏", F16, MAIN)
save(img, "01-menu")

# ---- 2. class (1.76 seven-skill blurbs) -------------------------------------
img, d = new_page()
title_band(d, "选择职业")
panel(d, 6, 42, 228, 150, PANEL_BG, PANEL_EDGE)
for i, nm in enumerate(["战士  高血高防", "法师  高魔脆皮", "道士  道术续航"]):
    mark = "＞" if i == 0 else "　"
    text(d, (18, 52 + i * (LH16 + 6)), mark + nm, F16, GOLD if i == 0 else MAIN)
panel(d, 6, 200, 228, 112, PANEL_BG, PANEL_EDGE)
text(d, (18, 210), "攻杀/刺杀/半月/烈火", F16, DIM)
text(d, (18, 230), "Lv7 起步,技能书解锁进阶", F16, DIM)
save(img, "02-class")

# ---- 3. main idle ----------------------------------------------------------
img, d = new_page()
main_chrome(d)
header(d, "比奇省 2层", 8, 87, 1234, 187, 210, 46, 60)
enemy(d, "挂机中 17/40", DIM)
loglines(d, [
    ("遭遇 稻草人 等 2 只!", MAIN),
    ("你造成 14 点伤害", MAIN),
    ("【攻杀剑术】造成 28 伤害!", MAIN),
    ("稻草人 反击,你受 9 伤害", RED),
    ("稻草人 倒下!经验+18", GOLD),
    ("遭遇 多钩猫!", MAIN),
])
action_bar(d, 0)
save(img, "03-main-idle")

# ---- 4. main battle: multi-mob in 石墓 --------------------------------------
img, d = new_page()
main_chrome(d)
header(d, "石墓 4层", 26, 73, 2380, 195, 230, 40, 70)
enemy(d, "▶黑野猪×3", MAIN, 0.42)
loglines(d, [
    ("你造成 23 点伤害", MAIN),
    ("【刺杀剑术】造成 34 伤害!", MAIN),
    ("黑野猪【冲撞】你受 16 伤害!", RED),
    ("红药 +62", GREEN),
    ("黑野猪 倒下!经验+52", GOLD),
    ("遭遇 楔蛾!", MAIN),
])
action_bar(d, 4)
save(img, "04-main-battle")

# ---- 5. main boss: 赤月恶魔 graduation fight (taoist + pet) -----------------
img, d = new_page()
main_chrome(d)
header(d, "赤月峡谷 3层", 38, 41, 5200, 226, 268, 61, 96)
enemy(d, "▶赤月恶魔!", GOLD, 0.71)
loglines(d, [
    ("【Boss】赤月恶魔 出现了!", GOLD),
    ("神兽 出现!", GREEN),
    ("【灵魂火符】造成 52 伤害!", MAIN),
    ("神兽 替你挡下 15 伤害", DIM),
    ("赤月恶魔 反击,你受 24 伤害", RED),
    ("治愈 +86", GREEN),
])
action_bar(d, 5)
save(img, "05-main-boss")

# ---- 6. modal (boss prompt) ------------------------------------------------
img, d = new_page()
main_chrome(d)
header(d, "沃玛寺庙 3层", 26, 55, 2210, 198, 230, 44, 66)
enemy(d, "挂机中 39/40", DIM)
action_bar(d, 0)
ov = Image.new("RGB", (W, H), (0, 0, 0))
img = Image.blend(img, ov, 0.45)
d = ImageDraw.Draw(img)
panel(d, 8, 85, 224, 150, (0x10, 0x18, 0x20), GOLD_EDGE, radius=6)
text_c(d, 120, 100, "【Boss】沃玛教主 出现了!", F16, GOLD)
text(d, (60, 140), "＞迎战", F16, GOLD)
text(d, (60, 170), "　回避", F16, MAIN)
save(img, "06-modal-boss")

# ---- 7. gear page: 8-slot paper doll + stats + potions (1.76 plan) -----------
# The 1.76 equipment panel minus candle and amulet: weapon / helmet / armor
# / necklace / bracelet x2 / ring x2. Rows wear the item's quality color
# (sample: a level-33 warrior in 沃玛-tier gear); the slot prefix keeps the
# twin bracelet/ring rows distinguishable. Skills moved to their own page
# (17). v1.5: the detail box grew to three rows — the four combat stats
# (攻/魔/道/防; the warrior reads 魔0 道0), both pools (the small warrior
# mana pool), and the potion counters. 8 rows at pitch 24 in a 200px list.
img, d = new_page()
title_band(d, "装备")
panel(d, 6, 42, 228, 184, PANEL_BG, PANEL_EDGE)
gear = [
    ("＞武器 井中月", "purple"),
    ("　头盔 黑铁头盔", "blue"),
    ("　衣服 天魔神甲", "blue"),
    ("　项链 恶魔铃铛", "blue"),
    ("　左手镯 骑士手镯", "blue"),
    ("　右手镯 铁手镯", "white"),
    ("　左戒指 力量戒指", "purple"),
    ("　右戒指 珊瑚戒指", "green"),
]
y = 50
for s, q in gear:
    text(d, (18, y), s, F16, QUAL.get(q, MAIN))
    y += LH16 + 2
panel(d, 6, 230, 228, 90, PANEL_BG, PANEL_EDGE)
text(d, (18, 238), "攻41 魔0 道0 防26", F16, DIM)
text(d, (18, 258), "血233/260 蓝20/43", F16, DIM)
x0 = 18 + d.textlength("血233/260 蓝20/43  ", font=F16)
text(d, (x0, 258), "红药x3", F16, RED)
text(d, (x0 + d.textlength("红药x3  ", font=F16), 258), "蓝药x2", F16, MP_BLUE)
text(d, (18, 278), "OK 卸下,长按返回", F16, DIM)
save(img, "07-status")

# ---- 17. skill page: 7 class skills with on/off states (1.76 plan) -----------
# Split from the gear page (8 slots + 7 skills no longer fit one screen).
# State column after two ASCII spaces: 常驻 (passive/proc, never off),
# 开/关 (OK flips and saves, 群攻 tag rides the toggle), then lock reasons
# with the level gate first and the source second — skill 0 needs no book
# and reads "Lv X 自动习得". OK on a fixed/unknown row swaps the help
# lines for the reason until the cursor moves (not shown in this static
# mockup). Sample: a level-33 warrior — every row state visible in one
# screen.
img, d = new_page()
title_band(d, "技能")
panel(d, 6, 42, 228, 190, PANEL_BG, PANEL_EDGE)
skills = [
    ("＞基本剑术  常驻", DIM),
    ("　攻杀剑术  常驻", DIM),
    ("　刺杀剑术  开", MAIN),
    ("　半月弯刀  群攻 关", MAIN),
    ("　野蛮冲撞  精英/Boss", MAIN),
    ("　烈火剑法  Lv35", DIM),
    ("　逐日剑法  Lv38", DIM),
]
y = 52
for s, c in skills:
    text(d, (18, y), s, F16, c)
    y += LH16 + 5
panel(d, 6, 240, 228, 72, PANEL_BG, PANEL_EDGE)
text(d, (18, 248), "OK 切换开关,立即存档", F16, DIM)
text(d, (18, 268), "群攻:2只以上才施放", F16, DIM)
text(d, (18, 288), "高阶书:精英/Boss掉落", F16, DIM)
save(img, "17-skills")

# ---- 8. backpack (gear-only slots; potions live on the gear page) ------------
# The device font's real line height is 20 (LH16 here approximates 19), so
# the list panel mirrors the code's 200px: 8 rows at pitch 24 fit. v1.5: the
# detail box grew to three rows — the five stat deltas of the comparison
# (攻/魔/道 then 防/血) plus the gold line.
img, d = new_page()
title_band(d, "背包")
panel(d, 6, 42, 228, 200, PANEL_BG, PANEL_EDGE)
items = [("＞1.炼狱 x1", "purple"), ("　2.骷髅头盔 x1", "white"),
         ("　3.恶魔铃铛 x1", "blue"), ("　4.空", DIM), ("　5.空", DIM),
         ("　6.空", DIM), ("　7.空", DIM), ("　8.空", DIM)]
y = 50
for s, q in items:
    color = q if isinstance(q, tuple) else QUAL.get(q, MAIN)
    text(d, (18, y), s, F16, color)
    y += LH16 + 4
panel(d, 6, 246, 228, 74, PANEL_BG, PANEL_EDGE)
text(d, (18, 254), "攻+9 魔+0 道+0", F16, DIM)
text(d, (18, 274), "防+5 血+30", F16, DIM)
text(d, (18, 294), "金币 2380", F16, DIM)
save(img, "08-backpack")

# ---- 9. store: potions + three class books, level-gated (1.76 plan) ----------
# Books 1-3 of each class sell here and buying now REQUIRES the learn level:
# an under-level row dims with its LvX hint and OK refuses (the v1.2 hint
# row becomes a hard gate). Drop-line books never appear here. Sample: a
# level-26 warrior — 半月弯刀 shown gated. Title reverts 药店→商店: with a
# three-book shelf the page is no longer a potion shop. v1.6 adds the plain
# 返回 row (OK or long-OK leaves) so long-OK on a potion row means
# hold-to-buy until release or the gold runs out. Gold shows compact k/M.
img, d = new_page()
title_band(d, "商店")
panel(d, 6, 42, 228, 270, PANEL_BG, PANEL_EDGE)
store = [
    ("＞红药 50金 x3", MAIN),
    ("　蓝药 40金 x2", MAIN),
    ("　攻杀剑术 300金 已学", DIM),
    ("　刺杀剑术 600金 已学", DIM),
    ("　半月弯刀 800金 Lv28", DIM),
    ("　返回", MAIN),
    ("", DIM),
    ("金币 2.2k", GOLD),
]
y = 52
for s, c in store:
    if s: text(d, (18, y), s, F16, c)
    y += LH16 + 6
save(img, "09-store")

# ---- 10. maps: safe zone + the 9 combat maps (v1.8 endgame) ------------------
# Ten entries no longer fit the 8-row list: the device renders an 8-row
# window anchored on the cursor. Sample: a late-game player standing on
# Sealed Demon Valley (map 8, unlocked) — the window shows rows 1-8 and
# Azure Moon Island (map 9) waits locked just past the edge.
img, d = new_page()
title_band(d, "地图")
panel(d, 6, 42, 228, 208, PANEL_BG, PANEL_EDGE)
maps = [("　1.比奇省", MAIN), ("　2.兽人古墓", MAIN), ("　3.石墓", MAIN),
        ("　4.沃玛寺庙", MAIN), ("　5.死亡山谷", MAIN), ("　6.祖玛寺庙", MAIN),
        ("　7.赤月峡谷", MAIN), ("＞8.封魔谷", GOLD)]
y = 50
for s, c in maps:
    text(d, (18, y), s, F16, c)
    y += LH16 + 4
panel(d, 6, 256, 228, 56, PANEL_BG, PANEL_EDGE)
text(d, (18, 264), "当前:封魔谷 1层", F16, DIM)
text(d, (18, 284), "击杀40出Boss,末层开下图", F16, DIM)
save(img, "10-maps")

# ---- 16. floor list: OK on a combat map opens its ladder ---------------------
# One row per open floor with that floor's boss; a back row last; the cursor
# rests on the deepest floor. Long-OK backs out to the map list. Sample:
# 祖玛寺庙, the deepest ladder (7 floors). Detail shows the map's full
# depth and the back hint.
img, d = new_page()
title_band(d, "地图")
panel(d, 6, 42, 228, 208, PANEL_BG, PANEL_EDGE)
floors = [("　1层 Boss:祖玛卫士", MAIN), ("　2层 Boss:祖玛弓箭手", MAIN),
          ("　3层 Boss:祖玛卫士", MAIN), ("＞4层 Boss:祖玛雕像", GOLD),
          ("　5层 Boss:祖玛卫士", MAIN), ("　6层 Boss:祖玛雕像", MAIN),
          ("　7层 Boss:祖玛教主", MAIN), ("　返回", MAIN)]
y = 50
for s, c in floors:
    text(d, (18, y), s, F16, c)
    y += LH16 + 4
panel(d, 6, 256, 228, 56, PANEL_BG, PANEL_EDGE)
text(d, (18, 264), "祖玛寺庙 共7层", F16, DIM)
text(d, (18, 284), "长按OK返回地图", F16, DIM)
save(img, "16-maps-floors")

# ---- 10b. main page while resting in the safe zone --------------------------
# 1.76 plan rule: ENTERING the safe zone restores full HP/MP — death respawn
# and a voluntary walk home both heal to max (previously only death did).
# Sample: a level-26 warrior limping out of 石墓; header bars sit at max.
img, d = new_page()
main_chrome(d)
header(d, "安全区", 26, 96, 2210, 230, 230, 70, 70)
enemy(d, "休息中,请选地图", DIM)
loglines(d, [
    ("黑野猪 倒下!经验+52", GOLD),
    ("你回到 安全区", DIM),
    ("血蓝已回满", GREEN),
    ("【安全区】休息中", GOLD),
    ("", MAIN),
    ("", MAIN),
])
action_bar(d, 2)
save(img, "13-main-safe")

# ---- 11. settings: toggles + adjustable auto-potion thresholds (v1.2) -------
# 5 cursor rows: the auto-potion trigger percentages are editable in place
# (OK enters edit, UP/DOWN steps 10%, OK saves); the auto-sell row shows the
# enabled quality set ("自动卖:白"); the battery readout stays a
# non-navigable dim row after a blank row.
img, d = new_page()
title_band(d, "设置")
panel(d, 6, 42, 228, 270, PANEL_BG, PANEL_EDGE)
sets = [
    ("＞自动喝药:开", MAIN),
    ("　红药线:50%", MAIN),
    ("　蓝药线:30%", MAIN),
    ("　自动卖:白", MAIN),
    ("　自动Boss:开", MAIN),
]
for i, (s, c) in enumerate(sets):
    text(d, (18, 52 + i * (LH16 + 6)), s, F16, c)
text(d, (18, 52 + 6 * (LH16 + 6)), "电量 96%", F16, DIM)
save(img, "11-settings")

# ---- 14. settings, threshold edit state (v1.2) -------------------------------
# OK on a threshold row enters edit: the value gains <brackets> and turns
# gold, UP/DOWN step it by 10 (clamped 20-80), OK confirms and saves. Other
# rows dim while editing so the focus is unambiguous.
img, d = new_page()
title_band(d, "设置")
panel(d, 6, 42, 228, 270, PANEL_BG, PANEL_EDGE)
edit = [
    ("　自动喝药:开", DIM),
    ("＞红药线:", MAIN),
    ("　蓝药线:30%", DIM),
    ("　自动卖白:开", DIM),
    ("　自动Boss:开", DIM),
]
y = 52
for s, c in edit:
    text(d, (18, y), s, F16, c)
    y += LH16 + 6
text(d, (18 + d.textlength("＞红药线:", font=F16), 52 + LH16 + 6), "<50%>", F16, GOLD)
text(d, (18, 52 + 6 * (LH16 + 6)), "电量 96%", F16, DIM)
save(img, "14-settings-edit")

# ---- 15. settings, auto-sell quality picker (v1.2) ---------------------------
# OK on the auto-sell row opens the picker: 白/绿/蓝/紫 toggle one bit each
# (every name drawn in its quality color — color is the quality language),
# gold is never auto-sold (legendary drops always reach the player). 完成
# saves and leaves edit; other settings dim while the picker is open.
img, d = new_page()
title_band(d, "设置")
panel(d, 6, 42, 228, 270, PANEL_BG, PANEL_EDGE)
dim_top = ["　自动喝药:开", "　红药线:50%", "　蓝药线:30%"]
y = 52
for s in dim_top:
    text(d, (18, y), s, F16, DIM)
    y += LH16 + 5
picker = [
    ("＞", "白", QUAL["white"], "  开", MAIN),
    ("　", "绿", QUAL["green"], "  开", MAIN),
    ("　", "蓝", QUAL["blue"], "  关", DIM),
    ("　", "紫", QUAL["purple"], "  关", DIM),
]
for mark, name, qc, state, sc in picker:
    text(d, (18, y), mark, F16, MAIN)
    text(d, (18 + d.textlength(mark, font=F16), y), name, F16, qc)
    text(d, (18 + d.textlength(mark + name, font=F16), y), state, F16, sc)
    y += LH16 + 5
text(d, (18, y), "　完成", F16, GOLD)
y += LH16 + 5
text(d, (18, y), "　自动Boss:开", F16, DIM)
y += LH16 + 5
text(d, (18, y), "电量 96%", F16, DIM)
save(img, "15-settings-sell")

print("all mockups rendered")
