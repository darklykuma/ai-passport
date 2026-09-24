// main/fog_model.c — FOG MARCH pure game model. Host-testable; no LVGL/ESP-IDF.
#include "fog_model.h"

#include <string.h>

const fog_class_stats_t FOG_CLASS_STATS[FOG_CLASS_COUNT] = {
    [FOG_CLASS_GENERAL] = { .strength = 12, .vision = 3, .range = 1 },
    [FOG_CLASS_SPEAR]   = { .strength = 10, .vision = 2, .range = 1 },
    [FOG_CLASS_ARCHER]  = { .strength = 6,  .vision = 3, .range = 2 },
};

// P0 lineup per PRD 8.2: general + spear + archer; cavalry is reserved (19.1).
static const uint8_t FOG_LINEUP[FOG_UNITS_PER_SIDE] = {
    FOG_CLASS_GENERAL, FOG_CLASS_SPEAR, FOG_CLASS_ARCHER,
};

// Player spawns at column 0, enemy mirrored at column 7 (PRD 8.1).
static const int8_t SPAWNS[FOG_UNITS_PER_SIDE][2] = { {0, 3}, {0, 4}, {0, 5} };
static const int8_t SPAWNS_MIRROR[FOG_UNITS_PER_SIDE][2] = {
    {FOG_MAP_W - 1, 3}, {FOG_MAP_W - 1, 4}, {FOG_MAP_W - 1, 5},
};

// --- PRNG: splitmix32. The only randomness in the game (PRD 8.1). ---------
uint32_t fog_rand(fog_game_t *g) {
    g->seed += 0x9E3779B9u;
    uint32_t z = g->seed;
    z = (z ^ (z >> 16)) * 0x21F0AAADu;
    z = (z ^ (z >> 15)) * 0x735A2D97u;
    return z ^ (z >> 15);
}

bool fog_passable(const fog_game_t *g, int x, int y) {
    if (x < 0 || y < 0 || x >= FOG_MAP_W || y >= FOG_MAP_H) return false;
    return g->terrain[y][x] != FOG_TERRAIN_RIVER;
}

// --- Fixed fallback map (PRD 8.1 / chapter 14) ----------------------------
// Deterministic hand layout: river band with two fords, scattered mountains
// and forests, city in the center. Connectivity holds by construction.
static void map_builtin(fog_game_t *g) {
    memset(g->terrain, FOG_TERRAIN_PLAIN, sizeof g->terrain);
    static const uint8_t ROCKS[][2] = {
        {1, 0}, {2, 1}, {5, 0}, {6, 2}, {1, 7}, {2, 8}, {5, 8}, {6, 7}, {4, 1},
    };
    static const uint8_t WOODS[][2] = {
        {1, 2}, {3, 2}, {5, 3}, {6, 4}, {2, 5}, {4, 6}, {1, 5}, {6, 6}, {3, 7},
    };
    for (size_t i = 0; i < sizeof ROCKS / sizeof ROCKS[0]; ++i)
        g->terrain[ROCKS[i][1]][ROCKS[i][0]] = FOG_TERRAIN_MOUNTAIN;
    for (size_t i = 0; i < sizeof WOODS / sizeof WOODS[0]; ++i)
        g->terrain[WOODS[i][1]][WOODS[i][0]] = FOG_TERRAIN_FOREST;
    for (int x = 0; x < FOG_MAP_W; ++x) g->terrain[6][x] = FOG_TERRAIN_RIVER;
    g->terrain[6][2] = FOG_TERRAIN_PLAIN;  // fords keep both halves connected
    g->terrain[6][5] = FOG_TERRAIN_PLAIN;
    g->terrain[FOG_CITY_Y][FOG_CITY_X] = FOG_TERRAIN_CITY;
}

