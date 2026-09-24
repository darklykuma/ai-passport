// Host tests for the FOG MARCH pure model (PRD_FOG_MARCH 17.5).
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../main/fog_model.c"

// Deterministic handcrafted state: all-plain map, lineups back on their
// spawns, full AP, player to move. Tests override terrain/positions after.
static void handcraft(fog_game_t *g) {
    fog_game_start(g, 1);
    memset(g->terrain, FOG_TERRAIN_PLAIN, sizeof g->terrain);
    for (int s = 0; s < FOG_SIDE_COUNT; ++s)
        for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
            fog_unit_t *u = &g->units[s][i];
            u->x = (int8_t)(s == FOG_SIDE_PLAYER ? 0 : FOG_MAP_W - 1);
            u->y = (int8_t)(3 + i);
            u->strength = FOG_CLASS_STATS[u->cls].strength;
            u->ap = FOG_AP_PER_TURN;
            u->acted = false;
            u->attacked = false;
            u->alive = true;
        }
    memset(g->seen, 0, sizeof g->seen);
    memset(g->explored, 0, sizeof g->explored);
    memset(g->ghost, 0, sizeof g->ghost);
    memset(g->ghost_cls, 0, sizeof g->ghost_cls);
    g->first_side = FOG_SIDE_PLAYER;
    g->phase_side = FOG_SIDE_PLAYER;
    g->phase = FOG_PHASE_SELECT;
    g->winner = FOG_WIN_NONE;
    g->round = 1;
    g->selected = 0;
    fog_vision_recompute(g, FOG_SIDE_PLAYER);
    fog_vision_recompute(g, FOG_SIDE_ENEMY);
}

// Park the non-general player units far away so a single observer remains.
static void isolate_general(fog_game_t *g) {
    g->units[FOG_SIDE_PLAYER][1].x = 7;
    g->units[FOG_SIDE_PLAYER][1].y = 9;
    g->units[FOG_SIDE_PLAYER][2].x = 7;
    g->units[FOG_SIDE_PLAYER][2].y = 8;
    fog_vision_recompute(g, FOG_SIDE_PLAYER);
}

static int find_cand(const fog_cand_t *c, int n, int kind, int x, int y) {
    for (int i = 0; i < n; ++i)
        if (c[i].kind == kind && c[i].x == x && c[i].y == y) return i;
    return -1;
}

static void test_prng_reproducible(void) {
    fog_game_t a, b;
    fog_game_start(&a, 12345u);
    fog_game_start(&b, 12345u);
    assert(memcmp(a.terrain, b.terrain, sizeof a.terrain) == 0);
    assert(a.first_side == b.first_side);
    for (int s = 0; s < FOG_SIDE_COUNT; ++s)
        for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
            assert(a.units[s][i].x == b.units[s][i].x);
            assert(a.units[s][i].y == b.units[s][i].y);
            assert(a.units[s][i].cls == b.units[s][i].cls);
        }
    fog_game_start(&b, 12346u);
    assert(memcmp(a.terrain, b.terrain, sizeof a.terrain) != 0);
}

static void test_map_generation(void) {
    for (uint32_t seed = 1; seed <= 100; ++seed) {
        fog_game_t g;
        fog_game_start(&g, seed);
        assert(fog_map_connected(&g));
        assert(g.terrain[FOG_CITY_Y][FOG_CITY_X] == FOG_TERRAIN_CITY);
        int river = 0;
        for (int y = 0; y < FOG_MAP_H; ++y)
            for (int x = 0; x < FOG_MAP_W; ++x)
                river += g.terrain[y][x] == FOG_TERRAIN_RIVER;
        // Band minus 2 fords, up to 2 spawn holes, and the city overwrite.
        assert(river >= 3);
        for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
            assert(g.units[0][i].alive && g.units[1][i].alive);
            assert(fog_passable(&g, g.units[0][i].x, g.units[0][i].y));
            assert(fog_passable(&g, g.units[1][i].x, g.units[1][i].y));
        }
    }
}

static void test_vision_radius_and_explored(void) {
    fog_game_t g;
    handcraft(&g);
    isolate_general(&g);                           // only the general (0,3), r=3
    for (int y = 0; y <= 6; ++y)
        for (int x = 0; x <= 3; ++x)
            assert(g.seen[FOG_SIDE_PLAYER][y][x]);
    assert(!g.seen[FOG_SIDE_PLAYER][0][7]);
    assert(!g.seen[FOG_SIDE_PLAYER][8][3]);       // far from all three units
    // Walk the observer away: seen clears, explored persists (PRD 8.3).
    g.units[FOG_SIDE_PLAYER][0].x = 7;
    g.units[FOG_SIDE_PLAYER][0].y = 9;
    fog_vision_recompute(&g, FOG_SIDE_PLAYER);
    assert(g.explored[FOG_SIDE_PLAYER][2][2]);
    assert(!g.seen[FOG_SIDE_PLAYER][2][2]);
}

