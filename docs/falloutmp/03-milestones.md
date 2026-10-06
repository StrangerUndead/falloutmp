# 03 — Milestones

Each milestone lists the work it contains and its **exit criteria**. A milestone is done when every exit criterion has linked evidence in STATUS.md (test output, CI run, self-test JSON, user test report).

**Capacity model (how estimates are derived).** Estimates are computed from the task list, not set by hand. Rule: `milestone weeks = Σ task size midpoints × throughput multiplier + (G-gated round trips × cadence)`, with size midpoints S = 1 day, M = 1 week, L = 4 weeks, XL = 8 weeks (README §3). Assumed throughput: Linux-verifiable work (L-unit/L-int/L-ts/L-fixture) runs at **~4× a solo developer** because Claude implements and tests it inside one session; Windows-CI and in-game work (W-ci/G-self/G-manual/D-real) runs at **~1×** and is additionally gated by the user's test cadence (Q-09 default: one batch per week). The sum of all 617 tasks is about 640 developer-weeks at midpoints; roughly 60% is Linux-verifiable (≈ 100 weeks at 4×) and 40% is gated (≈ 250 weeks at 1×, partly parallel with the Linux track). Each milestone below shows its computed range; re-run the computation at every milestone boundary and record it in STATUS.md (`tools/falloutmp-plan-stats.py`, QA-001).

**Overall timeline.** Scope for **1.0 = T0 + T1** (parity with SkyMP plus the Fallout-4 systems every server needs). **T2 systems ship in 1.x** (F18 VATS-lite, F21 companions, F27 quest framework, F29 stealth, F22 attacks/supply lines, F20 survival needs); their specs are complete so work can start when 1.0 is stable. With that scope, the two tracks in parallel give **about 18–26 months to 1.0**, with the T0 alpha (M8) at roughly month 9–12 and the T1 beta (M11) at month 15–21. The earlier 14–20 month figure assumed hand-set milestone ranges and is superseded. The biggest schedule risks are the animation probe outcome (M4), CommonLibF4 AE gaps (M3), and the Windows build path (R23).

Estimates are for one experienced developer working with Claude:
- Linux-verifiable work is fast.
- Platform/RE work is gated by Windows CI and in-game tests the user runs.

They are rough and are re-estimated at every milestone boundary.

```
M0 ─► M1 (server core) ─► M2 (FO4 data) ──────────┐
 └──► M3 (platform alive) ─► M4-platform (reflection) ─┴► M4 exit (connect & spawn) ─► M5 ─► M6 ─► M7 ─► M8 (T0 alpha)
                 F02-T01 anim probe runs alongside M3/M4                               ─► M9 ─► M10 ─► M11 (T1 beta) ─► M12 (1.0) ─► 1.x (T2)
```

M1 runs in parallel with M3, and M2 with the platform half of M4. **The M4 exit requires the M1 and M2 exits**: F00 depends on ESPM-001…010, REF-003 and NET-001; CLI-001 depends on PLAT-035, which depends on PVM-002; PLAT-089 depends on NET-001. The server track is Linux-only and fully verifiable by Claude. The platform track needs Windows CI and the user's game.

---

## M0 — Foundations & decisions (computed 1–2 weeks: 12 S tasks, Linux + one CI round trip)
- **Environment:** ENV-001, ENV-002, ENV-003, ENV-015, ENV-016; ENV-004/ENV-005 if the user approves.
- **CI:** ENV-010 (needs the user to enable Actions on the fork).
- **Baseline tests:** REF-000, REF-001.
- **Decisions:** get answers to Q-01…Q-19 ([05-risks-open-questions.md](05-risks-open-questions.md)); confirm or adjust the Proposed ADRs (001, 002, 003, 005, 006, 007, 008, 010–014, 016, 017, 019, 020).

**Exit criteria**
1. A fresh session runs the bootstrap and gets `./unit/unit "~[espm]"` fully green with no game data. This needs REF-001.
2. Fork CI builds fork code, or the user has declined CI. In that case, record the alternative verification path.
3. STATUS.md lists every decision as answered or as deferred with a reason.

