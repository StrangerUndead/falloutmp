# F14 — Leveled Lists, Deterministic Leveled Actors, World Respawn & Reloot

| Field | Value |
|---|---|
| Tier | T0 (legendary item rolls: T0 basic, T1 full pools) |
| Target level | L4 |
| SkyMP analogue | `LeveledListUtils::EvaluateList/EvaluateListRecurse/EvaluateTemplateChain` [src: skymp5-server/cpp/server_guest_lib/LeveledListUtils.cpp:17-225]; `MpActor::EnsureTemplateChainEvaluated` / `EvaluateDeathItem` [src: MpActor.cpp:1077-1240]; reloot `RequestReloot`/`DoReloot` [src: MpObjectReference.cpp:289-311, 1118-1152]; NPC `RespawnWithDelay`. SkyMP level L4 (reference/skymp-sync-inventory.md §2, Leveled lists and Reloot rows; §1.18) |
| Milestone | M6 |
| Workstreams | SRV, ESPM, PLAT, CLI, PVM, GM |
| Depends on | ESPM-006 (NPC_ ACBS/TPLT/TPTA), ESPM-007 (OBTE/OBTS), ESPM-008 (LVLI/LVLN, CONT), ESPM-005 (GLOB, CELL, LCTN), ESPM-016 (GMST by EDID), SRV-070 (server clock), SRV-080 (world-reset service), REF-020, F04, F06, F13 (NPC spawning/hosting), F12 (respawn), PLAT-070 |
| References | reference/fo4-data-formats.md §4.4 (ACBS, TPLT/TPTA, LTPT/LTPC, INAM, DOFT), §4.14 (CONT, LVLI/LVLN, OTFT), §4.19 (OBTE/OBTS); reference/fo4-systems-world-economy.md §1 (B6, B7, B8, B14), §2 S1(e), S7, S18, §3.3, §3.7; reference/fo4-systems-combat-character.md §16.1–16.3; reference/skymp-sync-inventory.md §1.17, §1.18, §2, §3.2 (I17), Appendix C (5, 19); reference/papyrus-api-map.md §1.3 (LeveledItem, LeveledActor), §1.5 (EncounterZone), §4.3; reference/prior-art.md §3.1.9, §3.1.11 |

## 1. Summary
Every random roll that decides *what exists in the world* is made once, on the server, and remembered: which raider variant stands at a spot, which level it has, whether it is legendary, which weapon and mods it carries, what a container or corpse holds, what a harvested plant yields. All clients therefore see the same NPC, the same loot and the same modded weapon (vanilla FO4 rolls these per client). The world resets like vanilla: cells refill after their reset interval when no player is nearby, `Respawns` containers restock, respawning NPCs return with fresh rolls, uniques stay dead, corpses and dropped junk are cleaned up. Server owners can tune or override every interval.

