# 07 — Dependencies & Companion Mods

FalloutMP does not have to build everything itself. Where a maintained Fallout 4 mod already solves a problem (crash logging, physics stability, body morphs, settings UI, extra script natives), FalloutMP can **require or recommend** it instead. This document is the policy and the current candidate list. Decision record: ADR-021 in [02-architecture.md](02-architecture.md).

Provenance: mod names and roles are from the modding ecosystem research; Nexus IDs and version compatibility are marked `[verify]` where not confirmed. Mod licenses and Nexus permissions must be checked before anything is redistributed (DOCS-010).

## 1. Policy

1. **Prefer an existing, maintained mod over new engine work** when it covers a need, is compatible with the target runtime (ADR-001: AE 1.11.x), and has a usable API (F4SE plugin interface, Papyrus natives, or a documented file format).
2. **Dependencies are user-installed**, not bundled, unless the mod's permissions explicitly allow redistribution. The client **manifest check** (F00-T09, PLAT-095) verifies presence and version at connect and gives a clear error.
3. **Any F4SE plugin that registers Papyrus natives becomes available to *client-side* JS** through runtime reflection, *provided ADR-006's reflection path is accepted* (PLAT-030 prototype; the hook must also capture natives bound by plugins that load before FalloutPlatform). Under ADR-006's fallback (a fixed native list), such natives must be listed explicitly. Server Papyrus and gamemode code never see client natives. Where it holds, this makes "include a native-extending mod" nearly free for client features.
4. **Core sync must not hard-depend on a closed-source mod.** Where a mod is used for a sync-critical path (e.g. appearance), keep a native fallback or a reduced mode.
5. **Server-defined load order.** ESM/ESP/ESL mods are allowed only if the server lists them (the manifest hash check is enforceable because the server computes it from the records the client must have). Client-side DLL mods are allowed by default unless the server denies them (`SRV-003` allow/deny list). Gameplay-affecting DLL mods (damage, spawns, AI) should be denied by default on PvP servers. **The DLL list is client-reported (PLAT-095), so it is a cooperation check for honest players, not anti-cheat**; a modified client can omit entries. Gameplay security rests on server-side validation (01 §1) and anomaly scoring (SRV-005).
6. **Compatibility is tracked** in a matrix (DOCS-005): mod, version, runtime, role, status (required / recommended / optional / incompatible), owner feature.

## 2. Required client dependencies

| Mod | Role | Notes |
|---|---|---|
| F4SE 0.7.x | Script extender; plugin loading | Must match the game runtime (ADR-001) |
| Address Library for F4SE Plugins | Engine addresses by ID | AE database for the pinned builds. Redistribution to be checked (BUILD-003) |

## 3. Strongly recommended (install prompts in the client; some features degrade without them)

| Mod | Role in FalloutMP | Owner | Why | Status |
|---|---|---|---|---|
| **Buffout 4 NG** | Crash logs for `G-manual`/`G-self` reports; engine memory and stability fixes | QA, F00 | Every in-game bug report needs a crash log; also fixes known engine crashes | Recommended `[verify AE build]` |
| **High FPS Physics Fix** | Decouples physics from frame rate; faster loads | F01, F09 | Players at different FPS must get the same physics (projectiles, ragdolls, movement speed). Reduces desync causes | Recommended `[verify]` |
| **LooksMenu** | Face/body editor API, BodyGen body morphs, appearance preset JSON | F03 | Character creation and live morph sync. Its preset JSON (head parts, morphs, tints, body) is a candidate wire/persistence format for `AppearanceFo4`, and its API can apply presets, replacing part of PLAT-082 | Recommended; native fallback kept (`F03-T12`) `[verify API/AE]` |
| **Mod Configuration Menu (MCM)** | In-game settings page: server address, keybinds, nameplates, debug toggles | CLI | Standard FO4 settings UI; avoids building a settings screen in the overlay | Recommended (`CLI-080`) `[verify AE]` |

## 4. Optional / evaluate

