# Fallout 4 Systems Reference — Items, Crafting, Settlements, Economy & World

> **Audience:** future Claude Code sessions implementing FalloutMP features (`features/F04, F06, F07, F14, F15, F16, F22, F23, F24, F25, F26, F27, F28, F03, F19, F20`).
> **Scope:** 20 world/economy systems. For each: (a) vanilla mechanics, (b) engine data, (c) runtime hooks, (d) single-player assumptions that break in MP, (e) FalloutMP design to the SkyMP standard, (f) difficulty and dependencies. Then cross-cutting messages/data models (§3) and tiering (§4).
> **Status:** research reference, Oct 2026. Nothing here is implemented yet. Message IDs are **not** assigned here; allocate them in `01-sync-standard.md §6`.

---

## 0. Conventions

**Provenance** (same as `README.md §3`):
- `[src: path:line]` — verified in code. Paths without prefix are this repo. External code uses these prefixes:
  - `CLF4:` = `github.com/libxse/commonlibf4` `include/RE/...` (snapshot read Oct 2026, AE 1.11.x IDs).
  - `F4SE:` = `github.com/ianpatt/f4se` 0.7.9, `scripts/vanilla/*.psc` (Bethesda vanilla script sources/signatures shipped with F4SE) and `scripts/modified/*.psc` (F4SE additions). We cite signatures and summarize logic; we do not copy bodies.
  - `xEdit:` = `github.com/TES5Edit/TES5Edit` branch `dev-4.1.6`, `Core/wbDefinitionsFO4.pas` / `Core/wbDefinitionsCommon.pas` (record layouts).
- `[web: URL]` — external page; re-check before relying on it. `wiki:` = `https://fallout.fandom.com/wiki/<Page>`.
- `[inference]` — reasoning, not verified.

**Sizes** (from `README.md §3`): S ≤ 2 days, M ≤ 2 weeks, L ≤ 6 weeks, XL > 6 weeks.

**Message names** are proposals (`PascalCase`). "Reuse X" means extend an existing SkyMP `MsgType` [src: falloutmp-server/cpp/messages/MsgType.h:4-44]. Every new message needs binary serialization + unit test (`06-workflow-conventions.md §4`).

**Gamemode events** follow SkyMP semantics: a C++ `GameModeEvent` is fired, any listener returning `false` blocks it and `OnFireBlocked` runs, otherwise `OnFireSuccess` applies the state change [src: falloutmp-server/cpp/server_guest_lib/gamemode_events/GameModeEvent.cpp:7-48]. New events below are listed as `onX(args) [blockable]`.

---

## 1. SkyMP baseline this document builds on

These code facts drive most designs below. Several are **bugs or gaps that must be fixed before FO4 work** (marked ⚠).

| # | Fact | Source |
|---|---|---|
| B1 | `Inventory::Entry` = `baseId`, `count` + Skyrim `ExtraData` (`health, enchantmentId, maxCharge, removeEnchantmentOnUnequip, chargePercent, name, soul, poisonId, poisonCount, worn, wornLeft`). Stack identity = `EqualExceptCount` over all those fields. | [src: falloutmp-server/cpp/server_guest_lib/Inventory.h:31-97], [src: .../Inventory.cpp:74-85] |
| B2 ⚠ | `Inventory::AddItems` returns from the whole function after the **first** merge (`return *this; // TODO: It seems there is a bug`). Any later entries in the same call are silently dropped when an earlier entry merged into an existing stack. FO4 multi-entry adds (crafting outputs, scrap yields, workshop transfers, reloot into a non-empty container) will lose items. Fix: `continue` the outer loop. | [src: .../Inventory.cpp:92-104] |
| B3 | Containers: activation of a `CONT` sets a single **occupant** (reach 512 u; FURN 256 u), sends `inventory` property + `OpenContainer`. `PutItem`/`TakeItem` throw unless the sender is the occupant. Second activation releases. | [src: .../MpObjectReference.cpp:1512-1568, 1570-1605, 632-658, 1640-1697] |
| B4 ⚠ | `CheckInteractionAbility` only checks same world/cell — **no activator distance check** (occupancy reach is measured from the *previous occupant*). Activation/transfer from anywhere in the cell is accepted. | [src: .../MpObjectReference.cpp:2003-2026] |
| B5 | Pickup of items/flora: `GivePickupItemsToActivationSource`, `SetHarvested(true)`, `RequestReloot()`; dynamic (`0xFF…`) items are deleted on pickup. | [src: .../MpObjectReference.cpp:1449-1466] |
| B6 | Reloot: per record type timers (`reloot` setting, defaults FLOR/TREE 1 h, items 1 h, CONT 1 h, **DOOR 3 s**), `forbiddenReloot` list; a CONT requests reloot when it becomes empty; `DoReloot` clears `isHarvested` and re-adds the base container (`baseContainerAdded=false`). ⚠ The CONT `Respawns` flag is **not** consulted. | [src: .../MpObjectReference.cpp:289-311, 873-892, 1118-1152], [src: falloutmp-server/cpp/addon/ScampServer.cpp:404-417], [src: docs/docs_server_configuration_reference.md:192-226] |
| B7 | `EnsureBaseContainerAdded` aggregates base CONT/NPC_ items + outfit into `std::map<baseId,count>` — no extra data, no instance templates. | [src: .../MpObjectReference.cpp:1947-2001] |
| B8 ⚠ | ESM refs instantiated only for base types `NPC_, FURN, ACTI, DOOR, CONT, FLOR/TREE (with result item)` and `IsItem` types. **Skipped:** "starts dead" actors, initially-disabled refs, deleted refs, every other base type (so FO4 `TERM`, `STAT`/`SCOL`/`MSTT` scrappables are invisible to the server). | [src: .../WorldState.cpp:391-437] |
| B9 ⚠ | `espm::utils::IsItem` = `AMMO ARMO BOOK INGR ALCH SCRL SLGM WEAP MISC LIGH`. FO4 needs `KEYM` and `NOTE`; `LIGH` is not an FO4 item. | [src: libespm/src/Utils.cpp:41-48] |
| B10 | Crafting: client infers a craft from container-change streaks while sitting at furniture, sends `CraftItem{workbench, craftInputObjects, resultObjectId}`; server matches COBJ by exact input counts + bench keyword + CTDA conditions, fires `onCraft`, then removes inputs / adds output. Skyrim temper benches are hardcoded out (`0xadb78`, `0x88108`). | [src: skymp5-client/src/services/services/craftService.ts:18-70], [src: .../CraftService.cpp:20-230], [src: .../gamemode_events/CraftEvent.cpp:16-40] |
| B11 | `onPutItem`/`onTakeItem` gamemode args omit extra data (`// TODO: implement extra data`). | [src: .../gamemode_events/PutItemEvent.cpp:17-31] |
| B12 | `DropItem`: blocks `0xF` (Gold001 — **also FO4 Caps001**), places a dynamic ref with `count`, keeps ≤ 10 drops per actor, deletes each after 120 s. Extra data is lost on drop. | [src: .../MpActor.cpp:1588-1720] |
| B13 | No corpse-loot path: `ProcessActivateNormal` has no `NPC_` branch, and Put/Take need an occupant. | [src: .../MpObjectReference.cpp:1419-1568] |
| B14 | Actor respawn (players and NPCs) after `spawnDelay` (default 25 s), re-rolling death items. | [src: .../MpChangeForms.h:109], [src: .../MpActor.cpp:1351-1420, 1515-1518] |
| B15 | `MpChangeFormREFR` persisted fields incl. `inv, isHarvested, isOpen, baseContainerAdded, nextRelootDatetime, isDisabled, profileId, isDeleted, count, equipment, factions, displayName, dynamicFields`. `quests` exists but is **not serialized**; `PapyrusQuest.GetStage` returns 0. | [src: .../MpChangeForms.h:57-133], [src: .../MpChangeForms.cpp:30-115], [src: .../script_classes/PapyrusQuest.cpp:5-38] |
| B16 | Time: the **client** sets globals `GameYear 0x35, GameMonth 0x36, GameDay 0x37, GameHour 0x38, TimeScale 0x3A` from real UTC + `hoursOffset` every 2 s and nudges timescale 0.6/1.2; no server clock. Server Papyrus: `GetCurrentGameTime` = days since start of the real year; `WaitGameTime` treats 1 game hour as 60 s (inconsistent). No weather sync anywhere. | [src: skymp5-client/src/services/services/timeService.ts:9-53], [src: .../script_classes/PapyrusUtility.cpp:49-79] |
| B17 | Client forces: fast travel disabled every frame; difficulty INI `iDifficulty:GamePlay=5`; `setInChargen(true,true,false)` (no save/wait); **every locked ref is unlocked locally**; activation blocked on FURN/ACTI/CONT/items/NPC/doors so the server decides. | [src: skymp5-client/src/services/services/disableFastTravelService.ts:9-11], [src: .../disableDifficultySelectionService.ts:20-29], [src: .../enforceLimitationsService.ts:11-17], [src: skymp5-client/src/extensions/objectReferenceEx.ts:37-60] |
| B18 | Properties: `mp.makeProperty` with `isVisibleByOwner/isVisibleByNeighbors` + client JS `updateOwner/updateNeighbor`; persisted in `dynamicFields`. Good vehicle for low-rate per-ref state (lock, power, radio). | [src: docs/docs_serverside_scripting_reference.md:6-41] |
| B19 | Grid visibility 4096-unit cells, 3×3 neighbourhood; ESM refs lazily attached per chunk. Each subscribed ref costs one `CreateActor` message. | [src: docs/FALLOUT4_PORT_RESEARCH.md §2.4] |

---
## 2. Systems

### S1. Item & inventory model (F04)

