# Skyrim coupling index (FalloutMP refactor reference)

Status: reference for the GameProfile refactor. Static analysis only: nothing was built or run while writing it.
Base: working tree of `/home/user/falloutmp` (SkyMP fork), 2026-10-05.
Starting list: `docs/FALLOUT4_PORT_RESEARCH.md` §4.4, §4.5 and §8. Every location below was re-checked against the source, and the list was extended.

## 0. How to read this document

### 0.1 Provenance marks

| Mark | Meaning |
|---|---|
| `[src: path:line]` | Checked in this repository at that line. Paths are repo-relative. Line ranges are inclusive. |
| `[ext: xEdit FO4:L]` | Fallout 4 record layout from xEdit `Core/wbDefinitionsFO4.pas` (TES5Edit/TES5Edit, branch dev-4.1.6), at line L of that file. |
| `[inference]` | An engineering judgement, or a Fallout 4 fact recalled from community knowledge (UESP / fopdoc / CK wiki) and not checked against a file. Check it before relying on it. |

Most table rows give the location in the first column (`file:line`). Treat that column as `[src: ...]`. Rows whose FO4 detail column cites xEdit or says inference are marked that way.

### 0.2 Path abbreviations

| Abbrev | Path |
|---|---|
| `sgl/` | `falloutmp-server/cpp/server_guest_lib/` |
| `addon/` | `falloutmp-server/cpp/addon/` |
| `msg/` | `falloutmp-server/cpp/messages/` |
| `mpc/` | `falloutmp-server/cpp/mp_common/` |
| `sts/` | `falloutmp-server/ts/` |
| `espm/` | `libespm/` (`include/libespm/*.h`, `src/*.cpp`) |
| `pvm/` | `papyrus-vm/` |
| `cl/` | `skymp5-client/src/` |

### 0.3 Category codes (column "Cat")

| Code | Category |
|---|---|
| LO | Load order and data files (ESM names, archives, strings, data dir) |
| FID | Hard-coded form IDs (vanilla Skyrim.esm objects, keywords, races, globals) |
| AV | Actor values (Health/Magicka/Stamina model, `espm::ActorValue` enum, regen) |
| REC | Record layouts and record-type lists (subrecord offsets, record 4CC sets) |
| DMG | Damage, combat and hit validation |
| ANIM | Animation events and graph variables |
| APP | Appearance (race menu, tints, head parts, face morphs) |
| EQI | Equipment, inventory, inventory extra data, spells as equipment |
| PAP | Papyrus natives, standard scripts, script events, script storage |
| CND | Condition functions (CTDA indices and semantics) |
| SWP | Gamemode-specific SweetPie (server-owner specific, not engine) |
| NET | Networking and protocol (message schemas, protocol version). `NET/PERS` = persisted change-form schema |
| SET | Settings and defaults (server-settings.json keys, start points, reloot) |
| TST | Tests and test data |
| BUG | A latent bug that will crash or misbehave on non-Skyrim data. It usually also carries another category |

### 0.4 Abstraction codes (column "Proposal")

| Code | Meaning |
|---|---|
| `GP.x` | Move behind a `GameProfile` method or data member `x` (interface in §5.1) |
| `CFG.x` | Make it a server-settings.json key `x`, with the default supplied by GameProfile |
| `FF` | Remove it or put it behind a feature flag (mostly SweetPie) |
| `KEEP` | Generic. No change needed, or the code is shared verbatim |
| `FIX` | Fix it as a game-neutral bug fix (upstreamable to SkyMP) |
| `ESPM.x` | Handle it in libespm (game-aware parsing, §5.2) |
| `MSG.x` | Message schema work (§5.3) |

### 0.5 Headline numbers

| Item | Count | Provenance |
|---|---|---|
| MsgTypes on the wire | 33 (1..33, `Max` = 34) | [src: msg/MsgType.h:4-44] |
| Papyrus native classes registered | 22 | [src: sgl/script_classes/PapyrusClassesFactory.cpp:33-54] |
| Embedded Skyrim `.pex` stubs (`standard_scripts`) | 133 | [src: falloutmp-server/standard_scripts/, cmrc at falloutmp-server/CMakeLists.txt:132-139] |
| Unit `TEST_CASE`s | 256 | [src: unit/**/*.cpp] |
| `[espm]`-tagged unit tests | 84 in 13 files (83 in 12 files on Linux) | [src: unit/] (§2.4) |
| Untagged unit tests that still load Skyrim data through `GetPartOne()` | about 20 files | [inference from src] (§2.4.3) |
| Skyrim ESMs in the default load order | 5 | [src: addon/ScampServer.cpp:315-321] |

### 0.6 Highest-risk findings (read these first)

1. **FO4 WEAP has no DATA subrecord.** Damage is in DNAM [ext: xEdit FO4:12877]. `espm::WEAP::GetData()` returns `weapData == nullptr` [src: espm/src/WEAP.cpp:14], and callers dereference it unchecked: `MpActor::EquipBestWeapon` [src: sgl/MpActor.cpp:171,173], `TES5DamageFormula` [src: sgl/formulas/TES5DamageFormula.cpp:62] and `GetWeightFromRecord` [src: sgl/GetWeightFromRecord.cpp:22]. BUG.
2. **FO4 ARMO has no DNAM.** `espm::ARMO::GetData()` throws when DNAM is missing [src: espm/src/ARMO.cpp:36]. FO4 armor rating is `FNAM.ArmorRating u16` [ext: xEdit FO4:5786]. BUG.
3. **FO4 BOD2 is 4 bytes** (a single u32 biped mask [ext: xEdit FO4:3393]). libespm reads it only when `dataSize >= 8` [src: espm/src/ARMO.cpp:24,28], so every FO4 armor gets slot mask 0. BUG.
4. **Biped bit 0x200 means different things.** Skyrim uses it for the shield slot (`kBodShield = 0x200`) [src: sgl/MpActor.cpp:2051]. In FO4, 0x200 is "39 - [U] L Leg" and the shield is 0x20000000 [ext: xEdit FO4:3356]. Any FO4 leg armor would be treated as a shield. GP.
5. **ACBS layout differs.** The Skyrim magicka/stamina/health offsets [src: espm/src/NPC_.cpp:24-34] do not exist in FO4. FO4 ACBS is Flags u32, XP offset s16, Level u16, CalcMin, CalcMax, Disposition s16, TemplateFlags u16, BleedoutOverride u16, unused [ext: xEdit FO4:10286]. FO4 adds per-flag templates TPTA [ext: xEdit FO4:10286+]. ESPM.
6. **Wire format is positional binary.** Adding any field to an existing message breaks old clients: BitStream archives write optionals as a presence flag plus value [src: serialization/include/archives/BitStreamOutputArchive.h:88-101]. JSON archives omit `nullopt` [src: serialization/include/archives/JsonOutputArchive.h:38-48]. New MsgTypes must stay below 123, because a header byte of `{` means JSON [src: msg/MessageSerializerFactory.cpp:178-179]. NET.
7. **Persisted change forms store raw global form IDs** (`learnedSpells`, inventory baseIds) [src: sgl/MpChangeForms.cpp:54,237]. They depend on load order. A FO4 database must never be opened with a Skyrim profile, so persist the game id. NET/PERS.
8. **Crash-on-foreign-data paths** (BUG, FIX):
   - `CrimeFactionsList` FLST 0x26953 is `reinterpret_cast` without a null check [src: sgl/WorldState.cpp:482,488-489].
   - `Effects::GetData` is `noexcept` but throws [src: espm/src/Effects.cpp:18,49].
   - `RecordHeader::GetScriptData` is `noexcept` but throws on unknown VMAD property types [src: espm/src/RecordHeader.cpp:43-60, espm/src/Utils.cpp:139]. FO4 VMAD has Struct/Var types.
   - LIGH DATA offsets are misaligned [src: espm/src/LIGH.cpp:18,23].
   - NAVM `kType` is `"NVNM"` [src: espm/include/libespm/NAVM.h:14].
   - NPC_ SNAM rank is read at offset 0 [src: espm/src/NPC_.cpp:19-21].
   - `SweetPieDamageFormula` uses `.rec` unchecked [src: sgl/formulas/SweetPieDamageFormula.cpp:86].
9. **`disableVanillaScriptsInExterior` assumes five Skyrim masters** (`formId < 0x05000000`) [src: sgl/MpObjectReference.cpp:1788-1789]. GP.
10. **`[espm]` is not a clean "needs data" tag.** About 20 untagged test files call `GetPartOne()`, which attaches the Skyrim ESM loader [src: unit/PartOne_ActivateTest.cpp:28-40], so `./unit/unit "~[espm]"` still needs Skyrim data for those tests [inference]. TST (refactor step A1).

---

## 1. Server coupling index (falloutmp-server)

### 1.1 `addon/ScampServer.cpp` / `.h` (N-API entry and settings parsing)

| Location | What | Cat | Proposal |
|---|---|---|---|
| addon/ScampServer.cpp:207-225 | `weaponStaminaModifiers` settings parsing (melee stamina costs keyed by Skyrim weapon kinds) | SET/ANIM | `CFG.weaponStaminaModifiers`; default from `GP.AnimationCostTable()` |
| addon/ScampServer.cpp:235-247 | `npcEnabled` parsing | SET | KEEP (generic) |
| addon/ScampServer.cpp:249-284 | `npcSettings`. BUG: `serverSettings.find("default")` at :253, then reads top-level `spawnInInterior`/`spawnInExterior` at :254-258 instead of the found object | SET/BUG | FIX (changes Skyrim behaviour only for configs that use `default`, so flag it in the PR) |
| addon/ScampServer.cpp:301 | `isPapyrusHotReloadEnabled` | SET | KEEP |
| addon/ScampServer.cpp:315-321 | Default load order: Skyrim.esm (:316), Update.esm, Dawnguard.esm, HearthFires.esm, Dragonborn.esm (:320) | LO | `GP.DefaultLoadOrder()`. FO4: Fallout4.esm, DLCRobot, DLCworkshop01, DLCCoast, DLCworkshop02, DLCworkshop03, DLCNukaWorld [inference] |
| addon/ScampServer.cpp:322-333 | `loadOrder` override from settings | LO | KEEP; add a game check (§5.2) |
| addon/ScampServer.cpp:335-341 | `lang` (strings language) | LO | KEEP; the strings file stem is per game (see LocalizationProvider) |
| addon/ScampServer.cpp:344-347 | Networking password prefix (`kNetworkingPasswordPrefix` "7_") | NET | `GP.ProtocolPrefix()` (§5.3) |
| addon/ScampServer.cpp:357-359 | `CreateConditionFunctions()` (Skyrim CTDA index table) | CND | `GP.CreateConditionFunctions()` |
| addon/ScampServer.cpp:361-388 | Damage formula chain: `TES5DamageFormula` :377, `DamageMultFormula` :378, `SweetPieDamageFormula` :380, `SweetPieSpellDamageFormula` :382, `DamageMultConditionalFormula` :384 | DMG/SWP | `GP.CreateDamageFormula(settings)`; SweetPie formulas FF |
| addon/ScampServer.cpp:391 | `ScriptStorageFactory::Create` (BSA `scripts` folder, `.pex`) | PAP/LO | Pass `GP.ScriptStorageOptions()` (archive kind BSA/BA2, recursive) |
| addon/ScampServer.cpp:404-411 | `reloot` settings by record type | SET | `CFG.reloot`; defaults from `GP.DefaultRelootTimes()` |
| addon/ScampServer.cpp:413-417 | `forbiddenReloot` | SET | KEEP |
| addon/ScampServer.cpp:796-899 | `GetLocalizedString`: FULL lstring, lowercase plugin stem :868-890 | LO | KEEP (FO4 uses the same `.STRINGS` scheme [inference]); the strings dir and stem come from GP |
| addon/ScampServer.cpp:1086-1088 | `Place` uses `FormDesc::Tamriel()` as the default world | FID/SET | `GP.DefaultWorldspace()` |
| addon/ScampServer.cpp:1092 | `Place` accepts only `NPC_` base | REC | KEEP |
| addon/ScampServer.cpp:1186-1187 | `GetNeighborsByPosition` grid `/4096` | REC | KEEP (FO4 cell size is also 4096 units [inference]) |
| addon/ScampServer.cpp:1693-1727 | `IsGameModeInsideDeathEventHandler`, returns magicka/stamina before death | AV | `GP.ActorValueCatalogue()`; return a generic AV map |
| addon/ScampServer.h:82 | Comment/name "SkyrimPlatform3 backend" | NET | KEEP (cosmetic) |
| addon/ScampServer.h:97-100 | magicka/stamina out-params | AV | Generic AV map |

