// rift_model.c — JSON scanners and Beijing-time helpers for RIFT BOARD.
// Hand-rolled pull scanning (not a DOM): the payloads are machine-generated
// with stable shapes, and the device cannot afford a full JSON tree. The
// scanner reads the payload in place and never copies more than one field.
// Runs on host (tests/test_rift_model.c) and on device unchanged.
#include "rift_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

bool rift_in_window(const rift_match_t *m, int min_day, int max_day);

/* --- minimal JSON pull scanner --------------------------------------------- */

typedef struct {
    const char *p, *end;
} jp_t;

static void jp_ws(jp_t *j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n'
                             || *j->p == '\r'))
        j->p++;
}

static int jp_peek(jp_t *j) {
    jp_ws(j);
    return j->p < j->end ? (unsigned char)*j->p : -1;
}

static int jp_take(jp_t *j, char c) {
    if (jp_peek(j) != (unsigned char)c) return -1;
    j->p++;
    return 0;
}

static int hex4(const char *s) {
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

/* Scans a string literal into out (NUL-terminated, truncated at cap-1).
 * Codepoints above 0x7F become '?' — every string the v1 UI shows is ASCII
 * (team codes/names, summoner names, champion ids). */
static int jp_string(jp_t *j, char *out, int cap) {
    if (jp_take(j, '"')) return -1;
    int n = 0;
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\') {
            if (j->p >= j->end) return -1;
            char e = *j->p++;
            switch (e) {
            case '"': case '\\': case '/': c = e; break;
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': {
                if (j->end - j->p < 4) return -1;
                int cp = hex4(j->p);
                j->p += 4;
                if (cp < 0) return -1;
                c = cp < 0x80 ? (char)cp : '?';
                break;
            }
            default: return -1;
            }
        }
        if (out && n < cap - 1) out[n++] = c;
    }
    if (jp_take(j, '"')) return -1;
    if (out) out[n] = '\0';
    return 0;
}

/* String length without materializing (for skipping). */
static int jp_string_len(jp_t *j) {
    if (jp_take(j, '"')) return -1;
    while (j->p < j->end && *j->p != '"') {
        if (*j->p == '\\' && j->p + 1 < j->end) j->p++;
        j->p++;
    }
    return jp_take(j, '"');
}

static int jp_long(jp_t *j, long *out) {
    jp_ws(j);
    int neg = 0;
    if (j->p < j->end && (*j->p == '-' || *j->p == '+'))
        neg = *j->p++ == '-';
    if (j->p >= j->end || *j->p < '0' || *j->p > '9') return -1;
    long v = 0;
    while (j->p < j->end && *j->p >= '0' && *j->p <= '9')
        v = v * 10 + (*j->p++ - '0');
    *out = neg ? -v : v;
    return 0;
}

/* Skips one complete value (object/array/string/number/bool/null). */
static int jp_skip(jp_t *j) {
    int c = jp_peek(j);
    if (c < 0) return -1;
    if (c == '"') return jp_string_len(j);
    if (c == '{' || c == '[') {
        char open = (char)c, close = open == '{' ? '}' : ']';
        j->p++;
        int depth = 1;
        while (j->p < j->end && depth > 0) {
            char ch = *j->p++;
            if (ch == '"') {
                j->p--;
                if (jp_string_len(j)) return -1;
            } else if (ch == open) {
                depth++;
            } else if (ch == close) {
                depth--;
            }
        }
        return depth == 0 ? 0 : -1;
    }
    while (j->p < j->end && *j->p != ',' && *j->p != '}' && *j->p != ']')
        j->p++;
    return 0;
}

/* Finds `key` in the object at the scanner position. Accepts a position
 * before the object's '{' or just after a previously read member (successive
 * jp_find calls walk one object). Success leaves the scanner at the value;
 * failure leaves it where the key could no longer match ('}' untouched). */
static int jp_find(jp_t *j, const char *key) {
    if (jp_peek(j) == '{') j->p++;
    char buf[40];
    for (;;) {
        int c = jp_peek(j);
        if (c == '}' || c == -1) return -1;
        if (c == ',') { j->p++; continue; }
        if (c != '"') return -1;
        if (jp_string(j, buf, sizeof buf)) return -1;
        if (jp_take(j, ':')) return -1;
        if (strcmp(buf, key) == 0) return 0;
        if (jp_skip(j)) return -1;
    }
}

/* Enters an array after '['; jp_more reports elements, consuming element
 * separators (',') and the final ']' — so after jp_more returns 1 the
 * scanner sits exactly on the next element. Returns -1 on truncated input
 * (end of buffer before the closing bracket). */
