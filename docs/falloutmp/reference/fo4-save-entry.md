# Fallout 4 saves and the main-menu entrance — research reference

Status: research reference (2026-10-06), for [F33 — The entrance](../features/F33-entrance.md). It extends [fo4-data-formats.md §7](fo4-data-formats.md) (the `.fos` layout as of 2023) with the Next-Gen (1.10.980/984) and Anniversary Edition (1.11.x) runtimes, and adds the engine side: loading a save from the main menu, the main-menu UI, save blocking and the missing-content prompt.

Nothing here has been run in game yet. Every claim carries a marker:

| Marker | Meaning |
|---|---|
| **[V]** | Read in source code or an official document (file:line or URL) |
| **[M:x]** | Measured by project *x* on real saves or in a live game. We read their code or notes; we could not reproduce it without real `.fos` files |
| **[X]** | Shipped and working in third-party code or seen in crash logs; not tested by us |
| **[I]** | Inference |
| **[U]** | Unknown or unverified |
| **[G]** | Must be checked in game (listed again in §10) |

## Sources

| Short | Source |
|---|---|
| xE | xEdit `Core/wbDefinitionsFO4Saves.pas`, dev-4.1.6 @ `9fb0168` (2026-09-06) |
| RS | ReSaver, `mdfairch/FallrimTools` @ `61bb57d` (2023-10-01), `src/main/java/resaver/ess/` |
| FW | `ThePie88/FO4_Wrld` @ `4200f32`: a Fallout 4 multiplayer project that already rewrites player position in 1.11.191 saves and live-tests the result (`savepos.py`, `fw_native/`) |
| CMP | `Variiuz/CommonwealthMP` @ `91de882`: a Fallout 4 multiplayer mod on libxse CommonLibF4 with JOIN/HOST main-menu buttons |
| po3 | `powerof3/StartOnSaveF4` @ `cb10417` (loads a save at startup on AE) and powerof3 CommonLibF4 @ `7e99df6` |
| Luca | `LucaDotGit/CommonLibF4` @ `e337150` (ID tables) |
| CL | The CommonLibF4 the plugin pins (libxse `7c8c6f8`) |
| F4SE | F4SE 0.7.9 sources |
| RC | `Force67/recreation` `components/bethesda/savegame*` (measurements on real saves) |
| MCP | `m4rmzNexus/fo4-mcp` (AE save inspection; ReSaver headless shim) |
| LP | `luisparravicini/fo4-save-inspector` |
| WB | Wrye Bash `Mopy/bash/bosh/save_headers.py` |
| FT | `nycz/FaceTransfer` (player face layout, 2015) |
| BO4 | Buffout 4 crash logs, `evildarkarchon/crash-logs` @ `763baed` (OG runtime 1.10.163) |
| SK | Upstream SkyMP @ `27cbc0d` (before the Skyrim parts were removed): `skyrim-platform/src/platform_se/skyrim_platform/`, `skymp5-client/src/services/services/`, `skymp5-server/ts/systems/` |
| TE | `tiltedphoques/TiltedEvolution` @ `d6ce567b` (Skyrim Together Reborn, Fallout Together) |
| CO | Commonwealth Online 1.1.0 (closed client; changelog and server packages @ `7b52878`) |
| F4MP | `cokwa/f4mp` and Jous99's F4MP client-ng |
| VMP | vaultmp (Fallout 3) |
| NVMP | NV:MP wiki (nuwiki.nv-mp.com) |
| TW | Thornswood, a SkyMP server fork (`EvilPatrick06/skymp` pull requests) |
| AS | Alternate-start mods: Skip (Nexus 88418), SKK Fast Start (29227), Start Me Up Redux (56984) |

---

## 1. The `.fos` format by runtime

| Runtime | Header `version` | `formVersion` | Evidence |
|---|---|---|---|
| Launch (1.1–1.2) | 11 (12, 13 seen) | ~60–61 | WB:616; RS requires fv ≥ 60 (ESS.java:287) [V] |
| OG with Creation Club (2019 save) | 15 | 68 (every change-form `version` byte = 68) | [M:RC savegametest.cc:435,537] |
| OG 1.10.163 | 15 | 68 | no sample [I] |
| Next-Gen 1.10.984 | 15 | 68 | [M:LP README:13-14] |
| Anniversary 1.11.169 / .191 / .221 | 15 | **69** | [M:MCP save_inspect.py:69-71]; [M:FW savepos.py:9]; [M:LP] |
| Anniversary 1.11.240 (our tester) | 15 [I] | 69 [I] | not measured [U] [G] |

- **The container did not change from OG to Next-Gen to Anniversary.** Header, screenshot, `formVersion` + `gameVersion`, plugin block, file location table (FLT) and block framing are the same:
  - FW walks a 1.11.191 save and asserts that global data 1 ends exactly at FLT[3] and the change forms end exactly at FLT[5] [M:FW savepos.py:204,244];
  - MCP did a byte-identical no-op round trip of a 5 MB Anniversary save [M:MCP];
  - pre-Next-Gen ReSaver 6.0.636 reads and rewrites Anniversary saves at identical size [M:MCP V2-backlog.md:307-337].
