# PLAT — `fallout4-platform` (F4SE plugin: Node.js, Papyrus reflection, hooks, natives, UI)

Context: [reference/commonlib-port-map.md](../reference/commonlib-port-map.md) (symbol map §1, offsets §2, F4SE vs SKSE §3, engine facilities §4, per-file verdicts §5, build §6, blockers §7); [reference/fo4-papyrus-pex-vm.md](../reference/fo4-papyrus-pex-vm.md) §5 (VM internals: `BindNativeMethod` slot 0x1B, `StackFrame` 0x40 without inline args, `std::function` dispatch fillers); [reference/papyrus-api-map.md](../reference/papyrus-api-map.md) §2 (client calls needing fast paths or new natives); [reference/prior-art.md](../reference/prior-art.md) §5 (IDs, layouts, adopt/avoid); ADR-003…ADR-007.

**Approach:**
- Fork `skyrim-platform/` into `fallout4-platform/`.
- Keep `src/tilted`, `platform_lib`, Node/JsEngine and the event plumbing. These use no game symbols (commonlib map §5).
- Rewrite everything that touches `RE::`/`SKSE::`.
- Every engine address comes from Address Library IDs through CommonLibF4 `REL::ID`. Never use raw RVAs.
- Record every new ID in `fallout4-platform/src/.../game/Offsets.h` with its source.

