// rift_app.c — RIFT BOARD state machine and input. Button callbacks only
// enqueue; the app task owns all LVGL access behind bsp_lvgl_lock().
// v1 increments ship offline-first: pages run from a bundled sample of the
// probed 2026-10-24 Swiss round plus the KC 3:0 MVK Play-Ins series (real
// window data); the Wi-Fi/HTTPS fetch lands in the networking increment.
#include "rift_app.h"

#include <stdio.h>
#include <string.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "rift_blufi.h"
#include "rift_net.h"
#include "rift_view.h"

static const char *TAG = "rift_app";

/* Worlds 2026 event window, inclusive Beijing days (2026-10-15..11-15). */
static const char *RIFT_WIN0_ISO = "2026-10-15T00:00:00Z";
static const char *RIFT_WIN1_ISO = "2026-11-15T23:59:59Z";

typedef enum {
    ROW_DATE = 0,      /* date bar */
    ROW_M0, ROW_M1,    /* up to two matches */
    ROW_TEASER,
    ROW_COUNT
} home_row_t;

static void process_event(const input_event_t *ev);

typedef enum {
    EV_BTN = 0,
    EV_NET_DIRTY,    // worker swapped the schedule; refresh from app task
    EV_DETAILS,      // per-match game list arrived
    EV_WINDOW,       // BP/KDA window stats arrived
} ev_kind_t;

typedef struct {
    ev_kind_t kind;
    bsp_btn_t btn;
    bsp_btn_ev_t ev;
} input_event_t;

static struct {
    QueueHandle_t queue;
    bool ready;
    int battery;
    rift_page_t page;
    volatile bool net_dirty;

    rift_match_t ms[RIFT_MATCH_MAX];
    int nmatch;
    rift_day_t days[RIFT_DAYS_MAX];
    int ndays;

    int cur_day;        /* Beijing day shown on home */
    int cur_day_pos;    /* index into the date list (0 = today-ish) */
    int cur_row;
    int cur_match;      /* index into ms[] for the match page */
    int cur_game;
    bool game_kda;
    int data_cursor;
    rift_match_t detail;   /* scratch: the match page's full data */
} s_app;

/* --- bundled sample (real probed data, offline increment) ------------------- */

