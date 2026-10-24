// rift_model.h — RIFT BOARD (S16 赛场看板) data model. Pure C, no ESP-IDF or
// LVGL: the JSON scanners, Beijing-time grouping, and formatting here are
// covered by tests/test_rift_model.c on the host.
//
// Data sources (lolesports.com unofficial APIs, probed 2026-10-23):
//   getSchedule      — event list with per-match teams/score/BO (paged)
//   getEventDetails  — per-match game list (ids, state, side)
//   feed livestats window — per-game roster/champions + final-frame stats
// Bans are NOT available from any reachable source in v1; the BP page shows
// picks only and the model carries no ban data.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RIFT_ID_MAX 24
#define RIFT_CODE_MAX 8
#define RIFT_NAME_MAX 28
#define RIFT_STAGE_MAX 16
/* Whole-struct array lives in internal RAM (no PSRAM): ~170 B per match.
 * The 2026 event window holds 46 matches; 48 caps the array under 10 KB.
 * Per-match game lists and window rosters live in ONE scratch rift_detail_t
 * (about 850 B) owned by the app: embedding them here would burn ~40 KB for
 * data only the currently open match ever shows. */
#define RIFT_MATCH_MAX 48
#define RIFT_GAMES_MAX 5
#define RIFT_DAYS_MAX 40
#define RIFT_WHO_MAX 20   /* "KC kyeahoo", "TheShy" */
#define RIFT_CHAMP_MAX 16 /* "Cassiopeia" */
#define RIFT_ROLE_MAX 8
/* Sliding-window size for streamed payload scans: the largest probed JSON
 * element is a 4.2 KB details game; the window must also stay allocable on
 * the no-PSRAM C3 next to Wi-Fi + BLE + LVGL (largest block ~21 KB). */
#define RIFT_JSTREAM_WIN 8192

typedef enum {
    RIFT_ST_UNSTARTED = 0,
    RIFT_ST_LIVE,
    RIFT_ST_DONE,
    RIFT_ST_TBD,        /* scheduled slot with TBD teams (draw pending) */
    RIFT_ST_UNPUBLISHED /* calendar day inside the event, no events yet */
} rift_state_t;

typedef struct {
    char who[RIFT_WHO_MAX];
    char champ[RIFT_CHAMP_MAX];
    char role[RIFT_ROLE_MAX];
    int k, d, a, cs;
} rift_player_t;

typedef struct {
    int kills, gold, towers, dragons, barons;
    rift_player_t p[5];
} rift_side_t;

typedef struct {
    char id[RIFT_ID_MAX];
    int64_t start;            /* epoch seconds, UTC */
    rift_state_t state;
    uint8_t bo;
    char stage[RIFT_STAGE_MAX]; /* blockName: Play-Ins/Swiss/Quarterfinals.. */
    char id1[RIFT_ID_MAX], id2[RIFT_ID_MAX];
    char code1[RIFT_CODE_MAX], code2[RIFT_CODE_MAX];
    char name1[RIFT_NAME_MAX], name2[RIFT_NAME_MAX];
    uint8_t wins1, wins2;
} rift_match_t;

/* Games and window rosters for the match the user opened. id binds it to its
 * rift_match_t so stale data is never shown under a different match. */
typedef struct {
    char id[RIFT_ID_MAX];
    uint8_t game_count;
    struct {
        char id[RIFT_ID_MAX];
        uint8_t state;        /* rift_state_t (unstarted/live/done) */
        uint8_t side1;        /* side of code1 in this game: 0=blue 1=red */
        uint8_t winner;       /* 0=none 1=code1 2=code2 (kills heuristic) */
    } games[RIFT_GAMES_MAX];
    bool window_valid;
    char game_state[12];      /* "in_game", "finished", ... */
    rift_side_t blue, red;
} rift_detail_t;

typedef struct {
    int day;                  /* days-since-epoch, Beijing calendar day */
    int first, count;         /* slice into the sorted match array */
} rift_day_t;

/* --- parsing (returns 0 on success, -1 on malformed input) ---------------- */
/* The schedule endpoint returns every past edition on the page; only
 * matches inside the inclusive Beijing-day window are stored, so the
 * bounded array cannot be starved by stale seasons. */
int rift_parse_schedule(const char *json, rift_match_t *out, int max,
                        int *count, int win0_day, int win1_day);
/* Extracts the schedule payload's pages.older token (false = none: the page
 * was the oldest). The first page slides with the calendar, so sync walks
 * older pages to backfill past event days. */
bool rift_schedule_older(const char *json, char *out, size_t cap);
/* Details fill the scratch struct and patch the owning match's team ids and
 * codes (the details payload is their only source). */
