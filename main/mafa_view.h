// main/mafa_view.h — MAFA CHRONICLE screens (LVGL, diff-refreshed by the
// app). PRD 1.2 keeps "the log is the picture"; v0.7 dresses it in panel
// chrome: gold title band, HP/MP bars, enemy strip with its own HP bar,
// framed log, bottom action bar. Every object is created once per screen
// and updated in place (the FOG MARCH pool lesson); the app owns cursors,
// text composition, and change-detection. The view only builds pages and
// holds the widget pointers.
#pragma once

#include "mafa_model.h"
#include "lvgl.h"

typedef struct {
    lv_obj_t *screen;
    /* list pages (menu, class, status, backpack, store, maps, settings) */
    lv_obj_t *title_label;     // band heading / menu-page logo
    lv_obj_t *items_label;     // list content
    lv_obj_t *detail_label;    // class blurbs / backpack compare / map name
    /* main page: header band */
    lv_obj_t *map_label;       // gold: map name + boss kill progress
    lv_obj_t *info_label;      // dim, right-aligned: level / battery / gold
    lv_obj_t *hp_bar, *hp_text;
    lv_obj_t *mp_bar, *mp_text;
    /* main page: enemy strip */
    lv_obj_t *enemy_label;     // recolor: ▶mob / gold boss / dim 挂机中
    lv_obj_t *enemy_bar;       // monster HP (red), empty while idle
    /* main page: framed log + bottom action bar */
    lv_obj_t *log_label;       // 6-line combat log (recolor on)
    lv_obj_t *menu_label;      // 2×3 action menu, font 20
    lv_obj_t *modal;           // prompt overlay panel, NULL when closed
    lv_obj_t *modal_label;
} mafa_view_t;

/* Each builder creates a fresh screen (the app deletes the previous one)
 * and stores the widget pointers the app needs into *v. */
lv_obj_t *mafa_view_page_menu(mafa_view_t *v);
lv_obj_t *mafa_view_page_class(mafa_view_t *v);
lv_obj_t *mafa_view_page_main(mafa_view_t *v);
lv_obj_t *mafa_view_page_status(mafa_view_t *v);
lv_obj_t *mafa_view_page_backpack(mafa_view_t *v);
lv_obj_t *mafa_view_page_store(mafa_view_t *v);
lv_obj_t *mafa_view_page_maps(mafa_view_t *v);
lv_obj_t *mafa_view_page_settings(mafa_view_t *v);

/* Prompt overlay (boss encounter / full backpack) on the current screen. */
void mafa_view_modal_open(mafa_view_t *v, const char *text);
void mafa_view_modal_text(mafa_view_t *v, const char *text);
void mafa_view_modal_close(mafa_view_t *v);