### 1.2 `addon/property_bindings` (gamemode JS property API)

| Location | What | Cat | Proposal |
|---|---|---|---|
| addon/property_bindings/PercentagesBinding.cpp:19,21,33,35,61,63,108,112 | `percentages` {health, magicka, stamina} through `espm::ActorValue` | AV | GP AV catalogue. FO4: health, actionPoints (plus rads?) [inference] |
| addon/property_bindings/RespawnPercentagesBinding.cpp:15,17,34,36 | Same for respawn | AV | Same |
| addon/property_bindings/AppearanceBinding.cpp:28,34,46 | `Appearance::FromJson`, change channel | APP | Per-game appearance schema (§5.3) |
| addon/property_bindings/PropertyBindingFactory.cpp:36,38,39,47,50,59 | `appearance`, `equipment`, `inventory`, `percentages`, `spawnPoint`, `respawnPercentages` | APP/EQI/AV/SET | Bindings dispatch to per-game JSON shapes |

### 1.3 Messages and protocol (`msg/`, `mpc/`)

| Location | What | Cat | Proposal |
|---|---|---|---|
| mpc/Config.h:8,12 | Protocol "7_", also the RakNet password prefix | NET | `GP.ProtocolPrefix()`. Skyrim stays "7_"; FO4 "fo4-1_" (§5.3) |
| mpc/MpClientPlugin.cpp:15,18,34 | Client plugin uses `kNetworkingPasswordPrefix`; password file `Data/Platform/Distribution/password` | NET | Compile-time define per client build, or `CreateClientEx(prefix)` |
| msg/MsgType.h:4-44 | uint8 enum 1..33 (PlayerBowShot=22 :28, SpellCast=23 :29, SetRaceMenuOpen=29 :37, Max :43) | NET | Freeze 1..33; reserve 64..95 for FO4 twins (< 123) |
| msg/MessageSerializerFactory.cpp:106-111,178-179 | Dispatch vectors sized by `MsgType::Max`; `{` means JSON | NET | Per-game registry (`MSG.registry`) |
| msg/Messages.h:36-69 | `REGISTER_MESSAGES` | NET | Split common and per-game lists |
| msg/UpdateMovementMessage.h:27-36,42-53 | `runMode` string "Standing" :47, `isInJumpState` :48, `isSneaking` :49, `isBlocking` :50, `isWeapDrawn` :51, `lookAt` :53 | NET/ANIM | FO4 twin `UpdateMovementFo4` (flags bitfield: sprint, sneak, ironSights/aiming, weaponDrawn, jump, power armor?) |
| msg/ChangeValuesMessage.h:17-19,23-25 | health/magicka/stamina | NET/AV | `ChangeValuesT` with a generic AV id→value map |
| msg/HitMessage.h:23-27,33-37 | `isBashAttack`, `isHitBlocked`, `isPowerAttack`, `isSneakAttack`, `projectile` | NET/DMG | Reusable; FO4 adds limb/bodypart and `isVatsAttack`? Optional fields in the FO4 twin |
| msg/SpellCastMessage.h:6,29-46 | `ActorAnimationVariables`, spell cast payload | NET/PAP | FO4: drop; replace with `WeaponFire` |
| msg/SpellCastData.h:5-9 | `SpellType` Left/Right/Voise/Instant | NET | Skyrim-only |
| msg/PlayerBowShotMessage.h:15-18 | weaponId, ammoId, power, isSunGazing | NET/DMG | FO4 `WeaponFireMessage` (weaponId, ammoId, shots, ammo used) |
| msg/CreateActorMessage.h:77-118 | `isRaceMenuOpen`, `learnedSpells`, heal/magicka/stamina rates, mults, values, percentages, `templateChain` | NET/AV/APP | `CreateActorT<Props>`; FO4 props use the AV map |
| msg/CreateActorMessage.h:132-141 | appearance, equipment | NET/APP/EQI | Per-game structs |
| msg/PutItemMessage.h, msg/TakeItemMessage.h | Inherit `Inventory::ExtraData` | NET/EQI | Templated on the ExtraData type |
| msg/DeathStateContainerMessage.h | Embeds ChangeValues + Teleport | NET/AV | Follows ChangeValuesT |
| msg/SetRaceMenuOpenMessage.h | Race menu state | APP | FO4: "LooksMenu"/chargen; reuse the MsgType with a game-neutral meaning ("character editor open") |

### 1.4 Form identity, persistence, change forms

| Location | What | Cat | Proposal |
|---|---|---|---|
| sgl/FormDesc.cpp:50-52 | `kSkyrimEsm`/0x3c legacy workaround | FID/LO | GP.LegacyFormDescAliases() (Skyrim only) |
| sgl/FormDesc.cpp:57,71 | `fileIdx*0x01000000`; no ESL (FE) handling | LO | `ESPM.esl` (upstreamable) |
| sgl/FormDesc.cpp:81-82,86-95 | ToString/FromString | LO | KEEP |
| sgl/FormDesc.cpp:100-104 | `kTamriel` "3c:Skyrim.esm" | FID | `GP.DefaultWorldspace()` (FO4 "3c:Fallout4.esm" Commonwealth [inference]) |
| sgl/MpChangeForms.h:73 | `learnedSpells` | NET/PERS | Keep the field; FO4 leaves it empty |
| sgl/MpChangeForms.h:100-102 | respawn H/M/S percentages | NET/PERS/AV | Generic AV map (JSON keys kept for Skyrim) |
| sgl/MpChangeForms.h:105-107 | Default `spawnPoint` {133857,-61130,14662} rot 72, Tamriel | SET/FID | `GP.DefaultSpawnPoint()` |
| sgl/MpChangeForms.cpp:54,237 | learnedSpells persisted as raw global IDs (load-order dependent) | NET/PERS | Store `"game"` in the DB meta; refuse a mismatch |
| sgl/MpChangeForms.cpp:56-62,68-75,78,138-161 | H/M/S percentage keys, spawnPoint keys, effects, JSON pointers | NET/PERS | Per-game extra keys; Skyrim keys untouched |
| sgl/ActiveMagicEffectsMap.cpp:33,56,71 | Persists `espm::ActorValue` enum ints | AV/NET/PERS | Persist AV ids through the GP catalogue (Skyrim ints unchanged) |

### 1.5 Actor values, regeneration

| Location | What | Cat | Proposal |
|---|---|---|---|
| sgl/ActorValues.h:9-21 | H/M/S percentages, values, rates, rate mults ("percentages expected on top") | AV | Keep the struct for Skyrim; add `ActorValuesGeneric` (map) used by GP |
| sgl/ActorValues.cpp:9-13 | Switch on `espm::ActorValue` | AV | GP catalogue |
| sgl/GetBaseActorValues.cpp:6,25,34,41,48,58,68,77-79 | Base values from RACE starting H/M/S + NPC offsets, UseTraits/UseStats template flags, regen from RACE | AV/REC | `GP.ComputeBaseActorValues(npc)`. FO4: NPC_ DNAM Calculated Health/AP [ext: xEdit FO4:10286+], PRPS AV properties, AVIF defaults |
| sgl/CropRegeneration.cpp:13,68,82,96,101 | Crop health/magicka/stamina regen; raceId from appearance; IsBlockActive | AV | GP regen model (FO4: AP regen, no magicka) |
| sgl/MpActor.cpp:61-74 | `restorationTimePoints` keyed by Skyrim AVs | AV | Key by GP AV ids |
| sgl/MpActor.cpp:647,677,695,702 | SetPercentage(s), NetSendChangeValues, `kDefaultAvFilter` H/M/S | AV/NET | GP `SyncedActorValues()` |
| sgl/MpActor.cpp:1275,1565,1571,1755,1798 | ModifyActorValuePercentage, Restore/DamageActorValue, SetActorValue(s) | AV | KEEP signatures; ids from catalogue |

### 1.6 Animation system

| Location | What | Cat | Proposal |
|---|---|---|---|
| sgl/AnimationSystem.cpp:14,24 | `blockStart`/`blockStop` (SweetPie StaminaRate :18-19, :27-28) | ANIM/SWP | `GP.AnimationTable()`; SweetPie part FF |
| sgl/AnimationSystem.cpp:43; AnimationSystem.h:47 | `hasSweetpie` flag | SWP | FF (setting `gamemodeHacks.sweetPie`, default auto-detect so behaviour is identical) |
| sgl/AnimationSystem.cpp:83-221 | Event→stamina cost: attackStart :83, attackStartLeftHand :90, AttackStartH2HRight :97, AttackStartH2HLeft :104, JumpStandingStart(10) :111, JumpDirectionalStart(15) :118, bowAttackStart :125, attackRelease(10/40) :129, crossbowAttackStart :144, SneakSprintStartRoll :151, attackStartDualWield(14) :158, attackPower*(30) :165-221, attackStartSprint(15) :214 | ANIM | `GP.AnimationTable()` (data). FO4: no stamina; AP use for sprint and VATS [inference] |
| sgl/AnimationSystem.cpp:230,290 | Only active if SweetPie; HandleAttackAnim stamina | SWP/ANIM | FF |
| sgl/MpObjectReference.h:65-74; MpObjectReference.cpp:317-323 | `AnimationVariableBool` names `bInJumpState`, `_skymp_isWeapDrawn`, `IsBlocking`, `IsSneaking` | ANIM | `GP.AnimationVariableNames()` |
| sgl/MpActor.cpp:953-955 | IsWeaponDrawn via `_skymp_isWeapDrawn` | ANIM | Same |

### 1.7 Appearance, equipment, inventory structs

| Location | What | Cat | Proposal |
|---|---|---|---|
| sgl/Appearance.h:7 | `Tint{texturePath, argb, type}` | APP | Skyrim appearance; FO4 `Fo4Appearance` (morph sliders, face regions, tint entries by template index, head parts, hair color form) [inference] |
| sgl/Appearance.h:46-56,66-67 | serialize; `options` (19 face morphs), `presets` (4) | APP/NET | Per-game struct; `MSG.UpdateAppearanceT` |
| sgl/Equipment.h:6,17-28 | IsSpellEquipped; left/right/voice/instant spells | EQI | FO4 struct without spell slots |
| sgl/Inventory.h:12 | `Worn` enum (None/Right/Left) | EQI | KEEP |
| sgl/Inventory.h:31,37-47 | `ExtraData`: health, enchantmentId, maxCharge, removeEnchantmentOnUnequip, chargePercent, name, soul, poisonId, poisonCount, worn, wornLeft | EQI/NET | `GP` extra-data schema; FO4 needs OMOD list (object mods), health, name, legendary flag [inference] |
| sgl/Inventory.cpp:74-85 | `EqualExceptCount` compares Skyrim extras | EQI | Templated / schema-driven |

### 1.8 `sgl/MpActor.cpp`

