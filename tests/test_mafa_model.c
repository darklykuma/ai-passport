// tests/test_mafa_model.c — host tests for the MAFA CHRONICLE model
// (PRD_MAFA_CHRONICLE chapters 8-9, skills-2.0 rebalance). Deterministic
// seeds; probabilistic assertions use large loops. No floats.

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
    assert(st.max_hp == 60 && st.atk == 10 && st.def == 5 && st.max_mp == 0);
    p.level = 5;
    mafa_stats(&p, &st);
    assert(st.max_hp == 60 + 8 * 4 && st.atk == 10 + 2 * 4 && st.def == 9);

    mafa_player_init(&p, MAFA_CLS_MAGE, 42);
    mafa_stats(&p, &st);
    assert(st.max_hp == 40 && st.atk == 14 && st.def == 3 && st.max_mp == 30);
    p.level = 6;                        /* def +1 every 2 levels */
    mafa_stats(&p, &st);
    assert(st.def == 3 + 6 / 2 && st.max_mp == 30 + 5 * 5);

    mafa_player_init(&p, MAFA_CLS_TAOIST, 42);
    mafa_stats(&p, &st);
    assert(st.max_hp == 60 && st.max_mp == 25);
}

static void test_xp_curve_front_fast_back_wall(void) {
    assert(mafa_xp_to_next(1) == 300);
    assert(mafa_xp_to_next(2) == 550);
    assert(mafa_xp_to_next(8) == 5500);
    assert(mafa_xp_to_next(14) == 150000);
    assert(mafa_xp_to_next(MAFA_MAX_LEVEL) == 0);
    /* The 14→15 wall is ~45 % of the total grind (rebalance target). */
    uint32_t total = 0;
    for (int lv = 1; lv < MAFA_MAX_LEVEL; ++lv) total += mafa_xp_to_next((uint8_t)lv);
    uint32_t wall = mafa_xp_to_next(14);
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
        p->books |= (uint16_t)(1u << (p->cls * MAFA_SKILLS_PER_CLASS + i));
}

static void test_book_gating_and_store(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 7);

    /* Skill 0 needs no book; higher skills need level AND book. */
    assert(mafa_skill_known(&p, 0));
    assert(!mafa_skill_known(&p, 1));           /* no book yet */
    p.level = 3;
    assert(!mafa_skill_known(&p, 1));           /* level ok, book missing */
    p.books = (uint16_t)(1u << (MAFA_CLS_WARRIOR * MAFA_SKILLS_PER_CLASS + 1));
    assert(mafa_skill_known(&p, 1));

    /* Buying: needs gold, refuses duplicates, grants the skill. */
    mafa_player_t q;
    mafa_player_init(&q, MAFA_CLS_MAGE, 7);
    q.level = 8;
    q.gold = 300;
    assert(mafa_book_price(MAFA_CLS_MAGE, 1) == 300);
    assert(!mafa_buy_book(&q, 0));              /* skill 0 is not sold */
    assert(!mafa_buy_book(&q, 3));              /* books 3-4 drop in battle */
    assert(mafa_buy_book(&q, 1));
    assert(q.gold == 0 && mafa_skill_known(&q, 1));
    assert(!mafa_buy_book(&q, 1));              /* already learned */
    assert(!mafa_buy_book(&q, 2));              /* 800 > 0 gold */
    q.gold = 800;
    assert(mafa_buy_book(&q, 2) && mafa_skill_known(&q, 2));
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
        p.map = 3;                              /* deepest map: often 3 mobs */
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

