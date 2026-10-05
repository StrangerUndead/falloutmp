# 03 — Milestones

Each milestone lists the work it contains and its **exit criteria**. A milestone is done when every exit criterion has linked evidence in STATUS.md (test output, CI run, self-test JSON, user test report).

**Overall timeline (realism check).** The milestone ranges add up to about 63–101 weeks if run strictly one after another. The server track (M1, M2, and later Linux-only parts of M6–M9) runs in parallel with the platform track (M3–M5), so a realistic single-developer-plus-Claude schedule is **about 14–20 months to 1.0**, with the T0 alpha (M8) at roughly month 8–11. The biggest schedule risks are the animation probe outcome (M4) and CommonLibF4 AE gaps (M3). Re-estimate at every milestone boundary and record it in STATUS.md.

Estimates are for one experienced developer working with Claude:
- Linux-verifiable work is fast.
- Platform/RE work is gated by Windows CI and in-game tests the user runs.

They are rough and are re-estimated at every milestone boundary.

```
M0 ─► M1 ─► M2 ─────────────┐
       └──► M3 ─► M4 ─► M5 ─┴► M6 ─► M7 ─► M8 (T0 parity alpha) ─► M9 ─► M10 ─► M11 (T1 beta) ─► M12 (1.0)
            F02-T01 anim probe runs alongside M3/M4
```

Server-side milestones (M1, M2) and platform milestones (M3–M5) can run in parallel. The server track is Linux-only and fully verifiable by Claude. The platform track needs Windows CI and the user's game.

---

## M0 — Foundations & decisions (est. 1–2 weeks)
- **Environment:** ENV-001, ENV-002, ENV-003, ENV-015, ENV-016; ENV-004/ENV-005 if the user approves.
- **CI:** ENV-010 (needs the user to enable Actions on the fork).
- **Baseline tests:** REF-000, REF-001.
- **Decisions:** get answers to Q-01…Q-13 ([05-risks-open-questions.md](05-risks-open-questions.md)); confirm or adjust the Proposed ADRs (001, 002, 005, 007, 010–014, 016, 017, 019, 020).

**Exit criteria**
1. A fresh session runs the bootstrap and gets `./unit/unit "~[espm]"` fully green with no game data. This needs REF-001.
2. Fork CI builds fork code, or the user has declined CI. In that case, record the alternative verification path.
3. STATUS.md lists every decision as answered or as deferred with a reason.

## M1 — Shared core becomes game-pluggable (est. 3–5 weeks) — Linux only
- **Refactors:** REF-002…REF-013, REF-015, REF-020, REF-021, REF-024, REF-030.
- **Network:** NET-001, NET-002, NET-003, NET-008, NET-009.
- **Build and server:** BUILD-001, BUILD-006, SRV-001 (scaffold), SRV-002, SRV-012, SRV-070.

**Exit criteria**
1. Skyrim behaviour is unchanged: full `ctest` with Skyrim data (CI) and `~[espm]` locally are green, and Skyrim packets are byte-identical (NET-002 test).
2. The server starts with `"game": "fallout4"` and an empty FO4 profile. A bot client with the `fo4-1_` prefix connects; a `7_` client is refused.
3. No Skyrim literals remain outside the profiles (REF-004 grep check).

## M2 — Fallout 4 data on the server (est. 4–6 weeks) — Linux only (+ D-real by user)
- **libespm:** ESPM-001…ESPM-017.
- **Build and data:** BUILD-007, DATA-010.
- **Papyrus VM:** PVM-001…PVM-006, PVM-010, PVM-011, PVM-012; ENV-013 for the fixtures.

**Exit criteria**
1. The synthetic fixture suites (PluginBuilder, BA2, PEX golden files) are green.
2. On the user's machine, `[fo4data]` passes: Fallout4.esm plus DLCs load; AVIF/GMST/FLST lookups by EDID work; sample records per type parse. Load time and memory are recorded.
3. The FO4 PEX golden fixtures parse and execute basic opcodes, including structs, Var and the new array ops.

## M3 — Platform alive in Fallout 4 (est. 4–8 weeks) — Windows CI + G-self
- **Build and CI:** BUILD-002, BUILD-004, BUILD-005, ENV-011, ENV-012 (optional).
- **Platform core:** PLAT-001…PLAT-005, PLAT-010…PLAT-012, PLAT-020, PLAT-021, PLAT-041, PLAT-060, PLAT-061, PLAT-089, PLAT-091, PLAT-095 (dependency detection: Buffout 4, High FPS Physics Fix, LooksMenu, MCM).
- **QA:** QA-010 (first checks), QA-012.
- **In parallel:** F02-T01, the animation probe. It needs PLAT-001 and PLAT-002 and BUILD-002.

