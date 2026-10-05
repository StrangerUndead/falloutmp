# F05 — Equipment (biped slots, weapon & grenade instances, layering)

| Field | Value |
|---|---|
| Tier | T0 |
| Target level | L4 |
| SkyMP analogue | `UpdateEquipment` (5) with `numChanges`; `ActionListener::OnUpdateEquipment` (spell/ownership/BOD2 overlap checks, `ActionListener.cpp:230-440`); `MpActor::EquipBestWeapon` (`MpActor.cpp:140-192`); client `sync/equipment.ts`. SkyMP level L3 (reference/skymp-sync-inventory.md §2, Equipment row) |
| Milestone | M6 |
| Workstreams | SRV, PLAT, CLI, NET, ESPM, PVM, GM |
| Depends on | F04 (item instances), F03 (appearance applied first), F02 (spawn ordering, subgraph wait), NET-002, NET-003, REF-008, ESPM-007 (ARMO 4-byte BOD2, WEAP DNAM), SRV-022 (OMOD stat resolver), PLAT-081, F17 (PA state) |
| References | reference/skyrim-coupling-index.md §0.6 items 1–4, §1.7, §1.8 (`:2051-2073`), §1.11, §1.12, §5.1 (`ShieldSlotMask`); reference/fo4-data-formats.md §4.6, §4.7, §5; reference/fo4-systems-combat-character.md §12, §15; reference/commonlib-port-map.md §1.13; reference/prior-art.md §3.1.6, §3.2.5; reference/papyrus-api-map.md §2.3.4, §2.3.10, §4.3; reference/fo4-animation-sync.md §3.4 |

