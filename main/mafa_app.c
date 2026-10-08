// main/mafa_app.c — MAFA CHRONICLE application: pages, input, idle pacing
// (PRD_MAFA_CHRONICLE 6/7/10). The input task owns the LVGL lock; button
// callbacks only enqueue (bsp_button.h). Auto-battle ticks run on the same
// task's queue timeout, one model round per tick, with app-side diff guards
// so unchanged labels never reach LVGL (the FOG MARCH pool lesson).
#include "mafa_app.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "mafa_model.h"
#include "mafa_view.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "mafa";

#define INPUT_QUEUE_DEPTH 8
#define LOG_LINES 6
#define LOG_LINE_CAP 56
#define NVS_NS "mafa"
#define NVS_KEY "save"

typedef enum {
    PAGE_MENU = 0,
    PAGE_CLASS,
    PAGE_MAIN,
    PAGE_STATUS,
    PAGE_SKILLS,
    PAGE_BACKPACK,
    PAGE_STORE,
    PAGE_MAPS,
    PAGE_SETTINGS,
} page_t;

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

/* Quality display (PRD 9.3): recolor codes for the log label. The LVGL
 * recolor parser swallows everything between # and the next space, so each
 * format site must keep a space between the code and the text it colors. */
static const char *Q_COLOR[MAFA_Q_COUNT] = {
    "#C8C8C8", "#5FC85F", "#4FA8F2", "#B06CF0", "#F0C04A",
};

/* Auto-sell picker rows (design 15): one quality name per row, drawn in its
 * own quality color. Each constant carries the parser-consumed space; the
 * closing # lives at the format site. */
static const char *SELL_PICK_COLOR[4] = {
    "#C8C8C8 ", "#5FC85F ", "#4FA8F2 ", "#B06CF0 ",
};
static const char *SELL_PICK_NAME[4] = {"白", "绿", "蓝", "紫"};

static const uint32_t PACE_MS[3] = {1500, 750, 375};   /* 1x/2x/4x (8.3) */
static const char *SPEED_NAME[3] = {"1x", "2x", "4x"};
/* 1.76 plan: the 技能 cell joins the bar (design 03); the 8th grid slot
 * stays blank — reserved for a future pet/codex page. */
static const char *MAIN_MENU[7] = {
    "背包", "装备", "技能", "商店", "地图", "设置", "加速",
};

static struct {
    mafa_player_t player;
    mafa_battle_t battle;
    bool in_battle;

    page_t page;
    int cur_menu;           // menu page cursor
    bool confirm_new;       // menu: overwrite-save confirmation shown
    int cur_class;
    int cur_main;           // action menu cursor (0..6)
    int cur_status;         /* gear page cursor: 0-7 equip slots */
    int cur_skill;          /* skill page cursor: 0-6 class skills */
    char skill_hint[48];    /* skill page: why OK had nothing to toggle;
                               empty shows the help lines, any navigation
                               or page entry restores them */
    int set_edit;           /* settings edit mode: 0 none, 1 potion line,
                               2 auto-sell picker */
    int cur_pick;           /* auto-sell picker cursor: 0-3 colors, 4 done */
    int cur_pack;
    bool packsub;           // backpack action submenu open
    int cur_packsub;        // 0 equip, 1 sell
    int cur_store;
    /* Store hold-to-buy (v1.6 UX): OK 长按 on a potion row keeps buying
     * until the key is released (BSP_BTN_RELEASE), gold runs out, or any
     * other event arrives. One NVS write per hold, not per potion. */
    struct {
        bool active;
        bool red;
        uint8_t bought;
    } buy_hold;
    int cur_maps;
    int floor_mode;         /* maps page: 0 map list, else map id → floor list */
    int cur_floor;          /* floor-list cursor, 0-based; last row = 返回 */
    int cur_set;
    int cur_modal;          // boss / drop prompt cursor
    bool boss_pending;
    uint8_t speed;
    bool battery_ok;
    int battery;
    bool has_save;

    char log[LOG_LINES][LOG_LINE_CAP];

    /* Diff shadows, wiped on page entry; bar caches init to -1 there. */
    char prev_map[64];
    char prev_info[48];
    char prev_hptext[16];
    char prev_mptext[16];
    char prev_enemy[64];
    char prev_log[LOG_LINES * LOG_LINE_CAP + LOG_LINES];
    char prev_menu[176];
    char prev_modal[224];
    int disp_hp, disp_hmax, disp_mp, disp_mmax, disp_ehp, disp_emax;
    bool disp_ebar;         /* enemy-bar visibility cache (hidden while idle) */

    mafa_view_t view;
    QueueHandle_t queue;
    TaskHandle_t task;
    volatile bool ready;
} s_app;

static void enter_page(page_t page);
static void tick_battle(void);
static bool save_now(void);
static void log_line(const char *fmt, ...);
static void refresh_store(void);

// --- Store hold-to-buy -------------------------------------------------------

#define MAFA_BUY_HOLD_MS 150    /* one potion per tick while OK stays down */

static void buy_hold_start(bool red) {
    s_app.buy_hold.active = mafa_buy_potion(&s_app.player, red);
    s_app.buy_hold.red = red;
    s_app.buy_hold.bought = s_app.buy_hold.active ? 1u : 0u;
}

static void buy_hold_stop(void) {
    if (!s_app.buy_hold.active) return;
    s_app.buy_hold.active = false;
    if (s_app.buy_hold.bought > 0) {
        log_line("#9AA3A8 连买%s药 x%u#",
                 s_app.buy_hold.red ? "红" : "蓝",
                 (unsigned)s_app.buy_hold.bought);
        save_now();
    }
    s_app.buy_hold.bought = 0;
}

static void buy_hold_tick(void) {
    if (!s_app.buy_hold.active) return;
    if (s_app.page != PAGE_STORE
        || !mafa_buy_potion(&s_app.player, s_app.buy_hold.red)) {
        buy_hold_stop();               /* gold out: stop mid-hold */
        if (s_app.page == PAGE_STORE) refresh_store();
        return;
    }
    s_app.buy_hold.bought++;
    refresh_store();
}

/* A refused potion buy is either the stack cap or gold — say which. */
static void log_buy_fail(bool red) {
    log_line(mafa_potion_full(&s_app.player, red)
                 ? "#E05A48 药水已满#"
                 : "#E05A48 金币不足#");
}

// --- Compact counters ---------------------------------------------------------

/* 999 raw, 1234 → "1.2k", 65000 → "65k", 12000000 → "12M": gold grows to
 * a 8-digit cap (v1.6) and rows must stay narrow; potion stacks (cap 255)
 * never leave the raw tier but share the formatter for symmetry. */
static void fmt_count(char *out, size_t cap, uint32_t v) {
    if (v < 10000) {
        snprintf(out, cap, "%u", (unsigned)v);
    } else if (v < 1000000) {
        uint32_t whole = v / 1000, tenths = (v % 1000) / 100;
        if (tenths)
            snprintf(out, cap, "%u.%uk", (unsigned)whole, (unsigned)tenths);
        else
            snprintf(out, cap, "%uk", (unsigned)whole);
    } else {
        uint32_t whole = v / 1000000, tenths = (v % 1000000) / 100000;
        if (tenths)
            snprintf(out, cap, "%u.%uM", (unsigned)whole, (unsigned)tenths);
        else
            snprintf(out, cap, "%uM", (unsigned)whole);
    }
}

// --- Log ring ----------------------------------------------------------------

static void log_line(const char *fmt, ...) {
    memmove(s_app.log, s_app.log + 1, (LOG_LINES - 1) * LOG_LINE_CAP);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_app.log[LOG_LINES - 1], LOG_LINE_CAP, fmt, ap);
    va_end(ap);
}

static void log_clear(const char *fmt, ...) {
    for (int i = 0; i < LOG_LINES; ++i) s_app.log[i][0] = '\0';
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_app.log[LOG_LINES - 1], LOG_LINE_CAP, fmt, ap);
    va_end(ap);
}

// --- Save (PRD 8.10): NVS blob, autosaved at every state change --------------

static bool save_now(void) {
    if (!s_app.ready) return false;
    uint8_t buf[96];
    size_t n = mafa_save_serialize(&s_app.player, buf, sizeof buf);
    if (n == 0) return false;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_blob(h, NVS_KEY, buf, n);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGW(TAG, "save failed: %s", esp_err_to_name(e));
    return e == ESP_OK;
}

static bool load_save(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t buf[96];
    size_t n = sizeof buf;
    esp_err_t e = nvs_get_blob(h, NVS_KEY, buf, &n);
    nvs_close(h);
    if (e != ESP_OK) return false;
    mafa_player_t p = s_app.player;     /* zeroed template at boot */
    if (!mafa_save_deserialize(&p, buf, n)) return false;
    s_app.player = p;
    return true;
}