| Location | What | Cat | Proposal |
|---|---|---|---|
| :107 | Default base 0x7 "NPC_" (player) | FID | `GP.PlayerBaseId()` (FO4 0x7 [inference]) |
| :140,171,173 | EquipBestWeapon dereferences `weapData->damage` | DMG/BUG | `GP` weapon view; null-check FIX |
| :350,370,378 | VisitProperties: isRaceMenuOpen, learnedSpells | APP/NET | Per-game props |
| :474,485-501,495,526 | OnEquip: SPEL/BOOK/INGR/ALCH/SCRL/WEAP/ARMO/AMMO types; LIGH CanBeCarried; "Ingredient"/"Potion" snippet types | EQI/REC | `GP.EquipRules()`; FO4 has no SPEL/BOOK/INGR/SCRL equip; ALCH = Aid, NOTE/holotapes [inference] |
| :582,596,606 | ApplyChangeForm forces race menu; base AVs | APP/AV | GP |
| :851 | IsSpellLearnedFromBase (NPC_/RACE SPLO) | PAP | Skyrim only |
| :938,946 | GetRaceId | APP | KEEP |
| :958 | GetBounds | REC | KEEP |
| :968-970 | IsCreatedAsPlayer: baseId <= 0x7 | FID | GP.PlayerBaseId() |
| :993,998,1003,1031 | Respawn %, SendAndSetDeathState, GetDeathStateMsg (full H/M/S) | AV/NET | AV map |
| :1061,1070 | EatItem, ReadBook | EQI | GP item-use rules |
| :1077,1088,1112-1148 | EnsureTemplateChainEvaluated; baseId==0x7; Skyrim template flags | REC | ESPM fo4 TPTA (per-flag templates) |
| :1190 | LoadFactions | REC | KEEP (check SNAM bug) |
| :1212,1219,1240 | EvaluateDeathItem, `kPlayerCharacterLevel=1`, INAM→LVLI | REC | KEEP; FO4 death item INAM exists? [inference] |
| :1327 | Kill sets H/M/S before death | AV | AV map |
| :1351,1397 | RespawnWithDelay; `SweetCantDrop` keep | SWP | FF |
| :1456,1461,1468,1478,1485 | GetSpawnPoint; editor ACHR; worldOrCell default 0x3c | FID/SET | GP.DefaultWorldspace() |
| :1526 | SetIsDead | KEEP | |
| :1588,1590,1606 | DropItem; 2-minute deletion; `kGold001 = 0xF` | FID/SET | `GP.CurrencyBaseId()` (FO4 Caps001 0xF [inference]); `CFG.droppedItemLifetime` |
| :1805-1917 | ApplyMagicEffect: CureDisease :1811, SPEL Disease :1816, SweetPie ×100 :1843-1844, PeakValueMod/ValueMod :1899/:1903, mult*4 hack :1916-1917 | AV/SWP/REC | GP magic-effect handlers; SweetPie FF |
| :1960,1971 | ReapplyMagicEffects SweetPie.esp check | SWP | FF |
| :1975,1998,2021,2044 | GetEquippedWeapon/Scroll/Light/Shield | EQI | GP equip model |
| :2051,2062,2064,2073 | `kBodShield = 0x200`; Skyrim shield worn comment | EQI/BUG | `GP.ShieldSlotMask()` (FO4 0x20000000 [ext: xEdit FO4:3356]) |
| sgl/MpActor.h:178-183 | hasSweetpie params | SWP | FF |

### 1.9 `sgl/MpObjectReference.cpp`

| Location | What | Cat | Proposal |
|---|---|---|---|
| :43 | `kPlayerCharacterLevel = 1` | SET | KEEP |
| :71 | DOOR baseRecordType | REC | KEEP |
| :151-153 | GetGridPos `/4096` | REC | KEEP |
| :289-307 | GetRelootTime defaults: FLOR/TREE 1h, DOOR 3s, items 1h, CONT 1h | SET | `GP.DefaultRelootTimes()` |
| :454 | Bleak Falls comment | - | - |
| :469,481,502,576,595,838,989-991 | Papyrus events OnActivate, SkympOnActivateClose, OnTriggerEnter/Leave, OnTrigger, OnItemAdded, OnInit/OnCellLoad/OnLoad | PAP | Names shared with FO4 [inference]; FO4 OnItemAdded signature equal [inference]. KEEP, with signatures in `GP.PapyrusEventSignatures()` |
| :885 | CONT reloot | SET | KEEP |
| :1338 | activationParents | REC | KEEP |
| :1356 | GivePickupItemsToActivationSource: TREE/FLOR PFIG, LIGH torch, LVLI | REC/EQI | KEEP; FO4 FLOR PFIG exists [inference] |
| :1419,1467,1490-1492 | ProcessActivateNormal; DOOR XTEL teleport; rad→deg | REC | KEEP (check XTEL layout in FO4 [inference]) |
| :1515,1543,1545 | Reach: CONT 512, FURN 256 | SET | `GP.ActivationReach()` |
| :1570,1596,1679 | ProcessActivateSecond, FURN | REC | KEEP |
| :1722,1752 | InitScripts; NPC_ UseScript template flag | PAP/REC | ESPM fo4 TPTA |
| :1788-1789 | `disableVanillaScriptsInExterior && formId < 0x05000000` (assumes 5 masters) | LO/BUG | `GP.IsVanillaForm(globalId)` computed from load order |
| :1798 | `kPrefix "Sweet"` script prefix | SWP | FF |
| :1813-1814 | Deny-list DA06PreRitualSceneTriggerScript, CritterSpawn | PAP | `GP.DeniedScripts()` / `CFG.deniedScripts` |
| :1875,1888 | Outfit DOFT/OTFT | REC | KEEP (FO4 has DOFT/OTFT [ext: xEdit FO4:9357]) |
| :1905,1947,1984 | Inventory objects; EnsureBaseContainerAdded; outfit worn Right | EQI | KEEP |
| :2003,2050 | CheckInteractionAbility; GetTotalItemWeight | EQI | KEEP (weight through GP views) |

### 1.10 `sgl/WorldState.*`, `sgl/PartOne.*`

| Location | What | Cat | Proposal |
|---|---|---|---|
| sgl/WorldState.h:249 | npcEnabled | SET | KEEP |
| sgl/WorldState.h:254 | `disableVanillaScriptsInExterior = true` | SET/PAP | KEEP the flag; logic through GP |
| sgl/WorldState.h:256-266 | `bannedEspmCharacterRaceIds` (0xe7713…, playable races 0x13740-0x13749, Mannequin 0x10760a) | FID | `GP.BannedNpcRaceIds()` |
| sgl/WorldState.cpp:50,58 | nextId 0xff000000; modIndex cache 0x100 | LO | `ESPM.esl`: cache size, ESL |
| sgl/WorldState.cpp:170 | baseType "STAT" | REC | KEEP |
| sgl/WorldState.cpp:183-209 | appearanceDump validation, RACE check :204 | APP | Per-game appearance |
| sgl/WorldState.cpp:392-409 | AttachEspmRecord type filter (NPC_/FURN/ACTI/DOOR/CONT/FLOR+PFIG/TREE+PFIG/items) | REC | `GP.IsSyncedRecordType()`; FO4 adds TERM, KEYM via IsItem, NOTE, MSTT? [inference] |
| sgl/WorldState.cpp:414,427-428 | StartsDead; flags 0x800/0x20 | REC | ESPM views |
| sgl/WorldState.cpp:472 | Skip essential/protected/unique NPCs | SET | `GP.NpcSpawnFilter()` |
| sgl/WorldState.cpp:482,488-489 | CrimeFactionsList FLST 0x26953; reinterpret_cast without a null check | FID/BUG | FIX null-check; `GP.CrimeFactionListId()` |
| sgl/WorldState.cpp:526,553 | Banned races; npcSettings | FID/SET | GP |
| sgl/WorldState.cpp:895,900,1163 | `>>24` mod index | LO | `ESPM.esl` |
| sgl/WorldState.cpp:993,1040 | PEX Reader; PapyrusClassesFactory | PAP | GP |
| sgl/WorldState.cpp:1116 | HasKeyword by EDID | REC | KEEP |
| sgl/PartOne.cpp:293 | Default name "Prisoner" | SET | `GP.DefaultPlayerName()` / CFG |
| sgl/PartOne.cpp:811-812 | baseId 0x0/0x7 not sent | FID | GP.PlayerBaseId() |
| sgl/PartOne.cpp:868-869 | DOOR baseRecordType | REC | KEEP |
| sgl/PartOne.h:107-109 | worldState/serverState/animationSystem members | - | Add `std::shared_ptr<GameProfile>` to WorldState |

### 1.11 `sgl/ActionListener.cpp`

| Location | What | Cat | Proposal |
|---|---|---|---|
| :116,159-166,168,173 | OnUpdateMovement: anim vars, block count 5, runMode "Standing" | ANIM/NET | FO4 twin handler |
| :209,217 | OnUpdateAppearance gated by IsRaceMenuOpen | APP | KEEP semantics, per-game struct |
| :230,247,317,322,390,432 | OnUpdateEquipment: SpellSlotId; ARMO BOD2 overlap; RemoveSpell; UnequipItem | EQI | `GP.ValidateEquipment()` |
| :456 | caster 0x14 (PlayerRef) | FID | `GP.PlayerRefId()` (FO4 0x14 [inference]) |
| :502,532,561 | SweetCantDrop | SWP | FF |
| :573,595,601 | OnPlayerBowShot: AMMO removal | DMG/EQI | FO4 `OnWeaponFire` |
| :721 | HostStart ChangeValues H/M/S | AV | AV map |
| :765,802,804,819 | OnChangeValues: health crop, magicka crop; stamina NOT cropped | AV/BUG | GP regen model; flag stamina gap |
| :829-831 | IsUnarmedAttack 0x1f4 | FID | `GP.UnarmedWeaponId()` |
| :834 | CalculateCurrentHealthPercentage | AV | KEEP |
| :857,864,868 | GetReach: RACE unarmedReach, WEAP DNAM reach, GMST fCombatDistance | DMG/REC | `GP.HitValidation()` (FO4 ranged: max range from DNAM) |
| :885,888 | Bounds kPatch 15 | DMG | GP hit params |
| :922,945,953,960,968,975,985,992 | IsBowOrCrossbowShot, IsDistanceValid, CanHit speed formula, ShouldBeBlocked | DMG | GP.HitValidation() |
| :1006,1020,1035,1067,1089 | OnHit: 0x14, 4096, SPEL | DMG/FID | GP |
| :1125,1138,1156,1189 | OnSpellCast: 0x14, "OnSpellCast" | PAP/DMG | Skyrim only |
| :1211 | OnSpellHit | DMG | Skyrim only |
| :1250,1269,1315-1334,1370-1373 | OnWeaponHit: splash; commented reach check; ARMO `equipSlotId > 0` = shield | DMG/EQI | GP |
| :1410,1424 | SendPapyrusOnHitEvent with the Skyrim 7-arg OnHit | PAP | `GP.MakeOnHitArgs()` (FO4 order differs [inference]) |

### 1.12 Crafting, conditions