static void test_boss_event_flow_and_book_guarantee(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 5);
    assert(!mafa_boss_ready(&p));
    mafa_battle_t b;
    assert(!mafa_boss_start(&p, &b));   /* not ready */
    p.kills = MAFA_KILLS_PER_BOSS;
    assert(mafa_boss_ready(&p));
    assert(mafa_boss_start(&p, &b));
    assert(b.is_boss && b.mob_n == 1);

    /* Fighting the floor-1 boss (森林雪人) and winning must open floor 2 of
     * the same map (v1.3) and guarantee the next missing late book (skill 3
     * first, then 4). */
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
        uint16_t b3 = (uint16_t)(1u << (MAFA_CLS_WARRIOR * MAFA_SKILLS_PER_CLASS + 3));
        assert(p.books & b3);               /* boss first-kill grants book 3 */
        /* Second boss kill (森林巨猿, floor 2 = the last floor) must hand
         * out book 4 and unlock map 2. */
        p.kills = MAFA_KILLS_PER_BOSS;
        mafa_battle_t b2;
        assert(mafa_boss_start(&p, &b2));
        guard = 0;
        bool saw_map_ev = false;
        while (!b2.over && guard < 100000) {
            mafa_battle_round(&p, &b2, &ev);
            guard++;
            for (int i = 0; i < ev.n; ++i)
                if (ev.e[i].kind == MAFA_EV_MAP_UNLOCK) saw_map_ev = true;
        }
        if (!b2.player_dead) {
            uint16_t b4 = (uint16_t)(2u << (MAFA_CLS_WARRIOR * MAFA_SKILLS_PER_CLASS + 3));
            assert((p.books & b4) == (b4 & 0x3FF));   /* book 4 bit set */
            assert(p.unlocked == 2 && saw_map_ev);    /* 废矿洞 opens */
            assert(p.floor == 2);                     /* stays on the top floor */
        }
    } else {
        assert(p.kills == 0);           /* death also resets the counter */
    }
    mafa_boss_pass(&p);
    assert(p.kills == 0);
}

static void test_taoist_pet_tanks(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_TAOIST, 31);
    p.level = 8;
    grant_books(&p, 1);                 /* 召唤骷髅 known */
    assert(mafa_skill_known(&p, 1));
    p.unlocked = 2;
    p.map = 2;                          /* tougher mobs: the fight must last */
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
    p.level = 12;
    grant_books(&p, 4);
    mafa_battle_t b;
    assert(mafa_battle_start(&p, &b));
    mafa_events_t ev;
    mafa_battle_round(&p, &b, &ev);     /* 烈火 priority: cast = charge armed */
    assert(b.charge_mult == 220);
    bool saw_charge_hit = false, saw_proc = false;
    int proc_hits = 0, swings = 0;
    for (int round = 0; round < 40 && !b.over; ++round) {
        mafa_battle_round(&p, &b, &ev);
        for (int i = 0; i < ev.n; ++i) {
            if (ev.e[i].kind == MAFA_EV_SKILL_HIT) {
                if (MAFA_SKILLS[p.cls][ev.e[i].id].kind == MAFA_SK_CHARGE)
                    saw_charge_hit = true;
                if (MAFA_SKILLS[p.cls][ev.e[i].id].kind == MAFA_SK_PROC)
                    saw_proc = true;
            }
            if (ev.e[i].kind == MAFA_EV_PLAYER_HIT) swings++;
        }
        if (b.over) break;
        /* Splice in fresh mobs so the proc keeps rolling over many swings. */
        if (b.over) break;
    }
    assert(saw_charge_hit);             /* the charged swing consumed the mark */
    assert(b.charge_mult == 0);

    /* Proc statistics: many plain swings must include 攻杀 procs (20 %). */
    mafa_player_t q;
    mafa_player_init(&q, MAFA_CLS_WARRIOR, 777);
    q.level = 9;
    grant_books(&q, 1);
    q.unlocked = 2;
    q.map = 2;                          /* same-level mobs: long fights */
    int procs = 0, total_swings = 0;
    for (int i = 0; i < 60; ++i) {
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
    (void)saw_proc; (void)proc_hits; (void)swings;
    assert(total_swings > 100);
    /* 20 % proc rate: 50..400 of 1000+ swings — loose binomial bounds. */
    assert(procs * 100 / total_swings > 8 && procs * 100 / total_swings < 40);
}

static void test_mage_shield_reduces_damage(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_MAGE, 64);
    p.level = 10;
    grant_books(&p, 3);
    p.unlocked = 2;
    p.map = 2;
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
    assert(p.map == 1);                 /* new games idle at once: Beech */
    assert(p.unlocked == 1);
    assert(p.floor == 1);               /* the ladder starts at floor 1 */
    mafa_switch_map(&p, 2, 1);          /* locked: ignored */
    assert(p.map == 1);
    mafa_switch_map(&p, 1, 2);          /* floor not unlocked yet: ignored */
    assert(p.map == 1 && p.floor == 1);
    p.unlocked = 3;
    p.floor_unlocked[2] = MAFA_MAP_FLOORS[3];
    mafa_switch_map(&p, 3, 7);
    assert(p.map == 3 && p.floor == 7);
    mafa_switch_map(&p, 3, 8);          /* beyond the ladder: ignored */
    assert(p.map == 3 && p.floor == 7);
    mafa_switch_map(&p, MAFA_MAP_SAFE, 0);      /* the town is always open */
    assert(p.map == MAFA_MAP_SAFE && p.floor == 0);
    mafa_switch_map(&p, 1, 1);          /* walk back out */
    assert(p.map == 1 && p.floor == 1);
}

