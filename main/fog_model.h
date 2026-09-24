// main/fog_model.h — FOG MARCH pure game model (PRD_FOG_MARCH chapters 8-12).
// No LVGL / ESP-IDF headers: this layer must build and test on the host.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FOG_MAP_W 8
#define FOG_MAP_H 10
#define FOG_CELLS (FOG_MAP_W * FOG_MAP_H)
#define FOG_UNITS_PER_SIDE 3
#define FOG_ROUND_CAP 40
#define FOG_AP_PER_TURN 3
#define FOG_MOVE_AP 1
#define FOG_FOREST_MOVE_AP 2
#define FOG_ATTACK_AP 2
#define FOG_BASE_ATTACK 3
#define FOG_LOW_STRENGTH 3
#define FOG_CAND_MAX 48
#define FOG_CITY_X 3
#define FOG_CITY_Y 4

typedef enum {
    FOG_TERRAIN_PLAIN = 0,
    FOG_TERRAIN_MOUNTAIN,
    FOG_TERRAIN_FOREST,
    FOG_TERRAIN_RIVER,
    FOG_TERRAIN_CITY,
} fog_terrain_t;

typedef enum {
    FOG_SIDE_PLAYER = 0,
    FOG_SIDE_ENEMY,
    FOG_SIDE_COUNT,
} fog_side_t;

typedef enum {
    FOG_CLASS_GENERAL = 0,
    FOG_CLASS_SPEAR,
    FOG_CLASS_ARCHER,
    FOG_CLASS_COUNT,
} fog_class_t;

typedef enum {
    FOG_WIN_NONE = 0,
    FOG_WIN_PLAYER,
    FOG_WIN_ENEMY,
    FOG_WIN_DRAW,
} fog_win_t;

typedef enum {
    FOG_PHASE_SELECT = 0,   // player picks a unit
    FOG_PHASE_ACTION,       // player cycles action candidates
    FOG_PHASE_ENEMY,        // AI acts (app paces it)
    FOG_PHASE_END,          // match over
} fog_phase_t;

typedef struct {
    int8_t x, y;
    uint8_t strength;
    uint8_t ap;
    uint8_t cls;
    uint8_t side;
    bool alive;
    bool acted;             // finished this phase (standby or AP exhausted)
    bool attacked;          // attacked this round (8.4: at most one attack)
} fog_unit_t;

// Per-class stats, PRD 8.2. Vision == range + 1 for the three combat classes;
// the general is the deliberate exception (3 / 1).
typedef struct {
    uint8_t strength;
    uint8_t vision;
    uint8_t range;
} fog_class_stats_t;

extern const fog_class_stats_t FOG_CLASS_STATS[FOG_CLASS_COUNT];

typedef struct {
    uint32_t seed;          // PRNG state; the original seed is kept in seed0
    uint32_t seed0;
    uint8_t terrain[FOG_MAP_H][FOG_MAP_W];
    fog_unit_t units[FOG_SIDE_COUNT][FOG_UNITS_PER_SIDE];
    uint16_t round;         // 1-based
    uint8_t first_side;     // fog_side_t, decided by the seed (8.4)
    uint8_t phase_side;
    uint8_t phase;          // fog_phase_t
    uint8_t winner;         // fog_win_t
    int8_t selected;        // selected unit index during FOG_PHASE_SELECT/ACTION
    uint8_t seen[FOG_SIDE_COUNT][FOG_MAP_H][FOG_MAP_W];       // terrain visible now
    uint8_t explored[FOG_SIDE_COUNT][FOG_MAP_H][FOG_MAP_W];   // terrain ever seen
    uint8_t ghost[FOG_SIDE_COUNT][FOG_MAP_H][FOG_MAP_W];      // muzzle-flash last-known
    uint8_t ghost_cls[FOG_SIDE_COUNT][FOG_MAP_H][FOG_MAP_W];  // ghost unit class + 1
} fog_game_t;