bool fog_map_connected(const fog_game_t *g) {
    uint8_t seen[FOG_MAP_H][FOG_MAP_W];
    int8_t queue[FOG_CELLS][2];
    memset(seen, 0, sizeof seen);
    int head = 0, tail = 0;
    // Seed the flood from the player spawn; every passable cell must be reached.
    if (!fog_passable(g, 0, 3)) return false;
    seen[3][0] = 1;
    queue[tail][0] = 0;
    queue[tail][1] = 3;
    tail++;
    static const int DX[4] = { 0, 1, 0, -1 };
    static const int DY[4] = { -1, 0, 1, 0 };
    while (head < tail) {
        int x = queue[head][0], y = queue[head][1];
        head++;
        for (int d = 0; d < 4; ++d) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!fog_passable(g, nx, ny) || seen[ny][nx]) continue;
            seen[ny][nx] = 1;
            queue[tail][0] = (int8_t)nx;
            queue[tail][1] = (int8_t)ny;
            tail++;
        }
    }
    int passable_cells = 0, reached = 0;
    for (int y = 0; y < FOG_MAP_H; ++y)
        for (int x = 0; x < FOG_MAP_W; ++x) {
            if (fog_passable(g, x, y)) {
                passable_cells++;
                reached += seen[y][x];
            }
        }
    return passable_cells == reached;
}

static void map_random(fog_game_t *g) {
    memset(g->terrain, FOG_TERRAIN_PLAIN, sizeof g->terrain);

    // River: one horizontal band in the middle third, with two fords.
    // A single open band cannot form a closed ring (PRD 8.1).
    int river_y = 3 + (int)(fog_rand(g) % 4);           // rows 3..6
    for (int x = 0; x < FOG_MAP_W; ++x) g->terrain[river_y][x] = FOG_TERRAIN_RIVER;
    int ford1 = (int)(fog_rand(g) % (FOG_MAP_W / 2));
    int ford2 = FOG_MAP_W / 2 + (int)(fog_rand(g) % (FOG_MAP_W - FOG_MAP_W / 2));
    g->terrain[river_y][ford1] = FOG_TERRAIN_PLAIN;
    g->terrain[river_y][ford2] = FOG_TERRAIN_PLAIN;

    // The river band may cross the spawn columns; spawn cells themselves must
    // stay passable (PRD 8.1 — connectivity covers all spawn cells).
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
        if (g->terrain[SPAWNS[i][1]][SPAWNS[i][0]] == FOG_TERRAIN_RIVER)
            g->terrain[SPAWNS[i][1]][SPAWNS[i][0]] = FOG_TERRAIN_PLAIN;
        if (g->terrain[SPAWNS_MIRROR[i][1]][SPAWNS_MIRROR[i][0]] == FOG_TERRAIN_RIVER)
            g->terrain[SPAWNS_MIRROR[i][1]][SPAWNS_MIRROR[i][0]] = FOG_TERRAIN_PLAIN;
    }

    g->terrain[FOG_CITY_Y][FOG_CITY_X] = FOG_TERRAIN_CITY;

    // Scatter mountains and forests away from spawns, fords, and the city.
    for (int i = 0; i < 9; ++i) {
        int x = (int)(fog_rand(g) % FOG_MAP_W);
        int y = (int)(fog_rand(g) % FOG_MAP_H);
        if (x == 0 || x == FOG_MAP_W - 1) continue;      // spawn columns stay clear
        if (y == river_y && (x == ford1 || x == ford2)) continue;
        if (x == FOG_CITY_X && y == FOG_CITY_Y) continue;
        if (g->terrain[y][x] == FOG_TERRAIN_RIVER) continue;
        g->terrain[y][x] = FOG_TERRAIN_MOUNTAIN;
    }
    for (int i = 0; i < 12; ++i) {
        int x = (int)(fog_rand(g) % FOG_MAP_W);
        int y = (int)(fog_rand(g) % FOG_MAP_H);
        if (x == 0 || x == FOG_MAP_W - 1) continue;
        if (y == river_y && (x == ford1 || x == ford2)) continue;
        if (x == FOG_CITY_X && y == FOG_CITY_Y) continue;
        if (g->terrain[y][x] != FOG_TERRAIN_PLAIN) continue;
        g->terrain[y][x] = FOG_TERRAIN_FOREST;
    }
}

