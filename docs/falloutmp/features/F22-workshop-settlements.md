# F22 — Workshop & Settlements

| Field | Value |
|---|---|
| Tier | **T1:** claim/ownership/permissions, build mode (place, move, scrap, store, repair), budget, shared workshop inventory, power grid, ratings and daily update, settlers (abstract + hosted), turrets, persistence, streaming. **T2:** attacks, supply lines/provisioners, DLC objects (logic gates, conveyors, manufacturing, cages/arenas, Vault 88, raider outposts/vassals), PvP capture |
| Target level | L4 |
| SkyMP analogue | None (SkyMP level L0). Building blocks: dynamic placed forms (`mp.place`, `0xFF…` ids), container occupancy (`MpObjectReference.cpp:1512-1568`), trigger primitives (`server_guest_lib/Primitive.cpp`). Prior art: cokwa broadcast `formType == 36` statics only; FO4_Wrld left it open (reference/prior-art.md §3.3, §5.4 item 6) |
| Milestone | M11 (T1, exit: 500 objects per settlement, 10 settlements per server), M12 (T2) |
| Workstreams | SRV, PLAT, CLI, NET, ESPM, GM, FRONT, PVM, QA |
| Depends on | F04, F06 (containers), F07 (activation, switches, doors), F10 (turrets), F13 (settler hosting), F14 (cell reset excludes build areas), F15 (`ComponentLedger`, `RecipeIndex`, `RequestResult`), F19 (Local Leader, Charisma), F23 (settlement stores), F25 (server clock), PLAT-087, SRV-060, SRV-070, SRV-080, REF-012, REF-020, ESPM-005, ESPM-008, ESPM-010, ESPM-016, PVM-015 |
| References | reference/fo4-systems-world-economy.md **S5** (all), S4, S16, §3.2–3.7, §4, §5 items 1–3 and 9; reference/fo4-data-formats.md §4.3 (REFR `XPRM`, `XWPG`/`XWPN`, `XPLK`, `XLKR`), §4.13 (COBJ), §4.17 (FURN/STAT/SCOL/MSTT/PKIN); reference/papyrus-api-map.md §1.2 ObjectReference (workshop natives/events) and §1.9 (workshop scripts); reference/commonlib-port-map.md §4.2–4.3 (`Workshop::Register*`, `WorkshopMenu`); reference/fo4-systems-combat-character.md §17 (turrets); reference/prior-art.md §3.1.10 (removal idiom), §3.3; 02-architecture.md ADR-010, ADR-012; 05-risks-open-questions.md R7 |

## 1. Summary
Players claim a workshop (workbench) in a cleared settlement and then build in its area with the vanilla Workshop menu:
- walls, floors, beds, crops, water pumps, generators, wires, lights, turrets, shops;
- they can move, scrap, store and repair objects, including pre-placed junk such as cars and trees.

The server owns every settlement. It checks permissions, the budget and the build area, consumes components from the shared workshop inventory, creates the objects, and simulates power, ratings, settlers and the daily economy on the server clock, whether or not anyone is nearby.

Everyone near a settlement sees the same objects, wiring and powered lights. Late joiners and players arriving later stream the whole settlement in efficiently: decor goes in chunked snapshots; interactive objects (containers, beds, doors, turrets, generators) become full server forms. Settlements persist across restarts. T2 adds attacks and supply lines.

## 2. Vanilla Fallout 4 behaviour
- **Architecture:** settlements are driven by Papyrus:
  - `WorkshopParentScript` (quest; master lists, rating table, daily update spread over 12 h, attacks, recruitment);
  - `WorkshopScript` on each workbench (`OwnedByPlayer`, `DailyUpdate`, `CheckForAttack`, `GetMaxWorkshopNPCs = 10 + Charisma`);
  - `WorkshopObjectScript` (power, assignment, destruction);
  - `WorkshopNPCScript` (settlers);
  - `workshopObjectActorScript` (turrets).
  Full data exists only for the loaded workshop [src: F4SE vanilla/WorkshopParentScript.psc, WorkshopScript.psc, WorkshopObjectScript.psc, WorkshopNPCScript.psc] (world-economy S5(a)).
- **Ratings** are AVs 0–44 on the workbench ref (0 food, 1 happiness, 2 population, 3 safety, 4 water, 5 power, 6 beds, … 44 missing safety) [src: WorkshopParentScript.psc:44-148]. The engine recalculates resource AVs from owned resource objects (`RecalculateResources`, `GetWorkshopResourceObjects`).
- **Formulas** (vanilla script constants):
  - happiness needs worth up to 20 each, base max 80;
  - storage caps: food 10 + 1/pop, water 5 + 0.25/pop, scavenge 100 + 5/pop;
  - vendor income needs pop ≥ 5 and is ≤ 50 caps/day;
  - attract chance 0.1 × happiness factor, ≤ 5 unassigned;
  - attack chance per daily update = max(0.02, 0.02 + 0.001·(food+water) − 0.01·safety − 0.005·pop), none within 7 days of the last or with 0 population
  [src: WorkshopScript.psc:128-186, 1399-1480] (world-economy S5(a)).
