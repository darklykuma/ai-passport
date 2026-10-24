// tests/test_rift_model.c — host tests for the RIFT BOARD model. The
// fixtures are real API payloads probed on 2026-10-23 (Worlds 2026
// Play-Ins/Swiss); expected values below are the probed ground truth.
/* Asserts are the test: never let NDEBUG (e.g. zig cc -O1) strip them. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../main/rift_model.c"

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    assert(f && "fixture missing");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    assert(fread(buf, 1, (size_t)n, f) == (size_t)n);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void test_time_helpers(void) {
    /* 2026-10-23T11:00:00Z == 19:00 Beijing == day 20749 (from Unix epoch) */
    int64_t t = parse_iso("2026-10-23T11:00:00Z");
    assert(t == 1792753200);
    assert(rift_day_of(t) == 20749);
    char buf[32];
    rift_fmt_hhmm(t, buf, sizeof buf);
    assert(strcmp(buf, "19:00") == 0);
    rift_fmt_date_label(20749, buf, sizeof buf);
    assert(strcmp(buf, "10月23日 周五") == 0);
    rift_fmt_weekday(20749, buf);
    assert(strcmp(buf, "周五") == 0);
    /* offset form: +08:00 of 19:00 local equals the same instant */
    assert(parse_iso("2026-10-23T19:00:00+08:00") == t);
}

/* The whole-page parse keeps only the 2026 window (the endpoint returns
 * every past edition; 34 stale 2025 matches must not starve the array). */
static int parse_window_fixture(rift_match_t *ms) {
    char *json = slurp("tests/fixtures/rift/schedule.json");
    int n = 0;
    int r = rift_parse_schedule(
        json, ms, RIFT_MATCH_MAX, &n,
        rift_day_of(parse_iso("2026-10-15T00:00:00Z")),
        rift_day_of(parse_iso("2026-11-15T23:59:59Z")));
    free(json);
    assert(r == 0);
    return n;
}

static void test_schedule(void) {
    rift_match_t ms[RIFT_MATCH_MAX];
    int n = parse_window_fixture(ms);
    assert(n == 46);   /* all 2026 matches on the probed page */

    /* KC 3-0 MVK, Play-Ins BO5, 2026-10-17 02:00 Beijing (several KC-MVK
     * BO5s exist in the page; pin the 10-17 one by its exact start). */
    rift_match_t *kc = NULL;
    for (int i = 0; i < n; i++)
        if (strcmp(ms[i].code1, "KC") == 0 && strcmp(ms[i].code2, "MVK") == 0
            && ms[i].start == 1792173600)
            kc = &ms[i];
    assert(kc && "KC vs MVK match missing");
    assert(kc->state == RIFT_ST_DONE);
    assert(kc->bo == 5);
    assert(kc->wins1 == 3 && kc->wins2 == 0);
    assert(strcmp(kc->name1, "Karmine Corp") == 0);
    assert(strcmp(kc->stage, "Play-Ins") == 0);
    assert(kc->id[0]);
    char hhmm[8];
    rift_fmt_hhmm(kc->start, hhmm, sizeof hhmm);
    assert(strcmp(hhmm, "02:00") == 0);
    assert(rift_day_of(kc->start) == 20743);   /* 10-17: 18:00Z +8h rolls
                                                  into the Beijing day */

    /* the 10-24 GEN vs IG unstarted BO1 (real pairing, 01:00 BJ) */
    int gen_ig = 0;
    for (int i = 0; i < n; i++)
        if (strcmp(ms[i].code1, "GEN") == 0 && strcmp(ms[i].code2, "IG") == 0
            && ms[i].state == RIFT_ST_UNSTARTED)
            gen_ig = i;
    assert(gen_ig);
    assert(ms[gen_ig].bo == 1 && ms[gen_ig].wins1 == 0);

    /* TBD slots exist (post-draw Swiss rounds) and parse as TBD */
    int tbds = 0;
    for (int i = 0; i < n; i++)
        if (ms[i].state == RIFT_ST_TBD) tbds++;
    assert(tbds == 32);

    /* grouping: 19 Beijing days across the 2026 event */
    rift_sort_matches(ms, n);
    rift_day_t days[RIFT_DAYS_MAX];
    int nd = rift_group_days(ms, n, days, RIFT_DAYS_MAX);
    assert(nd == 19);
    assert(days[0].count == 2 && days[0].day == 20742);  /* 10-16 Play-Ins */
    assert(days[1].count == 2);                          /* 10-17 = 20743 */
    int d24 = -1, d23 = -1;
    for (int i = 0; i < nd; i++) {
        if (days[i].day == 20750) d24 = i;    /* 10-24 */
        if (days[i].day == 20749) d23 = i;    /* 10-23 */
    }
    assert(d24 >= 0 && days[d24].count == 8);     /* Swiss round 1 */
    assert(d23 == -1);                            /* rest day before round 1 */

    /* truncated payload must fail cleanly, not crash */
    int n2 = 0;
    assert(rift_parse_schedule("{\"data\":{\"schedule\":{\"events\":[",
                               ms, RIFT_MATCH_MAX, &n2, 20741, 20757) == -1);
}