void fog_map_generate(fog_game_t *g, uint32_t seed) {
    g->seed0 = seed;
    g->seed = seed;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) g->seed = g->seed0 + (uint32_t)attempt;
        map_random(g);
        if (fog_map_connected(g)) return;
    }
    map_builtin(g);
}

// --- Unit helpers ---------------------------------------------------------
fog_unit_t *fog_unit_at(fog_game_t *g, int x, int y) {
    for (int s = 0; s < FOG_SIDE_COUNT; ++s)
        for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
            fog_unit_t *u = &g->units[s][i];
            if (u->alive && u->x == x && u->y == y) return u;
        }
    return NULL;
}

const fog_unit_t *fog_unit_at_const(const fog_game_t *g, int x, int y) {
    for (int s = 0; s < FOG_SIDE_COUNT; ++s)
        for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
            const fog_unit_t *u = &g->units[s][i];
            if (u->alive && u->x == x && u->y == y) return u;
        }
    return NULL;
}

int fog_side_alive(const fog_game_t *g, int side) {
    int n = 0;
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) n += g->units[side][i].alive;
    return n;
}

bool fog_side_done(const fog_game_t *g, int side) {
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
        const fog_unit_t *u = &g->units[side][i];
        if (u->alive && !u->acted) return false;
    }
    return true;
}

// --- Vision ---------------------------------------------------------------
// Bresenham ray; only strictly intermediate cells block (PRD 8.3 endpoint rule).
bool fog_los_clear(const fog_game_t *g, int x0, int y0, int x1, int y1) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x1 > x0 ? 1 : -1;
    int sy = y1 > y0 ? 1 : -1;
    int err = dx - dy;
    int x = x0, y = y0;
    while (x != x1 || y != y1) {
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx)  { err += dx; y += sy; }
        if (x == x1 && y == y1) break;                    // endpoint never blocks
        if (x < 0 || y < 0 || x >= FOG_MAP_W || y >= FOG_MAP_H) return false;
        uint8_t t = g->terrain[y][x];
        if (t == FOG_TERRAIN_MOUNTAIN || t == FOG_TERRAIN_FOREST) return false;
    }
    return true;
}

bool fog_unit_sees(const fog_game_t *g, const fog_unit_t *u, int x, int y) {
    if (!u->alive) return false;
    int r = FOG_CLASS_STATS[u->cls].vision;
    if (g->terrain[u->y][u->x] == FOG_TERRAIN_CITY) r++;  // +1 on city (8.3)
    int d = fog_cheb(u->x, u->y, x, y);
    if (d > r) return false;
    if (!fog_los_clear(g, u->x, u->y, x, y)) return false;
    // Forest concealment: a unit inside a forest is hidden at distance >= 3 (8.1).
    if (g->terrain[y][x] == FOG_TERRAIN_FOREST && d >= 3) return false;
    return true;
}

bool fog_can_see_unit(const fog_game_t *g, int side, int x, int y) {
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i)
        if (fog_unit_sees(g, &g->units[side][i], x, y)) return true;
    return false;
}

void fog_vision_recompute(fog_game_t *g, int side) {
    memset(g->seen[side], 0, sizeof g->seen[side]);
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
        const fog_unit_t *u = &g->units[side][i];
        if (!u->alive) continue;
        int r = FOG_CLASS_STATS[u->cls].vision;
        if (g->terrain[u->y][u->x] == FOG_TERRAIN_CITY) r++;
        for (int y = 0; y < FOG_MAP_H; ++y)
            for (int x = 0; x < FOG_MAP_W; ++x) {
                if (fog_cheb(u->x, u->y, x, y) > r) continue;
                if (fog_los_clear(g, u->x, u->y, x, y)) g->seen[side][y][x] = 1;
            }
    }
    for (int y = 0; y < FOG_MAP_H; ++y)
        for (int x = 0; x < FOG_MAP_W; ++x)
            if (g->seen[side][y][x]) {
                g->explored[side][y][x] = 1;
                g->ghost[side][y][x] = 0;   // fresher observation replaces ghosts
                g->ghost_cls[side][y][x] = 0;
            }
}

