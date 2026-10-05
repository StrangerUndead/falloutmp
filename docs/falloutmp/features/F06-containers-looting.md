# F06 — Containers, Looting, Pickup, Harvest & Drop

| Field | Value |
|---|---|
| Tier | T0 (quick-loot peek and stealing: T1-ready at M6; instanced loot: T2) |
| Target level | L4 |
| SkyMP analogue | Containers: CONT branch of `ProcessActivateNormal` (occupancy, 512 u), `PutItem`/`TakeItem` (8/9), `OpenContainer` (21) [src: MpObjectReference.cpp:1512-1568, 627-655]; pickup/harvest pickable branch [src: MpObjectReference.cpp:1449-1466]; drop `MpActor::DropItem` [src: MpActor.cpp:1588-1735]. SkyMP level L4 (reference/skymp-sync-inventory.md §2, Containers / Item pickup / Item drop rows) |
| Milestone | M6 |
| Workstreams | SRV, CLI, PLAT, NET, GM, PVM |
| Depends on | F04 (ItemKey, SetInventoryFo4), F07-T04 (reach/occupancy checks), F14 (reloot, leveled contents, dropped-item lifetime), F13 (corpses of hosted NPCs), NET-004, NET-005, NET-007, PLAT-080, PLAT-040 (`activate`, container/inventory events), SRV-080 |
| References | reference/fo4-systems-world-economy.md §1 (B3, B4, B5, B6, B8, B11, B12, B13), §2 S7, S15, S18, §3.1–3.3, §3.7; reference/skymp-sync-inventory.md §1.9, §1.12, §1.15 rows 9–11, §1.18, §2, §3.2 (I12, I15); reference/prior-art.md §3.1.11; reference/fo4-data-formats.md §4.3 (XOWN, XCNT, XLIB), §4.4 (ACBS No Loot), §4.14 (CONT flags), §4.17 (FLOR); reference/papyrus-api-map.md §4.3; ADR-019 |

## 1. Summary
Containers (chests, toolboxes, safes, desks), NPC corpses, world items and harvestable flora (tatos, mutfruit, corn…) are server-authoritative and shared by default. A player opens a container with the full `ContainerMenu` (one occupant at a time) or peeks at it through the HUD quick-loot panel and takes items without opening it. Every take, put, "take all", pickup, harvest and drop is an intent the server validates (ownership, reach, occupancy, version) and applies atomically, with item instances (OMODs, names) preserved end to end. Owned items are marked stolen. Rejections always resync the client. Nothing can be duplicated or lost, including across disconnects and restarts.

## 2. Vanilla Fallout 4 behaviour
- `ContainerMenu` serves containers, corpses, companions and pickpocketing; `ContainerMenuBase::DoItemTransfer(itemIndex, count, fromContainer)`, `ContainerMenu::TakeAllItems()` (world-economy §2 S7(c)).
- **Quick loot:** aiming at a container shows `QuickContainerStateData{itemData[5], containerRef, inventoryRef, mode: kLoot|kTeammate|kPowerArmor|kTurret|kWorkshop|kCrafting|kStealing|kStealingPowerArmor, isLocked, …}`; Activate takes the highlighted item, Transfer opens the full menu.
- **CONT record:** `DATA{u8 flags (0x2 Respawns, 0x4 Show Owner), float weight}`, `CNTO`+`COED`, `ONAM` filter list (fo4-data-formats §4.14). **Corpses:** NPC_ `ACBS` flag `No Loot` 0x1000, death item `INAM` (LVLI); ACHR flag 0x200 Starts Dead.
- **Ownership:** REFR `XOWN` is 12 bytes `{owner FACT/NPC_, unused, noCrime}`; cell ownership; runtime `SetActorOwner/SetFactionOwner`; `Actor.WouldBeStealing(ref)`, `SendStealAlarm` (world-economy §2 S7, S15). Owned items show "Steal"; detected theft angers the owner/faction (no bounty/jail in FO4).
- **Pickup:** world item REFRs (`XCNT` count, `XLIB` leveled item base); `FLOR` `PFIG` harvest result (ALCH/…/LVLI/MISC) with seasonal `PFPC` (fo4-data-formats §4.17).
- **Drop:** Pip-Boy drop creates a world ref locally; caps cannot be dropped or stored.
- **Single-player assumptions that break:** quick-loot needs contents before activation; several players can look at one container; corpses, pre-placed dead actors and `Respawns` are unsupported by SkyMP (B13, B8, B6); container randomness is per client in FO4_Wrld (first opener decides, mods lost: prior-art §3.1.11).

