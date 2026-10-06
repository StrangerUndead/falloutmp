# 00 — Vision, Scope, Parity

## 1. Vision

FalloutMP is an open-source, server-authoritative multiplayer framework for **Fallout 4**, derived from SkyMP. Server owners run a persistent shared Commonwealth. They extend it with JavaScript gamemodes (`mp` API) and server-side Papyrus, exactly as SkyMP servers do for Skyrim.

"Works just as well as SkyMP" means:

1. **Same architecture and developer experience.** That covers the gamemode API, client-side JS snippets, server Papyrus, hot reload, database drivers, metrics and offline mode.
2. **Same sync standard** ([01-sync-standard.md](01-sync-standard.md)) for every feature. Where SkyMP keeps state server-authoritative, persisted, validated, gamemode-hookable and unit-tested, FalloutMP does the same for the Fallout 4 equivalent.
3. **Every Fallout 4-specific system** (guns, ammo, power armor, weapon/armor mods, components/scrap, settlements, VATS, SPECIAL/perks, radiation, chems, survival, companions, terminals, vendors…) is synced to that same standard. Each one follows its own feature spec, not a cosmetic approximation.

## 2. What a "fully functional" release looks like (player and admin journeys)

| # | Journey | Acceptance (summary) | Specs |
|---|---|---|---|
| J1 | Install client and connect | Copy the client dist and F4SE into Fallout 4 (AE 1.11.x). Launch with `f4se_loader.exe`. The client connects to the server in `falloutmp-client-settings.txt`, or picks one from a server list. | PLAT, CLI, OPS |
| J2 | Login | Offline `profileId`, or online auth through a FalloutMP master server. Reconnecting restores the same character. | GM, OPS |
| J3 | Character creation | A new player gets the FO4 face/body editor (LooksMenu flow) with no Vault 111 intro. The result is validated, persisted and visible to others. | F03 |
| J4 | Spawn and explore | Spawn at a configurable start point (default near Vault 111 exit / Sanctuary). Free movement in exteriors and interiors, load doors, elevators. | F01, F07 |
| J5 | See others | Other players and NPCs move, animate (1st/3rd person, sprint, sneak, aim, fire, reload, melee, jump, sit), wear the right apparel, modded weapons and power armor, and show nameplates. | F01, F02, F05, F17, F31 |
| J6 | Loot and items | Containers, corpses and world items are server-authoritative and shared (or instanced, per server config). Pick up, drop, transfer, reloot timers, leveled lists evaluated on the server. No duplication, no loss. | F04, F06, F14 |
| J7 | Combat | Guns (semi/auto, ammo, magazines, reload), melee/unarmed, explosives, against players and NPCs. Damage computed on the server with FO4 DR/ER/RR rules, limbs and criticals. Hit validation with lag compensation. Death and respawn. | F09, F10, F11, F12 |
| J8 | NPC world | Enemies, creatures and settlers spawn from the ESMs and are simulated by a hosting client. Combat AI works, host migration works. Respawn follows server rules. | F13, F14 |
| J9 | Progression | XP from kills/lockpicks/hacks/crafting, levels, SPECIAL, perks (entry points evaluated on the server), bobbleheads/magazines. All persistent. | F19 |
| J10 | Crafting and modding | Chem/cooking/armor/weapon stations, component-based recipes, junk scrapping, weapon/armor modding with OMODs (perk-gated). Results visible to others. | F15, F16 |
| J11 | Power armor | Frames in the world, entering and exiting, pieces with health, paint/mods, fusion cores, jetpack, PA station. Others see it correctly. | F17 |
| J12 | Survival and status | Health, AP, Rads (max-HP reduction), chems and addiction, food/water, optional survival-mode needs. Server rules decide. | F08, F20 |
| J13 | Economy | Vendors with caps, barter prices, restock; player-to-player trade (gamemode). | F23 |
| J14 | Settlements | Claim a workshop, build/move/scrap objects in build mode, power, settlers, defense, ratings. Persistent, with owner permissions and budgets. | F22 |
| J15 | World interaction | Locks/lockpicking, terminals/hacking, holotapes, switches, power-dependent objects. | F24 |
| J16 | Companions | Player-owned companions and Dogmeat with commands and inventory, hosted by the owner. | F21 |
| J17 | Time, weather, map | Server-driven time and weather incl. radstorms. Map markers and discovery per player. Fast-travel rules set by the server. | F25, F26 |
| J18 | VATS | Server-resolved "VATS-lite" (no global slowdown), or disabled by server setting. | F18 |
| J19 | Quests and dialogue | Gamemode-authored quests and dialogue via server Papyrus/JS. Vanilla quests disabled by default (same policy as SkyMP). | F27 |
| J20 | Admin and gamemode authoring | `server-settings.json`, gamemode hot reload, `mp.*` API with FO4 properties/events, server Papyrus, console commands with permissions, chat, metrics, DB drivers. | GM, SRV, OPS, DOCS |