static void load_sample(void) {
    /* Swiss round 1, 2026-10-24 Beijing (probed 2026-10-23). */
    static const struct {
        const char *h, *c1, *c2;
        int bo;
    } rows[] = {
        {"01:00", "GEN", "IG", 1},   {"02:00", "HLE", "TES", 1},
        {"03:00", "BLG", "MKOI", 1}, {"04:00", "AL", "KC", 1},
        {"05:00", "G2", "DK", 1},    {"06:00", "TLAW", "CFO", 1},
        {"07:00", "LOS", "T1", 1},   {"08:00", "TSW", "LYON", 1},
    };
    /* All eight Swiss games share the day of 2026-10-23T17:00:00Z + k*3600.
     * Times are encoded as epoch so the Beijing grouping stays real. */
    int64_t base = rift_parse_iso("2026-10-23T17:00:00Z");
    int n = 0;
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        rift_match_t *m = &s_app.ms[n++];
        memset(m, 0, sizeof *m);
        snprintf(m->id, sizeof m->id, "swiss-%d", i);
        m->start = base + (int64_t)i * 3600;
        m->state = RIFT_ST_UNSTARTED;
        m->bo = (uint8_t)rows[i].bo;
        strcpy(m->stage, "Swiss");
        strcpy(m->code1, rows[i].c1);
        strcpy(m->code2, rows[i].c2);
    }
    /* Play-Ins: KC 3:0 MVK, 2026-10-17 02:00 Beijing (probed). */
    rift_match_t *kc = &s_app.ms[n++];
    memset(kc, 0, sizeof *kc);
    snprintf(kc->id, sizeof kc->id, "playin-kc-mvk");
    kc->start = 1792173600;
    kc->state = RIFT_ST_DONE;
    kc->bo = 5;
    strcpy(kc->stage, "Play-Ins");
    strcpy(kc->code1, "KC");
    strcpy(kc->code2, "MVK");
    strcpy(kc->name1, "Karmine Corp");
    strcpy(kc->name2, "MVK Esports");
    strcpy(kc->id1, "111692118851466302");
    strcpy(kc->id2, "107251245690956393");
    kc->wins1 = 3;
    kc->wins2 = 0;
    static const uint8_t SIDES[5] = {0, 0, 1, 0, 1};
    static const uint8_t WIN[5] = {1, 1, 1, 0, 0};
    kc->game_count = 5;
    for (int g = 0; g < 5; g++) {
        kc->games[g].state = g < 3 ? RIFT_ST_DONE : RIFT_ST_UNSTARTED;
        kc->games[g].side1 = SIDES[g];
        kc->games[g].winner = WIN[g];
        snprintf(kc->games[g].id, sizeof kc->games[g].id,
                 "11629964949830388%c", (char)('5' + g));
    }
    /* G1 window (real probed final frame). */
    static const struct {
        const char *who, *champ, *role;
        int k, d, a, cs;
    } BLUE[5] = {{"KC Canna", "Jayce", "top", 1, 0, 6, 301},
                 {"KC Yike", "LeeSin", "jungle", 2, 0, 5, 220},
                 {"KC kyeahoo", "Cassiopeia", "mid", 4, 1, 2, 284},
                 {"KC Caliste", "Zeri", "bottom", 6, 1, 1, 311},
                 {"KC Busio", "Yuumi", "support", 0, 0, 10, 8}};
    static const struct {
        const char *who, *champ, *role;
        int k, d, a, cs;
    } RED[5] = {{"MVK Kratos", "Yorick", "top", 0, 4, 0, 280},
                {"MVK Gury", "JarvanIV", "jungle", 1, 2, 0, 179},
                {"MVK Chika", "Akali", "mid", 0, 4, 0, 217},
                {"MVK Harky", "Ashe", "bottom", 1, 1, 1, 319},
                {"MVK SiuLoong", "Seraphine", "support", 0, 2, 0, 40}};
    for (int i = 0; i < 5; i++) {
        rift_player_t *b = &kc->blue.p[i], *r = &kc->red.p[i];
        strcpy(b->who, BLUE[i].who);
        strcpy(b->champ, BLUE[i].champ);
        strcpy(b->role, BLUE[i].role);
        b->k = BLUE[i].k;
        b->d = BLUE[i].d;
        b->a = BLUE[i].a;
        b->cs = BLUE[i].cs;
        strcpy(r->who, RED[i].who);
        strcpy(r->champ, RED[i].champ);
        strcpy(r->role, RED[i].role);
        r->k = RED[i].k;
        r->d = RED[i].d;
        r->a = RED[i].a;
        r->cs = RED[i].cs;
    }
    kc->blue.kills = 13;
    kc->red.kills = 2;
    kc->blue.gold = 58490;
    kc->red.gold = 47092;
    kc->blue.towers = 9;
    kc->red.towers = 2;
    kc->window_valid = true;
    s_app.nmatch = n;
    rift_sort_matches(s_app.ms, s_app.nmatch);
    s_app.ndays = rift_group_days(s_app.ms, s_app.nmatch, s_app.days,
                                  RIFT_DAYS_MAX);
}

/* --- refresh dispatch -------------------------------------------------------- */

static void refresh_home(void) {
    rift_view_home_refresh(s_app.ms, s_app.days, s_app.ndays, s_app.cur_day,
                           s_app.cur_row, s_app.cur_row);
}

static int match_index_for_day(int day, int which) {
    for (int i = 0; i < s_app.ndays; i++) {
        if (s_app.days[i].day != day) continue;
        int idx = s_app.days[i].first + which;
        if (which < s_app.days[i].count) return idx;
    }
    return -1;
}

/* Data-page Wi-Fi line mirrors the provisioning service state, so the
 * screen doubles as the on-device setup guide (mockups 08/09). */