- **Claiming:** activating the workbench of a cleared location with `EnableAutomaticPlayerOwnership` sets `OwnedByPlayer`; others need quests [src: WorkshopScript.psc:351-390].
- **Build mode:** `WorkshopMenu` (`lastBudget`, `inEditMode`; sinks `BuildableAreaEvent{exit}`, `PlacementStatusEvent`). Leaving the area for 5 s exits. Stored objects re-place for free.
  - Placement status enum: `kValidPlacement, kFloating, kIntersecting, kOutsideBuildArea, kMustSnap, kSplineTooLong, …`.
  - Engine functions: `PlaceCurrentReference`, `ScrapReference(ctx, ref, rewards*)`, `IsLocationWithinBuildableArea`.
  - Events: `ItemPlacedEvent`/`ItemMovedEvent`/`ItemDestroyedEvent`/`WorkshopModeEvent`, with register helpers.
  [src: CLF4 W/Workshop.h, W/WorkshopMenu.h] (commonlib-port-map §4.2–4.3).
- **Budget:** hidden AVs `WorkshopMaxTriangles/CurrentTriangles/MaxDraws/CurrentDraws`. Scrapping or storing refunds [src: WorkshopParentScript.psc AV group ~449-456].
- **Build area:** primitive refs (`XPRM` box/sphere/ellipsoid) tied to the workbench `[inference: by linked ref keyword; verify with D-real]`. Built objects link to their workshop with keyword `WorkshopItemKeyword` `00054BA6`.
- **Power:**
  - generators have AV `PowerGenerated`, consumers `PowerRequired`;
  - wires are `BNDS` spline refs; the workbench REFR stores the grid (`XWPG` count, `XWPN{node1, node2, line}`);
  - power also flows through snapped conduits and pylon radius; runtime `PowerUtils::PowerGrid{adjacencyMap, currentlyPowered, capacity, load}`;
  - Papyrus `IsPowered`, `OnPowerOn/OnPowerOff`.
  F4SE wire natives broke on NG 0.7.0 and were fixed in 0.7.2 [src: F4SE f4se_whatsnew.txt:25-56] (world-economy S5(a); data-formats §4.3).
- **Settlers:** a recruitment beacon (powered) attracts settlers up to 10 + Charisma. Jobs are object ownership; beds are auto-assigned. Command mode assigns a settler to an object.
- **Supply lines (T2):** Local Leader 1 (`0004D88D`) makes a settler a provisioner; the network shares junk/aid/misc and surplus food/water. Local Leader 2 (`001D2468`) unlocks stores and workbenches [web: wiki:Supply_line, wiki:Local_Leader].
- **Shared inventory:** the workbench container (`WorkshopLinkContainer` or the workbench itself) is shared by every station in the settlement [src: WorkshopScript.psc:614-620].
- **Sites:** 30 base-game workshops plus DLC (Far Harbor 4, Vault 88 with 4 workbenches, Nuka-World outposts, Automatron) [web: wiki:Workshop_(Fallout_4)].
- **SP assumptions that break:** one player owns everything; there is one "current workshop"; ratings are computed only for loaded cells (the documented partial-load corruption); placement, budget, component use and power are local and saved in the save; Papyrus timers assume a local clock that jumps. Vanilla scripts are blocked on clients (ADR-011).

## 3. SkyMP baseline
- No settlement concept. Reused pieces:
  - dynamic server forms (`0xFF…` ids) and `MpChangeFormREFR` persistence for interactive objects;
  - container occupancy (B3) for the workshop container;
  - `Primitive` box/sphere tests for build-area checks;
  - grid streaming (`CreateActor`/`DestroyActor`);
  - host model for settlers (F13);
  - the deferred, coalesced send channel for bursty snapshots (S19);
  - `ISaveStorage` drivers for a new record collection.
- **Gaps to fix:**
  - **B8:** ESM refs of `STAT/SCOL/MSTT/TERM` are never loaded, so pre-placed scrappables are invisible to the server. Fixed via REF-012 for build areas only, loaded lazily.
  - **B2:** the multi-entry `AddItems` bug loses scrap refunds (F04).
  - **B4:** no activation distance (F07-T04).
  - **File driver:** one JSON per change form would explode with thousands of placed decor objects (`FileDatabase.cpp`), so decor lives inside one workshop record.

## 4. Design

### 4.1 Authority model
- **Class A (server):** ownership, ACL, the object set (existence, base, transform after validation, stored, destroyed), budget, workshop inventory, wires and power state, ratings, settlers (existence, job, bed), daily production, attacks (T2), supply lines (T2).
- **Class B (owner client, validated):** the exact placement transform. The server cannot run Havok placement, so it validates bounds, area and snap plausibility, then accepts.
- **Class C:** settlers and turrets while hosted (F13). The server keeps assignments and rules.
- **Class D:** build-mode UI, placement preview, engine-side visual power on clients (overridden where possible), conveyors/ball tracks physics (T2).