/* Backfill: the first schedule page slides with the calendar; sync walks
 * pages.older and parses each older page into the tail of the array. */
static void test_schedule_older(void) {
    char *json = slurp("tests/fixtures/rift/schedule.json");
    char tok[256];
    assert(rift_schedule_older(json, tok, sizeof tok));
    assert(strcmp(tok, "b2xkZXI6OjExMzQ3NTc5ODAwNjY2NDU4Mg==") == 0);
    char tiny[4];
    assert(rift_schedule_older(json, tiny, sizeof tiny));
    assert(strcmp(tiny, "b2x") == 0);   /* truncated, still terminated */
    free(json);
    /* "older": null (oldest page reached) and a missing pages block both
     * end the walk */
    char out[8];
    assert(!rift_schedule_older(
        "{\"data\":{\"schedule\":{\"pages\":{\"older\":null}}}}",
        out, sizeof out));
    assert(out[0] == '\0');
    assert(!rift_schedule_older("{\"data\":{\"schedule\":{}}}",
                                out, sizeof out));
}

/* The append pattern sync uses: an older page parses at ms+total, then the
 * whole array re-sorts and re-groups (new 10-16 match joins that day). */
static void test_schedule_backfill_append(void) {
    rift_match_t ms[RIFT_MATCH_MAX];
    int n = parse_window_fixture(ms);
    const char *older =
        "{\"data\":{\"schedule\":{\"events\":[{\"type\":\"match\","
        "\"state\":\"completed\",\"startTime\":\"2026-10-15T20:00:00Z\","
        "\"blockName\":\"Play-Ins\",\"match\":{\"teams\":["
        "{\"code\":\"A\",\"result\":{\"gameWins\":1}},"
        "{\"code\":\"B\",\"result\":{\"gameWins\":0}}]}}]}}";
    int k = 0;
    assert(rift_parse_schedule(
               older, ms + n, RIFT_MATCH_MAX - n, &k, 20741, 20757) == 0);
    assert(k == 1);   /* 10-15T20:00Z rolls into Beijing day 10-16 */
    rift_sort_matches(ms, n + k);
    rift_day_t days[RIFT_DAYS_MAX];
    int nd = rift_group_days(ms, n + k, days, RIFT_DAYS_MAX);
    assert(nd == 19);
    assert(days[0].day == 20742 && days[0].count == 3);  /* was 2 */
    int found = 0;
    for (int i = days[0].first; i < days[0].first + days[0].count; i++)
        if (strcmp(ms[i].code1, "A") == 0 && ms[i].wins1 == 1
            && ms[i].state == RIFT_ST_DONE)
            found = 1;
    assert(found);
}

static void test_event_details(void) {
    rift_match_t ms[RIFT_MATCH_MAX];
    int n = parse_window_fixture(ms);
    rift_match_t *kc = NULL;
    for (int i = 0; i < n; i++)
        if (strcmp(ms[i].code1, "KC") == 0 && strcmp(ms[i].code2, "MVK") == 0
            && ms[i].start == 1792173600)
            kc = &ms[i];
    assert(kc);

    char *json = slurp("tests/fixtures/rift/event_details.json");
    rift_detail_t d;
    memset(&d, 0, sizeof d);
    assert(rift_parse_event_details(json, kc, &d) == 0);
    /* The payload lists all 5 BO5 slots: 3 completed + 2 "unneeded". */
    assert(d.game_count == 5);
    assert(d.games[0].state == RIFT_ST_DONE);
    assert(d.games[2].state == RIFT_ST_DONE);
    assert(d.games[3].state == RIFT_ST_UNSTARTED);
    assert(d.games[0].id[0]);
    assert(kc->id1[0] && kc->id2[0]);  /* ids live in the details payload */
    assert(strcmp(kc->code1, "KC") == 0);
    assert(d.games[0].side1 == 0);   /* KC blue in G1 (probed) */
    assert(d.games[1].side1 == 0);   /* KC blue in G2 (probed) */
    assert(d.games[2].side1 == 1);   /* KC red in G3 (probed) */
    free(json);
}

