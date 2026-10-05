# STATUS — FalloutMP

> Update this file at the end of every session (see README §1). Newest entries go at the top of each log.

## Current position
- **Plan version:** 1.1 (2026-10-05)
- **Working branch:** `claude/fallout4-port-research`
- **Current milestones:** the Linux-verifiable parts of M1, M2 and M6–M11 are built. The server game layer and the client services are implemented and tested. Everything that needs Fallout 4 running (M3 platform onwards) is not started.
- **Code so far:** 33 commits on top of upstream SkyMP `f926944`, 16 of them plan-only. See the evidence log below.

## What exists (2026-10-05)
| Area | State | Verified by |
|---|---|---|
| Game profiles, `game` setting, protocol prefix, MsgType 64–120 | Done | `GameProfileTest`, `Fo4MessagesTest` |
| libespm Fallout 4 readers (WEAP, ARMO, AMMO, MISC, CMPO, COBJ, OMOD, FURN, GLOB, ALCH, LVLI, LVLN, CONT, REFR, keywords, PRPS) | Done, synthetic plugins only | `Fo4EspmTest`, `Fo4EspmDataSourceTest` |
| Server systems: inventory with instances, crafting, auto-scrap, scrapping, OMOD stats and modding, power armor, workshops (claim, ACL, build mode, placement, power, wires, settlers, ratings, daily update), actor values, damage model, progression, consumables and addictions, barter, locks and hacking, ranged combat validation, parties and PvP, leveled lists, containers and corpse loot, equipment, clock, weather, map discovery and fast travel | Done | `Fo4*Test` (C++) |
| PartOne integration, per-actor and world persistence, load-order bootstrap | Done | `Fo4PartOneTest`, `Fo4WorldBootstrapTest` |
| `fo4` server settings block with validation | Done | `Fo4SettingsTest` |
| Gamemode API `mp.fo4.*` and `mp.onFo4...` events (6 blockable) | Done | `Fo4GamemodeApiTest`, `Fo4ServerTest` |
| `falloutmp-client` services, `falloutPlatform` contract, skymp bridge | Done, against a fake platform | `npm test` (47 tests, including the C++/TS protocol parity test) |
| fallout4-platform F4SE plugin | Not started (needs Windows) | — |
| PEX FO4 reader (M2 PVM-001…006) | Not started | — |
| Movement (F01): `UpdateMovementFo4` (65), server speed model (walk/sprint/encumbered/PA/jetpack/vertical), per-sample and windowed checks, scored corrections, cell-change rule, history for hit rewind, PA core drain from movement; client capture and interpolated replay | Done | `Fo4MovementTest`, `Fo4ServerTest` [F01], client `movement.test.ts` |
| Effects (F20-T03): `EffectsUpdate` (92) owner full and neighbour visual subset, pushed on use, expiry and join; `getEffects`/`cureAddictions`/`addRads`; client `EffectsService` | Done | `Fo4ServerTest` [F20], `Fo4MessagesTest`, client effects test |
| NPC hosting (F13), first slice: upstream host election (`Host`/`HostStart`/`HostStop`) kept; hosted NPC movement, fire (fire rate enforced, ammo unlimited), hit claims (`HitReport.shooterIdx`) and bounded AV reports (C1) validated like players; owner copies routed to the host; neighbour sends skip the host; Fallout 4 movement refreshes the upstream host-timeout clock; client hosting in movement, combat and actor values | Done | `Fo4ServerTest` [F13], client `hosting.test.ts` |
| F13 NPC data: libespm NPC_/OTFT readers; `NpcResolver` (per-aspect templates via TPTA/TPLT, leveled NPC picks once per spawn, PC level mult with calc min/max, leveled loadout and outfit); NPCs seeded on first touch (level, health, inventory, best gun, outfit, factions); essential/protected/invulnerable damage rules; hosted fire limited to carried guns | Done | `Fo4EspmTest` NPC_ case, `Fo4NpcTest` |
| F13 death items: the INAM leveled list is rolled once on death and lootable from the corpse | Done | `Fo4NpcTest` |
| F13 remainder: `NpcAiState` (111) threat/detection, host migration re-seed, hostility matrix, legendary rolls | Not started | — |
| T2 systems (companions, VATS, stealth, quests, survival) | Not started | — |

