# F04 — Inventory & Item Instances (OMOD, legendary, components, caps)

| Field | Value |
|---|---|
| Tier | T0 (favorites and tagged components: T1) |
| Target level | L4 |
| SkyMP analogue | `Inventory` + `ExtraData` (`Inventory.h:31-97`, `Inventory.cpp:74-136`), `MpObjectReference::SetInventory/AddItem/AddItems/RemoveItems` (`MpObjectReference.cpp:808-898`), `SetInventory` (28, deferred channel 0), client `sync/inventory.ts`. SkyMP level L4 (reference/skymp-sync-inventory.md §2, Inventory row) |
| Milestone | M6 |
| Workstreams | SRV, ESPM, PLAT, CLI, NET, PVM, GM |
| Depends on | F00, NET-002, NET-003 (`InventoryT<ExtraData>`), NET-005, REF-008 (inventory seams), REF-020, ESPM-007 (WEAP/ARMO/AMMO/OMOD/INNR/OBTE), ESPM-008 (MISC/CMPO/CONT/LVLI), ESPM-012 (`IsItem`), PLAT-080, SRV-022 (OMOD stat resolver), PVM-007 (inventory-filter events), F14 (leveled rolls) |
| References | reference/fo4-systems-world-economy.md §1 (B1, B2, B7, B9, B11, B12), §2 S1, §3.1–3.3; reference/fo4-data-formats.md §4.6–4.12, §4.19; reference/commonlib-port-map.md §1.13, §4.8; reference/papyrus-api-map.md §2.3.8, §4.3, §5.9; reference/prior-art.md §3.1.11, §3.2.5; reference/skymp-sync-inventory.md §1.9–1.12, §2, Appendix C (6, 21); reference/fo4-systems-combat-character.md §15 |

## 1. Summary
Every item a player, NPC or container holds is a server-authoritative **item instance**: base form + canonical OMOD list (receiver, barrel, legendary, paint…) + optional custom name + optional health (power-armor pieces). Two items stack only if their instance keys are equal, exactly like vanilla. The owner sees the same weapons, mods, names and counts after every transfer, reconnect and server restart. Other players never see someone's inventory (only what is worn, F05). Junk, components, caps (`Caps001` 0xF) and bobby pins are ordinary entries. Weight is computed on the server from the effective instance stats and feeds carry weight (F08). No operation may duplicate or lose an item.