static void test_window(void) {
    rift_match_t ms[RIFT_MATCH_MAX];
    int n = parse_window_fixture(ms);
    rift_match_t *kc = NULL;
    for (int i = 0; i < n; i++)
        if (strcmp(ms[i].code1, "KC") == 0 && strcmp(ms[i].code2, "MVK") == 0
            && ms[i].start == 1792173600)
            kc = &ms[i];
    assert(kc);

    char *json = slurp("tests/fixtures/rift/event_details.json");
    rift_detail_t d;
    memset(&d, 0, sizeof d);
    assert(rift_parse_event_details(json, kc, &d) == 0);
    free(json);

    json = slurp("tests/fixtures/rift/window_finished.json");
    assert(rift_parse_window(json, &d, 0) == 0);
    assert(strcmp(d.game_state, "finished") == 0);
    /* probed final frame of G1 */
    assert(d.blue.kills == 13 && d.red.kills == 2);
    assert(d.blue.gold == 58490 && d.red.gold == 47092);
    assert(d.blue.towers == 9 && d.red.towers == 2);
    assert(strcmp(d.blue.p[0].who, "KC Canna") == 0);
    assert(strcmp(d.blue.p[0].champ, "Jayce") == 0);
    assert(strcmp(d.blue.p[0].role, "top") == 0);
    assert(d.blue.p[0].k == 1 && d.blue.p[0].d == 0
           && d.blue.p[0].a == 6 && d.blue.p[0].cs == 301);
    assert(strcmp(d.red.p[4].who, "MVK SiuLoong") == 0);
    assert(strcmp(d.red.p[4].champ, "Seraphine") == 0);
    assert(d.red.p[4].k == 0 && d.red.p[4].d == 2
           && d.red.p[4].a == 0 && d.red.p[4].cs == 40);
    /* KC is blue (side1==0) and won G1 */
    assert(d.games[0].winner == 1);
    free(json);
}

static void test_window_truncated(void) {
    char *json = slurp("tests/fixtures/rift/window_finished.json");
    rift_detail_t d;
    memset(&d, 0, sizeof d);
    d.game_count = 1;
    /* game_idx bounds: idx 0 parses against the fake detail, idx 1 rejects */
    assert(rift_parse_window(json, &d, 0) == 0);
    assert(rift_parse_window(json, &d, 1) == -1);
    free(json);
}

static void test_filter_window(void) {
    rift_match_t ms[8];
    memset(ms, 0, sizeof ms);
    /* Beijing days 5, 6, 5, 9, 6 against an inclusive [5,6] window */
    static const int days[5] = {5, 6, 5, 9, 6};
    for (int i = 0; i < 5; i++) ms[i].start = (int64_t)days[i] * 86400 + 100;
    assert(rift_filter_window(ms, 5, 5, 6) == 4);
    assert(rift_day_of(ms[0].start) == 5);
    assert(rift_day_of(ms[1].start) == 6);
    assert(rift_day_of(ms[2].start) == 5);
    assert(rift_day_of(ms[3].start) == 6);
    /* edges: nothing to filter keeps zero, no hits keeps everything */
    assert(rift_filter_window(ms, 0, 0, 9) == 0);
    assert(rift_filter_window(ms, 4, 0, 99) == 4);
}

/* RAM budget guard: the schedule array must stay small on the no-PSRAM C3
 * (48 matches); games/window rosters belong to the scratch detail struct. */
static void test_struct_sizing(void) {
    assert(sizeof(rift_match_t) <= 240);
    assert(sizeof(rift_detail_t) > sizeof(rift_match_t));
}

int main(void) {
    test_time_helpers();
    test_struct_sizing();
    test_schedule();
    test_schedule_older();
    test_schedule_backfill_append();
    test_event_details();
    test_window();
    test_window_truncated();
    test_filter_window();
    printf("test_rift_model: all assertions passed\n");
    return 0;
}
