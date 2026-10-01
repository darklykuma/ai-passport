// tests/test_mafa_model.c — host tests for the MAFA CHRONICLE model
// (PRD_MAFA_CHRONICLE chapters 8-9, 1.76 alignment). Deterministic seeds;
// probabilistic assertions use large loops. No floats.

/* Asserts are the test: never let NDEBUG (e.g. zig cc -O1) strip them. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../main/mafa_model.c"

static void test_stats_and_growth(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 42);
    mafa_stats_t st;
    mafa_stats(&p, &st);
    /* v1.5: warriors carry the small mana pool, no 魔/道 lines. */
    assert(st.max_hp == 60 && st.atk == 11 && st.def == 5
           && st.max_mp == 10 && st.mc == 0 && st.sc == 0);
    p.level = 5;
    mafa_stats(&p, &st);
    assert(st.max_hp == 60 + 8 * 4 && st.atk == 11 + 2 * 4 && st.def == 9
           && st.max_mp == 10 + 4);

    mafa_player_init(&p, MAFA_CLS_MAGE, 42);
    mafa_stats(&p, &st);
    /* The mage's main stat moved to 魔法 (v1.5); 平砍 keeps a small 攻. */
    assert(st.max_hp == 40 && st.atk == 10 && st.mc == 14 && st.sc == 0
           && st.def == 3 && st.max_mp == 30);
    p.level = 6;                        /* def +1 every 2 levels */
    mafa_stats(&p, &st);
    assert(st.def == 3 + 6 / 2 && st.mc == 14 + 2 * 5 && st.atk == 10 + 5
           && st.max_mp == 30 + 5 * 5);

    mafa_player_init(&p, MAFA_CLS_TAOIST, 42);
    mafa_stats(&p, &st);
    assert(st.max_hp == 60 && st.max_mp == 25 && st.sc == 12 && st.mc == 0
           && st.atk == 10);
    p.level = 20;
    mafa_stats(&p, &st);
    assert(st.sc == 12 + 19 && st.atk == 10 + 19);

    /* The paper doll: 8 positions, twins share a slot type. */
    assert(MAFA_POS_TYPE[0] == MAFA_ST_WEAPON && MAFA_POS_TYPE[1] == MAFA_ST_HELMET);
    assert(MAFA_POS_TYPE[2] == MAFA_ST_ARMOR && MAFA_POS_TYPE[3] == MAFA_ST_NECKLACE);
    assert(MAFA_POS_TYPE[4] == MAFA_ST_BRACELET && MAFA_POS_TYPE[5] == MAFA_ST_BRACELET);
    assert(MAFA_POS_TYPE[6] == MAFA_ST_RING && MAFA_POS_TYPE[7] == MAFA_ST_RING);
}

/* First table id matching name/map/tier — keeps assertions honest across
 * table edits (v1.5 renumbered every id). */
static uint8_t find_item(const char *name, uint8_t map, uint8_t tier) {
    for (int i = 0; i < MAFA_ITEM_COUNT; ++i)
        if (MAFA_ITEMS[i].map == map && MAFA_ITEMS[i].tier == tier
            && strcmp(MAFA_ITEMS[i].name, name) == 0)
            return (uint8_t)i;
    assert(0 && "item not found");
    return MAFA_INV_EMPTY;
}

static void test_xp_curve_front_fast_back_wall(void) {
    assert(mafa_xp_to_next(1) == 100);
    assert(mafa_xp_to_next(2) == 180);
    assert(mafa_xp_to_next(8) == 2700);
    assert(mafa_xp_to_next(38) == 2130000);
    assert(mafa_xp_to_next(MAFA_MAX_LEVEL) == 0);
    /* The 39→40 wall is ~46 % of the total grind (rebalance target). */
    uint32_t total = 0;
    for (int lv = 1; lv < MAFA_MAX_LEVEL; ++lv) total += mafa_xp_to_next((uint8_t)lv);
    uint32_t wall = mafa_xp_to_next(39);
    uint32_t early = mafa_xp_to_next(1) + mafa_xp_to_next(2) + mafa_xp_to_next(3);
    assert(wall * 100 / total >= 40 && wall * 100 / total <= 50);
    /* Early game stays fast: the first three levels are under 6 % together. */
    assert(early * 100 / total < 6);
    printf("xp curve: total %lu, wall share %lu%%, early share %lu%%\n",
           (unsigned long)total, (unsigned long)(wall * 100 / total),
           (unsigned long)(early * 100 / total));
}

/* Grant every book of the player's class up to skill idx (test helper). */
static void grant_books(mafa_player_t *p, uint8_t max_idx) {
    for (int i = 1; i <= max_idx; ++i)
        p->books |= (1u << (p->cls * MAFA_SKILLS_PER_CLASS + i));
}

static void test_book_gating_and_store(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 7);

    /* Skill 0 needs no book; higher skills need level AND book. */
    assert(!mafa_skill_known(&p, 0));    /* 基本剑术 unlocks at L7, not L1 */
    p.level = 7;
    assert(mafa_skill_known(&p, 0));
    assert(!mafa_skill_known(&p, 1));    /* 攻杀 L19: no book yet */
    p.level = 19;
    assert(!mafa_skill_known(&p, 1));    /* level ok, book missing */
    p.books = (1u << (MAFA_CLS_WARRIOR * MAFA_SKILLS_PER_CLASS + 1));
    assert(mafa_skill_known(&p, 1));

    /* Buying: three shelf books at 300/600/900; books 4-6 drop in battle. */
    mafa_player_t q;
    mafa_player_init(&q, MAFA_CLS_MAGE, 7);
    q.level = 22;                        /* past 雷电17 / 爆裂22 */
    q.gold = 900;
    assert(mafa_book_price(MAFA_CLS_MAGE, 1) == 300);
    assert(!mafa_buy_book(&q, 0));       /* skill 0 is not sold */
    assert(!mafa_buy_book(&q, 4));       /* books 4-6 drop in battle */
    assert(mafa_buy_book(&q, 1));
    assert(q.gold == 600 && mafa_skill_known(&q, 1));
    assert(!mafa_buy_book(&q, 1));       /* already learned */
    assert(mafa_buy_book(&q, 2) && q.gold == 0 && mafa_skill_known(&q, 2));
    assert(!mafa_buy_book(&q, 3));       /* 火墙 L24 > 22 AND no gold */
}

static void test_store_level_gate(void) {
    /* The 1.76 plan gate: the store refuses to sell under the learn level. */
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 17);
    p.level = 17;
    p.gold = 9999;
    assert(p.level == 17 && mafa_book_price(MAFA_CLS_WARRIOR, 1) == 300);
    assert(!mafa_buy_book(&p, 1));       /* 攻杀 unlocks at L19 */
    assert(p.gold == 9999 && p.books == 0);   /* refused, no charge */
    p.level = 19;
    assert(mafa_buy_book(&p, 1));        /* at level: sold */
    assert(p.gold == 9999 - 300);

    mafa_player_t t;
    mafa_player_init(&t, MAFA_CLS_TAOIST, 13);
    t.level = 13;
    t.gold = 9999;
    /* 道士 shelf: 精神力9 / 施毒14 / 火符18. L13 may buy 1 but not 2. */
    assert(mafa_buy_book(&t, 1));
    assert(!mafa_buy_book(&t, 2));       /* 施毒 L14 > 13 */
}

static void test_battle_kills_and_settlement(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 99);
    mafa_battle_t b;
    assert(mafa_battle_start(&p, &b));
    assert(!b.is_boss);
    assert(b.mob_n >= 1 && b.mob_n <= MAFA_MOBS_MAX);
    mafa_events_t ev;
    int rounds = 0;
    int seen_killed = 0;
    while (!b.over && rounds < 10000) {
        mafa_battle_round(&p, &b, &ev);
        rounds++;
        for (int i = 0; i < ev.n; ++i) {
            if (ev.e[i].kind == MAFA_EV_MOB_KILLED) {
                seen_killed++;
                assert(ev.e[i].a > 0);          /* xp granted */
                assert(p.kills == (uint16_t)seen_killed);
            }
            assert(ev.e[i].kind != MAFA_EV_PLAYER_DEATH);  /* must survive */
        }
    }
    assert(b.over && seen_killed == b.mob_n);   /* every spawned mob settles */
    assert(b.alive_n == 0);
    for (int i = 0; i < b.mob_n; ++i) assert(!b.mob[i].alive);
}

static void test_multi_mob_pack_weights(void) {
    int packs[MAFA_MOBS_MAX + 1] = {0};
    int elites = 0;
    for (int i = 0; i < 1000; ++i) {
        mafa_player_t p;
        mafa_player_init(&p, MAFA_CLS_MAGE, (uint32_t)(500 + i));
        p.unlocked = 3;
        p.map = 3;                              /* mid-deep map: often 3 mobs */
        p.floor = MAFA_MAP_FLOORS[3];           /* full cumulative trash pool */
        mafa_battle_t b;
        assert(mafa_battle_start(&p, &b));
        packs[b.mob_n]++;
        if (b.mob_n == 1 && b.mob[0].elite) elites++;
        /* every spawned mob must be alive and from the current map */
        for (int k = 0; k < b.mob_n; ++k) {
            assert(b.mob[k].alive);
            assert(b.mob[k].base->map == 3 && !b.mob[k].base->boss);
        }
    }
    assert(packs[1] > 0 && packs[2] > 0 && packs[3] > 100);   /* 3-mob fights exist */
    assert(elites > 30 && elites < 200);        /* ~1/10 of single spawns */
}

/* The spawn cap is stepped (v1.6 on-ramp): before a class's first form
 * (火球术 is L7) it hugs the level, so Bichon floor 1 at L1 draws only the
 * chicken/deer tier and tiers join one level at a time; from L7 the classic
 * +2 band returns. */
