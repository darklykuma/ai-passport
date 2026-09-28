<p align="right">
  <a href="PRD_MAFA_CHRONICLE.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# MAFA CHRONICLE PRD

> Document status: **Draft v0.4, awaiting review**
> Product carrier: FoloToy AI Passport / ESP32-C3 / 240 × 320 LCD / three ADC buttons (UP/DOWN/OK)
> Target branch: `feature/mafa-chronicle` (to be created)
> PRD version: v0.4
> Updated: 2026-09-28

---

## 1. Product overview

### 1.1 Product name

- Chinese name (working title, renameable): Ma Fa Zhan Ji (the exact glyphs live in the Chinese edition of this PRD)
- English UI name: `MAFA CHRONICLE`
- Internal feature name: `Mafa Chronicle` (single-player text-legend-style RPG)

### 1.2 Positioning

MAFA CHRONICLE is a **single-player idle text RPG** (text-legend style) running on the FoloToy AI Passport. The player picks one of three classes (warrior / mage / taoist), picks a map, and the character **fights monsters automatically**; combat streams past as a scrolling log. The player's decisions happen outside combat — equipping, selling, buying potions, switching to deeper maps, answering boss events — living the loop of **fight → loot → equip → idle on deeper maps**.

The screen is the classic text-legend three bands: a top status bar (level / HP / MP / gold / map), a scrolling combat log in the middle (the game's "picture"), and an action menu at the bottom. Three buttons are the entire input.

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
     │   action menu: Backpack / Store / Map / Settings / Speed            │
     │        ├─ Backpack (equip / sell / sell-all-whites)                 │
     │        ├─ Store (buy red / buy blue)                                │
     │        ├─ Map (unlocked list; switching moves the idle spot)        │
     │        ├─ Settings (auto-potion / auto-sell-white toggles)          │
     │        └─ Speed (1x / 2x / 4x)                                      │
     └─ Boss event prompt (fight / pass) ◀── kill-count trigger ───────────┘
```

## 8. Core functional requirements

### 8.1 Classes

Three classes, chosen at game start, immutable. Stats: HP / MP / attack / defense / level.

| Class | Role | Damage style |
| --- | --- | --- |
| Warrior | High HP/defense, steady damage | solid normal hits, skills woven in on fixed cooldowns |
| Mage | Highest attack, fragile | high burst at high MP cost; fast clears, expensive in potions |
| Taoist | Balanced sustain fighter | talisman damage + heal/poison sustain; survives the deepest maps |

### 8.2 Stats and growth

- XP comes only from battles; level-ups auto-allocate points (no manual allocation in P0).
- XP to next level: `next(L) = 20 + (L − 1) × 25` (linear: L1→2 costs 20, L2→3 costs 45, …).
- Per-level growth (base value + per-level increment):

| Class | HP | MP | Attack | Defense |
| --- | --- | --- | --- | --- |
| Warrior | 60 +8 | — | 10 +2 | 5 +1 |
| Mage | 40 +5 | 30 +5 | 14 +2 | 3 +1 (every 2 levels) |
| Taoist | 60 +6 | 25 +4 | 12 +1 | 4 +1 |

- MP regeneration: +2 at the end of each battle round; outside combat, HP/MP regenerate slowly per 8.8.

### 8.3 Battle (automatic)

- **Combat is fully automatic**: on a map the character attacks every 1.5 s per round (speed 1x/2x/4x), presented entirely as a scrolling log.
- **Normal attack**: `damage = max(1, floor(attack × U(0.9, 1.1)) − defense)`; U is an integer random over 0.9–1.1.
- **Critical**: 10% chance, attack ×1.5; the log marks it with a "CRIT" prefix plus a sound cue.
- **Skills auto-cast** by the priority policy in 8.4; log lines open with the skill name ("[Slash Assault] deals 21 damage!").
- **Auto-potion** (settings toggle, default on): red potion below 50% HP, blue potion below 30% MP; with potions exhausted a taoist simply sustains through.
- **Death**: HP ≤ 0 → log "You were killed by Chicken…" → full restoration, idling continues; nothing is lost (no penalty in P0).
- **Combat log**: the latest 5–6 lines scrolling; ≤ 15 characters per line. Kills/drops/level-ups/boss events use accent colors.
- Monster AI (P0): normal attack; below 30% HP, a 50% chance to use its monster skill (see table 9.2).
- **Auto-sell-white** (settings toggle, default on): white drops convert to gold at sell price immediately, never occupying the backpack; green and above go to the backpack for the player to judge.

### 8.4 Skills

- Three per class, unlocked in tiers at L3 / L7 / L12 (level-up copy announces "You learned Flame Blade!"). Once unlocked they cast fully automatically — no player action required.
- Warrior uses cooldowns (no MP); mage/taoist use MP.

| Class | Skill | Unlock | Effect | Cost |
| --- | --- | --- | --- | --- |
| Warrior | Slash Assault | L3 | 1.8 × attack | 4-round cooldown |
| Warrior | Half-Moon Sweep | L7 | 1.5 × attack, ignores defense | 6-round cooldown |
| Warrior | Flame Blade | L12 | 2.6 × attack | 8-round cooldown |
| Mage | Thunder | L3 | 2.2 × attack, ignores defense | MP 10 |
| Mage | Firewall | L7 | applies burn: 3 rounds of 0.8 × attack per round | MP 16 |
| Mage | Frost Howl | L12 | 3.0 × attack, ignores defense | MP 28 |
| Taoist | Soul Fire Talisman | L3 | 2.4 × attack | MP 14 |
| Taoist | Heal | L7 | restore 30% max HP | MP 12 |
| Taoist | Poison | L12 | poisoned: 5 rounds of −4 HP and monster defense −30% | MP 12 |

**Auto-cast policy** (each round takes the first available by priority):

- Warrior: Flame Blade > Half-Moon Sweep > Slash Assault (independent cooldowns).
- Mage: Frost Howl > Thunder > Firewall, first one affordable.
- Taoist: Heal at HP < 60% > Poison if the monster is not poisoned > Soul Fire Talisman at MP ≥ 60%.

Burn/poison and similar effects attach to a per-monster effect list, resolved each round (covered by model-layer host tests).

**Boss-fight rules** (balance-simulator calibration, PRD 4.2):
- Bosses pierce 50% of player defense — keeping high-defense warriors and glass-cannon mages in the same danger band;
- Monster damage rolls within ±20% of the nominal value, so outcomes spread smoothly instead of tipping at a budget cliff;
- Potions restore fixed amounts: red +30 HP, blue +15 MP (no percentage heals).

### 8.5 Items and equipment

- **Slots**: weapon (attack) / armor (defense + HP) / accessory (attack or HP).
- **Quality**: white (common) / green (fine) / blue (rare) / purple (epic) / gold (legendary); the color is the text color.
- **Backpack 8 slots**; identical stackable items merge; **when full**, a new drop opens a three-key prompt (replace / discard / pass) and idling pauses during the prompt.
- **Equipment comparison**: selecting a backpack item shows the equipped piece vs the new one side by side (attack/defense/HP deltas); OK equips, long-press returns.
- No level requirements in P0; equip/unequip applies immediately and triggers autosave.
- **Consumables**: red and blue potions (stackable); gold has no other use in P0 (the store sells potions only).

### 8.6 Drops

One settlement roll per battle (see 9.3):

- Gold: monster level × U(2,5).
- Potions: 10% chance of red/blue.
- Equipment: normal monsters 25% (white 70% / green 25% / blue 5%); **elites appear randomly per map (about 1 in 10 battles) and always drop, blue 30%**; **the boss event always drops (purple 90% / gold 10%)**.
- **Sell prices** (used by manual selling and auto-sell-white): white 10 / green 30 / blue 80 / purple 200 / gold 500 gold.

### 8.7 Maps and unlocking

Three idle maps in P0, unlocked linearly (boss-event victory unlocks the next):

| Map | Suggested level | Monster theme | Boss |
| --- | --- | --- | --- |
| Beech Forest | L1–5 | chicken / deer / scarecrow / hook-cat / forest yeti | Forest Ape |
| Abandoned Mine | L5–10 | skeleton / skeleton warrior / mine rat / axe skeleton / cave scorpion | Corpse King |
| Zuma Temple | L10–15 | zuma guard / zuma statue / black maggot / contract moth / giant rat | Zuma Overlord |

- On the current map monsters spawn randomly by weight, fought one after another automatically.
- Boss event: triggers after every 25 kills on the map; repeatable (8.6 boss drop roll is independent each time).
- Monsters scale slightly with player level (area base + 5% per level difference).

### 8.8 Store

- No separate town screen in P0: the main screen is the camp, and the store is one action-menu entry. HP/MP regenerate slowly while not in combat (+5/s).
- Store: red potion 20 gold, blue potion 25 gold; gold cap 9999.

### 8.9 Death

HP ≤ 0: log "You were killed by Chicken…" → full restoration, idling continues on the current map; nothing lost; the boss kill counter resets (no penalty in P0, to avoid frustration).

### 8.10 Save

- NVS, namespace `mafa`, binary struct + magic + version + CRC8; incompatible data is treated as corrupt and only a new game may proceed.
- Contents: class / level / XP / gold / potion counts / backpack (8 × {item id, count}) / 3 equipped items / unlocked area.
- **Autosave points**: after every battle settlement, after store purchases, after equip/unequip. No manual save.
- Overwriting an existing save with a new game requires confirmation.

## 9. Numbers (initial P0 tables)

### 9.1 Levels and growth

See 8.2. Level cap in P0 = 15 (graduation of area 3).

### 9.2 Monster table

| Monster | Area | Level | HP | Attack | Defense | XP | Skill (below 30% HP) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Chicken | 1 | 1 | 15 | 4 | 0 | 6 | — |
| Deer | 1 | 1 | 20 | 5 | 1 | 8 | — |
| Scarecrow | 1 | 2 | 28 | 7 | 1 | 10 | Fire (1.5× attack) |
| Hook-cat | 1 | 3 | 35 | 8 | 2 | 12 | Flurry (2 hits of 0.7× attack) |
| Forest yeti | 1 | 4 | 45 | 9 | 3 | 15 | Heavy blow (1.6× attack) |
| Forest Ape (boss) | 1 | 5 | 300 | 16 | 4 | 90 | Roar (1.5× attack) |
| Skeleton | 2 | 5 | 40 | 10 | 3 | 15 | — |
| Mine rat | 2 | 6 | 35 | 12 | 2 | 16 | Flurry |
| Skeleton warrior | 2 | 7 | 60 | 13 | 5 | 20 | Heavy blow |
| Axe skeleton | 2 | 8 | 55 | 15 | 4 | 24 | Fire |
| Cave scorpion | 2 | 9 | 70 | 16 | 6 | 28 | Sting (1.2× attack, then −3 HP for 2 rounds) |
| Corpse King (boss) | 2 | 10 | 400 | 26 | 7 | 180 | Rot (1.5× attack) |
| Zuma guard | 3 | 10 | 80 | 18 | 7 | 30 | Heavy blow |
| Giant rat | 3 | 11 | 70 | 20 | 5 | 32 | Flurry |
| Black maggot | 3 | 12 | 95 | 21 | 8 | 38 | Sting |
| Contract moth | 3 | 13 | 90 | 24 | 6 | 44 | Fire |
| Zuma statue | 3 | 14 | 130 | 26 | 10 | 52 | Heavy blow |
| Zuma Overlord (boss) | 3 | 15 | 1200 | 22 | 12 | 380 | Hellfire (2.0× attack) |

Elite monsters = same-table monster ×1.5 HP ×1.2 attack, XP ×1.5. Boss numbers are calibrated by the balance simulator (PRD 4.2): a player at the suggested level with the map's T1 gear and 8 red / 4 blue potions should find at least one class whose win rate lands in 20–85%, and no class locked out entirely (0% or 100% is acceptable but must be present).

### 9.3 Drops and equipment tables

Quality colors: white `#C8C8C8` / green `#5FC85F` / blue `#4FA8F2` / purple `#B06CF0` / gold `#F0C04A`.

| Area | Weapons (attack) | Armor (def + HP) | Accessories |
| --- | --- | --- | --- |
| 1 | Wood Sword +2 / Bronze Sword +4 / Iron Sword +6 | Cloth 1+5 / Fine Cloth 2+10 / Light Armor 3+15 | Wood Bead atk1 / Amber Bead atk2 def1 / Blue Jade atk3 |
| 2 | Pickaxe +8 / Fine Steel Axe +10 / Asura +14 | Skeleton Mail 4+20 / Steel Mail 5+28 / Asura Mail 7+35 | Agate Pendant atk5 / Skeleton Ring atk6 def2 / Blue Jadeite atk8 |
| 3 | Purgatory +18 / Thunder Blade +20 / **Dragon Slayer +22 (gold)** | Sky Devil Mail 9+45 / Holy War Mail 11+55 / **Overlord Mail 13+70 (gold)** | Purple Conch atk10 / Dragon Scale atk12 def3 / **Soul Chain atk15 (gold)** |

- Area n normal monsters drop only from that area's table (quality roll per 8.6); quality maps to that slot's tier in the area (area 1 white/green/blue, area 2 green/blue/purple, area 3 blue/purple/gold).
- Boss drops a random slot with a tier roll of **t2 90% / t3 10%** — which maps to "purple 90% / gold 10%" in area 3 (areas 1/2 have no purple/gold tiers in their tables, so t2/t3 resolve to green/blue and blue/purple respectively).

## 10. Interaction spec

Global: UP/DOWN move the cursor, OK confirms, long-press returns to the main screen. List cursors use a `>` prefix plus highlight; the idle log auto-scrolls and needs no input.

| Screen | UP/DOWN | OK | Long-press |
| --- | --- | --- | --- |
| Main menu (continue/new) | move cursor | confirm | — |
| Pick class | move | confirm | back to menu |
| Main screen (idling) | move action-menu cursor | open Backpack/Store/Map/Settings/Speed | — |
| Main screen — speed | — | cycles 1x → 2x → 4x | — |
| Backpack | move | select (equip/sell submenu, with comparison) | back to main screen |
| Store | move | buy | back to main screen |
| Map list | move | go to that map | back to main screen |
| Settings | move | toggle (auto-potion / auto-sell-white) | back to main screen |
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
- v0.4 (2026-09-28): M2 balance calibration finalized. Taoist rebalanced as a sustain fighter (HP 60+6, attack 12+1; Soul Fire Talisman L3 2.4×/MP14, Heal L7, Poison L12); added boss-fight rules (50% defense pierce, ±20% monster damage roll, fixed-value potions); boss stats finalized (Ape 300/16, Corpse King 400/26, Overlord 1200/22); success metric redefined as "per map at least one class win rate within 20–85%, none locked at 0%".
