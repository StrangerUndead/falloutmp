# FalloutMP Project Plan — START HERE

This folder is the master plan for **FalloutMP**: a Fallout 4 multiplayer framework built from this SkyMP fork. Every system SkyMP syncs, and every Fallout 4 system SkyMP has no counterpart for, must work and sync to the standard in [01-sync-standard.md](01-sync-standard.md).

The plan is written for **future Claude Code sessions**, which start in a fresh, ephemeral Linux container with no memory of earlier sessions. It is meant to be the only context such a session needs. Human contributors can use it the same way.

---

**Plan statistics and ID check:** run `python3 tools/falloutmp-plan-stats.py` from the repo root at every milestone boundary (QA-001). It fails on duplicate or undefined task IDs.

## 1. Session start checklist (do this every time)

1. **Read this file**, then [STATUS.md](STATUS.md). STATUS.md gives the current milestone, the tasks in progress, blockers, and the decisions still waiting on the user.
2. **Check the branch.** Plan work happens on the branch named in STATUS.md (default `claude/fallout4-port-research` until there is a `develop` branch). Run `git fetch` and look at recent commits before changing anything.
3. **Bootstrap the dev environment** using [reference/dev-environment.md](reference/dev-environment.md). Containers are ephemeral, so the build has to be redone in every new session unless a SessionStart hook exists (task `ENV-004`).
4. **Choose work.** Take the next unblocked task of the current milestone from [03-milestones.md](03-milestones.md), [backlog/](backlog/) or the feature specs in [features/](features/). Prefer the lowest ID that is not blocked. Respect `Depends:` lines.
5. **Load context only as needed.** Open the feature spec or backlog item, then the reference docs it links to. Do not re-research what `reference/` already covers. Extend those docs if you learn something new.
6. **Verify** the way the task's `Verify:` field says (see §4). Never mark a task done without that evidence.
7. **Update STATUS.md**: task state, new blockers, what the next session should do. Commit the code and the plan together.

---

## 2. Document map

| File | Purpose | When to read |
|---|---|---|
| [STATUS.md](STATUS.md) | Live progress tracker, decisions log, next steps | Every session |
| [PLAN.md](PLAN.md) | One-file master summary of the whole plan (also shared as a doc) | For an overview, or to share |
| [00-vision-scope.md](00-vision-scope.md) | What "fully functional FalloutMP" means; scope tiers; parity matrix SkyMP→FO4; non-goals; quality targets | Once, and whenever scope is unclear |
| [01-sync-standard.md](01-sync-standard.md) | **The SkyMP Sync Standard (SSS)**: the contract every synced feature must meet, Definition-of-Done levels L0–L4, review checklist | Before designing or reviewing any feature |
| [02-architecture.md](02-architecture.md) | Target architecture, repo layout, component responsibilities, Architecture Decision Records (ADRs) | Before structural changes |
| [03-milestones.md](03-milestones.md) | Milestones M0…M12 with exit criteria and the epics/tasks in each | When choosing work |
| [04-testing-verification.md](04-testing-verification.md) | How to verify on Linux, in Windows CI, and in game through the user; fixtures; self-test plugin; load tests | Before writing tests / asking the user to test |
| [05-risks-open-questions.md](05-risks-open-questions.md) | Risk register and the open questions/decisions that need the user | When blocked or planning |
| [06-workflow-conventions.md](06-workflow-conventions.md) | Branching, commits, upstream SkyMP merges, coding conventions, how to request in-game tests, licensing rules | Before the first commit of a session |
| [07-dependencies-and-mods.md](07-dependencies-and-mods.md) | Policy and candidate list for required/recommended companion mods (F4SE, Address Library, Buffout 4, High FPS Physics Fix, LooksMenu, MCM, …) and mod-compatibility rules | Before platform/appearance/UI work, and for release docs |
| [guides/](guides/) | As-built docs: server admin settings, the `mp.fo4` gamemode API and events, the implementation guide | When running a server, writing a gamemode or extending the code |
| [backlog/](backlog/) | Infrastructure workstreams (ENV, BUILD, REF, ESPM, PVM, DATA, PLAT, NET, SRV, CLI, FRONT, GM, QA, OPS, DOCS) with task IDs | When picking infrastructure work |
| [features/](features/) | One spec per gameplay system (SkyMP parity and FO4-specific), each with its own task list `Fxx-Tnn` | When implementing or reviewing a feature |
| [reference/](reference/) | Deep research: SkyMP internals and sync inventory, CommonLibF4 port map, Papyrus API map, FO4 data formats, PEX/VM, animation, FO4 systems, prior art, coupling index, dev environment | As linked from tasks |
| [../FALLOUT4_PORT_RESEARCH.md](../FALLOUT4_PORT_RESEARCH.md) | First broad survey (Oct 2026) | Background only |