// --- Game lifecycle ---------------------------------------------------------
static void place_units(fog_game_t *g) {
    for (int s = 0; s < FOG_SIDE_COUNT; ++s)
        for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
            fog_unit_t *u = &g->units[s][i];
            u->cls = FOG_LINEUP[i];
            u->side = (uint8_t)s;
            u->strength = FOG_CLASS_STATS[u->cls].strength;
            u->x = s == FOG_SIDE_PLAYER ? SPAWNS[i][0] : SPAWNS_MIRROR[i][0];
            u->y = s == FOG_SIDE_PLAYER ? SPAWNS[i][1] : SPAWNS_MIRROR[i][1];
            u->alive = true;
            u->acted = false;
            u->attacked = false;
            u->ap = 0;
        }
}

static void begin_phase(fog_game_t *g, int side) {
    g->phase_side = (uint8_t)side;
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
        fog_unit_t *u = &g->units[side][i];
        u->ap = u->alive ? FOG_AP_PER_TURN : 0;
        u->acted = !u->alive;
        u->attacked = false;
    }
}

void fog_game_start(fog_game_t *g, uint32_t seed) {
    memset(g, 0, sizeof *g);
    fog_map_generate(g, seed);
    place_units(g);
    g->round = 1;
    g->winner = FOG_WIN_NONE;
    g->first_side = (uint8_t)(fog_rand(g) & 1);           // first mover from seed (8.4)
    begin_phase(g, g->first_side);
    g->phase = g->first_side == FOG_SIDE_PLAYER ? FOG_PHASE_SELECT : FOG_PHASE_ENEMY;
    g->selected = 0;
    fog_vision_recompute(g, FOG_SIDE_PLAYER);
    fog_vision_recompute(g, FOG_SIDE_ENEMY);
}

// --- Candidates ---------------------------------------------------------------
// Bearing priority: up, right, down, left (PRD 9.2 item 1).
static int bearing_index(int dx, int dy) {
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    if (ay >= ax) return dy < 0 ? 0 : 2;
    return dx > 0 ? 1 : 3;
}

static int cand_less(const fog_cand_t *a, const fog_cand_t *b, int ux, int uy) {
    int da = fog_cheb(ux, uy, a->x, a->y);
    int db = fog_cheb(ux, uy, b->x, b->y);
    if (da != db) return da < db;
    int ba = bearing_index(a->x - ux, a->y - uy);
    int bb = bearing_index(b->x - ux, b->y - uy);
    if (ba != bb) return ba < bb;
    if (a->y != b->y) return a->y < b->y;
    return a->x < b->x;
}

static void cand_sort(fog_cand_t *list, int n, int ux, int uy) {
    // Stable insertion sort — deterministic and n stays small.
    for (int i = 1; i < n; ++i) {
        fog_cand_t key = list[i];
        int j = i - 1;
        while (j >= 0 && cand_less(&key, &list[j], ux, uy)) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = key;
    }
}

