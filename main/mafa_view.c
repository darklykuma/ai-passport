// main/mafa_view.c — MAFA CHRONICLE screens. Text panels dressed in legend
// chrome (PRD 1.2, v0.7): gold header band with HP/MP bars, an enemy strip
// with its own HP bar, the framed combat log, and a bottom action bar. The
// log is still the picture — chrome only frames it. All objects are static
// per screen (about a dozen on the main page) and the app updates them in
// place behind diff guards. Vertical budget at font16 line height 19:
// header 62 + strip 36 + log 140 + action bar 82 = 320.
#include "mafa_view.h"

#include <string.h>

LV_FONT_DECLARE(mafa_font_16);
LV_FONT_DECLARE(mafa_font_20);

#define MAFA_TEXT_MAIN   lv_color_hex(0xE8E4D8)
#define MAFA_TEXT_DIM    lv_color_hex(0x9AA3A8)
#define MAFA_TEXT_GOLD   lv_color_hex(0xF0C04A)
#define MAFA_BG          lv_color_hex(0x0B0D10)
#define MAFA_HEADER_BG   lv_color_hex(0x10151C)
#define MAFA_STRIP_BG    lv_color_hex(0x0D1116)
#define MAFA_PANEL_BG    lv_color_hex(0x0E1319)
#define MAFA_PANEL_EDGE  lv_color_hex(0x2C3A48)
#define MAFA_EDGE_GOLD   lv_color_hex(0x8A6A30)
#define MAFA_BAR_TRACK   lv_color_hex(0x1A2129)
#define MAFA_HP_GREEN    lv_color_hex(0x5FC85F)
#define MAFA_MP_BLUE     lv_color_hex(0x4FA8F2)
#define MAFA_ENEMY_RED   lv_color_hex(0xE05A48)

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

