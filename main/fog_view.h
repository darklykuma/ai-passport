// main/fog_view.h — FOG MARCH battlefield rendering (LVGL, partial refresh).
#pragma once

#include "fog_model.h"
#include "lvgl.h"

#define FOG_CELL_PX 26
#define FOG_GRID_X 16
#define FOG_GRID_Y 28
#define FOG_STATUS_H 28
#define FOG_HINT_H 32

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *status_label;
    lv_obj_t *battery_label;
    lv_obj_t *hint_label;
    lv_obj_t *cells[FOG_MAP_H][FOG_MAP_W];        // terrain tile images
    lv_obj_t *unit_img[FOG_MAP_H][FOG_MAP_W];     // unit / ghost images
    lv_obj_t *bar_slot[FOG_MAP_H][FOG_MAP_W];     // strength bar slot
    lv_obj_t *bar_fill[FOG_MAP_H][FOG_MAP_W];     // strength bar fill
} fog_view_t;

typedef struct {
    const fog_game_t *game;
    const fog_cand_t *cands;   // candidate list during FOG_PHASE_ACTION (else NULL)
    int cand_count;
    int cand_index;            // highlighted candidate, -1 = none
    int selected;              // selected unit index, -1 = none
} fog_render_t;

// Builds the battlefield screen (status bar, tile grid, hint bar). Must run
// under the LVGL lock. Returns false on allocation failure.
bool fog_view_build(fog_view_t *v);

// Refreshes every cell from the game + interaction state. Touches only the
// objects whose source/flags changed, so LVGL invalidates just those cells.
void fog_view_refresh(fog_view_t *v, const fog_render_t *r);

// Status line: round, strength counts (or "AI 行动中"), battery percent.
// battery < 0 leaves the battery slot blank (graceful CW2017 degradation).
void fog_view_status(fog_view_t *v, const fog_game_t *g, int battery);

void fog_view_hint(fog_view_t *v, const char *text);
