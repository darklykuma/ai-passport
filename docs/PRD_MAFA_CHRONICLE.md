<p align="right">
  <a href="PRD_MAFA_CHRONICLE.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# MAFA CHRONICLE PRD

> Document status: **Draft v1.0, awaiting review**
> Product carrier: FoloToy AI Passport / ESP32-C3 / 240 × 320 LCD / three ADC buttons (UP/DOWN/OK)
> Target branch: `feature/mafa-chronicle` (to be created)
> PRD version: v1.0
> Updated: 2026-09-29

---

## 1. Product overview

### 1.1 Product name

- Chinese name (working title, renameable): Ma Fa Zhan Ji (the exact glyphs live in the Chinese edition of this PRD)
- English UI name: `MAFA CHRONICLE`
- Internal feature name: `Mafa Chronicle` (single-player text-legend-style RPG)

### 1.2 Positioning

MAFA CHRONICLE is a **single-player idle text RPG** (text-legend style) running on the FoloToy AI Passport. The player picks one of three classes (warrior / mage / taoist), picks a map, and the character **fights monsters automatically**; combat streams past as a scrolling log. The player's decisions happen outside combat — equipping, selling, buying potions, switching to deeper maps, answering boss events — living the loop of **fight → loot → equip → idle on deeper maps**.

