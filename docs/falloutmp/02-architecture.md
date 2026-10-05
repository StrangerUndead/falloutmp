# 02 — Target Architecture & Decision Records

## 1. Target architecture

```
                    Fallout 4 process (Windows, AE 1.11.x — ADR-001)
┌───────────────────────────────────────────────────────────────────────────────────┐
│ f4se_loader → Data/F4SE/Plugins/FalloutPlatform.dll (F4SEPlugin_Version + _Load)  │
│                 └─ Data/F4SE/Plugins/FalloutPlatformImpl.dll (ADR-003 layout rule)│
│   ├─ CommonLibF4 (libxse, pinned) + Address Library for F4SE (ADR-003)            │
│   ├─ Hooks: F4SE trampoline / vtable / Frida-gum (same toolset as SP)             │
│   ├─ Embedded Node.js (libnode.dll, N-API) — unchanged from SP (ADR-004)          │
│   │     └─ Data/Platform/Plugins/falloutmp-client.js                              │
│   │           └─ sp.mpClientPlugin → MpClientPlugin.dll (SLikeNet, unchanged) ────┼──┐
│   ├─ "SP3 for FO4": runtime reflection of the FO4 Papyrus VM (structs, Var) (ADR-006)
│   ├─ TESModPlatform.pex (FO4-compiled) natives: CreateNpc, appearance, inventory   │  │
│   │     with OMOD instances, power armor, weapon fire, etc.                        │  │
│   ├─ World entry: template .fos + MoveTo (ADR-007)                                │  │
│   └─ CEF overlay (Tilted UI, DX11 Present hook) → falloutmp-front (ADR-005)       │  │
└───────────────────────────────────────────────────────────────────────────────────┘  │
                                                                                       │ UDP
┌───────────────────────────────────────────────────────────────────────────────────┐  │
│ Node.js server (skymp5-server, game = "fallout4")                                 │◄─┘
│   ├─ scam_native.node: PartOne · WorldState · MpActor/MpObjectReference · Grid    │
│   │    GameProfile(Fallout4): AVIF actor values, FO4 damage, slots, OMOD          │
│   │    inventory, workshop, power armor, progression … (ADR-002)                  │
│   │    libespm (espm::Game::Fallout4, ESL, BA2 strings)                           │
│   │    papyrus-vm (FO4 PEX: LE, structs, Var, +11 opcodes)                        │
│   │    save storage: file / MongoDB (unchanged)                                   │
│   ├─ falloutmp-gamemode (public default gamemode: chat, spawn, admin) (ADR-018)   │
│   └─ login: offline profileId / FalloutMP master (ADR-017)                        │
└───────────────────────────────────────────────────────────────────────────────────┘
```

## 2. Repository layout (target)

```
falloutmp/
├─ libespm/              shared; game-aware parsing (espm::Game), ESL, FO4 records
├─ papyrus-vm/           shared; Skyrim + FO4 PEX formats, structs/Var, FO4 opcodes
├─ serialization/ viet/  shared; unchanged
├─ savefile/             Skyrim .ess (kept); FO4 .fos support only if ADR-007 changes
├─ skymp5-server/        shared; GameProfile abstraction + Fallout4 profile
├─ skymp5-scripts/       Skyrim scripts (kept)
├─ falloutmp-scripts/    NEW: FO4 Papyrus sources/stubs (our own) + compiled pex
├─ fallout4-platform/    NEW: fork of skyrim-platform on F4SE + CommonLibF4
├─ falloutmp-client/     NEW: fork of skymp5-client
├─ falloutmp-front/      NEW: fork of skymp5-front (Pip-Boy/terminal theme)
├─ falloutmp-gamemode/   NEW: public default gamemode (TS)
├─ falloutmp-client-deps/ NEW: Address Library bins (not redistributed if license forbids; see DOCS), template save, swf
├─ skyrim-platform/ skymp5-client/ skymp5-front/ skymp5-functions-lib/ client-deps/  (kept for upstream merges)
├─ unit/                 shared tests + FO4 tests (synthetic fixtures; [fo4data] real-data tag)
└─ docs/falloutmp/       this plan
```

CMake selects the game with `-DGAME=skyrim|fallout4` (task BUILD-001). It controls which client-side directories are built, which `dist/` layout is produced and which test data is used. The server and libraries are always built for both games. The game is chosen at runtime by `server-settings.json` → `"game"`.

## 3. Component responsibilities & seams