Test totals at the last commit: C++ 248 test cases and about 2100 assertions (`./unit/unit "~[espm]"`); client 59 tests.

Guides: [guides/server-admin.md](guides/server-admin.md), [guides/gamemode-api.md](guides/gamemode-api.md), [guides/implementation.md](guides/implementation.md).

## Next actions (for the next session)
1. M2: PEX FO4 reader (PVM-001…006), so server Papyrus can run Fallout 4 scripts.
2. F13 remainder: `NpcAiState` (111), legendary rolls (LTPT/LTPC), hostility from factions.
3. Windows work for the user or CI: PLAT-001+ (the F4SE plugin implementing `falloutPlatform.ts`), then the G-self checks in the verification table below.
4. Still open from planning: user answers to Q-01…Q-19 (05-risks-open-questions.md §2).

## Milestones
| Milestone | State | Evidence |
|---|---|---|
| M0 Foundations | [~] REF-001 done (data tests tagged); ENV scripts in `tools/` | `541425d` |
| M1 Game-pluggable core | [~] profiles, prefix and registry done; REF-004…013 refactors open | `91a92f3` |
| M2 FO4 data on server | [~] FO4 records and data source done; PEX reader open | `91a92f3`, `ea262f4` |
| M3 Platform alive | [ ] needs Windows | |
| M4 Reflection, connect & spawn | [ ] | |
| M5 See each other | [ ] | |
| M6 Items & world | [~] server and client logic done; in-game apply open | `af67519`, `f6c6654`, `b67cecf` |
| M7 Character & status | [~] server and client logic done | `78e6675` |
| M8 Combat — T0 parity alpha | [~] ranged validation and damage done; melee, explosives and VATS open | `45b2c81` |
| M9 Progression & economy | [~] done on the server and client | `78e6675`, `45b2c81` |
| M10 FO4 signature systems | [~] power armor done on the server and client | `e27301c`, `b67cecf` |
| M11 Settlements — T1 beta | [~] workshop server, snapshots, client mirror done | `1f084db`, `b67cecf` |
| M12 1.0 | [ ] | |

## In progress
_(none; the next session starts at "Next actions")_

## Blockers
- Windows CI requires the user to enable GitHub Actions on the fork (Q-08).
- Every in-game verification (G-self/G-manual) requires the user (Q-09).

## Decisions awaiting user
Q-01 … Q-19 (see 05-risks-open-questions.md §2). Proposed ADRs awaiting confirmation: 001, 002, 003, 005, 006, 007, 008, 010–014, 016, 017, 019, 020.