| Location | What | Cat | Proposal |
|---|---|---|---|
| sgl/CraftService.cpp:35 | Workbench must be FURN/ACTI | REC | KEEP |
| sgl/CraftService.cpp:84-93 | Temper keywords ArmorTable 0xadb78 / SharpeningWheel 0x88108, compared to the raw local `benchKeywordId` (not ToGlobalId) | FID/BUG | `GP.CraftingRules().excludedBenchKeywords` (global IDs) |
| sgl/CraftService.cpp:95-101 | CNTO exact count match | REC | FO4 COBJ has FVPA components (`wbComponents`) instead of CNTO [ext: xEdit FO4:10266] → ESPM fo4 COBJ |
| sgl/CraftService.cpp:116 | "COBJ" | REC | KEEP |
| sgl/CraftService.cpp:238 | `Condition::FromCtda` | CND | KEEP |
| sgl/Condition.cpp:74-88 | Static ConvertFunctionIndexToString map | CND | GP function table |
| sgl/ConditionsEvaluator.cpp:259 | 0x14 PlayerRef hotfix | FID | GP.PlayerRefId() |
| sgl/condition_functions/ConditionFunction.h:14 | Skyrim UESP indices | CND | GP table |
| sgl/condition_functions/* | GetItemCount 47, GetIsRace 69, IsWeaponMagicOut 101, GetEquipped 182, GetIsPlayableRace 254, IsWeaponOut 263, HasSpell 264, IsInInterior 300, IsBlocking 569, SpellHasKeyword 596, GetEquippedItemType 597, GetActorValuePercent 640, WornHasKeyword 682, WornApparelHasKeywordCount 722 (Skymp* customs 0xFFFF) | CND | GP table maps FO4 indices (re-check each against the FO4 CK function list [inference]) |
| sgl/condition_functions/GetActorValuePercent.cpp:18-20 | 0x3E8/0x3E9/0x3EA AV ids | AV/CND | GP catalogue |
| sgl/condition_functions/GetEquippedItemType.cpp:20,21,24,48,58,84 | UESP item types, animType, spells | CND/EQI | GP |
| sgl/condition_functions/WornApparelHasKeywordCount.cpp:25-32,49-51 | Skyrim BOD2 bits | CND/EQI | GP biped model |
| sgl/condition_functions/SkympWornHasKeywordCount.h:9 | - | CND | GP |
| sgl/condition_functions/SpellHasKeyword.cpp:15-16,30-33 | Casting sources | CND | Skyrim only |
| sgl/condition_functions/IsWeaponMagicOut.cpp:24; GetIsPlayableRace.cpp:47 | - | CND | GP |

### 1.13 Damage formulas

| Location | What | Cat | Proposal |
|---|---|---|---|
| sgl/formulas/TES5DamageFormula.h:5; .cpp:13 | Unarmed 0x1f4 | DMG/FID | Skyrim profile only |
| sgl/formulas/TES5DamageFormula.cpp:62 | `weapData->damage` (null on FO4) | DMG/BUG | FIX null-check |
| sgl/formulas/TES5DamageFormula.cpp:76-77,93-97,119,132,135 | DamageResist AV; ARMO baseRatingX100; EITM ENCH; RACE unarmedDamage; GMST fMaxArmorRating / fArmorScalingFactor | DMG/REC | Skyrim profile; FO4 `Fo4DamageFormula` (DR/ER damage reduction curve [inference]) |
| sgl/formulas/TES5DamageFormula.cpp:152,157,162,215-219 | Power ×2, block ×0.1, sneak ×1.3, MGEF Hostile/Detrimental Health | DMG | Skyrim profile |
| sgl/formulas/DamageMultFormula.cpp:10; SweetPieSpellDamageFormula.cpp:12 | 0x7 player base | FID | GP |
| sgl/formulas/SweetPieDamageFormula.cpp:38,61,86 | 5 levels; 4 formIds; unchecked `.rec` | SWP/BUG | FF |
| sgl/formulas/DamageMultConditionalFormula.h:19-24 | - | DMG | KEEP |

### 1.14 Gamemode events and SweetPie services

| Location | What | Cat | Proposal |
|---|---|---|---|
| sgl/gamemode_events/EatItemEvent.cpp:37,39,45 | ALCH; INGR commented; SweetPie.esp | EQI/SWP | GP; FF |
| sgl/gamemode_events/ReadBookEvent.cpp:54,65,83,100 | BOOK flags; RemoveSpell | EQI | Skyrim only (FO4 BOOK has no spell teach [inference]) |
| sgl/gamemode_events/DeathEvent.cpp:8-14 | magicka/stamina | AV | AV map |
| sgl/gamemode_events/UpdateAppearanceAttemptEvent.cpp:25; UpdateEquipmentAttemptEvent.cpp:25 | Expose Skyrim JSON to the gamemode | APP/EQI | Per-game JSON (document in the gamemode API) |
| sgl/SweetHidePlayerNamesService.cpp:19,22 | SweetPie.esp, "Stranger" | SWP | FF |

### 1.15 Papyrus native classes (`sgl/script_classes`)

| Location | What | Cat | Proposal |
|---|---|---|---|
| PapyrusClassesFactory.cpp:33-54 | Registers 22 classes | PAP | `GP.RegisterPapyrusClasses()` |
| PapyrusActor.cpp:13-24 | ConvertToAV("health","stamina","magicka") | PAP/AV | GP catalogue (FO4 AVs are AVIF forms, passed as `ActorValue` objects [inference]) |
| PapyrusActor.cpp:73,85,112,169,182 | AV natives | PAP/AV | FO4 signatures take an ActorValue form [inference] |
| PapyrusActor.cpp:288,324,403,559,601,643,682,702,759-786 | EquipItemEx (SKSE), EquipSpell, WornHasKeyword, Add/RemoveSpell, GetRace, GetSpellCount/GetNthSpell, registrations | PAP | Skyrim class set; FO4 class set separate |
| PapyrusForm.cpp:53,147,187-291 | GetType; EDID not FULL; Skyrim Form.GetType numbering | PAP | GP form-type table |
| PapyrusGame.cpp:144,155,169,176,190,234-248 | ShowRaceMenu/ShowLimitedRaceMenu, RaceMenuHelper, maxId 0x80000000, IncrementStat, GetCameraState, ShakeController | PAP/APP | Per game |
| PapyrusObjectReference.cpp:80,216,171,267,381,407,678,753,769,848 | LIGH torch; SpSnippet "SkympHacks" AddItem/RemoveItem; EXPL unsupported; NPC_ PlaceAtMe; PlayGamebryoAnimation; DOOR; LinkedRefUtils | PAP | Mostly KEEP; snippets per client |
| PapyrusNetImmerse.cpp:82-83 | SKSE NetImmerse natives | PAP | Skyrim only |
| PapyrusBook.cpp:32,47; PapyrusPotion.cpp:21,27 | GetSpell (SKSE); IsFood | PAP | Skyrim only |
| PapyrusUtility.cpp:164-182 | Wait…, GameTimeToString :172, Alias arrays :173/:178 | PAP | KEEP mostly |
| PapyrusKeyword.cpp:43,45; PapyrusLeveledBase.cpp:40; PapyrusDebug.cpp:52-55 | GetKeyword, GetNthForm | PAP | KEEP |
| sgl/script_objects/EspmGameObject.cpp:12-289 | Record type → Skyrim Papyrus script names (ALCH :105 potion, SCRL :59, AVIF :203, WOOP :249, SHOU :251…) | PAP | `GP.PapyrusTypeNameForRecord()` |
| sgl/script_compatibility_policies/HeuristicPolicy.cpp:57-70 | Event names OnActivate, OnObjectEquipped, OnInit, OnUpdate, OnTriggerEnter/Leave, OnTrigger, OnHit | PAP | GP event list (FO4 OnItemEquipped [inference]) |
| sgl/ScriptVariablesHolder.cpp:132-194,208,229 | VMAD property types; −10 array mapping | PAP/REC | ESPM VMAD v6 (Var 6, Struct 7, arrays 16/17 [inference]) |
| sgl/SpSnippet.cpp:40-44,89,100; SpSnippetFunctionGen.cpp:53-57 | Client snippet RPC | PAP/NET | KEEP mechanism; snippet classes per client |
| sgl/ConsoleCommands.cpp:83,106,130,154,159 | 0x14 | FID | GP.PlayerRefId() |

### 1.16 Script storages, localization, misc utils

| Location | What | Cat | Proposal |
|---|---|---|---|
| sgl/script_storages/BsaArchiveScriptStorage.cpp:4,40,42 | `bsa/tes4.hpp`, `bsa::tes4::archive`, `bsa["scripts"]` | PAP/LO | Ba2ArchiveScriptStorage (rsm-bsa `bsa::fo4` [inference]) |
| sgl/script_storages/ScriptStorageUtils.cpp:23-25,36 | `.pex`; non-recursive directory_iterator | PAP | Recursive for FO4 namespaces (`Foo:Bar` → `Foo/Bar.pex`) |
| sgl/script_storages/ScriptStorageFactory.cpp:39,54-56 | dataDir/scripts; "archives" | PAP/LO | KEEP + GP storage options |
| sgl/script_storages/AssetsScriptStorage.cpp:8-16 | cmrc `standard_scripts` | PAP | Per-game resource set |
| sgl/script_storages/DirectoryScriptStorage.cpp:18-19 | `pexDir/(name + ".pex")` | PAP | Namespace → path mapping |
| sgl/localization_provider/LocalizationProvider.cpp:96,98,108,135,148 | Case-sensitive `.strings`/`.dlstrings`; "strings" dir; stem "skyrim_russian" | LO | Case-insensitive lookup (FIX); stem from load order |
| sgl/LeveledListUtils.cpp:12-13,35,54,125-201 | LVSP; global chance → 100; UseAll; EvaluateTemplateChain (TPLT only, :171/:176) | REC | ESPM fo4 (TPTA, LVLO with COED stride) |
| sgl/EvaluateTemplate.h:30,43,47 | baseTemplate; templateDataFlags (no TPTA) | REC | Same |
| sgl/GetWeightFromRecord.cpp:20-69 | Per-type weight (WEAP DATA :22) | REC/BUG | GP views (FO4 weight in DNAM) |
| sgl/LocationalDataUtils.cpp:17-19,22-27 | Rotation conversions | KEEP | |
| sgl/MovementValidation.cpp:17,22 | 4096² | REC | KEEP |

### 1.17 Server build assets

| Location | What | Cat | Proposal |
|---|---|---|---|
| falloutmp-server/standard_scripts/ (133 files) | Skyrim .pex stubs | PAP | `standard_scripts/skyrim` + `standard_scripts/fallout4` |
| falloutmp-server/CMakeLists.txt:105-111 | SKYRIM_DIR esm_prefix | LO | `GAME`/`GAME_DIR` variables |
| falloutmp-server/CMakeLists.txt:116-119 | generate_server_settings | SET | Per-game template |
| falloutmp-server/CMakeLists.txt:132-139 | cmrc standard_scripts | PAP | Per game |
| falloutmp-server/cpp/CMakeLists.txt:9-23,70 | MpClientPlugin links server_guest_lib; GLOB_RECURSE | NET | New `game_profile/` subdirectory compiles automatically |

### 1.18 TypeScript server (`sts/`)

| Location | What | Cat | Proposal |
|---|---|---|---|
| sts/settings.ts:24 | master `gateway.skymp.net` | SET | Per-game default master (or none) |
| sts/settings.ts:30-36 | startPoints Tamriel (133857,-61130,14662 / 0x3c / 72) | SET/FID | Default from `game` |
| sts/settings.ts:56 | `./skymp5-gamemode` | SET | KEEP |
| sts/manifestGen.ts:18-24,49-58 | `.bsa` derivation | LO | `.ba2` naming (`<Plugin> - Main.ba2`, `- Textures.ba2` [inference]) |
| sts/ui.ts:49,50 | `.es[mpl]` allowed, `.bsa` blocked | LO | Also block `.ba2` |
| sts/systems/spawn.ts:26-36 | createActor + setRaceMenuOpen | APP | Per game |
| sts/systems/login.ts:90 | "loginWithSkympIo" | NET | CFG |
| sts/systems/metricsSystem.ts | `skymp_*` metric names | SET | KEEP |

---

## 2. Shared libraries

### 2.1 libespm

General design (KEEP): zero-copy views over the mapped file. `RecordHeader` is 16 bytes and every record class has `static_assert(sizeof(X) == sizeof(RecordHeader))`. `GetData()` iterates fields. `Convert<T>` checks only the 4CC `T::kType` [src: espm/include/libespm/Convert.h:6-13]. A record struct for another game with the same 4CC would therefore be accepted silently, so game awareness must come from the browser/loader (§5.2).

| Location | What | Cat | Proposal |
|---|---|---|---|
| espm/include/libespm/ActorValue.h:6-173 | Skyrim AV enum (Health=24, Magicka=25, Stamina=26 …) | AV | Keep as `espm::skyrim::ActorValue`. FO4 AVs are AVIF records [ext: xEdit FO4:7826] |
| espm/include/libespm/GMST.h:14-16 | `kFCombatDistance 0x55640`, `kFMaxArmorRating 0x37DEB`, `kFArmorScalingFactor 0x21A72` (Skyrim form IDs) | FID | Look up GMSTs by EDID (upstreamable), or `GP.GmstIds()` |
| espm/src/Utils.cpp:41-48 | `IsItem` record set (no KEYM/NOTE) | REC | `GP.IsItemType()`; FO4 adds KEYM [ext: xEdit FO4:9823], NOTE, CMPO? [inference] |
| espm/src/Utils.cpp:55-61 | `kCorrectHashcode`: CRCs of the 5 Skyrim ESMs (used only by unit/EspmTest.cpp:22) | TST | Move into the test |
| espm/src/Utils.cpp:69-74 | `GetMappedId` 8-bit mod index | LO | `ESPM.esl` |
| espm/src/Utils.cpp:89-141,143-170 | `ReadPropertyValue` throws on unknown types (:139); FillScriptArray | PAP/BUG | VMAD v6: Var, Struct, struct arrays; non-throwing (B3) |
| espm/src/Utils.cpp:184-187 | CNTO/COED | REC | KEEP |
| espm/src/RecordHeader.cpp:43-60 | `GetScriptData` noexcept but may throw | BUG | FIX |
| espm/include/libespm/RecordHeader.h:71-72,92-100 | Header struct comment | REC | KEEP (FO4 header has the same 24-byte layout, form version 131 [inference]) |
| espm/include/libespm/IdMapping.h:7 | `std::array<uint8_t,256>` | LO | `ESPM.esl` (uint16 file index) |
| espm/include/libespm/TES4.h:14-16; src/TES4.cpp:13,19 | HEDR version, MAST. No game detection (Loader.cpp:15-49) | LO | `espm::Game DetectGame()` (B1). FO4 HEDR 1.0, INCC subrecord [ext: xEdit FO4:12091] |
| espm/src/Combiner.cpp:21,31,40,48-61 | 256 sources; TES4; mapping | LO | `ESPM.esl` |
| espm/include/libespm/CombineBrowser.h:59; src/CombineBrowser.cpp:27,34 | `array<Source,256>`; Skyrim comment; uint8 fileIdx | LO | `ESPM.esl` |
| espm/include/libespm/NPC_.h:22-58,75 | Skyrim TemplateFlags | REC | `espm::fo4::NPC_` with TPTA [ext: xEdit FO4:10286+] |
| espm/src/NPC_.cpp:19-21 | SNAM faction rank read at offset 0 (should be +4) | REC/BUG | FIX (changes Skyrim faction ranks, so note it in the PR) |
| espm/src/NPC_.cpp:24-34 | Skyrim ACBS offsets: flags 0x2/0x20/0x800, magicka +4, stamina +6, template flags +18, health +20 | REC | fo4 struct: flags u32 (Essential bit1, Unique bit5, Protected bit11), level u16 @6, template flags u16 @16 [ext: xEdit FO4:10286] |
| espm/src/NPC_.cpp:43,45,47 | SPLO, TPLT, INAM | REC | fo4: LTPT, TPTA, PRKR, PRPS, DNAM (calc health/AP) [ext: xEdit FO4:10286+] |
| espm/src/RACE.cpp:15-23 | Skyrim RACE DATA offsets +32..+100 (starting H/M/S, regen, unarmed damage/reach) | REC | fo4 RACE [ext: xEdit FO4:11054] (layout differs) |
| espm/include/libespm/WEAP.h:13-17,22-33,37-43; src/WEAP.cpp:14,16 | WeapData (DATA), AnimType (Skyrim), DNAM | REC/BUG | fo4 WEAP: no DATA, DNAM carries ammo, speed, reach, ranges, flags, capacity, anim type (…Gun, Grenade, Mine), weight, value, base damage u16 [ext: xEdit FO4:12877] |
| espm/src/ARMO.cpp:14,16,19,22,24,28,36; ARMO.h:19 | EITM, DATA, DNAM (throws if missing :36), ETYP, BODT/BOD2 `dataSize >= 8`, equipSlotId | REC/BUG | fo4 ARMO: DATA value/weight/health, FNAM rating u16, BOD2 u32 [ext: xEdit FO4:5786,3393] |
| espm/src/AMMO.cpp:13-14 | SSE-only weight at DATA+0x10 | REC | fo4 AMMO: DATA value u32 + weight float; DNAM projectile/flags/damage [ext: xEdit FO4:5741] |
| espm/include/libespm/MGEF.h:14-63,65,91; src/MGEF.cpp:16-23 | Skyrim EffectType, flags, primaryAV, offsets 0x40/0x44 | REC/AV | fo4 MGEF [ext: xEdit FO4:10052] (AV fields are AVIF FormIDs) |
| espm/src/COBJ.cpp:14,17,20,23,26 | CNTO, CNAM, BNAM, NAM1, CTDA | REC | fo4 COBJ: FVPA components, CNAM, BNAM, FNAM categories, INTV count/priority [ext: xEdit FO4:10266] |
| espm/src/Effects.cpp:18,32,38,49 | noexcept + throw; EFID/EFIT | BUG | FIX |
| espm/src/LeveledListBase.cpp:16-24; .h:26,28,45 | Entry stride 18 (breaks when COED follows LVLO) | REC/BUG | Iterate subrecords (FIX); fo4 LVLI [ext: xEdit FO4:10009] |
| espm/src/OTFT.cpp:14-16 | INAM list | REC | KEEP |
| espm/src/REFR.cpp:13-45; REFR.h:40-101 | NAME, DATA, XTEL, XAPD, XAPR, XLKR, FNAM, TNAM; Skyrim MapMarkerType | REC | Mostly KEEP; map marker types per game |
| espm/src/ACHR.cpp:9 | StartsDead 0x200 | REC | KEEP (FO4 ACHR flag bit 9 "Starts Dead" too [ext: xEdit FO4:4483-4485]) |
| espm/src/ALCH.cpp:18-22 | ENIT flags ("not used in Skyrim") | REC | fo4 ENIT: value, flags (Food 0x2, Medicine 0x10000), addiction [ext: xEdit FO4:5691] |
| espm/src/BOOK.cpp:25-26 | Spell-tome flags | REC | Skyrim only |
| espm/src/LIGH.cpp:18,23 | Misaligned offsets 0x0F / 0x1F | REC/BUG | FIX; FO4 LIGH DATA flags u32 "Can be Carried" 0x2 [ext: xEdit FO4:9871] |
| espm/src/FLOR.cpp:20, TREE.cpp:20 | PFIG | REC | KEEP |
| espm/src/CELL.cpp:13 | CELL DATA flags | REC | KEEP (check width) |
| espm/include/libespm/NAVM.h:14; src/Browser.cpp:267 | kType "NVNM" → navmeshes never indexed | REC/BUG | FIX ("NAVM") |
| espm/src/Browser.cpp:142,235-244 | GetRecordsByType restricted; REFR bucket /4096 | REC | KEEP |
| espm/include/libespm/Property.h:10-22 | Property types without Var/Struct | PAP | Add types 6, 7, 16, 17 [inference] |
| espm/include/libespm/SPEL.h:13-22,55 | SPEL | REC | Skyrim only (FO4 has SPEL for abilities [inference]) |
| espm/include/libespm/CTDA.h:60,66,101 | 32-byte CTDA | CND | KEEP (FO4 CTDA is also 32 bytes [inference]) |
| espm/include/libespm/WRLD.h:103,106 | Tamriel comments | - | cosmetic |

### 2.2 papyrus-vm

| Location | What | Cat | Proposal |
|---|---|---|---|
| pvm/src/Reader.cpp:70-77 | FillHeader without validation (Signature :72, GameID :75) | PAP | Validate the magic and branch on GameID (1 = Skyrim, 2 = FO4 [inference]) |
| pvm/src/Reader.cpp:85 | Pops ".psc" source extension | PAP | KEEP |
| pvm/src/Reader.cpp:165-208 | Object table without structs/const flag | PAP | FO4: struct table, const flag [inference] |
| pvm/src/Reader.cpp:233-268 | VarValue types | PAP | Add Var/Struct/struct arrays |
| pvm/src/Reader.cpp:347-350 | Unchecked `numArgumentsForOpcodes[opcode]` | BUG | FIX bounds check |
| pvm/src/Reader.cpp:420-448 | Big-endian reads | PAP | Endianness by header (FO4 little-endian [inference]) |
| pvm/include/papyrus-vm/Reader.h:12 | Table of 36 opcodes | PAP | FO4 opcodes 0x24-0x2E (is/struct/array ops) [inference] |
| pvm/include/papyrus-vm/ScriptHeader.h:8-11 | FA57C0DE, 3.1, GameID 1 | PAP | KEEP; add FO4 constants |
| pvm/include/papyrus-vm/OpcodesImplementation.h:8-43 | Opcode enum | PAP | Extend |
| pvm/include/papyrus-vm/VarValue.h:38-46 | No Struct/Var | PAP | Extend |
| pvm/src/ActivePexInstance.cpp:265,343,660,744 | Default cases; "Skyrim LE" comment | PAP | Extend |
| pvm/src/VirtualMachine.cpp:19,30 | Scripts keyed by source name | PAP | Namespaced names (`Foo:Bar`) |

### 2.3 viet, serialization, savefile

No game coupling found. viet and serialization only use `FormDescType` templates. KEEP.
`savefile` is used only by skyrim-platform [src: skyrim-platform/src/platform_se/CMakeLists.txt:96]. It stays out of the FO4 platform fork (FO4 saves differ [inference]).

### 2.4 unit tests (`unit/`)

#### 2.4.1 Data loading and constants

| Location | What | Cat | Proposal |
|---|---|---|---|
| unit/main.cpp:19-55 | Loads 5 Skyrim ESMs; adds `~[espm]` automatically when Skyrim.esm is missing | TST | Parameterise by `UNIT_GAME`; skip by tag `[espm]`/`[fo4data]` |
| unit/TestUtils.hpp:13-18 | `kWhiterunCell 0x1a26f`, `kBarrelInWhiterun 0x4cc2d`, `kTamriel 0x3c` | TST/FID | Keep under `namespace SkyrimTestConstants` |
| unit/TestUtils.hpp:38-53,55-72 | jMovement (0x3c); jAppearance (19 options, 4 presets) | TST | Keep; FO4 equivalents later |
| unit/TestUtils.cpp:189-193 | `GetDataDir`: UNIT_DATA_DIR or SKYRIM_DIR/Data | TST | Per-game data dir |
| unit/PartOne_ActivateTest.cpp:28-40 | `GetPartOne()` attaches `GetEspmLoader` | TST | Split: `GetPartOneNoData()` vs `GetPartOneWithData()` |
| unit/CMakeLists.txt:11-16,41-43,49-55 | PapyrusCompiler via SKYRIM_DIR; SKYRIM_SE define | TST | Per game |

#### 2.4.2 `[espm]`-tagged tests (need Skyrim data)

| File | Count | Lines / notes |
|---|---|---|
| unit/ConsoleCommandTest.cpp | 2 | 79, 118 |
| unit/CraftTest.cpp | 5 | 11, 59, 109, 155, 172 (21 Skyrim IDs) |
| unit/EspmTest.cpp | 29 | 103 hex IDs; CRC check :22 |
| unit/LeveledListUtilsTest.cpp | 9 | 20 Skyrim IDs |
| unit/NpcExists*.cpp | 6 | |
| unit/PapyrusActorTest.cpp | 2 | 8, 41 |
| unit/PapyrusCompatibilityTest.cpp | 5 | |
| unit/PapyrusFormListTest.cpp | 1 | 9 |
| unit/PapyrusGameTest.cpp | 2 | 11, 29 |
| unit/PapyrusObjectReferenceTest.cpp | 4 | 64, 83, 118, 146 |
| unit/PartOne_ActivateTest.cpp | 16 | 19 Skyrim IDs |
| unit/PrimitiveTest.cpp | 2 | 17, 34 |
| platform_lib_tests/FileInfoTest.cpp | 1 | :12, Windows-only |
| **Total** | **84** in 13 files (83 in 12 on Linux) | |

#### 2.4.3 Untagged tests that still use Skyrim data or IDs [inference: they call `GetPartOne()` or use Skyrim IDs]

ActivateParentTest (9, 52; 0x54b15, 0x39fe4), AnimationSystemTest (8; SweetPie), ChangeValuesTest (16, 46, 130, 168), ConsoleCommandTest (11, 55), CropRegenerationTest (89), DropItemTest (10), GetBaseActorValuesTest (11), HealthRestorationTest (9), HitTest (22, 57, 95, 147, 187), LoadCellsTest (8, 15, 31; 0x04000800), MovementValidationTest (11, 30, 57), PapyrusActorTest (74), PickUpItemCountTest (12), RespawnTest (7, 36), TES5DamageFormulaTest (28, 52, 101; 0x1397e iron dagger, 0x12e46/0x12e4b/0x12e4d/0x13745/0x1f4), TemplateInventoryTest (9), TemplateScriptTest (29), WorldStateTest (122, 148), SweetHidePlayerNamesTest, DistContentsTest (71-74 SkyrimSE/AE tags; 129-131 Skyrim ESMs). Fixture data: `unit/papyrus_test_files/standard_scripts` (Skyrim pex).

Action (step A1): run each of these with Skyrim data absent. Add `[espm]` (or a new `[skyrimdata]` tag) to those that fail, so `./unit/unit "~[espm]"` becomes a real no-data subset.

---

## 3. Client side

### 3.1 skymp5-client/src (per file)

Levels: None, Low (wire mirrors and generic plumbing), Medium (some Skyrim IDs/APIs), High (the logic is Skyrim gameplay).

| File | Level | Coupled items |
|---|---|---|
| cl/index.ts | Medium | INI/GMST tweaks :61-63 (bAlwaysActive:General, iDeathDropWeaponChance, fAutoVanityModeDelay:Camera); Sweet services :30-58, :92-98, :118 |
| cl/sync/movementGet.ts | High | Graph vars SpeedSampled/Direction/bInJumpState/IsBlocking/IsSneaking; "health"/"CarryWeight" AVs; TESModPlatform.isPlayerRunningEnabled; 0x14 |
| cl/sync/movementApply.ts | High | SprintStart/Stop, BlockStart/Stop, SneakStart/Stop, Variable10 ragdoll, health AV, TESModPlatform.setWeaponDrawnMode/moveRefrToPosition, cellWidth 4096 (:218) |
| cl/sync/animation.ts | High | Skyrim chair/idle lists :36-96, SkympFakeEquip/Unequip, Ragdoll/GetUpBegin, OffsetBoundStandingPlayerInstant, OffsetCarryBasketStart, ignoredAnims :296-304 |
| cl/sync/appearance.ts | High | 19 morphs, 4 presets, tint masks, warpaint regex, headparts, silentVoiceTypeId 0x2f7c3 (:124), TESModPlatform createNpc/setNpc*/pushTintMask/setFormIdUnsafe |
| cl/sync/equipment.ts | High | Spell slots; isBadMenuShown :128-135 (InventoryMenu, FavoritesMenu, MagicMenu, ContainerMenu, Crafting Menu) |
| cl/sync/inventory.ts | High | Skyrim ExtraData types (Health, Count, Enchantment, Poison, Soul, TextDisplayData, Charge); addItemEx/pushWornState/resetContainer |
| cl/sync/actorvalues.ts | High | H/S/M |
| cl/sync/spell.ts | High | getSpellCount/getNthSpell |
| cl/view/formView.ts | High | Creature races :222-253 (draugr 0xd53, falmer 0x131f4 … wolf 0x1320a), Aggression, attackDamageMult :288, startDeferredKill, health/magicka 1e6 :299-303, "NPC Head [Head]" :499-559, iSize W/H:Display :28-29, NetImmerse |
| cl/view/modelApplyUtils.ts | Medium | caveGSecretDoor01 0x6f703, parentActivator 0x460ca |
| cl/extensions/objectReferenceEx.ts; formTypeEx.ts | Medium | 0x6f703 (:44); Skyrim item FormTypes |
| cl/services/services/worldCleanerService.ts | Medium | Chicken race 0xa919d (:81) |
| cl/services/services/timeService.ts | Medium | Globals GameYear 0x35, Month 0x36, Day 0x37, Hour 0x38, TimeScale 0x3a (:25-29) |
| cl/services/services/spSnippetService.ts | Medium | SkympHacks sounds 0x334ab/0x14115, gold 0xf, showracemenu |
| cl/services/services/remoteServer.ts | High | Iron helmet 0x12e4d (:94), ContainerMenu, H/M/S props :540-549, showRaceMenu :854, mod enumeration :576-577, quitToMainMenu |
| cl/services/services/sendInputsService.ts | Medium | 'RaceSex Menu' :240, IsCastingRight/Left/Dual :44-46, H/S/M |
| cl/services/services/deathService.ts | Medium | staggerStart / iGetUpType |
| cl/services/services/hitService.ts | Medium | Spell/Scroll sources |
| cl/services/services/magicSyncService.ts | High | Whole magic sync (mlh/mrh_equipped_event) |
| cl/services/services/playerBowShotService.ts | High | attackRelease/crossbowAttackStart |
| cl/services/services/ragdollService.ts | Medium | fDiffMultHPToPC* GMSTs |
| cl/services/services/singlePlayerService.ts, enforceLimitationsService.ts | Medium | setInChargen |
| cl/services/services/disableSkillAdvanceService.ts | High | fXPPerSkillRank + Skyrim skill AVs |
| cl/services/services/disableFastTravelService.ts, disableDifficultySelectionService.ts | Medium | iDifficulty:GamePlay |
| cl/services/services/browserService.ts | Medium | Skyrim Menu enum list |
| cl/services/services/authService.ts | Medium | skymp.net URLs, window.skyrimPlatform |
| cl/services/services/settingsService.ts | Low | gateway.skymp.net, "skymp5-client" settings section |
| cl/services/services/loadOrderVerificationService.ts | Medium | getModCount/getModName (no light mods) |
| cl/services/services/spVersionCheckService.ts, cl/version.ts | Low | MessageBoxMenu |
| cl/services/services/keyboardEventsService.ts | Low | DxScanCode from @skyrim-platform |
| cl/services/services/gamemodeUpdateService.ts, gamemodeEventSourceService.ts | Medium | Expose skyrimPlatform API to server gamemode code |
| cl/services/services/loadGameService.ts | Medium | sp.loadGame (.ess) |
| cl/services/services/sweet*.ts | High (SWP) | SweetPie-only (perk/skill IDs). Do not port |
| cl/services/messages/*.ts | Low | Mirror the server wire schema |
| Other generic plumbing (networking, storage, logging) | None/Low | |

TESModPlatform natives used (each needs a fallout4-platform equivalent): setWeaponDrawnMode, moveRefrToPosition, addItemEx, setNpcSkinColor/Sex/Race/HairColor, setFormIdUnsafe, resizeHeadpartsArray, resetContainer, pushWornState, pushTintMask, isPlayerRunningEnabled, getSkinColor, evaluateLeveledNpc, createNpc, clearTintMasks. Platform APIs also used: sp.loadGame, setInventory, hooks.sendAnimationEvent, browser, win32, mpClientPlugin, storage, settings, createText, getAnimationVariablesFromActor/applyAnimationVariablesToActor, castSpellImmediate.

Build files: skymp5-client/skyrim.json (skyrimFolder); webpack.config.js:17-40 (SKYRIMPATH/Data/Platform/PluginsDev); package.json (@skyrim-platform/skyrim-platform 2.8.0); CMakeLists.txt:14,29 (dist/client/Data/Platform/Plugins).

Files not classified above, because they were not inspected line by line (level is [inference], probably Low to Medium): activationService, animDebugService, blockPapyrusEventsService, blockedAnimationsService (Skyrim anim event names), consoleCommandsService, containersService (ContainerMenu), craftService (Crafting Menu), dropItemService, lastInvService, netInfoService, networkingService (None), profilingService, serverJsVerificationService, skympClient, timersService, frontHotReloadService, sync/movement.ts, view/{formViewArray,hostAttempts,model,playerCharacterDataHolder,spawnProcess,worldView,worldViewMisc}.ts, features/, lib/, messages_*/.

