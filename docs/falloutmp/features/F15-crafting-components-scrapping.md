# F15 — Crafting, Components & Scrapping

| Field | Value |
|---|---|
| Tier | T1 (chem, cooking, armor, weapon and PA stations; components; scrap). Robot workbench and Contraptions machines are T2 (F21, F22) |
| Target level | L4 |
| SkyMP analogue | `CraftItem` (MsgType 13): the client infers a craft from container-change streaks (`skymp5-client/src/services/services/craftService.ts:18-70`); the server matches a COBJ by exact input counts, bench keyword and CTDA (`skymp5-server/cpp/server_guest_lib/CraftService.cpp:20-277`) and fires `onCraft` (`gamemode_events/CraftEvent.cpp`). SkyMP level L3–L4 |
| Milestone | M9 (stations, components, scrap); workshop-container and supply-network sources complete with F22 in M11 |
| Workstreams | SRV, ESPM, CLI, PLAT, NET, GM, PVM |
| Depends on | F04 (`ItemKey`, B2 fix), F07 (workbench occupancy, F07-T04 reach), F19 (perks, XP), F22 (workshop container as a source; optional), ESPM-008, ESPM-010, ESPM-011, REF-009, REF-013, SRV-021, PLAT-042, PLAT-080, NET-007 |
| References | reference/fo4-systems-world-economy.md S3, S4, §1 (B2, B4, B10), §3.2–3.5, §5 items 6 and 9; reference/fo4-data-formats.md §4.11 (MISC/CMPO), §4.13 (COBJ), §4.17 (FURN `WBDT`), §4.20 + Appendix A (CTDA); reference/skymp-sync-inventory.md §2 (Crafting row), §3.2 (I15); reference/skyrim-coupling-index.md §1.12; reference/papyrus-api-map.md §1.3 (Component, MiscObject, ConstructibleObject); reference/commonlib-port-map.md §4.2 (event sources), §4.3 (menu names); reference/fo4-systems-combat-character.md §11 (XP) |

## 1. Summary
A player sits at a chemistry station, cooking station, armor or weapons workbench, and crafts items from **components** (steel, screws, adhesive…) and whole items (meat, Abraxo, stimpaks). Junk carried by the player, or stored in the settlement's workshop container, is broken down automatically to cover missing components; leftovers are kept. At armor and weapons benches a player can **scrap** a weapon or armor instance into components, with yields that depend on the Scrapper perk. Perk-gated recipes (Chemist, Medic, Demolition Expert, Science!…) only work if the server says the actor has the perk. The server resolves the recipe, consumes inputs, creates outputs and awards XP; the client only shows the vanilla menu and sends intents. Results are identical for every player and survive reconnect and restart.