## 3. Parity matrix (SkyMP feature → FalloutMP feature)

The "SkyMP level" column uses the L0–L4 scale from [01-sync-standard.md](01-sync-standard.md), assessed from the code (see [reference/skymp-sync-inventory.md](reference/skymp-sync-inventory.md)). FalloutMP must reach at least that level for the FO4 analogue. "Target" is the FalloutMP goal and is sometimes higher ("SkyMP-plus") where FO4 gameplay demands it, e.g. gun hit validation.

| SkyMP feature (Skyrim) | SkyMP level | FalloutMP analogue | Target | Spec |
|---|---|---|---|---|
| Movement (pos/rot/runMode/flags, 130 ms) | L3 (trusted pos, teleport validation) | Same, plus aiming/sighted/PA/jetpack flags | L3+ (speed sanity check) | F01 |
| Animation events | L2–L3 | 3rd-person graph events/variables, 1st→3rd mapping, weapon actions | L3 | F02 |
| Appearance + RaceMenu | L3 (accepted only while menu open) | FO4 morph/region/tint/headpart model + LooksMenu | L3 | F03 |
| Inventory + extra data | L4 | Inventory with OMOD instances, legendary, components | L4 | F04 |
| Equipment (+ slot validation) | L3 | FO4 biped slots, under-armor layering, PA pieces, weapon instances | L4 | F05 |
| Containers (open/take/put, occupancy) | L4 | Same, plus quick-loot transfer, workshop shared inventory | L4 | F06 |
| Item pickup / harvest / drop | L4 | Same (flora: tatos, mutfruit…), junk | L4 | F06 |
| Activation: doors, teleport doors, furniture, activation parents | L4 | Same, plus elevators, switches, power-dependent objects, PA frames as furniture | L4 | F07 |
| Crafting (forge COBJ) | L3–L4 | Component-based COBJ at chem/cooking/armor/weapon stations | L4 | F15 |
| Potions / eating (MGEF to AVs) | L3 | Chems, food, drink, addiction, rads | L4 | F20 |
| Spells / magic | L1–L2 (effects TODO) | n/a (no spells); magic-effect infrastructure is reused for chems, legendary effects, rad effects | L4 for effects | F20, F11 |
| Bow shots / ammo | L3 | Guns: fire modes, ammo per shot, magazines, reload, projectiles | L4 (SkyMP-plus) | F09 |
| Hits and damage (server formula) | L3 (reach check disabled) | FO4 damage model, limbs, crits, sneak, perks, lag-compensated validation | L4 (SkyMP-plus) | F09, F10, F11 |
| Blocking | L3 | Melee block | L3 | F10 |
| Health/Magicka/Stamina + regen cropping | L3 | Health, Action Points, Rads, limb condition, SPECIAL-derived values | L4 | F08 |
| Death and respawn | L4 | Same, plus bleedout/essential companions | L4 | F12 |
| NPC spawn from ESM + hosting | L3 | Same, plus FO4 filters (workshop NPCs, companions, synth/settlers) | L3+ | F13 |
| Leveled lists (server-evaluated) | L4 | Same, with FO4 LVLI/LVLN layout, legendary rolls | L4 | F14 |
| Reloot / world respawn | L4 | Same, by FO4 record types | L4 | F14 |
| Console commands (admin) | L3 | Same (FO4 command table) | L3 | F30 |
| Chat (gamemode) | L4 (gamemode) | Same, via a new **public** default gamemode | L4 | F30 |
| Time sync (client sets globals) | L2 | Server time and weather incl. radstorms | L3 | F25 |
| Nicknames / display names | L3 | Same (FO4 head node) | L3 | F31 |
| Custom properties / event sources (gamemode) | L4 | Same API, FO4 `ctx.sp` (falloutPlatform) | L4 | F31 |
| Learned spells | L4 | n/a → replaced by perks/recipes/known mods | L4 | F19 |
| Factions | L2 | Factions/hostility | L3 | F13, F27 |
| Quests (vanilla) | L0–L1 | Gamemode quests; vanilla blocked | L3 (gamemode) | F27 |

## 4. Fallout 4-only systems (no SkyMP analogue): all must reach the target level

| System | Target | Spec | Tier |
|---|---|---|---|
| Weapon/armor modding (OMOD, INNR names) | L4 | F16 | T1 |
| Components and scrapping (CMPO) | L4 | F15 | T1 |
| Power armor (frames, pieces, cores, jetpack, station) | L4 | F17 | T1 |
| SPECIAL, perks, XP, levels, bobbleheads, magazines | L4 | F19 | T1 |
| Radiation, chems, addiction | L4 | F08, F20 | T1 |
| Survival-mode needs (optional server rule) | L3 | F20 | T2 |
| Explosives, grenades, mines, turrets | L4 | F10 | T1 |
| VATS-lite | L3 | F18 | T2 |
| Companions and Dogmeat | L3 | F21 | T2 |
| Workshop / settlements | L4 | F22 | T1 (core build); T2 (attacks, supply lines) |
| Vendors, barter, caps | L4 | F23 | T1 |
| Locks, terminals, hacking, holotapes | L4 | F24 | T1 |
| Map markers, discovery, fast travel rules | L3 | F26 | T1 |
| Pip-Boy integration, radio, flashlight | L2 (cosmetic) / L3 (data) | F28 | T1/T2 |
| Stealth (detection, Stealth Boy) | L3 | F29 | T2 |