int rift_parse_event_details(const char *json, rift_match_t *m,
                             rift_detail_t *d);
int rift_parse_window(const char *json, rift_detail_t *m, int game_idx);

/* --- streaming scans (device heap: payloads exceed any single free block) --
 * One sliding window yields complete JSON elements one at a time; the
 * full-buffer functions above are one-shot feeds through the same walkers,
 * so host tests cover both paths. The caller owns the window bytes: they
 * must stay valid and the cap must leave room for a NUL sentinel. */
typedef struct rift_jstream rift_jstream_t;

/* Yields every complete object element of the array found under "key" to
 * cb(user, span). cb returning nonzero halts the walk (treated as done);
 * a malformed or over-window element fails the feed with -1. */
typedef int (*rift_elem_cb)(void *user, const char *elem, size_t len);

/* Sliding-window walker state; the window bytes live in the owner struct
 * or buffer right next to this. */
struct rift_jstream {
    char *win;
    size_t cap, len;
    int mode;               /* 0 seek-array, 1 elements, 2 seek-string, 3 done */
    const char *key;
    rift_elem_cb cb;
    void *user;
    char *str_out;
    size_t str_cap;
    bool closed;            /* the array ended (seen ']') */
    bool halted;            /* cb asked to stop */
    /* Called the moment an array closes; retargeting the stream (js-style
     * helpers inside the model) and returning 0 keeps processing pending
     * bytes with the new target, so nothing mid-payload is drained. */
    int (*on_close)(void *user);
};

/* Schedule page walk: events elements append to out. The pages object
 * precedes the events array in the payload, so the token is recovered from
 * a head snapshot instead of a post-array seek. */
typedef struct {
    rift_jstream_t js;
    rift_match_t *out;      /* append base for this page */
    int max, n;             /* capacity / kept matches so far */
    int win0, win1;
    bool closed;            /* the events array ended */
    char head[256];         /* payload head snapshot (holds pages) */
    size_t head_len;
    char older[64];         /* pages.older token ("" = none) */
} rift_sched_stream_t;

/* Details walk: match.teams elements patch the owning match, then the
 * games array (retarget after teams close) fills the detail struct. */
typedef struct {
    rift_jstream_t js;
    rift_match_t *m;
    rift_detail_t *d;
    int tidx;               /* teams elements seen (0, 1) */
    bool teams_closed, games_closed;
} rift_details_stream_t;

void rift_sched_stream_begin(rift_sched_stream_t *st, char *win, size_t cap,
                             rift_match_t *out, int max, int win0_day,
                             int win1_day);
/* Feed one chunk (fin = last). 0 = ok, -1 = malformed/overflow. */
int rift_sched_stream_feed(rift_sched_stream_t *st, const char *data,
                           size_t n, bool fin);
/* Token seen in the head snapshot ("" = no older page). */
void rift_sched_stream_older(rift_sched_stream_t *st, char *out, size_t cap);

void rift_details_stream_begin(rift_details_stream_t *st, char *win,
                               size_t cap, rift_match_t *m,
                               rift_detail_t *d);
/* Feed one chunk (fin = last). 0 = ok, -1 = malformed/overflow. */
int rift_details_stream_feed(rift_details_stream_t *st, const char *data,
                             size_t n, bool fin);

/* --- grouping and lookup --------------------------------------------------- */
void rift_sort_matches(rift_match_t *ms, int n);
int rift_group_days(const rift_match_t *ms, int n, rift_day_t *days,
                    int max_days);
/* The schedule endpoint spans every edition of the tournament; the app
 * keeps only the current event's window (inclusive Beijing days). */
bool rift_in_window(const rift_match_t *m, int min_day, int max_day);
/* In-place compaction: drops out-of-window matches, returns the new count.
 * Boot filters without a second RIFT_MATCH_MAX buffer on the main-task stack. */
int rift_filter_window(rift_match_t *ms, int n, int min_day, int max_day);
int rift_day_of(int64_t start);  /* Beijing days-since-epoch */
int rift_today_bj(void);         /* device clock day (-1 until network time) */

/* --- formatting (all Beijing time) ----------------------------------------- */
int64_t rift_parse_iso(const char *s);                       /* "2026-10-23T11:00:00Z" */
void rift_fmt_hhmm(int64_t start, char *buf, size_t cap);    /* "19:00" */
void rift_fmt_date_label(int day, char *buf, size_t cap);    /* "10月23日 周五" */
void rift_fmt_weekday(int day, char *out);                   /* "周五" */