## 3. SkyMP baseline
- **Open:** `Activate` → CONT branch: `EnsureBaseContainerAdded`, single `occupant` within 512 u of the previous occupant, `isOpen` UpdateProperty, `inventory` property + `OpenContainer` to the activator [src: MpObjectReference.cpp:1512-1541]. Second activation releases (`ProcessActivateSecond` 1570-1605).
- **Put/Take:** `CheckInteractionAbility` (same world/cell only, B4) + occupant check, then `PutItemEvent`/`TakeItemEvent` gamemode events; `Inventory::RemoveItems` atomic; **no correction on reject** (I12) [src: MpObjectReference.cpp:627-655; ActionListener.cpp:487-544]. Event args omit extra data (B11).
- **Pickup/harvest:** `GivePickupItemsToActivationSource`, `SetHarvested(true)`, `RequestReloot()`; dynamic items deleted on pickup [src: MpObjectReference.cpp:1449-1466]. No distance check (I15).
- **Drop:** forbids `0xF` (Gold001 = FO4 Caps001), fires `onDropItem`, places an FF ref with only `count` (extra data lost, B12), ≤ 10 drops per actor, 120 s lifetime [src: MpActor.cpp:1588-1720]. FF items are skipped on load, so a dropped item is lost on restart [src: PartOne.cpp:347-361].
- **Client:** `containersService.ts` diffs the player inventory on `containerChanged` into Put/Take messages; `dropItemService.ts` deletes the local dropped ref and sends `DropItem`.
- **Reuse:** occupancy model, gamemode events, atomic remove, deferred inventory sends, reloot hook. **Adapt:** item identity, reach, corrections, corpse branch. **Replace:** extra-data-less drop, inventory-diff inference (use stack-level capture).

## 4. Design

### 4.1 Authority model
Class A for container contents, occupancy, harvested/picked state, dropped refs and stolen marks. Clients send intents; the menu UI is local (class D) and is redrawn from server truth.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Container / corpse contents | `Inventory` (F04 schema) | `ChangeForm.inv` | yes | `CNTO`/`COED` + LVLI (F14), NPC inventory + death item |
| `isOpen` | bool | existing | yes | false |
| Occupant (full menu) | `MpActor*` | `MpObjectReference::occupant` (existing) | no | none |
| Peekers | `set<actorId>` + expiry | `MpObjectReference::peekers` (new) | no | empty |
| Contents version | u32 | F04 `invVersion` | no | 0 |
| `isHarvested` / picked (`isDisabled`, `isDeleted`) | bool | existing | yes | false |
| Dropped item instance | `Inventory::Entry` | `ChangeForm.droppedEntry` (new) on the FF ref | yes | — |
| Dropped item expiry | uint64 ms | `ChangeForm.droppedExpiresAt` (new; lifetime owned by F14/SRV-080) | yes | now + `droppedItems.lifetimeSec` |
| Owner override | `OwnerRef{kind, formDesc}` | `ChangeForm.ownerOverride` (new) | yes | ESM `XOWN` / cell owner |
| Instanced loot (T2) | `map<profileId, Inventory>` + reloot times | `ChangeForm.privateInventories` (new) | yes | rolled per profile on first open |
| `nextRelootDatetime`, `baseContainerAdded` | existing | existing | yes | F14 |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `Activate` (6) | C→S | `caster` (0x14 or hosted NPC), `target`, `isSecondActivation` (close menu) | R | event | reused |
| `OpenContainer` (21) | S→C | `target` | R | occupancy granted | reused |
| `SetInventoryFo4` (68) | S→C | `refId` = container/corpse, `version`, entries | R, overwrite per (user, refId) | on open, on change (occupant and peekers), on reject | reused (F04) |
| `ContainerPeek` (117) | C→S | `target` u32 (0 = stop) | R | crosshair change, ≤ 4/s | **new** (allocated by this spec) |
| `TakeItemFo4` (70) | C→S | `source` u32, `item` ItemKey (absent when `mode` = all), `count` u32, `mode` u8 (0 menu, 1 quick, 2 all), `version` u32, `opId` u32 | R | event | twin of `TakeItem` (9) |
| `PutItemFo4` (69) | C→S | `target` u32, `item` ItemKey, `count` u32, `version` u32, `opId` u32 | R | event | twin of `PutItem` (8) |
| `DropItemFo4` (71) | C→S | `item` ItemKey, `count` u32, `opId` u32 | R | event | twin of `DropItem` (19) |
| `UpdateProperty` (7) | S→C | `isOpen`, `isHarvested`, `inUse` (occupied flag for quick-loot "in use") | R | on change | reused |
| `CreateActorFo4`/`CreateActor` for FF item refs | S→C | `baseId`, `count`, `props.droppedItem` (ItemKey for mod rendering) | R | on subscribe | reused |

