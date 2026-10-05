# SkyMP → Fallout 4: In-Depth Research & Porting Blueprint

> Scope: how SkyMP (this repo, forked at upstream `f926944`) works end to end, and what it would take to turn it into a Fallout 4 multiplayer framework.
> Method: read the source for every subsystem, plus web research on the 2025–2026 Fallout 4 modding ecosystem.
> Marking: **[code]** = checked in this repo. **[web]** = taken from external sources (URLs at the end); re-check these before relying on them. **[inference]** = my own analysis.

---

## 0. TL;DR

1. **SkyMP is three products stacked together.**
   - **SkyrimPlatform (SP):** an SKSE plugin that embeds Node.js and Chromium (CEF) in Skyrim. It lets TypeScript call *any* Papyrus function.
   - **skymp5-client:** a TypeScript plugin running on SP. It syncs the player and draws remote actors.
   - **skymp5-server:** a Node.js process with a C++ N-API core. It loads `Skyrim.esm` itself, runs Papyrus server-side, and keeps the world state.
2. **About 40% of the code is game-agnostic and can be reused almost unchanged.** That covers networking (SLikeNet/RakNet UDP), the message/serialization layer, the grid/visibility system, the property/gamemode API, persistence (file/MongoDB), the client's DI/event plumbing, the Node/CEF embedding, the D3D11 overlay, and the host/owner model for NPCs.
3. **The rest is tightly bound to Skyrim and must be rewritten or made game-aware.**
   - The whole SKSE/CommonLibSSE layer and every Address Library ID.
   - Record layouts in `libespm`.
   - The Papyrus VM, which has no Fallout 4 PEX format, structs, `Var`, or the 11 new opcodes.
   - The `.ess` save writer used to "spawn" into the world.
   - Appearance, which uses Skyrim tints and headparts.
   - Actor values (magicka/stamina instead of AVIF/SPECIAL/AP/Rads).
   - Behaviour-graph event and variable names, menu names, the damage formula, and dozens of hardcoded `Skyrim.esm` form IDs.
4. **Fallout 4 adds systems with no Skyrim counterpart:** power armor, OMOD weapon/armor instances, workshops/settlements, VATS, the Pip-Boy, first-person-heavy play, and guns/ammo/projectiles.
5. **Recommended strategy:**
   - Make the shared libraries and server **game-pluggable** (a `GameProfile` abstraction), so upstream SkyMP fixes can still be merged.
   - **Fork** the client side into a new `fallout4-platform` (built on F4SE + CommonLibF4) and a new `falloutmp-client`.
   - Target **one runtime first**. The recommended one is the current Anniversary Edition 1.11.x line via `libxse/commonlibf4` **[web]**.
6. **Realistic first milestone ("walk around together"):**
   - Two players see each other moving and animating in the Commonwealth, with persistent position and inventory.
   - Prerequisites: an F4SE platform with JS + Papyrus reflection, FO4 parsing in libespm (NPC_/RACE/WEAP/ARMO/REFR/CELL/WRLD/ESL), movement/animation sync, and simplified appearance.
   - Estimate: several person-months for an experienced reverse engineer.
   - Full feature parity with SkyMP plus FO4-specific systems takes much longer.

---

## 1. Repository map

| Component | Size (LoC) | Language | Role | License |
|---|---|---|---|---|
| `skyrim-platform/` | ~28k | C++ / JS | SKSE plugin: Node.js + CEF + Papyrus reflection + hooks | GPLv3 |
| `skymp5-server/cpp` | ~32k | C++ | N-API addon: networking, world simulation, Papyrus host | AGPLv3 |
| `skymp5-server/ts` | ~1.6k | TS | Server bootstrap, login, master server, gamemode loader | AGPLv3 |
| `skymp5-client/` | ~11k | TS | Game client logic (runs inside SP) | GPLv3 |
| `skymp5-front/` | ~4.8k | React | In-game browser UI (chat, login, widgets) | GPLv3 |
| `skymp5-functions-lib/` | small | TS | Gamemode glue (pulls a private gamemode repo) | MIT |
| `libespm/` | ~6k | C++ | ESM/ESP parser | MIT |
| `papyrus-vm/` | ~3.9k | C++ | Papyrus bytecode interpreter | MIT |
| `savefile/` | ~5.7k | C++ | Skyrim `.ess` reader/writer (client spawn trick) | GPLv3 |
| `serialization/` | ~1k | C++ | Binary/JSON archives for net messages | — |
| `viet/` | ~1.3k | C++ | Promise, buffers, task queues, async save storage | MIT |
| `unit/` | ~8.5k | C++ | Catch2 tests (~255 cases) | — |

Build: CMake + vcpkg (manifest mode, git submodule). The build dir must be `<repo>/build`. The client parts are MSVC-only; the server, libraries and tests also build on Linux (clang, Docker) **[code]**.

---

## 2. How SkyMP works

### 2.1 Big picture