static void test_vision_endpoint_rule_and_blocking(void) {
    fog_game_t g;
    handcraft(&g);
    isolate_general(&g);
    g.terrain[3][1] = FOG_TERRAIN_MOUNTAIN;        // (1,3) blocks the row ahead
    fog_vision_recompute(&g, FOG_SIDE_PLAYER);
    assert(g.seen[FOG_SIDE_PLAYER][3][1]);         // endpoint rule: itself seen
    assert(!g.seen[FOG_SIDE_PLAYER][3][2]);        // directly behind: blocked
    assert(!g.seen[FOG_SIDE_PLAYER][3][3]);
    assert(!g.seen[FOG_SIDE_PLAYER][2][2]);        // this ray rounds through (1,3)
    assert(g.seen[FOG_SIDE_PLAYER][2][1]);         // diagonal around it is clear
}

static void test_forest_concealment(void) {
    fog_game_t g;
    handcraft(&g);
    isolate_general(&g);
    g.terrain[4][4] = FOG_TERRAIN_FOREST;
    fog_unit_t *archer = &g.units[FOG_SIDE_PLAYER][2];
    archer->x = 1;
    archer->y = 4;
    fog_unit_t *foe = &g.units[FOG_SIDE_ENEMY][0];
    foe->x = 4;
    foe->y = 4;                                    // stands in the forest
    fog_vision_recompute(&g, FOG_SIDE_PLAYER);
    assert(g.seen[FOG_SIDE_PLAYER][4][4]);         // terrain itself is seen
    assert(!fog_can_see_unit(&g, FOG_SIDE_PLAYER, 4, 4));  // hidden at dist 3
    archer->x = 2;                                 // distance 2: revealed (8.1)
    fog_vision_recompute(&g, FOG_SIDE_PLAYER);
    assert(fog_can_see_unit(&g, FOG_SIDE_PLAYER, 4, 4));
}

static void test_city_vision_bonus(void) {
    fog_game_t g;
    handcraft(&g);
    g.terrain[4][0] = FOG_TERRAIN_CITY;            // spearman stands on the city
    g.units[FOG_SIDE_PLAYER][0].x = 7;             // park the others far away
    g.units[FOG_SIDE_PLAYER][0].y = 9;
    g.units[FOG_SIDE_PLAYER][2].x = 7;
    g.units[FOG_SIDE_PLAYER][2].y = 8;
    fog_vision_recompute(&g, FOG_SIDE_PLAYER);
    // Spearman vision 2, city +1 -> 3: (3,4) seen, (4,4) beyond it is not.
    assert(g.seen[FOG_SIDE_PLAYER][4][3]);
    assert(!g.seen[FOG_SIDE_PLAYER][4][4]);
}

static void test_candidate_order_and_costs(void) {
    fog_game_t g;
    handcraft(&g);
    isolate_general(&g);                           // parks spear/archer far
    g.units[FOG_SIDE_PLAYER][0].x = 7;             // and the general too, so
    g.units[FOG_SIDE_PLAYER][0].y = 7;             // (0,3) is free for the spear
    fog_unit_t *u = &g.units[FOG_SIDE_PLAYER][1];  // spearman moves to (0,3)
    u->x = 0;
    u->y = 3;
    fog_cand_t c[FOG_CAND_MAX];
    int n = fog_candidates(&g, u, c);
    assert(n > 5);
    assert(c[n - 1].kind == FOG_CAND_STANDBY);     // fixed tail (9.2 item 2)
    // Distance-1 group ordered up, right, down (no left at the edge) (9.2).
    assert(c[0].x == 0 && c[0].y == 2);
    assert(c[1].x == 1 && c[1].y == 2);
    assert(c[2].x == 1 && c[2].y == 3);
    assert(c[3].x == 0 && c[3].y == 4);
    assert(c[4].x == 1 && c[4].y == 4);
    fog_cand_t c2[FOG_CAND_MAX];                   // stable ordering
    int n2 = fog_candidates(&g, u, c2);
    assert(n == n2 && memcmp(c, c2, (size_t)n * sizeof c[0]) == 0);

    g.terrain[2][0] = FOG_TERRAIN_FOREST;          // (0,2) costs 2 AP (8.4)
    n = fog_candidates(&g, u, c);
    int i2 = find_cand(c, n, FOG_CAND_MOVE, 0, 2);
    assert(i2 >= 0 && c[i2].ap_cost == FOG_FOREST_MOVE_AP);

    // A forest wall in column 1: crossing it sums to 3 AP, nothing beyond.
    g.terrain[2][1] = FOG_TERRAIN_FOREST;
    g.terrain[3][1] = FOG_TERRAIN_FOREST;
    g.terrain[4][1] = FOG_TERRAIN_FOREST;
    n = fog_candidates(&g, u, c);
    int cross = find_cand(c, n, FOG_CAND_MOVE, 2, 3);
    assert(cross >= 0 && c[cross].ap_cost == FOG_FOREST_MOVE_AP + FOG_MOVE_AP);
    assert(find_cand(c, n, FOG_CAND_MOVE, 3, 3) < 0);

    // One unit per cell (8.1): the enemy cell is never a destination, and its
    // cell blocks transit while a detour still reaches beyond it.
    fog_unit_t *foe = &g.units[FOG_SIDE_ENEMY][0];
    foe->x = 1;
    foe->y = 3;                                    // replaces that forest cell
    n = fog_candidates(&g, u, c);
    assert(find_cand(c, n, FOG_CAND_MOVE, 1, 3) < 0);
    assert(find_cand(c, n, FOG_CAND_MOVE, 2, 3) >= 0);
}