### 4.2 Server state & persistence
`MpWorkshop` is an ADR-010 record, one per workbench, in a new `workshops` save-storage collection (file driver: one JSON per workshop).

| State | Type | Where (class/field) | Persisted | Default source |
|---|---|---|---|---|
| Identity: workbench, location, container, build-area primitives, area cell set | FormDesc ×3, list, set | `MpWorkshop.{workbench, location, container}`, `areaPrimitives`, `areaCells` | ids yes; primitives/cells derived | ESM (workbench REFR, linked refs, LCTN) |
| Owner `{type: none|profile|group|faction, id}` | struct | `MpWorkshop.owner` | yes | none (or `workshops.preOwned`) |
| ACL `profileId|groupId → perms` (`build, scrap, container, craft, assign, admin`) | map | `MpWorkshop.acl` | yes | owner = all |
| Placed decor objects `{refId, baseId, recipeId, pos, rot, scale, flags(stored, destroyed, powered, interactive), assignedActor, resourceDamage}` | vector | `MpWorkshop.objects` | yes | empty |
| Interactive placed objects (containers, beds, crafting stations, doors, generators with switches, turrets, crops, pumps, beacons, store stalls) | `MpObjectReference` / `MpActor` + `workshopId` | `MpChangeFormREFR.workshopId` (new optional) + own fields (`inv`, `powered`, `destructionStage`) | yes | — |
| Scrapped pre-placed ESM refs | set<FormDesc> | `MpWorkshop.scrappedPrePlaced` | yes | empty |
| Stored objects `baseId → count` | map | `MpWorkshop.stored` | yes | empty |
| Wires `{wireRefId, a, b, splineBaseId}` | vector | `MpWorkshop.wires` | yes | ESM `XWPN` grid |
| Power components `{capacity, load, powered[]}` | derived | `PowerGraph` (runtime) | no (recomputed on load) | — |
| Ratings (45 AVs) | float[45] | `MpWorkshop.ratings` | yes | 0 / recomputed |
| Budget `{draws, tris, objects}` cur/max | struct | `MpWorkshop.budget` | cur derived, max yes | cost table + `workshops.maxObjects` |
| Settlers | actor ids + `SettlerState{workshopId, job, bed, flags}` | `MpWorkshop.settlers`; actor `MpChangeFormREFR.settler` (new optional) | yes | none |
| Daily-update clock | double game time | `MpWorkshop.lastDailyUpdate` | yes | claim time |
| Attack state (T2) | struct | `MpWorkshop.attack` | yes | none |
| Supply links (T2) `{otherWorkshop, provisionerId, progress}` | vector | `MpWorkshop.links` | yes | none |
| Record version | u32 (bumped on every object/wire change) | `MpWorkshop.version` | yes | 0 |

**Save policy (deviation from 01-sync-standard §8 rule 4, "workshop changes on the next tick"):** saves are coalesced to ≤ 1 per `workshops.saveIntervalMs` (default 1000 ms) per workshop, and are immediate on claim/abandon/ACL change. A 3k-object record is ~300 KB and building runs at up to 10 changes/s. A crash loses ≤ 1 s of building, never components without the object: the workshop container and the record are flushed in the same batch.

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `WorkshopMode` (93) | C→S | `workshopRefId`, `enter` | R | on `WorkshopModeEvent` | new (registry) |
| `WorkshopMode` (93) | S→C | `workshopRefId`, `enter`, `allowed`, `reason` u8, `perms` u16 | R | reply; forced exit (area left > 5 s, ACL revoked) | new |
| `WorkshopPlace` (94) | C→S | `nonce`, `workshopRefId`, `recipeId`, `baseId`, `fromStored`, `pos[3]`, `rot[3]`, `scale?`, `snap? {targetRefId, connectPoint u8}` | R | per placed object; ≤ 10/s | new |
| `WorkshopEdit` (95) | C→S | `nonce`, `workshopRefId`, `op` u8 (`move`, `scrap`, `store`, `repair`), `items[≤64] {refId, pos[3]?, rot[3]?}` | R | per edit; multi-select moves | new |
| `WorkshopWire` (97) | C→S | `nonce`, `workshopRefId`, `op` (`connect`, `disconnect`), `a`, `b`, `splineBaseId`, `wireRefId?` | R | per wire | new |
| `WorkshopWire` (97) | S→C | `workshopRefId`, `version`, `added[] {wireRefId, a, b, splineBaseId}`, `removed[]` | R | to snapshot holders | new |
| `WorkshopManage` (109) | C→S | `nonce`, `workshopRefId`, `op` u8 (`claim`, `abandon`, `setAcl`, `assign`, `unassign`, `supplyLine` (T2), `cancelSupplyLine` (T2)), `actorId?`, `objectRefId?`, `targetWorkshopId?`, `profileId?`, `perms?` | R | user action | **new (allocated here)** |
| `RequestResult` (107) | S→C | `nonce`, `requestType`, `ok`, `error`, `refId` (created object), `items[]` (refunds) | R | per request | reused (F15) |
| `WorkshopState` (96) | S→C | `workshopRefId`, `version`, `owner {type, id, name}`, `yourPerms`, `budget {cur/max draws, tris, objects}`, `ratings[] {idx u8, value}` (delta; full on first send), `power[] {componentId, capacity, load}`, `flags` (claimable, underAttack) | R | on change, coalesced per tick; daily | new (registry) |
| `WorkshopObjects` (108) | S→C | `workshopRefId`, `version`, `kind` (`snapshot`, `delta`), `chunk`/`chunkCount` u16, `added[] {refId, baseId, pos[3], rot[3], scale?, flags u16}`, `moved[] {refId, pos, rot}`, `removed[]`, `flagsChanged[] {refId, flags}`, `scrappedPrePlaced[]`, `wires[]` (snapshot only) | R | on stream-in (chunks of ≤ 96 objects via the deferred channel); deltas per tick | **new (allocated here)** |
| `CreateActorFo4` (64) / `DestroyActor` / `UpdateProperty` | S→C | interactive objects, settlers, turrets; props `powered`, `destructionStage`, `workshopId`, `inventory` | R | grid subscription | reused |
| `SetInventoryFo4` (68) | S→C | builder inventory; and with `refId` = workshop container (`version`) to its occupant/peekers | R | on change / correction | reused (F04/F06) |
| `Activate` (6) | C→S | workbench (container/transfer), switches, crops, beds | R | user action | reused (F06/F07) |