## M1 — Shared core becomes game-pluggable (computed 4–6 weeks: 14 S, 10 M, 1 L at 4×) — Linux only
- **Refactors:** REF-002…REF-013, REF-015, REF-020, REF-021, REF-024, REF-030.
- **Network:** NET-001, NET-002, NET-003, NET-008, NET-009.
- **Build and server:** BUILD-001, BUILD-006, SRV-001 (scaffold), SRV-002, SRV-012, SRV-070, SRV-090 (scale budget baseline: `maxPlayers` up to 1,000 as a setting, per-tick metrics).

**Exit criteria**
1. Skyrim behaviour is unchanged: full `ctest` with Skyrim data (CI) and `~[espm]` locally are green, and Skyrim packets are byte-identical (NET-002 test).
2. The server starts with `"game": "fallout4"` and an empty FO4 profile. A bot client with the `fo4-1_` prefix connects; a `7_` client is refused.
3. No Skyrim literals remain outside the profiles (REF-004 grep check).

## M2 — Fallout 4 data on the server (computed 5–8 weeks: 12 S, 17 M, 3 L at 4×, plus two D-real round trips) — Linux only (+ D-real by user)
- **libespm:** ESPM-001…ESPM-017.
- **Build and data:** BUILD-007, DATA-010.
- **Papyrus VM:** PVM-001…PVM-006, PVM-010, PVM-011, PVM-012; ENV-013 for the fixtures.

**Exit criteria**
1. The synthetic fixture suites (PluginBuilder, BA2, PEX golden files) are green.
2. On the user's machine, `[fo4data]` passes: Fallout4.esm plus DLCs load; AVIF/GMST/FLST lookups by EDID work; sample records per type parse. Load time and memory are recorded.
3. The FO4 PEX golden fixtures parse and execute basic opcodes, including structs, Var and the new array ops.

## M3 — Platform alive in Fallout 4 (computed 10–16 weeks: 5 S, 9 M, 3 L mostly at 1×, plus ~6 G-self round trips; the probe F02-T01 runs alongside) — Windows CI + G-self
- **Build and CI:** BUILD-002, BUILD-004, BUILD-005, ENV-011, ENV-012 (optional).
- **Platform core:** PLAT-001…PLAT-005, PLAT-010…PLAT-012, PLAT-020a, PLAT-021, PLAT-041, PLAT-060, PLAT-061, PLAT-089, PLAT-091, PLAT-095 (dependency detection: Buffout 4, High FPS Physics Fix, LooksMenu, MCM).
- **QA:** QA-010 (first checks), QA-012.
- **In parallel:** F02-T01, the animation probe. It needs only BUILD-002 (standalone plugin with its own ~10 IDs) and starts in week 1.