static void test_attack_and_muzzle_flash(void) {
    fog_game_t g;
    fog_cand_t c[FOG_CAND_MAX];
    int n, at;

    handcraft(&g);
    isolate_general(&g);
    fog_unit_t *u = &g.units[FOG_SIDE_PLAYER][1];  // spearman
    u->x = 3;
    u->y = 3;
    fog_unit_t *foe = &g.units[FOG_SIDE_ENEMY][0]; // enemy general
    foe->x = 4;
    foe->y = 3;
    fog_vision_recompute(&g, FOG_SIDE_PLAYER);
    fog_vision_recompute(&g, FOG_SIDE_ENEMY);

    n = fog_candidates(&g, u, c);
    at = find_cand(c, n, FOG_CAND_ATTACK, 4, 3);
    assert(at >= 0);
    assert(c[at].damage == FOG_BASE_ATTACK);       // P0 neutral resolver (ch.18)
    assert(c[at].target_left == foe->strength - FOG_BASE_ATTACK);
    assert(c[at].ap_cost == FOG_ATTACK_AP);
    fog_apply(&g, 1, &c[at]);
    assert(foe->strength == FOG_CLASS_STATS[FOG_CLASS_GENERAL].strength
                              - FOG_BASE_ATTACK);
    assert(u->attacked);
    // Melee survivor: the target sees its attacker, so no ghost lingers.
    assert(g.ghost[FOG_SIDE_ENEMY][3][3] == 0);
    assert(g.explored[FOG_SIDE_ENEMY][3][3] == 1); // cell intel stays (8.5)
    n = fog_candidates(&g, u, c);
    assert(find_cand(c, n, FOG_CAND_ATTACK, 4, 3) < 0);  // one attack per round

    // Muzzle flash persists when no surviving defender sees the shooter:
    // a ranged kill from a cell outside every remaining enemy's vision.
    handcraft(&g);
    isolate_general(&g);
    fog_unit_t *archer = &g.units[FOG_SIDE_PLAYER][2];
    archer->x = 2;
    archer->y = 3;
    fog_unit_t *target = &g.units[FOG_SIDE_ENEMY][0];
    target->x = 4;
    target->y = 3;
    target->strength = FOG_BASE_ATTACK;            // one hit kills
    g.units[FOG_SIDE_ENEMY][1].x = 7;              // keep other enemies far
    g.units[FOG_SIDE_ENEMY][1].y = 4;
    g.units[FOG_SIDE_ENEMY][2].x = 7;
    g.units[FOG_SIDE_ENEMY][2].y = 5;
    fog_vision_recompute(&g, FOG_SIDE_PLAYER);
    fog_vision_recompute(&g, FOG_SIDE_ENEMY);
    n = fog_candidates(&g, archer, c);
    at = find_cand(c, n, FOG_CAND_ATTACK, 4, 3);
    assert(at >= 0);
    fog_apply(&g, 2, &c[at]);
    assert(!target->alive);
    assert(g.ghost[FOG_SIDE_ENEMY][3][2] == 1);    // ghost survives recompute
    assert(g.explored[FOG_SIDE_ENEMY][3][2] == 1);
    // The ghost clears once the defender side actually sees the cell again.
    g.units[FOG_SIDE_ENEMY][1].x = 2;
    g.units[FOG_SIDE_ENEMY][1].y = 4;              // adjacent observer
    fog_vision_recompute(&g, FOG_SIDE_ENEMY);
    assert(g.ghost[FOG_SIDE_ENEMY][3][2] == 0);

    // Attacking needs line of sight: no shooting over a mountain (8.5).
    handcraft(&g);
    isolate_general(&g);
    archer = &g.units[FOG_SIDE_PLAYER][2];
    archer->x = 3;
    archer->y = 3;
    target = &g.units[FOG_SIDE_ENEMY][0];
    target->x = 3;
    target->y = 5;                                 // distance 2 = archer range
    g.terrain[4][3] = FOG_TERRAIN_MOUNTAIN;
    fog_vision_recompute(&g, FOG_SIDE_PLAYER);
    n = fog_candidates(&g, archer, c);
    assert(find_cand(c, n, FOG_CAND_ATTACK, 3, 5) < 0);
}