### 4.4 Client capture (owner side)
- **Build mode:**
  - `WorkshopModeEvent{workshop, start}` sink → `WorkshopMode`; until `allowed` arrives the client keeps the menu but places nothing;
  - `BuildableAreaEvent{exit}` → `WorkshopMode{enter=false}`.
- **Placement:**
  - `ItemPlacedEvent{workshop, placedItem}` (or a hook on `PlaceCurrentReference`) reads the base, transform, current recipe (`GetPlacementItem`), stored/new and the snap target;
  - sends `WorkshopPlace` and marks the local ref as a **ghost** keyed by nonce;
  - on `RequestResult ok`, adopts it as server `refId`; otherwise removes it (removal idiom, prior-art §3.1.10).
- **Edits:** `ItemMovedEvent` (multi-select batched per frame) and `ItemDestroyedEvent` (scrap) / store → `WorkshopEdit`.
- **Wires:** wire creation/removal → `WorkshopWire`.
- **Assignment:** `TESCommandModeGiveCommandEvent` (settler → object) → `WorkshopManage assign`.
- **Engine side effects** (component deduction, budget AVs, local power) are not trusted. The authoritative `SetInventoryFo4`, container contents and `WorkshopState` overwrite them.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Stream-in:** when the client's 3×3 grid neighbourhood first touches a cell in `areaCells`, the server sends a `WorkshopObjects` snapshot. The client applies it with the native `placeWorkshopObjects(batch)`, which:
  - places ≤ 50 objects per frame, keyframed;
  - links each object to the local workbench with `WorkshopItemKeyword`, so engine snapping and visuals work;
  - creates wires with `createWire(a, b, spline)`;
  - disables `scrappedPrePlaced` ESM refs without fade.
  Interactive objects arrive as normal `CreateActorFo4` forms.
- **Deltas:** apply `WorkshopObjects` deltas in `version` order. A gap in versions → the client asks for a resnapshot by re-sending `WorkshopMode{enter=false}`; the server replies with a full snapshot.
- **Power visuals:** `powered` flags call `setPowered(ref, bool)` (PLAT-087; RE needed to force the engine state). Otherwise the engine computes from the same wires (risk 2, world-economy §5).
- **Budget bar:** `WorkshopState.budget` is written into the local workbench's budget AVs.
- **Owner correction:**
  - a rejected place deletes the ghost; a rejected move snaps the ref back to the server transform from the record;
  - inventories are re-sent;
  - `WorkshopMode{allowed=false}` → `RequestExitWorkshop`.
- **Stream-out:** decor is removed when the area leaves the neighbourhood (`removeWorkshopObjects`).
- **Reconnect / restart:** state comes from the record; the snapshot is resent on subscribe.

### 4.6 Validation & anti-cheat
Every rejection sends `RequestResult{ok=false, error}` plus the state needed to undo the client's local change: delete the ghost (implicit), restore the transform via a `WorkshopObjects` delta to the requester, and re-send `SetInventoryFo4` and the workshop container contents (S12).
1. **Identity and permission:** sender's own actor (S6). The actor has the needed ACL permission (`build` for place/move/wire/repair; `scrap` for scrap/store; `assign`; `admin` for ACL); `onWorkshop*` gamemode veto.
2. **Build mode:** the actor is in server-acknowledged build mode at this workshop, its server position is inside the build area, and the workshop is owned. Leaving the area for > 5 s → forced exit.
3. **Rate:** ≤ 10 place/edit/wire requests/s per actor; per-request `items` ≤ 64; nonce dedupe (F15 cache).
4. **Transform** (place and move):
   - finite values; |pos| within the world bounds;
   - inside an area primitive (tolerance = base `OBND` half-extent);
   - ≤ 4096 u from the actor;
   - scale ∈ [0.1, 10] (vanilla scale unsupported → 1, unless `workshops.allowScale`).
   Optional AABB overlap check against load doors, workbenches and other players (`workshops.overlapCheck`; R7/risk 1).