**Exit criteria** (from the self-test JSON on the user's machine)
1. The plugin loads on every allow-listed runtime and refuses others cleanly.
2. A JS plugin runs and hot-reloads. `tick` fires every frame (PLAT-020a); the Papyrus-safe `update` is deferred to M4 (PLAT-020b).
3. The CEF overlay renders and takes input focus.
4. `MpClientPlugin` connects to a Linux server.

## M4 — Reflection, connect & spawn (computed 8–12 weeks: 8 S, 14 M, 2 L, half gated; requires M1 and M2 exits)
- **Platform:** PLAT-020b, PLAT-030…PLAT-036, PLAT-040, PLAT-042, PLAT-050…PLAT-052.
- **Client:** CLI-001, CLI-002, CLI-003, CLI-010, CLI-012, CLI-050, CLI-070, CLI-071, CLI-080 (MCM settings page).
- **Server:** SRV-003 (client-mod allow/deny lists in the manifest).
- **Feature:** F00 (all tasks), F33-T01…T14 (the entrance: writer, load call, template, two-phase join), F27-T01 (vanilla quest policy; the only F27 task before 1.x).
- **Gamemode:** GM-001, GM-010.
- **Animation probe:** F02-T01 must report by the end of M4.

**Exit criteria**
1. `falloutPlatform.ts` is generated, and `sp.Game.getPlayer().getPositionX()` works from JS.
2. A player starts the game, joins from the main menu and enters through a generated save (F33) at the persisted position, with one loading screen. Reconnecting restores the position. No vanilla main-quest (MQ) quests run (F00 and F33 tests).
3. The animation probe analysis is complete, and ADR-008 is Accepted or revised.

## M5 — See each other (computed 8–12 weeks: F01/F02/F03-MVP/F31/F13-T01…T04 ≈ 20 S, 18 M, 3 L; ~6 G-manual round trips)
- **Platform:** PLAT-062, PLAT-070…PLAT-073.
- **Features:**
  - F01 (all);
  - F02-T02…T09;
  - F03 MVP: race, sex and preset-level appearance;
  - F31: nameplates and display names;
  - F13-T01…T04: server-spawned NPCs with AI suppressed, no hosting yet (needed by F06 corpse loot and F14 in M6);
  - F33-T15…T22: entrance polish (main-menu rows, dialogs, full save guard, interiors, appearance under the curtain).
- **Client:** CLI-020, CLI-021, CLI-030, CLI-031, CLI-040; F28's "menus must not pause the game" policy task.

**Exit criteria**
1. Two players see each other walk, run, sprint, sneak, jump, swim, draw/holster, and aim. Each is checked in 1st and 3rd person on both sides: the `G-manual` F01 and F02 scripts pass.
2. Vanilla actors are cleaned up. Only server-spawned actors exist.
3. Movement validation unit tests are green, including speed and teleport rejection with correction.

## M6 — Items & world interaction (computed 8–12 weeks: ≈ 30 S, 30 M, 4 L, two thirds Linux-verifiable)
- **Features:** F04, F05, F06, F07, F14, F30 (chat and admin baseline), F24-T02 (lock state for doors), F25-T04 (clock message).
- **Platform:** PLAT-080, PLAT-081.
- **Server:** PVM-013, SRV-030, SRV-080, F25-T01 and F25-T04 (server clock message; SRV-070 itself lands in M1).
- **Network:** NET-004…NET-007, NET-010, NET-011.
- **Gamemode and QA:** GM-011, GM-012, QA-040.

**Exit criteria**
1. Inventory, equipment (with OMOD instances visible remotely), containers including corpse loot, pickup, drop, doors including load doors and elevators, and leveled lists all reach their target levels.
2. The adversarial duplication suite (QA-040) is green.
3. Chat and admin commands work through the public gamemode.

## M7 — Character & status (computed 7–10 weeks: ≈ 20 S, 22 M, 5 L, 1 XL started (SRV-021 core))
- **Features:** F03 (full LooksMenu flow and live morphs), F08, F12, F20 (core effects: stimpaks, chems, rads), F19 (core XP and levels).
- **Server:** SRV-010, SRV-011, SRV-020, SRV-060.
- **Gamemode:** GM-013 (appearance and death policy).
- **Papyrus:** PVM-007, PVM-008, PVM-009.

**Exit criteria**
1. Actor values (Health/AP/Rads/limbs) are server-authoritative with corrections.
2. Death and respawn work for players and NPCs, and `isDead` is broadcast.
3. Chems and radiation behave as the server rules say.
4. XP and levels persist.

## M8 — Combat → **T0 parity alpha** (computed 10–14 weeks: ≈ 20 S, 26 M, 6 L; load tests and G-manual gunfight scripts gate the exit)
- **Features:**
  - F09, F10, F11;
  - F13 (hosting with combat, owner election);
  - F18 T0 (VATS disabled);
  - F02-T10 (creature descriptors);
  - F29-T04 (server-side sneak-attack validity for F11; the rest of F29 is 1.x);
  - F32-T01…T04 (parties, friendly fire, hostility).
- **Server:** SRV-021 (damage-related entry points), SRV-022, SRV-023, SRV-050.
- **Platform:** PLAT-083.
- **QA:** QA-020, QA-021, QA-050.

**Exit criteria**
1. Every row of the parity matrix ([00-vision-scope.md §3](00-vision-scope.md)) is at SkyMP's level, with evidence.
2. PvP and PvE gunfights work at RTT ≤ 150 ms, and the false-reject rate is under 2% in simulation.
3. Load tests meet the §7 targets at the alpha stage: 64 players in one area and 300 players spread over the map with 600 hosted NPCs (QA-020).
4. Alpha release package (BUILD-008 draft).

## M9 — Progression & economy (computed 10–14 weeks: ≈ 30 S, 35 M, 4 L, remainder of SRV-021)
- **Features:** F15, F16, F19 (full perk engine), F23, F24, F25, F26, F32-T05…T09 (XP sharing, markers, party UI).
- **Server and Papyrus:** SRV-021 (full), PVM-014.
- **Platform:** PLAT-085 (locks/terminals, for F24), PLAT-086 (weather/time, for F25).

**Exit criteria:** crafting, scrapping, modding, perks, vendors, locks, terminals, server time and weather, and map/fast-travel rules are at their target levels.

## M10 — Fallout 4 signature systems (computed 6–9 weeks: F17 + F20 T1 options ≈ 10 S, 12 M, 2 L; T2 specs deferred to 1.x)
- **Features:** F17 (power armor), F20 T1 options (addiction, radiation storms); F05-T06 (PA/equipment interplay).
- **Deferred to 1.x (T2):** F21 (companions), F29 (stealth), F18 VATS-lite, F20 survival needs. Their specs stay complete; start them only when M12 is done or a server operator sponsors them.
- **Platform:** PLAT-084 (power armor; also the PA-station part of F16).

**Exit criteria:** power armor works end to end, including piece health, mods and paint, cores and jetpack, and remotes see it. F20 T1 options behave per the server rules.

## M11 — Settlements → **T1 beta** (computed 10–14 weeks: F22 T1 + F28 ≈ 12 S, 16 M, 4 L; 300-player load test)
- **Features:** F22 T1 (core build, ownership, budget, power, ratings, settlers), F28.
- **Platform and gamemode:** PLAT-087, GM-030.

**Exit criteria**
1. Workshop building persists and scales: 500 objects per settlement and 10 settlements on a server.
2. Every T1 feature is at its target level.
3. Load test at 300 concurrent players meets the §7 targets (QA-022).
4. Beta release.

## M12 — 1.0 (computed 6–9 weeks: release, docs, licensing, soak and 1,000-player load test; no new gameplay systems)
- **Dependencies & compatibility (ADR-021):** DOCS-005 compatibility matrix finalized; SRV-003 client-mod allow/deny lists documented.
- **Features:** the rest of F28 (radio sync options); F32 remainder (PvP zones). F22 T2 (attacks, supply lines) and F27 (quest framework) move to 1.x.
- **Release work:** OPS-001…OPS-003, OPS-010, DOCS-001…DOCS-011, BUILD-008, PLAT-069 resolved, QA-030 soak test, GM-020, GM-040.

**Exit criteria**
1. Every T0 and T1 feature is at its target level (T2 is tracked under 1.x).
2. The soak test passes, and the 1,000-player load test meets the §7 targets (QA-022, SRV-090…093).
3. Docs are complete, the licensing audit is done, and the release is published.

## 1.x — T2 systems (post-1.0)
F18 VATS-lite, F21 companions, F27 quest/dialogue framework, F29 stealth, F22-T22…T25 (attacks, supply lines), F20 survival needs, F09-T14 (navmesh line of sight), SRV-094 (sharding) if needed.

## Post-1.0 (other)
PLAT-090 (multi-runtime OG/NG), OPS-011 (NAT relay), OPS-012 (host GUI), DLC-specific content, mod-support policy, and Creations compatibility.
