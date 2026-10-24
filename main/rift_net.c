// rift_net.c — HTTPS schedule sync + SNTP. One worker task; responses are
// pulled into one bounded heap buffer (schedule page ≈ 44 KB probed) and
// parsed in place by rift_parse_schedule. Game-detail and live-window
// endpoints join in the next increment (the app currently serves those from
// the bundled sample).
//
// The lolesports API key below is the public key embedded in the
// lolesports.com web app itself; it is not a credential.
#include "rift_net.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "rift_model.h"

static const char *TAG = "rift_net";

#define API_KEY "0TvQnueqKa5mxJntVWt0w4LpLfEkrV1Ta8rQBb9Z"
#define SCHEDULE_URL \
    "https://esports-api.lolesports.com/persisted/gw/getSchedule?hl=en-US" \
    "&leagueId=98767975604431411"
#define RIFT_BUF_MAX (96 * 1024)
#define RIFT_SYNC_PAGES_MAX 8
#define RIFT_NVS_KEY "sched"
#define RIFT_SYNC_TASK_STACK (6 * 1024)

static volatile rift_net_state_t s_state;
static TaskHandle_t s_task;
static volatile bool s_req_pending;
static char s_last_sync[24];

/* Detail/window pipeline state (guarded by the worker's single thread). */
static rift_match_t *s_detail;
static rift_detail_t *s_detail_games;
static char s_pending_match_id[RIFT_ID_MAX];
static char s_pending_game_id[RIFT_ID_MAX];
static int64_t s_pending_game_start;
static int s_pending_game_idx;
static void (*s_ready_cb)(rift_net_res_t res, void *user);
static void *s_ready_user;

void rift_net_bind_detail(rift_match_t *match, rift_detail_t *detail) {
    s_detail = match;
    s_detail_games = detail;
}

void rift_net_set_ready_cb(void (*cb)(rift_net_res_t res, void *user),
                           void *user) {
    s_ready_cb = cb;
    s_ready_user = user;
}

void rift_net_request_details(const char *match_id) {
    snprintf(s_pending_match_id, sizeof s_pending_match_id, "%s", match_id);
    xTaskNotifyGive(s_task);   // worker notices the id and details first
}

void rift_net_request_window(const char *game_id, int64_t game_start,
                             int game_idx) {
    snprintf(s_pending_game_id, sizeof s_pending_game_id, "%s", game_id);
    s_pending_game_start = game_start;
    s_pending_game_idx = game_idx;
    xTaskNotifyGive(s_task);
}

// Worlds 2026 event window (inclusive Beijing days).
static int s_win0, s_win1;

// The app owns the storage; the worker fills it under a handshake.
// Pointers are set once by rift_net_attach before the task ever runs.
static rift_match_t *s_ms;
static int *s_nmatch;
static int *s_ndays;
static rift_day_t *s_days;
static volatile bool *s_dirty;   // app task refreshes UI when set

void rift_net_attach(rift_match_t *ms, int *nmatch, rift_day_t *days,
                     int *ndays, volatile bool *dirty) {
    s_ms = ms;
    s_nmatch = nmatch;
    s_days = days;
    s_ndays = ndays;
    s_dirty = dirty;
    s_win0 = rift_day_of(rift_parse_iso("2026-10-15T00:00:00Z"));
    s_win1 = rift_day_of(rift_parse_iso("2026-11-15T23:59:59Z"));
}