## 1. Summary
Everyone sees each player and NPC wearing exactly the apparel and holding exactly the modded weapon they have on the server: the right receiver, scope and paint, vault suit under combat-armor limbs, hats, glasses, and the selected grenade. Equipment is requested by the owner (or the NPC's host) and validated by the server against the FO4 biped-slot rules and the actor's inventory instances. Accepted changes are relayed to neighbours, persisted, and included in the late-joiner snapshot. Rejected changes are corrected on the sender. Power-armor pieces are server-placed by F17 and cannot be changed through this path.

## 2. Vanilla Fallout 4 behaviour
- **Biped slots:** ARMO/ARMA `BOD2` is a single **u32** mask; bit *n* = slot 30+*n* (fo4-data-formats §5). Slot 33 = BODY (full outfits), 36–40 = **[U] under-armor** torso/arms/legs, 41–45 = **[A] armor pieces**, 46 headband, 47 eyes, 48 beard, 49 mouth, 50 neck, 52 scalp, 59 shield, 60 Pip-Boy, 61 FX. Two items conflict only if their masks intersect; under-armor outfits sit beneath limb armor.
- **The 0x200 clash:** Skyrim's shield bit `kBodShield = 0x200` [src: skymp5-server/cpp/server_guest_lib/MpActor.cpp:2051] is FO4 slot 39 "[U] L Leg"; FO4's shield slot is 0x20000000 (skyrim-coupling-index §0.6 item 4). Any Skyrim-literal use treats FO4 leg clothing as a shield.
- **Instances:** weapons and armor are `BGSObjectInstance{object, instanceData}`. OMODs can change armor properties, including `BodyPart` (armor property 8) and weight/rating (fo4-data-formats §4.12), so the effective slot mask is an instance property, not only a base-record property.
- **Equip API:** `ActorEquipManager::EquipObject(Actor*, const BGSObjectInstance&, stackID, number, BGSEquipSlot*, queue, force, sounds, applyNow, locked)` (AE 2231392) and `UnequipObject` (2231395) (commonlib-port-map §1.13). Worn state is the stack flag `kSlotMask`, not an extra.
- **Weapons:** one equipped weapon (equip index 0); **grenades/mines** are WEAP records (animation type Grenade/Mine) equipped in a separate thrown-weapon slot; `GetEquippedItemType` returns 9 Gun, 10 Grenade, 11 Mine (papyrus-api-map §2.3.4). The internal biped-object indices `kWeaponGrenade` 42 / `kWeaponMine` 43 (combat-character §1.3) are *engine object indices*, not BOD2 slots 42/43 ([A] arms); never mix the two numberings.
- **Weapon subgraphs** depend on weapon and OMOD keywords; a ghost must hold the same instance before animation replay (fo4-animation-sync §3.4, F02).
- **Power armor:** entering switches the race and moves frame pieces into worn slots; apparel cannot be changed while inside (combat-character §12.1, §15.2).
- **Engine quirks:** the engine emits a transient unequip from the readied-weapon slot (`0x4334D`) on draw; items worn when a save loads fire no equip event; adding a weapon drags ammo along (prior-art §3.1.6, §3.1.11).
- **Papyrus:** `EquipItem(Form, bool, bool)`, `UnequipItem`, `UnequipAll`, `UnequipItemSlot(int)`, `IsEquipped(Form)` (base-form only), `GetEquippedWeapon(int aiEquipIndex)`, F4SE `GetWornItem(slot)`/`GetWornItemMods(slot)`; events `OnItemEquipped`/`OnItemUnequipped` (papyrus-api-map §2.3.4, §4.3).

## 3. SkyMP baseline
- Client diffs `getEquipment()` on equip/unequip and sends `UpdateEquipment{idx, inv (worn entries), spells, numChanges}` reliably (SIS:261-285).
- Server: spells learned, `inventory.HasItem(baseId)` (base only), BOD2/BODT overlap → accept: relay + `SetEquipment`; reject: `SendInventoryUpdate` + SpSnippet `UnequipItem`/`RemoveSpell` [src: ActionListener.cpp:230-440]. Only the player actor; NPC equipment changes only via `EquipBestWeapon` on host change [src: MpActor.cpp:140-192], which dereferences WEAP `DATA` (null on FO4, coupling §0.6 item 1).
- Remote apply: `applyEquipment` via `setInventory` + `queueNiNodeUpdate`, skipped while a menu is open (sync/equipment.ts:109-128).
- **Reuse:** message flow, `numChanges` ordering (S18), validate-then-relay, snapshot in `CreateActor`, persistence field `equipment`. **Adapt:** slot model and shield mask (GameProfile), instance matching. **Replace:** spell slots (dropped for FO4), client apply (real `BGSObjectInstance` equip), best-weapon logic.

## 4. Design

### 4.1 Authority model
Class A. The owner client (or the host for a hosted NPC) proposes the worn set; the server validates, stores and broadcasts it. Weapon *drawn* state is class B presentation (F01 flags / F02 keyframe). PA pieces are class A owned by F17.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Worn apparel | `std::vector<ItemKey>` (≤ 32) | `Fo4Equipment::apparel` in `MpActor` | yes (`equipment`, FO4 schema with `FormDesc` ids) | NPC default outfit `DOFT`→`OTFT` (F14) |
| Equipped weapon | `optional<ItemKey>` | `Fo4Equipment::weapon` | yes | best weapon (§4.8) |
| Thrown weapon (grenade/mine) | `optional<uint32 baseId>` | `Fo4Equipment::thrown` | yes | none |
| `numChanges` | u32 | `Fo4Equipment::numChanges` | yes | 0 |
| Occupied slot mask | u32 | derived (`EquipmentModel::OccupiedSlots`) | no | recomputed |
| Equipped weight, resistances | float, map | derived cache (F04 weight, F11 DR/ER/RR) | no | recomputed on change |
| PA lock | bool | derived from F17 `wornFrameRefId` | no (F17 persists) | false |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `UpdateEquipmentFo4` (67) | C→S (owner, or host for NPC idx); S→C relay to neighbours; S→C correction to sender | `idx` u32, `numChanges` u32, `apparel[]` (u8 len) `ItemKey` (F04 wire encoding without count), `weapon?` `ItemKey`, `thrown?` u32 baseId, `flags` u8 (bit0 `isCorrection`) | R ordered (NET-004) | on change, coalesced 100 ms; ≤ 10/s | twin of `UpdateEquipment` (5) |
| `CreateActorFo4` (64) `equipment` | S→C | same payload | R | on subscribe | reused (F00) |
| `SetInventoryFo4` (68) | S→C | inventory resync when a reject also implies an inventory mismatch | R | on reject | reused (F04) |

Typical message: 8 apparel items + a 6-OMOD weapon ≈ 120 B.

### 4.4 Client capture (owner side)
- Platform `equip`/`unequip` events (`TESEquipEvent`) and `ActorEquipManagerEvent` trigger `getEquipmentFo4(actor)`: worn stacks (`flags & kSlotMask`) with their `ItemKey` (F04 `getInventory`), equipped weapon instance (`Actor::GetEquippedItem(BGSObjectInstance*, equipIndex)`), thrown weapon.
- Filters:
  - ignore the transient readied-weapon unequip (`0x4334D`) and the immediate re-equip (prior-art §3.1.6);
  - ignore changes produced by applying server state (reentrancy guard, as in F02);
  - ignore PA enter/exit transitions (F17 drives them);
  - coalesce bursts (Pip-Boy multi-equip, outfit swaps) into one send per 100 ms.
- Diff against the last *acknowledged* equipment; send only on difference; increment `numChanges`.
- Hosted NPCs: the host captures the same way for each hosted actor (its local AI swaps weapons, throws grenades).

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Remote actors** (PLAT-081): for each worn item, ensure the ghost holds that instance (`addItemEx` count 1, no ammo drag-in), then `equipItemInstance(actor, item, slotKind)` → `EquipObject` with a real `BGSObjectInstance` so OMOD meshes, material swaps and body culling come from the engine (TE passed a null instance and lost all OMODs: prior-art §3.2.5). Items no longer worn are unequipped and removed from the ghost.
- **Order on stream-in** (F02 §4.5): appearance (F03) → equipment → archetype → subgraph wait → keyframe. A weapon change on a live ghost re-waits for the subgraph before F02 replays actions.
- **Owner correction:** `UpdateEquipmentFo4{isCorrection}` → equip/unequip the local player to match, under the reentrancy guard. The client then adopts the server `numChanges`.
- **Stream-in / respawn / reconnect:** the snapshot in `CreateActorFo4.equipment`; on reconnect the owner applies it after the inventory (F04), since items worn when the template save loads fire no event.
- **Menus:** unlike SkyMP, remote apply is not skipped while a local menu is open (it is a ghost); only the local player's own correction waits for menu close if the engine refuses to equip.

### 4.6 Validation & anti-cheat
Implemented in `GameProfile::ValidateEquipment` (REF-008), FO4 version:
1. Ownership: `idx` is the sender's actor or an NPC it hosts (S6); else `HostStop`/drop.
2. `numChanges` > stored value (S18); stale → drop silently (a newer state is already in flight).
3. Every `ItemKey` exists in the actor's inventory by exact key, with count ≥ number of times worn (F04 `FindByKey`; stronger than SkyMP's `HasItem(baseId)`).
4. Apparel: `ARMO`, playable (not flag 0x4), not broken (`health` > 0 when present). Effective masks come from the instance (`BOD2` + OMOD `BodyPart` via SRV-022) and must be **pairwise disjoint**; mask-0 items are allowed.
5. Weapon: `WEAP`, playable, animation type not Grenade/Mine. Thrown: `WEAP` with animation type Grenade or Mine.
6. Shield detection uses `GameProfile::ShieldSlotMask()` (FO4 0x20000000), never the literal 0x200 (fixes coupling §0.6 item 4).
7. PA lock (F17): while the actor wears a frame, apparel is frozen; only `weapon`/`thrown` may change. PA pieces never appear in client-proposed sets.
8. Rate: ≤ 10 updates/s per actor.

