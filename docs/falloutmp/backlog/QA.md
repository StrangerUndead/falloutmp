# QA — Test Infrastructure (beyond unit tests)

Context: [04-testing-verification.md](../04-testing-verification.md); [reference/dev-environment.md](../reference/dev-environment.md) §9.

- [ ] **QA-001** Keep 04-testing-verification.md current; maintain the test-tag index (`[F01]`… per feature); run `tools/falloutmp-plan-stats.py` (task counts per workstream/milestone, size sums, undefined-ID check, Accept/Files coverage) and paste its output into PLAN.md and STATUS.md at every milestone boundary — S — ongoing
- [ ] **QA-010** In-game **self-test plugin** (`falloutmp-selftest.js` + platform hook PLAT-091): a scripted checklist (platform init, reflection calls, events, spawn remote actor, apply appearance/equipment, inventory read/write, animation probes, overlay draw) → `falloutmp-selftest-<sha>.json` `{check, status, detail, ms}` — M — Depends: PLAT-010 — Verify: G-self
  - Accept: the user runs it with one launch, and Claude parses the JSON report.
- [ ] **QA-011** G-manual test-script library `docs/falloutmp/test-scripts/` (one per feature milestone, with the request template from 06-workflow-conventions §6) — S — ongoing
- [ ] **QA-012** GitHub issue template "In-game test report" (checklist id, sha, pass/fail, logs, self-test JSON) — S — Depends: — — Verify: —
- [ ] **QA-020** Bot load test harness: headless bot clients (MockServer/createBot + raw protocol) simulating movement, combat and inventory; scenarios "hot spot" (N players in one 3×3 area) and "spread" (N players across the map with hosted NPCs); Prometheus capture; first targets 64 hot-spot / 300 spread with 600 NPCs (M8) — M — Depends: F01-T05, F09 — Verify: L-int
- [ ] **QA-022** Scale load tests: 300 players (M11) and 1,000 players with 2,000 hosted NPCs (M12) on a documented reference machine; multi-process bot runner; report tick p95, relay fan-out, bandwidth, memory; results recorded in STATUS.md and 00-vision-scope §7 — M — Depends: QA-020, SRV-090…SRV-093 — Verify: L-int
- [ ] **QA-021** Network-condition emulation (latency, jitter, loss) for bot tests (tc/netem or in-process) — S — Depends: QA-020 — Verify: L-int
- [ ] **QA-030** Soak-test protocol: 2 h, 10 players, crash/reconnect/persistence checks — S — Depends: M8 — Verify: G-manual
- [ ] **QA-040** Adversarial persistence tests: duplication attempts (concurrent take/put, disconnect mid-transfer, crash between mutate and save) — M — Depends: F04, F06 — Verify: L-unit, L-int
- [ ] **QA-050** Two-clients-on-one-PC dev setup (single-instance mutex and Steam DRM research, GOG copy, prior-art C18) for faster user testing; run in M3, outcome recorded as R24/Q-19 — M — Depends: PLAT-001 — Verify: G-manual
  - Accept: a written result "possible via X" or "not possible; second tester required" in STATUS.
- [ ] **QA-013** Crash-report pipeline: the self-test and the client collect Buffout 4 NG crash logs + FalloutPlatform.log + client sha into a zip and open the "In-game test report" issue template (QA-012) prefilled; server side: `crash-reports/` directory with the last 20 reports — S — Depends: QA-010, PLAT-095 — Verify: G-manual