// --- Helpers -------------------------------------------------------------------

static int battery_read(void) {
    static TickType_t last;
    if (!s_app.battery_ok) return -1;
    TickType_t now = xTaskGetTickCount();
    if (s_app.battery < 0 || now - last >= pdMS_TO_TICKS(5000)) {
        last = now;
        s_app.battery = bsp_battery_soc();
    }
    return s_app.battery;
}

/* Guarded set: only push the text into LVGL when it actually changed.
 * The shadow buffers are wiped on page entry, so the first refresh of a
 * page always lands. */
static void label_set(lv_obj_t *label, const char *text, char *prev, size_t cap) {
    if (strncmp(prev, text, cap) == 0) return;
    strncpy(prev, text, cap - 1);
    prev[cap - 1] = '\0';
    lv_label_set_text(label, text);
}

/* Guarded bar update: range first (levels and gear move the max; an empty
 * MP pool gets a 0..1 range so LVGL never divides by zero), then the value,
 * each only when they actually changed. */
static void bar_set(lv_obj_t *bar, long v, long vmax, int *prev_v,
                    int *prev_max) {
    if (vmax < 1) vmax = 1;
    if (v > vmax) v = vmax;
    if (*prev_max != vmax) {
        lv_bar_set_range(bar, 0, (int)vmax);
        *prev_max = (int)vmax;
    }
    if (*prev_v != v) {
        lv_bar_set_value(bar, v, LV_ANIM_OFF);
        *prev_v = (int)v;
    }
}

// --- Composers ------------------------------------------------------------------

static void compose_log(char *buf, size_t cap) {
    buf[0] = '\0';
    for (int i = 0; i < LOG_LINES; ++i) {
        if (s_app.log[i][0] == '\0') continue;
        if (buf[0]) strncat(buf, "\n", cap - strlen(buf) - 1);
        strncat(buf, s_app.log[i], cap - strlen(buf) - 1);
    }
}

/* 2×4 grid at font 16 (1.76 plan, design 03): the 技能 cell makes seven,
 * the 8th slot stays blank. Four font-20 columns cannot fit 240px, so the
 * bar shares the list-page size; every prefix stays one full-width glyph
 * (＞ or 　) and rows pad with two ASCII spaces, keeping columns aligned
 * wherever the cursor rests. */
static void compose_main_menu(char *buf, size_t cap) {
    buf[0] = '\0';
    for (int i = 0; i < 7; ++i) {
        const char *tone = s_app.cur_main == i ? "#F0C04A " : "#9AA3A8 ";
        const char *mark = s_app.cur_main == i ? "＞" : "　";
        char cell[32];
        if (i == 6)
            snprintf(cell, sizeof cell, "%s%s加速%s#", tone, mark,
                     SPEED_NAME[s_app.speed]);
        else
            snprintf(cell, sizeof cell, "%s%s%s#", tone, mark, MAIN_MENU[i]);
        strncat(buf, cell, cap - strlen(buf) - 1);
        if (i != 3 && i != 6) strncat(buf, "  ", cap - strlen(buf) - 1);
        if (i == 3) strncat(buf, "\n", cap - strlen(buf) - 1);
    }
}

// --- Events → log (PRD 8.3 copy, quality colors for loot) -----------------------

static const mafa_skill_t *skill_of(uint8_t idx) {
    return &MAFA_SKILLS[s_app.player.cls][idx];
}

/* Name of the mob an event refers to (b = mob index). */
static const char *ev_mob_name(const mafa_events_t *ev, int i) {
    uint8_t m = (uint8_t)ev->e[i].b;
    if (m >= s_app.battle.mob_n) m = 0;
    return s_app.battle.mob[m].base->name;
}

static const char *pet_name(void) {
    return s_app.battle.pet_tier == 2 ? "神兽" : "骷髅";
}

static void handle_events(const mafa_events_t *ev) {
    bool settled = false;
    for (int i = 0; i < ev->n; ++i) {
        uint8_t kind = ev->e[i].kind;
        uint8_t id = ev->e[i].id;
        int32_t a = ev->e[i].a;
        switch (kind) {
        case MAFA_EV_PLAYER_HIT:
            log_line("你造成 %d 点伤害", a);
            break;
        case MAFA_EV_PLAYER_CRIT:
            log_line("【爆击】你造成 %d 伤害!", a);
            break;
        case MAFA_EV_SKILL_HIT:
            log_line("【%s】造成 %d 伤害!", skill_of(id)->name, a);
            break;
        case MAFA_EV_SKILL_SUPPORT:
            if (id & 0x80)
                log_line("#F0C04A 习得【%s】!#",
                         skill_of(id & 0x7F)->name);
            else
                log_line("【%s】发动!", skill_of(id)->name);
            break;
        case MAFA_EV_DOT_TICK:
            log_line("#E05A48 %s 受 %d 持续伤害#",
                     ev_mob_name(ev, i), a);
            break;
        case MAFA_EV_MOB_HIT:
            log_line("#E05A48 %s 反击,你受 %d 伤害#",
                     ev_mob_name(ev, i), a);
            break;
        case MAFA_EV_MOB_SKILL:
            log_line("#E05A48 %s【%s】你受 %d 伤害!#",
                     ev_mob_name(ev, i),
                     id == MAFA_MSK_FIRE ? "火攻"
                     : id == MAFA_MSK_FLURRY ? "连击"
                     : id == MAFA_MSK_HEAVY ? "重击"
                     : id == MAFA_MSK_STING ? "毒刺"
                     : id == MAFA_MSK_ROAR ? "咆哮"
                     : id == MAFA_MSK_HELLFIRE ? "地狱火" : "技能",
                     a);
            break;
        case MAFA_EV_MOB_STUNNED:
            log_line("#F0C04A %s 被撞晕!#", ev_mob_name(ev, i));
            break;
        case MAFA_EV_PLAYER_POISON:
            log_line("#E05A48 中毒,失去 %d HP#", a);
            break;
        case MAFA_EV_HEAL:
            log_line("#5FC85F %s +%d#",
                     id == 1 ? "红药" : id == 2 ? "蓝药" : "治愈", a);
            break;
        case MAFA_EV_PET_HIT:
            log_line("#9AA3A8 %s 攻击%s,造成 %d 伤害#",
                     pet_name(), ev_mob_name(ev, i), a);
            break;
        case MAFA_EV_PET_GUARD:
            log_line("#9AA3A8 %s 替你挡下 %d 伤害#",
                     pet_name(), a);
            break;
        case MAFA_EV_PET_SUMMON:
            log_line("#5FC85F %s 出现!#", id == 2 ? "神兽" : "骷髅");
            break;
        case MAFA_EV_PET_DOWN:
            log_line("#E05A48 %s 倒下了!#", pet_name());
            break;
        case MAFA_EV_MOB_KILLED:
            log_line("#F0C04A %s 倒下!经验+%d#",
                     ev_mob_name(ev, i), a);
            settled = true;
            break;
        case MAFA_EV_LEVELUP:
            log_line("#F0C04A 升级!Lv.%d#", id);
            break;
        case MAFA_EV_FLOOR:
            log_line("#F0C04A 已通往 第%d层!#", id);
            settled = true;
            break;
        case MAFA_EV_MAP_UNLOCK:
            log_line("#F0C04A 解锁【%s】!#", MAFA_MAP_NAMES[id]);
            settled = true;
            break;
        case MAFA_EV_DROP: {
            const mafa_item_t *it = &MAFA_ITEMS[id];
            if (a == 1)
                log_line("【掉落】%s %s%s!", Q_COLOR[it->quality], it->name, "#");
            else if (a == 2)
                log_line("售出 %s %s+%d 金#", Q_COLOR[it->quality],
                         it->name, (int)mafa_sell_price(id));
            else
                log_line("#E05A48 背包已满!#");
            settled = true;
            break;
        }
        case MAFA_EV_POTION:
            log_line("#5FC85F 获得 %s#", id == 1 ? "金创药" : "魔法药");
            settled = true;
            break;
        case MAFA_EV_BOOK:
            log_line("#F0C04A 【技能书】习得【%s】!#", skill_of(id)->name);
            settled = true;
            break;
        case MAFA_EV_DEATH_DROP:
            log_line("#E05A48 失去 %s#", MAFA_ITEMS[id].name);
            break;
        case MAFA_EV_GOLD_LOST: {
            char gold[12];
            fmt_count(gold, sizeof gold, (uint32_t)a);
            log_line("#E05A48 损失 %s 金#", gold);
            break;
        }
        case MAFA_EV_PLAYER_DEATH:
            log_line("#E05A48 你被 %s 杀死了…#", ev_mob_name(ev, i));
            log_line("#9AA3A8 满血回到安全区#");
            settled = true;
            break;
        default:
            break;
        }
    }
    if (settled) save_now();
}