5. **Snap:** the target exists in this workshop and the distance ≤ `snapPointQueryRadius` + bounds; `mustSnap` bases need a snap.
6. **Recipe:** the COBJ exists, its `BNAM` is a workshop keyword, its `CNAM` = `baseId` (or the FLST contains it), and its CTDA passes (e.g. Local Leader 2 for stores).
   - `fromStored` → `stored[baseId] ≥ 1`;
   - otherwise components via `ComponentLedger` with sources **workshop container → builder inventory → supply network (T2)** `[inference: vanilla source order]`.
7. **Budget:** `cur + cost(base) ≤ max` per dimension; `objects ≤ workshops.maxObjects` (default 1000). Unique-object limits are from GameProfile.
8. **Wire:**
   - both endpoints are powerable objects of this workshop;
   - length ≤ the engine maximum (`kSplineTooLong`, from the BNDS/GameProfile constant);
   - ≤ N wires per connector (GameProfile);
   - no duplicates.
9. **Scrap/store:**
   - the target is a placed object, or a pre-placed ESM ref in the area with a scrap recipe (REF-012), and is not quest-protected;
   - not an actor (settlers are never scrapped) and not the workbench;
   - refunds: player-built → the build COBJ's components `[inference: full refund]`; pre-placed → scrap recipe × scalar × rarity (F15 `ScrapYield`);
   - a container's contents move to the workshop container;
   - assignments are cleared.
10. **Repair:** the object has `resourceDamage` > 0 or is destroyed; components come from the repair cost `[inference: build COBJ]`.
11. **Claim:**
    - per `workshops.claimRule` (`firstClaim`, `gamemode`, `adminOnly`);
    - the location has no living boss-type NPC (`XLRT` boss ref types of the LCTN, server-known actor state);
    - claims per player ≤ `workshops.maxPerPlayer`;
    - not owned by someone else (PvP capture is a T2 gamemode rule through `onWorkshopClaim`).
    Abandonment happens after `workshops.abandonDays` with the owner offline.
12. **Commit is atomic** across the record, the inventories and the budget (S13). Version++ and a delta to snapshot holders follow.

### 4.7 Audience / visibility
- `WorkshopObjects`/`WorkshopWire` deltas go to every client holding the snapshot: subscribed through `areaCells` ∩ its 3×3 neighbourhood. The settlement is one streaming unit (S7).
- Interactive objects and settlers follow normal grid visibility.
- `WorkshopState` goes to the owner, ACL members and anyone currently in build mode at the workshop (ratings are private settlement data, S8).
- `RequestResult` goes to the requester only. Workshop container contents go to its occupant/peekers (F06).

### 4.8 NPC parity
- **Settlers** are server-owned `MpActor`s (`settler` state). When a player is within the hosting radius they are hosted like other NPCs (F13; class C, host validated on every NPC message); otherwise they are **abstract** (not hosted, position parked at the job or bed marker) and still counted by the daily update. That removes the vanilla partial-load bug.
- **Recruitment** (daily update): the beacon is powered; pop < 10 + owner CHA (`workshops.popBase`, F19 AV); unassigned ≤ 5; chance 0.1 × happiness factor. The actor is spawned from the settler leveled list on the server (F14) at the beacon.
- **Assignment:** `WorkshopManage assign` (from command mode) validates that both belong to the workshop and the job/bed is free; it sets the object's `assignedActor` and the settler's `job`/`bed`. Hosts get the job and bed as linked refs for idles.
- **Turrets** are workshop-built actors: server `MpActor`, faction = the workshop's owner faction, hosted (F10/F13). Their defense AV feeds `safety`.
- **Provisioners (T2)** are abstract route progress between workshop markers and become hosted only when a player is near the route.