static void test_inventory_equip_and_compare(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 3);
    /* Map-1 weapon ids: 0 木剑, 1 青铜剑, 2 铁剑. */
    assert(mafa_inv_add(&p, 0));
    assert(mafa_inv_add(&p, 0));        /* stacks */
    assert(p.inv_n[0] == 2);
    assert(mafa_equip(&p, 0));
    mafa_stats_t st;
    mafa_stats(&p, &st);
    assert(st.atk == 10 + 2);           /* 木剑 +2 */
    assert(p.inv_n[0] == 1);            /* one left in the stack */

    mafa_compare_t cmp;
    mafa_compare(&p, 2, &cmp);          /* 铁剑 +6 vs 木剑 +2 */
    assert(cmp.d_atk == 4);

    assert(mafa_equip(&p, 0));          /* stack shrinks to zero, slot frees */
    assert(p.equipped[MAFA_SLOT_WEAPON] == 0);
    mafa_stats(&p, &st);
    assert(st.atk == 10 + 2);           /* unchanged: swapped, not stacked */

    /* The returned 木剑 sits in the backpack; sell-all-whites clears it. */
    assert(mafa_sell_all_white(&p) == 10);
    uint32_t gold_before = p.gold;
    assert(mafa_inv_add(&p, 6));        /* 木珠, white accessory */
    uint32_t gained = mafa_sell(&p, 0);
    assert(gained == 10 && p.gold == gold_before + 10);
}

static void test_full_backpack_drop_prompt(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 1234);
    p.auto_sell = 0;                    /* sell nothing: force the prompt path */
    /* Eight distinct items fill every backpack slot. */
    static const uint8_t ids[MAFA_BACKPACK] = {1, 9, 10, 11, 12, 13, 18, 24};
    for (int i = 0; i < MAFA_BACKPACK; ++i)
        assert(mafa_inv_add(&p, ids[i]));
    p.pending_drop = 0;                 /* a 木剑 dropped with no room */
    mafa_drop_replace(&p, 3);           /* swap into slot 3, old item auto-sold */
    assert(p.pending_drop == MAFA_DROP_NONE);
    assert(p.inv_id[3] == 0);
    assert(p.gold >= 30);               /* 骷髅甲(绿) sold at 30 */
    p.pending_drop = 2;
    mafa_drop_discard(&p);
    assert(p.pending_drop == MAFA_DROP_NONE);
}

static void test_potions_and_store(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_MAGE, 21);
    p.gold = 90;
    assert(mafa_buy_potion(&p, true));      /* 50 */
    assert(mafa_buy_potion(&p, false));     /* 40 */
    assert(p.pot_red == 1 && p.pot_blue == 1 && p.gold == 0);
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
    for (int i = 0; i < MAFA_BACKPACK; ++i) assert(mafa_inv_add(&p, (uint8_t)(i + 9)));
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
    assert(p.gold <= 900 && p.gold >= 790);
}