static void test_starter_onramp_pool(void) {
    for (int i = 0; i < 200; ++i) {
        mafa_player_t p;
        mafa_player_init(&p, MAFA_CLS_MAGE, (uint32_t)(100 + i));
        mafa_battle_t b;
        assert(mafa_battle_start(&p, &b));
        for (int k = 0; k < (int)b.mob_n; ++k) {
            assert(b.mob[k].base->map == 1 && !b.mob[k].base->boss);
            assert(b.mob[k].base->level == 1);   /* chicken / deer only */
        }
    }
    int saw_l2 = 0;
    for (int i = 0; i < 200; ++i) {
        mafa_player_t p;
        mafa_player_init(&p, MAFA_CLS_MAGE, (uint32_t)(900 + i));
        p.level = 2;
        p.floor = 2;                     /* 稻草人 (L2) lives on floor 2 now */
        mafa_battle_t b;
        assert(mafa_battle_start(&p, &b));
        for (int k = 0; k < (int)b.mob_n; ++k) {
            assert(b.mob[k].base->level <= 2);
            if (b.mob[k].base->level == 2) saw_l2 = 1;
        }
    }
    assert(saw_l2);
    /* From the class's first form (L7) the classic +2 band returns: a L7
     * run in Orc Catacombs floor 1 draws 骷髅(7)/洞蛆(8) — above the level. */
    int saw_above = 0;
    for (int i = 0; i < 200; ++i) {
        mafa_player_t p;
        mafa_player_init(&p, MAFA_CLS_MAGE, (uint32_t)(1700 + i));
        p.level = 7;
        p.unlocked = 2;
        p.map = 2;
        mafa_battle_t b;
        assert(mafa_battle_start(&p, &b));
        for (int k = 0; k < (int)b.mob_n; ++k) {
            assert(b.mob[k].base->level >= 7 && b.mob[k].base->level <= 9);
            if (b.mob[k].base->level > 7) saw_above = 1;
        }
    }
    assert(saw_above);
}

static void test_boss_event_flow_and_floor_ladder(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 5);
    assert(!mafa_boss_ready(&p));
    mafa_battle_t b;
    assert(!mafa_boss_start(&p, &b));   /* not ready */
    p.kills = MAFA_KILLS_PER_BOSS;
    assert(mafa_boss_ready(&p));
    assert(mafa_boss_start(&p, &b));
    assert(b.is_boss && b.mob_n == 1);

    /* Fighting the floor-1 boss (稻草人, the newbie pen's gatekeeper) and
     * winning must open floor 2 of the same map (v1.3). A stocked L12
     * warrior beats the L3 boss — and the v1.7 book gate keeps every late
     * book (unlock L30+) away from an under-level player, boss kills
     * included. */
    p.level = 12;
    grant_books(&p, 3);
    mafa_stats_t bst;
    mafa_stats(&p, &bst);
    p.hp = (int16_t)bst.max_hp;
    p.pot_red = 30;
    uint32_t late_mask = 0;
    for (int i = 4; i < MAFA_SKILLS_PER_CLASS; ++i)
        late_mask |= (1u << (MAFA_CLS_WARRIOR * MAFA_SKILLS_PER_CLASS + i));
    int guard = 0;
    mafa_events_t ev;
    bool saw_floor_ev = false;
    while (!b.over && guard < 100000) {
        mafa_battle_round(&p, &b, &ev);
        guard++;
        for (int i = 0; i < ev.n; ++i)
            if (ev.e[i].kind == MAFA_EV_FLOOR && ev.e[i].id == 2)
                saw_floor_ev = true;
    }
    assert(b.over && guard < 100000);
    if (!b.player_dead) {
        assert(p.unlocked == 1);            /* floor boss ≠ map unlock */
        assert(p.floor_unlocked[0] == 2 && p.floor == 2 && saw_floor_ev);
        assert(p.kills == 0);
        assert(!(p.books & late_mask));     /* L12 < L30: no book drops */
        /* Second boss kill (半兽勇士, floor 2) opens floor 3; still no
         * late book below the gate. */
        p.kills = MAFA_KILLS_PER_BOSS;
        mafa_battle_t b2;
        assert(mafa_boss_start(&p, &b2));
        assert(b2.mob[0].base->boss && b2.mob[0].base->floor == 2);
        guard = 0;
        bool saw_floor3_ev = false;
        while (!b2.over && guard < 100000) {
            mafa_battle_round(&p, &b2, &ev);
            guard++;
            for (int i = 0; i < ev.n; ++i)
                if (ev.e[i].kind == MAFA_EV_FLOOR && ev.e[i].id == 3)
                    saw_floor3_ev = true;
        }
        if (!b2.player_dead) {
            assert(!(p.books & late_mask));         /* gate still holds */
            assert(p.floor_unlocked[0] == 3 && p.floor == 3 && saw_floor3_ev);
            assert(p.unlocked == 1);                /* still no map unlock */
        }
        /* Third boss kill (半兽统领, floor 3 = the last floor) unlocks
         * map 2 — and the whole L12 ladder ran book-free. */
        p.kills = MAFA_KILLS_PER_BOSS;
        mafa_battle_t b3;
        assert(mafa_boss_start(&p, &b3));
        assert(b3.mob[0].base->boss && b3.mob[0].base->floor == 3);
        guard = 0;
        bool saw_map_ev = false;
        while (!b3.over && guard < 100000) {
            mafa_battle_round(&p, &b3, &ev);
            guard++;
            for (int i = 0; i < ev.n; ++i)
                if (ev.e[i].kind == MAFA_EV_MAP_UNLOCK) saw_map_ev = true;
        }
        if (!b3.player_dead) {
            assert(!(p.books & late_mask));         /* no banked books */
            assert(p.unlocked == 2 && saw_map_ev);  /* 兽人古墓 opens */
            assert(p.floor == 3);                   /* stays on the top floor */
        }
    } else {
        assert(p.kills == 0);           /* death also resets the counter */
    }
    mafa_boss_pass(&p);
    assert(p.kills == 0);
}

static void test_book_drop_gate_and_odds(void) {
    /* v1.7: the boss first-kill guarantee became a 30/15/8 % roll (elites
     * 5 %), and a book only rolls once its unlock level is reached. The
     * L12 ladder test above proves the gate; here a max-level warrior
     * farms the floor-1 boss and books 4, 5, 6 must land in order within
     * a few dozen kills (8 % capstone: ~12.5 boss kills on average). */
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 4242);
    p.level = MAFA_MAX_LEVEL;
    grant_books(&p, 3);
    p.unlocked = 7;
    p.map = 1;
    p.floor = 1;
    p.auto_sell = 0;
    /* Orc-Tomb tier-1 kit: spawn scaling (+5 %/level) turns even the
     * scarecrow lethal for a naked max-level character. */
    for (int i = 0; i < MAFA_ITEM_COUNT; ++i) {
        if (MAFA_ITEMS[i].map != 2 || MAFA_ITEMS[i].tier != 1) continue;
        if (MAFA_ITEMS[i].line != MAFA_LINE_NEUTRAL
            && MAFA_ITEMS[i].line != p.cls) continue;
        assert(mafa_inv_add(&p, (uint8_t)i));
        if (MAFA_ITEMS[i].slot == MAFA_ST_BRACELET
            || MAFA_ITEMS[i].slot == MAFA_ST_RING)
            assert(mafa_inv_add(&p, (uint8_t)i));
    }
    for (int i = 0; i < MAFA_BACKPACK; ++i)
        if (p.inv_id[i] != MAFA_INV_EMPTY) mafa_equip(&p, (uint8_t)i);
    mafa_stats_t st;
    mafa_stats(&p, &st);
    p.pot_red = MAFA_POT_CAP;
    p.pot_blue = MAFA_POT_CAP;
    mafa_events_t ev;
    uint8_t want = 4;
    int kills = 0;
    while (want <= 6 && kills < 400) {
        p.kills = MAFA_KILLS_PER_BOSS;
        p.floor = 1;                    /* farm the weakest boss on purpose */
        p.hp = (int16_t)st.max_hp;
        p.mp = st.max_mp;
        mafa_battle_t b;
        assert(mafa_boss_start(&p, &b));
        int guard = 0;
        while (!b.over && guard < 100000) {
            mafa_battle_round(&p, &b, &ev);
            guard++;
            for (int i = 0; i < ev.n; ++i)
                if (ev.e[i].kind == MAFA_EV_BOOK && ev.e[i].a == 1) {
                    assert(ev.e[i].id == want);     /* strict 4 → 5 → 6 */
                    want++;
                }
        }
        assert(b.over && !b.player_dead && guard < 100000);
        if (p.pending_drop != MAFA_DROP_NONE) mafa_drop_discard(&p);
        kills++;
    }
    assert(want == 7 && kills < 100);   /* three books, no drought */
}

static void test_taoist_pet_tanks(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_TAOIST, 31);
    p.level = 20;
    mafa_stats_t st;
    mafa_stats(&p, &st);
    p.hp = (int16_t)st.max_hp;          /* pools match the raised level */
    p.mp = st.max_mp;
    grant_books(&p, 4);                 /* 召唤骷髅 (idx 4, L19) known */
    assert(mafa_skill_known(&p, 4));
    p.unlocked = 2;
    p.map = 2;                          /* tougher mobs: the fight must last */
    p.floor = 2;
    mafa_battle_t b;
    assert(mafa_battle_start(&p, &b));
    mafa_events_t ev;
    int guard = 0;
    bool saw_summon = false, saw_guard = false, pet_took_hit = false;
    int16_t hp_after_summon = 0;
    while (!b.over && guard < 10000) {
        mafa_battle_round(&p, &b, &ev);
        guard++;
        for (int i = 0; i < ev.n; ++i) {
            if (ev.e[i].kind == MAFA_EV_PET_SUMMON) {
                saw_summon = true;
                assert(b.pet_alive && b.pet_tier == 1);
                hp_after_summon = p.hp;
            }
            if (ev.e[i].kind == MAFA_EV_PET_GUARD) {
                saw_guard = true;
                pet_took_hit = true;
            }
        }
        if (b.pet_alive && pet_took_hit)
            assert(p.hp >= hp_after_summon - 3);   /* pet, not player, bleeds */
        if (b.pet_alive)
            for (int i = 0; i < ev.n; ++i)
                assert(ev.e[i].kind != MAFA_EV_MOB_HIT);   /* full taunt */
    }
    assert(b.over && saw_summon && saw_guard);
    assert(guard < 10000);
}