typedef enum {
    FOG_CAND_MOVE = 0,
    FOG_CAND_ATTACK,
    FOG_CAND_STANDBY,
} fog_cand_kind_t;

typedef struct {
    uint8_t kind;           // fog_cand_kind_t
    int8_t x, y;            // target cell (standby: unit position, unused)
    int8_t target;          // index of attacked enemy unit (attack only)
    uint8_t ap_cost;        // move path cost / FOG_ATTACK_AP / 0
    uint8_t damage;         // expected damage (attack only)
    uint8_t target_left;    // target strength after the hit (attack only)
} fog_cand_t;

// --- PRNG: the single source of randomness (PRD 8.1; no rand()/esp_random). ---
uint32_t fog_rand(fog_game_t *g);

// --- Map ---------------------------------------------------------------
void fog_map_generate(fog_game_t *g, uint32_t seed);
bool fog_map_connected(const fog_game_t *g);
bool fog_passable(const fog_game_t *g, int x, int y);

// --- Game lifecycle ----------------------------------------------------
// Starts a match: map from seed, mirrored lineups, first mover from the seed,
// round 1, fog cleared then computed. Same seed reproduces the same match.
void fog_game_start(fog_game_t *g, uint32_t seed);

// --- Queries -----------------------------------------------------------
fog_unit_t *fog_unit_at(fog_game_t *g, int x, int y);
const fog_unit_t *fog_unit_at_const(const fog_game_t *g, int x, int y);
bool fog_side_done(const fog_game_t *g, int side);
int fog_side_alive(const fog_game_t *g, int side);

// Terrain-level line of sight between two cells. Only intermediate cells block
// (endpoint rule, PRD 8.3); mountain and forest block.
bool fog_los_clear(const fog_game_t *g, int x0, int y0, int x1, int y1);

// Whether the specific unit sees the cell (radius, LOS, forest concealment,
// city +1 vision). Recomputed on demand; cheap (few units).
bool fog_unit_sees(const fog_game_t *g, const fog_unit_t *u, int x, int y);

// Whether any living unit of `side` sees a unit standing at (x, y).
bool fog_can_see_unit(const fog_game_t *g, int side, int x, int y);

// Recompute seen[][] for one side from its living units and update explored;
// clears ghosts on cells the side now sees (they have fresher information).
void fog_vision_recompute(fog_game_t *g, int side);

// --- Actions -----------------------------------------------------------
// Deterministic candidate list for a unit, PRD 9.2 ordering: moves first
// (distance ascending, then bearing up/right/down/left, then y, x), attack
// targets next (same order), standby pseudo-item always last.
// Returns the candidate count (>= 1: standby is always present).
int fog_candidates(const fog_game_t *g, const fog_unit_t *u, fog_cand_t out[FOG_CAND_MAX]);

// PRD 8.5 resolver, exact order: floor(multiplicative part), minimum 1, then
// the flat general bonus. Factors are per-mille (1000 == x1.0).
typedef struct {
    int base_attack;
    int counter_pm;     // counter factor
    int terrain_pm;     // target terrain reduction
    int general_bonus;  // flat bonus added after multiplication
} fog_damage_input_t;
int fog_damage_resolve(const fog_damage_input_t *in);

// Applies a candidate (from fog_candidates for this unit). Recomputes vision
// for both sides after position/strength changes, advances the phase when the
// acting side runs out of actions, checks victory and the round cap.
void fog_apply(fog_game_t *g, int unit_index, const fog_cand_t *cand);

// Standby shortcut (9.2 item 6: auto-standby when no real choice remains).
void fog_standby(fog_game_t *g, int unit_index);

// Miscellaneous helpers shared with the AI adapter.
static inline int fog_cheb(int x0, int y0, int x1, int y1) {
    int dx = x0 > x1 ? x0 - x1 : x1 - x0;
    int dy = y0 > y1 ? y0 - y1 : y1 - y0;
    return dx > dy ? dx : dy;
}