`opId` is echoed in log lines and lets the client match a correction to its optimistic UI change. Ids follow S17 (long ids for ESM-id corpses).

### 4.4 Client capture (owner side)
- **Activation:** `activationService` blocks local activation of CONT/NPC_ (dead)/items/FLOR (`BlockActivation(true, false)`), sends `Activate`; the menu opens on `OpenContainer`.
- **Menu transfers:** hook `ContainerMenuBase::DoItemTransfer` (PLAT-040 family) to read `(itemIndex → stack ItemKey, count, direction)` before the engine moves the stack; send `TakeItemFo4`/`PutItemFo4` with the last received `version`. The engine move stays optimistic (as SkyMP and FO4_Wrld do); `TakeAllItems` → `mode: all`. Fallback if the hook is unavailable: `BGSInventoryListEvent` stack ids (F04 §4.4) instead of SkyMP's inventory diff.
- **Quick loot:** `QuickContainerStateEvent` (crosshair container change) → `ContainerPeek{target}`; leaving → `ContainerPeek{0}`. Activate on the HUD item → `TakeItemFo4{mode: quick}`. The HUD list is filled from the server's `SetInventoryFo4` (PLAT quick-loot HUD injection, §5).
- **Pickup/harvest:** `Activate` on the item/flora ref.
- **Drop:** inventory-menu drop detected via `TESContainerChangedEvent` with no destination (SkyMP `dropItemService` pattern): delete the locally spawned ref, send `DropItemFo4` with the stack's `ItemKey`.

### 4.5 Apply
- **Occupant/peekers:** `SetInventoryFo4{refId}` → `resetContainer` + `addItemEx` on the local container ref; an open menu is redrawn (`UpdateList` + Scaleform `InvalidateLists`, prior-art §3.1.11). The own inventory arrives in the same tick (`refId` 0).
- **Rejected optimistic change:** the correction pair (container + own inventory) restores both lists; the menu stays open.
- **Pickup:** the item ref receives `DestroyActor` (FF) or `isDisabled`; flora receives `isHarvested` (`SetHarvested(true)` locally).
- **Dropped items:** neighbours (including the dropper) receive `CreateActor` for the FF ref; the client places it and applies OMODs with reference-level attach (`BGSObjectInstanceExtra::AttachModToReference`, commonlib-port-map §4.8) so a dropped modded rifle looks modded. Physics settle is local (cosmetic).
- **Stream-in / reconnect:** `isOpen`/`isHarvested`/`inUse` come in the snapshot; menus never survive reconnect (occupancy is released on disconnect).