static void test_save_roundtrip_v5(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_TAOIST, 77);
    p.level = 9;
    p.xp = 123456;                      /* > uint16 max: v2 widens xp */
    p.gold = 456;
    p.pot_red = 3;
    p.pot_blue = 4;
    p.unlocked = 1;
    p.map = MAFA_MAP_SAFE;              /* v0.9: town survives the roundtrip */
    p.floor = 0;
    p.floor_unlocked[0] = 2;            /* 比奇森林 fully climbed */
    p.kills = 17;
    p.auto_potion = false;
    p.auto_boss = true;
    p.pot_hp_pct = 70;                  /* v1.2 settings ride the payload */
    p.pot_mp_pct = 40;
    p.skills_off = (uint8_t)(1u << 2);  /* 灵魂火符 switched off */
    p.auto_sell = 0x05;                 /* white + blue sell at once */
    grant_books(&p, 2);                 /* books for idx 1 and 2 */
    assert(mafa_inv_add(&p, 11));
    assert(mafa_inv_add(&p, 3));
    assert(mafa_equip(&p, 1));          /* wears 骷髅甲 */
    p.hp = 111;
    p.mp = 22;
    p.rng = 0xDEADBEEF;                 /* live stream; deliberately not saved */

    uint8_t buf[64];
    size_t n = mafa_save_serialize(&p, buf, sizeof buf);
    assert(n == 4 + MAFA_SAVE_BODY_V5 + 1);
    assert(buf[3] == MAFA_SAVE_VERSION && buf[3] == 5);

    mafa_player_t q;
    mafa_player_init(&q, MAFA_CLS_WARRIOR, 1);
    assert(mafa_save_deserialize(&q, buf, n));
    assert(q.cls == p.cls && q.level == p.level && q.xp == 123456);
    assert(q.gold == p.gold && q.pot_red == 3 && q.pot_blue == 4);
    assert(q.books == p.books);
    assert(q.unlocked == 1 && q.map == MAFA_MAP_SAFE && q.kills == 17);
    assert(q.floor == 0 && q.floor_unlocked[0] == 2
           && q.floor_unlocked[1] == 1 && q.floor_unlocked[2] == 1);
    assert(q.auto_potion == false && q.auto_boss == true);
    assert(q.pot_hp_pct == 70 && q.pot_mp_pct == 40);
    assert(q.skills_off == (1u << 2) && q.auto_sell == 0x05);
    assert(q.equipped[MAFA_SLOT_ARMOR] == 3);
    assert(q.hp == 111 && q.mp == 22);
    assert(q.rng > 0);                  /* the live stream is kept, not saved */

    buf[5] ^= 0xFF;                     /* corrupt the payload */
    assert(!mafa_save_deserialize(&q, buf, n));
    /* Deserialization failure must not disturb the live player. */
    assert(q.cls == MAFA_CLS_TAOIST && q.gold == 456);

    /* The threshold lines are domain-checked: a forged out-of-range value
     * is rejected like any other corrupt field. */
    assert(mafa_save_serialize(&p, buf, sizeof buf));
    p.pot_hp_pct = 90;
    assert(mafa_save_serialize(&p, buf, sizeof buf));
    assert(!mafa_save_deserialize(&q, buf, n));

    /* A forged floor beyond the ladder is rejected too. */
    assert(mafa_save_serialize(&p, buf, sizeof buf));
    p.floor_unlocked[2] = 8;            /* 祖玛 tops out at 7 */
    assert(mafa_save_serialize(&p, buf, sizeof buf));
    assert(!mafa_save_deserialize(&q, buf, n));
}

static void test_init_defaults_v4(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_MAGE, 5);
    assert(p.auto_sell == 0x01);        /* white only (the old behavior) */
    assert(p.pot_hp_pct == 50 && p.pot_mp_pct == 30);
    assert(p.skills_off == 0);          /* every skill starts switched on */
    assert(p.floor == 1);
    for (int i = 0; i < MAFA_MAP_COUNT - 1; ++i)
        assert(p.floor_unlocked[i] == 1);
}

static void test_v3_save_migration_to_v4(void) {
    /* v3 saves (40-byte body) read as v4 with default thresholds/switches;
     * the old flags byte's auto-sell-white bit maps onto the white bit. */
    uint8_t buf[64];
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 8);
    assert(mafa_save_serialize(&p, buf, sizeof buf));
    buf[3] = 3;                         /* rewind the header to v3 */
    buf[4 + MAFA_SAVE_BODY_V2] = crc8(buf + 4, MAFA_SAVE_BODY_V2);

    mafa_player_t q;
    mafa_player_init(&q, MAFA_CLS_MAGE, 9);
    assert(mafa_save_deserialize(&q, buf, 4 + MAFA_SAVE_BODY_V2 + 1));
    assert(q.pot_hp_pct == MAFA_POT_HP_PCT_DEFAULT);
    assert(q.pot_mp_pct == MAFA_POT_MP_PCT_DEFAULT);
    assert(q.skills_off == 0);
    assert(q.auto_sell == 0x00);        /* v4 flags carry no sell bit */
    assert(q.floor_unlocked[0] == 1 && q.floor == 1);   /* fresh v1.3 ladder */

    /* An old save with 自动卖白 on (flags 0x20) maps to white-only. */
    assert(mafa_save_serialize(&p, buf, sizeof buf));
    buf[3] = 3;
    buf[4 + 18] |= 0x20;                /* the v2/v3 flags byte */
    buf[4 + MAFA_SAVE_BODY_V2] = crc8(buf + 4, MAFA_SAVE_BODY_V2);
    assert(mafa_save_deserialize(&q, buf, 4 + MAFA_SAVE_BODY_V2 + 1));
    assert(q.auto_sell == 0x01);
}