static void test_warrior_charge_and_proc(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 2024);
    p.level = 36;
    p.pot_red = 0;                      /* written against the 0-potion
                                         * starter: keep the cast rhythm
                                         * free of auto-potion turns */
    p.pot_blue = 0;
    grant_books(&p, 6);
    p.unlocked = 2;
    p.map = 2;                          /* pen mobs die to a single charge:
                                         * spar on tougher ground so the
                                         * 40-round window sees the swing */
    p.floor = 2;
    mafa_battle_t b;
    assert(mafa_battle_start(&p, &b));
    mafa_events_t ev;
    mafa_battle_round(&p, &b, &ev);     /* 烈火 priority: cast = charge armed */
    assert(b.charge_mult == 220 || b.over);
    bool saw_charge_hit = false;
    for (int round = 0; round < 40 && !b.over; ++round) {
        mafa_battle_round(&p, &b, &ev);
        for (int i = 0; i < ev.n; ++i)
            if (ev.e[i].kind == MAFA_EV_SKILL_HIT
                && MAFA_SKILLS[p.cls][ev.e[i].id].kind == MAFA_SK_CHARGE)
                saw_charge_hit = true;
    }
    if (!b.player_dead) {
        assert(saw_charge_hit);         /* the charged swing consumed the mark */
        assert(b.charge_mult == 0);
    }

    /* Proc statistics: many plain swings must include 攻杀 procs (20 %). */
    mafa_player_t q;
    mafa_player_init(&q, MAFA_CLS_WARRIOR, 777);
    q.level = 21;                       /* 攻杀 L19 known */
    grant_books(&q, 1);
    mafa_stats_t qst;
    mafa_stats(&q, &qst);
    q.hp = (int16_t)qst.max_hp;         /* pools match the raised level */
    q.pot_red = 40;                     /* survive the grind loop */
    q.unlocked = 2;
    q.map = 2;                          /* same-level mobs: long fights */
    q.floor = 2;
    int procs = 0, total_swings = 0;
    for (int i = 0; i < 60; ++i) {
        q.map = 2;                      /* a death may have sent us to town */
        q.floor = 2;
        if (q.pending_drop != MAFA_DROP_NONE) mafa_drop_discard(&q);
        mafa_regen(&q, 5);              /* the log pause between fights */
        mafa_battle_t bb;
        if (!mafa_battle_start(&q, &bb)) continue;
        int guard = 0;
        while (!bb.over && guard++ < 1000) {
            mafa_events_t e2;
            mafa_battle_round(&q, &bb, &e2);
            for (int k = 0; k < e2.n; ++k) {
                if (e2.e[k].kind == MAFA_EV_PLAYER_HIT
                    || e2.e[k].kind == MAFA_EV_PLAYER_CRIT)
                    total_swings++;
                if (e2.e[k].kind == MAFA_EV_SKILL_HIT) {
                    if (MAFA_SKILLS[q.cls][e2.e[k].id].kind == MAFA_SK_PROC)
                        procs++;
                    else
                        total_swings++;     /* a proc still consumed a swing */
                }
            }
        }
    }
    assert(total_swings > 100);
    /* 20 % proc rate: 8..40 % of 1000+ swings — loose binomial bounds. */
    assert(procs * 100 / total_swings > 8 && procs * 100 / total_swings < 40);
}

static void test_warrior_stun_skips_turns(void) {
    /* 野蛮冲撞 (idx 4, L30): the target skips its next attack turn. */
    int stuns = 0;
    for (int i = 0; i < 80 && stuns == 0; ++i) {
        mafa_player_t p;
        mafa_player_init(&p, MAFA_CLS_WARRIOR, (uint32_t)(4000 + i));
        p.level = 32;
        grant_books(&p, 4);
        p.unlocked = 3;
        p.map = 3;
        p.floor = 2;
        mafa_battle_t b;
        if (!mafa_battle_start(&p, &b)) continue;
        mafa_events_t ev;
        int guard = 0;
        while (!b.over && guard++ < 5000) {
            mafa_battle_round(&p, &b, &ev);
            for (int k = 0; k < ev.n; ++k)
                if (ev.e[k].kind == MAFA_EV_MOB_STUNNED) stuns++;
            if (p.pending_drop != MAFA_DROP_NONE) mafa_drop_discard(&p);
        }
    }
    assert(stuns > 0);

    /* Direct check: a stunned mob deals no damage that round. */
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 123);
    p.level = 32;
    grant_books(&p, 4);
    p.unlocked = 3;
    p.map = 3;
    p.floor = 2;
    mafa_battle_t b;
    assert(mafa_battle_start(&p, &b));
    mafa_events_t ev;
    int guard = 0;
    while (!b.over && guard++ < 5000) {
        mafa_battle_round(&p, &b, &ev);
        for (int k = 0; k < ev.n; ++k)
            if (ev.e[k].kind == MAFA_EV_MOB_STUNNED) {
                /* The stunned mob's skip must leave no hit from it. */
                uint8_t stunned = ev.e[k].id;
                int hits_from_stunned = 0;
                for (int j = 0; j < ev.n; ++j)
                    if ((ev.e[j].kind == MAFA_EV_MOB_HIT
                         || ev.e[j].kind == MAFA_EV_MOB_SKILL)
                        && ev.e[j].b == stunned)
                        hits_from_stunned++;
                /* The event fires inside player_turn: the mob has not had
                 * its turn yet this round — the skip is checked below by
                 * absence in THIS event list after mob_turn ran. */
                (void)hits_from_stunned;
            }
        if (p.pending_drop != MAFA_DROP_NONE) mafa_drop_discard(&p);
    }
    /* Counting logic: every stunned round must show zero mob hits from the
     * stunned index in the same event list (stun set before mob_turn). */
    int stun_rounds_checked = 0, stun_rounds_with_hit = 0;
    for (int i = 0; i < 40; ++i) {
        mafa_player_t q;
        mafa_player_init(&q, MAFA_CLS_WARRIOR, (uint32_t)(7000 + i));
        q.level = 32;
        grant_books(&q, 4);
        q.unlocked = 3;
        q.map = 3;
        q.floor = 2;
        mafa_battle_t bb;
        if (!mafa_battle_start(&q, &bb)) continue;
        mafa_events_t e2;
        int g2 = 0;
        while (!bb.over && g2++ < 5000) {
            mafa_battle_round(&q, &bb, &e2);
            for (int k = 0; k < e2.n; ++k)
                if (e2.e[k].kind == MAFA_EV_MOB_STUNNED) {
                    stun_rounds_checked++;
                    for (int j = 0; j < e2.n; ++j)
                        if ((e2.e[j].kind == MAFA_EV_MOB_HIT
                             || e2.e[j].kind == MAFA_EV_MOB_SKILL)
                            && e2.e[j].b == e2.e[k].id)
                            stun_rounds_with_hit++;
                }
            if (q.pending_drop != MAFA_DROP_NONE) mafa_drop_discard(&q);
        }
    }
    assert(stun_rounds_checked > 0);
    assert(stun_rounds_with_hit == 0);      /* stunned mobs never swing */
}

static void test_taoist_armor_buff(void) {
    /* 神圣战甲术 (idx 5, L25): defense buff when hurt, none up. */
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_TAOIST, 64);
    p.level = 26;
    mafa_stats_t st0;
    mafa_stats(&p, &st0);
    p.hp = (int16_t)st0.max_hp;         /* pools match the raised level */
    p.mp = st0.max_mp;
    grant_books(&p, 5);
    p.unlocked = 3;
    p.map = 3;
    p.floor = 2;
    mafa_battle_t b;
    assert(mafa_battle_start(&p, &b));
    /* 65 % HP: above the 治愈 gate (60 %), inside the armor gate (<70 %)
     * so 神圣战甲术 is the first castable skill. */
    p.hp = (int16_t)(p.hp * 65 / 100);
    mafa_events_t ev;
    int guard = 0;
    bool armor_up = false, saw_support = false;
    while (!b.over && guard < 10000) {
        mafa_battle_round(&p, &b, &ev);
        guard++;
        for (int i = 0; i < ev.n; ++i)
            if (ev.e[i].kind == MAFA_EV_SKILL_SUPPORT
                && !(ev.e[i].id & 0x80)
                && MAFA_SKILLS[p.cls][ev.e[i].id].kind == MAFA_SK_ARMOR)
                saw_support = true;
        if (b.armor_rounds > 0) { armor_up = true; break; }
    }
    assert(armor_up && saw_support);
    assert(guard < 10000);
}

static void test_mage_shield_reduces_damage(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_MAGE, 64);
    p.level = 33;
    p.pot_red = 0;                      /* written against the 0-potion
                                         * starter: the hurt state must
                                         * open the shield, not a bottle */
    p.pot_blue = 0;
    grant_books(&p, 5);
    p.unlocked = 3;
    p.map = 3;
    p.floor = 2;
    mafa_battle_t b;
    assert(mafa_battle_start(&p, &b));
    /* Hurt the mage so the <70 % shield gate opens on the next round. */
    p.hp = p.hp / 2;
    mafa_events_t ev;
    int guard = 0;
    bool shield_up = false, saw_support = false;
    while (!b.over && guard < 10000) {
        mafa_battle_round(&p, &b, &ev);
        guard++;
        for (int i = 0; i < ev.n; ++i)
            if (ev.e[i].kind == MAFA_EV_SKILL_SUPPORT
                && !(ev.e[i].id & 0x80)
                && MAFA_SKILLS[p.cls][ev.e[i].id].kind == MAFA_SK_SHIELD)
                saw_support = true;
        if (b.shield_rounds > 0) { shield_up = true; break; }
    }
    assert(shield_up && saw_support);
    assert(guard < 10000);
}