static int jp_array(jp_t *j) {
    return jp_take(j, '[');
}

static int jp_more(jp_t *j) {
    int c = jp_peek(j);
    if (c == ',') { j->p++; c = jp_peek(j); }
    if (c == ']') { j->p++; return 0; }
    return c == -1 ? -1 : 1;
}

/* Reads member `key` as a string from the object spanning [obj_start, obj_end)
 * (positioned at its '{'). Always restarts from the object start, so field
 * order never matters. Returns false when the key is absent. */
static bool jp_get_str(const char *obj_start, const char *obj_end,
                       const char *key, char *out, int cap) {
    jp_t q = {obj_start, obj_end};
    if (jp_find(&q, key)) return false;
    return jp_string(&q, out, cap) == 0;
}

static bool jp_get_long(const char *obj_start, const char *obj_end,
                        const char *key, long *out) {
    jp_t q = {obj_start, obj_end};
    if (jp_find(&q, key)) return false;
    return jp_long(&q, out) == 0;
}

/* Positions a sub-scan at member `key`'s value (object/array) within
 * [obj_start, obj_end). */
static bool jp_goto(const char *obj_start, const char *obj_end,
                    const char *key, jp_t *out) {
    jp_t q = {obj_start, obj_end};
    if (jp_find(&q, key)) return false;
    out->p = q.p;
    out->end = obj_end;
    return true;
}

/* --- time ------------------------------------------------------------------- */

/* Howard Hinnant's days_from_civil / civil_from_days. */
static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)(153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp + (mp < 10 ? 3 : -9));
    *y = (int)(yy + (*m <= 2));
}

/* "2026-10-23T11:00:00Z" (offset form also accepted) -> epoch seconds. */
static int64_t parse_iso(const char *s);

int64_t rift_parse_iso(const char *s) {
    return parse_iso(s);
}

static int64_t parse_iso(const char *s) {
    if (strlen(s) < 19) return 0;
    int64_t days = days_from_civil(atoi(s), atoi(s + 5), atoi(s + 8));
    int64_t sec = ((days * 24 + atoi(s + 11)) * 60 + atoi(s + 14)) * 60
                  + atoi(s + 17);
    if (s[19] == '+' || s[19] == '-') {
        int64_t off = atoi(s + 20) * 3600 + atoi(s + 23) * 60;
        sec += s[19] == '+' ? -off : off;
    }
    return sec;
}

#define RIFT_BJ_OFFSET (8 * 3600)

int rift_day_of(int64_t start) {
    return (int)((start + RIFT_BJ_OFFSET) / 86400);
}

int rift_today_bj(void) {
    time_t now = time(NULL);
    if (now < 1780000000) return -1;   /* before 2026: no real time yet */
    return (int)((now + RIFT_BJ_OFFSET) / 86400);
}

void rift_fmt_hhmm(int64_t start, char *buf, size_t cap) {
    int sec = (int)((start + RIFT_BJ_OFFSET) % 86400);
    snprintf(buf, cap, "%02d:%02d", sec / 3600, (sec % 3600) / 60);
}

static const char *WEEK[7] = {"周日", "周一", "周二", "周三", "周四", "周五",
                              "周六"};

void rift_fmt_weekday(int day, char *out) {
    strcpy(out, WEEK[(int)(((int64_t)day + 4) % 7)]);  /* 1970-01-01 = 周四 */
}

void rift_fmt_date_label(int day, char *buf, size_t cap) {
    int y, m, d;
    char wd[8];
    civil_from_days(day, &y, &m, &d);
    rift_fmt_weekday(day, wd);
    snprintf(buf, cap, "%d月%d日 %s", m, d, wd);
}

/* --- getSchedule ------------------------------------------------------------ */

static rift_state_t state_of(const char *s, const char *c1, const char *c2) {
    if (strcmp(s, "completed") == 0) return RIFT_ST_DONE;
    if (strcmp(s, "started") == 0) return RIFT_ST_LIVE;
    if (strcmp(c1, "TBD") == 0 || strcmp(c2, "TBD") == 0) return RIFT_ST_TBD;
    return RIFT_ST_UNSTARTED;
}

