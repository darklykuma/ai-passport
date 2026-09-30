// main/mafa_model.c — MAFA CHRONICLE pure game model. Host-testable;
// no LVGL/ESP-IDF. All rolls draw from one splitmix32 stream (PRD 8.1 rule
// carried over from FOG MARCH: a single source of randomness).
// v1.5 stats 2.0 (2026-09-30): the original's 攻击/魔法/道术 lines — each
// class's skills scale its own stat (warrior 攻 / mage 魔 / taoist 道),
// gear carries class-affine stat lines (圣战=攻 法神=魔 天尊=道), warriors
// gain a small mana pool (烈火/半月/野蛮 cost MP), taoist pet/poison/heal
// scale with 道术, trash also drops potions, and save v7 migrates v6 ids.
#include "mafa_model.h"

#include <string.h>

/* --- Content tables (PRD 9.2 / 9.3 / 8.4) --------------------------------- */

/* Paper-doll positions: the 1.76 panel minus candle and amulet. Twin
 * bracelets / rings are separate positions sharing one slot type, so a
 * drop equips into the free twin before replacing the worn one. */
const uint8_t MAFA_POS_TYPE[MAFA_EQ_SLOTS] = {
    MAFA_ST_WEAPON, MAFA_ST_HELMET, MAFA_ST_ARMOR, MAFA_ST_NECKLACE,
    MAFA_ST_BRACELET, MAFA_ST_BRACELET, MAFA_ST_RING, MAFA_ST_RING,
};

/* 252 rows (v1.5 stats 2.0): per (map, tier) the weapon and — from the
 * 沃玛 tier on — every jewelry slot carries 战/法/道 class lines, mirroring
 * the original's per-tier triads; helmets/armors stay single rows (neutral
 * early, class-tilted 赤月名 later). Item ids are stored as bytes across
 * saves/events, so the table must stay within 256 rows (asserted below).
 * Names are the original's real lines (verified 2026-09-30 against
 * community tables: warrior 匕首→青铜斧→修罗→炼狱→井中月→裁决之杖→屠龙;
 * mage 乌木剑→海魂→偃月→魔杖→血饮→骨玉权杖→嗜魂法杖; taoist 木剑→降魔→
 * 银蛇→无极棍→龙纹剑→逍遥扇(1.75, approved exception); 沃玛 triads 幽灵/
 * 生命/天珠 项链 + 幽灵手套/思贝儿/心灵 手镯 + 龙之戒/红宝石/铂金 戒指;
 * 祖玛 triads 绿色/恶魔铃铛/灵魂 项链 + 骑士/龙之手镯/三眼 手镯 + 力量/
 * 紫碧螺/泰坦 戒指; 赤月 sets 圣战/法神/天尊). The line column drives the
 * drop roll (own line ~60 %) and the kit pickers. */