**(a) Vanilla mechanics**
- Inventory tabs: Weapons, Apparel, Aid, Misc (keys, holotapes, notes, bobby pins), Junk, Mods (loose mods), Ammo; caps and carry weight shown at the bottom [web: wiki:Pip-Boy_3000_Mark_IV].
- Weapons and armor are **instances**: base form + attached OMODs (receiver, barrel, grip, sights, magazine, muzzle, legendary, paint…). Items with the exact same mod set stack; different mod sets do not [web: https://steamcommunity.com/app/377160/discussions/0/496881136904806805]. Renaming a weapon/armor at a workbench gives a custom name (not for unique/named legendaries) [web: wiki:Weapons_workbench_(Fallout_4)].
- **Legendary** items carry one legendary OMOD (record flag `Legendary Mod`), show a star and a prefix name; cannot be scrapped; can be further modded and sold [web: wiki:Fallout_4_legendary_weapon_effects], [src: xEdit wbDefinitionsFO4.pas OMOD flags 0x08 "Legendary Mod"]. Drops come from `LegendaryItemQuest`/`LegendaryItemQuestScript.GenerateLegendaryItem(ObjectToSpawnIn, LeveledListOfItemsToSpawn, ListOfSpecificModsToChooseFrom, ListOfSpecificModsToDisallow)` [web: wiki:LegendaryItemQuestScript.psc].
- **Condition:** no weapon/armor degradation in vanilla FO4. Only power-armor pieces have health (break to weight 0, repaired at the PA station) [web: wiki:Power_armor_station_(Fallout_4)]. `ObjectReference.GetItemHealthPercent()` ("smithed health") is vestigial [src: F4SE vanilla/ObjectReference.psc:415-416].
- **Favorites:** 12 slots (D-pad / PC keys 1…0,-,=) [web: https://gamefaqs.gamespot.com/boards/164593-fallout-4/73891978]. Components can be **tagged for search** (UI highlighting).
- **Junk** = MISC with a component list; auto-broken down when crafting/building needs a component; never weapons/armor [web: wiki:Fallout_4_crafting]. Caps cannot be dropped or put in containers [web: wiki:Bottlecap_(Fallout_4)].

**(b) Engine data**
| Data | Where | Notes |
|---|---|---|
| Inventory list | `TESObjectREFR::inventoryList : BGSInventoryList*` → `BSTArray<BGSInventoryItem> data`, `cachedWeight`, `owner`, `rwLock` | [src: CLF4 T/TESObjectREFR.h:608], [src: CLF4 B/BGSInventoryList.h] — replaces Skyrim `ExtraContainerChanges` |
| Item | `BGSInventoryItem{ TESBoundObject* object; BSTSmartPointer<Stack> stackData }`; `Stack{ nextStack, extra: ExtraDataList, count, flags }`, flags `kSlotIndex1..3` (= equipped), `kEquipStateLocked`, `kInvShouldEquip`, `kTemporary` | Stacks addressed by **index** (`GetStackByID`) — unstable across clients, never use as network identity [src: CLF4 B/BGSInventoryItem.h] |
| Write functors | `ModifyModDataFunctor(mod, slotIndex, attach, success*)`, `SetHealthFunctor`, `ModCountFunctor`, `SetFlagFunctor`, `ClearEquipFlagsFunctor` (with `shouldSplitStacks`) via `BGSInventoryList::FindAndWriteStackDataForItem` | Engine-native way to edit one stack [src: CLF4 B/BGSInventoryItem.h] |
| OMOD list | `BGSObjectInstanceExtra` (`EXTRA_DATA_TYPE::kObjectInstance`): `values` buffer of `BGSMod::ObjectIndexData{objectID, index(attach), rank, disabled}`; `AddMod`, `RemoveMod`, `HasMod`, `GetIndexData()`, static `AttachModToReference` | [src: CLF4 B/BGSObjectInstanceExtra.h], [src: CLF4 B/BGSMod.h:141-148] |
| Computed stats | `ExtraInstanceData{base, TBO_InstanceData}`; `BGSInventoryItem::GetInstanceData(stack)` | Derived; never sent [src: CLF4 E/ExtraInstanceData.h] |
| Other extras | `ExtraHealth{float}` (PA piece), `ExtraTextDisplayData` (custom name: `kCustomName`), `ExtraFavorite{quickkeyIndex}`, `ExtraAmmo{count}` (loaded rounds), `ExtraUniqueID{uniqueID, baseID}`, `kOwnership`, `kCount` | [src: CLF4 E/ExtraHealth.h, E/ExtraTextDisplayData.h, E/ExtraFavorite.h, E/ExtraAmmo.h, E/ExtraUniqueID.h, E/EXTRA_DATA_TYPE.h] |
| OMOD record | flags `Legendary Mod`(0x08), `Mod Collection`(0x40); `DATA`: target form type (`ARMO`/`NPC_`/`WEAP`), max rank, attach point keyword, attach parent slots, includes, property mods (`BGSMod::Property::Mod{target:11, op:Set/Mul/Add…, type, data}`); `MNAM` target OMOD keywords, `FNAM` filter keywords, `LNAM` loose mod MISC, `NAM1` priority | [src: xEdit wbDefinitionsFO4.pas:12501-12560], [src: CLF4 B/BGSMod.h] |
| Weapon/armor templates | `OBTE` count + combinations `OBTS{levelMin, levelMax, parentCombination, default, keywords[], includes[{omod, attachIdx, optional, dontUseAll}], props}` | Default/level-scaled mod sets for spawned items [src: xEdit wbDefinitionsFO4.pas:5499-5528] |
| Loose mod | `TESObjectMISC` with form flag `0x80` (`IsLooseMod`); map `BGSMod::Attachment::GetAllLooseMods()` | [src: CLF4 T/TESObjectMISC.h], [src: CLF4 B/BGSMod.h:153-157] |
| Naming | `INNR` (target Armor/Actor/Furniture/Weapon; rule sets of {text, keywords, property compare}) | [src: xEdit wbDefinitionsFO4.pas:12337-12385] |
| Leveled items | `LVLI`: `LVLD`, `LVLF`, `LVLG`, entries + `COED`, `LLKC` filter keyword chances, `LVSG` epic loot chance, `ONAM` override name | [src: xEdit wbDefinitionsFO4.pas:10009-10031] |
| IDs | Caps001 `0x0000000F`, BobbyPin `0x0000000A` | [web: wiki:Bottlecap_(Fallout_4)], [web: wiki:Bobby_pin_(Fallout_4)] |

**(c) Runtime hooks**
- Papyrus: `AddItem/RemoveItem/RemoveAllItems/GetItemCount/GetComponentCount/RemoveComponents/RemoveItemByComponent`, `AttachModToInventoryItem/RemoveModFromInventoryItem/RemoveAllModsFromInventoryItem`, events `OnItemAdded(Form, int, ObjectReference akItemReference, ObjectReference akSourceContainer)`, `OnItemRemoved`, `OnContainerChanged`; `Actor.MarkItemAsFavorite(Form, int aiSlot)` [src: F4SE vanilla/ObjectReference.psc:203-760, 1040-1048], [src: F4SE vanilla/Actor.psc:499]. F4SE: `GetInventoryItems()` (base forms only), `GetAllMods()`, `Actor.GetWornItemMods(slot)`, `FavoritesManager.GetFavorites/SetFavorites/AddTaggedForms/IsTaggedForm` [src: F4SE modified/ObjectReference.psc:2,36], [src: F4SE modified/Actor.psc:17], [src: F4SE modified/FavoritesManager.psc].
- Engine events: `TESContainerChangedEvent{oldContainer, newContainer, baseObject, itemCount, referenceFormID, uniqueID}`; `BGSInventoryListEvent::Event{changeType: AddStack/ChangedStack/AddNewItem/RemoveItem/Clear/UpdateWeight, owner, objAffected, count, stackID}` — the latter carries the **stack id**, use it to read the changed stack's `BGSObjectInstanceExtra` [src: CLF4 T/TESContainerChangedEvent.h], [src: CLF4 B/BGSInventoryListEvent.h]; `FavoriteMgr_Events::ComponentFavoriteEvent{component, isFavorited}` [src: CLF4 F/FavoriteMgr_Events.h].

**(d) SP assumptions that break**
- Engine stack indices and runtime instance data are client-local; two clients only agree on `baseId + OMOD list + name + health`.
- Leveled-list rolls of **which template/mods** a weapon spawns with are client-random → non-deterministic loot (the FO4_Wrld finding) [src: docs/FALLOUT4_PORT_RESEARCH.md §3.4].
- Favorites/tagged components are UI preferences stored in the save; in MP they must survive across machines.

**(e) FalloutMP design**
- *Authority:* server-authoritative (as SkyMP, L4).
- *Item identity (`ItemKey`):* `{ baseId, omods: OmodRef[] (sorted by attachIdx, objectId), name?: string (custom only), health?: float (PA pieces; quantize 1e-3) }`. `OmodRef = { id: FormId, idx: u8 attach index, rank: u8 }` (drop `disabled`, recompute). Legendary = derived (any OMOD with flag 0x08). `count` is separate. **Not** part of identity: favorites, loaded ammo (`ExtraAmmo`, kept in actor equipment state, F09), stack index.
- *Inventory changes:* add `std::optional<std::vector<OmodRef>> omods` to `Inventory::ExtraData`; include it in `EqualExceptCount` and `Serialize` (key `"omods"`, compact `[[id,idx,rank],…]`). Reuse `name` and `health`. Skyrim-only fields stay in the shared struct but are never set under the FO4 `GameProfile`. Fix B2. Add `Inventory::FindByKey(ItemKey)`.
- *Server instance evaluator:* new `ObjectModEvaluator` (libespm + server) applying OMOD property mods and `OBTS` template props to WEAP/ARMO base data → damage, AP cost, weight, value, ammo, keywords, INNR display name. Consumers: damage (F11), weight/encumbrance (F08), barter value (S6), mod validation (S2), logs/gamemode.
- *Spawned items:* when the server evaluates a LVLI producing a WEAP/ARMO, it also rolls the **object template combination** (level range, `LLKC` keyword chances, `default` flag) and stores the resulting `omods` on the entry. Legendary rolls happen on the server for legendary NPC death (`onLegendaryDrop` gamemode hook).
- *Persistence:* `MpChangeFormREFR.inv` unchanged in shape (entries carry `omods`). Player record (`PlayerProfile`, §3.2): `favorites[12]: ItemKey|null`, `taggedComponents: FormId[]`.
- *Protocol:* reuse `SetInventory`, `PutItem`, `TakeItem`, `DropItem` (add `omods`; `DropItem` must accept the full `Inventory::Entry`), `UpdateEquipment` (entries with omods), `CreateActor.props.inventory/equipment`. New `SetFavorites{slots: ItemKey?[12]}` (C→S, reliable, on change).
- *Client capture:* FO4 platform native `getInventory(ref)` walking `inventoryList->data[*].stackData` returning `{baseId, count, omods (GetIndexData), name (kCustomName only), health, equipped(flags&kSlotMask), favorite}`; diffing reuses `skymp5-client/src/sync/inventory.ts` (`getDiff`, `sumInventories`) with the new key.
- *Apply:* native `TESModPlatform.AddItemEx(ref, baseId, count, omods[], name?, health?)` that builds an `ExtraDataList` with `BGSObjectInstanceExtra` (via `AddMod` per entry) then `CreateInstanceDataForObjectAndExtra` [src: CLF4 B/BGSMod.h Template::Items], instead of Papyrus `AddItem`+`AttachModToInventoryItem` (which works on "an" item of that base and is ambiguous with several instances).
- *Validation:* every client message naming an item must match an existing entry by `ItemKey`; unknown OMODs rejected; `omods` must be attachable to the base (target form type, attach points) — reject otherwise.
- *Gamemode:* `onPutItem/onTakeItem/onDropItem` args gain an `item` object `{baseId,count,omods,name,health}` (fix B11). New `mp.get(refId,"inventory")` returns omods.
- *Server Papyrus:* implement `AttachModToInventoryItem`, `RemoveModFromInventoryItem`, `RemoveAllModsFromInventoryItem`, `GetComponentCount`, `RemoveComponents`, `RemoveItemByComponent`, F4SE `GetAllMods`; fire `OnItemAdded/OnItemRemoved` (commented out today [src: .../MpObjectReference.cpp:846-866]).
- *Tests (`[Inventory][fo4]`, L-unit):* AddItems multi-entry regression (B2); stacking by omods (same set stacks, order-insensitive, different rank splits); JSON + binary round-trip with/without omods; backward-compat load of old JSON (missing `omods` → none); `ObjectModEvaluator` against synthetic OMOD/WEAP fixtures (`[fixture]`), and `[fo4data]` cases (10mm pistol + automatic receiver).

**(f)** L. Depends: ESPM FO4 WEAP/ARMO/OMOD/MISC/INNR/LVLI parsers, PLAT natives (`getInventory`, `AddItemEx`), F14 (leveled lists).

---

### S2. Weapon & armor modding (F16)

**(a) Vanilla**
- Done at the weapons workbench (guns, melee), armor workbench (armor/clothing), power-armor station, robot workbench (S3). Each item exposes mod **slots** (attach points); choosing a mod either builds it (consumes components; needs perks) or attaches an owned loose mod ("Attach Mod" instead of "Build") [web: wiki:Fallout_4_weapon_mods §Notes].
- Replacing a mod puts the old one in the inventory as a loose mod (weight 0.5). Most weapon mods cannot simply be removed except muzzles; armor mods can be removed [web: wiki:Fallout_4_weapon_mods §Notes].
- Perk gates (via COBJ conditions): **Gun Nut** INT 3, ranks 1–4 (`0004A0DA, 0004A0DB, 0004A0DC, 0016578E`), **Science!** INT 6 (`000264D9, 000264DA, 000264DB, 0016578F`), **Armorer** STR 3 (`0004B254, 0004B255, 0004B256, 001797EA`), **Blacksmith** STR 4, 3 ranks (`0004B253, 0004B26A, 000264D8`) [web: wiki:Gun_Nut_(Fallout_4), wiki:Science!_(Fallout_4), wiki:Armorer_(Fallout_4), wiki:Blacksmith]. Handmade rifle (Nuka-World) additionally checks Commando/Gunslinger/Rifleman ranks.
- Receivers/grips change perk classification and **name** ("Pistol"/"Rifle" suffix via INNR). Renaming allowed except uniques.
- Scrapping a weapon/armor at its bench returns components of the item and its attached mods, scaled by component rarity (Scrapper ranks) and scrap scalar (S4). Legendaries cannot be scrapped.
- Crafting a mod grants a little XP; companions react (affinity).

**(b) Engine data:** OMOD (S1b); COBJ for each mod: `FVPA` components, `CTDA` conditions (perks), `CNAM` created object = the OMOD or loose-mod MISC, `BNAM` bench keyword, `FNAM` category keywords, `INTV{count, priority}` [src: xEdit wbDefinitionsFO4.pas:10266-10284]. FURN `WBDT` bench type: 2 Weapons, 7 Armor, 8 Power Armor, 9 Robot Mod, 5 Alchemy (chem+cooking), 1 Create Object [src: xEdit wbDefinitionsFO4.pas:6885-6900].

**(c) Hooks**
- Menu: `ExamineMenu` (`WorkbenchMenuBase` subclass; `INSPECT_MODE_STATE {kModSlotSelect, kSelectNewMod, kInvComponentSelect, kItemSelect, kConfirm}`, `modItem`, `modStack`, `scrappingArray`, `BuildWeaponScrappingArray()`, `ConsumeSelectedItems(autoBuild, sound)`, virtuals `CreateModdedInventoryItem`, `RenameCurrent`, `RepairSelectedItem`, `BuildConfirmed(ownerIsWorkbench)`) [src: CLF4 E/ExamineMenu.h]; `ExamineConfirmMenu`; `WorkbenchMenuBase::ModChoiceData{mod|object, recipe, requiredItems, requiredPerks, rank, index}`, `sharedContainerRef`, `workbenchContainerRef`, `sharedContainers` [src: CLF4 W/WorkbenchMenuBase.h]; `PowerArmorModMenu`, `RobotModMenu` (RTTI only) [src: CLF4 IDs_RTTI.h].
- Papyrus: `AttachMod(ObjectMod, int aiAttachIndex)`, `AttachModToInventoryItem(Form, ObjectMod)`, `RemoveMod`, `RemoveModFromInventoryItem`, `RemoveAllMods` [src: F4SE vanilla/ObjectReference.psc:230-233, 751-760]; event `Actor.OnPlayerModArmorWeapon(Form akBaseObject, ObjectMod akModBaseObject)`, `OnPlayerUseWorkBench(ObjectReference)` [src: F4SE vanilla/Actor.psc:1004-1016]; F4SE `ObjectMod.GetPropertyModifiers()`, `GetLooseMod()`, `GetMaxRank()` [src: F4SE modified/ObjectMod.psc:164-181].
- Engine: `BGSInventoryItem::ModifyModDataFunctor` is what the menu uses to attach to a stack (splits stacks).

**(d) Breaks in MP:** the menu consumes components and edits inventory locally; perk checks run on the client; loose-mod return is local; the workbench "shared container" (workshop + supply network) is client view of server data.

**(e) Design**
- *Authority:* server-authoritative transaction. Client UI is vanilla `ExamineMenu`, but the final commit is a request.
- *Capture:* hook `ExamineMenu::BuildConfirmed` / `CreateModdedInventoryItem` (or watch `OnPlayerModArmorWeapon` + inventory diff) → send **`ModItem`** `{ workbenchRefId, item: ItemKey, source: "inventory"|"workbench", attach: [{omodId, idx}], detach: [{omodId, idx}], useLooseMod: bool, rename?: string }` (C→S, reliable). Suppress the local result (revert via the server's `SetInventory`).
- *Server checks:* actor occupies the bench FURN (SkyMP occupancy, 256 u, B3) and bench type matches item type; item exists by `ItemKey`; OMOD target form type matches; attach point keyword exists in the item's current attach-parent set (base + attached mods); exclusivity/`childrenExclusive`; if building: a COBJ with `CNAM` = this OMOD (or its loose mod) and `BNAM` ∈ bench keywords, CTDA passes (needs FO4 condition functions `HasPerk`, `GetValue`, `GetGlobalValue`, `GetIsID`, `HasKeyword`, `GetStageDone`), components available from player + workshop shared inventory + supply network (S4/S5); if `useLooseMod`: loose MISC present in player or workbench inventory; rename: ≤ 64 chars, not on uniques, profanity filter hook.
- *Apply (atomic):* consume components (with junk auto-break, S4), remove loose mod if used, add replaced mods back as loose mods, replace the inventory entry with the new `ItemKey`, if equipped update `Equipment` and broadcast `UpdateEquipment` to neighbours. Reply **`ModResult{ok, error?, newItem: ItemKey}`**.
- *Scrap at bench:* **`ScrapItem{workbenchRefId, item: ItemKey, count}`** → yields computed by server (S4); rejected for legendary/quest items.
- *Remote rendering:* neighbours receive equipment with omods → `AddItemEx` + equip (S1).
- *Gamemode:* `onModItem(actorId, item, attach, detach) [blockable]`, `onScrapItem(actorId, item, yields) [blockable]`. *Papyrus:* fire `OnPlayerModArmorWeapon` on the actor; implement `AttachMod*`/`RemoveMod*` natives server-side (inventory + world refs).
- *Tests (`[Omod]`, `[Craft][fo4]`):* attach with/without perk; wrong bench; non-attachable OMOD; loose-mod path; replaced mod returned; equipped item re-broadcast; component shortfall → no change; concurrent double-submit idempotent (nonce).

**(f)** L. Depends: S1, S4, F19 (perks/conditions), ESPM COBJ/OMOD.

---

### S3. Crafting stations (F15)

**(a) Vanilla**
- Stations: chemistry (chems, grenades, mines, stimpaks, syringer ammo, cutting fluid), cooking (food/drink from ingredients), armor workbench, weapons workbench, power-armor station, robot workbench (Automatron, after *Mechanical Menace*; builds/modifies robot companions incl. Codsworth/Ada), Contraptions manufacturing machines (ammunition plant, armor/weapon forges, auto-loom, food processor…), Nuka-mixer (Nuka-World), decontamination arch (Wasteland Workshop, powered, removes rads) [web: wiki:Fallout_4_crafting], [web: wiki:Robot_workbench], [web: wiki:Decontamination_arch].
- Most recipes known by default; perk/quest/schematic conditions gate some (e.g., Chemist, Medic, Demolition Expert, Nuka-nuke schematics) [web: wiki:Fallout_4_crafting]. Stations in an allied settlement share the workshop inventory (and the supply-line network); Contraptions machines are the exception.
- Recipes use **components** and also whole items (meat, Abraxo, stimpaks…). Junk is broken down automatically to satisfy component needs; leftovers go to the workshop (S4).

**(b) Data:** COBJ (`FVPA` entries reference CMPO components *or* items; `CNAM` item or FLST; `BNAM` bench keyword; `FNAM` category; `INTV` count/priority) [src: xEdit wbDefinitionsFO4.pas:10266-10284]; `BGSConstructibleObject{requiredItems, conditions, createdItem, benchKeyword, data{numConstructed, workshopPriority}, filterKeywords}`, `FindRecipeForCreatedForm`, `PlayerPassesConditions` [src: CLF4 B/BGSConstructibleObject.h]; FURN `WBDT`.

**(c) Hooks:** menus — `ExamineMenu` (weapon/armor), `CookingMenu` RTTI (chem/cooking; *[inference]* verify it is the class for both), `PowerArmorModMenu`, `RobotModMenu` [src: CLF4 IDs_RTTI.h]; events `BGSCraftItemEvent{workbench, location, createdItemBase}` (story event) [src: CLF4 B/BGSCraftItemEvent.h], `TESFurnitureEvent{actor, targetFurniture, kEnter|kExit}` [src: CLF4 T/TESFurnitureEvent.h], Papyrus `Actor.OnPlayerUseWorkBench`, `OnPlayerCreateRobot(Actor)`, `OnPlayerModRobot(Actor, ObjectMod)` [src: F4SE vanilla/Actor.psc:980-1016]; F4SE `ConstructibleObject.GetConstructibleComponents/GetCreatedObject/GetWorkbenchKeyword` [src: F4SE modified/ConstructibleObject.psc].

**(d) Breaks:** SkyMP's craft inference from container streaks is fragile with auto-scrap (one craft removes several junk items and adds leftovers); local perk checks; robots are new actors created locally.

**(e) Design**
- **Replace inference by explicit `CraftItem` v2** (reuse MsgType 13, add fields): `{ workbench, recipeId (COBJ), count, clientNonce }` (old fields kept for Skyrim). Client captures from the menu (`BGSCraftItemEvent` + selected recipe) and suppresses local results.
- Server: occupancy of the bench, COBJ `BNAM` ∈ bench keywords, CTDA conditions, then resolves `requiredItems` against player + (if bench is linked to a workshop) workshop container + supply network, breaking junk with CVPA as needed (S4); output = `CNAM` × `INTV.count`. Remove the Skyrim temper constants from `RecipeItemsMatch` behind `GameProfile` [src: .../CraftService.cpp:84-93].
- Robot workbench: robot = `MpActor` owned by the crafter (`ownerProfileId`); robot OMODs (form type `NPC_`) stored as `actorOmods: OmodRef[]` on its change form and sent in `CreateActor.props` so all clients `AttachMod` them. Messages: `RobotBuild{workbench, recipeId}`, `RobotMod{workbench, robotId, attach[], detach[]}` (reuse `ModItem` shape). Companion behaviour → F21.
- Contraptions machines: server timer production while powered (S5 power), output to linked container/conveyor end; physics conveyors are cosmetic (S5).
- *Gamemode:* `onCraft` keeps its signature (+`count`), new `onRobotBuild`. *Papyrus:* fire `OnPlayerUseWorkBench`, `OnPlayerCreateRobot`, `OnPlayerModRobot`.
- *Tests:* extend `unit/CraftTest.cpp` with `[Craft][fo4][fixture]`: component recipe, item+component recipe, auto-scrap with leftovers to workshop, perk-gated recipe, FLST created object, count>1.

**(f)** M (stations) + M (robot workbench). Depends: S1, S4, F19, F13/F21 for robots.

---

### S4. Components & scrapping (F15)

**(a) Vanilla**
- 31 components (acid, adhesive, aluminum, antiseptic, asbestos, ballistic fiber, bone, ceramic, circuitry, cloth, concrete, copper, cork, crystal, fertilizer, fiber optics, fiberglass, gears, glass, gold, lead, leather, nuclear material, oil, plastic, rubber, screw, silver, spring, steel, wood); vegetable starch → adhesive and cutting fluid → oil are crafted sources [web: wiki:Fallout_4_junk_items §Scrapping].
- Scrapping junk always returns its listed components. Scrapping attached weapon/armor mods and **workshop statics** returns only a fraction: rarity (Common always; Uncommon needs Scrapper 1; Rare needs Scrapper 2; adhesive/oil zero yield) × scrap scalar (round down) [web: wiki:Fallout_4_junk_items]. **Scrapper** INT 5: `00065E65`, `001D2483`, rank 3 (Far Harbor) [web: wiki:Scrapper_(Fallout_4)].
- Workshop mode can scrap world objects inside the build area (trees, cars, debris); green outline = scrap or store, yellow = scrap only [web: wiki:Fallout_4_crafting]. Scrapping a container moves its contents to the workshop.
- Junk can be stored in bulk into a workbench ("store all junk") and is then usable by every linked settlement [web: wiki:Fallout_4_junk_items].

**(b) Data:** `CMPO{FULL, CUSD sound, DATA auto-calc value, MNAM scrap item (MISC), GNAM mod scrap scalar (GLOB)}` [src: xEdit wbDefinitionsFO4.pas:12294-12302], `BGSComponent{scrapItem, modScrapScalar}` [src: CLF4 B/BGSComponent.h]; MISC `CVPA` components + `CDIX` display indices, record flag `Calc From Components` [src: xEdit wbDefinitionsFO4.pas:10212-10247]; scrap recipes = COBJ with category keyword `WorkshopRecipeFilterScrap`, `CNAM` = item or FLST [web: https://forums.nexusmods.com/topic/4074885-items-scrapable/].

**(c) Hooks:** Papyrus `MiscObject.GetObjectComponentCount(Component)` [src: F4SE vanilla/MiscObject.psc:3-4]; F4SE `MiscObject.GetMiscComponents`, `Component.GetScrapItem/GetScrapScalar` [src: F4SE modified/MiscObject.psc, Component.psc]; F4SE debug-only `ObjectReference.Scrap(workshop)` (no materials, power bugs) [src: F4SE modified/ObjectReference.psc:24-30]; engine `Workshop::ScrapReference(ContextData, ref, BSTArray<(TESBoundObject*,count)>* rewards)` [src: CLF4 W/Workshop.h]; `ExamineMenu::scrappingArray`.

**(d) Breaks:** auto-break chooses arbitrary junk locally; workshop scrap deletes ESM refs locally (persisted in save as deleted/disabled) — other clients and the server never learn of it.

**(e) Design**
- **Server component resolver** `ComponentLedger::Take(sources[], needs[])`: sources ordered player → workshop container → linked network; satisfy from raw CMPO-item first, then break junk deterministically (least value per needed component first; stable by FormId); leftovers to the workshop container if at a workshop, else player. Pure function → unit-testable.
- **Scrap yields:** `yield(c) = floor(count(c) × scalar(c))` with rarity filter by Scrapper rank, from the item's scrap recipe + COBJs of each attached mod; constants from records, formula behind `GameProfile` with gamemode override.
- **Messages:** `ScrapItem` (S2); `WorkshopScrap{workshopRefId, targetRefId}` and `StoreAllJunk{workbenchRefId}` (S5). Pre-placed ESM targets get `MpChangeFormREFR.isDeleted=true` (new reason `scrapped`) — they must also be loaded by the server first (B8: extend to `STAT/SCOL/MSTT/TERM` when inside a workshop build area; lazily, only for objects that have a scrap recipe).
- *Validation:* target inside buildable area of a workshop the actor may edit (S5 permissions); has a scrap recipe; not quest-protected.
- *Gamemode:* `onScrapItem`, `onWorkshopScrap(actorId, workshopId, refId, yields) [blockable]`.
- *Tests (`[Scrap]`):* yields per rarity/perk rank; scalar rounding; legendary rejection; ledger break order determinism; leftover routing.

**(f)** M. Depends: ESPM CMPO/MISC CVPA/COBJ, S5 for workshop scrap.

---
### S5. Workshop / settlements (F22) — major system

**(a) Vanilla mechanics**

*Architecture (Papyrus quest-driven):*
- `WorkshopParentScript` (extends Quest) holds global data: `Workshops[]` (master list, index = workshopID), `WorkshopLocations[]`, the rating table, vendor types, caravan aliases, keywords, AVs, leveled items. Full data exists **only for the current (loaded) workshop**; unloaded workshops keep data as actor values / location keyword data [src: F4SE vanilla/WorkshopParentScript.psc:1-40, 154-165].
- Main flows: `DailyWorkshopUpdate` (every 24 game hours, spread over 12 h across workshops via a custom event), `BuildObjectPUBLIC`/`RemoveObjectPUBLIC` (called from the workshop's placed/destroyed events), `ResetWorkshop` (refresh on arrival), `CreateActor` (recruit; 20 % guard, 20 % brahmin with farmer, 10 % synth if population ≥ 4), `AssignActorToObjectPUBLIC`, `AssignCaravanActorPUBLIC`, `ResolveAttack` [src: F4SE vanilla/WorkshopParentScript.psc:1123-1160, 1242-1337, 2651-2817; constants ~560-600].
- Custom events: `WorkshopInitializeLocation, WorkshopDailyUpdate, WorkshopAddActor, WorkshopActorAssignedToWork, WorkshopActorUnassigned, WorkshopObjectBuilt, WorkshopObjectDestroyed, WorkshopObjectMoved, WorkshopObjectDestructionStageChanged, WorkshopObjectPowerStageChanged, WorkshopPlayerOwnershipChanged, WorkshopEnterMenu, WorkshopObjectRepaired, WorkshopActorCaravanAssign/Unassign` [src: F4SE vanilla/WorkshopParentScript.psc:610-652].
- `WorkshopScript` (on the workbench REFR): `OwnedByPlayer`, `EnableAutomaticPlayerOwnership`, `AllowAttacks(BeforeOwned)`, vendor containers per type, radio scene; events `OnWorkshopMode`, `OnWorkshopObjectPlaced/Moved/Destroyed/Repaired`; `DailyUpdate` (produce/consume/surplus/repair/attract settlers), `CheckForAttack`, `GetMaxWorkshopNPCs = 10 + player Charisma` [src: F4SE vanilla/WorkshopScript.psc:44-125, 351-456, 561-700, 1399-1500].
- `WorkshopObjectScript` (built objects): vendor type/level, `workshopID`, assignment (= actor ownership of the object), power handling (`RequiresPower` ⇔ AV `PowerRequired` > 0, `GeneratesPower` ⇔ `PowerGenerated` > 0, `OnPowerOn/OnPowerOff`), destruction/repair [src: F4SE vanilla/WorkshopObjectScript.psc:186-240, 522-562]. `WorkshopNPCScript` (settlers: worker/guard/scavenger/caravan/synth flags, multi-resource production) [src: F4SE vanilla/WorkshopNPCScript.psc:1-100]. `workshopObjectActorScript` (turrets as actors) [src: F4SE vanilla/workshopObjectActorScript.psc].

*Ratings:* actor values on the workshop REFR, indices 0–44: 0 food, 1 happiness, 2 population, 3 safety, 4 water, 5 power, 6 beds, 7 bonus happiness, 8 unassigned population, 9 radio, 10 current damage, 11 max damage, 12 days since last attack, 13–17 damaged food/water/safety/power/population, 18 happiness modifier, 19 actual food, 20 happiness target, 21 artillery, 22 artillery damage, 23 last attacking faction, 24 robots, 25 vendor income, 26 brahmin, 27–29 missing food/water/beds, 30–33 scavenging (general/building/parts/rare), 34 caravan, 35–42 food types (carrot…tato), 43 synths, 44 missing safety [src: F4SE vanilla/WorkshopParentScript.psc:44-148]. Engine recalculates resource AVs from owned resource objects (`RecalculateResources`, `GetWorkshopResourceObjects(av, option)`) [src: F4SE vanilla/ObjectReference.psc:485-492, 733, 979].

*Formulas (vanilla constants):*
- Happiness: each need (food, water, bed, shelter/defense) worth up to 20 (bed 10 + covered 10); base max 80, store/decor bonuses / population above that; robots fixed 50; ≤ 10 happiness loses ownership after a failed quest [src: F4SE vanilla/WorkshopScript.psc:128-150], [web: wiki:Happiness_(Fallout_4)].
- Storage caps: food 10 + 1/pop, water 5 + 0.25/pop, scavenge 100 + 5/pop, fertilizer 10 [src: F4SE vanilla/WorkshopScript.psc:152-165] (wiki gives `100 + ceil(pop/5)` for junk — script constants win) [web: wiki:Fallout_4_settlements].
- Vendor income: needs pop ≥ 5, ≤ 50 caps/day; settlers: ≤ 5 unassigned before arrivals stop; daily attract chance 0.1 × happiness factor [src: F4SE vanilla/WorkshopScript.psc:167-180].
- Attack chance per daily update = max(0.02, 0.02 + 0.001·(food+water incl. stored) − 0.01·safety − 0.005·pop), none within 7 days of the last, none with 0 population; strength = min(food+water,100) ± 50 %; off-screen resolution: attack roll = min(150, rand(1..100)+strength) vs defense = rand + min(100, safety+pop) [src: F4SE vanilla/WorkshopScript.psc:181-186, 1399-1480], [src: F4SE vanilla/WorkshopParentScript.psc:2697-2817], [web: wiki:Fallout_4_settlements §Attack chance].
- Attacks are story-manager radiant quests (`WorkshopEventAttack` keyword, `SendStoryEventAndWait(loc, strength, workshopRef)`); vassal workshops never attacked [src: F4SE vanilla/WorkshopParentScript.psc:2676-2695].

*Claiming:* activating the workbench when the location is cleared (no live bosses) and `EnableAutomaticPlayerOwnership` sets `OwnedByPlayer`; others need Minutemen quests (*Taking Point* etc.) [src: F4SE vanilla/WorkshopScript.psc:351-390], [web: wiki:Workshop_(Fallout_4)]. AV `WorkshopPlayerOwnership` mirrors ownership for conditions.

*Build mode:* `WorkshopMenu` (hold View near a workbench; leaving the build area for 5 s exits). Place/move/scrap/store objects; stored objects can be re-placed without cost. Multi-object structures move as one [web: wiki:Workshop_(Fallout_4)].

*Budget ("size"):* hidden AVs on the workshop: `WorkshopMaxTriangles`, `WorkshopCurrentTriangles`, `WorkshopMaxDraws`, `WorkshopCurrentDraws` [src: F4SE vanilla/WorkshopParentScript.psc AV group ~449-456]; the size bar fills with placed objects; scrapping/storing refunds (with known leaks via drop-and-scrap) [web: wiki:Fallout_4_settlements §Size], [web: https://github.com/NOBOBYoO/BuildingBudgetExtender].

*Placement rules:* engine `Workshop::PlacementStatusValue {kValidPlacement, kFloating, kIntersecting, kTerrainOnly, kWaterRestriction, kSplineTooLong, kOutsideBuildArea, kRedundantSpline, kAttachedSplineInvalid, kUnsupported, kMustSnap, kRadiusOverlap, kTimer}`; per-item `PlacementItemData{mustSnap, stacksWhenSnapped, allowUnsupportedStacking, anythingIsGround, snapPointQueryRadius, sinkDepth, zOffset, intersectTolerance,…}`; snap points are NIF connect points [src: CLF4 W/Workshop.h].

*Power & wiring:* generators (AV `PowerGenerated`), consumers (`PowerRequired`); wires are `BNDS` (Bendable Spline) refs; workshop REFR stores `Power Grid` (`XWPG` count, `XWPN{node1, node2, line(BNDS ref)}`), spline refs `XBSD`/`XPLK` [src: xEdit wbDefinitionsFO4.pas:11594-11600, 11836-11849, 12280-12292]; runtime `Workshop::ExtraData{currentPowerGrid, powerGrid[], deletedItems[{formID,count}], powerRating, offGridItems}`, `PowerUtils::PowerGrid{adjacencyMap, currentlyPowered[], capacity, load}`, `ExtraPowerLinks{[formID, linkType]}` [src: CLF4 W/Workshop.h, P/PowerUtils.h:31-42, E/ExtraPowerLinks.h]. Power also flows through snapped conduits and pylon radius. Papyrus: `IsPowered`, `HasSharedPowerGrid`, `OnPowerOn(gen)/OnPowerOff` [src: F4SE vanilla/ObjectReference.psc:562, 624, 1071-1076]. F4SE: `AttachWire(ref, spline=0001D971)` (requires the caller linked to a workshop via keyword `00054BA6`), `CreateWire`, `GetConnectPoints`, `TransmitConnectedPower` [src: F4SE modified/ObjectReference.psc:4-64]. **NG note:** these were broken/disabled in F4SE 0.7.0 (1.10.980) due to inlining and fixed in 0.7.2 (1.10.984) [src: F4SE f4se_whatsnew.txt:25-56]; re-verify on each AE build.

*Settlers & beacons:* recruitment radio beacon (needs power, 1 power; settlement recruitment radio station) attracts settlers; pop cap 10 + Charisma (Vault 88 +10); assignment by ownership; beds auto-assigned [web: wiki:Recruitment_radio_beacon], [web: wiki:Fallout_4_settlements §People].

*Supply lines:* Local Leader 1 (CHA 6, `0004D88D`; rank 2 `001D2468` unlocks stores/workbenches); a settler becomes a **provisioner** walking (with brahmin) between two settlements, essential, only "real" when its cells are loaded; the whole network shares Junk/Aid/Misc for building/crafting and surplus food/water; not for Home Plate, CC/Creations settlements, or raider outposts (all outposts are auto-linked to each other) [web: wiki:Supply_line], [web: wiki:Local_Leader], [web: wiki:Fallout_4_settlements §Nuka-World raider outposts].

*Shared inventory:* workbench container (`WorkshopLinkContainer` linked ref, or the workbench itself) shared by all stations in the settlement [src: F4SE vanilla/WorkshopScript.psc:614-620]; shared items are visible only in build/craft menus, not as lootable items elsewhere [web: wiki:Workshop_(Fallout_4)].

*Sites:* base game 30 workshops (incl. Home Plate), Automatron 1 (Mechanist's lair), Far Harbor 4, Vault-Tec Workshop: Vault 88 (4 workbenches), Nuka-World Red Rocket; plus CC settlements/homes [web: wiki:Workshop_(Fallout_4)].

*DLC objects:* Wasteland Workshop — cages capturing creatures/NPCs (tamed with beta-wave emitter), arenas, nixie lights, letters, taxidermy, concrete, decontamination arch; Contraptions — conveyors, ball tracks, **logic gates**, elevators (2–4 floors), manufacturing machines, display cases, weather-change firework shells; Vault-Tec — vault structures, population management system, experiment quests; Nuka-World — raider outposts, vassal/tribute settlements (keywords `WorkshopType02`, `WorkshopType02Vassal`; map marker types 66–68) [web: wiki:Wasteland_Workshop, wiki:Contraptions_Workshop, wiki:Vault-Tec_Workshop], [src: F4SE vanilla/WorkshopParentScript.psc:380-389], [src: xEdit wbDefinitionsFO4.pas:11750-11810]. Conveyors use `ApplyConveyorBelt/ConveyorBeltOn/SetConveyorBeltVelocity` and fans `ApplyFanMotor` [src: F4SE vanilla/ObjectReference.psc:215-227, 262, 877].

**(b) Engine data summary:** workbench REFR (+ `WorkshopScript`), buildable-area primitives (engine `Workshop::IsLocationWithinBuildableArea(workshop, pos)`), workshop container, link keyword `WorkshopItemKeyword` (`00054BA6`) from every built item to its workshop, COBJs with workshop category keywords (`Workshop::WorkshopMenuNode{filterKeyword, recipe, children}`, `WorkshopCanShowRecipe`), resource AVs on base objects (PRPS), BNDS wires, XWPG/XWPN grid, rating AVs above.

**(c) Runtime hooks**
- Menu: `WorkshopMenu` (sinks `Workshop::BuildableAreaEvent{exit}`, `Workshop::PlacementStatusEvent{value}`, `PickRefUpdateEvent`; members `lastBudget`, `inEditMode`, `electricalDevice`) [src: CLF4 W/WorkshopMenu.h]; `Workshop_CaravanMenu` (supply line target), `PipboyWorkshopMenu` [src: CLF4 IDs_RTTI.h].
- Engine functions: `StartWorkshop(ref)`, `RequestExitWorkshop`, `PlaceCurrentReference(ctx)`, `ScrapReference(ctx, ref, rewards*)`, `ToggleEditMode`, `UpdateActiveEdit(ctx, multiselect)`, `SetSelectedEditItem`, `InitializePlacementReference`, `GetCurrentPlacementItemData`, `GetPlacementItem`, `FindNearestValidWorkshop`, `FreeBuild` [src: CLF4 W/Workshop.h].
- Engine events: `Workshop::ItemPlacedEvent{workshop, placedItem}`, `ItemMovedEvent`, `ItemDestroyedEvent`, `WorkshopModeEvent{workshop, start}` (register functions provided) [src: CLF4 W/Workshop.h].
- Papyrus: `StartWorkshop`, `StoreInWorkshop(base,count)`, `GetWorkshopOwnedObjects(actor)`, `GetWorkshopResourceObjects`, `RecalculateResources`, `CanProduceForWorkshop`, `IsWithinBuildableArea`, `OpenWorkshopSettlementMenu(Ex)`, events `OnWorkshopMode(bool)`, `OnWorkshopObjectPlaced/Moved/Destroyed/Grabbed/Repaired(ref)`, `OnWorkshopNPCTransfer(Location, Keyword)` [src: F4SE vanilla/ObjectReference.psc:253, 485-492, 639, 672-680, 949-955, 1136-1161].

**(d) SP assumptions that break**
1. One player owns every settlement; "current workshop" is a single global.
2. Workshop scripts compute everything with the player's Charisma/perks and only for loaded cells (the wiki documents corrupted stats when a settlement is partially loaded [web: wiki:Fallout_4_settlements §Bugs]).
3. Placement, budget, component consumption and power simulation are local; object refs are created in the save.
4. Settlers and provisioners are Papyrus-managed persistent actors; daily update runs on Papyrus game-time timers that assume a local clock that can jump (sleep/wait/fast travel).
5. Vanilla scripts are blocked in SkyMP-style clients, so running vanilla Workshop scripts is not an option on the client; on the server they would need the full FO4 Papyrus VM plus hundreds of natives.

**(e) FalloutMP design**

*Decision:* do **not** run `WorkshopParentScript`. Implement a native server **WorkshopService** that reproduces the data model and formulas above (constants loaded from the scripts' values into a config table so servers can tune them). Vanilla workshop Papyrus events are re-fired on the server for gamemode/server scripts.

*Authority:* server-authoritative for ownership, objects, budget, inventory, ratings, settlers, attacks, power state; client-authoritative-with-validation only for exact placement transforms (the server cannot run Havok placement).

*Server data model* (new aggregate, one per workbench; persisted as its own record type, not as thousands of `MpChangeFormREFR` files — the `file` driver writes one JSON per change form [src: docs/docs_database_drivers.md]):
```cpp
struct WorkshopObject {            // static or interactive placed object
  uint32_t refId;                  // 0xFF… dynamic id (server form) or ESM id (pre-placed)
  uint32_t baseId, recipeId;       // COBJ used (for scrap refund)
  NiPoint3 pos, rot; float scale = 1;
  uint16_t flags;                  // stored, destroyed, powered, isPrePlaced, interactive, scrapped
  uint32_t assignedActor = 0;      // settler ownership
  float resourceDamage = 0;
};
struct WorkshopWire { uint32_t wireRefId, a, b; uint32_t splineBaseId; };
struct MpWorkshop {
  FormDesc workbench, location, container;
  OwnerRef owner;                  // {type: none|profile|group|faction, id}
  std::vector<Permission> acl;     // profileId/groupId → {build, scrap, container, assign, admin}
  std::array<float, 45> ratings;   // indices as WorkshopParentScript
  Budget budget;                   // maxDraws/curDraws/maxTris/curTris (server cost table)
  std::vector<WorkshopObject> objects; std::map<uint32_t,uint32_t> stored; // baseId→count
  std::vector<WorkshopWire> wires; std::vector<uint32_t> settlers;
  std::vector<SupplyLink> links;   // {otherWorkshop, provisionerId}
  AttackState attack; uint64_t lastDailyUpdateGameTime; uint32_t version;
};
```
Interactive objects (containers, beds, crafting stations, doors, turrets, generators with switches, vendor stalls, radios) **also** get a real `MpObjectReference` (activation, occupancy, persistence of inventory). Pure decor stays inside `MpWorkshop.objects` only and is streamed as a block — this is what keeps 1–3k objects per settlement affordable.

*Ownership & permissions (server config `workshops`):* claim rule = `firstClaim | gamemode | adminOnly`; claim requires the workshop location to have no living Boss-LCRT NPCs (server knows NPC state) and `maxWorkshopsPerPlayer`; groups (party/guild via gamemode); abandonment after `abandonDays` offline; `onWorkshopClaim [blockable]` lets gamemodes implement PvP capture. Workshop "owner Charisma" for population cap = owner's Charisma (or config).

*Protocol* (all reliable unless noted):
| Message | Dir | Fields | Notes |
|---|---|---|---|
| `WorkshopEnter` | C→S | `workshopRefId, enter` | sent on `WorkshopModeEvent`; server may refuse → client `RequestExitWorkshop` |
| `WorkshopState` | S→C | `workshopRefId, allowed, reason?, perms, budget, ratings, containerInventory` | owner/members + current builder; inventory lets the vanilla menu show affordability |
| `WorkshopPlace` | C→S | `workshopRefId, recipeId, baseId, pos, rot, snap?{refId, connectPoint}, nonce` | from `ItemPlacedEvent`; local ref deleted on send ("ghost" until confirmed) |
| `WorkshopMove` | C→S | `workshopRefId, items[{refId, pos, rot}]` | multi-select moves |
| `WorkshopScrap` / `WorkshopStore` | C→S | `workshopRefId, refId` | pre-placed scrappables too (S4) |
| `WorkshopWire` / `WorkshopUnwire` | C→S | `workshopRefId, a, b, splineBaseId` / `wireRefId` | validated length ≤ engine max (`kSplineTooLong`) |
| `WorkshopAssign` | C→S | `workshopRefId, actorId, objectRefId` | beds/jobs/stores |
| `WorkshopSupplyLine` | C→S | `fromWorkshop, actorId, toWorkshop` | needs Local Leader 1 |
| `WorkshopRepair` | C→S | `refId` | consumes components |
| `WorkshopResult` | S→C | `nonce, ok, refId?, error` | error = budget/permission/area/components/limit |
| `WorkshopSnapshot` | S→C | `workshopRefId, version, chunk, chunkCount, objects[{refId, baseId, pos, rot, flags}], wires[]` | on stream-in; ~32 B/object binary, 64–128 objects per packet |
| `WorkshopDelta` | S→C | `workshopRefId, version, added[], removed[], moved[], flags[]` | to all clients that hold the snapshot |
| `WorkshopRatings` | S→C | `workshopRefId, ratings` (subset for Pip-Boy) | owner/members, on change / daily |

*Client:* keep the vanilla `WorkshopMenu` for UX. Capture `ItemPlacedEvent/ItemMovedEvent/ItemDestroyedEvent` (or hook `PlaceCurrentReference`/`ScrapReference`), forward requests, and reconcile: confirmed objects are spawned/adopted by server `refId`; rejected ones are deleted and inventory restored from `SetInventory`. Block the engine's own component deduction effects by letting the authoritative inventories overwrite. For stream-in use a native `TESModPlatform.PlaceWorkshopObjects(batch)` with a per-frame budget (e.g., 50 objects/frame), keyframed motion type, linked to the local workbench with `WorkshopItemKeyword` so engine features (snapping, power) work; wires via a platform `CreateWire(a,b,spline)` native (do not depend on F4SE latent functions).

*Validation:* actor inside buildable area and within reach of `pos` (≤ 4096 u); permission; recipe exists with workshop category keyword and CTDA (perks such as Local Leader 2); components via `ComponentLedger` (S4); budget (server cost table per base: draw/triangle estimates precomputed from NIFs by an offline tool, or a simple per-object weight with `maxObjectsPerWorkshop` fallback); transforms finite, inside area, snap target exists and is within `snapPointQueryRadius` + bounds; rate limit (e.g., 10 places/s); unique-object limits (e.g., one fast-travel target, AV `WorkshopFastTravel`).

*Power simulation (server):* graph = generators, consumers, wires, conduit snaps, pylon radius; per connected component compute capacity vs load (vanilla behaviour: insufficient power leaves some consumers unpowered); set `powered` flag; broadcast changes via `WorkshopDelta.flags`. Clients still compute engine power locally from the same wires for visuals; server state is authoritative for gameplay (turrets fire, resources produce, doors open). Contraptions logic gates/pressure plates are evaluated server-side on the same graph (signal edges); physics conveyors/ball tracks are cosmetic (owner-client simulated) and manufacturing output is server timers.

*Settlers:* server-owned `MpActor`s (`createActor`) with `SettlerState{workshopId, job, bed, provisioner, wounded}`; hosted by a nearby client (SkyMP host model) only when a player is in range; otherwise abstract. Recruitment, production, consumption, happiness and surplus run in the server **daily update** on the authoritative clock (S17), reproducing `WorkshopScript.DailyUpdate` math — no dependency on loaded cells, which removes the vanilla partial-load bug. Provisioners are abstract route progress (polyline between workshop markers) and become a hosted NPC only when a player is near the route.

*Attacks:* server rolls per daily update with the vanilla formula; `onWorkshopAttack(workshopId, factionId, strength) [blockable]`; if an owner/member is online within N cells → spawn attackers (faction LVLN, scaled by encounter zone) as hosted NPCs at the settlement edge and run a timed event; else resolve off-screen (vanilla roll), apply damage to random resource objects and stored food/water.

*Shared inventory & network:* workshop container = `MpObjectReference` (occupancy model, B3); the `ComponentLedger` reads every container in the supply network (graph closure) when building/crafting.

*Audience:* snapshot/delta to every client whose grid neighbourhood touches the build area (settlement as one streaming unit); `WorkshopState/Ratings` to owner/members; requests only from the builder.

*Scale budget [inference]:* 2,000 objects × 32 B ≈ 64 KB per settlement snapshot (≈ 0.5–1 s at 100 KB/s); deltas tiny. Server memory ≈ 100 B/object. Interactive objects ≤ ~200 per settlement keep `CreateActor` traffic bounded.

*Gamemode events:* `onWorkshopClaim, onWorkshopEnter, onWorkshopPlace, onWorkshopMove, onWorkshopScrap, onWorkshopStore, onWorkshopWire, onWorkshopAssign, onSupplyLine, onWorkshopAttack, onWorkshopDailyUpdate (non-blockable)`. Properties: `mp.get(workbenchId, "workshop")` returns the aggregate; `mp.set` for owner/acl/ratings overrides.

*Server Papyrus events:* `OnWorkshopMode`, `OnWorkshopObjectPlaced/Moved/Destroyed/Grabbed/Repaired` on workbench and object, `OnPowerOn/OnPowerOff`, custom events with the vanilla names on a FalloutMP `WorkshopParent` stand-in quest so ported mod scripts can subscribe. Natives: `GetWorkshopOwnedObjects`, `GetWorkshopResourceObjects`, `RecalculateResources`, `WaitForWorkshopResourceRecalc`, `StoreInWorkshop`, `IsWithinBuildableArea`, `IsPowered`, `HasSharedPowerGrid`, `GetValue` on rating AVs.

*Tests:* `[Workshop]` place/move/scrap/store happy paths and every rejection; budget accounting incl. refunds; permission matrix; `[Power]` graphs (series, insufficient capacity, pylon radius, logic gates); `[WorkshopDaily]` golden tests reproducing vanilla formulas (happiness, storage caps, attack chance with fixed RNG seed); snapshot chunking/versioning; persistence round-trip of a 3k-object workshop under 50 ms; `L-int` two bots: one builds, the other receives deltas.

**(f)** XL (core build + persistence + streaming: L; power: M; settlers/daily update: L; attacks/supply lines: M; DLC objects: M). Depends: S1, S4, S17 clock, F13 hosting, F19 perks, ESPM COBJ/BNDS/STAT/SCOL parsing, PLAT natives (batched placement, wires, workshop event sinks), B8 extension.

---
### S6. Vendors, barter & caps (F23)

**(a) Vanilla**
- Merchants via dialogue → `BarterMenu`; vendor inventory lives in a **merchant container** (faction `VENC`), with vendor hours and buy/sell keyword list; caps also in that container; inventory and caps re-roll on refresh; Speech bobblehead +100 caps; Cap Collector 3 lets the player invest 500 caps (shared per settlement shop type) [web: wiki:Fallout_4_merchants], [web: wiki:Cap_Collector_(Fallout_4)].
- Prices: buy modifier = 3.50 − 0.15 × Charisma, sell modifier = 1 / buy modifier, times multiplicative perk/bobblehead/friend-discount modifiers; final buy ≥ 1.2 × value, sell ≤ 0.8 × value [web: wiki:Charisma_(Fallout_4) §Effect on Barter]. Cap Collector 1 = 10 %, 2 = 20 % more (multiplicative: 28 % buy discount / 32 % sell bonus) [web: wiki:Cap_Collector_(Fallout_4)] (ranks `01D2456, 0D75E2, 01D2457`).
- Settlement stores (Local Leader 2; tier 3 needs Cap Collector 2) assign a settler as vendor using `WorkshopVendorContainers` per vendor type/level (Misc, Armor, Weapons, Bar, Clinic, Clothing, Chems) [src: F4SE vanilla/WorkshopParentScript.psc:191-210].
- Vendor refresh: `iDaysToRespawnVendor` ≈ 2 days [web: https://steamcommunity.com/app/377160/discussions/0/3436829654709317843/] (low confidence); `FACTION_VENDOR_DATA.lastDayReset` [src: CLF4 F/FACTION_VENDOR_DATA.h].
- Caps (`Caps001` 0xF) cannot be dropped or stored in containers [web: wiki:Bottlecap_(Fallout_4)].

**(b) Data:** FACT `DATA` flag `Vendor` (0x4000), `VEND` buy/sell FLST, `VENC` merchant container REFR, `VENV{startHour, endHour, radius, buysStolen, buySellEverythingNotInList, buysNonStolen}`, `PLVD` location, conditions [src: xEdit wbDefinitionsFO4.pas:6785-6846]; runtime `FACTION_VENDOR_DATA{vendorValues, vendorLocation, vendorConditions, vendorSellBuyList, merchantContainer, lastDayReset}` [src: CLF4 F/FACTION_VENDOR_DATA.h, F/FACTION_VENDOR_DATA_VALUES.h].

**(c) Hooks:** `BarterMenu` (`ItemBarterData{stackQuantityMap, capsOwedByPlayer}`, `barteredItems`, `vendorChestRef`, `vendorActor`, tentative inventories, `confirmingTrade`; `CompleteTrade()`, `ClearTradingData()`, `GetCapsOwedByPlayer()`, virtual `ConfirmInvestment`) [src: CLF4 B/BarterMenu.h]; Papyrus `Actor.ShowBarterMenu()`, `ObjectReference.OnSell(Actor)`, `SellItem(...)` (pure Papyrus: pays `Caps001`) [src: F4SE vanilla/Actor.psc:760], [src: F4SE vanilla/ObjectReference.psc:801-843, 1091].

**(d) Breaks:** prices and trade are computed and executed locally; vendor container is a save-local REFR in a holding cell; refresh is driven by the local clock; one shared vendor stock competes between players.

**(e) Design**
- *Authority:* server-authoritative trade.
- *State:* merchant container = `MpObjectReference` (the `VENC` ref, loaded even though it is in a holding cell — add to B8 loading by explicit reference). `VendorState` per vendor faction: `{lastRestockGameDay, investedCaps, hoursOverride?}` in the container's change form (`dynamicFields` or new field). Option `vendors.stockMode = shared | perPlayer` (per-player = private inventory keyed by profileId with its own restock clock).
- *Flow:* dialogue barter topic (or Activate on vendor) → client `BarterOpen{vendorActorId}` (C→S) → server checks vendor alive, not hostile to this player (S15), within hours (S17 clock), distance ≤ 512 u → **`OpenBarter`** (S→C) `{vendorActorId, containerRefId, inventory, caps, buyMult, sellMult}` → client opens `BarterMenu` (via `ShowBarterMenu` SpSnippet) with the server inventory in the vendor chest. On confirm (hook `CompleteTrade`): **`BarterRequest`** (C→S) `{vendorActorId, buy[{item: ItemKey, count}], sell[{item, count}], expectedCapsDelta, invVersion, nonce}` → server prices each line with `ObjectModEvaluator` value × modifiers (formula above, constants in `GameProfile`, gamemode override), checks vendor list keywords/stolen flag, both sides' items and caps, then moves items and caps atomically → **`BarterResult{ok, error?, capsDelta}`** + `SetInventory` updates.
- Restock: `CellResetService`/timer: when `gameDay − lastRestockGameDay ≥ vendorRespawnDays` and container not occupied → `RelootContainer()` (B6, after fixing B2) — caps re-rolled from the container's leveled list.
- Player↔player trade (caps cannot be dropped): gamemode-level `TradeOffer/TradeAccept` custom packets, or built-in `PlayerTrade{targetId, offer[], caps}` with two-phase confirm (T2).
- *Validation:* negative/overflow counts, price mismatch beyond rounding (reject, do not "fix"), invest only with Cap Collector 3 and once per shop type.
- *Gamemode:* `onBarter(actorId, vendorId, buy, sell, capsDelta) [blockable]`, `onVendorRestock(vendorId)`. *Papyrus:* fire `OnSell` on sold refs; implement `ShowBarterMenu` (server → client snippet).
- *Tests (`[Vendor]`):* price table vs wiki values at CHA 1/5/10/15 with/without Cap Collector; clamps 1.2/0.8; atomicity on failure; restock timing with fake clock; shared vs per-player stock.

**(f)** M. Depends: S1 (item value), S15 (hostility), S17 (clock), F19 (perks), dialogue hook (S14).

---

### S7. Containers & looting (F06)

**(a) Vanilla:** `ContainerMenu` for containers, corpses, teammates, pickpocketing; the HUD **quick-loot** panel shows a container's items while aiming at it (take one with Activate, open full menu with Transfer) [src: CLF4 Q/QuickContainerStateData.h]. Owned items show "Steal"; detected theft angers owner/faction (S15). Containers with the `Respawns` flag refill on cell reset (S18); NPC corpses are lootable; caps cannot be stored.

**(b) Data:** CONT `DATA.flags {Allow Sounds When Animation, Respawns(0x02), Show Owner}`, `CNTO` items, `NTRM` native terminal, `ONAM` filter list [src: xEdit wbDefinitionsFO4.pas:6176-6214]; REFR ownership (`XOWN`/`kOwnership`), cell ownership; NPC_ `ACBS` flag `No Loot`(12), death item `INAM`.

**(c) Hooks:** `ContainerMenu` (`TakeAllItems()`, `valueStolenFromContainer`, `pickpocketInfo_mc`, `UpdateItemPickpocketInfo`, `plantedExplosiveWeapon`) on `ContainerMenuBase` (`DoItemTransfer(itemIndex, count, fromContainer)`, `containerRef`, `menuMode`) [src: CLF4 C/ContainerMenu.h, C/ContainerMenuBase.h]; `QuickContainerStateEvent` → `QuickContainerStateData{itemData[5], containerRef, inventoryRef, mode: kLoot|kTeammate|kPowerArmor|kTurret|kWorkshop|kCrafting|kStealing|kStealingPowerArmor, isLocked, containerActivated,…}` [src: CLF4 Q/QuickContainerMode.h, Q/QuickContainerStateData.h]; `TESContainerChangedEvent`, `BGSInventoryListEvent`; Papyrus `OnOpen/OnClose`, `SendStealAlarm`, `Actor.WouldBeStealing(ref)` [src: F4SE vanilla/Actor.psc:817].

**(d) Breaks:** quick-loot needs the contents **before** activation (SkyMP only sends inventory after occupancy, B3); several players may look at one container at once; corpses unsupported (B13); dead pre-placed actors not loaded (B8); reloot ignores `Respawns` (B6).

**(e) Design**
- Keep SkyMP occupancy for the full `ContainerMenu` (single writer) but add:
  - **`ContainerPeek{targetRefId}`** (C→S, on crosshair change, ≤ 4/s) → **`ContainerContents{targetRefId, version, entries}`** (S→C) with a peek subscription that ends when the crosshair leaves (or 10 s). Client fills the quick-loot HUD from it.
  - **Quick take:** `TakeItem` gets optional `quick: true`, `version` — server accepts without prior occupancy if the actor is within reach (≤ 200 u + slack) and no other occupant holds the container; stale `version` → reject with fresh contents.
  - **Take all:** `TakeAll{targetRefId, version}`.
  - Container contents changes are pushed to occupant and all peekers (`ContainerContents` deltas).
- **Corpses:** `ProcessActivateNormal` gets an `NPC_`/dead-actor branch identical to CONT (occupancy, inventory). Load "starts dead" ACHRs as dead `MpActor`s (B8) with their inventory, no respawn unless configured. `No Loot` flag respected.
- **Ownership & stealing:** server knows owner (REFR/cell/ESM + runtime `SetActorOwner/SetFactionOwner`). Taking an owned item marks the entry `stolenFrom = ownerFormId` (new optional field, part of `ItemKey`? → no; stored as extra but **excluded** from stacking to avoid splits) and fires `onSteal(actorId, ownerId, item) [blockable]`. Detection is decided by the host of a witnessing NPC: host sends `CrimeWitnessed{criminalId, witnessId, crime: steal|trespass|assault|murder|pickpocket}`; server applies S15 hostility.
- **Loot modes** (server config `loot.mode`): `shared` (SkyMP default) | `instanced` (per-profile inventory snapshot per container with own reloot time; CONT with `Respawns` only) | `hybrid` (instanced for containers, shared for world items/corpses).
- **Reach check (fix B4):** all Activate/Put/Take validate activator distance ≤ `activationReach` (default 300 u, configurable) using the server position.
- *Gamemode:* existing `onActivate/onPutItem/onTakeItem` (with item data), new `onContainerPeek` (non-blockable, metrics), `onSteal`, `onLootCorpse [blockable]`.
- *Papyrus:* fire `OnOpen/OnClose`, `OnItemAdded/OnItemRemoved`.
- *Tests:* peek subscription lifecycle; quick-take version conflicts with two actors; corpse loot occupancy; instanced loot isolation; reach rejection; `Respawns` flag honoured.

**(f)** M. Depends: S1, S15, S18, B4/B8 fixes, PLAT quick-loot HUD injection.

---

### S8. Locks & lockpicking (F24)

**(a) Vanilla:** locks on doors, safes, containers; picked with **bobby pins** in `LockpickingMenu`; difficulties Novice/Advanced/Expert/Master need **Locksmith** 0/1/2/3 (PER 4; ranks `523FF, 52400, 52401`; rank 4 `1D246A` = pins never break); XP 6/12/17/22 [web: wiki:Lock_(Fallout_4)], [web: wiki:Locksmith_(Fallout_4)]. Some locks need a key, a terminal, or are barred/chained (cannot be picked). Automatron lockpick module lets robots open locks. Picking an owned lock while seen is a crime.

**(b) Data:** REFR `XLOC{level u8, key KEYM, flags 'Leveled Lock'}` with level values `0 None, 1/25 Novice, 50 Advanced, 75 Expert, 100 Master, 251 Barred, 252 Chained, 253 Requires Terminal, 254 Inaccessible, 255 Requires Key` [src: xEdit wbDefinitionsFO4.pas:4500-4520, 11651-11672]; runtime `ExtraLock → REFR_LOCK{baseLevel, key, flags(kLocked 0x1, kLeveled 0x4), numTries}`, `GetLockLevel(owner)`, `SetLocked`, `IsBroken`, enum `LOCK_LEVEL {kUnlocked=-1, kEasy, kAverage, kHard, kVeryHard, kRequiresKey, kInaccessible, kTerminal, kBarred, kChained}` [src: CLF4 R/REFR_LOCK.h, L/LOCK_LEVEL.h, E/ExtraLock.h]; KEYM records (`TESKey : TESObjectMISC`) [src: CLF4 T/TESKey.h]; BobbyPin `0x0000000A`.

**(c) Hooks:** `LockpickingMenu` (`OpenLockpickingMenu(ref)`, `DamageLockpick()`, `sweetSpotCenter/Length`, `picksBroken`, `crimeDetected`) [src: CLF4 L/LockpickingMenu.h]; events `LocksPicked::Event` (no payload) and story `BGSPickLockEvent{actor, lockObject, isCrime}` [src: CLF4 L/LocksPicked.h, B/BGSPickLockEvent.h]; Papyrus `Lock(bool, abAsOwner)`, `Unlock`, `IsLocked`, `GetLockLevel`, `SetLockLevel`, `IsLockBroken`, `GetKey`, `AddKeyIfNeeded`, event `OnLockStateChanged` [src: F4SE vanilla/ObjectReference.psc:4-6, 159-167, 419, 436, 612-615, 646, 896, 1055].

**(d) Breaks:** SkyMP removes all locks client-side (B17); lock state and pin usage are local; minigame result cannot be verified.

**(e) Design**
- *State:* `LockState{level, locked, keyId, broken}` (new optional `MpChangeFormREFR.lock`, default from ESM XLOC); synced to neighbours as built-in property `lock` (CreateActor additional props + `UpdateProperty`). Client applies with `Lock()/SetLockLevel()` instead of unlocking (replace `ObjectReferenceEx.dealWithRef` lock removal).
- *Flow:* Activate on locked ref → server: has key → unlock + open (fires `OnLockStateChanged`); level 251–255 → refuse (message); else server replies with permission to pick (`LockpickAllowed{refId}` or reuse `OpenContainer` semantics) → client opens vanilla `LockpickingMenu` → on success/fail send **`LockpickResult{targetRefId, success, pinsBroken, elapsedMs}`**.
- *Validation:* Locksmith rank ≥ required; bobby pins ≥ pinsBroken (+1 if failed with 0 left), removed unless Locksmith 4; elapsedMs ≥ `minPickMs` (e.g., 800); rate limit; distance; lock still locked. Server awards XP (F19), applies crime if owned and a host reports a witness (S7/S15).
- *Relock:* lock restored only by cell reset (S18) — **not** by the DOOR 3 s reloot (B6 must not touch lock state).
- *Optional (T2):* server-authoritative sweet spot is impractical (continuous input, latency) — keep minigame local.
- *Gamemode:* `onLockpick(actorId, refId, success) [blockable]`, `onUnlock(actorId, refId, method: key|pick|terminal|script)`. *Papyrus:* `OnLockStateChanged`, natives above server-side.
- *Tests (`[Lock]`):* perk gate per level; pins accounting; key unlock; barred/chained refusal; persistence; reset restores.

**(f)** M. Depends: F19 (perks/XP), S18 reset, PLAT menu hooks.

---

### S9. Terminals & hacking (F24)

**(a) Vanilla:** terminals show header/welcome text and menus; actions unlock doors/safes, toggle turrets/spotlights/protectrons, read entries, run holotapes; locked terminals require **hacking**: pick the password from a word list, 4 attempts then 10 s lockout; brackets remove duds or reset tries; Intelligence sets word count (20 words at INT 1 … 6 at INT 10); tiers Novice…Master need **Hacker** 0/1/2/3 (INT 4; `00052403, 00052404, 00052405`; rank 4 `001D245D` = no lockout) [web: wiki:Hacking_(Fallout_4)], [web: wiki:Hacker_(Fallout_4)]. Robotics Expert lets the player hack robots.

**(b) Data:** `TERM{NAM0 header, WNAM welcome, FNAM flags ('Is a Radio'…), CNTO holotapes, BTXT body text + conditions, menu items {ITXT, RNAM response, ANAM type (4 submenu, 5 return to top, 6 force redraw, 8 display text, 16 display image), ITID id, UNAM text, VNAM image, TNAM submenu TERM, CTDA}, VMAD with PERK-style fragments}` [src: xEdit wbDefinitionsFO4.pas:12671-12760]; runtime `BGSTerminal : TESFurniture` (`MenuItem{itemText, responseText, selectionResult(subMenu|text|image|holotape), conditions, id, flags}`, `GetHackDifficultyLockLevel(ref)`, `IsTerminalRefInUse`, `Show(ref)`) [src: CLF4 B/BGSTerminal.h]; terminal lock = REFR `XLOC` on the terminal; containers can embed a native terminal (`NTRM`).

**(c) Hooks:** `TerminalMenu` (`Mode {kInit, kHack, kLogin, kList, kText, kImage, kHolotape, kWaitingForPapyrus}`, `menuItemList`, `history`, `terminalRunResultsCallback`), `TerminalMenuButtons`, `TerminalHolotapeMenu` [src: CLF4 T/TerminalMenu.h]; events `TerminalHacked::Event{terminal}`, story `BGSHackTerminal{terminal, success}` [src: CLF4 T/TerminalHacked.h, B/BGSHackTerminal.h]; Papyrus `Terminal.OnMenuItemRun(int auiMenuItemID, ObjectReference akTerminalRef)`, `Terminal.ShowOnPipboy()` [src: F4SE vanilla/Terminal.psc:1-6].

**(d) Breaks:** B8 skips `TERM` refs entirely; fragments run client-side; hacked state, lockout, and side-effects (unlocked safe, disabled turrets) are local.

**(e) Design**
- *State:* `TerminalState{hacked (global), lockoutUntil[profileId], perPlayerRead?: set, itemFlags}` on the terminal's change form; load TERM refs (B8).
- *Flow:* Activate → server checks lock (hack tier vs Hacker rank; lockout) → `TerminalOpen{refId, hacked, lockoutUntil}` → client shows vanilla `TerminalMenu`. Hack: **`TerminalHackResult{refId, success, attempts, elapsedMs}`** (plausibility: rank, elapsed ≥ 2 s, attempts ≤ 4 unless rank 4, lockout respected); server sets `hacked`, XP. Menu item: capture `OnMenuItemRun` (client registers for the event) → **`TerminalMenuItem{refId, menuItemId, terminalFormId (submenu)}`** → server evaluates item CTDA and runs the **TERM fragment on the server Papyrus VM** (FO4 PEX, F-PVM) or, before that exists, a native map of common patterns (linked-ref `Lock(false)`, turret faction swap, enable/disable). World changes propagate through normal channels (lock, isDisabled, factions).
- *Optional (T2) server-authoritative hacking:* server generates the word list/password, client UI renders it (CEF overlay or Scaleform injection), each guess round-trips → cheat-proof.
- *Gamemode:* `onTerminalHack [blockable]`, `onTerminalMenuItem(actorId, refId, itemId) [blockable]`. *Papyrus:* `OnMenuItemRun` on the TERM form script, `OnActivate`.
- *Tests (`[Terminal]`):* tier gating; lockout timing (fake clock); fragment dispatch stub; persistence of hacked state; submenu ids.

**(f)** M (L if fragments run on the server VM). Depends: F-PVM (FO4 PEX + fragments), S8, F19, B8.

---

### S10. Holotapes & notes (F24/F28)

**(a) Vanilla:** holotapes play in the Pip-Boy (Misc tab) or terminals: audio logs, scene-driven tapes, terminal-entry tapes, and SWF **programs** — games *Atomic Command, Grognak & the Ruby Ruins, Pipfall, Red Menace, Zeta Invaders* and utility programs (turret/protectron/spotlight override, P.A.M. decryption) [web: wiki:Fallout_4_holotapes]. In power armor the HUD plays them.

**(b) Data:** `NOTE{DNAM type, DATA value/weight, SNAM sound|scene|terminal, PNAM program file}` — xEdit names types `Sound, Voice, Program, Terminal` [src: xEdit wbDefinitionsFO4.pas:12470-12498]; CommonLibF4 `NOTE_TYPE {kVoice, kScene, kProgram, kTerminal}`, `programFile`, `hasBeenRead` [src: CLF4 B/BGSNote.h] (naming differs; trust CLF4 accessors). Papyrus type `Holotape` (no natives) [src: F4SE vanilla/Holotape.psc].

**(c) Hooks:** menus `HolotapeMenu`, `PipboyHolotapeMenu`, `TerminalHolotapeMenu` [src: CLF4 IDs_RTTI.h]; Papyrus `OnHolotapePlay(ObjectReference aTerminalRef)`, `OnHolotapeChatter(string, float)` (program ↔ script) [src: F4SE vanilla/ObjectReference.psc:1032-1038].

**(d) Breaks:** read/listened flags per save; scene holotapes may start quest scenes (blocked vanilla scripts).

**(e) Design:** playback stays **local** (cosmetic, L2). Holotapes/notes are normal inventory items (S1). Per-player read set `PlayerProfile.readNotes` (sent on login, `NoteRead{noteId}` C→S) so "unread" markers survive machines. Holotapes that trigger gameplay (`OnHolotapePlay` scripts, override programs) send `HolotapePlay{noteId, terminalRefId?}` → server runs the attached script/effect (gamemode-blockable `onHolotapePlay`). Optional leaderboard: `OnHolotapeChatter` scores → `HolotapeChatter{noteId, text, number}` → gamemode. *Tests:* NoteRead persistence; HolotapePlay dispatch.

**(f)** S. Depends: S1, F-PVM for scripted tapes.

---
### S11. Pip-Boy (F28)

**(a) Vanilla:** tabs STAT (status, SPECIAL, perks), INV (7 categories), DATA (Quests, Workshops, Stats with General/Quest/Combat/Crafting/Crime), MAP (world/local, supply lines), RADIO; **Pip-Boy light** by holding the Pip-Boy button (colour = HUD colour); adapter plug for vault doors/power armor/robots [web: wiki:Pip-Boy_3000_Mark_IV]. The official Pip-Boy companion app mirrors this data over the LAN [web: https://www.phonearena.com/news/Fallout-4-Pip-Boy-companion-app-is-now-available-for-iOS-and-Android-devices_id75476]. "Quick boy" is not a vanilla term [inference]; here it is read as fast access paths: Favorites quick-menu (S1), hold-for-light, and the companion app.

**(b)/(c) Engine:** `PipboyMenu`; `PipboyDataManager` singleton with `statsData, specialData, perksData, inventoryData, questData, workshopData, logData, mapData, radioData, playerInfoData, statusData`, `throttleManager` [src: CLF4 P/PipboyDataManager.h]; `PIPBOY_PAGES {kStat, kInv, kData, kMap, kRadio}` [src: CLF4 P/PIPBOY_PAGES.h]; `PipboyManager` (`InitPipboy`, `LowerPipboy(reason)`, `ClosedownPipboy`, `AddMenuToPipboy`) [src: CLF4 P/PipboyManager.h]; `PipboyWorkshopData` sinks `ActorValueChangedEvent` + `WorkshopModeEvent` [src: CLF4 P/PipboyWorkshopData.h]; `PipboyMapData` sinks `TravelMarkerStateChange`, `LocationMarkerArrayUpdate`, `CustomMarkerUpdate`, `TESLocationClearedEvent` [src: CLF4 P/PipboyMapData.h]; `PipboyLightEvent : BSTValueEvent<bool>` [src: CLF4 P/PipboyLightEvent.h]; `IsPipboyActiveEvent`.

**(d) Breaks:** opening the Pip-Boy pauses the local game (menu mode) while the world goes on (same as SkyMP inventory menus) [inference]; workshop tab only knows loaded settlements; stats are local counters.

**(e) Design:** leave the Pip-Boy **vanilla and local**; make its *sources* authoritative: inventory (S1), quests (S14), workshops tab fed by `WorkshopRatings` written into the workshop REFR AVs locally (S5), map from discovered markers (S13), stats from the server (S20), radio local (S12). **Pip-Boy light** is synced as a movement/state flag `pipboyLight` (+ colour once per session) — remote clients attach a LIGH to the remote actor's Pip-Boy/wrist node (`PlaceAtNode`/`AttachTo`) [inference]. Companion app: unchanged (local). No new authority. *Tests:* `L-ts` flag serialization.

**(f)** S (light) + M (feeding workshop/map/stat data). Depends: S5, S13, S14, S20, F01 movement flags.

---

### S12. Radio (F28)

**(a) Vanilla:** stations heard on the Pip-Boy and in-world radios: Diamond City Radio (DJ Travis, lines change after *Confidence Man*; 42 tracks), Classical Radio (stops after *The Nuclear Option*), Radio Freedom (after *Taking Independence* while the Castle transmitter is powered), relay-tower signals, Silver Shroud, Trinity Tower, recruitment beacon, DLC stations [web: wiki:Fallout_4_radio_stations], [web: wiki:Diamond_City_Radio]. Stations are quest scenes playing on transmitter refs with range.

**(b)/(c):** TERM/ACTI "Is a Radio" (`Activator.IsRadio()`); Papyrus `MakeRadioReceiver(freq, vol, OutputModel, active, noStatic)`, `MakeTransmitterRepeater`, `SetRadioOn/Frequency/Volume`, `IsRadioOn`, `GetRadioFrequency`, `GetTransmitterDistance`, `OnPipboyRadioDetection(bool)`; `Game.GetPlayerRadioFrequency/SetPlayerRadioFrequency/IsPlayerRadioOn/TurnPlayerRadioOn/IsPlayerInRadioRange/IsPlayerListening` [src: F4SE vanilla/ObjectReference.psc:468-471, 504, 630, 650-653, 922-928, 1063], [src: F4SE vanilla/Game.psc:173, 253-260, 322, 374], [src: F4SE vanilla/Activator.psc]; `PipboyRadioData{radioStations}` sinks `RadioManager::PipboyFrequencyDetectionEvent/PipboyRadioTuningEvent` [src: CLF4 P/PipboyRadioData.h]; extras `ExtraRadioData`, `ExtraRadioReceiver`, `ExtraRadioRepeater`.

**(d) Breaks:** station availability depends on vanilla quest states (blocked in MP); playback position is per client.

**(e) Design:** **local playback** (L2 cosmetic). Server config decides which station quests are allowed to run on the client (whitelist of radio quests so blocking vanilla scripts does not silence them; quest-state-gated stations like Radio Freedom become server flags pushed as globals). In-world radios (incl. settlement radios) sync `radioState{on, freq}` as a ref property. Optional T2 "shared broadcast": server time-stamps a track schedule per station and clients seek to it (only feasible for custom gamemode stations, not scene-driven vanilla DJs). Settlement recruitment beacon is gameplay → handled by S5 server logic. *Tests:* property round-trip.

**(f)** S. Depends: quest whitelist (S14), S5.

---

### S13. Map, markers, fast travel (F26)

**(a) Vanilla:** discovering a location adds its map marker (visible, travel-enabled); fast travel from the Pip-Boy map to discovered markers; time passes by distance (≈ 14 h corner to corner at timescale 20); interior fast travel only in Railroad HQ, Home Plate, Institute, Mechanist's lair, Vault 88; Survival disables fast travel except vertibird/Institute; blocked when over-encumbered or enemies nearby [web: wiki:Fast_travel]. **Vertibird signal grenade** (`BoSVertibirdGrenade` `00056917`) summons a vertibird you ride (minigun) and pick a destination; Brotherhood membership or post-ending variants [web: wiki:Vertibird_signal_grenade].

**(b) Data:** REFR map marker `XMRK` + `FNAM` flags `Visible(0x01), Can Travel To(0x02), "Show All" Hidden(0x04), Use Location Name(0x08)`, `FULL`, `TNAM.type` (0 Cave, 1 City, 2 Diamond City, … 12 Sanctuary, 13 Settlement, 15 Vault, … 66 Raider settlement, 67 Vassal, 68 Potential vassal, 69–80 Nuka-World/Far Harbor types, 81–99 custom) [src: xEdit wbDefinitionsFO4.pas:11714-11820]; runtime `ExtraMapMarker{MapMarkerData*}` — **`MapMarkerData` is undefined in all CommonLibF4 forks (RE gap)** [src: CLF4 E/ExtraMapMarker.h].

**(c) Hooks:** Papyrus `AddToMap(abAllowFastTravel)`, `CanFastTravelToMarker`, `EnableFastTravel`, `IsMapMarkerVisible`, `Game.FastTravel(ref)`, `Game.ShowAllMapMarkers`, `InputEnableLayer.EnableFastTravel(bool)`, `Cell.EnableFastTravel` [src: F4SE vanilla/ObjectReference.psc:212, 250, 317, 618], [src: F4SE vanilla/Game.psc:28, 94], [src: F4SE vanilla/InputEnableLayer.psc:28]; event `ScriptObject.OnPlayerTeleport` (load door, fast travel, moveto) [src: F4SE vanilla/ScriptObject.psc:~314]; `Actor.OnPlayerEnterVertibird(ref)` [src: F4SE vanilla/Actor.psc:984]; menus `PipboyMapMenu`, `VertibirdMenu` [src: CLF4 IDs_RTTI.h].

**(d) Breaks:** discovery is per save; fast travel advances the shared clock; SkyMP simply disables it (B17).

**(e) Design**
- *State:* `PlayerProfile.discoveredMarkers: set<FormDesc>`, `travelEnabled` subset; gamemode can grant/revoke.
- *Capture discovery:* client reports **`MarkerDiscovered{markerRefId}`** when a marker turns visible (poll `IsMapMarkerVisible` for nearby markers or hook the HUD "discovered" path); server validates distance (player within marker's discovery radius, e.g., ≤ 2048 u) and persists. On login the server sends **`MapMarkers{discovered[], travel[]}`**; client applies via `AddToMap(true)`.
- *Fast travel:* block engine travel (`InputEnableLayer.EnableFastTravel(false)` or hook map travel) and send **`FastTravelRequest{markerRefId}`**; server checks discovered+travel flag, not in combat (recent hits/hostile NPC hosts), carry weight (server-computed), server rule (`fastTravel: off|discoveredOnly|settlementsOnly|any`), cooldown, survival flag → `Teleport` (reuse MsgType 20/31) to the marker's travel point, no clock advance (config `fastTravelAdvancesTime=false`). `onFastTravel(actorId, markerId) [blockable]`.
- *Vertibird (T2):* server spawns a vertibird actor hosted by the summoner, occupancy as furniture; destination selection → server `FastTravelRequest` with flight cutscene; or simplify to a fast travel with cooldown.
- *Custom markers / quest markers:* local / from S14.
- *Tests (`[MapMarker]`):* discovery validation, persistence, rule matrix, encumbrance/combat denial.

**(f)** M. Depends: F08 (weight), F11 (combat state), RE for `MapMarkerData` (or Papyrus-only path).

---

### S14. Quests & dialogue (F27)

**(a) Vanilla:** 191 quests in the base game, 272 with add-ons; main quest (prologue *War Never Changes* MQ101 → *Out of Time* MQ102 → *Jewel of the Commonwealth* MQ103 → *Unlikely Valentine* MQ104 → *Getting a Clue* MQ105 → *Reunions* MQ106 → *Dangerous Minds* MQ202 → *The Glowing Sea* MQ204 → *Hunter/Hunted* MQ205 → *The Molecular Level* MQ206 → *Institutionalized* MQ207 → faction acts), mutually exclusive faction lines (Minutemen, Brotherhood, Railroad, Institute) with repeatable radiants, companion quests, story-manager radiant quests (randomized aliases), no level cap and post-game play [web: wiki:Fallout_4_quests], [web: wiki:Fallout_4_factions]. Dialogue: player is voiced; 4-option wheel; Charisma speech checks: chance = CHA × 15 % − difficulty, clamped 5–100 % [web: wiki:Charisma_(Fallout_4)].

**(b)/(c):** QUST (stages, objectives, aliases, fragments), DIAL/INFO, SCEN (phases/actions; player dialogue action) [src: xEdit wbDefinitionsFO4.pas:6309, 8879, 9672, 10684]; Papyrus `Quest.SetStage/GetStageDone/SetObjectiveDisplayed/Completed/Failed/CompleteQuest/Start/Stop/Reset`, events `OnStageSet`, `OnQuestInit`, `OnStory*` (incl. `OnStoryCraftItem`, `OnStoryClearLocation`) [src: F4SE vanilla/Quest.psc:83-260]; `Scene.Start/ForceStart/Stop/IsPlaying`, `OnBegin/OnEnd/OnPhaseBegin/OnAction` [src: F4SE vanilla/Scene.psc]; `DialogueMenu{dialogueButtonOBJs[4], speechChallengeAnimObj, isLookingAtPlayer}` [src: CLF4 D/DialogueMenu.h]; `Actor.OnSpeechChallengeAvailable` [src: F4SE vanilla/Actor.psc:1028]; `TESQuestEvent`, `PlayerCharacterQuestEvent`.

**(d) Breaks:** quests change shared world state (enable parents, deaths, faction relations, settlement ownership); aliases assume one player; scenes play on one client; outcomes are exclusive.

**(e) SkyMP today:** vanilla quests are not synced: `Quest` struct exists but is not serialized; `GetStage` returns 0; clients block vanilla Papyrus events (`blockPapyrusEventsService`) [src: .../Quest.h:6-43], [src: .../PapyrusQuest.cpp:11-31] (B15). FalloutMP scope keeps vanilla quests out of scope (`00-vision-scope.md §6`).

**Design options** (choose per server; default A+D):
- **A. Disabled vanilla quests** (default): client stops quest scripts/scenes except a whitelist (radio, ambient, workshop-free systems); world spawned in a "post-intro" state (template save at Vault 111 exit, see S19).
- **B. Instanced per-player progress:** server stores `questProgress[profileId][questId] = {stage, objectives}`; only for quests whose effects are player-local (misc/radiant fetch quests). Shared side-effects would need phasing, which SkyMP does not have → not for MQ/faction lines.
- **C. Shared world storyline:** server-wide quest state (one Minutemen General per server, faction war outcome) — gamemode-driven.
- **D. Gamemode-authored quests:** server Papyrus (FO4 PEX) or JS defines quests; client displays them through a pool of generic `FalloutMP_QuestSlotNN` QUST records in a FalloutMP plugin whose objective text uses text replacement (`AddTextReplacementData`) [inference]; message **`QuestUpdate{slot, title, stage, objectives[{id, text, state}], targets[{objectiveId, refId}]}`** (S→C) and **`DialogueChoice{speakerRefId, choiceId}`** (C→S) for server dialogues (CEF wheel UI or `DialogueMenu` with a generic topic set).
- Dialogue with hosted NPCs stays local/cosmetic unless the INFO has gameplay fragments, which are executed server-side by request (`DialogueAction{speakerRefId, infoId}` → validated: NPC alive/nearby, conditions pass).
- *Persistence:* serialize `quests` (fix B15) or new `PlayerProfile.questProgress`. *Gamemode:* `onQuestStage(actorId, questKey, stage) [blockable]`, `onDialogueChoice [blockable]`. *Papyrus:* real `Quest` natives on server for gamemode quests (`SetStage`, objectives).
- *Tests:* quest state round-trip; QuestUpdate serialization; stage permission.

**(f)** L (D framework), XL+ (B/C for real vanilla content). Depends: F-PVM, FalloutMP plugin with quest slots, S19 template save.

---

### S15. Factions, reputation, crime & ownership

**(a) Vanilla:** factions group NPCs for combat assistance, crime and ownership; major factions BoS `0005DE41`, Railroad `000994F6`, Institute `0005E558`, Minutemen `00068043` [web: wiki:Fallout_4_factions]. **No bounty or jail**: a detected crime turns the owner/faction hostile; most groups calm down after about 3 game days, murders of named NPCs are permanent [web: https://steamcommunity.com/app/377160/discussions/0/5371100420921628704/] (community-sourced). Companions have affinity (F21).

**(b) Data:** FACT relations (friend/ally/enemy), `DATA` flags (`Track Crime 0x40`, `Vendor`, `Can Be Owner 0x8000`, ignore-crime flags), `CRVA` crime values, ranks, vendor data [src: xEdit wbDefinitionsFO4.pas:6785-6846]; `TESFaction{crimeGoldMap, data, crimeData, vendorData, rankDataList, majorCrime, minorCrime, enemyFlagTimeStamp}` [src: CLF4 T/TESFaction.h]. Faction.psc still exposes Skyrim-style crime gold functions (vestigial) [src: F4SE vanilla/Faction.psc:4-76].

**(c) Hooks:** `Faction.SetEnemy/SetAlly/SetPlayerEnemy/IsPlayerEnemy/GetFactionReaction`, `Actor.AddToFaction/RemoveFromFaction/SetFactionRank/IsInFaction/GetFactionReaction/WouldBeStealing/SetCrimeFaction`, `ObjectReference.SendStealAlarm/SetActorOwner/SetFactionOwner/GetActorOwner/GetFactionOwner/HasOwner/IsOwnedBy`, `Game.SetPlayerReportCrime` [src: F4SE vanilla/Faction.psc, Actor.psc:170-226, 502-529, 658-698, 817], [src: F4SE vanilla/ObjectReference.psc:340-343, 393, 552-559, 621, 847-859, 887], [src: F4SE vanilla/Game.psc:325]; `TESObjectREFR::GetOwner()`, `IsCrimeToActivate()` [src: CLF4 T/TESObjectREFR.h:372, 452].

**(d) Breaks:** "player enemy" flags are global to the client's player; NPC AI on the host sees remote players as ordinary actors (not `PlayerRef`), so faction-based hostility toward a specific remote player cannot use `SetPlayerEnemy`.

**(e) Design:** SkyMP already persists actor factions and implements `AddToFaction/IsInFaction/GetFactions/RemoveFromFaction` [src: .../MpActor.cpp:262-349] (B15). Add per-player **`CrimeLedger{factionId → hostileUntilGameTime, permanent}`** and `factionStanding` in `PlayerProfile`; when hostile, the server adds the remote player's actor to a generated "MP_HostileTo_<faction>" faction on every client (pushed via `CreateActor.props.factions`), so the hosting client's AI attacks only that player; the owning client calls `SetPlayerEnemy` for its own player. Crimes reported by hosts (`CrimeWitnessed`, S7) and validated (witness alive, within 2048 u, in LOS per host). Ownership: runtime overrides `ownerOverride` on change form; workshop/player-home containers owned by profiles (permissions instead of crime). *Gamemode:* `onCrime(actorId, factionId, crime) [blockable]`, `onFactionChange`. *Tests:* ledger expiry with fake clock; faction push.

**(f)** M. Depends: F13 hosting, S7, S17 clock.

---
### S16. Doors, load doors, elevators, activators, switches, powered objects (F07)

**(a) Vanilla:** load doors teleport between cells (`XTEL`); normal doors open/close; locked/terminal-locked/barred doors (S8); elevators are either load-door style or scripted moving cars driven by `ElevatorMasterScript` (call buttons; `MakeElevatorFunctional()`; example cell *SwitchBoard*) [web: https://stepmodifications.org/forum/topic/10542-fallout-4-ck-elevator-script-creation/]; Contraptions adds buildable 2–4 floor elevators; activators/switches toggle linked refs (lights, traps, gates); many workshop objects need power (S5).

**(b)/(c):** DOOR, ACTI (`IsRadio`), FURN, REFR `XTEL`, activation parents/children (`XAPD`/`XAPR`), linked refs (`ExtraLinkedRef`), enable parents; Papyrus `Activate`, `BlockActivation`, `SetOpen/GetOpenState (0 none, 1 open, 2 opening, 3 closed, 4 closing)`, `PlayAnimation(AndWait)`, `TranslateTo/SplineTranslateTo` + `OnTranslationComplete`, `Game.SetPlayerOnElevator(bool)`, `IsPowered`, `OnPowerOn/Off`, `OnOpen/OnClose`, `SetDestroyed/GetCurrentDestructionStage` [src: F4SE vanilla/ObjectReference.psc:194, 239, 446-453, 697-703, 880, 910, 940-946, 1008-1018, 1058, 1071-1076, 1098-1108], [src: F4SE vanilla/Game.psc:318]; engine `TESActivateEvent`, `TESFurnitureEvent`.

**(d) Breaks:** scripted movers (elevators, drawbridges) run locally → different positions per client; power state is local; Papyrus on activators is blocked.

**(e) Design:** reuse SkyMP DOOR/teleport/activation-parent logic [src: .../MpObjectReference.cpp:1467-1511, 1607-1638]. Add:
- **Server-driven movers:** `MoverState{refId, segment/floor, startServerMs, durationMs}` property; clients animate with `TranslateTo` from the authoritative start time; elevator call = Activate → server schedules movement (native re-implementation of `ElevatorMasterScript` behaviour, or server PVM running the vanilla script when available). Riders: server moves passengers' authoritative positions with the car; clients keep `SetPlayerOnElevator`.
- **Switch/power:** switches toggle server state (`isOn`), propagating through the S5 power graph or linked refs; clients render via `SetOpen`/`PlayAnimation`/enable-disable.
- **Door state** keeps SkyMP `isOpen` with 3 s auto-close reloot; lock state is separate (S8).
- **Destructibles:** `destructionStage` property (turrets, generators, cars exploding) — server authoritative when damage is server-computed (F11).
- *Gamemode:* existing `onActivate`; new `onMoverStart`. *Papyrus:* `OnActivate`, `OnOpen/OnClose`, `OnTranslationComplete` on server.
- *Tests:* mover timeline math; power-gated door; activation child delays (existing tests extended).

**(f)** M. Depends: S5 (power), S8, F-PVM optional, B8 (load initially-disabled refs driven by enable parents).

---

### S17. Time & weather (F25)

**(a) Vanilla:** default timescale 20; globals **GameYear 0x35, GameMonth 0x36, GameDay 0x37, GameHour 0x38, GameDaysPassed 0x39, TimeScale 0x3A** — same IDs and `Calendar` layout as Skyrim (`gameYear, gameMonth, gameDay, gameHour, gameDaysPassed, timeScale`, then `midnightsPassed`, `rawDaysPassed`) [src: CLF4 C/Calendar.h], [web: https://steamcommunity.com/app/377160/discussions/0/458606877314983444/] (TimeScale `GLOB:0000003A` confirmed; the others inferred from the identical layout — verify with `help gamehour 4` in game). Waiting/sleeping (`SleepWaitMenu`, `SitWaitMenu`) and fast travel advance time. Weather alternates (clear, overcast, fog, rain); **radstorms** (~2 game hours, green fog, lightning that deals rads; interiors safe); Far Harbor's island is covered by radioactive **Fog**; Contraptions weather-change shells [web: wiki:Weather], [web: wiki:Radstorm].

**(b) Data:** WTHR flags `Pleasant/Cloudy/Rainy/Snow` (+aurora, rain occlusion, HUD rain) [src: xEdit wbDefinitionsFO4.pas:13066-13080]; **`UNAM` Magic: Lightning Strike {spell, threshold}, Weather Activate {spell, threshold}** — how radstorm rads are applied [src: xEdit wbDefinitionsCommon.pas:9897-9911]; CLMT/REGN weather lists.

**(c) Hooks:** `Weather.FindWeather(class)`, `ForceActive(override)`, `SetActive(override, accelerate)`, `GetCurrentWeather`, `GetOutgoingWeather`, `GetCurrentWeatherTransition`, `ReleaseOverride`, `GetSkyMode`, `GetClassification` [src: F4SE vanilla/Weather.psc]; `Game.PassTime(hours)`, `Utility.GetCurrentGameTime/WaitGameTime`, events `OnPlayerSleepStart(start, desiredEnd, bed)/OnPlayerSleepStop(interrupted, bed)/OnPlayerWaitStart/OnPlayerWaitStop` (after `RegisterForPlayerSleep/Wait`), `OnRadiationDamage(target, ingested)` [src: F4SE vanilla/Game.psc:266], [src: F4SE vanilla/Utility.psc:18-53], [src: F4SE vanilla/ScriptObject.psc:112-123, 300-322]; `Sky::currentGameHour` [src: CLF4 S/Sky.h:159].

**(d) Breaks:** each client's clock/weather drifts; sleeping/waiting/fast travel would skip shared time; radstorm rads applied by local spells.

**(e) Design**
- **WorldClock (server-authoritative):** `{serverEpochMs, gameDaysPassedAtEpoch, timeScale}` in server state (persisted); config `time: {mode: "scaled"|"realtime", timeScale: 20, startGameDay, hoursOffset}` (`realtime` = SkyMP behaviour, timescale 1). Message **`WorldClock{serverNowMs, gameDaysPassed, timeScale}`** (S→C, on connect and on change, plus every 60 s for drift). Client sets all six globals exactly and `TimeScale`, correcting drift > 30 game-seconds (replaces SkyMP's 0.6/1.2 nudging, B16). Server Papyrus `GetCurrentGameTime` returns `gameDaysPassed`; `WaitGameTime` uses the real conversion (fix B16).
- **Wait/sleep:** no global time skip. `RestRequest{kind: sleep|wait, hours, bedRefId?}` (C→S) → server validates (bed ownership/occupancy, not in combat, survival rules), applies benefits (HP regen, Well Rested 8 h / Lover's Embrace, survival fatigue) and replies `RestResult`; client shows a fade/animation without passing time (block the vanilla `SleepWaitMenu` time pass, `setInChargen(…, disableWaiting=true)` as SkyMP does [src: skymp5-client/src/services/services/enforceLimitationsService.ts:11-17]). Gamemodes may implement "everyone asleep → advance clock" via `mp.set(0, "worldClock", …)`.
- **Weather:** server `WeatherState{perWorldspace: {current, previous, transitionStartMs, nextChangeMs, forced}}` rolled from CLMT/REGN lists with server RNG at a configurable cadence; **`WeatherUpdate{worldspaceId, weatherId, transitionSec, forced}`** (S→C) to clients in that worldspace; client `SetActive(true, accelerate)` / `ForceActive`. Interiors ignore.
- **Radiation from weather:** server computes radstorm exposure for players in exterior cells of that worldspace (rate from the UNAM spells' MGEF magnitudes); client-applied local spell effects on rads are ignored/validated by F08 caps. Far Harbor Fog = location/trigger-based rads via server `geo` triggers.
- *Gamemode:* `onWeatherChange [blockable]`, `onRest [blockable]`; `mp.get/set(0,"worldClock"|"weather")`.
- *Tests (`[WorldClock]`, `[Weather]`):* clock math round-trips; global values for given server time; weather roll determinism with seed; rest validation.

**(f)** M. Depends: ESPM CLMT/REGN/WTHR parsing, F08 (rads), F20 (survival).

---

### S18. World persistence & respawn (F14)

**(a) Vanilla:** cells reset after `iHoursToRespawnCell` = 168 h, cleared locations after `iHoursToRespawnCellCleared` = 480 h (cross-checked by two community sources) [web: https://www.nexusmods.com/fallout4/mods/1371], [web: https://steamcommunity.com/app/377160/discussions/0/3436829654709317843/]; vendor refresh ≈ 2 days (S6). Reset respawns NPCs with the `Respawn` ACBS flag and leveled encounters (encounter zones may lock levels / never reset), refills `Respawns` containers, restores picked world items, relocks, removes corpses and non-persistent dropped items; uniques/quest refs stay; locations track cleared state.

**(b)/(c):** NPC_ `ACBS` flags (`Respawn` bit 3, `Unique` 5, `Spawns Dead` 26) [src: xEdit wbDefinitionsFO4.pas:10307-10340]; CONT `Respawns`; ECZN; LCTN; Papyrus `Location.IsCleared/SetCleared/HasEverBeenCleared/Reset`, `OnLocationCleared`, `Cell.Reset`, `ObjectReference.Reset/OnReset`, `DeleteWhenAble` [src: F4SE vanilla/Location.psc:22-80], [src: F4SE vanilla/Cell.psc:22], [src: F4SE vanilla/ObjectReference.psc:146, 771, 1087]; engine `TESLocationClearedEvent`, `TESCellAttachDetachEvent`.

**(d) Breaks:** resets trigger only when no player has the cell loaded; with players everywhere a world may never reset; SkyMP uses per-type reloot timers (default 1 h) and 25 s NPC respawn instead (B6, B14).

**(e) Design:** new **`CellResetService`** on top of SkyMP reloot:
- Per cell/location `CellResetState{lastResetGameTime, cleared, dirty}`; reset when `now − lastReset ≥ (cleared ? hoursCleared : hours)` **and** no player in the 3×3 grid neighbourhood (or after a grace period), using the S17 clock.
- On reset: CONT with `Respawns` → `RelootContainer`; ESM pickables → `SetHarvested(false)`; locks → ESM state; dead NPCs with `Respawn` flag (or in respawning encounter zones) → respawn at editor location with re-rolled leveled base; non-respawn uniques remain dead; dynamic dropped forms in the cell → delete; corpses → disable; workshop build areas excluded.
- SkyMP per-type `reloot`/`forbiddenReloot` stay as overrides; add `cellReset: {hours: 168, hoursCleared: 480, requireEmptyNeighbourhood: true}`; NPC `spawnDelay` for ESM NPCs defaults to "on cell reset" instead of 25 s (players keep `spawnDelay`).
- Dropped items: keep SkyMP's 120 s / 10-per-actor policy configurable (`droppedItems: {lifetimeSec, maxPerActor}`) but preserve `omods` (B12).
- Persist `lastResetGameTime` per cell in a new server record. *Gamemode:* `onCellReset(cellId) [blockable]`. *Tests (`[CellReset]`, extend `[Respawn]`):* timers with fake clock; neighbourhood occupancy blocking; respawn flag matrix; container `Respawns` honoured; uniques stay dead.

**(f)** M. Depends: S17, B6/B8 fixes, F13, F14.

---

### S19. Character creation & identity (F03)

**(a) Vanilla:** prologue MQ101 (pre-war Sanctuary): bathroom mirror editor for player and spouse (`LooksMenu`), then the Vault-Tec rep's form opens the SPECIAL screen (21 points to distribute; each stat 1–10) and the **name** field; MQ102 begins in Vault 111; the exit elevator offers a last edit; later edits at the **Mega Surgery Center** in Diamond City (Doc Crocker, Doctor Sun after *The Disappearing Act*, 100 caps) [web: https://gameranx.com/features/id/31568/article/fallout-4-guide-how-to-create-your-character/], [web: https://fallout.wiki/wiki/Mega_Surgery_Center]. Voiced protagonist (male/female voice types); Codsworth can speak 900+ recognized names [web: wiki:Codsworth].

**(b)/(c):** `Game.ShowRaceMenu(akMenuTarget, uiMode, spouseF, spouseM, vendor)`, `ShowSPECIALMenu`, `PrecacheCharGen/Clear`, `SetCharGenHUDMode`, `SetInChargen`, `ScriptObject.RegisterForLooksMenuEvent` → `OnLooksMenuEvent(aiFlavor)` [src: F4SE vanilla/Game.psc:273-276, 303, 312, 342-345], [src: F4SE vanilla/ScriptObject.psc:140, 350]; `LooksMenu : GameMenuBase, BSTEventSink<ChargenCharacterUpdateEvent>` (no `MENU_NAME` constant in CLF4; RTTI name `LooksMenu`) [src: CLF4 L/LooksMenu.h], `SPECIALMenu`, `SPECIALMenuEvent`.

**(d) Breaks:** the intro is a single-player cinematic with quest scripts, spouse, Shaun, and the bombs; name/SPECIAL commit locally; voice lines use the player's voice type.

**(e) Design (onboarding):**
1. Login (F-GM) → if the profile has no character: spawn into a quiet holding cell from the FalloutMP plugin (or Vault 111 cryo room interior) with `SetInChargen` and **`SetLooksMenuOpen{open, mode: "create"|"surgery"}`** (reuse `SetRaceMenuOpen`, MsgType 29, add `mode`) → client calls `ShowRaceMenu` (player only, no spouse).
2. Appearance commit via `UpdateAppearance` (MsgType 4) with the FO4 struct (`docs/FALLOUT4_PORT_RESEARCH.md §4.6`); accepted only while the server flag is open (SkyMP rule [src: docs/FALLOUT4_PORT_RESEARCH.md §2.4]).
3. **`SetSpecial{values[7]}`** + **`SetCharacterName{name}`** (or a CEF form) — server validates 1–10 each, sum = 28 at creation, name length/charset/uniqueness/profanity (`onSetName [blockable]`); SPECIAL ownership → F19.
4. Spawn at `startPoint` (default: Vault 111 exit, top of the elevator, or Sanctuary bridge; exact coordinates from the ESM marker — `[inference]` look up the elevator exit marker ref) — `spawnPoint` in `MpChangeFormREFR` replaces Tamriel coordinates [src: .../MpChangeForms.h:104-107].
5. Surgery: activating the Mega Surgery Center service (dialogue) → server charges 100 caps (gamemode-configurable) and opens `mode: "surgery"`.
- Voice: choose player voice type by sex (verify EDIDs in data) [inference]; name pronunciation N/A. *Gamemode:* `onUpdateAppearanceAttempt` (existing), `onCharacterCreated`. *Tests:* SPECIAL validation; appearance gate; name rules.

**(f)** M. Depends: F03 appearance struct, F19, PLAT `ShowRaceMenu` wrapper, holding-cell/template save (survey §4.1 Option B).

---

### S20. Other systems

**Pickpocketing.** Vanilla: `ContainerMenu` in pickpocket mode; max chance 90 %; +1 % per Agility point; weight and value penalties (value only for stealing); **Pickpocket** perk (PER 1; `4D88A, E3702, E3703, 1D248F`): ×1.25/×1.5/×1.75/×2, rank 2 plant live grenades, rank 3 steal equipped weapons, rank 4 steal equipped items; stacks are stolen whole; failure → hostility (`Actor.OnPickpocketFailed`) [web: wiki:Pickpocket_(Fallout_4)], [src: F4SE vanilla/Actor.psc:976]. MP: server computes chance and rolls RNG — **`PickpocketAttempt{targetActorId, item: ItemKey, count, direction: steal|plant}`** → `PickpocketResult{success}`; target must be an NPC (PvP pickpocket only if the gamemode enables it); failure → `CrimeWitnessed` path (S15). `onPickpocket [blockable]`. Size S–M.

**Sleeping & beds.** Well Rested (+10 % XP, 8 game hours; `WellRestedXPBonus 0005C526`; Survival `HC_WellRestedPerk 00000844` also +2 END/AGI, needs ≥ 7 h in a bed) when sleeping in an owned/rented/settlement bed not assigned to a settler; Lover's Embrace +15 % with a romanced companion; Survival limits sleeping bag 3 h / mattress 5 h [web: wiki:Well_Rested_(perk)], [web: wiki:Resting]. MP: handled by `RestRequest` (S17); bed = furniture occupancy (B3) + ownership check (S5 assignments). Size S.

**Photo mode.** No official photo mode in FO4 (NG or AE); only mods [web: https://www.pcgamesn.com/fallout-4/photo-mode-mod], [web: https://www.nexusmods.com/news/15400]; no `PhotoMenu` RTTI [src: CLF4 IDs_RTTI.h]. MP: client-local; if a mod freezes time locally it only affects that client. Size —.

**Achievements & MiscStats.** `Game.AddAchievement(id)`, `IncrementStat(name, n)`, `QueryStat(name)`, `RegisterForTrackedStatsEvent` → `OnTrackedStatsEvent`; engine `TESTrackedStatsEvent`; Pip-Boy DATA › Stats categories [src: F4SE vanilla/Game.psc:4, 205, 279], [src: F4SE vanilla/ScriptObject.psc:137], [web: wiki:Pip-Boy_3000_Mark_IV]. Modded saves do not earn achievements [web: wiki:Fallout_4_achievements_and_trophies]. MP: Steam achievements off (or untouched); server keeps `PlayerProfile.stats: map<string,int>` incremented by server-side actions (locks picked, items crafted, settlements unlocked…) and pushes to the client's MiscStats for Pip-Boy display (native setter needed); gamemode `onStat`. Size S–M.

**HUD markers & compass.** Quest targets, discovered/undiscovered locations, enemy ticks, companions. MP: quest targets from `QuestUpdate` (S14); optional party-member markers via custom HUD (CEF) or dummy marker refs [inference]. Client-local rendering. Size S.

**Difficulty.** `Game.GetDifficulty`: 0 Very Easy, 1 Easy, 2 Normal, 3 Hard, 4 Very Hard, 5 Survival (defunct), 6 Survival w/ Hardcore [src: F4SE vanilla/Game.psc:117-127]; `Actor.OnDifficultyChanged` [src: F4SE vanilla/Actor.psc:916]. SkyMP forces INI difficulty 5 [src: skymp5-client/src/services/services/disableDifficultySelectionService.ts:29] — in FO4 value 5 is defunct, so force **2 (Normal)** and let the server damage formula (F11) own multipliers; Survival rules (no fast travel, sleep-saves, needs) are server rules (F20), not the client difficulty. Size S.

**Creations / Creation Club content.** Anniversary Edition (Nov 10, 2025) bundles ~170 Bethesda Creations (~153 former CC items + new) as the *Creations Bundle*, mostly ESL/light plugins; new in-game Creations menu follows load order (`MarketplaceMenu` RTTI) [web: wiki:Creations], [web: https://www.nexusmods.com/news/15400]. CC settlements/homes cannot join supply lines [web: wiki:Supply_line]. MP: server load order must list every `cc*.esl/esm` it uses; clients verified by the existing manifest/CRC check (`loadOrderVerificationService`) [src: skymp5-client/src/services/services/loadOrderVerificationService.ts]; libespm needs ESL (`FE xxx`) support (`docs/FALLOUT4_PORT_RESEARCH.md §4.2`). Size covered by ESPM workstream.

---
## 3. Cross-cutting: network messages & server data models

> **Plan note (main session):** Where the feature specs differ from this reference, the specs win. Persist OMODs as `FormDesc` (not raw ids, see 01-sync-standard §8.3). `stolenFrom` is part of item identity (F04). Container contents use `SetInventoryFo4` with `refId` (F06).

> **Plan note (main session):** Message names and fields here are research proposals. The **authoritative IDs are in [01-sync-standard.md §6](../01-sync-standard.md)**. IDs 34–63 are reserved for upstream SkyMP; FO4 twins use 64–79 and new FO4 messages use 80–122. Map proposals onto that registry; do not use IDs proposed here.

### 3.1 Changes to existing SkyMP messages
| MsgType (id) | Change | Systems |
|---|---|---|
| `PutItem` (8), `TakeItem` (9) | entries carry `omods`; `TakeItem` adds `quick?: bool`, `version?: u32` | S1, S7 |
| `DropItem` (19) | send full `Inventory::Entry` (with `omods`, `name`, `health`), not only `baseId,count` | S1, S18 |
| `SetInventory` (28), `UpdateEquipment` (5), `CreateActor` (33) props `inventory/equipment` | entries with `omods`; `CreateActor.props` adds `lock`, `powered`, `destructionStage`, `radioState`, `moverState`, `actorOmods` (robots), MP faction list | S1, S3, S5, S8, S12, S15, S16 |
| `CraftItem` (13) | add `recipeId`, `count`, `nonce` | S3 |
| `Activate` (6) | unchanged; server adds reach check (B4) and new branches (dead actors, TERM, locked refs) | S7, S8, S9 |
| `SetRaceMenuOpen` (29) | add `mode: create|surgery` (LooksMenu) | S19 |
| `UpdateAppearance` (4) | FO4 appearance struct | S19 |
| `Teleport` (20)/`Teleport2` (31) | used for fast travel results | S13 |
| `UpdateMovement` (2) | flag `pipboyLight` | S11 |

### 3.2 New messages (IDs assigned in `01-sync-standard.md §6`)
All reliable unless noted. "nonce" = client-chosen id echoed in results for idempotency.

| Message | Dir | Key fields | Systems |
|---|---|---|---|
| `SetFavorites` | C→S | `slots: ItemKey?[12]`, `taggedComponents[]` | S1 |
| `ModItem` / `ModResult` | C→S / S→C | `workbenchRefId, item, attach[], detach[], useLooseMod, rename?, nonce` / `ok, error, newItem` | S2, S3 (robots) |
| `ScrapItem` | C→S | `workbenchRefId, item, count, nonce` | S2, S4 |
| `RobotBuild` | C→S | `workbenchRefId, recipeId, nonce` | S3 |
| `StoreAllJunk` | C→S | `workbenchRefId` | S4, S5 |
| `ContainerPeek` / `ContainerContents` | C→S / S→C | `targetRefId` / `targetRefId, version, entries` (delta-capable) | S7 |
| `TakeAll` | C→S | `targetRefId, version` | S7 |
| `CrimeWitnessed` | C(host)→S | `criminalId, witnessId, crime, targetRefId?` | S7, S15, S20 |
| `WorkshopEnter` / `WorkshopState` | C→S / S→C | see S5 | S5 |
| `WorkshopPlace`, `WorkshopMove`, `WorkshopScrap`, `WorkshopStore`, `WorkshopWire`, `WorkshopUnwire`, `WorkshopAssign`, `WorkshopSupplyLine`, `WorkshopRepair` | C→S | see S5 | S4, S5 |
| `WorkshopResult` | S→C | `nonce, ok, refId?, error` | S5 |
| `WorkshopSnapshot` / `WorkshopDelta` | S→C | chunked object table / versioned diffs (binary-packed) | S5 |
| `WorkshopRatings` | S→C | `workshopRefId, ratings` | S5, S11 |
| `BarterOpen` / `OpenBarter` | C→S / S→C | `vendorActorId` / `vendorActorId, containerRefId, inventory, caps, buyMult, sellMult` | S6 |
| `BarterRequest` / `BarterResult` | C→S / S→C | `vendorActorId, buy[], sell[], expectedCapsDelta, invVersion, nonce` / `ok, error, capsDelta` | S6 |
| `VendorInvest` | C→S | `vendorActorId` | S6 |
| `PlayerTrade*` (optional) | both | two-phase offer/accept | S6 |
| `LockpickResult` | C→S | `targetRefId, success, pinsBroken, elapsedMs` | S8 |
| `TerminalOpen` | S→C | `refId, hacked, lockoutUntil` | S9 |
| `TerminalHackResult` / `TerminalMenuItem` | C→S | `refId, success, attempts, elapsedMs` / `refId, menuItemId, terminalFormId` | S9 |
| `NoteRead`, `HolotapePlay`, `HolotapeChatter` | C→S | `noteId`, `noteId, terminalRefId?`, `noteId, text, number` | S10 |
| `MarkerDiscovered` / `MapMarkers` | C→S / S→C | `markerRefId` / `discovered[], travel[]` | S13 |
| `FastTravelRequest` / `FastTravelDenied` | C→S / S→C | `markerRefId` / `reason` | S13 |
| `QuestUpdate` | S→C | `slot, title, stage, objectives[], targets[]` | S14 |
| `DialogueChoice` / `DialogueAction` | C→S | `speakerRefId, choiceId` / `speakerRefId, infoId` | S14 |
| `WorldClock` | S→C | `serverNowMs, gameDaysPassed, timeScale` (unreliable periodic + reliable on change) | S17 |
| `WeatherUpdate` | S→C | `worldspaceId, weatherId, transitionSec, forced` | S17 |
| `RestRequest` / `RestResult` | C→S / S→C | `kind, hours, bedRefId?` / `ok, effects[]` | S17, S20 |
| `SetSpecial`, `SetCharacterName` | C→S | `values[7]`, `name` | S19 |
| `PickpocketAttempt` / `PickpocketResult` | C→S / S→C | `targetActorId, item, count, direction` / `success` | S20 |

### 3.3 Server data models
```cpp
// Inventory.h (shared, game-agnostic fields optional)
struct OmodRef { uint32_t id; uint8_t idx; uint8_t rank; };          // sorted canonical list
struct ItemKey { uint32_t baseId; std::vector<OmodRef> omods;
                 std::optional<std::string> name; std::optional<float> health; };
// Inventory::ExtraData += std::optional<std::vector<OmodRef>> omods;
//                       += std::optional<uint32_t> stolenFrom;   // excluded from stacking

// MpChangeFormREFR additions (all optional, default = absent → ESM/default behaviour)
std::optional<LockState> lock;            // {level, locked, keyId, broken}
std::optional<TerminalState> terminal;    // {hacked, lockouts{profileId→ms}, itemFlags}
std::optional<uint8_t> destructionStage;
std::optional<bool> powered, switchOn;
std::optional<RadioState> radio;          // {on, freq}
std::optional<MoverState> mover;          // {segment, startServerMs, durationMs}
std::optional<std::vector<OmodRef>> actorOmods;   // robots
std::optional<OwnerRef> ownerOverride;    // runtime actor/faction/profile owner
std::optional<uint32_t> workshopId;       // interactive workshop objects
std::optional<VendorState> vendor;        // on merchant containers
std::optional<std::map<int32_t, Inventory>> privateInventories; // instanced loot
// + deletion reason "scrapped" for pre-placed workshop scrap (isDeleted already exists)

// New per-player record (or dynamicFields namespace "fo4.*")
struct PlayerProfile {
  std::array<std::optional<ItemKey>,12> favorites; std::vector<uint32_t> taggedComponents;
  std::set<FormDesc> discoveredMarkers, travelMarkers, readNotes;
  std::map<std::string,int32_t> stats;  CrimeLedger crime;  QuestProgress quests;
  uint64_t wellRestedUntilGameTime; bool characterCreated; };

// New aggregate records (own collection in the DB driver)
struct MpWorkshop { /* see S5 */ };
struct CellResetState { FormDesc cell; double lastResetGameTime; bool cleared; };
struct WorldClockState { uint64_t epochMs; double gameDaysAtEpoch; float timeScale; };
struct WeatherState { std::map<uint32_t /*wrld*/, WeatherSlot> slots; };
```
Persistence rules: every new field needs a default, JSON round-trip test and backward-compatible loading (`06-workflow-conventions.md §4`). `MpWorkshop`, `CellResetState`, `WorldClockState`, `WeatherState` need a new save-storage collection (file driver: one file per workshop, not per object).

### 3.4 New server services
`ObjectModEvaluator` (S1/S2/S6), `ComponentLedger` (S3/S4/S5), `WorkshopService` + `PowerGraph` + `WorkshopDailyUpdate` (S5), `VendorService` (S6), `CellResetService` (S18), `WorldClock` + `WeatherService` (S17), `CrimeService` (S15), `MapService` (S13), `QuestSlotService` (S14).

### 3.5 Gamemode events (new; all `[blockable]` unless noted)
`onModItem, onScrapItem, onRobotBuild, onLegendaryDrop, onWorkshopClaim, onWorkshopEnter, onWorkshopPlace, onWorkshopMove, onWorkshopScrap, onWorkshopStore, onWorkshopWire, onWorkshopAssign, onSupplyLine, onWorkshopAttack, onWorkshopDailyUpdate (no), onBarter, onVendorRestock (no), onContainerPeek (no), onLootCorpse, onSteal, onCrime, onFactionChange (no), onLockpick, onUnlock (no), onTerminalHack, onTerminalMenuItem, onHolotapePlay, onMarkerDiscovered, onFastTravel, onQuestStage, onDialogueChoice, onWeatherChange, onRest, onCellReset, onPickpocket, onSetName, onCharacterCreated (no), onStat (no)`. Existing `onPutItem/onTakeItem/onDropItem/onCraft` gain item extra data.

### 3.6 Server Papyrus (FO4 VM) — natives and events to provide
- Natives (ObjectReference): `AttachMod`, `AttachModToInventoryItem`, `RemoveMod`, `RemoveModFromInventoryItem`, `RemoveAllMods(FromInventoryItem)`, F4SE `GetAllMods`, `GetComponentCount`, `RemoveComponents`, `RemoveItemByComponent`, `Lock/Unlock/IsLocked/GetLockLevel/SetLockLevel/IsLockBroken/GetKey`, `StoreInWorkshop`, `GetWorkshopOwnedObjects`, `GetWorkshopResourceObjects`, `RecalculateResources`, `WaitForWorkshopResourceRecalc`, `IsWithinBuildableArea`, `IsPowered`, `HasSharedPowerGrid`, `AddToMap`, `CanFastTravelToMarker`, `EnableFastTravel`, `IsMapMarkerVisible`, `SetRadioOn/Frequency`, `IsRadioOn`, `SendStealAlarm`, `Set/GetActorOwner`, `Set/GetFactionOwner`, `SetDestroyed`, `Reset`. Others: `MiscObject.GetObjectComponentCount`, `Actor.ShowBarterMenu` (→ client), `Game.FastTravel`, `Game.PassTime` (no-op/gamemode), `Game.IncrementStat/QueryStat`, `Game.GetDifficulty`, `Weather.*`, `Location.IsCleared/SetCleared`, `Cell.Reset`, `Quest.*` for gamemode quests, `Utility.GetCurrentGameTime/WaitGameTime` (fixed).
- Events fired: `OnItemAdded/OnItemRemoved`, `OnOpen/OnClose`, `OnLockStateChanged`, `OnWorkshopMode`, `OnWorkshopObjectPlaced/Moved/Destroyed/Grabbed/Repaired`, `OnPowerOn/OnPowerOff`, `Terminal.OnMenuItemRun`, `OnHolotapePlay`, `OnSell`, `OnPlayerModArmorWeapon`, `OnPlayerUseWorkBench`, `OnPlayerCreateRobot`, `OnPlayerModRobot`, `OnPlayerSleepStart/Stop`, `OnPlayerWaitStart/Stop`, `OnPlayerTeleport`, `OnPickpocketFailed`, `Location.OnLocationCleared`, `OnReset`, `OnTranslationComplete`, `OnStageSet` (gamemode quests).

### 3.7 New `server-settings.json` keys
`loot.mode`, `activationReach`, `vendors.stockMode`, `vendors.respawnDays`, `workshops.{claimRule, maxPerPlayer, abandonDays, maxObjects, attacks, supplyLines}`, `fastTravel`, `fastTravelAdvancesTime`, `time.{mode, timeScale, startGameDay, hoursOffset}`, `weather.{changeMinutes, allowRadstorms}`, `cellReset.{hours, hoursCleared, requireEmptyNeighbourhood}`, `droppedItems.{lifetimeSec, maxPerActor}`, `quests.{vanillaWhitelist}`, `startPoint`.

---

## 4. Recommended MP scope tiering

Aligned with `00-vision-scope.md §5` (T0 = SkyMP parity / alpha M8, T1 = Fallout 4 identity / beta M11, T2 = advanced / 1.0 M12).

| Tier | Systems (this doc) | Justification |
|---|---|---|
| **T0 — SkyMP parity** | S1 item model **with OMOD identity** (minus favorites); S7 containers incl. corpse loot, reach check, `Respawns` flag (quick-loot peek may lag to T1); S16 doors/load doors/activators (SkyMP level); S17 server clock (globals) — weather can stay local; S18 reloot/respawn at SkyMP level + B2/B6/B8/B12 fixes; S19 LooksMenu onboarding without intro; S20 difficulty forcing; CC ESL loading; S14 option A (vanilla quests disabled) | Everything SkyMP already does must work at its level, and FO4 cannot even display inventories/equipment without OMOD instances (every spawned gun is an instance). Fixing B2/B4/B8 is cheap and blocks dup/loss and invisible-object bugs. |
| **T1 — Fallout 4 identity** | S2 modding; S3 crafting stations (chem/cooking/armor/weapon/PA); S4 components & scrapping; S5 **core** settlements (claim, build/move/scrap/store, budget, persistence, streaming, shared inventory, power simulation, ratings/daily update, settlers as abstract + hosted); S6 vendors/barter/caps; S7 quick-loot + stealing; S8 locks; S9 terminals/hacking (plausibility model); S10 holotapes (local + read flags); S11 Pip-Boy data feeds + light; S13 map discovery + server fast travel; S15 factions/crime ledger; S17 weather + radstorms; S18 cell-reset service; S20 pickpocket, beds/Well Rested, stats | These define what players recognise as Fallout 4 (modding, scrap economy, settlements, caps, locks/terminals, map). All are deterministic server rules with local UIs, so they fit SkyMP's authority model without new engine-level risk beyond workshop placement/streaming. |
| **T2 — Advanced** | S3 robot workbench + Contraptions machines; S5 settlement **attacks**, supply lines/provisioners, DLC objects (cages/arenas, conveyors, logic gates, Vault 88, raider outposts/vassals), PvP claiming; S6 player-to-player trade, per-player vendor stock; S7 instanced loot; S9 server-authoritative hacking; S12 shared radio schedule; S13 vertibird travel; S14 options B/C/D (gamemode quest framework with quest slots); S16 server-driven elevators/movers; S20 party HUD markers, achievements | High effort or depends on T1 infrastructure (hosted NPC AI at scale, quest slots, mover sync); valuable but not needed to play. Vanilla quest co-op remains a non-goal. |

Critical path inside T0/T1: ESPM FO4 records (WEAP/ARMO/OMOD/COBJ/CMPO/MISC/INNR/LVLI/TERM/NOTE/FACT/KEYM/BNDS/WTHR/CLMT) → S1 item identity + platform `getInventory/AddItemEx` → S4 ledger → S2/S3 → S5 (largest single item: XL).

---

## 5. Open questions & risks
1. **Workshop placement trust:** the server cannot run Havok placement; malicious clients can place objects inside walls/doors. Mitigation: permission scoping to own workshop area, gamemode reporting; consider server-side AABB overlap checks using record object bounds (`OBND`).
2. **Engine power vs server power** may disagree visually (lights) — accept or add a native that forces receiver state (RE needed).
3. **Batched placement performance** of 2k+ objects on stream-in (frame hitches) — needs a platform native and profiling (`G-manual`).
4. **`MapMarkerData` undefined** in CommonLibF4 — Papyrus-only discovery path or RE work.
5. **Global IDs 0x35–0x39** inferred from identical `Calendar` layout; confirm in game (`help gamehour 4`).
6. **`CookingMenu`** assumed for chem/cooking stations; confirm with a menu-open log (`G-self`).
7. **Respawn/vendor GMST values** (168/480 h, 2 days) are community-sourced; read the real GMSTs from `Fallout4.esm` (`D-real`).
8. Running TERM/INFO fragments server-side requires the FO4 PEX VM (F-PVM) and many natives; until then use native pattern handlers.
9. Settler/provisioner simulation without loaded cells is a re-implementation of `WorkshopScript.DailyUpdate`; keep golden tests to avoid drift from vanilla balance.

---

## 6. Sources

**Code (this repo):** `falloutmp-server/cpp/server_guest_lib/{Inventory.h,Inventory.cpp,MpObjectReference.cpp,MpActor.cpp,WorldState.cpp,CraftService.cpp,MpChangeForms.h,MpChangeForms.cpp,Quest.h,script_classes/PapyrusQuest.cpp,script_classes/PapyrusUtility.cpp,gamemode_events/*}`, `falloutmp-server/cpp/messages/*.h`, `falloutmp-server/cpp/addon/ScampServer.cpp`, `libespm/src/Utils.cpp`, `skymp5-client/src/services/services/{containersService,craftService,timeService,disableFastTravelService,disableDifficultySelectionService,enforceLimitationsService}.ts`, `skymp5-client/src/extensions/objectReferenceEx.ts`, `docs/docs_server_configuration_reference.md`, `docs/FALLOUT4_PORT_RESEARCH.md`.

**External code:** libxse/commonlibf4 `include/RE/` (BGSInventoryList, BGSInventoryItem, BGSObjectInstanceExtra, BGSMod, TESObjectREFR, TESObjectMISC, BGSComponent, BGSConstructibleObject, BGSInstanceNamingRules, BGSNote, BGSTerminal, TESFaction, FACTION_VENDOR_DATA, REFR_LOCK, LOCK_LEVEL, Extra*, Workshop, WorkshopMenu, WorkbenchMenuBase, ExamineMenu, BarterMenu, ContainerMenu(Base), QuickContainer*, LockpickingMenu, LocksPicked, BGSPickLockEvent, TerminalMenu, TerminalHacked, BGSHackTerminal, PipboyDataManager, PipboyManager, Pipboy*Data, PipboyLightEvent, DialogueMenu, LooksMenu, Calendar, PowerUtils, TESContainerChangedEvent, BGSInventoryListEvent, TESFurnitureEvent, BGSCraftItemEvent, TESHarvestEvent, FavoriteMgr_Events, IDs_RTTI); ianpatt/f4se 0.7.9 `scripts/vanilla/{ObjectReference,Actor,Game,Quest,Scene,Faction,Location,Cell,Weather,Utility,ScriptObject,InputEnableLayer,Terminal,Holotape,MiscObject,Activator,WorkshopParentScript,WorkshopScript,WorkshopObjectScript,WorkshopNPCScript,WorkshopDataScript,workshopObjectActorScript}.psc`, `scripts/modified/{ObjectReference,ObjectMod,Component,MiscObject,ConstructibleObject,InstanceData,FavoritesManager,Actor}.psc`, `f4se_whatsnew.txt`; TES5Edit dev-4.1.6 `Core/wbDefinitionsFO4.pas`, `Core/wbDefinitionsCommon.pas`.

**Web:** fallout.fandom.com pages (via MediaWiki API): Lock_(Fallout_4), Locksmith_(Fallout_4), Hacker_(Fallout_4), Hacking_(Fallout_4), Bobby_pin_(Fallout_4), Bottlecap_(Fallout_4), Gun_Nut_(Fallout_4), Science!_(Fallout_4), Armorer_(Fallout_4), Blacksmith, Scrapper_(Fallout_4), Local_Leader, Cap_Collector_(Fallout_4), Charisma_(Fallout_4), Pickpocket_(Fallout_4), Fallout_4_weapon_mods, Weapons_workbench_(Fallout_4), Power_armor_station_(Fallout_4), Robot_workbench, Decontamination_arch, Fallout_4_crafting, Fallout_4_junk_items, Workshop_(Fallout_4), Fallout_4_settlements, Supply_line, Provisioner, Happiness_(Fallout_4), Recruitment_radio_beacon, Fallout_4_merchants, Fallout_4_holotapes, Pip-Boy_3000_Mark_IV, Fallout_4_radio_stations, Diamond_City_Radio, Fast_travel, Vertibird_signal_grenade, Fallout_4_quests, Fallout_4_factions, Weather, Radstorm, Codsworth, Well_Rested_(perk), Resting, Fallout_4_achievements_and_trophies, Creations, Wasteland_Workshop, Contraptions_Workshop, Vault-Tec_Workshop, Fallout_4_legendary_weapon_effects, LegendaryItemQuestScript.psc; https://www.nexusmods.com/fallout4/mods/1371; https://steamcommunity.com/app/377160/discussions/0/3436829654709317843/; https://steamcommunity.com/app/377160/discussions/0/458606877314983444/; https://steamcommunity.com/app/377160/discussions/0/5371100420921628704/; https://github.com/NOBOBYoO/BuildingBudgetExtender; https://forums.nexusmods.com/topic/4074885-items-scrapable/; https://stepmodifications.org/forum/topic/10542-fallout-4-ck-elevator-script-creation/; https://fallout.wiki/wiki/Mega_Surgery_Center; https://gameranx.com/features/id/31568/article/fallout-4-guide-how-to-create-your-character/; https://www.nexusmods.com/news/15400; https://www.pcgamesn.com/fallout-4/photo-mode-mod; https://gamefaqs.gamespot.com/boards/164593-fallout-4/73891978.