## 5. Scope tiers

- **T0, SkyMP parity:** everything in §3 at SkyMP's level, plus the FO4 adaptations needed to play at all (guns basic fire/ammo, Health/AP/Rads, FO4 appearance). This is the first public alpha (milestone M8).
- **T1, Fallout 4 identity:** OMOD modding, components/scrap, power armor, SPECIAL/perks/XP, chems/rads, explosives, vendors, locks/terminals, core settlement building, map. This is the beta (M11).
- **T2, advanced:** VATS-lite, companions, survival mode, settlement attacks/supply lines/settlers' jobs, stealth tech, Pip-Boy data sync, radio sync, gamemode quest framework. This is 1.0 (M12) and beyond.

## 6. Non-goals (unless the user explicitly changes scope)

- Co-op sync of the vanilla main quest and of faction questlines. SkyMP doesn't sync vanilla quests either; vanilla quest scripts are blocked. Gamemodes can author quests (F27).
- Game Pass / Microsoft Store and Epic versions (F4SE unsupported), Fallout 4 VR, consoles.
- Recreating Fallout 76 content.
- Mod-list sync beyond SkyMP's manifest/CRC check (server-defined load order only).
- Peer-to-peer hosting. The dedicated server model stays.

Candidate systems noted but not specced (see features/README.md): physics grab, scripted transit and Vertibirds, admin spectator, emotes, service NPCs, player stash, radiant encounters, voice chat, photo mode. They are added as specs when a milestone has room; none is required for 1.0.

## 7. Quality targets (proposed; confirm in M0; movement-only bot measurements from M5, full QA-020 from M8)

| Metric | Target | How measured |
|---|---|---|
| Concurrent players per server | **SkyMP parity = ~1,000** (SkyMP's compile-time cap is `MAX_PLAYERS=1000`, `falloutmp-server/cpp/CMakeLists.txt:46`; production SkyMP servers have run 1,000+ players). Staged: 64 at the M8 alpha, 300 at the M11 beta, **1,000 at 1.0**. FO4 actors cost more per client, so the server-side budget (interest management, relay cost, tick) is engineered for 1,000 from M1, and the client is budgeted for ~40 visible actors | Bot load tests (`QA-020`, `QA-022`) at each stage |
| Client frame rate | ≥ 45 FPS at 40 visible remote actors on the reference GPU (GTX 1070-class at 1080p, medium), ≥ 30 FPS at 64 in a hot spot; beyond `client.maxVisibleActors` remotes degrade to nameplate-only (CLI-090) | `G-manual` FPS capture at 20/40/64 actors |
| Server tick cost | p95 < 10 ms at 1,000 players and 2,000 hosted NPCs spread over the map; p95 < 10 ms with 64 players in one 3×3 grid area (relay hot spot) | Prometheus metrics, load tests |
| Remote movement smoothness | No visible teleporting at RTT ≤ 150 ms, 2% loss | `G-manual` scenario plus jitter injection |
| Hit registration | Server validates hits with rewind ≤ 250 ms; false-reject rate < 2% at RTT 150 ms | `L-int` simulation plus `G-manual` |
| Bandwidth | ≤ 20 KB/s down per client at 30 visible actors; ≤ 40 KB/s at 64 in one area (hot spot) ; server egress budget documented per 1,000 players | Metrics |
| Persistence | Zero item dup/loss across disconnect, server crash (≤ last flush) and restart | `L-unit`/`L-int` adversarial tests |
| Client stability | No crash in a 2 h session with 10 players; recoverable reconnect | `G-manual` soak test |
| Version support | Fallout 4 AE 1.11.x (pinned list); clear error on unsupported runtimes | `G-self` |

## 8. Companion mods

FalloutMP may require or recommend existing Fallout 4 mods rather than re-implement them (ADR-021, [07-dependencies-and-mods.md](07-dependencies-and-mods.md)). Required: F4SE and the Address Library. Strongly recommended: Buffout 4 NG, High FPS Physics Fix, LooksMenu and MCM. Dependencies are user-installed and verified by the client manifest.

## 9. Supported platforms

- **Client:** Windows 10/11, Fallout 4 Steam/GOG **AE 1.11.x** (exact builds pinned in ADR-001), F4SE 0.7.x, Address Library for F4SE.
- **Server:** Linux x64 (primary, Docker) and Windows x64, Node.js 22, same as SkyMP.
- **Build:** client parts MSVC 2022 on Windows / GitHub Actions; server, libraries and tests on Linux (clang) or Windows.