## 2. Vanilla Fallout 4 behaviour
- **Stations** are FURN with `WBDT` bench type: 1 Create Object, 2 Weapons, 5 Alchemy (chem *and* cooking; told apart by FURN keywords matched against COBJ `BNAM`), 7 Armor, 8 Power Armor, 9 Robot Mod [src: xEdit wbDefinitionsFO4.pas:6885-6900, per reference/fo4-data-formats.md §4.17].
- **COBJ:** `FVPA` 8·n `{formid component-or-item; u32 count}` (replaces Skyrim `CNTO`), `CITC`+`CTDA[]`, `CNAM` created object (item, OMOD, or FLST), `BNAM` bench keyword, `FNAM` category keywords (menu tabs; `WorkshopRecipeFilterScrap` marks scrap recipes), `INTV {u16 createdCount (default 1); u16 priority}`. `NAM1/NAM2/NAM3` are **unused** in FO4; libespm reads `NAM1` as the Skyrim count [src: libespm/src/COBJ.cpp:23] (data-formats §4.13).
- **CMPO:** `MNAM` scrap item (the MISC "Steel" etc.), `GNAM` mod-scrap-scalar GLOB. **MISC:** `CVPA` 8·n `{component; count}` = what scrapping the junk yields; record flag `Calc From Components` (data-formats §4.11).
- 31 components; junk is auto-broken to satisfy component needs and leftovers are kept; caps are not components (world-economy S4(a), S3(a)).
- **Scrap yields** for weapons/armor and attached mods: `floor(count × scalar)` filtered by component rarity: Common always, Uncommon needs Scrapper 1, Rare needs Scrapper 2; adhesive/oil yield 0. Scrapper (INT 5) `00065E65`, `001D2483`, rank 3 in Far Harbor [web: wiki:Fallout_4_junk_items], [web: wiki:Scrapper_(Fallout_4)]. Legendary items cannot be scrapped (world-economy S1(a)).
- **Perk/quest gates** are COBJ CTDA. FO4 indices: `GetValue` 14, `GetStageDone` 59, `GetIsID` 72, `GetGlobalValue` 74, `HasPerk` 448, `HasKeyword` 560 (data-formats §4.20). Indices SkyMP already implements keep the same number but some change parameter types (AVIF form ids).
- **Menus:** `ExamineMenu` (weapons/armor; `scrappingArray`, `BuildWeaponScrappingArray()`, `ConsumeSelectedItems`), `WorkbenchMenuBase::ModChoiceData{recipe, requiredItems, requiredPerks}` with `sharedContainerRef`/`workbenchContainerRef` [src: CLF4 E/ExamineMenu.h, W/WorkbenchMenuBase.h]. `CookingMenu` exists only as RTTI and is **not** among the 22 menu-name constants in libxse (reference/commonlib-port-map.md §4.3); that it serves both chem and cooking is `[inference]` (world-economy §5 item 6).
- **Events:** story event `BGSCraftItemEvent{workbench, location, createdItemBase}` [src: CLF4 B/BGSCraftItemEvent.h]; `TESFurnitureEvent` enter/exit; Papyrus `Actor.OnPlayerUseWorkBench(ObjectReference)` [src: F4SE vanilla/Actor.psc:980-1016]; global `ItemCrafted` event source needs RE (commonlib-port-map §4.2 list).
- **XP:** crafting grants a small amount of XP; exact values are unknown `[inference]`. Measure with G-self by logging `Actor::RewardExperience` (combat-character §11.1).
- **SP assumptions that break:** the menu consumes inputs and creates outputs locally; perk checks run on the client; auto-break picks arbitrary junk locally; the workbench's shared container is only the client's copy of server data.

## 3. SkyMP baseline
- Capture infers a craft from inventory-diff streaks while in the "Crafting Menu" and sends `CraftItem{workbench, craftInputObjects, resultObjectId}` (B10). With FO4 auto-break, one craft removes several junk stacks and adds leftovers, so inference cannot work.
- Server (`CraftService::OnCraftItem`): workbench must be FURN/ACTI (`CraftService.cpp:35`); bench keyword match; exact input match (`RecipeItemsMatch`, `:84-104`), with Skyrim temper keywords hard-coded and compared as raw local ids (skyrim-coupling-index §1.12); COBJ conditions via `ConditionsEvaluator` caller `kCraft` (`:232-277`); `CraftEvent` fires `onCraft` and swaps items.
- Gaps: no occupancy or distance check (I15/B4); every reject is a log line with no correction (I12); a full COBJ scan per craft (`CraftService.cpp:142-194`, perf hotspot).
- **Reuse:** the service skeleton, `ConditionsEvaluator`, `CraftEvent`/`onCraft` signature, `Inventory` atomic remove/add. **Replace:** input inference (explicit recipe id), exact-count matching (component resolver), Skyrim temper constants (GameProfile crafting rules, REF-013).

## 4. Design

