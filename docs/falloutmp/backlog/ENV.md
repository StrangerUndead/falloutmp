# ENV — Development Environment & CI

Context: [reference/dev-environment.md](../reference/dev-environment.md). The Linux baseline builds in about 25 min cold (vcpkg ~13 min plus build ~11 min). Without game data, `unit` passes 138 of 171 test cases. All 33 failures are data-dependent tests that lack the `[espm]` tag.

- [ ] **ENV-001** Commit a Linux bootstrap script (`scripts/dev/bootstrap-linux.sh`) implementing dev-environment §6.1: apt bits, clang-cpp symlink, vcpkg submodule + bootstrap, asset script, configure, build `unit` — S — Depends: — — Verify: L-unit — Files: scripts/dev/bootstrap-linux.sh
  - Accept: in a fresh container, one command produces `build/unit/unit`, and `./unit/unit "[PartOne]"` passes.
- [ ] **ENV-002** Commit the vcpkg asset-source workaround (`scripts/dev/vcpkg-github-via-git.sh`, SHA512-verified `git archive` rebuild of GitHub tarballs) with docs — S — Depends: — — Verify: L-unit — Files: scripts/dev/
  - Accept: a configure with `X_VCPKG_ASSET_SOURCES` set succeeds where GitHub archive URLs return 403.
- [ ] **ENV-003** `build.sh`: detect `clang-cpp` (fall back to `/usr/lib/llvm-*/bin/clang-cpp` or `clang -E`) instead of requiring a symlink — S — Depends: — — Verify: L-unit — Files: build.sh
  - Accept: libsodium configures on stock Ubuntu 24.04 clang 18. Candidate for upstreaming.
- [ ] **ENV-004** SessionStart hook for cloud sessions (`.claude/hooks/session-start.sh` + `.claude/settings.json`) — S — Depends: ENV-001 — Verify: L-unit — **Needs user approval** (changes session config)
  - Accept: a new session reaches a built `unit` without manual steps. Hook-time budget documented.
- [ ] **ENV-005** Seed the vcpkg binary cache as a GitHub release asset and download it in the bootstrap — S — Depends: ENV-001 — Verify: L-unit — **Needs user** (release upload / token)
  - Accept: a cold configure is under 3 min with the cache.
- [ ] **ENV-010** Fork CI fixes: `linux-build-base.yml` checks out `${{ github.repository }}`; `pr_base` NuGet feed uses `${{ github.repository_owner }}`; disable upstream deploy/installer/types-update workflows — M — Depends: — — Verify: W-ci — Files: .github/workflows/*, .github/actions/pr_base/action.yml
  - Accept: a PR or push on the fork runs Linux and Windows Skyrim jobs green against fork code. **Needs user** to enable Actions on the fork.
- [ ] **ENV-011** Windows CI job for `fallout4-platform` (sketch in dev-environment §7.3) with artifact upload `fallout4-platform-<sha>` (DLL, PDB, BUILD_INFO.txt) — M — Depends: ENV-010, BUILD-002, PLAT-001 — Verify: W-ci — Files: .github/workflows/pr-windows-fallout4.yml
  - Accept: CI builds the skeleton plugin and uploads it.
- [ ] **ENV-012** Optional: Linux clang-cl + xwin cross-compile check for platform code (LLVM ≥ 19) — M — Depends: BUILD-002 — Verify: L-unit — **Needs user** to accept the Microsoft license
  - Accept: `fallout4-platform` compiles (no link of CEF/node needed) in a Linux session.
- [ ] **ENV-013** FO4 PEX fixture pipeline: compile `unit/papyrus_test_files/fo4/*.psc` with upstream Caprica on Windows CI (or a fixed Linux Caprica); commit the `.pex` — S — Depends: ENV-010 — Verify: W-ci — Files: unit/papyrus_test_files/fo4/, .github/workflows/
  - Accept: golden FO4 `.pex` files are committed together with their sources and a reproducible build command.
- [ ] **ENV-014** Real-data test lane design (`[fo4data]`): self-hosted runner or user-local run script; never publish Bethesda files — S — Depends: ESPM-015 — Verify: D-real — **Needs user**
  - Accept: a documented, working way to run `[fo4data]` tests on the user's machine.
- [ ] **ENV-015** Lint toolchain: `pip install clang-format==21.1.8`; document `linter-config.json` local usage — S — Depends: — — Verify: L-unit
  - Accept: the lint passes on changed files locally.
- [ ] **ENV-016** Upstream remote and merge routine (06-workflow-conventions §3), with the first upstream merge — S — Depends: — — Verify: L-unit
  - Accept: `upstream` remote configured and documented; merge commit builds.