// --- Modals -----------------------------------------------------------------------

static void modal_show(const char *buf) {
    if (!s_app.view.modal)
        mafa_view_modal_open(&s_app.view, buf);
    else if (strncmp(s_app.prev_modal, buf, sizeof s_app.prev_modal) != 0)
        mafa_view_modal_text(&s_app.view, buf);
    strncpy(s_app.prev_modal, buf, sizeof s_app.prev_modal - 1);
    s_app.prev_modal[sizeof s_app.prev_modal - 1] = '\0';
}

static void modal_close(void) {
    mafa_view_modal_close(&s_app.view);
    s_app.prev_modal[0] = '\0';
}

static const char *map_boss_name(uint8_t map, uint8_t floor) {
    const mafa_monster_t *boss = mafa_map_boss(map, floor);
    return boss ? boss->name : "?";
}

static void boss_modal_refresh(void) {
    /* Design 06: gold title, gold highlighted action, plain other row. */
    char buf[160];
    snprintf(buf, sizeof buf, "#F0C04A 【Boss】%s 出现了!#\n%s\n%s",
             map_boss_name(s_app.player.map, s_app.player.floor),
        s_app.cur_modal == 0 ? "#F0C04A ＞迎战#" : "　迎战",
        s_app.cur_modal == 1 ? "#F0C04A ＞回避#" : "　回避");
    modal_show(buf);
}

static void drop_modal_refresh(void) {
    char buf[224];
    int n = snprintf(buf, sizeof buf, "背包已满!获得 %s\n",
                     MAFA_ITEMS[s_app.player.pending_drop].name);
    for (int i = 0; i < MAFA_BACKPACK && n > 0 && n < (int)sizeof buf; ++i) {
        uint8_t id = s_app.player.inv_id[i];
        n += snprintf(buf + n, sizeof buf - n, "%s%d.%s\n",
                      s_app.cur_modal == i ? "＞" : "　", i + 1,
                      id == MAFA_INV_EMPTY ? "空" : MAFA_ITEMS[id].name);
    }
    if (n > 0 && n < (int)sizeof buf)
        snprintf(buf + n, sizeof buf - n, "%s丢弃",
                 s_app.cur_modal == MAFA_BACKPACK ? "＞" : "　");
    modal_show(buf);
}

// --- Tick (idle auto-battle, PRD 8.3) ----------------------------------------------

/* Main page: five guarded refresh units — header (map + level/XP%), the two
 * player bars, the enemy strip, the log, the action menu. Layout contract
 * (240px wide, real font advances): row 1 is map name (≤90px from x=10)
 * plus ONE right-aligned info line; kill progress rides the idle enemy
 * strip, where the enemy bar is hidden and cannot collide with it. */
static void refresh_main(void) {
    char buf[160];
    if (s_app.player.map == MAFA_MAP_SAFE)
        snprintf(buf, sizeof buf, "#F0C04A %s#",
                 MAFA_MAP_NAMES[s_app.player.map]);
    else
        snprintf(buf, sizeof buf, "#F0C04A %s %d层#",
                 MAFA_MAP_NAMES[s_app.player.map], s_app.player.floor);
    label_set(s_app.view.map_label, buf, s_app.prev_map, sizeof s_app.prev_map);
    /* Header percent = XP progress toward the next level (design 03: "Lv.8
     * 87%"); the battery readout lives on the settings page instead. At the
     * cap there is no next level, so the line shrinks to bare "Lv.15". */
    uint32_t xp_next = mafa_xp_to_next(s_app.player.level);
    char gold[12];
    fmt_count(gold, sizeof gold, s_app.player.gold);
    if (xp_next > 0)
        snprintf(buf, sizeof buf, "#9AA3A8 Lv.%d %d%%#\n#F0C04A 金%s#",
                 s_app.player.level, (int)(s_app.player.xp * 100 / xp_next),
                 gold);
    else
        snprintf(buf, sizeof buf, "#9AA3A8 Lv.%d#\n#F0C04A 金%s#",
                 s_app.player.level, gold);
    label_set(s_app.view.info_label, buf, s_app.prev_info,
              sizeof s_app.prev_info);

    mafa_stats_t st;
    mafa_stats(&s_app.player, &st);
    bar_set(s_app.view.hp_bar, s_app.player.hp, st.max_hp, &s_app.disp_hp,
            &s_app.disp_hmax);
    snprintf(buf, sizeof buf, "%d", s_app.player.hp);
    label_set(s_app.view.hp_text, buf, s_app.prev_hptext,
              sizeof s_app.prev_hptext);
    bar_set(s_app.view.mp_bar, s_app.player.mp, st.max_mp, &s_app.disp_mp,
            &s_app.disp_mmax);
    snprintf(buf, sizeof buf, "%d", s_app.player.mp);
    label_set(s_app.view.mp_text, buf, s_app.prev_mptext,
              sizeof s_app.prev_mptext);

    bool want_bar = s_app.in_battle;
    if (want_bar != s_app.disp_ebar) {
        if (want_bar)
            lv_obj_clear_flag(s_app.view.enemy_bar, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_app.view.enemy_bar, LV_OBJ_FLAG_HIDDEN);
        s_app.disp_ebar = want_bar;
    }
    if (s_app.in_battle) {
        /* First-alive mob name × living count; the bar shows the pack's
         * pooled HP. The pet is followed through its log lines (summon /
         * guard / down) — the strip row is too narrow for a suffix. */
        uint8_t first = MAFA_MOBS_MAX;
        int32_t hp = 0, hpmax = 0;
        for (int i = 0; i < s_app.battle.mob_n; ++i) {
            if (!s_app.battle.mob[i].alive) continue;
            if (first == MAFA_MOBS_MAX) first = (uint8_t)i;
            hp += s_app.battle.mob[i].hp;
            hpmax += s_app.battle.mob[i].max_hp;
        }
        const char *mob_name = first < s_app.battle.mob_n
            ? s_app.battle.mob[first].base->name : "?";
        if (s_app.battle.is_boss)
            snprintf(buf, sizeof buf, "#F0C04A ▶%s!#", mob_name);
        else if (s_app.battle.alive_n > 1)
            snprintf(buf, sizeof buf, "▶%s×%d", mob_name,
                     s_app.battle.alive_n);
        else
            snprintf(buf, sizeof buf, "▶%s", mob_name);
        if (hpmax < 1) hpmax = 1;
        if (hp > hpmax) hp = hpmax;
        bar_set(s_app.view.enemy_bar, hp, hpmax, &s_app.disp_ehp,
                &s_app.disp_emax);
    } else if (s_app.player.map == MAFA_MAP_SAFE) {
        snprintf(buf, sizeof buf, "#9AA3A8 休息中,请选地图#");
    } else {
        snprintf(buf, sizeof buf, "#9AA3A8 挂机中 %d/%d#",
                 (int)s_app.player.kills, MAFA_KILLS_PER_BOSS);
    }
    label_set(s_app.view.enemy_label, buf, s_app.prev_enemy,
              sizeof s_app.prev_enemy);

    char logbuf[sizeof s_app.prev_log];
    compose_log(logbuf, sizeof logbuf);
    label_set(s_app.view.log_label, logbuf, s_app.prev_log, sizeof s_app.prev_log);
    compose_main_menu(buf, sizeof buf);
    label_set(s_app.view.menu_label, buf, s_app.prev_menu, sizeof s_app.prev_menu);
}

static void tick_battle(void) {
    if (s_app.boss_pending || s_app.player.pending_drop != MAFA_DROP_NONE)
        return;                          /* prompts pause the world (7) */
    if (!s_app.in_battle && mafa_boss_ready(&s_app.player)) {
        if (s_app.player.auto_boss) {    /* auto-answer boss events (10) */
            mafa_battle_t b;
            if (mafa_boss_start(&s_app.player, &b)) {
                s_app.battle = b;
                s_app.in_battle = true;
                log_line("#F0C04A 【Boss】%s!#", b.mob[0].base->name);
            }
        } else {
            s_app.boss_pending = true;
            s_app.cur_modal = 0;
            log_line("#F0C04A 【Boss】%s 出现了!#",
                     map_boss_name(s_app.player.map, s_app.player.floor));
        }
    }
    if (!s_app.in_battle) {
        if (!mafa_battle_start(&s_app.player, &s_app.battle)) return;
        s_app.in_battle = true;
        const mafa_monster_t *m = s_app.battle.mob[0].base;
        if (s_app.battle.mob[0].elite)
            log_line("【精英】%s 出现!", m->name);
        else if (s_app.battle.mob_n > 1)
            log_line("遭遇 %s 等 %d 只!", m->name, s_app.battle.mob_n);
        else
            log_line("遭遇 %s!", m->name);
    }
    mafa_events_t ev;
    mafa_battle_round(&s_app.player, &s_app.battle, &ev);
    handle_events(&ev);
    if (s_app.battle.over) {
        s_app.in_battle = false;
        if (!s_app.battle.player_dead) mafa_regen(&s_app.player, 3);
        if (!s_app.battle.is_boss && !s_app.player.auto_boss
            && mafa_boss_ready(&s_app.player)) {
            s_app.boss_pending = true;
            s_app.cur_modal = 0;
            log_line("#F0C04A 【Boss】%s 出现了!#",
                     map_boss_name(s_app.player.map, s_app.player.floor));
        }
    }
}