**Corrections:** any rule 3–8 failure → `UpdateEquipmentFo4{isCorrection}` with the server state to the sender (never silent, I12); rule 3 also resends `SetInventoryFo4`. A gamemode veto (§4.9) sends the same correction.

### 4.7 Audience / visibility
Accepted state is relayed to grid neighbours, excluding the sender (unless `show-me`). Equipment is public (worn items are visible); inventory stays private (F04). Corrections go to the sender only; NPC corrections go to the hoster (`GetActorToSendTo`, S14).

### 4.8 NPC parity
- **Hosted updates (SkyMP-plus):** a host may send `UpdateEquipmentFo4` for a hosted NPC idx; the same rules apply against the NPC's server inventory (F04). SkyMP rejects NPC equipment updates entirely.
- **FO4 `EquipBestWeapon`** (on spawn and host change): among inventory WEAP instances (excluding Grenade/Mine), pick the highest `damage × projectiles × shotsPerSecond` from the resolved instance stats (SRV-022); skip weapons flagged *NPCs Use Ammo* when the NPC has no matching ammo (fo4-data-formats §4.6 DNAM flags); tie → keep the current weapon, then lowest form id. No null dereference on missing data.
- **Default outfit:** `DOFT`→`OTFT` evaluated through F14 (LVLI with OMOD rolls) into `apparel` on first load.