static void test_switch_map_requires_unlock(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_MAGE, 11);
    assert(p.map == 1);                 /* new games idle at once: 比奇省 */
    assert(p.unlocked == 1);
    assert(p.floor == 1);               /* the ladder starts at floor 1 */
    mafa_switch_map(&p, 2, 1);          /* locked: ignored */
    assert(p.map == 1);
    mafa_switch_map(&p, 1, 2);          /* floor not unlocked yet: ignored */
    assert(p.map == 1 && p.floor == 1);
    p.unlocked = 3;
    p.floor_unlocked[2] = MAFA_MAP_FLOORS[3];
    mafa_switch_map(&p, 3, 4);
    assert(p.map == 3 && p.floor == 4);
    mafa_switch_map(&p, 3, 5);          /* beyond the ladder: ignored */
    assert(p.map == 3 && p.floor == 4);
    mafa_switch_map(&p, MAFA_MAP_SAFE, 0);      /* the town is always open */
    assert(p.map == MAFA_MAP_SAFE && p.floor == 0);
    mafa_switch_map(&p, 1, 1);          /* walk back out */
    assert(p.map == 1 && p.floor == 1);
}

static void test_safe_zone_full_restore(void) {
    /* 1.76 plan rule: a voluntary walk home heals to full, same as death. */
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_TAOIST, 88);
    p.level = 20;
    mafa_stats_t st;
    mafa_stats(&p, &st);
    p.hp = 3;
    p.mp = 1;
    mafa_switch_map(&p, MAFA_MAP_SAFE, 0);
    assert(p.map == MAFA_MAP_SAFE);
    assert(p.hp == st.max_hp && p.mp == st.max_mp);
    /* Staying re-selected while resting: still full (idempotent). */
    mafa_switch_map(&p, MAFA_MAP_SAFE, 0);
    assert(p.hp == st.max_hp);
}

static void test_inventory_equip_and_compare(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 3);
    /* Map-1 weapon ids: 0 木剑(+4), 1 木剑绿(+7), 2 木剑蓝(+11). */
    assert(mafa_inv_add(&p, 0));
    assert(mafa_inv_add(&p, 0));        /* stacks */
    assert(p.inv_n[0] == 2);
    assert(mafa_equip(&p, 0));
    mafa_stats_t st;
    mafa_stats(&p, &st);
    assert(st.atk == 11 + 4);           /* 木剑 +4 */
    assert(p.inv_n[0] == 1);            /* one left in the stack */

    mafa_compare_t cmp;
    mafa_compare(&p, 2, &cmp);          /* 木剑蓝 +11 vs 木剑 +4 */
    assert(cmp.d_atk == 7);
    assert(cmp.d_mc == 4 && cmp.d_sc == 4);   /* v1.5: the 魔法/道术 lines */

    assert(mafa_equip(&p, 0));          /* stack shrinks to zero, slot frees */
    assert(p.equipped[0] == 0);
    mafa_stats(&p, &st);
    assert(st.atk == 11 + 4);           /* unchanged: swapped, not stacked */

    /* The returned 木剑 sits in the backpack; sell-all-whites clears it. */
    assert(mafa_sell_all_white(&p) == 10);
    uint32_t gold_before = p.gold;
    assert(mafa_inv_add(&p, 12));       /* 大手镯, white bracelet */
    uint32_t gained = mafa_sell(&p, 0);
    assert(gained == 10 && p.gold == gold_before + 10);

    /* Twins: the second bracelet takes the free right-wrist position. */
    assert(mafa_inv_add(&p, 12));       /* 大手镯 white → slot 0 */
    assert(mafa_inv_add(&p, 13));       /* 大手镯 green → slot 1 */
    assert(mafa_equip(&p, 0));          /* white → left wrist (pos 4) */
    assert(p.equipped[4] == 12 && p.equipped[5] == MAFA_INV_EMPTY);
    assert(mafa_equip(&p, 1));          /* green → right wrist (pos 5) */
    assert(p.equipped[5] == 13);
    mafa_stats(&p, &st);
    assert(st.atk == 11 + 4 + 1 + 1 && st.def == 5 + 2 + 3);
    /* A third bracelet replaces the LEFT twin (first position of the type). */
    assert(mafa_inv_add(&p, 14));       /* 大手镯 blue → slot 0 */
    assert(mafa_equip(&p, 0));
    assert(p.equipped[4] == 14 && p.equipped[5] == 13);
}

static void test_full_backpack_drop_prompt(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 1234);
    p.auto_sell = 0;                    /* sell nothing: force the prompt path */
    /* Eight distinct items fill every backpack slot (map-1/2 ids). */
    static const uint8_t ids[MAFA_BACKPACK] = {1, 9, 10, 11, 12, 13, 18, 24};
    for (int i = 0; i < MAFA_BACKPACK; ++i)
        assert(mafa_inv_add(&p, ids[i]));
    p.pending_drop = 0;                 /* a 修罗 dropped with no room */
    mafa_drop_replace(&p, 3);           /* swap into slot 3, old item auto-sold */
    assert(p.pending_drop == MAFA_DROP_NONE);
    assert(p.inv_id[3] == 0);
    assert(p.gold >= 30);               /* 金项链(绿) sold at 30 */
    p.pending_drop = 2;
    mafa_drop_discard(&p);
    assert(p.pending_drop == MAFA_DROP_NONE);
}

static void test_potions_and_store(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_MAGE, 21);
    p.pot_red = 0;                      /* count from empty: the starter pack
                                         * must not offset the receipts */
    p.pot_blue = 0;
    p.gold = 90;
    assert(mafa_buy_potion(&p, true));      /* 50 */
    assert(mafa_buy_potion(&p, false));     /* 40 */
    assert(p.pot_red == 1 && p.pot_blue == 1 && p.gold == 0);
    assert(!mafa_potion_full(&p, true) && !mafa_potion_full(&p, false));
    assert(!mafa_buy_potion(&p, true));     /* not enough gold */

    p.hp = 1;
    p.pot_red = 0;
    mafa_battle_t b;
    assert(mafa_battle_start(&p, &b));
    mafa_events_t ev;
    mafa_battle_round(&p, &b, &ev);     /* auto-potion has no stock: fights on */
    assert(p.hp > 0 || b.over);
}

static void test_death_penalty_drops_and_gold(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 4242);
    p.unlocked = 3;
    p.map = 3;                          /* deep map, deadly at level 1 */
    p.floor = MAFA_MAP_FLOORS[3];
    p.gold = 1000;
    for (int i = 0; i < MAFA_BACKPACK; ++i)
        assert(mafa_inv_add(&p, (uint8_t)(i + 18)));
    p.auto_potion = false;              /* no saves */
    uint8_t occupied_before = 0;
    for (int i = 0; i < MAFA_BACKPACK; ++i)
        if (p.inv_id[i] != MAFA_INV_EMPTY) occupied_before++;

    mafa_battle_t b;
    assert(mafa_battle_start(&p, &b));
    mafa_events_t ev;
    int guard = 0;
    while (!b.over && guard < 100000) {
        mafa_battle_round(&p, &b, &ev);
        guard++;
    }
    assert(b.over && b.player_dead);
    mafa_stats_t st;
    mafa_stats(&p, &st);
    assert(p.hp == st.max_hp);          /* PRD 8.9: full restoration on death */
    assert(p.map == MAFA_MAP_SAFE);     /* respawn in town, pick the map */
    assert(p.floor == 0);               /* floors survive death, town has none */
    assert(guard < 100000);
    assert(p.kills == 0);               /* boss counter reset */

    /* Backpack: 1-2 stacks lost, equipped and potions untouched. */
    uint8_t occupied_after = 0;
    for (int i = 0; i < MAFA_BACKPACK; ++i)
        if (p.inv_id[i] != MAFA_INV_EMPTY) occupied_after++;
    assert(occupied_before - occupied_after >= 1);
    assert(occupied_before - occupied_after <= 2);

    /* Gold: 10-20 % gone. */
    assert(p.gold <= 900 && p.gold >= 800);
}

static void test_save_roundtrip_v6(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_TAOIST, 77);
    p.level = 18;
    p.xp = 123456;                      /* > uint16 max: widened xp */
    p.gold = 12345678;                  /* > uint16 max: v8 widened gold */
    p.pot_red = 3;
    p.pot_blue = 4;
    p.unlocked = 1;
    p.map = MAFA_MAP_SAFE;              /* v0.9: town survives the roundtrip */
    p.floor = 0;
    p.floor_unlocked[0] = 3;            /* 比奇省 fully climbed (3 floors) */
    p.kills = 17;
    p.auto_potion = false;
    p.auto_boss = true;
    p.pot_hp_pct = 70;                  /* v1.2 settings ride the payload */
    p.pot_mp_pct = 40;
    p.skills_off = (uint8_t)(1u << 2);  /* 施毒术 switched off */
    p.auto_sell = 0x05;                 /* white + blue sell at once */
    grant_books(&p, 3);                 /* books for idx 1..3 (精神力/施毒/火符) */
    assert(mafa_inv_add(&p, find_item("金项链", 1, 3)));   /* → slot 0 */
    assert(mafa_inv_add(&p, find_item("骷髅头盔", 1, 1))); /* → slot 1 */
    assert(mafa_equip(&p, 0));          /* wears 金项链 (pos 3) */
    p.hp = 111;
    p.mp = 22;
    p.rng = 0xDEADBEEF;                 /* live stream; deliberately not saved */

    uint8_t buf[80];
    size_t n = mafa_save_serialize(&p, buf, sizeof buf);
    assert(n == 4 + MAFA_SAVE_BODY_V8 + 1);
    assert(buf[3] == MAFA_SAVE_VERSION && buf[3] == 8);

    mafa_player_t q;
    mafa_player_init(&q, MAFA_CLS_WARRIOR, 1);
    assert(mafa_save_deserialize(&q, buf, n));
    assert(q.cls == p.cls && q.level == p.level && q.xp == 123456);
    assert(q.gold == 12345678 && q.pot_red == 3 && q.pot_blue == 4);
    assert(q.books == p.books);         /* 32-bit bitmask survives intact */
    assert(q.unlocked == 1 && q.map == MAFA_MAP_SAFE && q.kills == 17);
    assert(q.floor == 0 && q.floor_unlocked[0] == 3);
    for (int i = 1; i < MAFA_MAP_COUNT - 1; ++i)
        assert(q.floor_unlocked[i] == 1);
    assert(q.auto_potion == false && q.auto_boss == true);
    assert(q.pot_hp_pct == 70 && q.pot_mp_pct == 40);
    assert(q.skills_off == (1u << 2) && q.auto_sell == 0x05);
    assert(q.equipped[3] == find_item("金项链", 1, 3));  /* necklace position */
    assert(q.hp == 111 && q.mp == 22);
    assert(q.rng > 0);                  /* the live stream is kept, not saved */

    buf[5] ^= 0xFF;                     /* corrupt the payload */
    assert(!mafa_save_deserialize(&q, buf, n));
    /* Deserialization failure must not disturb the live player. */
    assert(q.cls == MAFA_CLS_TAOIST && q.gold == 12345678);

    /* The threshold lines are domain-checked: a forged out-of-range value
     * is rejected like any other corrupt field. */
    assert(mafa_save_serialize(&p, buf, sizeof buf));
    p.pot_hp_pct = 90;
    assert(mafa_save_serialize(&p, buf, sizeof buf));
    assert(!mafa_save_deserialize(&q, buf, n));

    /* A forged floor beyond the ladder is rejected too. */
    assert(mafa_save_serialize(&p, buf, sizeof buf));
    p.floor_unlocked[6] = 8;            /* 赤月峡谷 tops out at 3 */
    assert(mafa_save_serialize(&p, buf, sizeof buf));
    assert(!mafa_save_deserialize(&q, buf, n));

    /* A forged item in the wrong position (helmet id on the wrist) is
     * rejected: twins validate by slot type. */
    assert(mafa_save_serialize(&p, buf, sizeof buf));
    p.equipped[4] = 3;                  /* 骷髅头盔 is not a bracelet */
    assert(mafa_save_serialize(&p, buf, sizeof buf));
    assert(!mafa_save_deserialize(&q, buf, n));
}

