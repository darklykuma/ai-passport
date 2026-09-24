// main/fog_ai.c — P0 pure-search AI: engage what is visible, else explore.
#include "fog_ai.h"

#include <string.h>

// LOS from the AI's own knowledge: only *observed* mountains/forests block;
// unknown intermediate cells are assumed clear (optimistic, never omniscient).
static bool ai_los_clear(const fog_observation_t *obs, int x0, int y0, int x1, int y1) {
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
        if (x == x1 && y == y1) break;
        if (x < 0 || y < 0 || x >= FOG_MAP_W || y >= FOG_MAP_H) return false;
        if (!obs->terrain_known[y][x]) continue;           // unknown = clear
        uint8_t t = obs->terrain_known[y][x];
        // terrain_known stores the fog_terrain_t value + 1 (0 = unknown).
        if (t - 1 == FOG_TERRAIN_MOUNTAIN || t - 1 == FOG_TERRAIN_FOREST) return false;
    }
    return true;
}

static bool ai_cell_blocked(const fog_observation_t *obs, int x, int y) {
    if (obs->enemy_known[y][x]) return true;               // never step onto enemies
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
        const fog_unit_t *u = &obs->own[i];
        if (u->alive && u->x == x && u->y == y) return true;
    }
    uint8_t t = obs->terrain_known[y][x];
    return t != 0 && t - 1 == FOG_TERRAIN_RIVER;           // known river blocks
}

