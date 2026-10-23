// rift_view.h — RIFT BOARD screens. LVGL objects live behind the app task,
// which always holds bsp_lvgl_lock() before calling in here.
#pragma once

#include "lvgl.h"

#include "rift_model.h"

typedef enum {
    RIFT_PAGE_BOOT = 0,
    RIFT_PAGE_HOME,
    RIFT_PAGE_MATCH,
    RIFT_PAGE_GAME,
    RIFT_PAGE_LIVE,
    RIFT_PAGE_DATA,
} rift_page_t;

void rift_view_boot(void);                  /* create boot screen */
void rift_view_boot_progress(int pct, const char *line);

void rift_view_home_create(void);           /* switch to home screen */
void rift_view_home_refresh(const rift_match_t *ms, const rift_day_t *days,
                            int ndays, int cur_day, int cur_row, int rows);

void rift_view_match_create(void);
void rift_view_match_refresh(const rift_match_t *m, int cur_game);

void rift_view_game_create(void);
void rift_view_game_refresh(const rift_match_t *m, int game_idx, bool kda);

void rift_view_live_create(void);
void rift_view_live_refresh(const rift_match_t *m);

void rift_view_data_create(void);
void rift_view_data_refresh(int cursor, const char *cache_line,
                            bool wifi_ok, const char *wifi_line);