// --- Pages ---------------------------------------------------------------------------

static void refresh_menu(void) {
    char buf[96];
    if (s_app.confirm_new)
        snprintf(buf, sizeof buf, "覆盖现有存档?\n%s是\n%s否",
                 s_app.cur_menu == 0 ? "＞" : "　",
                 s_app.cur_menu == 1 ? "＞" : "　");
    else if (s_app.has_save)
        snprintf(buf, sizeof buf, "%s继续游戏\n%s新游戏",
                 s_app.cur_menu == 0 ? "＞" : "　",
                 s_app.cur_menu == 1 ? "＞" : "　");
    else
        snprintf(buf, sizeof buf, "%s新游戏", s_app.cur_menu == 0 ? "＞" : "　");
    lv_label_set_text(s_app.view.items_label, buf);
}

static void refresh_class(void) {
    static const char *ROWS[MAFA_CLS_COUNT] = {
        "战士  高血高防", "法师  高魔脆皮", "道士  道术续航",
    };
    static const char *BLURB[MAFA_CLS_COUNT] = {
        "攻杀/刺杀/半月/烈火", "雷电/火墙/盾/冰咆哮",
        "毒/火符/骷髅/神兽",
    };
    char buf[128];
    buf[0] = '\0';
    for (int i = 0; i < MAFA_CLS_COUNT; ++i) {
        char row[48];
        /* Design 02: the highlighted row is gold, the rest plain. */
        if (i == s_app.cur_class)
            snprintf(row, sizeof row, "#F0C04A ＞%s#", ROWS[i]);
        else
            snprintf(row, sizeof row, "　%s", ROWS[i]);
        strncat(buf, row, sizeof buf - strlen(buf) - 1);
        if (i < MAFA_CLS_COUNT - 1)
            strncat(buf, "\n", sizeof buf - strlen(buf) - 1);
    }
    lv_label_set_text(s_app.view.items_label, buf);
    char det[96];
    snprintf(det, sizeof det, "%s\nLv1 起步,技能书解锁进阶",
             BLURB[s_app.cur_class]);
    lv_label_set_text(s_app.view.detail_label, det);
}

/* Gear page (design 07, 1.76 plan): the 8-slot paper doll with rows wearing
 * each item's quality color, then stats + potions in the detail box. Skills
 * moved to their own page (技能) when the doll grew from 3 to 8 slots. */
static void refresh_status(void) {
    /* 左/右 distinguish the twin bracelet and ring positions: OK now
     * unequips the cursor row, so the names must say WHICH twin leaves. */
    static const char *SLOT_NAME[MAFA_EQ_SLOTS] = {
        "武器", "头盔", "衣服", "项链", "左手镯", "右手镯", "左戒指", "右戒指",
    };
    char buf[512];
    int n = 0;
    for (int i = 0; i < MAFA_EQ_SLOTS; ++i) {
        uint8_t id = s_app.player.equipped[i];
        const char *mark = s_app.cur_status == i ? "＞" : "　";
        if (id == MAFA_INV_EMPTY)
            n += snprintf(buf + n, sizeof buf - n, "#9AA3A8 %s%s 空#\n",
                          mark, SLOT_NAME[i]);
        else
            n += snprintf(buf + n, sizeof buf - n, "%s%s %s %s#\n", mark,
                          SLOT_NAME[i], Q_COLOR[MAFA_ITEMS[id].quality],
                          MAFA_ITEMS[id].name);
    }
    lv_label_set_text(s_app.view.items_label, buf);

    mafa_stats_t st;
    mafa_stats(&s_app.player, &st);
    char red[8], blue[8];
    fmt_count(red, sizeof red, s_app.player.pot_red);
    fmt_count(blue, sizeof blue, s_app.player.pot_blue);
    char det[128];
    /* Four detail lines (v1.6): the four combat stats, both pools, the
     * potion counters, and the OK-unequip hint the new gesture needs. */
    snprintf(det, sizeof det, "攻%d 魔%d 道%d 防%d\n血%d/%ld 蓝%ld/%ld"
             "\n红药x%s 蓝药x%s\nOK 卸下,长按返回",
             st.atk, st.mc, st.sc, st.def,
             s_app.player.hp, (long)st.max_hp,
             (long)s_app.player.mp, (long)st.max_mp,
             red, blue);
    lv_label_set_text(s_app.view.detail_label, det);
}

/* Skill page (design 17, 1.76 plan): the seven class skills with their
 * live state — 常驻 for passive/proc (fixed), 开/关 toggles (OK flips and
 * saves, the 群攻 tag rides the toggle), the store price, or the lock
 * reason with the level gate first and the source second (skill 0 needs
 * no book: it reads 自动习得). OK on a row with nothing to toggle swaps
 * the help lines for the reason until the cursor moves or the page is
 * re-entered — the page never reacts to OK with silence. */
static void refresh_skills(void) {
    char buf[512];
    int n = 0;
    for (int i = 0; i < MAFA_SKILLS_PER_CLASS; ++i) {
        const mafa_skill_t *sk = &MAFA_SKILLS[s_app.player.cls][i];
        const char *mark = s_app.cur_skill == i ? "＞" : "　";
        char state[40];
        size_t m = 0;
        state[0] = '\0';      /* toggleable non-AoE and passive rows print
                                 state as-is: never leak stale stack bytes */
        if (!mafa_skill_known(&s_app.player, (uint8_t)i)) {
            /* Lock rows carry no 群攻 tag: tag + gate + source measures
             * 236px at the worst against the 216px list panel and wraps
             * the row into the detail panel. The tag only means something
             * once the skill can actually cast, so it rides the toggle. */
            if (s_app.player.level < sk->unlock)
                m += (size_t)snprintf(state + m, sizeof state - m, "Lv%u ",
                                      (unsigned)sk->unlock);
            if (i == 0)
                m += (size_t)snprintf(state + m, sizeof state - m, "自动习得");
            else if (i <= 3)
                m += (size_t)snprintf(state + m, sizeof state - m, "书店%u金",
                                      (unsigned)mafa_book_price(s_app.player.cls,
                                                                (uint8_t)i));
            else
                m += (size_t)snprintf(state + m, sizeof state - m, "精英/Boss");
            n += snprintf(buf + n, sizeof buf - n, "#9AA3A8 %s%s  %s#\n",
                          mark, sk->name, state);
        } else if (sk->kind == MAFA_SK_PASSIVE || sk->kind == MAFA_SK_PROC) {
            n += snprintf(buf + n, sizeof buf - n, "#9AA3A8 %s%s  %s常驻#\n",
                          mark, sk->name, state);
        } else {
            if (sk->kind == MAFA_SK_AOE)
                m += (size_t)snprintf(state + m, sizeof state - m, "群攻 ");
            n += snprintf(buf + n, sizeof buf - n, "%s%s  %s%s\n",
                          mark, sk->name, state,
                          (s_app.player.skills_off >> i) & 1 ? "关" : "开");
        }
    }
    lv_label_set_text(s_app.view.items_label, buf);
    lv_label_set_text(s_app.view.detail_label,
                      s_app.skill_hint[0] ? s_app.skill_hint
                      : "OK 切换开关,立即存档\n"
                        "群攻:2只以上才施放\n"
                        "高阶书:精英/Boss掉落");
}

