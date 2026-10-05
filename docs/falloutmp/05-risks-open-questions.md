# 05 — Risks & Open Questions

## 1. Risk register

| ID | Risk | L | I | Mitigation | Owner task |
|---|---|---|---|---|---|
| R1 | First-person animation capture is insufficient: the 3rd-person graph is parked in first person | H | H | Hybrid strategy D (ADR-008); prototype probe first; parked-graph driver as fallback | F02-T01, F02-T12 |
| R2 | CommonLibF4 (libxse) AE coverage gaps: vtable IDs still use pre-AE numbering, ~30 missing event sources, missing hkb types | H | H | RTTI vtable finder; local type definitions; contribute fixes upstream; alandtse fallback | PLAT-002, PLAT-040, F02-T02 |
| R3 | Bethesda patches break the Address Library or offsets (AE has had six hotfixes in a year) | M | H | Runtime allow-list; ID-only addressing (no raw RVAs); fast refresh procedure (PLAT-006 runbook, ≤ 7 days); clear refusal UX | PLAT-003 |
| R4 | SP3 reflection port is hard (StackFrame without inline args, std::function dispatch, IStackCallbackFunctor with 6 virtuals) | M | H | Stage it (static, then member, then latent); fallback to F4SE static registration plus Dispatch calls | PLAT-031 |
| R5 | Template-save world entry leaks quest or world state | M | M | Quest guard; `.fos` patcher fallback | F00-T05, DATA-030 |
| R6 | Gun hit validation without server collision geometry, so line of sight can't be checked | H | M | Lag-compensated rewind, fire-rate/ammo checks, statistical anomaly detection; validation level setting; navmesh-based LOS later (F09-T14, 1.x) | F09, SRV-023 |
| R7 | Settlement scale (hundreds of objects per settlement) overwhelms streaming and persistence | M | H | Chunked decor snapshots; only interactive objects become full forms; budgets; load tests | F22 |
| R8 | Tilted UI/hook code is "All Rights Reserved" | M | M | Decide early (Q-04); replacement overlay task | PLAT-069 |
| R9 | FO4 keyboard/mouse input path (raw input vs DirectInput8) is unknown, which affects overlay input capture | M | M | Investigate in PLAT-042; window-message path | PLAT-042 |
| R10 | Project size: XL across many systems | H | H | Strict tiering (T0/T1/T2); milestone exits; Linux-first parallel tracks | 03-milestones |
| R11 | In-game verification depends on the user's time | H | M | Self-test plugin automation; batched test requests; two clients on one PC | QA-010, QA-050 |
| R12 | Legal/trademark exposure (name, assets, reused Commonwealth Online code whose Nexus permissions are not in the package) | L | H | Free, no assets, no Bethesda data in git/CI; naming decision Q-01; record Nexus permissions in THIRD_PARTY_LICENSES before verbatim reuse | DOCS-010 |
| R13 | Live face-morph sync via Reset3D is unverified on FO4 | M | M | Early self-test of an appearance round trip; fallback: respawn the actor with a new TESNPC | F03, PLAT-082 |
| R14 | Upstream SkyMP divergence makes merges painful | M | M | New code in new files; monthly merges; upstream the refactors | ENV-016, ENV-017 |
| R15 | Container environment drift (GitHub archive 403s, missing packages) | M | L | Bootstrap script, asset-script workaround, cached binaries | ENV-001…005 |
| R16 | Real FO4 data is unavailable in CI | C | M | Synthetic fixtures; `[fo4data]` lane on the user's machine | ESPM-002, ENV-014 |
| R17 | Users' extra mods conflict with sync | M | M | Server-defined load order and manifest check; documented "clean install" | F00-T09 |
| R18 | Power armor on Actor puppets is unsolved by prior art | M | H | Dedicated prototype in F17; engine enter sequence on remote copy | F17 |
| R19 | Leveled-actor determinism across clients | M | M | Server evaluates and sends the resolved base; TPTA handled server-side | F14, F13 |
| R20 | Rate limits / usage caps interrupt long agent research or implementation sessions | M | L | Smaller agent batches; resume agents; commit often | process |
| R22 | 1,000-player parity target may not be reachable with FO4's heavier actors and gun-fire message volume in a single process | M | H | Engineer for scale from M1 (SRV-090…093); staged load tests 64 → 300 → 1,000 (QA-020/022); sharding design as fallback (SRV-094) | SRV-09x, QA-022 |
| R23 | Windows build path unavailable or slow: private-fork Actions minutes cost 2×, the upstream Windows build takes hours with vcpkg, and ENV-013/BUILD-005/PLAT-021/PVM-010 all route through Windows CI because Caprica FO4 mode fails on Linux (a single point of failure) | M | H | Make ENV-012 cross-compile (xwin) a real fallback instead of optional (Q-11); vcpkg binary cache (ENV-005); fix Caprica FO4 on Linux as a stretch task; self-hosted Windows runner on the user's PC as last resort | ENV-011, ENV-012, ENV-005, ENV-013 |
| R24 | Two FO4 instances on one PC may be impossible (Steam DRM, single-instance mutex; GOG differs), so every multiplayer test needs a second person | M | M | QA-050 research first in M3; fallback: bot clients for everything server-side, user + one friend for G-manual; GOG copy as a second instance | QA-050, Q-19 |
| R25 | Signing-key compromise or loss for gamemode client snippets lets a hostile server run arbitrary JS in clients, or locks an operator out of their own gamemode | L | H | SRV-004 keygen/rotation runbook; keys never in git; per-server keys; clients only trust the key from the server they connected to | SRV-004 |
| R26 | ESM memory footprint of Fallout4.esm + DLC + 2,000 NPC change forms in one 1,000-player process exceeds the reference machine | M | M | ESPM-017 benchmark in M2; lazy chunk loading (SRV-093); strings table on demand; reference machine 32 GB | ESPM-017, SRV-093 |
| R21 | A recommended mod (LooksMenu, Buffout 4, High FPS Physics Fix, MCM) lags behind a Bethesda patch or changes its API | M | M | Native fallbacks for sync-critical paths; version ranges in the manifest; compatibility matrix updated per release | PLAT-095, SRV-003, DOCS-005 |