static int move_candidates(const fog_game_t *g, const fog_unit_t *u, fog_cand_t *out) {
    // Bellman-Ford relaxation by AP cost over the 8-neighbourhood: friendly
    // units are passable, enemy units block, destination must be empty (8.4).
    // Path cost never exceeds FOG_AP_PER_TURN, so AP+1 passes suffice.
    uint8_t cost[FOG_MAP_H][FOG_MAP_W];
    memset(cost, 0xFF, sizeof cost);
    cost[u->y][u->x] = 0;
    static const int DX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    static const int DY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
    for (int pass = 0; pass < FOG_AP_PER_TURN; ++pass) {
        bool changed = false;
        for (int y = 0; y < FOG_MAP_H; ++y)
            for (int x = 0; x < FOG_MAP_W; ++x) {
                if (cost[y][x] == 0xFF) continue;
                for (int d = 0; d < 8; ++d) {
                    int nx = x + DX[d], ny = y + DY[d];
                    if (nx < 0 || ny < 0 || nx >= FOG_MAP_W || ny >= FOG_MAP_H) continue;
                    if (!fog_passable(g, nx, ny)) continue;
                    const fog_unit_t *occ = fog_unit_at_const(g, nx, ny);
                    if (occ && occ->side != u->side) continue; // enemy blocks path
                    int step = g->terrain[ny][nx] == FOG_TERRAIN_FOREST
                                 ? FOG_FOREST_MOVE_AP : FOG_MOVE_AP;
                    int nc = cost[y][x] + step;
                    if (nc > u->ap) continue;
                    if ((uint8_t)nc < cost[ny][nx]) {
                        cost[ny][nx] = (uint8_t)nc;
                        changed = true;
                    }
                }
            }
        if (!changed) break;
    }
    int n = 0;
    for (int y = 0; y < FOG_MAP_H; ++y)
        for (int x = 0; x < FOG_MAP_W; ++x) {
            if (cost[y][x] == 0xFF || (x == u->x && y == u->y)) continue;
            if (fog_unit_at_const(g, x, y)) continue;      // destination must be empty
            out[n].kind = FOG_CAND_MOVE;
            out[n].x = (int8_t)x;
            out[n].y = (int8_t)y;
            out[n].target = -1;
            out[n].ap_cost = cost[y][x];
            out[n].damage = 0;
            out[n].target_left = 0;
            n++;
        }
    cand_sort(out, n, u->x, u->y);
    return n;
}

static int attack_candidates(const fog_game_t *g, const fog_unit_t *u, fog_cand_t *out) {
    if (u->attacked || u->ap < FOG_ATTACK_AP) return 0;
    int range = FOG_CLASS_STATS[u->cls].range;
    int n = 0;
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
        const fog_unit_t *t = &g->units[u->side ^ 1][i];
        if (!t->alive) continue;
        if (fog_cheb(u->x, u->y, t->x, t->y) > range) continue;
        if (!fog_unit_sees(g, u, t->x, t->y)) continue;    // attack needs vision (8.5)
        // P0 damage: neutral factors, constant FOG_BASE_ATTACK (PRD ch.18).
        fog_damage_input_t in = {
            .base_attack = FOG_BASE_ATTACK,
            .counter_pm = 1000,
            .terrain_pm = 1000,
            .general_bonus = 0,
        };
        int dmg = fog_damage_resolve(&in);
        out[n].kind = FOG_CAND_ATTACK;
        out[n].x = t->x;
        out[n].y = t->y;
        out[n].target = (int8_t)i;
        out[n].ap_cost = FOG_ATTACK_AP;
        out[n].damage = (uint8_t)dmg;
        out[n].target_left = (uint8_t)(t->strength > dmg ? t->strength - dmg : 0);
        n++;
    }
    cand_sort(out, n, u->x, u->y);
    return n;
}

int fog_candidates(const fog_game_t *g, const fog_unit_t *u, fog_cand_t out[FOG_CAND_MAX]) {
    if (!u->alive || u->acted) {
        out[0].kind = FOG_CAND_STANDBY;                    // nothing left to do
        out[0].x = u->x;
        out[0].y = u->y;
        out[0].target = -1;
        out[0].ap_cost = 0;
        out[0].damage = 0;
        out[0].target_left = 0;
        return 1;
    }
    int n = move_candidates(g, u, out);
    n += attack_candidates(g, u, out + n);
    out[n].kind = FOG_CAND_STANDBY;                        // fixed tail (9.2 item 2)
    out[n].x = u->x;
    out[n].y = u->y;
    out[n].target = -1;
    out[n].ap_cost = 0;
    out[n].damage = 0;
    out[n].target_left = 0;
    return n + 1;
}

