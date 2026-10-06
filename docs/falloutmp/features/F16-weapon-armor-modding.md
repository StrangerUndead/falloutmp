# F16 — Weapon & Armor Modding (OMOD)

| Field | Value |
|---|---|
| Tier | T1 (weapons/armor workbench, PA station mods and paint with F17). Robot mods are T2 (F21) |
| Target level | L4 |
| SkyMP analogue | None. Skyrim tempering is explicitly excluded from crafting (`falloutmp-server/cpp/server_guest_lib/CraftService.cpp:84-93`), and `Inventory::ExtraData` has no OMOD list (`Inventory.h:31-97`). SkyMP level L0; the nearest pattern is equipment re-apply with extra data (`skymp5-client/src/sync/equipment.ts`) |
| Milestone | M9 (weapons/armor benches); PA station parts with F17 in M10 |
| Workstreams | SRV, ESPM, CLI, PLAT, NET, GM, PVM |
| Depends on | F04 (`ItemKey` with `omods`), F05 (equipment re-apply with OMOD instances), F15 (`ComponentLedger`, `RecipeIndex`, `RequestResult`, nonce cache), F07 (occupancy), F19 (perks), F17 (PA station), SRV-021, SRV-022, ESPM-007, ESPM-008, PLAT-080, PLAT-081, PLAT-084 |
| References | reference/fo4-systems-world-economy.md S1, S2, §3.1–3.5; reference/fo4-data-formats.md §4.12 (OMOD), §4.13 (COBJ), §4.17 (INNR), §4.19 (OBTE/OBTS); reference/prior-art.md §5.1 B9, §5.3 Adopt 4; reference/papyrus-api-map.md §1.3 (ObjectMod, InstanceData, InstanceNamingRules) and the `addItemEx` row of the platform table; reference/fo4-systems-combat-character.md §1.7 (`WeaponInstanceResolver`), §12 (power armor); reference/fo4-animation-sync.md §3.4 (OMOD keywords drive weapon subgraphs) |

## 1. Summary
At a weapons or armor workbench, players change a weapon's receiver, barrel, grip, sights, magazine and muzzle, or an armor piece's lining and material. Each mod is built from components (perk-gated by Gun Nut, Science!, Armorer, Blacksmith) or attached from a loose mod they carry. Replaced mods return as loose mods. Players can rename items, and legendary effects are always preserved. The server validates the attach tree, consumes components, writes the new item instance (base + OMOD list + name) and recomputes its stats with the OMOD resolver. If the item is worn, every nearby client re-applies the equipment, so the new scope, barrel or paint shows up for everyone, including late joiners. Power armor pieces are modded and painted at the PA station with the same pipeline (with F17).

## 2. Vanilla Fallout 4 behaviour
- **Instance model:** an item = base WEAP/ARMO + OMODs in `BGSObjectInstanceExtra` (`ObjectIndexData{objectID, index(attach), rank, disabled}`). Identical mod sets stack; different sets do not [src: CLF4 B/BGSObjectInstanceExtra.h, B/BGSMod.h:141-148] (world-economy S1(b)). Prior art confirms extra type 0x35 and 8-byte records `{formID, attachIdx, rank, flag}` (prior-art §5.1 B9).
- **OMOD record** (data-formats §4.12): `DATA` holds the target form type (`WEAP`/`ARMO`/`NPC_`), max rank, **attach point** keyword, attach-parent slot keywords (sub-slots it opens), includes and 24-byte property entries (SET/MUL+ADD/ADD…). Other subrecords: `MNAM` target keywords, `FNAM` filter keywords, `LNAM` loose-mod MISC, `NAM1` priority. Items expose root slots through `APPR` keywords.
  - The Legendary/Mod-Collection flag bits disagree between references (0x08/0x40 in world-economy S1(b), 0x10/0x80 in data-formats §4.12). Verify before relying on either (§8).
