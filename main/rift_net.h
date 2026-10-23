// rift_net.h — RIFT BOARD networking worker. Runs HTTP fetches and SNTP in
// its own task; the app task consumes results via its queue. Parsing reuses
// the host-tested rift_model scanners.
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "rift_model.h"

typedef enum {
    RIFT_NET_IDLE = 0,
    RIFT_NET_FETCHING,
    RIFT_NET_OK,
    RIFT_NET_FAIL,
} rift_net_state_t;

void rift_net_init(void);
// Binds the app-owned schedule storage before rift_net_init.
void rift_net_attach(rift_match_t *ms, int *nmatch, rift_day_t *days,
                     int *ndays, volatile bool *dirty);
// Boot-time: load the last synced schedule from NVS (false = none).
bool rift_net_load_cache(rift_match_t *ms, int *n);

/* Per-match detail/window pipeline: the app binds one scratch match; the
 * worker fetches getEventDetails / livestats-window into it and reports
 * back through cb (worker context — enqueue only). */
typedef enum {
    RIFT_NET_RES_DETAILS = 0,
    RIFT_NET_RES_WINDOW,
} rift_net_res_t;
void rift_net_bind_detail(rift_match_t *detail);
void rift_net_set_ready_cb(void (*cb)(rift_net_res_t res, void *user),
                           void *user);
void rift_net_request_details(const char *match_id);
// Fetches the window feed; tries progressively later offsets until the
// final frame reports "finished" (final KDA), using game_start as anchor
// (0 = anchor on the current time, e.g. a live game).
void rift_net_request_window(const char *game_id, int64_t game_start,
                             int game_idx);
// Ask the worker to run SNTP + schedule sync (no-op while already fetching).
void rift_net_request_sync(void);
rift_net_state_t rift_net_state(void);
// Timeline of the last sync (Beijing "MM-DD HH:MM"), for the data page.
bool rift_net_last_sync(char *buf, int cap);
