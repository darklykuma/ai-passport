// rift_view.c — RIFT BOARD screens, pixel-matched to docs/design/rift-board
// (indigo night arena + hextech cyan; gold reserved for title band, winners,
// trophy). All objects are static per screen; refresh_* updates labels in
// place. Vertical budget: band 34 / content 254 / footer 32 = 320.
#include "rift_view.h"

#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(rift_font_16);
LV_FONT_DECLARE(rift_font_20);

#define C_BG        lv_color_hex(0x0A0C16)
#define C_HEADER_BG lv_color_hex(0x11172A)
#define C_STRIP_BG  lv_color_hex(0x0D1222)
#define C_PANEL_BG  lv_color_hex(0x0F1426)
#define C_PANEL_BG2 lv_color_hex(0x0D111F)
#define C_EDGE      lv_color_hex(0x2A3858)
#define C_EDGE_GOLD lv_color_hex(0x7A622E)
#define C_TRACK     lv_color_hex(0x181F33)
#define C_GOLD      lv_color_hex(0xE6C05E)
#define C_CYAN      lv_color_hex(0x46C8E8)
#define C_MAIN      lv_color_hex(0xDAE0EA)
#define C_DIM       lv_color_hex(0x8490A6)
#define C_BLUE      lv_color_hex(0x5496F2)
#define C_RED       lv_color_hex(0xE05844)
#define C_LIVE_RED  lv_color_hex(0xF25A40)
#define C_GREEN     lv_color_hex(0x5FC86A)
#define C_RED_EDGE  lv_color_hex(0x6E3028)

static lv_obj_t *label(lv_obj_t *parent, int x, int y, const lv_font_t *f,
                       lv_color_t c) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, "");
    return l;
}

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