const mafa_item_t MAFA_ITEMS[] = {
    /* map 1 比奇省: white / green / blue — the original's generic starter
     * gear (木剑 is the classic all-class first weapon, 盛趣 official) */
    {"木剑",   MAFA_ST_WEAPON,   MAFA_Q_WHITE, 1, 1, MAFA_LINE_NEUTRAL,  4, 0, 2, 2,  0},
    {"木剑",   MAFA_ST_WEAPON,   MAFA_Q_GREEN, 1, 2, MAFA_LINE_NEUTRAL,  7, 0, 4, 4,  0},
    {"木剑",   MAFA_ST_WEAPON,   MAFA_Q_BLUE,  1, 3, MAFA_LINE_NEUTRAL, 11, 0, 6, 6,  0},
    {"骷髅头盔", MAFA_ST_HELMET, MAFA_Q_WHITE, 1, 1, MAFA_LINE_NEUTRAL,  0, 2, 0, 0, 18},
    {"骷髅头盔", MAFA_ST_HELMET, MAFA_Q_GREEN, 1, 2, MAFA_LINE_NEUTRAL,  0, 3, 0, 0, 23},
    {"骷髅头盔", MAFA_ST_HELMET, MAFA_Q_BLUE,  1, 3, MAFA_LINE_NEUTRAL,  0, 4, 0, 0, 28},
    {"布衣",   MAFA_ST_ARMOR,    MAFA_Q_WHITE, 1, 1, MAFA_LINE_NEUTRAL,  0, 4, 0, 0, 35},
    {"布衣",   MAFA_ST_ARMOR,    MAFA_Q_GREEN, 1, 2, MAFA_LINE_NEUTRAL,  0, 5, 0, 0, 45},
    {"布衣",   MAFA_ST_ARMOR,    MAFA_Q_BLUE,  1, 3, MAFA_LINE_NEUTRAL,  0, 7, 0, 0, 55},
    {"金项链", MAFA_ST_NECKLACE, MAFA_Q_WHITE, 1, 1, MAFA_LINE_NEUTRAL,  4, 0, 4, 4, 11},
    {"金项链", MAFA_ST_NECKLACE, MAFA_Q_GREEN, 1, 2, MAFA_LINE_NEUTRAL,  5, 0, 5, 5, 14},
    {"金项链", MAFA_ST_NECKLACE, MAFA_Q_BLUE,  1, 3, MAFA_LINE_NEUTRAL,  7, 0, 7, 7, 18},
    {"大手镯", MAFA_ST_BRACELET, MAFA_Q_WHITE, 1, 1, MAFA_LINE_NEUTRAL,  1, 2, 1, 1,  0},
    {"大手镯", MAFA_ST_BRACELET, MAFA_Q_GREEN, 1, 2, MAFA_LINE_NEUTRAL,  1, 3, 2, 2,  0},
    {"大手镯", MAFA_ST_BRACELET, MAFA_Q_BLUE,  1, 3, MAFA_LINE_NEUTRAL,  2, 4, 3, 3,  0},
    {"古铜戒指", MAFA_ST_RING,   MAFA_Q_WHITE, 1, 1, MAFA_LINE_NEUTRAL,  4, 0, 4, 4,  0},
    {"古铜戒指", MAFA_ST_RING,   MAFA_Q_GREEN, 1, 2, MAFA_LINE_NEUTRAL,  5, 0, 5, 5,  0},
    {"古铜戒指", MAFA_ST_RING,   MAFA_Q_BLUE,  1, 3, MAFA_LINE_NEUTRAL,  6, 0, 6, 6,  0},
    /* map 2 兽人古墓: green / blue / purple */
    {"青铜斧", MAFA_ST_WEAPON,   MAFA_Q_GREEN, 2, 1, MAFA_LINE_WARRIOR,  7, 0, 0, 0,  0},
    {"海魂",   MAFA_ST_WEAPON,   MAFA_Q_GREEN, 2, 1, MAFA_LINE_MAGE,     4, 0, 7, 0,  0},
    {"降魔",   MAFA_ST_WEAPON,   MAFA_Q_GREEN, 2, 1, MAFA_LINE_TAOIST,   4, 0, 0, 7,  0},
    {"青铜斧", MAFA_ST_WEAPON,   MAFA_Q_BLUE,  2, 2, MAFA_LINE_WARRIOR, 10, 0, 0, 0,  0},
    {"海魂",   MAFA_ST_WEAPON,   MAFA_Q_BLUE,  2, 2, MAFA_LINE_MAGE,     5, 0,10, 0,  0},
    {"降魔",   MAFA_ST_WEAPON,   MAFA_Q_BLUE,  2, 2, MAFA_LINE_TAOIST,   5, 0, 0,10,  0},
    {"青铜斧", MAFA_ST_WEAPON,   MAFA_Q_PURPLE,2, 3, MAFA_LINE_WARRIOR, 14, 0, 0, 0,  0},
    {"海魂",   MAFA_ST_WEAPON,   MAFA_Q_PURPLE,2, 3, MAFA_LINE_MAGE,     7, 0,14, 0,  0},
    {"降魔",   MAFA_ST_WEAPON,   MAFA_Q_PURPLE,2, 3, MAFA_LINE_TAOIST,   7, 0, 0,14,  0},
    {"黑铁头盔", MAFA_ST_HELMET, MAFA_Q_GREEN, 2, 1, MAFA_LINE_NEUTRAL,  0, 3, 0, 0, 26},
    {"黑铁头盔", MAFA_ST_HELMET, MAFA_Q_BLUE,  2, 2, MAFA_LINE_NEUTRAL,  0, 4, 0, 0, 31},
    {"黑铁头盔", MAFA_ST_HELMET, MAFA_Q_PURPLE,2, 3, MAFA_LINE_NEUTRAL,  0, 5, 0, 0, 36},
    {"轻型盔甲", MAFA_ST_ARMOR,  MAFA_Q_GREEN, 2, 1, MAFA_LINE_NEUTRAL,  0, 6, 0, 0, 50},
    {"轻型盔甲", MAFA_ST_ARMOR,  MAFA_Q_BLUE,  2, 2, MAFA_LINE_NEUTRAL,  0, 7, 0, 0, 60},
    {"轻型盔甲", MAFA_ST_ARMOR,  MAFA_Q_PURPLE,2, 3, MAFA_LINE_NEUTRAL,  0, 9, 0, 0, 70},
    {"竹笛",   MAFA_ST_NECKLACE, MAFA_Q_GREEN, 2, 1, MAFA_LINE_NEUTRAL,  6, 0, 6, 6, 17},
    {"竹笛",   MAFA_ST_NECKLACE, MAFA_Q_BLUE,  2, 2, MAFA_LINE_NEUTRAL,  7, 0, 7, 7, 20},
    {"竹笛",   MAFA_ST_NECKLACE, MAFA_Q_PURPLE,2, 3, MAFA_LINE_NEUTRAL,  9, 0, 9, 9, 24},
    {"铁手镯", MAFA_ST_BRACELET, MAFA_Q_GREEN, 2, 1, MAFA_LINE_NEUTRAL,  2, 3, 2, 2,  0},
    {"铁手镯", MAFA_ST_BRACELET, MAFA_Q_BLUE,  2, 2, MAFA_LINE_NEUTRAL,  2, 4, 4, 4,  0},
    {"铁手镯", MAFA_ST_BRACELET, MAFA_Q_PURPLE,2, 3, MAFA_LINE_NEUTRAL,  3, 5, 6, 6,  0},
    {"珊瑚戒指", MAFA_ST_RING,   MAFA_Q_GREEN, 2, 1, MAFA_LINE_NEUTRAL,  6, 0, 6, 6,  0},
    {"珊瑚戒指", MAFA_ST_RING,   MAFA_Q_BLUE,  2, 2, MAFA_LINE_NEUTRAL,  7, 0, 7, 7,  0},
    {"珊瑚戒指", MAFA_ST_RING,   MAFA_Q_PURPLE,2, 3, MAFA_LINE_NEUTRAL,  8, 0, 8, 8,  0},
    /* map 3 石墓: green / blue / purple */
    {"修罗",   MAFA_ST_WEAPON,   MAFA_Q_GREEN, 3, 1, MAFA_LINE_WARRIOR, 10, 0, 0, 0,  0},
    {"偃月",   MAFA_ST_WEAPON,   MAFA_Q_GREEN, 3, 1, MAFA_LINE_MAGE,     5, 0,10, 0,  0},
    {"降魔",   MAFA_ST_WEAPON,   MAFA_Q_GREEN, 3, 1, MAFA_LINE_TAOIST,   5, 0, 0,10,  0},
    {"修罗",   MAFA_ST_WEAPON,   MAFA_Q_BLUE,  3, 2, MAFA_LINE_WARRIOR, 13, 0, 0, 0,  0},
    {"偃月",   MAFA_ST_WEAPON,   MAFA_Q_BLUE,  3, 2, MAFA_LINE_MAGE,     7, 0,13, 0,  0},
    {"降魔",   MAFA_ST_WEAPON,   MAFA_Q_BLUE,  3, 2, MAFA_LINE_TAOIST,   7, 0, 0,13,  0},
    {"修罗",   MAFA_ST_WEAPON,   MAFA_Q_PURPLE,3, 3, MAFA_LINE_WARRIOR, 17, 0, 0, 0,  0},
    {"偃月",   MAFA_ST_WEAPON,   MAFA_Q_PURPLE,3, 3, MAFA_LINE_MAGE,     9, 0,17, 0,  0},
    {"降魔",   MAFA_ST_WEAPON,   MAFA_Q_PURPLE,3, 3, MAFA_LINE_TAOIST,   9, 0, 0,17,  0},
    {"黑铁头盔", MAFA_ST_HELMET, MAFA_Q_GREEN, 3, 1, MAFA_LINE_NEUTRAL,  0, 4, 0, 0, 34},
    {"黑铁头盔", MAFA_ST_HELMET, MAFA_Q_BLUE,  3, 2, MAFA_LINE_NEUTRAL,  0, 5, 0, 0, 39},
    {"黑铁头盔", MAFA_ST_HELMET, MAFA_Q_PURPLE,3, 3, MAFA_LINE_NEUTRAL,  0, 6, 0, 0, 44},
    {"重型盔甲", MAFA_ST_ARMOR,  MAFA_Q_GREEN, 3, 1, MAFA_LINE_NEUTRAL,  0, 8, 0, 0, 65},
    {"重型盔甲", MAFA_ST_ARMOR,  MAFA_Q_BLUE,  3, 2, MAFA_LINE_NEUTRAL,  0, 9, 0, 0, 75},
    {"重型盔甲", MAFA_ST_ARMOR,  MAFA_Q_PURPLE,3, 3, MAFA_LINE_NEUTRAL,  0,11, 0, 0, 85},
    {"放大镜", MAFA_ST_NECKLACE, MAFA_Q_GREEN, 3, 1, MAFA_LINE_NEUTRAL,  8, 0, 8, 8, 23},
    {"放大镜", MAFA_ST_NECKLACE, MAFA_Q_BLUE,  3, 2, MAFA_LINE_NEUTRAL,  9, 0, 9, 9, 26},
    {"放大镜", MAFA_ST_NECKLACE, MAFA_Q_PURPLE,3, 3, MAFA_LINE_NEUTRAL, 11, 0,11,11, 30},
    {"铁手镯", MAFA_ST_BRACELET, MAFA_Q_GREEN, 3, 1, MAFA_LINE_NEUTRAL,  2, 4, 2, 2,  0},
    {"铁手镯", MAFA_ST_BRACELET, MAFA_Q_BLUE,  3, 2, MAFA_LINE_NEUTRAL,  2, 5, 4, 4,  0},
    {"铁手镯", MAFA_ST_BRACELET, MAFA_Q_PURPLE,3, 3, MAFA_LINE_NEUTRAL,  3, 6, 6, 6,  0},
    {"降妖除魔戒指", MAFA_ST_RING, MAFA_Q_GREEN, 3, 1, MAFA_LINE_NEUTRAL,  8, 0, 8, 8,  0},
    {"降妖除魔戒指", MAFA_ST_RING, MAFA_Q_BLUE,  3, 2, MAFA_LINE_NEUTRAL,  9, 0, 9, 9,  0},
    {"降妖除魔戒指", MAFA_ST_RING, MAFA_Q_PURPLE,3, 3, MAFA_LINE_NEUTRAL, 10, 0,10,10,  0},
    /* map 4 沃玛寺庙: blue / purple / purple — 沃玛级 triads begin */
    {"炼狱",     MAFA_ST_WEAPON,   MAFA_Q_BLUE,   4, 1, MAFA_LINE_WARRIOR, 13, 0, 0, 0,  0},
    {"魔杖",     MAFA_ST_WEAPON,   MAFA_Q_BLUE,   4, 1, MAFA_LINE_MAGE,     7, 0,13, 0,  0},
    {"银蛇",     MAFA_ST_WEAPON,   MAFA_Q_BLUE,   4, 1, MAFA_LINE_TAOIST,   7, 0, 0,13,  0},
    {"炼狱",     MAFA_ST_WEAPON,   MAFA_Q_PURPLE, 4, 2, MAFA_LINE_WARRIOR, 16, 0, 0, 0,  0},
    {"魔杖",     MAFA_ST_WEAPON,   MAFA_Q_PURPLE, 4, 2, MAFA_LINE_MAGE,     8, 0,16, 0,  0},
    {"银蛇",     MAFA_ST_WEAPON,   MAFA_Q_PURPLE, 4, 2, MAFA_LINE_TAOIST,   8, 0, 0,16,  0},
    {"炼狱",     MAFA_ST_WEAPON,   MAFA_Q_PURPLE, 4, 3, MAFA_LINE_WARRIOR, 20, 0, 0, 0,  0},
    {"魔杖",     MAFA_ST_WEAPON,   MAFA_Q_PURPLE, 4, 3, MAFA_LINE_MAGE,    10, 0,20, 0,  0},
    {"银蛇",     MAFA_ST_WEAPON,   MAFA_Q_PURPLE, 4, 3, MAFA_LINE_TAOIST,  10, 0, 0,20,  0},
    {"黑铁头盔", MAFA_ST_HELMET,   MAFA_Q_BLUE,   4, 1, MAFA_LINE_NEUTRAL,  0, 5, 0, 0, 42},
    {"黑铁头盔", MAFA_ST_HELMET,   MAFA_Q_PURPLE, 4, 2, MAFA_LINE_NEUTRAL,  0, 6, 0, 0, 47},
    {"黑铁头盔", MAFA_ST_HELMET,   MAFA_Q_PURPLE, 4, 3, MAFA_LINE_NEUTRAL,  0, 7, 0, 0, 52},
    {"重型盔甲", MAFA_ST_ARMOR,    MAFA_Q_BLUE,   4, 1, MAFA_LINE_NEUTRAL,  0,10, 0, 0, 80},
    {"重型盔甲", MAFA_ST_ARMOR,    MAFA_Q_PURPLE, 4, 2, MAFA_LINE_NEUTRAL,  0,11, 0, 0, 90},
    {"重型盔甲", MAFA_ST_ARMOR,    MAFA_Q_PURPLE, 4, 3, MAFA_LINE_NEUTRAL,  0,13, 0, 0,100},
    {"幽灵项链", MAFA_ST_NECKLACE, MAFA_Q_BLUE,   4, 1, MAFA_LINE_WARRIOR, 10, 0, 0, 0, 29},
    {"生命项链", MAFA_ST_NECKLACE, MAFA_Q_BLUE,   4, 1, MAFA_LINE_MAGE,     3, 0,10, 0, 29},
    {"天珠项链", MAFA_ST_NECKLACE, MAFA_Q_BLUE,   4, 1, MAFA_LINE_TAOIST,   3, 0, 0,10, 29},
    {"幽灵项链", MAFA_ST_NECKLACE, MAFA_Q_PURPLE, 4, 2, MAFA_LINE_WARRIOR, 11, 0, 0, 0, 32},
    {"生命项链", MAFA_ST_NECKLACE, MAFA_Q_PURPLE, 4, 2, MAFA_LINE_MAGE,     3, 0,11, 0, 32},
    {"天珠项链", MAFA_ST_NECKLACE, MAFA_Q_PURPLE, 4, 2, MAFA_LINE_TAOIST,   3, 0, 0,11, 32},
    {"幽灵项链", MAFA_ST_NECKLACE, MAFA_Q_PURPLE, 4, 3, MAFA_LINE_WARRIOR, 13, 0, 0, 0, 36},
    {"生命项链", MAFA_ST_NECKLACE, MAFA_Q_PURPLE, 4, 3, MAFA_LINE_MAGE,     4, 0,13, 0, 36},
    {"天珠项链", MAFA_ST_NECKLACE, MAFA_Q_PURPLE, 4, 3, MAFA_LINE_TAOIST,   4, 0, 0,13, 36},
    {"幽灵手套", MAFA_ST_BRACELET, MAFA_Q_BLUE,   4, 1, MAFA_LINE_WARRIOR,  3, 5, 0, 0,  0},
    {"思贝儿手镯", MAFA_ST_BRACELET, MAFA_Q_BLUE, 4, 1, MAFA_LINE_MAGE,     0, 5, 3, 0,  0},
    {"心灵手镯", MAFA_ST_BRACELET, MAFA_Q_BLUE,   4, 1, MAFA_LINE_TAOIST,   0, 5, 0, 3,  0},
    {"幽灵手套", MAFA_ST_BRACELET, MAFA_Q_PURPLE, 4, 2, MAFA_LINE_WARRIOR,  3, 6, 0, 0,  0},
    {"思贝儿手镯", MAFA_ST_BRACELET, MAFA_Q_PURPLE,4, 2, MAFA_LINE_MAGE,    0, 6, 3, 0,  0},
    {"心灵手镯", MAFA_ST_BRACELET, MAFA_Q_PURPLE, 4, 2, MAFA_LINE_TAOIST,   0, 6, 0, 3,  0},
    {"幽灵手套", MAFA_ST_BRACELET, MAFA_Q_PURPLE, 4, 3, MAFA_LINE_WARRIOR,  4, 7, 0, 0,  0},
    {"思贝儿手镯", MAFA_ST_BRACELET, MAFA_Q_PURPLE,4, 3, MAFA_LINE_MAGE,    0, 7, 4, 0,  0},
    {"心灵手镯", MAFA_ST_BRACELET, MAFA_Q_PURPLE, 4, 3, MAFA_LINE_TAOIST,   0, 7, 0, 4,  0},
    {"龙之戒",   MAFA_ST_RING,     MAFA_Q_BLUE,   4, 1, MAFA_LINE_WARRIOR, 10, 0, 0, 0,  0},
    {"红宝石戒指", MAFA_ST_RING,   MAFA_Q_BLUE,   4, 1, MAFA_LINE_MAGE,     0, 0,10, 0,  0},
    {"铂金戒指", MAFA_ST_RING,     MAFA_Q_BLUE,   4, 1, MAFA_LINE_TAOIST,   0, 0, 0,10,  0},
    {"龙之戒",   MAFA_ST_RING,     MAFA_Q_PURPLE, 4, 2, MAFA_LINE_WARRIOR, 11, 0, 0, 0,  0},
    {"红宝石戒指", MAFA_ST_RING,   MAFA_Q_PURPLE, 4, 2, MAFA_LINE_MAGE,     0, 0,11, 0,  0},
    {"铂金戒指", MAFA_ST_RING,     MAFA_Q_PURPLE, 4, 2, MAFA_LINE_TAOIST,   0, 0, 0,11,  0},
    {"龙之戒",   MAFA_ST_RING,     MAFA_Q_PURPLE, 4, 3, MAFA_LINE_WARRIOR, 12, 0, 0, 0,  0},
    {"红宝石戒指", MAFA_ST_RING,   MAFA_Q_PURPLE, 4, 3, MAFA_LINE_MAGE,     0, 0,12, 0,  0},
    {"铂金戒指", MAFA_ST_RING,     MAFA_Q_PURPLE, 4, 3, MAFA_LINE_TAOIST,   0, 0, 0,12,  0},
    /* map 5 死亡山谷: blue / purple / gold — 祖玛级 triads + 赤月-class
     * helmets/armors begin */
    {"井中月",   MAFA_ST_WEAPON,   MAFA_Q_BLUE,   5, 1, MAFA_LINE_WARRIOR, 16, 0, 0, 0,  0},
    {"血饮",     MAFA_ST_WEAPON,   MAFA_Q_BLUE,   5, 1, MAFA_LINE_MAGE,     8, 0,16, 0,  0},
    {"无极棍",   MAFA_ST_WEAPON,   MAFA_Q_BLUE,   5, 1, MAFA_LINE_TAOIST,   8, 0, 0,16,  0},
    {"井中月",   MAFA_ST_WEAPON,   MAFA_Q_PURPLE, 5, 2, MAFA_LINE_WARRIOR, 19, 0, 0, 0,  0},
    {"血饮",     MAFA_ST_WEAPON,   MAFA_Q_PURPLE, 5, 2, MAFA_LINE_MAGE,    10, 0,19, 0,  0},
    {"无极棍",   MAFA_ST_WEAPON,   MAFA_Q_PURPLE, 5, 2, MAFA_LINE_TAOIST,  10, 0, 0,19,  0},
    {"井中月",   MAFA_ST_WEAPON,   MAFA_Q_GOLD,   5, 3, MAFA_LINE_WARRIOR, 23, 0, 0, 0,  0},
    {"血饮",     MAFA_ST_WEAPON,   MAFA_Q_GOLD,   5, 3, MAFA_LINE_MAGE,    12, 0,23, 0,  0},
    {"无极棍",   MAFA_ST_WEAPON,   MAFA_Q_GOLD,   5, 3, MAFA_LINE_TAOIST,  12, 0, 0,23,  0},
    {"圣战头盔", MAFA_ST_HELMET,   MAFA_Q_BLUE,   5, 1, MAFA_LINE_WARRIOR,  2, 6, 0, 0, 50},
    {"圣战头盔", MAFA_ST_HELMET,   MAFA_Q_PURPLE, 5, 2, MAFA_LINE_WARRIOR,  3, 7, 0, 0, 55},
    {"圣战头盔", MAFA_ST_HELMET,   MAFA_Q_GOLD,   5, 3, MAFA_LINE_WARRIOR,  4, 8, 0, 0, 60},
    {"天魔神甲", MAFA_ST_ARMOR,    MAFA_Q_BLUE,   5, 1, MAFA_LINE_WARRIOR,  2,12, 0, 0, 95},
    {"法神披风", MAFA_ST_ARMOR,    MAFA_Q_BLUE,   5, 1, MAFA_LINE_MAGE,     0,12, 2, 0, 95},
    {"天尊道袍", MAFA_ST_ARMOR,    MAFA_Q_BLUE,   5, 1, MAFA_LINE_TAOIST,   0,12, 0, 2, 95},
    {"天魔神甲", MAFA_ST_ARMOR,    MAFA_Q_PURPLE, 5, 2, MAFA_LINE_WARRIOR,  3,13, 0, 0,105},
    {"法神披风", MAFA_ST_ARMOR,    MAFA_Q_PURPLE, 5, 2, MAFA_LINE_MAGE,     0,13, 3, 0,105},
    {"天尊道袍", MAFA_ST_ARMOR,    MAFA_Q_PURPLE, 5, 2, MAFA_LINE_TAOIST,   0,13, 0, 3,105},
    {"天魔神甲", MAFA_ST_ARMOR,    MAFA_Q_GOLD,   5, 3, MAFA_LINE_WARRIOR,  4,15, 0, 0,115},
    {"法神披风", MAFA_ST_ARMOR,    MAFA_Q_GOLD,   5, 3, MAFA_LINE_MAGE,     0,15, 4, 0,115},
    {"天尊道袍", MAFA_ST_ARMOR,    MAFA_Q_GOLD,   5, 3, MAFA_LINE_TAOIST,   0,15, 0, 4,115},
    {"绿色项链", MAFA_ST_NECKLACE, MAFA_Q_BLUE,   5, 1, MAFA_LINE_WARRIOR, 12, 0, 0, 0, 35},
    {"恶魔铃铛", MAFA_ST_NECKLACE, MAFA_Q_BLUE,   5, 1, MAFA_LINE_MAGE,     4, 0,12, 0, 35},
    {"灵魂项链", MAFA_ST_NECKLACE, MAFA_Q_BLUE,   5, 1, MAFA_LINE_TAOIST,   4, 0, 0,12, 35},
    {"绿色项链", MAFA_ST_NECKLACE, MAFA_Q_PURPLE, 5, 2, MAFA_LINE_WARRIOR, 13, 0, 0, 0, 38},
    {"恶魔铃铛", MAFA_ST_NECKLACE, MAFA_Q_PURPLE, 5, 2, MAFA_LINE_MAGE,     4, 0,13, 0, 38},
    {"灵魂项链", MAFA_ST_NECKLACE, MAFA_Q_PURPLE, 5, 2, MAFA_LINE_TAOIST,   4, 0, 0,13, 38},
    {"绿色项链", MAFA_ST_NECKLACE, MAFA_Q_GOLD,   5, 3, MAFA_LINE_WARRIOR, 15, 0, 0, 0, 42},
    {"恶魔铃铛", MAFA_ST_NECKLACE, MAFA_Q_GOLD,   5, 3, MAFA_LINE_MAGE,     5, 0,15, 0, 42},
    {"灵魂项链", MAFA_ST_NECKLACE, MAFA_Q_GOLD,   5, 3, MAFA_LINE_TAOIST,   5, 0, 0,15, 42},
    {"骑士手镯", MAFA_ST_BRACELET, MAFA_Q_BLUE,   5, 1, MAFA_LINE_WARRIOR,  3, 6, 0, 0,  0},
    {"龙之手镯", MAFA_ST_BRACELET, MAFA_Q_BLUE,   5, 1, MAFA_LINE_MAGE,     0, 6, 3, 0,  0},
    {"三眼手镯", MAFA_ST_BRACELET, MAFA_Q_BLUE,   5, 1, MAFA_LINE_TAOIST,   0, 6, 0, 3,  0},
    {"骑士手镯", MAFA_ST_BRACELET, MAFA_Q_PURPLE, 5, 2, MAFA_LINE_WARRIOR,  3, 7, 0, 0,  0},
    {"龙之手镯", MAFA_ST_BRACELET, MAFA_Q_PURPLE, 5, 2, MAFA_LINE_MAGE,     0, 7, 3, 0,  0},
    {"三眼手镯", MAFA_ST_BRACELET, MAFA_Q_PURPLE, 5, 2, MAFA_LINE_TAOIST,   0, 7, 0, 3,  0},
    {"骑士手镯", MAFA_ST_BRACELET, MAFA_Q_GOLD,   5, 3, MAFA_LINE_WARRIOR,  4, 8, 0, 0,  0},
    {"龙之手镯", MAFA_ST_BRACELET, MAFA_Q_GOLD,   5, 3, MAFA_LINE_MAGE,     0, 8, 4, 0,  0},
    {"三眼手镯", MAFA_ST_BRACELET, MAFA_Q_GOLD,   5, 3, MAFA_LINE_TAOIST,   0, 8, 0, 4,  0},
    {"力量戒指", MAFA_ST_RING,     MAFA_Q_BLUE,   5, 1, MAFA_LINE_WARRIOR, 12, 0, 0, 0,  0},
    {"紫碧螺",   MAFA_ST_RING,     MAFA_Q_BLUE,   5, 1, MAFA_LINE_MAGE,     0, 0,12, 0,  0},
    {"泰坦戒指", MAFA_ST_RING,     MAFA_Q_BLUE,   5, 1, MAFA_LINE_TAOIST,   0, 0, 0,12,  0},
    {"力量戒指", MAFA_ST_RING,     MAFA_Q_PURPLE, 5, 2, MAFA_LINE_WARRIOR, 13, 0, 0, 0,  0},
    {"紫碧螺",   MAFA_ST_RING,     MAFA_Q_PURPLE, 5, 2, MAFA_LINE_MAGE,     0, 0,13, 0,  0},
    {"泰坦戒指", MAFA_ST_RING,     MAFA_Q_PURPLE, 5, 2, MAFA_LINE_TAOIST,   0, 0, 0,13,  0},
    {"力量戒指", MAFA_ST_RING,     MAFA_Q_GOLD,   5, 3, MAFA_LINE_WARRIOR, 14, 0, 0, 0,  0},
    {"紫碧螺",   MAFA_ST_RING,     MAFA_Q_GOLD,   5, 3, MAFA_LINE_MAGE,     0, 0,14, 0,  0},
    {"泰坦戒指", MAFA_ST_RING,     MAFA_Q_GOLD,   5, 3, MAFA_LINE_TAOIST,   0, 0, 0,14,  0},
    /* map 6 祖玛寺庙: purple / gold / gold — 祖玛三神兵 + 圣战-class armors */
    {"裁决之杖", MAFA_ST_WEAPON,   MAFA_Q_PURPLE, 6, 1, MAFA_LINE_WARRIOR, 19, 0, 0, 0,  0},
    {"骨玉权杖", MAFA_ST_WEAPON,   MAFA_Q_PURPLE, 6, 1, MAFA_LINE_MAGE,    10, 0,19, 0,  0},
    {"龙纹剑",   MAFA_ST_WEAPON,   MAFA_Q_PURPLE, 6, 1, MAFA_LINE_TAOIST,  10, 0, 0,19,  0},
    {"裁决之杖", MAFA_ST_WEAPON,   MAFA_Q_GOLD,   6, 2, MAFA_LINE_WARRIOR, 22, 0, 0, 0,  0},
    {"骨玉权杖", MAFA_ST_WEAPON,   MAFA_Q_GOLD,   6, 2, MAFA_LINE_MAGE,    11, 0,22, 0,  0},
    {"龙纹剑",   MAFA_ST_WEAPON,   MAFA_Q_GOLD,   6, 2, MAFA_LINE_TAOIST,  11, 0, 0,22,  0},
    {"裁决之杖", MAFA_ST_WEAPON,   MAFA_Q_GOLD,   6, 3, MAFA_LINE_WARRIOR, 26, 0, 0, 0,  0},
    {"骨玉权杖", MAFA_ST_WEAPON,   MAFA_Q_GOLD,   6, 3, MAFA_LINE_MAGE,    13, 0,26, 0,  0},
    {"龙纹剑",   MAFA_ST_WEAPON,   MAFA_Q_GOLD,   6, 3, MAFA_LINE_TAOIST,  13, 0, 0,26,  0},
    {"法神头盔", MAFA_ST_HELMET,   MAFA_Q_PURPLE, 6, 1, MAFA_LINE_MAGE,     0, 7, 2, 0, 58},
    {"法神头盔", MAFA_ST_HELMET,   MAFA_Q_GOLD,   6, 2, MAFA_LINE_MAGE,     0, 8, 3, 0, 63},
    {"法神头盔", MAFA_ST_HELMET,   MAFA_Q_GOLD,   6, 3, MAFA_LINE_MAGE,     0, 9, 4, 0, 68},
    {"圣战宝甲", MAFA_ST_ARMOR,    MAFA_Q_PURPLE, 6, 1, MAFA_LINE_WARRIOR,  3,14, 0, 0,110},
    {"霓裳羽衣", MAFA_ST_ARMOR,    MAFA_Q_PURPLE, 6, 1, MAFA_LINE_MAGE,     0,14, 3, 0,110},
    {"天师长袍", MAFA_ST_ARMOR,    MAFA_Q_PURPLE, 6, 1, MAFA_LINE_TAOIST,   0,14, 0, 3,110},
    {"圣战宝甲", MAFA_ST_ARMOR,    MAFA_Q_GOLD,   6, 2, MAFA_LINE_WARRIOR,  4,15, 0, 0,120},
    {"霓裳羽衣", MAFA_ST_ARMOR,    MAFA_Q_GOLD,   6, 2, MAFA_LINE_MAGE,     0,15, 4, 0,120},
    {"天师长袍", MAFA_ST_ARMOR,    MAFA_Q_GOLD,   6, 2, MAFA_LINE_TAOIST,   0,15, 0, 4,120},
    {"圣战宝甲", MAFA_ST_ARMOR,    MAFA_Q_GOLD,   6, 3, MAFA_LINE_WARRIOR,  4,17, 0, 0,130},
    {"霓裳羽衣", MAFA_ST_ARMOR,    MAFA_Q_GOLD,   6, 3, MAFA_LINE_MAGE,     0,17, 4, 0,130},
    {"天师长袍", MAFA_ST_ARMOR,    MAFA_Q_GOLD,   6, 3, MAFA_LINE_TAOIST,   0,17, 0, 4,130},
    {"绿色项链", MAFA_ST_NECKLACE, MAFA_Q_PURPLE, 6, 1, MAFA_LINE_WARRIOR, 14, 0, 0, 0, 41},
    {"恶魔铃铛", MAFA_ST_NECKLACE, MAFA_Q_PURPLE, 6, 1, MAFA_LINE_MAGE,     4, 0,14, 0, 41},
    {"灵魂项链", MAFA_ST_NECKLACE, MAFA_Q_PURPLE, 6, 1, MAFA_LINE_TAOIST,   4, 0, 0,14, 41},
    {"绿色项链", MAFA_ST_NECKLACE, MAFA_Q_GOLD,   6, 2, MAFA_LINE_WARRIOR, 15, 0, 0, 0, 44},
    {"恶魔铃铛", MAFA_ST_NECKLACE, MAFA_Q_GOLD,   6, 2, MAFA_LINE_MAGE,     5, 0,15, 0, 44},
    {"灵魂项链", MAFA_ST_NECKLACE, MAFA_Q_GOLD,   6, 2, MAFA_LINE_TAOIST,   5, 0, 0,15, 44},
    {"绿色项链", MAFA_ST_NECKLACE, MAFA_Q_GOLD,   6, 3, MAFA_LINE_WARRIOR, 17, 0, 0, 0, 48},
    {"恶魔铃铛", MAFA_ST_NECKLACE, MAFA_Q_GOLD,   6, 3, MAFA_LINE_MAGE,     5, 0,17, 0, 48},
    {"灵魂项链", MAFA_ST_NECKLACE, MAFA_Q_GOLD,   6, 3, MAFA_LINE_TAOIST,   5, 0, 0,17, 48},
    {"骑士手镯", MAFA_ST_BRACELET, MAFA_Q_PURPLE, 6, 1, MAFA_LINE_WARRIOR,  4, 7, 0, 0,  0},
    {"龙之手镯", MAFA_ST_BRACELET, MAFA_Q_PURPLE, 6, 1, MAFA_LINE_MAGE,     0, 7, 4, 0,  0},
    {"三眼手镯", MAFA_ST_BRACELET, MAFA_Q_PURPLE, 6, 1, MAFA_LINE_TAOIST,   0, 7, 0, 4,  0},
    {"骑士手镯", MAFA_ST_BRACELET, MAFA_Q_GOLD,   6, 2, MAFA_LINE_WARRIOR,  4, 8, 0, 0,  0},
    {"龙之手镯", MAFA_ST_BRACELET, MAFA_Q_GOLD,   6, 2, MAFA_LINE_MAGE,     0, 8, 4, 0,  0},
    {"三眼手镯", MAFA_ST_BRACELET, MAFA_Q_GOLD,   6, 2, MAFA_LINE_TAOIST,   0, 8, 0, 4,  0},
    {"骑士手镯", MAFA_ST_BRACELET, MAFA_Q_GOLD,   6, 3, MAFA_LINE_WARRIOR,  5, 9, 0, 0,  0},
    {"龙之手镯", MAFA_ST_BRACELET, MAFA_Q_GOLD,   6, 3, MAFA_LINE_MAGE,     0, 9, 5, 0,  0},
    {"三眼手镯", MAFA_ST_BRACELET, MAFA_Q_GOLD,   6, 3, MAFA_LINE_TAOIST,   0, 9, 0, 5,  0},
    {"力量戒指", MAFA_ST_RING,     MAFA_Q_PURPLE, 6, 1, MAFA_LINE_WARRIOR, 14, 0, 0, 0,  0},
    {"紫碧螺",   MAFA_ST_RING,     MAFA_Q_PURPLE, 6, 1, MAFA_LINE_MAGE,     0, 0,14, 0,  0},
    {"泰坦戒指", MAFA_ST_RING,     MAFA_Q_PURPLE, 6, 1, MAFA_LINE_TAOIST,   0, 0, 0,14,  0},
    {"力量戒指", MAFA_ST_RING,     MAFA_Q_GOLD,   6, 2, MAFA_LINE_WARRIOR, 15, 0, 0, 0,  0},
    {"紫碧螺",   MAFA_ST_RING,     MAFA_Q_GOLD,   6, 2, MAFA_LINE_MAGE,     0, 0,15, 0,  0},
    {"泰坦戒指", MAFA_ST_RING,     MAFA_Q_GOLD,   6, 2, MAFA_LINE_TAOIST,   0, 0, 0,15,  0},
    {"力量戒指", MAFA_ST_RING,     MAFA_Q_GOLD,   6, 3, MAFA_LINE_WARRIOR, 16, 0, 0, 0,  0},
    {"紫碧螺",   MAFA_ST_RING,     MAFA_Q_GOLD,   6, 3, MAFA_LINE_MAGE,     0, 0,16, 0,  0},
    {"泰坦戒指", MAFA_ST_RING,     MAFA_Q_GOLD,   6, 3, MAFA_LINE_TAOIST,   0, 0, 0,16,  0},
    /* map 7 赤月峡谷: purple / gold / gold — 赤月 sets; 屠龙 = the graduation
     * easter egg, 嗜魂法杖/逍遥扇 the 法/道 counterparts (逍遥扇 1.75) */
    {"屠龙",     MAFA_ST_WEAPON,   MAFA_Q_PURPLE, 7, 1, MAFA_LINE_WARRIOR, 23, 0, 0, 0,  0},
    {"嗜魂法杖", MAFA_ST_WEAPON,   MAFA_Q_PURPLE, 7, 1, MAFA_LINE_MAGE,    12, 0,23, 0,  0},
    {"逍遥扇",   MAFA_ST_WEAPON,   MAFA_Q_PURPLE, 7, 1, MAFA_LINE_TAOIST,  12, 0, 0,23,  0},
    {"屠龙",     MAFA_ST_WEAPON,   MAFA_Q_GOLD,   7, 2, MAFA_LINE_WARRIOR, 26, 0, 0, 0,  0},
    {"嗜魂法杖", MAFA_ST_WEAPON,   MAFA_Q_GOLD,   7, 2, MAFA_LINE_MAGE,    13, 0,26, 0,  0},
    {"逍遥扇",   MAFA_ST_WEAPON,   MAFA_Q_GOLD,   7, 2, MAFA_LINE_TAOIST,  13, 0, 0,26,  0},
    {"屠龙",     MAFA_ST_WEAPON,   MAFA_Q_GOLD,   7, 3, MAFA_LINE_WARRIOR, 30, 0, 0, 0,  0},
    {"嗜魂法杖", MAFA_ST_WEAPON,   MAFA_Q_GOLD,   7, 3, MAFA_LINE_MAGE,    15, 0,30, 0,  0},
    {"逍遥扇",   MAFA_ST_WEAPON,   MAFA_Q_GOLD,   7, 3, MAFA_LINE_TAOIST,  15, 0, 0,30,  0},
    {"天尊头盔", MAFA_ST_HELMET,   MAFA_Q_PURPLE, 7, 1, MAFA_LINE_TAOIST,   0, 8, 0, 2, 66},
    {"天尊头盔", MAFA_ST_HELMET,   MAFA_Q_GOLD,   7, 2, MAFA_LINE_TAOIST,   0, 9, 0, 3, 71},
    {"天尊头盔", MAFA_ST_HELMET,   MAFA_Q_GOLD,   7, 3, MAFA_LINE_TAOIST,   0,10, 0, 4, 76},
    {"圣战宝甲", MAFA_ST_ARMOR,    MAFA_Q_PURPLE, 7, 1, MAFA_LINE_WARRIOR,  3,16, 0, 0,125},
    {"霓裳羽衣", MAFA_ST_ARMOR,    MAFA_Q_PURPLE, 7, 1, MAFA_LINE_MAGE,     0,16, 3, 0,125},
    {"天师长袍", MAFA_ST_ARMOR,    MAFA_Q_PURPLE, 7, 1, MAFA_LINE_TAOIST,   0,16, 0, 3,125},
    {"圣战宝甲", MAFA_ST_ARMOR,    MAFA_Q_GOLD,   7, 2, MAFA_LINE_WARRIOR,  4,17, 0, 0,135},
    {"霓裳羽衣", MAFA_ST_ARMOR,    MAFA_Q_GOLD,   7, 2, MAFA_LINE_MAGE,     0,17, 4, 0,135},
    {"天师长袍", MAFA_ST_ARMOR,    MAFA_Q_GOLD,   7, 2, MAFA_LINE_TAOIST,   0,17, 0, 4,135},
    {"圣战宝甲", MAFA_ST_ARMOR,    MAFA_Q_GOLD,   7, 3, MAFA_LINE_WARRIOR,  4,19, 0, 0,145},
    {"霓裳羽衣", MAFA_ST_ARMOR,    MAFA_Q_GOLD,   7, 3, MAFA_LINE_MAGE,     0,19, 4, 0,145},
    {"天师长袍", MAFA_ST_ARMOR,    MAFA_Q_GOLD,   7, 3, MAFA_LINE_TAOIST,   0,19, 0, 4,145},
    {"圣战项链", MAFA_ST_NECKLACE, MAFA_Q_PURPLE, 7, 1, MAFA_LINE_WARRIOR, 16, 0, 0, 0, 47},
    {"法神项链", MAFA_ST_NECKLACE, MAFA_Q_PURPLE, 7, 1, MAFA_LINE_MAGE,     5, 0,16, 0, 47},
    {"天尊项链", MAFA_ST_NECKLACE, MAFA_Q_PURPLE, 7, 1, MAFA_LINE_TAOIST,   5, 0, 0,16, 47},
    {"圣战项链", MAFA_ST_NECKLACE, MAFA_Q_GOLD,   7, 2, MAFA_LINE_WARRIOR, 17, 0, 0, 0, 50},
    {"法神项链", MAFA_ST_NECKLACE, MAFA_Q_GOLD,   7, 2, MAFA_LINE_MAGE,     5, 0,17, 0, 50},
    {"天尊项链", MAFA_ST_NECKLACE, MAFA_Q_GOLD,   7, 2, MAFA_LINE_TAOIST,   5, 0, 0,17, 50},
    {"圣战项链", MAFA_ST_NECKLACE, MAFA_Q_GOLD,   7, 3, MAFA_LINE_WARRIOR, 19, 0, 0, 0, 54},
    {"法神项链", MAFA_ST_NECKLACE, MAFA_Q_GOLD,   7, 3, MAFA_LINE_MAGE,     6, 0,19, 0, 54},
    {"天尊项链", MAFA_ST_NECKLACE, MAFA_Q_GOLD,   7, 3, MAFA_LINE_TAOIST,   6, 0, 0,19, 54},
    {"圣战手镯", MAFA_ST_BRACELET, MAFA_Q_PURPLE, 7, 1, MAFA_LINE_WARRIOR,  4, 8, 0, 0,  0},
    {"法神手镯", MAFA_ST_BRACELET, MAFA_Q_PURPLE, 7, 1, MAFA_LINE_MAGE,     0, 8, 4, 0,  0},
    {"天尊手镯", MAFA_ST_BRACELET, MAFA_Q_PURPLE, 7, 1, MAFA_LINE_TAOIST,   0, 8, 0, 4,  0},
    {"圣战手镯", MAFA_ST_BRACELET, MAFA_Q_GOLD,   7, 2, MAFA_LINE_WARRIOR,  4, 9, 0, 0,  0},
    {"法神手镯", MAFA_ST_BRACELET, MAFA_Q_GOLD,   7, 2, MAFA_LINE_MAGE,     0, 9, 4, 0,  0},
    {"天尊手镯", MAFA_ST_BRACELET, MAFA_Q_GOLD,   7, 2, MAFA_LINE_TAOIST,   0, 9, 0, 4,  0},
    {"圣战手镯", MAFA_ST_BRACELET, MAFA_Q_GOLD,   7, 3, MAFA_LINE_WARRIOR,  5,10, 0, 0,  0},
    {"法神手镯", MAFA_ST_BRACELET, MAFA_Q_GOLD,   7, 3, MAFA_LINE_MAGE,     0,10, 5, 0,  0},
    {"天尊手镯", MAFA_ST_BRACELET, MAFA_Q_GOLD,   7, 3, MAFA_LINE_TAOIST,   0,10, 0, 5,  0},
    {"圣战戒指", MAFA_ST_RING,     MAFA_Q_PURPLE, 7, 1, MAFA_LINE_WARRIOR, 16, 0, 0, 0,  0},
    {"法神戒指", MAFA_ST_RING,     MAFA_Q_PURPLE, 7, 1, MAFA_LINE_MAGE,     0, 0,16, 0,  0},
    {"天尊戒指", MAFA_ST_RING,     MAFA_Q_PURPLE, 7, 1, MAFA_LINE_TAOIST,   0, 0, 0,16,  0},
    {"圣战戒指", MAFA_ST_RING,     MAFA_Q_GOLD,   7, 2, MAFA_LINE_WARRIOR, 17, 0, 0, 0,  0},
    {"法神戒指", MAFA_ST_RING,     MAFA_Q_GOLD,   7, 2, MAFA_LINE_MAGE,     0, 0,17, 0,  0},
    {"天尊戒指", MAFA_ST_RING,     MAFA_Q_GOLD,   7, 2, MAFA_LINE_TAOIST,   0, 0, 0,17,  0},
    {"圣战戒指", MAFA_ST_RING,     MAFA_Q_GOLD,   7, 3, MAFA_LINE_WARRIOR, 18, 0, 0, 0,  0},
    {"法神戒指", MAFA_ST_RING,     MAFA_Q_GOLD,   7, 3, MAFA_LINE_MAGE,     0, 0,18, 0,  0},
    {"天尊戒指", MAFA_ST_RING,     MAFA_Q_GOLD,   7, 3, MAFA_LINE_TAOIST,   0, 0, 0,18,  0},
};
const int MAFA_ITEM_COUNT = (int)(sizeof MAFA_ITEMS / sizeof MAFA_ITEMS[0]);