### 4.6 Validation & anti-cheat
1. **Sender/caster:** player actor only for Put/Take/Drop/Peek (SkyMP rule); `Activate` caster = own actor or hosted NPC (S6).
2. **Reach (I15):** activator ↔ target distance ≤ `GameProfile::ActivationReach(type)` + bounds radius + latency slack, measured from the server position (F07-T04). Quick take uses `quickLootReach` (default 200 u + slack). Occupancy is released when the occupant moves beyond reach + 128 u.
3. **Occupancy:** full-menu Put/Take require `occupant == sender`; quick take/peek require no *other* occupant. A second player activating an occupied container gets a refusal notification (no menu).
4. **Version:** `version` must equal the current contents version for quick take and take-all; menu takes with a stale version are accepted only if the exact `ItemKey` and count still exist. Otherwise reject + fresh `SetInventoryFo4`.
5. **Item:** exact `ItemKey` and count in the source (F04 `FindByKey`). Put: caps refused unless `caps.allowContainerStore`; WEAP `Can't Drop` flag and quest-flagged items refused; CONT `ONAM` filter list honoured.
6. **Corpses:** target actor is dead; `No Loot` flag → reject; live NPC activation in sneak (pickpocket) → reject with a notification; pickpocketing has no owning spec yet (proposed owner: F29, using `PickpocketAttempt` from world-economy §2 S20); player corpses only if `loot.playerCorpses` (default false).
7. **Locked containers:** lock state is F24; a locked container refuses open/peek (peek returns `isLocked` via an empty contents message + notification) until unlocked.
8. **Drop:** caps (`CurrencyBaseId`) refused; `Can't Drop` refused; per-actor live-drop cap (`droppedItems.maxPerActor`, default 10) and global cap; ≤ 5 drops/s.
9. **Atomic transfer (S13):** validate source and destination first, then mutate both in one tick; the source copy is committed only if the add side succeeds (fix of the `RemoveItems(entries, target)` ordering).
10. **Every reject sends a correction** (I12, NET-007): `SetInventoryFo4` for the container (if visible to the sender) and for the sender's own inventory; a refused drop also resends the own inventory so the deleted local ref's item reappears.

### 4.7 Audience / visibility
- Contents: occupant + live peekers only (never broadcast, S8).
- `isOpen`, `isHarvested`, `inUse`: all grid listeners (`SendMessageToActorListeners`, reliable honoured per NET-006).
- Dropped refs: grid neighbours via subscription.

### 4.8 NPC parity
- NPC corpses (ESM and hosted) are lootable through the dead-actor branch; pre-placed "starts dead" ACHRs are loaded as dead `MpActor`s with their inventory and no respawn (fix B8 part, coordinated with F13/F14).
- Hosted NPCs may `Activate` doors/furniture (F07) but never loot or drop through this path; NPC item pickup by AI is ignored (the host's local AI pickup is reverted by F13's world cleaner rules).
- Companion inventory exchange is F21; PA frame inventory is F17; workshop shared containers are F22; vendors are F23.

### 4.9 Gamemode API & server Papyrus
- Events (all args gain `item {baseId,count,omods,name,health}`, fix B11): `onActivate` (existing), `onPutItem`, `onTakeItem`, `onDropItem` (existing, blockable; veto now sends the correction), new `onContainerPeek(actorId, refId)` (observe-only), `onLootCorpse(actorId, corpseId) [blockable]`, `onSteal(actorId, ownerId, item) [blockable]`.
- Properties: `isOpen` (existing), `inventory` on containers (F04), new `owner` (get/set: `{kind:'actor'|'faction'|'profile', id}`), `lootMode` per container override (T2).
- Settings: `activationReach`, `quickLootReach`, `loot.mode` (`shared` | `instanced` | `hybrid`), `loot.playerCorpses`, `caps.allowContainerStore`, `droppedItems.{lifetimeSec, maxPerActor, persistAcrossRestart}` (lifetime semantics in F14).
- Papyrus events: `OnOpen`/`OnClose` (P1), `OnActivate`, `OnItemAdded`/`OnItemRemoved` on both container and actor (via F04-T09), `OnContainerChanged` on picked/dropped refs.
- Papyrus natives: `SendStealAlarm`, `GetActorOwner/SetActorOwner/GetFactionOwner/SetFactionOwner/HasOwner/IsOwnedBy`, `Actor.WouldBeStealing`, `RemoveAllItems(akTransferTo)` (atomic transfer), `SetHarvested`.
- **Stealing:** taking an owned item (from an owned container or as an owned world item; corpses are never owned) marks the entry `stolenFrom = owner` (F04 identity) and fires `onSteal`. Detection and hostility (who saw it) belong to F13/F29: the witnessing NPC's host reports the crime in `NpcAiState` (111) and F13 applies hostility; this spec only records the theft and exposes the hook.