static void refresh_backpack(void) {
    /* Design 08: item rows wear their quality color, empty slots are dim;
     * the recolor span swallows the one space after the color code, so dim
     * rows keep two visible leading spaces. */
    char buf[384];
    buf[0] = '\0';
    for (int i = 0; i < MAFA_BACKPACK; ++i) {
        uint8_t id = s_app.player.inv_id[i];
        const char *mark = !s_app.packsub && s_app.cur_pack == i ? "＞" : "　";
        char row[64];
        if (id == MAFA_INV_EMPTY)
            snprintf(row, sizeof row, "#9AA3A8 %s%d.空#", mark, i + 1);
        else
            snprintf(row, sizeof row, "%s %s%d.%s x%d#",
                     Q_COLOR[MAFA_ITEMS[id].quality], mark, i + 1,
                     MAFA_ITEMS[id].name, s_app.player.inv_n[i]);
        strncat(buf, row, sizeof buf - strlen(buf) - 1);
        if (i < MAFA_BACKPACK - 1)
            strncat(buf, "\n", sizeof buf - strlen(buf) - 1);
    }
    lv_label_set_text(s_app.view.items_label, buf);

    char det[80];
    char gold[12];
    fmt_count(gold, sizeof gold, s_app.player.gold);
    uint8_t id = s_app.player.inv_id[s_app.cur_pack];
    if (s_app.packsub) {
        snprintf(det, sizeof det, "%s装备\n%s卖出",
                 s_app.cur_packsub == 0 ? "＞" : "　",
                 s_app.cur_packsub == 1 ? "＞" : "　");
    } else if (id == MAFA_INV_EMPTY) {
        snprintf(det, sizeof det, "金币 %s", gold);
    } else {
        mafa_compare_t cmp;
        mafa_compare(&s_app.player, id, &cmp);
        /* v1.5: five stat deltas over three rows. */
        snprintf(det, sizeof det, "攻%+d 魔%+d 道%+d\n防%+d 血%+d\n金币 %s",
                 cmp.d_atk, cmp.d_mc, cmp.d_sc, cmp.d_def, (int)cmp.d_hp,
                 gold);
    }
    lv_label_set_text(s_app.view.detail_label, det);
}

static void refresh_store(void) {
    /* Rows: red, blue, then the three store books of the player's class
     * (design 09, 1.76 plan). Potion rows carry the stack already held,
     * learned books go dim, and under-level books show their unlock level —
     * the store now refuses to sell those, so the row doubles as the gate. */
    char buf[384];
    const mafa_skill_t *sk = MAFA_SKILLS[s_app.player.cls];
    char nred[8], nblue[8], gold[12];
    fmt_count(nred, sizeof nred, s_app.player.pot_red);
    fmt_count(nblue, sizeof nblue, s_app.player.pot_blue);
    fmt_count(gold, sizeof gold, s_app.player.gold);
    int n = snprintf(buf, sizeof buf, "%s红药 50金 x%s\n%s蓝药 40金 x%s\n",
                     s_app.cur_store == 0 ? "＞" : "　", nred,
                     s_app.cur_store == 1 ? "＞" : "　", nblue);
    for (int i = 0; i < 3; ++i) {
        const char *mark = s_app.cur_store == i + 2 ? "＞" : "　";
        uint8_t idx = (uint8_t)(i + 1);
        if (mafa_skill_known(&s_app.player, idx))
            n += snprintf(buf + n, sizeof buf - n,
                          "#9AA3A8 %s%s %u金 已学#\n", mark, sk[idx].name,
                          (unsigned)mafa_book_price(s_app.player.cls, idx));
        else if (s_app.player.level < sk[idx].unlock)
            n += snprintf(buf + n, sizeof buf - n,
                          "#9AA3A8 %s%s %u金 Lv%u#\n", mark, sk[idx].name,
                          (unsigned)mafa_book_price(s_app.player.cls, idx),
                          (unsigned)sk[idx].unlock);
        else
            n += snprintf(buf + n, sizeof buf - n, "%s%s %u金\n", mark,
                          sk[idx].name,
                          (unsigned)mafa_book_price(s_app.player.cls, idx));
    }
    /* Row 5 is plain nav (matches the maps floor list): OK or long-OK
     * leaves, so the potion rows can own long-OK for hold-to-buy. */
    n += snprintf(buf + n, sizeof buf - n, "%s返回\n",
                  s_app.cur_store == MAFA_STORE_ROWS - 1 ? "＞" : "　");
    snprintf(buf + n, sizeof buf - n, "#F0C04A 金币 %s#", gold);
    lv_label_set_text(s_app.view.items_label, buf);
}

static void refresh_maps(void) {
    /* 8 shown rows: a locked endgame row carries the color markup plus the
     * lock suffix, so the window can hold ~280 bytes at worst. */
    char buf[320];
    buf[0] = '\0';
    if (s_app.floor_mode) {
        /* Floor list of one unlocked combat map (v1.3): every open floor
         * plus a back row; the deepest floor is the default cursor. */
        uint8_t map = (uint8_t)s_app.floor_mode;
        int deepest = s_app.player.floor_unlocked[map - 1];
        char row[64];
        for (int f = 0; f < deepest; ++f) {
            snprintf(row, sizeof row, "%s%d层 Boss:%s",
                     s_app.cur_floor == f ? "＞" : "　", f + 1,
                     map_boss_name(map, (uint8_t)(f + 1)));
            strncat(buf, row, sizeof buf - strlen(buf) - 1);
            strncat(buf, "\n", sizeof buf - strlen(buf) - 1);
        }
        snprintf(row, sizeof row, "%s返回", s_app.cur_floor == deepest ? "＞" : "　");
        strncat(buf, row, sizeof buf - strlen(buf) - 1);
        lv_label_set_text(s_app.view.items_label, buf);
        char det[96];
        snprintf(det, sizeof det, "%s 共%d层\n长按OK返回地图",
                 MAFA_MAP_NAMES[map], MAFA_MAP_FLOORS[map]);
        lv_label_set_text(s_app.view.detail_label, det);
        return;
    }
    /* Ten entries (v1.8) no longer fit the 8-row list (design 10): render
     * an 8-row window anchored so the cursor row always stays visible. */
    int first = 0;
    if (s_app.cur_maps > 7) first = (int)s_app.cur_maps - 7;
    int last = first + 7;
    if (last > MAFA_MAP_COUNT - 1) last = MAFA_MAP_COUNT - 1;
    for (int i = first; i <= last; ++i) {
        bool unlocked = i == MAFA_MAP_SAFE || i <= s_app.player.unlocked;
        char row[56];
        /* Design 10: locked rows are dim. */
        if (unlocked)
            snprintf(row, sizeof row, "%s%d.%s",
                     s_app.cur_maps == i ? "＞" : "　", i, MAFA_MAP_NAMES[i]);
        else
            snprintf(row, sizeof row, "#9AA3A8 %s%d.%s 锁定#",
                     s_app.cur_maps == i ? "＞" : "　", i, MAFA_MAP_NAMES[i]);
        strncat(buf, row, sizeof buf - strlen(buf) - 1);
        if (i < last) strncat(buf, "\n", sizeof buf - strlen(buf) - 1);
    }
    lv_label_set_text(s_app.view.items_label, buf);
    char det[96];
    if (s_app.player.map == MAFA_MAP_SAFE)
        snprintf(det, sizeof det, "当前:%s", MAFA_MAP_NAMES[s_app.player.map]);
    else
        snprintf(det, sizeof det, "当前:%s %d层",
                 MAFA_MAP_NAMES[s_app.player.map], s_app.player.floor);
    strncat(det, "\n击杀40出Boss,末层开下图", sizeof det - strlen(det) - 1);
    lv_label_set_text(s_app.view.detail_label, det);
}

/* Settings (design 11/14/15, v1.2): five cursor rows — the auto-potion
 * toggle, its two editable trigger lines, the auto-sell quality set, and
 * auto-boss — with the battery as a non-navigable dim row. Three render
 * modes: plain, potion-line edit (value gold in <brackets>, others dim),
 * and the auto-sell picker (four quality-colored names + 完成). */