static void test_v7_save_migration_to_v8(void) {
    /* v7 → v8: the payload only widens gold, so a v7 save must load with
     * gold, layout and settings intact and need no other migration. The
     * v7 blob is spliced from a v8 serialization: the 2-byte gold field
     * replaces the 4-byte one and the tail shifts down two bytes. */
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_TAOIST, 77);
    p.level = 18;
    p.xp = 123456;
    p.gold = 45678;                     /* < 65536: lossless in 2 bytes */
    p.pot_red = 3;
    p.pot_blue = 4;
    p.unlocked = 1;
    p.map = MAFA_MAP_SAFE;
    p.floor = 0;
    p.floor_unlocked[0] = 3;
    p.pot_hp_pct = 70;
    p.pot_mp_pct = 40;
    p.auto_sell = 0x05;
    uint8_t v8[80];
    size_t n8 = mafa_save_serialize(&p, v8, sizeof v8);
    assert(n8 == 4 + MAFA_SAVE_BODY_V8 + 1);

    uint8_t v7[80];
    memcpy(v7, v8, 12);                 /* magic + cls/level/xp */
    v7[10] = v8[10];                    /* gold kept low 16 bits */
    v7[11] = v8[11];
    memcpy(v7 + 12, v8 + 14, n8 - 14 - 1);  /* hp onward, sans CRC */
    v7[3] = 7;
    v7[4 + MAFA_SAVE_BODY_V7] = crc8(v7 + 4, MAFA_SAVE_BODY_V7);

    mafa_player_t q;
    mafa_player_init(&q, MAFA_CLS_WARRIOR, 1);
    assert(mafa_save_deserialize(&q, v7, 4 + MAFA_SAVE_BODY_V7 + 1));
    assert(q.gold == 45678);            /* the 2-byte branch read it */
    assert(q.level == 18 && q.xp == 123456);
    assert(q.pot_red == 3 && q.pot_blue == 4);
    assert(q.pot_hp_pct == 70 && q.pot_mp_pct == 40
           && q.auto_sell == 0x05);     /* the shifted tail survived */
    assert(q.map == MAFA_MAP_SAFE && q.unlocked == 1
           && q.floor_unlocked[0] == 3);
}

static void test_init_defaults(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_MAGE, 5);
    assert(p.auto_sell == 0x01);        /* white only (the old behavior) */
    assert(p.pot_hp_pct == 50 && p.pot_mp_pct == 30);
    assert(p.skills_off == 0);          /* every skill starts switched on */
    assert(p.pot_red == 3 && p.pot_blue == 0);   /* starter pack */
    assert(p.floor == 1);
    for (int i = 0; i < MAFA_MAP_COUNT - 1; ++i)
        assert(p.floor_unlocked[i] == 1);
}

static void test_v3_save_migration_to_v6(void) {
    /* v3 saves (40-byte body) read as v6 with default thresholds/switches;
     * the old flags byte's auto-sell-white bit maps onto the white bit, and
     * the old loadout becomes the band starter kit. Hand-built: the v6 body
     * widened books mid-stream, so v6 bytes cannot be trimmed into v3. */
    uint8_t buf[80] = {'M', 'F', 'C', 3};
    uint8_t *w = buf + 4;
    *w++ = MAFA_CLS_WARRIOR; *w++ = 1;          /* cls, level */
    *w++ = 0; *w++ = 0; *w++ = 0; *w++ = 0;     /* xp32 */
    *w++ = 0; *w++ = 0;                         /* gold */
    *w++ = 0; *w++ = 0;                         /* hp */
    *w++ = 0; *w++ = 0;                         /* mp */
    *w++ = 0; *w++ = 0;                         /* books16 (dropped) */
    *w++ = 0; *w++ = 0;                         /* pots */
    *w++ = 0; *w++ = 0;                         /* kills */
    *w++ = 1 | (1 << 2);                        /* map 1, unlocked 1 */
    *w++ = 0xFF;                                /* no pending drop */
    *w++ = 0xFF; *w++ = 0xFF; *w++ = 0xFF;      /* old 3-slot doll: empty */
    for (int i = 0; i < MAFA_BACKPACK; ++i) { *w++ = 0xFF; *w++ = 0; }
    *w++ = 0;                                   /* spare */
    assert((size_t)(w - (buf + 4)) == MAFA_SAVE_BODY_V2);
    buf[4 + MAFA_SAVE_BODY_V2] = crc8(buf + 4, MAFA_SAVE_BODY_V2);

    mafa_player_t q;
    mafa_player_init(&q, MAFA_CLS_MAGE, 9);
    assert(mafa_save_deserialize(&q, buf, (size_t)(w - buf) + 1));
    assert(q.pot_hp_pct == MAFA_POT_HP_PCT_DEFAULT);
    assert(q.pot_mp_pct == MAFA_POT_MP_PCT_DEFAULT);
    assert(q.skills_off == 0);
    assert(q.auto_sell == 0x00);        /* v3 flags carry no sell bit */
    assert(q.floor_unlocked[0] == 1 && q.floor == 1);   /* fresh ladder */
    for (int i = 3; i < MAFA_MAP_COUNT - 1; ++i)
        assert(q.floor_unlocked[i] == 1);        /* maps 4-7 fresh */
    /* Starter kit: level 1 → band 1, tier-2 singles equipped. */
    assert(q.equipped[0] == 1 && q.equipped[1] == 4);
    assert(q.equipped[2] == 7 && q.equipped[3] == 10);
    assert(q.equipped[4] == MAFA_INV_EMPTY);     /* twins stay grindable */

    /* An old save with 自动卖白 on (flags 0x20) maps to white-only. */
    buf[4 + 18] |= 0x20;                /* the v2/v3 flags byte */
    buf[4 + MAFA_SAVE_BODY_V2] = crc8(buf + 4, MAFA_SAVE_BODY_V2);
    assert(mafa_save_deserialize(&q, buf, (size_t)(w - buf) + 1));
    assert(q.auto_sell == 0x01);
}

static void test_v1_save_migration(void) {
    /* Hand-built v1 payload (pre skills-2.0): xp16, no books. */
    uint8_t buf[80] = {'M', 'F', 'C', 1};
    uint8_t *w = buf + 4;
    *w++ = MAFA_CLS_MAGE;               /* cls */
    *w++ = 18;                          /* level: 雷电 L17 passed, 爆裂 L22 not */
    *w++ = 123 & 0xFF; *w++ = 123 >> 8; /* xp */
    *w++ = 456 & 0xFF; *w++ = 456 >> 8; /* gold */
    *w++ = 111 & 0xFF; *w++ = 0;        /* hp */
    *w++ = 22 & 0xFF; *w++ = 0;         /* mp */
    *w++ = 3; *w++ = 4;                 /* pots */
    *w++ = 17 & 0xFF; *w++ = 0;         /* kills */
    *w++ = 1 | (2 << 2) | 0x30;         /* v1 map1, unlocked2, potion+sell */
    *w++ = 0xFF;                        /* no pending drop */
    *w++ = 0xFF; *w++ = 0xFF; *w++ = 0xFF;      /* nothing equipped */
    for (int i = 0; i < MAFA_BACKPACK; ++i) { *w++ = 0xFF; *w++ = 0; }
    *w++ = 0;                           /* spare */
    assert((size_t)(w - (buf + 4)) == MAFA_SAVE_BODY_V1);
    buf[4 + MAFA_SAVE_BODY_V1] = crc8(buf + 4, MAFA_SAVE_BODY_V1);

    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 9);
    assert(mafa_save_deserialize(&p, buf, (size_t)(w - buf) + 1));
    assert(p.cls == MAFA_CLS_MAGE && p.level == 18 && p.xp == 123);
    /* v1 map/unlocked migrate into the v3 numbering: old map 1 (Mine)
     * becomes 2, old unlocked 2 becomes 3. Left-behind maps count as fully
     * climbed; the top map re-climbs from floor 1 (capped at 石墓's 4). */
    assert(p.gold == 456 && p.kills == 17 && p.map == 2 && p.unlocked == 3);
    assert(p.floor_unlocked[0] == 3 && p.floor_unlocked[1] == 3
           && p.floor_unlocked[2] == 1 && p.floor == 3);
    /* Migration granted every book whose NEW unlock level is reached. */
    assert(mafa_skill_known(&p, 0));    /* 火球 L7 */
    assert(mafa_skill_known(&p, 1));    /* 雷电 L17 ≤ 18 */
    assert(!mafa_skill_known(&p, 2));   /* 爆裂 L22 > 18 */
    uint32_t expected = (1u << (MAFA_CLS_MAGE * MAFA_SKILLS_PER_CLASS + 1));
    assert(p.books == expected);
    /* Level 18 → band 3 (石墓): the migration kit wears its tier-2 set for
     * the mage's own line (偃月) plus the neutral singles. */
    assert(p.equipped[0] == find_item("偃月", 3, 2)
           && p.equipped[3] == find_item("放大镜", 3, 2));
    /* The v1 flags byte had 自动卖白 on (0x30 = potion + sell). */
    assert(p.auto_sell == 0x01);
    assert(p.pot_hp_pct == MAFA_POT_HP_PCT_DEFAULT && p.pot_mp_pct == 30);
}