int rift_parse_schedule(const char *json, rift_match_t *out, int max,
                        int *count, int win0_day, int win1_day) {
    jp_t j = {json, json + strlen(json)};
    *count = 0;
    if (jp_find(&j, "data")) return -1;
    if (jp_find(&j, "schedule")) return -1;
    if (jp_find(&j, "events")) return -1;
    if (jp_array(&j)) return -1;
    int n = 0;
    for (;;) {
        int more = jp_more(&j);
        if (more < 0) return -1;
        if (!more) break;
        if (n >= max) break;
        const char *es = j.p;                /* element start */
        jp_t e = j;
        if (jp_skip(&e)) return -1;
        const char *ee = e.p;                /* element end */
        j = e;

        char type[12] = "", state[12] = "", iso[40] = "";
        rift_match_t *m = &out[n];
        memset(m, 0, sizeof *m);

        jp_get_str(es, ee, "type", type, sizeof type);
        jp_get_str(es, ee, "state", state, sizeof state);
        jp_get_str(es, ee, "blockName", m->stage, RIFT_STAGE_MAX);
        if (jp_get_str(es, ee, "startTime", iso, sizeof iso))
            m->start = parse_iso(iso);
        if (strcmp(type, "match")) continue;

        jp_t mv;
        if (!jp_goto(es, ee, "match", &mv)) continue;
        const char *ms = mv.p;
        jp_t me = mv;
        if (jp_skip(&me)) return -1;
        const char *mend = me.p;

        jp_get_str(ms, mend, "id", m->id, RIFT_ID_MAX);
        long bo = 1;
        jp_t sv;
        if (jp_goto(ms, mend, "strategy", &sv)) {
            const char *ss = sv.p;
            jp_t se = sv;
            if (jp_skip(&se)) return -1;
            jp_get_long(ss, se.p, "count", &bo);
        }
        m->bo = (uint8_t)bo;

        jp_t tv;
        if (!jp_goto(ms, mend, "teams", &tv)) continue;
        if (jp_array(&tv)) continue;
        for (int t = 0; t < 2; t++) {
            if (jp_more(&tv) != 1) break;
            const char *ts = tv.p;
            jp_t te = tv;
            if (jp_skip(&te)) return -1;
            const char *tee = te.p;
            tv = te;

            char code[RIFT_CODE_MAX] = "", name[RIFT_NAME_MAX] = "";
            char tid[RIFT_ID_MAX] = "";
            long wins = 0;
            jp_get_str(ts, tee, "id", tid, sizeof tid);
            jp_get_str(ts, tee, "code", code, sizeof code);
            jp_get_str(ts, tee, "name", name, sizeof name);
            jp_t rv;
            if (jp_goto(ts, tee, "result", &rv)) {
                const char *rs = rv.p;
                jp_t re = rv;
                if (jp_skip(&re)) return -1;
                jp_get_long(rs, re.p, "gameWins", &wins);
            }
            if (t == 0) {
                strcpy(m->id1, tid);
                strcpy(m->code1, code);
                strcpy(m->name1, name);
                m->wins1 = (uint8_t)wins;
            } else {
                strcpy(m->id2, tid);
                strcpy(m->code2, code);
                strcpy(m->name2, name);
                m->wins2 = (uint8_t)wins;
            }
        }
        m->state = state_of(state, m->code1, m->code2);
        if (!rift_in_window(m, win0_day, win1_day)) continue;
        n++;
    }
    *count = n;
    return 0;
}

/* --- getEventDetails -------------------------------------------------------- */

