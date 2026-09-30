// main/mafa_model.h — MAFA CHRONICLE pure game model (PRD_MAFA_CHRONICLE 8-9,
// skills-2.0 rebalance 2026-09-29). No LVGL / ESP-IDF headers: this layer
// builds and tests on the host.
// Combat is automatic: the model runs one round per call and reports what
// happened through a bounded event list; the view renders log lines from it.
// Battles pit the player against 1-3 monsters (bosses stay 1v1); the taoist
// summons a pet that taunts; skills unlock by level AND skill book.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAFA_MAX_LEVEL 15
#define MAFA_MAP_COUNT 4         /* 0 = safe zone (town), 1-3 combat maps */
#define MAFA_MAP_SAFE 0          /* always open, no monsters, no boss */
#define MAFA_BACKPACK 8
#define MAFA_EQ_SLOTS 3          /* 0 weapon, 1 armor, 2 accessory */
#define MAFA_SKILLS_PER_CLASS 5
#define MAFA_MOBS_MAX 3          /* monsters in one non-boss battle */
#define MAFA_KILLS_PER_BOSS 40   /* mobs killed (a 3-mob battle counts 3) */
#define MAFA_GOLD_CAP 9999
#define MAFA_INV_EMPTY 0xFF
#define MAFA_DROP_NONE 0xFF
#define MAFA_SAVE_VERSION 5
/* Auto-potion trigger lines are settable in steps of 10 (PRD 10, v1.2). */
#define MAFA_POT_PCT_MIN 20
#define MAFA_POT_PCT_MAX 80
#define MAFA_POT_HP_PCT_DEFAULT 50
#define MAFA_POT_MP_PCT_DEFAULT 30

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
    MAFA_SK_PASSIVE = 0,/* always on: mult ×100 applies to normal attacks */
    MAFA_SK_PROC,       /* proc_pct % chance on a normal hit: ×mult damage */
    MAFA_SK_DMG,        /* direct damage, mult × attack */
    MAFA_SK_AOE,        /* direct damage to every living mob */
    MAFA_SK_BURN,       /* AoE dot: N rounds of mult × attack each */
    MAFA_SK_HEAL,       /* restore pct of max HP */
    MAFA_SK_POISON,     /* dot: N rounds of flat damage + monster def down */
    MAFA_SK_SHIELD,     /* player takes def_pct % less damage for N rounds */
    MAFA_SK_CHARGE,     /* next normal attack deals mult ×100 % damage */
    MAFA_SK_PET,        /* summon / upgrade the taoist pet */
} mafa_skill_kind_t;

typedef struct {
    const char *name;
    uint8_t unlock;         /* level; skill 0 of each class needs no book */
    uint8_t kind;           /* mafa_skill_kind_t */
    uint16_t mult;          /* ×100 vs attack (dmg/aoe/burn/passive/charge) */
    uint8_t ignore_def;     /* damage skips monster defense */
    uint8_t proc_pct;       /* MAFA_SK_PROC trigger chance */
    uint8_t rounds;         /* burn/poison/shield duration */
    uint8_t flat;           /* poison damage per round */
    uint8_t def_down_pct;   /* monster defense ×(100−pct)/100 while poisoned */
    uint8_t shield_pct;     /* MAFA_SK_SHIELD damage reduction */
    uint8_t cd;             /* cooldown rounds after cast (0 = none) */
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
    uint8_t map;     /* home map id 1..3 (0 never spawns) */
    uint8_t floor;   /* 1-based floor inside the map; a boss row guards its
                        floor, trash rows spawn on their floor and every
                        deeper one (PRD 8.7, v1.3) */
    uint8_t level;   /* base level; also the spawn-band and drop-tier key */
    uint16_t hp;
    uint8_t atk, def;
    uint32_t xp;
    uint8_t skill;   /* mafa_mob_skill_t */
    bool boss;       /* exactly one row per (map, floor) */
} mafa_monster_t;

extern const mafa_monster_t MAFA_MONSTERS[];
extern const int MAFA_MONSTER_COUNT;
extern const char *const MAFA_MAP_NAMES[MAFA_MAP_COUNT];
extern const uint8_t MAFA_MAP_FLOORS[MAFA_MAP_COUNT];

/* The (map, floor) boss row, or NULL for the safe zone / an unknown pair. */
const mafa_monster_t *mafa_map_boss(uint8_t map, uint8_t floor);

/* --- Player -------------------------------------------------------------- */