// BFS from the active unit over 8 neighbours in fixed order. Fills dist and
// parent; deterministic: nearest target ties break by enqueue (scan) order.
static void ai_bfs(const fog_observation_t *obs, const fog_unit_t *u,
                   uint8_t dist[FOG_MAP_H][FOG_MAP_W], int8_t px[FOG_MAP_H][FOG_MAP_W],
                   int8_t py[FOG_MAP_H][FOG_MAP_W]) {
    static const int DX[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
    static const int DY[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
    int8_t queue[FOG_CELLS][2];
    int head = 0, tail = 0;
    memset(dist, 0xFF, FOG_MAP_H * FOG_MAP_W);
    memset(px, -1, FOG_MAP_H * FOG_MAP_W);
    memset(py, -1, FOG_MAP_H * FOG_MAP_W);
    dist[u->y][u->x] = 0;
    queue[tail][0] = u->x;
    queue[tail][1] = u->y;
    tail++;
    while (head < tail) {
        int x = queue[head][0], y = queue[head][1];
        head++;
        for (int d = 0; d < 8; ++d) {
            int nx = x + DX[d], ny = y + DY[d];
            if (nx < 0 || ny < 0 || nx >= FOG_MAP_W || ny >= FOG_MAP_H) continue;
            if (dist[ny][nx] != 0xFF) continue;
            if (ai_cell_blocked(obs, nx, ny)) continue;
            dist[ny][nx] = (uint8_t)(dist[y][x] + 1);
            px[ny][nx] = (int8_t)x;
            py[ny][nx] = (int8_t)y;
            queue[tail][0] = (int8_t)nx;
            queue[tail][1] = (int8_t)ny;
            tail++;
        }
    }
}

// Walk the BFS parent chain from the target back to the unit and stop at the
// farthest cell the remaining AP can pay for. Unknown terrain costs 1,
// observed forest costs 2 (matches the model's move rules where known).
static fog_ai_action_t ai_move_toward(const fog_observation_t *obs, const fog_unit_t *u,
                                      int tx, int ty) {
    fog_ai_action_t act = { .kind = FOG_AI_STANDBY, .x = u->x, .y = u->y,
                            .target = -1, .ap_cost = 0 };
    uint8_t dist[FOG_MAP_H][FOG_MAP_W];
    int8_t px[FOG_MAP_H][FOG_MAP_W], py[FOG_MAP_H][FOG_MAP_W];
    ai_bfs(obs, u, dist, px, py);
    if (dist[ty][tx] == 0xFF) return act;                  // unreachable

    // Collect the path unit -> target, then pay costs from the unit outwards.
    int8_t path_x[FOG_CELLS], path_y[FOG_CELLS];
    int n = 0;
    int cx = tx, cy = ty;
    while (!(cx == u->x && cy == u->y)) {
        path_x[n] = (int8_t)cx;
        path_y[n] = (int8_t)cy;
        n++;
        int nx = px[cy][cx], ny = py[cy][cx];
        cx = nx;
        cy = ny;
    }
    // path[] is target-first; walk from the end (closest to the unit) forward.
    int ap = u->ap;
    int fx = u->x, fy = u->y;
    for (int i = n - 1; i >= 0; --i) {
        int sx = path_x[i], sy = path_y[i];
        uint8_t known = obs->terrain_known[sy][sx];
        int cost = (known != 0 && known - 1 == FOG_TERRAIN_FOREST)
                       ? FOG_FOREST_MOVE_AP : FOG_MOVE_AP;
        if (cost > ap) break;
        ap -= cost;
        fx = sx;
        fy = sy;
    }
    if (fx == u->x && fy == u->y) return act;              // cannot afford one step
    act.kind = FOG_AI_MOVE;
    act.x = (int8_t)fx;
    act.y = (int8_t)fy;
    act.ap_cost = (uint8_t)(u->ap - ap);
    return act;
}

fog_ai_action_t fog_ai_decide(const fog_observation_t *obs, const fog_ai_memory_t *mem) {    (void)mem;
    fog_ai_action_t act = { .kind = FOG_AI_STANDBY, .x = 0, .y = 0,
                            .target = -1, .ap_cost = 0 };
    if (obs->active < 0 || obs->active >= FOG_UNITS_PER_SIDE) return act;
    const fog_unit_t *u = &obs->own[obs->active];
    if (!u->alive || u->acted) return act;
    act.x = u->x;
    act.y = u->y;

    // 1. Engage: visible enemy inside range with clear (known-terrain) LOS.
    if (!u->attacked && u->ap >= FOG_ATTACK_AP) {
        int range = FOG_CLASS_STATS[u->cls].range;
        int best = -1, best_strength = 255;
        for (int y = 0; y < FOG_MAP_H; ++y)
            for (int x = 0; x < FOG_MAP_W; ++x) {
                if (!obs->enemy_known[y][x]) continue;
                if (fog_cheb(u->x, u->y, x, y) > range) continue;
                if (!ai_los_clear(obs, u->x, u->y, x, y)) continue;
                int s = obs->enemy_strength[y][x];
                if (s == 0) s = 99;                        // unknown = assume healthy
                if (s < best_strength) {
                    best_strength = s;
                    best = y * FOG_MAP_W + x;
                }
            }
        if (best >= 0) {
            act.kind = FOG_AI_ATTACK;
            act.x = (int8_t)(best % FOG_MAP_W);
            act.y = (int8_t)(best / FOG_MAP_W);
            act.ap_cost = FOG_ATTACK_AP;
            return act;
        }
    }

    // 2. Search: head for the nearest unexplored cell; when everything the AI
    //    can reach is explored, patrol toward the objective.
    uint8_t dist[FOG_MAP_H][FOG_MAP_W];
    int8_t px[FOG_MAP_H][FOG_MAP_W], py[FOG_MAP_H][FOG_MAP_W];
    ai_bfs(obs, u, dist, px, py);
    int best = -1, best_dist = 255;
    for (int y = 0; y < FOG_MAP_H; ++y)
        for (int x = 0; x < FOG_MAP_W; ++x) {
            if (dist[y][x] == 0xFF) continue;
            if (obs->terrain_known[y][x]) continue;        // already explored
            if (dist[y][x] < best_dist) {
                best_dist = dist[y][x];
                best = y * FOG_MAP_W + x;
            }
        }
    if (best < 0) {
        if (!obs->objective_known) return act;             // nothing to seek
        best = obs->objective_y * FOG_MAP_W + obs->objective_x;
    }
    return ai_move_toward(obs, u, best % FOG_MAP_W, best / FOG_MAP_W);
}

void fog_ai_observe(fog_observation_t *obs, const fog_game_t *g, int side, int active) {
    memset(obs, 0, sizeof *obs);
    const int foe = side ^ 1;
    for (int y = 0; y < FOG_MAP_H; ++y)
        for (int x = 0; x < FOG_MAP_W; ++x)
            obs->terrain_known[y][x] = g->explored[side][y][x]
                ? (uint8_t)(g->terrain[y][x] + 1) : 0;
    memcpy(obs->visible, g->seen[side], sizeof obs->visible);
    for (int i = 0; i < FOG_UNITS_PER_SIDE; ++i) {
        const fog_unit_t *u = &g->units[foe][i];
        if (u->alive && fog_can_see_unit(g, side, u->x, u->y)) {
            obs->enemy_known[u->y][u->x] = 1;
            obs->enemy_strength[u->y][u->x] = u->strength;
        }
    }
    for (int y = 0; y < FOG_MAP_H; ++y)
        for (int x = 0; x < FOG_MAP_W; ++x)
            if (g->ghost[side][y][x]) obs->enemy_known[y][x] = 1;
    if (g->explored[side][FOG_CITY_Y][FOG_CITY_X]) {
        obs->objective_known = 1;
        obs->objective_x = FOG_CITY_X;
        obs->objective_y = FOG_CITY_Y;
    }
    obs->round = g->round;
    memcpy(obs->own, g->units[side], sizeof obs->own);
    obs->active = (int8_t)active;
}