## 2. Vanilla Fallout 4 behaviour
- **Storage:** `TESObjectREFR::inventoryList : BGSInventoryList*` replaces Skyrim's `ExtraContainerChanges`. `data` is `BSTArray<BGSInventoryItem{object, stackData}>`; each `Stack{nextStack, extra: ExtraDataList, count, flags}`; flags `kSlotIndex1..3` = equipped (reference/commonlib-port-map.md §1.13, §4.8). Stack indices are client-local and must never be used as network identity (world-economy §2 S1(b)).
- **Instances:** `BGSObjectInstanceExtra` (`kObjectInstance`) holds `ObjectIndexData{objectID, index (attach), rank, disabled}`. Effective stats (`ExtraInstanceData`) are derived and never sent [src: CLF4 B/BGSObjectInstanceExtra.h, E/ExtraInstanceData.h via world-economy §2 S1(b)].
- **Stacking:** same base + same mod set stacks; different sets do not (world-economy §2 S1(a)).
- **Other extras:** `ExtraHealth` (PA pieces only; no weapon/armor degradation in vanilla), `ExtraTextDisplayData` `kCustomName`, `ExtraFavorite`, `ExtraAmmo` (loaded rounds, F09), `ExtraUniqueID`, ownership.
- **Legendary:** an attached OMOD with record flag *Legendary Mod*. The bit value differs between our references (0x08 in world-economy §2 S1(b), 0x10 in fo4-data-formats §4.12); ESPM-007 must settle it against xEdit. Legendaries cannot be scrapped.
- **Junk/components:** junk = `MISC` with `CVPA {component, count}`; a component's scrap item is the `MISC` in `CMPO.MNAM` (fo4-data-formats §4.11). Loose mods are `MISC` with form flag 0x80.
- **Currency:** `Caps001` 0xF, `BobbyPin` 0xA (world-economy §2 S1(b)). Caps weigh 0 and cannot be dropped or put into containers.
- **Records:** WEAP has **no `DATA`** (value/weight/damage live in `DNAM`); ARMO `DATA{value, weight, health}`; AMMO weight at `DATA+4`; templates `OBTE…STOP` with `OBTS{levelMin, levelMax, default, keywords, includes}` (fo4-data-formats §4.6–4.8, §4.19).
- **Papyrus:** `AddItem/RemoveItem/GetItemCount/IsEquipped` work on base forms only; `AttachModToInventoryItem` fails or is ambiguous with more than one instance (papyrus-api-map §5.9). `OnItemAdded/OnItemRemoved` are delivered only to scripts with an inventory event filter (papyrus-api-map §0 item 5).
- **Engine events:** `TESContainerChangedEvent` has no instance data; `BGSInventoryListEvent{changeType, owner, objAffected, count, stackID}` carries the stack id, from which the instance extra can be read (world-economy §2 S1(c)).
- **SP assumptions that break:** template/mod rolls of spawned weapons are client-random (FO4_Wrld finding); `AddItem` of a weapon drags 5–9 rounds of ammo along (reference/prior-art.md §3.1.11); favorites live in the local save.

## 3. SkyMP baseline
- `Inventory::Entry{baseId, count, ExtraData}`; stack identity `EqualExceptCount` over Skyrim extras [src: skymp5-server/cpp/server_guest_lib/Inventory.cpp:74-85].
- **Bug B2:** `AddItems` returns from the whole function after the first merge, dropping later entries [src: Inventory.cpp:92-104]. Multi-entry adds (crafting yields, scrap, reloot, transfers) lose items.
- `RemoveItems` is atomic (works on a copy, throws if short) [src: Inventory.cpp:106-136] (S13). `MpObjectReference::RemoveItems(entries, target)` removes then calls `target->AddItems` [src: MpObjectReference.cpp:873-892].
- `OnItemAdded` fires only from `AddItem`; the `AddItems` call is commented out [src: MpObjectReference.cpp:817-865] (I22).
- `SendInventoryUpdate` → `SetInventory` on deferred channel 0 (overwrite) to the owner; `CreateActor.props.inventory` owner-only (skymp-sync-inventory §1.5, §1.8).
- `EnsureBaseContainerAdded` aggregates CONT/NPC_/outfit items into `map<baseId,count>` with no extra data (B7). `GetWeightFromRecord` dereferences null WEAP data on FO4 (skyrim-coupling-index §0.6 item 1).
- Client: `applyInventory`, `getDiff`, `sumInventories` (sync/inventory.ts), 5 s `pcInv` re-apply (RS:67-91).
- **Reuse:** entry/inventory container, atomic remove, deferred overwrite channel, owner-only audience, periodic reconcile. **Adapt:** extra-data schema, JSON ids. **Replace:** client natives (`setInventory`, `addItemEx`), Skyrim-only extras (never set under the FO4 profile).

## 4. Design