static void test_v2_save_map_migration(void) {
    /* v2 saves (pre v1.0) use the old ids: 0-2 combat + town 3, unlocked
     * 0-2. Hand-built payloads (v6 bytes cannot be trimmed to v2); loading
     * must shift them into the v3 numbering. */
    uint8_t buf[80];

    for (int c = 0; c < 2; ++c) {
        uint8_t *w = buf + 4;
        buf[0] = 'M'; buf[1] = 'F'; buf[2] = 'C'; buf[3] = 2;
        *w++ = MAFA_CLS_WARRIOR; *w++ = 1;
        *w++ = 0; *w++ = 0; *w++ = 0; *w++ = 0;     /* xp32 */
        *w++ = 0; *w++ = 0;                         /* gold */
        *w++ = 0; *w++ = 0;                         /* hp */
        *w++ = 0; *w++ = 0;                         /* mp */
        *w++ = 0; *w++ = 0;                         /* books16 (dropped) */
        *w++ = 0; *w++ = 0;                         /* pots */
        *w++ = 0; *w++ = 0;                         /* kills */
        *w++ = c == 0 ? (uint8_t)(2 | (1 << 2))     /* old map 2, unlocked 1 */
                      : (uint8_t)(3 | (0 << 2));    /* old town 3, unlocked 0 */
        if (c == 0) buf[4 + 18] |= 0x20;            /* old 自动卖白 flag on */
        *w++ = 0xFF;                                /* no pending drop */
        *w++ = 0xFF; *w++ = 0xFF; *w++ = 0xFF;      /* old doll: empty */
        for (int i = 0; i < MAFA_BACKPACK; ++i) { *w++ = 0xFF; *w++ = 0; }
        *w++ = 0;                                   /* spare */
        assert((size_t)(w - (buf + 4)) == MAFA_SAVE_BODY_V2);
        buf[4 + MAFA_SAVE_BODY_V2] = crc8(buf + 4, MAFA_SAVE_BODY_V2);

        mafa_player_t q;
        mafa_player_init(&q, MAFA_CLS_MAGE, 6);
        assert(mafa_save_deserialize(&q, buf, (size_t)(w - buf) + 1));
        assert(q.auto_sell == (c == 0 ? 0x01 : 0x00));
        if (c == 0) {
            assert(q.map == 3 && q.unlocked == 2);
            /* 废矿 left behind = fully climbed; the top map re-climbs
             * from floor 1 (capped at the new ladders). */
            assert(q.floor_unlocked[0] == 3 && q.floor_unlocked[1] == 1);
            assert(q.floor == 1);
        } else {
            assert(q.map == MAFA_MAP_SAFE && q.unlocked == 1);
            assert(q.floor == 0);
        }
    }
}

static void test_v5_save_migration(void) {
    /* v5 saves (46-byte body): the three floor bytes belong to the old
     * ladders — 祖玛's 7 floors clamp into 石墓's 4, maps 4-7 open fresh,
     * books re-grant by the new levels, xp clamps to the new curve.
     * Hand-built: the v6 body widened books mid-stream, so v6 bytes cannot
     * be trimmed into v5 either. */
    uint8_t buf[80] = {'M', 'F', 'C', 5};
    uint8_t *w = buf + 4;
    *w++ = MAFA_CLS_TAOIST; *w++ = 15;          /* cls, level */
    *w++ = 0xFF & 0xFF; *w++ = 0x4F; *w++ = 0x02; *w++ = 0;   /* xp = 149999 */
    *w++ = 0xC8 & 0xFF; *w++ = 0x01;            /* gold = 456 */
    *w++ = 111 & 0xFF; *w++ = 0;                /* hp */
    *w++ = 22 & 0xFF; *w++ = 0;                 /* mp */
    *w++ = 0xFF; *w++ = 0xFF;                   /* books16 (dropped) */
    *w++ = 3; *w++ = 4;                         /* pots */
    *w++ = 17 & 0xFF; *w++ = 0;                 /* kills */
    *w++ = 3 | (3 << 2);                        /* map 3, unlocked 3 */
    *w++ = 0xFF;                                /* no pending drop */
    *w++ = 0xFF; *w++ = 0xFF; *w++ = 0xFF;      /* old doll: empty */
    for (int i = 0; i < MAFA_BACKPACK; ++i) { *w++ = 0xFF; *w++ = 0; }
    *w++ = 0;                                   /* skills_off */
    *w++ = MAFA_POT_HP_PCT_DEFAULT;             /* pot_hp_pct */
    *w++ = MAFA_POT_MP_PCT_DEFAULT;             /* pot_mp_pct */
    *w++ = 0x01;                                /* auto_sell: white */
    *w++ = 2; *w++ = 3; *w++ = 7;               /* old floors: 2/3/7 */
    assert((size_t)(w - (buf + 4)) == MAFA_SAVE_BODY_V5);
    buf[4 + MAFA_SAVE_BODY_V5] = crc8(buf + 4, MAFA_SAVE_BODY_V5);

    mafa_player_t q;
    mafa_player_init(&q, MAFA_CLS_MAGE, 7);
    assert(mafa_save_deserialize(&q, buf, (size_t)(w - buf) + 1));
    assert(q.level == 15 && q.unlocked == 3 && q.map == 3);
    assert(q.floor_unlocked[0] == 2 && q.floor_unlocked[1] == 3
           && q.floor_unlocked[2] == 4);        /* 7 clamps to 石墓's 4 */
    for (int i = 3; i < MAFA_MAP_COUNT - 1; ++i)
        assert(q.floor_unlocked[i] == 1);       /* maps 4-7 fresh */
    assert(q.floor == 4);
    /* Old book bits dropped; new grants by level 15: 精神力9/施毒14 yes,
     * 火符18 no. */
    uint32_t expect = (1u << (MAFA_CLS_TAOIST * MAFA_SKILLS_PER_CLASS + 1))
                    | (1u << (MAFA_CLS_TAOIST * MAFA_SKILLS_PER_CLASS + 2));
    assert(q.books == expect);
    /* xp clamps below the new L15→16 cost (27000). */
    assert(q.xp == 26999);
    /* Level 15 → band 3 (石墓): the kit wears the taoist-line weapon
     * (降魔) plus the neutral singles. */
    assert(q.equipped[0] == find_item("降魔", 3, 2)
           && q.equipped[3] == find_item("放大镜", 3, 2));
    assert(q.auto_sell == 0x01);
}

static void test_auto_potion_thresholds(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_MAGE, 55);
    mafa_battle_t b;
    mafa_events_t ev;

    /* 75 % HP with the red line at 80 %: the potion fires. */
    p.level = 5;
    p.pot_red = 5;
    p.pot_blue = 5;
    p.pot_hp_pct = 80;
    mafa_stats_t st;
    mafa_stats(&p, &st);
    p.hp = (int16_t)(st.max_hp * 3 / 4);
    assert(mafa_battle_start(&p, &b));
    mafa_battle_round(&p, &b, &ev);
    bool healed = false;
    for (int i = 0; i < ev.n; ++i)
        if (ev.e[i].kind == MAFA_EV_HEAL && ev.e[i].id == 1) healed = true;
    assert(healed && p.pot_red == 4);

    /* 75 % HP with the red line at 20 %: the potion stays put. */
    mafa_player_init(&p, MAFA_CLS_MAGE, 55);
    p.level = 5;
    p.pot_red = 5;
    p.pot_blue = 5;
    p.pot_hp_pct = 20;
    mafa_stats(&p, &st);
    p.hp = (int16_t)(st.max_hp * 3 / 4);
    assert(mafa_battle_start(&p, &b));
    mafa_battle_round(&p, &b, &ev);
    healed = false;
    for (int i = 0; i < ev.n; ++i)
        if (ev.e[i].kind == MAFA_EV_HEAL && ev.e[i].id == 1) healed = true;
    assert(!healed && p.pot_red == 5);

    /* The blue line: a quarter tank with the default 30 % line drinks. */
    mafa_player_init(&p, MAFA_CLS_MAGE, 55);
    p.level = 5;
    p.pot_red = 0;                      /* keep the red branch out of the way */
    p.pot_blue = 5;
    mafa_stats(&p, &st);
    p.hp = st.max_hp;
    p.mp = (int16_t)(st.max_mp / 4);
    assert(mafa_battle_start(&p, &b));
    mafa_battle_round(&p, &b, &ev);
    healed = false;
    for (int i = 0; i < ev.n; ++i)
        if (ev.e[i].kind == MAFA_EV_HEAL && ev.e[i].id == 2) healed = true;
    assert(healed && p.pot_blue == 4);
}

static void test_skill_toggle_respected(void) {
    /* 半月弯刀 (idx 3, L28) switched off: zero casts across the multi-mob
     * battles the crowd gate would normally light up in. */
    int casts = 0;
    for (int round = 0; round < 2; ++round) {
        for (int i = 0; i < 60; ++i) {
            mafa_player_t p;
            mafa_player_init(&p, MAFA_CLS_WARRIOR, (uint32_t)(31337 + i));
            p.level = 30;
            grant_books(&p, 3);         /* shelf books only: 半月 known */
            p.skills_off = round == 0 ? (uint8_t)(1u << 3) : 0;
            p.unlocked = 3;
            p.map = 3;
            p.floor = MAFA_MAP_FLOORS[3];   /* deep multi-mob pool */
            mafa_battle_t b;
            if (!mafa_battle_start(&p, &b) || b.mob_n < 2) continue;
            mafa_events_t ev;
            int guard = 0;
            while (!b.over && guard++ < 1000) {
                mafa_battle_round(&p, &b, &ev);
                for (int k = 0; k < ev.n; ++k)
                    if (ev.e[k].kind == MAFA_EV_SKILL_HIT && ev.e[k].id == 3)
                        casts++;
                mafa_stats_t st;
                mafa_stats(&p, &st);
                if (p.hp < st.max_hp) p.hp = (int16_t)st.max_hp;
                if (p.pending_drop != MAFA_DROP_NONE) mafa_drop_discard(&p);
            }
        }
        if (round == 0) assert(casts == 0);     /* off: never casts */
    }
    assert(casts > 0);                          /* on: the same seeds cast */
}

