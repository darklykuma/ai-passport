// rift_net.c — HTTPS schedule sync + SNTP. One worker task. Schedule pages
// (≈ 44 KB) and event details (≈ 13 KB) stream through an 8 KB sliding
// window (rift_model jstream): next to Wi-Fi + BLE + LVGL no free heap
// block on this board holds a whole payload, so whole-buffer fetches were
// silent no-ops. The live-window endpoint keeps its whole-buffer parse
// behind a tiered allocation (best effort, BP page degrades to sample).
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

/* Sliding window for streamed fetches, carved out once at init while the
 * heap is still whole (the largest block only shrinks once BLE starts). */
static char *s_win;

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

/* Streaming variant of http_to_buf: hands body chunks to on_data (worker
 * context); a nonzero return aborts the read. Returns total bytes
 * delivered, or -1. Header/status handling mirrors http_to_buf. */
static int http_to_stream(
    const char *url,
    int (*on_data)(void *user, const char *buf, int len), void *user) {
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
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    int total = 0;
    int n = 0;
    if (status == 200) {
        char buf[1024];
        while ((n = esp_http_client_read(client, buf, sizeof buf)) > 0) {
            total += n;
            if (on_data && on_data(user, buf, n) != 0) {
                total = -1;
                break;
            }
        }
        if (total >= 0 && n < 0) total = -1;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (status != 200 || total < 0) return -1;
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
    /* The mode probe this replaces was always true on a fresh boot (lwIP
     * default == IMMED), so SNTP never started and the clock stayed in
     * 1970; only an already-valid clock skips the sync. */
    if (time(NULL) > 1780000000) return;
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
        ESP_LOGW(TAG, "sntp gave no time (now=%lld); retry next sync",
                 (long long)now);
        esp_netif_sntp_deinit();
        return;
    }
    ESP_LOGI(TAG, "sntp time ok");
    esp_netif_sntp_deinit();
}

typedef struct {
    rift_sched_stream_t st;
    int rc;
} page_ctx_t;

static int page_feed(void *user, const char *buf, int len) {
    page_ctx_t *ctx = (page_ctx_t *)user;
    if (rift_sched_stream_feed(&ctx->st, buf, (size_t)len, false)) {
        ctx->rc = -1;
        return -1;   /* abort the read */
    }
    return 0;
}

/* One schedule page through the sliding window: appends in-window matches
 * at out (keep), yields the pages.older token ("" = oldest page). */
static bool sync_page(const char *url, rift_match_t *out, int max, int *keep,
                      char *older, size_t older_cap) {
    if (!s_win) {
        ESP_LOGE(TAG, "stream window unavailable (alloc failed at init)");
        return false;
    }
    page_ctx_t ctx = {.rc = 0};
    rift_sched_stream_begin(&ctx.st, s_win, RIFT_JSTREAM_WIN, out, max,
                            s_win0, s_win1);
    int total = http_to_stream(url, page_feed, &ctx);
    *keep = ctx.st.n;
    bool ok = total >= 0 && ctx.rc == 0 && ctx.st.closed;
    if (ok)
        rift_sched_stream_older(&ctx.st, older, older_cap);
    else
        ESP_LOGW(TAG, "schedule page failed (fetch=%d parse=%d closed=%d)",
                 total, ctx.rc, ctx.st.closed);
    return ok;
}

/* The first schedule page only spans the days around now; past event days
 * slide off it. Walk pages.older to backfill the whole window so a sync
 * never drops results it already showed. Bounded by page count and by two
 * consecutive pages that added nothing in-window (everything older is out). */
static void sync_once(void) {
    char url[384], token[64];
    token[0] = '\0';
    int total = 0, empty_pages = 0;
    ESP_LOGI(TAG, "sync: heap free=%u largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    for (int page = 0; page < RIFT_SYNC_PAGES_MAX; page++) {
        if (token[0])
            snprintf(url, sizeof url, SCHEDULE_URL "&pageToken=%s", token);
        else
            snprintf(url, sizeof url, "%s", SCHEDULE_URL);
        int keep = 0;
        char older[64] = "";
        if (!sync_page(url, s_ms + total, RIFT_MATCH_MAX - total, &keep,
                       older, sizeof older))
            break;
        total += keep;
        ESP_LOGI(TAG, "schedule page %d: +%d in-window (%d total)", page,
                 keep, total);
        snprintf(token, sizeof token, "%s", older);
        if (!token[0]) break;
        if (total >= RIFT_MATCH_MAX) break;
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
    } else {
        ESP_LOGW(TAG, "schedule sync produced nothing");
    }
}