## Decisions log
| Date | Decision | By | Affects |
|---|---|---|---|
| 2026-10-05 | Fully player-run world: no NPC characters and no game factions. Landlords, traders, governments and factions are real players. Keep `npcEnabled` off; NPC hosting (F13) stays in the code but unused. Systems that assumed NPCs (vendors, settler ratings, NPC quest givers, NPC kill XP) need player-run replacements | User | F13, F21, F22, F23, F27, F32 |
| 2026-10-05 | Movement speed limits off by default; human NPCs off by default; ownership models for wasteland buildings under discussion (rent in cities like Keizaal Online) | User | F01, F13, F22 |
| 2026-10-05 | Review pass applied (plan v1.1). Scope: 1.0 = T0+T1, T2 → 1.x. Estimates now computed from the task list (03 capacity model, `tools/falloutmp-plan-stats.py`): ~18–26 months to 1.0. New spec F32 parties/PvP (`PartyAction` 120). Lockpick/hack outcomes server-rolled (F24-T15). Hosted-NPC AV reports bounded (F08-T15). PLAT-020 split a/b; F27-T01 pulled into M4; F13-T01…T04 into M5; GM-013 split from GM-010. New tasks: PLAT-006 patch runbook, SRV-004 signing keys, SRV-005 anomaly scoring, SRV-013 schema migration, CLI-090 actor budget, OPS-004/005, QA-013, DOCS-006, FRONT-007, ENV-017, F00-T11, F09-T14, F31-T11. Risks R23–R26, Q-19 | Claude (planning) | all |
| 2026-10-05 | User states the Commonwealth Online 1.1.0 server package is open source and its code may be used. The package has no license file, so record the Nexus permissions in THIRD_PARTY_LICENSES before verbatim reuse | User | prior-art §3.5.1, OPS-002, OPS-011, QA-020 |
| 2026-10-05 | Scale target corrected to SkyMP parity: ~1,000 concurrent players at 1.0 (compile cap `MAX_PLAYERS=1000`; user has seen 1,200 on SkyMP servers), staged 64 → 300 → 1,000. Added SRV-090…094 and QA-022 | User + Claude | 00 §7, 03, SRV, QA, R22 |
| 2026-10-05 | ADR-021 Accepted: FalloutMP may require/recommend existing mods (F4SE, Address Library, Buffout 4 NG, High FPS Physics Fix, LooksMenu, MCM); policy in 07-dependencies-and-mods.md. Realism pass: overall timeline stated as ~14–20 months to 1.0 | Claude (planning) + user direction | 02, 03, 07, PLAT-095, SRV-003, CLI-080, F03-T12, F22-T26, DOCS-005 |
| 2026-10-05 | Plan v1 adopted as working baseline; ADR-004, ADR-009, ADR-015 and ADR-018 Accepted | Claude (planning) | all |

## Environment notes (as of 2026-10-05)
- The Linux baseline build works with 3 fixes (clang-cpp symlink, autoconf-archive, vcpkg asset script for GitHub-archive 403s). It takes about 25 min cold. See reference/dev-environment.md.
- `unit` without data: 138/171 test cases pass. All 33 failures are untagged Skyrim-data tests (REF-001).
- Docker daemon and `add_repo` were denied by the session permission classifier. Do not retry without user approval (Q-12).
- Caprica builds on Linux (Styyx1 fork + 2-line patch). Skyrim mode works; FO4 mode does not yet. Use Windows CI for FO4 PEX fixtures (ENV-013).

## Facts to verify (collected from research/specs; verify via G-self or D-real, then update the reference doc)
| Fact | Where used | How to verify |
|---|---|---|
| PlayerRef 0x14 / player base 0x7 / Commonwealth 0x3C / Caps 0xF in Fallout4.esm | F00, F04, REF-004 | D-real (ESPM-015) |
| AV form ids (RadResistIngestion 0x2E5 vs 0x2E9; RadResistExposure) | F08 | D-real; resolve by EditorID (ESPM-016) |
| Game-time globals 0x35–0x39 (only TimeScale 0x3A confirmed) | F25 | G-self (`help gamehour 4`) |
| OMOD flag bits (Legendary 0x08 vs 0x10; Mod Collection 0x40 vs 0x80) | F04, F16 | D-real |
| BA2 version 7 vs 8 for GNRL/DX10 in NG/AE | ESPM-013/014 | D-real (accept both) |
| `CookingMenu` is the chem/cooking workbench menu | F15 | G-self (F15-T09) |
| FO4 head node name `"Head"` for nameplates | F31 | G-self |
| Keyboard/mouse input path (raw input vs DirectInput8) | PLAT-042 | G-self |
| libxse vtable IDs on AE (pre-AE numbering) | PLAT-002 | G-self |
| ~30 missing event-source getters | PLAT-040 | G-self |
| Live morph rebuild via `Reset3D` on a live actor | F03 | G-self |
| First-person event and variable behaviour (questions 1–5) | F02 | F02-T01 probe |
| Bullet projectiles hitscan vs missile; fire cadence; ammo decrement timing | F09 | D-real + G-self |
| Crits bypass DR?; explosion falloff; VATS hit-chance formula | F10, F11, F18 | G-manual measurements |
| `LVSG` epic loot chance semantics | F14 | D-real |
| Vendor restock days; respawn timers (168 h / 480 h) | F14, F23 | D-real (GMSTs) |
| DAMA entry size in WEAP/ARMO; OMOD property flag bits | F04, F16 (`Fo4Records.cpp`) | D-real |
| Workshop keyword editor ids and `WorkshopWorkbench` prefix used by the bootstrap | F22 (`Fo4WorldBootstrap.cpp`) | D-real |
| Power armor blocked biped slots `0x3E00` | F17 (`Fo4ServerSettings`) | G-self |
| Fusion core drain rates per movement state | F17 (`PowerArmorSettings`) | G-manual |
| Default weather ids used in guide examples | guides/gamemode-api.md | D-real |
| Player walk/run/sprint/PA/jetpack speeds in units per second | F01 (`MovementSettings`) | G-manual |
| AVIF form id of SpeedMult (now resolved by editor id "SpeedMult" when the setting is 0) | F01 (`movement.speedMultAvId`) | D-real |
| FO4 NPC_ SNAM is 5 bytes (faction + rank) | F13 (`Fo4Records.cpp`) | D-real |
| NPC health formula without DNAM | F13 (`npcHealthBase/PerLevel`) | D-real |