/* Static chrome panel: flat fill, thin border, no interaction, no scroll. */
static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h,
                       lv_color_t bg, lv_color_t edge) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, bg, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, edge, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_radius(p, 4, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

/* Gold-topped heading band for list pages. */
static void title_band(lv_obj_t *scr, const char *text) {
    lv_obj_t *band = panel(scr, 0, 0, 240, 34, MAFA_HEADER_BG, MAFA_EDGE_GOLD);
    lv_obj_set_style_radius(band, 0, 0);
    lv_obj_set_style_border_side(band, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_t *label = new_label(band, 0, 5, true);
    lv_obj_set_width(label, 240);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);
}

static lv_obj_t *bar(lv_obj_t *parent, int x, int y, int w, int h,
                     lv_color_t fill) {
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_bar_set_range(b, 0, 100);
    lv_bar_set_value(b, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(b, MAFA_BAR_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(b, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, fill, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(b, 1, LV_PART_INDICATOR);
    return b;
}

/* List pages draw per-row colors through recolor spans (design docs
 * docs/design/mafa 07-11: quality-colored items, dim empty/learned/locked
 * rows, gold currency), so every list label carries recolor. */
static void list_style(lv_obj_t *label) {
    lv_label_set_recolor(label, true);
    lv_obj_set_style_text_line_space(label, 6, 0);
    /* Cursor rows pair ＞ with 　 (full-width space) as the unmarked
     * filler — both exactly 16 px in mafa_font_16, so the name column
     * stays put while the cursor moves; two ASCII spaces are 8 px and
     * made every row shift sideways on UP/DOWN. */
}

/* Main page chrome: header band, enemy strip, framed log, action bar. */
lv_obj_t *mafa_view_page_main(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();

    /* Header band: map + boss progress, level/battery/gold, HP/MP bars. */
    lv_obj_t *header = panel(v->screen, 0, 0, 240, 62, MAFA_HEADER_BG,
                             MAFA_EDGE_GOLD);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    v->map_label = new_label(header, 10, 6, true);
    lv_label_set_recolor(v->map_label, true);
    /* Info column, TWO lines, right-aligned (the bar numbers now live on
     * the bars, so this zone is bar-free): worst lines "Lv.14 100%" 73px
     * (the percent is XP toward the next level; the cap shows bare
     * "Lv.15") / "金65535" 61px in the 142px box — nothing collides. */
    v->info_label = new_label(header, 92, 6, false);
    lv_label_set_recolor(v->info_label, true);
    lv_obj_set_style_text_color(v->info_label, MAFA_TEXT_DIM, 0);
    lv_obj_set_width(v->info_label, 142);
    lv_obj_set_style_text_align(v->info_label, LV_TEXT_ALIGN_RIGHT, 0);
    /* Bars span 10..140; the current-value number is drawn ON the bar's
     * right end (creation order keeps the label on top; the 19px text line
     * bleeds a few px past the 10px bar onto the dark background). */
    v->hp_bar = bar(header, 10, 33, 130, 10, MAFA_HP_GREEN);
    v->hp_text = new_label(header, 70, 28, false);
    lv_obj_set_style_text_color(v->hp_text, MAFA_TEXT_MAIN, 0);
    lv_obj_set_width(v->hp_text, 66);
    lv_obj_set_style_text_align(v->hp_text, LV_TEXT_ALIGN_RIGHT, 0);
    v->mp_bar = bar(header, 10, 48, 130, 7, MAFA_MP_BLUE);
    v->mp_text = new_label(header, 70, 44, false);
    lv_obj_set_style_text_color(v->mp_text, MAFA_TEXT_MAIN, 0);
    lv_obj_set_width(v->mp_text, 66);
    lv_obj_set_style_text_align(v->mp_text, LV_TEXT_ALIGN_RIGHT, 0);

    /* Enemy strip: ▶mob name (+ ×N pack count; gold when boss) + monster
     * HP bar. The label is created first and never width-clamped, so the
     * later-created opaque bar draws over anything past its x: the worst
     * strip text "▶祖玛弓箭手×3" is ▶16 + 5×16 + ×16 + digit 9 = 121px,
     * ending at x=131 — the bar starts at 134 to stay clear of it. */
    lv_obj_t *strip = panel(v->screen, 0, 62, 240, 36, MAFA_STRIP_BG,
                            MAFA_PANEL_EDGE);
    lv_obj_set_style_radius(strip, 0, 0);
    lv_obj_set_style_border_side(strip, LV_BORDER_SIDE_BOTTOM, 0);
    v->enemy_label = new_label(strip, 10, 9, false);
    lv_label_set_recolor(v->enemy_label, true);
    v->enemy_bar = bar(strip, 134, 13, 98, 9, MAFA_ENEMY_RED);

    /* Framed combat log; the gold caption rides the top border like a
     * fieldset legend (opaque bg punches out the border line). */
    lv_obj_t *logbox = panel(v->screen, 4, 98, 232, 140, MAFA_PANEL_BG,
                             MAFA_PANEL_EDGE);
    lv_obj_t *caption = new_label(v->screen, 12, 89, false);
    lv_obj_set_style_text_color(caption, MAFA_TEXT_GOLD, 0);
    lv_obj_set_style_bg_color(caption, MAFA_BG, 0);
    lv_obj_set_style_bg_opa(caption, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(caption, 4, 0);
    lv_label_set_text(caption, "战况");
    v->log_label = new_label(logbox, 10, 9, false);
    lv_label_set_recolor(v->log_label, true);
    lv_obj_set_style_text_line_space(v->log_label, 2, 0);

    /* Bottom action bar: 2×4 grid at font 16 (1.76 plan: the 技能 cell makes
     * seven). Four font-20 columns cannot fit 240px (4×(20 marker + 40 name)
     * alone = 240), so the bar drops to the list-page size and the log keeps
     * its six lines; cursor alignment still comes from the full-width
     * markers the app composes. The 8th cell stays blank (future pet page). */
    lv_obj_t *menubar = panel(v->screen, 0, 238, 240, 82, lv_color_hex(0x0C0F13),
                              MAFA_EDGE_GOLD);
    lv_obj_set_style_radius(menubar, 0, 0);
    lv_obj_set_style_border_side(menubar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(menubar, 2, 0);
    v->menu_label = new_label(menubar, 12, 13, false);
    lv_label_set_recolor(v->menu_label, true);
    lv_obj_set_style_text_line_space(v->menu_label, 12, 0);
    return v->screen;
}

lv_obj_t *mafa_view_page_menu(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    lv_obj_t *logo = panel(v->screen, 30, 44, 180, 96, MAFA_PANEL_BG,
                           MAFA_EDGE_GOLD);
    lv_obj_set_style_radius(logo, 8, 0);
    lv_obj_set_style_border_width(logo, 2, 0);
    v->title_label = new_label(logo, 0, 20, true);
    lv_obj_set_width(v->title_label, 180);
    lv_obj_set_style_text_align(v->title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(v->title_label, 8, 0);
    lv_label_set_text(v->title_label, "玛法战纪");
    lv_obj_t *sub = new_label(logo, 0, 56, false);
    lv_obj_set_style_text_color(sub, MAFA_TEXT_DIM, 0);
    lv_obj_set_width(sub, 180);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(sub, "「文字传奇」");
    v->items_label = new_label(v->screen, 80, 180, false);
    list_style(v->items_label);
    return v->screen;
}

lv_obj_t *mafa_view_page_class(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    title_band(v->screen, "选择职业");
    lv_obj_t *list = panel(v->screen, 6, 42, 228, 150, MAFA_PANEL_BG,
                           MAFA_PANEL_EDGE);
    v->items_label = new_label(list, 12, 10, false);
    list_style(v->items_label);
    lv_obj_t *detail = panel(v->screen, 6, 200, 228, 112, MAFA_PANEL_BG,
                             MAFA_PANEL_EDGE);
    v->detail_label = new_label(detail, 12, 10, false);
    lv_obj_set_style_text_color(v->detail_label, MAFA_TEXT_DIM, 0);
    return v->screen;
}

lv_obj_t *mafa_view_page_status(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    title_band(v->screen, "装备");
    /* 1.76 paper doll (design 07): 8 slot rows in a tightened list (line
     * space 2: rows end at 176px), then the v1.6 four-line detail — stats,
     * pools, potions, and the OK-unequip hint (84px in the 90px panel). */
    lv_obj_t *list = panel(v->screen, 6, 42, 228, 184, MAFA_PANEL_BG,
                           MAFA_PANEL_EDGE);
    v->items_label = new_label(list, 12, 10, false);
    list_style(v->items_label);
    lv_obj_set_style_text_line_space(v->items_label, 2, 0);
    lv_obj_t *detail = panel(v->screen, 6, 230, 228, 90, MAFA_PANEL_BG,
                             MAFA_PANEL_EDGE);
    v->detail_label = new_label(detail, 8, 8, false);
    lv_obj_set_style_text_color(v->detail_label, MAFA_TEXT_DIM, 0);
    return v->screen;
}

lv_obj_t *mafa_view_page_skills(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    title_band(v->screen, "技能");
    /* Seven class skills (design 17): 7 rows at pitch 24 in a 190px list,
     * then the three dim explainer lines. */
    lv_obj_t *list = panel(v->screen, 6, 42, 228, 190, MAFA_PANEL_BG,
                           MAFA_PANEL_EDGE);
    v->items_label = new_label(list, 12, 10, false);
    list_style(v->items_label);
    lv_obj_t *detail = panel(v->screen, 6, 240, 228, 72, MAFA_PANEL_BG,
                             MAFA_PANEL_EDGE);
    v->detail_label = new_label(detail, 8, 8, false);
    lv_obj_set_style_text_color(v->detail_label, MAFA_TEXT_DIM, 0);
    return v->screen;
}

lv_obj_t *mafa_view_page_backpack(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    title_band(v->screen, "背包");
    /* The device font's real line height is 20 (the mockups approximate it
     * at 19): 8 rows at pitch 20+4 need 188px, so the list keeps 200 for a
     * tight pad — the v1.5 detail grows to three rows (五项对比 + 金币). */
    lv_obj_t *list = panel(v->screen, 6, 42, 228, 200, MAFA_PANEL_BG,
                           MAFA_PANEL_EDGE);
    v->items_label = new_label(list, 12, 10, false);
    list_style(v->items_label);
    /* 8 rows need a tighter lead than the shared 6. */
    lv_obj_set_style_text_line_space(v->items_label, 4, 0);
    lv_obj_t *detail = panel(v->screen, 6, 246, 228, 74, MAFA_PANEL_BG,
                             MAFA_PANEL_EDGE);
    v->detail_label = new_label(detail, 8, 8, false);
    lv_obj_set_style_text_color(v->detail_label, MAFA_TEXT_DIM, 0);
    return v->screen;
}

lv_obj_t *mafa_view_page_store(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    /* 商店 (1.76 plan): two potions + three class books + a plain 返回
     * row; with a three-book shelf the old 药店 title stopped being
     * honest. The back row frees long-OK on potion rows for hold-to-buy. */
    title_band(v->screen, "商店");
    lv_obj_t *list = panel(v->screen, 6, 42, 228, 270, MAFA_PANEL_BG,
                           MAFA_PANEL_EDGE);
    v->items_label = new_label(list, 12, 10, false);
    list_style(v->items_label);
    return v->screen;
}

lv_obj_t *mafa_view_page_maps(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    title_band(v->screen, "地图");
    /* Eight rows (safe zone + seven combat maps, design 10) in the 208px
     * list; the floor picker shares it (7 floors + 返回). */
    lv_obj_t *list = panel(v->screen, 6, 42, 228, 208, MAFA_PANEL_BG,
                           MAFA_PANEL_EDGE);
    v->items_label = new_label(list, 12, 10, false);
    list_style(v->items_label);
    lv_obj_set_style_text_line_space(v->items_label, 4, 0);
    lv_obj_t *detail = panel(v->screen, 6, 256, 228, 56, MAFA_PANEL_BG,
                             MAFA_PANEL_EDGE);
    v->detail_label = new_label(detail, 8, 8, false);
    lv_obj_set_style_text_color(v->detail_label, MAFA_TEXT_DIM, 0);
    return v->screen;
}

lv_obj_t *mafa_view_page_settings(mafa_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = base_screen();
    title_band(v->screen, "设置");
    lv_obj_t *list = panel(v->screen, 6, 42, 228, 270, MAFA_PANEL_BG,
                           MAFA_PANEL_EDGE);
    v->items_label = new_label(list, 12, 10, false);
    list_style(v->items_label);
    return v->screen;
}

void mafa_view_modal_open(mafa_view_t *v, const char *text) {
    if (!v->modal) {
        v->modal = lv_obj_create(v->screen);
        lv_obj_set_size(v->modal, 224, 150);
        lv_obj_center(v->modal);
        lv_obj_set_style_bg_color(v->modal, lv_color_hex(0x101820), 0);
        lv_obj_set_style_bg_opa(v->modal, LV_OPA_90, 0);
        lv_obj_set_style_border_color(v->modal, MAFA_EDGE_GOLD, 0);
        lv_obj_set_style_border_width(v->modal, 1, 0);
        lv_obj_set_style_radius(v->modal, 6, 0);
        lv_obj_set_style_pad_all(v->modal, 8, 0);
        lv_obj_clear_flag(v->modal, LV_OBJ_FLAG_SCROLLABLE);
        v->modal_label = new_label(v->modal, 0, 0, false);
        lv_label_set_recolor(v->modal_label, true);
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
