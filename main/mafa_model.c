// main/mafa_model.c — MAFA CHRONICLE pure game model. Host-testable;
// no LVGL/ESP-IDF. All rolls draw from one splitmix32 stream (PRD 8.1 rule
// carried over from FOG MARCH: a single source of randomness).
// Skills-2.0 rebalance (2026-09-29): 5 skills per class unlocked by level AND
// book, 1-3 mob battles, taoist pet tank, player status layer (shield /
// charge / proc), front-fast back-wall XP curve, death drops backpack stacks
// and 10-20 % of gold, gold gear boss-only.
#include "mafa_model.h"

#include <string.h>

/* --- Content tables (PRD 9.2 / 9.3 / 8.4) --------------------------------- */

const mafa_item_t MAFA_ITEMS[] = {
    /* map 1: white / green / blue */
    {"木剑",   MAFA_SLOT_WEAPON,    MAFA_Q_WHITE,  1, 1,   2,  0,  0},
    {"青铜剑", MAFA_SLOT_WEAPON,    MAFA_Q_GREEN,  1, 2,   4,  0,  0},
    {"铁剑",   MAFA_SLOT_WEAPON,    MAFA_Q_BLUE,   1, 3,   6,  0,  0},
    {"布衣",   MAFA_SLOT_ARMOR,     MAFA_Q_WHITE,  1, 1,   0,  1,  5},
    {"精制布衣", MAFA_SLOT_ARMOR,   MAFA_Q_GREEN,  1, 2,   0,  2, 10},
    {"轻甲",   MAFA_SLOT_ARMOR,     MAFA_Q_BLUE,   1, 3,   0,  3, 15},
    {"木珠",   MAFA_SLOT_ACCESSORY, MAFA_Q_WHITE,  1, 1,   1,  0,  0},
    {"琥珀珠", MAFA_SLOT_ACCESSORY, MAFA_Q_GREEN,  1, 2,   2,  1,  0},
    {"蓝玉坠", MAFA_SLOT_ACCESSORY, MAFA_Q_BLUE,   1, 3,   3,  0,  0},
    /* map 2: green / blue / purple */
    {"矿镐",   MAFA_SLOT_WEAPON,    MAFA_Q_GREEN,  2, 1,   8,  0,  0},
    {"精钢斧", MAFA_SLOT_WEAPON,    MAFA_Q_BLUE,   2, 2,  10,  0,  0},
    {"修罗",   MAFA_SLOT_WEAPON,    MAFA_Q_PURPLE, 2, 3,  14,  0,  0},
    {"骷髅甲", MAFA_SLOT_ARMOR,     MAFA_Q_GREEN,  2, 1,   0,  4, 20},
    {"精钢甲", MAFA_SLOT_ARMOR,     MAFA_Q_BLUE,   2, 2,   0,  5, 28},
    {"修罗甲", MAFA_SLOT_ARMOR,     MAFA_Q_PURPLE, 2, 3,   0,  7, 35},
    {"玛瑙坠", MAFA_SLOT_ACCESSORY, MAFA_Q_GREEN,  2, 1,   5,  0,  0},
    {"骷髅环", MAFA_SLOT_ACCESSORY, MAFA_Q_BLUE,   2, 2,   6,  2,  0},
    {"蓝翡链", MAFA_SLOT_ACCESSORY, MAFA_Q_PURPLE, 2, 3,   8,  0,  0},
    /* map 3: blue / purple / gold */
    {"炼狱",   MAFA_SLOT_WEAPON,    MAFA_Q_BLUE,   3, 1,  18,  0,  0},
    {"雷刃",   MAFA_SLOT_WEAPON,    MAFA_Q_PURPLE, 3, 2,  20,  0,  0},
    {"屠龙",   MAFA_SLOT_WEAPON,    MAFA_Q_GOLD,   3, 3,  22,  0,  0},
    {"天魔甲", MAFA_SLOT_ARMOR,     MAFA_Q_BLUE,   3, 1,   0,  9, 45},
    {"圣战甲", MAFA_SLOT_ARMOR,     MAFA_Q_PURPLE, 3, 2,   0, 11, 55},
    {"霸主甲", MAFA_SLOT_ARMOR,     MAFA_Q_GOLD,   3, 3,   0, 13, 70},
    {"紫螺链", MAFA_SLOT_ACCESSORY, MAFA_Q_BLUE,   3, 1,  10,  0,  0},
    {"龙鳞链", MAFA_SLOT_ACCESSORY, MAFA_Q_PURPLE, 3, 2,  12,  3,  0},
    {"灵魂链", MAFA_SLOT_ACCESSORY, MAFA_Q_GOLD,   3, 3,  15,  0,  0},
};
const int MAFA_ITEM_COUNT = (int)(sizeof MAFA_ITEMS / sizeof MAFA_ITEMS[0]);