## A. Skeleton & runtime
- [ ] **PLAT-001** Skeleton F4SE plugin: `F4SEPlugin_Version` (`AddressLibrary_1_11_137` flag, hand-written version data), `F4SEPlugin_Load`, `F4SE::Init`, logging to `Documents/My Games/Fallout4/F4SE/FalloutPlatform.log` (first line: version, sha, runtime, f4se) — M — Depends: BUILD-002 — Verify: W-ci, G-self — Files: fallout4-platform/src/platform_fo4/*
  - Accept: the plugin loads on the pinned AE runtime, logs its init line, and the game runs normally.
- [ ] **PLAT-002** Offsets/ID database for the target runtime:
  - port SP's `Offsets.h` entries using commonlib map §2;
  - mark each ID `verified` or `inferred`;
  - add an RTTI-based vtable finder for vtables whose IDs are still pre-AE (blocker R1).
  - L — Depends: PLAT-001 — Verify: G-self — Files: game/Offsets.h, game/VTableFinder.*
  - Accept: the self-test resolves every ID/vtable and logs `OK`/`FAIL` per symbol.
- [ ] **PLAT-003** Runtime allow-list and refusal UX: on an unsupported runtime or a missing Address Library, show a message box, log, and disable cleanly — S — Depends: PLAT-001 — Verify: G-manual
- [ ] **PLAT-004** Two-DLL layout: both DLLs in `Data/F4SE/Plugins` (ADR-003 constraint); libnode and CEF in `Data/Platform/Distribution/RuntimeDependencies` added to the DLL search path; start without the `WinMain` dependency (use the F4SE plugin-load hook path, commonlib map §2) — M — Depends: PLAT-001 — Verify: G-self
- [ ] **PLAT-005** Logging/diagnostics contract (dev-environment §9): `[INIT] hooks=… papyrus_natives=… node=… cef=…`; crash-safe flush; log rotation — S — Depends: PLAT-001 — Verify: G-self

## B. JavaScript runtime & tick
- [ ] **PLAT-010** Port `NodeInstance`/`JsEngine`/`SkyrimPlatform.cpp` → `FalloutPlatform.cpp`:
  - plugin folders `Data/Platform/Plugins;PluginsDev`, `DirectoryMonitor` hot reload;
  - `FalloutPlatform.ini` (rename of `SkyrimPlatform.ini` keys);
  - `process.dlopen` path updated to `Data/F4SE/Plugins/FalloutPlatformImpl.dll`.
  - M — Depends: PLAT-004 — Verify: G-self
  - Accept: a JS plugin prints to the console; editing it hot-reloads.
- [ ] **PLAT-011** Port the game-agnostic APIs: Encoding, HttpClient, Win32, FileInfo, Text (createText via overlay), storage proxy, settings, DevApi, IPC — S — Depends: PLAT-010 — Verify: G-self
- [ ] **PLAT-012** Console API:
  - `printConsole` via the FO4 `ConsoleLog`;
  - `findConsoleCommand` / console command override (FO4 script function table; commonlib map §1.14).
  - M — Depends: PLAT-010 — Verify: G-self
- [ ] **PLAT-020** Tick model: `TickHandler` via the F4SE task interface, plus `TESModPlatform.Add` dispatched with `DispatchStaticCall` (FO4 signature with `std::function` arg filler) → JS `update` (Papyrus-safe) and `tick` events, exactly like SP's semantics — M — Depends: PLAT-021, PLAT-030 — Verify: G-self
  - Accept: `update` fires every frame in game. `tick` fires in the main menu. Papyrus calls from `update` succeed.
- [ ] **PLAT-021** `TESModPlatform.psc` for FO4 (native declarations only, `Native` script, FO4 type names) compiled with BUILD-005 and shipped to `Data/Scripts` — S — Depends: BUILD-005 — Verify: W-ci

## C. Papyrus reflection ("SP3 for FO4")
- [ ] **PLAT-030** Hook `IVirtualMachine::BindNativeMethod` (vtable slot **0x1B**, installed before the VM is created via `F4SEPlugin_Preload` (F4SE 0.7+); fall back to xSE PluginPreloader F4 only if preload runs too late; needs PLAT-002's vtable finder) and record all bound natives (game + F4SE + other plugins) — M — Depends: PLAT-002 — Verify: G-self
  - Accept: the self-test lists more than 800 natives including F4SE ones.
- [ ] **PLAT-031** Port `CallNative`/`VmProvider`/`GetNativeFunctionAddr`:
  - FO4 `StackFrame` (0x40, no inline args; use `Stack::GetStackFrameVariable`);
  - `isLatent` +0x42;
  - callback at +0x50 (std::function for libxse-created natives; raw pointer for vanilla, verify);
  - latent calls via `DispatchStaticCall`/`DispatchMethodCall` with an `IStackCallbackFunctor` (6 virtuals) resolving a JS Promise.
  - L — Depends: PLAT-030 — Verify: G-self
  - Accept: the self-test calls 20 representative natives (static, member, latent) and gets correct results.
- [ ] **PLAT-032** Marshalling: JS ↔ `BSScript::Variable` for int/float/bool/string/object/arrays plus **Struct** (JS object with member names) and **Var** (tagged JS value); handles via the FO4 handle policy; object pool flushed per update (SP semantics) — L — Depends: PLAT-031 — Verify: G-self
- [ ] **PLAT-033** `sp3.js` for FO4: runtime classes, namespaced script names (`Namespace:Script` → `sp["Namespace:Script"]` and camelCase aliases), `ScriptObject` root, compatibility config — M — Depends: PLAT-031 — Verify: G-self
- [ ] **PLAT-034** C++ fast paths plus stub natives, which must be registered so lookup succeeds (papyrus-api-map §2):
  - `notifyAnimationGraph` (no Debug.SendAnimationEvent in FO4)
  - `Game.getFormEx`
  - `Form.getType` plus an FO4 FormType enum
  - `getModCount`/`getModName` (or F4SE `GetInstalledPlugins`)
  - `getINI*`
  - `Input.isKeyPressed`/`getNumKeysPressed`
  - `Game.getPlayer` fast path
  - M — Depends: PLAT-031 — Verify: G-self
- [ ] **PLAT-035** Codegen: in-game `FunctionsDump` (FO4 VM + FO4 PEX reader from PVM-001/002) → `tools/ts_converter` → `falloutPlatform.ts`; `Definitions.txt` FO4 (events, custom API, enums: FormType, biped slots 30–61, menus, AV EDIDs, MotionType with keyframed = 2) — M — Depends: PLAT-033, PVM-002 — Verify: W-ci (typecheck) + G-self (dump)
- [ ] **PLAT-036** `blockPapyrusEvents` / `hooks.sendPapyrusEvent` via a hook on FO4 VM event dispatch, with an FO4 allow-list (ADR-011) — M — Depends: PLAT-030 — Verify: G-self

## D. Events & game lifecycle
- [ ] **PLAT-040** Event sinks: port `EventHandler` to the FO4 `TES*Event` set available in libxse, plus local definitions for the ~30 missing sources (candidate IDs in commonlib map §4.2; verify each in game); drop Skyrim-only story events — L — Depends: PLAT-002 — Verify: G-self
  - Accept: the self-test triggers and observes activate, equip/unequip, hit, death, container changed, furniture, cell attach/detach, load, menu open/close, combat state, quest stage.
- [ ] **PLAT-041** F4SE messaging → JS lifecycle events (`kGameDataReady`(true) → `gameDataLoaded`, `kPreLoadGame`, `kPostLoadGame`, `kNewGame`, `kPreSaveGame`/`kPostSaveGame`, `kDeleteGame`) — S — Depends: PLAT-010 — Verify: G-self
- [ ] **PLAT-042** Input & menus: `MenuOpenCloseEvent` names, button/mouse/thumbstick events. Determine the FO4 keyboard/mouse input path (raw input vs DirectInput8, blocker 6) and adapt the Tilted input hooks — M — Depends: PLAT-040 — Verify: G-self

## E. World entry & saves (ADR-007)
- [ ] **PLAT-050** `loadGame(saveName)` via `BGSSaveLoadManager` (ID from F4SE address 0xBEE760 on 1.11.240 → AE ID) + `postLoadGame` promise — M — Depends: PLAT-002 — Verify: G-self
- [ ] **PLAT-051** `moveRefrToPosition(ref, cellOrWorld, pos, rot)` (MoveTo; F4SE 0x1181020 → AE ID; or `SetLocationOnReference` AE 2201138) — M — Depends: PLAT-002 — Verify: G-self
- [ ] **PLAT-052** Save/load guard and autosave suppression hooks; INI forcing helpers — S — Depends: PLAT-050 — Verify: G-self

## F. Browser overlay & UI
- [ ] **PLAT-060** CEF overlay port: swap chain via `BSGraphics::RendererData` (`GetRendererData` NG/AE 2704429; Present `Renderer::End` 2276834 per prior-art), D3D11 device, render handler, sprite batch; CEF subprocess packaging — L — Depends: PLAT-004 — Verify: G-self
  - Accept: `browser.loadUrl` shows a page over the game; no FPS drop over 5% at 1080p.
- [ ] **PLAT-061** Browser focus/cursor: FO4 `CursorMenu` handling (AS3 paths, not Skyrim AS2), input capture while focused, `browserMessage` events, `executeJavaScript` — M — Depends: PLAT-060, PLAT-042 — Verify: G-self
- [ ] **PLAT-062** `worldPointToScreenPoint` (NiCamera::WorldPtToScreenPt3 NG/AE 2270344) for nameplates — S — Depends: PLAT-002 — Verify: G-self
- [ ] **PLAT-069** Tilted UI licensing decision (ADR-016): keep as-is, replace with our own overlay, or seek permission — S — Depends: — — Verify: — — **Needs user** (Q-04)

## G. Multiplayer natives (`TESModPlatform` FO4)
- [ ] **PLAT-070** Remote-actor creation: per-player runtime `TESNPC` via `IFormFactory` (AE 4796464) with player-like defaults (never base 0x7), Actor placement via `CreateReferenceAtLocation` (NG/AE 2192301) with `NEW_REFR_DATA`, skip-save flag — L — Depends: PLAT-031 — Verify: G-self
- [ ] **PLAT-071** AI/motion suppression for remote actors in the engine (ActorProcess/SetPosition/Rotate/RunDetection hooks; prior-art Adopt 2) — M — Depends: PLAT-070 — Verify: G-self
- [ ] **PLAT-072** `setActorTransform` (character-controller warp) and keyframed motion helper — M — Depends: PLAT-070 — Verify: G-self
- [ ] **PLAT-073** DoNothing-style AI package template for remote NPC bases (FO4 form id lookup) — S — Depends: PLAT-070 — Verify: G-self
- [ ] **PLAT-075** Animation hooks and `AnimApi` (see F02-T03/T04) — tracked in F02.
- [ ] **PLAT-076** hkb/ActorMediator/BGSActionData local types (see F02-T02) — tracked in F02.
- [ ] **PLAT-080** Inventory API: read `BGSInventoryList` (stacks, extra lists, `BGSObjectInstanceExtra` OMODs) → JS; `setInventory` with item instances; `addItemEx` (instance + health + name) — L — Depends: PLAT-031 — Verify: G-self (F04)
- [ ] **PLAT-081** Equip API: `ActorEquipManager::EquipObject/UnequipObject` (NG/AE 2231392/2231395) with a real `BGSObjectInstance` (OMODs kept); filter the transient readied-weapon unequip; strip auto-added ammo — M — Depends: PLAT-080 — Verify: G-self (F05)
- [ ] **PLAT-082** Appearance natives: write the TESNPC face block (head parts, morph sliders, region sliders, facial-bone sliders, body morph weights, tints, hair/skin colour) + `Actor::Reset3D` (NG/AE 2229913) with `bUseFaceGenPreprocessedHeads=0` — L — Depends: PLAT-070 — Verify: G-self (F03)
- [ ] **PLAT-083** Weapon/projectile natives: block `Fire`/`Launch` on remote actors; spawn a cosmetic projectile from an origin/direction (`Projectile::Launch` with `bUseOrigin`; AE ID needs RE); muzzle/impact FX — L — Depends: PLAT-002 — Verify: G-self (F09)
- [ ] **PLAT-084** Power armor natives: enter/exit on actors (`SwitchToPowerArmor`, furniture interaction), frame piece inventory, core charge read/write — L — Depends: PLAT-081 — Verify: G-self (F17)
- [ ] **PLAT-085** Lock/terminal/hack natives: lock level/state read/write (`REFR_LOCK::SetLocked` NG/AE 2191020), terminal menu hooks, lockpick result hooks — M — Depends: PLAT-040 — Verify: G-self (F24)
- [ ] **PLAT-086** Weather/time natives: `Sky` force/reset weather (2208861/2208860), Calendar/GameHour, timescale — S — Depends: PLAT-002 — Verify: G-self (F25)
- [ ] **PLAT-087** Workshop natives: build-mode enter/exit events, placement/move/scrap intents interception, applying server-placed objects, power-grid visuals — XL — Depends: PLAT-040 — Verify: G-self (F22)
- [ ] **PLAT-088** `setCollision` replacement and NetImmerse-style node APIs (node world position, scale, material swap) — M — Depends: PLAT-031 — Verify: G-self
- [ ] **PLAT-089** `MpClientPlugin.dll` loading from `Data/F4SE/Plugins` with the protocol prefix (`CreateClientEx`) — S — Depends: NET-001 — Verify: G-self
- [ ] **PLAT-090** Multi-runtime support (OG 1.10.163 / NG 1.10.984) via dual ID tables — XL — Depends: M8 — Verify: G-self on each runtime — (post-1.0, optional)
- [ ] **PLAT-091** Self-test hooks: `[Debug] bSelfTest=1` runs the QA-010 suite after the first load and writes `falloutmp-selftest-<sha>.json` — S — Depends: PLAT-010 — Verify: G-self
- [ ] **PLAT-095** Dependency detection (ADR-021): enumerate loaded F4SE plugins and versions via `F4SEInterface::GetPluginInfo`, detect Buffout 4 NG, High FPS Physics Fix, LooksMenu, MCM, HUDFramework; report in the self-test JSON and in the connect manifest; show an in-game prompt listing missing required/recommended mods — S — Depends: PLAT-001, PLAT-091 — Verify: G-self