int rift_parse_event_details(const char *json, rift_match_t *m,
                             rift_detail_t *d) {
    const char *end = json + strlen(json);
    jp_t dv, ev, mv, gv;
    if (!jp_goto(json, end, "data", &dv)) return -1;
    if (!jp_goto(dv.p, end, "event", &ev)) return -1;
    if (!jp_goto(ev.p, end, "match", &mv)) return -1;
    /* Team ids only exist here (the schedule payload omits them); they key
     * the per-game side resolution below. */
    {
        jp_t tv2;
        if (!jp_goto(mv.p, end, "teams", &tv2)) return -1;
        if (jp_array(&tv2)) return -1;
        for (int t = 0; t < 2; t++) {
            if (jp_more(&tv2) != 1) break;
            const char *ts = tv2.p;
            jp_t te = tv2;
            if (jp_skip(&te)) return -1;
            const char *tee = te.p;
            tv2 = te;
            char tid[RIFT_ID_MAX] = "", tcode[RIFT_CODE_MAX] = "";
            jp_get_str(ts, tee, "id", tid, sizeof tid);
            jp_get_str(ts, tee, "code", tcode, sizeof tcode);
            if (t == 0) {
                strcpy(m->id1, tid);
                if (tcode[0]) strcpy(m->code1, tcode);
            } else {
                strcpy(m->id2, tid);
                if (tcode[0]) strcpy(m->code2, tcode);
            }
        }
    }
    if (!jp_goto(mv.p, end, "games", &gv)) return -1;
    if (jp_array(&gv)) return -1;

    d->game_count = 0;
    for (;;) {
        int more = jp_more(&gv);
        if (more < 0) return -1;
        if (!more || d->game_count >= RIFT_GAMES_MAX) break;
        const char *gs = gv.p;
        jp_t ge = gv;
        if (jp_skip(&ge)) return -1;
        const char *gee = ge.p;
        gv = ge;

        char gid[RIFT_ID_MAX] = "", state[12] = "";
        uint8_t side1 = 0;
        jp_get_str(gs, gee, "id", gid, sizeof gid);
        jp_get_str(gs, gee, "state", state, sizeof state);

        jp_t tv;
        if (jp_goto(gs, gee, "teams", &tv)) {
            if (jp_array(&tv)) return -1;
            for (int t = 0; t < 2 && jp_more(&tv); t++) {
                const char *ts = tv.p;
                jp_t te = tv;
                if (jp_skip(&te)) return -1;
                const char *tee = te.p;
                tv = te;
                char tid[RIFT_ID_MAX] = "", side[8] = "";
                jp_get_str(ts, tee, "id", tid, sizeof tid);
                jp_get_str(ts, tee, "side", side, sizeof side);
                /* Position t belongs to whichever schedule team id appears
                 * first here; resolve code1's side by id comparison. */
                if (strcmp(tid, m->id1) == 0)
                    side1 = strcmp(side, "red") == 0 ? 1 : 0;
            }
        }
        d->games[d->game_count].state =
            strcmp(state, "completed") == 0 ? RIFT_ST_DONE
            : strcmp(state, "started") == 0 ? RIFT_ST_LIVE
            : RIFT_ST_UNSTARTED;
        strcpy(d->games[d->game_count].id, gid);
        d->games[d->game_count].side1 = side1;
        d->games[d->game_count].winner = 0;
        d->game_count++;
    }
    return 0;
}

/* --- livestats window ------------------------------------------------------- */

static int parse_side(jp_t *meta_side, jp_t *frame_side, rift_side_t *side) {
    /* roster: gameMetadata.<side>TeamMetadata.participantMetadata[5], in
     * participantId order; the frame's participants[] use the same order.
     * jp_find only scans forward, so every key read restarts from its
     * object start via the jp_get_long / jp_goto helpers. */
    const char *ms = meta_side->p;
    jp_t me = *meta_side;
    if (jp_skip(&me)) return -1;
    const char *mend = me.p;
    jp_t pm;
    if (!jp_goto(ms, mend, "participantMetadata", &pm)) return -1;
    if (jp_array(&pm)) return -1;
    for (int i = 0; i < 5 && jp_more(&pm) == 1; i++) {
        const char *rs = pm.p;
        jp_t re = pm;
        if (jp_skip(&re)) return -1;
        const char *ree = re.p;
        pm = re;
        rift_player_t *pl = &side->p[i];
        memset(pl, 0, sizeof *pl);
        jp_get_str(rs, ree, "summonerName", pl->who, RIFT_WHO_MAX);
        jp_get_str(rs, ree, "championId", pl->champ, RIFT_CHAMP_MAX);
        jp_get_str(rs, ree, "role", pl->role, RIFT_ROLE_MAX);
    }

    const char *fs = frame_side->p;
    jp_t fe = *frame_side;
    if (jp_skip(&fe)) return -1;
    const char *fend = fe.p;
    long v = 0;
    if (jp_get_long(fs, fend, "totalKills", &v)) side->kills = (int)v;
    v = 0;
    if (jp_get_long(fs, fend, "totalGold", &v)) side->gold = (int)v;
    v = 0;
    if (jp_get_long(fs, fend, "towers", &v)) side->towers = (int)v;
    v = 0;
    if (jp_get_long(fs, fend, "dragons", &v)) side->dragons = (int)v;
    v = 0;
    if (jp_get_long(fs, fend, "barons", &v)) side->barons = (int)v;

    jp_t pv;
    if (!jp_goto(fs, fend, "participants", &pv)) return -1;
    if (jp_array(&pv)) return -1;
    for (int i = 0; i < 5 && jp_more(&pv) == 1; i++) {
        const char *rs = pv.p;
        jp_t re = pv;
        if (jp_skip(&re)) return -1;
        const char *ree = re.p;
        pv = re;
        rift_player_t *pl = &side->p[i];
        v = 0;
        if (jp_get_long(rs, ree, "kills", &v)) pl->k = (int)v;
        v = 0;
        if (jp_get_long(rs, ree, "deaths", &v)) pl->d = (int)v;
        v = 0;
        if (jp_get_long(rs, ree, "assists", &v)) pl->a = (int)v;
        v = 0;
        if (jp_get_long(rs, ree, "creepScore", &v)) pl->cs = (int)v;
    }
    return 0;
}

