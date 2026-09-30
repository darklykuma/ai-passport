<p align="right">
  <a href="PRD_MAFA_CHRONICLE.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# MAFA CHRONICLE PRD

> Document status: **Draft v1.5, awaiting review**
> Product carrier: FoloToy AI Passport / ESP32-C3 / 240 × 320 LCD / three ADC buttons (UP/DOWN/OK)
> Target branch: `feature/mafa-chronicle`
> PRD version: v1.5
> Updated: 2026-09-30

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
2. Three selectable classes, **7 auto-cast skills each at the original 1.76 learn levels** (v1.4, chapter 8.4).
3. **Seven escalating maps along the original's leveling route** (v1.4, chapter 8.7) — Beech Province, Orc Tomb, Stone Tomb, Woma Temple, Death Valley, Zuma Temple, Red Moon Canyon; 26 floor-boss checkpoints; the level cap is 40.
4. Equipment on the **1.76 paper doll minus candle and amulet — 8 positions over 6 slot types** (weapon / helmet / armor / necklace / bracelets / rings ×2), five quality tiers, with class-affine **attack / magic / taoism** stat lines (Holy-War favors attack, Archmage magic, Celestial taoism); drops and the store both supply gear; auto-sell-set and auto-potion on by default.
5. NVS persistence across power cycles, with autosave points covering every state-changing action (counters the reference's worst review).

### 4.2 Success metrics

- On-device DoD fully green (chapter 13).
- Host simulator (per floor, at the suggested level with that floor's arrival kit, store books 1–3 and 8 red / 4 blue potions):
  - battle pace 4–15 s;
  - suggested-level idle death interval ≥ 5 min;
  - **wall floors** (each map's last floor): at least one class in 45–75%, no class at 0%;
  - **corridor floors**: pace and death gates hold for every class, plus "no free wins" (at least one class ≤ 90%).

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
     │   action menu (2×4, font 16): Backpack / Gear / Skills / Store /      │
     │                               Map / Settings / Speed (8th cell spare) │
     │        ├─ Backpack (equip / sell / sell-all-whites)                   │
     │        ├─ Gear (8 paper-doll slots + stats + potions)                 │
     │        ├─ Skills (7 class skills with state, on/off toggles)          │
     │        ├─ Store (potions + 3 level-gated class books)                 │
     │        ├─ Map (safe zone + 7 combat maps → floor lists)               │
     │        ├─ Settings (toggles + potion thresholds + battery)            │
     │        └─ Speed (1x / 2x / 4x)                                        │
     └─ Boss event prompt (fight / pass) ◀── kill-count trigger ────────────┘
```

## 8. Core functional requirements

### 8.1 Classes

Three classes, chosen at game start, immutable. Stats: HP / MP / attack / magic / taoism / defense / level (v1.5: the original's three stat lines — skills and gear each resolve on their own line).

| Class | Role | Damage style |
| --- | --- | --- |
| Warrior | High HP/defense, steady damage | solid normal hits, procs and charge, AoE on packs |
| Mage | Highest magic, fragile | nukes + AoE burn, Magic Shield survival; fast clears, expensive in potions |
| Taoist | Sustain fighter driven by taoism | pet tank + talisman damage + heal/poison sustain; survives the deepest maps |

### 8.2 Stats and growth

- XP comes only from battles; level-ups auto-allocate points (no manual allocation in P0). **Level cap 40 (v1.4)** — the cap covers every 1.76 skill (highest unlock 35) plus a few levels of full-kit play.
- XP to next level follows a **front-fast, back-wall curve** shaped after the original 1.76 curve (quick newbie pace, ~×1.3 compounding through the mid-game, one long wall at the cap). The 39-entry table (v1.4) summarizes by band:

| Band | Shape | XP for the band's last step |
| --- | --- | --- |
| L1–7 | near-linear (100 → 1800) | the original's quick newbie phase |
| L8–20 | ×1.3 compounding | L20→21 = 92 000 |
| L21–30 | ×1.25 compounding | L30→31 = 610 000 |
| L31–38 | ×1.28 steepening | L38→39 = 2 130 000 |
| L39→40 | **the wall** | 12 000 000 (≈46 % of the ≈26.3 M total) |

- **Three stat lines (v1.5)**: each class has one main line — the warrior scales **attack**, the mage **magic**, the taoist **taoism**; skills (8.4) and gear stat lines (8.5) resolve on that line, matching the original's three-class stat system. Mage/taoist normal attacks stay low-attack (weapon-driven); the warrior has no magic/taoism line.
- Per-level growth (base value + per-level increment; mage HP +1/level since v1.4 — the sim showed the 1.76-route late maps starving the old 40+5 pool; the warrior carries a small mana pool since v1.5 — Flame Blade / Half-Moon / Savage Charge cost MP):

| Class | HP | MP | Attack | Magic | Taoism | Defense |
| --- | --- | --- | --- | --- | --- | --- |
| Warrior | 60 +8 | 10 +1 | 11 +2 | — | — | 5 +1 |
| Mage | 40 +6 | 30 +5 | 10 +1 | 14 +2 | — | 3 +1 (every 2 levels) |
| Taoist | 60 +6 | 25 +4 | 10 +1 | — | 12 +1 | 4 +1 |

- MP regeneration: +2 at the end of each battle round; outside combat, HP/MP regenerate slowly per 8.8.

### 8.3 Battle (automatic)

- **Combat is fully automatic**: on a map the character attacks every 1.5 s per round (speed 1x/2x/4x), presented entirely as a scrolling log.
- **Packs (v0.8)**: normal battles spawn **1–3 monsters** of the current map (weights shift to 3 mobs on deeper maps: map 1 ≈ 70/30/0, map 2 ≈ 40/40/20, maps 3+ ≈ 20/40/40). Spawn species are drawn from the player's level band (mob level ≤ player level + 2; full map pool as fallback). The player strikes the first living mob; AoE skills hit every living mob; **each living monster acts every round** (a stunned monster skips its turn, 8.4). Boss battles stay 1v1.
- **Normal attack**: `damage = max(1, floor(attack × U(0.9, 1.1)) − defense)`; U is an integer random over 0.9–1.1.
- **Critical**: 10% chance, attack ×1.5; the log marks it with a "CRIT" prefix.
- **Skills auto-cast** by the priority policy in 8.4; log lines open with the skill name.
- **Auto-potion** (settings toggle, default on): red potion when HP falls below the **red line**, blue potion when MP falls below the **blue line** (v1.2: both thresholds are adjustable on the settings page, 20–80% in steps of 10; defaults 50% HP / 30% MP). Potions restore level-scaled flat amounts (8.8) so one potion always answers roughly one mob hit, early and late.
- **Death**: HP ≤ 0 → log "You were killed by…" → respawn in the safe zone at full HP/MP, the player picks the next map themselves; death penalties per 8.9.
- **Combat log**: the latest 6 lines scrolling; kills/drops/level-ups/boss events use accent colors. Monster packs show as "▶multi-hook-cat×3" with a pooled HP bar; the pet announces itself through log lines.
- Monster AI (P0): normal attack; below 30% HP, a 50% chance to use its monster skill (see table 9.2).
- **Auto-sell (v1.2, replaces auto-sell-white)**: a **selectable set of qualities** converts its drops to gold at sell price immediately, never occupying the backpack; everything else goes to the backpack for the player to judge. Pickable: white / green / blue / purple — one bit each, default **white only** (the old behavior). **Gold is never auto-sold**: legendary drops always reach the player. The settings row summarizes the set in Chinese on device ("auto-sell: white"); OK opens an in-place picker listing the four colors in their quality colors plus a done row (mockup 15).

### 8.4 Skills

- **Seven per class at the original's real 1.76 learn levels (v1.4)** — the table below uses the community-verified 1.76 skill tables; Sun-Chasing Sword is the one post-1.76 skill (user-approved exception). Skills unlock by **level AND skill book** (8.5): skill 0 of each class is free, skills 1–3 are store books, skills 4–6 drop only. Once unlocked they cast fully automatically.
- **Skills resolve on their stat line (v1.5)**: warrior skills scale attack, mage skills magic; the taoist's talisman / poison / heal / summons scale taoism (heal = 25% max HP + taoism; poison ticks for 5 + taoism/5; pet stats grow with level and taoism). **The warrior carries a small mana pool since v1.5** (the original's warrior has mana and pays for his active sword skills — community values: Flame Blade ≈8 / Half-Moon ≈5 / Savage Charge ≈3): mage/taoist are MP-driven, the warrior is MP + cooldown gated. Two forms joined the roster (v1.4): **stun** (Savage Charge) and **armor** (Holy Armor — defense +40% for 4 rounds).
- **Skill state is always visible on the skill page (v1.4)** — its own action-menu cell since the paper doll grew to 8 slots: all seven class skills with their live state — "always on" (passive/proc), "on/off" (active; OK toggles and saves), "store N gold" (buyable book), or the lock reason "Lv35" / "elite/boss" (level gate first, then the source; skill 0 needs no book, so its locked row reads "Lv X auto-learned"). A dropped book may be **banked before its unlock level**; the skill activates at the level-up that reaches the gate — the skill page is what makes that waiting state visible.
- **Active skills can be switched off (v1.2)**: OK on a skill row toggles it (passive/proc rows are fixed); the auto-cast policy skips switched-off skills. Switching every active skill off leaves plain attacks — a legitimate choice, not an error. OK on a fixed or not-yet-learned row no longer falls silent (v1.4): the bottom help panel temporarily shows the reason — level gate, store book, elite/boss drop, or passive always-on — until the cursor moves or the page reopens.
- **AoE forms are crowd-only by design**: Half-Moon Sweep / Frost Howl / Chain Lightning / Explosive Flame cast only with **2+ living monsters**. The skill page's "AoE" tag is where that condition lives; castable rows carry it next to the on/off state, while lock rows show only the gate and source (tag + gate + source cannot fit the row).

| Class | Skill | Unlock | Book | Effect | Cost |
| --- | --- | --- | --- | --- | --- |
| Warrior | Basic Swordsmanship | L7 | — | passive: normal attack ×1.1 | — |
| Warrior | Slash Assault | L19 | store 300 | proc: 20% on a normal hit, the strike lands twice | — |
| Warrior | Assassination | L25 | store 600 | 1.4 × attack, ignores defense | 5-round cooldown |
| Warrior | Half-Moon Sweep | L28 | store 900 | 0.9 × attack to **every** monster | MP 5, 6-round cooldown |
| Warrior | Savage Charge | L30 | elite/boss drop | 1.3 × attack + the target is **stunned** 1 round | MP 3, 8-round cooldown |
| Warrior | Flame Blade | L35 | elite/boss drop | charge: the next normal attack hits ×2.2 | MP 8, 8-round cooldown |
| Warrior | Sun-Chasing Sword | L38 | elite/boss drop | 2.6 × attack, ignores defense | 10-round cooldown |
| Mage | Fire Ball | L7 | — | 1.6 × attack | MP 8 |
| Mage | Thunder | L17 | store 300 | 2.2 × attack, ignores defense | MP 14 |
| Mage | Explosive Flame | L22 | store 600 | 1.2 × attack to **every** monster | MP 20 |
| Mage | Firewall | L24 | store 900 | burn **all** monsters: 3 rounds of 0.6 × attack per round | MP 18 |
| Mage | Chain Lightning | L30 | elite/boss drop | 1.5 × attack to **every** monster, ignores defense | MP 26 |
| Mage | Magic Shield | L31 | elite/boss drop | damage taken −40% for 4 rounds | MP 16 |
| Mage | Frost Howl | L35 | elite/boss drop | 1.6 × attack to **every** monster, ignores defense | MP 30 |
| Taoist | Heal | L7 | — | restore 25% max HP + taoism (casts below 60% HP) | MP 12 |
| Taoist | Spirit Force | L9 | store 300 | passive: normal attack ×1.15 | — |
| Taoist | Poison | L14 | store 600 | poisoned: 5 rounds of −(5+taoism/5) HP and monster defense −30% | MP 12 |
| Taoist | Soul Fire Talisman | L18 | store 900 | 2.4 × attack | MP 14 |
| Taoist | Summon Skeleton | L19 | elite/boss drop | pet tank (see below) | MP 20, 6-round cooldown |
| Taoist | Holy Armor | L25 | elite/boss drop | defense +40% for 4 rounds | MP 18, 8-round cooldown |
| Taoist | Summon Divine Beast | L35 | elite/boss drop | replaces the skeleton with a stronger pet | MP 30, 6-round cooldown |

**Store book gate (v1.4)**: the store **refuses to sell a book below its learn level** — an under-level row dims with its "Lv X" hint and OK logs "level too low". Elites and bosses still hand books out regardless of level (banking stays a drop perk). Store prices stepped to 300 / 600 / 900 with the wider shelf.

**Pets (taoist identity)**: a battle-side entity (HP/attack/defense scaling with player level **and taoism**, v1.5; skeleton = 30+9×level+taoism HP, divine beast = 45+12×level+1.5×taoism HP). While alive it **taunts**: every monster swing hits the pet instead of the player. The pet attacks the first living mob each round. When it falls there is a 6-round wait before re-summoning; monster STING poison only applies when a hit lands on the player.

**Auto-cast policy** (each round takes the first available by priority; skills switched off on the skill page are skipped):

- Warrior: Flame Blade charge > Sun-Chasing Sword > Savage Charge > Half-Moon Sweep (2+ monsters) > Assassination.
- Mage: Magic Shield (none active and HP < 70%) > Frost Howl (2+) > Chain Lightning (2+) > Firewall (not already burning) > Explosive Flame (2+) > Thunder > Fire Ball.
- Taoist: Heal at HP < 60% > Divine Beast (pet down, cooldown ready) > Skeleton > Poison if the target is not poisoned > Holy Armor (none up and HP < 70%) > Soul Fire Talisman with MP ≥ 30%.

Burn/poison/stun/armor effects attach to per-monster and per-battle effect lists, resolved each round (covered by model-layer host tests).

**Boss-fight rules** (balance-simulator calibration, PRD 4.2):
- Bosses pierce 50% of player defense — keeping high-defense warriors and glass-cannon mages in the same danger band;
- Monster damage rolls within ±20% of the nominal value, so outcomes spread smoothly instead of tipping at a budget cliff;
- Potions restore flat amounts scaled by level: red +30 +2/level HP, blue +15 +1/level MP (one potion ≈ one mob hit at any stage).

### 8.5 Items and equipment

- **Paper doll (v1.4)**: the 1.76 equipment panel minus candle and amulet — **8 positions over 6 slot types**: weapon / helmet / armor / necklace / **twin bracelets** / **twin rings**. Equipping a bracelet or ring takes the **free twin first**; only when both twins are worn does the left twin get replaced (the returned piece goes back to the backpack).
- **Quality**: white (common) / green (fine) / blue (rare) / purple (epic) / gold (legendary); the color is the text color.
- **Backpack 8 slots**; identical stackable items merge; **when full**, a new drop opens a three-key prompt (replace / discard / pass) and idling pauses during the prompt.
- **Gear stat lines (v1.5)**: every item carries attack/magic/taoism/defense/HP lines assigned by the original's class ownership — Holy-War favors attack, Archmage magic, Celestial taoism, and the weapon slot splits into three class lines (the Verdict / Bone-Jade / Dragon-Pattern trinity each to its class); map 1–3 pieces are neutral and serve all three lines. The drop roll favors the player's own line (~60%), so farming your class's gear is the norm.
- **Equipment comparison**: selecting a backpack item shows the equipped piece vs the new one side by side (five deltas — attack/magic/taoism/defense/HP, v1.5 — against the position the item would land in); OK equips, long-press returns.
- No level requirements in P0; equip/unequip applies immediately and triggers autosave.
- **Consumables**: red and blue potions (stackable). Counts are visible on the **gear page** (v1.2, a "red potion ×3, blue potion ×2" line) and ride the store rows (v1.2), so a purchase is confirmed at a glance.
- **Skill books (v0.8, v1.4)**: not backpack items — a bitmask in the save. Books 1–3 per class are sold in the store **behind the level gate** (8.4); books 4–6 come only from elite drops (≈20%) and a **boss first-kill guarantee** (skill 4 first, then 5, then 6). Dropped books may be banked before their unlock level; the skill casts only from that level on, and the skill page shows the waiting state (8.4).

### 8.6 Drops

One settlement roll per killed monster (see 9.3):

- Gold: monster level × U(2,5) — sized so a mid-map kill funds about one potion.
- **Potions (v1.5)**: trash drops a Healing / Mana potion **15%** of the time (60% red / 40% blue), straight into the counters (never the backpack, capped at 99), logged as "obtained a Healing Potion"; the store remains the primary supply (drops target ~1/3 of consumption).
- Equipment: normal monsters **10%** (v1.5, was 20%; tier 1 70% / tier 2 26% / tier 3 4%); **elites** (about 1 in 10 battles) always drop (tier 2 70% / tier 3 30%); **the boss always drops (tier 2 92% / tier 3 8%)**. The slot roll covers all **6 slot types (v1.5 fix: rings could previously never drop)**.
- **Gold-tier gear is boss-only (8%)** — trash and elites stop at the map's top non-gold tier. Maps 5–7 carry gold rows; **trash on those maps rolls tier 3 as tier 2**, so Dragon Slayer and the Red-Moon-set pieces stay a chase.
- **Sell prices** (used by manual selling and the auto-sell set): white 10 / green 30 / blue 80 / purple 200 / gold 500 gold.

### 8.7 Maps and unlocking

**Seven combat maps along the original 1.76 leveling route (v1.4)**; id 0 is the safe zone (8.9), listed first on the map page. Each combat map is a floor ladder (v1.3): **one boss checkpoint per floor**, and the next map opens only through the previous map's last-floor boss.

| Map | Floors | Suggested level | Monster theme | Last-floor boss |
| --- | --- | --- | --- | --- |
| 1 Beech Province | 2 | L1–6 | chicken / deer / scarecrow / hook-cat / rake-cat / half-orc | **Half-orc Chieftain** |
| 2 Orc Tomb | 3 | L7–13 | skeleton / cave maggot / skeleton warrior / axe skeleton | **Skeleton Spirit** |
| 3 Stone Tomb | 4 | L14–21 | red boar / black boar / scorpion snake / wedge moth | **Corpse King** |
| 4 Woma Temple | 3 | L22–27 | woma warrior / woma fighter / woma guard | **Woma Overlord** |
| 5 Death Valley | 4 | L28–33 | centipede / black maggot / pincer worm | **Evil Pincer Worm** |
| 6 Zuma Temple | 7 | L34–38 | zuma guard / zuma archer / zuma statue | **Zuma Overlord** |
| 7 Red Moon Canyon | 3 | L38–40 | moon spider / sky wolf spider / two-head titan / two-head blood fiend | **Red Moon Demon** (the graduation fight) |

26 floor-boss checkpoints in total (2+3+4+3+4+7+3). Corridor bosses reuse roster names at boss scale (the original's same-mob tiered pattern); names are verified against the original's mob lists (see the Chinese edition for the full roster and sources).

- Floor progression: **40 kills** trigger the current floor's boss (a 3-mob battle counts 3). Victory opens **and enters** the next floor of the same map (gold log line); the last floor's boss unlocks the next map (gold log line). On the top floor the boss event stays repeatable (8.6 boss drop roll runs each time).
- Floor selection (v1.3): OK on an unlocked combat map opens a **floor list** — one row per open floor (with that floor's boss name) plus a back row, cursor resting on the deepest floor (mockup 16). The player may enter any reached floor — e.g. idling overnight on a low-risk shallow floor. Long-press backs out of the floor list to the map list; the safe zone still enters directly.
- Re-entering a map (from the menu or after death) always lands on its **deepest open floor**; death never demotes floors (8.9).
- Spawns use a **cumulative floor pool** (v1.3): a floor fights its own trash plus every shallower floor's rows — deeper floors get busier, dungeon style — still filtered by the level band (8.3).
- Monsters scale slightly with player level (area base + 5% per level difference, never down).

### 8.8 Store

- No separate town screen in P0: the main screen is the camp, and the store is one action-menu entry. HP/MP regenerate slowly while not in combat (+10/s).
- Store (retitled from "potion shop" since v1.4 — with a three-book shelf the old title stopped being honest): red potion **50** gold (+30+2/level HP), blue potion **40** gold (+15+1/level MP), and the **three** store books of the player's class (300 / 600 / 900 gold); gold cap 9999. Rows show the potion stack already held (a "red potion 50 gold ×3" row); a book whose unlock level is above the player's dims with its "Lv X" hint (v1.2) and — since v1.4 — **cannot be bought** until the level is reached (8.4).
- Potion economy (v0.8): a mid-map kill funds roughly one potion — supply is a decision, not a given.

### 8.9 Death and the safe zone

HP ≤ 0: log "You were killed by…" → the player respawns in the **safe zone** at full HP/MP (v0.9) and idling stops until they pick the next map from the map page themselves — getting kicked back to town is part of the death cost. The boss kill counter resets, and the white-name PvE penalty from the original applies (v0.8):

- **Backpack**: 1–2 random stacks are lost permanently (no corpse run in an idle game);
- **Gold**: 10–20% of carried gold is lost;
- **Equipped gear is safe** (too harsh for a single-player idle game; potions are safe too).

The safe zone (v0.9) is map id 0 and heads the map page (v1.0): always open, no monsters, no boss; combat maps stay locked until the previous map's last-floor boss falls (8.7, v1.3 floors). **Entering the safe zone restores full HP/MP (v1.4)** — death respawn and a voluntary walk home heal the same, making the town a real rest spot; the log answers with "HP/MP fully restored".

### 8.10 Save

- NVS, namespace `mafa`, binary struct + magic + version + CRC8; incompatible data is treated as corrupt and only a new game may proceed.
- **Version 2 (v0.8)**: class / level / XP (32-bit) / gold / books bitmask / potion counts / backpack (8 × {item id, count}) / 3 equipped items / unlocked area / settings toggles (auto-potion, auto-sell-white, auto-boss).
- **Version 3 (v1.0)**: same payload as v2; only the map fields are renumbered (safe zone 3 → 0, combat maps 0–2 → 1–3).
- **Version 4 (v1.2)**: extends the payload with the per-class **skill switches** (bitmask, one bit per skill of the player's class), the two **auto-potion thresholds** (red/blue %, defaults 50/30, clamped 20–80), and the **auto-sell quality set** (4-bit mask white/green/blue/purple, default white-only — the old flags byte's white bit maps onto it during migration).
- **Version 5 (v1.3)**: extends the payload with the **per-map deepest-open-floor** bytes (one per combat map, 1-based, range-checked against the floor table of 8.7; a deeper map may only be open when the previous map is fully climbed). Defaults for everyone else: **v1–v4 saves load and migrate** — v1 grants every skill whose unlock level is already reached its book; v1/v2 map ids shift into the v3 numbering; the old auto-sell-white flag becomes the white bit on or off; every map the player has **left behind** counts as fully climbed, while the current top map's floor ladder re-climbs from floor 1 (that ladder is the v1.3 content). Old saves never lose progress.
- **Version 7 (v1.5, stats 2.0)**: the payload is byte-identical to v6 (57 bytes); the version byte now marks the **item-table generation** — the gear table became a 252-row triad table and every id renumbered. v1–v6 saves load and migrate: a v6 save's old-id loadout is replaced by a band-and-class-line starter kit (level, XP, gold, books, settings, and floors all survive); v1–v5 run the v6 migration first, then the same kit swap.
- **Version 6 (v1.4, 1.76 alignment)**: books bitmask widened to 32 bits (21 skill bits), the paper doll grows to **8 equipped positions**, the floor array to **7 maps**, map/unlocked fields to 3 bits each, and `skills_off` to 7 bits — a 57-byte payload. v1–v5 saves load and migrate: old combat maps keep ids 1–3 **by content band** (forest→Beech Province, mine→Orc Tomb, the old L10–15 band→Stone Tomb; Zuma Temple itself is the new map 6 and is re-earned through Woma/Death Valley), old ladders clamp to the new floor tables, old book bits re-grant by the **new** unlock levels, carried XP clamps below the new curve, and the old 3-slot loadout is replaced by a band-appropriate tier-2 starter kit (the item table changed wholesale).
- **Autosave points**: after every battle settlement, after store purchases, after equip/unequip. No manual save.
- Overwriting an existing save with a new game requires confirmation.

## 9. Numbers (initial P0 tables)

### 9.1 Levels and growth

See 8.2. Level cap = 40 (v1.4; the L39→40 wall is the graduation event).

### 9.2 Monster table

| Monster | Map | Floor | Level | HP | Attack | Defense | XP | Skill (below 30% HP) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Chicken | 1 | 1 | 1 | 50 | 7 | 0 | 12 | — |
| Deer | 1 | 1 | 1 | 65 | 8 | 1 | 15 | — |
| Scarecrow | 1 | 1 | 2 | 95 | 10 | 1 | 18 | Fire (1.5× attack) |
| Hook-cat | 1 | 1 | 3 | 130 | 12 | 2 | 22 | Flurry (2 hits of 0.7× attack) |
| Rake-cat | 1 | 2 | 3 | 140 | 13 | 2 | 26 | Flurry |
| Half-orc | 1 | 2 | 4 | 200 | 15 | 3 | 30 | Heavy blow (1.6× attack) |
| Half-orc Warrior (boss) | 1 | 1 | 4 | 450 | 21 | 3 | 120 | Heavy blow |
| Half-orc Chieftain (boss) | 1 | 2 | 5 | 520 | 26 | 4 | 150 | Roar (1.5× attack) |
| Skeleton | 2 | 1 | 7 | 160 | 16 | 5 | 85 | — |
| Cave maggot | 2 | 1 | 8 | 155 | 17 | 4 | 95 | Sting (1.2× attack, then −3 HP for 2 rounds) |
| Skeleton warrior | 2 | 2 | 9 | 190 | 19 | 6 | 110 | Heavy blow |
| Axe skeleton | 2 | 2 | 10 | 175 | 20 | 5 | 125 | Fire |
| Skeleton warrior (boss) | 2 | 1 | 8 | 730 | 31 | 6 | 280 | Heavy blow |
| Axe skeleton (boss) | 2 | 2 | 10 | 830 | 34 | 7 | 340 | Fire |
| Skeleton Spirit (boss) | 2 | 3 | 12 | 1100 | 40 | 8 | 420 | Flurry |
| Red boar | 3 | 1 | 14 | 195 | 26 | 9 | 280 | Heavy blow |
| Black boar | 3 | 1 | 15 | 210 | 28 | 10 | 320 | Heavy blow |
| Scorpion snake | 3 | 2 | 16 | 230 | 29 | 11 | 360 | Sting |
| Wedge moth | 3 | 2 | 17 | 215 | 30 | 9 | 390 | Fire |
| Zombie (boss) | 3 | 1 | 14 | 940 | 50 | 10 | 520 | — (an HP wall) |
| White Boar (boss) | 3 | 2 | 16 | 1100 | 44 | 11 | 650 | Roar |
| Scorpion snake (boss) | 3 | 3 | 17 | 1200 | 50 | 12 | 720 | Sting |
| Corpse King (boss) | 3 | 4 | 18 | 1365 | 51 | 12 | 850 | Roar |
| Woma warrior | 4 | 1 | 22 | 325 | 38 | 14 | 700 | Heavy blow |
| Woma fighter | 4 | 1 | 23 | 345 | 40 | 13 | 750 | Flurry |
| Woma guard | 4 | 2 | 24 | 390 | 42 | 16 | 820 | Heavy blow |
| Woma warrior (boss) | 4 | 1 | 22 | 1300 | 60 | 15 | 1400 | Heavy blow |
| Woma guard (boss) | 4 | 2 | 24 | 1430 | 62 | 16 | 1600 | Heavy blow |
| Woma Overlord (boss) | 4 | 3 | 26 | 1755 | 68 | 18 | 2200 | Hellfire (2.0× attack) |
| Centipede | 5 | 1 | 28 | 360 | 50 | 18 | 1500 | Flurry |
| Black maggot | 5 | 1 | 29 | 380 | 52 | 19 | 1600 | Sting |
| Pincer worm | 5 | 2 | 30 | 410 | 54 | 20 | 1700 | Heavy blow |
| Centipede (boss) | 5 | 1 | 28 | 1560 | 84 | 20 | 3000 | Flurry |
| Pincer worm (boss) | 5 | 2 | 30 | 1665 | 78 | 21 | 3300 | Heavy blow |
| Black maggot (boss) | 5 | 3 | 31 | 1755 | 90 | 22 | 3600 | Sting |
| Evil Pincer Worm (boss) | 5 | 4 | 33 | 2015 | 86 | 24 | 4400 | Roar |
| Zuma guard | 6 | 1 | 34 | 440 | 54 | 24 | 3300 | Heavy blow |
| Zuma archer | 6 | 2 | 35 | 425 | 55 | 22 | 3400 | Sting |
| Zuma statue | 6 | 3 | 36 | 495 | 58 | 26 | 3600 | Heavy blow |
| Zuma guard (boss I) | 6 | 1 | 34 | 1650 | 90 | 25 | 5200 | Heavy blow |
| Zuma archer (boss) | 6 | 2 | 35 | 1700 | 100 | 26 | 5600 | Sting |
| Zuma guard (boss II) | 6 | 3 | 36 | 1750 | 87 | 27 | 6000 | Heavy blow |
| Zuma statue (boss I) | 6 | 4 | 36 | 1700 | 90 | 28 | 6500 | Heavy blow |
| Zuma guard (boss III) | 6 | 5 | 37 | 1900 | 99 | 29 | 7000 | Roar |
| Zuma statue (boss II) | 6 | 6 | 37 | 1850 | 94 | 30 | 7500 | Heavy blow |
| Zuma Overlord (boss) | 6 | 7 | 38 | 2150 | 99 | 32 | 9000 | Hellfire |
| Moon spider | 7 | 1 | 38 | 430 | 71 | 30 | 6200 | Sting |
| Sky wolf spider | 7 | 1 | 39 | 455 | 73 | 31 | 6500 | Flurry |
| Two-head titan | 7 | 2 | 39 | 515 | 71 | 33 | 7000 | Heavy blow |
| Two-head blood fiend | 7 | 2 | 40 | 530 | 73 | 32 | 7200 | Heavy blow |
| Sky wolf spider (boss) | 7 | 1 | 39 | 1750 | 116 | 32 | 9000 | Flurry |
| Two-head titan (boss) | 7 | 2 | 40 | 1900 | 108 | 34 | 10000 | Heavy blow |
| Red Moon Demon (boss) | 7 | 3 | 40 | 2200 | 115 | 36 | 14000 | Hellfire |

Trash rows spawn on their floor **and every deeper one** (cumulative pool, 8.7). Elite monsters = same-table monster ×1.5 HP ×1.2 attack, XP ×1.5. Numbers are calibrated by the balance simulator (PRD 4.2) per **map × floor** cell (26 cells, all green after the v1.5 recalibration), with per-floor arrival kits (what a player realistically wears when first reaching that floor):

- **Wall floors** (each map's last floor — Half-orc Chieftain, Skeleton Spirit, Corpse King, Woma Overlord, Evil Pincer Worm, Zuma Overlord, Red Moon Demon) keep the strict band: a player at the suggested level with the arrival kit, store books 1–3, and 8 red / 4 blue potions should find at least one class whose win rate lands in 45–75% (no 0% matchups).
- **Corridor floors** follow the original's dungeon shape — grindable corridors, one wall per map: the gates on battle pace (4–15 s) and suggested-level death interval (≥5 min) apply to every class, plus "no free wins" (at least one class ≤ 90%). Mid-floor danger is carried by the XP wall and the death economy, not by per-floor boss walls.

### 9.3 Drops and equipment tables

Quality colors: white `#C8C8C8` / green `#5FC85F` / blue `#4FA8F2` / purple `#B06CF0` / gold `#F0C04A`.

The table (v1.5) is **252 rows** — (map 1–7) × (tier 1–3) × slot, where the weapon and (from the Woma tier on) every jewelry slot carries **three class lines (warrior/mage/taoist)**, mirroring the original's per-tier triads; every name is a real original item (verified against the original's item tables; the full stat table lives in `main/mafa_model.c`):

| Slot type | Map 1 → 7 names (triads split warrior/mage/taoist) |
| --- | --- |
| Weapon | Wooden Sword (neutral) → Bronze Axe / Sea Soul / Demon Subduer → Asura / Crescent / Silver Snake → Purgatory / Magic Wand / Limitless Staff → Well-Moon / Blood Drink / Limitless Staff → Verdict Staff / Bone-Jade Staff / Dragon-Pattern Sword → **Dragon Slayer / Soul-Devouring Staff / Carefree Fan** (gold easter egg) |
| Helmet | Skull Helm → Black-Iron Helm (holds to map 4: the classic game has no helmet between black-iron and the Red Moon sets) → Holy-War (attack) / Archmage (magic) / Celestial (taoism) helm rotation |
| Armor | Cloth Robe → Light Mail → Heavy Mail (holds to map 4) → Sky-Devil Mail / Archmage Cloak / Celestial Robe → Holy-War Plate / Rainbow-Feather Robe / Celestial-Master Robe |
| Necklace | Gold Chain / Bamboo Flute / Magnifier (neutral) → Ghost / Life / Sky-Pearl chains → Green / Demon-Bell / Soul chains → Holy-War / Archmage / Celestial chains |
| Bracelet (×2) | Great / Iron bracelets (neutral) → Ghost Gloves / Sibeielle / Mind bracelets → Knight / Dragon / Three-Eye bracelets → Holy-War / Archmage / Celestial bracelets |
| Ring (×2) | Bronze / Coral / Demon-Bane rings (neutral) → Dragon Ring / Ruby / Platinum rings → Ring of Power / Purple Snail / Titan rings → Holy-War / Archmage / Celestial rings |

- Quality lines escalate by map: m1 white/green/blue, m2–3 green/blue/purple, m4 blue/purple/purple, m5–7 blue/purple/gold (the last carrying the boss-only gold chase, 8.6).
- Line ownership follows the original's research: Dragon Ring = Woma-tier **warrior** ring, Ruby = Woma **mage**, Purple Snail = Zuma **mage**, Titan = Zuma **taoist** (anchors in project memory); the Carefree Fan is 1.75 gear — the same approved exception as the Sun-Chasing Sword.
- A map's normal monsters drop only from that map's table (quality roll per 8.6); the tier maps to the quality line above; triad slots resolve with the 8.6 own-line preference.
- Boss drops a random slot **type** with a tier roll of **t2 92% / t3 8%**; twins equip into the free wrist/finger first (8.5).
- Trash never rolls the gold tier: maps 5–7 trash rolls t3 as t2 (v1.4).

## 10. Interaction spec

Global: UP/DOWN move the cursor, OK confirms, long-press returns to the main screen. List cursors use a `>` prefix plus highlight; the idle log auto-scrolls and needs no input. The settings page ends with a non-navigable battery readout: UP/DOWN moves the five cursor rows (v1.2 — toggles plus the two potion thresholds).

| Screen | UP/DOWN | OK | Long-press |
| --- | --- | --- | --- |
| Main menu (continue/new) | move cursor | confirm | — |
| Pick class | move | confirm | back to menu |
| Main screen (idling) | move action-menu cursor (7 cells, 2×4) | open Backpack/Gear/Skills/Store/Map/Settings/Speed | — |
| Gear (8 slots + stats + potions, v1.4) | move cursor over the 8 slots | leave to the main screen (view-only page; equipping happens from the backpack) | back to main screen |
| Skills (v1.4) | move over the 7 class skills | toggle on/off and save at once; fixed and not-yet-learned rows explain themselves in the help panel | back to main screen |
| Main screen — speed | — | cycles 1x → 2x → 4x | — |
| Backpack | move | select (equip/sell submenu, with comparison) | back to main screen |
| Store | move | buy; on an under-level book the buy is **refused** with a "level too low" log line (v1.4) | back to main screen |
| Map list | move (locked rows skipped) | safe zone: rest at once (full restore, v1.4); combat map: open its floor list | back to main screen |
| Floor list (v1.3) | move over open floors + a back row, cursor on the deepest | enter that map at that floor | back to the map list |
| Settings | move | toggle; on a threshold row OK enters edit — value turns gold in `<50%>` brackets, other rows dim, UP/DOWN step 10% within 20–80, OK saves; on the auto-sell row OK opens the quality picker — UP/DOWN moves over the four colors plus a done row, OK toggles a color / saves (v1.2) | back to main screen (also leaves edit, saving) |
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
- **Post-40 content (v1.4 horizon)**: Bull-Monster Temple / Cangmu Isle / the dark-boss series — the classic content above Red Moon, once a 40-level save population exists.

## 15. Revision history

- v1.5 (2026-09-30): **stats 2.0 + drop-economy rework**. The three class stat lines land — warrior attack (11+2) / mage magic (14+2, normal attack lowered to 10+1) / taoist taoism (12+1, normal attack 10+1); skills resolve on their line (talisman / poison / heal / pet scale with taoism); the warrior gains a small mana pool (10+1; Flame Blade 8 / Half-Moon 5 / Savage Charge 3, community values); the gear table becomes a **252-row triad table** (Holy-War favors attack, Archmage magic, Celestial taoism; the weapon trinity Verdict / Bone-Jade / Dragon-Pattern lands home; adds Titan Ring, the Ghost/Life/Mind/Platinum Woma pieces, Soul-Devouring Staff, Carefree Fan; Purgatory moves from map 2 to map 4; the Wooden Sword is the classic all-class level-1 weapon, Shanda-official); drops reworked — trash 20% → **10%** (tiers 70/26/4), the slot roll expands to 6 types (**fixing the never-dropping-ring bug**: MAFA_SLOT_TYPES 5→6), trash also drops Healing/Mana potions **15%** of the time (straight into the counters), and the roll favors the player's own line ~60%; the potion economy is unchanged (the store stays the primary supply); save **v7** (v6 byte layout, the version byte marks the item-table generation; v1–v6 saves migrate to a band-and-line starter kit); the balance sim re-calibrated to all 26 cells green (Scorpion-snake boss attack 50 / Corpse King 51 / Zuma guard II HP 1750, heal 25%+taoism, Holy Armor +40%); the gear/backpack detail panels grow to three rows (five-delta comparison); font subsets 369 → **415** codepoints.
- v1.4 (2026-09-30): **1.76 alignment** — maps, skills, and levels matched to the original. Level cap 15 → **40** with a new 39-entry XP curve (front-fast, ×1.3 mid compounding, the L39→40 wall ≈46 % of ≈26.3 M total XP); **7 skills per class at the real 1.76 learn levels** (two new forms: Savage Charge stun, Holy Armor defense buff; Sun-Chasing Sword the one approved post-1.76 exception); store books 1–3 with a **level gate** (under-level buys refused, drops may still bank books), books 4–6 elite/boss drops; the map list becomes the original's route — Beech Province / Orc Tomb / Stone Tomb / Woma Temple / Death Valley / Zuma Temple / Red Moon Canyon, **26 floor-boss checkpoints**, all monster names verified against the original's mob lists (the Half-orc Warrior/Chieftain replace the non-canonical Forest Yeti/Ape; the mine's skeletons move to the Orc Tomb, the zombie line to Stone Tomb); equipment becomes the **1.76 paper doll minus candle and amulet** — 8 positions over 5 slot types with twin bracelets/rings and the original's real item ladders (Asura through Dragon Slayer, 105 table rows); mage HP 40+5 → 40+6 (late-route survivability, sim-driven); entering the safe zone now **restores full HP/MP** like death respawn; UI: the action bar becomes 2×4 at font 16 with a Skills cell (7 cells + spare), the gear page shows the 8 slots (view-only), a new skill page carries the 7 skills with toggles, the store page retitles to Store; **save v6** (21-bit books, 8 equipped positions, 7 floor bytes) with v1–v5 migration (content-band map remap, level-clamped XP, re-granted books, band starter kit); balance sim extended to 26 cells — all green after four calibration passes; font subsets 272 → 369 codepoints. Design mockups re-rendered first and approved (07 gear, 17 skills new, 09 store, 10 maps, 13 safe zone).
- v0.1 (2026-09-28): initial draft. Positioning and loop established from the FOG MARCH on-device playtest feedback (2026-09-28): the player prefers a monster-slaying, loot-grinding progression loop.
- v0.2 (2026-09-28): aligned to the text-legend genre and the reference product (TapTap "Text Legend" / Wenzi Chuanqi, app 842887) including its player reviews: combat changed from manual turn-based to an **auto-idle log stream**; added boss kill events, auto-potion/auto-sell-white, equipment comparison on equip, sell pricing; removed the town screen and the 5-floor area structure; MMO theater moved to P1.
- v0.3 (2026-09-28): skills expanded from 1 to 3 per class (L3/L7/L12), with burn/poison DoT effects and per-class auto-cast priorities; added sell-price details; copy budget raised to 600 characters.
- v0.7 (2026-09-29): first visual pass on the UI (legend-panel style): gold header band (map + boss progress, level/battery/gold, HP/MP bars), enemy strip with its own HP bar, framed log panel, bottom action bar with full-width-glyph column alignment; the glyph pipeline now covers UI symbols (▶ cursor, … ellipsis), font subsets 223 → 231 codepoints.
- v0.8 (2026-09-29): **skills 2.0 + rebalance** from the second playtest ("too easy, no original-growth feel"): 5 skills per class with distinct forms (passive / proc / nuke / AoE / burn / heal / poison / shield / charge / pets), unlocks gated by level AND skill book (store books 1–2, elite/boss books 3–4, boss first-kill guarantee); battles spawn 1–3 monsters (deeper maps favor packs), the taoist pet taunts; front-fast back-wall XP curve (14→15 wall ≈ 45 % of total, time-to-max ≈ several idle days); white-name death penalty (1–2 backpack stacks + 10–20 % gold, gear safe); potion economy (50/40 gold, level-scaled heals); gold-tier gear boss-only (8 %); boss-kill counter 25 → 40 mobs; save v2 with v1 migration; sim gates recalibrated (battle pace 4–15 s, boss band 45–75 %, ≥1 class in band, no 0 % matchups).
- v0.9 (2026-09-29): on-device playtest found the death loop (respawn kept 0 HP, dying endlessly). Death now respawns the player in a new **safe zone** (4th map id, always open, no monsters/boss, also a voluntary rest spot) at full HP/MP; idling stays stopped until the player picks the next map themselves — the town kick becomes part of the death cost. Penalties unchanged; map-page cursor only rests on enterable rows.
- v1.0 (2026-09-29): the safe zone becomes **map id 0** and heads the map page (row "0" + safe-zone name; combat maps renumbered 1–3, making the 8.3 pack-weight table literal). New games start idling in Beech Forest at once instead of resting in town. Save bumps to **v3**: byte layout identical to v2, only the map fields migrate (town 3 → 0, combat 0–2 → 1–3); v1/v2 saves load and migrate automatically. Code replaces the `map*6+5` boss lookup with `mafa_map_boss()`; balance-sim gates unchanged and still met.
- v1.1 (2026-09-29): the header percent now shows **XP progress toward the next level** ("Lv.8 87%") instead of the battery — playtest feedback was that level-up timing was invisible, decisive on the 14→15 wall (≈45 % of total grind). At the cap the line shrinks to bare "Lv.15". The battery readout moved to the settings page as a non-navigable info row. Design mockups 03/11 re-rendered to match; font subsets regenerated for the new battery label.
- v1.3 (2026-09-30): **floor ladders (per-map dungeon floors)**, closing the content-depth gap against the original (map count and the boss ladder both ran out at Zuma, roughly two thirds of classic 1.76 content). Each combat map becomes a dungeon in the original's shape — Beech Forest 2 floors, Abandoned Mine 3, Zuma Temple 7 (12 boss checkpoints instead of 3; the mine regains its canonical Zombie line, the temple walks the guard/archer/statue ladder with tiered variants, mirroring the original's per-floor HP tiers of the same mobs; all names verified against the original's mob lists). Floor bosses open the next floor; the last floor's boss opens the next map, with gold log lines for both. OK on a map opens a floor list so the player can farm any reached floor (overnight-safe shallow floors); re-entry lands on the deepest floor; death never demotes floors. Spawns use cumulative floor pools (deeper = busier). Save **v5** carries the three per-map deepest floors; v1–v4 saves migrate (left-behind maps count as fully climbed, the current top map's ladder re-climbs). Balance sim extended to 12 map × floor cells with per-floor arrival kits; the strict 45–75% boss band now applies to each map's wall floor, corridors require pace/grind gates plus "no free wins" — all 12 cells pass. The main-screen header shows "map, floor N" and the header XP-percent behavior is unchanged. Design mockups re-rendered: main screens carry the floor suffix, the 40-kill counter, and current monster names (03-06); the map page detail shows the floor (10); a new mockup 16 documents the floor list.
- v1.2 (2026-09-29): playtest feedback round — four visibility/control gaps closed. (1) **Potion counts became visible**: a potions line on the gear page and holdings on the store rows (they previously existed only as invisible counters). (2) **Auto-potion thresholds became adjustable**: the fixed 50% HP / 30% MP triggers turned into settings rows (red line / blue line), editable in place, 20–80% in steps of 10; save v4 carries the thresholds and per-class skill switches, v1–v3 saves load with defaults. (3) **"Learned but never cast" explained**: a host probe showed the cast logic has no bug — Half-Moon Sweep fires in 100 % of multi-mob battles at L9 with the book, but books can be banked before the unlock level (0 casts at L8 despite 118 multi-mob battles) and map 1 spawns lone monsters ≈70 % of the time while AoE is crowd-only by design. The fix is visibility, not numbers: the gear page now lists all five skills with live state (always-on / on-off toggle / store price / "Lv12 boss"-style lock reason, plus an AoE tag on crowd-only skills). (4) **Skills became toggleable** (OK on a skill row; passive/proc fixed). (5) **Auto-sell became a selectable color set** (white/green/blue/purple bits, default white-only, gold never auto-sold): the settings row shows the enabled colors and OK opens an in-place quality picker; save v4 carries the 4-bit set and older saves map the old white-only flag onto it. Mockup 07 became the final gear-page design (absorbing the old 12-skills-proposal, now removed), 09/11 re-rendered, 14 added for the threshold edit state, 15 for the auto-sell picker.
- v0.6 (2026-09-28): on-device feedback added an "auto-boss" settings toggle (boss events are fought automatically instead of prompting); save contents and the settings page updated.
- v0.5 (2026-09-28): on-device feedback added a gear-overview page (action menu item 2: the three equipped slots plus a stat summary); the action menu grew to six entries.
- v0.4 (2026-09-28): M2 balance calibration finalized. Taoist rebalanced as a sustain fighter (HP 60+6, attack 12+1; Soul Fire Talisman L3 2.4×/MP14, Heal L7, Poison L12); added boss-fight rules (50% defense pierce, ±20% monster damage roll, fixed-value potions); boss stats finalized (Ape 300/16, Corpse King 400/26, Overlord 1200/22); success metric redefined as "per map at least one class win rate within 20–85%, none locked at 0%".