- **Workbench flow:** choosing a mod either **builds** it (COBJ with `CNAM` = the OMOD; components; CTDA perk conditions) or **attaches** an owned loose mod. The replaced mod returns as a loose mod (weight 0.5). Most weapon slots cannot be emptied except muzzles; armor mods can be removed [web: wiki:Fallout_4_weapon_mods §Notes] (world-economy S2(a)).
- **Perk gates** (COBJ CTDA `HasPerk`): Gun Nut 1–4 `0004A0DA, 0004A0DB, 0004A0DC, 0016578E`; Science! 1–4 `000264D9, 000264DA, 000264DB, 0016578F`; Armorer 1–4 `0004B254, 0004B255, 0004B256, 001797EA`; Blacksmith 1–3 `0004B253, 0004B26A, 000264D8` [web: wiki pages cited in world-economy S2(a)].
- **Naming:** `INNR` rule sets (text + keywords + property compare) build names like "Short Hardened Combat Rifle". Receivers/grips change the "Pistol/Rifle" classification. A custom name lives in `ExtraTextDisplayData` (`kCustomName`); uniques cannot be renamed (world-economy S1(a), S2(a); data-formats §4.17 INNR).
- **Spawned instances** use object templates `OBTE`/`OBTS` (level range, default combination, includes) (data-formats §4.19). The server rolls them in F14.
- **Menus:** `ExamineMenu` (`INSPECT_MODE_STATE`, `modItem`, `modStack`, virtuals `CreateModdedInventoryItem`, `RenameCurrent`, `BuildConfirmed(ownerIsWorkbench)`), `WorkbenchMenuBase::ModChoiceData{mod, recipe, requiredItems, requiredPerks, rank, index}`, `PowerArmorModMenu`, `RobotModMenu` [src: CLF4 E/ExamineMenu.h, W/WorkbenchMenuBase.h]. The engine attaches to one stack with `BGSInventoryItem::ModifyModDataFunctor` (which splits stacks).
- **Papyrus:**
  - `AttachMod(ObjectMod, int aiAttachIndex)`, `AttachModToInventoryItem(Form, ObjectMod)`, `RemoveMod`, `RemoveModFromInventoryItem`, `RemoveAllMods`;
  - event `Actor.OnPlayerModArmorWeapon(Form akBaseObject, ObjectMod akModBaseObject)` [src: F4SE vanilla/ObjectReference.psc:230-233, 751-760; Actor.psc:1004-1016];
  - F4SE `ObjectMod.GetLooseMod/GetMaxRank/GetPropertyModifiers` (papyrus-api-map §1.3).
  - `AttachModToInventoryItem` is ambiguous when the container holds several instances of the base (papyrus-api-map, `addItemEx` row).
- **SP assumptions that break:** components, perk checks, loose-mod returns and the new stack are all computed locally; stack indices are client-local, so they are never a network identity.

## 3. SkyMP baseline
- There is no instance concept. Item identity is `baseId + Skyrim ExtraData` (B1), and Skyrim tempering COBJs are filtered out of crafting.
- **Reuse:**
  - `Inventory::RemoveItems`/`AddItems` atomic patterns, after the B2 fix (F04);
  - `UpdateEquipment` broadcast to neighbours and the `CreateActor` equipment snapshot (F05);
  - `ConditionsEvaluator` for COBJ CTDA (via F15).
- **New:** OMOD attach-tree validation, the mod transaction, OMOD stat resolution (SRV-022), and the client ExamineMenu capture.

## 4. Design

### 4.1 Authority model
- **Class A:**
  - the item instance (`ItemKey`: base, sorted `omods[]`, custom `name`, `health` for PA pieces);
  - mod costs and returns, loose-mod inventory, perk gates;
  - derived instance stats (SRV-022), consumed by F09 (capacity, fire rate, ammo), F11 (damage), F08 (weight) and F23 (value).