### 4.9 Gamemode API & server Papyrus
- **Events** (blockable unless noted): `onWorkshopClaim(actorId, workshopId)`, `onWorkshopEnter(actorId, workshopId)`, `onWorkshopPlace(actorId, workshopId, baseId, pos, rot)`, `onWorkshopMove`, `onWorkshopScrap(actorId, workshopId, refId, yields)`, `onWorkshopStore`, `onWorkshopWire`, `onWorkshopAssign(actorId, workshopId, settlerId, objectId)`, `onWorkshopDailyUpdate(workshopId, report)` (**not blockable**), T2: `onWorkshopAttack(workshopId, factionId, strength)`, `onSupplyLine`.
- **Properties:** `mp.get(workbenchId, "workshop")` → `{owner, acl, ratings, budget, settlers, objectCount, version}`; `mp.set(workbenchId, "workshop", {owner?, acl?, ratingsOverride?, maxObjects?})`; `mp.get(refId, "workshopId")`.
- **Settings** (SRV-002 `workshop.*`, GM-030): `claimRule`, `maxPerPlayer`, `abandonDays`, `maxObjects`, `saveIntervalMs`, `allowScale`, `overlapCheck`, `popBase`, `dailyUpdate.staggerHours`, `attacks` (T2), `supplyLines` (T2), `preOwned`.
- **Papyrus events** (S16): `OnWorkshopMode(bool)` on the workbench; `OnWorkshopObjectPlaced/Moved/Destroyed/Grabbed/Repaired(ref)` on the workbench and the object; `OnPowerOn(akGenerator)`/`OnPowerOff`. The vanilla custom-event names (`WorkshopObjectBuilt`, `WorkshopDailyUpdate`, …) are raised on a FalloutMP `WorkshopParent` stand-in quest, so ported mod scripts can subscribe. `WorkshopParentScript` itself is never run (ADR-012).
- **Papyrus natives** (PVM-015): `GetWorkshopOwnedObjects`, `GetWorkshopResourceObjects`, `RecalculateResources`, `WaitForWorkshopResourceRecalc` (returns immediately), `StoreInWorkshop`, `IsWithinBuildableArea`, `IsPowered`, `HasSharedPowerGrid`, `GetValue` on rating AVs, `StartWorkshop` (→ client snippet, S15).

### 4.10 Edge cases & failure modes
- **Two builders at once:** requests are serialized; a move of an object another builder just scrapped → `ItemNotFound` + snap-back.
- **Builder disconnects in build mode:** the mode ends; ghosts were never committed.
- **Owner offline:** the daily update still runs, and the settlement keeps producing into its container (capped by storage caps).
- **ACL revoked mid-build:** forced `WorkshopMode{allowed=false}`.
- **Workbench container occupied** by a player in ContainerMenu while building consumes components: the occupant receives the updated contents (F06).
- **Cell reset** (F14/SRV-080) skips cells in `areaCells` for workshop-owned objects; vanilla ESM loot inside the area still follows its own rules.
- **Plugin removed** (base no longer exists): the object is dropped on load with a warning; its budget is refunded.
- **Snapshot during heavy building:** deltas carry `version`; a client that misses one resnapshots.
- **Server restart:** the power graph and ratings are recomputed from the record; settlers are restored as abstract and hosted again when players arrive.
- **Interior settlements** (e.g. Home Plate, Vault 88): `areaCells` is the interior cell id; the same logic applies.

### 4.11 Performance budget
- **Snapshot:** ≈ 34 B per decor object (refId 4, base 4, pos 12, rot 12, flags 2). 2,000 objects ≈ 68 KB in ≤ 21 chunks, ≤ 8 chunks per tick via the deferred channel (S19), so it arrives in ≤ 1 s at 100 KB/s.
- **Deltas:** ≤ 64 B per object.
- **Client apply:** ≤ 50 placements per frame; a 2k-object settlement is applied in ≤ 40 frames.
- **Server:**
  - place validation ≤ 50 µs;
  - power recompute is O(objects in the touched component), ≤ 1 ms for 1k nodes;
  - daily update ≤ 2 ms per workshop, staggered;
  - memory ≈ 100 B per decor object.
- **Persistence:** a 3k-object record serializes in < 50 ms (world-economy S5 tests).
- Interactive objects ≤ ~200 per settlement keep `CreateActorFo4` traffic bounded (R7).
- M11 exit target: 10 settlements × 500 objects with 20 bots, within the 00-vision §7 targets.

## 5. Engine / platform work required
- **PLAT-087:**
  - workshop event sinks (`ItemPlaced/Moved/Destroyed`, `WorkshopModeEvent`, `BuildableAreaEvent`, `PlacementStatusEvent`);
  - placement intent capture and ghost adoption;
  - `placeWorkshopObjects(batch)` / `removeWorkshopObjects(ids)` with a per-frame budget and the `WorkshopItemKeyword` link;
  - `createWire`/`removeWire`, without F4SE latent wire functions;
  - `setPowered(ref, bool)` (RE);
  - budget AV write; `requestExitWorkshop`;
  - `TESCommandMode*Event` sinks.