/* Item ids ride in single bytes (backpack, paper doll, save payload, event
 * payloads): the table must never outgrow the byte range. */
_Static_assert(sizeof MAFA_ITEMS / sizeof MAFA_ITEMS[0] <= 256,
               "item ids must fit uint8_t");

/* Seven forms per class at the original's real 1.76 learn levels (verified
 * 2026-09-30, community tables): the book gate lives in mafa_skill_known —
 * skill 0 is free, skills 1-3 are store books (level-gated), skills 4-6
 * drop from elites/bosses. mult is ×100 against the row's stat source
 * (战士 攻 / 法师 魔 / 道士 道, v1.5); the pet tier for MAFA_SK_PET and %
 * max HP for MAFA_SK_HEAL (the heal adds 2×道术). Warrior actives pay the
 * original's small MP costs (~烈火8/半月5/野蛮3, community values);
 * 逐日剑法 is the one post-1.76 skill (user-approved). */
const mafa_skill_t MAFA_SKILLS[MAFA_CLS_COUNT][MAFA_SKILLS_PER_CLASS] = {
    [MAFA_CLS_WARRIOR] = {
        {"基本剑术",  7, MAFA_SK_PASSIVE, MAFA_SK_STAT_ATK, 110, 0,  0, 0, 0,  0,  0, 0, 0},
        {"攻杀剑术", 19, MAFA_SK_PROC,    MAFA_SK_STAT_ATK, 200, 0, 20, 0, 0,  0,  0, 0, 0},
        {"刺杀剑术", 25, MAFA_SK_DMG,     MAFA_SK_STAT_ATK, 140, 1,  0, 0, 0,  0,  0, 5, 0},
        {"半月弯刀", 28, MAFA_SK_AOE,     MAFA_SK_STAT_ATK,  90, 0,  0, 0, 0,  0,  0, 6, 5},
        {"野蛮冲撞", 30, MAFA_SK_STUN,    MAFA_SK_STAT_ATK, 130, 0,  0, 1, 0,  0,  0, 8, 3},
        {"烈火剑法", 35, MAFA_SK_CHARGE,  MAFA_SK_STAT_ATK, 220, 0,  0, 0, 0,  0,  0, 8, 8},
        {"逐日剑法", 38, MAFA_SK_DMG,     MAFA_SK_STAT_ATK, 260, 1,  0, 0, 0,  0,  0, 10, 0},
    },
    [MAFA_CLS_MAGE] = {
        {"火球术",    7, MAFA_SK_DMG,     MAFA_SK_STAT_MC, 160, 0,  0, 0, 0,  0,  0, 0, 8},
        {"雷电术",   17, MAFA_SK_DMG,     MAFA_SK_STAT_MC, 220, 1,  0, 0, 0,  0,  0, 0, 14},
        {"爆裂火焰", 22, MAFA_SK_AOE,     MAFA_SK_STAT_MC, 120, 0,  0, 0, 0,  0,  0, 0, 20},
        {"火墙",     24, MAFA_SK_BURN,    MAFA_SK_STAT_MC,  60, 0,  0, 3, 0,  0,  0, 0, 18},
        {"地狱雷光", 30, MAFA_SK_AOE,     MAFA_SK_STAT_MC, 150, 1,  0, 0, 0,  0,  0, 0, 26},
        {"魔法盾",   31, MAFA_SK_SHIELD,  MAFA_SK_STAT_MC,   0, 0,  0, 4, 0,  0, 40, 0, 16},
        {"冰咆哮",   35, MAFA_SK_AOE,     MAFA_SK_STAT_MC, 160, 1,  0, 0, 0,  0,  0, 0, 30},
    },
    [MAFA_CLS_TAOIST] = {
        {"治愈术",    7, MAFA_SK_HEAL,    MAFA_SK_STAT_SC,  25, 0,  0, 0, 0,  0,  0, 0, 12},
        {"精神力战法", 9, MAFA_SK_PASSIVE, MAFA_SK_STAT_ATK, 115, 0,  0, 0, 0,  0,  0, 0, 0},
        {"施毒术",   14, MAFA_SK_POISON,  MAFA_SK_STAT_SC,   0, 0,  0, 5, 5, 30,  0, 0, 12},
        {"灵魂火符", 18, MAFA_SK_DMG,     MAFA_SK_STAT_SC, 240, 0,  0, 0, 0,  0,  0, 0, 14},
        {"召唤骷髅", 19, MAFA_SK_PET,     MAFA_SK_STAT_SC,   1, 0,  0, 0, 0,  0,  0, 6, 20},
        {"神圣战甲术", 25, MAFA_SK_ARMOR, MAFA_SK_STAT_SC,   0, 0,  0, 4, 0,  0, 40, 8, 18},
        {"召唤神兽", 35, MAFA_SK_PET,     MAFA_SK_STAT_SC,   2, 0,  0, 0, 0,  0,  0, 6, 30},
    },
};