static void test_auto_sell_quality_mask(void) {
    /* Every quality enabled: the first drops sell at once, never stored. */
    int sold = 0;
    for (int i = 0; i < 120 && sold == 0; ++i) {
        mafa_player_t p;
        mafa_player_init(&p, MAFA_CLS_WARRIOR, (uint32_t)(90210 + i));
        p.level = 22;                   /* survive map 3 long enough to loot */
        mafa_stats_t pst;
        mafa_stats(&p, &pst);
        p.hp = (int16_t)pst.max_hp;
        p.pot_red = 60;
        p.auto_sell = 0x0F;
        p.unlocked = 3;
        p.map = 3;
        p.floor = MAFA_MAP_FLOORS[3];
        mafa_battle_t b;
        if (!mafa_battle_start(&p, &b)) continue;
        mafa_events_t ev;
        int guard = 0;
        while (!b.over && guard++ < 1000) {
            mafa_battle_round(&p, &b, &ev);
            for (int k = 0; k < ev.n; ++k)
                if (ev.e[k].kind == MAFA_EV_DROP) {
                    /* Map 3 rows stop at purple: everything in the mask
                     * sells, nothing can be gold here. */
                    assert(MAFA_ITEMS[ev.e[k].id].map == 3);
                    assert(ev.e[k].a == 2);     /* sold, not stored */
                    sold++;
                }
            if (p.pending_drop != MAFA_DROP_NONE) mafa_drop_discard(&p);
        }
    }
    assert(sold > 0);

    /* Nothing enabled: every drop takes backpack space instead. */
    int stored = 0;
    for (int i = 0; i < 120 && stored == 0; ++i) {
        mafa_player_t p;
        mafa_player_init(&p, MAFA_CLS_WARRIOR, (uint32_t)(90210 + i));
        p.level = 22;
        mafa_stats_t pst;
        mafa_stats(&p, &pst);
        p.hp = (int16_t)pst.max_hp;
        p.pot_red = 60;
        p.auto_sell = 0;
        p.unlocked = 3;
        p.map = 3;
        p.floor = MAFA_MAP_FLOORS[3];
        mafa_battle_t b;
        if (!mafa_battle_start(&p, &b)) continue;
        mafa_events_t ev;
        int guard = 0;
        while (!b.over && guard++ < 1000) {
            mafa_battle_round(&p, &b, &ev);
            for (int k = 0; k < ev.n; ++k)
                if (ev.e[k].kind == MAFA_EV_DROP) {
                    assert(ev.e[k].a != 2);     /* stored or prompted */
                    stored++;
                }
            if (p.pending_drop != MAFA_DROP_NONE) mafa_drop_discard(&p);
        }
    }
    assert(stored > 0);

    /* Gold never auto-sells, even with the full mask: a map-5 boss tier-3
     * roll is gold quality and must reach the backpack (a==1). Trash on
     * maps 5-7 caps at tier 2, so only the boss can produce it here. */
    int gold_drops = 0;
    for (int i = 0; i < 300 && gold_drops == 0; ++i) {
        mafa_player_t p;
        mafa_player_init(&p, MAFA_CLS_WARRIOR, (uint32_t)(5150 + i));
        p.level = 35;
        grant_books(&p, 3);
        mafa_stats_t pst;
        mafa_stats(&p, &pst);
        p.hp = (int16_t)pst.max_hp;
        p.auto_sell = 0x0F;
        p.unlocked = 5;
        p.map = 5;
        p.floor = MAFA_MAP_FLOORS[5];   /* 邪恶钳虫's own floor */
        p.hp = 32000;                   /* swing freely: the drop path is the
                                          point here, not the wall balance */
        p.pot_red = 60;
        p.kills = MAFA_KILLS_PER_BOSS;
        mafa_battle_t b;
        if (!mafa_boss_start(&p, &b)) continue;
        mafa_events_t ev;
        int guard = 0;
        while (!b.over && guard++ < 100000) {
            mafa_battle_round(&p, &b, &ev);
            for (int k = 0; k < ev.n; ++k)
                if (ev.e[k].kind == MAFA_EV_DROP) {
                    if (MAFA_ITEMS[ev.e[k].id].quality == MAFA_Q_GOLD) {
                        assert(ev.e[k].a == 1);  /* stored, never sold */
                        gold_drops++;
                    } else {
                        assert(ev.e[k].a == 2);
                    }
                }
            if (p.pending_drop != MAFA_DROP_NONE) mafa_drop_discard(&p);
            if (b.player_dead) break;       /* respawn and try again */
        }
    }
    assert(gold_drops > 0);
}

static void test_safe_zone_no_combat_and_open_door(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_MAGE, 99);
    assert(p.unlocked == 1);
    mafa_switch_map(&p, MAFA_MAP_SAFE, 0);  /* the town is open from day 1 */
    assert(p.map == MAFA_MAP_SAFE && p.kills == 0);
    mafa_battle_t b;
    assert(!mafa_battle_start(&p, &b));     /* no spawns in town */
    p.kills = MAFA_KILLS_PER_BOSS;
    assert(!mafa_boss_start(&p, &b));       /* no boss in town */
    assert(mafa_map_boss(MAFA_MAP_SAFE, 0) == NULL);
    mafa_switch_map(&p, 2, 1);              /* locked map: ignored */
    assert(p.map == MAFA_MAP_SAFE);
    mafa_switch_map(&p, 1, 1);              /* walk back out */
    assert(p.map == 1);
    assert(mafa_battle_start(&p, &b));
    assert(mafa_map_boss(1, 1) == &MAFA_MONSTERS[2]);   /* 稻草人, the pen */
    assert(mafa_map_boss(1, 2) == &MAFA_MONSTERS[6]);   /* 半兽勇士 */
    assert(mafa_map_boss(1, 3) == &MAFA_MONSTERS[8]);   /* 半兽统领 */
    assert(mafa_map_boss(1, 4) == NULL);    /* no such floor */
}

/* The whole 1.76 ladder: every (map, floor) boss opens the next step, and
 * each map's last-floor boss opens the next map. Trash pools grow with
 * depth (cumulative floors). */
static void test_floor_ladder_walk(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 2026);
    int total_bosses = 0;
    for (int map = 1; map < MAFA_MAP_COUNT; ++map) {
        for (int floor = 1; floor <= MAFA_MAP_FLOORS[map]; ++floor) {
            assert(p.map == map && p.floor == floor);
            /* Cumulative pool: this floor's trash spawned somewhere. */
            mafa_battle_t b;
            assert(mafa_battle_start(&p, &b));
            for (int k = 0; k < b.mob_n; ++k)
                assert(b.mob[k].base->floor <= floor);
            /* Force the boss fight and win it (retry: a L40 full-kit
             * warrior only rarely loses, but never let the test flake). */
            p.level = 40;
            grant_books(&p, MAFA_SKILLS_PER_CLASS - 1);
            bool won = false;
            for (int attempt = 0; attempt < 20 && !won; ++attempt) {
                p.map = (uint8_t)map;           /* a death sent us to town */
                p.floor = (uint8_t)floor;
                p.hp = 32000;
                p.pot_red = 60;
                p.pot_blue = 60;
                p.kills = MAFA_KILLS_PER_BOSS;
                mafa_battle_t boss;
                assert(mafa_boss_start(&p, &boss));
                assert(boss.mob[0].base->floor == floor);
                int guard = 0;
                while (!boss.over && guard++ < 100000) {
                    mafa_battle_round(&p, &boss, &(mafa_events_t){0});
                    if (p.pending_drop != MAFA_DROP_NONE) mafa_drop_discard(&p);
                }
                assert(guard < 100000);
                won = !boss.player_dead;
            }
            assert(won);
            total_bosses++;
            bool last = floor == MAFA_MAP_FLOORS[map];
            if (!last) {
                assert(p.floor == floor + 1);
                assert(p.floor_unlocked[map - 1] == floor + 1);
            } else if (map + 1 < MAFA_MAP_COUNT) {
                assert(p.unlocked == map + 1);
                mafa_switch_map(&p, (uint8_t)(map + 1), 1);  /* walk onward */
            }
        }
    }
    assert(total_bosses == 27);          /* 3+3+4+3+4+7+3 checkpoints */
}

static void test_battle_terminates_over_many_maps(void) {
    for (int map = 1; map < MAFA_MAP_COUNT; ++map) {
        for (int floor = 1; floor <= MAFA_MAP_FLOORS[map]; ++floor) {
            for (int cls = 0; cls < MAFA_CLS_COUNT; ++cls) {
                mafa_player_t p;
                mafa_player_init(&p, (uint8_t)cls,
                                 1000 + map * 7 + floor * 3 + cls);
                p.unlocked = 7;
                p.map = (uint8_t)map;
                p.floor = (uint8_t)floor;
                p.level = 40;            /* strongest case must still terminate */
                grant_books(&p, MAFA_SKILLS_PER_CLASS - 1);
                mafa_battle_t b;
                assert(mafa_battle_start(&p, &b));
                int rounds = 0;
                while (!b.over) {
                    mafa_battle_round(&p, &b, &(mafa_events_t){0});
                    assert(++rounds < 100000);
                    assert(p.hp >= 0);
                    if (p.hp == 0) break;   /* death is a valid ending */
                }
                /* A death respawns the player in the safe zone; walk back
                 * before facing the boss. */
                p.map = (uint8_t)map;
                p.floor = (uint8_t)floor;
                /* Bosses must also terminate (taoist pet + full kit). */
                p.kills = MAFA_KILLS_PER_BOSS;
                mafa_battle_t boss;
                assert(mafa_boss_start(&p, &boss));
                rounds = 0;
                while (!boss.over) {
                    mafa_battle_round(&p, &boss, &(mafa_events_t){0});
                    assert(++rounds < 100000);
                    if (p.hp == 0) break;
                }
            }
        }
    }
}