### 3.2 skymp5-front

Skyrim-themed components: SkyrimFrame, SkyrimButton, SkyrimHint, SkyrimInput, SkyrimSlider, FrameButton (image), features/skillsMenu (Russian RP professions, Skyrim ores), testMenu, constructorComponents/chat/dices/skillDices (Skyrim magic schools and weapons), sounds (Magic/Vampire/Werewolf/Sword/Shield…), locales/en.json:5 "travel to skyrim". The `window.skyrimPlatform` bridge is in index.js:13-41 and App.js:46-82. With `BUILD_FRONT`, CMakeLists.txt:1-40 downloads skyrim-multiplayer/skymp5-front from GitHub. Fork into falloutmp-front: keep the bridge (renamed `window.falloutPlatform` with an alias) and chat; restyle the rest.

### 3.3 skymp5-functions-lib

index.ts is gamemode glue that imports the private skymp5-gamemode sources (SweetPie listeners). It contains form IDs (:196-253) and Skyrim `cellOrWorldDesc` strings ('16dfe:Skyrim.esm' etc., :376-461). Its CMakeLists downloads skyrim-multiplayer/skymp5-gamemode (`BUILD_GAMEMODE`). It is SweetPie/Skyrim-specific end to end: do not port it, and write a new FalloutMP gamemode. FF.