### 4.9 Gamemode API & server Papyrus
- `mp.get(id, 'equipment')` → `{apparel:[ItemKey], weapon?, thrown?, numChanges}`; new `mp.set(id, 'equipment', …)` (validated with §4.6; throws on failure; broadcasts).
- Events: existing `onUpdateEquipmentAttempt` (warning only) is kept; new `onEquipChange(actorId, equipped[], unequipped[])` **blockable** (veto → correction).
- Papyrus natives (PVM-013/014): `EquipItem`, `UnequipItem`, `UnequipAll`, `UnequipItemSlot`, `IsEquipped`, `GetEquippedWeapon(aiEquipIndex)`, `GetEquippedItemType`, `WornHasKeyword`, F4SE `GetWornItem`/`GetWornItemMods`. Server natives mutate `Fo4Equipment` and broadcast `UpdateEquipmentFo4` instead of SkyMP's SpSnippet-only `EquipItem` (S15).
- Papyrus events: `OnItemEquipped(akBaseObject, akReference)` / `OnItemUnequipped` replace `OnObjectEquipped` [src: MpActor.cpp:550] (papyrus-api-map §4.3 P0).
- Condition functions `GetEquipped`, `WornHasKeyword`, `WornApparelHasKeywordCount`, `GetEquippedItemType` use the FO4 biped model (coupling §1.12; ESPM-011).

### 4.10 Edge cases & failure modes
- **Equipped item leaves the inventory** (consumed, dropped, sold, scrapped, stolen): F04 removal triggers an automatic unequip + broadcast.
- **OMOD change on a worn item** (F16): the worn `ItemKey` is replaced atomically with the inventory entry and re-broadcast.
- **PA piece breaks** (health 0, F17/F11): auto-unequip rules belong to F17; F05 rejects re-equip of broken pieces.
- **Disconnect mid-burst:** last accepted state persists; the reconnect snapshot is authoritative.
- **Host migration:** the new host receives the server equipment in `HostStart` flow (`EquipBestWeapon` only when the NPC has no valid weapon).
- **Race change** (PA race, F03): masks are re-evaluated; conflicting items are unequipped server-side.

### 4.11 Performance budget
≤ 150 B per update; ≤ 10/s per actor (typically < 1/min). Validation O(items × OMODs) with a per-instance mask cache. Remote apply ≤ 2 ms per item swap on the client (NIF load is asynchronous in the engine).

## 5. Engine / platform work required
- PLAT-081: `equipItemInstance`/`unequipItemInstance` using `ActorEquipManager` with a real `BGSObjectInstance` and stack id; readied-weapon transient filter; ammo strip; thrown-weapon slot (`BGSEquipSlot` form ids looked up in Fallout4.esm, commonlib-port-map §1.7); `getEquipmentFo4`.
- Platform events `equip`/`unequip` with instance data, and `ActorEquipManagerEvent` (commonlib-port-map §4.2 list).

## 6. Tests
- `L-unit` (`[F05]`, `[Fo4Equipment]`):
  - serialize round trip of `UpdateEquipmentFo4` (binary + JSON);
  - accept: relay to neighbours (not the sender), persisted, `numChanges` stored;
  - reject + correction: item not owned; instance mismatch (same base, different OMODs); two torso armors; vault suit + combat-armor arm **accepted**; leg cloth (0x200) not treated as shield; broken PA piece; grenade in weapon slot; PA lock;
  - stale `numChanges` dropped; rate limit;
  - wrong user / wrong host → drop/`HostStop`; hosted NPC update accepted from the host only;
  - late joiner `CreateActorFo4.equipment`; persistence round trip with `FormDesc` ids;
  - FO4 `EquipBestWeapon` picks by resolved DPS and skips ammo-less *NPCs Use Ammo* weapons;
  - gamemode veto of `onEquipChange` → correction; `onPapyrusEvent:OnItemEquipped` observed.
- `L-fixture`: synthetic ARMO with 4-byte BOD2 and an OMOD changing `BodyPart`.
- `L-ts`: capture filters (readied-weapon transient, reentrancy), diff and coalescing.
- `L-int`: bot A equips a modded rifle; bot B (late joiner) receives it in the snapshot.
- `G-self`: equip a 6-OMOD weapon on a ghost; dump attached mod nodes; body cull with slot 33.
- `G-manual`: two players; outfits with under-armor layering, hats/glasses, modded guns, grenade selection, reconnect; 1st and 3rd person.

## 7. Tasks
- [ ] **F05-T01** `Fo4Equipment` model + FO4 `equipment` JSON (`FormDesc`) + `UpdateEquipmentFo4` (67) struct and TS mirror — M — Depends: F04-T01, NET-002, NET-003 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/fo4/Fo4Equipment.{h,cpp}, skymp5-server/cpp/messages/UpdateEquipmentFo4Message.h, Messages.h, falloutmp-client/src/services/messages/updateEquipmentFo4Message.ts
  - Accept: round trips; legacy-free FO4 JSON loads with defaults.