/* Floor ladder (PRD 8.7, 1.76 plan): 比奇 2 / 兽人古墓 3 / 石墓 4 / 沃玛 3 /
 * 死亡山谷 4 / 祖玛 7 / 赤月 3 — one boss checkpoint per floor, the next
 * map opens through the last floor's boss. */
const uint8_t MAFA_MAP_FLOORS[MAFA_MAP_COUNT] = {0, 2, 3, 4, 3, 4, 7, 3};

/* Names verified against the original's mob lists (2026-09-29/30 research:
 * 比奇省 trash incl. 半兽人/钉耙猫 — 17173 白金典藏练级攻略; 半兽勇士/半兽
 * 统领 = 比奇省西部/野外 半兽系 — 17173 专文 + 腾讯官方怪物库; 骷髅系 =
 * 兽人古墓(骷髅洞), 骷髅精灵 在 3 层 — 新浪练级指南/百度经验; 白野猪/蝎蛇/
 * 楔蛾 = 石墓(猪洞) — 17173; 尸王 按用户批准映射落位石墓末层; 沃玛战士/
 * 勇士/卫士/教主 = 沃玛寺庙; 蜈蚣/黑色恶蛆/钳虫/邪恶钳虫 = 死亡山谷
 * (蜈蚣洞); 祖玛 教主之下三强 = 雕像/弓箭手/卫士 — 17173 大锤怪专文;
 * 月魔蜘蛛/天狼蜘蛛/双头金刚/双头血魔/赤月恶魔 = 赤月峡谷). */
const mafa_monster_t MAFA_MONSTERS[] = {
    /* -- map 1 比奇省, 2 floors ------------------------------------------ */
    {"鸡",       1, 1,  1,   50,  7,  0,   12, MAFA_MSK_NONE,    false},
    {"鹿",       1, 1,  1,   65,  8,  1,   15, MAFA_MSK_NONE,    false},
    {"稻草人",   1, 1,  2,   95, 10,  1,   18, MAFA_MSK_FIRE,    false},
    {"多钩猫",   1, 1,  3,  130, 12,  2,   22, MAFA_MSK_FLURRY,  false},
    {"钉耙猫",   1, 2,  3,  140, 13,  2,   26, MAFA_MSK_FLURRY,  false},
    {"半兽人",   1, 2,  4,  200, 15,  3,   30, MAFA_MSK_HEAVY,   false},
    {"半兽勇士", 1, 1,  4,  450, 21,  3,  120, MAFA_MSK_HEAVY,   true},
    {"半兽统领", 1, 2,  5,  520, 26,  4,  150, MAFA_MSK_ROAR,    true},
    /* -- map 2 兽人古墓, 3 floors ---------------------------------------- */
    {"骷髅",     2, 1,  7,  160, 16,  5,   85, MAFA_MSK_NONE,    false},
    {"洞蛆",     2, 1,  8,  155, 17,  4,   95, MAFA_MSK_STING,   false},
    {"骷髅战士", 2, 2,  9,  190, 19,  6,  110, MAFA_MSK_HEAVY,   false},
    {"掷斧骷髅", 2, 2, 10,  175, 20,  5,  125, MAFA_MSK_FIRE,    false},
    {"骷髅战士", 2, 1,  8,  730, 31,  6,  280, MAFA_MSK_HEAVY,   true},
    {"掷斧骷髅", 2, 2, 10,  830, 34,  7,  340, MAFA_MSK_FIRE,    true},
    {"骷髅精灵", 2, 3, 12, 1100, 40,  8,  420, MAFA_MSK_FLURRY,  true},
    /* -- map 3 石墓, 4 floors -------------------------------------------- */
    {"红野猪",   3, 1, 14,  195, 26,  9,  280, MAFA_MSK_HEAVY,   false},
    {"黑野猪",   3, 1, 15,  210, 28, 10,  320, MAFA_MSK_HEAVY,   false},
    {"蝎蛇",     3, 2, 16,  230, 29, 11,  360, MAFA_MSK_STING,   false},
    {"楔蛾",     3, 2, 17,  215, 30,  9,  390, MAFA_MSK_FIRE,    false},
    {"僵尸",     3, 1, 14,  940, 50, 10,  520, MAFA_MSK_NONE,    true},
    {"白野猪",   3, 2, 16, 1100, 44, 11,  650, MAFA_MSK_ROAR,    true},
    {"蝎蛇",     3, 3, 17, 1200, 50, 12,  720, MAFA_MSK_STING,   true},
    {"尸王",     3, 4, 18, 1365, 51, 12,  850, MAFA_MSK_ROAR,    true},
    /* -- map 4 沃玛寺庙, 3 floors ---------------------------------------- */
    {"沃玛战士", 4, 1, 22,  325, 38, 14,  700, MAFA_MSK_HEAVY,   false},
    {"沃玛勇士", 4, 1, 23,  345, 40, 13,  750, MAFA_MSK_FLURRY,  false},
    {"沃玛卫士", 4, 2, 24,  390, 42, 16,  820, MAFA_MSK_HEAVY,   false},
    {"沃玛战士", 4, 1, 22, 1300, 60, 15, 1400, MAFA_MSK_HEAVY,   true},
    {"沃玛卫士", 4, 2, 24, 1430, 62, 16, 1600, MAFA_MSK_HEAVY,   true},
    {"沃玛教主", 4, 3, 26, 1755, 68, 18, 2200, MAFA_MSK_HELLFIRE, true},
    /* -- map 5 死亡山谷, 4 floors ---------------------------------------- */
    {"蜈蚣",     5, 1, 28,  360, 50, 18, 1500, MAFA_MSK_FLURRY,  false},
    {"黑色恶蛆", 5, 1, 29,  380, 52, 19, 1600, MAFA_MSK_STING,   false},
    {"钳虫",     5, 2, 30,  410, 54, 20, 1700, MAFA_MSK_HEAVY,   false},
    {"蜈蚣",     5, 1, 28, 1560, 84, 20, 3000, MAFA_MSK_FLURRY,  true},
    {"钳虫",     5, 2, 30, 1665, 78, 21, 3300, MAFA_MSK_HEAVY,   true},
    {"黑色恶蛆", 5, 3, 31, 1755, 90, 22, 3600, MAFA_MSK_STING,   true},
    {"邪恶钳虫", 5, 4, 33, 2015, 86, 24, 4400, MAFA_MSK_ROAR,    true},
    /* -- map 6 祖玛寺庙, 7 floors ---------------------------------------- */
    {"祖玛卫士",  6, 1, 34,  440, 54, 24, 3300, MAFA_MSK_HEAVY,  false},
    {"祖玛弓箭手",6, 2, 35,  425, 55, 22, 3400, MAFA_MSK_STING,  false},
    {"祖玛雕像",  6, 3, 36,  495, 58, 26, 3600, MAFA_MSK_HEAVY,  false},
    {"祖玛卫士",  6, 1, 34, 1650, 90, 25, 5200, MAFA_MSK_HEAVY,  true},
    {"祖玛弓箭手",6, 2, 35, 1700, 100, 26, 5600, MAFA_MSK_STING,  true},
    {"祖玛卫士",  6, 3, 36, 1750, 87, 27, 6000, MAFA_MSK_HEAVY,  true},
    {"祖玛雕像",  6, 4, 36, 1700, 90, 28, 6500, MAFA_MSK_HEAVY,  true},
    {"祖玛卫士",  6, 5, 37, 1900, 99, 29, 7000, MAFA_MSK_ROAR,   true},
    {"祖玛雕像",  6, 6, 37, 1850, 94, 30, 7500, MAFA_MSK_HEAVY,  true},
    {"祖玛教主",  6, 7, 38, 2150, 99, 32, 9000, MAFA_MSK_HELLFIRE, true},
    /* -- map 7 赤月峡谷, 3 floors --------------------------------------- */
    {"月魔蜘蛛", 7, 1, 38,  430, 71, 30, 6200, MAFA_MSK_STING,   false},
    {"天狼蜘蛛", 7, 1, 39,  455, 73, 31, 6500, MAFA_MSK_FLURRY,  false},
    {"双头金刚", 7, 2, 39,  515, 71, 33, 7000, MAFA_MSK_HEAVY,   false},
    {"双头血魔", 7, 2, 40,  530, 73, 32, 7200, MAFA_MSK_HEAVY,   false},
    {"天狼蜘蛛", 7, 1, 39, 1750, 116, 32, 9000, MAFA_MSK_FLURRY,  true},
    {"双头金刚", 7, 2, 40, 1900, 108, 34, 10000, MAFA_MSK_HEAVY,  true},
    {"赤月恶魔", 7, 3, 40, 2200, 115, 36, 14000, MAFA_MSK_HELLFIRE, true},
};
const int MAFA_MONSTER_COUNT = (int)(sizeof MAFA_MONSTERS / sizeof MAFA_MONSTERS[0]);

const char *const MAFA_MAP_NAMES[MAFA_MAP_COUNT] = {
    "安全区", "比奇省", "兽人古墓", "石墓",
    "沃玛寺庙", "死亡山谷", "祖玛寺庙", "赤月峡谷",
};

const mafa_monster_t *mafa_map_boss(uint8_t map, uint8_t floor) {
    for (int i = 0; i < MAFA_MONSTER_COUNT; ++i)
        if (MAFA_MONSTERS[i].map == map && MAFA_MONSTERS[i].floor == floor
            && MAFA_MONSTERS[i].boss)
            return &MAFA_MONSTERS[i];
    return NULL;
}

static const uint32_t MAFA_SELL_PRICE[MAFA_Q_COUNT] = {10, 30, 80, 200, 500};

/* Front-fast, back-wall curve (1.76 plan): the early game keeps the
 * original's quick newbie pace (L1-7 near-linear), costs compound ~×1.3
 * through the mid-game, and the 39→40 wall alone is ~46 % of total
 * time-to-max (12,000,000 of ~26,300,000 XP) — the famous 经验墙. */
static const uint32_t MAFA_XP_NEXT[MAFA_MAX_LEVEL - 1] = {
    100, 180, 320, 520, 800, 1200, 1800,
    2700, 3900, 5500, 7800, 10800, 15000, 20000, 27000, 35000, 45000,
    58000, 73000, 92000,
    115000, 142000, 175000, 213000, 258000, 310000, 370000, 440000,
    520000, 610000,
    730000, 860000, 1010000, 1180000, 1380000, 1600000, 1850000, 2130000,
    12000000,
};