// --- Combat (PRD 8.5) -----------------------------------------------------
int fog_damage_resolve(const fog_damage_input_t *in) {
    // Integer per-mille arithmetic; multiply first, floor, then the flat bonus.
    int product = in->base_attack * in->counter_pm * in->terrain_pm;
    int divided = product / (1000 * 1000);
    if (product < 0) return in->general_bonus;             // defensive: bad input
    if (divided < 1) divided = 1;                          // minimum damage floor
    return divided + in->general_bonus;
}

// --- Phase advance ----------------------------------------------------------
static void check_victory(fog_game_t *g) {
    if (g->winner != FOG_WIN_NONE) return;
    int player = fog_side_alive(g, FOG_SIDE_PLAYER);
    int enemy = fog_side_alive(g, FOG_SIDE_ENEMY);
    if (player > 0 && enemy == 0) g->winner = FOG_WIN_PLAYER;
    else if (player == 0 && enemy > 0) g->winner = FOG_WIN_ENEMY;
    else if (player == 0 && enemy == 0) g->winner = FOG_WIN_DRAW;
}

static void advance_if_done(fog_game_t *g) {
    if (g->winner != FOG_WIN_NONE) {
        g->phase = FOG_PHASE_END;
        return;
    }
    if (!fog_side_done(g, g->phase_side)) return;
    int next = g->phase_side ^ 1;
    if (next == g->first_side) {
        g->round++;
        if (g->round > FOG_ROUND_CAP) {                    // 40-round draw backstop
            g->winner = FOG_WIN_DRAW;
            g->phase = FOG_PHASE_END;
            return;
        }
    }
    begin_phase(g, next);
    g->phase = next == FOG_SIDE_PLAYER ? FOG_PHASE_SELECT : FOG_PHASE_ENEMY;
    g->selected = 0;
}

static void vision_recompute_both(fog_game_t *g) {
    fog_vision_recompute(g, FOG_SIDE_PLAYER);
    fog_vision_recompute(g, FOG_SIDE_ENEMY);
}

void fog_standby(fog_game_t *g, int unit_index) {
    fog_unit_t *u = &g->units[g->phase_side][unit_index];
    u->acted = true;
    advance_if_done(g);
}

void fog_apply(fog_game_t *g, int unit_index, const fog_cand_t *cand) {
    fog_unit_t *u = &g->units[g->phase_side][unit_index];
    if (!u->alive || u->acted) return;

    if (cand->kind == FOG_CAND_MOVE) {
        u->x = cand->x;
        u->y = cand->y;
        u->ap = (uint8_t)(u->ap > cand->ap_cost ? u->ap - cand->ap_cost : 0);
        if (u->ap == 0) u->acted = true;
        vision_recompute_both(g);
    } else if (cand->kind == FOG_CAND_ATTACK) {
        fog_unit_t *t = &g->units[g->phase_side ^ 1][cand->target];
        if (!t->alive || t->x != cand->x || t->y != cand->y) return;
        // P0: neutral factors; the full resolver is exercised by host tests.
        fog_damage_input_t in = {
            .base_attack = FOG_BASE_ATTACK,
            .counter_pm = 1000,
            .terrain_pm = 1000,
            .general_bonus = 0,
        };
        int dmg = fog_damage_resolve(&in);
        t->strength = (uint8_t)(t->strength > dmg ? t->strength - dmg : 0);
        u->ap = (uint8_t)(u->ap > FOG_ATTACK_AP ? u->ap - FOG_ATTACK_AP : 0);
        u->attacked = true;
        if (u->ap == 0) u->acted = true;
        if (t->strength == 0) {
            t->alive = false;
            // The observer's ghost stays: they have not seen the cell since.
        }
        // Muzzle flash: the attacker's cell is revealed to the enemy side (8.5).
        int opp = g->phase_side ^ 1;
        g->ghost[opp][u->y][u->x] = 1;
        g->ghost_cls[opp][u->y][u->x] = (uint8_t)(u->cls + 1);
        g->explored[opp][u->y][u->x] = 1;
        check_victory(g);
        vision_recompute_both(g);
    } else {
        u->acted = true;
    }
    advance_if_done(g);
}