/* Five forms per class, all distinct (skills-2.0): the book gate lives in
 * mafa_skill_known — skill 0 is free, the rest need their book. mult is
 * ×100 for damage kinds, the pet tier for MAFA_SK_PET, and % max HP for
 * MAFA_SK_HEAL. */
const mafa_skill_t MAFA_SKILLS[MAFA_CLS_COUNT][MAFA_SKILLS_PER_CLASS] = {
    [MAFA_CLS_WARRIOR] = {
        {"基础剑术",  1, MAFA_SK_PASSIVE, 110, 0,  0, 0, 0,  0,  0, 0, 0},
        {"攻杀剑术",  3, MAFA_SK_PROC,    200, 0, 20, 0, 0,  0,  0, 0, 0},
        {"刺杀剑术",  6, MAFA_SK_DMG,     140, 1,  0, 0, 0,  0,  0, 5, 0},
        {"半月弯刀",  9, MAFA_SK_AOE,      90, 0,  0, 0, 0,  0,  0, 6, 0},
        {"烈火剑法", 12, MAFA_SK_CHARGE,  220, 0,  0, 0, 0,  0,  0, 8, 0},
    },
    [MAFA_CLS_MAGE] = {
        {"火球术",    1, MAFA_SK_DMG,     160, 0,  0, 0, 0,  0,  0, 0,  8},
        {"雷电术",    3, MAFA_SK_DMG,     220, 1,  0, 0, 0,  0,  0, 0, 14},
        {"火墙",      8, MAFA_SK_BURN,     60, 0,  0, 3, 0,  0,  0, 0, 18},
        {"魔法盾",   10, MAFA_SK_SHIELD,    0, 0,  0, 4, 0,  0, 40, 0, 16},
        {"冰咆哮",   13, MAFA_SK_AOE,     160, 1,  0, 0, 0,  0,  0, 0, 26},
    },
    [MAFA_CLS_TAOIST] = {
        {"治愈术",    3, MAFA_SK_HEAL,     30, 0,  0, 0, 0,  0,  0, 0, 12},
        {"召唤骷髅",  7, MAFA_SK_PET,       1, 0,  0, 0, 0,  0,  0, 6, 20},
        {"施毒术",    9, MAFA_SK_POISON,    0, 0,  0, 5, 5, 30,  0, 0, 12},
        {"灵魂火符", 11, MAFA_SK_DMG,     240, 0,  0, 0, 0,  0,  0, 0, 14},
        {"召唤神兽", 13, MAFA_SK_PET,       2, 0,  0, 0, 0,  0,  0, 6, 30},
    },
};

/* Floor ladder (PRD 8.7, v1.3): mirrors the original's 沃玛 3 层 / 祖玛
 * 7 层 dungeon shape — one boss checkpoint per floor, the next map opens
 * through the last floor's boss. */
const uint8_t MAFA_MAP_FLOORS[MAFA_MAP_COUNT] = {0, 2, 3, 7};

/* Names verified against the original's mob lists (2026-09-29 web research:
 * 比奇省 trash incl. 半兽人/钉耙猫 — 17173 白金典藏练级攻略; 废矿区 尸王 =
 * 僵尸首领 — 百度百科; 祖玛 教主之下三强 = 雕像/弓箭手/卫士, 卫士分血量
 * 档次 — 17173 大锤怪专文; 骷髅精灵 = 兽人古墓系 boss). 恶灵僵尸 rejected:
 * it belongs to 苍月岛尸魔洞, not the mine. */