- **Class A** state rendered remotely: worn instances via F05 and PA frames via F17.
- **Class D:** the ExamineMenu preview model and stat comparison.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Item instance in inventory | `Inventory::Entry` + `ExtraData.omods`, `name` | actor `MpChangeFormREFR.inv` (F04) | yes (`inv`, key `omods`) | ESM / F14 template roll |
| Worn instance | equipment entry with `omods` | `equipment` (F05) | yes (`equipment`) | — |
| PA piece instance (omods, paint, `health`) | inventory entry | PA frame `inv` (F17) | yes | frame OBTE |
| Mod catalogue: per base, root `APPR` slots; OMOD by attach point; attach-parent slots; loose-mod map `LNAM` ↔ OMOD; mod COBJs by `CNAM` | cache | `ModCatalogue` (new) | no | ESM |
| Instance stats | struct | `ObjectModEvaluator` cache keyed by instance hash (SRV-022) | no (derived) | — |
| Resolved display name (INNR) | string | derived on demand (`InstanceNameResolver`) | no | — |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `ModItem` (85) | C→S | `nonce` u32, `workbenchRefId`, `target` u8 (`inventory`, `paFrame`, `robot` (T2)), `targetRefId` (0 = own inventory; else PA frame or robot), `item` (`ItemKey`), `ops[]` `{op: attach|detach, omodId, attachIdx u8, source: build|looseMod}`, `rename?` (≤ 64 B UTF-8) | R | on build/attach/rename confirm; ≤ 96 B + name | new |
| `RequestResult` (107) | S→C | `nonce`, `requestType`=85, `ok`, `error`, `items[0]` = new `ItemKey` | R | once per request | reused (F15) |
| `SetInventoryFo4` (68) | S→C | owner inventory | R | accept + every reject | reused (F04) |
| `UpdateEquipmentFo4` (67) | S→C | worn instances with `omods`, `numChanges` | R | if the modded item is worn | reused (F05) |
| `PowerArmorState` (88) | S→C | frame pieces with `omods`/paint/`health` | R | PA station mods | reused (F17) |
| `CreateActorFo4` (64) | S→C | equipment instances for late joiners | R | on subscribe | reused (F00/F05) |

Error codes are the F15 set, plus `NotAttachable`, `SlotRequired`, `LegendaryLocked`, `RenameDenied`, `LooseModMissing`.

### 4.4 Client capture (owner side)
- A platform hook on `ExamineMenu::BuildConfirmed` / `CreateModdedInventoryItem` / `RenameCurrent` (and on `PowerArmorModMenu`) reads:
  - `modItem` / `modStack` → `ItemKey` (PLAT-080);
  - the selected `ModChoiceData` (mod, recipe, index), from which `source` is derived (`recipe` set → build; else loose mod);
  - the slot index;
  - the rename text.
- It emits `modConfirm{workbench, target, item, ops, rename}`.
- Fallback without the hook: `OnPlayerModArmorWeapon` plus an inventory diff of the stack.
- Commit policy is the same as F15: T1 lets the local menu commit, and `SetInventoryFo4` (plus `UpdateEquipmentFo4` to self if worn) is the truth. A pre-commit cancel replaces this when RE allows.
- Filters: only while occupying a bench with `WBDT` 2/7/8 (9 at T2); the item must be in the local player's inventory, or in the frame at the PA station.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Owner:** `SetInventoryFo4` rebuilds stacks with `AddItemEx(ref, base, count, omods[], name?, health?)`, which builds a real `BGSObjectInstanceExtra` (PLAT-080). Papyrus `AttachModToInventoryItem` is never used, because it is ambiguous. If the item is worn, F05 re-equips it with a real `BGSObjectInstance` (PLAT-081). The menu refreshes (`refreshWorkbenchMenu`, F15-T09).
- **Remotes:** `UpdateEquipmentFo4` → F05 unequips the old instance and equips the new one (AddItemEx + EquipObject). The engine resolves INNR names and models locally from the same OMOD list. OMOD keywords can change the weapon subgraph, so F02 waits for the subgraph and re-sends a keyframe (fo4-animation-sync §3.4).
- **PA:** `PowerArmorState` carries the pieces; F17 applies them with `SyncFurnitureVisualsToInventory` for frames, or equip for a wearer.
- **Stream-in / respawn / reconnect:** worn instances come from the `CreateActorFo4` equipment snapshot; inventories from F04. There is no extra F16 state.