### 4.1 Authority model
Class A for everything: entries, counts, OMOD lists, names, health, caps, weight. Clients only send intents owned by other features (put/take/drop F06, use F20, craft/scrap F15, mod F16, barter F23); each is answered with authoritative state. Favorites are owner preferences (persisted, validated for existence only). Pip-Boy sorting/filters are class D.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Inventory entries | `Inventory` (FO4 schema) | `MpObjectReference` → `ChangeForm.inv` | yes (`inv`) | ESM `CNTO`/`COED`, outfit, LVLI (F14), death item |
| OMOD list per entry | `std::optional<std::vector<OmodRef{id,idx,rank}>>` sorted by (idx, id) | `Inventory::Entry::omods` (new) | yes (`inv[].omods`) | `OBTS` default combination rolled at creation (F14) |
| Custom name | `optional<string>` | `Entry::name` (reused) | yes | none |
| Health (PA pieces) | `optional<float>`, quantized 1e-3 | `Entry::health` (reused) | yes | ARMO `DATA.health` |
| Stolen-from owner | `optional<uint32>` | `Entry::stolenFrom` (new; set by F06) | yes (FormDesc) | none |
| Feature-owned extras (not identity) | `ammoLoaded` u16 (F09), `paPiece` bool (F17) | `Entry::ammoLoaded`, `Entry::paPiece` (new) | yes (`inv[]`) | 0 / false |
| Inventory version | u32 | `MpObjectReference::invVersion` (new) | no | 0 at load |
| Weight cache | float | `MpObjectReference::cachedWeight` (new) | no (derived) | recomputed on change |
| Favorites[12], tagged components | `ItemKey?`, `FormId[]` | `PlayerProfile` (SRV-060) | yes | empty |
| `baseContainerAdded` | bool | existing | yes | false |

**Item identity (`ItemKey`)** = `{baseId, omods (canonical order), name?, health?, stolenFrom?}`; `count` is separate. Legendary is derived (any OMOD with the legendary flag). Not part of identity: favorites, loaded rounds (`ammoLoaded`, F09; when two stacks merge the `min(values)` so no rounds are created (C4; F09 §4.2)), `paPiece` (F17), stack index. `stolenFrom` is part of identity (deviation from world-economy §2 S7, which excludes it): the engine keeps owned items in separate stacks [inference], and per-unit stolen state must stay exact for F23 vendors.