- **What `formVersion` 69 changed is unknown.** It must sit inside payloads that ReSaver keeps raw or parses compatibly [I]. No new global-data type is documented [U]; xEdit's save definitions only had cosmetic commits since 2022 [V].
- **Compression: none for the whole body, on any runtime.** ReSaver reads a compression field only for Skyrim SE; for Fallout 4 `supportsCompression()` is false (RS Header.java:122, ESS.java:1023-1040) [V]; Wrye Bash hard-codes no compression for Fallout 4 (save_headers.py:624) [V]. The "None/ZLIB/LZ4" list seen on the ReSaver page is the Skyrim SE enum. **Individual change forms** are zlib-compressed when `length2 > 0`; `length2 == 0` (stored) is also legal (RS ChangeForm.java:96) [V].
- **Light plugins:** a `u16` count plus names after the full list, present when `formVersion` ≥ 68 (RS ESS.java:1009); xEdit tests header version > 14 (xE:7308); all agree on real saves [V]. Anniversary Creations appear as ordinary entries [M:MCP].
- **Papyrus (global data 1001):** own `u16` header, string table (u32 count when fv > 61), u32 script and struct counts, then definitions and instances (RS papyrus/Papyrus.java:70-150) [V]. ReSaver parses Anniversary Papyrus blocks [M:MCP].
- **Loading across runtimes:** newer runtimes load older saves; the Anniversary patch of 16 Dec 2025 says "Game now recognizes Creation Club content upon loading a save file from an older build" (help.bethesda.net a_id 72995) [V]. An Anniversary save (fv 69) on an older runtime: assume refused or unsafe [U]. **A template must be made on the runtime it targets, or an older one with the same container.**
- **Checksums: none.** No parser has one; ReSaver's CRC32 is an in-memory digest (ESS.java:248). FW's edited saves load in the engine [V/M:FW].

## 2. Layouts a patching writer touches

All little-endian. `wstr` = `u16` length + bytes, no NUL.

### 2.1 Header (xE:7269-7310; RS Header.java:42-127) [V]
`char[12] "FO4_SAVEGAME"`, `u32 headerSize` (bytes from `version` to `shotHeight`; RS rejects ≥ 256), `u32 version`, `u32 saveNumber`, `wstr playerName` (may be empty), `u32 level`, `wstr location` (display text), `wstr playTime` (localized text, e.g. `5d.16h.29m.5 days.16 hours.29 minutes`), `wstr raceEditorId`, `u16 sex`, `f32 curXP`, `f32 nextXP`, `u64 FILETIME`, `u32 shotWidth`, `u32 shotHeight`, then `w*h*4` bytes of RGBA.

FW moves players without touching any byte before the FLT, and the engine loads the result [M:FW savepos.py:604]: **the header text is cosmetic**. ReSaver accepts a tiny screenshot; 0×0 in the engine is unknown, so keep a small real image [U].

### 2.2 Plugin block [V]
`u8 formVersion`, `wstr gameVersion` (e.g. `1.11.191.0`), `u32 pluginInfoSize` (bytes after the field; RS checks it, PluginInfo.java:130), `u8 n` + n names, `u16 m` + m light names.