L/I = likelihood/impact: H high, M medium, L low, C certain. (I19 from 01 §4 has no risk row because it is documentation-only; see reference §3.2.)

## 2. Open questions — need the user

| ID | Question | Default if unanswered | Blocks |
|---|---|---|---|
| Q-01 | Project name and branding: is "FalloutMP" acceptable, given the trademark? | Keep "FalloutMP" internally; choose a public name before any release | DOCS-010, M12 |
| Q-02 | Target runtime: AE 1.11.x latest only, or also NG 1.10.984 / OG 1.10.163 at launch? | AE only (ADR-001); multi-runtime post-1.0 | PLAT-002 |
| Q-03 | Branch model: create `develop`, and do you want PRs into your fork? | Keep working on `claude/fallout4-port-research`; no PRs unless asked | process |
| Q-04 | Tilted UI code: keep as upstream does, or plan a replacement? | Keep for development; decide before public release | PLAT-069 |
| Q-05 | Online auth/master server: host one (where)? Discord login? | Offline mode only until M8 | OPS-002 |
| Q-06 | Gameplay priorities: PvP vs PvE focus; settlements vs survival priority; target player count | PvE+PvP both, settlements at T1, survival optional at T2 (1.x), ~1,000 players at 1.0 (00 §7), PvP default `flagged` (F32) | scope |
| Q-07 | Template save: may we commit a template `.fos`, or must it be generated per user? | Generate per user via a documented procedure | F00-T01 |
| Q-08 | Enable GitHub Actions on the fork (incl. Windows minutes)? | Needed for W-ci; otherwise the user builds locally | ENV-010/011 |
| Q-09 | Can you run in-game tests (and provide Fallout4.esm locally for `[fo4data]`)? How often? | Assume weekly batches | M2–M12 |
| Q-10 | Approve the SessionStart hook and a vcpkg binary-cache release asset? | No hook; bootstrap manually (~25 min) | ENV-004/005 |
| Q-11 | Accept the Microsoft MSVC/SDK license via xwin for Linux cross-compiles? | No cross-compiles; rely on CI | ENV-012 |
| Q-12 | Widen this environment's network policy for GitHub archive downloads, or attach repos (add_repo was denied by the permission classifier)? | Keep using the git-based asset script | ENV-002 |
| Q-14 | Essential/protected NPCs (vendors, quest givers): spawn them as server-invulnerable (F13 default) instead of SkyMP's skip? | Spawn as invulnerable | F13, F23 |
| Q-15 | Fast-travel default: `discoveredOnly` (F26) or off (SkyMP parity)? | `discoveredOnly` | F26 |
| Q-19 | Can two Fallout 4 instances run on your PC (Steam vs GOG copy, single-instance mutex)? If not, who is the second tester and how often are they available? | Assume a second person weekly; bots for everything else | QA-050, R24, M5–M12 G-manual |
| Q-16 | Sleep/wait: allow the T2 option "everyone sleeping advances the clock"? | Off; instant rest disabled | F25 |
| Q-17 | Quests: shared world quest state only, or also per-player instanced? | Shared only for T2; instancing later | F27 |
| Q-18 | Companion-mod policy: OK to **require** Buffout 4 NG, High FPS Physics Fix and LooksMenu on the client (vs recommend only)? | Recommend; require only F4SE + Address Library | 07-dependencies-and-mods.md |
| Q-13 | Upstream fixes to SkyMP (requires signing their CLA: copyright assignment)? | Don't upstream | ENV-016 |

## 3. Decisions log
Record answers in STATUS.md → "Decisions" with the date, then update the affected ADR status.