const mafa_monster_t MAFA_MONSTERS[] = {
    /* -- map 1 比奇森林, 2 floors ---------------------------------------- */
    {"鸡",       1, 1,  1,   50,  7,  0,   12, MAFA_MSK_NONE,    false},
    {"鹿",       1, 1,  1,   65,  8,  1,   15, MAFA_MSK_NONE,    false},
    {"稻草人",   1, 1,  2,   95, 10,  1,   18, MAFA_MSK_FIRE,    false},
    {"多钩猫",   1, 1,  3,  130, 12,  2,   22, MAFA_MSK_FLURRY,  false},
    {"钉耙猫",   1, 2,  3,  140, 13,  2,   26, MAFA_MSK_FLURRY,  false},
    {"半兽人",   1, 2,  4,  200, 15,  3,   30, MAFA_MSK_HEAVY,   false},
    {"森林雪人", 1, 1,  4,  340, 16,  3,  120, MAFA_MSK_HEAVY,   true},
    {"森林巨猿", 1, 2,  5,  380, 20,  4,  150, MAFA_MSK_ROAR,    true},
    /* -- map 2 废矿洞, 3 floors ------------------------------------------ */
    {"骷髅",     2, 1,  5,   90, 11,  4,   34, MAFA_MSK_NONE,    false},
    {"矿鼠",     2, 1,  6,   80, 12,  2,   38, MAFA_MSK_FLURRY,  false},
    {"骷髅战士", 2, 2,  7,  130, 14,  6,   44, MAFA_MSK_HEAVY,   false},
    {"掷斧骷髅", 2, 2,  8,  120, 15,  5,   50, MAFA_MSK_FIRE,    false},
    {"洞蝎",     2, 3,  9,  150, 16,  7,   58, MAFA_MSK_STING,   false},
    {"僵尸",     2, 1,  6,  440, 22,  5,  180, MAFA_MSK_NONE,    true},
    {"骷髅精灵", 2, 2,  8,  700, 26,  6,  240, MAFA_MSK_FLURRY,  true},
    {"尸王",     2, 3, 10,  830, 27,  8,  300, MAFA_MSK_ROAR,    true},
    /* -- map 3 祖玛寺庙, 7 floors ---------------------------------------- */
    {"祖玛卫士",  3, 1, 10,  175, 15,  9,   72, MAFA_MSK_HEAVY,   false},
    {"大老鼠",    3, 2, 11,  180, 16,  6,   80, MAFA_MSK_FLURRY,  false},
    {"黑色恶蛆",  3, 3, 12,  230, 18, 10,   92, MAFA_MSK_STING,   false},
    {"契蛾",      3, 4, 13,  220, 20,  8,  104, MAFA_MSK_FIRE,    false},
    {"祖玛弓箭手",3, 4, 12,  190, 19,  7,  100, MAFA_MSK_STING,   false},
    {"祖玛雕像",  3, 5, 14,  300, 22, 12,  118, MAFA_MSK_HEAVY,   false},
    {"祖玛卫士",  3, 1, 10, 1250, 27,  7,  280, MAFA_MSK_HEAVY,   true},
    {"祖玛弓箭手",3, 2, 11, 1150, 28,  8,  330, MAFA_MSK_STING,   true},
    {"祖玛卫士",  3, 3, 12, 1400, 28, 10,  390, MAFA_MSK_HEAVY,   true},
    {"祖玛雕像",  3, 4, 12, 1450, 29, 11,  430, MAFA_MSK_HEAVY,   true},
    {"祖玛卫士",  3, 5, 13, 1500, 30, 11,  460, MAFA_MSK_ROAR,    true},
    {"祖玛雕像",  3, 6, 14, 1550, 31, 13,  530, MAFA_MSK_HEAVY,   true},
    {"祖玛教主",  3, 7, 15, 1600, 31, 14,  800, MAFA_MSK_HELLFIRE, true},
};
const int MAFA_MONSTER_COUNT = (int)(sizeof MAFA_MONSTERS / sizeof MAFA_MONSTERS[0]);

const char *const MAFA_MAP_NAMES[MAFA_MAP_COUNT] = {
    "安全区", "比奇森林", "废矿洞", "祖玛寺庙",
};

const mafa_monster_t *mafa_map_boss(uint8_t map, uint8_t floor) {
    for (int i = 0; i < MAFA_MONSTER_COUNT; ++i)
        if (MAFA_MONSTERS[i].map == map && MAFA_MONSTERS[i].floor == floor
            && MAFA_MONSTERS[i].boss)
            return &MAFA_MONSTERS[i];
    return NULL;
}

