# STATUS — FalloutMP

> Update this file at the end of every session (see README §1). Newest entries go at the top of each log.

## Current position
- **Plan version:** 1.0 (2026-10-05)
- **Working branch:** `claude/fallout4-port-research`
- **Current milestone:** **M0 — Foundations & decisions** (not started)
- **Code changes so far:** none. The repo is upstream SkyMP `f926944` plus plan docs.

## Next actions (for the next session)
1. Get answers from the user to the open questions in [05-risks-open-questions.md §2](05-risks-open-questions.md). The most important are Q-02 (runtime), Q-06 (priorities), Q-08 (CI), Q-09 (in-game testing cadence) and Q-10 (SessionStart hook).
2. ENV-001/ENV-002/ENV-003: commit the bootstrap script, the asset script and the build.sh fix. Verify on a fresh container.
3. REF-000/REF-001: baseline test matrix, then tag the 33 data-dependent tests so `ctest` is green without data.
4. ENV-016: add the `upstream` remote; first merge.
5. Start M1 refactors (REF-002, REF-003) and M2 fixtures (ESPM-002) in parallel.

## Milestones
| Milestone | State | Evidence |
|---|---|---|
| M0 Foundations | [ ] | |
| M1 Game-pluggable core | [ ] | |
| M2 FO4 data on server | [ ] | |
| M3 Platform alive | [ ] | |
| M4 Reflection, connect & spawn | [ ] | |
| M5 See each other | [ ] | |
| M6 Items & world | [ ] | |
| M7 Character & status | [ ] | |
| M8 Combat — T0 parity alpha | [ ] | |
| M9 Progression & economy | [ ] | |
| M10 FO4 signature systems | [ ] | |
| M11 Settlements — T1 beta | [ ] | |
| M12 1.0 | [ ] | |

## In progress
_(none)_

## Blockers
- Windows CI requires the user to enable GitHub Actions on the fork (Q-08).
- Every in-game verification (G-self/G-manual) requires the user (Q-09).

## Decisions awaiting user
Q-01 … Q-18 (see 05-risks-open-questions.md §2). Proposed ADRs awaiting confirmation: 001, 002, 003, 005, 006, 007, 008, 010–014, 016, 017, 019, 020.

## Decisions log
| Date | Decision | By | Affects |
|---|---|---|---|
| 2026-10-05 | User states the Commonwealth Online 1.1.0 server package is open source and its code may be used. The package has no license file, so record the Nexus permissions in THIRD_PARTY_LICENSES before verbatim reuse | User | prior-art §3.5.1, OPS-002, OPS-011, QA-020 |
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

## Deliberate deviations recorded during planning
- F22 coalesces workshop saves to ≤ 1/s per workshop (vs 01-sync-standard §8 rule 4 "next tick").
- F04 makes `stolenFrom` part of item identity (stacking differs from vanilla).
- F07 uses `activationReach` = 300 u + 64 u slack with per-type overrides (SkyMP: 512 containers / 256 furniture).
- F13 spawns essential NPCs as server-invulnerable instead of skipping them (needs Q-14).
- F03 keeps `SetRaceMenuOpen` (29) frozen; the editor mode is an owner-only `looksMenuMode` property.

## Evidence log
| Date | Task | Evidence |
|---|---|---|
| 2026-10-05 | Research | reference/*.md (11 documents) written by research agents; spot-checked by the main session (e.g. NAVM kType bug, GetScriptData noexcept, FO4 lacks SendAnimationEvent/KeepOffsetFromActor in vanilla .psc) |

## Upstream ports
| Date | Upstream commit | What | Ported to |
|---|---|---|---|

## Session log
| Date | Session summary |
|---|---|
| 2026-10-05 | Research (11 references) + full plan v1 written: vision, sync standard, architecture/ADRs, milestones, backlog, 32 feature specs, testing, risks. No code changes. |