static bool wifi_line(char *buf, int cap, bool *ok) {
    switch (rift_blufi_state()) {
    case RIFT_BLUFI_WIFI_CONNECTED: {
        char ssid[33], ip[16];
        rift_blufi_wifi_info(ssid, sizeof ssid, ip, sizeof ip);
        snprintf(buf, cap, "已连接 · %s · %s", ssid, ip);
        *ok = true;
        return true;
    }
    case RIFT_BLUFI_WIFI_CONNECTING:
        snprintf(buf, cap, "Wi-Fi 连接中…");
        break;
    case RIFT_BLUFI_BLE_CONNECTED:
        snprintf(buf, cap, "手机已连 · 请发送 Wi-Fi 凭据");
        break;
    case RIFT_BLUFI_ADVERTISING:
        snprintf(buf, cap, "等待配网 · ESP Config 小程序");
        break;
    case RIFT_BLUFI_FAILED:
        snprintf(buf, cap, "配网服务异常 · OK重试");
        break;
    default:
        snprintf(buf, cap, "配网服务未启动");
        break;
    }
    *ok = false;
    return false;
}

static void refresh_data(void) {
    char wbuf[64], cache[64], sync[24];
    bool wifi_ok;
    wifi_line(wbuf, sizeof wbuf, &wifi_ok);
    if (rift_net_last_sync(sync, sizeof sync))
        snprintf(cache, sizeof cache, "%s · 赛程%d场", sync, s_app.nmatch);
    else
        snprintf(cache, sizeof cache, "示例数据 · 未联网同步");
    rift_view_data_refresh(s_app.data_cursor, cache, wifi_ok, wbuf);
}

/* --- input -------------------------------------------------------------------- */