static const uint32_t MAFA_SELL_PRICE[MAFA_Q_COUNT] = {10, 30, 80, 200, 500};

/* Front-fast, back-wall curve (2026-09-29 rebalance): the early game keeps
 * the original's quick newbie pace, costs compound from mid-game, and the
 * 14→15 wall alone is ~45 % of total time-to-max (L14→15 = 150k of 315k). */
static const uint32_t MAFA_XP_NEXT[MAFA_MAX_LEVEL - 1] = {
    300, 550, 900, 1400, 2100, 3000, 4200, 5500,
    8000, 13000, 22000, 38000, 65000, 150000,
};

/* Store books (skill 1 / skill 2 per class); books 3-4 drop in battle. */
static const uint32_t MAFA_BOOK_PRICE[2] = {300, 800};

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
    if (skill_idx < 1 || skill_idx > 2) return 0;    /* only these are sold */
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
            p->books |= (uint16_t)(1u << (p->cls * MAFA_SKILLS_PER_CLASS + i));
}

/* --- Player --------------------------------------------------------------- */

void mafa_player_init(mafa_player_t *p, uint8_t cls, uint32_t seed) {
    memset(p, 0, sizeof *p);
    p->cls = cls;
    p->level = 1;
    p->gold = 0;
    p->map = MAFA_MAP_SAFE + 1;         /* new games idle at once: Beech */
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
    mafa_stats_t st = {0, 0, 0, 0};
    switch (p->cls) {
    case MAFA_CLS_WARRIOR:
        st.max_hp = 60 + 8 * lv;
        st.atk = 10 + 2 * lv;
        st.def = 5 + lv;
        break;
    case MAFA_CLS_MAGE:
        st.max_hp = 40 + 5 * lv;
        st.atk = 14 + 2 * lv;
        st.def = 3 + (lv + 1) / 2;      /* +1 every 2 levels (L2, L4, …) */
        st.max_mp = 30 + 5 * lv;
        break;
    default: /* taoist */
        st.max_hp = 60 + 6 * lv;
        st.atk = 12 + lv;
        st.def = 4 + lv;
        st.max_mp = 25 + 4 * lv;
        break;
    }
    for (int i = 0; i < MAFA_EQ_SLOTS; ++i) {
        if (p->equipped[i] == MAFA_INV_EMPTY) continue;
        const mafa_item_t *it = &MAFA_ITEMS[p->equipped[i]];
        st.atk += it->atk;
        st.def += it->def;
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

bool mafa_equip(mafa_player_t *p, uint8_t idx) {
    if (idx >= MAFA_BACKPACK || p->inv_id[idx] == MAFA_INV_EMPTY) return false;
    uint8_t id = p->inv_id[idx];
    uint8_t slot = MAFA_ITEMS[id].slot;
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
    uint8_t old = p->equipped[out->item->slot];
    mafa_stats_t next = cur;
    if (old != MAFA_INV_EMPTY) {
        const mafa_item_t *o = &MAFA_ITEMS[old];
        next.atk -= o->atk;
        next.def -= o->def;
        next.max_hp -= o->hp;
    }
    next.atk += out->item->atk;
    next.def += out->item->def;
    next.max_hp += out->item->hp;
    out->d_atk = next.atk - cur.atk;
    out->d_def = next.def - cur.def;
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
    if (skill_idx < 1 || skill_idx > 2) return false;   /* 3-4 drop in battle */
    uint16_t bit = (uint16_t)(1u << (p->cls * MAFA_SKILLS_PER_CLASS + skill_idx));
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
 * AoE skills their identity back. */
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

/* Book drops (skills-2.0 D): elites sometimes drop a missing late book;
 * a boss kill guarantees the next missing one (skill 3 first, then 4). */
static void roll_book_drop(mafa_player_t *p, bool boss, bool elite,
                           mafa_events_t *ev) {
    if (!boss && !elite) return;
    if (!boss && rng_next(p) % 100 >= 20) return;
    for (int i = 3; i < MAFA_SKILLS_PER_CLASS; ++i) {
        uint16_t bit = (uint16_t)(1u << (p->cls * MAFA_SKILLS_PER_CLASS + i));
        if (!(p->books & bit)) {
            p->books |= bit;
            mafa_ev_push(ev, MAFA_EV_BOOK, (uint8_t)i, 1, 0);
            return;
        }
    }
}

/* Drop roll (PRD 8.6, retuned): gate 20 % for normal mobs; gold-tier gear
 * comes from bosses only (8 %) — elites and trash stop at the map's purple
 * line so top items stay a chase. */
static void roll_gear_drop(mafa_player_t *p, const mafa_battle_t *b,
                           const mafa_mob_t *m, mafa_events_t *ev) {
    uint32_t drop_roll = rng_next(p) % 100;
    if (!(b->is_boss || m->elite || drop_roll < 20)) return;
    uint8_t tier;
    if (b->is_boss) tier = rng_next(p) % 100 < 8 ? 3 : 2;
    else if (m->elite) tier = rng_next(p) % 100 < 30 ? 3 : 2;
        else {
            uint32_t tr = rng_next(p) % 100;
            if (tr < 60) tier = 1;
            else if (tr < 92) tier = 2;
            else tier = m->base->map == 3 ? 2 : 3;   /* trash never drops gold */
        }
    uint8_t slot = (uint8_t)(rng_next(p) % MAFA_EQ_SLOTS);
    for (int i = 0; i < MAFA_ITEM_COUNT; ++i)
        if (MAFA_ITEMS[i].map == m->base->map && MAFA_ITEMS[i].tier == tier
            && MAFA_ITEMS[i].slot == slot) {
            uint8_t q = MAFA_ITEMS[i].quality;
            /* The auto-sell quality set (v1.2); gold never auto-sells —
             * legendary drops always reach the player. */
            if (q != MAFA_Q_GOLD && (p->auto_sell >> q) & 1) {
                uint32_t g = MAFA_SELL_PRICE[q];
                add_gold(p, g);
                mafa_ev_push(ev, MAFA_EV_DROP, (uint8_t)i, 2, 0);
            } else if (mafa_inv_add(p, (uint8_t)i)) {
                mafa_ev_push(ev, MAFA_EV_DROP, (uint8_t)i, 1, 0);
            } else if (p->pending_drop == MAFA_DROP_NONE) {
                p->pending_drop = (uint8_t)i;
                mafa_ev_push(ev, MAFA_EV_DROP, (uint8_t)i, 3, 0);
            }
            break;
        }
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

/* --- Combat (PRD 8.3, skills-2.0) ------------------------------------------------ */

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

static void summon_pet(mafa_player_t *p, mafa_battle_t *b, uint8_t tier) {
    int lv = p->level;
    b->pet_tier = tier;
    b->pet_alive = true;
    if (tier == 2) {
        b->pet_max_hp = 45 + 12 * lv;
        b->pet_atk = 6 + 3 * lv / 2;
        b->pet_def = 3 + lv / 2;
    } else {
        b->pet_max_hp = 30 + 9 * lv;
        b->pet_atk = 4 + lv;
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
     * Gates keep each form honest: AoE only into a crowd, shield when hurt
     * and none up, pet while down, heal when hurt, poison once, and the
     * taoist talisman only with mana to spare. Skills switched off on the
     * gear page (skills_off, v1.2) are skipped here. */
    static const uint8_t ORDER[MAFA_CLS_COUNT][MAFA_SKILLS_PER_CLASS] = {
        {4, 3, 2, 0xFF, 0xFF},          /* warrior: 烈火 半月 刺杀 */
        {3, 4, 1, 2, 0},                /* mage: 盾 冰咆哮 雷电 火墙 火球 */
        {0, 1, 2, 3, 0xFF},             /* taoist: 治愈 骷髅 毒 火符 */
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
            int32_t dmg = roll_damage(p, st.atk, mdef, s->mult,
                                      s->ignore_def != 0, &crit);
            b->mob[first_alive(b)].hp -= dmg;
            mafa_ev_push(ev, crit ? MAFA_EV_PLAYER_CRIT : MAFA_EV_SKILL_HIT,
                         idx, dmg, first_alive(b));
            break;
        }
        case MAFA_SK_AOE: {
            for (uint8_t i = 0; i < b->mob_n; ++i) {
                if (!b->mob[i].alive) continue;
                bool crit;
                int32_t dmg = roll_damage(p, st.atk, mob_effective_def(&b->mob[i]),
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
                    b->mob[i].burn_dmg = (int16_t)(st.atk * s->mult / 100);
                }
            mafa_ev_push(ev, MAFA_EV_SKILL_SUPPORT, idx, 0, 0);
            break;
        case MAFA_SK_POISON: {
            uint8_t t = first_alive(b);
            b->mob[t].poison_rounds = s->rounds;
            b->mob[t].poison_dmg = s->flat;
            b->mob[t].def_down_rounds = s->rounds;
            mafa_ev_push(ev, MAFA_EV_SKILL_SUPPORT, idx, 0, 0);
            break;
        }
        case MAFA_SK_HEAL: {
            int32_t heal = st.max_hp * s->mult / 100;
            p->hp += (int16_t)heal;
            if (p->hp > st.max_hp) p->hp = (int16_t)st.max_hp;
            mafa_ev_push(ev, MAFA_EV_HEAL, 0, heal, 0);
            break;
        }
        case MAFA_SK_SHIELD:
            b->shield_rounds = s->rounds;
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
    /* 魔法盾 reduction, resolved once per round from the known skill. */
    uint8_t shield_pct = 0;
    if (b->shield_rounds > 0)
        for (int k = 0; k < MAFA_SKILLS_PER_CLASS; ++k)
            if (mafa_skill_known(p, (uint8_t)k)
                && MAFA_SKILLS[p->cls][k].kind == MAFA_SK_SHIELD)
                shield_pct = MAFA_SKILLS[p->cls][k].shield_pct;
    /* The pet is re-checked per hit: it can fall mid-round and the next
     * monster swings at the player instead. */

    for (uint8_t i = 0; i < b->mob_n && !b->over; ++i) {
        if (!b->mob[i].alive) continue;
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

    /* End of round: DoTs on the monsters, cooldowns, shield/pet timers,
     * mana regen. */
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

/* --- Save (PRD 8.10, v2) ---------------------------------------------------------- */

static uint8_t crc8(const uint8_t *d, size_t n) {
    uint8_t c = 0;
    for (size_t i = 0; i < n; ++i) {
        c ^= d[i];
        for (int k = 8; k > 0; --k)
            c = (uint8_t)((c & 0x80) ? (c << 1) ^ 0x07 : c << 1);
    }
    return c;
}

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
    const size_t total = 4 + MAFA_SAVE_BODY_V5 + 1;
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
    *w++ = (uint8_t)(p->books & 0xFF); *w++ = (uint8_t)(p->books >> 8);
    *w++ = p->pot_red; *w++ = p->pot_blue;
    *w++ = p->kills & 0xFF; *w++ = p->kills >> 8;
    *w++ = (uint8_t)(p->map | (p->unlocked << 2)
                     | (p->auto_potion ? 0x10 : 0)
                     | (p->auto_boss ? 0x40 : 0));
    *w++ = p->pending_drop;
    for (int i = 0; i < MAFA_EQ_SLOTS; ++i) *w++ = p->equipped[i];
    for (int i = 0; i < MAFA_BACKPACK; ++i) { *w++ = p->inv_id[i]; *w++ = p->inv_n[i]; }
    *w++ = p->skills_off & 0x1F;
    *w++ = p->pot_hp_pct;
    *w++ = p->pot_mp_pct;
    *w++ = p->auto_sell & 0x0F;
    for (int i = 0; i < MAFA_MAP_COUNT - 1; ++i) *w++ = p->floor_unlocked[i];
    size_t body = (size_t)(w - (buf + 4));
    if (body != MAFA_SAVE_BODY_V5) return 0;
    buf[4 + body] = crc8(buf + 4, body);
    return total;
}

/* Version-aware payload reader: v1 (36 B) has xp16 and no books; v2/v3
 * (40 B) have xp32 + books; v4 (44 B) adds the skills_off / threshold /
 * auto-sell tail; v5 (46 B) replaces v4's spare byte with the three
 * per-map floor_unlocked bytes. Everything after mp shifts accordingly. */
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
    if (body >= MAFA_SAVE_BODY_V2) {
        t->books = (uint16_t)(r[0] | (r[1] << 8));
        r += 2;
    }
    t->pot_red = *r++; t->pot_blue = *r++;
    t->kills = (uint16_t)(r[0] | (r[1] << 8)); r += 2;
    uint8_t flags = *r++;
    t->map = flags & 3;
    t->unlocked = (flags >> 2) & 3;
    if (t->map >= MAFA_MAP_COUNT) return false;
    if (version >= 3) {
        /* v3: 0 = safe zone, 1-3 combat maps; unlocked must leave the
         * player somewhere to fight. */
        if (t->unlocked < 1) return false;
    } else {
        /* v1/v2 → v3 map migration: the safe zone moved from id 3 to 0
         * and combat maps 0-2 shifted to 1-3. */
        if (t->unlocked >= 3) return false;     /* old domain topped at 2 */
        t->map = t->map == 3 ? MAFA_MAP_SAFE : (uint8_t)(t->map + 1);
        t->unlocked = (uint8_t)(t->unlocked + 1);
    }
    t->auto_potion = (flags & 0x10) != 0;
    t->auto_boss = (flags & 0x40) != 0;
    /* v1-v3 defaults; the v4 tail below overwrites them. The old white-only
     * auto-sell flag maps onto the quality mask's white bit. */
    t->auto_sell = (flags & 0x20) ? 0x01 : 0x00;
    t->pot_hp_pct = MAFA_POT_HP_PCT_DEFAULT;
    t->pot_mp_pct = MAFA_POT_MP_PCT_DEFAULT;
    t->skills_off = 0;
    t->pending_drop = *r++;
    if (t->pending_drop != MAFA_DROP_NONE
        && t->pending_drop >= MAFA_ITEM_COUNT) return false;
    for (int i = 0; i < MAFA_EQ_SLOTS; ++i) {
        t->equipped[i] = *r++;
        if (t->equipped[i] != MAFA_INV_EMPTY
            && (t->equipped[i] >= MAFA_ITEM_COUNT
                || MAFA_ITEMS[t->equipped[i]].slot != i)) return false;
    }
    for (int i = 0; i < MAFA_BACKPACK; ++i) {
        t->inv_id[i] = *r++; t->inv_n[i] = *r++;
        if (t->inv_id[i] != MAFA_INV_EMPTY
            && (t->inv_id[i] >= MAFA_ITEM_COUNT || t->inv_n[i] == 0)) return false;
        if (t->inv_id[i] == MAFA_INV_EMPTY) t->inv_n[i] = 0;
    }
    if (body >= MAFA_SAVE_BODY_V4) {
        t->skills_off = (uint8_t)(*r++ & 0x1F);
        t->pot_hp_pct = *r++;
        t->pot_mp_pct = *r++;
        if (t->pot_hp_pct < MAFA_POT_PCT_MIN || t->pot_hp_pct > MAFA_POT_PCT_MAX)
            return false;
        if (t->pot_mp_pct < MAFA_POT_PCT_MIN || t->pot_mp_pct > MAFA_POT_PCT_MAX)
            return false;
        t->auto_sell = (uint8_t)(*r++ & 0x0F);
    }
    if (body == MAFA_SAVE_BODY_V5) {
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
    }
    return true;
}

bool mafa_save_deserialize(mafa_player_t *p, const uint8_t *buf, size_t len) {
    if (len < 4 + MAFA_SAVE_BODY_V1 + 1) return false;
    if (buf[0] != 'M' || buf[1] != 'F' || buf[2] != 'C') return false;
    size_t body;
    if (buf[3] == MAFA_SAVE_VERSION) body = MAFA_SAVE_BODY_V5;
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
        /* v1 → v3 migration: every skill whose unlock level is reached is
         * granted its book, so old saves never lose learned skills. */
        grant_level_books(&t);
    }
    if (buf[3] < 5) {
        /* v1-v4 → v5 floor migration: maps the player has left behind count
         * as fully cleared; the top map's floor ladder re-climbs from
         * floor 1 — that ladder is the v1.3 content. */
        for (int i = 0; i < MAFA_MAP_COUNT - 1; ++i)
            t.floor_unlocked[i] = t.unlocked >= i + 2
                                      ? MAFA_MAP_FLOORS[i + 1] : 1;
        t.floor = t.map == MAFA_MAP_SAFE ? 0 : t.floor_unlocked[t.map - 1];
    }
    *p = t;
    return true;
}
