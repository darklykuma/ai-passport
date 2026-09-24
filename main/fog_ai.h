// main/fog_ai.h — FOG MARCH AI, PRD chapter 13. P0: pure search, empty memory.
// The decision function receives ONLY the observation; it never sees the real
// battlefield. Enforced by the signature, verified by tests/test_fog_ai.c.
#pragma once

#include <stdint.h>

#include "fog_model.h"

typedef struct {
    uint8_t terrain_known[FOG_MAP_H][FOG_MAP_W];   /* observed terrain, 0 unknown */
    uint8_t visible[FOG_MAP_H][FOG_MAP_W];         /* current own-unit vision */
    uint8_t enemy_known[FOG_MAP_H][FOG_MAP_W];     /* confirmed enemy cell (vision + flash) */
    uint8_t enemy_strength[FOG_MAP_H][FOG_MAP_W];  /* last known strength, 0 unknown */
    uint8_t last_seen_round[FOG_MAP_H][FOG_MAP_W]; /* 0 = never (unused in P0 wiring) */
    uint8_t objective_x, objective_y;              /* valid when objective_known */
    uint8_t objective_known;                       /* 0 until the city cell was explored */
    uint8_t objective_owner;                       /* unused in P0 */
    uint16_t round;
    fog_unit_t own[FOG_UNITS_PER_SIDE];
    int8_t active;                                 /* index of the unit to act */
} fog_observation_t;

// P0 memory: no belief map yet (PRD 18: P0 AI has no memory layer).
typedef struct {
    uint8_t reserved;
} fog_ai_memory_t;

enum {
    FOG_AI_MOVE = 0,
    FOG_AI_ATTACK,
    FOG_AI_STANDBY,
};

typedef struct {
    uint8_t kind;      /* FOG_AI_* */
    int8_t x, y;       /* target cell */
    int8_t target;     /* enemy unit index if known, else -1 */
    uint8_t ap_cost;   /* estimated cost (validated by the caller) */
} fog_ai_action_t;

// Deterministic decision from the observation only. The caller validates the
// result against fog_candidates() and falls back to standby when illegal.
fog_ai_action_t fog_ai_decide(const fog_observation_t *obs,
                              const fog_ai_memory_t *mem);

// Fills `obs` from the real game for the AI-controlled `side`. Pure function:
// it reads only the side's own fog state (explored/seen/ghost), never hidden
// information — the same adapter runs in the app and in host tests.
void fog_ai_observe(fog_observation_t *obs, const fog_game_t *g, int side,
                    int active);
