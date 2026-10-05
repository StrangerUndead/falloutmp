# QA — Test Infrastructure (beyond unit tests)

Context: [04-testing-verification.md](../04-testing-verification.md); [reference/dev-environment.md](../reference/dev-environment.md) §9.

- [ ] **QA-001** Keep 04-testing-verification.md current; maintain the test-tag index (`[F01]`… per feature) — S — ongoing
- [ ] **QA-010** In-game **self-test plugin** (`falloutmp-selftest.js` + platform hook PLAT-091): a scripted checklist (platform init, reflection calls, events, spawn remote actor, apply appearance/equipment, inventory read/write, animation probes, overlay draw) → `falloutmp-selftest-<sha>.json` `{check, status, detail, ms}` — M — Depends: PLAT-010 — Verify: G-self
  - Accept: the user runs it with one launch, and Claude parses the JSON report.
- [ ] **QA-011** G-manual test-script library `docs/falloutmp/test-scripts/` (one per feature milestone, with the request template from 06-workflow-conventions §6) — S — ongoing
- [ ] **QA-012** GitHub issue template "In-game test report" (checklist id, sha, pass/fail, logs, self-test JSON) — S — Depends: — — Verify: —
- [ ] **QA-020** Bot load test: headless bot clients (MockServer/createBot + raw protocol) simulating movement, combat and inventory for 100 players and 300 hosted NPCs; Prometheus capture — M — Depends: F01-T05, F09 — Verify: L-int
- [ ] **QA-021** Network-condition emulation (latency, jitter, loss) for bot tests (tc/netem or in-process) — S — Depends: QA-020 — Verify: L-int
- [ ] **QA-030** Soak-test protocol: 2 h, 10 players, crash/reconnect/persistence checks — S — Depends: M8 — Verify: G-manual
- [ ] **QA-040** Adversarial persistence tests: duplication attempts (concurrent take/put, disconnect mid-transfer, crash between mutate and save) — M — Depends: F04, F06 — Verify: L-unit, L-int
- [ ] **QA-050** Two-clients-on-one-PC dev setup (single-instance patch research, prior-art C18) for faster user testing — S — Depends: PLAT-002 — Verify: G-manual