```
                         Skyrim SE/AE process (Windows)
┌──────────────────────────────────────────────────────────────────────────────┐
│ skse64_loader → SkyrimPlatform.dll (SKSE entry) → SkyrimPlatformImpl.dll     │
│   ├─ CommonLibSSE-NG + Address Library (REL::Relocate(SE_ID, AE_ID))         │
│   ├─ Hooks: SKSE trampoline, VM vtable (BindNativeMethod), Frida-gum         │
│   ├─ Embedded Node.js (libnode.dll, N-API) ── runs Data/Platform/Plugins/*.js│
│   │      └─ skymp5-client.js  (TypeScript client)                            │
│   │            └─ sp.mpClientPlugin → MpClientPlugin.dll (SLikeNet UDP) ─────┼──┐
│   ├─ "SP3": runtime reflection of every bound Papyrus native → JS classes    │  │
│   ├─ TESModPlatform.pex natives (CreateNpc, tints, AddItemEx, MoveRefr…)     │  │
│   └─ Tilted CEF overlay (DXGI Present hook) ── file:///Data/Platform/UI      │  │
│          └─ skymp5-front (React)                                             │  │
└──────────────────────────────────────────────────────────────────────────────┘  │
                                                                                  │ UDP :7777
┌──────────────────────────────────────────────────────────────────────────────┐  │
│ Node.js server (skymp5-server.js)                                            │◄─┘
│   ├─ scam_native.node (N-API, C++):                                          │
│   │    PartOne ─ WorldState ─ MpObjectReference/MpActor ─ GridImpl (4096u)   │
│   │    ActionListener (validates client messages)                            │
│   │    libespm (reads Skyrim.esm …)   papyrus-vm (runs .pex server-side)     │
│   │    Save storage: file / MongoDB / zip                                    │
│   ├─ gamemode.js  (global `mp` = ScampServer; hot-reloaded)                  │
│   ├─ login (master server gateway.skymp.net / offline profileId), Discord    │
│   └─ UI server on port+1 (serves skymp5-front)                               │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 SkyrimPlatform: the client runtime [code]

**Loading**
- `SkyrimPlatform.dll` (`skyrim-platform/src/platform_se/skyrim_platform_entry/main.cpp`) is a thin SKSE plugin. It exports `SKSEPlugin_Load` and loads `SkyrimPlatformImpl.dll` from `Data/Platform/Distribution/RuntimeDependencies`.
- The Impl DLL (`skyrim_platform/main.cpp`) runs this sequence: `SKSE::Init`, register the `TESModPlatform` Papyrus natives, register an SKSE messaging listener, `Hooks::Install()`, `Frida::InstallHooks()`.
- A `DllMain` initializer (`src/tilted/reverse/Entry.cpp`) hooks the game's `WinMain`, which is found via `Offsets::WinMain = REL::Relocate(35545, 36544)`. That is where the D3D11, DirectInput and WndProc hooks and the CEF overlay start.
- `game/Offsets.h` centralizes all Address Library IDs as SE/AE pairs.

**JavaScript runtime**
- SP uses **embedded Node.js through the embedder API** (`NodeInstance.cpp`, `JsEngine.cpp`). ChakraCore is no longer used, even though some docs still say it is.
- Plugins are compiled `.js` files in `Data/Platform/Plugins`. `DirectoryMonitor` reloads them when they change (hot reload). `sp.storage` survives reloads.

**Calling any Papyrus function from JS ("SP3")** (`Hooks.cpp`, `Sp3Api.cpp`, `assets/sp3.js`, `CallNative.cpp`)
1. SP patches `VirtualMachine` vtable slot `0x18` (`BindNativeMethod`). Every native function the game or any SKSE plugin binds is recorded in `g_boundNatives`.
2. `assets/sp3.js` reflects over that registry and builds JS classes (`Actor`, `Game`, `Debug`, `MiscUtil`, …) **at runtime**. Third-party SKSE natives therefore show up in JS automatically.
3. To call a native, `CallNativeSafe` takes over the top frame of a running Papyrus stack, fills in `self` and the arguments, and calls `IFunction::Call` on the VM thread. Latent functions (`Utility.Wait`, …) go through `DispatchStaticCall` and resolve a JS Promise.
4. **Typings are generated:** an in-game dump (9+O+L → `FunctionsDump.txt`) is merged with the hand-written `Definitions.txt` by `tools/ts_converter` to produce `skyrimPlatform.ts`.

**The tick trick** (`TickHandler.h`, `PapyrusTESModPlatform.cpp`)
- Each frame SP calls `vm->DispatchStaticCall("TESModPlatform","Add")`.
- When the VM runs that native, SP fires the JS **`update`** event. Inside it, JS may synchronously call Papyrus, because the VM thread is waiting on an asio queue.
- The separate **`tick`** event runs without game access.
- This mechanism can be reused on Fallout 4 almost as-is. FO4 also has `BSScript::IVirtualMachine::DispatchStaticCall` **[inference]**.

**Hooks and events**
- Frida hooks:
  - Papyrus `SendEvent`, so Papyrus events can be blocked; the client blocks vanilla scripts.
  - `NotifyAnimationGraph`, giving the `hooks.sendAnimationEvent` hook. JS can rewrite or cancel animation events.
  - Draw/sheathe, which forces weapon-drawn mode.
  - QueueNiNodeUpdate plus ApplyMasksToRenderTargets, which apply per-actor face tints.
  - RenderCursorMenu.
- About 2.5k lines of event sinks (`EventHandler.cpp`) cover `TESActivateEvent`, `TESHitEvent`, `TESEquipEvent`, `TESContainerChangedEvent`, menus, input, story events and more.

**Multiplayer-specific natives (`TESModPlatform.psc`)**
- `CreateNpc` memcpy-clones template NPC `0x10D13E` and gives it the DoNothing AI package `0x654E2`.
- Appearance setters: sex, race, skin/hair colour, headparts, tint masks.
- `AddItemEx` builds raw `ExtraDataList`s (enchantment, charge, poison, soul, worn).
- `MoveRefrToPosition`, `SetWeaponDrawnMode`, `ResetContainer`, `BlockPapyrusEvents`.

**Spawning into the world** (`LoadGame.cpp`)
- SP ships `assets/template.ess`, a Skyrim save.
- With the `savefile` library it patches player position, cell, time, weather, load order and the player's NPC change form, writes the save to `My Games/.../Saves`, and calls `BGSSaveLoadManager::Load`.

**Animation variables** (`MagicApi.cpp`, `Magic/*`)
- SP reads and writes Havok `hkbVariableValueSet` directly.
- It uses fixed index tables for Skyrim's master behaviour graph.

**UI** (`src/tilted/*`, from Tilted Phoques)
- The `IDXGISwapChain::Present` vtable hook draws a CEF 108 off-screen browser using DirectXTK `SpriteBatch`.
- A fake `IDirectInputDevice8` and WndProc capture input.
- Browser → game messages arrive as the `browserMessage` event. Game → browser calls go through `executeJavaScript`.

### 2.3 The client (skymp5-client) [code]

**Structure**
- About 49 services, each a `ClientListener`, share one typed `EventEmitter` bus. Wiring is in `src/services/spApiInteractor.ts` and `src/services/events/events.ts`.
- Networking: `sp.mpClientPlugin.send(json, reliable)` and `tick()`, backed by `MpClientPlugin.dll` (SLikeNet). JSON is converted to a compact binary format on the C++ side.

**Local player sync** (`sendInputsService.ts`)

| Data | When | Source |
|---|---|---|
| Movement | Unreliable, every **130 ms** | `getMovement()`: pos, rot, `worldOrCell`, `runMode`, Havok vars `Direction`, `SpeedSampled`, `bInJumpState`, `IsSneaking`, `IsBlocking`, weapon drawn, dead, health %, `lookAt` |
| Animation | On change | Hook on `sendAnimationEvent`; noise filtered out (moveStart/turn…); equip events collapsed into `SkympFakeEquip` |
| Appearance | When the `RaceSex Menu` closes | Race, sex, weight, hair/skin colour, headparts, 19 face morphs, 4 presets, tint masks |
| Equipment / inventory | On equip/unequip and container change | Inventory with Skyrim extra data (enchantment, charge, poison, soul) |
| Actor values | Throttled to 2 s | Health / magicka / stamina percentages |

**Remote actors** (`src/view/*`)
- `WorldModel` is built from `CreateActor`/`UpdateMovement`/... messages.
- `FormView` spawns each remote actor with `player.placeAtMe(base)`. `base` is either the ESM base or a freshly created `TESModPlatform.createNpc()` with the appearance applied.
- The spawned actor is made locally **immortal** and harmless: `attackDamageMult=0`, `startDeferredKill`, health 1e6.
- Movement is applied with `translateTo` (200 ms extrapolation) plus `keepOffsetFromActor(self)` to drive the locomotion animation.
- Animations are replayed with `Debug.sendAnimationEvent`. State changes go through `SprintStart`, `SneakStart`, `BlockStart` and `setWeaponDrawnMode`.
- `WorldCleanerService` continuously deletes vanilla actors, so only server-spawned actors (`0xFF…` IDs) exist.

**Server in charge**
- `blockActivation(true)` is set on every interactable object. The client sends `Activate` and the server decides.
- Containers are opened only after the server sends `OpenContainer`. Item moves are diffed and sent as `PutItem`/`TakeItem`.
- The server can run arbitrary client Papyrus calls through **`SpSnippet`**: `{class, function, self, args}` → reflective `sp[class][function]` → `FinishSpSnippet`.

**NPC hosting**
- The client volunteers with `Host{remoteId}`. Once granted (`HostStart`), it simulates that NPC with local AI and reports its movement, animations and hits as if it were a player.

**Gamemode client code**
- The server sends signed JS strings (`updateOwner`/`updateNeighbor`/`eventSources`). The client runs them with `new Function('ctx', src)` against the full `skyrimPlatform` module.
- Chat, widgets and similar features are built this way.

### 2.4 The server (skymp5-server) [code]

**Process**
- `ts/index.ts` loads `server-settings.json` and creates `ScampServer` (C++, `cpp/addon/ScampServer.cpp`).
- The tick loop is `server.tick(); await setTimeout(1)`. There is no fixed simulation rate; everything is event-driven.

**Network**
- SLikeNet UDP on port 7777. The protocol version `"7_"` (`mp_common/Config.h`) doubles as the RakNet password prefix, so a mismatched client is refused.
- There are 33 message types (`cpp/messages/MsgType.h`). Each one has a single `Serialize(Archive&)` that drives both the binary BitStream format and JSON.

**World**
- `PartOne` holds `WorldState`, which holds `MpForm → MpObjectReference → MpActor`.
- Persistent state lives in `MpChangeFormREFR`.
- **Visibility:** `GridImpl` uses 4096-unit cells; each actor sees the surrounding 3×3 cells. ESM references are loaded **lazily** the first time a chunk is touched (`GetRecordsAtPos` → `AttachEspmRecord`).

**Authority model (semi-authoritative)**

| Area | What the server does |
|---|---|
| Movement | Trusts the client, but rejects world changes and jumps of 4096 units or more (teleports back) |
| Inventory, containers, looting, crafting, drops, death items | Fully server-side. Leveled lists are evaluated on the server |
| Equipment | Checked against inventory, learned spells and body-slot overlap |
| Hits | Checked (same cell, distance, weapon equipped). Damage is computed by the server (`TES5DamageFormula` → mult formulas). Melee reach check is commented out |
| Health/magicka/stamina regeneration | Capped (`CropRegeneration`) |
| Magic effects of spells | Not applied (TODO); only potions are |
| Appearance | Accepted only while the race menu is open |

**Data**
- `libespm` memory-maps the load order and merges it with 8-bit plugin-index mapping.
- `FormDesc` (`"3c:Skyrim.esm"`) makes save IDs independent of load order.

**Papyrus on the server**
- `papyrus-vm` runs `.pex` files that come from `data/scripts`, BSAs, or 133 embedded stub scripts.
- Natives are reimplemented in `server_guest_lib/script_classes/` (Actor, ObjectReference, Game, Form, Debug, Utility, …). Many of them forward to the client through SpSnippet.
- Vanilla scripts in exteriors are stripped except those with the `Sweet*` prefix.

**Gamemode API**
- The global `mp` *is* the ScampServer instance.
- Main calls: `mp.get/set` (built-in and custom properties), `makeProperty`, `makeEventSource`, `createActor`, `place`, `lookupEspmRecordById`, `callPapyrusFunction`, `registerPapyrusFunction`.
- Events: `onActivate`, `onDeath`, `onPutItem`, … Returning `false` blocks the default action.

**Persistence**
- Drivers: `file` (one JSON file per change form), `mongodb`, `zip`, `migration`.
- Writes are async through `viet::AsyncSaveStorage`.

**Login**
- Online: a session is checked against `gateway.skymp.net`. Offline: a `profileId`.
- Optional Discord guild and role checks.

---

## 3. Fallout 4 ecosystem (what you would build on)

### 3.1 Runtimes [web]

| Line | Version | F4SE | Notes |
|---|---|---|---|
| Old-gen | 1.10.163 | 0.6.23 | Long-time modding baseline |
| Next-Gen | 1.10.980 / **1.10.984** | 0.7.2 | Apr–May 2024. New compiler and heavy inlining; every DLL plugin broke. New BA2 v7/v8 |
| Anniversary | 1.11.137 → **1.11.240** | 0.7.4 → 0.7.9 | Nov 2025 onward. New Address Library ID set; plugins must be recompiled |

- F4SE supports Steam and GOG only; Game Pass and Epic are not supported.
- The player base is split across all three lines.

### 3.2 F4SE plugin contract [web]

- From 0.7.x, a plugin must export `F4SEPluginVersionData F4SEPlugin_Version` (data symbol) and `F4SEPlugin_Load`. `F4SEPlugin_Query` is no longer used.
- On 1.11.x the plugin must set the `AddressLibrary_1_11_137` independence flag, or F4SE disables it.
- Interfaces: Messaging, Scaleform, Papyrus, Serialization, Task (`AddTask`/`AddUITask`), Object, Trampoline. These map closely onto what SP uses from SKSE.

### 3.3 CommonLibF4 options [web]

| Fork | Runtimes | Build | Comment |
|---|---|---|---|
| `libxse/commonlibf4` (= shad0wshayd3 main) | AE up to 1.11.240 | xmake, C++23 | Most active. GPL-3.0 with modding exception |
| `alandtse/CommonLibF4` | OG + NG + VR (no AE IDs yet) | CMake / vcpkg | Same NG multi-runtime design SkyMP already uses for Skyrim |
| `LucaDotGit/CommonLibF4` | OG + NG + AE in one DLL | CMake | Small fork; check how mature it is |
| Ryan-rsm-McKenzie original | OG | — | Superseded |

- The Address Library for F4SE is Nexus mod 47327. It ships per-runtime `.bin` files, for example `Data/F4SE/Plugins/version-1-11-240-0.bin`.

### 3.4 Prior FO4 multiplayer art [web]

| Project | Approach | Lesson |
|---|---|---|
| **F4MP** (cokwa, archived 2020; Jous99 revival) | F4SE + librg / GameNetworkingSockets. Remote players shown as placeholder Protectrons | Got movement only. Stalled on animation |
| **Fallout Together** (Tilted Phoques) | TiltedEvolution codebase | Deprioritized in 2022. FO4 references removed from the main repo in Nov 2025 |
| **Commonwealth Online** (2026) | F4SE + Python relay + Iroh + PrismaUI. Syncs movement, appearance + morphs, apparel, ranged weapon state, time/weather, PvP | Proves that FO4 appearance/morph sync and HTML UI work. No NPCs, quests or settlements yet |
| **FO4_Wrld** (1.11.191) | No F4SE: dxgi proxy. Replicates **per-bone skeleton transforms** instead of animation events | Documents the key pitfalls: first person parks the third-person graph, power armor is OMOD + skeleton swap, leveled lists aren't deterministic across clients |

**No FO4 equivalent of SkyrimPlatform exists.** You would be building the first one.

---

## 4. Porting analysis by component

Effort scale: **S** = days, **M** = 1–3 weeks, **L** = 1–2 months, **XL** = more than 2 months for one experienced dev **[inference]**.

### 4.1 Fallout 4 platform ("FalloutPlatform") — XL, the critical path

| Part of SP | Reuse? | Work for FO4 |
|---|---|---|
| Entry DLL and loader (`SKSEPlugin_Load`, `SKSE::Init`, trampolines, task/messaging) | Rewrite | Export `F4SEPlugin_Version` + `F4SEPlugin_Load`. Use `F4SE::Init`, `GetTaskInterface`, `GetMessagingInterface`, `GetPapyrusInterface`. Launch with `f4se_loader.exe`, `Data/F4SE/Plugins` |
| Node.js embedding, N-API, JsEngine, hot reload, `sp.storage`, HttpClient, Win32 API, IPC | **Reuse** | Change hardcoded paths (`Data/Platform/...` is fine) and the `SkyrimSE.exe` process-kill name |
| Tilted CEF overlay, D3D11 Present hook, DInput / WndProc hooks, TextApi | **Mostly reuse** | FO4 is also DX11 + DirectInput8. Re-check `BSRenderManager`→swap chain (FO4: `RE::BSGraphics::RendererData`) and `MenuScreenData`. Replace the `CursorMenu` / Scaleform paths. The NirnLab backend is Skyrim-only, so drop it. PrismaUI (Ultralight) is an alternative backend **[web]** |
| `Offsets.h` (WinMain, SendEvent, NotifyAnimationGraph, DrawSheathe, QueueNiNodeUpdate, VPrint, event sources, PushActorAway, CreateReferenceAtLocation …) | Rewrite | Find FO4 Address Library IDs for each, or use CommonLibF4 members where they exist |
| SP3 reflection (`BindNativeMethod` vtable hook, `objectTypeMap`, stack hijack, `GetNativeFunctionAddr` layout) | Port | FO4's `BSScript::Internal::VirtualMachine` is close in shape but differs in details. Handle **structs** (`BSScript::Struct`), **`Var`**, namespaced type names (`Foo:Bar`), and FO4's `ScriptObject` root class. Re-derive the native-function layout offsets |
| Tick trick (`TESModPlatform.Add`) | Port | Recompile `TESModPlatform.psc` with the FO4 compiler (`Institute_Papyrus_Flags.flg`) |
| Codegen (`FunctionsDump`, `ts_converter`, `const_enum_extractor`) | Port | Requires an FO4 PEX reader (see 4.3). Produce `falloutPlatform.ts`. New const enums: FO4 biped slots, form types, menu names, AV names |
| Event sinks (~2.5k LoC) | Rewrite partly | FO4 has `TESActivateEvent`, `TESHitEvent`, `TESEquipEvent`, `TESContainerChangedEvent`, `TESDeathEvent`, `MenuOpenCloseEvent`, `TESFurnitureEvent` and others **[inference: confirm in CommonLibF4 `RE/Bethesda/Events.h`]**. Drop Skyrim story events (dragon souls, shouts) |
| `TESModPlatform` natives (CreateNpc, tints, headparts, AddItemEx, SetWeaponDrawnMode…) | Rewrite | FO4 TESNPC layout, FO4 `BGSCharacterTint`/morph data, `ExtraDataList` with `BGSObjectInstanceExtra` (OMODs). Choose a new FO4 template NPC and a DoNothing-style package |
| `MagicApi` / spells / Havok var tables | Replace | No spells in FO4. Rebuild the variable index tables for FO4's behaviour graphs, or read variables by name |
| `LoadGame` (+ `savefile` `.ess` writer) | Rewrite or avoid | FO4 saves are `.fos` (`FO4_SAVEGAME`, different change-form set). Option A: implement a `.fos` writer. **Option B (recommended first):** ship a minimal FO4 template save made once in game, load it, then `MoveTo` the player and apply state through Papyrus/natives |

**Game-version choice**
- **Start with AE 1.11.x only.** It is the active ecosystem (`libxse/commonlibf4`, the current F4SE, the current Address Library) and the runtime Commonwealth Online and FO4_Wrld target **[web]**.
- Add NG/OG later through `REL::RelocationID`-style dual IDs, the same way SP handles SE/AE today.
- `libxse/commonlibf4` builds with xmake, so wrap it in a vcpkg overlay port. A pre-build step in `overlay_ports/` can call xmake, the way `overlay_ports/commonlibsse-ng-flatrim` patches and builds CommonLibSSE.
- `overlay_ports/commonlibse` and `commonlibae` exist but are unused leftovers whose descriptions mistakenly say "F4SE".

### 4.2 libespm — L

The container format is identical: 24-byte record and group headers, GRUP types 0–10, zlib, `XXXX`. `Browser` will walk `Fallout4.esm` structurally as-is **[code + inference]**. The work is in record *semantics*:

| Area | Change |
|---|---|
| Game detection | Read HEDR version (FO4 = 1.0, VR = 0.95) and record form version (FO4 = 131; SSE = 44) to pick parsers. Neither is read today |
| **ESL / light plugins** | No support today. Add the TES4 `0x200` flag, `FE xxx yyy` IDs with a 12-bit light index, and replace the 256-entry `IdMapping`. FO4 Creation Club and Creations content relies heavily on ESLs, and AE ships about 150 CC items **[web]** |
| NPC_ | New ACBS layout. Stats via PRPS (AVIF + value). TPTA template actors with per-flag templates. Morph data (MSDK/MSDV, FMRI/FMRS), tint layers. Also fix the existing SNAM rank bug (rank read from offset 0) |
| RACE | New DATA layout. The hardcoded offsets (+32, +36…+100) are invalid |
| WEAP | Large DNAM (damage, ammo, speed, reach, AP cost…), OMOD attach points, ammo reference |
| ARMO | DATA = value/weight/health; rating in FNAM plus a DAMA resistance list. **Today the code throws on a missing DNAM, so every FO4 ARMO would fail** |
| AMMO, COBJ (FVPA components, no CNTO), LVLI/LVLN, MGEF (AVIF refs), CONT, OTFT, REFR (XTEL etc.) | Re-verify layouts |
| New records | **AVIF** (actor values), **OMOD**, **CMPO** (components), **INNR**, **SCOL**, **PKIN**, **TRNS**… as needed |
| VMAD | FO4 uses v6 and adds **Struct** and **Var** property types. Today `ReadPropertyValue` throws inside `noexcept GetScriptData`, so the **process terminates**. Fix this first |
| Archives | Server-side BSA reader (`bsa/tes4.hpp`) → BA2 (`BTDX`, GNRL) if scripts are read from archives. `manifestGen.ts` derives `.bsa` names; change it to `- Main.ba2` |
| Tests | `unit/main.cpp` loads the 5 Skyrim ESMs and the CRC table in `libespm/src/Utils.cpp`. Add a FO4 data set. The `~[espm]` auto-skip already lets non-data tests run without game files |

**Recommended design:** keep `Browser`/`Combiner` shared, and select record parsers through a `Game` enum (`espm::Game::Skyrim|Fallout4`) chosen by the Loader from HEDR. Template the typed records (`espm::fo4::WEAP`) so Skyrim code keeps compiling.

Upstream bugs found along the way (worth upstreaming) **[code]**:
- `NAVM::kType = "NVNM"` (`libespm/include/libespm/NAVM.h:14`) means navmeshes are never indexed.
- NPC_ SNAM rank is read from the formId offset.
- `RecordHeader::GetScriptData` is `noexcept` but can throw (`libespm/src/RecordHeader.cpp:43`).
- The PEX reader has no header validation and does unchecked opcode-table indexing.

### 4.3 papyrus-vm — L

Fallout 4 support is completely missing **[code]**:
- **Reader:** FO4 PEX is little-endian, v3.9, gameID 2 **[inference, consistent with Champollion and Caprica]**. Today's reader is big-endian only and never checks the header, so FO4 files would be misparsed silently.
  - Add endian/version detection.
  - Add the **struct table**, const flags, and extra debug sections (property groups, struct orders).
- **`VarValue`:** add `Var` (a boxed variant) and `Struct` (a reference type with named fields), plus struct arrays.
- **Opcodes 0x24–0x2E:** `IS`, `STRUCT_CREATE/GET/SET`, `ARRAY_FINDSTRUCT/RFINDSTRUCT`, `ARRAY_ADD/INSERT/REMOVELAST/REMOVE/CLEAR`. Extend `numArgumentsForOpcodes` (sized for 36 today) and `ExecuteOpCode`.
- **Names:** namespaced scripts (`Workshop:Foo`) map to `Workshop\Foo.pex`. Fix this in `CIMap` lookups and `DirectoryScriptStorage`.
- **Language features:** `CustomEvent`/`SendCustomEvent`/`RegisterForCustomEvent`, `RegisterForRemoteEvent`, and `ScriptObject` as the root type.
- **Natives:** the server-side native classes (`script_classes/`) need FO4 signatures. For example, `Actor.GetValue(ActorValue akAV)` takes an AVIF form instead of a string. Replace `standard_scripts/` (133 Skyrim stubs) with FO4 stubs.

**Strategy note:** running vanilla FO4 quest scripts server-side is far more ambitious than SkyMP's own scope (SkyMP strips most vanilla scripts). For an MVP, run **only your own scripts** on the server and block vanilla script events on the client, which SP already supports via `blockPapyrusEvents`.

### 4.4 Server (`skymp5-server`) — L to XL

Introduce a `GameProfile` (C++ interface plus settings key `"game": "fallout4"`) that owns the things below.

| Concern | Skyrim today | Fallout 4 |
|---|---|---|
| Default load order | Skyrim, Update, Dawnguard, HearthFires, Dragonborn (`ScampServer.cpp:315`) | Fallout4.esm, DLCRobot, DLCworkshop01, DLCCoast, DLCworkshop02, DLCworkshop03, DLCNukaWorld, (DLCUltraHighResolution), plus CC ESLs |
| Default world / FormDesc | `3c:Skyrim.esm` hardcoded (`FormDesc.cpp:50,100`) | Commonwealth is also `0x3C`, but in `Fallout4.esm`. Make the hardcoded file name come from the profile |
| Spawn point | Tamriel coordinates in `ts/settings.ts`, `MpChangeForms.h`, `MpActor.cpp`, `ScampServer.cpp` | e.g. near Sanctuary / Vault 111 exit |
| Actor values | `espm::ActorValue` enum; Health/Magicka/Stamina percentages; regen from RACE | AVIF form-based. Health, **ActionPoints**, **Rads** (reduce max HP), SPECIAL, carry weight. Change `ActorValues.h`, `ChangeValuesMessage`, `CropRegeneration`, `GetBaseActorValues` |
| Damage | `TES5DamageFormula` (armor rating × scaling) | FO4 has separate physical/energy/rad/poison damage with a nonlinear damage-vs-resistance curve, plus ammo, projectiles, headshots / limb damage, sneak and crit multipliers **[inference]** |
| Combat validation | Melee/bow/spell; `PlayerBowShot` removes ammo | **Guns:** ammo consumption per shot, fire rate from WEAP/OMOD, reload. Hitscan vs projectile. A `WeaponFire` message would replace `PlayerBowShot` |
| Equipment | Left/right/voice/instant spells; BOD2 slot checks | No spells. FO4 biped slots (armor under outfit layers). **OMOD instance data** must be part of item identity |
| Inventory `ExtraData` | enchantment, charge, soul, poison | OMOD list, legendary mod, instance naming, health/condition (survival), ammo in magazine |
| Crafting (`CraftService`) | COBJ CNTO + bench keyword | COBJ FVPA components with **component scrapping (CMPO)**. Chem/cooking/armor/weapon workbenches. Weapon/armor *modding* = attaching OMODs (new message) |
| Conditions (`condition_functions/`) | Skyrim CTDA indices | FO4 CTDA indices (many shared names, different IDs) |
| Appearance (`Appearance.h`) | Skyrim struct | New FO4 struct (§4.6). Applied only during character creation (`LooksMenu`) |
| Animation stamina (`AnimationSystem.cpp`) | Skyrim attack/jump events | Replace with AP costs (sprint, VATS), or drop |
| NPC filters | CrimeFactionsList `0x26953`, banned races | FO4 equivalents. Also filter **workshop NPCs, companions and synth/settler scripts** at first |
| Hardcoded IDs | `0x1F4` unarmed, `0xF` gold, temper keywords, `0x200` shield… | Check each in Fallout4.esm. `0xF` is Caps001 in FO4 (convenient); the player is still `0x14`/`0x7` |
| Protocol | `"7_"` | Bump to e.g. `"fo4-1_"` so Skyrim and FO4 clients can never cross-connect |
| SweetPie coupling | `SweetCantDrop`, `SweetPie.esp` checks, Sweet* script filter | Remove or make configurable |

**Reusable unchanged:** networking, `MessageSerializerFactory`, the grid (FO4 exterior cells are also 4096 units **[inference]**), `PartOne`/`WorldState` skeleton, hosting, the properties and gamemode API, save storages, login/master/Discord systems, metrics, packet history and bots.

### 4.5 Client (`skymp5-client` → `falloutmp-client`) — L

| Module | Reuse | FO4 work |
|---|---|---|
| DI/event bus, networking, `messages.ts`, `IdManager`, `WorldModel`, host attempts, auth/settings/timers, gamemode JS execution, `ServerJsVerification` | **Reuse** | Rename `skyrimPlatform` imports |
| `movementGet/Apply.ts` | Port | FO4 graph variables (Speed, Direction, `bInJumpState`?, sneaking…) must be **discovered by dumping FO4's behaviour graph**; don't assume Skyrim names. Check that `translateTo`/`keepOffsetFromActor` exist in FO4 Papyrus (both do on `ObjectReference`/`Actor` **[inference]**) |
| `animation.ts` + all hooks | Rewrite tables | Every event name is Skyrim's (`JumpStandingStart`, `attackPowerStart`, `bowAttackStart`, `IdleChair*`…). FO4 needs gun events (`weaponFire`, `reloadStart`, `weaponSwing`…), sighted/aim state (`IsSighted`?), throwing grenades, and furniture idles |
| **First person** | New | FO4 players are often in first person, and the third-person graph is then "parked" **[web: FO4_Wrld]**. Animation events from the local player arrive on the *first-person* graph. You need either a mapping from 1st-person to 3rd-person events, or to sync graph *variables* and state instead of events, or bone transforms as FO4_Wrld does. **This is the single biggest technical risk** |
| `appearance.ts` | Rewrite | New struct; character-creation menu `LooksMenu` instead of `RaceSex Menu` |
| `equipment.ts` / `inventory.ts` | Rewrite extras | OMOD lists. Remove spells, voice, staffs. Inventory diff logic (`getDiff`, `sumInventories`) is reusable |
| `magicSyncService`, `spell.ts`, bow service | Drop / replace | New `WeaponFireService` (shot origin, direction, weapon instance) |
| `hitService`, `deathService` | Port | FO4 hit event fields (limb, projectile), ragdoll/bleedout, essential-companion downed state |
| `formView.ts` | Port | New template NPC; remove Skyrim creature race list; use `"Head"` node for nicknames instead of `"NPC Head [Head]"` |
| Menus | Rewrite | `PipboyMenu`, `ContainerMenu`, `BarterMenu`, `LooksMenu`, `WorkshopMenu`, `ExamineMenu`, `PowerArmorModMenu`, `VATSMenu`, `TerminalMenu`, `PauseMenu`, `LoadingMenu`… |
| Disabling single-player systems | Port | `setInChargen`, fast travel, `fXPPerSkillRank` (FO4 has no skill XP; block XP/level via perks/AV instead), difficulty, time globals (FO4 GameHour etc. IDs differ), `WorldCleanerService` |
| `skymp5-front` | Reskin | React app and widget bridge are reusable. Replace the Skyrim frame components with a Pip-Boy/terminal theme |

### 4.6 Appearance in Fallout 4 — M

FO4 character data **[web + inference]**:
- Race, sex, and body weight as a 3-axis triangle (thin / muscular / large).
- Head parts (hair, beard, eyes, brows…), hair colour and skin tone.
- **Face morph sliders:** MSDK/MSDV key → value pairs.
- **Face region morphs:** position/rotation/scale per region, FMRI/FMRS.
- **Tint layers:** `BGSCharacterTint` entries with template index, colour and value.

Proposed wire struct:

```ts
interface Fo4Appearance {
  isFemale: boolean; raceId: number; name: string;
  bodyMorph: [thin: number, muscular: number, large: number];
  headPartIds: number[]; hairColorId: number; skinToneIndex: number;
  morphSliders: { key: number; value: number }[];          // MSDK/MSDV
  faceRegions: { region: number; values: number[] }[];      // FMRI/FMRS (pos/rot/scale)
  tints: { templateId: number; colorId: number; value: number }[];
  // optional: LooksMenu BodyGen/BodySlide morphs if that mod is present
}
```

To apply it to a cloned TESNPC you need native setters in the new `TESModPlatform`. F4SE's `GameCustomization.h` and CommonLibF4 document the relevant `TESNPC` members. After changing them, refresh the face with the FO4 equivalent of `QueueNiNodeUpdate`/`DoReset3D`. Commonwealth Online already syncs appearance and morphs, which proves it can be done **[web]**.

### 4.7 Systems unique to Fallout 4 (post-MVP)

| System | Suggested approach |
|---|---|
| **Power armor** | The frame is furniture you enter; pieces are armor items with OMODs; the skeleton and behaviour graph swap. Model it as `{frameRefId, pieces[slot] = {baseId, omods[], health}}` on the server. Sync enter/exit as activation of the frame (server-validated occupancy, like SkyMP's containers and furniture). The remote view needs `AddItem` + `AttachModToInventoryItem` and the PA race/skeleton **[web: FO4_Wrld notes]**. Fusion cores: AV/ammo drain on the server |
| **OMOD / weapon & armor modding** | Item identity = `baseId + sorted omod list (+ legendary)`. Add it to `Inventory::Entry`, `Equipment` and `CreateActor`. Modding at a workbench becomes a server-validated `ModItem{item, attach[], detach[]}` that consumes components |
| **VATS** | It slows time locally, which conflicts with shared time. MVP: disable it (block `VATSMenu`). Later: a local-only "VATS lite" where the client asks the server for hit chances and the server resolves the shots, without slowing time |
| **Settlements / workshop** | Vanilla `WorkshopParentScript` is fragile and quest-driven. Build a **server-side workshop system** instead: placed objects are server forms (SkyMP's `place()` + `MpObjectReference` already persist dynamic objects), with a budget, ownership (`profileId`) and power grid as server data. The client uses `WorkshopMenu` for placement and the server validates. A large but very valuable feature |
| **Pip-Boy** | Native Scaleform. Leave it mostly vanilla (inventory/map/stats read local game state, which the server keeps in sync). The CEF overlay handles chat/login/server UI. Radio = local |
| **Companions** | Hostable NPCs with an owner. Start without them; later "player-owned NPC" entries under the hosting model |
| **Radiation / survival** | Rads as a server AV that reduces max health. Rad zones = triggers (the server already has `geo` polygon triggers) |
| **Leveled lists** | The server already evaluates LVLI/LVLN centrally, which gives every client the same raider. That fixes the determinism bug FO4_Wrld hit **[code + web]** |
| **Terminals, locks, hacking** | Activation + server-validated result. The minigame stays local |
| **Quests** | As in SkyMP, out of scope early. Strip vanilla quest scripts; build gamemode quests in JS/Papyrus server-side |

---

## 5. Recommended architecture for the fork

```
falloutmp/
├─ libespm/            ← game-aware parsers (espm::Game::Skyrim | Fallout4), ESL support
├─ papyrus-vm/         ← dual-format Reader (TES5 BE / FO4 LE), structs, Var, 11 opcodes
├─ serialization/ viet/                ← unchanged
├─ skymp5-server/      ← GameProfile abstraction; FO4 profile: AVIF, damage, appearance, OMOD inventory
├─ fallout4-platform/  ← NEW: fork of skyrim-platform on F4SE + CommonLibF4 (Node, CEF, SP3 reflection)
├─ falloutmp-client/   ← NEW: fork of skymp5-client (reuse services/net/model; new sync/* and view/*)
├─ falloutmp-front/    ← reskinned skymp5-front
├─ skyrim-platform/ skymp5-client/ skymp5-front/  ← keep building (or disable) to ease upstream merges
└─ CMake: GAME=skyrim|fallout4, FALLOUT4_DIR (checks Fallout4.exe), feature fo4-ae → commonlibf4
```

**Why not fork everything blindly?**
- Upstream SkyMP is still active (LCTN record support landed in the latest commit).
- If the shared C++ libraries and server stay game-pluggable, fixes keep flowing in with `git merge upstream/main`.
- The client and platform are inherently game-specific, so separate directories are cleaner than `#ifdef`s everywhere.

**Build system changes**
- Root `CMakeLists.txt`: add `GAME`/`FALLOUT4_DIR`, check for `Fallout4.exe`, and copy dist into the FO4 folder.
- vcpkg: add feature `fallout4-ae` → `commonlibf4` overlay port.
- `client-deps`: F4SE Address Library `.bin`.
- `cmake/add_papyrus_library_ck.cmake`: use the FO4 compiler path and `Institute_Papyrus_Flags.flg`.
- `generate_server_settings.cmake`: FO4 load order.
- `dev_service`: `f4se_loader.exe` and `Fallout4.exe`.
- CI: an FO4 test-data source. **Do not** publicly redistribute `Fallout4.esm` the way upstream downloads Skyrim ESMs from a GitLab mirror. Use a private cache or self-hosted runner.

---

## 6. Phased roadmap

| Phase | Goal | Key deliverables | Est. |
|---|---|---|---|
| **0. Groundwork** | Decide and set up | Pick runtime (AE 1.11.x); CommonLibF4 overlay port builds; `GAME` CMake switch; FO4 data set for tests; protocol version bump | M |
| **1. Platform core** | JS runs in FO4 | F4SE entry; Node embedding; `update`/`tick` via a `TESModPlatform.Add` tick; `printConsole`; hot reload; CEF overlay drawing + input; `mpClientPlugin` loads | L |
| **2. Papyrus reflection** | `sp.Game.getPlayer().getPositionX()` works | BindNativeMethod hook; FO4 SP3 (structs/Var marshalling); function dump; FO4 PEX reader in papyrus-vm; generated `falloutPlatform.ts` | L |
| **3. Data layer** | Server loads Fallout4.esm | libespm game detection, ESL, FO4 NPC_/RACE/WEAP/ARMO/AMMO/CONT/LVLI/REFR/ACHR/CELL/WRLD/AVIF; VMAD crash fix; unit tests on FO4 data | L |
| **4. "See each other"** | 2 players, movement + basic animation | Spawn via template save + MoveTo; template NPC clone; movement/runMode/sneak/weapon-drawn sync; graph-variable discovery; first-person→third-person strategy; world cleaner | L |
| **5. Persistence & items** | Inventory, containers, looting, equip | OMOD-aware inventory; container open/take/put; equipment with FO4 slots; FO4 appearance + LooksMenu flow | L |
| **6. Combat** | PvP/PvE with guns | WeaponFire message; ammo; server damage formula (DR curve); hit validation; death/respawn; NPC hosting with combat AI | XL |
| **7. FO4 systems** | Power armor, workbench modding, crafting/scrapping | As in §4.7 | XL |
| **8. Settlements** | Server-side workshop | Placement, ownership, power, budget, persistence | XL |
| **9. Polish** | Pip-Boy integration, VATS-lite, companions, rads, terminals, quests via gamemode | | ongoing |

---

## 7. Risks and open questions

1. **Animation fidelity in first person.** This is the main unknown. Prototype it early in Phase 4: hook FO4 `NotifyAnimationGraph` and log which graph the player's events arrive on in first and third person. Decide between event replay (SkyMP style), variable sync, or bone sync (FO4_Wrld style).
2. **CommonLibF4 coverage and churn.** AE IDs only exist in the libxse line, which is xmake-based, C++23 and fast-moving. Pin a commit. Expect to add missing RE types yourself (VM internals, TESNPC face data, hkb variables).
3. **Runtime fragmentation.** Players are split across OG, NG and AE. Every new Bethesda patch breaks the Address Library until it is updated. Bake a runtime check into the platform with a clear error.
4. **SP3 on FO4's VM.** Stack-frame takeover relies on undocumented `BSScript` internals. If it can't be ported, fall back to F4SE Papyrus registration of a fixed native set plus `DispatchStaticCall` for vanilla functions.
5. **Spawning without a save writer.** Option B (template save + teleport) is simpler but may leave quest and world state from the template save. Make a save at the Vault 111 exit with the MQ suppressed and verify it.
6. **Licensing.**
   - Server: AGPLv3 (a public modified server must offer its source).
   - Platform and client: GPLv3.
   - libespm and papyrus-vm: MIT.
   - TiltedUI/TiltedCore/TiltedHooks/TiltedReverse are "© Tilted Phoques, All Rights Reserved" **[code: THIRD_PARTY_LICENSES]**. Keep the notices, or replace the overlay with an independent implementation if that is a concern.
   - CommonLibF4 (libxse) is GPL-3.0 with a modding exception, which is compatible.
   - `CLA.md` only applies if you contribute upstream.
7. **Legal and Bethesda.** Keep it free. Don't redistribute game assets or ESMs, including in CI. Require a legitimate copy and avoid implying endorsement. Bethesda has tolerated free Nexus-distributed multiplayer mods so far **[web]**.
8. **Platform coverage.** F4SE is Steam and GOG only **[web]**, so Game Pass and Epic users can't play.

---

## 8. Quick reference: where the Skyrim assumptions live

**Build and packaging**
- `CMakeLists.txt` (lines ~52, 59–65, 115–139, 247)
- `vcpkg.json`
- `overlay_ports/commonlibsse-ng-*`
- `cmake/download_skyrim_data.cmake`
- `cmake/scripts/generate_server_settings.cmake:10`
- `cmake/add_papyrus_library_ck.cmake` + `TESV_Papyrus_Flags.flg`
- `client-deps/{se,ae}`
- `skyrim-platform/tools/dev_service/{index,game}.js`

**Platform**
- `skyrim-platform/src/platform_se/skyrim_platform/game/Offsets.h` (all addresses)
- `Hooks.cpp` (VM vtable `0x18`, `skse64_1_6_1170.dll`)
- `FridaHooks.cpp`
- `CallNative.cpp` and `GetNativeFunctionAddr.cpp`
- `PapyrusTESModPlatform.cpp` (`0x10D13E`, `0x654E2`, ExtraDataList)
- `LoadGame.cpp` + `assets/template.ess`
- `EventHandler.*`
- `Magic/AnimationVariablesForMasterBehavior.h`
- `ConstEnumApi.cpp`
- `codegen/convert-files/Definitions.txt`

**Data libraries**
- `libespm/src/{NPC_,RACE,WEAP,ARMO,AMMO,MGEF,COBJ}.cpp`
- `libespm/include/libespm/{ActorValue,GMST,Combiner}.h`
- `libespm/src/Utils.cpp` (CRCs, VMAD)
- `papyrus-vm/src/papyrus-vm-lib/Reader.cpp`
- `OpcodesImplementation.h`
- `VarValue.h`
- `savefile/*`

**Server**
- `skymp5-server/cpp/addon/ScampServer.cpp:315` (load order)
- `server_guest_lib/FormDesc.cpp`
- `WorldState.cpp` (`AttachEspmRecord` filters, `0x26953`)
- `ActionListener.cpp` (hits, `0x1f4`)
- `TES5DamageFormula.cpp`
- `ActorValues.h`
- `CropRegeneration.cpp`
- `GetBaseActorValues.cpp`
- `AnimationSystem.cpp`
- `Appearance.h`
- `Equipment.h`
- `Inventory.h`
- `CraftService.cpp`
- `condition_functions/*`
- `script_classes/*`
- `standard_scripts/`
- `ts/settings.ts` (spawn)
- `ts/manifestGen.ts` (BSA names)

**Client**
- `skymp5-client/src/sync/{movementGet,movementApply,animation,appearance,equipment,inventory,actorvalues,spell}.ts`
- `src/view/formView.ts` (creature races, head node)
- `services/services/{deathService,hitService,magicSyncService,playerBowShotService,timeService,browserService,worldCleanerService,remoteServer}.ts`
- `src/index.ts` (INI tweaks)

---

## 9. Sources (web)

**Versions and F4SE**
- F4SE: https://f4se.silverlock.org/ and https://github.com/ianpatt/f4se (`f4se/PluginAPI.h`, `f4se_common/f4se_version.h`)
- FO4 patch history: https://fallout.fandom.com/wiki/Fallout_4_patches
- Anniversary Edition: https://fallout.fandom.com/wiki/Fallout_4:_Anniversary_Edition
- Next-Gen impact on mods: https://comicbook.com/gaming/news/what-fallout-4s-next-gen-update-means-for-your-mods/
- BA2 v7/v8 backport: https://github.com/Nukem9/fallout4-backported-archive2-support

**CommonLibF4 and Address Library**
- https://github.com/libxse/commonlibf4
- https://github.com/alandtse/CommonLibF4
- https://github.com/LucaDotGit/CommonLibF4
- https://github.com/Ryan-rsm-McKenzie/CommonLibF4
- Address Library for F4SE: https://www.nexusmods.com/fallout4/mods/47327

**Prior FO4 multiplayer projects**
- https://github.com/cokwa/F4MP-Archive
- https://github.com/Jous99/F4MP
- https://github.com/tiltedphoques/TiltedEvolution
- https://commonwealth-online.com/ and https://github.com/G-A-R-D-E-N/Commonwealth-Online
- https://github.com/ThePie88/FO4_Wrld

**UI frameworks**
- PrismaUI F4: https://github.com/PRISMA-USER-INTERFACE-FRAMEWORK/Fallout-4-Prisma-UI-Framework
- F4SE Menu Framework: https://github.com/DCCStudios/F4SEMenuFramework

**Papyrus and data formats**
- Papyrus: https://falloutck.uesp.net/wiki/Struct_Reference, https://falloutck.uesp.net/wiki/Custom_Papyrus_Events, https://falloutck.uesp.net/wiki/Differences_from_Skyrim_to_Fallout_4
- Champollion (PEX decompiler): https://github.com/Orvid/Champollion
- FO4 record docs: https://github.com/TES5Edit/fopdoc/blob/master/Fallout4/Records.md
- Object Mod: https://falloutck.uesp.net/wiki/Object_Mod
