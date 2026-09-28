// main/mafa_model.c — MAFA CHRONICLE pure game model. Host-testable;
// no LVGL/ESP-IDF. All rolls draw from one splitmix32 stream (PRD 8.1 rule
// carried over from FOG MARCH: a single source of randomness).
#include "mafa_model.h"

#include <string.h>

/* --- Content tables (PRD 9.2 / 9.3 / 8.4) --------------------------------- */

const mafa_item_t MAFA_ITEMS[] = {
    /* map 1: white / green / blue */
    {"木剑",   MAFA_SLOT_WEAPON,    MAFA_Q_WHITE,  0, 1,   2,  0,  0},
    {"青铜剑", MAFA_SLOT_WEAPON,    MAFA_Q_GREEN,  0, 2,   4,  0,  0},
    {"铁剑",   MAFA_SLOT_WEAPON,    MAFA_Q_BLUE,   0, 3,   6,  0,  0},
    {"布衣",   MAFA_SLOT_ARMOR,     MAFA_Q_WHITE,  0, 1,   0,  1,  5},
    {"精制布衣", MAFA_SLOT_ARMOR,   MAFA_Q_GREEN,  0, 2,   0,  2, 10},
    {"轻甲",   MAFA_SLOT_ARMOR,     MAFA_Q_BLUE,   0, 3,   0,  3, 15},
    {"木珠",   MAFA_SLOT_ACCESSORY, MAFA_Q_WHITE,  0, 1,   1,  0,  0},
    {"琥珀珠", MAFA_SLOT_ACCESSORY, MAFA_Q_GREEN,  0, 2,   2,  1,  0},
    {"蓝玉坠", MAFA_SLOT_ACCESSORY, MAFA_Q_BLUE,   0, 3,   3,  0,  0},
    /* map 2: green / blue / purple */
    {"矿镐",   MAFA_SLOT_WEAPON,    MAFA_Q_GREEN,  1, 1,   8,  0,  0},
    {"精钢斧", MAFA_SLOT_WEAPON,    MAFA_Q_BLUE,   1, 2,  10,  0,  0},
    {"修罗",   MAFA_SLOT_WEAPON,    MAFA_Q_PURPLE, 1, 3,  14,  0,  0},
    {"骷髅甲", MAFA_SLOT_ARMOR,     MAFA_Q_GREEN,  1, 1,   0,  4, 20},
    {"精钢甲", MAFA_SLOT_ARMOR,     MAFA_Q_BLUE,   1, 2,   0,  5, 28},
    {"修罗甲", MAFA_SLOT_ARMOR,     MAFA_Q_PURPLE, 1, 3,   0,  7, 35},
    {"玛瑙坠", MAFA_SLOT_ACCESSORY, MAFA_Q_GREEN,  1, 1,   5,  0,  0},
    {"骷髅环", MAFA_SLOT_ACCESSORY, MAFA_Q_BLUE,   1, 2,   6,  2,  0},
    {"蓝翡链", MAFA_SLOT_ACCESSORY, MAFA_Q_PURPLE, 1, 3,   8,  0,  0},
    /* map 3: blue / purple / gold */
    {"炼狱",   MAFA_SLOT_WEAPON,    MAFA_Q_BLUE,   2, 1,  18,  0,  0},
    {"雷刃",   MAFA_SLOT_WEAPON,    MAFA_Q_PURPLE, 2, 2,  20,  0,  0},
    {"屠龙",   MAFA_SLOT_WEAPON,    MAFA_Q_GOLD,   2, 3,  22,  0,  0},
    {"天魔甲", MAFA_SLOT_ARMOR,     MAFA_Q_BLUE,   2, 1,   0,  9, 45},
    {"圣战甲", MAFA_SLOT_ARMOR,     MAFA_Q_PURPLE, 2, 2,   0, 11, 55},
    {"霸主甲", MAFA_SLOT_ARMOR,     MAFA_Q_GOLD,   2, 3,   0, 13, 70},
    {"紫螺链", MAFA_SLOT_ACCESSORY, MAFA_Q_BLUE,   2, 1,  10,  0,  0},
    {"龙鳞链", MAFA_SLOT_ACCESSORY, MAFA_Q_PURPLE, 2, 2,  12,  3,  0},
    {"灵魂链", MAFA_SLOT_ACCESSORY, MAFA_Q_GOLD,   2, 3,  15,  0,  0},
};
const int MAFA_ITEM_COUNT = (int)(sizeof MAFA_ITEMS / sizeof MAFA_ITEMS[0]);