| Seam | Owner | Purpose | Spec/Task |
|---|---|---|---|
| `GameProfile` (C++, server) | skymp5-server | All game-specific server rules: load order defaults, worldspace/spawn, actor-value catalogue, damage formula, equipment slots, inventory extra-data schema, crafting rules, condition functions, NPC spawn filters, reloot defaults, standard scripts and native classes, animation→state map, protocol prefix | REF-010…REF-030 |
| `espm::Game` | libespm | Detect the game from TES4 HEDR/form version; select record layouts; ESL-aware ID mapping | ESPM-001… |
| PEX format flag | papyrus-vm | Per-script format (TES5 BE / FO4 LE) and feature gates (structs, Var, opcodes ≥ 0x24) | PVM-001… |
| Message schema | messages / client | Shared message structs with optional game-specific fields, plus new FO4 messages (MsgType ≥ 34) | NET-001… |
| `falloutPlatform` TS module | fallout4-platform | Same surface as `skyrimPlatform` where the concepts match, generated from the FO4 VM dump | PLAT-030… |
| Client services | falloutmp-client | Forked services; shared pieces (networking, model, DI) are kept as close to upstream as possible for easy porting | CLI-001… |

## 4. Architecture Decision Records

Status legend: **Accepted** (implement), **Proposed** (default plan; confirm with the user before passing its first prototype task), **Open** (research needed).

### ADR-001 Target Fallout 4 runtime — *Proposed*
- **Decision:** Target the **Anniversary Edition 1.11.x** line (latest Steam/GOG build at M0, plus the builds supported by the chosen CommonLibF4 commit and Address Library), using F4SE 0.7.x.
- Pin an explicit allow-list of runtime versions. The platform refuses to start on any other version and shows a clear message.
- **Why:** It is the actively maintained line: F4SE 0.7.9, libxse/commonlibf4 up to 1.11.240, current Address Library. Recent FO4 multiplayer projects (Commonwealth Online, FO4_Wrld) target it.
- **Alternatives:**
  - NG 1.10.984: large user base; alandtse fork; no AE IDs.
  - OG 1.10.163: old modding baseline.
  - Multi-runtime: LucaDotGit fork or our own dual IDs. This is the later goal (`PLAT-090`).
- **Consequences:** Every Bethesda patch needs an Address Library refresh and a platform release. Users on NG/OG must upgrade or downgrade.
- **Confirm with user:** Q-02.

### ADR-002 Repository strategy: game-pluggable shared core, forked client side — *Proposed*
- **Decision:**
  - `libespm`, `papyrus-vm`, `serialization`, `viet` and `skymp5-server` become game-pluggable (GameProfile / espm::Game / PEX flag). Skyrim behaviour must stay unchanged.
  - Client-side components are forked into new directories.
- **Why:**
  - The shared code is ~60% game-agnostic, and upstream SkyMP is actively developed there, so merging stays cheap.
  - The client side is overwhelmingly game-specific, so a fork avoids `#ifdef` sprawl.
- **Alternatives:**
  - Full fork (cheap now, expensive later: no upstream fixes).
  - Single codebase with compile-time `GAME` macros everywhere (fragile).
- **Consequences:** Some refactor cost up front (REF backlog). Skyrim unit tests act as the regression net.

### ADR-003 Reverse-engineering foundation — *Proposed (prototype in PLAT-001/BUILD-002)*
- **Decision:** use `libxse/commonlibf4` pinned at `7c8c6f8` (AE 1.11.x) together with `libxse/commonlib-shared`, consumed through two vcpkg overlay ports:
  - `commonlib-shared`, which already ships CMake;
  - `commonlibf4-ae`, with a CMakeLists.txt we supply, because upstream builds only with xmake.
- C++23 applies only to the `fallout4-platform` targets; the rest of the repo stays C++20. F4SE plugin version data is written by hand.
- The community vcpkg `commonlibf4` port is the obsolete 2022 pre-AE library and must not be used.
- **Layout constraint:** the library loads `version-1-11-xxx-0.bin` from next to the DLL that contains it, under an `F4SE` folder. So **both** `FalloutPlatform.dll` and `FalloutPlatformImpl.dll` must live in `Data/F4SE/Plugins`. SP's `Data/Platform/Distribution/RuntimeDependencies` location would crash at startup. Other runtime dependencies (libnode, CEF) can stay in `RuntimeDependencies`.
- **Known RE gaps:** vtable IDs still use pre-AE numbering; about 30 event-source getters are missing; animation-hook slots; named-save loading; MoveTo. Details in reference/commonlib-port-map.md §7.
- **Fallback:** alandtse/CommonLibF4 (MIT, CMake) plus our own AE ID table.

### ADR-004 JavaScript runtime — *Accepted*
- Keep SP's embedded Node.js (embedder API, N-API addon, hot reload, `sp.storage`).
- It does not depend on the game, so we reuse proven code.