### 4.6 Validation & anti-cheat
Every failure sends `RequestResult{ok=false}` + `SetInventoryFo4` to the owner. If the item is worn, the owner also gets `UpdateEquipmentFo4`, so the local visual reverts (S12, I12). All mutations happen on copies (S13).
1. Own actor; rate ≤ 10 requests/s; nonce dedupe (shared F15 cache) (S6).
2. Occupancy and reach (F07-T04). Bench type vs target: `WBDT` 2 → WEAP; 7 → non-PA ARMO; 8 → PA pieces in a frame that is at the station (≤ 256 u) and usable by the actor under F17 rules; 9 → robot (T2).
3. The item exists by `ItemKey` (one instance is split off a stack); the frame holds the piece when `target = paFrame`.
4. **Attach-tree check** (`ModOpValidator`, pure), applied op by op:
   - OMOD form type = base type;
   - attach point ∈ available slots (base `APPR` ∪ attach-parent slots of currently attached mods);
   - `MNAM`/`FNAM` keyword compatibility; rank ≤ max rank; includes expanded;
   - attaching into an occupied slot implicitly detaches the old mod and its dependent sub-slot mods.
5. **Detach** to an empty slot is allowed only if the slot is optional. A slot is required when the base's default `OBTS` combination places an OMOD at that attach point `[inference]`; detaching from a required slot fails with `SlotRequired`.
6. **Legendary:** an OMOD with the Legendary flag can never be detached or replaced (`LegendaryLocked`); other slots stay moddable, and the legendary OMOD is kept in the new `ItemKey`.
7. **Source = build:** a COBJ with `CNAM` = the OMOD (or its `LNAM`); `BNAM` ∈ bench keywords; CTDA passes (Gun Nut/Science!/Armorer/Blacksmith via SRV-021); components via `ComponentLedger` (player → workshop container if permitted, F22).
   **Source = looseMod:** the `LNAM` MISC is in the player's inventory (or the workbench's workshop container); it is consumed.
8. **Returns:** each removed OMOD with an `LNAM` adds that loose mod to the player's inventory; an OMOD without `LNAM` returns nothing `[inference]`.
9. **Rename:** ≤ 64 bytes, printable, no control characters; denied for items whose base carries a keyword from `GameProfile.modding.noRenameKeywords` (uniques; `[inference]` mechanism); empty = clear the custom name.
10. Gamemode `onModItem` veto → `GamemodeBlocked`.
11. **Commit:** replace the instance; recompute stats (SRV-022, cache invalidated by the new instance hash). If worn: update `equipment` and broadcast. If the magazine capacity dropped, F09 returns the excess loaded rounds to the inventory.

### 4.7 Audience / visibility
- Owner: `RequestResult`, `SetInventoryFo4` (S8).
- Grid neighbours of the wearer: `UpdateEquipmentFo4`, only when the instance is worn.
- Neighbours of a PA frame (or its wearer): `PowerArmorState`.

### 4.8 NPC parity
- NPCs do not use workbenches. Hosted NPCs' inventories and equipment use the same `ItemKey`/resolver: their template rolls come from F14, and they are re-applied by F05 on the host and on remotes.
- Server Papyrus `AttachModToInventoryItem`/`AttachMod` on an NPC, or on a world ref, runs the same validator and commit, without the bench and cost checks.
- Companion gear modded by its owner (F21) follows the player path: trade the item, mod it, give it back.