typedef struct {
    rift_details_stream_t st;
    int rc;
} details_ctx_t;

static int details_feed(void *user, const char *buf, int len) {
    details_ctx_t *ctx = (details_ctx_t *)user;
    if (rift_details_stream_feed(&ctx->st, buf, (size_t)len, false)) {
        ctx->rc = -1;
        return -1;
    }
    return 0;
}

static void fetch_details_once(void) {
    if (!s_detail || !s_detail_games || !s_pending_match_id[0]) return;
    if (!s_win) {
        ESP_LOGE(TAG, "stream window unavailable (alloc failed at init)");
        return;
    }
    char url[160];
    snprintf(url, sizeof url,
             "https://esports-api.lolesports.com/persisted/gw/"
             "getEventDetails?hl=en-US&id=%s",
             s_pending_match_id);
    details_ctx_t ctx = {.rc = 0};
    rift_details_stream_begin(&ctx.st, s_win, RIFT_JSTREAM_WIN, s_detail,
                              s_detail_games);
    int total = http_to_stream(url, details_feed, &ctx);
    if (total >= 0 && ctx.rc == 0 && ctx.st.games_closed) {
        /* Bind the fresh games to the match they belong to, so the app can
         * tell this scratch pair from a stale one opened earlier. */
        strcpy(s_detail_games->id, s_pending_match_id);
        if (s_ready_cb) s_ready_cb(RIFT_NET_RES_DETAILS, s_ready_user);
    } else {
        ESP_LOGW(TAG, "event details failed (fetch=%d parse=%d closed=%d)",
                 total, ctx.rc, ctx.st.games_closed);
    }
}

/* Whole-buffer fetches must at least say why they failed: the window
 * payload (18+ KB) needs the largest remaining block after BLE started. */
static char *buf_alloc(int want, int *got) {
    static const int TIERS[] = {96 * 1024, 48 * 1024, 32 * 1024,
                                24 * 1024, 16 * 1024};
    for (unsigned i = 0; i < sizeof TIERS / sizeof TIERS[0]; i++) {
        if (TIERS[i] > want) continue;
        char *p = heap_caps_malloc((size_t)TIERS[i], MALLOC_CAP_8BIT);
        if (p) {
            *got = TIERS[i];
            if (TIERS[i] < want)
                ESP_LOGW(TAG, "fetch buffer shrunk %d -> %d B", want,
                         TIERS[i]);
            return p;
        }
    }
    ESP_LOGW(TAG, "no heap for a %d B fetch buffer (largest %u B)", want,
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return NULL;
}

/* The window feed returns the frames around startingTime; a finished game
 * yields its final stats from any late offset. Probe later anchors until
 * the last frame says "finished" (probed: start+45min hit for a 32:32
 * game); an in-progress game answers at the current time. */
static void fetch_window_once(void) {
    if (!s_detail || !s_detail_games || !s_pending_game_id[0]) return;
    static const int OFFSET_MIN[] = {0, 35, 45, 60, 90};
    int cap = 0;
    char *buf = buf_alloc(RIFT_BUF_MAX, &cap);
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
        int len = http_to_buf(url, buf, cap);
        if (len <= 0) continue;
        if (len >= cap) {
            ESP_LOGW(TAG, "window payload hit the %d B buffer cap", cap);
            break;
        }
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
    s_win = heap_caps_malloc(RIFT_JSTREAM_WIN, MALLOC_CAP_8BIT);
    if (!s_win)
        ESP_LOGE(TAG, "stream window alloc failed (%d B): sync is dead",
                 RIFT_JSTREAM_WIN);
    if (xTaskCreate(net_task, "rift_net", RIFT_SYNC_TASK_STACK, NULL, 4,
                    &s_task) != pdPASS) {
        ESP_LOGE(TAG, "net task create failed");
        s_task = NULL;
    }
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