## 2. Vanilla Fallout 4 behaviour
- **LVLI/LVLN layout:** `LVLD` u8 chance none; `LVLM` max count; `LVLF` flags (0x1 calc from all levels ≤ PC level, 0x2 calc for each item in count, 0x4 use all; LVLN "calculate all" still picks one); `LVLG` GLOB chance-none global; `LLCT` count; **`LVLO` 12 bytes** `{u16 level, u8[2], formid ref, u16 count, u8 chanceNone, u8}`; optional `COED` after each entry; `LLKC {KYWD, u32 chance}[]` filter keyword chances; `LVSG` GLOB epic-loot chance (LVLI); `ONAM` override name (fo4-data-formats §4.14).
- **Object templates:** WEAP/ARMO/NPC_/FURN carry `OBTE` combinations `OBTS{levelMin, levelMax, parentCombination, default, keywords[], includes[{omod, attachPointIndex, optional, dontUseAll}]}`; spawned items pick a combination by level and keywords, interacting with `LLKC` (fo4-data-formats §4.19, marked [inference] there).
- **Leveled actors:** NPC_ `TPLT` default template (NPC_ or LVLN) plus `TPTA` = 13 per-flag templates (bit 0 Traits … 12 Keywords); for flag bit *b* the data comes from `TPTA[b]` if non-null, else `TPLT` (fo4-data-formats §4.4). `ACBS` gives level, calc min/max, PC-level-mult flag 0x80, flags 0x8 Respawn, 0x20 Unique, 0x1000 No Loot, 0x4000000 Spawns Dead. `LTPT`/`LTPC` = legendary template + GLOB chance; `INAM` death item LVLI; `DOFT` default outfit.
- **Encounter zones (ECZN):** min/max level, "never resets", "match PC below minimum"; Papyrus `EncounterZone.Reset`, F4SE `GetMinLevel/GetMaxLevel/IsNeverResetable/IsWorkshop` (papyrus-api-map §1.5). An ECZN locks the level of its leveled actors and loot when first entered [inference; verify D-real].
- **Cell reset:** `iHoursToRespawnCell` = 168 h, `iHoursToRespawnCellCleared` = 480 h (community-sourced; read the GMSTs); reset respawns `Respawn`-flag NPCs and leveled encounters, refills `Respawns` containers (CONT `DATA` flag 0x2), restores picked items, relocks, removes corpses and non-persistent dropped items; uniques/quest refs stay; `Location.IsCleared/SetCleared`, `OnLocationCleared`, `Cell.Reset`, `ObjectReference.Reset/OnReset` (world-economy §2 S18).
- **Legendary enemies and items:** spawn chance by difficulty; legendary death drop from level-gated pools via `LegendaryItemQuestScript.GenerateLegendaryItem` (world-economy §2 S1(a); combat-character §16.1).
- **Single-player assumptions that break:** every roll is local, so the same REFR becomes a different NPC per client (FO4_Wrld, combat-character §16.2); resets only happen when no player has the cell loaded, which in MP may be never.

## 3. SkyMP baseline
- `EvaluateList`: chance none from `LVLD`, but **any `LVLG` global means 100 % none** (I17) [src: LeveledListUtils.cpp:35]; uniform pick among all entries with level ≤ pcLevel (ignores the "calc from all levels" rule); `std::random_device` per call (not reproducible); count multiplication and `Each` flag in `EvaluateListRecurse` [src: LeveledListUtils.cpp:17-121].
- libespm reads `LVLO` as contiguous `{u32 level, u32 formId, u32 count}`, so FO4 `count` absorbs the chance-none byte and `COED` breaks the stride (fo4-data-formats §4.14).
- `EvaluateTemplateChain` follows `TPLT` only (no per-flag `TPTA`); the chain is persisted in `templateChain` and sent to all listeners; the client's leveled visual variant is not synced (`formView.ts:132-147`, Appendix C item 19).
- Death items and NPC inventories use `kPlayerCharacterLevel = 1` (skyrim-coupling-index §1.8 `:1212-1219`).
- Reloot: per-type timers (FLOR/TREE 1 h, items 1 h, CONT 1 h, DOOR 3 s), `reloot`/`forbiddenReloot` settings; CONT reloot when emptied, ignoring the `Respawns` flag (B6) [src: MpObjectReference.cpp:289-311, 873-892, 1118-1152]. NPC/player respawn after `spawnDelay` 25 s (B14). FF items are skipped on load [src: PartOne.cpp:347-361].
- **Reuse:** recursion, count/each semantics, `templateChain` persistence, reloot timer plumbing (`nextRelootDatetime`), respawn path. **Fix:** I17, FO4 `LVLO` parsing, level rule, RNG. **Add:** OMOD template rolls, per-flag templates, encounter zones, cell reset, legendary rolls, deterministic client spawning.

## 4. Design

