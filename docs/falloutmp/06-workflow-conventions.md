# 06 — Workflow & Conventions

## 1. Branches and pull requests

- **Session branch.** Each Claude session works on one branch, named in STATUS.md. Default: `claude/fallout4-port-research` until the user names a long-lived integration branch.
- Never push to `main`. Never open a PR unless the user asks.
- Each pushed branch must build on Linux (`L-unit` green). Once Windows CI exists (`ENV-010`), Windows builds must stay green for the components that are already ported.
- After a PR merges, follow-up work goes on a fresh branch from the updated default branch. Never stack on merged history.
- **Proposed long-term model** (confirm with the user, see Q-03 in [05-risks-open-questions.md](05-risks-open-questions.md)):
  - `main` mirrors releases.
  - `develop` collects milestone work.
  - Short-lived `claude/<topic>` / `feat/<topic>` branches feed `develop`.

## 2. Commits

Follow the upstream style seen in `git log`:
- `feat(<component>): …`, `fix(<component>): …`, `internal: …`, `docs: …`, `chore(<area>): …`
- Components: `libespm`, `papyrus-vm`, `skymp5-server`, `fallout4-platform`, `falloutmp-client`, `falloutmp-front`, `build`, `ci`, `plan`.

Rules:
- One logical change per commit. Plan updates (STATUS.md, task checkboxes) go in the same commit as the code they describe.
- Run the repo linters before committing. They are configured in `linter-config.json`: CRLF check, linelint, clang-format (`.clang-format`), prettier (`.prettierrc.json`).
- End commit messages with the attribution trailer the session instructions specify. Never put a model identifier in a commit, PR, code comment or artifact.

## 3. Upstream SkyMP merges

**Setup:**
```bash
git remote add upstream https://github.com/skyrim-multiplayer/skymp.git
git fetch upstream main
```

**Policy:**
- **When:** merge `upstream/main` into the integration branch at least monthly, and always before starting a milestone.
- **How:** always a merge commit, never a rebase of shared history.
- **Conflicts:** resolve so that both games keep working in the shared code.
- **Forked directories** (`fallout4-platform/`, `falloutmp-client/`, `falloutmp-front/`): do not auto-merge. Review the upstream diffs of `skyrim-platform/`, `skymp5-client/` and `skymp5-front/` and port relevant fixes by hand.
  - Log each port in STATUS.md under "Upstream ports" with the upstream commit hash.
- **Upstreamable work:** pure refactors and bug fixes in shared code are good candidates to offer upstream. Examples: the `NAVM` kType bug, `GetScriptData` noexcept, PEX header validation, the GameProfile seams.
  - Only offer them if the user agrees. Upstream requires signing `CLA.md` (copyright assignment).

## 4. Code conventions

**C++**
- C++20, matching upstream (`CMakeLists.txt` sets the standard).
- Formatting: `.clang-format`. Naming follows the surrounding code: `PascalCase` methods in `server_guest_lib`, `snake_case` files where upstream does.
- The FO4 platform may need C++23 if CommonLibF4 (libxse) requires it. Keep that requirement inside the `fallout4-platform` targets (ADR-003).

**TypeScript**
- Matches `skymp5-client`: services derive from `ClientListener`, the event bus is typed, and `prettier` formatting applies.

**Game abstraction**
- Shared code must not hardcode FO4 or Skyrim specifics. Put them behind `GameProfile` (server) or `espm::Game` (libespm). See ADR-002 and [reference/skyrim-coupling-index.md](reference/skyrim-coupling-index.md).
- Do not add new game-specific `#ifdef`s in shared code. A runtime game selection is preferred.

**New network messages**
- Follow the recipe in [reference/skymp-sync-inventory.md](reference/skymp-sync-inventory.md) (§4).
- Every new message needs binary serialization, a unit test, and an entry in the protocol table in [01-sync-standard.md](01-sync-standard.md) (§6).

**Persistence**
- Every new ChangeForm field needs:
  - a default value;
  - JSON round-trip tests;
  - backward-compatible loading (missing field → default);
  - an entry in the persistence table of the owning feature spec.

## 5. Decision records

Architectural choices go into [02-architecture.md](02-architecture.md) as ADRs.
- Status values: `Proposed`, `Accepted`, `Superseded by ADR-xxx`.
- A `Proposed` ADR that changes user-visible scope or cost must be confirmed by the user before implementation proceeds past its first prototype task. Track it in STATUS.md → "Decisions awaiting user".

## 6. Asking the user to test in game

Use this template, and save each test script under `docs/falloutmp/test-scripts/` (create the folder when the first one is needed):

```
### In-game test request: <task id> — <short title>
Build: <branch> @ <commit>  (CI artifact: <link or path>)
Setup:
  1. Fallout 4 version: Help → About / exe properties (must be one of ADR-001's pinned builds)
  2. Install: copy <dist path> into <Fallout 4 folder>; F4SE 0.7.x; Address Library <version>
  3. Server: <command> with settings <file>
Steps:
  1. …
Expected:
  - …
Collect & send back:
  - <FO4 Documents>/My Games/Fallout4/F4SE/*.log
  - <FO4 Documents>/My Games/Fallout4/F4SE/FalloutPlatform.log (platform log; PLAT-001/PLAT-005)
  - <FO4 Documents>/My Games/Fallout4/F4SE/falloutmp-selftest-<sha>.json (if the self-test plugin ran)
  - server console output
```

When results come back, record them in STATUS.md and mark the task done, or open follow-up tasks.

## 7. Data and legal hygiene

- No Bethesda data in git, in CI caches that are publicly downloadable, or in issue attachments. Real-data tests (`D-real`) run only where the user provides the files.
- Do not use "Bethesda", "Fallout" logos or trademarks in a way that implies endorsement. Project name and branding are decided in Q-01.
- Licenses:
  - Keep `TERMS.md` obligations: AGPLv3 server (public servers must offer source), GPLv3 client/platform.
  - Add every new third-party component to `THIRD_PARTY_LICENSES`.
  - CommonLibF4 (libxse) is GPL-3.0-or-later with a modding exception, which is compatible with a GPLv3 platform. Verify the license text when you vendor it.

## 8. Keeping the plan healthy

- After finishing a task, update its checkbox and STATUS.md, plus any spec whose design changed.
- Discoveries go into the matching `reference/*.md`. Example: a new Address Library ID, a behaviour-graph variable, a CommonLibF4 member offset.
- Prefer appending a dated "Update" note over rewriting history in reference docs, unless something is wrong. If it is wrong, fix it and note the fix.
- Re-estimate milestone sizes at each milestone boundary.