### 4.10 Edge cases & failure modes
- **Two players, one container:** one occupant; others peek. Concurrent quick takes resolve by `version` (first wins, second gets fresh contents).
- **Occupant disconnects / dies / is disabled / walks away:** occupancy released, `isOpen=false`, peekers keep their view.
- **Corpse despawns while open** (cell reset, F14): occupant gets `SetInventoryFo4` empty + menu close.
- **Server restart:** dropped refs with `droppedEntry` and unexpired lifetime are recreated (fixes the FF-item skip for this case); others expire. Occupancy is never persisted.
- **Container emptied:** reloot is scheduled only for `Respawns` containers (B6 fix, F14).
- **Instanced loot (T2):** each profile sees its own copy; world items and corpses stay shared in `hybrid`.
- **Mid-transfer packet loss:** reliable ordered (NET-004); duplicates are impossible because each op is checked against the current state, not replayed blindly.

### 4.11 Performance budget
- Peek: ≤ 4/s per player; ≤ 1 `SetInventoryFo4` per container per tick to all peekers (shared serialized buffer).
- Container contents ≤ 4 KB typical. Occupancy and reach checks O(1).
- Dropped refs: capped per actor and globally (`droppedItems.globalMax`, default 2000) to bound grid size.

## 5. Engine / platform work required
- Hooks: `ContainerMenuBase::DoItemTransfer`, `ContainerMenu::TakeAllItems`, `QuickContainerStateEvent` sink, menu redraw (`UpdateList`/`InvalidateLists`) (world-economy §2 S7(c); prior-art §3.1.11).
- Quick-loot HUD injection: fill `QuickContainerStateData.itemData` from server contents for refs whose local inventory is empty or stale (RE needed, PLAT backlog follow-up under PLAT-080).
- `resetContainer`, `addItemEx` (F04-T07); reference-level OMOD attach for dropped refs (`AttachModToReference`, AE 2189033).

## 6. Tests
- `L-unit` (`[F06]`, `[Containers]`):
  - serialize round trips of `PutItemFo4`, `TakeItemFo4` (all modes), `DropItemFo4`, `ContainerPeek`;
  - open: occupancy granted within reach; refused beyond reach (I15); second activator refused;
  - take/put accept: both inventories updated atomically, one `SetInventoryFo4` each to occupant and peekers; `onTakeItem` carries `item` with OMODs;
  - every reject path sends the correction pair: not occupant, stale version (quick), unknown `ItemKey`, caps put, `Can't Drop`, gamemode veto;
  - quick take race: two actors, same version → first accepted, second corrected;
  - take all moves every entry (regression for B2 path);
  - corpse: loot allowed when dead; `No Loot` refused; starts-dead ACHR loaded with inventory;
  - stealing: owned container marks `stolenFrom`, `onSteal` veto blocks the take;
  - pickup: dynamic item with `droppedEntry` gives the exact instance and is deleted; flora `PFIG` LVLI result via F14; `isHarvested` to listeners;
  - drop: instance preserved (B12), caps refused, per-actor cap, persistence across restart restores the ref with remaining lifetime;
  - late joiner sees `isOpen`, `isHarvested`, dropped ref with `droppedItem`;
  - `onPapyrusEvent:OnOpen` / `OnItemRemoved` observed.
- `L-int`: two bots loot one container concurrently (100 random ops, QA-040); total item count conserved; disconnect mid-transfer.
- `L-ts`: transfer capture → message mapping; correction rollback of an optimistic UI change.
- `G-self`: `DoItemTransfer` hook fires with correct stack keys; dropped modded weapon renders mods.
- `G-manual`: container, corpse, quick-loot, take-all, flora harvest, drop/pickup between two players; owned-container "Steal" marking.

## 7. Tasks
- [ ] **F06-T01** Messages `PutItemFo4` (69), `TakeItemFo4` (70), `DropItemFo4` (71), `ContainerPeek` (117) + TS mirrors — M — Depends: F04-T01, NET-002 — Verify: L-unit — Files: skymp5-server/cpp/messages/{PutItemFo4,TakeItemFo4,DropItemFo4,ContainerPeek}Message.h, Messages.h, falloutmp-client/src/services/messages/
  - Accept: binary/JSON round trips; protocol version bumped (NET-001).
- [ ] **F06-T02** Server transfer handlers: `ItemKey`, occupancy, reach (F07-T04), version, atomic two-sided transfer, correction on every reject (I12), `item` arg in events (B11) — M — Depends: F06-T01, F07-T04, NET-007 — Verify: L-unit — Files: ActionListener.cpp, MpObjectReference.cpp (`PutItem`/`TakeItem`/`RemoveItems`), gamemode_events/{PutItemEvent,TakeItemEvent}.cpp, unit/PartOne_ContainersFo4Test.cpp
- [ ] **F06-T03** Quick loot: peek subscriptions with expiry, push to peekers, quick take, take all, `inUse` property — M — Depends: F06-T02 — Verify: L-unit — Files: MpObjectReference.{h,cpp}
  - Accept: peek lifecycle and quick-take race tests pass.