### 4.1 Authority model
Class A for all rolls and reset state. Clients never evaluate leveled lists or templates; they render the server's results.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field / record) | Default source |
|---|---|---|---|---|
| Template chain (TPLT path) | `vector<FormDesc>` | `templateChain` (existing) | yes | rolled at first load |
| Per-flag template picks | `array<optional<FormDesc>,13>` | `ChangeForm.templateSlots` (new) | yes | rolled from `TPTA` |
| Actor level | u16 | `ChangeForm.actorLevel` (new) | yes | ACBS + ECZN lock |
| Legendary | bool | `ChangeForm.isLegendary` (new) | yes | `LTPC` GLOB roll |
| Roll generation | u32 | `ChangeForm.rollGeneration` (new; ++ on every reset/respawn) | yes | 0 |
| Rolled inventories / outfits | `Inventory` with OMODs | `inv`, `equipment` (F04/F05) | yes | this spec |
| Reloot time | uint64 ms | `nextRelootDatetime` (existing) | yes | per-type override or cell reset |
| Cell reset state | `CellResetState{key, lastResetGameTime, cleared, dirty}` | new ADR-010 record `cellReset` (key = interior CELL `FormDesc`, or worldspace + exterior grid x,y) | yes | first load |
| Encounter-zone lock | `EncounterZoneState{ezId, lockedLevel, lockedAtGameTime}` | new ADR-010 record `encounterZones` | yes | first player entry |
| Runtime leveled-list additions | `map<listId, vector<{form, level, count}>>` | new ADR-010 record `leveledOverrides` | yes | Papyrus `AddForm` |
| World RNG seed | u64 | DB meta record (with SRV-012) | yes | random at DB creation |

### 4.3 Protocol
No new messages. The results ride on existing ones:
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `CreateActorFo4` (64) | S→C | `baseId`, `props.templateChain`, `props.templateSlots[13]`, `props.actorLevel`, `props.isLegendary` (all listeners); equipment (F05) | R | on subscribe | reused (F00); new props |
| `SetInventoryFo4` (68) | S→C | rolled contents (owner/occupant/peekers) | R | on open/peek | reused (F04) |
| `UpdateProperty` (7) | S→C | `isHarvested`, `isOpen`, `isDisabled`, `isDead` after reset/respawn | R | on reset | reused |
| `DestroyActor` (25) / `CreateActor` | S→C | removed corpses and dropped refs; respawned NPCs | R | on reset | reused |

### 4.4 Client capture (owner side)
None. The client must **not** run leveled evaluation: vanilla leveled spawns and container rolls are suppressed by the world cleaner (CLI-020) and by creating NPC ghosts from resolved bases (§4.5).

### 4.5 Apply
- **Deterministic leveled actors:** the client creates the ghost from `baseId` + `templateSlots` with a platform native `createResolvedNpc(baseId, templateSlots[13], level, isLegendary)` that clones the base TESNPC through `IFormFactory` (PLAT-070) and replaces every template reference with the resolved **non-leveled** NPC_ for that flag, so the engine's own template resolution cannot re-roll [inference: the engine follows `TPTA`/`baseTemplateForm` at load; verify with G-self]. Fallback: hook the engine's leveled-character evaluation (`ExtraLeveledCreature`) to force the server's pick (RE).
- **Legendary:** the ghost gets the legendary star/name from `isLegendary` (INNR/`LTPT` data); mutation effects come from F11/F13.
- **Loot:** container/corpse contents and NPC equipment arrive through F04/F05/F06 with their OMODs; no client roll.
- **Reset:** flora `isHarvested=false`, picked items re-enabled, respawned NPCs streamed in with new picks, removed corpses `DestroyActor`.