- **ESPM gaps:** `BNDS`; REFR `XPRM`/`XWPG`/`XWPN`/`XPLK`; base-object `PRPS` resource AVs (food, water, safety, power, beds); workshop keywords looked up by EDID (ESPM-016).
- **Offline cost tool** (needs the user's game data, D-real): estimate draws/triangles per buildable base from NIFs into `data/fo4/workshop-costs.json`. Do not commit Bethesda data; the tool regenerates the file locally.

## 6. Tests
- `L-unit` (`[F22]`, `[Workshop]`, `[Power]`, `[WorkshopDaily]`):
  - message round trips (93–97, 108, 109);
  - place/move/scrap/store/repair accept paths, each with the correct audience (delta to snapshot holders, `WorkshopState` to members only);
  - every reject (no permission, not in build mode, outside area, too far, bad snap, budget, components, rate, duplicate nonce, gamemode veto), each asserting `RequestResult` + undo state;
  - budget accounting with refunds; pre-placed scrap (`scrappedPrePlaced`) refund rules;
  - claim rules and permission matrix;
  - `[Power]`: series wiring, insufficient capacity (stable allocation), pylon radius, conduit snap, switch off;
  - `[WorkshopDaily]` golden tests reproducing vanilla constants (happiness approach, storage caps, recruitment chance, vendor income) with a fixed RNG seed and fake clock (SRV-070);
  - snapshot chunking and versioned deltas; late-joiner snapshot contents;
  - settler assignment and abstract ↔ hosted transition (F13 host checks);
  - persistence round trip of a 3k-object workshop (< 50 ms) and backward-compatible load of a record missing new fields.
- `L-fixture`: synthetic plugin with a workbench REFR, area primitive, COBJ workshop recipes, a generator/consumer with PRPS, and `XWPN` grid.
- `L-int`: bot A claims and builds 300 objects; bot B in range receives deltas; bot C arrives later and receives the snapshot; the server restarts and C re-subscribes to the identical version.
- `L-ts`: ghost adoption/reject state machine; delta ordering and resnapshot on a gap; per-frame placement budget.
- `G-self`: workshop event sinks fire with the correct refs; `placeWorkshopObjects` of 500 objects without hitches > 50 ms; wire creation renders.
- `G-manual`: two players at Sanctuary: claim, build a powered house with lights and a turret, assign a settler; the second player sees everything and power state matches; both relog.

## 7. Tasks
**T1 — core build**
- [ ] **F22-T01** ESPM gaps: `BNDS`, REFR `XPRM`/`XWPG`/`XWPN`/`XPLK`, base-object `PRPS`, workshop keywords by EDID — M — Depends: ESPM-005, ESPM-010, ESPM-016 — Verify: L-fixture — Files: libespm/include/libespm/fo4/{BNDS.h,REFR.h}, libespm/src/fo4/*
  - Accept: fixture refs expose primitive, grid and spline data.
- [ ] **F22-T02** `MpWorkshop` record, `workshops` save-storage collection, JSON schema with defaults, coalesced save policy — L — Depends: REF-020, F00-T07 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/fo4/workshop/MpWorkshop.{h,cpp}, viet/include/save_storages/ISaveStorage.h (collection), database_drivers/*
  - Accept: round trip, backward-compatible load, 3k objects < 50 ms.
- [ ] **F22-T03** Workshop discovery: workbench refs, linked container, build-area primitives, `areaCells`, pre-placed scrappable index (lazy, REF-012) — M — Depends: F22-T01, REF-012 — Verify: L-unit, D-real — Files: fo4/workshop/WorkshopRegistry.{h,cpp}
- [ ] **F22-T04** Messages 93–97, 108, 109 + TS mirrors, protocol bump — M — Depends: NET-002, NET-003, F15-T01 — Verify: L-unit, L-ts — Files: falloutmp-server/cpp/messages/Workshop*Message.h, Messages.h; falloutmp-client/src/services/messages/
- [ ] **F22-T05** Ownership, claim/abandon, ACL (`WorkshopManage` claim/abandon/setAcl), `onWorkshopClaim`, settings — M — Depends: F22-T02, F22-T04, SRV-060, GM-030 — Verify: L-unit
- [ ] **F22-T06** Build mode: `WorkshopMode` handling, area tracking from movement, forced exit — S — Depends: F22-T03, F22-T04, F01 — Verify: L-unit
- [ ] **F22-T07** Place/move/scrap/store/repair handlers with validation, `ComponentLedger` provider, budget, refunds, `RequestResult` + undo corrections, gamemode events — L — Depends: F22-T05, F22-T06, F15-T03, F15-T07, NET-007 — Verify: L-unit
  - Accept: every §6 accept/reject case passes.
- [ ] **F22-T08** Budget cost table: loader + fallback per-type weights; offline generator tool (D-real) — M — Depends: F22-T07, DATA-010 — Verify: L-unit, D-real — Files: tools/workshop-cost-gen/, data/fo4/ (generated, not committed)
- [ ] **F22-T09** Interactive vs decor classification (GameProfile table); interactive objects as `MpObjectReference`/`MpActor` with `workshopId` — M — Depends: F22-T07, F06, F07 — Verify: L-unit
- [ ] **F22-T10** Streaming: subscription by `areaCells`, chunked `WorkshopObjects` snapshot via the deferred channel, versioned deltas, `WorkshopWire` deltas — M — Depends: F22-T04, F22-T09 — Verify: L-unit, L-int
- [ ] **F22-T11** `PowerGraph` (pure) + integration: wires, conduit snaps, pylon radius, switches, `powered` flags/property — M — Depends: F22-T01, F22-T07 — Verify: L-unit — Files: fo4/workshop/PowerGraph.{h,cpp}
- [ ] **F22-T12** Ratings + `WorkshopDailyUpdate` (pure) on the server clock, constants file, staggering, production into the container, golden tests — L — Depends: F22-T02, SRV-070, F25 — Verify: L-unit — Files: fo4/workshop/WorkshopDailyUpdate.{h,cpp}, data/fo4/workshop-constants.json (values only)
- [ ] **F22-T13** Settlers: recruitment, `WorkshopManage assign/unassign`, beds, abstract ↔ hosted lifecycle — L — Depends: F22-T12, F13, F14 — Verify: L-unit, G-manual
- [ ] **F22-T14** Shared workshop inventory: container occupancy rules, member visibility, client copy for workbench menus — S — Depends: F06, F15-T07 — Verify: L-unit
- [ ] **F22-T15** Turrets and defense: workshop-built turret actors with owner faction, hosted, `safety` rating — M — Depends: F22-T13, F10, F13 — Verify: L-unit, G-manual
- [ ] **F22-T16** Platform workshop natives and sinks (PLAT-087 scope in §5) — L — Depends: PLAT-087, PLAT-040 — Verify: W-ci, G-self — Files: fallout4-platform/src/.../WorkshopApi.cpp
  - Accept: the self-test places 500 objects in batches without a frame > 50 ms and logs placed/moved/destroyed events.
- [ ] **F22-T17** Client `workshopService.ts`: capture, ghost adoption, streaming apply, resnapshot, budget AVs, power visuals — L — Depends: F22-T04, F22-T16, CLI-050 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/workshopService.ts
- [ ] **F22-T18** Server Papyrus events/natives and the `WorkshopParent` stand-in quest — M — Depends: PVM-015, PVM-007, F22-T07 — Verify: L-unit
- [ ] **F22-T19** Gamemode reference handlers and permission prompts — S — Depends: GM-020, FRONT-006, F22-T05 — Verify: L-int
- [ ] **F22-T20** Load test: 10 settlements × 500 objects, 20 bots, snapshot/delta bandwidth metrics (SRV-050) — M — Depends: F22-T10, QA-020 — Verify: L-int
- [ ] **F22-T21** `G-manual` settlement scenario script and sign-off — S — Depends: F22-T17 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F22-settlements.md

**T2 — advanced**
- [ ] **F22-T22** Attacks: daily roll (vanilla formula), on-screen spawn of hosted attackers when members are near, off-screen resolution, damage to resource objects, `onWorkshopAttack` — L — Depends: F22-T12, F22-T15, F13, F14 — Verify: L-unit, G-manual
- [ ] **F22-T23** Supply lines/provisioners: `WorkshopManage supplyLine` (Local Leader 1), network graph closure for the ledger and surplus sharing, abstract provisioner routes — L — Depends: F22-T13, F19 — Verify: L-unit
- [ ] **F22-T24** DLC objects: logic gates/pressure plates as power-graph signals, manufacturing machine timers, cosmetic conveyors, cages/arenas, Vault 88 multi-workbench, raider outposts/vassals, PKIN expansion — XL — Depends: F22-T11, F22-T22 — Verify: L-unit, G-manual
- [ ] **F22-T25** Settlement stores: vendor stalls make an assigned settler a vendor (F23), daily store income — S — Depends: F22-T13, F23 — Verify: L-unit

- [ ] **F22-T26** Blueprint import/export (ADR-021): evaluate the Transfer Settlements blueprint JSON schema as the server's settlement export/import and backup format; admin command to export/import a settlement; validation against budget and ownership on import — M — Depends: F22-T10 — Verify: L-unit, G-manual

## 8. Open questions & risks
- **Companion-mod compatibility (ADR-021):** Workshop Framework, Place Everywhere and Scrap Everything change build-mode behaviour; they are deny-by-default in `workshop.clientMods` until tested (F22-T05 settings); Transfer Settlements' blueprint schema is evaluated in F22-T26.
- **R7 scale:** decor snapshots and the per-frame placement budget must hold at 2k objects; verify in F22-T16/T20.
- **Placement trust:** the server has no collision. Objects can be placed inside doors or walls. Mitigations: area bounds, optional AABB overlap check, ACL scoping, gamemode reporting.
- **Engine vs server power** may disagree visually (risk 2). `setPowered` needs RE; otherwise accept engine visuals and keep gameplay server-side.
- How the **build area** is linked to the workbench (keyword/primitive), how **boss refs** mark a location cleared, and the vanilla **component source order** are `[inference]`; confirm with D-real.
- Refund rules for scrapping player-built objects and repair costs are `[inference]`.
- The daily update re-implements `WorkshopScript.DailyUpdate`; golden tests guard against balance drift (world-economy §5 item 9).
- The save-policy deviation (§4.2) must be recorded as an S-checklist deviation in the review.