### 4.1 Authority model
- **Class A** for everything with gameplay value: recipe resolution, perk/condition checks, component consumption (including which junk is broken), outputs, leftovers, scrap yields, crafting XP, crafting stats.
- **Class A** (F07) for workbench occupancy.
- **Class D** for the menu UI, previews and sounds. Furniture idles are F02/F07.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Actor inventory (components, junk, outputs) | `Inventory` with `ItemKey` entries | actor `MpChangeFormREFR.inv` | yes (`inv`) | ESM/template |
| Workshop container inventory (source and leftover sink) | `Inventory` | workshop container `MpChangeFormREFR.inv` (F22) | yes (`inv`) | ESM |
| Workbench occupant(s) | `map<markerIdx, actorId>` | `MpObjectReference::occupants` (F07; replaces SkyMP's single `occupant`) | no | empty |
| Recipe index (bench keyword → COBJs; created form → COBJs; item → scrap recipe) | cache | `RecipeIndex` (new) | no | built lazily from COBJ |
| Component catalogue (MISC `CVPA`, CMPO `MNAM`/`GNAM`, rarity) | cache | `ComponentLedger` (new) | no | ESM |
| Recent request nonces (last 64 per actor, with cached result) | ring | `MpActor::recentRequests` (new; shared with F16, F22) | no | empty |
| Crafting counters (`itemsCrafted`, `itemsScrapped`) | map | `PlayerProfile.stats` (SRV-060) | yes | 0 |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `Activate` (6) | C→S | occupy/release the workbench | R | on activation | reused (F07) |
| `CraftItemFo4` (76) | C→S | `nonce` u32, `workbenchRefId`, `recipeId` (COBJ global id), `count` u16 (1–100) | R | on craft confirm; ≤ 16 B | twin of CraftItem (13) |
| `ScrapItem` (86) | C→S | `nonce`, `workbenchRefId`, `item` (`ItemKey`), `count` u16 | R | on scrap confirm | new |
| `RequestResult` (107) | S→C | `nonce`, `requestType` u8 (request MsgType), `ok`, `error` u16, `refId` u32 (0 here), `items[]` `{ItemKey, count}` (craft: created; scrap: yields) | R | once per request | **new (allocated here)** |
| `SetInventoryFo4` (68) | S→C | crafter's inventory (authoritative) | R | on accept and on every reject | reused (F04) |
| `SetInventoryFo4` (68) with `refId` = workshop container, `version` | S→C | workshop container after change, to its occupant/peekers | R | when touched | reused (F06/F22) |
| `ProgressionUpdate` (89) | S→C | XP from crafting | R | on accept | reused (F19) |

`RequestResult.error` codes (shared with F16/F22): `NotOccupant, TooFar, BenchMismatch, UnknownRecipe, ConditionsFailed, InsufficientComponents, ItemNotFound, NotScrappable, Legendary, Equipped, RateLimited, Duplicate, GamemodeBlocked, Permission, Internal`.

### 4.4 Client capture (owner side)
- **Primary:** platform hook on the workbench menus' craft/scrap confirm (`WorkbenchMenuBase`/`CookingMenu` craft confirm; `ExamineMenu` scrap of the current `modItem`). It reads `ModChoiceData.recipe` (the COBJ) and the stack's `ItemKey` (PLAT-080), and emits `craftConfirm{workbench, recipeId, count}` / `scrapConfirm{workbench, item, count}` events.
- **Commit policy:** T1 lets the local menu commit and treats the server's `SetInventoryFo4` as the truth, the same as SkyMP's flow. A pre-commit cancel (no local mutation) replaces this once the hook RE lands (F15-T09); the message flow does not change.
- **Filters:** only while the local player occupies a FURN with `WBDT` ≠ 0 (server-confirmed occupancy); only recipes whose `BNAM` is not the workshop build keyword (those are F22 `WorkshopPlace`); ignore events during reconnect/loading.
- The client's copy of the workshop container is filled by the server (F22/F06), so the vanilla menu shows correct affordability.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Owner:** `SetInventoryFo4` replaces the local inventory (F04 apply). The menu is refreshed through a platform native (`refreshWorkbenchMenu`), so counts update without closing. `RequestResult` drives a HUD message on failure ("Missing components", "Requires Chemist 2").
- **Remotes:** nothing gameplay-visible. Furniture enter/exit and idles come from F02/F07.
- **Stream-in / respawn:** no crafting state of its own; inventories arrive with F04/F06 snapshots.
- **Reconnect:** a request is either committed or not (atomic). The re-sent inventory snapshot is the truth; a replayed nonce returns the cached `RequestResult`.

### 4.6 Validation & anti-cheat
Checks run in order. Every failure sends `RequestResult{ok=false, error}` **plus** `SetInventoryFo4` to the owner (S12, I12, NET-007) and, if a workshop container was involved, its contents to its viewers.
1. Sender's own actor (players only; NPCs do not craft) (S6).
2. Rate ≤ 10 craft/scrap requests per second per actor (S22: `crafting.maxRequestsPerSec`). A repeated `nonce` returns the cached result without re-executing (`Duplicate` only if no cached result exists).
3. Occupancy: the actor is an occupant of the workbench, and its server position is within the F07 activation reach (`GameProfile::ActivationReach(FURN)`, setting `activationReach`, default 300 u + 64 u slack) (I15, F07-T04).
4. Bench: the base is FURN with `WBDT` ≠ 0; COBJ `BNAM` ∈ the bench's keywords, compared as **global** ids (fixes the raw-id temper bug). Excluded bench keywords come from `GameProfile::Crafting()` (REF-013).
5. Recipe: COBJ exists in the load order and is not a workshop build or scrap recipe; `count` ∈ [1, 100].
6. Conditions: CTDA evaluated with `ConditionsEvaluator` (`kCraft`) and the FO4 function table (REF-009, ESPM-011); `HasPerk` is answered by SRV-021.
7. Components: `ComponentLedger::Take(sources, FVPA × count)` with sources ordered **player → workshop container** (if the bench is linked to a workshop and the actor has the F22 `craft` permission) **→ supply network** (T2). Within a source: exact item/CMPO-scrap-item first, then break junk deterministically (lowest value per needed component, ties by FormId). Leftovers go to the workshop container at a workshop, else to the player. Everything happens on copies and commits only if the whole operation succeeds (S13).
8. Output: `CNAM` × `INTV.createdCount` × `count`. A `CNAM` FLST uses its first entry and a `CNAM` LVLI is evaluated by F14 `[inference: vanilla semantics unverified]`.
9. Gamemode `onCraft` veto → `GamemodeBlocked` + correction; no mutation.

**Scrap (`ScrapItem`):** checks 1–3 apply. Then:
- the item exists by `ItemKey` with ≥ `count`;
- bench type matches the item (Weapons ↔ WEAP, Armor ↔ ARMO);
- the item is not legendary (any OMOD with the Legendary flag), not equipped (`Equipped`) and has no `CantDrop`/quest keyword (`NotScrappable`).

Yields come from `ComponentLedger::ScrapYield(item, scrapperRank)`: the item's scrap recipe plus the COBJ components of every attached OMOD, each × scalar, rarity-filtered, floored. `onScrapItem` can veto.

### 4.7 Audience / visibility
- Owner only: `RequestResult`, `SetInventoryFo4`, `ProgressionUpdate` (S8).
- Workshop container contents go to its occupant/peekers (F06) and to settlement members (F22).
- Nothing is broadcast to neighbours except the F07 furniture state.

### 4.8 NPC parity
N/A for crafting: NPCs do not craft, and hosted NPCs using workbenches play idles only (F07). Robot construction at the robot workbench (T2) creates a server-owned actor and is specified with F21; it will reuse `CraftItemFo4` with a `CNAM` of type NPC_ (F15-T12).

### 4.9 Gamemode API & server Papyrus
- `onCraft(actorId, itemId, count, recipeId, workbenchId, consumed[])` **blockable**. The first four arguments keep SkyMP's order; `consumed` lists `{baseId, count, source: "player"|"workshop"|"network"}`.
- `onScrapItem(actorId, workbenchId, item, count, yields[])` **blockable**.
- Settings (S22): `crafting.maxRequestsPerSec`, `crafting.xp` (per-station table, default from G-self measurement), `scrap.formula` (`"vanilla"`, or gamemode override via `mp.set(0, "scrapRules", …)`).
- `mp.get(actorId, "inventory")` shows the result (F04). There is no new crafting property.
- Papyrus events (S16): `Actor.OnPlayerUseWorkBench(akWorkBench)` when occupancy starts; `OnItemAdded`/`OnItemRemoved` for every change (F04-T09).
- Papyrus natives (PVM-014): `ObjectReference.GetComponentCount`, `RemoveComponents`, `RemoveItemByComponent` (through the ledger), `MiscObject.GetObjectComponentCount`; F4SE getters `MiscObject.GetMiscComponents`, `Component.GetScrapItem/GetScrapScalar`, `ConstructibleObject.GetConstructibleComponents/GetCreatedObject/GetCreatedCount/GetWorkbenchKeyword`.

### 4.10 Edge cases & failure modes
- **Concurrent use of one workshop container** from two benches: the server is single-threaded per tick, so the second request sees the post-first inventory and may fail with `InsufficientComponents` + correction.
- **Workbench removed** (scrapped in workshop mode, disabled) while occupied: F22/F07 release occupancy; pending requests fail with `NotOccupant`.
- **Disconnect mid-craft:** no partial state. The occupancy sink releases the bench (existing `OccupantDisableEventSink`).
- **Store all junk** (ContainerMenu button on the workshop container) is F06 transfers (`PutItemFo4` per junk stack), not a crafting request.
- **Hot reload / restart:** no crafting state outside inventories. The recipe index is rebuilt lazily.
- **Unknown or removed plugin recipe** → `UnknownRecipe`.
- **Over-encumbrance** does not block crafting (vanilla).

### 4.11 Performance budget
- `CraftItemFo4` ≤ 16 B; `RequestResult` ≤ 64 B typical.
- Recipe lookup O(1) via `RecipeIndex`, replacing the full COBJ scan.
- Ledger ≤ 50 µs for a typical recipe (≤ 6 needs × ≤ 200 inventory entries × 3 sources). Index memory ≤ 2 MB for Fallout4.esm + DLC COBJs `[inference]`.

## 5. Engine / platform work required
- Workbench menu hooks (PLAT-042 menus): craft confirm in `CookingMenu`/`WorkbenchMenuBase`, scrap confirm in `ExamineMenu`, reading `ModChoiceData.recipe` and the selected stack. Natives `refreshWorkbenchMenu()` and `getWorkbenchMenuState()`.
- Confirm the `CookingMenu` RTTI name and that it serves chem and cooking (G-self menu-open log).
- `getInventory`/`AddItemEx` with `ItemKey` (PLAT-080) for capture and apply.
- Optional pre-commit cancel of the vanilla craft (RE on `ConsumeSelectedItems`/the craft-confirm path).

## 6. Tests
- `L-unit` (`[F15]`):
  - serialize round trips for `CraftItemFo4`, `ScrapItem`, `RequestResult` (binary + JSON);
  - `ComponentLedger`: raw component before junk; deterministic break order; leftovers to workshop vs player; shortfall leaves every source unchanged;
  - accept: craft at a chem station changes the inventory, fires `onCraft`, sends `RequestResult ok` + `SetInventoryFo4` to the owner only, and awards XP through F19;
  - every reject (not occupant, too far, bench mismatch, `HasPerk` false, insufficient, rate limit, gamemode veto) asserts `RequestResult` + `SetInventoryFo4`;
  - duplicate nonce returns the cached result without double consumption;
  - scrap yields per Scrapper rank 0/1/2 with floor rounding; legendary and equipped rejection;
  - workshop container as source, with and without the F22 `craft` permission;
  - persistence round trip: craft → restart → inventories identical;
  - B2 regression: a multi-entry `AddItems` (outputs + leftovers) keeps every entry.
- `L-fixture`: synthetic plugin (ESPM-002) with CMPO, MISC `CVPA`, COBJ `FVPA`/`INTV`/CTDA, and FURN `WBDT`.
- `D-real` (`[fo4data]`): stimpak and purified water recipes; scrap of a 10mm pistol with an automatic receiver.
- `L-int`: a bot crafts at a chem station using components stored in a workshop container; a second bot occupying that container receives updated contents.
- `L-ts`: capture → message builder; occupancy filter; reconcile on `RequestResult`.
- `G-self`: log the menu class names on station open; read `ModChoiceData.recipe`; one craft round trip with correction.
- `G-manual`: two players at one settlement craft from the same workshop container; one lacks Chemist and is refused; scrap a modded weapon with and without Scrapper.

## 7. Tasks
- [ ] **F15-T01** Messages `CraftItemFo4` (76), `ScrapItem` (86), `RequestResult` (107) + TS mirrors, protocol bump — S — Depends: NET-002, NET-003, F04 (`ItemKey` serializer) — Verify: L-unit, L-ts — Files: skymp5-server/cpp/messages/{CraftItemFo4Message.h,ScrapItemMessage.h,RequestResultMessage.h}, Messages.h, MsgType.h; falloutmp-client/src/services/messages/
  - Accept: binary and JSON round trips; FO4-only registration; error-code enum shared in one header.
- [ ] **F15-T02** `RecipeIndex` (bench keyword → COBJ, created form → COBJ, item → scrap recipe; global ids) — S — Depends: ESPM-008 — Verify: L-unit, L-fixture — Files: skymp5-server/cpp/server_guest_lib/fo4/crafting/RecipeIndex.{h,cpp}
  - Accept: fixture lookups O(1); FLST created objects indexed; no `NAM1` read on FO4.
- [ ] **F15-T03** `ComponentLedger` (pure): `Take(sources, needs)`, leftovers, `ScrapYield(item, scrapperRank)` — M — Depends: ESPM-008, F04 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/fo4/crafting/ComponentLedger.{h,cpp}
  - Accept: determinism (same inputs → same breaks); shortfall is a no-op; yields match the wiki rarity table for the fixture.
- [ ] **F15-T04** FO4 `CraftService` path: occupancy/reach, bench check via `GameProfile::Crafting()`, conditions, ledger, outputs, nonce cache, `RequestResult` + correction, extended `onCraft` — M — Depends: F15-T01…T03, REF-013, REF-009, F07-T04, NET-007 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/{CraftService.{h,cpp},ActionListener.cpp,gamemode_events/CraftEvent.cpp}
  - Accept: all §6 craft cases pass; Skyrim `[Craft][espm]` tests unchanged.
- [ ] **F15-T05** Scrap handler + `onScrapItem` event — S — Depends: F15-T03, F15-T04 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/fo4/crafting/ScrapService.{h,cpp}, gamemode_events/ScrapItemEvent.{h,cpp}
- [ ] **F15-T06** FO4 crafting condition functions (`GetValue` 14, `GetStageDone` 59, `GetIsID` 72, `GetGlobalValue` 74, `HasPerk` 448, `HasKeyword` 560), unless F19 already delivered them — M — Depends: ESPM-011, REF-009, SRV-010, SRV-021 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/condition_functions/fo4/*
  - Accept: a fixture COBJ gated by `HasPerk(Chemist01)` passes and fails as expected.
- [ ] **F15-T07** `IComponentSource` provider interface so F22 can add the workshop container and supply network — S — Depends: F15-T03 — Verify: L-unit — Files: fo4/crafting/ComponentSource.h
- [ ] **F15-T08** Crafting XP through F19 (`AwardXp(source="craft")`) + `PlayerProfile.stats` counters — S — Depends: F15-T04, F19, SRV-060 — Verify: L-unit
- [ ] **F15-T09** Platform workbench menu hooks, `craftConfirm`/`scrapConfirm` events, `refreshWorkbenchMenu`, `CookingMenu` RTTI check; pre-commit cancel if RE allows — M — Depends: PLAT-042, PLAT-080 — Verify: W-ci, G-self — Files: fallout4-platform/src/.../WorkbenchApi.cpp, hooks
  - Accept: the self-test logs the recipe FormID for a chem craft and a cooking craft, and the scrapped item's `ItemKey`.
- [ ] **F15-T10** Client `craftService.ts` (FO4): capture, send, reconcile with `RequestResult`, HUD messages — M — Depends: F15-T01, F15-T09, CLI-050 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/craftService.ts
- [ ] **F15-T11** Server Papyrus natives and `OnPlayerUseWorkBench` — S — Depends: PVM-014, F15-T03 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/script_classes/{PapyrusObjectReference.cpp,PapyrusMiscObject.cpp,PapyrusComponent.cpp,PapyrusConstructibleObject.cpp}
- [ ] **F15-T12** (T2) Robot workbench: `CraftItemFo4` with an NPC_ `CNAM` → owned robot actor; robot OMODs via F16 — L — Depends: F21, F13, F16-T04 — Verify: L-unit, G-manual
- [ ] **F15-T13** `G-manual` crafting/scrap scenario script and sign-off — S — Depends: F15-T10 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F15-crafting.md

## 8. Open questions & risks
- `CookingMenu` class identity and whether the craft confirm can be cancelled before the local commit (RE). Fallback: local commit + authoritative overwrite, with brief count flicker.
- Crafting XP values and whether INT already applies (G-self, F19).
- Vanilla semantics of `CNAM` = FLST/LVLI for non-scrap recipes `[inference]`. Verify against D-real data.
- Where component **rarity** comes from: probably the CMPO `GNAM` scalar GLOB (common/uncommon/rare globals adjusted by Scrapper) `[inference]`. Confirm with D-real before fixing the `ScrapYield` rule.
- Vanilla leftover routing at non-settlement benches: player inventory is assumed `[inference]`.