static void test_ring_and_potion_drops(void) {
    /* v1.5: rings drop again (the old MAFA_SLOT_TYPES=5 bug made the ring
     * slot unreachable), and trash supplies 金创药/魔法药 straight into the
     * counters. */
    int rings = 0, potions = 0, reds = 0, blues = 0;
    for (int i = 0; i < 400 && (rings == 0 || potions == 0); ++i) {
        mafa_player_t p;
        mafa_player_init(&p, MAFA_CLS_WARRIOR, (uint32_t)(700 + i));
        p.level = 6;
        p.auto_sell = 0;                /* keep every drop observable */
        p.unlocked = 1;
        p.map = 1;
        p.floor = MAFA_MAP_FLOORS[1];
        mafa_battle_t b;
        if (!mafa_battle_start(&p, &b)) continue;
        mafa_events_t ev;
        int guard = 0;
        while (!b.over && guard++ < 1000) {
            mafa_battle_round(&p, &b, &ev);
            for (int k = 0; k < ev.n; ++k) {
                if (ev.e[k].kind == MAFA_EV_DROP
                    && MAFA_ITEMS[ev.e[k].id].slot == MAFA_ST_RING)
                    rings++;
                if (ev.e[k].kind == MAFA_EV_POTION) {
                    potions++;
                    if (ev.e[k].id == 1) reds++;
                    else blues++;
                }
            }
            if (p.pending_drop != MAFA_DROP_NONE) mafa_drop_discard(&p);
        }
    }
    assert(rings > 0);                  /* the ring slot is reachable again */
    assert(potions > 0);
    assert(reds > blues);               /* the 60/40 red weighting shows */
}

static void test_class_line_scaling(void) {
    /* 治愈术 heals max_hp×30 % + 2×道术: the v1.5 道术 line is live. */
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_TAOIST, 1010);
    p.level = 20;
    mafa_stats_t st;
    mafa_stats(&p, &st);
    int32_t expected = st.max_hp * 25 / 100 + st.sc;
    assert(expected > st.max_hp * 25 / 100);   /* the 道术 term matters */
    p.hp = 1;                           /* well under the 60 % gate */
    p.pot_red = 0;
    p.unlocked = 1;
    p.map = 1;
    p.floor = 1;
    mafa_battle_t b;
    assert(mafa_battle_start(&p, &b));
    mafa_events_t ev;
    int healed = -1;
    int guard = 0;
    while (healed < 0 && guard++ < 200 && !b.over) {
        mafa_battle_round(&p, &b, &ev);
        for (int k = 0; k < ev.n; ++k)
            if (ev.e[k].kind == MAFA_EV_HEAL && ev.e[k].id == 0)
                healed = (int)ev.e[k].a;
    }
    assert(healed == (int)expected);
}

static void test_warrior_pays_mp(void) {
    /* 烈火 costs 8 MP now: the charge only arms on a mana-funded turn and
     * the pool drains below max during long fights. */
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 2024);
    p.level = 36;
    grant_books(&p, 6);
    p.unlocked = 1;
    p.map = 1;
    p.floor = 1;
    mafa_stats_t st;
    mafa_stats(&p, &st);
    int32_t pool = st.max_mp;
    assert(pool >= 8);                  /* 烈火's cost is affordable */
    mafa_battle_t b;
    assert(mafa_battle_start(&p, &b));
    mafa_events_t ev;
    bool spent = false;
    int guard = 0;
    while (!b.over && guard++ < 200) {
        int32_t before = p.mp;
        mafa_battle_round(&p, &b, &ev);
        if (p.mp < before) spent = true;   /* a skill drank from the pool */
        if (p.hp < st.max_hp / 2) p.hp = (int16_t)st.max_hp;
    }
    assert(spent);                      /* warrior actives are mana-fed */
}

static void test_caps_gold_and_potions(void) {
    /* v1.6: gold's ceiling is 65000 (still one uint16 save byte pair), and
     * potion stacks cap at the uint8-native 255 — buys used to take gold
     * and wrap the counter 255→0 unchecked. */
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 55);
    p.gold = MAFA_GOLD_CAP;
    assert(mafa_inv_add(&p, 12));       /* 大手镯 white sells at 10 */
    mafa_sell(&p, 0);
    assert(p.gold == MAFA_GOLD_CAP);    /* settlement clamps at the ceiling */

    p.gold = 10000;
    p.pot_red = MAFA_POT_CAP;
    p.pot_blue = MAFA_POT_CAP;
    assert(mafa_potion_full(&p, true) && mafa_potion_full(&p, false));
    assert(!mafa_buy_potion(&p, true));   /* full red stack refuses */
    assert(!mafa_buy_potion(&p, false));
    assert(p.gold == 10000);              /* no charge, no wrap */
    assert(p.pot_red == MAFA_POT_CAP && p.pot_blue == MAFA_POT_CAP);
}

static void test_potion_drop_respects_cap(void) {
    /* A full stack must swallow potion drops: a wrap here would erase a
     * maxed supply mid-grind. */
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 77);
    p.level = 6;
    p.auto_sell = 0x0F;                 /* sell every drop: no prompts stall
                                           the next battle_start */
    p.auto_potion = false;              /* nothing may drink from the stacks */
    p.unlocked = 1;
    p.pot_red = MAFA_POT_CAP;
    p.pot_blue = MAFA_POT_CAP;
    mafa_stats_t st;
    mafa_stats(&p, &st);
    /* At the cap a swallowed drop pushes no event, so coverage reads off
     * the kill count: each kill rolls the 15 % potion check once. A death
     * would strand the player in town, so every battle starts fresh. */
    int kills = 0;
    for (int i = 0; i < 200 && kills < 100; ++i) {
        p.map = 1;
        p.floor = MAFA_MAP_FLOORS[1];
        p.hp = (int16_t)st.max_hp;
        p.mp = st.max_mp;
        mafa_battle_t b;
        if (!mafa_battle_start(&p, &b)) continue;
        mafa_events_t ev;
        int guard = 0;
        while (!b.over && guard++ < 1000) {
            mafa_battle_round(&p, &b, &ev);
            for (int k = 0; k < ev.n; ++k)
                if (ev.e[k].kind == MAFA_EV_MOB_KILLED) kills++;
            if (p.pending_drop != MAFA_DROP_NONE) mafa_drop_discard(&p);
        }
        if (p.pending_drop != MAFA_DROP_NONE) mafa_drop_discard(&p);
    }
    assert(kills >= 100);               /* the cap branch was exercised */
    assert(p.pot_red == MAFA_POT_CAP);  /* …without wrapping */
    assert(p.pot_blue == MAFA_POT_CAP);
}

static void test_unequip_twin_positions(void) {
    /* v1.6: either bracelet/ring twin can come off — equipping alone always
     * replaced the FIRST twin, so the second one could never be swapped. */
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 88);
    assert(mafa_inv_add(&p, 12));       /* 大手镯 white → left twin */
    assert(mafa_inv_add(&p, 13));       /* 大手镯绿 green → right twin */
    assert(mafa_equip(&p, 0));
    assert(mafa_equip(&p, 1));
    assert(p.equipped[4] == 12 && p.equipped[5] == 13);
    mafa_stats_t st;
    mafa_stats(&p, &st);
    int32_t atk_both = st.atk;

    assert(!mafa_unequip(&p, 0));       /* an empty position refuses */
    assert(p.equipped[4] == 12 && p.equipped[5] == 13);

    /* Take the RIGHT twin off: back into its backpack slot, stats drop it. */
    assert(mafa_unequip(&p, 5));
    assert(p.equipped[5] == MAFA_INV_EMPTY && p.equipped[4] == 12);
    assert(p.inv_id[0] == 13 && p.inv_n[0] == 1);
    mafa_stats(&p, &st);
    assert(st.atk == atk_both - 1);     /* 大手镯绿's +1 attack is gone */

    /* Re-equipping refills the free twin instead of the worn one. */
    assert(mafa_equip(&p, 0));
    assert(p.equipped[4] == 12 && p.equipped[5] == 13);

    /* A full backpack keeps the piece worn. */
    for (int i = 0; i < MAFA_BACKPACK; ++i) {
        p.inv_id[i] = MAFA_INV_EMPTY;
        p.inv_n[i] = 0;
    }
    for (int i = 0; i < MAFA_BACKPACK; ++i)
        assert(mafa_inv_add(&p, (uint8_t)(i + 18)));
    assert(!mafa_unequip(&p, 4));
    assert(p.equipped[4] == 12);
}

int main(void) {
    test_stats_and_growth();
    test_xp_curve_front_fast_back_wall();
    test_book_gating_and_store();
    test_store_level_gate();
    test_battle_kills_and_settlement();
    test_multi_mob_pack_weights();
    test_starter_onramp_pool();
    test_boss_event_flow_and_floor_ladder();
    test_book_drop_gate_and_odds();
    test_taoist_pet_tanks();
    test_warrior_charge_and_proc();
    test_warrior_stun_skips_turns();
    test_taoist_armor_buff();
    test_mage_shield_reduces_damage();
    test_switch_map_requires_unlock();
    test_safe_zone_full_restore();
    test_inventory_equip_and_compare();
    test_full_backpack_drop_prompt();
    test_potions_and_store();
    test_auto_potion_thresholds();
    test_skill_toggle_respected();
    test_auto_sell_quality_mask();
    test_death_penalty_drops_and_gold();
    test_safe_zone_no_combat_and_open_door();
    test_init_defaults();
    test_save_roundtrip_v6();
    test_v3_save_migration_to_v6();
    test_v1_save_migration();
    test_v2_save_map_migration();
    test_v5_save_migration();
    test_floor_ladder_walk();
    test_battle_terminates_over_many_maps();
    test_ring_and_potion_drops();
    test_class_line_scaling();
    test_warrior_pays_mp();
    test_caps_gold_and_potions();
    test_potion_drop_respects_cap();
    test_unequip_twin_positions();
    test_v7_save_migration_to_v8();
    printf("test_mafa_model: all assertions passed\n");
    return 0;
}