/* Store books (skills 1-3 per class, three price steps); books 4-6 drop. */
static const uint32_t MAFA_BOOK_PRICE[3] = {300, 600, 900};

/* --- RNG: splitmix32 (single randomness source) --------------------------- */

static uint32_t rng_next(mafa_player_t *p) {
    p->rng += 0x9E3779B9u;
    uint32_t z = p->rng;
    z = (z ^ (z >> 16)) * 0x21F0AAADu;
    z = (z ^ (z >> 15)) * 0x735A2D97u;
    return z ^ (z >> 15);
}

/* --- Books ------------------------------------------------------------------ */

uint32_t mafa_book_price(uint8_t cls, uint8_t skill_idx) {
    (void)cls;
    if (skill_idx < 1 || skill_idx > 3) return 0;    /* only these are sold */
    return MAFA_BOOK_PRICE[skill_idx - 1];
}

bool mafa_skill_known(const mafa_player_t *p, uint8_t skill_idx) {
    if (skill_idx >= MAFA_SKILLS_PER_CLASS) return false;
    if (p->level < MAFA_SKILLS[p->cls][skill_idx].unlock) return false;
    if (skill_idx == 0) return true;
    return (p->books >> (p->cls * MAFA_SKILLS_PER_CLASS + skill_idx)) & 1;
}

/* Grant every book the level already passed (save migration / new game). */
static void grant_level_books(mafa_player_t *p) {
    for (int i = 1; i < MAFA_SKILLS_PER_CLASS; ++i)
        if (p->level >= MAFA_SKILLS[p->cls][i].unlock)
            p->books |= (1u << (p->cls * MAFA_SKILLS_PER_CLASS + i));
}

/* --- Player --------------------------------------------------------------- */

void mafa_player_init(mafa_player_t *p, uint8_t cls, uint32_t seed) {
    memset(p, 0, sizeof *p);
    p->cls = cls;
    p->level = 1;
    p->gold = 0;
    p->map = MAFA_MAP_SAFE + 1;         /* new games idle at once: 比奇省 */
    p->floor = 1;
    p->unlocked = MAFA_MAP_SAFE + 1;
    for (int i = 0; i < MAFA_MAP_COUNT - 1; ++i) p->floor_unlocked[i] = 1;
    p->auto_potion = true;
    p->auto_sell = 0x01;                /* white only (PRD 8.3, v1.2) */
    p->pot_hp_pct = MAFA_POT_HP_PCT_DEFAULT;
    p->pot_mp_pct = MAFA_POT_MP_PCT_DEFAULT;
    p->pending_drop = MAFA_DROP_NONE;
    for (int i = 0; i < MAFA_EQ_SLOTS; ++i) p->equipped[i] = MAFA_INV_EMPTY;
    for (int i = 0; i < MAFA_BACKPACK; ++i) p->inv_id[i] = MAFA_INV_EMPTY;
    p->rng = seed ? seed : 1;
    mafa_stats_t st;
    mafa_stats(p, &st);
    p->hp = (int16_t)st.max_hp;
    p->mp = st.max_mp;
}

void mafa_stats(const mafa_player_t *p, mafa_stats_t *out) {
    int lv = p->level - 1;
    mafa_stats_t st = {0, 0, 0, 0, 0, 0};
    switch (p->cls) {
    case MAFA_CLS_WARRIOR:
        st.max_hp = 60 + 8 * lv;
        st.max_mp = 10 + lv;            /* v1.5: the small warrior pool */
        st.atk = 11 + 2 * lv;
        st.def = 5 + lv;
        break;
    case MAFA_CLS_MAGE:
        st.max_hp = 40 + 6 * lv;
        st.atk = 10 + lv;               /* 平砍走武器; spells scale 魔 */
        st.mc = 14 + 2 * lv;
        st.def = 3 + (lv + 1) / 2;      /* +1 every 2 levels (L2, L4, …) */
        st.max_mp = 30 + 5 * lv;
        break;
    default: /* taoist */
        st.max_hp = 60 + 6 * lv;
        st.atk = 10 + lv;
        st.sc = 12 + lv;                /* 道 drives 火符/毒/治愈/神兽 */
        st.def = 4 + lv;
        st.max_mp = 25 + 4 * lv;
        break;
    }
    for (int i = 0; i < MAFA_EQ_SLOTS; ++i) {
        if (p->equipped[i] == MAFA_INV_EMPTY) continue;
        const mafa_item_t *it = &MAFA_ITEMS[p->equipped[i]];
        st.atk += it->atk;
        st.def += it->def;
        st.mc += it->mc;
        st.sc += it->sc;
        st.max_hp += it->hp;
    }
    *out = st;
}

uint32_t mafa_xp_to_next(uint8_t level) {
    if (level < 1) return MAFA_XP_NEXT[0];
    if (level >= MAFA_MAX_LEVEL) return 0;
    return MAFA_XP_NEXT[level - 1];
}

void mafa_regen(mafa_player_t *p, uint8_t seconds) {
    mafa_stats_t st;
    mafa_stats(p, &st);
    p->hp += (int16_t)(10 * seconds);
    if (p->hp > st.max_hp) p->hp = (int16_t)st.max_hp;
    if (st.max_mp > 0) {
        p->mp += (int16_t)(10 * seconds);
        if (p->mp > st.max_mp) p->mp = st.max_mp;
    }
}

/* --- Inventory ------------------------------------------------------------- */

bool mafa_inv_add(mafa_player_t *p, uint8_t item_id) {
    for (int i = 0; i < MAFA_BACKPACK; ++i)
        if (p->inv_id[i] == item_id) {
            if (p->inv_n[i] == 0xFF) return false;   /* stack cap */
            p->inv_n[i]++;
            return true;
        }
    for (int i = 0; i < MAFA_BACKPACK; ++i)
        if (p->inv_id[i] == MAFA_INV_EMPTY) {
            p->inv_id[i] = item_id;
            p->inv_n[i] = 1;
            return true;
        }
    return false;
}

static void inv_take(mafa_player_t *p, uint8_t idx) {
    if (--p->inv_n[idx] == 0) p->inv_id[idx] = MAFA_INV_EMPTY;
}

static void inv_remove_all(mafa_player_t *p, uint8_t idx) {
    p->inv_id[idx] = MAFA_INV_EMPTY;
    p->inv_n[idx] = 0;
}

/* Equip position routing: the free twin of a bracelet/ring takes the item
 * first; only when both twins are worn does the first twin get replaced. */
static int equip_position(const mafa_player_t *p, uint8_t type) {
    int first = -1;
    for (int i = 0; i < MAFA_EQ_SLOTS; ++i) {
        if (MAFA_POS_TYPE[i] != type) continue;
        if (first < 0) first = i;
        if (p->equipped[i] == MAFA_INV_EMPTY) return i;
    }
    return first;
}

bool mafa_equip(mafa_player_t *p, uint8_t idx) {
    if (idx >= MAFA_BACKPACK || p->inv_id[idx] == MAFA_INV_EMPTY) return false;
    uint8_t id = p->inv_id[idx];
    int pos = equip_position(p, MAFA_ITEMS[id].slot);
    if (pos < 0) return false;
    uint8_t slot = (uint8_t)pos;
    uint8_t old = p->equipped[slot];

    /* Take the new item first; remember whether its slot freed up. */
    uint8_t taken_from = idx;
    bool took_all = p->inv_n[idx] == 1;
    inv_take(p, idx);

    if (old != MAFA_INV_EMPTY) {
        /* The freed slot (if any) or any stack/empty slot must absorb it. */
        bool placed = false;
        if (took_all && p->inv_id[taken_from] == MAFA_INV_EMPTY) {
            p->inv_id[taken_from] = old;
            p->inv_n[taken_from] = 1;
            placed = true;
        } else {
            for (int i = 0; i < MAFA_BACKPACK && !placed; ++i)
                if (p->inv_id[i] == old && p->inv_n[i] < 0xFF) {
                    p->inv_n[i]++;
                    placed = true;
                }
            for (int i = 0; i < MAFA_BACKPACK && !placed; ++i)
                if (p->inv_id[i] == MAFA_INV_EMPTY) {
                    p->inv_id[i] = old;
                    p->inv_n[i] = 1;
                    placed = true;
                }
            if (!placed) {           /* rollback: put the new item back */
                if (p->inv_id[taken_from] == id) p->inv_n[taken_from]++;
                else if (p->inv_id[taken_from] == MAFA_INV_EMPTY) {
                    p->inv_id[taken_from] = id;
                    p->inv_n[taken_from] = 1;
                } else {
                    /* No rollback slot: restore count inside the old stack. */
                    for (int i = 0; i < MAFA_BACKPACK; ++i)
                        if (p->inv_id[i] == id) { p->inv_n[i]++; break; }
                }
                return false;
            }
        }
    }
    p->equipped[slot] = id;
    return true;
}

void mafa_compare(const mafa_player_t *p, uint8_t item_id, mafa_compare_t *out) {
    out->item = &MAFA_ITEMS[item_id];
    mafa_stats_t cur;
    mafa_stats(p, &cur);
    mafa_stats_t next = cur;
    /* Compare against the position this item would land in: a twin bracelet
     * compares vs the free twin when one is open (else the first twin). */
    int pos = equip_position(p, out->item->slot);
    uint8_t old = pos >= 0 ? p->equipped[pos] : MAFA_INV_EMPTY;
    if (old != MAFA_INV_EMPTY) {
        const mafa_item_t *o = &MAFA_ITEMS[old];
        next.atk -= o->atk;
        next.def -= o->def;
        next.mc -= o->mc;
        next.sc -= o->sc;
        next.max_hp -= o->hp;
    }
    next.atk += out->item->atk;
    next.def += out->item->def;
    next.mc += out->item->mc;
    next.sc += out->item->sc;
    next.max_hp += out->item->hp;
    out->d_atk = next.atk - cur.atk;
    out->d_def = next.def - cur.def;
    out->d_mc = next.mc - cur.mc;
    out->d_sc = next.sc - cur.sc;
    out->d_hp = next.max_hp - cur.max_hp;
}

uint32_t mafa_sell_price(uint8_t item_id) {
    return MAFA_SELL_PRICE[MAFA_ITEMS[item_id].quality];
}

uint32_t mafa_sell(mafa_player_t *p, uint8_t idx) {
    if (idx >= MAFA_BACKPACK || p->inv_id[idx] == MAFA_INV_EMPTY) return 0;
    uint32_t gold = mafa_sell_price(p->inv_id[idx]);
    inv_remove_all(p, idx);
    p->gold += gold;
    if (p->gold > MAFA_GOLD_CAP) p->gold = MAFA_GOLD_CAP;
    return gold;
}

uint32_t mafa_sell_all_white(mafa_player_t *p) {
    uint32_t total = 0;
    for (int i = 0; i < MAFA_BACKPACK; ++i)
        if (p->inv_id[i] != MAFA_INV_EMPTY
            && MAFA_ITEMS[p->inv_id[i]].quality == MAFA_Q_WHITE)
            total += mafa_sell(p, (uint8_t)i);
    return total;
}

bool mafa_buy_potion(mafa_player_t *p, bool red) {
    uint32_t price = red ? 50 : 40;
    if (p->gold < price) return false;
    p->gold -= price;
    if (red) p->pot_red++;
    else p->pot_blue++;
    return true;
}

bool mafa_buy_book(mafa_player_t *p, uint8_t skill_idx) {
    if (skill_idx < 1 || skill_idx > 3) return false;   /* 4-6 drop in battle */
    /* 1.76 plan gate: the store refuses to sell below the learn level. */
    if (p->level < MAFA_SKILLS[p->cls][skill_idx].unlock) return false;
    uint32_t bit = 1u << (p->cls * MAFA_SKILLS_PER_CLASS + skill_idx);
    if (p->books & bit) return false;               /* already learned */
    uint32_t price = mafa_book_price(p->cls, skill_idx);
    if (p->gold < price) return false;
    p->gold -= price;
    p->books |= bit;
    return true;
}

void mafa_switch_map(mafa_player_t *p, uint8_t map, uint8_t floor) {
    if (map >= MAFA_MAP_COUNT) return;
    if (map != MAFA_MAP_SAFE && map > p->unlocked) return;
    if (map == MAFA_MAP_SAFE) {
        p->map = MAFA_MAP_SAFE;
        p->floor = 0;
        p->kills = 0;
        /* Entering town restores full HP/MP (1.76 plan): death respawn and
         * a voluntary walk home heal the same — the safe zone is a real
         * rest spot, not just a death hub. */
        mafa_stats_t st;
        mafa_stats(p, &st);
        p->hp = (int16_t)st.max_hp;
        if (st.max_mp > 0) p->mp = st.max_mp;
        return;
    }
    if (floor < 1 || floor > p->floor_unlocked[map - 1]) return;
    p->map = map;
    p->floor = floor;
    p->kills = 0;
}

/* --- Monster spawn ---------------------------------------------------------- */

static int16_t scale5(int32_t v, int diff) {
    return (int16_t)((v * (100 + 5 * diff)) / 100);
}

static void spawn_mob(mafa_player_t *p, const mafa_monster_t *base,
                      bool elite, mafa_mob_t *m) {
    memset(m, 0, sizeof *m);
    int diff = (int)p->level - (int)base->level;
    if (diff < 0) diff = 0;
    m->base = base;
    m->max_hp = scale5(base->hp, diff);
    if (elite) m->max_hp = m->max_hp * 3 / 2;
    m->hp = m->max_hp;
    m->atk = scale5(base->atk, diff);
    if (elite) m->atk = m->atk * 6 / 5;
    m->def = scale5(base->def, diff);
    m->elite = elite;
    m->alive = true;
}

/* Non-boss pack size shifts toward 3 mobs on deeper maps (skills-2.0 B):
 * more bodies per fight lengthens battles into the 5-15 s band and gives
 * AoE skills their identity back. Maps 3+ share the deep-dungeon weights. */
static uint8_t roll_pack(mafa_player_t *p, uint8_t map) {
    uint32_t r = rng_next(p) % 10;
    if (map == 1) return r < 7 ? 1 : 2;
    if (map == 2) return r < 4 ? 1 : (r < 8 ? 2 : 3);
    return r < 2 ? 1 : (r < 6 ? 2 : 3);
}

bool mafa_battle_start(mafa_player_t *p, mafa_battle_t *b) {
    if (p->pending_drop != MAFA_DROP_NONE) return false;
    memset(b, 0, sizeof *b);
    int pool[MAFA_MONSTER_COUNT], n = 0;
    /* Cumulative floor pool (v1.3): a floor spawns its own trash plus every
     * shallower row — deeper floors get busier, very much the original's
     * dungeon feel. The level band keeps newbie maps from spawning for a
     * max-level player; fall back to the whole (floor-filtered) map when
     * the band is empty (over-leveled player). */
    for (int i = 0; i < MAFA_MONSTER_COUNT; ++i)
        if (MAFA_MONSTERS[i].map == p->map && !MAFA_MONSTERS[i].boss
            && MAFA_MONSTERS[i].floor <= p->floor
            && MAFA_MONSTERS[i].level <= p->level + 2)
            pool[n++] = i;
    if (n == 0)
        for (int i = 0; i < MAFA_MONSTER_COUNT; ++i)
            if (MAFA_MONSTERS[i].map == p->map && !MAFA_MONSTERS[i].boss
                && MAFA_MONSTERS[i].floor <= p->floor)
                pool[n++] = i;
    if (n == 0) return false;
    bool elite = rng_next(p) % 10 == 0;         /* ~1/10 battles (PRD 8.6) */
    b->mob_n = elite ? 1 : roll_pack(p, p->map);
    if (b->mob_n > MAFA_MOBS_MAX) b->mob_n = MAFA_MOBS_MAX;
    for (int i = 0; i < (int)b->mob_n; ++i) {
        const mafa_monster_t *base = &MAFA_MONSTERS[pool[rng_next(p) % (uint32_t)n]];
        spawn_mob(p, base, elite, &b->mob[i]);
        b->alive_n++;
    }
    return true;
}