const mafa_skill_t MAFA_SKILLS[MAFA_CLS_COUNT][MAFA_SKILLS_PER_CLASS] = {
    [MAFA_CLS_WARRIOR] = {
        {"攻杀剑术",  3,  MAFA_SK_DMG,  180, 0, 0, 0, 0,  4, 0},
        {"半月弯刀",  7,  MAFA_SK_DMG,  150, 1, 0, 0, 0,  6, 0},
        {"烈火剑法", 12,  MAFA_SK_DMG,  260, 0, 0, 0, 0,  8, 0},
    },
    [MAFA_CLS_MAGE] = {
        {"雷电术",   3,  MAFA_SK_DMG,  220, 1, 0, 0, 0, 0, 10},
        {"火墙",     7,  MAFA_SK_BURN,  80, 0, 3, 0, 0, 0, 16},
        {"冰咆哮",  12,  MAFA_SK_DMG,  300, 1, 0, 0, 0, 0, 28},
    },
    [MAFA_CLS_TAOIST] = {
        {"灵魂火符",  3, MAFA_SK_DMG,  240, 0, 0, 0, 0,  0, 14},
        {"治愈术",    7, MAFA_SK_HEAL,   0, 0, 0, 0, 0,  0, 12},
        {"施毒术",   12, MAFA_SK_POISON, 0, 0, 5, 4, 30, 0, 12},
    },
};

const mafa_monster_t MAFA_MONSTERS[] = {
    {"鸡",       0,  1,  15,  4,  0,   6, MAFA_MSK_NONE,    false},
    {"鹿",       0,  1,  20,  5,  1,   8, MAFA_MSK_NONE,    false},
    {"稻草人",   0,  2,  28,  7,  1,  10, MAFA_MSK_FIRE,    false},
    {"多钩猫",   0,  3,  35,  8,  2,  12, MAFA_MSK_FLURRY,  false},
    {"森林雪人", 0,  4,  45,  9,  3,  15, MAFA_MSK_HEAVY,   false},
    {"森林巨猿", 0,  5, 300, 16,  4,  90, MAFA_MSK_ROAR,    true},
    {"骷髅",     1,  5,  40, 10,  3,  15, MAFA_MSK_NONE,    false},
    {"矿鼠",     1,  6,  35, 12,  2,  16, MAFA_MSK_FLURRY,  false},
    {"骷髅战士", 1,  7,  60, 13,  5,  20, MAFA_MSK_HEAVY,   false},
    {"掷斧骷髅", 1,  8,  55, 15,  4,  24, MAFA_MSK_FIRE,    false},
    {"洞蝎",     1,  9,  70, 16,  6,  28, MAFA_MSK_STING,   false},
    {"尸王",     1, 10, 400, 26,  7, 180, MAFA_MSK_ROAR,    true},
    {"祖玛卫士", 2, 10,  80, 18,  7,  30, MAFA_MSK_HEAVY,   false},
    {"大老鼠",   2, 11,  70, 20,  5,  32, MAFA_MSK_FLURRY,  false},
    {"黑色恶蛆", 2, 12,  95, 21,  8,  38, MAFA_MSK_STING,   false},
    {"契蛾",     2, 13,  90, 24,  6,  44, MAFA_MSK_FIRE,    false},
    {"祖玛雕像", 2, 14, 130, 26, 10,  52, MAFA_MSK_HEAVY,   false},
    {"祖玛教主", 2, 15, 1200, 22, 12, 380, MAFA_MSK_HELLFIRE, true},
};
const int MAFA_MONSTER_COUNT = (int)(sizeof MAFA_MONSTERS / sizeof MAFA_MONSTERS[0]);