- [ ] **F05-T02** GameProfile FO4 biped model: slot table 30–61, `ShieldSlotMask()` = 0x20000000, effective instance mask (BOD2 + OMOD `BodyPart`), `GetEquippedShield`/`kBodShield` call sites routed through the profile — S — Depends: REF-008, ESPM-007, SRV-022 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/game_profile/fallout4/Fo4BipedModel.{h,cpp}, MpActor.cpp:2044-2073
  - Accept: grep finds no `0x200` shield literal outside `SkyrimGameProfile`; Skyrim equipment tests green.
- [ ] **F05-T03** Server handler: validation rules §4.6, correction, relay, persistence, rate limit — M — Depends: F05-T01, F05-T02 — Verify: L-unit — Files: ActionListener.cpp (FO4 handler), skymp5-server/cpp/server_guest_lib/fo4/Fo4EquipmentValidator.{h,cpp}, unit/PartOne_UpdateEquipmentFo4Test.cpp
  - Accept: every §6 accept/reject case passes.
- [ ] **F05-T04** Hosted-NPC equipment updates + FO4 `EquipBestWeapon` — M — Depends: F05-T03, F13 (hosting) — Verify: L-unit — Files: MpActor.cpp (`EquipBestWeapon`), ActionListener.cpp
  - Accept: host update accepted; non-host rejected; best-weapon cases pass without Skyrim `weapData`.
- [ ] **F05-T05** Equipment–inventory consistency: auto-unequip on removal, worn `ItemKey` replacement on OMOD change, race-change re-evaluation — S — Depends: F05-T03, F04-T09 — Verify: L-unit
- [ ] **F05-T06** PA interplay hooks for F17: apparel lock while worn, server-placed PA pieces exempt from client sets — S — Depends: F05-T03, F17 — Verify: L-unit
- [ ] **F05-T07** Platform: `equipItemInstance`/`unequipItemInstance`/`getEquipmentFo4`, readied-weapon filter, thrown slot — M — Depends: PLAT-081, F04-T07 — Verify: W-ci, G-self — Files: fallout4-platform/src/platform_fo4/EquipApi.cpp
  - Accept: self-test equips a modded weapon and armor instance on a ghost and reads back the same keys.
- [ ] **F05-T08** Client capture + send (fork of `sync/equipment.ts`, `sendInputsService` equipment part) — M — Depends: F05-T01, F05-T07, CLI-050 — Verify: L-ts — Files: falloutmp-client/src/sync/equipment.ts, falloutmp-client/src/services/services/sendInputsService.ts
- [ ] **F05-T09** Client apply: remote ghosts, owner correction, stream-in ordering in `formView.ts` with F02 — M — Depends: F05-T07, F02-T08 — Verify: L-ts, G-manual — Files: falloutmp-client/src/sync/equipment.ts, falloutmp-client/src/view/formView.ts
- [ ] **F05-T10** Gamemode: `equipment` get/set, `onEquipChange` (blockable), docs — S — Depends: F05-T03 — Verify: L-unit, L-int — Files: skymp5-server/cpp/addon/property_bindings/EquipmentBinding.cpp, skymp5-server/cpp/server_guest_lib/gamemode_events/EquipChangeEvent.{h,cpp}
- [ ] **F05-T11** Papyrus natives and `OnItemEquipped`/`OnItemUnequipped`; FO4 condition functions on the biped model — M — Depends: PVM-013, PVM-014, ESPM-011, REF-009 — Verify: L-unit — Files: script_classes/PapyrusActor.cpp, condition_functions/*
- [ ] **F05-T12** `CreateActorFo4` equipment snapshot + resistances/weight cache notifications to F11/F08 — S — Depends: F05-T03 — Verify: L-unit
- [ ] **F05-T13** `G-manual` equipment scenario script and sign-off — S — Depends: F05-T09 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F05-equipment.md

## 8. Open questions & risks
- Exact `BGSEquipSlot` form ids for the thrown-weapon slot and whether `EquipObject` needs it for grenades (verify with G-self).
- Whether OMOD `BodyPart` changes occur in vanilla data (D-real scan); if never, cache masks per base only.
- Engine equip on ghosts may trigger AI side effects (auto-draw, sounds); PLAT-071 suppression must cover them.