static void test_v1_save_migration(void) {
    /* Hand-built v1 payload (pre skills-2.0): xp16, no books. */
    uint8_t buf[64] = {'M', 'F', 'C', 1};
    uint8_t *w = buf + 4;
    *w++ = MAFA_CLS_MAGE;               /* cls */
    *w++ = 10;                          /* level: 火墙 L8 and 魔法盾 L10 passed */
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
    assert(p.cls == MAFA_CLS_MAGE && p.level == 10 && p.xp == 123);
    /* v1 map/unlocked migrate into the v3 numbering: old map 1 (Mine)
     * becomes 2, old unlocked 2 becomes 3. Left-behind maps count as fully
     * climbed; the top map (祖玛) re-climbs from floor 1. */
    assert(p.gold == 456 && p.kills == 17 && p.map == 2 && p.unlocked == 3);
    assert(p.floor_unlocked[0] == 2 && p.floor_unlocked[1] == 3
           && p.floor_unlocked[2] == 1 && p.floor == 3);
    /* Migration granted every book whose unlock level is reached. */
    assert(mafa_skill_known(&p, 0));
    assert(mafa_skill_known(&p, 1));    /* 雷电术 L3 */
    assert(mafa_skill_known(&p, 2));    /* 火墙 L8 */
    assert(mafa_skill_known(&p, 3));    /* 魔法盾 L10 */
    assert(!mafa_skill_known(&p, 4));   /* 冰咆哮 L13: still needs the book */
    uint16_t expected = (uint16_t)((1u << 6) | (1u << 7) | (1u << 8));
    assert(p.books == expected);
    /* The v1 flags byte had 自动卖白 on (0x30 = potion + sell). */
    assert(p.auto_sell == 0x01);
    assert(p.pot_hp_pct == MAFA_POT_HP_PCT_DEFAULT && p.pot_mp_pct == 30);
}