### 4.6 Validation & anti-cheat
- No client input exists, so validation is about data robustness: every evaluated form must resolve; unknown/foreign records are skipped with a log line; recursion depth ≤ 16; total count per evaluation ≤ `leveledLists.maxItemsPerRoll` (default 512).
- **Determinism:** `LeveledRng(seed = hash64(worldSeed, refFormId, rollGeneration, purpose))` with `purpose ∈ {template, level, legendary, inventory, outfit, deathItem, obts, harvest}`; evaluation is a pure function of `(records, globals, level, seed)` so tests are reproducible and results survive restarts even before persistence.
- **Evaluation rules (FO4 profile):**
  1. chance none = `LVLG` GLOB value if set (I17), else `LVLD`; each entry then applies its own `chanceNone` byte;
  2. eligible entries: level ≤ L; unless `LVLF` 0x1, keep only the entries of the highest eligible level [inference: standard Bethesda semantics; Skyrim profile keeps SkyMP's behaviour];
  3. use-all (0x4) / each-in-count (0x2) as today; LVLN always yields one NPC;
  4. WEAP/ARMO results pick an `OBTS` combination: level in [levelMin, levelMax], filtered by `LLKC` keyword chances; none matches → `default` combination; includes with `optional` are rolled; produces `omods` (F04 `ObjectModValidator` must accept them);
  5. `LVSG` epic-loot chance: if the GLOB roll succeeds and `legendary.useEpicLootChance` is on, the item gets a legendary OMOD from the configured pool [inference; verify D-real].
- **Level L:** actor level = ACBS level, or `clamp(L_zone × mult, calcMin, calcMax)` with PC-level-mult; `L_zone` = the ECZN locked level, locked when the first player enters the zone after a reset as `clamp(playerLevel, ezMin, ezMax or ∞)` ("match PC below minimum" → playerLevel). No ECZN → `leveledLists.levelSource` (`firstPlayer` default, or `fixed:<n>`). Containers and death items use the same L (fixes `kPlayerCharacterLevel = 1`).

### 4.7 Audience / visibility
Template picks, level and legendary flag are public (all listeners, like SkyMP's `templateChain`). Contents stay owner/occupant-only (F04/F06).

### 4.8 NPC parity
All NPCs, hosted or not, are rolled on the server at first `AttachEspmRecord`/spawn and on each respawn. The hoster receives the same snapshot as everyone else; host migration never re-rolls. Spawned (FF) NPCs from gamemode `mp.place`/Papyrus `PlaceActorAtMe` are rolled the same way with their own seed.

### 4.9 Gamemode API & server Papyrus
- Events: `onCellReset(cellKey) [blockable]`, `onLegendaryDrop(actorId, item) [blockable; may replace the item]`, `onLeveledRoll(refId, purpose, result)` (observe-only, debug).
- API: `mp.resetCell(cellKey)`, `mp.getCellResetState(cellKey)`; properties `templateChain`, `templateSlots`, `actorLevel` (get; set only before first spawn), `isLegendary` (get).
- Settings: `cellReset.{hours (168), hoursCleared (480), requireEmptyNeighbourhood (true), retryMinutes (5), maxDeferHours (24)}` (defaults overwritten by the ESM GMSTs when present); `npcRespawn.mode` (`cellReset` default | `delay`); `reloot` / `forbiddenReloot` (SkyMP keys, per-type overrides); `relootIgnoresRespawnFlag` (false); `corpses.lifetimeSec` (0 = until cell reset); `droppedItems.{lifetimeSec (120), maxPerActor (10), globalMax, persistAcrossRestart (true)}`; `leveledLists.levelSource`; `legendary.{chanceMult, useEpicLootChance, pools}`.
- Papyrus natives: `LeveledItem.AddForm/Revert`, `LeveledActor.AddForm/Revert` (persisted in `leveledOverrides`), `Location.IsCleared/SetCleared/HasEverBeenCleared/Reset`, `Cell.Reset`, `ObjectReference.Reset`, `EncounterZone.Reset` (+ F4SE getters), `GlobalVariable` values feeding `LVLG`/`LVSG`/`LTPC`, `DeleteWhenAble`.
- Papyrus events: `OnReset`, `Location.OnLocationCleared`, `OnLoad`/`OnCellAttach` per load (P1, papyrus-api-map §4.3).

### 4.10 Edge cases & failure modes
- **Players never leave a cell:** reset is deferred up to `maxDeferHours`, then refs not subscribed by any player are reset individually (partial reset); subscribed ones wait.
- **Respawn while a corpse is being looted:** the corpse is kept until the occupant leaves (F06), then reset.
- **Uniques and quest refs** (`Unique` flag, gamemode-protected, workshop build areas from F22) never reset.
- **Plugin added/removed:** picks are `FormDesc`; an unresolvable pick triggers a re-roll with the same seed and a log line.
- **Server restart:** reset timers are game-time based (SRV-070) and re-armed from `lastResetGameTime`; dropped refs re-armed with remaining lifetime (F06-T07).
- **Clock jumps** (gamemode changes timescale): resets use game time consistently; long jumps trigger at most one reset per cell.

### 4.11 Performance budget
- Evaluation ≤ 50 µs per list (recursion depth ≤ 16), cached record views.
- Cell reset sweep: incremental, ≤ 1 ms per tick (dirty-cell queue); only cells with changes are tracked.
- Snapshot cost of new props ≤ 80 B per NPC.

## 5. Engine / platform work required
- `createResolvedNpc` native (PLAT-070 extension): clone TESNPC, write resolved template forms per flag, set level, legendary naming; G-self proof that the engine does not re-roll.
- Suppression of client-side leveled spawns and container rolls for server-managed refs (CLI-020 world cleaner + platform hook if needed).

## 6. Tests
- `L-fixture` (`[F14][fixture]`): synthetic LVLI/LVLN with 12-byte `LVLO`, `COED`, `LLKC`, `LVSG`, `LVLG`; NPC_ with `TPLT` + `TPTA`; WEAP with `OBTE` combinations.
- `L-unit` (`[F14]`, `[LeveledList]`, `[CellReset]`, extend `[Respawn]`):
  - `LVLG` global value 0 → items yielded, 100 → none (I17); per-entry chance none;
  - highest-level rule vs `LVLF` 0x1; use-all; each-in-count; recursion limits;
  - same seed → same result; different `rollGeneration` → re-roll;
  - OBTS combination respects level range and `LLKC`; result passes `ObjectModValidator`;
  - TPTA per-flag resolution matches the rule in §2; `templateSlots` persisted and in `CreateActorFo4` for all listeners;
  - ECZN lock: first entrant locks the level; later entrants see the same level; reset unlocks unless "never resets";
  - cell reset with a fake clock: not before 168 h; blocked while a player is in the 3×3 neighbourhood; deferred partial reset after `maxDeferHours`;
  - reset actions: `Respawns` containers refilled (B6), non-respawn containers untouched, flora restored, `Respawn` NPCs re-rolled, uniques stay dead, corpses removed, dropped refs deleted, workshop areas excluded;
  - dropped item lifetime and restart persistence;
  - `onCellReset` veto keeps the cell; `onPapyrusEvent:OnReset` observed;
  - Skyrim `LeveledListUtilsTest` / `TemplateInventoryTest` unchanged.
- `L-int`: two bots enter a leveled-spawn cell; both receive identical `templateSlots`, level and equipment.
- `G-self`: `createResolvedNpc` for a raider LVLN on two clients yields the same race/outfit/weapon instance.
- `G-manual`: two players see the same raider gang; loot a legendary; wait (accelerated timescale) for a reset.
- `D-real` (`[fo4data]`): evaluate every LVLI/LVLN in Fallout4.esm without errors; GMST values read.

## 7. Tasks
- [ ] **F14-T01** FO4 leveled evaluation in `LeveledListUtils` on top of ESPM-008 (per-entry chance none, u16 count, `COED` tolerance, highest-level rule, recursion limits) behind GameProfile — M — Depends: ESPM-008, REF-003 — Verify: L-unit, L-fixture — Files: skymp5-server/cpp/server_guest_lib/LeveledListUtils.{h,cpp}, unit/LeveledListFo4Test.cpp
- [ ] **F14-T02** Item instance rolls: `OBTS` combination selection (level, `LLKC`, default, optional includes) → `omods` — M — Depends: F14-T01, ESPM-007, F04-T06 — Verify: L-unit, L-fixture — Files: skymp5-server/cpp/server_guest_lib/fo4/ObjectTemplateRoller.{h,cpp}
- [ ] **F14-T03** Evaluate `LVLG` chance-none global (I17) via the server global-variable store (ESM `GLOB` + Papyrus `SetValue` overrides) — S — Depends: F14-T01, PVM-013 — Verify: L-unit
  - Accept: a list with a 0 % global yields items; SkyMP's "any global = none" behaviour is gone under both profiles (bug fix, Skyrim tests green).
- [ ] **F14-T04** Deterministic `LeveledRng` (world seed in DB meta, per-ref/purpose/generation seeds) replacing `std::random_device` — S — Depends: SRV-012 — Verify: L-unit
- [ ] **F14-T05** Leveled actors: per-flag `TPTA` resolution, actor level (ACBS/ECZN/PC-level mult), `LTPT`/`LTPC` legendary roll, outfit and death item with the actor's level; `templateSlots`, `actorLevel`, `isLegendary`, `rollGeneration` persisted and in `CreateActorFo4` — L — Depends: F14-T01, F14-T04, ESPM-006 — Verify: L-unit — Files: MpActor.cpp (`EnsureTemplateChainEvaluated`, `EvaluateDeathItem`), EvaluateTemplate.h, MpChangeForms.{h,cpp}
- [ ] **F14-T06** Encounter zones: ECZN record parsing (min/max level, flags; not covered by ESPM-005…010), `EncounterZoneState` record, lock/unlock rules — M — Depends: ESPM-001, REF-020 — Verify: L-fixture, L-unit — Files: libespm/include/libespm/fo4/ECZN.h, libespm/src/fo4/ECZN.cpp, skymp5-server/cpp/server_guest_lib/fo4/EncounterZoneService.{h,cpp}
- [ ] **F14-T07** Client/platform deterministic spawning: `createResolvedNpc`, formView integration, no client re-roll — L — Depends: PLAT-070, F14-T05 — Verify: W-ci, G-self — Files: fallout4-platform/src/platform_fo4/NpcApi.cpp, falloutmp-client/src/view/formView.ts
- [ ] **F14-T08** `CellResetService` core (SRV-080 part 1): per-cell keys, dirty tracking, game-time timers (SRV-070), GMSTs by EDID (ESPM-016), empty-neighbourhood rule, deferral and partial reset, `cellReset` record — M — Depends: SRV-070, SRV-080, ESPM-016 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/fo4/CellResetService.{h,cpp}, WorldState.{h,cpp}
- [ ] **F14-T09** Reset actions: `Respawns` containers only (B6 fix, `relootIgnoresRespawnFlag`), flora, picked ESM items, lock hook (F24), `Respawn`-flag NPC respawn with new `rollGeneration`, uniques stay dead, corpse removal, workshop exclusion (F22), FO4 default per-type reloot table (DOOR 3 s only) — M — Depends: F14-T08, F14-T05, F12 — Verify: L-unit — Files: MpObjectReference.cpp (`GetRelootTime`, `DoReloot`), MpActor.cpp (respawn), game_profile/fallout4/
- [ ] **F14-T10** Dropped-item lifetime and persistence across restart (SRV-080 part 2): `droppedExpiresAt`, re-arm on load, caps per actor/global, FF-item load path for `droppedEntry` refs — S — Depends: SRV-080, F06-T07 — Verify: L-unit — Files: PartOne.cpp (load skip at 347-361), MpObjectReference.cpp
- [ ] **F14-T11** Location cleared tracking (boss/LocRefType deaths from F13 → `cleared`, `hoursCleared`), `OnLocationCleared` — S — Depends: F14-T08, F13 — Verify: L-unit
- [ ] **F14-T12** Legendary item drops: death of a legendary NPC → legendary OMOD roll from configured pools, `onLegendaryDrop`; `LVSG` handling — M — Depends: F14-T02, F14-T05 — Verify: L-unit, D-real
- [ ] **F14-T13** Gamemode & Papyrus surface: events/API/settings of §4.9, `LeveledItem/LeveledActor.AddForm/Revert` with `leveledOverrides`, `Cell.Reset`, `ObjectReference.Reset`, `EncounterZone.Reset`, docs — M — Depends: F14-T08, PVM-014 — Verify: L-unit, L-int — Files: script_classes/PapyrusLeveledItem.cpp, script_classes/, skymp5-server/cpp/addon/ScampServer.cpp
- [ ] **F14-T14** Verification: `[fo4data]` evaluation sweep over Fallout4.esm + DLCs; `G-manual` same-raider-gang and reset scenario script — S — Depends: F14-T07, F14-T09, ESPM-015 — Verify: D-real, G-manual — Files: unit/LeveledListFo4DataTest.cpp, docs/falloutmp/test-scripts/F14-leveled-respawn.md

## 8. Open questions & risks
- The exact vanilla level rule (highest-level-only vs all eligible) and ECZN lock semantics must be confirmed against the engine (G-self comparison of 1000 local rolls vs the server's distribution).
- Whether cloning a TESNPC with resolved templates fully prevents engine re-rolls (outfits, leveled inventory on the ghost); fallback is the `ExtraLeveledCreature` hook (RE).
- `LVSG` meaning is inferred; until D-real confirms it, it is off by default.
- Reset intervals of 168/480 game hours at timescale 20 are 8.4/24 real hours; servers may prefer shorter values (Q-06 gameplay priorities).
