// main/mafa_view.c — MAFA CHRONICLE screens. Text panels only (PRD 1.2):
// the log is the picture. Per-screen objects: 2-5 labels, far below the
// LVGL pool budget; per-tick churn lives in the app's diff guards.
#include "mafa_view.h"

#include <string.h>

LV_FONT_DECLARE(mafa_font_16);
LV_FONT_DECLARE(mafa_font_20);

#define MAFA_TEXT_MAIN lv_color_hex(0xE8E4D8)
#define MAFA_TEXT_DIM  lv_color_hex(0x9AA3A8)
#define MAFA_TEXT_GOLD lv_color_hex(0xF0C04A)
#define MAFA_BG        lv_color_hex(0x0B0D10)

static lv_obj_t *new_label(lv_obj_t *parent, int x, int y, bool title) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, title ? &mafa_font_20 : &mafa_font_16, 0);
    lv_obj_set_style_text_color(label, title ? MAFA_TEXT_GOLD : MAFA_TEXT_MAIN, 0);
    lv_obj_set_pos(label, x, y);
    lv_label_set_text(label, "");
    return label;
}

static lv_obj_t *base_screen(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, MAFA_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

static void list_style(lv_obj_t *label) {
    lv_obj_set_style_text_line_space(label, 6, 0);
}

lv_obj_t *mafa_view_page_menu(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    v->title_label = new_label(v->screen, 64, 64, true);
    lv_label_set_text(v->title_label, "玛法战纪");
    v->items_label = new_label(v->screen, 76, 150, false);
    list_style(v->items_label);
    return v->screen;
}

lv_obj_t *mafa_view_page_class(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    v->title_label = new_label(v->screen, 64, 28, true);
    lv_label_set_text(v->title_label, "选择职业");
    v->items_label = new_label(v->screen, 24, 80, false);
    list_style(v->items_label);
    v->detail_label = new_label(v->screen, 24, 218, false);
    lv_obj_set_style_text_color(v->detail_label, MAFA_TEXT_DIM, 0);
    return v->screen;
}

lv_obj_t *mafa_view_page_main(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    v->status_label = new_label(v->screen, 8, 4, false);
    v->log_label = new_label(v->screen, 8, 44, false);
    lv_label_set_recolor(v->log_label, true);
    lv_obj_set_style_text_line_space(v->log_label, 6, 0);
    v->menu_label = new_label(v->screen, 8, 210, false);
    lv_obj_set_style_text_line_space(v->menu_label, 2, 0);
    return v->screen;
}

lv_obj_t *mafa_view_page_status(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    v->title_label = new_label(v->screen, 64, 6, true);
    lv_label_set_text(v->title_label, "装备");
    v->items_label = new_label(v->screen, 16, 40, false);
    list_style(v->items_label);
    v->detail_label = new_label(v->screen, 16, 180, false);
    lv_obj_set_style_text_color(v->detail_label, MAFA_TEXT_DIM, 0);
    return v->screen;
}

lv_obj_t *mafa_view_page_backpack(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    v->title_label = new_label(v->screen, 64, 6, true);
    lv_label_set_text(v->title_label, "背包");
    v->items_label = new_label(v->screen, 16, 36, false);
    list_style(v->items_label);
    v->detail_label = new_label(v->screen, 16, 226, false);
    lv_obj_set_style_text_color(v->detail_label, MAFA_TEXT_DIM, 0);
    return v->screen;
}

lv_obj_t *mafa_view_page_store(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    v->title_label = new_label(v->screen, 64, 6, true);
    lv_label_set_text(v->title_label, "药店");
    v->items_label = new_label(v->screen, 24, 60, false);
    list_style(v->items_label);
    return v->screen;
}

lv_obj_t *mafa_view_page_maps(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    v->title_label = new_label(v->screen, 64, 6, true);
    lv_label_set_text(v->title_label, "地图");
    v->items_label = new_label(v->screen, 24, 60, false);
    list_style(v->items_label);
    v->detail_label = new_label(v->screen, 24, 180, false);
    lv_obj_set_style_text_color(v->detail_label, MAFA_TEXT_DIM, 0);
    return v->screen;
}

lv_obj_t *mafa_view_page_settings(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    v->title_label = new_label(v->screen, 64, 6, true);
    lv_label_set_text(v->title_label, "设置");
    v->items_label = new_label(v->screen, 24, 60, false);
    list_style(v->items_label);
    return v->screen;
}

void mafa_view_modal_open(mafa_view_t *v, const char *text) {
    if (!v->modal) {
        v->modal = lv_obj_create(v->screen);
        lv_obj_set_size(v->modal, 216, 140);
        lv_obj_center(v->modal);
        lv_obj_set_style_bg_color(v->modal, lv_color_hex(0x101820), 0);
        lv_obj_set_style_bg_opa(v->modal, LV_OPA_90, 0);
        lv_obj_set_style_border_color(v->modal, lv_color_hex(0x3A4A58), 0);
        lv_obj_set_style_border_width(v->modal, 1, 0);
        lv_obj_set_style_radius(v->modal, 6, 0);
        lv_obj_set_style_pad_all(v->modal, 8, 0);
        lv_obj_clear_flag(v->modal, LV_OBJ_FLAG_SCROLLABLE);
        v->modal_label = new_label(v->modal, 0, 0, false);
        lv_obj_set_style_text_line_space(v->modal_label, 6, 0);
    }
    lv_obj_clear_flag(v->modal, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(v->modal_label, text);
}

void mafa_view_modal_text(mafa_view_t *v, const char *text) {
    if (v->modal_label) lv_label_set_text(v->modal_label, text);
}

void mafa_view_modal_close(mafa_view_t *v) {
    if (v->modal) {
        lv_obj_delete(v->modal);
        v->modal = NULL;
        v->modal_label = NULL;
    }
}
