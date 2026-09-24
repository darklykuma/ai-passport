// Host tests for the FOG MARCH AI (PRD_FOG_MARCH 13.3, 17.5).
// The fairness test is the load-bearing one: with an identical observation,
// the decision must not depend on the real battlefield state.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../main/fog_model.c"
#include "../main/fog_ai.c"

static void park_player_units(fog_game_t *g, int corner) {
    static const int spots[2][FOG_UNITS_PER_SIDE][2] = {
        { {0, 0}, {1, 0}, {2, 0} },      // top-left, invisible to column-7 spawns
        { {0, 7}, {0, 8}, {0, 9} },      // bottom-left, also invisible
    };
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
        g->units[FOG_SIDE_PLAYER][i].x = (int8_t)spots[corner][i][0];
        g->units[FOG_SIDE_PLAYER][i].y = (int8_t)spots[corner][i][1];
        // Simulate the enemy phase: full AP regardless of who moved first.
        g->units[FOG_SIDE_ENEMY][i].ap = FOG_AP_PER_TURN;
        g->units[FOG_SIDE_ENEMY][i].acted = false;
        g->units[FOG_SIDE_ENEMY][i].attacked = false;
    }
    fog_vision_recompute(g, FOG_SIDE_PLAYER);
    fog_vision_recompute(g, FOG_SIDE_ENEMY);
}

static void test_fairness(void) {
    fog_game_t a, b;
    fog_game_start(&a, 777u);
    fog_game_start(&b, 777u);                    // identical map and turn state
    park_player_units(&a, 0);
    park_player_units(&b, 1);                    // real player forces differ

    fog_observation_t obs_a, obs_b;
    fog_ai_observe(&obs_a, &a, FOG_SIDE_ENEMY, 0);
    fog_ai_observe(&obs_b, &b, FOG_SIDE_ENEMY, 0);
    // Sanity: neither observation leaked the hidden player positions.
    for (int y = 0; y < FOG_MAP_H; ++y)
        for (int x = 0; x < FOG_MAP_W; ++x) {
            assert(obs_a.enemy_known[y][x] == 0);
            assert(obs_b.enemy_known[y][x] == 0);
        }
    fog_ai_memory_t mem = { 0 };
    fog_ai_action_t act_a = fog_ai_decide(&obs_a, &mem);
    fog_ai_action_t act_b = fog_ai_decide(&obs_b, &mem);
    assert(act_a.kind == act_b.kind);
    assert(act_a.x == act_b.x && act_a.y == act_b.y);
    assert(act_a.ap_cost == act_b.ap_cost);

    // Same observation, repeated call: identical output (determinism).
    fog_ai_action_t again = fog_ai_decide(&obs_a, &mem);
    assert(again.kind == act_a.kind && again.x == act_a.x && again.y == act_a.y);
}

static void test_engages_visible_enemy(void) {
    fog_game_t g;
    fog_game_start(&g, 42u);
    memset(g.terrain, FOG_TERRAIN_PLAIN, sizeof g.terrain);
    g.units[FOG_SIDE_ENEMY][0].x = 1;            // enemy general
    g.units[FOG_SIDE_ENEMY][0].y = 3;
    g.units[FOG_SIDE_ENEMY][1].x = 7;
    g.units[FOG_SIDE_ENEMY][1].y = 8;
    g.units[FOG_SIDE_ENEMY][2].x = 7;
    g.units[FOG_SIDE_ENEMY][2].y = 9;
    g.units[FOG_SIDE_PLAYER][0].x = 0;           // player general adjacent
    g.units[FOG_SIDE_PLAYER][0].y = 3;
    g.units[FOG_SIDE_PLAYER][1].x = 0;
    g.units[FOG_SIDE_PLAYER][1].y = 9;
    g.units[FOG_SIDE_PLAYER][2].x = 1;
    g.units[FOG_SIDE_PLAYER][2].y = 9;
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {    // enemy phase: full AP
        g.units[FOG_SIDE_ENEMY][i].ap = FOG_AP_PER_TURN;
        g.units[FOG_SIDE_ENEMY][i].acted = false;
        g.units[FOG_SIDE_ENEMY][i].attacked = false;
    }
    fog_vision_recompute(&g, FOG_SIDE_ENEMY);

    fog_observation_t obs;
    fog_ai_observe(&obs, &g, FOG_SIDE_ENEMY, 0);
    assert(obs.enemy_known[3][0] == 1);
    fog_ai_memory_t mem = { 0 };
    fog_ai_action_t act = fog_ai_decide(&obs, &mem);
    assert(act.kind == FOG_AI_ATTACK);
    assert(act.x == 0 && act.y == 3);
}

static void test_searches_when_nothing_visible(void) {
    fog_game_t g;
    fog_game_start(&g, 4242u);
    park_player_units(&g, 0);

    fog_observation_t obs;
    fog_ai_observe(&obs, &g, FOG_SIDE_ENEMY, 0);
    fog_ai_memory_t mem = { 0 };
    fog_ai_action_t act = fog_ai_decide(&obs, &mem);
    assert(act.kind == FOG_AI_MOVE);
    assert(act.x != obs.own[0].x || act.y != obs.own[0].y);

    // The chosen cell must be a legal move in the real game.
    const fog_unit_t *u = &g.units[FOG_SIDE_ENEMY][0];
    fog_cand_t cands[FOG_CAND_MAX];
    int n = fog_candidates(&g, u, cands);
    int found = -1;
    for (int i = 0; i < n; ++i)
        if (cands[i].kind == FOG_CAND_MOVE && cands[i].x == act.x
            && cands[i].y == act.y) { found = i; break; }
    assert(found >= 0);
}

static void test_standby_without_ap(void) {
    fog_game_t g;
    fog_game_start(&g, 4242u);
    park_player_units(&g, 0);
    fog_observation_t obs;
    fog_ai_observe(&obs, &g, FOG_SIDE_ENEMY, 0);
    obs.own[0].ap = 0;                           // pure input tweak
    fog_ai_memory_t mem = { 0 };
    fog_ai_action_t act = fog_ai_decide(&obs, &mem);
    assert(act.kind == FOG_AI_STANDBY);
}

int main(void) {
    test_fairness();
    test_engages_visible_enemy();
    test_searches_when_nothing_visible();
    test_standby_without_ap();
    puts("FOG MARCH AI tests: PASS");
    return 0;
}