const char *const MAFA_MAP_NAMES[MAFA_MAP_COUNT] = {"比奇森林", "废矿洞", "祖玛寺庙"};

static const uint32_t MAFA_SELL_PRICE[MAFA_Q_COUNT] = {10, 30, 80, 200, 500};

/* --- RNG: splitmix32 (single randomness source) --------------------------- */

static uint32_t rng_next(mafa_player_t *p) {
    p->rng += 0x9E3779B9u;
    uint32_t z = p->rng;
    z = (z ^ (z >> 16)) * 0x21F0AAADu;
    z = (z ^ (z >> 15)) * 0x735A2D97u;
    return z ^ (z >> 15);
}

/* --- Player --------------------------------------------------------------- */

void mafa_player_init(mafa_player_t *p, uint8_t cls, uint32_t seed) {
    memset(p, 0, sizeof *p);
    p->cls = cls;
    p->level = 1;
    p->gold = 0;
    p->map = 0;
    p->unlocked = 0;
    p->auto_potion = true;
    p->auto_sell_white = true;
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

uint16_t mafa_xp_to_next(uint8_t level) {
    if (level >= MAFA_MAX_LEVEL) return 0;
    return (uint16_t)(20 + (level - 1) * 25);
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
    uint32_t price = red ? 20 : 25;
    if (p->gold < price) return false;
    p->gold -= price;
    if (red) p->pot_red++;
    else p->pot_blue++;
    return true;
}

void mafa_switch_map(mafa_player_t *p, uint8_t map) {
    if (map < MAFA_MAP_COUNT && map <= p->unlocked) {
        p->map = map;
        p->kills = 0;
    }
}

/* --- Monster spawn ---------------------------------------------------------- */

static int16_t scale5(int32_t v, int diff) {
    return (int16_t)((v * (100 + 5 * diff)) / 100);
}

static void spawn(mafa_player_t *p, const mafa_monster_t *base,
                  bool elite, mafa_battle_t *b) {
    memset(b, 0, sizeof *b);
    b->is_boss = base->boss;
    int diff = (int)p->level - (int)base->level;
    if (diff < 0) diff = 0;
    b->mob.base = base;
    b->mob.max_hp = scale5(base->hp, diff);
    if (elite) b->mob.max_hp = b->mob.max_hp * 3 / 2;
    b->mob.hp = b->mob.max_hp;
    b->mob.atk = scale5(base->atk, diff);
    if (elite) b->mob.atk = b->mob.atk * 6 / 5;
    b->mob.def = scale5(base->def, diff);
    b->mob.elite = elite;
}

bool mafa_battle_start(mafa_player_t *p, mafa_battle_t *b) {
    if (p->pending_drop != MAFA_DROP_NONE) return false;
    /* Weighted table in P0 is uniform over the map's normal monsters. */
    int pool[MAFA_MONSTER_COUNT], n = 0;
    for (int i = 0; i < MAFA_MONSTER_COUNT; ++i)
        if (MAFA_MONSTERS[i].map == p->map && !MAFA_MONSTERS[i].boss)
            pool[n++] = i;
    if (n == 0) return false;
    const mafa_monster_t *base = &MAFA_MONSTERS[pool[rng_next(p) % (uint32_t)n]];
    bool elite = rng_next(p) % 10 == 0;         /* ~1/10 battles (PRD 8.6) */
    spawn(p, base, elite, b);
    return true;
}

bool mafa_boss_ready(const mafa_player_t *p) {
    return p->kills >= MAFA_KILLS_PER_BOSS;
}

bool mafa_boss_start(mafa_player_t *p, mafa_battle_t *b) {
    if (!mafa_boss_ready(p) || p->pending_drop != MAFA_DROP_NONE) return false;
    for (int i = 0; i < MAFA_MONSTER_COUNT; ++i)
        if (MAFA_MONSTERS[i].map == p->map && MAFA_MONSTERS[i].boss) {
            spawn(p, &MAFA_MONSTERS[i], false, b);
            return true;
        }
    return false;
}

void mafa_boss_pass(mafa_player_t *p) {
    p->kills = 0;
}

/* --- Settlement -------------------------------------------------------------- */

static void add_gold(mafa_player_t *p, uint32_t g) {
    p->gold += g;
    if (p->gold > MAFA_GOLD_CAP) p->gold = MAFA_GOLD_CAP;
}

static void settle_kill(mafa_player_t *p, mafa_battle_t *b, mafa_events_t *ev) {
    const mafa_monster_t *m = b->mob.base;
    uint32_t xp = m->xp;
    if (b->mob.elite) xp = xp * 3 / 2;
    p->xp += (uint16_t)xp;

    uint32_t gold = (uint32_t)m->level * (2 + rng_next(p) % 4);
    if (b->mob.elite) gold = gold * 3 / 2;
    add_gold(p, gold);
    mafa_ev_push(ev, MAFA_EV_MOB_KILLED, 0, (int32_t)xp);

    /* Level-ups (PRD 8.2): auto allocation, current HP/MP gain the increment. */
    while (p->level < MAFA_MAX_LEVEL && p->xp >= mafa_xp_to_next(p->level)) {
        p->xp -= mafa_xp_to_next(p->level);
        mafa_stats_t before, after;
        mafa_stats(p, &before);
        p->level++;
        mafa_stats(p, &after);
        p->hp += (int16_t)(after.max_hp - before.max_hp);
        p->mp += (int16_t)(after.max_mp - before.max_mp);
        mafa_ev_push(ev, MAFA_EV_LEVELUP, p->level, 0);
        /* A skill unlock lands in the same beat as its level-up. */
        for (int i = 0; i < MAFA_SKILLS_PER_CLASS; ++i)
            if (MAFA_SKILLS[p->cls][i].unlock == p->level) {
                mafa_ev_push(ev, MAFA_EV_SKILL_SUPPORT, (uint8_t)(0x80 | i), 0);
                break;
            }
    }

    /* Drop roll (PRD 8.6): the 25% gate, the tier, and the slot are separate
     * rolls so a low gate roll cannot force the bottom tier. */
    uint32_t drop_roll = rng_next(p) % 100;
    bool drops = b->is_boss || b->mob.elite || drop_roll < 25;
    if (drops) {
        uint8_t tier;
        if (b->is_boss) tier = rng_next(p) % 100 < 90 ? 2 : 3;
        else if (b->mob.elite) tier = rng_next(p) % 100 < 30 ? 3 : 2;
        else {
            uint32_t tr = rng_next(p) % 100;
            tier = tr < 70 ? 1 : (tr < 95 ? 2 : 3);
        }
        uint8_t slot = (uint8_t)(rng_next(p) % MAFA_EQ_SLOTS);
        for (int i = 0; i < MAFA_ITEM_COUNT; ++i)
            if (MAFA_ITEMS[i].map == m->map && MAFA_ITEMS[i].tier == tier
                && MAFA_ITEMS[i].slot == slot) {
                if (MAFA_ITEMS[i].quality == MAFA_Q_WHITE && p->auto_sell_white) {
                    uint32_t g = MAFA_SELL_PRICE[MAFA_Q_WHITE];
                    add_gold(p, g);
                    mafa_ev_push(ev, MAFA_EV_DROP, (uint8_t)i, 2);
                } else if (mafa_inv_add(p, (uint8_t)i)) {
                    mafa_ev_push(ev, MAFA_EV_DROP, (uint8_t)i, 1);
                } else if (p->pending_drop == MAFA_DROP_NONE) {
                    p->pending_drop = (uint8_t)i;
                    mafa_ev_push(ev, MAFA_EV_DROP, (uint8_t)i, 3);
                }
                break;
            }
    }
    if (b->is_boss) {
        p->kills = 0;
        if (p->map + 1 < MAFA_MAP_COUNT && p->unlocked < p->map + 1)
            p->unlocked = (uint8_t)(p->map + 1);
    } else {
        p->kills++;
    }
}

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

/* --- Combat (PRD 8.3) ---------------------------------------------------------- */

void mafa_ev_push(mafa_events_t *ev, uint8_t kind, uint8_t id, int32_t a) {
    if (ev->n >= MAFA_EV_MAX) return;   /* bounded: overflow drops newest */
    ev->e[ev->n].kind = kind;
    ev->e[ev->n].id = id;
    ev->e[ev->n].a = a;
    ev->n++;
}

/* Monster defense as seen by the attacker accounts for 施毒术's debuff. */
static int32_t mob_effective_def(const mafa_mob_t *m) {
    return m->def * (m->def_down_rounds > 0 ? 7 : 10) / 10;
}

static int32_t roll_damage(mafa_player_t *p, int32_t atk, int32_t def,
                           uint16_t mult, bool ignore_def, bool *crit) {
    *crit = rng_next(p) % 100 < 10;
    int32_t v = atk * (mult ? mult : 100) / 100;
    v = v * (int32_t)(9 + rng_next(p) % 3) / 10;    /* U(0.9, 1.1) */
    if (*crit) v = v * 3 / 2;
    if (!ignore_def) {
        v -= def;
        if (v < 1) v = 1;
    }
    return v;
}

/* Flat potion heals (PRD 8.5): fixed values keep the boss-fight potion
 * budget identical across classes, so one boss line threatens all of them. */
#define MAFA_RED_HEAL 30
#define MAFA_BLUE_MP 15

static bool try_potion(mafa_player_t *p, mafa_events_t *ev) {
    if (!p->auto_potion) return false;
    mafa_stats_t st;
    mafa_stats(p, &st);
    if (p->hp * 2 < st.max_hp && p->pot_red > 0) {
        p->pot_red--;
        int32_t heal = MAFA_RED_HEAL;
        p->hp += (int16_t)heal;
        if (p->hp > st.max_hp) p->hp = (int16_t)st.max_hp;
        mafa_ev_push(ev, MAFA_EV_HEAL, 1, heal);
        return true;
    }
    if (st.max_mp > 0 && p->mp * 10 < st.max_mp * 3 && p->pot_blue > 0) {
        p->pot_blue--;
        int32_t fill = MAFA_BLUE_MP;
        p->mp += (int16_t)fill;
        if (p->mp > st.max_mp) p->mp = st.max_mp;
        mafa_ev_push(ev, MAFA_EV_HEAL, 2, fill);
        return true;
    }
    return false;
}

static const mafa_skill_t *pick_skill(mafa_player_t *p, mafa_battle_t *b,
                                      uint8_t *idx) {
    const mafa_skill_t *sk = MAFA_SKILLS[p->cls];
    mafa_stats_t st;
    mafa_stats(p, &st);
    /* Per-class priority (PRD 8.4); the first ready-and-affordable wins.
     * Taoist gates: heal only when hurt, poison only when not poisoned,
     * talisman only with mana to spare — otherwise it normal-attacks. */
    static const uint8_t ORDER[MAFA_CLS_COUNT][MAFA_SKILLS_PER_CLASS] = {
        {2, 1, 0},   /* warrior: 烈火 > 半月 > 攻杀 */
        {2, 0, 1},   /* mage: 冰咆哮 > 雷电 > 火墙 */
        {1, 2, 0},   /* taoist: 治愈 > 施毒 > 火符, gated below */
    };
    for (int o = 0; o < MAFA_SKILLS_PER_CLASS; ++o) {
        uint8_t i = ORDER[p->cls][o];
        const mafa_skill_t *s = &sk[i];
        if (p->level < s->unlock || b->cd[i] > 0) continue;
        if (s->mp > 0 && p->mp < s->mp) continue;
        if (p->cls == MAFA_CLS_TAOIST) {
            if (s->kind == MAFA_SK_HEAL && p->hp * 10 >= st.max_hp * 6) continue;
            if (s->kind == MAFA_SK_POISON && b->mob.poison_rounds > 0) continue;
            if (s->kind == MAFA_SK_DMG && p->mp * 10 < st.max_mp * 6) continue;
        }
        *idx = i;
        return s;
    }
    return NULL;
}

static void apply_dot(mafa_player_t *p, mafa_battle_t *b, const mafa_skill_t *s,
                      mafa_events_t *ev) {
    if (s->kind == MAFA_SK_BURN) {
        mafa_stats_t st;
        mafa_stats(p, &st);
        b->mob.burn_rounds = s->rounds;
        b->mob.burn_dmg = (int16_t)(st.atk * s->mult / 100);
    } else if (s->kind == MAFA_SK_POISON) {
        b->mob.poison_rounds = s->rounds;
        b->mob.poison_dmg = s->flat;
        b->mob.def_down_rounds = s->rounds;
    }
    mafa_ev_push(ev, MAFA_EV_SKILL_SUPPORT, (uint8_t)(s - MAFA_SKILLS[p->cls]), 0);
}

static void player_turn(mafa_player_t *p, mafa_battle_t *b, mafa_events_t *ev) {
    if (try_potion(p, ev)) return;
    uint8_t idx = 0;
    const mafa_skill_t *s = pick_skill(p, b, &idx);
    int32_t mdef = mob_effective_def(&b->mob);
    if (s) {
        if (s->mp > 0) p->mp -= s->mp;
        b->cd[idx] = s->cd;
        mafa_stats_t st;
        mafa_stats(p, &st);
        if (s->kind == MAFA_SK_DMG) {
            bool crit;
            int32_t dmg = roll_damage(p, st.atk, mdef, s->mult,
                                      s->ignore_def, &crit);
            b->mob.hp -= dmg;
            mafa_ev_push(ev, crit ? MAFA_EV_PLAYER_CRIT : MAFA_EV_SKILL_HIT,
                         idx, dmg);
        } else {
            apply_dot(p, b, s, ev);
            if (s->kind == MAFA_SK_HEAL) {
                int32_t heal = st.max_hp * 3 / 10;
                p->hp += (int16_t)heal;
                if (p->hp > st.max_hp) p->hp = (int16_t)st.max_hp;
                mafa_ev_push(ev, MAFA_EV_HEAL, 0, heal);
            }
        }
        return;
    }
    mafa_stats_t st;
    mafa_stats(p, &st);
    bool crit;
    int32_t dmg = roll_damage(p, st.atk, mdef, 0, false, &crit);
    b->mob.hp -= dmg;
    mafa_ev_push(ev, crit ? MAFA_EV_PLAYER_CRIT : MAFA_EV_PLAYER_HIT, 0, dmg);
}

static void mob_turn(mafa_player_t *p, mafa_battle_t *b, mafa_events_t *ev) {
    const mafa_monster_t *m = b->mob.base;
    bool low = b->mob.hp * 10 < b->mob.max_hp * 3;
    bool use_skill = m->skill != MAFA_MSK_NONE && low
                     && rng_next(p) % 100 < 50;
    int32_t hits[2] = {0, 0};
    uint8_t n_hits = 1;
    uint8_t kind = MAFA_MSK_NONE;

    if (use_skill) {
        kind = m->skill;
        switch (m->skill) {
        case MAFA_MSK_FIRE:    hits[0] = b->mob.atk * 15 / 10; break;
        case MAFA_MSK_FLURRY:  hits[0] = b->mob.atk * 7 / 10;
                               hits[1] = b->mob.atk * 7 / 10;
                               n_hits = 2; break;
        case MAFA_MSK_HEAVY:   hits[0] = b->mob.atk * 16 / 10; break;
        case MAFA_MSK_STING:   hits[0] = b->mob.atk * 12 / 10;
                               b->player_poison_rounds = 2;
                               b->player_poison_dmg = 3; break;
        case MAFA_MSK_ROAR:    hits[0] = b->mob.atk * 15 / 10; break;
        case MAFA_MSK_HELLFIRE: hits[0] = b->mob.atk * 20 / 10; break;
        default: use_skill = false; break;
        }
    }
    if (!use_skill) hits[0] = b->mob.atk;

    /* Player defense applies to physical hits (all mob skills are physical).
     * Bosses pierce 50% of defense (PRD 9.2): the one lever that keeps
     * high-defense warriors and glass-cannon mages in the same danger band.
     * Mob damage rolls U(0.8, 1.2) so fight outcomes spread smoothly. */
    mafa_stats_t st;
    mafa_stats(p, &st);
    int32_t pdef = st.def;
    if (b->is_boss) pdef /= 2;
    for (uint8_t i = 0; i < n_hits; ++i) {
        int32_t dmg = (hits[i] * (int32_t)(8 + rng_next(p) % 5) / 10) - pdef;
        if (dmg < 1) dmg = 1;
        p->hp -= (int16_t)dmg;
        mafa_ev_push(ev, use_skill ? MAFA_EV_MOB_SKILL : MAFA_EV_MOB_HIT,
                     kind, dmg);
    }
}

void mafa_battle_round(mafa_player_t *p, mafa_battle_t *b, mafa_events_t *ev) {
    ev->n = 0;
    if (b->over) return;
    b->rounds++;                        /* count every player action */

    /* Player poison tick (cave scorpion line) — ignores defense. */
    if (b->player_poison_rounds > 0) {
        b->player_poison_rounds--;
        p->hp -= (int16_t)b->player_poison_dmg;
        mafa_ev_push(ev, MAFA_EV_PLAYER_POISON, 0, b->player_poison_dmg);
        if (p->hp <= 0) {
            p->hp = 0;
            b->over = true;
            b->player_dead = true;
            mafa_ev_push(ev, MAFA_EV_PLAYER_DEATH, 0, 0);
            p->kills = 0;                   /* death resets the boss counter */
            return;
        }
    }

    player_turn(p, b, ev);

    if (b->mob.hp <= 0) {
        b->mob.hp = 0;
        b->over = true;
        settle_kill(p, b, ev);
        return;
    }

    mob_turn(p, b, ev);
    if (p->hp <= 0) {
        p->hp = 0;
        b->over = true;
        b->player_dead = true;
        mafa_ev_push(ev, MAFA_EV_PLAYER_DEATH, 0, 0);
        p->kills = 0;
        return;
    }

    /* End of round: DoTs on the monster, cooldowns, mana regen. */
    if (b->mob.burn_rounds > 0) {
        b->mob.burn_rounds--;
        b->mob.hp -= b->mob.burn_dmg;
        mafa_ev_push(ev, MAFA_EV_DOT_TICK, 0, b->mob.burn_dmg);
    }
    if (b->mob.poison_rounds > 0) {
        b->mob.poison_rounds--;
        b->mob.hp -= b->mob.poison_dmg;
        mafa_ev_push(ev, MAFA_EV_DOT_TICK, 0, b->mob.poison_dmg);
    }
    if (b->mob.def_down_rounds > 0) b->mob.def_down_rounds--;
    if (b->mob.hp <= 0) {
        b->mob.hp = 0;
        b->over = true;
        settle_kill(p, b, ev);
        return;
    }
    for (int i = 0; i < MAFA_SKILLS_PER_CLASS; ++i)
        if (b->cd[i] > 0) b->cd[i]--;
    mafa_stats_t st;
    mafa_stats(p, &st);
    if (st.max_mp > 0) {
        p->mp += 2;
        if (p->mp > st.max_mp) p->mp = st.max_mp;
    }
}

/* --- Save (PRD 8.10) ---------------------------------------------------------- */

static uint8_t crc8(const uint8_t *d, size_t n) {
    uint8_t c = 0;
    for (size_t i = 0; i < n; ++i) {
        c ^= d[i];
        for (int k = 0; k < 8; ++k)
            c = (uint8_t)((c & 0x80) ? (c << 1) ^ 0x07 : c << 1);
    }
    return c;
}

size_t mafa_save_serialize(const mafa_player_t *p, uint8_t *buf, size_t cap) {
    size_t payload = 2 + 2 + 2 + 2 + 2 + 2 + 2 + 1 + 1 + 3 + MAFA_BACKPACK * 2 + 1;
    size_t total = 4 + payload + 1;     /* magic+version, payload, CRC8 */
    if (cap < total) return 0;
    buf[0] = 'M'; buf[1] = 'F'; buf[2] = 'C'; buf[3] = MAFA_SAVE_VERSION;
    uint8_t *w = buf + 4;
    *w++ = p->cls;
    *w++ = p->level;
    *w++ = (uint8_t)(p->xp & 0xFF); *w++ = (uint8_t)(p->xp >> 8);
    *w++ = (uint8_t)(p->gold & 0xFF); *w++ = (uint8_t)(p->gold >> 8);
    *w++ = (uint8_t)((uint16_t)p->hp & 0xFF); *w++ = (uint8_t)((uint16_t)p->hp >> 8);
    *w++ = (uint8_t)((uint16_t)p->mp & 0xFF); *w++ = (uint8_t)((uint16_t)p->mp >> 8);
    *w++ = p->pot_red; *w++ = p->pot_blue;
    *w++ = p->kills & 0xFF; *w++ = p->kills >> 8;
    *w++ = (uint8_t)(p->map | (p->unlocked << 2)
                     | (p->auto_potion ? 0x10 : 0)
                     | (p->auto_sell_white ? 0x20 : 0)
                     | (p->auto_boss ? 0x40 : 0));
    *w++ = p->pending_drop;
    for (int i = 0; i < MAFA_EQ_SLOTS; ++i) *w++ = p->equipped[i];
    for (int i = 0; i < MAFA_BACKPACK; ++i) { *w++ = p->inv_id[i]; *w++ = p->inv_n[i]; }
    *w++ = 0;   /* spare byte keeps payload even and leaves room to grow */
    size_t body = (size_t)(w - (buf + 4));
    buf[4 + body] = crc8(buf + 4, body);
    return total;
}

bool mafa_save_deserialize(mafa_player_t *p, const uint8_t *buf, size_t len) {
    if (len < 4 + 36 + 1) return false;     /* header + full v1 payload + CRC */
    if (buf[0] != 'M' || buf[1] != 'F' || buf[2] != 'C') return false;
    if (buf[3] != MAFA_SAVE_VERSION) return false;
    size_t body = len - 5;
    if (body != 36) return false;           /* v1 payload is exactly 36 bytes */
    if (crc8(buf + 4, body) != buf[4 + body]) return false;

    mafa_player_t t = *p;               /* keep the RNG stream on failure */
    const uint8_t *r = buf + 4;
    t.cls = *r++;
    t.level = *r++;
    if (t.cls >= MAFA_CLS_COUNT || t.level < 1 || t.level > MAFA_MAX_LEVEL)
        return false;
    t.xp = (uint16_t)(r[0] | (r[1] << 8)); r += 2;
    t.gold = (uint16_t)(r[0] | (r[1] << 8)); r += 2;
    t.hp = (int16_t)(r[0] | (r[1] << 8)); r += 2;
    t.mp = (int16_t)(r[0] | (r[1] << 8)); r += 2;
    t.pot_red = *r++; t.pot_blue = *r++;
    t.kills = (uint16_t)(r[0] | (r[1] << 8)); r += 2;
    uint8_t flags = *r++;
    t.map = flags & 3;
    t.unlocked = (flags >> 2) & 3;
    if (t.map >= MAFA_MAP_COUNT || t.unlocked >= MAFA_MAP_COUNT) return false;
    t.auto_potion = (flags & 0x10) != 0;
    t.auto_sell_white = (flags & 0x20) != 0;
    t.auto_boss = (flags & 0x40) != 0;
    t.pending_drop = *r++;
    if (t.pending_drop != MAFA_DROP_NONE
        && t.pending_drop >= MAFA_ITEM_COUNT) return false;
    for (int i = 0; i < MAFA_EQ_SLOTS; ++i) {
        t.equipped[i] = *r++;
        if (t.equipped[i] != MAFA_INV_EMPTY
            && t.equipped[i] >= MAFA_ITEM_COUNT) return false;
        if (t.equipped[i] != MAFA_INV_EMPTY
            && MAFA_ITEMS[t.equipped[i]].slot != i) return false;
    }
    for (int i = 0; i < MAFA_BACKPACK; ++i) {
        t.inv_id[i] = *r++; t.inv_n[i] = *r++;
        if (t.inv_id[i] != MAFA_INV_EMPTY
            && (t.inv_id[i] >= MAFA_ITEM_COUNT || t.inv_n[i] == 0)) return false;
        if (t.inv_id[i] == MAFA_INV_EMPTY) t.inv_n[i] = 0;
    }
    *p = t;
    return true;
}