### ADR-005 In-game browser UI — *Proposed*
- **Decision:** Reuse the Tilted CEF overlay: D3D11 Present hook, DInput/WndProc capture.
- **Why:** FO4 is also D3D11 + DirectInput8, and it keeps `skymp5-front` and the widgets system as they are.
- **Alternatives:**
  - PrismaUI (Ultralight), already used by FO4 mods. It is lighter but a different API, and its NG-runtime support is uneven.
  - Scaleform-only UI.
- **Revisit if** the overlay conflicts with FO4's renderer or input (`PLAT-060` prototype).

### ADR-006 Papyrus bridge — *Proposed (pending PLAT-030 prototype)*
- **Decision:**
  - Port SP3 runtime reflection: hook `BindNativeMethod`, record bound natives, build JS classes at runtime, call through VM stack-frame takeover or `DispatchStaticCall`/`DispatchMethodCall`.
  - Add marshalling for **Struct** and **Var**.
  - Generate `falloutPlatform.ts` from an in-game dump plus the FO4 PEX reader.
- **Fallback:** A fixed list of natives registered through F4SE's Papyrus interface, plus latent `Dispatch*` calls for everything else.

### ADR-007 World entry (spawning into the world) — *Proposed*
- **Decision:**
  1. Ship a small template `.fos` created once in game: new character after Vault 111, main quest suppressed.
  2. On connect, load it with `BGSSaveLoadManager`.
  3. Move the player to the server position (`MoveTo`) and apply appearance, inventory and actor values through natives.