static void refresh_settings(void) {
    char buf[512];
    int n = 0;
    int bat = battery_read();
    char bat_s[12];   /* room for the full int range: GCC counts "%d%%" as 12 */
    if (bat < 0)
        strcpy(bat_s, "--");
    else
        snprintf(bat_s, sizeof bat_s, "%d%%", bat);

    if (s_app.set_edit == 2) {
        /* Auto-sell picker: the other rows dim, the four quality names
         * wear their quality colors, 完成 closes. */
        n += snprintf(buf + n, sizeof buf - n, "#9AA3A8 %s自动喝药:%s#\n",
                      "  ", s_app.player.auto_potion ? "开" : "关");
        n += snprintf(buf + n, sizeof buf - n, "#9AA3A8 %s红药线:%u%%#\n",
                      "  ", (unsigned)s_app.player.pot_hp_pct);
        n += snprintf(buf + n, sizeof buf - n, "#9AA3A8 %s蓝药线:%u%%#\n",
                      "  ", (unsigned)s_app.player.pot_mp_pct);
        for (int i = 0; i < 4; ++i)
            n += snprintf(buf + n, sizeof buf - n, "%s%s%s  %s#\n",
                          s_app.cur_pick == i ? "＞" : "　",
                          SELL_PICK_COLOR[i], SELL_PICK_NAME[i],
                          (s_app.player.auto_sell >> i) & 1 ? "开" : "关");
        n += snprintf(buf + n, sizeof buf - n, "%s#F0C04A 完成#\n",
                      s_app.cur_pick == 4 ? "＞" : "　");
        n += snprintf(buf + n, sizeof buf - n, "#9AA3A8 %s自动Boss:%s#\n",
                      "  ", s_app.player.auto_boss ? "开" : "关");
        snprintf(buf + n, sizeof buf - n, "#9AA3A8 电量 %s#", bat_s);
        lv_label_set_text(s_app.view.items_label, buf);
        return;
    }

    bool editing = s_app.set_edit == 1;
    /* Auto-sell summary: the enabled quality names, or 关. */
    char sold[13] = {0};
    for (int i = 0; i < 4; ++i)
        if ((s_app.player.auto_sell >> i) & 1)
            strncat(sold, SELL_PICK_NAME[i], sizeof sold - strlen(sold) - 1);
    if (sold[0] == '\0') strcpy(sold, "关");

    if (editing) {
        /* design 14: the edited line keeps its cursor and shows the value
         * gold in <brackets>; every other row dims. */
        n += snprintf(buf + n, sizeof buf - n, "#9AA3A8 %s自动喝药:%s#\n",
                      "  ", s_app.player.auto_potion ? "开" : "关");
        n += snprintf(buf + n, sizeof buf - n, "%s红药线:",
                      s_app.cur_set == 1 ? "＞" : "　");
        if (s_app.cur_set == 1)
            n += snprintf(buf + n, sizeof buf - n, "#F0C04A <%u%%>#\n",
                          (unsigned)s_app.player.pot_hp_pct);
        else
            n += snprintf(buf + n, sizeof buf - n, "#9AA3A8 %s%u%%#\n",
                          "  ", (unsigned)s_app.player.pot_hp_pct);
        n += snprintf(buf + n, sizeof buf - n, "%s蓝药线:",
                      s_app.cur_set == 2 ? "＞" : "　");
        if (s_app.cur_set == 2)
            n += snprintf(buf + n, sizeof buf - n, "#F0C04A <%u%%>#\n",
                          (unsigned)s_app.player.pot_mp_pct);
        else
            n += snprintf(buf + n, sizeof buf - n, "#9AA3A8 %s%u%%#\n",
                          "  ", (unsigned)s_app.player.pot_mp_pct);
        n += snprintf(buf + n, sizeof buf - n, "#9AA3A8 %s自动卖:%s#\n",
                      "  ", sold);
        n += snprintf(buf + n, sizeof buf - n, "#9AA3A8 %s自动Boss:%s#\n",
                      "  ", s_app.player.auto_boss ? "开" : "关");
    } else {
        n += snprintf(buf + n, sizeof buf - n, "%s自动喝药:%s\n",
                      s_app.cur_set == 0 ? "＞" : "　",
                      s_app.player.auto_potion ? "开" : "关");
        n += snprintf(buf + n, sizeof buf - n, "%s红药线:%u%%\n",
                      s_app.cur_set == 1 ? "＞" : "　",
                      (unsigned)s_app.player.pot_hp_pct);
        n += snprintf(buf + n, sizeof buf - n, "%s蓝药线:%u%%\n",
                      s_app.cur_set == 2 ? "＞" : "　",
                      (unsigned)s_app.player.pot_mp_pct);
        n += snprintf(buf + n, sizeof buf - n, "%s自动卖:%s\n",
                      s_app.cur_set == 3 ? "＞" : "　", sold);
        n += snprintf(buf + n, sizeof buf - n, "%s自动Boss:%s\n",
                      s_app.cur_set == 4 ? "＞" : "　",
                      s_app.player.auto_boss ? "开" : "关");
    }
    snprintf(buf + n, sizeof buf - n, "\n#9AA3A8 电量 %s#", bat_s);
    lv_label_set_text(s_app.view.items_label, buf);
}

static void enter_page(page_t page) {
    lv_obj_t *prev = s_app.view.screen;   /* deleted after the new screen is
                                             loaded (fog load_screen order) */
    s_app.page = page;
    memset(s_app.prev_map, 0, sizeof s_app.prev_map);
    memset(s_app.prev_info, 0, sizeof s_app.prev_info);
    memset(s_app.prev_hptext, 0, sizeof s_app.prev_hptext);
    memset(s_app.prev_mptext, 0, sizeof s_app.prev_mptext);
    memset(s_app.prev_enemy, 0, sizeof s_app.prev_enemy);
    memset(s_app.prev_log, 0, sizeof s_app.prev_log);
    memset(s_app.prev_menu, 0, sizeof s_app.prev_menu);
    memset(s_app.prev_modal, 0, sizeof s_app.prev_modal);
    s_app.disp_hp = s_app.disp_hmax = -1;
    s_app.disp_mp = s_app.disp_mmax = -1;
    s_app.disp_ehp = s_app.disp_emax = -1;
    s_app.disp_ebar = true;               /* force the first idle apply */
    switch (page) {
    case PAGE_MENU:
        mafa_view_page_menu(&s_app.view);
        break;
    case PAGE_CLASS:
        mafa_view_page_class(&s_app.view);
        break;
    case PAGE_MAIN:
        mafa_view_page_main(&s_app.view);
        break;
    case PAGE_STATUS:
        mafa_view_page_status(&s_app.view);
        break;
    case PAGE_SKILLS:
        mafa_view_page_skills(&s_app.view);
        break;
    case PAGE_BACKPACK:
        mafa_view_page_backpack(&s_app.view);
        break;
    case PAGE_STORE:
        mafa_view_page_store(&s_app.view);
        break;
    case PAGE_MAPS:
        mafa_view_page_maps(&s_app.view);
        break;
    case PAGE_SETTINGS:
        mafa_view_page_settings(&s_app.view);
        break;
    }
    lv_screen_load(s_app.view.screen);    /* make the new screen visible */
    if (prev) lv_obj_delete(prev);
    switch (page) {
    case PAGE_MENU: refresh_menu(); break;
    case PAGE_CLASS: refresh_class(); break;
    case PAGE_MAIN: refresh_main(); break;
    case PAGE_BACKPACK: refresh_backpack(); break;
    case PAGE_STORE: refresh_store(); break;
    case PAGE_MAPS: refresh_maps(); break;
    case PAGE_SETTINGS:
        s_app.set_edit = 0;               /* every entry starts plain */
        s_app.cur_pick = 0;
        refresh_settings();
        break;
    case PAGE_STATUS: refresh_status(); break;
    case PAGE_SKILLS:
        s_app.skill_hint[0] = '\0';   /* reasons never survive page exit */
        refresh_skills();
        break;
    }
}

// --- Input (PRD 10) --------------------------------------------------------------------

static void input_main(bsp_btn_t btn, bool click) {
    /* Prompt modals capture input first. */
    if (s_app.boss_pending) {
        if (!click) return;
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)
            s_app.cur_modal = (s_app.cur_modal + 1) % 2;
        else if (btn == BSP_BTN_OK) {
            if (s_app.cur_modal == 0) {
                mafa_battle_t b;
                if (mafa_boss_start(&s_app.player, &b)) {
                    s_app.battle = b;
                    s_app.in_battle = true;
                    log_line("【Boss】%s!", b.mob[0].base->name);
                }
                s_app.boss_pending = false;
                modal_close();
            } else {
                mafa_boss_pass(&s_app.player);
                s_app.boss_pending = false;
                modal_close();
            }
        }
        if (s_app.boss_pending) boss_modal_refresh();
        else refresh_main();
        return;
    }
    if (s_app.player.pending_drop != MAFA_DROP_NONE) {
        if (!click) return;
        int rows = MAFA_BACKPACK + 1;    /* 8 slots + 丢弃 */
        if (btn == BSP_BTN_UP)
            s_app.cur_modal = (s_app.cur_modal + rows - 1) % rows;
        else if (btn == BSP_BTN_DOWN)
            s_app.cur_modal = (s_app.cur_modal + 1) % rows;
        else if (btn == BSP_BTN_OK) {
            if (s_app.cur_modal < MAFA_BACKPACK)
                mafa_drop_replace(&s_app.player, (uint8_t)s_app.cur_modal);
            else
                mafa_drop_discard(&s_app.player);
            modal_close();
            save_now();
        }
        if (s_app.player.pending_drop != MAFA_DROP_NONE) drop_modal_refresh();
        else refresh_main();
        return;
    }
    if (!click) return;
    if (btn == BSP_BTN_UP)
        s_app.cur_main = (s_app.cur_main + 6) % 7;
    else if (btn == BSP_BTN_DOWN)
        s_app.cur_main = (s_app.cur_main + 1) % 7;
    else if (btn == BSP_BTN_OK) {
        switch (s_app.cur_main) {
        case 0: enter_page(PAGE_BACKPACK); return;
        case 1: enter_page(PAGE_STATUS); return;
        case 2: enter_page(PAGE_SKILLS); return;
        case 3: enter_page(PAGE_STORE); return;
        case 4:
            s_app.cur_maps = s_app.player.map;
            s_app.floor_mode = 0;
            enter_page(PAGE_MAPS);
            return;
        case 5: enter_page(PAGE_SETTINGS); return;
        default:
            s_app.speed = (s_app.speed + 1) % 3;
            break;
        }
    }
    refresh_main();
}

