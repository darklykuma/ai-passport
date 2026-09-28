// main/mafa_model.h — MAFA CHRONICLE pure game model (PRD_MAFA_CHRONICLE 8-9).
// No LVGL / ESP-IDF headers: this layer builds and tests on the host.
// Combat is automatic: the model runs one round per call and reports what
// happened through a bounded event list; the view renders log lines from it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAFA_MAX_LEVEL 15
#define MAFA_MAP_COUNT 3
#define MAFA_BACKPACK 8
#define MAFA_EQ_SLOTS 3          /* 0 weapon, 1 armor, 2 accessory */
#define MAFA_SKILLS_PER_CLASS 3
#define MAFA_KILLS_PER_BOSS 25
#define MAFA_GOLD_CAP 9999
#define MAFA_INV_EMPTY 0xFF
#define MAFA_DROP_NONE 0xFF
#define MAFA_SAVE_VERSION 1

typedef enum {
    MAFA_CLS_WARRIOR = 0,
    MAFA_CLS_MAGE,
    MAFA_CLS_TAOIST,
    MAFA_CLS_COUNT,
} mafa_class_t;

typedef enum {
    MAFA_Q_WHITE = 0,
    MAFA_Q_GREEN,
    MAFA_Q_BLUE,
    MAFA_Q_PURPLE,
    MAFA_Q_GOLD,
    MAFA_Q_COUNT,
} mafa_quality_t;

typedef enum {
    MAFA_SLOT_WEAPON = 0,
    MAFA_SLOT_ARMOR,
    MAFA_SLOT_ACCESSORY,
} mafa_slot_t;

typedef struct {
    const char *name;   /* content data; glyph coverage tested at M4 */
    uint8_t slot;       /* mafa_slot_t */
    uint8_t quality;    /* mafa_quality_t: color + sell price */
    uint8_t map;        /* home map 0..2 */
    uint8_t tier;       /* 1..3 within the map's table */
    int16_t atk, def;
    uint16_t hp;
} mafa_item_t;

extern const mafa_item_t MAFA_ITEMS[];
extern const int MAFA_ITEM_COUNT;

typedef enum {
    MAFA_SK_DMG = 0,    /* direct damage, mult × attack */
    MAFA_SK_BURN,       /* dot: N rounds of mult × attack each (locked at cast) */
    MAFA_SK_HEAL,       /* restore pct of max HP */
    MAFA_SK_POISON,     /* dot: N rounds of flat damage + monster def down */
} mafa_skill_kind_t;

typedef struct {
    const char *name;
    uint8_t unlock;         /* level */
    uint8_t kind;           /* mafa_skill_kind_t */
    uint16_t mult;          /* ×100 vs attack (dmg/burn) */
    uint8_t ignore_def;     /* damage skips monster defense */
    uint8_t rounds;         /* burn/poison duration */
    uint8_t flat;           /* poison damage per round */
    uint8_t def_down_pct;   /* monster defense ×(100−pct)/100 while poisoned */
    uint8_t cd;             /* cooldown rounds after cast */
    uint8_t mp;             /* 0 = cooldown-based (warrior) */
} mafa_skill_t;

extern const mafa_skill_t MAFA_SKILLS[MAFA_CLS_COUNT][MAFA_SKILLS_PER_CLASS];

typedef enum {
    MAFA_MSK_NONE = 0,
    MAFA_MSK_FIRE,      /* 1.5× attack */
    MAFA_MSK_FLURRY,    /* 2 hits × 0.7× attack */
    MAFA_MSK_HEAVY,     /* 1.6× attack */
    MAFA_MSK_STING,     /* 1.2× attack + poisons the player 2 rounds × 3 */
    MAFA_MSK_ROAR,      /* 1.5× attack */
    MAFA_MSK_HELLFIRE,  /* 2.0× attack */
} mafa_mob_skill_t;

typedef struct {
    const char *name;
    uint8_t map;
    uint8_t level;
    uint16_t hp;
    uint8_t atk, def;
    uint16_t xp;
    uint8_t skill;      /* mafa_mob_skill_t */
    bool boss;
} mafa_monster_t;

extern const mafa_monster_t MAFA_MONSTERS[];
extern const int MAFA_MONSTER_COUNT;
extern const char *const MAFA_MAP_NAMES[MAFA_MAP_COUNT];

/* --- Player -------------------------------------------------------------- */

typedef struct {
    uint8_t cls;            /* mafa_class_t */
    uint8_t level;          /* 1..MAFA_MAX_LEVEL */
    uint16_t xp, gold;
    int16_t hp, mp;         /* current; mp stays 0 for the warrior */
    uint8_t pot_red, pot_blue;
    uint8_t inv_id[MAFA_BACKPACK];
    uint8_t inv_n[MAFA_BACKPACK];
    uint8_t equipped[MAFA_EQ_SLOTS];    /* item id or MAFA_INV_EMPTY */
    uint8_t map;            /* current idle map */
    uint8_t unlocked;       /* highest unlocked map index */
    uint16_t kills;         /* kills on the current map, toward the boss */
    uint8_t pending_drop;   /* item id awaiting the full-backpack prompt */
    bool auto_potion;       /* settings toggle, default on */
    bool auto_sell_white;   /* settings toggle, default on */
    uint32_t rng;           /* splitmix32 state */
} mafa_player_t;