static void process_event(const input_event_t *ev) {
    bool click = ev->ev == BSP_BTN_CLICK;
    bool long_ok = ev->ev == BSP_BTN_LONG && ev->btn == BSP_BTN_OK;

    switch (s_app.page) {
    case RIFT_PAGE_HOME:
        if (ev->btn == BSP_BTN_UP || ev->btn == BSP_BTN_DOWN) {
            int rows = ROW_COUNT;
            if (ev->btn == BSP_BTN_UP)
                s_app.cur_row = (s_app.cur_row + rows - 1) % rows;
            else
                s_app.cur_row = (s_app.cur_row + 1) % rows;
            refresh_home();
        } else if (click && s_app.cur_row == ROW_DATE) {
            /* cycle one day per click; long-press enters the data page */
            s_app.cur_day += 1;
            refresh_home();
        } else if (click && s_app.cur_row == ROW_TEASER) {
            s_app.cur_day =
                match_index_for_day(s_app.cur_day, 0) >= 0
                    ? s_app.cur_day
                    : s_app.cur_day;
            refresh_home();
        } else if (click
                   && (s_app.cur_row == ROW_M0 || s_app.cur_row == ROW_M1)) {
            int idx = match_index_for_day(s_app.cur_day,
                                          s_app.cur_row == ROW_M0 ? 0 : 1);
            if (idx >= 0) {
                s_app.cur_match = idx;
                s_app.cur_game = 0;
                s_app.detail = s_app.ms[idx];
                if (!s_app.detail.game_count && s_app.detail.id[0])
                    rift_net_request_details(s_app.detail.id);
                s_app.page = RIFT_PAGE_MATCH;
                rift_view_match_create();
                rift_view_match_refresh(&s_app.detail, 0);
            }
        } else if (long_ok) {
            s_app.page = RIFT_PAGE_DATA;
            s_app.data_cursor = 0;
            rift_view_data_create();
            refresh_data();
        }
        break;

    case RIFT_PAGE_MATCH: {
        const rift_match_t *m = &s_app.detail;
        if (ev->btn == BSP_BTN_UP || ev->btn == BSP_BTN_DOWN) {
            int dir = ev->btn == BSP_BTN_UP ? -1 : 1;
            for (int k = 0; k < m->game_count; k++) {
                s_app.cur_game =
                    (s_app.cur_game + dir + m->game_count) % m->game_count;
                if (m->games[s_app.cur_game].state == RIFT_ST_DONE) break;
            }
            rift_view_match_refresh(m, s_app.cur_game);
        } else if (click && m->games[s_app.cur_game].state == RIFT_ST_DONE) {
            s_app.page = RIFT_PAGE_GAME;
            s_app.game_kda = false;
            rift_view_game_create();
            rift_view_game_refresh(m, s_app.cur_game, false);
            if (m->games[s_app.cur_game].id[0])
                rift_net_request_window(m->games[s_app.cur_game].id,
                                        m->start, s_app.cur_game);
        } else if (long_ok) {
            s_app.page = RIFT_PAGE_HOME;
            rift_view_home_create();
            refresh_home();
        }
        break;
    }

    case RIFT_PAGE_GAME: {
        rift_match_t *m = &s_app.detail;
        if (click) {
            s_app.game_kda = !s_app.game_kda;
            rift_view_game_refresh(m, s_app.cur_game, s_app.game_kda);
        } else if (long_ok) {
            s_app.page = RIFT_PAGE_MATCH;
            rift_view_match_create();
            rift_view_match_refresh(m, s_app.cur_game);
        }
        break;
    }

    case RIFT_PAGE_LIVE:
        if (long_ok || click) {
            s_app.page = RIFT_PAGE_HOME;
            rift_view_home_create();
            refresh_home();
        }
        break;

    case RIFT_PAGE_DATA:
        if (ev->btn == BSP_BTN_UP || ev->btn == BSP_BTN_DOWN) {
            s_app.data_cursor =
                (s_app.data_cursor + (ev->btn == BSP_BTN_UP ? 3 : 1)) % 4;
            refresh_data();
        } else if (click && s_app.data_cursor == 0) {
            /* Wi-Fi row: connected → clear credentials (re-provision);
             * otherwise → retry the stored-credential connect. */
            if (rift_blufi_wifi_ok())
                rift_blufi_clear_credentials();
            else
                rift_blufi_reconnect();
            refresh_data();
        } else if (click && s_app.data_cursor == 1) {
            rift_net_request_sync();
            refresh_data();
        } else if (long_ok) {
            s_app.page = RIFT_PAGE_HOME;
            rift_view_home_create();
            refresh_home();
        }
        break;

    case RIFT_PAGE_BOOT:
        break;
    }
}

static void input_task(void *arg) {
    (void)arg;
    input_event_t ev;
    for (;;) {
        if (xQueueReceive(s_app.queue, &ev, portMAX_DELAY) == pdTRUE) {
            if (!bsp_lvgl_lock(500)) continue;
            if (ev.kind == EV_BTN) {
                process_event(&ev);
            } else if (ev.kind == EV_NET_DIRTY) {
                s_app.net_dirty = false;
                rift_sort_matches(s_app.ms, s_app.nmatch);
                s_app.ndays = rift_group_days(s_app.ms, s_app.nmatch,
                                              s_app.days, RIFT_DAYS_MAX);
                int today = rift_today_bj();
                if (today > 0) s_app.cur_day = today;
                if (s_app.page == RIFT_PAGE_HOME) refresh_home();
                else if (s_app.page == RIFT_PAGE_DATA) refresh_data();
            } else if (ev.kind == EV_DETAILS
                       && s_app.page == RIFT_PAGE_MATCH) {
                rift_view_match_refresh(&s_app.detail, s_app.cur_game);
            } else if (ev.kind == EV_WINDOW
                       && (s_app.page == RIFT_PAGE_GAME
                           || s_app.page == RIFT_PAGE_LIVE)) {
                rift_view_game_refresh(&s_app.detail, s_app.cur_game,
                                       s_app.game_kda);
            }
            bsp_lvgl_unlock();
        }
    }
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t event, void *user) {
    (void)user;
    if (!s_app.ready || !s_app.queue) return;
    const input_event_t input = {.kind = EV_BTN, .btn = btn, .ev = event};
    xQueueSend(s_app.queue, &input, 0);
}