### 4.9 Gamemode API & server Papyrus
- `onModItem(actorId, workbenchId, item, ops, newItem, rename)` **blockable**.
- `mp.get(refId, "inventory")` shows `omods`/`name`; `mp.getItemStats(itemKey)` (read-only, from SRV-022) for gamemode UIs and balance logs.
- Settings (S22): `modding.noRenameKeywords`, `modding.maxNameLength`, `modding.allowDetachRequired` (default false).
- Papyrus events: `Actor.OnPlayerModArmorWeapon(akBaseObject, akModBaseObject)` once per attached OMOD; `OnItemAdded`/`OnItemRemoved` for loose mods and components (F04-T09).
- Papyrus natives (PVM-014): `AttachMod`, `RemoveMod`, `RemoveAllMods` (world refs: update the ref's instance and broadcast via F05/CreateActor props); `AttachModToInventoryItem`, `RemoveModFromInventoryItem`, `RemoveAllModsFromInventoryItem` (resolve to the **first** matching stack in canonical `ItemKey` order, deterministically); F4SE `GetAllMods`, `ObjectMod.GetLooseMod`/`GetMaxRank`/`GetPropertyModifiers`.

### 4.10 Edge cases & failure modes
- **Stack of N identical instances:** exactly one is split off; the others keep the old `ItemKey`.
- **Modding the equipped weapon while reloading/firing:** the server orders messages. The new instance takes effect on the next F09 shot; in-flight shots use the instance hash logged in the shot registry (SRV-023).
- **Concurrent equip change and ModItem:** both are reliable and ordered (NET-004). If the `ItemKey` no longer exists → `ItemNotFound` + correction.
- **Disconnect mid-request:** atomic; no partial instance.
- **Plugin removed from the load order:** an unknown OMOD in a persisted `ItemKey` is dropped on load with a warning. The item keeps its base (backward-compatible load, §8 of the standard).
- **INNR differences:** none, since clients load the server's load order (F00-T09 manifest).
- **PA frame entered by another player mid-mod:** F17 occupancy wins; the request fails with `NotOccupant`.

### 4.11 Performance budget
- `ModItem` ≤ 96 B + name. The validator is O(slots × ops), ≤ 100 µs.
- Resolver results are cached per instance hash (SRV-022); a cache miss costs ≤ 50 µs `[inference]`.
- At most one `UpdateEquipmentFo4` per accepted request; mod sessions are rare (≤ 1/s per player).

## 5. Engine / platform work required
- ExamineMenu / PowerArmorModMenu hooks: confirm, slot/choice read, rename, refresh (shared with F15-T09).
- `AddItemEx` with OMOD list, name and health (PLAT-080); `EquipObject` with a real `BGSObjectInstance`, filtering the transient readied-weapon unequip and stripping auto-added ammo (PLAT-081).
- PA frame piece read/write and visuals (PLAT-084, F17).
- `getInstanceDisplayName(ref|stack)` (engine INNR) to cross-check the server name resolver in G-self.

## 6. Tests
- `L-unit` (`[F16]`, `[Omod]`):
  - `ModItem` round trip;
  - `ModOpValidator`: attach into root slot, sub-slot opened by a receiver, wrong form type, wrong attach point, filter keyword clash, implicit replace with dependent sub-slot removal, required-slot detach refused, legendary locked;
  - build with and without Gun Nut 2; loose-mod path consumes the MISC; replaced mod returned as loose mod;
  - component shortfall → no change + correction;
  - worn item → `UpdateEquipmentFo4` to neighbours and the late-joiner `CreateActorFo4` has the new instance;
  - unworn item → no neighbour traffic;
  - rename rules;
  - duplicate nonce idempotent;
  - gamemode veto → correction;
  - persistence round trip of a modded instance and of an old JSON without `omods`.
- `L-fixture`: synthetic WEAP with `APPR`, OMODs with attach points/parent slots/`LNAM`, COBJs with CTDA `HasPerk`.
- `D-real` (`[fo4data]`): 10mm pistol + automatic receiver + long barrel stats vs SRV-022; combat armor + lining; legendary instance kept.
- `L-int`: bot A mods its equipped rifle; bot B (neighbour) receives `UpdateEquipmentFo4` with the new `omods`; a late-joining bot C gets it in `CreateActorFo4`.
- `L-ts`: capture → `ModItem` builder from mocked menu state; reconcile on reject.
- `G-self`: read the `ModChoiceData` of a selected mod; INNR name from the engine equals the server name.
- `G-manual`: two players; A swaps a scope and paints PA, B watches in 1st and 3rd person; A reconnects and B rejoins late.

## 7. Tasks
- [ ] **F16-T01** `ModCatalogue` (root slots, OMOD by attach point, parent slots, `LNAM` map, mod COBJs) — M — Depends: ESPM-007, ESPM-008, F15-T02 — Verify: L-unit, L-fixture — Files: falloutmp-server/cpp/server_guest_lib/fo4/modding/ModCatalogue.{h,cpp}
  - Accept: the fixture weapon lists its slot tree; loose-mod lookups both directions.
- [ ] **F16-T02** `ModOpValidator` (pure: base + omods + ops → new omods | error), legendary and required-slot rules — M — Depends: F16-T01 — Verify: L-unit — Files: fo4/modding/ModOpValidator.{h,cpp}
  - Accept: every validator case in §6 passes; output is canonical (sorted by attachIdx, id).
- [ ] **F16-T03** `ModItem` (85) message + TS mirror — S — Depends: NET-002, F15-T01 — Verify: L-unit, L-ts — Files: falloutmp-server/cpp/messages/ModItemMessage.h, Messages.h; falloutmp-client/src/services/messages/
- [ ] **F16-T04** `ModService`: occupancy, bench type, build/loose-mod sources via `ComponentLedger`, returns, rename, atomic commit, `RequestResult` + corrections, `onModItem` — M — Depends: F16-T02, F16-T03, F15-T03, F15-T04, F04, NET-007 — Verify: L-unit — Files: fo4/modding/ModService.{h,cpp}, ActionListener.cpp, gamemode_events/ModItemEvent.{h,cpp}
  - Accept: all §6 service cases pass.
- [ ] **F16-T05** Worn-instance update: `equipment` rewrite, `UpdateEquipmentFo4` broadcast, instance-hash invalidation, F09 magazine reconciliation hook — S — Depends: F16-T04, F05, SRV-022 — Verify: L-unit
- [ ] **F16-T06** Resolver golden tests for modding scenarios (fixture + `[fo4data]`) — S — Depends: SRV-022, F16-T01 — Verify: L-unit, D-real — Files: unit/ModdingResolverTest.cpp
- [ ] **F16-T07** `InstanceNameResolver` (INNR rule sets + custom name) for gamemode/logs/barter display — S — Depends: ESPM-007, SRV-022 — Verify: L-unit, G-self — Files: fo4/modding/InstanceNameResolver.{h,cpp}
  - Accept: matches the engine name for 5 D-real instances.
- [ ] **F16-T08** PA station modding/paint: target `paFrame`, piece instances in the frame inventory, `PowerArmorState` broadcast (with F17) — M — Depends: F16-T04, F17, PLAT-084 — Verify: L-unit, G-manual
- [ ] **F16-T09** Platform ExamineMenu/PowerArmorModMenu capture (`modConfirm`), name read native; pre-commit cancel if RE allows — M — Depends: F15-T09, PLAT-080, PLAT-081 — Verify: W-ci, G-self — Files: fallout4-platform/src/.../WorkbenchApi.cpp
- [ ] **F16-T10** Client `modService.ts`: capture, send, reconcile, HUD errors — M — Depends: F16-T03, F16-T09, CLI-050 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/modService.ts
- [ ] **F16-T11** Server Papyrus OMOD natives + `OnPlayerModArmorWeapon` — S — Depends: PVM-014, F16-T02 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/script_classes/{PapyrusObjectReference.cpp,PapyrusObjectMod.cpp}
- [ ] **F16-T12** `G-manual` modding scenario script and sign-off — S — Depends: F16-T10, F05 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F16-modding.md
- [ ] **F16-T13** (T2) Robot workbench mods (`target = robot`, actor OMODs `actorOmods` in `CreateActorFo4` props) — M — Depends: F21, F15-T12 — Verify: L-unit, G-manual

## 8. Open questions & risks
- **OMOD flag bits** for Legendary/Mod Collection disagree between references (0x08/0x40 vs 0x10/0x80). Verify with a D-real dump before coding the legendary rule.
- The **required-slot** rule for detaching is `[inference]` from object templates; confirm in game which slots offer "none".
- The **unique/no-rename** mechanism is unknown `[inference]`; keep it data-driven (`modding.noRenameKeywords`).
- Visual pop when a worn weapon is re-equipped on remotes (subgraph reload) — measure in G-manual; F02 handles keyframe re-send.
- The ExamineMenu pre-commit cancel may need RE that does not exist yet. The fallback (local commit + overwrite) is acceptable but can flicker.
