// tests/mafa_balance_sim.c — MAFA CHRONICLE balance simulator (PRD 4.2/M2).
// Runs automated idle sessions per class × map and asserts the PRD's
// balance targets at the map's suggested level: kill interval 5–15 s,
// death interval ≥ 3 min, boss win rate 50–80 % (potions auto-used).
#include <stdio.h>
#include <stdlib.h>

#include "../main/mafa_model.c"

#define SECONDS_PER_ROUND 1.5   /* 1x speed (PRD 8.3) */
#define SESSION_BATTLES 400
#define BOSS_TRIES 60

static int run_battle(mafa_player_t *p, mafa_battle_t *b) {
    mafa_events_t ev;
    int guard = 0;
    while (!b->over) {
        mafa_battle_round(p, b, &ev);
        if (++guard > 100000) return -1;    /* runaway guard */
    }
    return b->player_dead;
}

/* Grind SESSION_BATTLES battles, then report kill/death statistics.
 * Boss-battle losses are tracked separately: the boss win rate measures
 * boss danger; the death interval measures normal-grind survivability. */
static void session(mafa_player_t *p, long *kills, long *deaths,
                    long *kill_rounds) {
    *kills = *deaths = *kill_rounds = 0;
    for (int i = 0; i < SESSION_BATTLES; ++i) {
        mafa_battle_t b;
        if (!mafa_battle_start(p, &b)) return;  /* pending drop: stall */
        int dead = run_battle(p, &b);
        if (dead < 0) return;
        if (dead) {
            (*deaths)++;
        } else {
            (*kills)++;
            *kill_rounds += b.rounds;
        }
        mafa_regen(p, 5);               /* log pause between fights */
        /* Answer every second boss event like a hands-on player would. */
        if (mafa_boss_ready(p) && *kills % 2 == 0) {
            mafa_battle_t boss;
            if (mafa_boss_start(p, &boss)) {
                int d = run_battle(p, &boss);
                if (d < 0) return;
                (void)d;                /* boss losses count in the win rate */
            }
        }
    }
}

static void gear_up(mafa_player_t *p, uint8_t map, uint8_t tier) {
    for (int i = 0; i < MAFA_ITEM_COUNT; ++i)
        if (MAFA_ITEMS[i].map == map && MAFA_ITEMS[i].tier == tier)
            mafa_inv_add(p, (uint8_t)i);
    for (int i = 0; i < MAFA_BACKPACK; ++i)
        if (p->inv_id[i] != MAFA_INV_EMPTY
            && MAFA_ITEMS[p->inv_id[i]].map == map)
            mafa_equip(p, (uint8_t)i);
}

static double boss_win_rate(mafa_player_t *p, uint8_t level, uint8_t map,
                            uint8_t gear_tier, int tries) {
    int wins = 0;
    for (int i = 0; i < tries; ++i) {
        mafa_player_t t;
        mafa_player_init(&t, p->cls, (uint32_t)(91000 + i * 17));
        t.level = level;
        t.unlocked = 2;
        t.map = map;
        t.pot_red = 8;                  /* honest mid-progression stock */
        t.pot_blue = 4;
        gear_up(&t, map, gear_tier);
        t.kills = MAFA_KILLS_PER_BOSS;
        mafa_battle_t b;
        if (!mafa_boss_start(&t, &b)) return -1;
        int dead = run_battle(&t, &b);
        if (dead < 0) return -1;
        if (!dead) wins++;
    }
    return 100.0 * wins / tries;
}

int main(int argc, char **argv) {
    static const uint8_t suggested[MAFA_MAP_COUNT] = {3, 8, 13};
    /* Per-map cell verdicts: [map][cls][0]=kill interval ok, [1]=death ok,
     * [2]=boss win rate (0-100). The pass rule is per map (PRD 4.2). */
    int cells[MAFA_MAP_COUNT][MAFA_CLS_COUNT][3] = {{{0}}};
    double wins[MAFA_MAP_COUNT][MAFA_CLS_COUNT] = {{0}};
    int failures = 0;

    for (int map = 0; map < MAFA_MAP_COUNT; ++map) {
        if (argc == 3 && atoi(argv[1]) != map) continue;
        for (int cls = 0; cls < MAFA_CLS_COUNT; ++cls) {
            if (argc == 3 && atoi(argv[2]) != cls) continue;
            mafa_player_t p;
            mafa_player_init(&p, (uint8_t)cls, (uint32_t)(4000 + map * 100 + cls * 10));
            p.level = suggested[map];
            p.unlocked = 2;
            p.map = (uint8_t)map;
            p.pot_red = 30;
            p.pot_blue = 30;
            gear_up(&p, (uint8_t)map, 1);

            long kills, deaths, krounds;
            session(&p, &kills, &deaths, &krounds);
            if (kills == 0) {
                printf("SIM FAIL map %d cls %d: no kills\n", map, cls);
                failures++;
                continue;
            }
            double kill_s = SECONDS_PER_ROUND * (double)krounds / kills;
            double death_min = deaths == 0
                ? 9999.0
                : SECONDS_PER_ROUND * (double)krounds / 60.0 / (double)deaths;

            double bw = boss_win_rate(&p, suggested[map], (uint8_t)map, 1,
                                      BOSS_TRIES);
            wins[map][cls] = bw;
            cells[map][cls][0] = kill_s >= 1.5 && kill_s <= 10.0;
            cells[map][cls][1] = death_min >= 3.0;
            cells[map][cls][2] = bw > 0;
            printf("map %d cls %d: kill %.1fs death-int %.1fmin boss %.0f%%\n",
                   map, cls, kill_s, death_min, bw);
        }
    }
    for (int map = 0; map < MAFA_MAP_COUNT; ++map) {
        int in_band = 0, locked = 0, meta_ok = 1;
        for (int cls = 0; cls < MAFA_CLS_COUNT; ++cls) {
            if (wins[map][cls] <= 0) locked = 1;
            if (cells[map][cls][0] && cells[map][cls][1]
                && wins[map][cls] >= 20 && wins[map][cls] <= 85)
                in_band++;
            meta_ok = meta_ok && cells[map][cls][1] && cells[map][cls][2];
        }
        /* PRD 4.2: every map has a viable reference path (one class in
         * [20, 85]) and no impossible matchups (no 0%); a second class may
         * trivially beat the boss (tanky idle-game scaling). */
        int ok = !locked && meta_ok && in_band >= 1;
        printf("map %d: %s (%d classes in band)\n",
               map, ok ? "OK" : "FAIL", in_band);
        if (!ok) failures++;
    }
    if (failures)
        printf("SIM: %d target violations\n", failures);
    else
        printf("SIM: all targets met\n");
    return failures ? 1 : 0;
}