static void process_event(const input_event_t *ev) {
    bool click = ev->event == BSP_BTN_CLICK;
    bool long_ok = ev->event == BSP_BTN_LONG && ev->btn == BSP_BTN_OK;
    if (s_app.buy_hold.active) {
        bool ok_release = ev->event == BSP_BTN_RELEASE
                          && ev->btn == BSP_BTN_OK;
        buy_hold_stop();
        if (ok_release) return;   /* the hold ended normally; nothing else */
        /* another key pressed mid-hold: stop, then handle that key too */
    }
    if (!click && !long_ok) return;

    switch (s_app.page) {
    case PAGE_MENU: {
        int rows = s_app.confirm_new ? 2 : (s_app.has_save ? 2 : 1);
        if (!click) break;
        if (ev->btn == BSP_BTN_UP || ev->btn == BSP_BTN_DOWN)
            s_app.cur_menu = (s_app.cur_menu + 1) % rows;
        else if (ev->btn == BSP_BTN_OK) {
            if (s_app.confirm_new) {
                if (s_app.cur_menu == 0) {          /* confirmed overwrite */
                    s_app.confirm_new = false;
                    enter_page(PAGE_CLASS);
                    return;
                }
                s_app.confirm_new = false;
            } else if (s_app.has_save && s_app.cur_menu == 0) {
                load_save();
                enter_page(PAGE_MAIN);
                log_clear("欢迎回来,冒险者!");
                refresh_main();
                return;
            } else if (s_app.has_save) {
                s_app.confirm_new = true;           /* 新游戏 → overwrite ask */
            } else {
                enter_page(PAGE_CLASS);
                return;
            }
        }
        refresh_menu();
        break;
    }
    case PAGE_CLASS:
        if (long_ok) { enter_page(PAGE_MENU); break; }
        if (!click) break;
        if (ev->btn == BSP_BTN_UP)
            s_app.cur_class = (s_app.cur_class + MAFA_CLS_COUNT - 1) % MAFA_CLS_COUNT;
        else if (ev->btn == BSP_BTN_DOWN)
            s_app.cur_class = (s_app.cur_class + 1) % MAFA_CLS_COUNT;
        else if (ev->btn == BSP_BTN_OK) {
            mafa_player_init(&s_app.player, (uint8_t)s_app.cur_class,
                             esp_random());
            save_now();
            enter_page(PAGE_MAIN);
            log_clear("冒险开始!怪物即将出现");
            refresh_main();
            break;
        }
        refresh_class();
        break;
    case PAGE_MAIN:
        input_main(ev->btn, click);
        break;
    case PAGE_STATUS:
        if (long_ok) { enter_page(PAGE_MAIN); break; }
        if (!click) break;
        if (ev->btn == BSP_BTN_UP)
            s_app.cur_status = (s_app.cur_status + MAFA_EQ_SLOTS - 1)
                               % MAFA_EQ_SLOTS;
        else if (ev->btn == BSP_BTN_DOWN)
            s_app.cur_status = (s_app.cur_status + 1) % MAFA_EQ_SLOTS;
        else if (ev->btn == BSP_BTN_OK) {
            /* v1.6 UX: OK takes the piece off so EITHER bracelet/ring twin
             * can be swapped — equipping always fills the free twin first,
             * which used to lock the second twin behind an impossible
             * replace. The item re-equips from the backpack. */
            if (s_app.player.equipped[s_app.cur_status] == MAFA_INV_EMPTY)
                break;                  /* nothing worn here */
            if (mafa_unequip(&s_app.player, (uint8_t)s_app.cur_status))
                save_now();
            else
                log_line("#E05A48 背包已满,卸下失败#");
        }
        refresh_status();
        break;
    case PAGE_SKILLS:
        if (long_ok) { enter_page(PAGE_MAIN); break; }
        if (!click) break;
        if (ev->btn == BSP_BTN_UP) {
            s_app.cur_skill = (s_app.cur_skill + MAFA_SKILLS_PER_CLASS - 1)
                              % MAFA_SKILLS_PER_CLASS;
            s_app.skill_hint[0] = '\0';
        } else if (ev->btn == BSP_BTN_DOWN) {
            s_app.cur_skill = (s_app.cur_skill + 1) % MAFA_SKILLS_PER_CLASS;
            s_app.skill_hint[0] = '\0';
        } else if (ev->btn == BSP_BTN_OK) {
            /* Skill row: OK toggles a learned active; fixed and unknown
             * rows say why there is nothing to toggle in the detail panel
             * instead of failing silently. */
            uint8_t idx = (uint8_t)s_app.cur_skill;
            const mafa_skill_t *sk = &MAFA_SKILLS[s_app.player.cls][idx];
            if (mafa_skill_known(&s_app.player, idx)
                && sk->kind != MAFA_SK_PASSIVE
                && sk->kind != MAFA_SK_PROC) {
                s_app.player.skills_off ^= (uint8_t)(1u << idx);
                save_now();
                s_app.skill_hint[0] = '\0';
            } else if (!mafa_skill_known(&s_app.player, idx)) {
                if (s_app.player.level < sk->unlock)
                    snprintf(s_app.skill_hint, sizeof s_app.skill_hint,
                             "等级不足,需 Lv%u", (unsigned)sk->unlock);
                else if (idx <= 3)
                    snprintf(s_app.skill_hint, sizeof s_app.skill_hint,
                             "书店购买后习得");
                else
                    snprintf(s_app.skill_hint, sizeof s_app.skill_hint,
                             "精英/Boss掉落习得");
            } else {
                snprintf(s_app.skill_hint, sizeof s_app.skill_hint,
                         "被动技能常驻生效,无开关");
            }
        }
        refresh_skills();
        break;
    case PAGE_BACKPACK:
        if (long_ok) { enter_page(PAGE_MAIN); break; }
        if (!click) break;
        if (s_app.packsub) {
            if (ev->btn == BSP_BTN_UP || ev->btn == BSP_BTN_DOWN)
                s_app.cur_packsub = (s_app.cur_packsub + 1) % 2;
            else if (ev->btn == BSP_BTN_OK) {
                if (s_app.cur_packsub == 0)
                    mafa_equip(&s_app.player, (uint8_t)s_app.cur_pack);
                else
                    mafa_sell(&s_app.player, (uint8_t)s_app.cur_pack);
                s_app.packsub = false;
                save_now();
            }
        } else {
            if (ev->btn == BSP_BTN_UP)
                s_app.cur_pack = (s_app.cur_pack + MAFA_BACKPACK - 1) % MAFA_BACKPACK;
            else if (ev->btn == BSP_BTN_DOWN)
                s_app.cur_pack = (s_app.cur_pack + 1) % MAFA_BACKPACK;
            else if (ev->btn == BSP_BTN_OK
                     && s_app.player.inv_id[s_app.cur_pack] != MAFA_INV_EMPTY) {
                s_app.packsub = true;
                s_app.cur_packsub = 0;
            }
        }
        refresh_backpack();
        break;
    case PAGE_STORE:
        if (long_ok) {
            /* Potion rows: 长按连买 (holds repeat until release/gold out);
             * everywhere else long-OK still leaves the store. */
            if (s_app.cur_store <= 1) {
                buy_hold_start(s_app.cur_store == 0);
                if (!s_app.buy_hold.active)
                    log_buy_fail(s_app.cur_store == 0);
                refresh_store();
                break;
            }
            enter_page(PAGE_MAIN);
            break;
        }
        if (!click) break;
        if (ev->btn == BSP_BTN_UP)
            s_app.cur_store = (s_app.cur_store + MAFA_STORE_ROWS - 1)
                              % MAFA_STORE_ROWS;
        else if (ev->btn == BSP_BTN_DOWN)
            s_app.cur_store = (s_app.cur_store + 1) % MAFA_STORE_ROWS;
        else if (ev->btn == BSP_BTN_OK) {
            if (s_app.cur_store == MAFA_STORE_ROWS - 1) {
                enter_page(PAGE_MAIN);  /* 返回 row */
                break;
            }
            if (s_app.cur_store <= 1) {
                bool red = s_app.cur_store == 0;
                if (!mafa_buy_potion(&s_app.player, red))
                    log_buy_fail(red);
            }
            else {
                /* 1.76 plan gate: the store refuses under-level buys; the
                 * log says why instead of failing silently. */
                uint8_t skill_idx = (uint8_t)(s_app.cur_store - 1);
                const mafa_skill_t *sk
                    = &MAFA_SKILLS[s_app.player.cls][skill_idx];
                if (mafa_buy_book(&s_app.player, skill_idx))
                    log_line("#F0C04A 习得【%s】!#", sk->name);
                else if (s_app.player.level < sk->unlock)
                    log_line("#E05A48 等级不足,需 Lv%u#",
                             (unsigned)sk->unlock);
                else
                    log_line("#E05A48 金币不足#");
            }
            save_now();
        }
        refresh_store();
        break;
    case PAGE_MAPS:
        if (long_ok) {
            /* Long-OK backs out one level inside the floor list; from the
             * map list it still returns to the main page. */
            if (s_app.floor_mode != 0) {
                s_app.floor_mode = 0;
                refresh_maps();
            } else {
                enter_page(PAGE_MAIN);
            }
            break;
        }
        if (!click) break;
        if (s_app.floor_mode != 0) {
            /* Floor list: rows 0..deepest-1 are floors, deepest is 返回. */
            int rows = s_app.player.floor_unlocked[s_app.floor_mode - 1] + 1;
            if (ev->btn == BSP_BTN_UP || ev->btn == BSP_BTN_DOWN)
                s_app.cur_floor = (s_app.cur_floor + (ev->btn == BSP_BTN_UP
                                       ? rows - 1 : 1)) % rows;
            else if (ev->btn == BSP_BTN_OK) {
                if (s_app.cur_floor == rows - 1) {
                    s_app.floor_mode = 0;
                    refresh_maps();
                    break;
                }
                mafa_switch_map(&s_app.player, (uint8_t)s_app.floor_mode,
                                (uint8_t)(s_app.cur_floor + 1));
                save_now();
                enter_page(PAGE_MAIN);
                log_clear("【%s %d层】开始挂机", MAFA_MAP_NAMES[s_app.player.map],
                          s_app.player.floor);
                refresh_main();
                break;
            }
            refresh_maps();
            break;
        }
        if (ev->btn == BSP_BTN_UP || ev->btn == BSP_BTN_DOWN) {
            /* The cursor only rests on rows the player may enter: the
             * safe zone plus unlocked combat maps. */
            do {
                s_app.cur_maps = ev->btn == BSP_BTN_UP
                    ? (s_app.cur_maps + MAFA_MAP_COUNT - 1) % MAFA_MAP_COUNT
                    : (s_app.cur_maps + 1) % MAFA_MAP_COUNT;
            } while (s_app.cur_maps != MAFA_MAP_SAFE
                     && s_app.cur_maps > s_app.player.unlocked);
        } else if (ev->btn == BSP_BTN_OK) {
            if (s_app.cur_maps == MAFA_MAP_SAFE) {
                mafa_switch_map(&s_app.player, MAFA_MAP_SAFE, 0);
                save_now();
                enter_page(PAGE_MAIN);
                /* Entering town restores full HP/MP (1.76 plan): death
                 * respawn and a voluntary walk home heal the same. */
                log_clear("#9AA3A8 你回到 安全区#");
                log_line("#5FC85F 血蓝已回满#");
                log_line("#F0C04A 【安全区】休息中#");
                refresh_main();
                break;
            }
            /* v1.3: OK opens the floor picker, cursor at the deepest floor. */
            s_app.floor_mode = s_app.cur_maps;
            s_app.cur_floor = s_app.player.floor_unlocked[s_app.cur_maps - 1] - 1;
            refresh_maps();
            break;
        }
        refresh_maps();
        break;
    case PAGE_SETTINGS:
        if (long_ok) {
            if (s_app.set_edit != 0) save_now();   /* leaving edit saves */
            enter_page(PAGE_MAIN);
            break;
        }
        if (!click) break;
        if (s_app.set_edit == 1) {
            /* Potion-line edit (design 14): UP/DOWN step 10 %, OK saves. */
            uint8_t *pct = s_app.cur_set == 1 ? &s_app.player.pot_hp_pct
                                              : &s_app.player.pot_mp_pct;
            if (ev->btn == BSP_BTN_UP) {
                if (*pct < MAFA_POT_PCT_MAX) *pct = (uint8_t)(*pct + 10);
            } else if (ev->btn == BSP_BTN_DOWN) {
                if (*pct > MAFA_POT_PCT_MIN) *pct = (uint8_t)(*pct - 10);
            } else if (ev->btn == BSP_BTN_OK) {
                s_app.set_edit = 0;
                save_now();
            }
        } else if (s_app.set_edit == 2) {
            /* Auto-sell picker (design 15): OK flips a quality; 完成
             * closes the picker. */
            if (ev->btn == BSP_BTN_UP)
                s_app.cur_pick = (s_app.cur_pick + 4) % 5;
            else if (ev->btn == BSP_BTN_DOWN)
                s_app.cur_pick = (s_app.cur_pick + 1) % 5;
            else if (ev->btn == BSP_BTN_OK) {
                if (s_app.cur_pick < 4)
                    s_app.player.auto_sell ^= (uint8_t)(1u << s_app.cur_pick);
                else
                    s_app.set_edit = 0;
                save_now();
            }
        } else {
            if (ev->btn == BSP_BTN_UP)
                s_app.cur_set = (s_app.cur_set + 4) % 5;
            else if (ev->btn == BSP_BTN_DOWN)
                s_app.cur_set = (s_app.cur_set + 1) % 5;
            else if (ev->btn == BSP_BTN_OK) {
                switch (s_app.cur_set) {
                case 0:
                    s_app.player.auto_potion = !s_app.player.auto_potion;
                    save_now();
                    break;
                case 1:
                case 2:
                    s_app.set_edit = 1;
                    break;
                case 3:
                    s_app.set_edit = 2;
                    s_app.cur_pick = 0;
                    break;
                default:
                    s_app.player.auto_boss = !s_app.player.auto_boss;
                    save_now();
                    break;
                }
            }
        }
        refresh_settings();
        break;
    }
}