## Deliberate deviations recorded during planning
- F22 coalesces workshop saves to ≤ 1/s per workshop (vs 01-sync-standard §8 rule 4 "next tick").
- F04 makes `stolenFrom` part of item identity (stacking differs from vanilla).
- F07 uses `activationReach` = 300 u + 64 u slack with per-type overrides (SkyMP: 512 containers / 256 furniture).
- F13 spawns essential NPCs as server-invulnerable instead of skipping them (needs Q-14).
- F03 keeps `SetRaceMenuOpen` (29) frozen; the editor mode is an owner-only `looksMenuMode` property.
- F26 `discoveredOnly` fast travel differs from SkyMP (fast travel off); default confirmed by Q-15.
- F24 lockpick/hack outcomes are server-rolled (class A) instead of client minigame results (review C3).
- F08 hosted-NPC AV reports are cause-bounded and can never kill (review C1).

## Deviations recorded during implementation
- F17: worn power armor pieces live in the wearer's worn record, not in the player inventory. They move between the frame and the record only on the client's Ack.
- F22 and others: world state (settlements, frames, locks, parties, containers, clock, weather) is stored in a JSON file (`fo4.worldStatePath`), not in ADR-010 records yet. Writes go through a temporary file and a rename.
- F09: `WeaponFire` gained `clientShotId`, echoed to the shooter only, so hit claims can name the server sequence. This is not in the 01-sync-standard registry text yet.
- F32: party pushes reuse `PartyAction` (op `State` with nonce 0, op `Invite` to the invitee). There is no separate message.
- F22: `WorkshopObjects` snapshots are chunked at `fo4.workshopSnapshotChunk` (200) objects. Wires and scrapped pre-placed refs ride in chunk 0.
- F17: fusion core drain carries a signed sub-step remainder (`pendingDrain`) because 10 Hz movement drains less than one condition step per sample. The remainder isn't persisted (at most 0.1% of a core lost on restart).
- F01: speed limits are off by default (`fo4.movement.enforceSpeed`, user decision 2026-10-05) until speeds are measured in game. Sequence, death and cell-change rules still apply. `CreateActorFo4` doesn't carry the last flags yet.
- F13: human NPCs are off by default (`fo4.npc.humanNpcs`, user decision 2026-10-05). The race comes from the traits template chain, and a leveled template counts as human if any entry is.
- F13: a hosted NPC may only fire guns it carries from its data. Only NPCs whose data has no gun at all take the host's word (`npcTrustHostWeapons`). NPC ammo is unlimited, but fire rate, range and the shot log still apply.
- F13: an NPC's level uses the highest-level player within `npcLevelScanRadius` when it is first touched (vanilla uses the single player). NPC health without DNAM uses `npcHealthBase + npcHealthPerLevel × level` [verify].
- F13: essential NPCs stop at 1 HP, protected NPCs can't be killed by other NPCs, and invulnerable NPCs take no damage.
- Gamemode events are named `onFo4...` (for example `onFo4PvpFlagChange`) rather than the unprefixed names in some specs.
- `claimRule: "gamemode"` blocks every player claim; owners are set only with `mp.fo4.setWorkshopOwner`.