// Runs in the ESP event task: enqueue only, no LVGL, no blocking.
static void on_got_ip(void *user) {
    (void)user;
    rift_net_request_sync();
}

// Runs in the rift_net worker: enqueue only.
static void on_net_ready(rift_net_res_t res, void *user) {
    (void)user;
    if (!s_app.ready || !s_app.queue) return;
    const input_event_t ev = {
        .kind = res == RIFT_NET_RES_DETAILS ? EV_DETAILS : EV_WINDOW};
    xQueueSend(s_app.queue, &ev, 0);
}

/* --- boot ---------------------------------------------------------------------- */

void rift_app_boot(void) {
    memset(&s_app, 0, sizeof s_app);
    s_app.battery = -1;
    if (bsp_battery_init() == ESP_OK) s_app.battery = bsp_battery_soc();
    else ESP_LOGW(TAG, "battery gauge unavailable");

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    rift_view_boot();
    rift_view_boot_progress(20, "载入内置示例数据…");
    load_sample();
    rift_view_boot_progress(50, "载入网络缓存…");
    {
        rift_match_t cached[RIFT_MATCH_MAX];
        int nc = 0;
        if (rift_net_load_cache(cached, &nc)) {
            memcpy(s_app.ms, cached, (size_t)nc * sizeof *s_app.ms);
            s_app.nmatch = nc;
        }
    }
    rift_view_boot_progress(80, "就绪");

    int w0 = rift_day_of(rift_parse_iso(RIFT_WIN0_ISO));
    int w1 = rift_day_of(rift_parse_iso(RIFT_WIN1_ISO));
    rift_match_t keep[RIFT_MATCH_MAX];
    int nk = 0;
    for (int i = 0; i < s_app.nmatch; i++)
        if (rift_in_window(&s_app.ms[i], w0, w1)) keep[nk++] = s_app.ms[i];
    memcpy(s_app.ms, keep, sizeof keep);
    s_app.nmatch = nk;
    rift_sort_matches(s_app.ms, s_app.nmatch);
    s_app.ndays = rift_group_days(s_app.ms, s_app.nmatch, s_app.days,
                                  RIFT_DAYS_MAX);
    /* offline v1: today is unknown until network time lands; show the
     * first event day of the sample. */
    s_app.cur_day = s_app.ndays ? s_app.days[0].day : 0;
    s_app.cur_row = ROW_DATE;

    if (bsp_button_init(on_key, NULL) != ESP_OK)
        ESP_LOGE(TAG, "button init failed");

    s_app.queue = xQueueCreate(8, sizeof(input_event_t));
    xTaskCreate(input_task, "rift_input", 4096, NULL, 5, NULL);

    rift_net_attach(s_app.ms, &s_app.nmatch, s_app.days, &s_app.ndays,
                    &s_app.net_dirty);
    rift_net_bind_detail(&s_app.detail);
    rift_net_set_ready_cb(on_net_ready, NULL);
    rift_net_init();
    esp_err_t blufi = rift_blufi_start(on_got_ip, NULL);
    if (blufi != ESP_OK) ESP_LOGW(TAG, "blufi service not started: %s",
                                  esp_err_to_name(blufi));
    rift_view_boot_progress(
        90, rift_blufi_wifi_ok()
                ? "Wi-Fi 已连接 · 同步中"
                : "等待配网 · 打开 ESP Config 小程序");

    vTaskDelay(pdMS_TO_TICKS(600));
    s_app.ready = true;
    s_app.page = RIFT_PAGE_HOME;
    rift_view_home_create();
    if (!bsp_lvgl_lock(500)) return;
    refresh_home();
    bsp_lvgl_unlock();
    ESP_LOGI(TAG, "RIFT BOARD ready (%d matches, %d days)", s_app.nmatch,
             s_app.ndays);
}