---

## 4. Build, CI, docs coupling

| Location | What | Proposal |
|---|---|---|
| CMakeLists.txt:46-56 | Skyrim SE messages | `GAME` option (skyrim / fallout4) |
| CMakeLists.txt:59-72 | SKYRIM_VR, vcpkg features skyrim-vr/skyrim-flatrim | Keep; add `fallout4` feature later |
| CMakeLists.txt:92 | BUILD_SKYRIM_PLATFORM | Add BUILD_FALLOUT4_PLATFORM |
| CMakeLists.txt:115-131 | UNIT_DATA_DIR / DOWNLOAD_SKYRIM_DATA | Per-game data var |
| CMakeLists.txt:117,133-142 | SKYRIM_DIR + SkyrimSE.exe check | GAME_DIR + exe per game (Fallout4.exe) |
| CMakeLists.txt:215 | prepare_nexus_archives deps | Skyrim only |
| CMakeLists.txt:244-251 | install_client_dist to SKYRIM_DIR | GAME_DIR |
| CMakeLists.txt:253-293 | Integration tests (misc/tests) | Per-game test list |
| cmake/download_skyrim_data.cmake:6-13 | GitLab pospelov/se-data, 5 ESMs | Skyrim only. FO4 data cannot be redistributed: use a private CI artifact |
| cmake/scripts/generate_server_settings.cmake:10-15,27 | Load order; gateway.skymp.net | Per-game template |
| cmake/scripts/generate_client_settings.cmake:26 | gateway | Per game |
| cmake/add_papyrus_library_ck.cmake:25 | TESV_Papyrus_Flags.flg | Institute_Papyrus_Flags.flg for FO4 [inference] |
| cmake/prepare_nexus_archives.cmake:12 | Skyrim exclusions (SkyrimSoulsRE, TIF__000361DF.pex, QF_MQ201_00035D5F.pex, DLC2SummonDremoraMerchantScript.pex, playerbookshelfcontainerscript.pex, uimenubase.pex) | Skyrim only |
| vcpkg.json:3-5 | Name/description Skyrim | cosmetic |
| vcpkg.json:33-35 | rsm-bsa | KEEP (BA2 via `bsa::fo4` [inference]) |
| vcpkg.json:52-55 | nirnlab | Skyrim client only |
| vcpkg.json:70-88 | Features skyrim-flatrim / skyrim-vr | Add `fallout4` (commonlibf4) for the client |
| overlay_ports/commonlibsse-ng-flatrim, -vr | Skyrim client | Add a commonlibf4 port |
| overlay_ports/commonlibae, commonlibse | Unused; descriptions wrongly say F4SE | cleanup |
| overlay_ports/rsm-bsa (4.1.0) | BSA/BA2 | KEEP |
| client-deps/ | SE/AE SKSE version bins; AE Skyrim script overrides; CombatAlertOverlayMenu.swf; common MpClientPlugin.pex/fonts; CMakeLists SKYRIM_SE switch, depends on skyrim-platform | Fork into client-deps-fo4 |
| skymp5-scripts/ | Compiled with the Skyrim PapyrusCompiler | Per game |
| .github/workflows/linux-build-base.yml:135-165,178,229 | Downloads ESMs; UNIT_DATA_DIR; ctest | Add a FO4 job without data (no-data subset) |
| .github/actions/pr_base/action.yml:6-11,256-257,263,360 | DOWNLOAD_SKYRIM_DATA, SKYRIM_VR, ctest | Same |
| .github/workflows/pr-windows-flatrim.yml:32-33; pr-windows-skyrimvr.yml:1,32-33 | Skyrim client builds | Add pr-windows-fo4 later |
| .github/workflows/sp-release.yml; trigger-sp-types-update.yml:15 | skyrimPlatform.ts types | Per platform |
| .github/workflows/deploy*.yml | skymp5-installer-v2 | Per game |
| misc/tests/test_crash | "3c:Skyrim.esm" | Skyrim-data test |
| misc/tests/test_dlc1chauruscocoonscript_stack_overflow | DLC1ChaurusCocoonScript | Skyrim |
| misc/tests/test_factions | 0x15A6E/0x3E096/"13:Skyrim.esm" | Skyrim |
| misc/tests/test_findclosestreferenceoftypefromref | 0x4cc2d | Skyrim |
| misc/tests/test_isdead | 0xDC558 | Skyrim |
| misc/tests/test_onPapyrusEvent_OnItemAdded | 0xF / 0x3c | Skyrim |
| misc/tests/test_onactivate | 0x3c | Skyrim |
| misc/tests/test_partial_location_save | 0xDC558, "37ee0:Skyrim.esm", dc558_Skyrim.esm.json | Skyrim |
| misc/tests/test_registerforsingleupdate | - | Skyrim scripts |
| misc/tests/test_removeallitems | 0xf / 0x12eb7 | Skyrim |
| misc/tests/test_spawnpoint_of_placed_actors | 0x4cc2d / 0x23abe | Skyrim |
| misc/tests/*.settings.json | npcEnabled | KEEP |

Docs with Skyrim references: docs_skyrim_platform.md (25), docs_server_configuration_reference.md (15: loadOrder :115-131, archives "Skyrim - Misc.bsa" :137-149, reloot UESP :192-225, startPoints :243, sweetPie* :297-342, npcSettings :358-377), docs_deploy (10), docs_running_a_server (6), docs_project_history (6), docs_onhit_and_damage (4). Add FalloutMP docs under `docs/falloutmp/` and leave upstream docs untouched (this avoids merge conflicts).

---

## 5. Proposed interfaces

### 5.1 Server `GameProfile` (C++)

#### Location and wiring [inference: design]

```
falloutmp-server/cpp/server_guest_lib/game_profile/
  GameId.h                      enum class GameId { Skyrim, Fallout4 }; ToString/FromString
  GameProfile.h                 abstract interface (below)
  GameProfileData.h             plain data structs (ActorValueDesc, AnimationCost, ...)
  GameProfileFactory.h/.cpp     std::shared_ptr<GameProfile> Create(GameId)
  skyrim/SkyrimGameProfile.h/.cpp   moves today's constants here, byte for byte
  fallout4/Fallout4GameProfile.h/.cpp   added in phase D
```

- `server_guest_lib` uses GLOB_RECURSE [src: falloutmp-server/cpp/CMakeLists.txt:70], so the new directory needs no CMake edits.
- `WorldState` gets `void SetGameProfile(std::shared_ptr<GameProfile>)` and `const GameProfile& GetGameProfile() const`. The member defaults to `SkyrimGameProfile`, so all existing unit tests (which build `WorldState`/`PartOne` directly) keep working unchanged.
- `ScampServer` reads `serverSettings["game"]` (default `"skyrim"`) before `AttachEspm` (addon/ScampServer.cpp:315). After loading, it compares that value with `espm::CombineBrowser::GetGame()` (§5.2) and refuses to start on a mismatch.
- The persisted database stores `game` in its metadata (§0.6 item 7).
- Gamemode JS gets a read-only `mp.getGame()` [inference].

#### Interface sketch

```cpp
// game_profile/GameProfile.h
#pragma once
#include "FormDesc.h"
#include "NiPoint3.h"
#include "condition_functions/ConditionFunctionMap.h"
#include "formulas/IDamageFormula.h"
#include "game_profile/GameId.h"
#include "papyrus-vm/VarValue.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>

namespace espm { class CombineBrowser; class LookupResult; }
class VirtualMachine; class IPapyrusCompatibilityPolicy; class IPapyrusClassBase;
class MpActor; class Inventory;

struct ActorValueDesc {
  uint32_t id;                    // Skyrim: espm::ActorValue int; FO4: AVIF global FormID
  std::string name;               // "health", "magicka", "actionPoints" ... (gamemode/JSON key)
  bool synced;                    // part of ChangeValues / percentages
  bool hasPercentage;             // health/magicka/stamina in Skyrim
  std::optional<uint32_t> regenRateAvId, regenMultAvId;
};

struct AnimationCost {            // AnimationSystem.cpp:83-221 as data
  std::string eventName; std::string avName; float cost; bool sweetPieOnly;
};

struct HitValidationParams {      // ActionListener.cpp:857-992
  uint32_t unarmedWeaponId;       // 0x1F4 Skyrim
  float boundsPatch;              // 15
  float defaultReachGmst;         // fCombatDistance fallback
  bool allowRanged;               // FO4: true; ranged checks use DNAM max range
};

struct SpawnPoint { NiPoint3 pos; NiPoint3 rot; FormDesc worldOrCell; };

class GameProfile {
public:
  virtual ~GameProfile() = default;

  // identity / data
  virtual GameId GetGameId() const = 0;
  virtual std::vector<std::string> DefaultLoadOrder() const = 0;     // ScampServer.cpp:315-321
  virtual FormDesc DefaultWorldspace() const = 0;                    // FormDesc.cpp:100-104
  virtual SpawnPoint DefaultSpawnPoint() const = 0;                  // MpChangeForms.h:105-107
  virtual std::vector<std::pair<std::string,std::string>> LegacyFormDescAliases() const = 0; // FormDesc.cpp:50-52
  virtual std::string ProtocolPrefix() const = 0;                    // Config.h:8,12 ("7_")
  virtual std::string DefaultPlayerName() const = 0;                 // PartOne.cpp:293

  // well-known forms
  virtual uint32_t PlayerRefId() const = 0;                          // 0x14
  virtual uint32_t PlayerBaseId() const = 0;                         // 0x7
  virtual uint32_t CurrencyBaseId() const = 0;                       // 0xF Gold001 / Caps001
  virtual std::optional<uint32_t> CrimeFactionListId() const = 0;    // 0x26953
  virtual std::vector<uint32_t> BannedNpcRaceIds() const = 0;        // WorldState.h:256-266
  virtual bool IsVanillaForm(uint32_t globalId, const espm::CombineBrowser&) const = 0; // MpObjectReference.cpp:1788

  // actor values
  virtual const std::vector<ActorValueDesc>& ActorValueCatalogue() const = 0;
  virtual std::vector<uint32_t> SyncedActorValues() const = 0;       // MpActor.cpp:702
  virtual nlohmann::json ComputeBaseActorValues(const espm::LookupResult& npc,
                                                uint32_t raceId) const = 0; // GetBaseActorValues.cpp

  // combat
  virtual std::unique_ptr<IDamageFormula> CreateDamageFormula(
    const nlohmann::json& serverSettings) const = 0;                 // ScampServer.cpp:361-388
  virtual HitValidationParams HitValidation() const = 0;
  virtual const std::vector<AnimationCost>& AnimationTable() const = 0;
  virtual std::vector<std::string> AnimationVariableNames() const = 0; // MpObjectReference.cpp:317-323

  // equipment / inventory
  virtual uint32_t ShieldSlotMask() const = 0;                       // 0x200 vs 0x20000000
  virtual bool IsItemType(std::string_view recordType) const = 0;    // espm Utils.cpp:41-48
  virtual bool ValidateEquipment(const MpActor&, const nlohmann::json& equipment,
                                 std::string* error) const = 0;      // ActionListener.cpp:230-432
  virtual nlohmann::json InventoryExtraDataSchema() const = 0;       // Inventory.h:37-47

  // world rules
  virtual bool IsSyncedRecordType(std::string_view recordType) const = 0; // WorldState.cpp:392-409
  virtual bool ShouldSpawnNpc(const espm::LookupResult& achrBase) const = 0; // WorldState.cpp:472,526
  virtual std::vector<std::pair<std::string,uint32_t>> DefaultRelootTimes() const = 0; // MpObjectReference.cpp:289-307
  virtual float ActivationReach(std::string_view baseType) const = 0; // 512 / 256
  struct CraftingRules { std::vector<uint32_t> excludedBenchKeywords; bool usesComponents; };
  virtual CraftingRules Crafting() const = 0;                        // CraftService.cpp:84-101

  // conditions / papyrus
  virtual ConditionFunctionMap CreateConditionFunctions() const = 0;
  virtual std::vector<std::unique_ptr<IPapyrusClassBase>> RegisterPapyrusClasses(
    VirtualMachine&, const std::shared_ptr<IPapyrusCompatibilityPolicy>&) const = 0;
  virtual std::string PapyrusTypeNameForRecord(std::string_view recordType) const = 0; // EspmGameObject.cpp
  virtual std::vector<std::string> DeniedScripts() const = 0;        // MpObjectReference.cpp:1813-1814
  virtual std::vector<std::string> StandardScriptResourcePrefix() const = 0; // cmrc set
  struct ScriptStorageOptions { bool ba2; bool recursive; std::string strictExt = ".pex"; };
  virtual ScriptStorageOptions ScriptStorage() const = 0;
  virtual std::vector<VarValue> MakeOnHitArgs(/* aggressor, source, projectile, flags */) const = 0;
};
```

Rules for the implementer:
- Step A3 adds the interface with a minimal set of methods. Each later step adds the methods it needs. Do not land unused virtuals.
- `SkyrimGameProfile` must return exactly the current literals. Name each constant after its source line in a comment.
- SweetPie behaviour stays outside the profile. Gate it with `gamemodeHacks.sweetPie` (default: auto-detect `SweetPie.esp` as today), so the Skyrim server is byte-for-byte identical.

#### Settings

```jsonc
{
  "game": "skyrim",            // or "fallout4"; default "skyrim"
  "loadOrder": [...],          // default GP.DefaultLoadOrder()
  "startPoints": [...],        // default GP.DefaultSpawnPoint()
  "reloot": {...},             // merged over GP.DefaultRelootTimes()
  "gamemodeHacks": { "sweetPie": "auto" }
}
```

### 5.2 libespm: game-aware parsing

1. **`espm::Game` enum** in `libespm/include/libespm/Game.h`: `{ Unknown, SkyrimSE, SkyrimLE, Fallout4 }`.
2. **Detection** in `Loader`/`TES4` [inference: heuristic]:
   - Order of evidence: (a) the first master's name or the file name (Skyrim.esm / Fallout4.esm); (b) the HEDR version float (Skyrim SE 1.7 / 1.71, LE 0.94, FO4 1.0, FO4 VR 0.95); (c) the record-header form version (SSE 44, FO4 131); (d) the presence of the TES4 `INCC` subrecord (FO4 only [ext: xEdit FO4:12091]).
   - Expose `CombineBrowser::GetGame()`. Throw on mixed games.
3. **Record namespaces.**
   - Keep the existing classes as the Skyrim implementations. Leave them in `espm::` for upstream compatibility, add `using namespace` aliases `espm::skyrim::X`, and give each class `static constexpr Game kGame = Game::SkyrimSE`.
   - Add `libespm/include/libespm/fo4/{WEAP,ARMO,AMMO,NPC_,RACE,COBJ,ALCH,MGEF,LVLI,LIGH,AVIF}.h` and `libespm/src/fo4/*.cpp`, each `struct espm::fo4::WEAP : RecordHeader` with `static constexpr char kType[] = "WEAP"` and `kGame = Fallout4`, plus the same `static_assert(sizeof == sizeof(RecordHeader))`. This keeps the zero-copy design.
   - `Convert<T>(LookupResult)` checks `T::kGame` against the browser's game, with an overload that keeps the current behaviour for records without `kGame`.
4. **Game-neutral views** (`libespm/include/libespm/views/*.h`). These are small value structs, for example `views::Weapon { float damage; float reach; float weight; uint32_t value; uint32_t ammo; AnimKind kind; }`, filled by `views::GetWeapon(LookupResult, cache)`, which dispatches on the game. Server code (TES5DamageFormula, EquipBestWeapon, GetWeightFromRecord, GetReach) moves to views, which also removes the null-deref class of bugs.
5. **Light plugins (ESL).** Support the FE-prefix form IDs (`FE xxx yyy`): `IdMapping` becomes uint16 with more than 256 sources, `FormDesc` and `WorldState` `>>24` call sites use a `espm::GetFileIndex(globalId)` helper. This also matters for SSE, so it is upstreamable.
6. **VMAD.** Version 6 property types (Var 6, Struct 7, arrays 16/17 [inference]); FO4 object format 2 handles. Never throw from `noexcept` functions.
7. **Look up by EDID** (GMST, FLST) instead of hard-coded Skyrim form IDs. Upstreamable.
8. **BA2.** rsm-bsa supports `fo4` archives [inference]. Add `Ba2ArchiveScriptStorage` server side (not in libespm).

### 5.3 Message schema evolution

**Principle.** Freeze the Skyrim wire format: MsgTypes 1..33 and protocol "7_" stay byte-identical. Add FO4 variants as new types, not as optional fields. In the BitStream archive even an optional field changes the byte layout [src: serialization/include/archives/BitStreamOutputArchive.h:88-101].

| Message | Skyrim (unchanged) | FO4 plan |
|---|---|---|
| CreateActor | CreateActorMessage (props with H/M/S, learnedSpells, isRaceMenuOpen, appearance, equipment) | `CreateActorFo4` = `CreateActorT<Fo4Appearance, Fo4Equipment, AvMap>` |
| UpdateAppearance / SetRaceMenuOpen | Appearance (19 morphs, 4 presets, tints) | `UpdateAppearanceFo4` (sliders map, morph regions, tint entries, head parts, hair color); SetRaceMenuOpen reused (neutral semantics) |
| UpdateEquipment | Equipment with spell slots | `UpdateEquipmentFo4` (inventory + worn biped mask, no spells) |
| SetInventory, PutItem, TakeItem | Inventory::ExtraData (Skyrim) | `...Fo4` with `Fo4ExtraData` (OMOD list, health, name, legendary) |
| ChangeValues, DeathStateContainer | health/magicka/stamina | `ChangeValuesAv` with `std::vector<std::pair<uint32_t,float>>` (AV id → percentage) |
| UpdateMovement | runMode string + 5 bools | `UpdateMovementFo4` (flags bitfield: sneaking, sprinting, aiming/iron sights, weapon drawn, jumping, in power armor) |
| PlayerBowShot, SpellCast | Skyrim | `WeaponFire` (weaponId, ammoId, shotCount, aim direction); SpellCast unused |
| Hit | HitMessage | Reuse; add `HitFo4` only if limb or VATS data is needed |
| All others (Activate, Teleport, OpenContainer, CustomPacket, CraftItem, DropItem, ...) | - | Reused unchanged (game-neutral) |

Mechanics:
- Reserve MsgType 64..95 for FO4 twins. All values must stay below 123 (`{` means a JSON packet [src: msg/MessageSerializerFactory.cpp:178-179]).
- `MessageSerializerFactory` gets per-game registration (`CreateMessageSerializer(GameId)`), so a Skyrim server never decodes FO4 types and the reverse. The dispatch vectors stay sized by `MsgType::Max` (raise Max).
- Template the payload structs (`CreateActorT<...>`) so serialisation code is shared. In-memory domain types can be supersets, because JSON persistence omits `nullopt` [src: serialization/include/archives/JsonOutputArchive.h:38-48] and the JSON reader tolerates missing keys [src: serialization/include/archives/JsonInputArchive.h:69-81].
- **Protocol version.** `GP.ProtocolPrefix()` returns "7_" (Skyrim) or "fo4-1_" (FO4). Bump only the FO4 counter while it is unstable. The client plugin needs the prefix too: either a compile definition per client build (`MP_PROTOCOL_PREFIX`) or `CreateClientEx(host, port, prefix)` next to the existing `CreateClient` [src: falloutmp-server/cpp/client/main.cpp:50-54]. A Skyrim client then cannot connect to a FO4 server, because the RakNet password differs.
- The gamemode JSON shapes (`appearance`, `equipment`, `inventory`, `percentages`) are per game. Document them in `docs/falloutmp/`.

---

## 6. Sequenced refactor plan

Rules:
- Every step builds, and `ctest --verbose` passes with Skyrim data in CI (linux-build-base.yml).
- Locally, use the no-data subset `./unit/unit "~[espm]"` (valid after A1) plus focused tags (for example `./unit/unit [FormDesc]`, `./unit/unit [Respawn]`).
- Skyrim behaviour stays identical unless a step is marked "behaviour change".
- "Up" means the step is a candidate for upstreaming to SkyMP.
- Local caveat: the existing `/home/user/falloutmp/build` configure failed at the vcpkg port rsm-binary-io (clang 18). Use the repo's documented build (`build.sh`, Docker) or CI.

| Step | Goal | Files | Risk | Verification | Up |
|---|---|---|---|---|---|
| A0 | Baseline: record the test list and pass/fail with and without data | none | - | `./unit/unit --list-tests`; `ctest` | - |
| A1 | Tag data-dependent tests honestly | unit/* from §2.4.3 | Low | With Skyrim data absent, `./unit/unit "~[espm]"` is green | Up |
| A2 | Crash fixes for foreign data: null checks (WEAP DATA, CrimeFactionsList, SweetPie `.rec`), noexcept-throw (Effects, GetScriptData), NAVM 4CC, LIGH offsets, Reader opcode bound, LVLO iteration, case-insensitive strings | espm/src/{WEAP,ARMO,Effects,RecordHeader,LIGH,LeveledListBase}.cpp, NAVM.h, sgl/WorldState.cpp, MpActor.cpp, TES5DamageFormula.cpp, pvm/src/Reader.cpp, LocalizationProvider.cpp | Low to Medium | Existing `[espm]` tests; add a test per fix. NAVM makes navmeshes visible to `GetRecordsByType` (verify nothing iterates them unexpectedly) | Up |
| A2b | Behaviour-changing fixes, in separate PRs: SNAM rank offset, npcSettings `default`, stamina crop, temper keyword ToGlobalId | NPC_.cpp, ScampServer.cpp, ActionListener.cpp, CraftService.cpp | Medium (behaviour change) | New tests; release notes | Up |
| A3 | GameProfile skeleton: GameId, GameProfile with only `GetGameId`, `DefaultLoadOrder`, `DefaultWorldspace`, `ProtocolPrefix`; SkyrimGameProfile; factory; `WorldState::GetGameProfile()`; `"game"` setting | sgl/game_profile/*, WorldState.h/.cpp, addon/ScampServer.cpp:315-347 | Low | New `[GameProfile]` tests; all tests unchanged | maybe |
| A4 | Move well-known IDs and defaults: 0x14, 0x7, 0xF, 0x1F4, 0x3c, spawn point, banned races, crime list, reloot defaults, reach, "Prisoner", vanilla-form threshold (computed from load order; same result for 5 masters) | §1.4, §1.8-1.11 rows marked FID/SET | Low | Full suite; grep that no `0x14`/`0x3c` literal remains outside the profile | - |
| A5 | AV catalogue: route H/M/S through `ActorValueCatalogue()`; keep ActorValues struct and JSON keys | ActorValues.*, MpActor.cpp, PercentagesBinding, CropRegeneration, GetBaseActorValues, ActiveMagicEffectsMap | Medium | ChangeValuesTest, HealthRestorationTest, CropRegenerationTest, RespawnTest | - |
| A6 | Damage formula factory and hit validation params | ScampServer.cpp:361-388, ActionListener.cpp:829-1004 | Low | HitTest, TES5DamageFormulaTest | - |
| A7 | Animation table as data; SweetPie flag `gamemodeHacks.sweetPie` (auto) | AnimationSystem.*, MpActor.h:178-183, SweetPie formulas/services | Low | AnimationSystemTest; SweetPie behaviour unchanged with auto | Up (flag) |
| A8 | Equipment/inventory: ShieldSlotMask, IsItemType, ValidateEquipment, extra-data schema (Skyrim identical) | MpActor.cpp:2051-2073, ActionListener.cpp:230-432, Inventory.* | Medium | Equipment/inventory tests, PartOne_ActivateTest | - |
| A9 | Condition function table from profile | ConditionFunctionFactory, Condition.cpp:74-88, ScampServer.cpp:357-359 | Low | CraftTest, condition tests | - |
| A10 | Papyrus: class registration, record→script-name map, event names/OnHit args, denied scripts, storage options | PapyrusClassesFactory, EspmGameObject.cpp, HeuristicPolicy.cpp, ActionListener.cpp:1410-1424, MpObjectReference.cpp:1788-1814, script_storages/* | Medium | Papyrus* tests, PapyrusCompatibilityTest | - |
| A11 | NPC spawn filter and synced record types | WorldState.cpp:392-553 | Low | NpcExists, LoadCellsTest | - |
| A12 | Crafting rules | CraftService.cpp | Low | CraftTest | - |
| A13 | Message registry per game; MsgType range reservation; client prefix define (Skyrim "7_") | msg/*, mpc/*, client/main.cpp | Medium | Message serialisation tests; byte-compare Skyrim packets before and after | - |
| A14 | TS server: `game` setting, start points, manifest `.ba2`, ui blocks `.ba2` | sts/settings.ts, manifestGen.ts, ui.ts, systems/spawn.ts | Low | `npm run build`; manual start with Skyrim settings | partly |
| A15 | Build: `GAME` CMake variable (default skyrim), per-game settings generation and data dir | CMakeLists.txt, cmake/scripts/*, unit/CMakeLists.txt | Low | CI config unchanged | - |
| B1 | `espm::Game` detection, `CombineBrowser::GetGame()`, game check in ScampServer | libespm Loader/TES4/CombineBrowser | Low | EspmTest + new synthetic-ESP tests (hand-built TES4 headers, no game data) | Up |
| B2 | ESL support (FE prefix, uint16 file index) | IdMapping, Combiner, CombineBrowser, FormDesc, WorldState | High | Full suite; synthetic ESL test | Up |
| B3 | VMAD v6 (Var/Struct), non-throwing | Utils.cpp, Property.h, ScriptVariablesHolder | Medium | Synthetic VMAD tests | Up (robustness) |
| B4 | `espm::fo4` records + `views::*`; move server reads to views | libespm/fo4/*, views/*, server call sites | Medium | Synthetic record tests; Skyrim suite | views Up |
| B5 | BA2 script storage + recursive/namespaced directory storage | script_storages/* | Low | Unit test with a generated BA2 (if rsm-bsa can write fo4) | - |
| C | papyrus-vm FO4: header GameID, little-endian, structs, Var, new opcodes, namespaces | papyrus-vm/* | High | PEX fixtures compiled with the FO4 compiler (private), decompiled samples | - |
| D | Fallout4GameProfile, FO4 messages (64..95), FO4 damage formula, FO4 standard_scripts, private FO4 test data job | game_profile/fallout4/*, msg/*, standard_scripts/fallout4 | High | `[fo4data]` tests in a private CI job | - |
| E | Client forks: fallout4-platform (CommonLibF4), falloutmp-client (sync rewritten per §3.1 High rows), falloutmp-front | new directories | High | Manual in-game tests | - |

Upstream-merge hygiene:
- Put new code in new files (`game_profile/`, `libespm/fo4/`, `views/`). Keep edits in upstream files to call-site substitutions.
- Do not rename upstream classes. Avoid reformatting.