### 2.3 File location table (100 bytes, absolute offsets; RS FileLocationTable.java) [V]
`[0]` FormID array, `[1]` unknown table 3, `[2]` global data 1, `[3]` global data 2, `[4]` change forms, `[5]` global data 3, `[6–8]` counts of gd1/gd2/gd3 (exact in Fallout 4; Skyrim's is off by one), `[9]` change-form count, `[10–24]` unused.

- **File order:** gd1, gd2, change forms, gd3, FormID array, visited worldspaces, unknown table 3.
- **Rebuild** (FLT.java:105-125): `gd1 = 16 + headerSize + w*h*4 + 1 + 2 + len(gameVersion) + 4 + pluginInfoSize + 100`; each later offset = previous + Σ(8 + len) of the blocks in between; `[1] = [0] + 4 + 4·nFormIDs + 4 + 4·nVisitedWorldspaces`.
- **What to shift** (FW savepos.py:407,563): plugin block changed → all six; gd1 changed → `[0,1,3,4,5]`; change forms changed → `[0,1,5]`; FormID array grew → `[1]`.
- ReSaver seeks straight to `[0]` and `[2]`, so offsets must be exact; FW also checks their order.

### 2.4 Global data blocks: `{u32 type; u32 len; u8 data[len]}`
Table 1 holds types 0–8 (xE:3613), table 2 types 100 and up, and table 3 types 1000 and up (Papyrus is 1001), numbered as in Skyrim [V]. The four blocks below are all in table 1, which matches FW's shift rule for them (§2.3).
- **Type 1, Player Location: 30 bytes** (no trailing byte; xEdit's "31" is a Skyrim leftover, RC measured 30 on 54 FO4 saves, savegame.cc:375): `u32 nextObjectId` (every 0xFF id in the save is below it: keep it), `refId worldspace` (interiors keep the last worldspace or null), `s32 gridX, gridY`, `refId worldOrCell` (the cell for interiors), `f32 x, y, z` [M:FW savepos.py:22-24,49-57]. The grid equals floor(pos/4096) in 38 of 55 saves and is off by one otherwise (RC savegame.h:113-121); FW writes floor(x/4096) [M].
- **Type 3, Global Variables:** `vsval n` + n × `{refId, f32}` (xE:3648) [V]; an OG save had 910 entries [M:RC]. GameHour `0x38` and GameDaysPassed `0x39` per F25 [I]; their refIds are usually kind 1 (`40 00 38`), but resolve generically and patch the float in place.
- **Type 6, Weather:** 63 bytes, 92 outdoors [M:FW savepos.py:62-66]: refIds at 0 (climate), 3 (weather), 6 (previous), 9, 12, 15 (region weather); f32 at 18, 22, 26 (times, partly inferred); a u32 at 54; **u32 sky mode at 58: 3 = outdoors, 1 = interior**; u8 flags at 62 (bits 0–1 add optional blobs, xE:3668-3690; +29 bytes outdoors).
- **Type 7, Audio:** holds the acoustic space. FW: moving a player from an interior to an exterior without copying types 6 and 7 from an outdoor save gave "no shadows… a purple sky" until the player used a door (savepos.py:60-70) [M].

### 2.5 Change forms (RS ChangeForm.java:40-130) [V]
`refId` (3 bytes big-endian), `u32 changeFlags`, `u8 type` (low 6 bits = type; top 2 bits = length width: 0 u8, 1 u16, 2 u32, 3 invalid), `u8 version`, `len1`, `len2` (inflated size, 0 = stored), data. The width must fit max(len1, len2): ReSaver keeps the old width on rewrite (bug, line 193); FaceTransfer writes the invalid 0xC0 (bug); FW widens correctly (savepos.py:427-457).

### 2.6 The player's ACHR (refId `40 00 14`, change-form type 1)
FW observed `MOVE` (0x2) set, the body starting with initial data [M]. The initial-data layout depends on the flags (xE:2284-2312; RS ChangeFormACHR.java:47-56) [V]:

| Case | Initial data |
|---|---|
| `MOVE` or `HAVOK_MOVE` | type 4, 27 bytes: `refId cell/worldspace; f32 pos[3]; f32 rot[3]` (radians; FW writes `(0, 0, yaw)`) |
| `CELL_CHANGED` (0x8) or `PROMOTED` (0x2000000) | type 6, 34 bytes: + `refId startingCell; s16; s16` |
| created ref (kind 2) | type 5, 31 bytes: + `u8; refId base` |

The first 27 bytes are the same in all three. With `HAVOK_MOVE`, a vsval-length havok blob follows (RS:66-67). **Power armor:** a worn frame is its own created REFR (base `0x2079E`), linked from the player by ExtraPowerArmor (0xBB); it must move too, with its 0x4000 temporary bit cleared [M:FW:33-46].

### 2.7 The player's NPC_ (refId `40 00 07`): face and body
xEdit still nulls FACE and BODY_SCALES "until decoded" (xE:6661,6706); ReSaver applies Skyrim's layout (ChangeFormNPC.java:257-275); neither is valid for Fallout 4 [V]. The only Fallout 4 layout is FaceTransfer's (2015, GPL-3, extract.py:208-238), verified on 1.2-era saves only:
- flag 1: 20 bytes; flag 6: factions; flag 5: wstr; flag 24: u8;
- **flag 11, FACE:** u8, refId, 4-byte colour, refId, vsval + head-part refIds, u8 tints flag [+ u32 n + n×8], u32 n + n×40 (morph sliders), u32 n + n×10;
- **flag 14, BODY_SCALES:** u32 n + n×4, then f32 thin, muscular, large.

Unverified on Next-Gen/Anniversary [U]. **The entrance applies appearance after the load through natives (F03) and does not write NPC_ data.**

### 2.8 RefID, FormID array, other tables
- **RefID** (xEdit wbInterface.pas:21632-21656; engine `BGSNumericIDIndex`) [V]: 24-bit big-endian; top 2 bits = kind: 0 → `FormID[v−1]` (0 = null), 1 → Fallout4.esm form `v`, 2 → `0xFF000000 | v`, 3 → invalid.
- **FormID array:** `u32 n` + n × u32 load-order FormIDs [V]. High byte = index in the save's full plugin list; light ids are `0xFE000000 | lightIdx<<12 | id&0xFFF` (RS PluginInfo.java:53). It can be empty (an OG save with only Fallout4.esm forms had none) [M:RC]. To reference a new form: append it, use kind 0 with index n+1, add 4 to FLT[1].
- **Visited worldspaces:** `u32 n` + n × u32 load-order FormIDs; detached exterior CELL forms point into it by u16 index (xE:6423-6440).
- **Unknown table 3:** `u32 size` + `u32 n` + wstrings (kept raw).
- **vsval:** low 2 bits = width; sources disagree on code 2 (4 bytes per xEdit and FW, 3 per ReSaver and FaceTransfer); only matters above 0x4000. **Never re-encode a vsval; patch in place.**

## 3. Plugin lists and the missing-content prompt

- **A missing full or light plugin** shows "This save relies on content that is no longer present. Some objects may no longer be available. Continue loading?" (help.bethesda.net a_id 33791) [V]. `BGSSaveLoadManager::QUEUED_TASK` has `kMissingContentLoad = 0x100` and `kConfirmModsLoad = 0x8000` (CL BGSSaveLoadManager.h:41,48) [V].
- **The Anniversary update adds Creations prompts:** "This save relies on missing Creations" (a_id 72929), search/restore prompts and a "disabled but not deleted" dialogue (Dec 2025 notes) [V doc].
- **Extra loaded plugins** on the client: no prompt [I]; their start-game-enabled quests start as if the mod were added mid-game [I].
- **Stored FormIDs refer to the save's own plugin list; the engine remaps by name.** F4SE does the same for its co-save (`LookupModByName`, InternalSerialization.cpp:20-77) [V for F4SE, I for the engine].
- **Consequence:** the prompt is avoided when every name in the template's lists is loaded. Writing the player's exact list is **unnecessary**, and replacing the list is only safe if every FormID-array and visited-worldspace high byte (and FE light index) is remapped, which is impossible for raw FormIDs inside opaque blocks [U].
- **SkyMP's `OverwritePluginInfo`** replaces the list without remapping, and it broke as soon as a client's DLC order differed from the template's. Thornswood, a SkyMP fork, counted "10,547 of 10,562 entries named another DLC" [web: github.com/EvilPatrick06/skymp/pull/36]. Their fix:
  - keeps the save's own lists;
  - remaps the ids it writes by plugin name;
  - refuses saves that name plugins the client lacks.
- **Extra plugins have a cost too:** plugins the save doesn't name are new to it, so their start-game-enabled quests start at load, as when DLC is installed mid-game [I].
- **Suppression switches exist** but should not be relied on: `LoadGame(..., ignoreMissingContent=true)` (po3 maps its "Disable Missing Content Warning" option to it, Settings.cpp:60,128) [X]; the Nexus plugin "Skip Missing Mod Warning" (mods/109657, built for 1.11.240) [V: author's claim]. The Creations prompts may still block automated loading [I].

## 4. Loading a save by name from the main menu

### 4.1 The function the plan named is the wrong one
`commonlib-port-map.md` and `PLAT-050` planned to call F4SE's `LoadGame` address `0xBEE760` with a save name. That address is the **inner** loader `BGSSaveLoadGame::LoadGame(BGSSaveLoadFile*, bool, BSScrapArray<BSFixedString>&)`, which takes a file object [X: F4SE OG address `0xCDE9F0` (f4se git f0744a3) matches the BO4 frame `BGSSaveLoadGame::LoadGame(...)` with `BGSSaveLoadManager::DoLoadGame(char*,int,uint,bool,bool)` and `LoadGameJob::JobFn` above it on a job thread, BO4 `crash-2023-08-18-19-11-05 Chaseguy.log:53-70`]. Likewise `0xBEE0B0` is the inner `BGSSaveLoadGame::SaveGame` [X: BO4 `crash-2023-03-20-02-44-42 DeluvE.log`].

### 4.2 The engine functions (Anniversary IDs)

| Function | AE ID (OG ID) | Signature / note | Source |
|---|---|---|---|
| `BGSSaveLoadManager::LoadGame` (the load request) | **2228039** (1245410) | `bool(mgr, const char* name, int32 device = -1, uint32 stats = 0, bool checkForMods, bool ignoreMissingContent)`; name without path or `.fos` | po3CL BGSSaveLoad.h:176-181; kamicane IDs.hpp:375 [X] |
| `DoLoadGame` (job body) | 2228040 (540706) | same arguments; don't call directly | Luca IDs.hpp:600; BO4 [X] |
| `QueueSaveLoadTask` / `BuildSaveGameList` / `GetSaveDirectoryPath` | 2228080 / 2228053 / 2228028 | | CL BGSSaveLoadManager.h:78-97, IDs.h:392-398 [V] |
| `SaveGame(char*, int, uint, bool)` / `DeleteSaveFile` | 2228036 (954817) / 2228033 | | Luca IDs.hpp:585-602 [X] |
| `StartMenuBase::DoLoadGame(int index)` | vfunc 0x14 | | CL StartMenuBase.h:20 [V] |
| `Console::ExecuteCommand(const char*)` | 2248537 (1061864) | | CL Console.h:17-22, IDs.h:954 [V] |
| `DoBeforeNewOrLoad()` | 2228951 | | po3 Settings.cpp:5-10 [X] |
| `Main::QGameSystemsShouldUpdate` (`bool*`) | 2698031 | | CL Main.h:48-52, IDs.h:1481 [V] |
| `StartNewGame(MainMenu*)` | 2249317 | | po3 Settings.cpp:12-17 [X] |
| `MessageMenuManager` singleton / `Create` | 4796373 / 2249456 | dialogs at the title menu | CL MessageMenuManager.h:20-34 [V]; CMP flow.cpp:100-112 [X] |

**Expected 1.11.240 addresses [I]:** every F4SE save/load hook moved by exactly +0x520 between 1.11.191 and 1.11.240 (F4SE git 85f9732 vs 4cb4566) [V]. Applying the shift to FW's 1.11.191 addresses predicts 2228039 → `0xBF98D0`, 2228040 → `0xBF9980`, 2228036 → `0xBF9330`, 2228080 → ~`0xBFE9E0` [G]. `REL::Offset2ID` and `IDDB::offset` do nearest-match lookups without an exact check (CL commonlib-shared Offset2ID.cpp:35-59, IDDB.cpp:425-451) [V]: **assert `REL::ID(id).offset()` against the expected address at startup.**

### 4.3 Routes
- **A. Direct call (recommended)** — what po3 ships for the Anniversary runtime (po3 main.cpp:9-11, Settings.cpp:65-133) [X]: at `kGameDataReady` build the save list if needed; then in an F4SE task: `DoBeforeNewOrLoad()`, `*QGameSystemsShouldUpdate = true`, `mgr->LoadGame(name, -1, 0, true, ignoreMissing)`. FW's decompile of the console `load` command shows the same steps (precondition, prep, flag, `LoadGame(mgr, name, -1, flags, 1, 0)`) [X: FW offsets.h:589-617]. For an unreadable file it returns 0 and the world is untouched [X: FW lifecycle.cpp:497-512]. It blocks the main thread for ~6 s while pumping Windows messages (re-entrancy) [X: FW CHANGELOG.md:1022-1031, main_menu_hook.cpp:481].
- **B. Console:** `RE::Console::ExecuteCommand("load \"FalloutMP_x\"")` on the main thread; reg2k's fo4-autoload did this from a MainMenu.swf callback (Autoload.cpp:33-39,156-166,224-253) and CMP runs console commands at the title menu (net/query.cpp:126-133) [X]. No return value; can't suppress the missing-content prompt. **The fallback.**
- **C. The menu's queue:** set `queuedEntryToLoad` (only `fileName`), then `QueueSaveLoadTask(kLoadGame = 0x40)` [X: kamicane RotatingSaves project-main.hpp:100-111]; asynchronous.
- **D.** `BuildSaveGameList`, then `MainMenu->DoLoadGame(index)` [X: CMP menu/forms.cpp:108-161].
- **Papyrus has no load function** (Game.psc: QuitToMainMenu, RequestAutoSave, RequestSave; F4SE adds none) [V].

F4SE hooks the entries of the inner SaveGame, LoadGame and DeleteSaveGame with 5-byte branches (Hooks_SaveLoad.cpp:15-27,42-54,84-162) [V], so every route still produces `kPreLoadGame` (with the name) and `kPostLoadGame` (with a success bool) and handles the co-save. Don't detour those three entries ourselves [I]. A missing `.f4se` co-save is fine (Serialization.cpp:420-427) [V]; a **stale** co-save with the same name would be loaded, so use unique names and delete leftovers.

### 4.4 Event order and "playable"
1. Main thread: the load request runs; MainMenu closes; LoadingMenu opens [I].
2. **Loader thread** (a BSJobs worker, not the main thread): `LoadGameJob → DoLoadGame → kPreLoadGame → BGSSaveLoadGame::LoadGame → kPostLoadGame` [X: BO4 stack; FW offsets.h:3089]. **Our `Main.cpp` handlers for these messages run off the main thread**: flip atomic flags only and hand work to a task.
3. `TESLoadGameEvent` (ID 2201848): order relative to `kPostLoadGame` unknown [G].
4. LoadingMenu closes, then FaderMenu; reg2k treats "FaderMenu closed after the post-load message" as load complete (Autoload.cpp:255-267) [X].

**Playable test:** LoadingMenu, FaderMenu and MainMenu closed; the player has a parent cell and 3D; a settle window (FW: 120 ticks in a row and ≥ 2 s with no load and no cell change, main_menu_hook.cpp:186-190) [X]. STATUS.md (2026-10-06) records that changing the world during start-up crashed the game; FW notes the world can tick inside the blocking load call (lifecycle.cpp:540-546). **Set a `loadInFlight` flag before calling LoadGame.**

**Timing:** FW measured ~6 s. Load speed depends on the frame rate during loading screens: High FPS Physics Fix's Load Accelerator (VSync off while loading, up to 350 FPS; nexusmods.com/fallout4/mods/10283) and its `[Fixes] DisableBlackLoadingScreens` / `DisableAnimationOnLoadingScreens` ("up to 2x faster") help (gist.github.com/GamerPoets/62088ed23024b31f6ca31c03f55b51b9). `bAlwaysActive=1` keeps loading while alt-tabbed [X: FW launcher/fo4_ini.py].

## 5. The main menu

- **Ready:** `MenuOpenCloseEvent` for "MainMenu" plus `uiMovie && menuObj.IsObject() && hasDoneFirstAdvanceMovie && gameDataReady (@0x220) && GetIsMenuReady()` (vfunc 0x16) [X: CMP flow.cpp:55-66; V: CL StartMenuBase.h:20-22,71]. Calling LoadGame while the menu is still being built gave FW a black screen; it waits 4 s (main_menu_hook.cpp:46-58,69) [X]. po3 loads from `kGameDataReady` through an F4SE task. F4SE sends `kGameDataReady` just before the game sets its own ready flag (Hooks_GameData.cpp:26-31) [V]; `kInputLoaded` is never sent by 0.7.9 (PluginAPI.h:116) [V].
- **The pause menu uses the same `Interface/MainMenu.swf`;** tell them apart with `root.Menu_mc.PauseMode` [X: F4SEMenuFramework PauseMenuButton.cpp:21-25,104-111].
- **Status text:** `StartMenuBase::confirmText` (@0x118) [X: CMP flow.cpp:80-89]. **Dialogs:** `MessageMenuManager::Create(header, body, callback, WARNING_TYPES::kMenus, "OK", …)` works at the title menu [X: CMP flow.cpp:100-112]. HUD messages need HUDMenu, which is not open at the main menu [I].
- **Buttons (MCM-style Scaleform injection):** `F4SE::GetScaleformInterface()->Register(name, callback)` runs for every menu movie (Hooks_Scaleform.cpp:446-528) [V]; pick the one whose `root.loaderInfo.url == "Interface/MainMenu.swf"`. MCM loads its own SWF into `root.Menu_mc` with a `flash.display.Loader` (reg2k/f4mcm ScaleformMCM.cpp:940-962) [X]. The menu rows are `{text, index}` objects in `root.Menu_mc.MainPanel_mc.List_mc.entryList`: `splice` rows in or out, call `InvalidateData()`, handle `"BSScrollingList::itemPress"` and read `selectedEntry.index`; native callbacks via `CreateFunction`. CMP adds JOIN and HOST this way (CommonwealthMP_Menu.as:49-66,119-190) [X]. The vanilla row indices must be dumped in game [G].
- **New game from code:** `mm->queueStartNewGame = true; mainMenuExitCondition = kNewGame` [X: CMP chargen.cpp:9-19], or `StartNewGame(MainMenu*)` (2249317) [X: po3].
- **Skipping the intro and splash** (Fallout4Custom.ini `[General]`, names checked against the 1.11.191 executable by FW, fo4_ini.py:61-77) [X]: `sIntroSequence=` (empty), `bSkipSplash=1`, `bShowCompanionAppMain=0`, `uCompanionAppWarnings=0`; also `uMainMenuDelayBeforeAllowSkip=0`, `fChancesToPlayAlternateIntro=0` (github.com/mczolton/Fallout4-Preferences) [web]. MainMenu has `userEngaged`, `mainBinkShown` and `allowSkip` fields (CL MainMenu.h:37-39) [V] whose effect is unknown; po3 loads without a key press [I].

## 6. Saves during a multiplayer session

- **INI keys** (names verified by FW, fo4_ini.py:50-89, lifecycle.cpp:275-285) [X]: `[SaveGame] bDisableAutoSave=1, fAutosaveEveryXMins=999999, bAllowScriptedAutosave=0, bAllowScriptedForceSave=0`; `[General] bSaveGameOnQuitToMainMenu=0`; `[Workshop] bWorkshopAutoSaveOnExit=0`. `bSaveOnPause/Travel/Wait/Rest` live in Fallout4Prefs.ini under `[MAIN]` [web: mczolton Fallout4Prefs.ini:80-87]; FW found they are not read from Fallout4Custom.ini and sets them in memory [X: FW launcher/fo4_ini.py:19-23]. No evidence that `iAutoSaveCount` exists. Force them in memory after `kGameDataReady` with `RE::GetINISetting("bDisableAutoSave:SaveGame")->SetBinary(true)` (CL Setting.h:401-412) [V]; list `INIPrefSettingCollection` in game for the prefs keys [G].
- **Save request flags** (FW offsets.h:3094; CL BGSSaveLoadManager.h:31-50): 0x1 autosave, 0x2 manual save, 0x4 death reload, 0x8 quicksave, 0x10 quickload, 0x40 menu load, 0x4000 and 0x10000 exits. Door and fast-travel autosaves go through `SaveGame` (2228036) [X: BO4].
- **Without detours:**
  - disable `MenuControls::quickSaveLoadHandler` (@0x48; clear its `inputEventHandlingEnabled` @0x08 or `UnregisterHandler`) for F5/F9 (CL MenuControls.h:55-74) [V; effect G];
  - remove the pause menu's SAVE and LOAD rows;
  - `Game.SetInChargen(true, true, false)` blocks saving and waiting from Papyrus (f4se Game.psc:312) [V]. SkyMP calls it after every load (skymp5-client enforceLimitationsService.ts:11-17) [X]; whether it also stops autosaves is [G].
  - `BGSSaveLoadManager::IsSavingAllowed` (2228045, Luca IDs.hpp:593) is the check a detour could force to false [X].
- **Full coverage (FW, tested live):** detour `QueueSaveLoadTask` to drop save flags; make `SaveGame` (2228036) return false (the engine shows "You cannot save right now"); refuse unwanted loads inside the load job by returning 0 and send "PauseMenuBackOutFromLoadGame" (lifecycle.cpp:343-404,447-477,513-632) [X]. CommonLibF4's `THook` rewrites existing CALL/JMP instructions only (THook.h:80-107) [V]: a function-entry detour needs an xbyak trampoline.
- **Death:** the game reloads `mostRecentSaveGame` (@0x60) through flag 0x4 after `fPlayerDeathReloadTime` [X: FW offsets.h:3090,3095]. Once the generated save is gone, that reload fails, so it must be intercepted (F12 respawns server-side).
- **Deleting the generated save:** after `kPostLoadGame` succeeds, `DeleteSaveFile(name, -1, bool)` (2228033) — its signature matches F4SE's DeleteSaveGame hook, which also removes the co-save (Hooks_SaveLoad.cpp:56-65, Serialization.cpp:521-534) [V; same function: I]. Delete leftover `FalloutMP_*.fos` at startup, or Continue offers them.
- **Save folder:** `GetSaveDirectoryPath` (2228028) includes `sLocalSavePath`; by hand: Documents + `My Games\` + `F4SE::GetSaveFolderName()` + `sLocalSavePath` (default `Saves\`) (PluginAPI.h:50-52, Serialization.cpp:80-98) [V]. Seen in practice: OneDrive-redirected Documents, MO2's `__MO_Saves\`, custom subfolders [V: crash logs]. Game Pass saves are WGS containers, not `.fos` files [V] (and F4SE does not run there).
- **Steam Cloud** syncs saves on exit, and deleted saves can come back (steamcommunity.com/app/377160/discussions/0/3166519278494182026) [web]; deleting the generated save before exit keeps it out of the cloud [I].
- **File names:** vanilla `<Save|Autosave|Quicksave|Exitsave><N>_<playerId 8 hex>[S|M]_<hex(name)>_<location>_<DDHHMM>_<YYYYMMDDhhmmss>_<level>_2.fos` (`S` survival [V: Steam]; `M` modded [I]); parsed by `SavefileMetadata::FillDataFromFileName` (2228156). Other names load too, by name without path or extension; FW boots `fwck_<id>_<date>_<time>` saves on 1.11.191 (engine_calls.cpp:2612-2690) [M]. Console-named saves don't appear in the reload list [V: Steam]. The game writes `<name>.fos.tmp` and renames [V].

## 7. Other engine facts for the entrance

- **Character creation after load:** `Game.ShowRaceMenu(None, 1)` (mode 1 = remake without sex change; mode 0 = start-of-game with the spouse logic) (papyrus-api-map.md:2404); CMP uses `showlooksmenu player` then `ShowSPECIALMenu` (chargen.cpp:31-41) [X]. Body and sex changes this way [G].
- **Input layers:** after the load, log `BSInputEnableManager` layers and debug names (@0x130, @0x160); unlock with `ForceUserEventEnabled` or the console `EnablePlayerControls` (CL BSInputEnableManager.h:41-99; CMP chargen.cpp:26). Make the template after MQ102 is done.
- **Pip-Boy:** ARMO `0x21B3B` must be equipped (CleanWorld already does).
- **Difficulty** is per save: `PlayerCharacter::GetDifficultyLevel`; Survival is 5, "true" Survival 6 (CL DifficultyLevel.h, PlayerCharacter.h:179-183) [V]. Templates are made non-Survival.
- **No-save alternative** (CMP): new game, then `StopQuest MQ101/MQ102`, `coc SanctuaryExt` (chargen.cpp:9-29) [X] — the pre-war intro still loads first. This is what FalloutMP does today with the clean world.

## 8. Existing tools and libraries

| Tool | Reads / writes | Last update | NG/AE | License | Use for FalloutMP |
|---|---|---|---|---|---|
| ReSaver / FallrimTools (Java) | full read/write, Papyrus editing, change-form deletion | GitHub 2023-10-01; Nexus 6.0.636 2023-09-07 | no official update; AE round-trips [M:MCP] | Apache-2.0 | **CI oracle**: `ESS.readESS(Path, ModelBuilder)` / `writeESS` (ESS.java:73,123) with strict size checks; the CLI has GUI flags only, so wrap it in a headless JDK 21 shim (MCP has a ~90-line one) |
| xEdit save mode / xDump | read only | 2026-09 | no version branches | MPL-2.0 | layout reference; xDump `tsSaves` mode, Windows only |
| FO4_Wrld `savepos.py` | patches Player Location, player + power-armor move, copies sky/audio | 2026-10 | 1.11.191, live-tested | AGPL-3.0 | best behavioural reference; reimplement, don't copy |
| Force67/recreation (C++) | read | 2026 | OG measured | none | measurements only |
| fo4-mcp (Python) | header read; writer that drops change forms; ReSaver shim | 2026-06 | AE | MIT | shim recipe |
| fo4-save-inspector | reads the player | 2026 | 1.10.984 / 1.11.221 | MIT | fixtures |
| Wrye Bash | header + master rename | 2026-04 | yes | GPL-3 | header check |
| FaceTransfer | reads/writes the player NPC_ face | 2015 | no | GPL-3 | face layout only |
| powerof3 StartOnSaveF4 | loads a named save at startup | 2025+ | AE | MIT | **reference for the load call sequence** |
| CommonwealthMP | main-menu JOIN/HOST injection, title-menu dialogs | 2026 | AE | see repo | **reference for main-menu UX** |

## 9. How other multiplayer projects enter the world

### 9.1 SkyMP (SK, the model FalloutMP follows)
1. **Main menu.** An offline `profileId` skips the login UI (`authService.ts:128-136`); otherwise a login form appears. Server info is fetched with callbacks, because "promises don't work in the main menu" (`settingsService.ts:60-139`).
2. **Login.** `connectionAccepted` sends the login. The server emits `spawnAllowed`; `spawn.ts:17-37` enables an existing actor, or creates one at a random start point with `setRaceMenuOpen`. Its TODO at :19 reads "Show race menu if character is not created after relogging".
3. **The server binds at once.** `SetUserActor` sends `CreateActor isMe` and streams the actor to neighbours before the client has loaded anything (`PartOne.cpp:192-240`).
4. **Branch.** `once('update')` firing first means a game is running (the reconnect path). Two `tick`s without an `update` mean the main menu (`remoteServer.ts:485-618`).
5. **Main-menu path.** The client calls `loadGame(pos, rot, worldOrCell, npc, loadOrder, time)` with its own mod list, a GameHour from UTC and its appearance. On an exception it retries without the appearance ("Hotfix non-vanilla headparts bug", `loadGameService.ts:10-20`).
6. **The writer** (`LoadGameApi.cpp:154-199` → `LoadGame.cpp:91-123`) works on the embedded `template.ess`, a 4.6 MB Skyrim LE save (header version 9). Its level-1 character still has the default name "Prisoner" (Пленник) and stands in Solstheim, so it was probably made with `coc` from the main menu, without character creation [I]. Upstream `2849e67` (2026-10-05) still ships it and this whole flow. It:
   - replaces the plugin list with the client's, without remapping (:144-158, :231-237);
   - sets GameHour `0x38` (weather is left alone);
   - replaces the player NPC_ change form (name, race, sex, skin, head parts, presets);
   - patches Player Location and the player's `MOVE` data (refId at 0, pos at 3, rot at 15);
   - writes `TESMODPLATFORM-<GUID>.ess` and loads it.

   Its bugs: `CreateRefId` copies `countWas` bytes instead of entries (`SFStructure.cpp:11-19`); `nextObjectId` is hard-coded `0xFF0014FE`; the grid uses truncating division (`LoadGame.cpp:303-317`); RefIDs are only of kinds 1 and 2, so plugin forms can't be written.
7. **After `TESLoadGameEvent`.** It waits 50 Papyrus updates (~5 s), then deletes every `TESMODPLATFORM-*` file (`LoadGame.cpp:16-66`). Then it:
   - applies inventory twice ("requires two calls");
   - blocks saving with `setInChargen(true, true, false)` after every load;
   - deletes one random actor per update within 8192 u;
   - unequips the template's iron helmet and opens the race menu 0.3 s later (`remoteServer.ts:93-99, 845-860`).
8. **Checks and errors.**
   - The load-order check runs only once in the world, and only warns (`loadOrderVerificationService.ts:17-76`).
   - A load the platform didn't cause → disconnect into "single-player mode".
   - No login within 15 s → "technical difficulties", and the saved login is wiped.
   - An unreachable server shows "connecting…" forever [I].

### 9.2 Other projects
| Project | Entry model | Notable details |
|---|---|---|
| FW (FO4, 2 players) | a server-named checkpoint, or a genesis save the server moved; auto-loaded from the main menu | LoadGame in the MainMenu registrar hook → black screen, so a worker waits 4 s and loads on the main thread (`main_menu_hook.cpp:47-58,69`). The client waits up to 20 s for the file, checks its sha256 and reports which save it used. `savepos.py` re-parses and diffs every untouched byte. New characters are staged 10,000 u up, with their position withheld (CHANGELOG 1456-1486). Door sync assumes every client loaded the same base save (`protocol.py:1966-1968`). MQ102 raiders still fight. |
| CO 1.1.0 (FO4) | a per-server local save, created once in a custom onboarding cell (COVault109) with LooksMenu and SPECIAL | Browser with Cancel/Retry; fallback and timeout when character creation doesn't open; the join finishes only in a "stable gameplay area"; the first join stays pending until the MP save exists; join generations are fenced. |
| TE (Fallout/Skyrim Together) | own save; connect from the overlay | Structured refusals: wrong_version, mods_mismatch with the list, client_mods_disallowed, wrong_password, server_full (`TransportService.cpp:196-270`); `uGridsToLoad` ≠ 5 refused; the FO4 build patches out "press any button" (`Games/Fallout4/Interface/UI.cpp:303-307`). Skyrim Together: "You MUST finish the first Helgen mission before attempting to connect". |
| F4MP (cokwa; Jous99) | own save; F1 or `]` to connect | No intro handling or save blocking; "backup your game"; Steam updates break F4SE (a downgrade guide). |
| VMP (FO3) | the launcher checks plugin CRCs, writes the plugin list, starts the game, then `coc` into the server cell | "Ask the server owner to send you the file or try to use 'Get mods'"; `bSaveOn*` off; quickload, Esc and console disabled. |
| NVMP (New Vegas) | separate local MP saves through New/Continue, with the vanilla Doc Mitchell intro | "New/Continue does nothing"; crashes after Continue (players mail their `.fos` to an admin); "invalid mod revisions" (plugins.txt mismatch); doors missing because a bug was baked into saves. |
| TW (SkyMP fork) | generated saves like SkyMP | PR #36: the template's DLC order differed from the clients' (§3); light plugins couldn't be named, so characters hung "loading game in world/cell". PR #14: neighbours saw a bodyless "somebody", so `CreateActor` now waits for a stored face. PR #12/#13: creation waits for location, inventory and outfit. PR #16: item adds failed while the game was paused. |
| AS (alternate starts, FO4) | new game, intro skipped | Skip: MQ101, MQ101KelloggSequence, MQ101PlayerComments, MQ101Radio, MQ101SanctuaryHills, MQ101TVStation and MQ101Vault111 must stay stopped. SKK: WorkshopParent needs 30–120 s to register workshops; MQ101 is "super fragile" (UI pop-ups freeze it); Creations firing at the vault exit bug Codsworth and Preston. Start Me Up Redux waits 20 s before MQ102 stage 10, and stage 10→15 turns on the Diamond City and Classical radio and the BoS signal. |

### 9.3 Lessons, ranked
1. **One load, with the destination in the save.** FW measured 116–339 s of loading screen for a fast travel after a load, and a 50 s freeze on a far direct move (CHANGELOG 379-383, 315-317).
2. **Never rewrite the plugin list without remapping.** Keep the template base-game-only and refuse templates naming plugins the client lacks (§3, TW #36).
3. **A minimal, self-verifying writer** on FW's model. Avoid SkyMP's three bugs, and don't write FO4 face data.
4. **A deliberate template per runtime.** Base game only, after the vault exit:
   - the MQ101 family stopped;
   - MQ102 stage 15 done;
   - WorkshopParent finished;
   - no created refs, frames or TEMPORARY flags;
   - an empty inventory apart from the Pip-Boy.

   Every client loads the identical file.
5. **Two-phase join.** Bind after the client reports loaded and stable; measure stability rather than waiting a fixed time.
6. **Own the save lifecycle.** `SetInChargen` after every load; FW's hooks and INI keys; intercept the death reload (the reloaded save is gone).
7. **Save hygiene.** A unique prefix; delete after the load; clean up at startup (Steam Cloud brings files back).
8. **Main-menu automation on the main thread, once the menu is idle.** A re-entry guard; Shift to skip (reg2k).
9. **Check the setup before generating anything.** Error messages should name the file and the fix (TE, VMP).
10. **Character creation as a state machine.** Pending and confirmed states, with timeouts and a fallback (FW, CO); reopen it on relog if it never finished (the SkyMP TODO).
11. **Visible status with Cancel/Retry.** Never wipe credentials on a timeout. Drive the flow from F4SE lifecycle messages, not from the first `update`: SkyMP's trick broke after quit-to-menu (skymp issue 892).
12. **Reconnect in game without a reload.** Resume tokens, backoff, fenced join generations.

## 10. In-game checks this research leaves open

1. Header `version`/`formVersion` written by 1.11.240 (expect 15/69).
2. `REL::ID` addresses for 2228039, 2228040, 2228036, 2228080, 2228951, 2698031 (expected values in §4.2), logged with their first bytes.
3. `LoadGame` from an F4SE task at the main menu: no black screen, returns true, how long it blocks; which thread `kPreLoadGame`/`kPostLoadGame` arrive on (compare `Main::threadID` @0x40).
4. What `checkForMods` and `ignoreMissingContent` actually do.
5. Order of `kPostLoadGame`, `TESLoadGameEvent`, LoadingMenu and FaderMenu closing.
6. The generated save can be deleted right after the load and Continue doesn't offer it.
7. F5/F9 and every autosave are really off.
8. The vanilla MainMenu rows `(text, index)`.
9. A 0×0 or 1×1 screenshot is accepted.
10. GameHour/GameDaysPassed form ids (by EDID through libespm).
11. Interior targets from an outdoor template (sky/audio blocks), and the reverse.
12. Input layers and Pip-Boy state after loading the template.