| Mod | Possible role | Owner | Evaluate in |
|---|---|---|---|
| **HUDFramework** | HUD widgets (nameplates, compass markers, status) via the native HUD instead of the Chromium overlay | F28, F31 | PLAT-060 alternative; F31-T0x |
| **PrismaUI (Ultralight)** | Alternative HTML UI backend if the CEF overlay conflicts with the renderer or input | ADR-005 fallback | PLAT-060 |
| **Garden of Eden Papyrus Script Extender** or similar native packs | Hundreds of extra Papyrus natives (forms, actor values, nodes, cells). Reflection exposes them to the client for free | PLAT, CLI | PLAT-033 `[verify license, AE]` |
| **Transfer Settlements** | Its settlement blueprint JSON is a proven schema for serializing placed objects; candidate for F22 import/export and backups | F22 | `F22-T26` |
| **Workshop Framework** | Hook points for workshop events; **not** used for simulation (ADR-012 keeps the server authoritative) | F22 | F22 design review |
| **Unofficial Fallout 4 Patch** | Bug fixes; a server may include it in its load order | server load order | DOCS-005 |
| **Place Everywhere** | Build-mode freedom; must remain compatible with server placement validation | F22 | compatibility test |
| **xSE PluginPreloader F4** | Loads plugins before engine init if `F4SEPlugin_Preload` is insufficient for the `BindNativeMethod` hook | PLAT-030 | only if needed |
| **Baka ScrapHeap** | Memory allocator fixes | stability | DOCS-005 |

## 5. Mods that need a compatibility policy (not dependencies)

- **UI replacers** (DEF_UI, FallUI): the overlay and menu-pause policy (F28) must not break them. Test early.
- **Scrapping/settlement mods** (Scrap Everything, Sim Settlements 2): they change workshop behaviour; server authority (F22) must either reject or model their actions. Default: deny on servers that enable settlements until tested.
- **Weapon/armor packs** (ESP/ESL): fine if in the server load order; the server needs the records to compute stats (SRV-022).
- **D3D11/DXGI wrappers** (ENB, ReShade, upscaler bridges, Steam/Discord/Nvidia overlays): they contend with the CEF overlay's `Present`/`Renderer::End` hook (PLAT-060). Very common in FO4 installs. Test early in PLAT-060 with ReShade and ENB installed; document the supported hook order; PrismaUI stays the fallback (ADR-005).
- **Mod Organizer 2 virtual file system**: `Data/Platform/Plugins` hot reload, `Data/Platform/Logs` and `sp.storage` live under a virtual Data directory with MO2. Test in PLAT-010/PLAT-005; DOCS-001 states "install the client into the real Data folder (manual or Vortex); MO2 works only with the documented profile setup".
- **Downgraders** (Simple Fallout 4 Downgrader, BackPorter): widespread; PLAT-003 detects NG/OG binaries and points to the runtime-update runbook (PLAT-006).
- **High FPS Physics Fix** patches the main loop and the loading flow (load accelerator) `[verify interaction]` with PLAT-050 template loading; **Buffout 4 NG** replaces the memory allocator `[verify]` that the Frida-gum/trampoline hooks coexist (one G-self check in PLAT-095).
- **Animation replacers**: may change behaviour graph variables and events; F02 resolves variables by name at runtime to tolerate this, but hosted-NPC animation parity can suffer. Document as "cosmetic, at your own risk".

## 6. Developer tools (not shipped)

xEdit (FO4Edit) for records, Champollion (PEX decompiler), Caprica (PEX compiler), Creation Kit, Bethesda Archive Extractor, NifSkope, x64dbg/IDA for reverse engineering, Address Library database files.

## 7. Tasks introduced by this policy

| Task | Summary |
|---|---|
| PLAT-095 | Dependency detection: enumerate loaded F4SE plugins and versions (`F4SEInterface::GetPluginInfo`), report in the self-test JSON and in the connect manifest |
| SRV-003 | Server settings `clientMods.required`, `clientMods.recommended`, `clientMods.denied` with versions; manifest validation at login; clear client error text |
| CLI-080 | MCM settings page (server address, profile, keybinds, nameplates, debug overlays) with a fallback to the settings file |
| F03-T12 | Evaluate LooksMenu preset JSON as the `AppearanceFo4` schema and LooksMenu's API for apply; keep the native path as fallback |
| F22-T26 | Evaluate the Transfer Settlements blueprint schema for settlement import/export and backups |
| DOCS-005 | Compatibility matrix and recommended-mods list for players and server admins |