static void test_v2_save_map_migration(void) {
    /* v2 saves (pre v1.0) use the old ids: 0-2 combat + town 3, unlocked
     * 0-2. Serialize with those raw values, rewind the header to v2 and
     * re-sign; loading must shift them into the v3 numbering. */
    uint8_t buf[64];

    for (int c = 0; c < 2; ++c) {
        mafa_player_t p;
        mafa_player_init(&p, MAFA_CLS_WARRIOR, 5 + c);
        if (c == 0) {
            p.unlocked = 1;             /* old: Mine open */
            p.map = 2;                  /* old: idling at the Temple */
        } else {
            p.unlocked = 0;             /* old: only Beech open */
            p.map = 3;                  /* old: resting in town */
        }
        assert(mafa_save_serialize(&p, buf, sizeof buf));
        buf[3] = 2;                     /* rewind the header to v2 */
        if (c == 0) buf[4 + 18] |= 0x20;        /* old 自动卖白 flag on */
        buf[4 + MAFA_SAVE_BODY_V2] = crc8(buf + 4, MAFA_SAVE_BODY_V2);

        mafa_player_t q;
        mafa_player_init(&q, MAFA_CLS_MAGE, 6);
        assert(mafa_save_deserialize(&q, buf, 4 + MAFA_SAVE_BODY_V2 + 1));
        assert(q.auto_sell == (c == 0 ? 0x01 : 0x00));
        if (c == 0) {
            assert(q.map == 3 && q.unlocked == 2);
            /* 废矿 left behind = fully climbed; 祖玛 (now the top map)
             * re-climbs from floor 1. */
            assert(q.floor_unlocked[0] == 2 && q.floor_unlocked[1] == 1);
            assert(q.floor == 1);
        } else {
            assert(q.map == MAFA_MAP_SAFE && q.unlocked == 1);
            assert(q.floor == 0);
        }
    }
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
    /* 半月弯刀 (idx 3) switched off: zero casts across the multi-mob
     * battles the gear gate would normally light up in. */
    int casts = 0;
    for (int round = 0; round < 2; ++round) {
        for (int i = 0; i < 60; ++i) {
            mafa_player_t p;
            mafa_player_init(&p, MAFA_CLS_WARRIOR, (uint32_t)(31337 + i));
            p.level = 12;
            grant_books(&p, 4);
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
        p.level = 12;                   /* survive map 3 long enough to loot */
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
                    /* Map-3 elites can still roll gold tier: gold stores,
                     * everything else in the mask sells. */
                    if (MAFA_ITEMS[ev.e[k].id].quality == MAFA_Q_GOLD)
                        assert(ev.e[k].a == 1);
                    else {
                        assert(ev.e[k].a == 2);     /* sold, not stored */
                        sold++;
                    }
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
        p.level = 12;
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

    /* Gold never auto-sells, even with the full mask: map-3 boss tier-3
     * rolls are gold quality and must reach the backpack (a==1). */
    int gold_drops = 0;
    for (int i = 0; i < 300 && gold_drops == 0; ++i) {
        mafa_player_t p;
        mafa_player_init(&p, MAFA_CLS_WARRIOR, (uint32_t)(5150 + i));
        p.level = 15;
        grant_books(&p, 4);
        p.auto_sell = 0x0F;
        p.unlocked = 3;
        p.map = 3;
        p.floor = MAFA_MAP_FLOORS[3];   /* the 教主's own floor */
        p.pot_red = 30;
        p.pot_blue = 30;
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

static void test_battle_terminates_over_many_maps(void) {
    for (int map = 1; map < MAFA_MAP_COUNT; ++map) {
        for (int floor = 1; floor <= MAFA_MAP_FLOORS[map]; ++floor) {
            for (int cls = 0; cls < MAFA_CLS_COUNT; ++cls) {
                mafa_player_t p;
                mafa_player_init(&p, (uint8_t)cls,
                                 1000 + map * 7 + floor * 3 + cls);
                p.unlocked = 3;
                p.map = (uint8_t)map;
                p.floor = (uint8_t)floor;
                p.level = 15;           /* strongest case must still terminate */
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
    assert(mafa_map_boss(1, 1) == &MAFA_MONSTERS[6]);   /* 森林雪人 */
    assert(mafa_map_boss(1, 2) == &MAFA_MONSTERS[7]);   /* 森林巨猿 */
    assert(mafa_map_boss(1, 3) == NULL);    /* no such floor */
}

/* The whole v1.3 ladder: every (map, floor) boss opens the next step, and
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
            /* Force the boss fight and win it (retry: a L15 full-kit
             * warrior only rarely loses, but never let the test flake). */
            p.level = 15;
            grant_books(&p, MAFA_SKILLS_PER_CLASS - 1);
            bool won = false;
            for (int attempt = 0; attempt < 20 && !won; ++attempt) {
                p.map = (uint8_t)map;           /* a death sent us to town */
                p.floor = (uint8_t)floor;
                p.hp = 32000;
                p.pot_red = 30;
                p.pot_blue = 30;
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
    assert(total_bosses == 12);         /* 2 + 3 + 7 checkpoints */
}

int main(void) {
    test_stats_and_growth();
    test_xp_curve_front_fast_back_wall();
    test_book_gating_and_store();
    test_battle_kills_and_settlement();
    test_multi_mob_pack_weights();
    test_boss_event_flow_and_book_guarantee();
    test_taoist_pet_tanks();
    test_warrior_charge_and_proc();
    test_mage_shield_reduces_damage();
    test_switch_map_requires_unlock();
    test_inventory_equip_and_compare();
    test_full_backpack_drop_prompt();
    test_potions_and_store();
    test_auto_potion_thresholds();
    test_skill_toggle_respected();
    test_auto_sell_quality_mask();
    test_death_penalty_drops_and_gold();
    test_safe_zone_no_combat_and_open_door();
    test_init_defaults_v4();
    test_save_roundtrip_v5();
    test_v3_save_migration_to_v4();
    test_v1_save_migration();
    test_v2_save_map_migration();
    test_floor_ladder_walk();
    test_battle_terminates_over_many_maps();
    printf("test_mafa_model: all assertions passed\n");
    return 0;
}