bool mafa_boss_ready(const mafa_player_t *p) {
    return p->kills >= MAFA_KILLS_PER_BOSS;
}

bool mafa_boss_start(mafa_player_t *p, mafa_battle_t *b) {
    if (!mafa_boss_ready(p) || p->pending_drop != MAFA_DROP_NONE) return false;
    const mafa_monster_t *boss = mafa_map_boss(p->map, p->floor);
    if (!boss) return false;
    memset(b, 0, sizeof *b);
    spawn_mob(p, boss, false, &b->mob[0]);
    b->mob_n = 1;
    b->alive_n = 1;
    b->is_boss = true;
    return true;
}

void mafa_boss_pass(mafa_player_t *p) {
    p->kills = 0;
}

/* --- Settlement -------------------------------------------------------------- */

static void add_gold(mafa_player_t *p, uint32_t g) {
    p->gold += g;
    if (p->gold > MAFA_GOLD_CAP) p->gold = MAFA_GOLD_CAP;
}

static void grant_levelups(mafa_player_t *p, mafa_events_t *ev) {
    while (p->level < MAFA_MAX_LEVEL && p->xp >= mafa_xp_to_next(p->level)) {
        p->xp -= mafa_xp_to_next(p->level);
        mafa_stats_t before, after;
        mafa_stats(p, &before);
        p->level++;
        mafa_stats(p, &after);
        p->hp += (int16_t)(after.max_hp - before.max_hp);
        p->mp += (int16_t)(after.max_mp - before.max_mp);
        mafa_ev_push(ev, MAFA_EV_LEVELUP, p->level, 0, 0);
        /* A book-learned skill lands in the same beat as its level-up. */
        for (int i = 1; i < MAFA_SKILLS_PER_CLASS; ++i)
            if (MAFA_SKILLS[p->cls][i].unlock == p->level
                && mafa_skill_known(p, (uint8_t)i)) {
                mafa_ev_push(ev, MAFA_EV_SKILL_SUPPORT,
                             (uint8_t)(0x80 | i), 0, 0);
                break;
            }
    }
}

/* Book drops (1.76 plan): elites sometimes drop a missing late book; a boss
 * kill guarantees the next missing one (skill 4 first, then 5, then 6). */
static void roll_book_drop(mafa_player_t *p, bool boss, bool elite,
                           mafa_events_t *ev) {
    if (!boss && !elite) return;
    if (!boss && rng_next(p) % 100 >= 20) return;
    for (int i = 4; i < MAFA_SKILLS_PER_CLASS; ++i) {
        uint32_t bit = 1u << (p->cls * MAFA_SKILLS_PER_CLASS + i);
        if (!(p->books & bit)) {
            p->books |= bit;
            mafa_ev_push(ev, MAFA_EV_BOOK, (uint8_t)i, 1, 0);
            return;
        }
    }
}

/* Drop roll (PRD 8.6, v1.5): gate 10 % for normal mobs; gold-tier gear
 * comes from bosses only (8 %) — elites and trash stop at the map's top
 * non-gold tier so 屠龙 stays a chase. Maps 5-7 carry gold rows; trash on
 * those maps caps at tier 2. Class-line rows (weapon triads, the 沃玛/
 * 祖玛/赤月 jewelry triads): the player's own line wins ~60 % of rolls so
 * farming your class's gear stays the norm, off-line pieces still drop. */
static void roll_gear_drop(mafa_player_t *p, const mafa_battle_t *b,
                           const mafa_mob_t *m, mafa_events_t *ev) {
    uint32_t drop_roll = rng_next(p) % 100;
    if (!(b->is_boss || m->elite || drop_roll < 10)) return;
    uint8_t tier;
    if (b->is_boss) tier = rng_next(p) % 100 < 8 ? 3 : 2;
    else if (m->elite) tier = rng_next(p) % 100 < 30 ? 3 : 2;
        else {
            uint32_t tr = rng_next(p) % 100;
            if (tr < 70) tier = 1;
            else if (tr < 96) tier = 2;
            else tier = m->base->map >= 5 ? 2 : 3;    /* trash never drops gold */
        }
    uint8_t stype = (uint8_t)(rng_next(p) % MAFA_SLOT_TYPES);
    uint8_t matches[3];
    int nm = 0;
    for (int i = 0; i < MAFA_ITEM_COUNT && nm < 3; ++i)
        if (MAFA_ITEMS[i].map == m->base->map && MAFA_ITEMS[i].tier == tier
            && MAFA_ITEMS[i].slot == stype)
            matches[nm++] = (uint8_t)i;
    if (nm == 0) return;
    uint8_t choice = matches[0];
    if (nm > 1) {
        uint8_t others[2];
        int no = 0;
        uint8_t own = 0xFF;
        for (int k = 0; k < nm; ++k) {
            if (MAFA_ITEMS[matches[k]].line == p->cls) own = matches[k];
            else others[no++] = matches[k];
        }
        if (own != 0xFF && rng_next(p) % 100 < 60) choice = own;
        else if (no > 0) choice = others[rng_next(p) % (uint32_t)no];
        else choice = own;
    }
    uint8_t q = MAFA_ITEMS[choice].quality;
    /* The auto-sell quality set (v1.2); gold never auto-sells —
     * legendary drops always reach the player. */
    if (q != MAFA_Q_GOLD && (p->auto_sell >> q) & 1) {
        uint32_t g = MAFA_SELL_PRICE[q];
        add_gold(p, g);
        mafa_ev_push(ev, MAFA_EV_DROP, choice, 2, 0);
    } else if (mafa_inv_add(p, choice)) {
        mafa_ev_push(ev, MAFA_EV_DROP, choice, 1, 0);
    } else if (p->pending_drop == MAFA_DROP_NONE) {
        p->pending_drop = choice;
        mafa_ev_push(ev, MAFA_EV_DROP, choice, 3, 0);
    }
}

/* Potion drops (v1.5): the original's trash supplies 金创药/魔法药. A ~15 %
 * roll per kill, weighted toward the red bottle; potions go straight into
 * the stack counters (never the backpack) and cap at 99. */
static void roll_potion_drop(mafa_player_t *p, mafa_events_t *ev) {
    if (rng_next(p) % 100 >= MAFA_POTION_DROP_PCT) return;
    bool red = rng_next(p) % 100 < 60;
    uint8_t *pot = red ? &p->pot_red : &p->pot_blue;
    if (*pot >= 99) return;
    (*pot)++;
    mafa_ev_push(ev, MAFA_EV_POTION, red ? 1 : 2, 1, 0);
}

static void settle_kill(mafa_player_t *p, mafa_battle_t *b, uint8_t mob_idx,
                        mafa_events_t *ev) {
    const mafa_mob_t *m = &b->mob[mob_idx];
    uint32_t xp = m->base->xp;
    if (m->elite) xp = xp * 3 / 2;
    p->xp += xp;

    uint32_t gold = (uint32_t)m->base->level * (2 + rng_next(p) % 4);
    if (m->elite) gold = gold * 3 / 2;
    add_gold(p, gold);
    mafa_ev_push(ev, MAFA_EV_MOB_KILLED, 0, (int32_t)xp, mob_idx);

    grant_levelups(p, ev);
    roll_book_drop(p, b->is_boss, m->elite, ev);
    roll_gear_drop(p, b, m, ev);
    roll_potion_drop(p, ev);

    if (b->is_boss) {
        p->kills = 0;
        uint8_t last_floor = MAFA_MAP_FLOORS[p->map];
        if (p->floor < last_floor) {
            /* The floor boss falls: the next floor opens and the idle flow
             * walks straight into it (v1.3). */
            if (p->floor_unlocked[p->map - 1] < p->floor + 1)
                p->floor_unlocked[p->map - 1] = (uint8_t)(p->floor + 1);
            p->floor++;
            mafa_ev_push(ev, MAFA_EV_FLOOR, p->floor, 0, 0);
        } else if (p->map + 1 < MAFA_MAP_COUNT && p->unlocked < p->map + 1) {
            /* Last floor: unlock the next COMBAT map; the safe zone (id 0)
             * is open from the start and is not a progression step. */
            p->unlocked = (uint8_t)(p->map + 1);
            mafa_ev_push(ev, MAFA_EV_MAP_UNLOCK, p->unlocked, 0, 0);
        }
    } else {
        p->kills++;
    }
}

/* Mark a mob dead and settle it exactly once. */
static void kill_mob(mafa_player_t *p, mafa_battle_t *b, uint8_t mob_idx,
                     mafa_events_t *ev) {
    if (!b->mob[mob_idx].alive) return;
    b->mob[mob_idx].hp = 0;
    b->mob[mob_idx].alive = false;
    b->alive_n--;
    settle_kill(p, b, mob_idx, ev);
    if (b->alive_n == 0) b->over = true;
}

/* --- Combat (PRD 8.3, 1.76 plan) ------------------------------------------------ */

void mafa_ev_push(mafa_events_t *ev, uint8_t kind, uint8_t id, int32_t a,
                  int32_t b) {
    if (ev->n >= MAFA_EV_MAX) return;   /* bounded: overflow drops newest */
    ev->e[ev->n].kind = kind;
    ev->e[ev->n].id = id;
    ev->e[ev->n].a = a;
    ev->e[ev->n].b = b;
    ev->n++;
}

static uint8_t first_alive(const mafa_battle_t *b) {
    for (uint8_t i = 0; i < b->mob_n; ++i)
        if (b->mob[i].alive) return i;
    return 0;
}

/* Monster defense as seen by the attacker accounts for 施毒术's debuff. */
static int32_t mob_effective_def(const mafa_mob_t *m) {
    return m->def * (m->def_down_rounds > 0 ? 7 : 10) / 10;
}

/* The stat a skill row scales with (v1.5: the 攻击/魔法/道术 lines). */
static int32_t skill_src(const mafa_skill_t *s, const mafa_stats_t *st) {
    return s->stat == MAFA_SK_STAT_MC ? st->mc
         : s->stat == MAFA_SK_STAT_SC ? st->sc
         : st->atk;
}

static int32_t roll_damage(mafa_player_t *p, int32_t atk, int32_t def,
                           uint16_t mult, bool ignore_def, bool *crit) {
    *crit = false;
    int32_t v = atk * (mult ? mult : 100) / 100;
    v = v * (int32_t)(9 + rng_next(p) % 3) / 10;    /* U(0.9, 1.1) */
    if (rng_next(p) % 100 < 10) {
        *crit = true;
        v = v * 3 / 2;
    }
    if (!ignore_def) {
        v -= def;
        if (v < 1) v = 1;
    }
    return v;
}

/* Flat potion heals (PRD 8.5), scaled mildly by level: one potion always
 * answers roughly one mob hit, early and late. */
#define MAFA_RED_HEAL(p_lvl) (30 + 2 * (p_lvl))
#define MAFA_BLUE_MP(p_lvl) (15 + (p_lvl))

static bool try_potion(mafa_player_t *p, mafa_events_t *ev) {
    if (!p->auto_potion) return false;
    mafa_stats_t st;
    mafa_stats(p, &st);
    if (p->hp * 100 < st.max_hp * p->pot_hp_pct && p->pot_red > 0) {
        p->pot_red--;
        int32_t heal = MAFA_RED_HEAL(p->level);
        p->hp += (int16_t)heal;
        if (p->hp > st.max_hp) p->hp = (int16_t)st.max_hp;
        mafa_ev_push(ev, MAFA_EV_HEAL, 1, heal, 0);
        return true;
    }
    if (st.max_mp > 0 && p->mp * 100 < st.max_mp * p->pot_mp_pct
        && p->pot_blue > 0) {
        p->pot_blue--;
        int32_t fill = MAFA_BLUE_MP(p->level);
        p->mp += (int16_t)fill;
        if (p->mp > st.max_mp) p->mp = st.max_mp;
        mafa_ev_push(ev, MAFA_EV_HEAL, 2, fill, 0);
        return true;
    }
    return false;
}

/* Pet stats grow with the player's level AND 道术 (v1.5): the original's
 * 召唤兽 scales with the summoner's power. */
static void summon_pet(mafa_player_t *p, mafa_battle_t *b, uint8_t tier) {
    int lv = p->level;
    mafa_stats_t st;
    mafa_stats(p, &st);
    b->pet_tier = tier;
    b->pet_alive = true;
    if (tier == 2) {
        b->pet_max_hp = 45 + 12 * lv + st.sc * 3 / 2;
        b->pet_atk = 6 + 3 * lv / 2 + st.sc / 2;
        b->pet_def = 3 + lv / 2 + st.sc / 3;
    } else {
        b->pet_max_hp = 30 + 9 * lv + st.sc;
        b->pet_atk = 4 + lv + st.sc / 3;
        b->pet_def = 2 + lv / 2;
    }
    b->pet_hp = b->pet_max_hp;
}

static const mafa_skill_t *pick_skill(mafa_player_t *p, mafa_battle_t *b,
                                      uint8_t *idx) {
    const mafa_skill_t *sk = MAFA_SKILLS[p->cls];
    mafa_stats_t st;
    mafa_stats(p, &st);
    /* Per-class priority over castable skills; PASSIVE/PROC never cast.
     * Gates keep each form honest: AoE only into a crowd, shield/armor when
     * hurt and none up, pet while down, heal when hurt, poison once, and
     * the taoist talisman only with mana to spare. Skills switched off on
     * the skill page (skills_off, v1.2) are skipped here. */
    static const uint8_t ORDER[MAFA_CLS_COUNT][MAFA_SKILLS_PER_CLASS] = {
        {5, 6, 4, 3, 2, 0xFF, 0xFF},  /* warrior: 烈火 逐日 野蛮 半月 刺杀 */
        {5, 6, 4, 3, 2, 1, 0},       /* mage: 盾 冰咆 雷光 火墙 爆裂 雷电 火球 */
        {0, 6, 4, 2, 5, 3, 0xFF},    /* taoist: 治愈 神兽 骷髅 毒 战甲 火符 */
    };
    uint8_t target = first_alive(b);
    for (int o = 0; o < MAFA_SKILLS_PER_CLASS; ++o) {
        uint8_t i = ORDER[p->cls][o];
        if (i == 0xFF) break;
        const mafa_skill_t *s = &sk[i];
        if (s->kind == MAFA_SK_PASSIVE || s->kind == MAFA_SK_PROC) continue;
        if (!mafa_skill_known(p, i) || b->cd[i] > 0) continue;
        if (p->skills_off & (1u << i)) continue;
        if (s->mp > 0 && p->mp < s->mp) continue;
        switch (s->kind) {
        case MAFA_SK_AOE:
            if (b->alive_n < 2) continue;             /* crowd-only */
            break;
        case MAFA_SK_BURN:
            /* 火墙 works on a lone boss too — only skip while it burns. */
            if (b->mob[target].burn_rounds > 0) continue;
            break;
        case MAFA_SK_HEAL:
            if (p->hp * 10 >= st.max_hp * 6) continue;
            break;
        case MAFA_SK_SHIELD:
            if (b->shield_rounds > 0 || p->hp * 10 >= st.max_hp * 7) continue;
            break;
        case MAFA_SK_ARMOR:
            if (b->armor_rounds > 0 || p->hp * 10 >= st.max_hp * 7) continue;
            break;
        case MAFA_SK_PET:
            if (b->pet_alive || b->pet_cd > 0) continue;
            break;
        case MAFA_SK_POISON:
            if (b->mob[target].poison_rounds > 0) continue;
            break;
        case MAFA_SK_CHARGE:
            if (b->charge_mult > 0) continue;
            break;
        case MAFA_SK_DMG:
            if (p->cls == MAFA_CLS_TAOIST
                && p->mp * 10 < st.max_mp * 3) continue;   /* keep pet mana */
            break;
        default:
            break;
        }
        *idx = i;
        return s;
    }
    return NULL;
}