---

## 3. Conventions used in this plan

**Task IDs**
- `ENV-001`, `PLAT-012`, … are infrastructure tasks in `backlog/<PREFIX>.md`.
- `F09-T03` is task 3 of feature spec `features/F09-*.md`.
- `M4` is a milestone. `ADR-007` is a decision record in `02-architecture.md`.

**Task fields**
- `Size`: S ≤ 2 days, M ≤ 2 weeks, L ≤ 6 weeks, XL > 6 weeks of focused work.
- `Depends:` other task IDs that must be done first.
- `Verify:` how to prove the task is done (see §4).
- `Accept:` concrete acceptance criteria.
- `Files:` where the work lands.

**Status markers** (used in STATUS.md and the task lists): `[ ]` todo, `[~]` in progress, `[x]` done (with evidence), `[!]` blocked (say why), `[-]` dropped (say why).

**Provenance marks** (used in reference and spec docs):
- `[src: path:line]`: verified in source code (this repo or a named external repo/commit).
- `[web: URL]`: taken from an external source; re-check it before relying on it.
- `[inference]`: reasoning, not verified.

**Feature levels L0–L4**: defined in [01-sync-standard.md §3](01-sync-standard.md). Every feature has a *target level*. "Parity" means reaching the level SkyMP reaches for the analogous Skyrim feature.

---

## 4. Verification modes (what "done" means for each task)

| Code | Meaning | Who can do it |
|---|---|---|
| `L-unit` | Linux build plus Catch2 unit tests (`./unit/unit "[Tag]"`) | Claude in the container |
| `L-int` | Linux integration test (server process plus scripted bot clients, `misc/tests`-style) | Claude in the container |
| `L-ts` | TypeScript compile, lint and unit tests (Node, mocked game API) | Claude in the container |
| `L-fixture` | Parser tests against synthetic plugin/PEX/BA2 fixtures (no Bethesda data) | Claude in the container |
| `W-ci` | Windows MSVC build in GitHub Actions (platform DLLs, client bundle) | Claude, by pushing and reading CI results |
| `G-self` | In-game self-test plugin run by the user; Claude reads the JSON report it produces | Needs the user (Fallout 4 on Windows) |
| `G-manual` | Manual in-game scenario following a written test script; the user reports results and logs | Needs the user |
| `D-real` | Tests against real `Fallout4.esm`/DLC data (tag `[fo4data]`, skipped when data is absent) | Needs the user's data locally or a private runner |

Claude **cannot** run Fallout 4, and it cannot (yet) build Windows DLLs locally. For anything needing `G-*`, write a test script ([04-testing-verification.md §5](04-testing-verification.md)), push a build, and ask the user to run it. Record the result in STATUS.md.

---

## 5. Hard rules

1. **Never commit Bethesda game data**: no `.esm`/`.esl`/`.esp` from the game, no `.ba2`, no vanilla `.pex`/`.psc` bodies, no extracted assets. Synthetic fixtures generated by our own code are fine.
2. **Keep the tests green.** Every change must keep `./unit/unit` and the client tests passing on Linux. Skyrim support was removed (STATUS.md decisions log, 2026-10-05), so older plan text about keeping Skyrim behaviour no longer applies.
3. **License hygiene.**
   - The server is AGPLv3. The client (`falloutmp-client`) is GPLv3. `libespm`, `papyrus-vm` and `viet` are MIT.
   - Copy third-party code only if its license is compatible, and record it in `THIRD_PARTY_LICENSES`.
4. **No model identifiers** in commits, PRs or code. Commit trailers follow the session's attribution instructions.
5. **Don't open PRs or push to `main`** unless the user asks. Work on the session branch.
6. **Update the plan when reality differs.** If a task turns out wrong, edit the plan in the same commit as the code and log the change in STATUS.md's decision log.