// Input task: paces the idle battle via queue timeout; button events arrive
// from the shared esp_timer task and are only enqueued (PRD 9.3 pattern).
static void input_task(void *arg) {
    (void)arg;
    input_event_t ev;
    for (;;) {
        bool idle_page = s_app.page == PAGE_MAIN && !s_app.boss_pending
                         && s_app.player.pending_drop == MAFA_DROP_NONE;
        TickType_t wait;
        if (s_app.buy_hold.active)
            wait = pdMS_TO_TICKS(MAFA_BUY_HOLD_MS);
        else
            wait = idle_page ? pdMS_TO_TICKS(PACE_MS[s_app.speed])
                             : portMAX_DELAY;
        if (xQueueReceive(s_app.queue, &ev, wait) == pdTRUE) {
            if (!bsp_lvgl_lock(500)) continue;
            process_event(&ev);
            bsp_lvgl_unlock();
        } else {
            if (!bsp_lvgl_lock(500)) continue;
            if (s_app.buy_hold.active)
                buy_hold_tick();
            else {
                tick_battle();
                refresh_main();
            }
            bsp_lvgl_unlock();
        }
    }
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t event, void *user) {
    (void)user;
    if (!s_app.ready || !s_app.queue) return;
    const input_event_t input = {.btn = btn, .event = event};
    (void)xQueueSend(s_app.queue, &input, 0);
}

// --- Boot --------------------------------------------------------------------------------

void mafa_app_boot(void) {
    memset(&s_app, 0, sizeof s_app);
    s_app.battery = -1;
    s_app.battery_ok = bsp_battery_init() == ESP_OK;
    if (!s_app.battery_ok)
        ESP_LOGW(TAG, "battery gauge unavailable; slot stays blank");

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);

    s_app.queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_app.queue) {
        ESP_LOGE(TAG, "input queue alloc failed");
        return;
    }
    if (xTaskCreate(input_task, "mafa_input", 4096, NULL, 5, &s_app.task)
        != pdPASS) {
        vQueueDelete(s_app.queue);
        s_app.queue = NULL;
        ESP_LOGE(TAG, "input task create failed");
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button init failed");
    }

    if (bsp_lvgl_lock(1000)) {
        s_app.has_save = load_save();
        if (!s_app.has_save)
            mafa_player_init(&s_app.player, MAFA_CLS_WARRIOR, esp_random());
        enter_page(PAGE_MENU);
        bsp_lvgl_unlock();
        s_app.ready = true;
    } else {
        ESP_LOGE(TAG, "LVGL lock timeout; UI not built");
    }
    ESP_LOGI(TAG, "MAFA CHRONICLE ready battery=%d save=%d",
             s_app.battery, s_app.has_save);
}