int rift_parse_window(const char *json, rift_detail_t *m, int game_idx) {
    if (game_idx < 0 || game_idx >= m->game_count) return -1;
    const char *end = json + strlen(json);

    jp_t meta;
    if (!jp_goto(json, end, "gameMetadata", &meta)) return -1;
    jp_t blue_meta;
    if (!jp_goto(meta.p, end, "blueTeamMetadata", &blue_meta)) return -1;
    jp_t red_meta;
    if (!jp_goto(meta.p, end, "redTeamMetadata", &red_meta)) return -1;

    jp_t fr;
    if (!jp_goto(json, end, "frames", &fr)) return -1;
    if (jp_array(&fr)) return -1;
    const char *last = NULL;
    int nframes = 0;
    for (;;) {
        int more = jp_more(&fr);
        if (more < 0) return -1;
        if (!more) break;
        last = fr.p;
        nframes++;
        if (jp_skip(&fr)) return -1;
    }
    if (!nframes || !last) return -1;

    char gs[12] = "";
    jp_t lf = {last, fr.p};   /* fr.p sits past ']' = frames end */
    jp_t le = lf;
    if (jp_skip(&le)) return -1;
    if (!jp_get_str(last, le.p, "gameState", gs, sizeof gs)) return -1;
    strcpy(m->game_state, gs);

    memset(&m->blue, 0, sizeof m->blue);
    memset(&m->red, 0, sizeof m->red);
    jp_t lb, lr;
    if (!jp_goto(last, le.p, "blueTeam", &lb)) return -1;
    if (!jp_goto(last, le.p, "redTeam", &lr)) return -1;
    jp_t lb_end = lb, lr_end = lr;
    if (jp_skip(&lb_end) || jp_skip(&lr_end)) return -1;
    if (parse_side(&blue_meta, &lb, &m->blue)) return -1;
    if (parse_side(&red_meta, &lr, &m->red)) return -1;
    (void)lr_end;
    m->window_valid = true;

    /* Game winner: the feed has no explicit winner, so a finished frame
     * decides by team kills, then gold, then towers (v1 heuristic). */
    m->games[game_idx].winner = 0;
    if (strcmp(m->game_state, "finished") == 0) {
        int b = m->blue.kills, r = m->red.kills;
        if (b == r) { b = m->blue.gold; r = m->red.gold; }
        if (b == r) { b = m->blue.towers; r = m->red.towers; }
        if (b != r) {
            bool blue_won = b > r;
            bool s1_blue = m->games[game_idx].side1 == 0;
            m->games[game_idx].winner = blue_won == s1_blue ? 1 : 2;
        }
    }
    return 0;
}

/* --- grouping ---------------------------------------------------------------- */

static int cmp_match(const void *a, const void *b) {
    const rift_match_t *x = a, *y = b;
    if (x->start < y->start) return -1;
    if (x->start > y->start) return 1;
    return 0;
}

void rift_sort_matches(rift_match_t *ms, int n) {
    qsort(ms, (size_t)n, sizeof *ms, cmp_match);
}

int rift_group_days(const rift_match_t *ms, int n, rift_day_t *days,
                    int max_days) {
    int nd = 0;
    for (int i = 0; i < n; i++) {
        int d = rift_day_of(ms[i].start);
        if (nd == 0 || days[nd - 1].day != d) {
            if (nd >= max_days) break;
            days[nd].day = d;
            days[nd].first = i;
            days[nd].count = 0;
            nd++;
        }
        days[nd - 1].count++;
    }
    return nd;
}

bool rift_in_window(const rift_match_t *m, int min_day, int max_day) {
    int d = rift_day_of(m->start);
    return d >= min_day && d <= max_day;
}

int rift_filter_window(rift_match_t *ms, int n, int min_day, int max_day) {
    /* keep <= i always, so the write never clobbers an entry still to read. */
    int keep = 0;
    for (int i = 0; i < n; i++)
        if (rift_in_window(&ms[i], min_day, max_day)) ms[keep++] = ms[i];
    return keep;
}
