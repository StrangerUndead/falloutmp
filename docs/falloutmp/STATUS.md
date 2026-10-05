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
Q-01 … Q-13 (see 05-risks-open-questions.md §2). Proposed ADRs awaiting confirmation: 001, 002, 003, 005, 006, 007, 008, 010–014, 016, 017, 019, 020.

## Decisions log
| Date | Decision | By | Affects |
|---|---|---|---|
| 2026-10-05 | Plan v1 adopted as working baseline; ADR-004, ADR-009, ADR-015 and ADR-018 Accepted | Claude (planning) | all |

## Environment notes (as of 2026-10-05)
- The Linux baseline build works with 3 fixes (clang-cpp symlink, autoconf-archive, vcpkg asset script for GitHub-archive 403s). It takes about 25 min cold. See reference/dev-environment.md.
- `unit` without data: 138/171 test cases pass. All 33 failures are untagged Skyrim-data tests (REF-001).
- Docker daemon and `add_repo` were denied by the session permission classifier. Do not retry without user approval (Q-12).
- Caprica builds on Linux (Styyx1 fork + 2-line patch). Skyrim mode works; FO4 mode does not yet. Use Windows CI for FO4 PEX fixtures (ENV-013).

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