**FO4 persisted entry JSON** (ids as `FormDesc`, per 01-sync-standard §8.3; world-economy's compact raw-id form is wire-only): `{"baseDesc":"4822:Fallout4.esm","count":1,"omods":[["4a0da:Fallout4.esm",0,1]],"name":"Lucky","health":0.75,"stolenFrom":"1c5e1:Fallout4.esm"}`. The loader also accepts the legacy numeric `baseId`. Each entry is parsed defensively: a missing base or OMOD drops that entry/OMOD with a log line, never the whole form (01-sync-standard §8.2).

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `SetInventoryFo4` (68) | S→C | `refId` u32 (0 = own actor; otherwise a container/corpse the user occupies or peeks, F06), `version` u32, `entries[]{baseId u32, count u32, omods[] (u8 len) {id u32, idx u8, rank u8}, name? (≤ 64 B), health? f32, stolenFrom? u32, ammoLoaded? u16, paPiece? bool}` | R, deferred channel 0, overwrite per (user, refId) per tick | on change; full snapshot | twin of `SetInventory` (28) |
| `CreateActorFo4` (64) `props.inventory` | S→C | same entry list | R | on subscribe; owner (and hoster subset, §4.7) only | reused (F00) |
| `SetFavorites` (118) | C→S | `slots[12]{present bool, item ItemKey}`, `taggedComponents u32[]` (≤ 64) | R | on change, ≤ 2/s | **new** (allocated by this spec, T1) |

All other inventory-changing intents are defined by their feature (F06 `PutItemFo4`/`TakeItemFo4`/`DropItemFo4`, F20 `UseItem`, F15 `CraftItemFo4`/`ScrapItem`, F16 `ModItem`, F23 `Barter`). They are reliable (I3, NET-005), carry an `ItemKey`, and are answered with `SetInventoryFo4`.

### 4.4 Client capture (owner side)
- The owner does not upload its inventory. Native `getInventory(ref)` (PLAT-080) walks `inventoryList->data[*].stackData` under the list read lock and returns `{baseId, count, omods (GetIndexData), name (kCustomName only), health, equipped (flags & kSlotMask), favorite}`.
- A `BGSInventoryListEvent` sink (platform event `inventoryChanged{ref, changeType, baseId, count, stackId, item}`) gives other features the exact `ItemKey` of a moved stack at capture time.
- Divergence check: after each local change, diff local vs last server snapshot by `ItemKey` (port of `getDiff`/`sumInventories`). A divergence not explained by a pending intent (vanilla script, auto-added ammo) is reverted to server truth and counted (`inventory_divergence_total`).
- Favorites (T1): `FavoritesMenu` close / `ExtraFavorite` change → `SetFavorites`.

### 4.5 Apply
- **Owner:** `SetInventoryFo4{refId:0}` → diff vs local by `ItemKey` → `removeItemEx(ref, item, count)` / `addItemEx(ref, baseId, count, omods[], name?, health?)` (PLAT-080). `addItemEx` builds an `ExtraDataList` with `BGSObjectInstanceExtra::AddMod` per OMOD, then instance data, instead of `AddItem`+`AttachModToInventoryItem` (papyrus-api-map §2.3.8). Silent (no HUD spam); ammo dragged in by a weapon add is stripped (prior-art §3.1.11). Equipped stacks are never removed-and-re-added (F05 keeps worn state).
- **Viewed containers/corpses** (`refId ≠ 0`): `resetContainer` + fill on the local ref (F06).
- **Remote actors:** no inventory is sent (S8). Ghosts receive only the worn instances through F05.
- **Stream-in / reconnect / respawn:** `CreateActorFo4.props.inventory` is applied after world entry (F00 §4.5 step 2); the 5 s reconcile (SkyMP `pcInv`) stays; the last applied `version` is kept in `sp.storage` so a hot reload re-applies without a full reset.

### 4.6 Validation & anti-cheat
1. Every intent naming an item resolves by **exact `ItemKey`** (`Inventory::FindByKey`). No match or short count → reject + `SetInventoryFo4` correction (I12, NET-007).
2. Server-created entries (`mp.set inventory`, console `AddItem`, Papyrus with mods, F14 rolls) pass `ObjectModValidator`: OMOD exists; target form type matches base (`WEAP`/`ARMO`); attach-point keyword is offered by the base `APPR` or by an attached OMOD's attach-parent slots; rank ≤ max rank; one OMOD per attach index (fo4-data-formats §4.12). Shared with F16.
3. Counts are u32 > 0, capped at `GameProfile::MaxStackCount()` (default 2^31 − 1); arithmetic overflow → reject.
4. `name` ≤ 64 UTF-8 bytes, no control characters; only F16 rename may set it from a client request.
5. Atomicity (S13): every multi-entry operation runs on a copy and commits once; B2 fixed; cross-reference transfers validate both sides before mutating either, and both ChangeForms land in the same save tick (§4.10).
6. Health is quantized to 1e-3 so float noise never splits stacks.
7. Caps: `GameProfile::CurrencyBaseId()` (FO4 0xF). Never dropped (F06), never stored in containers by players unless `caps.allowContainerStore` (server setting, default false); vendor containers are server-side (F23).
8. Rate limit: ≤ 20 inventory intents/s per actor; excess dropped with one correction.

### 4.7 Audience / visibility
- Actor inventory: owner only (`VisitPropertiesMode::All`, S8).
- Hosted NPC: the hoster receives a **combat subset** (WEAP, AMMO, thrown weapons) so its local AI can switch weapons (SkyMP-plus; SkyMP sends nothing). Routed with `GetActorToSendTo()` (S14).
- Containers and corpses: the occupant and peekers (F06). Never broadcast.

### 4.8 NPC parity
NPC inventories are built on the server from ESM data: `EnsureBaseContainerAdded` (FO4 variant) evaluates `CNTO`+`COED`, default outfit (`DOFT`→`OTFT`) and LVLI entries through F14, producing entries **with rolled OMODs** (fixes B7). The hoster gets the combat subset (§4.7); ammo and grenade consumption on the NPC's behalf goes through F09/F10. NPC loot after death is F06 corpse looting.

### 4.9 Gamemode API & server Papyrus
- `mp.get/set(id, 'inventory')` with FO4 entries `{baseId, count, omods:[{id,idx,rank}], name?, health?, stolenFrom?}`; `set` is validated (§4.6) and throws on invalid input.
- New read-only bindings: `inventoryWeight`, `caps`.
- Events: `onPutItem/onTakeItem/onDropItem` gain an `item` object (fix B11, implemented in F06); new `onInventoryChange(refId, added[], removed[], reason)` (observe-only, coalesced per tick).
- Papyrus natives: P0 (PVM-013) `AddItem/RemoveItem/GetItemCount/RemoveAllItems` with base-form semantics over instances (`RemoveItem` takes unmodded stacks first, then the fewest-mod stacks, stable by `ItemKey` order); P1 (PVM-014) `AttachModToInventoryItem`, `RemoveModFromInventoryItem`, `RemoveAllModsFromInventoryItem` (deterministic first match, documented), `GetComponentCount`, F4SE `GetAllMods`, `GetInventoryItems`, `GetInventoryWeight`, `Actor.MarkItemAsFavorite`.
- `OnItemAdded(akBaseItem, aiItemCount, akItemReference, akSourceContainer)` / `OnItemRemoved(…, akDestContainer)` fire from **every** inventory path (I22) through one choke point `MpObjectReference::NotifyInventoryDelta`, honouring `AddInventoryEventFilter` once PVM-007 lands. Gated by `GameProfile` so Skyrim keeps its current AddItem-only behaviour. Never fired by `ApplyChangeForm` loads.

### 4.10 Edge cases & failure modes
- **Disconnect mid-transfer:** the server applies or rejects atomically; the reconnecting client gets a fresh snapshot.
- **Crash between two ChangeForm writes:** transfers mutate both refs in one tick, so both reach the same `Upsert` batch (skymp-sync-inventory §1.10 step 2). The file driver still writes one file at a time; the residual window is covered by QA-040 and §8.
- **Plugin removed from the load order:** entries/OMODs whose `FormDesc` no longer resolves are dropped and logged (never the whole form).
- **Equipped item removed** (consumed, dropped, sold): F05 unequips and broadcasts.
- **Stack normalization:** after an OMOD change (F16) two entries may become equal; `Inventory::Normalize` merges them.
- **Hot reload:** client re-applies from `sp.storage`; server state is untouched.

### 4.11 Performance budget
- Entry on the wire ≈ 10 B + 6 B per OMOD; a 150-entry player inventory ≈ 3 KB. Log a warning above 16 KB.
- One `SetInventoryFo4` per (user, refId) per tick (overwrite channel). `FindByKey` is O(1) through a hash index; weight is recomputed only on change.

## 5. Engine / platform work required
- PLAT-080: `getInventory`, `addItemEx`, `removeItemEx`, `resetContainer`, `inventoryChanged` event with stack instance data, ammo stripping, favorites read/write (`ExtraFavorite` / F4SE `FavoritesManager`).
- Server: `ObjectModValidator` (new), `ObjectModEvaluator` for weight/value (SRV-022), FO4 `GetWeightFromRecord` via record views (REF-030).

## 6. Tests
- `L-unit` (`[F04]`, `[Inventory]`):
  - `AddItems` multi-entry regression (B2), Skyrim tests green;
  - stacking: same OMOD set in any order stacks; different rank splits; quantized health stacks; `stolenFrom` splits;
  - binary + JSON round trip of `SetInventoryFo4` with/without OMODs, name, health;
  - persistence round trip with `FormDesc` ids; legacy numeric `baseId` loads; a missing OMOD is dropped, the form survives;
  - accept: a server mutation sends exactly one `SetInventoryFo4` per tick to the owner only;
  - reject: unknown `ItemKey`, short count, invalid OMOD in `mp.set` → correction / exception;
  - late joiner: `CreateActorFo4.props.inventory` present for the owner, absent for neighbours; hoster gets only the combat subset;
  - `OnItemAdded`/`OnItemRemoved` fired once per entry on AddItems, transfer, reloot and craft paths; not on load;
  - weight: instance weight via the resolver, survival ammo-weight toggle, caps weigh 0.
- `L-fixture`: synthetic WEAP + OMOD + OBTE plugin (PluginBuilder) → `ObjectModValidator` accept/reject cases.
- `L-ts`: `ItemKey` diff and apply planner (adds/removes, equipped stacks untouched).
- `L-int`: two bots; server gives a modded pistol to A; A reconnects and still has it; B never receives A's inventory.
- `G-self`: `addItemEx` of a 10mm pistol with automatic receiver + legendary OMOD; `getInventory` returns the same key; no ammo dragged in.
- `G-manual`: give/take/rename flows; Pip-Boy shows correct names, stars and weights after reconnect.
- `D-real`: `[fo4data]` ESM-derived NPC inventories produce valid OMOD sets.

## 7. Tasks
- [ ] **F04-T01** FO4 extra-data schema: `OmodRef`, `ItemKey`, `Entry::omods`/`stolenFrom`, canonical sort, quantized health, `EqualExceptCount`, `FindByKey`, `Normalize`; Skyrim fields untouched — M — Depends: REF-008, NET-003 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/Inventory.{h,cpp}, unit/InventoryFo4Test.cpp
  - Accept: §6 stacking cases pass; `./unit/unit "[Inventory]"` (Skyrim) unchanged.
- [ ] **F04-T02** Fix bug B2 (`AddItems` early return → `continue` the outer loop) with regression test — S — Depends: — — Verify: L-unit — Files: Inventory.cpp, unit/InventoryTest.cpp
  - Accept: adding `[A(merges), B(new)]` yields both; upstreamable commit.
- [ ] **F04-T03** `SetInventoryFo4` (68): struct, TS mirror, per-(user, refId) overwrite channel, `invVersion` — S — Depends: NET-002, F04-T01 — Verify: L-unit — Files: skymp5-server/cpp/messages/SetInventoryFo4Message.h, Messages.h, MsgType.h, MpObjectReference.cpp (`SendInventoryUpdate`), falloutmp-client/src/services/messages/setInventoryFo4Message.ts
  - Accept: binary/JSON round trip; one message per tick under 50 mutations.
- [ ] **F04-T04** FO4 `EnsureBaseContainerAdded`: `CNTO`+`COED`, outfit, LVLI through F14, entries with rolled OMODs (B7) — M — Depends: ESPM-008, F14-T02, F14-T05 — Verify: L-unit, L-fixture — Files: MpObjectReference.cpp, skymp5-server/cpp/server_guest_lib/fo4/BaseContainerFo4.{h,cpp}
  - Accept: a synthetic NPC with a leveled weapon gets an entry whose OMODs match an `OBTS` combination.
- [ ] **F04-T05** Weight model: FO4 `GetWeightFromRecord` (no null deref), instance weight via SRV-022, survival ammo-weight toggle, cached weight, change notification to F08 — M — Depends: SRV-022, REF-030 — Verify: L-unit — Files: GetWeightFromRecord.cpp, MpObjectReference.cpp
  - Accept: weight of a modded rifle equals base + OMOD weight properties; F08 receives one notification per change.
- [ ] **F04-T06** `ObjectModValidator` for server-created entries (mp.set, console, Papyrus, F14) — M — Depends: ESPM-007 — Verify: L-unit, L-fixture — Files: skymp5-server/cpp/server_guest_lib/fo4/ObjectModValidator.{h,cpp}
  - Accept: wrong form type, missing attach point, duplicate attach index and over-rank are rejected.
- [ ] **F04-T07** Platform natives `getInventory`, `addItemEx`, `removeItemEx`, `resetContainer`, `inventoryChanged` event, ammo stripping — L — Depends: PLAT-080 — Verify: W-ci, G-self — Files: fallout4-platform/src/platform_fo4/InventoryApi.cpp, TESModPlatform natives
  - Accept: self-test round-trips a 3-OMOD legendary pistol and a PA piece with health 0.5.
- [ ] **F04-T08** Client apply/diff by `ItemKey` (fork of `sync/inventory.ts`), 5 s reconcile, divergence metric, `sp.storage` version — M — Depends: F04-T03, F04-T07, CLI-050 — Verify: L-ts, G-manual — Files: falloutmp-client/src/sync/inventory.ts, falloutmp-client/src/services/services/remoteServer.ts
  - Accept: L-ts planner suite green; no inventory flicker in G-manual.
- [ ] **F04-T09** `OnItemAdded`/`OnItemRemoved` on every inventory path (I22) via `NotifyInventoryDelta`, inventory-filter gating, GameProfile gate — M — Depends: F04-T01, PVM-007 — Verify: L-unit — Files: MpObjectReference.{h,cpp}, unit/InventoryEventsFo4Test.cpp
  - Accept: events observed (`onPapyrusEvent:OnItemAdded`) for AddItems, transfer, reloot and craft; none on load.
- [ ] **F04-T10** Gamemode surface: FO4 `inventory` shape, `inventoryWeight`, `caps`, `onInventoryChange`, docs — S — Depends: F04-T01 — Verify: L-unit, L-int — Files: skymp5-server/cpp/addon/property_bindings/InventoryBinding.cpp, docs (DOCS-003)
- [ ] **F04-T11** Papyrus natives: instance-aware `AddItem/RemoveItem/GetItemCount/RemoveAllItems`; `AttachModToInventoryItem` family; `GetComponentCount`; F4SE `GetAllMods/GetInventoryItems/GetInventoryWeight` — M — Depends: PVM-013, PVM-014, F04-T06 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/script_classes/PapyrusObjectReference.cpp
- [ ] **F04-T12** Hosted-NPC combat subset to the hoster — S — Depends: F04-T03, F13 (hosting) — Verify: L-unit
  - Accept: a new hoster receives the NPC's WEAP/AMMO entries; neighbours receive nothing.
- [ ] **F04-T13** (T1) Favorites and tagged components: `SetFavorites` (118), `PlayerProfile` fields, client capture/apply — S — Depends: SRV-060, F04-T07 — Verify: L-unit, G-manual
  - Accept: favorites survive reconnect on another machine; an unknown `ItemKey` is ignored with a log line.
- [ ] **F04-T14** Persistence: `FormDesc` ids in FO4 `inv` JSON, per-entry defensive parsing, legacy numeric load — S — Depends: REF-020, F04-T01 — Verify: L-unit — Files: MpChangeForms.cpp, unit/SaveStorageTest.cpp
- [ ] **F04-T15** In-game sign-off: instance round trip, reconnect, rename display; test script — S — Depends: F04-T08 — Verify: G-self, G-manual — Files: docs/falloutmp/test-scripts/F04-inventory.md

## 8. Open questions & risks
- File driver writes one JSON per form; a crash between two writes of a transfer can still dup/lose. Option: a write-ahead "transfer journal" record (ADR-010 record), decided after QA-040 results.
- Large junk inventories may exceed the snapshot budget; a delta variant of `SetInventoryFo4` may be needed (measure with NET-011).
- Exact legendary flag bit and `RemoveItem` instance-selection order must be verified in game (`G-self` against the vanilla engine).
