# Fallout 4 data formats — reference for the FalloutMP port

Status: research reference (2026-10-05). Audience: the engineer (human or Claude Code session) who implements Fallout 4 (FO4) support in `libespm`, `skymp5-server`, the archive/strings loaders and the spawn/save path. Orientation: `docs/FALLOUT4_PORT_RESEARCH.md` §4.2.

All multi-byte integers are **little-endian**. `formid` = u32 FormID **as stored in the file** (file-local: top byte indexes that plugin's `MAST` list; must be remapped, see §3). `lstring` = u32 string ID if the plugin is localized, else a zstring. `fv` = the record's **form version** (record header u16 at +0x14). Offsets are hex from the start of the subrecord *payload* (after the 6-byte field header) unless stated.

## Provenance legend

| Tag | Meaning |
|---|---|
| `[src: path:line]` | This repository (`/home/user/falloutmp`), current branch `claude/fallout4-port-research` |
| `[xe: file:line]` | xEdit (TES5Edit) `Core/` sources, commit `9fb01688` (2026-09-06). FO4 record definitions are `wbDefinitionsFO4.pas` (abbr. **FO4.pas**), common helpers `wbDefinitionsCommon.pas` (**Common.pas**), FO4 saves `wbDefinitionsFO4Saves.pas` (**FO4Saves.pas**) |
| `[mut: file]` | Mutagen `Mutagen.Bethesda.Fallout4/Records/...xml`, commit `4f533562` (2026-10-01) — used as an independent cross-check |
| `[rsv: file]` | FallrimTools/ReSaver Java sources (`src/main/java/resaver/ess/`), commit `61bb57d` (2023-10-01) |
| `[bsa: file]` | rsm-bsa (Ryan-rsm-McKenzie/bsa); tag `4.1.0` = `e5c979c` (2023-08-29, **the version pinned here**), master = `2c7280d` (2025-02-08) |
| `[clib: file]` | CommonLibF4 (libxse line), `include/RE/...` |
| `[hc]` | Verified by parsing xEdit's real FO4-format plugin `Core/Hardcoded/Fallout4.esp` with a throw-away Python dumper |
| `[web: URL]` | Web source (search summaries where the page itself was not fetchable — marked "(summary)") |
| `[inference]` | My reasoning, not directly stated by a source. Verify against real data before relying on it |

Tooling used for every offset table below: the xEdit field list was transcribed into a script that sums packed field sizes, so offsets are mechanically derived from xEdit, then spot-checked against Mutagen (WEAP DNAM, RACE DATA, NPC_ ACBS, ARMO FNAM, AMMO DNAM all agree).

---

## 0. TL;DR — the differences that break the current code

1. **Container is the same** (24-byte record/group headers, 6-byte field headers, `XXXX`, zlib) → `espm::Browser` walks `Fallout4.esm` unchanged. Detect the game from the record header **form version** (`131` in FO4, `44` in SSE) and/or `HEDR.version` (`1.0`/`0.95` FO4, `1.7`/`1.71` SSE) [hc][xe: FO4.pas:13366].
2. **Light plugins**: FO4 has ESL (`TES4` flag `0x200`, `.esl` extension). Runtime IDs `FE xxx yyy`: 12-bit light slot, 12-bit object ID; up to 4096 light + 254 full plugins. `libespm`'s 256-entry `IdMapping` and `formId>>24` everywhere (`FormDesc`, `WorldState::GetFileIdx`) cannot express this (§3).
3. **Actor values are forms** (`AVIF`, `0x2BC…`) — not an enum. `MGEF.DATA+0x44`, `WEAP.DNAM` skill/resist, `NPC_`/`RACE` `PRPS`, `BOOK` teaches, CTDA `GetValue` params are AVIF FormIDs that need remapping.
4. Record layouts that the current parsers misread on FO4: `NPC_` (ACBS same size, different meaning of +0x04/+0x14; no magicka/stamina; `SNAM` is 5 bytes), `RACE` (new 200-byte `DATA`, fields gated by fv), `WEAP` (132-byte `DNAM`, value/weight/damage moved into `DNAM`), `ARMO` (no `DNAM` → current code **throws**; `BOD2` is 4 bytes → never parsed), `AMMO` (weight at `DATA+4`), `COBJ` (`FVPA` components; count in `INTV`, `NAM1` unused), `LVLO` (`chanceNone` byte inside what libespm reads as `count`), `LIGH` (64-byte DATA), `VMAD` (Struct/Var property types → current code throws inside a `noexcept`).
5. **Localization**: `Fallout4.esm` is localized; strings live in `Data\Strings\<Plugin>_<lang>.(STRINGS|DLSTRINGS|ILSTRINGS)` with 2-letter languages (`en`), shipped **inside `Fallout4 - Interface.ba2`**. Current `LocalizationProvider` only reads loose `dataDir/strings/*.strings` (lower-case, case-sensitive), so it finds nothing for FO4 (§2).
6. **BA2**: rsm-bsa `4.1.0` (pinned in `overlay_ports/rsm-bsa`) **rejects any version ≠ 1**; NG/AE archives are v7/v8. Bump the port to rsm-bsa master (commit `04d1fdf`+) (§6).
7. **.fos saves** are uncompressed (no SSE-style LZ4 body), 12-byte magic, RGBA screenshot, extra "game version" string, light-plugin list, different global-data type set; `savefile/` (Skyrim LE v9 only) cannot read them. A minimal opaque-blob reader/writer is small (§7).

---

## 1. Plugin container: FO4 vs SSE

### 1.1 Record header (24 bytes) — identical layout

| Off | Size | Field | FO4 notes |
|---|---|---|---|
| 0x00 | 4 | signature `char[4]` | |
| 0x04 | 4 | dataSize u32 | bytes after the 24-byte header (compressed size if `0x40000`) |
| 0x08 | 4 | flags u32 | §1.3 |
| 0x0C | 4 | FormID u32 | file-local; `TES4` = 0 |
| 0x10 | 4 | version-control info 1 | FO4 packs a date differently: day = bits 0-4, month = bits 5-8, year-2000 = bits 9-15, user = bits 16-23, index = 24-31 (pre-FO4: day u8, months-since-2003 u8, user, index) [xe: wbInterface.pas:23466] |
| 0x14 | 2 | **form version** u16 | **FO4 = 131** for current CK output (all 180 records of `Hardcoded/Fallout4.esp` are 131 [hc]); SSE = 44, LE = 43. Old FO4 records/mods can carry lower values (fields "fromVersion N" in xEdit; see RACE/EXPL/FURN) |
| 0x16 | 2 | version-control info 2 | ignored |

[xe: Common.pas:8565 `wbRecordHeader`; FO4.pas:3273-3275 `wbSizeOfMainRecordStruct := 24`]

`libespm::RecordHeader` is the 16 bytes after `type,dataSize` (`flags,id,revision,version u16,unk u16`) [src: libespm/include/libespm/RecordHeader.h:43-50] — matches FO4 exactly; `GetVersion()` already returns the form version but nothing calls it.

### 1.2 Group header (24 bytes) — identical

| Off | Size | Field |
|---|---|---|
| 0x00 | 4 | `GRUP` |
| 0x04 | 4 | groupSize u32 **including** the 24-byte header |
| 0x08 | 4 | label (type-dependent) |
| 0x0C | 4 | groupType s32 |
| 0x10 | 2 | date stamp u16 (FO4 encoding as above) |
| 0x12 | 2 | unknown |
| 0x14 | 2 | version u16 (0 in xEdit-written files [hc]) |
| 0x16 | 2 | unknown |

Group types 0–10 are the same as Skyrim: 0 Top (label = record signature), 1 World children (label = WRLD FormID), 2 Interior cell block (s32 block#), 3 Interior sub-block, 4 Exterior block (label = s16 Y, s16 X), 5 Exterior sub-block, 6 Cell children (CELL FormID), 7 Topic children (DIAL), 8 Cell persistent, 9 Cell temporary, 10 (Skyrim: visible distant; FO4 files don't use it in practice [inference]). [xe: wbImplementation.pas:1895-1902, 17264-17372]; `espm::GroupHeader` / `GroupType` already match [src: libespm/include/libespm/GroupHeader.h:28-37, GroupType.h].

FO4 top-group order differs (FO4 adds e.g. `AVIF`, `OMOD`, `CMPO`, `INNR`, `SCOL`, `PKIN`, `TRNS`, `ZOOM`, `AORU`, `NOCM`, `GDRY` …) [xe: FO4.pas:~13200-13348 `wbAddGroupOrder`]; `Browser` does not depend on order.

### 1.3 Record flags (generic meanings; per-record meanings vary)

| Bit | Mask | Meaning (FO4) | Applies to |
|---|---|---|---|
| 0 | 0x00000001 | ESM / master | TES4 |
| 4 | 0x00000010 | Optimized (TES4) / Ground Piece (REFR) | |
| 5 | 0x00000020 | **Deleted** | all |
| 7 | 0x00000080 | **Localized** (TES4) / Turn Off Fire (proj refs) / No Pre Vis (CELL) | |
| 9 | 0x00000200 | **ESL / light** (TES4, non-VR FO4) · Starts Dead (ACHR) · Casts Shadows (LIGH ref) · Hidden From Local Map (some REFR) | |
| 10 | 0x00000400 | **Persistent** (REFR/ACHR/CELL) | |
| 11 | 0x00000800 | **Initially Disabled** (REFR/ACHR/NAVM) · Calc Value From Components (KEYM/MISC) | |
| 13 | 0x00002000 | Starts Unconscious (ACHR) · Pack-In Use Only (ACTI/STAT/TERM/KEYM/MISC) | |
| 14 | 0x00004000 | Partial Form (CELL/WRLD/QUST) | |
| 15 | 0x00008000 | Visible When Distant / Has Distant LOD | |
| 16 | 0x00010000 | Is Full LOD (REFR) · Random Anim Start | |
| 18 | 0x00040000 | **Compressed** (zlib) | all |
| 19 | 0x00080000 | Can't Wait (CELL/WRLD) | |
| 25 | 0x02000000 | No AI Acquire (REFR/ACHR) · Power Armor (FURN) · Obstacle | |
| 26/27/30 | | NavMesh Filter / Bounding Box / Ground (statics) | |
| 28 | 0x10000000 | Reflected By Auto Water (REFR) · Must Exit To Talk (FURN) | |
| 29 | 0x20000000 | Don't Havok Settle (REFR/ACHR) · Child Can Use (FURN) · Bleedout Override (NPC_) | |
| 30 | 0x40000000 | No Respawn (REFR) | |
| 31 | 0x80000000 | Multibound (REFR) | |

Sources: generic helpers [xe: wbInterface.pas:20525-20620] (`IsDeleted`=0x20, `IsLocalized`=0x80, `IsPersistent`=0x400, `IsInitiallyDisabled`=0x800, `IsCompressed`=0x40000, `IsLight`=0x200 for non-Starfield); per-record lists in FO4.pas (TES4:12091, REFR:11347-11450, ACHR:4483-4490, NPC_:10286-10292, FURN:6848-6859, OMOD:12501 (bit 4 "Legendary Mod", bit 7 "Mod Collection"), KEYM:9823, MISC:10212).

The server's `AttachEspmRecord` uses `0x800` (disabled), `0x20` (deleted), ACHR `0x200` (starts dead) [src: skymp5-server/cpp/server_guest_lib/WorldState.cpp:368-445; libespm/src/ACHR.cpp:8] — all still correct for FO4.

### 1.4 Field (subrecord) header, `XXXX`, compression

- Field header: `char[4] type; u16 size` (6 bytes); `XXXX` (size 4, payload u32) overrides the size of the **next** field (needed for large `NVNM`, `OFST`, `XCRI`…). Same as SSE [src: libespm/include/libespm/RecordHeaderAccess.h:44-58 already handles it].
- Compressed record (`flags & 0x40000`): payload = `u32 decompressedSize` + zlib stream (`dataSize-4` bytes). **zlib only** in FO4 plugins (no LZ4) [xe: wbImplementation.pas:10293-10313].
- **Bug to fix while there**: `ZlibDecompress` calls `inflate(Z_NO_FLUSH)` once and ignores `Z_BUF_ERROR`/short output; use `Z_FINISH` and assert `total_out == decompSize` [src: libespm/include/libespm/ZlibUtils.h:7-28; inference].

### 1.5 TES4 record (FO4)

| Field | Size | Layout / meaning |
|---|---|---|
| `HEDR` | 12 | `+0 float version` (**1.0** CK ≥ Nov-2019 / 1.10.162+; **0.95** original FO4, FO4 VR and VR-compatible plugins), `+4 u32 numRecords`, `+8 u32 nextObjectId` (xEdit seeds new FO4 files with `1`; 0x800 for VR) [xe: Common.pas:8865; FO4.pas:13366-13369] [web: https://www.nexusmods.com/fallout4/articles/6122 (summary: "header version 1.00 … older FO4 VR cannot load")] |
| `OFST`, `DELE` | var | ignored by runtime |
| `CNAM` | z | author |
| `SNAM` | z | description |
| `MAST` + `DATA` | z + 8 | master file name; `DATA` = u64 (file size, usually 0). Pairs, ordered |
| `ONAM` | 4·n | overridden forms (ACHR, DIAL, DLBR, INFO, LAND, NAVM, PARW…PMIS, REFR, SCEN) — needed by the game for persistent overrides, not by us |
| `SCRN` | var | screenshot, ignored |
| `TNAM` | var | "transient types" (CK only): `u32 formType` (78 DIAL, 126 SCEN) + formid[] |
| `INTV` | 4 | u32, ignored by runtime |
| `INCC` | 4 | u32 interior cell count (required) |

[xe: FO4.pas:12091-12128]. `Hardcoded/Fallout4.esp` = `HEDR(0.95, 180, 0x800) CNAM SNAM INTV(1)`, flags 0 [hc].

TES4 flags: `0x1` ESM, `0x10` Optimized, `0x80` Localized, `0x200` ESL [xe: FO4.pas:12092-12098]. (Bit 20 "Update" exists only in xEdit's VR-ESL mode; ignore.) libespm `TES4::GetData` reads only HEDR/CNAM/SNAM/MAST [src: libespm/src/TES4.cpp:9-20] — add flags accessors (`IsLocalized()`, `IsLight()`, `IsMaster()`).

### 1.6 Game detection rule (proposed)

```
fv = first record header form version (TES4 itself carries it, 131 in FO4 files [hc])
if fv >= 100 → Fallout4          // FO4 form versions are 60..131; SSE ≤ 44
else if HEDR.version in {1.7, 1.71, 0.94} → Skyrim SE
```
FO4 range: ReSaver rejects FO4 save form versions < 60 [rsv: ESS.java:285-290]; plugins carry 1xx (xEdit fields gated at 69-131). Skyrim plugins never exceed 44 [inference: SSE CK writes 44]. Make it explicit configuration too (`"game": "fallout4"` in server settings) and only *verify* with the heuristic.

---

## 2. Localized strings

### 2.1 How lstrings are stored in records

- If `TES4.flags & 0x80`, every field xEdit defines as `wbLString` (FULL, DESC, SHRT, ATTX, ONAM short names, ITXT/BTXT/RNAM/UNAM terminal text, NAM0/WNAM terminal header/welcome, DNAM addiction name, MPPN/TTGP morph/tint names, GMST string values, …) is a **4-byte u32 string ID**; `0` = empty. Otherwise it is an inline zstring.
- The ID refers to the string tables of **the plugin that contains that version of the record** (the winning override, if it is localized) — `ScampServer::GetLocalizedString` already does this via `lookupRes.fileIdx` [src: skymp5-server/cpp/addon/ScampServer.cpp:~850-890].
- Which table: xEdit's rule [xe: wbLocalization.pas:~548-561 `LocalizedValueDecider`]:

| Record / field | Table |
|---|---|
| `DESC` in any record except `LSCR` | `.DLSTRINGS` |
| `QUST.CNAM` (log entry), `BOOK.CNAM` (description) | `.DLSTRINGS` |
| `INFO.*` except `INFO.RNAM` | `.ILSTRINGS` |
| everything else (FULL, SHRT, NAM0, ITXT, BTXT, …) | `.STRINGS` |

libespm currently returns `FULL` as `const char*` in `CONT`, `TREE`, `FLOR`, `QUST` [src: libespm/src/CONT.cpp:18, TREE.cpp:17, FLOR.cpp:17, QUST.cpp:180] — garbage for localized plugins (already wrong for Skyrim.esm). Change to `lstring` + `bool isLocalized` from TES4.

### 2.2 STRINGS / DLSTRINGS / ILSTRINGS file format (same as Skyrim)

| Off | Size | Field |
|---|---|---|
| 0x00 | 4 | `count` u32 |
| 0x04 | 4 | `dataSize` u32 (bytes of string data) |
| 0x08 | 8·count | directory: `{u32 id; u32 offset}` — offset relative to the data block start (`8 + 8·count`) |
| data | dataSize | `.STRINGS`: zstring. `.DLSTRINGS`/`.ILSTRINGS`: `u32 length` (**includes** the terminating NUL) + bytes + NUL |

[xe: wbLocalization.pas:242-345 (`ReadZString`, `ReadLenZString`, `ReadDirectory`), 349-390 writer]

Encoding: xEdit's default for unknown language codes (FO4's `en`) is **UTF-8 with cp1252 fallback** on invalid sequences [xe: wbInterface.pas:24057-24058]. Treat FO4 strings as UTF-8 and fall back to cp1252 per string [inference].

### 2.3 Where FO4 keeps them and naming

- Path: `Strings\<PluginBaseName>_<lang>.<EXT>`, e.g. `Strings\Fallout4_en.STRINGS`, `Strings\DLCRobot_en.DLSTRINGS` [xe: wbLocalization.pas:~690-705 `GetLocalizationFileNameByType`]. Language comes from `Fallout4.ini [General] sLanguage`; xEdit's FO4 default is `En` [xe: ../xEdit/xeInit.pas:935, 1321-1366]. Codes are short (`en`, `fr`, `de`, `es`, `it`, `ja`, `ru` confirmed by players' ini edits; `pl`, `ptbr`, `zhhans`, `esmx` [inference]) [web: https://steamcommunity.com/app/377160/discussions/0/1658943116244797021/ (summary)].
- Vanilla: **inside `Fallout4 - Interface.ba2`** (`strings\fallout4_en.strings` etc.) [web: https://www.nexusmods.com/fallout4/mods/4265 (summary: "Fallout4 - Interface.ba2 holds strings\fallout4_en.strings")]. DLC/CC strings are inside their own `<Plugin> - Main.ba2` [inference: xEdit loads strings through its archive container handler for every plugin, wbLocalization.pas:~670-685]. Loose `Data\Strings\` overrides archives.
- Lookup must be **case-insensitive** (BA2 names and loose files differ in case).

### 2.4 Current SkyMP localization path and required changes

| Where | Today | Problem for FO4 | Change |
|---|---|---|---|
| `LocalizationProvider` ctor [src: skymp5-server/cpp/localization_provider/LocalizationProvider.cpp:104-123] | scans `dataDir/"strings"` (lower-case dir), loads every file whose name **contains** `language` | Linux is case-sensitive (`Strings`); FO4 has no loose strings; `"en"` substring matches unrelated names | Source = (loose dir, case-insensitive) ∪ (BA2 archives in load order via rsm-bsa); match exact `<plugin>_<lang>.<ext>` |
| `Parse()` [src: …LocalizationProvider.cpp:78-102] | key = filename before last `_` (case preserved); extension compared to lower-case `.strings/.dlstrings/.ilstrings` | `Fallout4_en.STRINGS` → never parsed; key `Fallout4` vs `Get()` caller lower-cases → mismatch | lower-case key and extension |
| `ParseILDLStrings` [src: …:54-75] | `length = (uint32_t)buffer[start]` (one signed byte); loop bound `length - start` | wrong length for > 127-byte strings; works only by NUL-stopping | read u32 length; copy `length-1` bytes |
| `ParseDirectoryEntries` [src: …:10-31] | no bounds checks | crash on malformed files | validate `8+8n ≤ size`, `offset < dataSize` |
| `ScampServer::GetLocalizedString` [src: ScampServer.cpp:~795-893] | only `FULL` of size 4; plugin name taken from `serverSettings["loadOrder"][fileIdx]` (absent when default load order) | default load order (no `loadOrder` key) returns nothing; only FULL | use `Loader::GetFileNames()[fileIdx]`; support DESC/ATTX/SHRT with table selection per §2.1; respect TES4 localized flag |
| `lang` setting [src: ScampServer.cpp:334-339] | `"lang": "english"` style | FO4 uses `en` | document FO4 values |

---

## 3. Form IDs, load order, ESM/ESP/ESL

### 3.1 Semantics in FO4 (non-VR)

| Concept | FO4 rule | Source |
|---|---|---|
| Master sorting | `.esm` **and `.esl`** extension ⇒ treated as master (sorted before non-masters). ESM flag (0x1) on a `.esp` also makes it a master | [xe: wbLoadOrder.pas:323-347] |
| Light | `.esl` extension forces light; `.esp`/`.esm` with TES4 `0x200` = "ESPFE"/"ESL-flagged ESM" | [xe: wbLoadOrder.pas:349-366] |
| Slots | full plugins `0x00…0xFD` (254 incl. Fallout4.esm); `0xFE` = light block; `0xFF` = runtime-created | [xe: wbInterface.pas:22584-22611 `LightFullSlot=$FE`, `MaxFullSlot=$FD`, `MaxLightSlot=$FFF`] [web: https://www.nexusmods.com/fallout4/articles/6251 (summary: 254 full, up to 4096 light)] |
| Light FormID | `0xFE000000 \| (lightIndex << 12) \| (objectId & 0xFFF)` — `lightIndex` 0…4095 in light-load order | [xe: wbInterface.pas:22500-22510] [rsv: PluginInfo.java `makeFormID`/`splitFormID`] |
| Light object-ID range | HEDR **1.0**: new records may use `0x001…0xFFF` (4095); HEDR **0.95**: `0x800…0xFFF` (2048). xEdit marks a file light-incompatible if any new record has `formID & 0x00FFF000 ≠ 0` | [xe: wbImplementation.pas:2450-2460] [web: https://www.nexusmods.com/fallout4/mods/35922 (summary)] |
| "Hardcoded" range in normal plugins | with HEDR ≥ 1.0 and ≥ 1 master, new records may use object IDs below 0x800 | [xe: wbImplementation.pas:4076-4094 `GetAllowHardcodedRangeUse`] |
| Implicit order | `Fallout4.esm`, then official DLC in fixed order: `DLCRobot`, `DLCworkshop01`, `DLCCoast`, `DLCworkshop02`, `DLCworkshop03`, `DLCNukaWorld`, `DLCUltraHighResolution` (all `.esm`), then every file listed in `<GameRoot>\Fallout4.ccc` (one name per line, in order), then `plugins.txt` order | [xe: FO4.pas:13352-13364; wbLoadOrder.pas:466-500; ../xEdit/xeMainForm.pas:4708-4719] |
| plugins.txt | `%LOCALAPPDATA%\Fallout4\Plugins.txt`; `*` prefix = active; `#` comment. Masters still sort before non-masters regardless of file order | [xe: wbLoadOrder.pas:430-460; xeInit.pas:573-586] |
| Archive limit | 1.11.240 (Aug-2026) raised BA2 limit 256 → 1024 | [web: https://www.nexusmods.com/fallout4/articles/6251 (summary)] |

Creation Club / "Creations": AE (1.11.137+) ships ~150 CC items as `cc*.esl` + `cc* - Main.ba2`/`- Textures.ba2`, listed in `Fallout4.ccc` (broad survey §4.2 [web]). Many are ESL; several are ESL-flagged ESMs. A server that loads "the client's load order" must therefore support light plugins from day one.

### 3.2 What libespm does today

- `IdMapping = std::array<uint8_t, 256>` [src: libespm/include/libespm/IdMapping.h:7]; `GetMappedId(id, map) = (id & 0xFFFFFF) | map[id>>24] << 24` [src: libespm/src/Utils.cpp:69-74]; `CombineBrowser::Impl::sources` is `std::array<Source,256>` [src: libespm/include/libespm/CombineBrowser.h:59]; `BrowserInfo::fileIdx` is `uint8_t` [src: BrowserInfo.h:17]. `Combiner::Combine` fills `toComb[m] = globalIdx` for masters and `toComb[numMasters] = selfIdx`, `0xFF` elsewhere; `LookupById` maps a *combined* ID back to raw per source and skips results `>= 0xFF000000` [src: libespm/src/Combiner.cpp:27-56; CombineBrowser.cpp:24-46].
- Combined ID == "load-order index in `espmFiles` << 24 | object" — that convention leaks into the server: `FormDesc::ToFormId/FromFormId` [src: skymp5-server/cpp/server_guest_lib/FormDesc.cpp:46-98], `WorldState::GetFileIdx(formId) = formId >> 24` [src: WorldState.cpp:~1158], DB/change-form storage (`FormDesc` strings like `"3c:Skyrim.esm"`).
- Latent bug: `CombineBrowser::FindNavMeshes` computes `rawFormId` but passes the combined `worldSpaceId` to the source browser [src: libespm/src/CombineBrowser.cpp:74].

### 3.3 Concrete design: light-aware combining

1. **Load-order model** (new `espm::LoadOrder` built by `Combiner`): for each source `i` store `kind ∈ {Full, Light}`, `fullIndex` (0…253) or `lightIndex` (0…4095), and `runtimePrefix(i)`: `Full → fullIndex<<24`, `Light → 0xFE000000 | lightIndex<<12`. A light plugin is one with TES4 `0x200` **or** `.esl` extension (game == FO4/SSE only). Masters-first ordering is the caller's job (server settings list), but validate: error if an ESP precedes an ESM/ESL.
2. **Global ("combined") FormID = the game's runtime FormID.** This makes server IDs identical to client `Form.GetFormID()` (important for FO4 where the client sends FE-prefixed IDs). Skyrim keeps working because with no light plugins it degenerates to today's `index<<24`.
3. **Per-source mapping** replaces `IdMapping`:
   ```cpp
   struct SourceIdMap {             // one per plugin
     std::vector<uint32_t> masterPrefix;  // per MAST index m: runtime prefix of that master (full or FE-light)
     std::vector<bool>     masterIsLight;
     uint32_t selfPrefix; bool selfIsLight;
   };
   // raw (file-local) → global
   uint32_t ToGlobal(uint32_t raw, const SourceIdMap& s) {
     uint32_t idx = raw >> 24, obj = raw & 0xFFFFFF;
     bool light; uint32_t pfx;
     if (idx < s.masterPrefix.size()) { pfx = s.masterPrefix[idx]; light = s.masterIsLight[idx]; }
     else if (idx == s.masterPrefix.size()) { pfx = s.selfPrefix; light = s.selfIsLight; }
     else return 0;                      // invalid (or 0xFF created)
     return light ? (pfx | (obj & 0xFFF)) : (pfx | obj);
   }
   ```
   Note: a file-local FormID never contains `FE`; the light-ness comes from the *referenced* plugin.
4. **Global → raw** (used by `LookupById` per source): decode `{plugin, obj}` from the global ID (`top==0xFE` ⇒ light index `(id>>12)&0xFFF`, `obj = id&0xFFF`; else full index `id>>24`), then for each source that has that plugin as master or self, `raw = (masterIdxOrSelf << 24) | obj`. Precompute `std::vector<int16_t> globalPluginToLocalIdx` per source (size = number of plugins, −1 = not visible) instead of a 256-byte table.
5. **Fast lookup**: replace the O(numSources) scan in `LookupById` (which today probes every source — fine for 5 ESMs, not for 150+ CC ESLs) with `unordered_map<uint32_t globalId, small_vector<(srcIdx, const RecordHeader*)>>` built once after combining; `LookupByIdAll` returns the vector. Memory: ~1–2 M records for FO4+DLC [inference].
6. Widen types: `fileIdx` → `uint16_t` (≥ 4350 plugins theoretically), `sources` → `std::vector`.
7. **Server side**: `FormDesc` must serialize light records as `"<obj&0xFFF hex>:<file>"` and `ToFormId` must look up the file's runtime prefix through the loader (not `index*0x01000000`); `WorldState::GetFileIdx` → `loader.PluginIndexOf(formId)`. Keep `0xFF` created-ID handling unchanged. `FormDesc.cpp:50,81-83,100` hardcode `3c:Skyrim.esm` (Tamriel); FO4's Commonwealth is also `0x3C` but in `Fallout4.esm` — make the default world a profile setting.
8. **Validation**: warn when a light plugin defines a new record outside its allowed range (§3.1) — the game would ignore/mis-map it.

### 3.4 Default FO4 load order for server settings

`Fallout4.esm, DLCRobot.esm, DLCworkshop01.esm, DLCCoast.esm, DLCworkshop02.esm, DLCworkshop03.esm, DLCNukaWorld.esm, [DLCUltraHighResolution.esm], <Fallout4.ccc entries that exist>, <user ESM/ESL/ESP>` — replace the hardcoded Skyrim list [src: skymp5-server/cpp/addon/ScampServer.cpp:315-321].

---

## 4. Record layouts needed by FalloutMP

Notation: `Sig (size) — meaning`; `[]` = repeating subrecord; `n·x` = packed array filling the subrecord (count = size/x) unless a count prefix is given. "count u32/u8 prefix" = xEdit array count `-1`/`-4` [xe: wbInterface.pas:13440-13455]. Common subrecords that appear on most items:

| Sig | Size | Layout |
|---|---|---|
| `EDID` | z | editor ID (not localized) |
| `OBND` | 12 | `s16 x1,y1,z1,x2,y2,z2` |
| `PTRN` | 4 | formid TRNS (preview transform) |
| `STCP` | 4 | formid STAG (animation sound) |
| `FULL` | 4 / z | lstring name |
| `MODL`/`MODT`/`MODC`/`MODS`/`MODF` | var | model path, texture hashes, color remap (float), material swap (MSWP), flags |
| `KSIZ` + `KWDA` | 4 + 4·n | keyword count, formid KYWD[] |
| `PRPS` | 8·n | `{formid AVIF; float value}` "Properties" (actor values / item AV modifiers) [xe: FO4.pas:5266-5272] |
| `APPR` | 4·n | formid KYWD[] — **attach parent slots** (OMOD attach points this object offers) [xe: FO4.pas:5275] |
| `FTYP` | 4 | formid LCRT forced loc-ref type |
| `NTRM` | 4 | formid TERM native terminal |
| `ATTX` | 4 | lstring activate-text override |
| `YNAM`/`ZNAM` | 4 | pickup / putdown SNDR |
| `DEST`… | var | destructible data (`DEST`,`DAMC`,`DSTD`,`DSTA`,`DMDL`,`DSTF`) |
| `VMAD` | var | scripts (§4.21) |
| `DAMA` | 8·n | `{formid DMGT; u32 amount}` damage types / resistances [xe: Common.pas:8728] (the `+CURV` 3rd field is fv ≥ 152 = FO76/SF only) |
| `INRD` | 4 | formid INNR instance naming rules |
| `OBTE`…`STOP` | var | object template (§4.19) |
| `CITC` + `CTDA`[] (+`CIS1`/`CIS2`) | 4 + 32·n | condition count then conditions (§4.20) |
| `COCT` + `CNTO`[] (+`COED`) | 4 + 8 (+12) | item count; `CNTO {formid item; s32 count}`; optional `COED {formid owner; formid GLOB / s32 rank; float condition}` follows each CNTO [xe: FO4.pas:3290-3316] |

### 4.1 GMST / GLOB / KYWD / AVIF / FLST

| Record | Fields |
|---|---|
| **GMST** [xe: FO4.pas:6936] | `EDID` (prefix selects type: `s` string, `i` int, `f` float, `b` bool, `u` uint [inference: Skyrim convention]); `DATA` = lstring (**localized** for `s*`) / s32 / float / u32 bool. libespm reads float only [src: libespm/src/GMST.cpp:12]; its hardcoded Skyrim IDs `kFCombatDistance=0x55640` etc. are meaningless in FO4 [src: libespm/include/libespm/GMST.h:14-16] — resolve by EDID |
| **GLOB** [xe: FO4.pas:6921] | flag `0x40` Constant; `FNAM` u8 type (`'s'`,`'l'`,`'f'`,`'b'`); `FLTV` float (always float on disk) |
| **KYWD** [xe: FO4.pas:6947] | `CNAM` u8[4] color; `DNAM` z notes; `TNAM` u32 keyword type; `DATA` formid AORU; `FULL`; (legacy `NNAM`) |
| **AVIF** [xe: FO4.pas:7826-7878] | `EDID`, `FULL`, `DESC`, `ANAM` lstring abbreviation, `NAM0` float default value, `AVFL` u32 flags (0x2 Skill, 0x4 Uses Enum, 0x8 no script edits, 0x400/0x800/0x1000 default-to 0 / 1.0 / 100.0, 0x100000 Min 1, 0x200000 Max 10, 0x400000 Max 100, 0x800000 ×100, 0x1000000 Percentage, 0x4000000 Damage Is Positive, 0x8000000 God-mode immune, 0x80000000 Hardcoded), `NAM1` u32 type (0 Derived attr, 1 SPECIAL, 2 Skill, 3 AI attr, 4 Resistance, 5 Condition, 6 Charge, 7 Int value, 8 Variable, 9 Resource) |
| **FLST** [xe: FO4.pas:7574] | `LNAM`[] formid (same as Skyrim) |

**AVIF FormIDs.** Hardcoded actor values occupy `0x2BC…0x389` (engine index *i* → FormID `0x2BC + i` [xe: FO4.pas:7878 comment]); 164 of them are defined only in xEdit's hardcoded plugin, the rest live in `Fallout4.esm` [hc]. Known IDs:

| AV | FormID | Source |
|---|---|---|
| Aggression/Confidence/Energy/Morality | 0x2BC/0x2BD/0x2BE/0x2BF | [hc] |
| Assistance | 0x2C1 | [hc] |
| Strength (then Perception, Endurance, Charisma, Intelligence, Agility, Luck consecutive) | 0x2C2 (…0x2C8) | [web: https://fallout.fandom.com/wiki/Fallout_4_console_commands (summary)]; consecutive order [inference] |
| Health | 0x2D4 | [web: same (summary)] |
| ActionPoints | 0x2D5 | [web: same (summary)] |
| ActionPointsRate / RadsRate | 0x2D8 / 0x2DB | [hc] |
| CarryWeight | 0x2DC | [web (summary)] |
| CritChance / MeleeDamage / Mass | 0x2DD / 0x2DE / 0x2E0 | [hc] |
| DamageResist | 0x2E3 | [web (summary)] |
| RadResistIngestion / RadResistExposure / EnergyResist | 0x2E9 / 0x2EA / 0x2EB | [web (summary)] (xEdit comment says RadResistIngestion is index 0x29 = 0x2E5 — conflicting; **resolve by EDID**) |
| RadHealthMax, PowerArmor{Head,Torso,LeftArm}Condition | 0x2EE, 0x2EF–0x2F1 | [hc] |
| Fatigue, FatigueAPMax, PowerArmorBattery, PA RightArm/LeftLeg cond., PA RightLeg cond. | 0x34F, 0x350, 0x35C, 0x35D/0x35E, 0x388 | [hc] |
| WorkshopPlayerOwned, Food, Water, Happiness, PowerGenerated | 0x33C, 0x331, 0x332, 0x335, 0x32E | [hc] |

Recommendation: at load time build `unordered_map<string EDID, uint32_t globalFormId>` from all AVIF records **plus** a built-in table of the hardcoded ones (copy from `Hardcoded/Fallout4.esp`, 164 entries, or embed that esp's AVIF list) and refer to AVs by EDID in server code.

### 4.2 WRLD / CELL / LCTN / NAVM

**WRLD** [xe: FO4.pas:13110-13176] — same signatures as Skyrim: `FULL`, `WCTR` (s16 x, s16 y), `LTMP`, `XEZN`, `XLCN`, `WNAM`+`PNAM`(u16 use-flags: 0x1 land, 0x2 LOD, 0x4 map, 0x8 water, 0x10 climate, 0x40 sky cell), `CNAM` climate, `NAM2` water, `NAM3`/`NAM4` LOD water, `DNAM` (2 floats land/ocean height), `ICON`, cloud model, `MNAM` map data, `ONAM` map offset (4 floats), `NAMA` float, `DATA` u8 flags (0x1 small world, 0x2 can't fast travel, 0x8 no LOD water, 0x10 no landscape, 0x20 no sky, 0x40 fixed dimensions, 0x80 no grass), `NAM0`/`NAM9` object bounds, `ZNAM` music, `XWEM`, `TNAM`/`UNAM` HD LOD textures, `MHDT`/`OFST`/`CLSZ` large data. Commonwealth = `0x0000003C` in `Fallout4.esm`. libespm bug (both games): `WCTR`, `DNAM`, `ONAM` are copied with `std::copy_n(char*)` → byte-wise into int16/float arrays [src: libespm/src/WRLD.cpp:19,35,54] — use `memcpy`.

**CELL** [xe: FO4.pas:5953-6100]: record flags 0x80 no previs, 0x400 persistent, 0x4000 partial, 0x20000 off limits, 0x80000 can't wait. `DATA` u16 flags: 0x1 Interior, 0x2 Has Water, 0x4 Can't Travel From Here, 0x8 No LOD Water, 0x20 Public, 0x40 Hand Changed, 0x80 Show Sky, 0x100 Use Sky Lighting, 0x400 Hidden From Interior Cell List, 0x800 Sunlight Shadows, 0x1000 Distant LOD only, 0x2000 Player Followers Can't Travel Here. `XCLC` (12): `s32 x; s32 y; u8 landFlags; u8[3]`. `XCLL` lighting (FO4 adds fields; up to ~112 bytes, optional from field 11). `VISI`/`PCMB` u16 timestamps, `RVIS`, `CNAM`/`ZNAM` u8 precombine levels, `LTMP` (required), `XCLW` water height, `XCLR` regions, `XLCN` location, `XCWT` water, ownership (`XOWN` 12 bytes, `XRNK`), `XILL` lock list, `XEZN`, `XCMO` music, `XCIM`, `XGDR`, `XPRI`/`XCRI` previs/precombined ref indices (huge; use `XXXX`).

**LCTN** [xe: FO4.pas:8039-8177]: identical to Skyrim's ACxx/LCxx arrays (`ACPR`/`LCPR` 12-byte `{ref, world/cell, s16 y, s16 x}`, `ACUN`/`LCUN` 12-byte `{NPC_, ACHR, LCTN}`, `ACSR`/`LCSR` 16-byte, `ACEC`/`LCEC` world + coord pairs, `ACID`/`LCID` refs, `ACEP`/`LCEP` 12-byte enable parents) **plus** "removed" arrays `RCPR` (formid[]), `RCUN` (NPC_[]), `RCSR` (formid[]), `RCEC`; then `FULL`, keywords, `PNAM` parent, `NAM1` music, `FNAM` unreported-crime faction, `MNAM` world marker ref, `RNAM` float radius, **`ANAM` float actor fade mult** (new), `CNAM` color. Skyrim's `NAM0` horse marker is gone. Current parser [src: libespm/src/LCTN.cpp] works except it ignores `RC*`.

**NAVM** [xe: FO4.pas:7212-7238, NVNM 4360-4480]: `NVNM` header: `+0 u32 version (15)`, `+4 u32 CRC`, `+8 formid world`, `+0C {s16 gridY, s16 gridX} | formid CELL`, `+10 u32 vertexCount`, `+14 vertices (12 bytes each)`; then `u32 n` triangles of **21 bytes** (`u16 v0,v1,v2; s16 e01,e12,e20; float height; u8 unk; u16 flags; u16 coverFlags`) — Skyrim triangles are 16 bytes. Edge links, door links (`s16 tri; u32 CRC; formid door`), cover, waypoints, grid follow. libespm's vertex offsets (+8,+12,+16,+20) still hold [src: libespm/src/NAVM.cpp:27-39]. **Bug**: `NAVM::kType = "NVNM"` so `Browser` never indexes navmeshes [src: libespm/include/libespm/NAVM.h:14] → set to `"NAVM"`.

### 4.3 REFR / ACHR (placed objects)

Common placed-ref subrecords [xe: FO4.pas:11347-11868 (REFR), 4483-4620 (ACHR)]:

| Sig | Size | Layout (FO4) | vs Skyrim |
|---|---|---|---|
| `NAME` | 4 | formid base | same |
| `DATA` | 24 | `float pos[3]; float rot[3]` (radians) | same |
| `XSCL` | 4 | float scale | same |
| `XTEL` | 36 | `+0 formid door; +4 float pos[3]; +10 float rot[3]; +1C u32 flags (0x1 No Alarm, 0x2 No Load Screen, 0x4 Relative Position); +20 formid CELL transition interior` | Skyrim 32 (no transition interior). libespm reads first 28 bytes → OK |
| `XTNM` | 4 | formid MESG teleport loc name | new |
| `XLKR` | 8 (or 4) | `{formid keyword-or-ref; formid ref}`, repeating | same |
| `XESP` | 8 | `{formid parent; u8 flags (0x1 opposite state, 0x2 pop in); u8[3]}` | same |
| `XOWN` | **12** | `{formid owner (FACT/NPC_); u8[4] unused; u8 noCrime; u8[3]}` | Skyrim 4 bytes; reading first u32 still OK |
| `XRNK` | 4 | s32 faction rank | same |
| `XLOC` | 20 | `+0 u8 level (0 none,1,25,50 Advanced,75 Expert,100 Master,251 Barred,252 Chained,253 Requires Terminal,254 Inaccessible,255 Requires Key); +1 u8[3]; +4 formid KEYM; +8 u8 flags (0x4 leveled lock); +9 u8[3]; +0C u8[8] unknown` | level semantics differ (FO4 adds 251–254 and "terminal") |
| `XAPD` | 1 | u8 parent-activate-only | same |
| `XAPR` | 8 | `{formid ref; float delay}` repeating | same |
| `XCNT` | 4 | s32 item count | same |
| `XHLT` | 4 | u32 health % | new (was float in Skyrim? [inference]) |
| `XPRM` | 32 | `float bounds[3]; float rgb[3]; float unknown; u32 type (1 Box,2 Sphere,3 Plane,4 Line,5 Ellipsoid)` | same |
| `XMBO` | 12 | bound half extents | same |
| `XRDS` | 4 | float radius | same |
| `XEZN`, `XLCN`, `XLRL`, `XLRT[]` | 4 | encounter zone, persist location, location ref, loc-ref types | same |
| `XLIB` | 4 | formid LVLI (leveled item base) | new |
| `XLCM` | 4 | s32 level modifier (Easy…Very Hard) | same |
| `XACT` | 4 | u32 action flags (Use Default/Activate/Open/Open by default) | same |
| `ONAM` | 0 | open by default | same |
| `XMRK`,`FNAM`,`FULL`,`TNAM` | 0,1,4,2 | map marker: `FNAM` u8 (0x1 visible, 0x2 can travel, 0x4 show-all hidden, 0x8 use location name); `TNAM {u8 type (0..80 FO4 marker types: 1 City, 12 Sanctuary, 13 Settlement, 15 Vault, 57 Minutemen…); u8 unused}` | marker type list differs entirely; libespm `MapMarkerType` enum is Skyrim-only [src: libespm/include/libespm/REFR.h:40-102] |
| `XATR` | 4 | attach ref | new |
| `XWPG` + `XWPN`[] | 4 + 12 | workshop power grid `{node1, node2, line(BNDS ref)}` | new (settlements) |
| `XPLK` | 8 | spline connection `{ref; u32}` | new |
| `XRDO` | 16 | radio `{float freq, minWeak, maxWeak; u32 flags}` | new |
| `XLYR`, `XMSP`, `XRFG`, `XASP` | 4 | layer, material swap, reference group, acoustic | new/same |
| `XAMC` | 4 | u32 ammo count | new |
| `XCZR`/`XCZC`/`XCZA`, `XCVL`/`XCVR` | | water current data | new |

ACHR flags: 0x200 Starts Dead, 0x400 Persistent, 0x800 Initially Disabled, 0x2000 Starts Unconscious, 0x2000000 No AI Acquire, 0x20000000 Don't Havok Settle. ACHR has `NAME`(NPC_), `XLOC`, `XLCM`, `XCNT`, `XRDS`, `XHLT`, `XLKR`, `XAPD/XAPR`, `XESP`, ownership, `XSCL`, `DATA`.

Placed-projectile records `PARW PBAR PBEA PCON PFLA PGRE PHZD PMIS` share one layout [xe: FO4.pas:3209-3263, 5944-5951] — not needed by the server initially.

### 4.4 NPC_ (non-player character)

[xe: FO4.pas:10286-10479] Subrecord order: EDID, VMAD, OBND, PTRN, STCP, **ACBS**, SNAM[], INAM, VTCK, TPLT, LTPT, LTPC, TPTA, RNAM, SPCT+SPLO[], DEST, WNAM, ANAM, ATKR, attacks (ATKD/ATKE/…), SPOR/OCOR/GWOR/ECOR/FCPL/RCLR, PRKZ+PRKR[], PRPS, FTYP, NTRM, COCT+CNTO[], AIDT, PKID[], KSIZ/KWDA, APPR, OBTE…STOP, CNAM, FULL, SHRT, DATA(0), DNAM, PNAM[], HCLF, BCLF, ZNAM, GNAM, NAM5, NAM6, NAM7, NAM4, MWGT, NAM8, CS2H/CS2K/CS2D/CS2E/CS2F, CSCR, PFRN, DOFT, SOFT, DPLT, CRIF, FTST, QNAM, MSDK, MSDV, TETI/TEND[], MRSV, FMRI/FMRS[], FMIN, ATTX.

**ACBS (20 bytes)**

| Off | Type | Field |
|---|---|---|
| 0x00 | u32 | flags (below) |
| 0x04 | s16 | XP value offset |
| 0x06 | u16 | level, or level mult ×1000 if flag 0x80 (PC Level Mult) |
| 0x08 | u16 | calc min level |
| 0x0A | u16 | calc max level |
| 0x0C | s16 | disposition base |
| 0x0E | u16 | **template flags** (below) |
| 0x10 | u16 | bleedout override |
| 0x12 | u8[2] | unused |

Skyrim ACBS is also 20 bytes but `+0x04` = magicka offset, `+0x06` stamina offset, `+0x14`… — libespm reads magicka@4, stamina@6, templateFlags@18, health@20 [src: libespm/src/NPC_.cpp:29-34]; on FO4 `+18` is bleedout/unused and `+20` is **past the end**. Template flags are at **0x0E** in FO4.

ACBS flags (FO4): 0x1 Female, **0x2 Essential**, 0x4 Is CharGen Face Preset, 0x8 Respawn, 0x10 Auto-calc stats, **0x20 Unique**, 0x40 Doesn't affect stealth meter, 0x80 PC Level Mult, 0x100 Has Base Sound Data (runtime `kUsesTemplate` [clib: RE/A/ACTOR_BASE_DATA.h]), 0x200 Calc For Each Template, 0x400 Use Attack Percentage, **0x800 Protected**, 0x1000 No Loot, 0x2000 No Rumors, 0x4000 Summonable, 0x8000 Disable Non-Combat Regen, 0x10000 Doesn't bleed, 0x20000 Very Simple Actor, 0x40000 Has Bleedout Override, 0x80000 Swap Gender Anims, 0x100000 Simple Actor, 0x800000 No Activation/Hellos, 0x1000000 Diffuse Alpha Test, 0x2000000 Disable Combat, 0x4000000 Spawns Dead, 0x8000000 Player Protected, 0x10000000 Do Not Use Load Doors, 0x20000000 Is Ghost, 0x40000000 No Bleedout Recovery, 0x80000000 Invulnerable. (Essential/Unique/Protected bits are the same as Skyrim's, so the server's skip filter [src: WorldState.cpp:~472] stays valid.)

Template flags (bit → "use X from template"): 0 Traits, 1 Stats, 2 Factions, 3 Spell List, 4 AI Data, 5 AI Packages, 6 Model/Animation (runtime "unused"), 7 Base Data, 8 Inventory, 9 Script, 10 Def Pack List, 11 Attack Data, 12 Keywords [xe: Common.pas:8429-8450] [clib: ACTOR_BASE_DATA::TEMPLATE_USE_FLAG] — same bit values as libespm's enum [src: libespm/include/libespm/NPC_.h:22-58].

**Templates**: `TPLT` formid (NPC_/LVLN) = default template; `TPTA` (52 bytes) = 13 formids (NPC_/LVLN/BMMO/null), one per template-flag bit 0…12 [xe: FO4.pas:10353-10368]. Rule: for flag bit *b*, if set, take that data from `TPTA[b]` when non-null, else from `TPLT` [inference: matches runtime `baseTemplateForm` + `templateForms[]` in `TESActorBaseData` [clib: RE/T/TESActorBaseData.h:61-62]]. `LTPT`/`LTPC` = legendary template + GLOB chance. ⇒ `EvaluateTemplate<Flag>` must follow `TPTA[bit]` first, and `LeveledListUtils::EvaluateTemplateChain` (which follows only `TPLT`) needs a per-flag variant.

Other NPC_ fields:

| Sig | Size | Layout |
|---|---|---|
| `SNAM` | **5** | `formid FACT; s8 rank` (Skyrim 8 bytes; libespm reads rank from offset 0 — fix to +4) [xe: Common.pas:8801-8808] |
| `INAM` | 4 | death item LVLI |
| `VTCK` | 4 | voice VTYP |
| `RNAM` | 4 | race (required) |
| `SPCT`+`SPLO`[] | 4 + 4 | spells (SPEL/LVSP) |
| `WNAM` / `ANAM` | 4 | skin ARMO / far-away model ARMO |
| `ATKR` | 4 | attack race |
| `PRKZ`+`PRKR`[] | 4 + 5 | perks `{formid PERK; u8 rank}` |
| `PRPS` | 8·n | **actor value base values** `{AVIF, float}` (SPECIAL etc.) |
| `COCT`+`CNTO`[]+`COED` | | inventory |
| `AIDT` | 24 | `u8 aggression, confidence, energy, morality, mood, assistance; u8 aggroRadiusBehavior; u8 unk; u32 warn, warnAttack, attack; u8 noSlowApproach (fv≥29); u8[3]` |
| `PKID`[] | 4 | packages |
| `CNAM` | 4 | class (required) |
| `SHRT` | 4 | lstring short name |
| `DNAM` | 8 | `u16 calculatedHealth; u16 calculatedActionPoints; u16 farAwayModelDistance; u8 gearedUpWeapons; u8` |
| `PNAM`[] | 4 | head parts HDPT |
| `HCLF` / `BCLF` | 4 | hair color / facial hair color (CLFM) |
| `ZNAM` / `GNAM` | 4 | combat style / gift filter |
| `NAM6` / `NAM4` | 4 | float height min / height max; `NAM7` float unused; `NAM5` unknown (required) |
| `MWGT` | 12 | body weight `float thin, muscular, fat` |
| `NAM8` | 4 | u32 sound level |
| `PFRN` | 4 | power armor stand FURN |
| `DOFT` / `SOFT` | 4 | default / sleeping outfit OTFT |
| `DPLT` / `CRIF` | 4 | default package list FLST / crime faction |
| `FTST` | 4 | head texture TXST |
| `QNAM` | 16 | float RGBA texture lighting (skin tint) |
| `MSDK` / `MSDV` | 4·n / 4·n | **morph slider keys** u32 / values float (parallel arrays). Keys resolve against the race's `MSID` morph values / `MPPI` preset indices [xe: FO4.pas:2614-2700] |
| `TETI`+`TEND` | 4 + 7 | **tint layer**: `TETI {u16 dataType (1 Value/Color, 2 Value); u16 tintIndex (= RACE TETI index)}`, `TEND {u8 value (/100); u8 rgba[4]; s16 templateColorIndex}` (TEND may be 1 byte for value-only) |
| `MRSV` | 20 | body morph region values `float head, upperTorso, arms, lowerTorso, legs` |
| `FMRI`+`FMRS` | 4 + 28+ | **face morph region**: `FMRI u32 index` (resolves to race `FMRI`), `FMRS {float pos[3]; float rot[3]; float scale; u8[] unknown}` |
| `FMIN` | 4 | float facial morph intensity |

Health in FO4 is not "race starting health + offset": use `DNAM.calculatedHealth` / `calculatedActionPoints` (CK-computed) as base, or compute from Endurance/level when auto-calc [inference]. `GetBaseActorValues` must be rewritten accordingly [src: skymp5-server/cpp/server_guest_lib/GetBaseActorValues.cpp:25-87].

### 4.5 RACE

[xe: FO4.pas:11054-11345] Subrecords: EDID, STCP, FULL, DESC, SPCT/SPLO, WNAM skin, **BOD2 (4 bytes)**, keywords, **PRPS** (race AV defaults), APPR, **DATA**, MNAM/ANAM/MODT (male skeleton), FNAM/ANAM/MODT (female), NAM2, MTNM[], VTCK (2 formids m/f), HCLF (2 formids default hair color m/f), TINL u16, PNAM float, UNAM float, ATKR, attacks, NAM1/MNAM/FNAM body data (INDX+MODL per part), GNAM BPTD, NAM3 + behavior graphs (MNAM/FNAM + MODL), NAM4 impact material, NAM5 impact data set, NAM7, CNAM, NAM2, ONAM/LNAM corpse sounds, NAME[] biped object names, RBPC, MTYP+SPED[] movement overrides, VNAM u32 equipment flags, QNAM equip slots, UNWP unarmed WEAP, PHTN/PHWT phonemes, WKMV/SWMV/FLMV/SNMV movement defaults, then per sex: NAM0 + MNAM/FNAM, NNAM (12) neck fat, INDX+HEAD head parts, RPRM/RPRF presets, AHCM/AHCF hair colors, FTSM/FTSF face details, DFTM/DFTF default face texture, **tint groups** (TTGP name, options: TETI {u16 slot; u16 index}, TTGP, TTEF u16 flags, conditions, TTET[] textures, TTEB u32 blend op, TTEC n·14 `{formid CLFM; float alpha; u16 templateIndex; u32 blendOp}`, TTED float default; TTGE u32 category), **morph groups** (MPGN name, MPPC count, MPPI/MPPN/MPPM/MPPT/MPPF presets, MPPK u16 mask, MPGS u32[] slider indices), **face morphs** (FMRI u32 + FMRN lstring), WMAP; then NAM8 morph race, RNAM armor race, SRAC/SADD subgraph template/additive race, **subgraph data** [SAKD[] actor keywords, SGNM behaviour graph, SAPT[] animation paths, STKD[] target keywords, SRAF {u16 role (0 MT,1 Weapon,2 Furniture,3 Idle,4 Pipboy); u16 perspective (0 3rd,1 1st)}], PTOP/NTOP idle chatter, MSID+MSM0+MSM1 morph values, MLSI, HNAM/HLTX hair color LUTs, QSTI dialogue quest, BSMP… bone scale data.

**RACE.DATA** (200 bytes at fv ≥ 124; fields after a gated field shift if the record's fv is lower):

| Off | Type | Field |
|---|---|---|
| 0x00 | float | male height |
| 0x04 | float | female height |
| 0x08 | float[3] | male default weight thin/muscular/fat (**fv ≥ 109**) |
| 0x14 | float[3] | female default weight (fv ≥ 109) |
| 0x20 | u32 | flags (same bit table as Skyrim: 0x1 Playable, 0x2 FaceGen Head, 0x4 Child, … 0x10000000 Can Pickup Items, 0x40000000 Can Dual Wield) |
| 0x24 | float | acceleration rate |
| 0x28 | float | deceleration rate |
| 0x2C | u32 | size (0 Small … 3 Extra Large) |
| 0x30 | u8[8] | unknown |
| 0x38 | float | injured health % |
| 0x3C | s32 | shield biped object (slot 30.. = 0.., −1 none) |
| 0x40 | s32 | beard biped object (**fv ≥ 124**) |
| 0x44 | s32 | body biped object |
| 0x48 | float | aim angle tolerance |
| 0x4C | float | flight radius |
| 0x50 | float | angular acceleration rate |
| 0x54 | float | angular tolerance |
| 0x58 | u32 | flags 2 (0x1 adv. avoidance, 0x2 non-hostile, 0x4 floats, 0x200 ungendered, 0x1000 subsegmented damage, 0x40000 has facial rig, 0x80000 can use crippled limbs, 0x400000 cannot use playable items, …) |
| 0x5C | u8[36] | unknown |
| 0x80 | s32 | Pip-Boy biped object |
| 0x84 | s16 | XP value |
| 0x86 | float, u8, u8 | severable debris scale/count/decal count |
| 0x8C | float, u8, u8 | explodable debris scale/count/decal count |
| 0x92 | formid ×6 | severable explosion/debris/impact, explodable explosion/debris/impact |
| 0xAA | float,u8,u8,formid×3 | on-cripple data (**fv ≥ 96**) |
| 0xBC | formid | explodable subsegment explosion (**fv ≥ 118**) |
| 0xC0 | float | orientation limit pitch (fv ≥ 98) |
| 0xC4 | float | orientation limit roll (fv ≥ 101 xEdit / 98 Mutagen) |

[xe: FO4.pas:11069-11193] [mut: Major Records/Race.xml DATA]. There is **no** starting health/magicka/stamina, regen or unarmed damage in FO4 DATA — libespm's offsets +32…+100 [src: libespm/src/RACE.cpp:15-23] read flags at the right place (0x20) only by coincidence and garbage elsewhere. Base AVs come from race `PRPS` + AVIF `NAM0` defaults; unarmed damage from `UNWP` weapon.

### 4.6 WEAP

[xe: FO4.pas:12877-13032] [mut: Major Records/Weapon.xml] Subrecords: EDID, VMAD, OBND, PTRN, STCP, FULL, MODL…, ICON, MICO, EITM (+EAMT u16), DEST, ETYP (EQUP), BIDS, BAMT, YNAM, ZNAM, KSIZ/KWDA, DESC, **INRD**, **APPR**, **OBTE…STOP**, **NNAM** (embedded weapon OMOD), MOD4… (1st-person model), **DNAM**, **FNAM**, **CRDT**, INAM (impact data set), LNAM (NPC add-ammo LVLI), WAMD (AMDL aim model), WZMD (ZOOM), CNAM (template WEAP), **DAMA** (extra damage types: `{DMGT, u32}`), FLTR, MASE (u32 melee speed 0 Very Slow … 4 Very Fast). Record flags: 0x4 Non-Playable, 0x40000000 High-Res 1st Person Only. There is **no `DATA`** — value/weight/damage moved into DNAM (libespm `WEAP::Data::weapData` will be null → `GetWeightFromRecord` dereferences null [src: skymp5-server/cpp/server_guest_lib/GetWeightFromRecord.cpp:20-23]).

**DNAM (132 bytes, packed — note unaligned floats from 0x37)**

| Off | Type | Field |
|---|---|---|
| 0x00 | formid AMMO | ammo |
| 0x04 | float | speed (animation mult) |
| 0x08 | float | reload speed |
| 0x0C | float | reach |
| 0x10 | float | min range |
| 0x14 | float | max range |
| 0x18 | float | attack delay (s) |
| 0x1C | float | unused |
| 0x20 | float | damage out-of-range mult (default 0.5) |
| 0x24 | u32 | on hit (hit behaviour enum: 0 Normal, 1 Dismember Only, 2 Explode Only, 3 No Dismember/Explode [inference: Skyrim enum]) |
| 0x28 | formid AVIF | skill |
| 0x2C | formid AVIF | resist |
| 0x30 | u32 | flags: 0x1 Player Only, 0x2 NPCs Use Ammo, 0x4 No Jam After Reload, 0x8 Charging Reload, 0x10 Minor Crime, 0x20 Fixed Range, 0x40 Not Used In Normal Combat, 0x100 Crit Effect on Death, 0x200 Charging Attack, 0x800 Hold Input To Power, 0x1000 Non Hostile, 0x2000 Bound Weapon, 0x4000 Ignores Normal Weapon Resistance, **0x8000 Automatic**, 0x10000 Repeatable Single Fire, 0x20000 Can't Drop, 0x40000 Hide Backpack, 0x80000 Embedded Weapon, 0x100000 Not Playable, 0x200000 Has Scope, 0x400000 Bolt Action, 0x800000 Secondary Weapon, 0x1000000 Disable Shells |
| 0x34 | u16 | ammo capacity (magazine) |
| 0x36 | u8 | animation type: 0 HandToHandMelee, 1 OneHandSword, 2 OneHandDagger, 3 OneHandAxe, 4 OneHandMace, 5 TwoHandSword, 6 TwoHandAxe, 7 Bow, 8 Staff, **9 Gun**, 10 Grenade, 11 Mine |
| 0x37 | float | secondary damage |
| 0x3B | float | weight |
| 0x3F | u32 | value |
| 0x43 | u16 | base damage |
| 0x45 | u32 | sound level (0 Loud, 1 Normal, 2 Silent, 3 Very Loud, 4 Quiet [inference: Skyrim enum]) |
| 0x49…0x65 | formid SNDR ×8 | attack, attack 2D, attack loop, attack fail, idle, equip, unequip, fast equip |
| 0x69 | u8 | accuracy bonus |
| 0x6A | float | animation attack seconds |
| 0x6E | u8[2] | unknown |
| 0x70 | float | **action point cost** (VATS) |
| 0x74 | float | full power seconds |
| 0x78 | float | min power per shot |
| 0x7C | u32 | stagger (0 None, 1 Small, 2 Medium, 3 Large, 4 Extra Large) |
| 0x80 | u8[4] | unknown |

**FNAM (41 bytes)**: `0x00 float animFireSeconds; 0x04 float rumbleLeft; 0x08 float rumbleRight; 0x0C float rumbleDuration; 0x10 float animReloadSeconds; 0x14 u8[4] boltAnimSeconds; 0x18 float sightedTransitionSeconds; 0x1C u8 numProjectiles; 0x1D formid PROJ overrideProjectile; 0x21 u32 pattern (0 Constant,1 Square,2 Triangle,3 Sawtooth); 0x25 u32 rumblePeriodMs`.
**CRDT (12)**: `float critDamageMult (2.0); float critChargeBonus; formid SPEL critEffect`.
Fire rate (shots/s) for automatic weapons is derived from `speed`/`attackDelay`/`animFireSeconds`, and OMOD properties override them per instance [inference]. **Instance data**: a weapon's effective stats = base WEAP + attached OMODs (§4.12) — the server must model `baseId + OMOD list` as item identity.

### 4.7 ARMO / ARMA

[xe: FO4.pas:5786-5843] ARMO: flags 0x4 Non-Playable, 0x40 Shield; EDID, VMAD, OBND, PTRN, FULL, EITM, male (MOD2/MO2T/MODC/MO2S, ICON, MICO), female (MOD4…, ICO2, MIC2), **BOD2 (4)**, DEST, YNAM, ZNAM, ETYP, BIDS, BAMT, **RNAM** (race), KSIZ/KWDA, DESC, INRD, `INDX`(u16 addon index)+`MODL`(ARMA) pairs, **DATA**, **FNAM**, **DAMA** (resistances), TNAM (template ARMO), APPR, OBTE…STOP.

| Sig | Size | Layout |
|---|---|---|
| `DATA` | 12 | `s32 value; float weight; u32 health` (condition) |
| `FNAM` | 8 | `u16 armorRating; u16 baseAddonIndex; u8 staggerRating (enum); u8[3]` |
| `DAMA` | 8·n | `{formid DMGT; u32 amount}` — energy/rad/poison resist; physical DR = `FNAM.armorRating` |
| `BOD2` | **4** | `u32 bipedFlags` (§5). Skyrim: 8 (flags + armor type). libespm requires `dataSize >= 8` [src: libespm/src/ARMO.cpp:28] → FO4 BOD2 ignored; and **throws when DNAM missing** [src: ARMO.cpp:36] → every FO4 ARMO throws |

[mut: Major Records/Armor.xml FNAM] agrees. ARMA [xe: FO4.pas:5845-5889]: BOD2(4), RNAM race, DNAM (12: `u8 malePriority; u8 femalePriority; u8 weightSliderMale; u8 weightSliderFemale; u8[2]; u8 detectionSoundValue; u8; float weaponAdjust`), MOD2/MOD3 biped models, MOD4/MOD5 1st person, NAM0–NAM3 skin textures, MODL[] additional races, SNDD footstep set, ONAM art object.

### 4.8 AMMO

[xe: FO4.pas:5741-5767] Record flag 0x4 Non-Playable. `DATA` (8, optional from field 1): `u32 value; float weight` → weight at **+4** (libespm reads +0x10, "SSE only" [src: libespm/src/AMMO.cpp:13-14]). `DNAM` (16): `formid PROJ projectile; u8 flags (0x1 Ignores Normal Weapon Resistance, 0x2 Non-Playable, 0x4 Has Count Based 3D); u8[3]; float damage; u32 health` — `health` is the charge of **fusion cores / energy cells** (e.g. fusion core charge) [inference]. `ONAM` lstring short name, `NAM1` casing model + `NAM2` model info.

### 4.9 ALCH (ingestible)

[xe: FO4.pas:5691-5739] Record flag 0x20000000 Medicine. Keywords, models, ETYP, CUSD (crafting sound), DESC, `DATA` float weight, **`ENIT` (20)**: `s32 value; u32 flags (0x1 No Auto-Calc, 0x2 Food Item, 0x10000 Medicine, 0x20000 Poison); formid addiction (SPEL); float addictionChance; formid SNDR consumeSound`; `DNAM` lstring addiction name; effects `EFID`+`EFIT`+`CTDA`[]. libespm reads ENIT flags at +4 (correct) [src: libespm/src/ALCH.cpp:18-22]; Skyrim's ENIT is also 20 bytes but +8 = addiction formid in both.

### 4.10 MGEF / SPEL / ENCH / effects

**MGEF.DATA (152 bytes)** [xe: FO4.pas:10057-10204]:

| Off | Type | Field |
|---|---|---|
| 0x00 | u32 | flags (0x1 Hostile, 0x2 Recover, 0x4 Detrimental, 0x8 Snap to Navmesh, 0x10 No Hit Event, 0x100 Dispel w/ Keywords, 0x200 No Duration, 0x400 No Magnitude, 0x800 No Area, 0x1000 FX Persist, 0x4000 Gory Visuals, 0x8000 Hide in UI, 0x20000 No Recast, 0x200000 Power Affects Magnitude, 0x400000 Power Affects Duration, 0x4000000 Painless, 0x8000000 No Hit Effect, 0x10000000 No Death Dispel) |
| 0x04 | float | base cost |
| 0x08 | formid | associated item (LIGH/WEAP/ARMO/NPC_/HAZD/SPEL/RACE/ENCH/KYWD/DMGT depending on archetype) |
| 0x0C | u8[4] | unused (Skyrim: magic skill) |
| 0x10 | formid AVIF | resist value |
| 0x14 | u16 | counter effect count (then `ESCE`[] formids) |
| 0x16 | u8[2] | unused |
| 0x18 | formid LIGH | casting light |
| 0x1C | float | taper weight |
| 0x20/0x24 | formid EFSH | hit / enchant shader |
| 0x28 | u32 | minimum skill level |
| 0x2C/0x30 | u32 / float | spellmaking area / casting time |
| 0x34/0x38/0x3C | float | taper curve / taper duration / second AV weight |
| **0x40** | u32 | **archetype** (0 Value Modifier, 1 Script, 2 Dispel, 3 Cure Disease, 4 Absorb, 5 Dual Value Modifier, … 21 Paralysis, 28 Cure Addiction, 29 Cure Poison, 30 Concussion, **31 Stimpak**, 32 Accumulate Magnitude, 33 Stagger, 34 Peak Value Modifier, 35 Cloak, 37 Slow Time, 38 Rally, 39 Enhance Weapon, 40 Spawn Hazard, 41 Etherealize, 42 Banish, 43 Spawn Scripted Ref, 44 Disguise, **45 Damage**, 46 Immunity, 47 Permanent Reanimate, 48 Jetpack, 49 Chameleon) [clib: EffectArchetypes::ArchetypeID] |
| **0x44** | formid **AVIF** | primary actor value (Skyrim: s32 AV index) |
| 0x48/0x4C | formid | projectile / explosion |
| 0x50 | u32 | casting type (0 Constant, 1 Fire&Forget, 2 Concentration, 3 Scroll) |
| 0x54 | u32 | delivery (0 Self, 1 Touch, 2 Aimed, 3 Target Actor, 4 Target Location) |
| 0x58 | formid AVIF | second actor value |
| 0x5C/0x60/0x64 | formid | casting art / hit effect art / impact data |
| 0x68 | float | skill usage mult |
| 0x6C/0x70 | formid, float | dual-cast art / scale |
| 0x74…0x88 | formid ×6 | enchant art, hit visuals, enchant visuals, equip ability, image space mod, perk to apply |
| 0x8C | u32 | casting sound level |
| 0x90/0x94 | float | script effect AI score / delay |

Offsets are byte-identical to Skyrim, so the existing `MGEF::GetData` reads archetype@0x40 correctly, but **0x44 is now an AVIF formid** (needs `ToGlobalId`), not `espm::ActorValue` [src: libespm/src/MGEF.cpp:19-23]. FO4 has no Skyrim "magic" flags semantics for players; effects drive chems, perks, radiation, legendary effects.

**SPEL.SPIT (36)** [xe: FO4.pas:11972-12028]: `u32 baseCost; u32 flags (0x1 Manual Cost Calc, 0x20000 PC Start Spell, 0x40000 Instant Cast, 0x80000 Area Ignores LOS, 0x100000 Ignore Resistance, 0x200000 No Absorb/Reflect, 0x800000 No Dual Cast Mod); u32 type (0 Spell,1 Disease,2 Power,3 Lesser Power,4 Ability,5 Poison,10 Addiction,11 Voice); float chargeTime; u32 castType; u32 targetType; float castDuration; float range; formid PERK castingPerk` — same layout as Skyrim (libespm `SPITData` OK). SPEL also has OBND, FULL, keywords, ETYP, DESC.

**ENCH.ENIT (36, optional from field 8)** [xe: FO4.pas:6726-6750]: `s32 cost; u32 flags (0x1 No Auto-Calc, 0x4 Extend Duration On Recast); u32 castType; s32 enchantmentAmount; u32 targetType; u32 enchantType (6 Enchantment, 0xC Staff); float chargeTime; formid ENCH base; formid FLST wornRestrictions`. Used for legendary effects and armor/weapon effects.

**Effect** (`wbEffect`): `EFID` formid MGEF; `EFIT` (12) `float magnitude; u32 area; u32 duration`; optional `CTDA`[] [xe: FO4.pas:5068-5076]. Same as Skyrim.

### 4.11 KEYM / MISC / BOOK / NOTE / CMPO

| Record | Key fields |
|---|---|
| **KEYM** [xe: FO4.pas:9823] | flags 0x800 Calc Value From Components, 0x2000 Pack-In Use Only; FULL (required); `DATA` (8) `s32 value; float weight` |
| **MISC** [xe: FO4.pas:10212-10247] | same flags; `FIMD` featured message; `DATA` (8) `s32 value; float weight` (libespm reads weight @+4 ✔ [src: libespm/src/MISC.cpp:12]); **`CVPA` 8·n `{formid component (CMPO or any base object); u32 count}`** = what scrapping yields; `CDIX` u8[] display indices |
| **BOOK** [xe: FO4.pas:5891-5931] | `FIMD`; `DATA` (8) `u32 value; float weight`; **`DNAM` (13)** `u8 flags (0x1 Advance Actor Value, 0x2 Can't be Taken, 0x4 Add Spell, 0x10 Add Perk); formid teaches (AVIF / SPEL / PERK depending on flag); u32 textOffsetX; u32 textOffsetY`; `CNAM` lstring (DLSTRINGS) description; `INAM` STAT inventory art. libespm reads flags/teaches/weight from Skyrim `DATA` (+0,+4,+0xC) [src: libespm/src/BOOK.cpp:18-24] → FO4: flags & teaches from `DNAM`, weight `DATA+4`. Perk magazines = flag 0x10 + PERK |
| **NOTE** (holotapes) [xe: FO4.pas:12470-12499] | `DNAM` u8 type (0 Sound, 1 Voice, 2 Program, 3 Terminal); `DATA` (8) `u32 value; float weight` (DNAM before fv 65); `SNAM` (4) union: Sound→SNDR, Voice→SCEN, Terminal→TERM, Program→unused; `PNAM` z program file (SWF) |
| **CMPO** [xe: FO4.pas:12294-12302] | OBND, FULL, CUSD, `DATA` u32 auto-calc value, `MNAM` formid MISC **scrap item**, `GNAM` formid GLOB **mod scrap scalar** |

### 4.12 OMOD (object modification)

[xe: FO4.pas:12501-12544; properties 5458-5497; deciders 2360-2427] [clib: RE/B/BGSMod.h] Record flags: 0x10 Legendary Mod, 0x80 Mod Collection. Subrecords: EDID, FULL, DESC, model, **DATA**, `MNAM` 4·n target OMOD keywords, `FNAM` 4·n filter keywords, `LNAM` formid loose mod (MISC item you hold in inventory), `NAM1` u8 priority, `FLTR`.

**OMOD.DATA** (variable):

| Off | Type | Field |
|---|---|---|
| 0x00 | u32 | include count |
| 0x04 | u32 | property count |
| 0x08 | u8 | unknown bool 1 |
| 0x09 | u8 | unknown bool 2 |
| 0x0A | u32 | form type as signature: `'ARMO'`, `'NPC_'`, `'WEAP'`, `'NONE'` |
| 0x0E | u8 | max rank |
| 0x0F | u8 | level tier scaled offset |
| 0x10 | formid KYWD | **attach point** (keyword this mod plugs into; must match a parent's `APPR`) |
| 0x14 | u32 n | attach parent slot count, then `n × formid KYWD` (slots this mod provides to children) |
| … | u32 m | legacy items count, then `m × 8` bytes |
| … | 7·include count | includes `{formid OMOD; u8 minLevel; u8 optional; u8 dontUseAll}` |
| … | 24·property count | properties (below) |

**Property entry (24 bytes)**:

| Off | Type | Field |
|---|---|---|
| 0x00 | u8 | value type: 0 Int, 1 Float, 2 Bool, 3 String, 4 FormID+Int, 5 Enum, 6 FormID+Float |
| 0x01 | u8[3] | unused |
| 0x04 | u8 | function: Int/Float/FormID+Float → 0 SET, 1 MUL+ADD, 2 ADD; Bool → 0 SET, 1 AND, 2 OR; Enum → 0 SET; FormID+Int → 0 SET, 1 REM, 2 ADD |
| 0x05 | u8[3] | unused |
| 0x08 | u16 | property index into the target type's enum (below) |
| 0x0A | u8[2] | unused |
| 0x0C | 4 | value 1: s32 / float / u32 bool / formid (types 4, 6) / enum |
| 0x10 | 4 | value 2: (Int) s32, (Float) float, (Bool) u32, (FormID+Int) s32 count, (FormID+Float) float |
| 0x14 | float | step |

Runtime: `BGSMod::Property::OP {kSet=0, kMul/kAnd/kRem=1, kAdd/kOr=2}`, `TYPE {Int,Float,Bool,String,Form,Enum,Pair}` [clib]. MUL+ADD = `base × (1 + value)` (percentage) [web: https://falloutck.uesp.net/wiki/Object_Mod (summary: "MUL+ADD multiplies the BASE value by the number entered, then adds it")]. For a float with MUL+ADD, value1 is the multiplier and value2 is unused/second operand [inference].

Property enums [xe: FO4.pas:5338-5456]:
- **Weapon** (form type WEAP): 0 Speed, 1 Reach, 2 MinRange, 3 MaxRange, 4 AttackDelaySec, 6 OutOfRangeDamageMult, 7 SecondaryDamage, 8 CriticalChargeBonus, 9 HitBehaviour, 10 Rank, 12 AmmoCapacity, 15 Type, 16 IsPlayerOnly, 17 NPCsUseAmmo, 18 HasChargingReload, 19 IsMinorCrime, 20 IsFixedRange, 21 HasEffectOnDeath, 22 HasAlternateRumble, 23 IsNonHostile, 24 IgnoreResist, 25 IsAutomatic, 26 CantDrop, 27 IsNonPlayable, 28 AttackDamage, 29 Value, 30 Weight, 31 Keywords, 32 AimModel, 33–47 AimModel* (cone/recoil params), 48 HasScope, 49 ZoomDataFOVMult, 50 FireSeconds, 51 NumProjectiles, 52–58 sounds, 59 SoundLevel, 60 ImpactDataSet, 61 Ammo, 62 CritEffect, 63 BashImpactDataSet, 64 BlockMaterial, 65 Enchantments, 66 AimModelBaseStability, 67 ZoomData, 68 ZoomDataOverlay, 69 ZoomDataImageSpace, 70–72 ZoomDataCameraOffsetX/Y/Z, 73 EquipSlot, 74 SoundLevelMult, 75 NPCAmmoList, 76 ReloadSpeed, 77 DamageTypeValues, 78 AccuracyBonus, 79 AttackActionPointCost, 80 OverrideProjectile, 81 HasBoltAction, 82 StaggerValue, 83 SightedTransitionSeconds, 84 FullPowerSeconds, 85 HoldInputToPower, 86 HasRepeatableSingleFire, 87 MinPowerPerShot, 88 ColorRemappingIndex, 89 MaterialSwaps, 90 CriticalDamageMult, 91 FastEquipSound, 92 DisableShells, 93 HasChargingAttack, 94 ActorValues.
- **Armor**: 0 Enchantments, 1 BashImpactDataSet, 2 BlockMaterial, 3 Keywords, 4 Weight, 5 Value, 6 Rating, 7 AddonIndex, 8 BodyPart, 9 DamageTypeValue, 10 ActorValues, 11 Health, 12 ColorRemappingIndex, 13 MaterialSwaps.
- **Actor** (NPC_): 0 Keywords, 1 ForcedInventory, 2 XPOffset, 3 Enchantments, 4 ColorRemappingIndex, 5 MaterialSwaps.

Attachment model: an item has `APPR` slots (keywords); an OMOD whose `DATA.attachPoint` equals one of the parent's slots can attach there; the OMOD's own attach-parent-slot list opens sub-slots (e.g. receiver → muzzle). `MNAM` target keywords restrict which OMODs it can co-exist with; `FNAM` filter keywords match the parent's keywords. Server validation must replicate this tree when players mod at workbenches [inference].

### 4.13 COBJ (constructible object / recipe)

[xe: FO4.pas:10249-10284]

| Sig | Size | Layout |
|---|---|---|
| `YNAM`/`ZNAM` | 4 | sounds |
| **`FVPA`** | 8·n | components `{formid (CMPO, MISC, ALCH, …); u32 count}` — replaces Skyrim's `COCT`/`CNTO` |
| `DESC` | 4 | lstring |
| `CITC`+`CTDA`[] | | conditions |
| `CNAM` | 4 | created object (any base object, incl. OMOD for mod recipes) |
| `BNAM` | 4 | workbench keyword (KYWD) |
| `NAM1`,`NAM2`,`NAM3` | | **unused** (Skyrim NAM1 = count; libespm reads it [src: libespm/src/COBJ.cpp:23]) |
| `ANAM` | 4 | menu art object (ARTO) |
| `FNAM` | 4·n | category keywords (workbench menu tabs) |
| `INTV` | 4 | `u16 createdObjectCount (default 1); u16 priority` (optional from field 1) |

Crafting semantics: required items are CMPO components; the game satisfies a CMPO requirement from loose CMPO-bearing MISC items by scrapping (MISC `CVPA`) and from the component's `MNAM` scrap item [inference]. Workbench FURN keywords come from FURN `KWDA` (as today in `CraftService::OnCraftItem` [src: skymp5-server/cpp/server_guest_lib/CraftService.cpp:53-57]); Skyrim temper bench IDs `0xadb78`/`0x88108` [src: CraftService.cpp:84-93] must go.

### 4.14 CONT / LVLI / LVLN / OTFT

**CONT** [xe: FO4.pas:6176-6212]: flags 0x8000 Has Distant LOD, 0x10000 Random Anim Start, 0x2000000 Obstacle; COCT+CNTO[]+COED, DEST, **`DATA` (5)** `u8 flags (0x1 Allow Sounds When Animating, 0x2 Respawns, 0x4 Show Owner); float weight`, keywords, FTYP, NTRM, PRPS, `SNAM`/`QNAM`/`TNAM` sounds open/close/take-all, `ONAM` filter list FLST. `GetContainerObjects` ignores COED [src: libespm/src/Utils.cpp:150-160] — fine for MVP.

**LVLI / LVLN** [xe: FO4.pas:9979-10031; LVLO Common.pas:9062-9081]:

| Sig | Size | Layout |
|---|---|---|
| `OBND` | 12 | |
| `LVLD` | 1 | u8 chance none (required) |
| `LVLM` | 1 | u8 max count (always 0) |
| `LVLF` | 1 | u8 flags: 0x1 Calc from all levels ≤ PC level, 0x2 Calc for each item in count, 0x4 Use All (LVLN: "Calculate All", still picks one) |
| `LVLG` | 4 | formid GLOB chance-none global |
| `LLCT` | 1 | u8 entry count |
| `LVLO` | **12** | `u16 level; u8[2]; formid ref; u16 count; u8 chanceNone; u8` |
| `COED` | 12 | optional, after each LVLO |
| `LLKC` | 8·n | filter keyword chances `{formid KYWD; u32 chance}` |
| `LVSG` | 4 | (LVLI) formid GLOB epic loot chance |
| `ONAM` | 4 | (LVLI) lstring override name |
| `MODL`… | | (LVLN) model |

Skyrim LVLO is `u16 level; u8[2]; formid; u16 count; u8[2]`. libespm reads `Entry {u32 level; u32 formId; u32 count}` contiguously after LLCT [src: libespm/include/libespm/LeveledListBase.h:22-29, src/LeveledListBase.cpp:22-24] → on FO4 `count` = `count | chanceNone<<16 | …` and any `COED` breaks the stride. Parse each LVLO field individually; apply per-entry chance-none. `LeveledListUtils` treats `chanceNoneGlobalId != 0` as 100% none [src: skymp5-server/cpp/server_guest_lib/LeveledListUtils.cpp:35] — read the GLOB value instead.

**OTFT** [xe: FO4.pas:9357-9360]: `INAM` 4·n formid (ARMO/LVLI) — same as Skyrim (libespm OK, but `count = dataSize / sizeof(dataSize)` happens to equal /4 [src: libespm/src/OTFT.cpp:13]).

### 4.15 FACT / QUST

**FACT** [xe: FO4.pas:6785-6846]: `XNAM` (12) `{formid FACT/RACE; s32 modifier; u32 combat reaction 0 Neutral,1 Enemy,2 Ally,3 Friend}`; `DATA` u32 flags (same bits as Skyrim; 0x4000 Vendor, 0x8000 Can Be Owner); JAIL/WAIT/STOL/PLCN/CRGR/JOUT; **`CRVA` (20)** `u8 arrest; u8 attackOnSight; u16 murder, assault, trespass, pickpocket, unknown; float stealMult; u16 escape; u16 werewolf` (optional from field 7 → 12/16/20 bytes); ranks `RNAM` u32 + `MNAM`/`FNAM` lstring titles; **vendor** `VEND` FLST buy/sell list, `VENC` REFR merchant container, `VENV` (12) `u16 startHour, endHour, radius; u8[2]; u8 buysStolen; u8 buySellEverythingNotInList; u8 buysNonStolen; u8`; `PLVD` location; CITC+CTDA. libespm FACT is compatible.

**QUST** [xe: FO4.pas:10684-…]: `DNAM` (12) `u16 flags (0x1 Start Game Enabled, 0x2 Completed, 0x10 Starts Enabled, 0x20 Displayed In HUD, 0x40 Failed, 0x100 Run Once, 0x800 Active, …); u8 priority; u8; float delayTime; u8 type (0 None,1 Main,2 BoS,3 Institute,4 Minutemen,5 Railroad,6 Misc,7 Side,8–14 DLC01–07); u8[3]` (libespm's `QuestData` is Skyrim-shaped), `ENAM` u32 event, `LNAM` location, `XNAM` GLOB completion XP, `QTGL`[], `FLTR`, dialogue/story conditions, stages `INDX` (4) `{u16 stage; u8 flags; u8}` + log entries `QSDT` u8, CTDA, `NAM2` note, `CNAM` lstring (DLSTRINGS), `NAM0` next quest; objectives, aliases (ALST/ALLS… as Skyrim plus FO4 additions). Server: brief/out of scope initially.

### 4.16 TERM

[xe: FO4.pas:12671-12764] VMAD (fragmented, PERK-style), OBND, PTRN, `NAM0` lstring header, `WNAM` lstring welcome, FULL, model, keywords, PRPS, `PNAM`, `SNAM` looping sound, `FNAM` u16 flags, `COCT` u32 + `CNTO`[] `{formid NOTE holotape; s32 count}`, `MNAM` u32 furniture marker flags, `WBDT`, `XMRK`, `SNAM` marker params, **`BSIZ` u32 + body text [`BTXT` lstring + CTDA[]]**, **`ISIZ` u32 + menu items [`ITXT` lstring item text, `RNAM` lstring response, `ANAM` u8 type (4 Submenu-Terminal, 5 Return to top, 6 Force redraw, 8 Display text, 16 Display image), `ITID` u16 item ID, `UNAM` lstring display text, `VNAM` z image, `TNAM` formid sub-TERM, CTDA[]]**. Menu item execution is a Papyrus fragment (VMAD) keyed by `ITID` [inference].

### 4.17 DOOR / FURN / ACTI / FLOR / LIGH / statics

| Record | Key fields (FO4) |
|---|---|
| **DOOR** [xe: FO4.pas:6477-6508] | flags 0x10 Non Occluder, 0x800000 Is Marker; NTRM; `SNAM`/`ANAM`/`BNAM` open/close/loop sounds; `FNAM` u8 (0x2 Automatic, 0x4 Hidden, 0x8 Minimal Use, 0x10 Sliding, 0x20 Do Not Open in Combat Search, 0x40 No "To" Text); `ONAM`/`CNAM` lstring alt open/close text; `TNAM`[] random teleport destinations (CELL/WRLD). Load-door destination lives on the REFR `XTEL` |
| **FURN** [xe: FO4.pas:6848-6919] | flags 0x4 Has Container, 0x80 Is Perch, 0x800000 Is Marker, **0x2000000 Power Armor**, 0x10000000 Must Exit To Talk, 0x20000000 Child Can Use; keywords (workbench KYWDs), PRPS, NTRM, FTYP, `PNAM`, `WNAM` water, ATTX, `FNAM` u16, CITC+CTDA, COCT+CNTO, **`MNAM` u32** active markers (bits 0–21 interaction points; 0x400000 Allow Awake Sound, 0x800000 Enter With Weapon Drawn, 0x1000000 Play Anim When Full, 0x2000000 Disables Activation, 0x4000000 Is Perch, 0x8000000 Must Exit to Talk, 0x80000000 Is Sleep Furniture), **`WBDT` (2)** `u8 benchType (0 None, 1 Create Object, 2 Weapons, 5 Alchemy (chem/cooking), 7 Armor, 8 Power Armor, 9 Robot Mod); u8`, `NAM1` associated form (ARMO/WEAP/PERK/SPEL/HAZD), markers `ENAM` s32 + `NAM0` (4) `{u8[2]; u16 disabledEntryPoints}`, `FNPR` (4) `{u16 animType; u16 entryPoints}`, `XMRK` marker model, `SNAM` n × marker `{float x,y,z; float rotZ; formid KYWD; u8 entryTypes (fv≥125); u8[3]}`, APPR, OBTE (power-armor frame furniture uses an object template), NVNM |
| **ACTI** [xe: FO4.pas:5611-5670] | flags incl. 0x20000 Dangerous, 0x800000 Is Marker; PRPS, NTRM, FTYP, `PNAM`, `SNAM`/`VNAM` sounds, `WNAM` water, ATTX, `FNAM` u16 (0x1 No Displacement, 0x2 Ignored by Sandbox, 0x10 Is a Radio), `KNAM` interaction keyword, `RADR` (14) `{formid SOPM; float frequency; float volume; u8 startsActive; u8 noSignalStatic}`, CITC+CTDA, NVNM |
| **FLOR** [xe: FO4.pas:12167-12197] | FULL (required), keywords, PRPS, ATTX, `RNAM` lstring, `FNAM` u16, **`PFIG` formid harvest result** (ALCH/AMMO/ARMO/BOOK/INGR/KEYM/LIGH/LVLI/MISC/NOTE/WEAP), `SNAM` harvest sound, `PFPC` (4) seasonal production `u8 spring, summer, fall, winter` |
| **TREE** [xe: FO4.pas:12134-12165] | `PFIG`, `SNAM`, `PFPC`, then `FULL`, `CNAM` (48 bytes of floats) |
| **LIGH** [xe: FO4.pas:9871-9944] | **`DATA` (64)**: `s32 time; u32 radius; u8 color[4]; u32 flags (0x2 Can be Carried, 0x8 Flicker, 0x20 Off By Default, 0x80 Pulse, 0x400/0x800/0x1000 shadow spot/hemi/omni, 0x4000 NonShadow Spotlight, 0x8000 Non Specular, 0x10000 Attenuation Only, 0x20000 NonShadow Box, …); float falloffExponent @0x10; float fov @0x14; float nearClip @0x18; float flickerPeriod @0x1C; float flickerIntensityAmp @0x20; float flickerMovementAmp @0x24; float constant @0x28; float scalar @0x2C; float exponent @0x30; float godRaysNearClip @0x34; u32 value @0x38; float weight @0x3C`. Skyrim DATA is 48 bytes (value@0x28, weight@0x2C); libespm also has typos (0x0F, 0x1F) [src: libespm/src/LIGH.cpp:18,23]. Then `FNAM` float fade, `NAM0` gobo, `SNAM`, `LNAM` lens, `WGDR` god rays |
| **STAT** [xe: FO4.pas:12045-12089] | PTRN, FTYP, model, PRPS, FULL, `DNAM` (16) `{float maxAngle; formid MATO; float leafAmp; float leafFreq}`, NVNM, `MNAM` 4×260-byte LOD paths |
| **SCOL** [xe: FO4.pas:12627-12648] | model, FULL, FLTR, parts: `ONAM` formid static + `DATA` n × `{float pos[3]; float rot[3]; float scale}` [inference: wbStaticPart] |
| **MSTT** [xe: FO4.pas:7067-7096] | model, DEST, keywords, PRPS, `DATA` u8 on-local-map, `SNAM` looping sound |
| **PKIN** (pack-in) [xe: FO4.pas:12571-12580] | flag 0x200 Prefab; OBND, FLTR, `CNAM` formid CELL (a hidden cell whose refs are the pack contents), `VNAM` u32 version. Placing a PKIN = instantiating all refs of that cell relative to the PKIN ref [inference] |
| **INNR** [xe: FO4.pas:12337-12385] | `UNAM` u32 target (0x1D Armor, 0x2D Actor, 0x2A Furniture, 0x2B Weapon = ENUM_FORM_ID); rulesets: `VNAM` u32 count, names [`WNAM` lstring text, KSIZ/KWDA keywords, `XNAM` (6) `{float value; u8 target (armor property enum); u8 op (0 ≥,1 >,2 ≤,3 <,4 =)}`, `YNAM` u16 index]. Used client-side for item names like "Legendary Combat Rifle" — server only needs it to render names [inference] |

### 4.18 PERK / HAZD / EXPL / PROJ / IDLE / AACT

| Record | Key data |
|---|---|
| **PERK** [xe: FO4.pas:7582-7690] | flag 0x4 Non-Playable; VMAD fragments, FULL, DESC, `ICON`, conditions, **`DATA` (3–5)** `u8 trait; u8 level; u8 numRanks; u8 playable (size≥4); u8 hidden (size≥5)`, `SNAM`, `NNAM` next perk (rank chain), `FNAM` SWF; effects: `PRKE` (3) `{u8 type 0 Quest+Stage,1 Ability,2 Entry Point; u8 rank; u8 priority}` + `DATA` union (quest+u16 stage / SPEL / `{u8 entryPoint; u8 function (1 Set,2 Add,3 Multiply,4 Add Range,5 Add AV Mult,6 Abs,7 −Abs,8 Add LVLI,9 Add Activate Choice,10 Select Spell,11 Select Text,12 Set to AV Mult,13 Mul AV Mult,14 Mul 1+AV Mult,15 Set Text); u8 tabCount}`), `PRKC` s8 + CTDA[], `EPFT` u8 param type (0 None,1 Float,2 Float/AV,Float,3 LVLI,4 SPEL+lstring+flags,5 SPEL,6 string,7 lstring,8 AVIF), `EPFB` u16, `EPF2` lstring, `EPF3` u16, `EPFD` data, `PRKF` end. Entry point enum: `wbEntryPointsEnum` in FO4.pas |
| **HAZD** [xe: FO4.pas:7180-7210] | `MNAM` IMAD; **`DNAM` (52)** `u32 limit; float radius; float lifetime; float imageSpaceRadius; float targetInterval; u32 flags (0x1 Affects Player Only, 0x2 Inherit Duration, 0x4 Align to Impact Normal, 0x8 Inherit Radius, 0x10 Drop to Ground, 0x20 Taper Effectiveness); formid effect (SPEL/ENCH) @0x18; formid light; formid impact data set; formid sound; float fullEffectRadius @0x28; float taperWeight; float taperCurve` — radiation zones are usually HAZD spawned refs or trigger ACTIs with MGEF scripts [inference] |
| **EXPL** [xe: FO4.pas:7334-7386] | EITM, `MNAM`; **`DATA` (84 at fv≥112)**: `formid light, sound1, sound2, impactDataSet, placedObject, spawnProjectile; float force @0x18; float damage @0x1C; float innerRadius (fv≥97) @0x20; float outerRadius; float isRadius; float verticalOffsetMult; u32 flags @0x30; u32 soundLevel; float placedObjAutoFadeDelay (fv≥70); u32 stagger (fv≥91); float spawnX,Y,Z, spreadDeg; u32 spawnCount (fv≥112)` — offsets shift for older fv |
| **PROJ** [xe: FO4.pas:7115-7178] | **`DNAM` (93)**: `u16 flags (0x1 Hitscan, 0x2 Explosion, 0x4 Alt Trigger, 0x8 Muzzle Flash, 0x20 Can Be Disabled, 0x40 Can Be Picked Up, 0x80 Supersonic, 0x100 Pins Limbs, 0x200 Pass Through Small Transparent, 0x400 Disable Combat Aim Correction, 0x800 Penetrates Geometry, 0x1000 Continuous Update, 0x2000 Seeks Target); u16 type (1 Missile, 2 Lobber, 4 Beam, 8 Flame, 0x10 Cone, 0x20 Barrier, 0x40 Arrow); float gravity @4; float speed @8; float range @0xC; formid light; formid muzzleFlashLight; float altTriggerProximity; float altTriggerTimer; formid EXPL @0x20; formid sound; float muzzleFlashDuration; float fadeDuration; float impactForce @0x30; formid soundCountdown; formid soundDisable; formid defaultWeaponSource @0x3C; float coneSpread @0x40; float collisionRadius; float lifetime @0x48; float relaunchInterval; formid TXST decal; formid COLL; u8 tracerFrequency @0x58; formid vatsProjectile @0x59`; `NAM1`/`NAM2` muzzle flash model; `VNAM` u32 sound level |
| **IDLE** [xe: FO4.pas:9646-9670] | CTDA[], `DNAM` z behavior graph, `ENAM` z **animation event**, `ANAM` (8) `{formid parent (AACT/IDLE); formid previous}`, `DATA` (6) `{u8 loopMin; u8 loopMax; u8 flags (0x1 Loose, 0x2 Sequence, 0x4 No Attacking); u8 animGroupSection; u16 replayDelay}`, `GNAM` z animation file |
| **AACT** [xe: FO4.pas:6970-6982] | `CNAM` u8[4] color, `DNAM` z notes, `TNAM` u32 type, `DATA` formid AORU, FULL — actions (e.g. ActionFire, ActionReload) referenced by IDLE trees and animation events |

### 4.19 Object templates (`OBTE` … `STOP`) — default OMOD combinations

[xe: FO4.pas:5499-5528] Present on WEAP, ARMO, NPC_, FURN. `OBTE` u32 combination count; per combination: optional `OBTF` (empty, editor only), `FULL` lstring name, **`OBTS`**; closed by `STOP` (empty, required).

**OBTS** (variable):

| Off | Type | Field |
|---|---|---|
| 0x00 | u32 | include count |
| 0x04 | u32 | property count |
| 0x08 | u8 | level min |
| 0x09 | u8 | unused |
| 0x0A | u8 | level max |
| 0x0B | u8 | unused |
| 0x0C | s16 | parent combination index (−1 none) |
| 0x0E | u8 | default (bool) — the combination used when the item is spawned without explicit mods |
| 0x0F | u8 k | keyword count, then `k × formid KYWD` |
| … | u8, u8 | min level for ranks, alt levels per tier |
| … | 7·includes | `{formid OMOD; u8 attachPointIndex; u8 optional; u8 dontUseAll}` |
| … | 24·props | properties, same 24-byte struct as OMOD (§4.12) |

Leveled weapons in vendors/loot pick a combination by level and keywords (LVLI `LLKC` filter keyword chances interact with OBTS keywords; CTDA function 741 `ObjectTemplateItem_HasKeyword`) [inference].

### 4.20 CTDA (conditions)

32 bytes, same layout as Skyrim [xe: FO4.pas:5183-5254]:

| Off | Type | Field |
|---|---|---|
| 0x00 | u8 | type: bits 5–7 operator (0 ==, 1 !=, 2 >, 3 >=, 4 <, 5 <=); bit0 OR, bit1 Use Aliases, bit2 **Use Global** (comparison is a GLOB formid), bit3 Use Packdata, bit4 Swap Subject/Target [xe: Common.pas:3494-3507] |
| 0x01 | u8[3] | unused |
| 0x04 | float / formid | comparison value |
| 0x08 | u16 | **function index** (FO4 table, Appendix A) |
| 0x0A | u8[2] | unused |
| 0x0C | 4 | param 1 (formid / s32 / float / enum per function) |
| 0x10 | 4 | param 2 |
| 0x14 | u32 | run on: 0 Subject, 1 Target, 2 Reference, 3 Combat Target, 4 Linked Reference, 5 Quest Alias, 6 Package Data, 7 Event Data, **8 Command Target, 9 Event Camera Ref, 10 My Killer** (8–10 new) |
| 0x18 | formid | reference (when run on = Reference) |
| 0x1C | s32 | param 3 (alias / event-data member; −1 default) |

String params follow as `CIS1`/`CIS2` zstrings; FO4 records preface condition lists with `CITC` u32 count. libespm `CTDA` struct matches [src: libespm/include/libespm/CTDA.h:56-68]. Param types are formids for actor value (`AVIF`!), keyword, race, perk, etc. — remap with `ToGlobalId`.

Commonly used FO4 condition functions (index, params) [xe: FO4.pas:262-740]:

| Idx | Function | Params | SkyMP has a Skyrim impl.? |
|---|---|---|---|
| 14 | **GetValue** (= GetActorValue) | AVIF | via 640 |
| 47 | GetItemCount | base object | ✔ (47) |
| 56/58/59 | GetQuestRunning / GetStage / GetStageDone | QUST (, stage) | |
| 69 | GetIsRace | RACE | ✔ (69) |
| 70 | GetIsSex | sex | |
| 71 / 73 | GetInFaction / GetFactionRank | FACT | |
| 72 | GetIsID | base object | |
| 74 | GetGlobalValue | GLOB | |
| 80 | GetLevel | — | |
| 101 | IsWeaponMagicOut | — | ✔ (101) |
| 182 | GetEquipped | base object | ✔ (182) |
| 214 | HasMagicEffect | MGEF | |
| 254 | GetIsPlayableRace | — | ✔ (254) |
| 263 | IsWeaponOut | — | ✔ (263) |
| 264 | HasSpell | effect item | ✔ (264) |
| 277 | GetBaseValue | AVIF | |
| 286 / 287 / 289 | IsSneaking / IsRunning / IsInCombat | — / — / int | |
| 300 | IsInInterior | — | ✔ (300) |
| 354 / 476 / 641 | IsEssential / IsProtected / IsUnique | — | |
| 372 | IsInList | FLST | |
| 398 | IsWeaponInList | FLST | |
| 448 | HasPerk | PERK | |
| 494 | GetPermanentValue | AVIF | |
| 560 | **HasKeyword** | KYWD | |
| 562 | LocationHasKeyword | KYWD | |
| 568 | IsSprinting | — | |
| 569 | IsBlocking | — | ✔ (569) |
| 596 | SpellHasKeyword | casting source, KYWD | ✔ (596) |
| 597 | GetEquippedItemType | casting source | ✔ (597) |
| 640 | **GetValuePercent** | AVIF | ✔ (640, as GetActorValuePercent) |
| 682 | **WornHasKeyword** | KYWD | ✔ (682) |
| 722 | WornApparelHasKeywordCount | KYWD | ✔ (722) |
| 736 | EPIsDamageType | DMGT | |
| 741 / 743 | ObjectTemplateItem_HasKeyword / _GetLevel | KYWD / — | |
| 756 | GetLoadedAmmoCount | — | |
| 792 | IsInWorkshopMode | — | |

The indices the server already implements happen to have the **same index and meaning** in FO4 (47, 69, 101, 182, 254, 263, 264, 300, 569, 596, 597, 640, 682, 722) [src: skymp5-server/cpp/server_guest_lib/condition_functions/*.cpp `GetFunctionIndex`] — but their parameters change (AVIF formids instead of AV indices for 640; "magic" ones are mostly irrelevant). There is **no `IsInPowerArmor` condition function** in FO4's table; use `WornHasKeyword(ArmorTypePower)` or the PA furniture state [xe table has no such entry].

### 4.21 VMAD (script attachments), FO4 version

[xe: FO4.pas:3698-3900] `s16 version` (FO4 writes **6**), `s16 objectFormat` (**2**), `u16 scriptCount`, scripts: `{u16 len + name; u8 flags (0 Local,1 Inherited,2 Removed,3 Inherited+Removed); u16 propertyCount; properties}`. Property: `{u16 len + name; u8 type; u8 flags (1 Edited, 3 Removed); value}`. Types: 1 Object (format 2: `u16 unused; s16 alias; formid` = 8 bytes), 2 String (`u16 len + bytes`), 3 Int32, 4 Float, 5 Bool (u8), **6 Variable (no payload)**, **7 Struct** (`u32 memberCount` + members encoded like properties, recursive), 11–15 arrays (`u32 count` + elements), **16 Array of Variable** (`u32 count`, no elements stored), **17 Array of Struct** (`u32 count` + structs). Fragment variants (QUST/PERK/TERM/INFO/SCEN/PACK) append fragment data after the scripts.

libespm throws on types 6/7/16/17 [src: libespm/src/Utils.cpp:134-140] from inside `RecordHeader::GetScriptData`, which is `noexcept` [src: libespm/include/libespm/RecordHeader.h:29-31] → `std::terminate` on the first FO4 record with a struct property. Add the cases, and make `GetScriptData` either non-`noexcept` or catch internally.

---

## 5. FO4 biped slots (BOD2 bit mask)

`BOD2` (4 bytes) = `u32` mask; bit *n* ↔ slot `30+n`. Enum form (RACE shield/body/beard/pipboy biped object fields): slot index `n` (0 = slot 30), −1 = none. [xe: FO4.pas:3318-3398]

| Bit | Mask | Slot | Name | Layer |
|---|---|---|---|---|
| 0 | 0x00000001 | 30 | Hair Top | head |
| 1 | 0x00000002 | 31 | Hair Long | head |
| 2 | 0x00000004 | 32 | FaceGen Head | head |
| 3 | 0x00000008 | 33 | BODY | body (full outfits/jumpsuits) |
| 4 | 0x00000010 | 34 | L Hand | |
| 5 | 0x00000020 | 35 | R Hand | |
| 6 | 0x00000040 | 36 | [U] Torso | **under-armor** |
| 7 | 0x00000080 | 37 | [U] L Arm | under-armor |
| 8 | 0x00000100 | 38 | [U] R Arm | under-armor |
| 9 | 0x00000200 | 39 | [U] L Leg | under-armor |
| 10 | 0x00000400 | 40 | [U] R Leg | under-armor |
| 11 | 0x00000800 | 41 | [A] Torso | **armor piece** |
| 12 | 0x00001000 | 42 | [A] L Arm | armor piece |
| 13 | 0x00002000 | 43 | [A] R Arm | armor piece |
| 14 | 0x00004000 | 44 | [A] L Leg | armor piece |
| 15 | 0x00008000 | 45 | [A] R Leg | armor piece |
| 16 | 0x00010000 | 46 | Headband | head gear (hats/helmets) |
| 17 | 0x00020000 | 47 | Eyes | glasses/goggles |
| 18 | 0x00040000 | 48 | Beard | (bandanas/masks) |
| 19 | 0x00080000 | 49 | Mouth | |
| 20 | 0x00100000 | 50 | Neck | scarves |
| 21 | 0x00200000 | 51 | Ring | |
| 22 | 0x00400000 | 52 | Scalp | |
| 23 | 0x00800000 | 53 | Decapitation | |
| 24–28 | 0x01000000–0x10000000 | 54–58 | Unnamed (mods use these) | |
| 29 | 0x20000000 | 59 | Shield | |
| 30 | 0x40000000 | 60 | Pipboy | |
| 31 | 0x80000000 | 61 | FX | |

Layering rule: clothing ("[U]" or slot 33 outfits) can be worn under armor pieces ("[A]" 41–45); two items conflict only if their masks intersect. Power-armor pieces occupy the PA race's armor slots (helmet 30/46/47 + …, torso 41, arms 42/43, legs 44/45) [inference — verify on PA ARMO records]. Slot names can be renamed per race (`RACE.NAME` biped object names) — the table is the vanilla human race. The server's Skyrim slot assumptions (e.g. `0x200` "shield" [broad survey §4.4]) must be replaced with this table.

---

## 6. BA2 archives

### 6.1 Header (24 bytes for FO4 v1/v7/v8)

| Off | Size | Field |
|---|---|---|
| 0x00 | 4 | magic `BTDX` |
| 0x04 | 4 | version u32: **1** (FO4 2015–1.10.163), **7/8** (FO4 Next-Gen 1.10.980+ and AE 1.11.x; structurally identical to v1), 2/3 (Starfield: +u64 `1`; v3 also +u32 compression, 3 = LZ4) |
| 0x08 | 4 | type `GNRL` (general) or `DX10` (textures) |
| 0x0C | 4 | file count u32 |
| 0x10 | 8 | name table offset u64 (absolute; 0 = no names) |

[bsa: src/bsa/fo4.cpp (master) header_t; commit 04d1fdf "add support for fo4 next-gen update" sets v1/v7/v8 header size 0x18] [xe: wbBSArchive.pas:533-553, 1330-1450] [web: https://www.nexusmods.com/fallout4/mods/81640 (summary: "changed version number from 1 to 7 and 8 with no apparent changes to the structure")]. Which of 7/8 is used for GNRL vs DX10 is not documented in the sources I could access; parsers must simply accept both.

### 6.2 File records (immediately after the header)

**GNRL** — 36 bytes per file, exactly one chunk:

| Off | Size | Field |
|---|---|---|
| 0x00 | 4 | name hash: CRC32 of lower-cased file stem (no extension) |
| 0x04 | 4 | extension: first ≤4 chars of the lower-cased extension, NUL-padded (e.g. `pex\0`) |
| 0x08 | 4 | directory hash: CRC32 of lower-cased parent path with `\` separators |
| 0x0C | 1 | mod index / unknown (0) |
| 0x0D | 1 | chunk count (= 1) |
| 0x0E | 2 | chunk header size (= 0x10) |
| 0x10 | 8 | data offset u64 (absolute) |
| 0x18 | 4 | packed size u32 (0 = stored uncompressed) |
| 0x1C | 4 | unpacked size u32 |
| 0x20 | 4 | sentinel `0xBAADF00D` |

**DX10** — 24-byte header + `chunkCount × 24`:

| Off | Size | Field |
|---|---|---|
| 0x00 | 12 | name hash, extension (`dds\0`), dir hash (as GNRL) |
| 0x0C | 1 | unknown (0) |
| 0x0D | 1 | chunk count (≤ 4 in vanilla engine) |
| 0x0E | 2 | chunk header size (= 0x18) |
| 0x10 | 2 | height |
| 0x12 | 2 | width |
| 0x14 | 1 | mip count |
| 0x15 | 1 | DXGI format |
| 0x16 | 1 | flags (0x1 cubemap) |
| 0x17 | 1 | tile mode (8 on PC) |
| chunk +0x00 | 8 | offset u64 |
| chunk +0x08 | 4 | packed size |
| chunk +0x0C | 4 | unpacked size |
| chunk +0x10 | 2 | start mip |
| chunk +0x12 | 2 | end mip |
| chunk +0x14 | 4 | `0xBAADF00D` |

DDS header is **not stored**; readers reconstruct it from width/height/mips/format (rsm-bsa does, via DirectXTex). [xe: wbBSArchive.pas:1450-1500] [bsa: src/bsa/fo4.cpp constants `chunk_header_size_gnrl=0x10`, `_dx10=0x18`, `chunk_sentinel=0xBAADF00D`, hashing `hash_file_in_place` (`directory=crc32(parent)`, `file=crc32(stem)`, ext packed little-endian)].

**Name table** at `nameTableOffset`: per file, in record order: `u16 length` + bytes (no NUL), paths use `\` or `/` (Archive2 writes `/`; normalize).
**Compression**: zlib (deflate with zlib header) per file/chunk for FO4 (all versions). LZ4 exists only for Starfield v3 [bsa: compression_lz4 = 3, rejected unless v3]. Xbox archives use a smaller window, still zlib.

### 6.3 Vanilla archives (what is where)

| Archive | Type | Contents relevant to FalloutMP |
|---|---|---|
| `Fallout4 - Interface.ba2` | GNRL | `Interface\*.swf` (Scaleform), **`Strings\Fallout4_<lang>.*STRINGS`** [web (summary), §2.3] |
| `Fallout4 - Misc.ba2` | GNRL | **`Scripts\*.pex`** (compiled vanilla Papyrus) [web: https://scrivener07.github.io/BGS-Handbook/wiki/fo4/papyrus/getting-started/index (summary)]; misc data |
| `Fallout4 - Startup.ba2` | GNRL | startup assets (loaded before plugins via `sResourceStartUpArchiveList`) |
| `Fallout4 - Animations.ba2` | GNRL | `Meshes\Actors\…\Animations\*.hkx`, behavior graphs [inference: name; listed in SResourceArchiveList2] |
| `Fallout4 - Meshes.ba2`, `- MeshesExtra.ba2` | GNRL | NIF meshes |
| `Fallout4 - Materials.ba2` | GNRL | `.bgsm/.bgem` materials |
| `Fallout4 - Shaders.ba2`, `- Sounds.ba2`, `- Voices.ba2` | GNRL | shaders, `.xwm/.wav`, `.fuz` voice |
| `Fallout4 - Textures1…9.ba2` | DX10 | textures |
| `Fallout4 - Nvflex.ba2` | GNRL | NVIDIA Flex (pre-NG) |
| `<DLC> - Main.ba2` / `- Textures.ba2` (`DLCRobot`, `DLCworkshop01..03`, `DLCCoast`, `DLCNukaWorld`), `- Voices_en.ba2` | GNRL/DX10 | DLC scripts, strings, meshes / textures [inference] |
| `cc*-Main.ba2` / `- Textures.ba2` | | Creation Club |

Archive list keys in `Fallout4.ini [Archive]`: `SResourceArchiveList`, `SResourceArchiveList2`, `sResourceStartUpArchiveList`, `sResourceIndexFileList` (textures) [web: https://loadorderlibrary.com/lists/falou-4/embed/fallout4.ini (search summary)]. Plugin-attached archives: `<Plugin> - Main.ba2` and `<Plugin> - Textures.ba2` are auto-loaded for each active plugin [inference: same rule as Skyrim's `<Plugin>.bsa`].

### 6.4 rsm-bsa in this repo and SkyMP's server usage

- `vcpkg.json` depends on `rsm-bsa` (not on macOS/emscripten) via the overlay `overlay_ports/rsm-bsa` pinned to **tag 4.1.0** (`REF 4.1.0`, SHA512 `c488a4f7…`) with three local patches (`variant-emplace-fix`, `structural-binding`, `fix-static-cast-error` — the last one already patches `src/bsa/fo4.cpp`) [src: vcpkg.json:31-34; overlay_ports/rsm-bsa/portfile.cmake:1-14].
- rsm-bsa 4.1.0 has full FO4 BA2 read/write (GNRL + DX10, zlib), **but its header reader throws `"invalid version"` unless `version == 1`** [bsa: 4.1.0 src/bsa/fo4.cpp:134-137]. v7/v8 support landed on master in commit `04d1fdf` (2024-05-19), plus v2/v3 Starfield and LZ4; there is no newer release tag. ⇒ Change the overlay to `REF 2c7280d5c9199f90b7338d70425181029c9bb2f2` (or later), re-check that the three patches still apply, and add the `lz4`/`directxtex` deps already listed. Without this, every NG/AE `Fallout4 - *.ba2` fails to open.
- Server usage today: only `BsaArchiveScriptStorage` (`bsa::tes4::archive bsa; bsa.read(path); bsa["scripts"]` iterates the `scripts` folder of a Skyrim BSA) [src: skymp5-server/cpp/server_guest_lib/script_storages/BsaArchiveScriptStorage.cpp:36-62]; linked when `TARGET bsa::bsa` exists, else `NO_BSA` [src: skymp5-server/cpp/CMakeLists.txt:141-145]. FO4 equivalent: `bsa::fo4::archive ba2; ba2.read(path);` then iterate `for (auto& [key, file] : ba2)`, filter `key.name()` starting with `scripts\` (case-insensitive) and ending `.pex`, and get bytes via `file.write(stream, bsa::fo4::format::general)` or by decompressing `file[0]`. FO4 script names are **namespaced** (`Scripts\Workshop\WorkshopParentScript.pex` ↔ `Workshop:WorkshopParentScript`) — map the subpath into the colon form (broad survey §4.3). The localization provider (§2.4) needs the same BA2 access for `Strings\`.

---

## 7. `.fos` save format and the spawn strategy

### 7.1 File layout

All values little-endian; `wstring` = `u16 length` + bytes (no NUL; UTF-8/cp1252).

| Part | Layout | Source |
|---|---|---|
| magic | `char[12] "FO4_SAVEGAME"` (Skyrim: 13 bytes `TESV_SAVEGAME`) | [xe: FO4Saves.pas:7299-7300] [rsv: Header.java] |
| headerSize | u32 = bytes of the header struct below | |
| header | `u32 version` (**≥ 11**; light-plugin list present when **version > 14**), `u32 saveNumber`, `wstring playerName`, `u32 playerLevel`, `wstring location`, `wstring playTime`, `wstring raceEditorId`, `u16 sex`, `float curXP`, `float levelUpXP`, `u64 FILETIME`, `u32 shotWidth`, `u32 shotHeight` — **no compression field** (SSE has `u16 compressionType` here) | [xe: FO4Saves.pas:7262-7276, 337-340] [rsv: Header.java FO4 branch] |
| screenshot | `width × height × 4` bytes **RGBA** (Skyrim LE: RGB ×3) | [xe: FO4Saves.pas:583-604] [rsv: Header.java `BYPP = 4`] |
| formVersion | u8 (ReSaver requires ≥ 60; ≥ 68 ⇒ light plugins; > 61 ⇒ 32-bit Papyrus string indices) | [rsv: ESS.java:266-291, 990-1010] |
| gameVersion | `wstring` runtime version, e.g. `"1.10.163.0"` — **FO4 only** | [xe: FO4Saves.pas:7305] [rsv: ESS.java:290] |
| pluginInfo | `u32 size` (bytes that follow, incl. light list), `u8 count` + `count × wstring`, then if supported `u16 lightCount` + `lightCount × wstring` | [rsv: PluginInfo.java] [xe: FO4Saves.pas:7306-7308] |
| file location table | 100 bytes: `u32 formIDArrayCountOffset, unknownTable3Offset, globalData1Offset, globalData2Offset, changeFormsOffset, globalData3Offset, globalData1Count, globalData2Count, globalData3Count, changeFormCount; u32 unused[15]` (offsets absolute) — FO4's table-3 count is exact (Skyrim omits type 1001 → off by one) | [xe: FO4Saves.pas:7285-7297] [rsv: FileLocationTable.java] |
| global data 1, 2 | each block `{u32 type; u32 length; u8 data[length]}` | [xe: FO4Saves.pas:3609-3612] |
| change forms | `refId(3) ; u32 changeFlags ; u8 type (bits 0-5 type, 6-7 length width: 0 u8, 1 u16, 2 u32) ; u8 version ; length1 ; length2 ; data[length1]` — zlib-compressed when `length2 > 0` (= uncompressed size) | [rsv: ChangeForm.java:47-100] |
| global data 3 | blocks as above (1001 = Papyrus VM, FO4 variant) | |
| FormID array | `u32 count` + `count × u32` load-order FormIDs | [xe: FO4Saves.pas:7316] [rsv: ESS.java:474] |
| visited worldspaces | `u32 count` + `u32[]` | |
| unknown table 3 | `u32 size` + `u32 count` + `wstring[]` | |

There is **no whole-body compression** in FO4 (ReSaver: `supportsCompression() == false` for FO4 [rsv: ESS.java:1029-1040]; xEdit's FO4 header has no compression field, unlike TES5 saves [xe: wbDefinitionsTES5Saves.pas:6095]). Individual change forms use zlib, like Skyrim.

**RefID** (3 bytes, big-endian `b0<<16|b1<<8|b2`): top 2 bits = type: 0 = index+1 into the FormID array (0 = null), 1 = "default" (object in `Fallout4.esm`, value = object ID), 2 = created (`0xFF` prefix), 3 = invalid; value = low 22 bits [rsv: RefID.java]. Identical to Skyrim (`savefile/SFStructure.h` `RefID` [src: savefile/include/savefile/SFStructure.h:22-90]). Light FormIDs (`FE…`) appear only via the FormID array.

**Global data types** (union index order from [xe: FO4Saves.pas:650-672, 3612-4170]):
- Table 1 (0–11): 0 Misc Stats, **1 Player Location**, 2 TES, **3 Global Variables**, 4 Created Objects, 5 Effects, **6 Weather**, 7 Audio, 8 Sky Cells, 9 Input Enable Manager, 10 StoryTeller, 11 ReservedIDs. (Skyrim: 0–8.)
- Table 2 (100–117): 100 Process Lists, 101 Combat, 102 Interface, 103 Actor Causes, 104 Detection Manager, 105 Location MetaData, 106 Quest Static Data, 107 Attraction Object LOS, 108 Animation, 109 Player Controls, 110 Story Event Manager, 111 Ingredient Shared, 112 Menu Controls, 113 Menu Topic Manager, 114 Scene-filler Actors, 115 Range Formations, 116 Anim Objects, 117 Radio Manager. (Skyrim: 100–114 with different meanings, e.g. 108 StoryTeller, 109 Magic Favorites.)
- Table 3 (1000–1007): 1000 Temp Effects, **1001 Papyrus**, 1002 Anim Objects, 1003 Timer, 1004 Synchronized Animations, 1005 Main, 1006 Topic Infos, 1007 Explosion Manager.

Player Location (type 1): `u32 unknown/nextObjectId; refId worldspace; s32 cellX; s32 cellY; refId worldOrCell; float x, y, z` (no trailing byte) [xe: FO4Saves.pas:3631-3640]. Global Variables (type 3): `vsval count` + `{refId; float}` [xe: FO4Saves.pas:3648-3651]. Weather (type 6): `refId climate, weather×4, region; float×9; u32×2; u8 flags; optional blobs` — same byte length as Skyrim's but different field meaning [xe: FO4Saves.pas:3668-3690].

**Change form types** [xe: FO4Saves.pas:4175-4226]: 0 REFR, **1 ACHR**, 2 PMIS, 3 PGRE, 4 PBEA, 5 PFLA, 6 CELL, 7 INFO, 8 QUST, **9 NPC_**, 10 ACTI, 11 TACT, 12 ARMO, 13 BOOK, 14 CONT, 15 DOOR, 16 INGR, 17 LIGH, 18 MISC, 19 STAT, 20 MSTT, 21 FURN, 22 WEAP, 23 AMMO, 24 KEYM, 25 ALCH, 26 IDLM, 27 NOTE, 28 ECZN, 29 CLAS, 30 FACT, 31 PACK, 32 NAVM, 33 WOOP, 34 MGEF, 35 SMQN, 36 SCEN, 37 LCTN, 38 RELA, 39 PHZD, 40 PBAR, 41 PCON, 42 FLST, 43 LVLN, 44 LVLI, 45 LVSP, 46 PARW, 47 ENCH, 48 TERM, 49 INNR. ACHR=1 and NPC_=9 coincide with Skyrim (`SFStructure.h ChangeForm::Type` [src: savefile/include/savefile/SFStructure.h:94-99]).

Change flags (ACHR, type 1): 0 FORM_FLAGS, **1 REFR_MOVE**, 2 HAVOK_MOVE, 3 CELL_CHANGED, 4 SCALE, 5 INVENTORY, 6 EXTRA_OWNERSHIP, 7 BASEOBJECT, 8 EXTRA_LINK_REF, 9 EXTRA_WORKSHOP, 10 LIFESTATE, 11 EXTRA_PACKAGE_DATA, 12 EXTRA_MERCHANT_CONTAINER, 17 DISMEMBERED_LIMBS, 18 LEVELED_ACTOR, 19–23 disposition/temp/damage/override/permanent modifiers, 25 PROMOTED, 26 ACTIVATING_CHILDREN, 27 LEVELED_INVENTORY, 28 ANIMATION, 29 ENCOUNTER_ZONE, 30 CREATED_ONLY, 31 GAME_ONLY [xe: FO4Saves.pas:4265-4298]. NPC_ (type 9): 1 BASE_DATA, 2 ATTRIBUTES, 3 AIDATA, 4 SPELLLIST, 5 FULLNAME, 6 FACTIONS, 9 NPC_SKILLS, 10 CLASS, **11 FACE**, 12 DEFAULT_OUTFIT, 13 SLEEP_OUTFIT, **14 BODY_SCALES** (new), 24 GENDER, 25 RACE [xe: FO4Saves.pas:4552-4586]. ACHR data begins with "initial data" when MOVE/HAVOK_MOVE is set: `refId cell; float pos[3]; float rot[3]` (radians) [rsv: ChangeFormACHR.java, ChangeFormInitialData.java case 4] — the same 27-byte prefix SkyMP patches for Skyrim [src: skyrim-platform/src/platform_se/skyrim_platform/LoadGame.cpp `EditChangeForm` (d+0 refId, d+3 pos, d+15 rot)]. The FO4 NPC_ FACE payload (morphs/tints) is undocumented in xEdit (decoded as raw) and ReSaver.

### 7.2 Differences vs `savefile/` (Skyrim LE v9 implementation)

| Aspect | `savefile/` today | FO4 |
|---|---|---|
| Magic | `ReadString(13)` [src: savefile/src/SFReader.cpp:54] | 12 bytes |
| Screenshot | `w*h*3` [src: SFReader.cpp:57-62] | `w*h*4` |
| After form version | plugin info directly [src: SFReader.cpp:65-67] | `wstring gameVersion` first |
| Plugins | `u8` count only [src: SFReader.cpp:139-149] | + `u16` light list (version > 14); `OverwritePluginInfo` must write both and recompute `pluginInfoSize` |
| Table 3 count | reads an extra `fixForBag` u64 because Skyrim's count is off by one [src: SFReader.cpp:91-95] | count exact; remove the hack |
| Global data | parses every Skyrim type into structs, no `default:` → unknown FO4 types (9–11, 115–117, 1006, 1007) desync/assert [src: SFReader.cpp:172-262] | treat all blocks as opaque bytes except 1, 3, 6 |
| Template | `assets/template.ess` is **LE v9** (magic `TESV_SAVEGAME`, header version 9, RGB) — SSE loads it through its legacy path [verified by dumping the first bytes] | FO4 has no legacy format to fall back on; the template must be a real FO4 save made with the target runtime |
| Save dir | `My Games\Skyrim Special Edition\Saves\*.ess` [src: LoadGame.cpp:49-50,125-130] | `My Games\Fallout4\Saves\*.fos` (+ F4SE co-save `.f4se`) |

### 7.3 Feasibility: (a) write a `.fos` from a template vs (b) load a shipped template and move the player

**(a) Template-patching writer (SkyMP's approach).** What `LoadGame::Run` does for Skyrim [src: LoadGame.cpp:91-123]: overwrite plugin list with the running game's, set GameHour global (`0x38`), patch Weather, replace the player NPC_ change form (appearance), rewrite PlayerLocation (type 1) and the player ACHR change form's MOVE prefix, then write and `BGSSaveLoadManager::Load`. For FO4 the same edits are feasible with a **new ~500-line opaque-block reader/writer**: parse header → screenshot (can be replaced by a 1×1 or black image) → gameVersion → plugin lists → FLT → keep global data/change forms as byte blobs → patch: plugin lists (must match exactly the client's full + light lists or the game warns "content missing"), type 1 player location, type 3 GameHour (FO4 `GameHour` is also a GLOB; resolve its RefID through the FormID array or "default" type), the player ACHR (RefID `0x000014` "default") MOVE prefix, recompute all FLT offsets. Risks: (1) the FO4 appearance change form (FACE/BODY_SCALES) is undocumented → do **not** patch NPC_ data; apply appearance after load via natives; (2) unknown dependence of Papyrus (1001) and quest state on the template's world state; (3) save version drift between runtimes (OG 1.10.163 vs NG/AE) — the template must be produced by each supported runtime; (4) the game validates the plugin list but not offsets beyond the FLT [inference]. Effort: **M (1–2 weeks)** including tests against real saves [inference].

**(b) Load a shipped template save, then teleport.** Ship one save per runtime (made at e.g. the Vault 111 exit with MQ101 progressed), load it unmodified via `BGSSaveLoadManager::Load` (CommonLibF4 has the singleton), then `MoveTo`/`SetPosition` the player and set time/weather through Papyrus/natives. Needs no format code at all, tolerates unknown blocks, but: the plugin list in the save must be a subset of the active load order (extra client plugins are fine; missing ones trigger warnings), and template world/quest state leaks into the session (mitigate with a "clean" save and the world cleaner). Effort: **S (days)**.

Recommendation: start with **(b)** (already the broad survey's recommendation, §4.1), and keep **(a)** as an optimization once loading screens/teleport artefacts matter; implement (a) as a separate `savefile_fo4` module rather than generalizing the Skyrim struct-heavy reader.

---

## 8. Concrete change list and design

### 8.1 Game-aware parsing design

1. **`enum class espm::Game : uint8_t { Skyrim, Fallout4 };`** stored per `Browser` (detected from TES4 at construction, §1.6) and on `CombineBrowser` (error if sources disagree). Add `Browser::GetGame()`, `GetTes4Info()` (flags: master/localized/light, HEDR version, masters).
2. **Separate typed records per game in a namespace**, keeping the Skyrim ones untouched: `libespm/include/libespm/fo4/{NPC_,RACE,WEAP,ARMO,AMMO,COBJ,LVLI,LVLN,MGEF,BOOK,LIGH,OMOD,CMPO,AVIF,MISC,KEYM,NOTE,TERM,FURN,…}.h` with `namespace espm::fo4`. They derive from `RecordHeader` exactly like today (zero-copy, `static_assert(sizeof == sizeof(RecordHeader))`) and expose `Data GetData(CompressedFieldsCache&) const`. Records whose layout is identical (FLST, OTFT, KYWD EDID, CELL DATA, WRLD, LCTN, ACHR/REFR core, SPEL, ENCH, FACT, CTDA) stay shared.
3. **Form-version-aware readers**: a small helper `FieldReader r(data, size); r.skip_if(fv < 109, 24)…` so RACE/EXPL/FURN/AIDT gated fields are read correctly; pass `GetVersion()` (record form version) to parsers that need it.
4. **Accessors through the game**: server code calls game-neutral facades (`espm::ItemStats GetItemStats(LookupResult)`, `GetWeight`, `GetActorBase`, `GetLeveledEntries`, `GetRecipe`) implemented by switching on `br.GetGame()`. This keeps `#ifdef`s out of `server_guest_lib` and lets Skyrim and FO4 coexist in one binary (useful for unit tests).
5. **Bounds safety**: every new FO4 parser checks `dataSize` against the struct size before `reinterpret_cast` (today's parsers trust sizes; FO4 mods with old fv produce shorter structs).
6. **Localized strings**: `lstring` fields return `{uint32 id}` or `{const char*}` depending on the source TES4 localized flag; a `StringTable` service (§2.4) resolves them.

### 8.2 libespm, file by file

| File | Change for FO4 |
|---|---|
| `Browser.h/.cpp` | Detect game + TES4 flags on construction; fix NAVM indexing (via `NAVM.h`); keep the `GetRecordsByType` whitelist but add `AVIF`, `OMOD`, `COBJ` (already), `LVLI`; optional: index `REFR/ACHR` by base ID for spawn queries. `ReadAny` assumes `dataSize` sane — add `pos + 24 + dataSize <= length` checks |
| `RecordHeader.h/.cpp` | Add `IsCompressed/IsDeleted/IsPersistent/IsInitiallyDisabled` helpers; `GetScriptData`: drop `noexcept` or catch (VMAD v6 Struct/Var) |
| `RecordHeaderAccess.h`, `ZlibUtils.h` | `inflate(Z_FINISH)` + size check |
| `RecordFlags.h` | Add FO4-only flags (0x2000000 FURN Power Armor, ACHR 0x2000 Starts Unconscious, OMOD 0x10 Legendary) in an `fo4` sub-enum |
| `TES4.h/.cpp` | Expose flags (master 0x1, localized 0x80, light 0x200), `INCC`, master `DATA`; float `version` |
| `IdMapping.h`, `Utils.cpp::GetMappedId`, `Combiner.cpp`, `CombineBrowser.h/.cpp`, `BrowserInfo.h`, `LookupResult.h` | Light-plugin model §3.3: per-source `SourceIdMap`, runtime-prefix global IDs, `uint16_t fileIdx`, `std::vector<Source>`, global→record hash index; fix `FindNavMeshes` raw-ID bug |
| `Loader.h/.cpp` | Accept `.esl`; compute light/full ordering; expose `PluginIndexOf(formId)`, `GetPluginName(formId)`, `GetRuntimePrefix(fileIdx)`; optional in-memory sources (for tests): `Loader(std::vector<std::pair<std::string, std::vector<uint8_t>>>)` |
| `ActorValue.h` | Keep for Skyrim; add `espm::fo4::AvifResolver` (EDID ↔ global FormID, incl. the 164 hardcoded AVIFs) |
| `NPC_.h/.cpp` | Skyrim: fix SNAM rank offset (+4). New `fo4::NPC_`: ACBS per §4.4 (template flags @0x0E), `TPLT`+`TPTA[13]`, `SNAM` 5 bytes, `PRPS`, `DNAM` calc health/AP, `PKID`, `DOFT/SOFT`, `CNTO/COED`, `PNAM`, `HCLF/BCLF`, `FTST`, `QNAM`, `MSDK/MSDV`, `TETI/TEND`, `MRSV`, `FMRI/FMRS`, `MWGT`, `NAM6/NAM4`, `PFRN`, keywords, `APPR`, `OBTE` |
| `RACE.h/.cpp` | New `fo4::RACE`: DATA §4.5 (fv-gated), `PRPS`, `BOD2`(4), `UNWP`, `VTCK`, `HCLF`, head parts, tint groups, morph groups/values (`MSID`), face morphs (`FMRI`), subgraph data (`SAKD/SGNM/SAPT/STKD/SRAF`) |
| `WEAP.h/.cpp` | New `fo4::WEAP`: DNAM 132 / FNAM 41 / CRDT 12 / DAMA / APPR / OBTE / NNAM / INRD; `weapData` (Skyrim DATA) is absent |
| `ARMO.h/.cpp` | Remove the throw for missing DNAM (`fo4` path); FO4 DATA 12, FNAM 8, DAMA, `BOD2` 4 bytes, `INDX/MODL`, RNAM, APPR, OBTE |
| `AMMO.h/.cpp` | FO4: `DATA` value@0 weight@4; `DNAM` projectile/flags/damage/health; `ONAM` |
| `MGEF.h/.cpp` | FO4: `actorValue@0x44`, `secondActorValue@0x58`, `resistValue@0x10` as AVIF formids (`uint32_t`, not `ActorValue`); archetype enum per §4.10 |
| `COBJ.h/.cpp` | FO4: `FVPA` components, `INTV` count/priority, `FNAM` categories; ignore NAM1 |
| `LeveledListBase.h/.cpp`, `LVLI.h`, `LVLN.h` | Parse each `LVLO` field (12 bytes, u16 level, u16 count, u8 chanceNone); collect `COED`; `LVLM`, `LLKC`, `LVSG`, `ONAM`; LVLN model. Applies to Skyrim too (current contiguous-array trick breaks on COED) |
| `CONT.h/.cpp`, `TREE.cpp`, `FLOR.cpp`, `QUST.cpp` | `FULL` as lstring; FO4 CONT DATA (flags,weight); FLOR `PFIG` + `PFPC` |
| `ALCH.cpp` | Same ENIT; expose addiction, addiction chance, Medicine flag 0x10000 |
| `BOOK.h/.cpp` | FO4: flags/teaches from `DNAM` (13), value/weight from `DATA` (8) |
| `MISC.h/.cpp` | Add value + `CVPA` components |
| `LIGH.h/.cpp` | Fix Skyrim typos (0x0F→0x10, 0x1F→0x20); FO4 64-byte DATA (value 0x38, weight 0x3C) |
| `REFR.h/.cpp`, `ACHR.h/.cpp` | `XTEL` transition interior, `XOWN` 12 bytes, `XLOC`, `XESP`, `XLIB`, `XAMC`, `XHLT`, map-marker enum per game, `XWPG/XWPN`, `XATR`; keep DATA |
| `WRLD.cpp` | `copy_n` → `memcpy` (bug in both games) |
| `LCTN.cpp` | Add `RCPR/RCUN/RCSR/RCEC`, `ANAM` |
| `NAVM.h/.cpp` | `kType = "NAVM"`; FO4 NVNM version 15 (vertex offsets unchanged) |
| `GMST.h/.cpp` | Value union by EDID prefix; drop hardcoded Skyrim FormIDs (resolve by EDID) |
| `KYWD.cpp`, `FLST.cpp`, `OTFT.cpp`, `FACT.cpp`, `SPEL.cpp`, `ENCH.cpp`, `Effects.cpp`, `CTDA.h`, `CELL.cpp` | Layout-compatible; CELL add XCLC; CTDA add run-on 8–10 |
| `Property.h`, `Utils.cpp` (`ReadPropertyValue`/`FillScriptArray`) | Types 6, 7, 16, 17; VMAD v6 |
| `Utils.cpp::IsItem` | FO4 item types: add `KEYM`, `NOTE`, `OMOD` (loose mods are MISC), remove `SCRL`/`SLGM`/`INGR` for FO4 |
| `Utils.cpp::kCorrectHashcode` | Add FO4 ESM CRCs per supported runtime (CI only) |
| New: `fo4/AVIF.h`, `fo4/OMOD.h`, `fo4/CMPO.h`, `fo4/INNR.h`, `fo4/KEYM.h`, `fo4/NOTE.h`, `fo4/TERM.h`, `fo4/FURN.h`, `fo4/PROJ.h`, `fo4/EXPL.h`, `fo4/HAZD.h`, `fo4/PERK.h`, `fo4/ObjectTemplate.h` | per §4 |

### 8.3 Consumers in `skymp5-server`

| Consumer | What must change |
|---|---|
| `GetBaseActorValues.cpp` [src: …/server_guest_lib/GetBaseActorValues.cpp:25-87] | FO4: Health/AP from `NPC_.DNAM` (+ PRPS/AVIF defaults, level scaling); no magicka/stamina; regen from AVIF rates (`ActionPointsRate` 0x2D8 …) instead of `RACE.healRegen` |
| `EvaluateTemplate.h` [src: …/EvaluateTemplate.h:96-154] | Per-flag template target: if `templateFlags & bit` → `TPTA[bit]` (if non-null) else `TPLT`; flag bit values unchanged |
| `LeveledListUtils.cpp` [src: …/LeveledListUtils.cpp:17-225] | Use parsed entries (count u16, per-entry chance none, COED); `LVLG` global value; `UseAll` flag 0x4 same; template chain per flag |
| `WorldState::AttachEspmRecord` [src: …/WorldState.cpp:368-660] | FO4 base types to attach: NPC_, FURN, ACTI, DOOR, CONT, items (+KEYM, NOTE), FLOR with PFIG, TERM; `CrimeFactionsList 0x26953` is Skyrim-only → profile setting; ACHR flags same; ownership `XOWN` |
| `FormDesc.cpp`, `WorldState::GetFileIdx`, `IsNpcAllowed` | Light-aware IDs (§3.3) |
| `CraftService.cpp` [src: …/CraftService.cpp:20-260] | FVPA + component scrapping; INTV count; remove Skyrim temper keywords; FO4 workbench types via FURN `WBDT` |
| `GetWeightFromRecord.cpp` [src: …/GetWeightFromRecord.cpp:17-75] | FO4 offsets: WEAP DNAM@0x3B, ARMO DATA@4, AMMO DATA@4, ALCH DATA, BOOK DATA@4, MISC/KEYM/NOTE DATA@4, LIGH DATA@0x3C |
| `MpActor.cpp`, `ActorValues.h`, `CropRegeneration.cpp`, `PercentagesBinding.cpp` | `espm::ActorValue` enum → AVIF-based set (Health, ActionPoints, Rads, …) |
| `condition_functions/*`, `Condition.cpp` | FO4 indices (Appendix A); AVIF params |
| `ActionListener.cpp`, `formulas/TES5DamageFormula.cpp` | FO4 damage: WEAP DNAM base damage + DAMA, ammo DNAM damage, ARMO FNAM rating + DAMA resistances (curve per GMSTs) [inference] |
| `script_storages/BsaArchiveScriptStorage.cpp` | BA2 + namespaced script names (§6.4) |
| `localization_provider/*`, `ScampServer::GetLocalizedString` | §2.4 |
| `ScampServer.cpp:315-321` | FO4 default load order + `.ccc` |
| `savefile/`, `skyrim-platform/…/LoadGame.cpp` | §7 |

### 8.4 Unit-testing FO4 parsers without Bethesda data

`unit/main.cpp` loads the five Skyrim ESMs and auto-skips `[espm]` tests when absent [src: unit/main.cpp:19-55]. For FO4, build plugins **in memory** from a tiny builder and feed `espm::Browser` directly (its constructor takes `(const void*, size_t)` [src: libespm/include/libespm/Browser.h:17]). Put the builder at `unit/PluginBuilder.h` (unit sources are globbed from the top-level `unit/` dir only [src: unit/CMakeLists.txt:21]).

```cpp
// unit/PluginBuilder.h — synthetic Bethesda plugin writer (TES4 + GRUPs + records)
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>

namespace testutils {

class Bytes {
public:
  template <class T> Bytes& put(const T& v) {
    auto p = reinterpret_cast<const uint8_t*>(&v);
    buf.insert(buf.end(), p, p + sizeof(T));
    return *this;
  }
  Bytes& raw(const void* d, size_t n) {
    auto p = static_cast<const uint8_t*>(d);
    buf.insert(buf.end(), p, p + n);
    return *this;
  }
  Bytes& zstr(const std::string& s) { return raw(s.c_str(), s.size() + 1); }
  std::vector<uint8_t> buf;
};

struct Record {
  char sig[5];
  uint32_t flags = 0, formId = 0;
  uint16_t formVersion = 131;           // FO4; use 44 for SSE fixtures
  Bytes fields;
  Record(const char* s, uint32_t id) : formId(id) { std::memcpy(sig, s, 5); }
  Record& field(const char* type, const Bytes& payload) {
    const auto& p = payload.buf;
    if (p.size() > 0xFFFF) {             // XXXX size override
      fields.raw("XXXX", 4).put<uint16_t>(4).put<uint32_t>(uint32_t(p.size()));
      fields.raw(type, 4).put<uint16_t>(0);
    } else {
      fields.raw(type, 4).put<uint16_t>(uint16_t(p.size()));
    }
    fields.raw(p.data(), p.size());
    return *this;
  }
  template <class T> Record& field(const char* type, const T& pod) {
    Bytes b; b.put(pod); return field(type, b);
  }
  std::vector<uint8_t> Serialize(bool compress = false) const {
    std::vector<uint8_t> data = fields.buf;
    uint32_t fl = flags;
    if (compress) {                      // 0x40000: u32 rawSize + zlib
      uLongf n = compressBound(data.size());
      std::vector<uint8_t> z(n + 4);
      uint32_t raw = uint32_t(data.size());
      std::memcpy(z.data(), &raw, 4);
      compress2(z.data() + 4, &n, data.data(), data.size(), 9);
      z.resize(n + 4); data = std::move(z); fl |= 0x40000;
    }
    Bytes out;
    out.raw(sig, 4).put<uint32_t>(uint32_t(data.size())).put(fl).put(formId)
       .put<uint32_t>(0).put(formVersion).put<uint16_t>(0)
       .raw(data.data(), data.size());
    return out.buf;
  }
};

struct Group {
  uint32_t label = 0; int32_t type = 0;      // 0 = top-level (label = signature)
  std::vector<std::vector<uint8_t>> children; // serialized records / groups
  static Group Top(const char* sig) { Group g; std::memcpy(&g.label, sig, 4); return g; }
  Group& add(const Record& r) { children.push_back(r.Serialize()); return *this; }
  Group& add(const Group& g) { children.push_back(g.Serialize()); return *this; }
  std::vector<uint8_t> Serialize() const {
    size_t sz = 24; for (auto& c : children) sz += c.size();
    Bytes out;
    out.raw("GRUP", 4).put<uint32_t>(uint32_t(sz)).put(label).put(type)
       .put<uint32_t>(0).put<uint32_t>(0);
    for (auto& c : children) out.raw(c.data(), c.size());
    return out.buf;
  }
};

struct PluginBuilder {
  float hedrVersion = 1.0f; uint32_t tes4Flags = 0x80; // localized by default
  std::vector<std::string> masters; std::vector<Group> groups;
  std::vector<uint8_t> Build() const {
    Record tes4("TES4", 0);
    Bytes hedr; hedr.put(hedrVersion).put<uint32_t>(0).put<uint32_t>(0x800);
    tes4.flags = tes4Flags;
    tes4.field("HEDR", hedr).field("CNAM", Bytes().zstr("fixture"));
    for (auto& m : masters) tes4.field("MAST", Bytes().zstr(m)).field("DATA", uint64_t{0});
    tes4.field("INCC", uint32_t{0});
    auto out = tes4.Serialize();
    for (auto& g : groups) { auto b = g.Serialize(); out.insert(out.end(), b.begin(), b.end()); }
    return out;
  }
};

} // namespace testutils
```

Example test (FO4 WEAP DNAM + light-plugin mapping):

```cpp
#include "PluginBuilder.h"
#include <catch2/catch_all.hpp>
#include "libespm/Browser.h"
#include "libespm/Combiner.h"
#include "libespm/fo4/WEAP.h"   // to be written

TEST_CASE("FO4 WEAP DNAM is parsed", "[espm_fo4]") {   // no [espm] tag → always runs
  using namespace testutils;
  #pragma pack(push, 1)
  struct Dnam { uint32_t ammo; float speed, reload, reach, minR, maxR, delay, unused, oor;
                uint32_t onHit, skill, resist, flags; uint16_t capacity; uint8_t animType;
                float secDmg, weight; uint32_t value; uint16_t baseDmg; uint8_t rest[0x84 - 0x45]; };
  #pragma pack(pop)
  static_assert(sizeof(Dnam) == 0x84);
  Dnam d{}; d.ammo = 0x0001F276; d.weight = 3.5f; d.value = 55; d.baseDmg = 18;
  d.capacity = 12; d.animType = 9; d.flags = 0x8000;   // automatic gun

  Record weap("WEAP", 0x00000800);
  weap.field("EDID", Bytes().zstr("FixturePistol")).field("DNAM", d);
  PluginBuilder pb; pb.groups.push_back(Group::Top("WEAP").add(weap));
  auto bytes = pb.Build();

  espm::Browser br(bytes.data(), bytes.size());
  REQUIRE(br.GetGame() == espm::Game::Fallout4);          // fv 131
  auto rec = espm::Convert<espm::fo4::WEAP>(br.LookupById(0x800));
  REQUIRE(rec);
  espm::CompressedFieldsCache cache;
  auto data = rec->GetData(cache);
  REQUIRE(data.dnam->weight == 3.5f);
  REQUIRE(data.dnam->value == 55);
  REQUIRE(data.dnam->baseDamage == 18);
}

TEST_CASE("light plugin records map to FE xxx yyy", "[espm_fo4]") {
  using namespace testutils;
  PluginBuilder master; master.tes4Flags = 0x81;                 // ESM + localized
  master.groups.push_back(Group::Top("KYWD").add(Record("KYWD", 0x00000801)));
  PluginBuilder light;  light.tes4Flags = 0x281; light.masters = { "Fallout4.esm" };
  light.groups.push_back(Group::Top("KYWD").add(Record("KYWD", 0x01000802)));  // own record
  auto a = master.Build(), b = light.Build();
  espm::Browser ba(a.data(), a.size()), bb(b.data(), b.size());
  espm::Combiner c; c.AddSource(&ba, "Fallout4.esm"); c.AddSource(&bb, "ccTest.esl");
  auto cb = c.Combine();
  REQUIRE(cb->LookupById(0x00000801).rec);
  REQUIRE(cb->LookupById(0xFE000802).rec);                       // light slot 0, object 0x802
  REQUIRE(cb->LookupById(0x01000802).rec == nullptr);
}
```

Fixture guidance: (1) build one fixture per record type from the §4 tables (one test = one table), including **old form versions** (e.g. RACE with fv 108 → no default weights) and compressed records; (2) cross-validate fixtures by dumping them with xEdit/FO4Edit once (optional, manual); (3) keep a separate `[espm_fo4_data]` tag for tests against real `Fallout4.esm` (CI self-hosted only — do not redistribute game files); (4) the same builder can produce Skyrim fixtures (`formVersion = 44`, `hedrVersion = 1.71f`) to lock in current behavior before refactoring.

---

## Appendix A — FO4 condition functions (complete, from xEdit)

Format `index Name(param types)`; 479 entries [xe: FO4.pas:262-740].

<details><summary>Show all</summary>

0 GetWantBlocking · 1 GetDistance(Reference) · 5 GetLocked · 6 GetPos(Axis) · 8 GetAngle(Axis) · 10 GetStartingPos(Axis)  
11 GetStartingAngle(Axis) · 12 GetSecondsPassed · 14 GetValue(ActorValue) · 18 GetCurrentTime · 24 GetScale · 25 IsMoving  
26 IsTurning · 27 GetLineOfSight(Reference) · 32 GetInSameCell(Reference) · 35 GetDisabled · 36 MenuMode(Integer) · 39 GetDisease  
41 GetClothingValue · 42 SameFaction(Actor) · 43 SameRace(Actor) · 44 SameSex(Actor) · 45 GetDetected(Actor) · 46 GetDead  
47 GetItemCount(BaseObject) · 48 GetGold · 49 GetSleeping · 50 GetTalkedToPC · 56 GetQuestRunning(Quest) · 58 GetStage(Quest)  
59 GetStageDone(Quest,QuestStage) · 60 GetFactionRankDifference(Faction,Actor) · 61 GetAlarmed · 62 IsRaining · 63 GetAttacked · 64 GetIsCreature  
65 GetLockLevel · 66 GetShouldAttack(Actor) · 67 GetInCell(Cell) · 68 GetIsClass(Class) · 69 GetIsRace(Race) · 70 GetIsSex(Sex)  
71 GetInFaction(Faction) · 72 GetIsID(BaseObject) · 73 GetFactionRank(Faction) · 74 GetGlobalValue(Global) · 75 IsSnowing · 77 GetRandomPercent  
79 WouldBeStealing(Reference) · 80 GetLevel · 81 IsRotating · 84 GetDeadCount(ActorBase) · 91 GetIsAlerted · 98 GetPlayerControlsDisabled(Integer,Integer,Integer)  
99 GetHeadingAngle(Reference) · 101 IsWeaponMagicOut · 102 IsTorchOut · 103 IsShieldOut · 106 IsFacingUp · 107 GetKnockedState  
108 GetWeaponAnimType · 109 IsWeaponSkillType(ActorValue) · 110 GetCurrentAIPackage · 111 IsWaiting · 112 IsIdlePlaying · 116 IsIntimidatedbyPlayer  
117 IsPlayerInRegion(Region) · 118 GetActorAggroRadiusViolated · 122 GetCrime(Actor,CrimeType) · 123 IsGreetingPlayer · 125 IsGuard · 127 HasBeenEaten  
128 GetStaminaPercentage · 129 HasBeenRead · 130 GetDying · 131 GetSceneActionPercent(Scene,Integer) · 132 WouldRefuseCommand(Reference) · 133 SameFactionAsPC  
134 SameRaceAsPC · 135 SameSexAsPC · 136 GetIsReference(Reference) · 141 IsTalking · 142 GetComponentCount(BaseObject) · 143 GetCurrentAIProcedure  
144 GetTrespassWarningLevel · 145 IsTrespassing · 146 IsInMyOwnedCell · 147 GetWindSpeed · 148 GetCurrentWeatherPercent · 149 GetIsCurrentWeather(Weather)  
150 IsContinuingPackagePCNear · 152 GetIsCrimeFaction(Faction) · 153 CanHaveFlames · 154 HasFlames · 157 GetOpenState · 159 GetSitting  
161 GetIsCurrentPackage(Package) · 162 IsCurrentFurnitureRef(Reference) · 163 IsCurrentFurnitureObj(Furniture) · 170 GetDayOfWeek · 172 GetTalkedToPCParam(Actor) · 175 IsPCSleeping  
176 IsPCAMurderer · 180 HasSameEditorLocationAsRef(Reference,Keyword) · 181 HasSameEditorLocationAsRefAlias(Alias,Keyword) · 182 GetEquipped(BaseObject) · 185 IsSwimming · 190 GetAmountSoldStolen  
192 GetIgnoreCrime · 193 GetPCExpelled(Faction) · 195 GetPCFactionMurder(Faction) · 197 GetPCEnemyofFaction(Faction) · 199 GetPCFactionAttack(Faction) · 203 GetDestroyed  
214 HasMagicEffect(BaseEffect) · 215 GetDefaultOpen · 223 IsSpellTarget(EffectItem) · 224 GetVATSMode · 225 GetPersuasionNumber · 226 GetVampireFeed  
227 GetCannibal · 228 GetIsClassDefault(Class) · 229 GetClassDefaultMatch · 230 GetInCellParam(Cell,Reference) · 231 GetPlayerDialogueInput · 235 GetVatsTargetHeight  
237 GetIsGhost · 242 GetUnconscious · 244 GetRestrained · 246 GetIsUsedItem(BaseObject) · 247 GetIsUsedItemType(FormType) · 248 IsScenePlaying(Scene)  
249 IsInDialogueWithPlayer · 250 GetLocationCleared(Location) · 254 GetIsPlayableRace · 255 GetOffersServicesNow · 258 HasAssociationType(Actor,AssociationType) · 259 HasFamilyRelationship(Actor)  
261 HasParentRelationship(Actor) · 262 IsWarningAbout(FormList) · 263 IsWeaponOut · 264 HasSpell(EffectItem) · 265 IsTimePassing · 266 IsPleasant  
267 IsCloudy · 274 IsSmallBump · 277 GetBaseValue(ActorValue) · 278 IsOwner(Owner) · 280 IsCellOwner(Cell,Owner) · 282 IsHorseStolen  
285 IsLeftUp · 286 IsSneaking · 287 IsRunning · 288 GetFriendHit · 289 IsInCombat(Integer) · 300 IsInInterior  
304 IsWaterObject · 305 GetPlayerAction · 306 IsActorUsingATorch · 309 IsXBox · 310 GetInWorldspace(WorldSpace) · 312 GetPCMiscStat(MiscStat)  
313 GetPairedAnimation · 314 IsActorAVictim · 315 GetTotalPersuasionNumber · 318 GetIdleDoneOnce · 320 GetNoRumors · 323 GetCombatState  
325 GetWithinPackageLocation(Packdata) · 327 IsRidingMount · 329 IsFleeing · 332 IsInDangerousWater · 338 GetIgnoreFriendlyHits · 339 IsPlayersLastRiddenMount  
353 IsActor · 354 IsEssential · 358 IsPlayerMovingIntoNewSpace · 359 GetInCurrentLocation(Location) · 360 GetInCurrentLocationAlias(Alias) · 361 GetTimeDead  
362 HasLinkedRef(Keyword) · 365 IsChild · 366 GetStolenItemValueNoCrime(Faction) · 367 GetLastPlayerAction · 368 IsPlayerActionActive(Integer) · 370 IsTalkingActivatorActor(Actor)  
372 IsInList(FormList) · 373 GetStolenItemValue(Faction) · 375 GetCrimeGoldViolent(FactionNull) · 376 GetCrimeGoldNonviolent(FactionNull) · 378 IsOwnedBy(Actor) · 380 GetCommandDistance  
381 GetCommandLocationDistance · 390 GetHitLocation · 391 IsPC1stPerson · 396 GetCauseofDeath · 397 IsLimbGone(Integer) · 398 IsWeaponInList(FormList)  
402 IsBribedbyPlayer · 403 GetRelationshipRank(Actor) · 407 GetVATSValue(Integer,Integer) · 408 IsKiller(Actor) · 409 IsKillerObject(FormList) · 410 GetFactionCombatReaction(Faction,Faction)  
414 Exists(Reference) · 415 GetGroupMemberCount · 416 GetGroupTargetCount · 426 GetIsVoiceType(VoiceType) · 427 GetPlantedExplosive · 429 IsScenePackageRunning  
430 GetHealthPercentage · 432 GetIsObjectType(FormType) · 434 PlayerVisualDetection · 435 PlayerAudioDetection · 437 GetIsCreatureType(Integer) · 438 HasKey(Reference)  
439 IsFurnitureEntryType(BaseObject) · 444 GetInCurrentLocationFormList(FormList) · 445 GetInZone(EncounterZone) · 446 GetVelocity(Axis) · 447 GetGraphVariableFloat(String) · 448 HasPerk(Perk)  
449 GetFactionRelation(Actor) · 450 IsLastIdlePlayed(IdleForm) · 453 GetPlayerTeammate · 454 GetPlayerTeammateCount · 458 GetActorCrimePlayerEnemy · 459 GetCrimeGold(FactionNull)  
463 IsPlayerGrabbedRef(Reference) · 465 GetKeywordItemCount(Keyword) · 470 GetDestructionStage · 473 GetIsAlignment(Alignment) · 476 IsProtected · 477 GetThreatRatio(Actor)  
479 GetIsUsedItemEquipType(EquipType) · 483 GetPlayerActivated · 485 GetFullyEnabledActorsInHigh · 487 IsCarryable · 488 GetConcussed · 491 GetMapMarkerVisible  
493 PlayerKnows(BaseObject) · 494 GetPermanentValue(ActorValue) · 495 GetKillingBlowLimb · 497 CanPayCrimeGold(Faction) · 499 GetDaysInJail · 500 EPAlchemyGetMakingPoison  
501 EPAlchemyEffectHasKeyword(Keyword) · 503 GetAllowWorldInteractions · 506 DialogueGetAv(ActorValue) · 507 DialogueHasPerk(Perk) · 508 GetLastHitCritical · 510 DialogueGetItemCount(BaseObject)  
511 LastCrippledCondition(ActorValue) · 512 HasSharedPowerGrid(Reference) · 513 IsCombatTarget(Actor) · 515 GetVATSRightAreaFree(Reference) · 516 GetVATSLeftAreaFree(Reference) · 517 GetVATSBackAreaFree(Reference)  
518 GetVATSFrontAreaFree(Reference) · 519 GetIsLockBroken · 520 IsPS3 · 521 IsWindowsPC · 522 GetVATSRightTargetVisible(Reference) · 523 GetVATSLeftTargetVisible(Reference)  
524 GetVATSBackTargetVisible(Reference) · 525 GetVATSFrontTargetVisible(Reference) · 528 IsInCriticalStage(CriticalStage) · 530 GetXPForNextLevel · 533 GetInfamy(FactionNull) · 534 GetInfamyViolent(FactionNull)  
535 GetInfamyNonViolent(FactionNull) · 536 GetTypeCommandPerforming · 543 GetQuestCompleted(Quest) · 544 GetSpeechChallengeSuccessLevel · 547 IsGoreDisabled · 550 IsSceneActionComplete(Scene,Integer)  
552 GetSpellUsageNum(EffectItem) · 554 GetActorsInHigh · 555 HasLoaded3D · 560 HasKeyword(Keyword) · 561 HasRefType(LocationRefType) · 562 LocationHasKeyword(Keyword)  
563 LocationHasRefType(LocationRefType) · 565 GetIsEditorLocation(Location) · 566 GetIsAliasRef(Alias) · 567 GetIsEditorLocationAlias(Alias) · 568 IsSprinting · 569 IsBlocking  
570 HasEquippedSpell(CastingSource) · 571 GetCurrentCastingType(CastingSource) · 572 GetCurrentDeliveryType(CastingSource) · 574 GetAttackState · 576 GetEventData(Event,EventData) · 577 IsCloserToAThanB(Reference,Reference)  
578 LevelMinusPCLevel · 580 IsBleedingOut · 584 GetRelativeAngle(Reference,Axis) · 589 GetMovementDirection · 590 IsInScene · 591 GetRefTypeDeadCount(Location,LocationRefType)  
592 GetRefTypeAliveCount(Location,LocationRefType) · 594 GetIsFlying · 595 IsCurrentSpell(EffectItem,CastingSource) · 596 SpellHasKeyword(CastingSource,Keyword) · 597 GetEquippedItemType(CastingSource) · 598 GetLocationAliasCleared(Alias)  
600 GetLocationAliasRefTypeDeadCount(Alias,LocationRefType) · 601 GetLocationAliasRefTypeAliveCount(Alias,LocationRefType) · 602 IsWardState(WardState) · 603 IsInSameCurrentLocationAsRef(Reference,Keyword) · 604 IsInSameCurrentLocationAsRefAlias(Alias,Keyword) · 605 LocationAliasIsLocation(Alias,Location)  
606 GetKeywordDataForLocation(Location,Keyword) · 608 GetKeywordDataForAlias(Alias,Keyword) · 610 LocationAliasHasKeyword(Alias,Keyword) · 611 IsNullPackageData(Packdata) · 612 GetNumericPackageData(Packdata) · 613 IsPlayerRadioOn  
614 GetPlayerRadioFrequency · 615 GetHighestRelationshipRank · 616 GetLowestRelationshipRank · 617 HasAssociationTypeAny(AssociationType) · 618 HasFamilyRelationshipAny · 619 GetPathingTargetOffset(Axis)  
620 GetPathingTargetAngleOffset(Axis) · 621 GetPathingTargetSpeed · 622 GetPathingTargetSpeedAngle(Axis) · 623 GetMovementSpeed · 624 GetInContainer(Reference) · 625 IsLocationLoaded(Location)  
626 IsLocationAliasLoaded(Alias) · 627 IsDualCasting · 629 GetVMQuestVariable(Quest,String) · 630 GetCombatAudioDetection · 631 GetCombatVisualDetection · 632 IsCasting  
633 GetFlyingState · 635 IsInFavorState · 636 HasTwoHandedWeaponEquipped · 637 IsFurnitureExitType(BaseObject) · 638 IsInFriendStatewithPlayer · 639 GetWithinDistance(Reference,Float)  
640 GetValuePercent(ActorValue) · 641 IsUnique · 642 GetLastBumpDirection · 644 GetInfoChallangeSuccess · 645 GetIsInjured · 646 GetIsCrashLandRequest  
647 GetIsHastyLandRequest · 650 IsLinkedTo(Reference,Keyword) · 651 GetKeywordDataForCurrentLocation(Keyword) · 652 GetInSharedCrimeFaction(Reference) · 654 GetBribeSuccess · 655 GetIntimidateSuccess  
656 GetArrestedState · 657 GetArrestingActor · 659 HasVMScript(String) · 660 GetVMScriptVariable(String,String) · 661 GetWorkshopResourceDamage(ActorValue) · 664 HasValidRumorTopic(Quest)  
672 IsAttacking · 673 IsPowerAttacking · 674 IsLastHostileActor · 675 GetGraphVariableInt(String) · 678 ShouldAttackKill(Actor) · 680 GetActivationHeight  
682 WornHasKeyword(Keyword) · 683 GetPathingCurrentSpeed · 684 GetPathingCurrentSpeedAngle(Axis) · 691 GetWorkshopObjectCount(BaseObject) · 693 EPMagic_SpellHasKeyword(Keyword) · 694 GetNoBleedoutRecovery  
696 EPMagic_SpellHasSkill(ActorValue) · 697 IsAttackType(Keyword) · 698 IsAllowedToFly · 699 HasMagicEffectKeyword(Keyword) · 700 IsCommandedActor · 701 IsStaggered  
702 IsRecoiling · 703 HasScopeWeaponEquipped · 704 IsPathing · 705 GetShouldHelp(Actor) · 706 HasBoundWeaponEquipped(CastingSource) · 707 GetCombatTargetHasKeyword(Keyword)  
709 GetCombatGroupMemberCount · 710 IsIgnoringCombat · 711 GetLightLevel · 713 SpellHasCastingPerk(Perk) · 714 IsBeingRidden · 715 IsUndead  
716 GetRealHoursPassed · 718 IsUnlockedDoor · 719 IsHostileToActor(Actor) · 720 GetTargetHeight(Reference) · 721 IsPoison · 722 WornApparelHasKeywordCount(Keyword)  
723 GetItemHealthPercent · 724 EffectWasDualCast · 725 GetKnockStateEnum · 726 DoesNotExist · 728 GetPlayerWalkAwayFromDialogueScene · 729 GetActorStance  
734 CanProduceForWorkshop · 735 CanFlyHere · 736 EPIsDamageType(DamageType) · 738 GetActorGunState · 739 GetVoiceLineLength · 741 ObjectTemplateItem_HasKeyword(Keyword)  
742 ObjectTemplateItem_HasUniqueKeyword(Keyword) · 743 ObjectTemplateItem_GetLevel · 744 MovementIdleMatches(Integer,Integer) · 745 GetActionData · 746 GetActionDataShort(Integer) · 747 GetActionDataByte(Integer)  
748 GetActionDataFlag(Integer) · 749 ModdedItemHasKeyword(Keyword) · 750 GetAngryWithPlayer · 751 IsCameraUnderWater · 753 IsActorRefOwner(Actor) · 754 HasActorRefOwner(Actor)  
756 GetLoadedAmmoCount · 757 IsTimeSpanSunrise · 758 IsTimeSpanMorning · 759 IsTimeSpanAfternoon · 760 IsTimeSpanEvening · 761 IsTimeSpanSunset  
762 IsTimeSpanNight · 763 IsTimeSpanMidnight · 764 IsTimeSpanAnyDay · 765 IsTimeSpanAnyNight · 766 CurrentFurnitureHasKeyword(Keyword) · 767 GetWeaponEquipIndex  
769 IsOverEncumbered · 770 IsPackageRequestingBlockedIdles · 771 GetActionDataInt · 772 GetVATSRightMinusLeftAreaFree(Reference) · 773 GetInIronSights(Reference) · 774 GetActorStaggerDirection  
775 GetActorStaggerMagnitude · 776 WornCoversBipedSlot(Integer) · 777 GetInventoryValue · 778 IsPlayerInConversation · 779 IsInDialogueCamera · 780 IsMyDialogueTargetPlayer  
781 IsMyDialogueTargetActor · 782 GetMyDialogueTargetDistance · 783 IsSeatOccupied(Keyword) · 784 IsPlayerRiding · 785 IsTryingEventCamera · 786 UseLeftSideCamera  
787 GetNoteType · 788 LocationHasPlayerOwnedWorkshop · 789 IsStartingAction · 790 IsMidAction · 791 IsWeaponChargeAttack · 792 IsInWorkshopMode  
793 IsWeaponChargingHoldAttack · 794 IsEncounterAbovePlayerLevel · 795 IsMeleeAttacking · 796 GetVATSQueuedTargetsUnique · 797 GetCurrentLocationCleared · 798 IsPowered  
799 GetTransmitterDistance · 800 GetCameraPlaybackTime · 801 IsInWater · 802 GetWithinActivateDistance(Reference) · 803 IsUnderWater · 804 IsInSameSpace(Reference)  
805 LocationAllowsReset · 806 GetVATSBackRightAreaFree(Reference) · 807 GetVATSBackLeftAreaFree(Reference) · 808 GetVATSBackRightTargetVisible(Reference) · 809 GetVATSBackLeftTargetVisible(Reference) · 810 GetVATSTargetLimbVisible(Reference)  
811 IsPlayerListening(Float) · 812 GetPathingRequestedQuickTurn · 813 EPIsCalculatingBaseDamage · 814 GetReanimating · 817 IsInRobotWorkbench  

</details>

## Appendix B — upstream libespm bugs found (fix regardless of FO4)

| Bug | Location |
|---|---|
| `NAVM::kType = "NVNM"` → navmeshes never indexed | [src: libespm/include/libespm/NAVM.h:14] |
| NPC_ `SNAM` rank read from offset 0 (the FormID) | [src: libespm/src/NPC_.cpp:21] |
| `GetScriptData` is `noexcept` but `ReadPropertyValue` throws | [src: libespm/include/libespm/RecordHeader.h:29-31; src/Utils.cpp:100,134-140] |
| `WRLD` `WCTR`/`DNAM`/`ONAM` copied byte-wise with `std::copy_n(char*)` | [src: libespm/src/WRLD.cpp:19,35,54] |
| `LIGH` falloff @0x0F and intensity amplitude @0x1F (should be 0x10/0x20) | [src: libespm/src/LIGH.cpp:18,23] |
| Leveled list entries assumed contiguous after `LLCT` (breaks with `COED`) | [src: libespm/src/LeveledListBase.cpp:22-24] |
| `FULL` returned as `const char*` in CONT/TREE/FLOR/QUST although Skyrim.esm is localized | [src: libespm/src/CONT.cpp:18 …] |
| `CombineBrowser::FindNavMeshes` passes the combined ID instead of `rawFormId` | [src: libespm/src/CombineBrowser.cpp:74] |
| `ZlibDecompress` uses a single `inflate(Z_NO_FLUSH)` without checking output size | [src: libespm/include/libespm/ZlibUtils.h:7-28] |
| `LocalizationProvider::ParseILDLStrings` reads a 1-byte length | [src: skymp5-server/cpp/localization_provider/LocalizationProvider.cpp:63] |

## Appendix C — sources

- xEdit: https://github.com/TES5Edit/TES5Edit (Core/wbDefinitionsFO4.pas, wbDefinitionsFO4Saves.pas, wbDefinitionsCommon.pas, wbInterface.pas, wbImplementation.pas, wbLocalization.pas, wbLoadOrder.pas, wbBSArchive.pas, Hardcoded/Fallout4.esp; xEdit/xeInit.pas, xeMainForm.pas)
- fopdoc (Fallout 4 section is sparse; record header/flags/compressed data, groups): https://github.com/TES5Edit/fopdoc/tree/master/Fallout4
- Mutagen FO4 record XML: https://github.com/Mutagen-Modding/Mutagen/tree/dev/Mutagen.Bethesda.Fallout4/Records
- FallrimTools / ReSaver: https://github.com/mdfairch/FallrimTools
- fo4-save-cleaner (2026, Python, round-trips .fos): https://github.com/pub-struct/fo4-save-cleaner ; 010 template: https://github.com/okzach/fallout_savegame_010_editor_binary_template
- rsm-bsa: https://github.com/Ryan-rsm-McKenzie/bsa (tag 4.1.0, master `2c7280d`, commit `04d1fdf`)
- CommonLibF4 (libxse): https://github.com/libxse/commonlibf4 (RE/B/BGSMod.h, RE/A/ACTOR_BASE_DATA.h, RE/T/TESActorBaseData.h, RE/A/ActorValue.h)
- F4SE runtime list (current release runtime 1.11.240, "creation club"): https://github.com/ianpatt/f4se `f4se_common/f4se_version.h`
- Web: header 1.0 vs 0.95 / VR (https://www.nexusmods.com/fallout4/articles/6122), ESL ranges (https://www.nexusmods.com/fallout4/mods/35922), engine limits incl. 1.11.240 BA2 limit (https://www.nexusmods.com/fallout4/articles/6251), BA2 v7/v8 (https://www.nexusmods.com/fallout4/mods/81640), strings in Interface.ba2 (https://www.nexusmods.com/fallout4/mods/4265), scripts in Misc.ba2 (https://scrivener07.github.io/BGS-Handbook/wiki/fo4/papyrus/getting-started/index), Object Mod MUL+ADD (https://falloutck.uesp.net/wiki/Object_Mod), console AV IDs (https://fallout.fandom.com/wiki/Fallout_4_console_commands). The Fallout CK wiki (falloutck.uesp.net) returned HTTP 403 to direct fetches; its content was only available through search summaries.