static void test_damage_resolver(void) {
    assert(fog_damage_resolve(&(fog_damage_input_t){3, 1000, 1000, 0}) == 3);
    assert(fog_damage_resolve(&(fog_damage_input_t){3, 1000, 670, 0}) == 2);
    assert(fog_damage_resolve(&(fog_damage_input_t){3, 1500, 1000, 0}) == 4);
    assert(fog_damage_resolve(&(fog_damage_input_t){3, 1500, 670, 0}) == 3);
    assert(fog_damage_resolve(&(fog_damage_input_t){3, 670, 1000, 0}) == 2);
    assert(fog_damage_resolve(&(fog_damage_input_t){3, 670, 670, 0}) == 1);
    assert(fog_damage_resolve(&(fog_damage_input_t){3, 1500, 1000, 1}) == 5);
    // Floor and minimum 1 apply before the flat bonus (8.5 order).
    assert(fog_damage_resolve(&(fog_damage_input_t){1, 100, 100, 0}) == 1);
    assert(fog_damage_resolve(&(fog_damage_input_t){1, 100, 100, 1}) == 2);
}

static void test_turn_handover_and_draw(void) {
    fog_game_t g;
    handcraft(&g);
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) fog_standby(&g, i);
    assert(g.phase_side == FOG_SIDE_ENEMY);
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) fog_standby(&g, i);
    assert(g.round == 2 && g.phase_side == FOG_SIDE_PLAYER);

    g.round = FOG_ROUND_CAP;                       // finishing round 40 draws
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) fog_standby(&g, i);
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) fog_standby(&g, i);
    assert(g.winner == FOG_WIN_DRAW && g.phase == FOG_PHASE_END);
}

static void test_annihilation_victory(void) {
    fog_game_t g;
    handcraft(&g);
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
        g.units[FOG_SIDE_ENEMY][i].strength = 1;   // one hit each
        g.units[FOG_SIDE_ENEMY][i].x = (int8_t)(5 + i);
        g.units[FOG_SIDE_ENEMY][i].y = 5;
        g.units[FOG_SIDE_PLAYER][i].x = (int8_t)(5 + i);
        g.units[FOG_SIDE_PLAYER][i].y = 4;         // adjacent pairs
    }
    fog_vision_recompute(&g, FOG_SIDE_PLAYER);
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
        assert(g.winner == FOG_WIN_NONE);
        fog_cand_t c[FOG_CAND_MAX];
        fog_unit_t *u = &g.units[FOG_SIDE_PLAYER][i];
        int n = fog_candidates(&g, u, c);
        int at = -1;
        for (int k = 0; k < n; ++k)
            if (c[k].kind == FOG_CAND_ATTACK) { at = k; break; }
        assert(at >= 0);
        fog_apply(&g, i, &c[at]);
    }
    assert(fog_side_alive(&g, FOG_SIDE_ENEMY) == 0);
    assert(g.winner == FOG_WIN_PLAYER && g.phase == FOG_PHASE_END);
}

static void test_auto_standby_when_trapped(void) {
    fog_game_t g;
    handcraft(&g);
    g.units[FOG_SIDE_PLAYER][1].ap = 0;
    fog_cand_t c[FOG_CAND_MAX];
    int n = fog_candidates(&g, &g.units[FOG_SIDE_PLAYER][1], c);
    assert(n == 1 && c[0].kind == FOG_CAND_STANDBY);   // 9.2 item 6
}

int main(void) {
    test_prng_reproducible();
    test_map_generation();
    test_vision_radius_and_explored();
    test_vision_endpoint_rule_and_blocking();
    test_forest_concealment();
    test_city_vision_bonus();
    test_candidate_order_and_costs();
    test_attack_and_muzzle_flash();
    test_damage_resolver();
    test_turn_handover_and_draw();
    test_annihilation_victory();
    test_auto_standby_when_trapped();
    puts("FOG MARCH model tests: PASS");
    return 0;
}
