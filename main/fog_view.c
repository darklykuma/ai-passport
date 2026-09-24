// main/fog_view.c — FOG MARCH battlefield rendering.
// Terrain and units draw from the generated sprites (PRD 10.7); bars, ghosts
// (opacity), and highlights are procedural. Fog states use the full-color tile,
// the dim tile, or a hidden tile (PRD 10.3).
#include "fog_view.h"

#include <stdio.h>
#include <string.h>

// --- Embedded sprites (see tools/gen_fog_march_assets.py, PRD 10.7) --------
// EMBED_FILES symbols keep the full file name with '.' turned into '_':
// assets/fog_terrain_plain.rgb565 -> _binary_fog_terrain_plain_rgb565_start.
#define FOG_TILE_BYTES (FOG_CELL_PX * FOG_CELL_PX * 2)
#define FOG_UNIT_BYTES (FOG_CELL_PX * FOG_CELL_PX * 3)

#define FOG_TILE_EXTERN(name) \
    extern const uint8_t name##_start[] asm("_binary_" #name "_rgb565_start")
#define FOG_TILE_DSC(var, name)                                          \
    static const lv_image_dsc_t var = {                                  \
        .header = { .magic = LV_IMAGE_HEADER_MAGIC,                      \
                    .cf = LV_COLOR_FORMAT_RGB565, .flags = 0,            \
                    .w = FOG_CELL_PX, .h = FOG_CELL_PX,                  \
                    .stride = FOG_CELL_PX * 2 },                         \
        .data_size = FOG_TILE_BYTES, .data = name##_start }

FOG_TILE_EXTERN(fog_terrain_plain);
FOG_TILE_EXTERN(fog_terrain_plain_dim);
FOG_TILE_EXTERN(fog_terrain_mountain);
FOG_TILE_EXTERN(fog_terrain_mountain_dim);
FOG_TILE_EXTERN(fog_terrain_forest);
FOG_TILE_EXTERN(fog_terrain_forest_dim);
FOG_TILE_EXTERN(fog_terrain_river);
FOG_TILE_EXTERN(fog_terrain_river_dim);
FOG_TILE_EXTERN(fog_terrain_city);
FOG_TILE_EXTERN(fog_terrain_city_dim);
FOG_TILE_DSC(k_tile_plain, fog_terrain_plain);
FOG_TILE_DSC(k_tile_plain_dim, fog_terrain_plain_dim);
FOG_TILE_DSC(k_tile_mountain, fog_terrain_mountain);
FOG_TILE_DSC(k_tile_mountain_dim, fog_terrain_mountain_dim);
FOG_TILE_DSC(k_tile_forest, fog_terrain_forest);
FOG_TILE_DSC(k_tile_forest_dim, fog_terrain_forest_dim);
FOG_TILE_DSC(k_tile_river, fog_terrain_river);
FOG_TILE_DSC(k_tile_river_dim, fog_terrain_river_dim);
FOG_TILE_DSC(k_tile_city, fog_terrain_city);
FOG_TILE_DSC(k_tile_city_dim, fog_terrain_city_dim);

#define FOG_UNIT_EXTERN(name) \
    extern const uint8_t name##_start[] asm("_binary_" #name "_rgb565a8_start")
#define FOG_UNIT_DSC(var, name)                                         \
    static const lv_image_dsc_t var = {                                 \
        .header = { .magic = LV_IMAGE_HEADER_MAGIC,                     \
                    .cf = LV_COLOR_FORMAT_RGB565A8, .flags = 0,         \
                    .w = FOG_CELL_PX, .h = FOG_CELL_PX,                 \
                    .stride = FOG_CELL_PX * 2 },                        \
        .data_size = FOG_UNIT_BYTES, .data = name##_start }

FOG_UNIT_EXTERN(fog_unit_spear_blue);
FOG_UNIT_EXTERN(fog_unit_archer_blue);
FOG_UNIT_EXTERN(fog_unit_cavalry_blue);
FOG_UNIT_EXTERN(fog_unit_general_blue);
FOG_UNIT_EXTERN(fog_unit_spear_red);
FOG_UNIT_EXTERN(fog_unit_archer_red);
FOG_UNIT_EXTERN(fog_unit_cavalry_red);
FOG_UNIT_EXTERN(fog_unit_general_red);
FOG_UNIT_DSC(k_unit_spear_blue, fog_unit_spear_blue);
FOG_UNIT_DSC(k_unit_archer_blue, fog_unit_archer_blue);
FOG_UNIT_DSC(k_unit_cavalry_blue, fog_unit_cavalry_blue);
FOG_UNIT_DSC(k_unit_general_blue, fog_unit_general_blue);
FOG_UNIT_DSC(k_unit_spear_red, fog_unit_spear_red);
FOG_UNIT_DSC(k_unit_archer_red, fog_unit_archer_red);
FOG_UNIT_DSC(k_unit_cavalry_red, fog_unit_cavalry_red);
FOG_UNIT_DSC(k_unit_general_red, fog_unit_general_red);

static const lv_image_dsc_t *tile_dsc(uint8_t terrain, bool dim) {
    switch (terrain) {
    case FOG_TERRAIN_MOUNTAIN: return dim ? &k_tile_mountain_dim : &k_tile_mountain;
    case FOG_TERRAIN_FOREST:   return dim ? &k_tile_forest_dim : &k_tile_forest;
    case FOG_TERRAIN_RIVER:    return dim ? &k_tile_river_dim : &k_tile_river;
    case FOG_TERRAIN_CITY:     return dim ? &k_tile_city_dim : &k_tile_city;
    default:                   return dim ? &k_tile_plain_dim : &k_tile_plain;
    }
}

// The sprite set ships cavalry ahead of the model lineup (P3 expansion), so
// the table keeps four entries and every descriptor stays referenced.
#define FOG_SPRITE_CLASSES 4

static const lv_image_dsc_t *unit_dsc(int cls, bool enemy) {
    static const lv_image_dsc_t *blue[FOG_SPRITE_CLASSES] = {
        &k_unit_general_blue, &k_unit_spear_blue, &k_unit_archer_blue,
        &k_unit_cavalry_blue,
    };
    static const lv_image_dsc_t *red[FOG_SPRITE_CLASSES] = {
        &k_unit_general_red, &k_unit_spear_red, &k_unit_archer_red,
        &k_unit_cavalry_red,
    };
    return (enemy ? red : blue)[cls % FOG_SPRITE_CLASSES];
}

// PRD 10.5 palette (RGB565 panel; LVGL converts from RGB888).
#define FOG_COLOR_CURSOR lv_color_hex(0xF2E85C)      // selected unit outline
#define FOG_COLOR_MOVE lv_color_hex(0x53C7E8)        // move candidate (cool)
#define FOG_COLOR_ATTACK lv_color_hex(0xF08C3A)      // attack candidate (warm)
#define FOG_CAND_FADE_OPA (LV_OPA_40)                // faded range display
#define FOG_COLOR_BAR_SLOT lv_color_hex(0x2A2E33)
#define FOG_COLOR_BAR_BLUE lv_color_hex(0x53A8F2)
#define FOG_COLOR_BAR_RED lv_color_hex(0xE05A48)
#define FOG_COLOR_BAR_LOW lv_color_hex(0xF08C3A)
#define FOG_TEXT_MAIN lv_color_hex(0xE8E4D8)
#define FOG_TEXT_DIM lv_color_hex(0x9AA3A8)
#define FOG_BG lv_color_hex(0x07090C)
#define FOG_GHOST_OPA (LV_OPA_30)

bool fog_view_build(fog_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = lv_obj_create(NULL);
    if (!v->screen) return false;
    lv_obj_set_style_bg_color(v->screen, FOG_BG, 0);
    lv_obj_set_style_bg_opa(v->screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(v->screen, 0, 0);
    lv_obj_set_style_border_width(v->screen, 0, 0);
    lv_obj_clear_flag(v->screen, LV_OBJ_FLAG_SCROLLABLE);

    LV_FONT_DECLARE(fog_font_16);

    v->status_label = lv_label_create(v->screen);
    lv_obj_set_style_text_font(v->status_label, &fog_font_16, 0);
    lv_obj_set_style_text_color(v->status_label, FOG_TEXT_MAIN, 0);
    lv_obj_set_pos(v->status_label, 6, 5);

    v->battery_label = lv_label_create(v->screen);
    lv_obj_set_style_text_font(v->battery_label, &fog_font_16, 0);
    lv_obj_set_style_text_color(v->battery_label, FOG_TEXT_DIM, 0);
    lv_obj_align(v->battery_label, LV_ALIGN_TOP_RIGHT, -6, 5);

    for (int y = 0; y < FOG_MAP_H; ++y)
        for (int x = 0; x < FOG_MAP_W; ++x) {
            int px = FOG_GRID_X + x * FOG_CELL_PX;
            int py = FOG_GRID_Y + y * FOG_CELL_PX;

            lv_obj_t *tile = lv_image_create(v->screen);
            lv_obj_set_pos(tile, px, py);
            lv_obj_set_size(tile, FOG_CELL_PX, FOG_CELL_PX);
            lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_border_width(tile, 0, 0);
            lv_obj_add_flag(tile, LV_OBJ_FLAG_EVENT_BUBBLE);
            v->cells[y][x] = tile;

            lv_obj_t *unit = lv_image_create(tile);
            lv_obj_set_pos(unit, 0, 0);
            lv_obj_set_size(unit, FOG_CELL_PX, FOG_CELL_PX);
            lv_obj_add_flag(unit, LV_OBJ_FLAG_HIDDEN);
            v->unit_img[y][x] = unit;

            // Strength bars are created lazily in set_bar(): at most six cells
            // ever hold a visible unit, which keeps the LVGL pool small.
        }

    v->hint_label = lv_label_create(v->screen);
    lv_obj_set_style_text_font(v->hint_label, &fog_font_16, 0);
    lv_obj_set_style_text_color(v->hint_label, FOG_TEXT_MAIN, 0);
    lv_obj_set_pos(v->hint_label, 6, 320 - FOG_HINT_H + 6);
    lv_label_set_text(v->hint_label, "");

    return true;
}

static void set_bar(fog_view_t *v, int x, int y, const fog_unit_t *u) {
    lv_obj_t *slot = v->bar_slot[y][x];
    lv_obj_t *fill = v->bar_fill[y][x];
    if (!slot) {                                        // lazy creation
        slot = lv_obj_create(v->cells[y][x]);
        lv_obj_set_pos(slot, 2, FOG_CELL_PX - 5);
        lv_obj_set_size(slot, 22, 3);
        lv_obj_set_style_bg_color(slot, FOG_COLOR_BAR_SLOT, 0);
        lv_obj_set_style_bg_opa(slot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(slot, 0, 0);
        lv_obj_set_style_border_width(slot, 0, 0);
        lv_obj_set_style_pad_all(slot, 0, 0);
        lv_obj_clear_flag(slot, LV_OBJ_FLAG_SCROLLABLE);
        fill = lv_obj_create(slot);
        lv_obj_set_pos(fill, 0, 0);
        lv_obj_set_size(fill, 22, 3);
        lv_obj_set_style_bg_color(fill, FOG_COLOR_BAR_BLUE, 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(fill, 0, 0);
        lv_obj_set_style_border_width(fill, 0, 0);
        lv_obj_set_style_pad_all(fill, 0, 0);
        lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
        v->bar_slot[y][x] = slot;
        v->bar_fill[y][x] = fill;
    }
    lv_obj_clear_flag(slot, LV_OBJ_FLAG_HIDDEN);
    int full = FOG_CLASS_STATS[u->cls].strength;
    int slot_w = full * 22 / 12;
    int fill_w = u->strength * 22 / 12;
    if (fill_w < 1) fill_w = 1;                       // cripples stay visible (10.6)
    lv_obj_set_size(slot, (int32_t)slot_w, 3);
    lv_obj_set_size(fill, (int32_t)fill_w, 3);
    lv_color_t color = u->side == FOG_SIDE_PLAYER ? FOG_COLOR_BAR_BLUE
                                                  : FOG_COLOR_BAR_RED;
    if (u->strength <= FOG_LOW_STRENGTH) color = FOG_COLOR_BAR_LOW;
    lv_obj_set_style_bg_color(fill, color, 0);
    // Keep the absolute-scale bar centered: recenter slot inside the cell.
    lv_obj_set_x(slot, (FOG_CELL_PX - slot_w) / 2);
}

void fog_view_refresh(fog_view_t *v, const fog_render_t *r) {
    const fog_game_t *g = r->game;
    const int player = FOG_SIDE_PLAYER;
    int hl_x = -1, hl_y = -1;
    lv_color_t hl_color = FOG_COLOR_MOVE;
    if (r->cands && r->cand_index >= 0 && r->cand_index < r->cand_count) {
        const fog_cand_t *c = &r->cands[r->cand_index];
        hl_x = c->x;
        hl_y = c->y;
        hl_color = c->kind == FOG_CAND_ATTACK ? FOG_COLOR_ATTACK : FOG_COLOR_MOVE;
        if (c->kind == FOG_CAND_STANDBY) {              // standby marks the unit
            const fog_unit_t *u = &g->units[player][r->selected];
            hl_x = u->x;
            hl_y = u->y;
            hl_color = FOG_COLOR_MOVE;
        }
    }

    // Faded range display (9.2 item 4): every non-highlighted candidate gets
    // a 1 px low-opacity border so the whole action range reads at a glance.
    uint8_t faint[FOG_MAP_H][FOG_MAP_W] = { 0 };      // 0 none, 1 move, 2 attack
    if (r->cands) {
        for (int i = 0; i < r->cand_count; ++i) {
            if (i == r->cand_index) continue;         // bright border elsewhere
            const fog_cand_t *c = &r->cands[i];
            if (c->kind == FOG_CAND_MOVE) faint[c->y][c->x] = 1;
            else if (c->kind == FOG_CAND_ATTACK) faint[c->y][c->x] = 2;
        }
    }

    for (int y = 0; y < FOG_MAP_H; ++y)
        for (int x = 0; x < FOG_MAP_W; ++x) {
            lv_obj_t *tile = v->cells[y][x];
            bool seen = g->seen[player][y][x] != 0;
            bool explored = g->explored[player][y][x] != 0;
            if (!explored) {
                lv_obj_add_flag(tile, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(tile, LV_OBJ_FLAG_HIDDEN);
                lv_image_set_src(tile, tile_dsc(g->terrain[y][x], !seen));
            }

            // Highlight border (candidate / selected unit).
            const fog_unit_t *sel = &g->units[player][r->selected];
            bool is_sel = r->selected >= 0 && sel->alive && sel->x == x && sel->y == y
                          && g->phase != FOG_PHASE_ENEMY;
            if (is_sel && !(hl_x == x && hl_y == y)) {
                lv_obj_set_style_border_color(tile, FOG_COLOR_CURSOR, 0);
                lv_obj_set_style_border_width(tile, 2, 0);
                lv_obj_set_style_border_opa(tile, LV_OPA_COVER, 0);
            } else if (hl_x == x && hl_y == y) {
                lv_obj_set_style_border_color(tile, hl_color, 0);
                lv_obj_set_style_border_width(tile, 2, 0);
                lv_obj_set_style_border_opa(tile, LV_OPA_COVER, 0);
            } else if (faint[y][x]) {
                lv_obj_set_style_border_color(tile, faint[y][x] == 2 ? FOG_COLOR_ATTACK
                                                                     : FOG_COLOR_MOVE, 0);
                lv_obj_set_style_border_width(tile, 1, 0);
                lv_obj_set_style_border_opa(tile, FOG_CAND_FADE_OPA, 0);
            } else {
                lv_obj_set_style_border_width(tile, 0, 0);
            }

            // Unit / ghost layer.
            lv_obj_t *img = v->unit_img[y][x];
            const fog_unit_t *u = fog_unit_at_const(g, x, y);
            bool ghost = g->ghost[player][y][x] != 0;
            if (u && (u->side == player || fog_can_see_unit(g, player, x, y))) {
                lv_image_set_src(img, unit_dsc(u->cls, u->side != player));
                lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_img_opa(img, LV_OPA_COVER, 0);
                set_bar(v, x, y, u);
            } else if (ghost) {
                int cls = g->ghost_cls[player][y][x];
                cls = cls > 0 ? cls - 1 : FOG_CLASS_SPEAR;
                lv_image_set_src(img, unit_dsc(cls, true));
                lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_img_opa(img, FOG_GHOST_OPA, 0);
                if (v->bar_slot[y][x])
                    lv_obj_add_flag(v->bar_slot[y][x], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
                if (v->bar_slot[y][x])
                    lv_obj_add_flag(v->bar_slot[y][x], LV_OBJ_FLAG_HIDDEN);
            }
        }
}

void fog_view_status(fog_view_t *v, const fog_game_t *g, int battery) {
    char text[64];
    if (g->phase == FOG_PHASE_ENEMY) {
        snprintf(text, sizeof text, "回合 %02u  AI 行动中", (unsigned)g->round);
    } else {
        snprintf(text, sizeof text, "回合 %02u  我方 %d  敌方 %d",
                 (unsigned)g->round,
                 fog_side_alive(g, FOG_SIDE_PLAYER),
                 fog_side_alive(g, FOG_SIDE_ENEMY));
    }
    lv_label_set_text(v->status_label, text);

    if (battery >= 0) {
        char bat[12];
        snprintf(bat, sizeof bat, "%d%%", battery);
        lv_label_set_text(v->battery_label, bat);
    } else {
        lv_label_set_text(v->battery_label, "");        // graceful degradation
    }
}

void fog_view_hint(fog_view_t *v, const char *text) {
    lv_label_set_text(v->hint_label, text);
}