typedef struct {
    int32_t max_hp;
    int16_t max_mp, atk, def;
} mafa_stats_t;

typedef struct {
    const mafa_item_t *item;
    int16_t d_atk, d_def;
    int32_t d_hp;           /* deltas vs currently equipped (empty = 0) */
} mafa_compare_t;

/* --- Monster instance (base × level scaling × elite) --------------------- */

typedef struct {
    const mafa_monster_t *base;
    int32_t hp, max_hp;
    int32_t atk, def;
    bool elite;
    /* effect list (8.4): burn = mage, poison = taoist (flat + def down) */
    uint8_t burn_rounds;
    int16_t burn_dmg;
    uint8_t poison_rounds, poison_dmg, def_down_rounds;
} mafa_mob_t;

/* --- Battle --------------------------------------------------------------- */

typedef struct {
    mafa_mob_t mob;
    bool is_boss;
    uint8_t cd[MAFA_SKILLS_PER_CLASS];
    uint8_t rounds;
    uint8_t player_poison_rounds, player_poison_dmg;
    bool over, player_dead;
} mafa_battle_t;

typedef enum {
    MAFA_EV_PLAYER_HIT = 0,     /* a = damage */
    MAFA_EV_PLAYER_CRIT,        /* a = damage */
    MAFA_EV_SKILL_HIT,          /* id = skill index, a = damage */
    MAFA_EV_SKILL_SUPPORT,      /* id = skill index (burn/poison/heal cast) */
    MAFA_EV_DOT_TICK,           /* a = damage to the monster */
    MAFA_EV_MOB_HIT,            /* a = damage */
    MAFA_EV_MOB_SKILL,          /* id = mafa_mob_skill_t, a = damage */
    MAFA_EV_PLAYER_POISON,      /* a = damage to the player */
    MAFA_EV_HEAL,               /* a = healed */
    MAFA_EV_MOB_KILLED,         /* a = xp, b = gold (settlement rolls in tail) */
    MAFA_EV_DROP,               /* id = item, a = 1 stored / 2 sold / 3 no room */
    MAFA_EV_LEVELUP,            /* id = new level */
    MAFA_EV_PLAYER_DEATH,
} mafa_ev_kind_t;

#define MAFA_EV_MAX 8
typedef struct {
    uint8_t n;
    struct {
        uint8_t kind;
        uint8_t id;
        int32_t a;
    } e[MAFA_EV_MAX];
} mafa_events_t;

void mafa_ev_push(mafa_events_t *ev, uint8_t kind, uint8_t id, int32_t a);

/* --- API ------------------------------------------------------------------ */

void mafa_player_init(mafa_player_t *p, uint8_t cls, uint32_t seed);
void mafa_stats(const mafa_player_t *p, mafa_stats_t *out);
uint16_t mafa_xp_to_next(uint8_t level);    /* 0 at MAFA_MAX_LEVEL */

/* Out-of-combat regeneration: +5 HP and +5 MP per second (PRD 8.8). */
void mafa_regen(mafa_player_t *p, uint8_t seconds);

/* Spawns a battle against a random monster of the current map (elite 1/10).
 * Returns false when a drop prompt is still pending or the map is invalid. */
bool mafa_battle_start(mafa_player_t *p, mafa_battle_t *b);
/* Boss event: returns false when kills < MAFA_KILLS_PER_BOSS or a prompt
 * is pending. mafa_boss_pass resets the kill counter instead of fighting. */
bool mafa_boss_ready(const mafa_player_t *p);
bool mafa_boss_start(mafa_player_t *p, mafa_battle_t *b);
void mafa_boss_pass(mafa_player_t *p);

/* One automatic combat round (PRD 8.3): potions → skill policy → attack,
 * then the monster, then end-of-round ticks. Settlement (XP/gold/drop/
 * level-up/boss unlock) happens inside when the monster dies. */
void mafa_battle_round(mafa_player_t *p, mafa_battle_t *b, mafa_events_t *ev);

/* Full-backpack prompt (PRD 8.5): the drop waits in p->pending_drop until
 * the player replaces the given backpack slot or passes the drop. */
void mafa_drop_replace(mafa_player_t *p, uint8_t inv_idx);
void mafa_drop_discard(mafa_player_t *p);

bool mafa_inv_add(mafa_player_t *p, uint8_t item_id);
bool mafa_equip(mafa_player_t *p, uint8_t inv_idx);     /* false = no room */
void mafa_compare(const mafa_player_t *p, uint8_t item_id, mafa_compare_t *out);
uint32_t mafa_sell_price(uint8_t item_id);
uint32_t mafa_sell(mafa_player_t *p, uint8_t inv_idx);  /* gold gained */
uint32_t mafa_sell_all_white(mafa_player_t *p);         /* gold gained */
bool mafa_buy_potion(mafa_player_t *p, bool red);       /* 20 / 25 gold */
void mafa_switch_map(mafa_player_t *p, uint8_t map);    /* must be unlocked */

/* NVS-ready serialization (PRD 8.10): magic + version + payload + CRC8.
 * Returns the written size, or 0 when the buffer is too small / data bad. */
size_t mafa_save_serialize(const mafa_player_t *p, uint8_t *buf, size_t cap);
bool mafa_save_deserialize(mafa_player_t *p, const uint8_t *buf, size_t len);