The screen keeps the classic text-legend three bands, dressed in gold-edged panel chrome (v0.7): a header band (map name with boss kill progress, level with XP percent toward the next level, gold, HP and MP bars), an enemy strip with its own HP bar (the name turns gold during boss fights), the scrolling combat log in the middle (the game's "picture", framed with a small gold caption), and a bottom action bar. Every chrome object is created once per screen and diff-refreshed — no per-tick allocation. Three buttons are the entire input.

### 1.3 Core value

1. **Numbers go up every minute**: XP, gold, drops, levels — high feedback density.
2. **Loot excitement**: quality tiers (white/green/blue/purple/gold) and random drops make every fight potentially open a good item.
3. **Playable with three buttons**: UP/DOWN to move, OK to confirm, long-press to go back — zero learning cost.
4. **Short sessions**: idle starts the moment the device boots; drop it anytime. Unlocking a new map takes roughly 20–40 minutes — fits fragmented time.

### 1.4 Relationship to FOG MARCH

- This is an **independent derivative application**: its own PRD, UI, and code (`main/mafa_*.c`); the tactics game's visual shell is not reused. BSP, the font-generation pipeline, the LVGL diff-refresh pattern, and the "pure model + host tests" architecture are reused directly.
- FOG MARCH P0 remains in the tree in a stable, playable state; `main.c` switches the entry point to this application.

## 2. Background and design inputs

### 2.1 Hardware facts

- ESP32-C3, 8 MB flash, no PSRAM; 240 × 320 RGB565 LCD; three ADC-ladder buttons (UP/DOWN/OK); mono speaker; readable battery gauge.
- LVGL 9.5 + esp_lvgl_port; built-in malloc pool currently 96 KB (FOG MARCH lesson: exhausting the pool makes the render task spin while holding the lock, freezing the whole device).
- An NVS partition exists; a tool pipeline generates CJK subset fonts from a codepoint inventory.

### 2.2 Design inputs

- FOG MARCH on-device playtest feedback (2026-09-28): controls acceptable, but the tactics gameplay "isn't fun" — decisions lacked weight and matches lacked an arc. The player explicitly prefers a **monster-slaying, loot-grinding progression loop**.
- **Reference product**: TapTap workshop "Text Legend" (Wenzi Chuanqi; app 842887; portrait idle text-legend RPG: auto-battle grinding, equipment drop collection, shop purchases, multi-map exploration, five maps). Its player reviews translate directly into hard requirements for this product:
  1. "No save; every entry is a fresh start" → saves are first-class; power-cycle resume is an acceptance item.
  2. "Attack and defense are still manual? Terrible operation" → combat must be automatic; player decisions live outside combat.
  3. "All you do is endlessly sell backpack items and click heal/mana" → auto-sell-whites and auto-potion toggles are mandatory.
  4. "Extremely thin content" → content volume is the lifeline; P0 starts with three solid maps.
- The mature structure of the text-legend genre: menu-driven, number growth, loot surprises; historically native to 9-key/3-key devices.

### 2.3 The core input constraint

All interaction = UP / DOWN / OK (click) + OK long-press (back). Every screen must be fully operable with only these inputs; no critical action may depend on long-press (long-press is a convenience back).

## 3. Key assumptions and risks

### 3.1 Highest-priority assumption

**H1: the progression loop itself produces fun.** Verified through the DoD in chapter 13: multiple on-device sessions where the player voluntarily wants "one more run".

### 3.2 Secondary assumptions

- H2: three buttons suffice for every action without feeling clumsy (action menu ≤ 5 entries, backpack ≤ 8 slots, list depth controlled).
- H3: 16 px CJK text with HP bars and a log reads clearly on 240 × 320.

### 3.3 Identified risks

- **Content volume overrun**: the genre's fun depends on content volume. Countermeasure: keep the P0 boundary (4.3) deliberately small; verify the feel first.
- **Numeric imbalance**: countermeasure: a host-side simulator in M2 that runs ≥1000 automated battles to validate per-area win-rate curves; tables are tuned data, not code.
- **LVGL pool pressure**: text screens use far fewer objects than the tactics map, but keep the diff-refresh pattern and re-verify on device.
- **Font inventory gaps**: follow the FOG MARCH codepoint-check test pattern (tests/test_fog_ui_glyphs.py); any copy change must update the inventory.

## 4. Goals and non-goals

### 4.1 P0 goals

1. A replayable growth loop: idle grinding → drops → equip/sell → idle on deeper maps.
2. Three selectable classes, 3 auto-cast skills each (unlocked at L3/L7/L12).
3. Three escalating maps (forest / mine / temple); boss-event victory unlocks the next.
4. Equipment in three slots × five quality tiers, from drops and the store; auto-sell-whites and auto-potion on by default.
5. NVS persistence across power cycles, with autosave points covering every state-changing action (counters the reference's worst review).

### 4.2 Success metrics

- On-device DoD fully green (chapter 13).
- Host simulator (per map, at the suggested level with the map's T1 gear and 8 red / 4 blue potions):
  - kill interval 1.5–10 s per monster;
  - idle death interval ≥ 3 min;
  - boss win rate: at least one class per map within 20–85% (a viable reference path) and no class locked out at 0%; a second class trivially beating the boss is normal idle-game scaling.

### 4.3 P0 non-goals

- Networking, multiplayer, PvP, chat; MMO theater (world channel / fake players / leaderboards — P1, chapter 14).
- Idle/offline gains while powered off, multiple save slots, equipment enhancement/forging, class change, skill trees.
- Touch input, real-time action, a map editor, a manual combat mode.
- Open-world exploration (linear map unlock instead).

## 5. Target users and scenarios

### 5.1 Target users

The device owner; familiar with the text-legend genre; plays a few rounds in fragmented time.

### 5.2 Core scenarios

1. **Five-minute commute**: power on → continue → watch the log while a few items drop → power off; progress already saved.
2. **Twenty-minute block**: switch to the deepest map, answer a boss event, equip new loot, buy potions, keep idling.
3. **The farming moment**: camp on a high-tier map, re-trigger the boss, gamble for purple/gold; nobody else's progress matters — the pace is yours.

## 6. Product loop

```text
Main screen (pick map, idle)
   │  character auto-fights: log scrolls, XP/gold/drops keep landing
   │
   ├─ out-of-combat actions: equip/sell · buy potions · switch to deeper maps
   │
   ├─ kill count triggers boss event ──▶ win: unlock next map + purple/gold drop
   │                                     lose: no penalty, keep idling
   ▼
numbers grow ──▶ deeper maps ──▶ better loot ──▶ (loop)
```

- **Idling is the default state**: boot lands on the main screen already auto-fighting on the current map; the player may just watch.
- **Boss event**: after every 25 kills on a map ("Forest Ape appears!"), a three-key prompt offers fight/pass; after victory the counter resets so it can trigger again (repeatable farming). Boss drops roll purple/gold — the ultimate farming hook.
- **Sense of progression**: unlocking a map = the world grows; deeper maps have stronger monsters and higher loot tiers (9.2/9.3).
- Nested feedback: second-scale log lines per battle (short), minute-scale boss events (medium), long-term level/map unlocks (long).

## 7. Information architecture (screen flow)

```text
Boot ─▶ [Continue / New game] (new game → pick class)
              │
              ▼
     ┌── Main screen: idle on map (status bar + combat log + action menu) ──┐
     │   action menu: Backpack / Gear / Store / Map / Settings / Speed      │
     │        ├─ Backpack (equip / sell / sell-all-whites)                 │
     │        ├─ Gear (equip slots + potions + skills on/off)              │
     │        ├─ Store (potions + class books, shows holdings)             │
     │        ├─ Map (unlocked list; switching moves the idle spot)        │
     │        ├─ Settings (toggles + potion thresholds + battery)          │
     │        └─ Speed (1x / 2x / 4x)                                      │
     └─ Boss event prompt (fight / pass) ◀── kill-count trigger ───────────┘
```

## 8. Core functional requirements

### 8.1 Classes

Three classes, chosen at game start, immutable. Stats: HP / MP / attack / defense / level.

| Class | Role | Damage style |
| --- | --- | --- |
| Warrior | High HP/defense, steady damage | solid normal hits, procs and charge, AoE on packs |
| Mage | Highest attack, fragile | nukes + AoE burn, Magic Shield survival; fast clears, expensive in potions |
| Taoist | Balanced sustain fighter | pet tank + talisman damage + heal/poison sustain; survives the deepest maps |

### 8.2 Stats and growth

- XP comes only from battles; level-ups auto-allocate points (no manual allocation in P0).
- XP to next level follows a **front-fast, back-wall curve** (v0.8, modeled on the original 1.76 curve: quick newbie pace, compounding from mid-game, one long wall at the cap):

| L→L+1 | 1→2 | 2→3 | 3→4 | 4→5 | 5→6 | 6→7 | 7→8 | 8→9 | 9→10 | 10→11 | 11→12 | 12→13 | 13→14 | 14→15 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| XP | 300 | 550 | 900 | 1400 | 2100 | 3000 | 4200 | 5500 | 8000 | 13000 | 22000 | 38000 | 65000 | 150000 |

  The 14→15 wall alone is ~45 % of the total grind (≈315 k XP), sized at roughly 700+ map-3 battles — the graduation event.
- Per-level growth (base value + per-level increment):

| Class | HP | MP | Attack | Defense |
| --- | --- | --- | --- | --- |
| Warrior | 60 +8 | — | 10 +2 | 5 +1 |
| Mage | 40 +5 | 30 +5 | 14 +2 | 3 +1 (every 2 levels) |
| Taoist | 60 +6 | 25 +4 | 12 +1 | 4 +1 |

- MP regeneration: +2 at the end of each battle round; outside combat, HP/MP regenerate slowly per 8.8.

### 8.3 Battle (automatic)

- **Combat is fully automatic**: on a map the character attacks every 1.5 s per round (speed 1x/2x/4x), presented entirely as a scrolling log.
- **Packs (v0.8)**: normal battles spawn **1–3 monsters** of the current map (weights shift to 3 mobs on deeper maps: map 1 ≈ 70/30/0, map 2 ≈ 40/40/20, map 3 ≈ 20/40/40). Spawn species are drawn from the player's level band (mob level ≤ player level + 2; full map pool as fallback). The player strikes the first living mob; AoE skills hit every living mob; **each living monster acts every round**. Boss battles stay 1v1.
- **Normal attack**: `damage = max(1, floor(attack × U(0.9, 1.1)) − defense)`; U is an integer random over 0.9–1.1.
- **Critical**: 10% chance, attack ×1.5; the log marks it with a "CRIT" prefix.
- **Skills auto-cast** by the priority policy in 8.4; log lines open with the skill name.
- **Auto-potion** (settings toggle, default on): red potion when HP falls below the **red line**, blue potion when MP falls below the **blue line** (v1.2: both thresholds are adjustable on the settings page, 20–80% in steps of 10; defaults 50% HP / 30% MP). Potions restore level-scaled flat amounts (8.8) so one potion always answers roughly one mob hit, early and late.
- **Death**: HP ≤ 0 → log "You were killed by…" → respawn in the safe zone at full HP/MP, the player picks the next map themselves; death penalties per 8.9.
- **Combat log**: the latest 6 lines scrolling; kills/drops/level-ups/boss events use accent colors. Monster packs show as "▶multi-hook-cat×3" with a pooled HP bar; the pet announces itself through log lines.
- Monster AI (P0): normal attack; below 30% HP, a 50% chance to use its monster skill (see table 9.2).
- **Auto-sell (v1.2, replaces auto-sell-white)**: a **selectable set of qualities** converts its drops to gold at sell price immediately, never occupying the backpack; everything else goes to the backpack for the player to judge. Pickable: white / green / blue / purple — one bit each, default **white only** (the old behavior). **Gold is never auto-sold**: legendary drops always reach the player. The settings row summarizes the set ("自动卖:白"); OK opens an in-place picker listing the four colors in their quality colors plus a done row (mockup 15).

### 8.4 Skills

- **Five per class (v0.8)**, with distinct forms: passive, proc, single-target nuke, AoE nuke, AoE burn, heal, poison, shield, charge, and the taoist pets. Skills unlock by **level AND skill book** (8.5): skill 1 of each class is free, the rest need their book (store or drop). Once unlocked they cast fully automatically.
- Warrior uses cooldowns (no MP); mage/taoist use MP.
- **Skill state is always visible (v1.2)**: the gear page lists all five class skills below the equipment, each with its live state — "always on" (passive/proc), "on/off" (active; OK toggles), "store 300" (buyable book), or the lock reason "Lv12 boss" / "elite/boss" (level gate first, then the drop source). A book may be **banked before its unlock level** (drops and purchases don't check level); the skill activates at the level-up that reaches the gate — the gear page is what makes that waiting state visible, instead of a one-shot log line.
- **Active skills can be switched off (v1.2)**: OK on a skill row toggles it (passive/proc rows are fixed); the auto-cast policy skips switched-off skills. Switching every active skill off leaves plain attacks — a legitimate choice, not an error.
- **AoE forms are crowd-only by design**: Half-Moon Sweep / Frost Howl cast only with **2+ living monsters** (a 0.9× swing on a lone target would be weaker than a normal attack). Map 1 spawns a lone monster ≈70% of the time, so these skills light up mostly from map 2 onward; the gear page's "AoE" hint is where that condition lives.

| Class | Skill | Unlock | Book | Effect | Cost |
| --- | --- | --- | --- | --- | --- |
| Warrior | Basic Swordsmanship | L1 | — | passive: normal attack ×1.1 | — |
| Warrior | Slash Assault | L3 | store 300 | proc: 20% on a normal hit, the strike lands twice | — |
| Warrior | Assassination | L6 | store 800 | 1.4 × attack, ignores defense | 5-round cooldown |
| Warrior | Half-Moon Sweep | L9 | elite drop | 0.9 × attack to **every** monster | 6-round cooldown |
| Warrior | Flame Blade | L12 | boss drop | charge: the next normal attack hits ×2.2 | 8-round cooldown |
| Mage | Fire Ball | L1 | — | 1.6 × attack | MP 8 |
| Mage | Thunder | L3 | store 300 | 2.2 × attack, ignores defense | MP 14 |
| Mage | Firewall | L8 | store 800 | burn **all** monsters: 3 rounds of 0.6 × attack per round | MP 18 |
| Mage | Magic Shield | L10 | elite drop | damage taken −40% for 4 rounds | MP 16 |
| Mage | Frost Howl | L13 | boss drop | 1.6 × attack to **every** monster, ignores defense | MP 26 |
| Taoist | Heal | L3 | store 300 | restore 30% max HP (casts below 60% HP) | MP 12 |
| Taoist | Summon Skeleton | L7 | store 800 | pet tank (see below) | MP 20, 6-round cooldown |
| Taoist | Poison | L9 | elite drop | poisoned: 5 rounds of −5 HP and monster defense −30% | MP 12 |
| Taoist | Soul Fire Talisman | L11 | elite drop | 2.4 × attack | MP 14 |
| Taoist | Summon Divine Beast | L13 | boss drop | replaces the skeleton with a stronger pet | MP 30, 6-round cooldown |

**Pets (taoist identity)**: a battle-side entity (HP/attack/defense scaling with player level; skeleton at L7 ≈ 93 HP / 11 atk, divine beast at L13 ≈ 201 HP / 25 atk). While alive it **taunts**: every monster swing hits the pet instead of the player. The pet attacks the first living mob each round. When it falls there is a 6-round wait before re-summoning; monster STING poison only applies when a hit lands on the player.

**Auto-cast policy** (each round takes the first available by priority; skills switched off on the gear page are skipped, v1.2):

- Warrior: Flame Blade charge > Half-Moon Sweep (2+ monsters) > Assassination.
- Mage: Magic Shield (none active and HP < 70%) > Frost Howl (2+ monsters) > Thunder > Firewall (not already burning) > Fire Ball.
- Taoist: Heal at HP < 60% > Summon (pet down, cooldown ready) > Poison if the target is not poisoned > Soul Fire Talisman with MP ≥ 30%.

Burn/poison and similar effects attach to a per-monster effect list, resolved each round (covered by model-layer host tests).

**Boss-fight rules** (balance-simulator calibration, PRD 4.2):
- Bosses pierce 50% of player defense — keeping high-defense warriors and glass-cannon mages in the same danger band;
- Monster damage rolls within ±20% of the nominal value, so outcomes spread smoothly instead of tipping at a budget cliff;
- Potions restore flat amounts scaled by level: red +30 +2/level HP, blue +15 +1/level MP (one potion ≈ one mob hit at any stage).

### 8.5 Items and equipment

- **Slots**: weapon (attack) / armor (defense + HP) / accessory (attack or HP).
- **Quality**: white (common) / green (fine) / blue (rare) / purple (epic) / gold (legendary); the color is the text color.
- **Backpack 8 slots**; identical stackable items merge; **when full**, a new drop opens a three-key prompt (replace / discard / pass) and idling pauses during the prompt.
- **Equipment comparison**: selecting a backpack item shows the equipped piece vs the new one side by side (attack/defense/HP deltas); OK equips, long-press returns.
- No level requirements in P0; equip/unequip applies immediately and triggers autosave.
- **Consumables**: red and blue potions (stackable). Counts are visible on the **gear page** (v1.2, "红药x3 蓝药x2" line) and ride the store rows (v1.2), so a purchase is confirmed at a glance.
- **Skill books (v0.8)**: not backpack items — a bitmask in the save. Books 1–2 per class are sold in the store (the main gold sink); books 3–4 come only from elite drops (≈20%) and a **boss first-kill guarantee** (skill 3, then skill 4). Books may be banked before their unlock level; the skill casts only from that level on, and the gear page shows the waiting state (8.4).

### 8.6 Drops

One settlement roll per killed monster (see 9.3):

- Gold: monster level × U(2,5) — sized so a map-3 kill funds about one potion.
- Equipment: normal monsters **20%** (white 60% / green 32% / blue 8%); **elites** (about 1 in 10 battles) always drop (tier 2 70% / tier 3 30%); **the boss always drops (tier 2 92% / tier 3 8%)**.
- **Gold-tier gear is boss-only (8%)** — trash and elites stop at the map's purple line, so Dragon Slayer / Overlord Mail / Soul Chain stay a chase.
- **Sell prices** (used by manual selling and auto-sell-white): white 10 / green 30 / blue 80 / purple 200 / gold 500 gold.

### 8.7 Maps and unlocking

Three idle maps in P0, unlocked linearly (boss-event victory unlocks the next). Map ids are 1–3 (v1.0); id 0 is the safe zone (8.9), listed first on the map page:

| Map | Suggested level | Monster theme | Boss |
| --- | --- | --- | --- |
| Beech Forest | L1–5 | chicken / deer / scarecrow / hook-cat / forest yeti | Forest Ape |
| Abandoned Mine | L5–10 | skeleton / skeleton warrior / mine rat / axe skeleton / cave scorpion | Corpse King |
| Zuma Temple | L10–15 | zuma guard / zuma statue / black maggot / contract moth / giant rat | Zuma Overlord |

- On the current map monsters spawn randomly (level band per 8.3), fought one after another automatically.
- Boss event: triggers after every **40 kills** on the map (a 3-mob battle counts 3); repeatable (8.6 boss drop roll is independent each time).
- Monsters scale slightly with player level (area base + 5% per level difference, never down).

### 8.8 Store

- No separate town screen in P0: the main screen is the camp, and the store is one action-menu entry. HP/MP regenerate slowly while not in combat (+10/s).
- Store: red potion **50** gold (+30+2/level HP), blue potion **40** gold (+15+1/level MP), and the two store skill books of the player's class (300 / 800 gold); gold cap 9999. Rows show the potion stack already held ("红药 50金 x3") and a "Lv X" hint on a book whose unlock level is above the player's (v1.2) — buying early is allowed, the skill simply waits.
- Potion economy (v0.8): a mid-map kill funds roughly one potion — supply is a decision, not a given.

### 8.9 Death

HP ≤ 0: log "You were killed by…" → the player respawns in the **safe zone** at full HP/MP (v0.9) and idling stops until they pick the next map from the map page themselves — getting kicked back to town is part of the death cost. The boss kill counter resets, and the white-name PvE penalty from the original applies (v0.8):

- **Backpack**: 1–2 random stacks are lost permanently (no corpse run in an idle game);
- **Gold**: 10–20% of carried gold is lost;
- **Equipped gear is safe** (too harsh for a single-player idle game; potions are safe too).

The safe zone (v0.9) is map id 0 and heads the map page (v1.0): always open, no monsters, no boss; switching to it is a voluntary rest option, and combat maps stay locked until their boss falls (8.7).

### 8.10 Save

- NVS, namespace `mafa`, binary struct + magic + version + CRC8; incompatible data is treated as corrupt and only a new game may proceed.
- **Version 2 (v0.8)**: class / level / XP (32-bit) / gold / books bitmask / potion counts / backpack (8 × {item id, count}) / 3 equipped items / unlocked area / settings toggles (auto-potion, auto-sell-white, auto-boss).
- **Version 3 (v1.0)**: same payload as v2; only the map fields are renumbered (safe zone 3 → 0, combat maps 0–2 → 1–3).
- **Version 4 (v1.2)**: extends the payload with the per-class **skill switches** (bitmask, one bit per skill of the player's class), the two **auto-potion thresholds** (red/blue %, defaults 50/30, clamped 20–80), and the **auto-sell quality set** (4-bit mask white/green/blue/purple, default white-only — the old flags byte's white bit maps onto it during migration). Defaults for everyone else: **v1/v2/v3 saves load and migrate** — v1 grants every skill whose unlock level is already reached its book; v1/v2 map ids shift into the v3 numbering; the old auto-sell-white flag becomes the white bit on or off. Old saves never lose progress.
- **Autosave points**: after every battle settlement, after store purchases, after equip/unequip. No manual save.
- Overwriting an existing save with a new game requires confirmation.

## 9. Numbers (initial P0 tables)

### 9.1 Levels and growth

See 8.2. Level cap in P0 = 15 (graduation of area 3).

### 9.2 Monster table

| Monster | Area | Level | HP | Attack | Defense | XP | Skill (below 30% HP) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Chicken | 1 | 1 | 50 | 7 | 0 | 12 | — |
| Deer | 1 | 1 | 65 | 8 | 1 | 15 | — |
| Scarecrow | 1 | 2 | 95 | 10 | 1 | 18 | Fire (1.5× attack) |
| Hook-cat | 1 | 3 | 130 | 12 | 2 | 22 | Flurry (2 hits of 0.7× attack) |
| Forest yeti | 1 | 4 | 170 | 13 | 3 | 26 | Heavy blow (1.6× attack) |
| Forest Ape (boss) | 1 | 5 | 380 | 20 | 4 | 150 | Roar (1.5× attack) |
| Skeleton | 2 | 5 | 90 | 11 | 4 | 34 | — |
| Mine rat | 2 | 6 | 80 | 12 | 2 | 38 | Flurry |
| Skeleton warrior | 2 | 7 | 130 | 14 | 6 | 44 | Heavy blow |
| Axe skeleton | 2 | 8 | 120 | 15 | 5 | 50 | Fire |
| Cave scorpion | 2 | 9 | 150 | 16 | 7 | 58 | Sting (1.2× attack, then −3 HP for 2 rounds) |
| Corpse King (boss) | 2 | 10 | 830 | 27 | 8 | 300 | Rot (1.5× attack) |
| Zuma guard | 3 | 10 | 170 | 15 | 9 | 72 | Heavy blow |
| Giant rat | 3 | 11 | 150 | 16 | 6 | 80 | Flurry |
| Black maggot | 3 | 12 | 200 | 18 | 10 | 92 | Sting |
| Contract moth | 3 | 13 | 190 | 20 | 8 | 104 | Fire |
| Zuma statue | 3 | 14 | 260 | 22 | 12 | 118 | Heavy blow |
| Zuma Overlord (boss) | 3 | 15 | 1600 | 31 | 14 | 800 | Hellfire (2.0× attack) |

Elite monsters = same-table monster ×1.5 HP ×1.2 attack, XP ×1.5. Boss numbers are calibrated by the balance simulator (PRD 4.2, v0.8 gates): a player at the suggested level with the map's T2 gear, store books + elite book 3, and 8 red / 4 blue potions should find at least one class whose win rate lands in 45–75% (no 0% matchups); battle pace 4–15 s and suggested-level grinding with a working potion economy is rarely deadly (≥5 min death interval).

### 9.3 Drops and equipment tables

Quality colors: white `#C8C8C8` / green `#5FC85F` / blue `#4FA8F2` / purple `#B06CF0` / gold `#F0C04A`.

| Area | Weapons (attack) | Armor (def + HP) | Accessories |
| --- | --- | --- | --- |
| 1 | Wood Sword +2 / Bronze Sword +4 / Iron Sword +6 | Cloth 1+5 / Fine Cloth 2+10 / Light Armor 3+15 | Wood Bead atk1 / Amber Bead atk2 def1 / Blue Jade atk3 |
| 2 | Pickaxe +8 / Fine Steel Axe +10 / Asura +14 | Skeleton Mail 4+20 / Steel Mail 5+28 / Asura Mail 7+35 | Agate Pendant atk5 / Skeleton Ring atk6 def2 / Blue Jadeite atk8 |
| 3 | Purgatory +18 / Thunder Blade +20 / **Dragon Slayer +22 (gold)** | Sky Devil Mail 9+45 / Holy War Mail 11+55 / **Overlord Mail 13+70 (gold)** | Purple Conch atk10 / Dragon Scale atk12 def3 / **Soul Chain atk15 (gold)** |

- Area n normal monsters drop only from that area's table (quality roll per 8.6); quality maps to that slot's tier in the area (area 1 white/green/blue, area 2 green/blue/purple, area 3 blue/purple/gold).
- Boss drops a random slot with a tier roll of **t2 92% / t3 8%** — which maps to "purple 92% / gold 8%" in area 3 (areas 1/2 have no purple/gold tiers in their tables, so t2/t3 resolve to green/blue and blue/purple respectively).
- Trash never rolls the gold tier: area-3 trash rolls t3 as t2 instead (v0.8).

## 10. Interaction spec

Global: UP/DOWN move the cursor, OK confirms, long-press returns to the main screen. List cursors use a `>` prefix plus highlight; the idle log auto-scrolls and needs no input. The settings page ends with a non-navigable battery readout: UP/DOWN moves the five cursor rows (v1.2 — toggles plus the two potion thresholds).

| Screen | UP/DOWN | OK | Long-press |
| --- | --- | --- | --- |
| Main menu (continue/new) | move cursor | confirm | — |
| Pick class | move | confirm | back to menu |
| Main screen (idling) | move action-menu cursor | open Backpack/Gear/Store/Map/Settings/Speed | — |
| Gear (equip + potions + skills) | move cursor over the 3 slots and 5 skill rows | on an equip slot: back to main screen; on a skill: toggle on/off (v1.2; passive/proc rows fixed) | back to main screen |
| Main screen — speed | — | cycles 1x → 2x → 4x | — |
| Backpack | move | select (equip/sell submenu, with comparison) | back to main screen |
| Store | move | buy | back to main screen |
| Map list | move | go to that map | back to main screen |
| Settings | move | toggle; on a threshold row OK enters edit — value turns gold in `<50%>` brackets, other rows dim, UP/DOWN step 10% within 20–80, OK saves; on the auto-sell row OK opens the quality picker — UP/DOWN moves over 白/绿/蓝/紫 plus 完成, OK toggles a color / saves (v1.2) | back to main screen (also leaves edit, saving) |
| Boss event prompt | move | fight / pass | — |
| Backpack-full drop prompt | move | replace / discard | pass the drop |

## 11. Copy and fonts

- Copy style: short lines with legend flavor ("You land a deadly strike!", "Obtained Bronze Sword (green)"); ≤ 15 characters per line; no long paragraphs.
- Fonts: reuse the codepoint-inventory + generator pipeline to produce `mafa_font_16` (body) / `mafa_font_20` (titles). P0 copy, deduplicated, expected ≤ 600 characters (including ASCII).
- The codepoint-check test follows every copy change (the fog glyphs-test pattern).

## 12. Memory and flash budget

- LVGL pool: ≤ 40 objects per text screen, with diff-refresh and on-demand creation; the pool stays at 96 KB — no further increase.
- Flash: font subset ≈ 100 KB; monster portraits (optional, small) + equipment icons ≈ 50 KB; code and tables ≈ 100 KB. Ample within 8 MB.
- Task stacks: keep fog's 4 KB input-task configuration; battle logic does not recurse.

## 13. Milestones and acceptance

| Milestone | Content | Acceptance |
| --- | --- | --- |
| M1 | PRD finalized | user review passed |
| M2 | `mafa_model` + host tests + **balance simulator** (≥1000 idle-battle curves) | validate.sh --static green; metrics in 4.2 met |
| M3 | Main-screen idle loop + backpack/store/map playable on device | 10 minutes of idling on hardware; equip/buy/switch-map/loot all exercised |
| M4 | Content tables + NVS save + boss events | power-cycle resume; backpack/equip/purchase all autosave |
| M5 | On-device acceptance | DoD all green |

**DoD (this chapter = M5)**:

1. ≥ 3 on-device sessions covering: new-game creation, L1→L8+, map 2 unlocked and idled, ≥ 1 boss event fought and won, ≥ 1 purple drop observed, ≥ 1 death and respawn.
2. Mechanical criteria: no freezes, no reboots, no missing glyphs (boxes) throughout; after every power cycle the progress matches pre-power-off state (counters reference review #1).
3. Experience criterion: the player voluntarily says "I want to keep idling a bit longer" (answers H1).

## 14. Future directions (P1 candidates, not P0)

- Equipment enhancement/forging, more skills and skill trees, class change.
- Elite/boss affixes, daily dungeons, idle/offline gains.
- Expanded sound pack (loot/level-up/boss short melodies), more monster portraits.
- Multiple save slots, simple achievements (first purple, first boss kill).

## 15. Revision history

- v0.1 (2026-09-28): initial draft. Positioning and loop established from the FOG MARCH on-device playtest feedback (2026-09-28): the player prefers a monster-slaying, loot-grinding progression loop.
- v0.2 (2026-09-28): aligned to the text-legend genre and the reference product (TapTap "Text Legend" / Wenzi Chuanqi, app 842887) including its player reviews: combat changed from manual turn-based to an **auto-idle log stream**; added boss kill events, auto-potion/auto-sell-white, equipment comparison on equip, sell pricing; removed the town screen and the 5-floor area structure; MMO theater moved to P1.
- v0.3 (2026-09-28): skills expanded from 1 to 3 per class (L3/L7/L12), with burn/poison DoT effects and per-class auto-cast priorities; added sell-price details; copy budget raised to 600 characters.
- v0.7 (2026-09-29): first visual pass on the UI (legend-panel style): gold header band (map + boss progress, level/battery/gold, HP/MP bars), enemy strip with its own HP bar, framed log panel, bottom action bar with full-width-glyph column alignment; the glyph pipeline now covers UI symbols (▶ cursor, … ellipsis), font subsets 223 → 231 codepoints.
- v0.8 (2026-09-29): **skills 2.0 + rebalance** from the second playtest ("too easy, no original-growth feel"): 5 skills per class with distinct forms (passive / proc / nuke / AoE / burn / heal / poison / shield / charge / pets), unlocks gated by level AND skill book (store books 1–2, elite/boss books 3–4, boss first-kill guarantee); battles spawn 1–3 monsters (deeper maps favor packs), the taoist pet taunts; front-fast back-wall XP curve (14→15 wall ≈ 45 % of total, time-to-max ≈ several idle days); white-name death penalty (1–2 backpack stacks + 10–20 % gold, gear safe); potion economy (50/40 gold, level-scaled heals); gold-tier gear boss-only (8 %); boss-kill counter 25 → 40 mobs; save v2 with v1 migration; sim gates recalibrated (battle pace 4–15 s, boss band 45–75 %, ≥1 class in band, no 0 % matchups).
- v0.9 (2026-09-29): on-device playtest found the death loop (respawn kept 0 HP, dying endlessly). Death now respawns the player in a new **safe zone** (4th map id, always open, no monsters/boss, also a voluntary rest spot) at full HP/MP; idling stays stopped until the player picks the next map themselves — the town kick becomes part of the death cost. Penalties unchanged; map-page cursor only rests on enterable rows.
- v1.0 (2026-09-29): the safe zone becomes **map id 0** and heads the map page (row "0" + safe-zone name; combat maps renumbered 1–3, making the 8.3 pack-weight table literal). New games start idling in Beech Forest at once instead of resting in town. Save bumps to **v3**: byte layout identical to v2, only the map fields migrate (town 3 → 0, combat 0–2 → 1–3); v1/v2 saves load and migrate automatically. Code replaces the `map*6+5` boss lookup with `mafa_map_boss()`; balance-sim gates unchanged and still met.
- v1.1 (2026-09-29): the header percent now shows **XP progress toward the next level** ("Lv.8 87%") instead of the battery — playtest feedback was that level-up timing was invisible, decisive on the 14→15 wall (≈45 % of total grind). At the cap the line shrinks to bare "Lv.15". The battery readout moved to the settings page as a non-navigable info row. Design mockups 03/11 re-rendered to match; font subsets regenerated for the new battery label.
- v1.2 (2026-09-29): playtest feedback round — four visibility/control gaps closed. (1) **Potion counts became visible**: a potions line on the gear page and holdings on the store rows (they previously existed only as invisible counters). (2) **Auto-potion thresholds became adjustable**: the fixed 50% HP / 30% MP triggers turned into settings rows (red line / blue line), editable in place, 20–80% in steps of 10; save v4 carries the thresholds and per-class skill switches, v1–v3 saves load with defaults. (3) **"Learned but never cast" explained**: a host probe showed the cast logic has no bug — Half-Moon Sweep fires in 100 % of multi-mob battles at L9 with the book, but books can be banked before the unlock level (0 casts at L8 despite 118 multi-mob battles) and map 1 spawns lone monsters ≈70 % of the time while AoE is crowd-only by design. The fix is visibility, not numbers: the gear page now lists all five skills with live state (always-on / on-off toggle / store price / "Lv12 boss"-style lock reason, plus an AoE tag on crowd-only skills). (4) **Skills became toggleable** (OK on a skill row; passive/proc fixed). (5) **Auto-sell became a selectable color set** (white/green/blue/purple bits, default white-only, gold never auto-sold): the settings row shows the enabled colors and OK opens an in-place quality picker; save v4 carries the 4-bit set and older saves map the old white-only flag onto it. Mockup 07 became the final gear-page design (absorbing the old 12-skills-proposal, now removed), 09/11 re-rendered, 14 added for the threshold edit state, 15 for the auto-sell picker.
- v0.6 (2026-09-28): on-device feedback added an "auto-boss" settings toggle (boss events are fought automatically instead of prompting); save contents and the settings page updated.
- v0.5 (2026-09-28): on-device feedback added a gear-overview page (action menu item 2: the three equipped slots plus a stat summary); the action menu grew to six entries.
- v0.4 (2026-09-28): M2 balance calibration finalized. Taoist rebalanced as a sustain fighter (HP 60+6, attack 12+1; Soul Fire Talisman L3 2.4×/MP14, Heal L7, Poison L12); added boss-fight rules (50% defense pierce, ±20% monster damage roll, fixed-value potions); boss stats finalized (Ape 300/16, Corpse King 400/26, Overlord 1200/22); success metric redefined as "per map at least one class win rate within 20–85%, none locked at 0%".