## Evidence log
| Date | Task | Evidence |
|---|---|---|
| 2026-10-05 | F13 NPC data | `Fo4NpcTest` (4 cases), `Fo4EspmTest` NPC_/OTFT; suite 248 cases |
| 2026-10-05 | F13 hosting slice | `Fo4ServerTest` "a host drives its NPC within the same rules"; client `hosting.test.ts` (4 tests); suite 243/2046 |
| 2026-10-05 | F20-T03 effects | `Fo4ServerTest` "effects are pushed on use and on expiry", message round trip; suite 242/2030 |
| 2026-10-05 | F01 movement | `Fo4MovementTest` (9 cases), `Fo4ServerTest` movement and PA drain cases, client `movement.test.ts` (7 tests); suite 241/2003 |
| 2026-10-05 | Settings, gamemode events, guides | `Fo4SettingsTest` (3 cases), `Fo4ServerTest` "gamemode events observe and can block actions"; full suite 230/1879 |
| 2026-10-05 | Client services + parity | `b67cecf`: 47 node tests; parity test parses Fo4Messages.h, MsgType.h and the fo4 enums |
| 2026-10-05 | Equipment, clock, weather, map, fast travel | `77b355a`, 224/1799 |
| 2026-10-05 | Leveled lists, containers, loot, drops | `f6c6654` |
| 2026-10-05 | Gamemode API `mp.fo4` | `eb214cf`, `Fo4GamemodeApiTest`, server tsc clean |
| 2026-10-05 | Bootstrap from the load order | `cd5e26a`, `Fo4WorldBootstrapTest` |
| 2026-10-05 | PartOne routing and persistence | `ada1243`, `Fo4PartOneTest` |
| 2026-10-05 | Server facade | `c237673`, `Fo4ServerTest` |
| 2026-10-05 | Message structs 64–120 | `e49b5cc`, `Fo4MessagesTest` (binary and JSON round trips, Skyrim layout unchanged) |
| 2026-10-05 | libespm data source | `ea262f4`, `Fo4EspmDataSourceTest` |
| 2026-10-05 | Game systems | `af67519`, `e27301c`, `1f084db`, `78e6675`, `45b2c81` with their `Fo4*Test` files |
| 2026-10-05 | Profiles, prefix, registry, FO4 readers | `91a92f3`, `GameProfileTest`, `Fo4EspmTest` |
| 2026-10-05 | REF-001 data tagging | `541425d`: suite green without game data |
| 2026-10-05 | Research | reference/*.md (11 documents) written by research agents; spot-checked by the main session (e.g. NAVM kType bug, GetScriptData noexcept, FO4 lacks SendAnimationEvent/KeepOffsetFromActor in vanilla .psc) |

## Upstream ports
| Date | Upstream commit | What | Ported to |
|---|---|---|---|

## Session log
| Date | Session summary |
|---|---|
| 2026-10-05 | Research (11 references) + full plan v1 written: vision, sync standard, architecture/ADRs, milestones, backlog, 32 feature specs, testing, risks. No code changes. |
| 2026-10-05 | Implementation: server Fallout 4 game layer, PartOne integration and persistence, settings, gamemode API and events, falloutmp-client services with the parity test, admin, gamemode and implementation guides. All Linux tests green. |
| 2026-10-05 | Plan v1.1: two adversarial reviews applied (5 + 5 critical, 15 + 12 major findings). 33 specs, 645 tasks. Added `tools/falloutmp-plan-stats.py`. No code changes. |
