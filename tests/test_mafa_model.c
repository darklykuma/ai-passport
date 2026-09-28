// tests/test_mafa_model.c — host tests for the MAFA CHRONICLE model
// (PRD_MAFA_CHRONICLE chapters 8-9). Deterministic seeds; no floats.
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

static void test_xp_curve_and_levelup(void) {
    assert(mafa_xp_to_next(1) == 20);
    assert(mafa_xp_to_next(2) == 45);
    assert(mafa_xp_to_next(10) == 20 + 9 * 25);
    assert(mafa_xp_to_next(MAFA_MAX_LEVEL) == 0);
}

static void test_battle_kills_and_settlement(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 99);
    mafa_battle_t b;
    assert(mafa_battle_start(&p, &b));
    assert(!b.is_boss);
    mafa_events_t ev;
    int rounds = 0;
    while (!b.over && rounds < 1000) {
        mafa_battle_round(&p, &b, &ev);
        rounds++;
        for (int i = 0; i < ev.n; ++i) {
            if (ev.e[i].kind == MAFA_EV_MOB_KILLED) {
                assert(ev.e[i].a > 0);          /* xp granted */
                assert(p.kills == 1);           /* kill counter advanced */
                return;
            }
            assert(ev.e[i].kind != MAFA_EV_PLAYER_DEATH);  /* must survive */
        }
    }
    assert(0 && "battle never settled within 1000 rounds");
}

static void test_boss_event_flow(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_WARRIOR, 5);
    assert(!mafa_boss_ready(&p));
    mafa_battle_t b;
    assert(!mafa_boss_start(&p, &b));   /* not ready */
    p.kills = MAFA_KILLS_PER_BOSS;
    assert(mafa_boss_ready(&p));
    assert(mafa_boss_start(&p, &b));
    assert(b.is_boss);

    /* Fighting the map-1 boss and winning must unlock map 2 (PRD 8.7). */
    int guard = 0;
    while (!b.over && guard < 10000) { mafa_battle_round(&p, &b, &(mafa_events_t){0}); guard++; }
    assert(b.over && guard < 10000);
    if (!b.player_dead) {
        assert(p.unlocked >= 1);
        assert(p.kills == 0);
    } else {
        assert(p.kills == 0);           /* death also resets the counter */
    }
    mafa_boss_pass(&p);
    assert(p.kills == 0);
}

static void test_switch_map_requires_unlock(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_MAGE, 11);
    mafa_switch_map(&p, 1);             /* locked: ignored */
    assert(p.map == 0);
    p.unlocked = 2;
    mafa_switch_map(&p, 2);
    assert(p.map == 2);
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
    p.auto_sell_white = false;          /* force the prompt path */
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
    p.gold = 45;
    assert(mafa_buy_potion(&p, true));      /* 20 */
    assert(mafa_buy_potion(&p, false));     /* 25 */
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

static void test_save_roundtrip(void) {
    mafa_player_t p;
    mafa_player_init(&p, MAFA_CLS_TAOIST, 77);
    p.level = 9;
    p.xp = 123;
    p.gold = 456;
    p.pot_red = 3;
    p.pot_blue = 4;
    p.unlocked = 1;
    p.map = 1;
    p.kills = 17;
    p.auto_potion = false;
    p.auto_boss = true;
    assert(mafa_inv_add(&p, 11));
    assert(mafa_inv_add(&p, 3));
    assert(mafa_equip(&p, 1));          /* wears 骷髅甲 */
    p.hp = 111;
    p.mp = 22;
    p.rng = 0xDEADBEEF;                 /* live stream; deliberately not saved */

    uint8_t buf[64];
    size_t n = mafa_save_serialize(&p, buf, sizeof buf);
    assert(n > 0 && n <= sizeof buf);

    mafa_player_t q;
    mafa_player_init(&q, MAFA_CLS_WARRIOR, 1);
    assert(mafa_save_deserialize(&q, buf, n));
    assert(q.cls == p.cls && q.level == p.level && q.xp == p.xp);
    assert(q.gold == p.gold && q.pot_red == 3 && q.pot_blue == 4);
    assert(q.unlocked == 1 && q.map == 1 && q.kills == 17);
    assert(q.auto_potion == false && q.auto_sell_white == true);
    assert(q.auto_boss == true);
    assert(q.equipped[MAFA_SLOT_ARMOR] == 3);
    assert(q.hp == 111 && q.mp == 22);
    assert(q.rng > 0);                  /* the live stream is kept, not saved */

    buf[5] ^= 0xFF;                     /* corrupt the payload */
    assert(!mafa_save_deserialize(&q, buf, n));
    /* Deserialization failure must not disturb the live player. */
    assert(q.cls == MAFA_CLS_TAOIST && q.gold == 456);
}

static void test_battle_terminates_over_many_maps(void) {
    for (int map = 0; map < MAFA_MAP_COUNT; ++map) {
        for (int cls = 0; cls < MAFA_CLS_COUNT; ++cls) {
            mafa_player_t p;
            mafa_player_init(&p, (uint8_t)cls, 1000 + map * 7 + cls);
            p.unlocked = 2;
            p.map = (uint8_t)map;
            p.level = 15;               /* strongest case must still terminate */
            mafa_battle_t b;
            assert(mafa_battle_start(&p, &b));
            int rounds = 0;
            while (!b.over) {
                mafa_battle_round(&p, &b, &(mafa_events_t){0});
                assert(++rounds < 100000);
                assert(p.hp >= 0);
                if (p.hp == 0) break;   /* death is a valid ending */
            }
        }
    }
}

int main(void) {
    test_stats_and_growth();
    test_xp_curve_and_levelup();
    test_battle_kills_and_settlement();
    test_boss_event_flow();
    test_switch_map_requires_unlock();
    test_inventory_equip_and_compare();
    test_full_backpack_drop_prompt();
    test_potions_and_store();
    test_save_roundtrip();
    test_battle_terminates_over_many_maps();
    printf("test_mafa_model: all assertions passed\n");
    return 0;
}