static void player_turn(mafa_player_t *p, mafa_battle_t *b, mafa_events_t *ev) {
    if (try_potion(p, ev)) return;
    uint8_t idx = 0;
    const mafa_skill_t *s = pick_skill(p, b, &idx);
    if (s) {
        if (s->mp > 0) p->mp -= s->mp;
        if (s->cd > 0) b->cd[idx] = s->cd;
        mafa_stats_t st;
        mafa_stats(p, &st);
        switch (s->kind) {
        case MAFA_SK_DMG: {
            int32_t mdef = mob_effective_def(&b->mob[first_alive(b)]);
            bool crit;
            int32_t dmg = roll_damage(p, skill_src(s, &st), mdef, s->mult,
                                      s->ignore_def != 0, &crit);
            b->mob[first_alive(b)].hp -= dmg;
            mafa_ev_push(ev, crit ? MAFA_EV_PLAYER_CRIT : MAFA_EV_SKILL_HIT,
                         idx, dmg, first_alive(b));
            break;
        }
        case MAFA_SK_STUN: {
            uint8_t t = first_alive(b);
            int32_t mdef = mob_effective_def(&b->mob[t]);
            bool crit;
            int32_t dmg = roll_damage(p, skill_src(s, &st), mdef, s->mult,
                                      false, &crit);
            b->mob[t].hp -= dmg;
            b->mob[t].stun_rounds = s->rounds;
            mafa_ev_push(ev, crit ? MAFA_EV_PLAYER_CRIT : MAFA_EV_SKILL_HIT,
                         idx, dmg, t);
            mafa_ev_push(ev, MAFA_EV_MOB_STUNNED, t, 0, 0);
            break;
        }
        case MAFA_SK_AOE: {
            for (uint8_t i = 0; i < b->mob_n; ++i) {
                if (!b->mob[i].alive) continue;
                bool crit;
                int32_t dmg = roll_damage(p, skill_src(s, &st),
                                          mob_effective_def(&b->mob[i]),
                                          s->mult, s->ignore_def != 0, &crit);
                b->mob[i].hp -= dmg;
                mafa_ev_push(ev, crit ? MAFA_EV_PLAYER_CRIT : MAFA_EV_SKILL_HIT,
                             idx, dmg, i);
            }
            break;
        }
        case MAFA_SK_BURN:
            for (uint8_t i = 0; i < b->mob_n; ++i)
                if (b->mob[i].alive) {
                    b->mob[i].burn_rounds = s->rounds;
                    b->mob[i].burn_dmg =
                        (int16_t)(skill_src(s, &st) * s->mult / 100);
                }
            mafa_ev_push(ev, MAFA_EV_SKILL_SUPPORT, idx, 0, 0);
            break;
        case MAFA_SK_POISON: {
            uint8_t t = first_alive(b);
            b->mob[t].poison_rounds = s->rounds;
            b->mob[t].poison_dmg = (uint8_t)(s->flat + st.sc / 5);
            b->mob[t].def_down_rounds = s->rounds;
            mafa_ev_push(ev, MAFA_EV_SKILL_SUPPORT, idx, 0, 0);
            break;
        }
        case MAFA_SK_HEAL: {
            int32_t heal = st.max_hp * s->mult / 100 + st.sc;
            p->hp += (int16_t)heal;
            if (p->hp > st.max_hp) p->hp = (int16_t)st.max_hp;
            mafa_ev_push(ev, MAFA_EV_HEAL, 0, heal, 0);
            break;
        }
        case MAFA_SK_SHIELD:
            b->shield_rounds = s->rounds;
            mafa_ev_push(ev, MAFA_EV_SKILL_SUPPORT, idx, 0, 0);
            break;
        case MAFA_SK_ARMOR:
            b->armor_rounds = s->rounds;
            mafa_ev_push(ev, MAFA_EV_SKILL_SUPPORT, idx, 0, 0);
            break;
        case MAFA_SK_CHARGE:
            b->charge_mult = s->mult;
            mafa_ev_push(ev, MAFA_EV_SKILL_SUPPORT, idx, 0, 0);
            break;
        case MAFA_SK_PET:
            summon_pet(p, b, (uint8_t)s->mult);
            mafa_ev_push(ev, MAFA_EV_PET_SUMMON, (uint8_t)s->mult, 0, 0);
            break;
        default:
            break;
        }
        return;
    }

    /* Normal attack: the passive and any charge arm the swing, then 攻杀
     * (proc) may double it — crit only rolls on a plain swing. */
    mafa_stats_t st;
    mafa_stats(p, &st);
    uint16_t mult = 100;
    int charge_idx = -1, proc_idx = -1;
    for (int i = 0; i < MAFA_SKILLS_PER_CLASS; ++i) {
        if (!mafa_skill_known(p, (uint8_t)i)) continue;
        const mafa_skill_t *sk = &MAFA_SKILLS[p->cls][i];
        if (sk->kind == MAFA_SK_PASSIVE) {
            mult = sk->mult;
        } else if (sk->kind == MAFA_SK_PROC) {
            proc_idx = i;
        } else if (sk->kind == MAFA_SK_CHARGE) {
            charge_idx = i;
        }
    }
    bool charged = b->charge_mult > 0;
    if (charged) mult = b->charge_mult;
    uint8_t t = first_alive(b);
    bool crit;
    int32_t dmg = roll_damage(p, st.atk, mob_effective_def(&b->mob[t]), mult,
                              false, &crit);
    b->mob[t].hp -= dmg;
    if (charged) {
        b->charge_mult = 0;
        mafa_ev_push(ev, MAFA_EV_SKILL_HIT, (uint8_t)charge_idx, dmg, t);
    } else if (proc_idx >= 0
               && rng_next(p) % 100 < MAFA_SKILLS[p->cls][proc_idx].proc_pct) {
        int32_t extra = dmg;                        /* 攻杀: the second blade */
        b->mob[t].hp -= extra;
        mafa_ev_push(ev, MAFA_EV_SKILL_HIT, (uint8_t)proc_idx, dmg + extra, t);
    } else {
        mafa_ev_push(ev, crit ? MAFA_EV_PLAYER_CRIT : MAFA_EV_PLAYER_HIT, 0,
                     dmg, t);
    }
}

static void pet_turn(mafa_player_t *p, mafa_battle_t *b, mafa_events_t *ev) {
    if (!b->pet_alive || b->over) return;
    uint8_t t = first_alive(b);
    int32_t dmg = b->pet_atk * (int32_t)(9 + rng_next(p) % 3) / 10;
    dmg -= mob_effective_def(&b->mob[t]);
    if (dmg < 1) dmg = 1;
    b->mob[t].hp -= dmg;
    mafa_ev_push(ev, MAFA_EV_PET_HIT, 0, dmg, t);
}

static void mob_turn(mafa_player_t *p, mafa_battle_t *b, mafa_events_t *ev) {
    mafa_stats_t st;
    mafa_stats(p, &st);
    int32_t pdef = st.def;
    if (b->is_boss) pdef /= 2;      /* bosses pierce 50 % of defense (PRD 9.2) */
    /* 魔法盾 reduction and 神圣战甲术 defense bonus, resolved once per round
     * from the known skills. */
    uint8_t shield_pct = 0;
    if (b->shield_rounds > 0)
        for (int k = 0; k < MAFA_SKILLS_PER_CLASS; ++k)
            if (mafa_skill_known(p, (uint8_t)k)
                && MAFA_SKILLS[p->cls][k].kind == MAFA_SK_SHIELD)
                shield_pct = MAFA_SKILLS[p->cls][k].shield_pct;
    if (b->armor_rounds > 0)
        for (int k = 0; k < MAFA_SKILLS_PER_CLASS; ++k)
            if (mafa_skill_known(p, (uint8_t)k)
                && MAFA_SKILLS[p->cls][k].kind == MAFA_SK_ARMOR)
                pdef = pdef * (100 + MAFA_SKILLS[p->cls][k].shield_pct) / 100;
    /* The pet is re-checked per hit: it can fall mid-round and the next
     * monster swings at the player instead. */

    for (uint8_t i = 0; i < b->mob_n && !b->over; ++i) {
        if (!b->mob[i].alive) continue;
        if (b->mob[i].stun_rounds > 0) {        /* 野蛮冲撞: turn skipped */
            b->mob[i].stun_rounds--;
            continue;
        }
        const mafa_monster_t *m = b->mob[i].base;
        bool low = b->mob[i].hp * 10 < b->mob[i].max_hp * 3;
        bool use_skill = m->skill != MAFA_MSK_NONE && low
                         && rng_next(p) % 100 < 50;
        int32_t hits[2] = {0, 0};
        uint8_t n_hits = 1;
        uint8_t kind = MAFA_MSK_NONE;

        if (use_skill) {
            kind = m->skill;
            switch (m->skill) {
            case MAFA_MSK_FIRE:    hits[0] = b->mob[i].atk * 15 / 10; break;
            case MAFA_MSK_FLURRY:  hits[0] = b->mob[i].atk * 7 / 10;
                                   hits[1] = b->mob[i].atk * 7 / 10;
                                   n_hits = 2; break;
            case MAFA_MSK_HEAVY:   hits[0] = b->mob[i].atk * 16 / 10; break;
            case MAFA_MSK_STING:   hits[0] = b->mob[i].atk * 12 / 10;
                                   if (!b->pet_alive) {   /* poison needs flesh */
                                       b->player_poison_rounds = 2;
                                       b->player_poison_dmg = 3;
                                   }
                                   break;
            case MAFA_MSK_ROAR:    hits[0] = b->mob[i].atk * 15 / 10; break;
            case MAFA_MSK_HELLFIRE: hits[0] = b->mob[i].atk * 20 / 10; break;
            default: use_skill = false; break;
            }
        }
        if (!use_skill) hits[0] = b->mob[i].atk;

        for (uint8_t h = 0; h < n_hits && !b->over; ++h) {
            int32_t tdef = b->pet_alive ? b->pet_def : pdef;
            int32_t dmg = (hits[h] * (int32_t)(8 + rng_next(p) % 5) / 10)
                          - tdef;
            if (dmg < 1) dmg = 1;
            if (shield_pct > 0) {
                dmg = dmg * (100 - shield_pct) / 100;
                if (dmg < 1) dmg = 1;
            }
            if (b->pet_alive) {
                b->pet_hp -= dmg;
                mafa_ev_push(ev, MAFA_EV_PET_GUARD, kind, dmg, i);
                if (b->pet_hp <= 0) {
                    b->pet_alive = false;
                    b->pet_cd = 6;
                    mafa_ev_push(ev, MAFA_EV_PET_DOWN, 0, 0, 0);
                }
            } else {
                p->hp -= (int16_t)dmg;
                mafa_ev_push(ev, use_skill ? MAFA_EV_MOB_SKILL : MAFA_EV_MOB_HIT,
                             kind, dmg, i);
                if (p->hp <= 0) {
                    p->hp = 0;
                    b->over = true;
                    b->player_dead = true;
                    mafa_ev_push(ev, MAFA_EV_PLAYER_DEATH, 0, 0, 0);
                }
            }
        }
    }
}

/* White-name PvE death (single-player 传奇, PRD 8.9): lose 1-2 random
 * backpack stacks and 10-20 % of carried gold; equipped gear is safe.
 * The boss counter resets, then the player respawns in the safe zone at
 * full HP/MP and picks the next map themselves. Full HP is mandatory: the
 * next battle must never start at 0 HP or the player dies again every
 * round, shedding the penalty each time. */
static void settle_player_death(mafa_player_t *p, mafa_events_t *ev) {
    uint8_t occupied[MAFA_BACKPACK], n_occ = 0;
    for (int i = 0; i < MAFA_BACKPACK; ++i)
        if (p->inv_id[i] != MAFA_INV_EMPTY) occupied[n_occ++] = (uint8_t)i;
    uint8_t n_lose = 1 + (uint8_t)(rng_next(p) % 2);
    if (n_lose > n_occ) n_lose = n_occ;
    for (uint8_t k = 0; k < n_lose && n_occ > 0; ++k) {
        uint8_t pick = rng_next(p) % n_occ;
        uint8_t slot = occupied[pick];
        occupied[pick] = occupied[--n_occ];
        uint8_t id = p->inv_id[slot];
        inv_remove_all(p, slot);
        mafa_ev_push(ev, MAFA_EV_DEATH_DROP, id, 1, slot);
    }
    if (p->gold > 0) {
        uint32_t lost = p->gold * (10 + rng_next(p) % 11) / 100;
        if (lost == 0) lost = 1;
        p->gold -= lost;
        mafa_ev_push(ev, MAFA_EV_GOLD_LOST, 0, (int32_t)lost, 0);
    }
    p->kills = 0;
    p->map = MAFA_MAP_SAFE;             /* respawn in town (v0.9) */
    p->floor = 0;                       /* floors survive death (v1.3) */
    mafa_stats_t st;
    mafa_stats(p, &st);
    p->hp = st.max_hp;
    if (st.max_mp > 0) p->mp = st.max_mp;
}

void mafa_battle_round(mafa_player_t *p, mafa_battle_t *b, mafa_events_t *ev) {
    ev->n = 0;
    if (b->over) return;
    b->rounds++;                        /* count every player action */

    /* Player poison tick (cave scorpion line) — ignores defense. */
    if (b->player_poison_rounds > 0) {
        b->player_poison_rounds--;
        p->hp -= (int16_t)b->player_poison_dmg;
        mafa_ev_push(ev, MAFA_EV_PLAYER_POISON, 0, b->player_poison_dmg, 0);
        if (p->hp <= 0) {
            p->hp = 0;
            b->over = true;
            b->player_dead = true;
            mafa_ev_push(ev, MAFA_EV_PLAYER_DEATH, 0, 0, 0);
            settle_player_death(p, ev);
            return;
        }
    }

    player_turn(p, b, ev);
    for (uint8_t i = 0; i < b->mob_n && !b->over; ++i)
        if (b->mob[i].alive && b->mob[i].hp <= 0)
            kill_mob(p, b, i, ev);

    if (b->over) return;

    pet_turn(p, b, ev);
    for (uint8_t i = 0; i < b->mob_n && !b->over; ++i)
        if (b->mob[i].alive && b->mob[i].hp <= 0)
            kill_mob(p, b, i, ev);

    if (b->over) return;

    mob_turn(p, b, ev);
    if (b->over) {                      /* player died inside mob_turn */
        settle_player_death(p, ev);
        return;
    }

    /* End of round: DoTs on the monsters, cooldowns, shield/armor/pet
     * timers, mana regen. */
    for (uint8_t i = 0; i < b->mob_n && !b->over; ++i) {
        if (!b->mob[i].alive) continue;
        if (b->mob[i].burn_rounds > 0) {
            b->mob[i].burn_rounds--;
            b->mob[i].hp -= b->mob[i].burn_dmg;
            mafa_ev_push(ev, MAFA_EV_DOT_TICK, 0, b->mob[i].burn_dmg, i);
        }
        if (b->mob[i].poison_rounds > 0) {
            b->mob[i].poison_rounds--;
            b->mob[i].hp -= b->mob[i].poison_dmg;
            mafa_ev_push(ev, MAFA_EV_DOT_TICK, 0, b->mob[i].poison_dmg, i);
        }
        if (b->mob[i].def_down_rounds > 0) b->mob[i].def_down_rounds--;
        if (b->mob[i].hp <= 0) kill_mob(p, b, i, ev);
    }
    if (b->over) return;

    if (b->shield_rounds > 0) b->shield_rounds--;
    if (b->armor_rounds > 0) b->armor_rounds--;
    if (b->pet_cd > 0) b->pet_cd--;
    for (int i = 0; i < MAFA_SKILLS_PER_CLASS; ++i)
        if (b->cd[i] > 0) b->cd[i]--;
    mafa_stats_t st;
    mafa_stats(p, &st);
    if (st.max_mp > 0) {
        p->mp += 2;
        if (p->mp > st.max_mp) p->mp = st.max_mp;
    }
}

/* --- Full-backpack prompt ------------------------------------------------------ */

void mafa_drop_replace(mafa_player_t *p, uint8_t inv_idx) {
    if (p->pending_drop == MAFA_DROP_NONE) return;
    uint8_t id = p->pending_drop;
    p->pending_drop = MAFA_DROP_NONE;
    uint8_t old = p->inv_id[inv_idx];
    p->inv_id[inv_idx] = id;
    p->inv_n[inv_idx] = 1;
    if (old != MAFA_INV_EMPTY) {
        /* A three-key swap must not lose the replaced item: auto-sell it. */
        add_gold(p, mafa_sell_price(old));
    }
}