static int http_to_buf(const char *url, char *buf, int cap) {
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -1;
    esp_http_client_set_header(client, "x-api-key", API_KEY);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return -1;
    }
    // Response headers (and the status code) must be fetched before the
    // body read; after close() they are gone.
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    int total = 0;
    if (status == 200) {
        int n;
        while ((n = esp_http_client_read(client, buf + total, cap - total)) >
               0) {
            total += n;
            if (total >= cap) break;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (status != 200 || total <= 0) return -1;
    return total;
}

static void cache_store(const rift_match_t *ms, int n) {
    nvs_handle_t h;
    if (nvs_open("rift", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, RIFT_NVS_KEY, ms, (size_t)n * sizeof *ms);
    nvs_set_i32(h, "sched_n", n);
    nvs_commit(h);
    nvs_close(h);
}

static bool cache_load(rift_match_t *ms, int *n) {
    nvs_handle_t h;
    if (nvs_open("rift", NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = RIFT_MATCH_MAX * sizeof *ms;
    int32_t count = 0;
    bool ok = nvs_get_blob(h, RIFT_NVS_KEY, ms, &len) == ESP_OK
              && nvs_get_i32(h, "sched_n", &count) == ESP_OK
              && count > 0 && count <= RIFT_MATCH_MAX
              && len == (size_t)count * sizeof *ms;
    nvs_close(h);
    if (ok) *n = (int)count;
    return ok;
}

static void time_sync(void) {
    setenv("TZ", "CST-8", 1);
    tzset();
    if (sntp_get_sync_mode() == SNTP_SYNC_MODE_IMMED) return;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("ntp.aliyun.com");
    cfg.start = false;
    cfg.sync_cb = NULL;
    if (esp_netif_sntp_init(&cfg) != ESP_OK) return;
    esp_netif_sntp_start();
    // Bounded wait: the board has no RTC, so without network time the home
    // day picker just stays on the first cached day.
    for (int i = 0; i < 50; i++) {
        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(200)) == ESP_OK) break;
    }
    time_t now = 0;
    time(&now);
    if (now < 1780000000) {   // before 2026: no real time yet
        esp_netif_sntp_deinit();
        return;
    }
    esp_netif_sntp_deinit();
}

/* The first schedule page only spans the days around now; past event days
 * slide off it. Walk pages.older to backfill the whole window so a sync
 * never drops results it already showed. Bounded by page count and by two
 * consecutive pages that added nothing in-window (everything older is out). */
static void sync_once(void) {
    char *buf = heap_caps_malloc(RIFT_BUF_MAX, MALLOC_CAP_8BIT);
    if (!buf) return;
    char url[384], token[256];
    token[0] = '\0';
    int total = 0, empty_pages = 0;
    for (int page = 0; page < RIFT_SYNC_PAGES_MAX; page++) {
        if (token[0])
            snprintf(url, sizeof url, SCHEDULE_URL "&pageToken=%s", token);
        else
            snprintf(url, sizeof url, "%s", SCHEDULE_URL);
        int len = http_to_buf(url, buf, RIFT_BUF_MAX);
        if (len <= 0) {
            ESP_LOGW(TAG, "schedule fetch failed (page %d)", page);
            break;
        }
        int keep = 0;
        if (rift_parse_schedule(buf, s_ms + total, RIFT_MATCH_MAX - total,
                                &keep, s_win0, s_win1) != 0)
            break;
        total += keep;
        ESP_LOGI(TAG, "schedule page %d: +%d in-window (%d total)", page, keep,
                 total);
        if (total >= RIFT_MATCH_MAX) break;
        if (!rift_schedule_older(buf, token, sizeof token)) break;
        if (keep == 0 && ++empty_pages >= 2) break;
    }
    if (total > 0) {
        rift_sort_matches(s_ms, total);
        *s_nmatch = total;
        *s_ndays = rift_group_days(s_ms, total, s_days, RIFT_DAYS_MAX);
        cache_store(s_ms, total);
        time_t now = time(NULL) + 8 * 3600;
        struct tm tm;
        localtime_r(&now, &tm);
        snprintf(s_last_sync, sizeof s_last_sync, "%02d-%02d %02d:%02d",
                 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
        *s_dirty = true;
        if (s_ready_cb) s_ready_cb(RIFT_NET_RES_SCHEDULE, s_ready_user);
    }
    heap_caps_free(buf);
}

static void fetch_details_once(void) {
    if (!s_detail || !s_detail_games || !s_pending_match_id[0]) return;
    char url[160];
    snprintf(url, sizeof url,
             "https://esports-api.lolesports.com/persisted/gw/"
             "getEventDetails?hl=en-US&id=%s",
             s_pending_match_id);
    char *buf = heap_caps_malloc(32 * 1024, MALLOC_CAP_8BIT);
    if (!buf) return;
    int len = http_to_buf(url, buf, 32 * 1024);
    if (len > 0
        && rift_parse_event_details(buf, s_detail, s_detail_games) == 0) {
        /* Bind the fresh games to the match they belong to, so the app can
         * tell this scratch pair from a stale one opened earlier. */
        strcpy(s_detail_games->id, s_pending_match_id);
        if (s_ready_cb) s_ready_cb(RIFT_NET_RES_DETAILS, s_ready_user);
    } else {
        ESP_LOGW(TAG, "event details fetch failed");
    }
    heap_caps_free(buf);
}

/* The window feed returns the frames around startingTime; a finished game
 * yields its final stats from any late offset. Probe later anchors until
 * the last frame says "finished" (probed: start+45min hit for a 32:32
 * game); an in-progress game answers at the current time. */
static void fetch_window_once(void) {
    if (!s_detail || !s_detail_games || !s_pending_game_id[0]) return;
    static const int OFFSET_MIN[] = {0, 35, 45, 60, 90};
    char *buf = heap_caps_malloc(96 * 1024, MALLOC_CAP_8BIT);
    if (!buf) return;
    for (unsigned i = 0; i < sizeof OFFSET_MIN / sizeof OFFSET_MIN[0]; i++) {
        time_t t = (time_t)(s_pending_game_start
                                ? s_pending_game_start
                                      + (int64_t)OFFSET_MIN[i] * 60
                                : time(NULL));
        char when[24];
        struct tm tm;
        gmtime_r(&t, &tm);
        strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%SZ", &tm);
        char url[160];
        snprintf(url, sizeof url,
                 "https://feed.lolesports.com/livestats/v1/window/"
                 "%s?startingTime=%s",
                 s_pending_game_id, when);
        int len = http_to_buf(url, buf, 96 * 1024);
        if (len <= 0) continue;
        if (rift_parse_window(buf, s_detail_games, s_pending_game_idx) == 0) {
            if (s_ready_cb) s_ready_cb(RIFT_NET_RES_WINDOW, s_ready_user);
            if (strcmp(s_detail_games->game_state, "finished") == 0) break;
        }
    }
    heap_caps_free(buf);
}

static void net_task(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_pending_match_id[0]) {
            s_state = RIFT_NET_FETCHING;
            fetch_details_once();
            s_pending_match_id[0] = '\0';
        }
        if (s_pending_game_id[0]) {
            s_state = RIFT_NET_FETCHING;
            fetch_window_once();
            s_pending_game_id[0] = '\0';
        }
        if (s_req_pending) {
            s_req_pending = false;
            s_state = RIFT_NET_FETCHING;
            time_sync();
            sync_once();
            s_state = s_last_sync[0] ? RIFT_NET_OK : RIFT_NET_FAIL;
        }
    }
}

void rift_net_init(void) {
    xTaskCreate(net_task, "rift_net", RIFT_SYNC_TASK_STACK, NULL, 4, &s_task);
}

void rift_net_request_sync(void) {
    if (!s_task || s_state == RIFT_NET_FETCHING) return;
    s_req_pending = true;
    xTaskNotifyGive(s_task);
}

rift_net_state_t rift_net_state(void) { return s_state; }

bool rift_net_last_sync(char *buf, int cap) {
    if (!s_last_sync[0]) return false;
    snprintf(buf, cap, "%s", s_last_sync);
    return true;
}

// Called by the app at boot: prefer the network cache over the sample.
bool rift_net_load_cache(rift_match_t *ms, int *n) {
    return cache_load(ms, n);
}