static lv_obj_t *screen(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

static void band(lv_obj_t *scr, lv_obj_t **title, lv_obj_t **right,
                 lv_color_t edge) {
    lv_obj_t *b = panel(scr, 0, 0, 240, 34, C_HEADER_BG, edge);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
    *title = label(b, 10, 5, &rift_font_20, C_GOLD);
    *right = label(b, 120, 8, &rift_font_16, C_DIM);
    lv_obj_set_width(*right, 110);
    lv_obj_set_style_text_align(*right, LV_TEXT_ALIGN_RIGHT, 0);
}

static void footer(lv_obj_t *scr, lv_obj_t **f, const char *s) {
    lv_obj_t *p = panel(scr, 0, 288, 240, 32, C_PANEL_BG2, C_EDGE);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_border_side(p, LV_BORDER_SIDE_TOP, 0);
    *f = label(p, 10, 7, &rift_font_16, C_DIM);
    lv_label_set_text(*f, s);
}

/* --- boot ------------------------------------------------------------------ */

static lv_obj_t *s_boot_scr, *s_boot_line, *s_boot_bar, *s_boot_pct;

void rift_view_boot(void) {
    s_boot_scr = screen();
    lv_obj_t *t, *r;
    band(s_boot_scr, &t, &r, C_EDGE_GOLD);
    lv_label_set_text(t, "RIFT BOARD");
    lv_label_set_text(r, "v1.0");
    /* trophy silhouette: flat polygons only */
    lv_obj_t *cup = lv_obj_create(s_boot_scr);
    lv_obj_set_pos(cup, 96, 74);
    lv_obj_set_size(cup, 48, 48);
    lv_obj_set_style_bg_color(cup, C_GOLD, 0);
    lv_obj_set_style_bg_opa(cup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cup, 0, 0);
    lv_obj_set_style_radius(cup, 6, 0);
    lv_obj_clear_flag(cup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *sub = label(s_boot_scr, 0, 136, &rift_font_20, C_MAIN);
    lv_obj_set_width(sub, 240);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(sub, "S16 赛场看板");
    lv_obj_t *sub2 = label(s_boot_scr, 0, 164, &rift_font_16, C_DIM);
    lv_obj_set_width(sub2, 240);
    lv_obj_set_style_text_align(sub2, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(sub2, "2026 全球总决赛 · 实时赛况");
    s_boot_line = label(s_boot_scr, 24, 216, &rift_font_16, C_DIM);
    s_boot_bar = lv_bar_create(s_boot_scr);
    lv_obj_set_pos(s_boot_bar, 34, 276);
    lv_obj_set_size(s_boot_bar, 148, 8);
    lv_bar_set_range(s_boot_bar, 0, 100);
    lv_obj_set_style_bg_color(s_boot_bar, C_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_boot_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_boot_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_boot_bar, C_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_boot_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    s_boot_pct = label(s_boot_scr, 190, 272, &rift_font_16, C_DIM);
    lv_obj_t *f;
    footer(s_boot_scr, &f, "数据源 lolesports · 非官方接口");
    lv_scr_load(s_boot_scr);
}

void rift_view_boot_progress(int pct, const char *line) {
    lv_label_set_text(s_boot_line, line);
    lv_bar_set_value(s_boot_bar, pct, LV_ANIM_OFF);
    char b[8];
    snprintf(b, sizeof b, "%d%%", pct);
    lv_label_set_text(s_boot_pct, b);
}

/* --- home ------------------------------------------------------------------ */

#define HOME_ROWS 4   /* date bar + up to 2 matches + next-day teaser */

static lv_obj_t *s_home_scr;
static struct {
    lv_obj_t *date, *count;
    lv_obj_t *card[3];          /* up to 2 matches + teaser */
    struct {
        lv_obj_t *time, *c1, *score, *c2, *state, *sub;
    } row[3];
} s_home;

void rift_view_home_create(void) {
    s_home_scr = screen();
    lv_obj_t *t, *r;
    band(s_home_scr, &t, &r, C_EDGE_GOLD);
    lv_label_set_text(t, "S16 赛场看板");
    s_home.count = r;
    lv_obj_t *bar = panel(s_home_scr, 0, 34, 240, 30, C_STRIP_BG, C_EDGE);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    s_home.date = label(bar, 0, 6, &rift_font_16, C_MAIN);
    lv_obj_set_width(s_home.date, 240);
    lv_obj_set_style_text_align(s_home.date, LV_TEXT_ALIGN_CENTER, 0);
    for (int i = 0; i < 3; i++) {
        int y = 70 + i * 58;
        s_home.card[i] = panel(s_home_scr, 4, y, 232, i < 2 ? 53 : 29,
                               C_PANEL_BG, C_EDGE);
        s_home.row[i].time = label(s_home.card[i], 14, i < 2 ? 5 : 6,
                                   &rift_font_16, C_DIM);
        s_home.row[i].c1 = label(s_home.card[i], 40, i < 2 ? 3 : 6,
                                 &rift_font_20, C_MAIN);
        s_home.row[i].score = label(s_home.card[i], 0, i < 2 ? 3 : 6,
                                    &rift_font_20, C_GOLD);
        lv_obj_set_width(s_home.row[i].score, 240);
        lv_obj_set_style_text_align(s_home.row[i].score, LV_TEXT_ALIGN_CENTER,
                                    0);
        s_home.row[i].c2 = label(s_home.card[i], 236 - 42, i < 2 ? 3 : 6,
                                 &rift_font_20, C_GOLD);
        s_home.row[i].state = label(s_home.card[i], 0, i < 2 ? 6 : 6,
                                    &rift_font_16, C_DIM);
        s_home.row[i].sub = label(s_home.card[i], 14, i < 2 ? 31 : 6,
                                  &rift_font_16, C_DIM);
    }
    lv_obj_t *f;
    footer(s_home_scr, &f, "OK进入 日期行切日 长按=数据页");
}

void rift_view_home_refresh(const rift_match_t *ms, const rift_day_t *days,
                            int ndays, int cur_day, int cur_row, int rows) {
    (void)ndays;
    const rift_day_t *d = NULL;
    for (int i = 0; i < ndays; i++)
        if (days[i].day == cur_day) d = &days[i];
    char b[40];
    rift_fmt_date_label(cur_day, b, sizeof b);
    lv_label_set_text(s_home.date, b);

    for (int i = 0; i < 3; i++) {
        bool focused = cur_row == i;
        lv_obj_t *card = s_home.card[i];
        lv_obj_set_style_border_color(
            card, focused ? C_CYAN : C_EDGE, 0);
        lv_label_set_text(s_home.row[i].time, "");
        lv_label_set_text(s_home.row[i].c1, "");
        lv_label_set_text(s_home.row[i].score, "");
        lv_label_set_text(s_home.row[i].c2, "");
        lv_label_set_text(s_home.row[i].state, "");
        lv_label_set_text(s_home.row[i].sub, "");
    }

    /* count chip in the band */
    if (d) {
        snprintf(b, sizeof b, "%d场", d->count);
        lv_label_set_text(s_home.count, b);
    } else {
        lv_label_set_text(s_home.count, rows == -2 ? "未公布" : "0场");
    }

    if (rows == -1) {           /* rest day: no events on this date */
        lv_obj_t *c = s_home.card[1];
        lv_label_set_text(s_home.row[1].time, "今日休赛");
        lv_label_set_text(s_home.row[1].sub, "本场次日无比赛,OK看下一场");
        lv_obj_set_style_border_color(c, cur_row == -1 ? C_CYAN : C_EDGE, 0);
        return;
    }
    if (rows == -2) {           /* inside event calendar, day unpublished */
        lv_label_set_text(s_home.row[1].time, "赛程未公布");
        lv_label_set_text(s_home.row[1].sub, "抽签或赛程更新后显示");
        return;
    }
    if (!d) return;

    for (int i = 0; i < d->count && i < 2; i++) {
        const rift_match_t *m = &ms[d->first + i];
        rift_fmt_hhmm(m->start, b, sizeof b);
        lv_label_set_text(s_home.row[i].time, b);
        lv_label_set_text(s_home.row[i].c1, m->code1);
        lv_obj_set_style_text_color(s_home.row[i].c2,
                                    m->state == RIFT_ST_DONE ? C_GOLD : C_MAIN,
                                    0);
        lv_label_set_text(s_home.row[i].c2, m->code2);
        if (m->state == RIFT_ST_TBD) {
            lv_label_set_text(s_home.row[i].score, "待定");
            lv_obj_set_style_text_color(s_home.row[i].score, C_DIM, 0);
            lv_label_set_text(s_home.row[i].sub, "瑞士轮对阵待公布");
        } else if (m->state == RIFT_ST_DONE) {
            snprintf(b, sizeof b, "%d:%d", m->wins1, m->wins2);
            lv_label_set_text(s_home.row[i].score, b);
            lv_obj_set_style_text_color(s_home.row[i].score, C_GOLD, 0);
            snprintf(b, sizeof b, "%s · BO%d · 已结束", m->stage, m->bo);
            lv_label_set_text(s_home.row[i].sub, b);
        } else if (m->state == RIFT_ST_LIVE) {
            snprintf(b, sizeof b, "%d:%d", m->wins1, m->wins2);
            lv_label_set_text(s_home.row[i].score, b);
            lv_obj_set_style_text_color(s_home.row[i].score, C_MAIN, 0);
            lv_obj_t *st = s_home.row[i].state;
            lv_obj_set_pos(st, 196, 6);
            lv_obj_set_style_text_color(st, C_LIVE_RED, 0);
            lv_label_set_text(st, "LIVE");
            lv_label_set_text(s_home.row[i].sub, "进行中 · OK看实时");
        } else {
            lv_obj_set_style_text_color(s_home.row[i].score, C_MAIN, 0);
            lv_label_set_text(s_home.row[i].score, "-:-");
            snprintf(b, sizeof b, "%s · BO%d · 未开始", m->stage, m->bo);
            lv_label_set_text(s_home.row[i].sub, b);
        }
    }
    /* teaser: first match of the next day with events */
    int nd = -1;
    for (int i = 0; i < ndays; i++)
        if (days[i].day > cur_day) { nd = i; break; }
    if (nd >= 0) {
        const rift_match_t *m = &ms[days[nd].first];
        rift_fmt_hhmm(m->start, b, sizeof b);
        char t2[48];
        snprintf(t2, sizeof t2, "次日 %s %s vs %s", b + 0, m->code1, m->code2);
        /* "HH:MM" is all we want; strip to time text */
        lv_label_set_text(s_home.row[2].time, t2);
        lv_obj_set_style_text_color(s_home.row[2].time,
                                    cur_row == 2 ? C_CYAN : C_DIM, 0);
    }
}

/* --- match ----------------------------------------------------------------- */

static lv_obj_t *s_match_scr;
static struct {
    lv_obj_t *c1, *score, *c2, *names, *meta;
    lv_obj_t *card[RIFT_GAMES_MAX];
    lv_obj_t *g_res[RIFT_GAMES_MAX], *g_dur[RIFT_GAMES_MAX];
    lv_obj_t *g_name[RIFT_GAMES_MAX];
} s_match;

void rift_view_match_create(void) {
    s_match_scr = screen();
    lv_obj_t *t, *r;
    band(s_match_scr, &t, &r, C_EDGE_GOLD);
    lv_label_set_text(t, "对局详情");
    lv_obj_t *head = panel(s_match_scr, 4, 38, 232, 72, C_PANEL_BG, C_EDGE);
    s_match.c1 = label(head, 20, 6, &rift_font_20, C_GOLD);
    s_match.score = label(head, 0, 6, &rift_font_20, C_GOLD);
    lv_obj_set_width(s_match.score, 240);
    lv_obj_set_style_text_align(s_match.score, LV_TEXT_ALIGN_CENTER, 0);
    s_match.c2 = label(head, 236 - 52, 6, &rift_font_20, C_MAIN);
    s_match.names = label(head, 0, 32, &rift_font_16, C_DIM);
    lv_obj_set_width(s_match.names, 232);
    lv_obj_set_style_text_align(s_match.names, LV_TEXT_ALIGN_CENTER, 0);
    s_match.meta = label(head, 0, 51, &rift_font_16, C_DIM);
    lv_obj_set_width(s_match.meta, 232);
    lv_obj_set_style_text_align(s_match.meta, LV_TEXT_ALIGN_CENTER, 0);
    for (int i = 0; i < RIFT_GAMES_MAX; i++) {
        int y = 116 + i * 34;
        s_match.card[i] = panel(s_match_scr, 4, y, 232, 30, C_PANEL_BG,
                                C_EDGE);
        lv_obj_t *g = label(s_match.card[i], 16, 5, &rift_font_16, C_DIM);
        s_match.g_name[i] = g;
        s_match.g_res[i] = label(s_match.card[i], 48, 5, &rift_font_16,
                                 C_DIM);
        s_match.g_dur[i] = label(s_match.card[i], 150, 5, &rift_font_16,
                                 C_DIM);
        lv_obj_set_width(s_match.g_dur[i], 70);
        lv_obj_set_style_text_align(s_match.g_dur[i], LV_TEXT_ALIGN_RIGHT, 0);
    }
    lv_obj_t *f;
    footer(s_match_scr, &f, "OK 小场详情 · 长按OK 返回");
}

void rift_view_match_refresh(const rift_match_t *m, int cur_game) {
    char b[64];
    lv_label_set_text(s_match.c1, m->code1);
    snprintf(b, sizeof b, "%d : %d", m->wins1, m->wins2);
    lv_label_set_text(s_match.score, b);
    lv_label_set_text(s_match.c2, m->code2);
    snprintf(b, sizeof b, "%s · %s", m->name1, m->name2);
    lv_label_set_text(s_match.names, b);
    snprintf(b, sizeof b, "BO%d · %s", m->bo,
             m->state == RIFT_ST_DONE ? "已结束" : "未结束");
    lv_label_set_text(s_match.meta, b);

    for (int i = 0; i < RIFT_GAMES_MAX; i++) {
        bool shown = i < m->game_count;
        lv_obj_t *card = s_match.card[i];
        lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
        if (!shown) continue;
        lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(card, i == cur_game ? C_CYAN : C_EDGE,
                                      0);
        char gl[8];
        snprintf(gl, sizeof gl, "G%d", i + 1);
        lv_obj_t *glab = s_match.g_name[i];
        lv_label_set_text(glab, gl);
        lv_label_set_text(s_match.g_res[i],
                          m->games[i].state == RIFT_ST_DONE
                              ? (m->games[i].winner == 0
                                     ? "已结束"
                                     : m->games[i].winner == 1 ? "胜" : "负")
                              : m->games[i].state == RIFT_ST_LIVE ? "进行中"
                                                                  : "未开始");
        lv_obj_set_style_text_color(s_match.g_res[i],
                                    m->games[i].winner ? C_GOLD : C_DIM, 0);
        lv_label_set_text(s_match.g_dur[i],
                          m->games[i].side1 == 0 ? "蓝方" : "红方");
    }
}

/* --- game (BP / KDA) -------------------------------------------------------- */

static lv_obj_t *s_game_scr;
static struct {
    lv_obj_t *title, *bp_tab, *kda_tab;
    lv_obj_t *ban_note;
    lv_obj_t *bp_rows[10];      /* role+name+champ per player */
    struct { lv_obj_t *name, *kda, *cs, *gold; } kda_rows[10];
    lv_obj_t *team_head[2];
} s_game;

static const char *ROLE[5] = {"上", "野", "中", "下", "辅"};

void rift_view_game_create(void) {
    s_game_scr = screen();
    lv_obj_t *t, *r;
    band(s_game_scr, &t, &r, C_EDGE_GOLD);
    s_game.title = t;
    lv_obj_set_style_text_font(t, &rift_font_16, 0);
    s_game.bp_tab = panel(s_game_scr, 152, 6, 34, 20, C_CYAN, C_CYAN);
    lv_obj_t *bpt = label(s_game.bp_tab, 0, 2, &rift_font_16, C_BG);
    lv_obj_set_width(bpt, 32);
    lv_obj_set_style_text_align(bpt, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(bpt, "BP");
    s_game.kda_tab = panel(s_game_scr, 192, 6, 38, 20, C_PANEL_BG2, C_EDGE);
    lv_obj_t *kdat = label(s_game.kda_tab, 0, 2, &rift_font_16, C_DIM);
    lv_obj_set_width(kdat, 36);
    lv_obj_set_style_text_align(kdat, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(kdat, "KDA");
    s_game.ban_note = label(s_game_scr, 0, 38, &rift_font_16, C_DIM);
    lv_obj_set_width(s_game.ban_note, 228);
    lv_obj_set_style_text_align(s_game.ban_note, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_game.ban_note, "BAN数据源待接入");
    for (int i = 0; i < 10; i++) {
        int y = 58 + (i % 5) * 20 + (i / 5) * 102;
        s_game.bp_rows[i] = label(s_game_scr, 12, y, &rift_font_16, C_MAIN);
        int ky = 38 + (i % 5) * 19 + (i / 5) * 100;
        s_game.kda_rows[i].name = label(s_game_scr, 14, ky + (i / 5) * 2,
                                        &rift_font_16, C_MAIN);
        s_game.kda_rows[i].kda = label(s_game_scr, 100, ky + (i / 5) * 2,
                                       &rift_font_16, C_MAIN);
        s_game.kda_rows[i].cs = label(s_game_scr, 150, ky + (i / 5) * 2,
                                      &rift_font_16, C_DIM);
        s_game.kda_rows[i].gold = label(s_game_scr, 190, ky + (i / 5) * 2,
                                        &rift_font_16, C_DIM);
    }
    for (int i = 0; i < 2; i++)
        s_game.team_head[i] = label(s_game_scr, 14, 38, &rift_font_16,
                                    i == 0 ? C_BLUE : C_RED);
    lv_obj_t *f;
    footer(s_game_scr, &f, "OK 切换KDA · 长按OK 返回");
}

void rift_view_game_refresh(const rift_match_t *m, int game_idx, bool kda) {
    char b[48];
    snprintf(b, sizeof b, "第%d场 %s", game_idx + 1,
             m->games[game_idx].state == RIFT_ST_DONE ? "已结束" : "未结束");
    lv_label_set_text(s_game.title, b);
    lv_obj_set_style_bg_color(s_game.bp_tab, kda ? C_PANEL_BG2 : C_CYAN, 0);
    lv_obj_set_style_border_color(s_game.bp_tab, kda ? C_EDGE : C_CYAN, 0);
    lv_obj_set_style_bg_color(s_game.kda_tab, kda ? C_CYAN : C_PANEL_BG2, 0);
    lv_obj_set_style_border_color(s_game.kda_tab, kda ? C_CYAN : C_EDGE, 0);

    const rift_side_t *sides[2] = {&m->blue, &m->red};
    for (int side = 0; side < 2; side++) {
        for (int i = 0; i < 5; i++) {
            const rift_player_t *p = &sides[side]->p[i];
            int idx = side * 5 + i;
            /* BP view: 上 Canna · Jayce */
            snprintf(b, sizeof b, "%s %s  ·  %s", ROLE[i], p->who, p->champ);
            lv_label_set_text(s_game.bp_rows[idx], b);
            lv_obj_set_style_text_color(
                s_game.bp_rows[idx], side == 0 ? C_BLUE : C_RED, 0);
            /* KDA view */
            snprintf(b, sizeof b, "%d/%d/%d", p->k, p->d, p->a);
            lv_label_set_text(s_game.kda_rows[idx].kda, b);
            snprintf(b, sizeof b, "%d", p->cs);
            lv_label_set_text(s_game.kda_rows[idx].cs, b);
            snprintf(b, sizeof b, "%dk", p->cs * 0 + sides[side]->gold / 5000);
            lv_label_set_text(s_game.kda_rows[idx].gold, b);
            lv_label_set_text(s_game.kda_rows[idx].name, p->who);
        }
        snprintf(b, sizeof b, "%s  %d杀 · %dk", side == 0 ? m->code1 : m->code2,
                 sides[side]->kills, sides[side]->gold / 1000);
        lv_label_set_text(s_game.team_head[side], b);
    }
    /* visibility: hide the inactive view's objects */
    for (int i = 0; i < 10; i++) {
        if (kda) {
            lv_obj_add_flag(s_game.bp_rows[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_game.kda_rows[i].name, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_game.kda_rows[i].kda, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_game.kda_rows[i].cs, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_game.bp_rows[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_game.kda_rows[i].name, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_game.kda_rows[i].kda, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_game.kda_rows[i].cs, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(s_game.kda_rows[i].gold, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 2; i++) {
        int y = kda ? (36 + i * 100) : -20;
        lv_obj_set_pos(s_game.team_head[i], 14, y);
        lv_obj_add_flag(s_game.team_head[i],
                        kda ? 0 : LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(s_game.ban_note, kda ? LV_OBJ_FLAG_HIDDEN : 0);
    lv_obj_t *f = lv_obj_get_child(s_game_scr, -1);
    (void)f;
}

/* --- live ------------------------------------------------------------------- */

static lv_obj_t *s_live_scr;
static struct {
    lv_obj_t *state, *c1, *score, *c2, *k1, *k2;
    lv_obj_t *rows[10];
} s_live;

void rift_view_live_create(void) {
    s_live_scr = screen();
    lv_obj_t *t, *r;
    band(s_live_scr, &t, &r, C_RED_EDGE);
    lv_label_set_text(t, "实时看板");
    s_live.state = r;
    lv_obj_set_style_text_color(s_live.state, C_LIVE_RED, 0);
    lv_obj_t *head = panel(s_live_scr, 4, 38, 232, 54, C_PANEL_BG, C_RED_EDGE);
    s_live.c1 = label(head, 20, 24, &rift_font_20, C_MAIN);
    s_live.score = label(head, 0, 24, &rift_font_20, C_GOLD);
    lv_obj_set_width(s_live.score, 240);
    lv_obj_set_style_text_align(s_live.score, LV_TEXT_ALIGN_CENTER, 0);
    s_live.c2 = label(head, 236 - 52, 24, &rift_font_20, C_MAIN);
    s_live.k1 = label(head, 20, 6, &rift_font_16, C_BLUE);
    s_live.k2 = label(head, 160, 6, &rift_font_16, C_RED);
    lv_obj_set_width(s_live.k2, 60);
    lv_obj_set_style_text_align(s_live.k2, LV_TEXT_ALIGN_RIGHT, 0);
    for (int i = 0; i < 10; i++) {
        int col = i / 5, row = i % 5;
        s_live.rows[i] = label(s_live_scr, 12 + col * 120, 104 + row * 22,
                               &rift_font_16, C_MAIN);
    }
    lv_obj_t *f;
    footer(s_live_scr, &f, "OK 立即刷新 · 每30秒自动更新");
}

void rift_view_live_refresh(const rift_match_t *m) {
    char b[48];
    lv_label_set_text(s_live.state, "LIVE · 实时");
    lv_label_set_text(s_live.c1, m->code1);
    lv_label_set_text(s_live.c2, m->code2);
    snprintf(b, sizeof b, "%d : %d", m->wins1, m->wins2);
    lv_label_set_text(s_live.score, b);
    snprintf(b, sizeof b, "击杀 %d", m->blue.kills);
    lv_label_set_text(s_live.k1, b);
    snprintf(b, sizeof b, "%d 击杀", m->red.kills);
    lv_label_set_text(s_live.k2, b);
    for (int side = 0; side < 2; side++) {
        const rift_side_t *sd = side == 0 ? &m->blue : &m->red;
        for (int i = 0; i < 5; i++) {
            const rift_player_t *p = &sd->p[i];
            snprintf(b, sizeof b, "%s %d/%d/%d", p->who, p->k, p->d, p->a);
            lv_label_set_text(s_live.rows[side * 5 + i], b);
        }
    }
}

/* --- data -------------------------------------------------------------------- */

static lv_obj_t *s_data_scr;
static struct {
    lv_obj_t *wifi_sub, *cache_sub, *cursor;
    lv_obj_t *card[4];
} s_data;

void rift_view_data_create(void) {
    s_data_scr = screen();
    lv_obj_t *t, *r;
    band(s_data_scr, &t, &r, C_EDGE_GOLD);
    lv_label_set_text(t, "数据与网络");
    static const char *TITLES[4] = {"Wi-Fi", "数据缓存", "时区", "数据源"};
    for (int i = 0; i < 4; i++) {
        s_data.card[i] = panel(s_data_scr, 4, 40 + i * 52, 232, 46,
                               C_PANEL_BG, C_EDGE);
        lv_obj_t *ti = label(s_data.card[i], 16, 4, &rift_font_16, C_MAIN);
        lv_label_set_text(ti, TITLES[i]);
    }
    s_data.wifi_sub = label(s_data.card[0], 16, 24, &rift_font_16, C_GREEN);
    s_data.cache_sub = label(s_data.card[1], 16, 24, &rift_font_16, C_CYAN);
    lv_obj_t *tz = label(s_data.card[2], 16, 24, &rift_font_16, C_DIM);
    lv_label_set_text(tz, "北京时间 UTC+8");
    lv_obj_t *src = label(s_data.card[3], 16, 24, &rift_font_16, C_DIM);
    lv_label_set_text(src, "lolesports 非官方接口");
    s_data.cursor = label(s_data_scr, 216, 0, &rift_font_16, C_CYAN);
    lv_obj_t *note = panel(s_data_scr, 4, 250, 232, 30, C_PANEL_BG2, C_EDGE);
    lv_obj_t *nt = label(note, 14, 6, &rift_font_16, C_DIM);
    lv_label_set_text(nt, "离线时可浏览全部已缓存内容");
    lv_obj_t *f;
    footer(s_data_scr, &f, "OK 刷新数据 · 长按OK 返回");
}

void rift_view_data_refresh(int cursor, const char *cache_line,
                            bool wifi_ok, const char *wifi_line) {
    for (int i = 0; i < 4; i++)
        lv_obj_set_style_border_color(s_data.card[i],
                                      i == cursor ? C_CYAN : C_EDGE, 0);
    lv_obj_set_y(s_data.cursor, 40 + cursor * 52 + 16);
    lv_label_set_text(s_data.cursor, ">");
    lv_label_set_text(s_data.wifi_sub,
                      wifi_ok ? wifi_line : "未连接 · 离线模式");
    lv_obj_set_style_text_color(s_data.wifi_sub, wifi_ok ? C_GREEN : C_RED, 0);
    lv_label_set_text(s_data.cache_sub, cache_line);
}