void mafa_drop_discard(mafa_player_t *p) {
    p->pending_drop = MAFA_DROP_NONE;
}

/* --- Save (PRD 8.10, v6) ---------------------------------------------------------- */

static uint8_t crc8(const uint8_t *d, size_t n) {
    uint8_t c = 0;
    for (size_t i = 0; i < n; ++i) {
        c ^= d[i];
        for (int k = 8; k > 0; --k)
            c = (uint8_t)((c & 0x80) ? (c << 1) ^ 0x07 : c << 1);
    }
    return c;
}

/* v7 payload (v1.5 stats 2.0): byte layout identical to v6 (57 bytes) —
 * the version byte only marks the item-table generation. v6 saves carry
 * ids from the old 105-row table: they load, then the migration replaces
 * the loadout with a band/line-appropriate starter kit. */
#define MAFA_SAVE_BODY_V7 57
#define MAFA_SAVE_BODY_V6 MAFA_SAVE_BODY_V7
/* v5 payload (v1.3): v4 body + 3 per-map floor_unlocked bytes = 46. The old
 * v4 spare byte comes back as the first floor byte. */
#define MAFA_SAVE_BODY_V5 46
/* v4 payload (v1.2): v2 body + skills_off + pot_hp_pct + pot_mp_pct +
 * auto_sell = 44 bytes (even, the old spare byte put to work). */
#define MAFA_SAVE_BODY_V4 44
/* v2/v3 payload: cls level xp32 gold hp mp books pots kills flags drop eq
 * inv spare = 40 bytes (even, room to grow). v3 only renumbers the map
 * fields; the byte layout is unchanged. */
#define MAFA_SAVE_BODY_V2 40
/* v1 payload (pre skills-2.0): xp16, no books = 36 bytes. */
#define MAFA_SAVE_BODY_V1 36

size_t mafa_save_serialize(const mafa_player_t *p, uint8_t *buf, size_t cap) {
    const size_t total = 4 + MAFA_SAVE_BODY_V7 + 1;
    if (cap < total) return 0;
    buf[0] = 'M'; buf[1] = 'F'; buf[2] = 'C'; buf[3] = MAFA_SAVE_VERSION;
    uint8_t *w = buf + 4;
    *w++ = p->cls;
    *w++ = p->level;
    *w++ = (uint8_t)(p->xp & 0xFF); *w++ = (uint8_t)((p->xp >> 8) & 0xFF);
    *w++ = (uint8_t)((p->xp >> 16) & 0xFF); *w++ = (uint8_t)(p->xp >> 24);
    *w++ = (uint8_t)(p->gold & 0xFF); *w++ = (uint8_t)(p->gold >> 8);
    *w++ = (uint8_t)((uint16_t)p->hp & 0xFF); *w++ = (uint8_t)((uint16_t)p->hp >> 8);
    *w++ = (uint8_t)((uint16_t)p->mp & 0xFF); *w++ = (uint8_t)((uint16_t)p->mp >> 8);
    *w++ = (uint8_t)(p->books & 0xFF); *w++ = (uint8_t)((p->books >> 8) & 0xFF);
    *w++ = (uint8_t)((p->books >> 16) & 0xFF); *w++ = (uint8_t)(p->books >> 24);
    *w++ = p->pot_red; *w++ = p->pot_blue;
    *w++ = p->kills & 0xFF; *w++ = p->kills >> 8;
    *w++ = (uint8_t)(p->map | (p->unlocked << 3)
                     | (p->auto_potion ? 0x40 : 0)
                     | (p->auto_boss ? 0x80 : 0));
    *w++ = p->pending_drop;
    for (int i = 0; i < MAFA_EQ_SLOTS; ++i) *w++ = p->equipped[i];
    for (int i = 0; i < MAFA_BACKPACK; ++i) { *w++ = p->inv_id[i]; *w++ = p->inv_n[i]; }
    *w++ = p->skills_off & 0x7F;
    *w++ = p->pot_hp_pct;
    *w++ = p->pot_mp_pct;
    *w++ = p->auto_sell & 0x0F;
    for (int i = 0; i < MAFA_MAP_COUNT - 1; ++i) *w++ = p->floor_unlocked[i];
    size_t body = (size_t)(w - (buf + 4));
    if (body != MAFA_SAVE_BODY_V7) return 0;
    buf[4 + body] = crc8(buf + 4, body);
    return total;
}

/* The map the level places the player in — used by the v<7 starter kit. */
static uint8_t band_map(uint8_t level) {
    if (level <= 6) return 1;
    if (level <= 13) return 2;
    if (level <= 21) return 3;
    if (level <= 27) return 4;
    if (level <= 33) return 5;
    return 6;
}

/* v<7 saves carry item ids from an older table — the table changed
 * wholesale, so the migration replaces the loadout with tier-2 pieces of
 * the player's level band and OWN class line (weapon/helmet/armor
 * equipped, bag empty; twins stay grindable). */
static void grant_migration_kit(mafa_player_t *t) {
    for (int i = 0; i < MAFA_EQ_SLOTS; ++i) t->equipped[i] = MAFA_INV_EMPTY;
    for (int i = 0; i < MAFA_BACKPACK; ++i) {
        t->inv_id[i] = MAFA_INV_EMPTY;
        t->inv_n[i] = 0;
    }
    t->pending_drop = MAFA_DROP_NONE;
    uint8_t m = band_map(t->level);
    for (int i = 0; i < MAFA_ITEM_COUNT; ++i)
        if (MAFA_ITEMS[i].map == m && MAFA_ITEMS[i].tier == 2
            && (MAFA_ITEMS[i].line == MAFA_LINE_NEUTRAL
                || MAFA_ITEMS[i].line == t->cls)) {
            int pos = equip_position(t, MAFA_ITEMS[i].slot);
            if (pos >= 0 && pos < MAFA_ST_BRACELET)   /* singles only */
                t->equipped[pos] = (uint8_t)i;
        }
}

/* Version-aware payload reader: v1 (36 B) has xp16 and no books; v2/v3
 * (40 B) have xp32 + books16; v4 (44 B) adds the skills_off / threshold /
 * auto-sell tail; v5 (46 B) replaces v4's spare byte with three floor
 * bytes; v6 (57 B) widens books to 32 bits, grows the paper doll to 8
 * positions and the floor array to 7 maps. Everything after mp shifts
 * accordingly. */
static bool load_payload(mafa_player_t *t, const uint8_t *r, size_t body,
                         uint8_t version) {
    t->cls = r[0];
    t->level = r[1];
    if (t->cls >= MAFA_CLS_COUNT || t->level < 1 || t->level > MAFA_MAX_LEVEL)
        return false;
    r += 2;
    if (body >= MAFA_SAVE_BODY_V2) {
        t->xp = (uint32_t)r[0] | ((uint32_t)r[1] << 8)
                | ((uint32_t)r[2] << 16) | ((uint32_t)r[3] << 24);
        r += 4;
    } else {
        t->xp = (uint16_t)(r[0] | (r[1] << 8));
        r += 2;
    }
    t->gold = (uint16_t)(r[0] | (r[1] << 8)); r += 2;
    t->hp = (int16_t)(r[0] | (r[1] << 8)); r += 2;
    t->mp = (int16_t)(r[0] | (r[1] << 8)); r += 2;
    if (body == MAFA_SAVE_BODY_V6) {
        t->books = (uint32_t)r[0] | ((uint32_t)r[1] << 8)
                   | ((uint32_t)r[2] << 16) | ((uint32_t)r[3] << 24);
        r += 4;
    } else if (body >= MAFA_SAVE_BODY_V2) {
        /* v<6 book bits index the old 5-skill tables — dropped; the
         * migration re-grants by the NEW unlock levels. */
        r += 2;
    }
    t->pot_red = *r++; t->pot_blue = *r++;
    t->kills = (uint16_t)(r[0] | (r[1] << 8)); r += 2;
    uint8_t flags = *r++;
    if (body == MAFA_SAVE_BODY_V6) {
        t->map = flags & 7;
        t->unlocked = (flags >> 3) & 7;
    } else {
        t->map = flags & 3;
        t->unlocked = (flags >> 2) & 3;
    }
    if (t->map >= MAFA_MAP_COUNT) return false;
    if (version >= 3) {
        /* v3+: 0 = safe zone, combat maps 1..; unlocked must leave the
         * player somewhere to fight. */
        if (t->unlocked < 1) return false;
    } else {
        /* v1/v2 → v3 map migration: the safe zone moved from id 3 to 0
         * and combat maps 0-2 shifted to 1-3. */
        if (t->unlocked >= 3) return false;     /* old domain topped at 2 */
        t->map = t->map == 3 ? MAFA_MAP_SAFE : (uint8_t)(t->map + 1);
        t->unlocked = (uint8_t)(t->unlocked + 1);
    }
    if (body == MAFA_SAVE_BODY_V6) {
        t->auto_potion = (flags & 0x40) != 0;
        t->auto_boss = (flags & 0x80) != 0;
    } else {
        t->auto_potion = (flags & 0x10) != 0;
        t->auto_boss = (flags & 0x40) != 0;
    }
    /* v1-v3 defaults; the v4 tail below overwrites them. The old white-only
     * auto-sell flag maps onto the quality mask's white bit. */
    t->auto_sell = (flags & 0x20) ? 0x01 : 0x00;
    t->pot_hp_pct = MAFA_POT_HP_PCT_DEFAULT;
    t->pot_mp_pct = MAFA_POT_MP_PCT_DEFAULT;
    t->skills_off = 0;
    t->pending_drop = *r++;
    if (t->pending_drop != MAFA_DROP_NONE
        && t->pending_drop >= MAFA_ITEM_COUNT) return false;
    if (body == MAFA_SAVE_BODY_V6) {
        for (int i = 0; i < MAFA_EQ_SLOTS; ++i) {
            t->equipped[i] = *r++;
            /* v6 ids belong to the old item table: accept them here and
             * let the v7 migration replace the loadout wholesale. */
            if (version >= MAFA_SAVE_VERSION
                && t->equipped[i] != MAFA_INV_EMPTY
                && (t->equipped[i] >= MAFA_ITEM_COUNT
                    || MAFA_ITEMS[t->equipped[i]].slot != MAFA_POS_TYPE[i]))
                return false;
        }
    } else {
        /* v<6: old 3-slot ids — validated against nothing, wiped by the
         * migration kit. Skip the 3 bytes. */
        r += 3;
    }
    for (int i = 0; i < MAFA_BACKPACK; ++i) {
        if (body == MAFA_SAVE_BODY_V6) {
            t->inv_id[i] = *r++; t->inv_n[i] = *r++;
            if (version >= MAFA_SAVE_VERSION
                && t->inv_id[i] != MAFA_INV_EMPTY
                && (t->inv_id[i] >= MAFA_ITEM_COUNT || t->inv_n[i] == 0))
                return false;
            if (t->inv_id[i] == MAFA_INV_EMPTY) t->inv_n[i] = 0;
        } else {
            r += 2;    /* old ids are meaningless in the new table */
        }
    }
    if (body >= MAFA_SAVE_BODY_V4) {
        t->skills_off = (uint8_t)(*r++ & 0x7F);
        t->pot_hp_pct = *r++;
        t->pot_mp_pct = *r++;
        if (t->pot_hp_pct < MAFA_POT_PCT_MIN || t->pot_hp_pct > MAFA_POT_PCT_MAX)
            return false;
        if (t->pot_mp_pct < MAFA_POT_PCT_MIN || t->pot_mp_pct > MAFA_POT_PCT_MAX)
            return false;
        t->auto_sell = (uint8_t)(*r++ & 0x0F);
    }
    if (body == MAFA_SAVE_BODY_V6) {
        for (int i = 0; i < MAFA_MAP_COUNT - 1; ++i) {
            t->floor_unlocked[i] = *r++;
            if (t->floor_unlocked[i] < 1
                || t->floor_unlocked[i] > MAFA_MAP_FLOORS[i + 1]) return false;
        }
        /* A deeper map opens only through the previous map's last floor. */
        for (int i = 1; i < MAFA_MAP_COUNT - 1; ++i)
            if (t->unlocked >= i + 1
                && t->floor_unlocked[i - 1] != MAFA_MAP_FLOORS[i]) return false;
        t->floor = t->map == MAFA_MAP_SAFE
                       ? 0 : t->floor_unlocked[t->map - 1];
    } else if (body == MAFA_SAVE_BODY_V5) {
        /* v5's three floor bytes belong to the OLD ladders (祖玛 had 7
         * floors; it is 石墓's band now) — read then clamp to the new
         * ladders; the remap below extends to maps 4-7. */
        for (int i = 0; i < 3; ++i) {
            uint8_t f = *r++;
            if (f < 1) f = 1;
            if (f > MAFA_MAP_FLOORS[i + 1]) f = MAFA_MAP_FLOORS[i + 1];
            t->floor_unlocked[i] = f;
        }
    }
    return true;
}

bool mafa_save_deserialize(mafa_player_t *p, const uint8_t *buf, size_t len) {
    if (len < 4 + MAFA_SAVE_BODY_V1 + 1) return false;
    if (buf[0] != 'M' || buf[1] != 'F' || buf[2] != 'C') return false;
    size_t body;
    if (buf[3] == MAFA_SAVE_VERSION) body = MAFA_SAVE_BODY_V7;
    else if (buf[3] == 6) body = MAFA_SAVE_BODY_V6;
    else if (buf[3] == 5) body = MAFA_SAVE_BODY_V5;
    else if (buf[3] == 4) body = MAFA_SAVE_BODY_V4;
    else if (buf[3] == 3 || buf[3] == 2) body = MAFA_SAVE_BODY_V2;
    else if (buf[3] == 1) body = MAFA_SAVE_BODY_V1;
    else return false;
    if (len - 5 < body) return false;
    if (crc8(buf + 4, body) != buf[4 + body]) return false;

    mafa_player_t t;
    memset(&t, 0, sizeof t);
    t.rng = p->rng;                     /* the live stream is never saved */
    if (!load_payload(&t, buf + 4, body, buf[3])) return false;
    if (buf[3] == 1) {
        /* v1 → v6 migration: every skill whose (new) unlock level is
         * reached is granted its book, so old saves never lose learned
         * skills. */
        grant_level_books(&t);
    }
    if (buf[3] < 6) {
        /* v1-v5 → v7 migration. Maps: old combat ids stay 1-3 by content
         * band (森林→比奇省, 废矿→兽人古墓, 祖玛's old band→石墓), so the
         * ids already read correctly; maps 4-7 are fresh ladders at floor
         * 1 and stay locked until their own last-floor bosses fall — the
         * old endgame content (祖玛) is re-earned as the new map 6. v1-v4
         * saves carry no floor bytes: maps left behind count as fully
         * cleared, the top map re-climbs from floor 1 (v5's clamped
         * bytes stay). Books re-grant by the new unlock levels, carried xp
         * clamps to the new curve. */
        if (body < MAFA_SAVE_BODY_V5)
            for (int i = 0; i < 3; ++i)
                t.floor_unlocked[i] = t.unlocked >= i + 2
                                          ? MAFA_MAP_FLOORS[i + 1] : 1;
        for (int i = 3; i < MAFA_MAP_COUNT - 1; ++i) t.floor_unlocked[i] = 1;
        t.unlocked = t.unlocked > 3 ? 3 : t.unlocked;
        t.books = 0;
        grant_level_books(&t);
        uint32_t cap = mafa_xp_to_next(t.level);
        if (cap && t.xp >= cap) t.xp = cap - 1;
        t.floor = t.map == MAFA_MAP_SAFE ? 0 : t.floor_unlocked[t.map - 1];
    }
    if (buf[3] < 7) {
        /* v1-v6: the loadout's ids come from an older item table — swap
         * in the band/line starter kit (level, gear curve, settings and
         * floors all survive). */
        grant_migration_kit(&t);
    }
    *p = t;
    return true;
}