- [ ] **F06-T04** Corpse looting: dead-actor activation branch (B13), starts-dead ACHR loading (B8 part), `No Loot`, `loot.playerCorpses`, `onLootCorpse` — M — Depends: F06-T02, F13 — Verify: L-unit, D-real — Files: MpObjectReference.cpp (`ProcessActivateNormal`), WorldState.cpp (`AttachEspmRecord`)
- [ ] **F06-T05** Ownership & stealing: owner resolution (`XOWN` 12 B, cell owner, `ownerOverride`), `WouldBeStealing`, `stolenFrom`, `onSteal`, `owner` property, Papyrus owner natives — M — Depends: F06-T02, ESPM-005 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/fo4/OwnershipService.{h,cpp}
- [ ] **F06-T06** Pickup & harvest FO4: instance from `droppedEntry`/ESM item (OMOD roll once, persisted), `XCNT`, `XLIB`, FLOR `PFIG` via F14, reach check — M — Depends: F06-T02, F14-T02, F07-T04 — Verify: L-unit — Files: MpObjectReference.cpp (`GivePickupItemsToActivationSource`)
- [ ] **F06-T07** Drop with extra data (B12): `DropItemFo4` handler, `droppedEntry`, caps/`Can't Drop`, caps per actor and global, restart persistence completed by F14-T10 — M — Depends: F06-T01, SRV-080 — Verify: L-unit — Files: MpActor.cpp (`DropItem`), MpChangeForms.{h,cpp}, PartOne.cpp (load skip), unit/DropItemFo4Test.cpp
- [ ] **F06-T08** Occupancy hardening: release on distance/death/disconnect/disable; refusal notification; no persisted occupancy — S — Depends: F06-T02 — Verify: L-unit
- [ ] **F06-T09** (T2) Loot modes per ADR-019: `shared`/`instanced`/`hybrid`, `privateInventories`, per-profile reloot — M — Depends: F06-T02, F14-T09 — Verify: L-unit — Files: MpObjectReference.cpp, MpChangeForms.{h,cpp}
- [ ] **F06-T10** Client: `containersService` fork (transfer hook capture, peek, quick take, take all), `dropItemService` fork, apply/redraw, dropped-ref OMOD attach — L — Depends: F06-T01, F04-T08, PLAT-080 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/{containersService,dropItemService,activationService}.ts
- [ ] **F06-T11** Platform: `DoItemTransfer`/`TakeAllItems` hooks, `QuickContainerStateEvent` sink, quick-loot HUD fill, menu redraw, `attachModsToReference` — L — Depends: PLAT-040, F04-T07 — Verify: W-ci, G-self — Files: fallout4-platform/src/platform_fo4/ContainerApi.cpp
- [ ] **F06-T12** Papyrus: `OnOpen`/`OnClose`, `OnContainerChanged`, `SendStealAlarm`, `RemoveAllItems(akTransferTo)` atomic — S — Depends: PVM-014, F04-T09 — Verify: L-unit
- [ ] **F06-T13** Adversarial anti-dup suite (contribution to QA-040): concurrent take/put, disconnect mid-transfer, crash between mutate and save — M — Depends: F06-T03, F06-T07 — Verify: L-unit, L-int — Files: unit/ContainersDupTest.cpp, misc/tests/
  - Accept: item totals conserved over 10 000 randomized ops.
- [ ] **F06-T14** `G-manual` looting scenario script and sign-off — S — Depends: F06-T10, F06-T11 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F06-containers.md

## 8. Open questions & risks
- Quick-loot HUD injection needs RE of the HUD data path; fallback for T0 is "peek fills the local container ref before the HUD reads it".
- Whether the `DoItemTransfer` hook sees the stack id before the engine merges stacks (G-self).
- Pickpocketing is out of scope here and currently unowned (proposed: F29); stealing detection depends on F13 `NpcAiState` crime reports.
- Player-corpse looting policy (Q-06 PvP focus) may change the `loot.playerCorpses` default.
