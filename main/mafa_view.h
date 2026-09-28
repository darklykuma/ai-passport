// main/mafa_view.h — MAFA CHRONICLE screens (LVGL, diff-refreshed by the app).
// Object budget per screen is tiny (2-5 labels); the app owns all cursors,
// text composition, and change-detection. The view only builds pages and
// holds the label pointers.
#pragma once

#include "mafa_model.h"
#include "lvgl.h"

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *title_label;     // menu / class pages
    lv_obj_t *items_label;     // list pages (menu, class, store, maps, settings)
    lv_obj_t *detail_label;    // class blurbs / backpack compare line
    lv_obj_t *status_label;    // main page: player + monster line
    lv_obj_t *log_label;       // main page: 6-line combat log (recolor on)
    lv_obj_t *menu_label;      // main page: action menu
    lv_obj_t *modal;           // prompt overlay panel, NULL when closed
    lv_obj_t *modal_label;
} mafa_view_t;

/* Each builder creates a fresh screen (the app deletes the previous one)
 * and stores the label pointers the app needs into *v. */
lv_obj_t *mafa_view_page_menu(mafa_view_t *v);
lv_obj_t *mafa_view_page_class(mafa_view_t *v);
lv_obj_t *mafa_view_page_main(mafa_view_t *v);
lv_obj_t *mafa_view_page_backpack(mafa_view_t *v);
lv_obj_t *mafa_view_page_store(mafa_view_t *v);
lv_obj_t *mafa_view_page_maps(mafa_view_t *v);
lv_obj_t *mafa_view_page_settings(mafa_view_t *v);

/* Prompt overlay (boss encounter / full backpack) on the current screen. */
void mafa_view_modal_open(mafa_view_t *v, const char *text);
void mafa_view_modal_text(mafa_view_t *v, const char *text);
void mafa_view_modal_close(mafa_view_t *v);