**Exit criteria** (from the self-test JSON on the user's machine)
1. The plugin loads on every allow-listed runtime and refuses others cleanly.
2. A JS plugin runs and hot-reloads. `update` and `tick` fire with the correct semantics.
3. The CEF overlay renders and takes input focus.
4. `MpClientPlugin` connects to a Linux server.

## M4 — Reflection, connect & spawn (est. 4–6 weeks)
- **Platform:** PLAT-030…PLAT-036, PLAT-040, PLAT-042, PLAT-050…PLAT-052.
- **Client:** CLI-001, CLI-002, CLI-003, CLI-010, CLI-012, CLI-050, CLI-070, CLI-071, CLI-080 (MCM settings page).
- **Server:** SRV-003 (client-mod allow/deny lists in the manifest).
- **Feature:** F00 (all tasks).
- **Gamemode:** GM-001, GM-010.
- **Animation probe:** F02-T01 must report by the end of M4.

**Exit criteria**
1. `falloutPlatform.ts` is generated, and `sp.Game.getPlayer().getPositionX()` works from JS.
2. A player connects to the Linux server and spawns via the template save at the persisted position. Reconnecting restores the position. No vanilla main-quest (MQ) quests run (F00 tests).
3. The animation probe analysis is complete, and ADR-008 is Accepted or revised.

## M5 — See each other (est. 4–6 weeks)
- **Platform:** PLAT-062, PLAT-070…PLAT-073.
- **Features:**
  - F01 (all);
  - F02-T02…T09;
  - F03 MVP: race, sex and preset-level appearance;
  - F31: nameplates and display names.
- **Client:** CLI-020, CLI-021, CLI-030, CLI-031, CLI-040; F28's "menus must not pause the game" policy task.

**Exit criteria**
1. Two players see each other walk, run, sprint, sneak, jump, swim, draw/holster, and aim. Each is checked in 1st and 3rd person on both sides: the `G-manual` F01 and F02 scripts pass.
2. Vanilla actors are cleaned up. Only server-spawned actors exist.
3. Movement validation unit tests are green, including speed and teleport rejection with correction.

## M6 — Items & world interaction (est. 5–8 weeks)
- **Features:** F04, F05, F06, F07, F14, F30 (chat and admin baseline).
- **Platform:** PLAT-080, PLAT-081.
- **Server:** PVM-013, SRV-030, SRV-070, SRV-080, F25-T01 (server clock, needed by respawn timers and Papyrus timers).
- **Network:** NET-004…NET-007, NET-010, NET-011.
- **Gamemode and QA:** GM-011, GM-012, QA-040.

**Exit criteria**
1. Inventory, equipment (with OMOD instances visible remotely), containers including corpse loot, pickup, drop, doors including load doors and elevators, and leveled lists all reach their target levels.
2. The adversarial duplication suite (QA-040) is green.
3. Chat and admin commands work through the public gamemode.

## M7 — Character & status (est. 4–6 weeks)
- **Features:** F03 (full LooksMenu flow and live morphs), F08, F12, F20 (core effects: stimpaks, chems, rads), F19 (core XP and levels).
- **Server:** SRV-010, SRV-011, SRV-020, SRV-060.
- **Papyrus:** PVM-007, PVM-008, PVM-009.

**Exit criteria**
1. Actor values (Health/AP/Rads/limbs) are server-authoritative with corrections.
2. Death and respawn work for players and NPCs, and `isDead` is broadcast.
3. Chems and radiation behave as the server rules say.
4. XP and levels persist.

## M8 — Combat → **T0 parity alpha** (est. 6–10 weeks)
- **Features:**
  - F09, F10, F11;
  - F13 (hosting with combat, owner election);
  - F18 T0 (VATS disabled);
  - F02-T10 (creature descriptors).
- **Server:** SRV-021 (damage-related entry points), SRV-022, SRV-023, SRV-050.
- **Platform:** PLAT-083.
- **QA:** QA-020, QA-021, QA-050.

**Exit criteria**
1. Every row of the parity matrix ([00-vision-scope.md §3](00-vision-scope.md)) is at SkyMP's level, with evidence.
2. PvP and PvE gunfights work at RTT ≤ 150 ms, and the false-reject rate is under 2% in simulation.
3. A load test with 100 bots and 300 hosted NPCs meets the §7 quality targets.
4. Alpha release package (BUILD-008 draft).

## M9 — Progression & economy (est. 6–10 weeks)
- **Features:** F15, F16, F19 (full perk engine), F23, F24, F25, F26.
- **Server and Papyrus:** SRV-021 (full), PVM-014.
- **Platform:** PLAT-085 (locks/terminals, for F24), PLAT-086 (weather/time, for F25).

**Exit criteria:** crafting, scrapping, modding, perks, vendors, locks, terminals, server time and weather, and map/fast-travel rules are at their target levels.

## M10 — Fallout 4 signature systems (est. 6–10 weeks)
- **Features:** F17 (power armor), F21 (companions), F29 (stealth), F20 survival-mode options, F18 VATS-lite.
- **Platform:** PLAT-084 (power armor; also the PA-station part of F16).

**Exit criteria:** power armor works end to end, including piece health, mods and paint, cores and jetpack, and remotes see it. Companions are owner-hosted. VATS-lite is server-resolved.

## M11 — Settlements → **T1 beta** (est. 8–12 weeks)
- **Features:** F22 T1 (core build, ownership, budget, power, ratings, settlers), F28.
- **Platform and gamemode:** PLAT-087, GM-030.

**Exit criteria**
1. Workshop building persists and scales: 500 objects per settlement and 10 settlements on a server.
2. Every T1 feature is at its target level.
3. Beta release.

## M12 — 1.0 (est. 8–12 weeks)
- **Dependencies & compatibility (ADR-021):** DOCS-005 compatibility matrix finalized; SRV-003 client-mod allow/deny lists documented.
- **Features:** F22 T2 (attacks, supply lines), F27 (quest framework), the rest of F28 (radio sync options).
- **Release work:** OPS-001…OPS-003, OPS-010, DOCS-001…DOCS-011, BUILD-008, PLAT-069 resolved, QA-030 soak test, GM-020, GM-040.

**Exit criteria**
1. Every feature is at its target level.
2. The soak test passes.
3. Docs are complete, the licensing audit is done, and the release is published.

## Post-1.0
PLAT-090 (multi-runtime OG/NG), OPS-011 (NAT relay), DLC-specific content, mod-support policy, and Creations compatibility.