- **Why:** Writing `.fos` files (SkyMP's approach for `.ess`) needs a full FO4 save writer (`savefile` only knows Skyrim). The template approach needs only a save loader.
- **Revisit:** If template state leaks (quest/world flags) cause problems, implement a `.fos` patcher (`DATA-030`).

### ADR-008 Animation sync strategy — *Proposed (confirm with the F02-T01 prototype)*
- **Decision:** the hybrid "state + action + curated events + name-resolved graph variables" (strategy D in reference/fo4-animation-sync.md §6):
  - **engine state**: ActorState gun/weapon/knock/life/sit bits, sneak, sprint, sighted, power armor, furniture;
  - **`BGSAction` replay** via `Actor::PerformAction`;
  - **a whitelist of animation events** via native `NotifyAnimationGraph`;
  - **behaviour-graph variables** resolved by name at runtime, written after channel flush, with a 100–150 ms jitter buffer.
- **Why:**
  - FO4 Papyrus has neither `Debug.SendAnimationEvent` nor `KeepOffsetFromActor` (verified in `f4se/scripts/vanilla/*.psc`), so SkyMP's locomotion trick cannot be ported.
  - In first person the third-person graph is "parked" and receives no events. Engine state and actions are perspective-independent.
  - Bandwidth stays at SkyMP's level (~1–2 KB/s per remote actor).
  - Actions give the server semantics to validate fire rate, ammo and AP.
- **Alternatives:**
  - A. event replay: first-person gaps;
  - B. STR action + hash-indexed vars: fragile with mods;
  - C. bone replication (FO4_Wrld): ~26 KB/s per actor, no semantics, per-skeleton retargeting.
- **Fallback:** if the prototype shows gaps, drive the parked 3rd-person graph on the *sender* (FO4_Wrld technique) behind a setting. Never stream full skeletons.

### ADR-009 Protocol & message evolution — *Accepted*
- **Freeze the Skyrim wire format.** MsgTypes 1–33 and protocol `"7_"` stay byte-identical.
  - BitStream messages are positional, so even an optional field changes the layout (reference/skyrim-coupling-index.md §5.3).
- **FO4 twins.** Game-specific payloads become new message types:
  - IDs 64–79 for twins of existing messages (CreateActorFo4, UpdateMovementFo4, UpdateAppearanceFo4, …);
  - IDs 80–122 for new FO4 messages.
  - IDs 34–63 are reserved for upstream growth. All IDs stay < 123, because `{` marks JSON.
  - The registry is in 01-sync-standard.md §6.
- **Shared payload code.** Payload structs are templated (`CreateActorT<Appearance, Equipment, AvMap>`), so serialization code is shared.
- **Per-game registration.** `MessageSerializerFactory` registers messages per game, selected by GameProfile.
- **Protocol prefix.** `GameProfile::ProtocolPrefix()` returns `"7_"` for Skyrim and `"fo4-<n>_"` for FO4.
  - The client plugin receives the prefix through `CreateClientEx(host, port, prefix)` or a compile definition.
  - A client of the wrong game is refused at the RakNet password step.

### ADR-010 Persistence model — *Proposed*
- Reuse `MpChangeFormREFR` for references and actors. Add FO4 fields as optional JSON members with defaults: progression, power armor link, OMOD instances, rads, limb condition, etc.
- Use new change-form-like records for non-reference state: workshops, player progression if kept separate, vendor stock. They are stored through the same `ISaveStorage`/DB drivers.
- Schema changes are additive. The `migration` driver handles breaking changes.

### ADR-011 Vanilla quests & scripts — *Proposed*
- Same policy as SkyMP. The client blocks vanilla Papyrus events (`blockPapyrusEvents`) with an allow-list. The server strips vanilla scripts from references by default.
- The FO4 allow-list must include scripts needed for doors, elevators, terminals and similar.
- Gamemode-authored quests run in the server VM (F27).

### ADR-012 Settlements — *Proposed*
- A server-side workshop model replaces `WorkshopParentScript` logic.
- The vanilla `WorkshopMenu` is used for placement UX. The client reports placement intents and the server validates budget, permission and location, then creates server forms.

### ADR-013 VATS — *Proposed*
- Disabled at T0. Block `VATSMenu`.
- T2 "VATS-lite": no global slow-motion. The client sends target and body-part selections. The server resolves hit chance and outcome using FO4 formulas plus line-of-sight from movement history.

### ADR-014 Combat authority — *Proposed*
- The owner client reports fire events: weapon instance, origin, direction, timestamp, shot sequence.
- The server validates ammo, fire rate and line-of-sight against rewound positions (lag compensation). It computes damage with the FO4 formula and perks.
- This exceeds SkyMP's model, which trusts hits with light checks. The extra validation is needed because guns make cheating trivial otherwise.

### ADR-015 Verification strategy — *Accepted*
- **Linux-first:** unit and integration tests with synthetic fixtures (plugins/PEX/BA2), and a mocked client API for TS tests.
- **Windows:** CI for DLLs.
- **In game:** a self-test plugin (`QA-010`) plus manual scripts run by the user.
- **Real data:** tests tagged `[fo4data]`, run only where the user provides the data.

### ADR-016 Third-party code & licensing — *Proposed*
- Keep the upstream license structure (AGPL server, GPL client/platform, MIT libs).
- Tilted Phoques UI/hook code is "All Rights Reserved, provided as is". It is used by upstream as-is; keep its notices.
- Before a public release, decide on keeping it, replacing the overlay (`PLAT-069`), or getting permission (Q-04).
- CommonLibF4 (GPL-3.0-or-later with exceptions) is compatible.
- Reuse code from prior-art projects only if its license is compatible (see reference/prior-art.md).

### ADR-017 Master server & auth — *Proposed*
- Offline mode (`profileId`) through M8.
- Afterwards, a FalloutMP master service (server list, sessions) compatible with the existing `MasterClient`/`login.ts` protocol, or a Discord-only login.
- `gateway.skymp.net` belongs to SkyMP and must not be used.

### ADR-018 Default public gamemode — *Accepted*
- Upstream's real gamemode (`skymp5-gamemode`) is private; `skymp5-functions-lib` only pulls it.
- FalloutMP ships its own public `falloutmp-gamemode`: spawn, chat, admin commands, basic rules. Servers have something to run, and every feature has a reference integration.

### ADR-019 Loot model — *Proposed*
- Default is shared world loot (SkyMP behaviour).
- Optional server setting for per-player instanced containers (T2) to reduce griefing.

### ADR-020 Progression authority — *Proposed*
- XP, level, SPECIAL and perks are server-authoritative.
- Client menus (LevelUpMenu/PerkChart) become *requests* that the server validates.
- Perk entry points that affect damage, prices, etc. are evaluated on the server.

### ADR-021 Third-party mod dependencies — *Accepted*
- **Decision:** FalloutMP may require or recommend existing Fallout 4 mods instead of re-implementing what they do. Policy, candidates and tasks are in [07-dependencies-and-mods.md](07-dependencies-and-mods.md).
- Required: F4SE and the Address Library. Strongly recommended: Buffout 4 NG (crash logs, stability), High FPS Physics Fix (consistent physics across clients), LooksMenu (appearance API and presets), MCM (settings UI).
- Any F4SE plugin that registers Papyrus natives becomes available to FalloutMP scripts through reflection (ADR-006) at no extra cost.
- **Constraints:** dependencies are user-installed unless redistribution is permitted; the client manifest verifies them; core sync keeps a native fallback where a dependency is closed source; the server allow/deny-lists client DLL mods (SRV-003).
- **Why:** it cuts platform work (appearance, settings UI, stability), and it matches how the Fallout 4 modding ecosystem already works.