typedef struct {
    uint8_t cls;            /* mafa_class_t */
    uint8_t level;          /* 1..MAFA_MAX_LEVEL */
    uint32_t xp;            /* progress toward the next level */
    uint16_t gold;
    int16_t hp, mp;         /* current; mp stays 0 for the warrior */
    uint16_t books;         /* skill-book bitmask: bit = cls*5 + skill idx */
    uint8_t pot_red, pot_blue;
    uint8_t inv_id[MAFA_BACKPACK];
    uint8_t inv_n[MAFA_BACKPACK];
    uint8_t equipped[MAFA_EQ_SLOTS];    /* item id or MAFA_INV_EMPTY */
    uint8_t map;            /* current idle map (MAFA_MAP_SAFE = town) */
    uint8_t floor;          /* current floor, 1-based; 0 in the safe zone */
    uint8_t unlocked;       /* highest unlocked combat map index */
    uint8_t floor_unlocked[MAFA_MAP_COUNT - 1];
                            /* deepest open floor per combat map (index 0 =
                               map 1); the next map opens only through the
                               previous map's last floor */
    uint16_t kills;         /* mobs killed on the current map, toward boss */
    uint8_t pending_drop;   /* item id awaiting the full-backpack prompt */
    bool auto_potion;       /* settings toggle, default on */
    uint8_t auto_sell;      /* quality bitmask, bit = 1 << quality: drops of
                               these qualities sell at once; gold never
                               (default: white only) */
    bool auto_boss;         /* settings toggle: answer boss events without
                               the prompt (PRD 10 settings) */
    uint8_t pot_hp_pct;     /* auto-red below this % HP (20..80, default 50) */
    uint8_t pot_mp_pct;     /* auto-blue below this % MP (20..80, default 30) */
    uint8_t skills_off;     /* bit i = skill i of the player's class switched
                               off on the gear page (passive/proc are fixed) */
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

/* Store stock (PRD 10): the two book skills of each class are buyable;
 * books 3-4 (skill idx 3-4) come from elites and boss first-kills. */
#define MAFA_STORE_ROWS 4       /* red, blue, book(skill 1), book(skill 2) */
uint32_t mafa_book_price(uint8_t cls, uint8_t skill_idx);
bool mafa_skill_known(const mafa_player_t *p, uint8_t skill_idx);

/* --- Monster instance (base × level scaling × elite) --------------------- */

typedef struct {
    const mafa_monster_t *base;
    int32_t hp, max_hp;
    int32_t atk, def;
    bool elite;
    bool alive;
    /* effect list (8.4): burn = mage, poison = taoist (flat + def down) */
    uint8_t burn_rounds;
    int16_t burn_dmg;
    uint8_t poison_rounds, poison_dmg, def_down_rounds;
} mafa_mob_t;

/* --- Battle --------------------------------------------------------------- */

typedef struct {
    mafa_mob_t mob[MAFA_MOBS_MAX];
    uint8_t mob_n;          /* spawned count, 1..MAFA_MOBS_MAX */
    uint8_t alive_n;
    bool is_boss;           /* boss battles are always 1v1 */
    /* player-side status */
    uint8_t cd[MAFA_SKILLS_PER_CLASS];
    uint8_t shield_rounds;  /* 魔法盾: incoming damage ×(100−pct)/100 */
    uint16_t charge_mult;   /* 烈火: next normal attack ×charge_mult/100 */
    /* taoist pet: taunts while alive, attacks the first living mob */
    bool pet_alive;
    uint8_t pet_tier;       /* 1 骷髅, 2 神兽 */
    uint8_t pet_cd;         /* re-summon cooldown */
    int32_t pet_hp, pet_max_hp, pet_atk, pet_def;
    uint8_t rounds;
    uint8_t player_poison_rounds, player_poison_dmg;
    bool over, player_dead;
} mafa_battle_t;

typedef enum {
    MAFA_EV_PLAYER_HIT = 0,     /* a = damage */
    MAFA_EV_PLAYER_CRIT,        /* a = damage */
    MAFA_EV_SKILL_HIT,          /* id = skill index, a = damage */
    MAFA_EV_SKILL_SUPPORT,      /* id = skill index (or 0x80|idx = learned) */
    MAFA_EV_DOT_TICK,           /* a = damage, b = mob index */
    MAFA_EV_MOB_HIT,            /* a = damage, b = mob index */
    MAFA_EV_MOB_SKILL,          /* id = mafa_mob_skill_t, a = damage, b = mob */
    MAFA_EV_PLAYER_POISON,      /* a = damage to the player */
    MAFA_EV_HEAL,               /* a = healed */
    MAFA_EV_PET_HIT,            /* a = damage the pet deals */
    MAFA_EV_PET_GUARD,          /* a = damage the pet takes */
    MAFA_EV_PET_SUMMON,         /* id = pet tier */
    MAFA_EV_PET_DOWN,           /* the pet falls */
    MAFA_EV_MOB_KILLED,         /* a = xp, b = mob index */
    MAFA_EV_DROP,               /* id = item, a = 1 stored / 2 sold / 3 no room */
    MAFA_EV_BOOK,               /* id = skill index, a = 1 book gained */
    MAFA_EV_LEVELUP,            /* id = new level */
    MAFA_EV_DEATH_DROP,         /* id = item, a = stacks lost on death */
    MAFA_EV_GOLD_LOST,          /* a = gold lost on death */
    MAFA_EV_PLAYER_DEATH,
    MAFA_EV_FLOOR,              /* id = the floor just entered (boss win) */
    MAFA_EV_MAP_UNLOCK,         /* id = the combat map just unlocked */
} mafa_ev_kind_t;

#define MAFA_EV_MAX 12
typedef struct {
    uint8_t n;
    struct {
        uint8_t kind;
        uint8_t id;
        int32_t a;
        int32_t b;
    } e[MAFA_EV_MAX];
} mafa_events_t;

void mafa_ev_push(mafa_events_t *ev, uint8_t kind, uint8_t id, int32_t a,
                  int32_t b);

/* --- API ------------------------------------------------------------------ */

void mafa_player_init(mafa_player_t *p, uint8_t cls, uint32_t seed);
void mafa_stats(const mafa_player_t *p, mafa_stats_t *out);
uint32_t mafa_xp_to_next(uint8_t level);    /* 0 at MAFA_MAX_LEVEL */

/* Out-of-combat regeneration: +10 HP and +10 MP per call of 1 s. */
void mafa_regen(mafa_player_t *p, uint8_t seconds);

/* Spawns a battle against 1-3 monsters of the current map (weight shifts to
 * 3 mobs on deeper maps; elite 1/10 spawns a single boosted mob).
 * Returns false when a drop prompt is still pending or the map has no
 * spawns (invalid map or the safe zone). */
bool mafa_battle_start(mafa_player_t *p, mafa_battle_t *b);
/* Boss event: returns false when kills < MAFA_KILLS_PER_BOSS or a prompt
 * is pending. mafa_boss_pass resets the kill counter instead of fighting. */
bool mafa_boss_ready(const mafa_player_t *p);
bool mafa_boss_start(mafa_player_t *p, mafa_battle_t *b);
void mafa_boss_pass(mafa_player_t *p);

/* One automatic combat round (PRD 8.3): potions → skill policy → attack,
 * then every living monster (the pet taunts), then end-of-round ticks.
 * Settlement (XP/gold/drop/book/level-up/boss unlock) happens per killed
 * monster inside the round. */
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
bool mafa_buy_potion(mafa_player_t *p, bool red);       /* 50 / 40 gold */
bool mafa_buy_book(mafa_player_t *p, uint8_t skill_idx);
/* Enter a map: the safe zone ignores the floor; combat maps must be
 * unlocked and the floor within 1..floor_unlocked (v1.3 选层). Resets the
 * boss kill counter. */
void mafa_switch_map(mafa_player_t *p, uint8_t map, uint8_t floor);

/* NVS-ready serialization (PRD 8.10): magic + version + payload + CRC8.
 * Returns the written size, or 0 when the buffer is too small / data bad.
 * Older saves load and migrate (PRD 8.10, v1.2/v1.3): v1 grants books for
 * every skill whose unlock level is reached; v1/v2 map ids shift into the v3
 * numbering (safe zone 3 → 0, combat maps 0-2 → 1-3); v1-v3 read as v4 with
 * default thresholds/switches and the old auto-sell-white flag mapped onto
 * the quality mask's white bit; v4 reads as v5 with every map the player has
 * left behind fully cleared (the current top map's floors re-climb from
 * floor 1 — that ladder is the v1.3 content). */
size_t mafa_save_serialize(const mafa_player_t *p, uint8_t *buf, size_t cap);
bool mafa_save_deserialize(mafa_player_t *p, const uint8_t *buf, size_t len);
