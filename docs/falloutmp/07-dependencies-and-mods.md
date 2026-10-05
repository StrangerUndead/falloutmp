# 07 — Dependencies & Companion Mods

FalloutMP does not have to build everything itself. Where a maintained Fallout 4 mod already solves a problem (crash logging, physics stability, body morphs, settings UI, extra script natives), FalloutMP can **require or recommend** it instead. This document is the policy and the current candidate list. Decision record: ADR-021 in [02-architecture.md](02-architecture.md).

Provenance: mod names and roles are from the modding ecosystem research; Nexus IDs and version compatibility are marked `[verify]` where not confirmed. Mod licenses and Nexus permissions must be checked before anything is redistributed (DOCS-010).

## 1. Policy

1. **Prefer an existing, maintained mod over new engine work** when it covers a need, is compatible with the target runtime (ADR-001: AE 1.11.x), and has a usable API (F4SE plugin interface, Papyrus natives, or a documented file format).
2. **Dependencies are user-installed**, not bundled, unless the mod's permissions explicitly allow redistribution. The client **manifest check** (F00-T09, PLAT-095) verifies presence and version at connect and gives a clear error.
3. **Any F4SE plugin that registers Papyrus natives is automatically available to FalloutMP scripts** through runtime reflection (ADR-006, PLAT-030/033). This makes "include a native-extending mod" nearly free, and it is the preferred route for one-off natives.
4. **Core sync must not hard-depend on a closed-source mod.** Where a mod is used for a sync-critical path (e.g. appearance), keep a native fallback or a reduced mode.
5. **Server-defined load order.** ESM/ESP/ESL mods are allowed only if the server lists them. Client-side DLL mods are allowed by default unless the server denies them (`SRV-003` allow/deny list). Gameplay-affecting DLL mods (damage, spawns, AI) should be denied by default on PvP servers.
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
