# FalloutMP dev environment: building and testing in Claude Code cloud sessions

Verified on 2026-10-05 in a Claude Code cloud container. Repo commit `c16c7b9`, vcpkg submodule `cb2981c4` (vcpkg-tool `2026-03-04`).
The container restarted once mid-session after an API rate limit. The build directory, the scratchpad, `~/.cache/vcpkg` and the system changes (apt packages, the `/usr/bin/clang-cpp` symlink, pip `clang-format`) all survived. The tests ran after the restart against the build made before it.

## 0. TL;DR

| Question | Answer |
|---|---|
| Does the Linux baseline (server + libs + unit tests) build here? | **Yes**, with 3 environment fixes (§3). The cold run took about 12 min for vcpkg deps and 11 min for the build. |
| Do the tests pass? | `unit` (without game data): **138 of 171 test cases pass. All 33 failures need `Skyrim.esm` but lack the `[espm]` tag**, so the auto-skip misses them (§4). All 11 integration tests need `Skyrim.esm`. |
| Docker / CI image route? | **Not available.** The docker CLI exists but no daemon runs, and starting `dockerd` was denied by the session's permission classifier. |
| devcontainer CDN binary cache? | Reachable with curl, but vcpkg's HTTP provider failed with `curl error 35`. Its ABI hashes (clang-15/Debian) could not match ours anyway. **0 hits.** |
| Biggest blocker | The egress proxy returns **403 for `https://github.com/<owner>/<repo>/archive/*`** (34 of 42 vcpkg ports download from there) unless the repo is attached to the session. Workaround in §2.2. |
| Windows-only parts (client / platform DLL) | They can't be compiled here (no MSVC). Use GitHub Actions `windows-2022` (the fork needs CI edits, §7), or try clang-cl + xwin cross-compiling (§7.4: needs LLVM ≥ 19 and a human to accept the MSVC licence). |
| Caprica (FO4 Papyrus compiler) on Linux | Partly. A Linux fork builds with g++-14 plus a 2-line patch. **Skyrim mode works; FO4 mode produced no `.pex` or hung** (§8). |
| Is the Skyrim PapyrusCompiler needed for existing tests? | **No.** Tests use committed `.pex` files, and CK targets auto-skip without `SKYRIM_DIR`. |

Fast path for a new session (details in §6):

```bash
cd /home/user/falloutmp
apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq autoconf-archive flex
[ -e /usr/bin/clang-cpp ] || ln -s /usr/lib/llvm-18/bin/clang-cpp /usr/bin/clang-cpp
git submodule update --init --depth 1 vcpkg
# write the asset script from §2.2 to ~/.local/bin/vcpkg-github-via-git.sh, then:
export X_VCPKG_ASSET_SOURCES="x-script,$HOME/.local/bin/vcpkg-github-via-git.sh {url} {sha512} {dst}"
./build.sh --configure -DBUILD_UNIT_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo   # ~12-14 min cold
./build.sh --build --target unit        # or plain --build for everything (~11 min)
cd build && ./unit/unit "[PartOne]"
```

## 1. Environment facts

| Item | Value |
|---|---|
| Machine | Firecracker VM, kernel 6.18.44, 4 vCPU Xeon 2.8 GHz, 15 GB RAM, no swap, ~30 GB free on `/` |
| OS | Ubuntu 24.04.4 LTS, running as root |
| Compilers | clang/clang++ 18.1.3 (`/usr/bin/clang` → llvm-18), gcc/g++ 13.3.0. Also present: `clang-cl-18`, `lld-link`, `llvm-lib`, `llvm-rc`, `llvm-mt`, `clang-tidy-18`, `clang-format` 18 |
| Build tools | cmake 3.28.3, ninja 1.11.1, autoconf 2.71, automake 1.16.5, libtool 2.4.7, bison 3.8.2, pkg-config 1.8.1, gdb 15.1. vcpkg downloads its own cmake 3.31.10 and ninja 1.13.2 for port builds |
| JS / Python | node 22.22.0, yarn 1.22.22, npm 10.9.4, python 3.11.15 (pip works; pypi.org is in `NO_PROXY`) |
| Missing from the stock image | `autoconf-archive` (libsodium autoreconf), `flex` (installed for parity with the Dockerfile; not proven necessary), the `/usr/bin/clang-cpp` symlink, clang-format ≥ 21 (needed by the repo linter) |
| Docker | CLI 29.6.2 and `dockerd` binaries exist, but there is no daemon socket. Starting the daemon was **denied** by the auto-mode permission classifier ("containment escape"). Treat Docker as unavailable. |
| GitHub | `git` over HTTPS works for any public repo. HTTPS web/API/archive access is limited to repos attached to the session (§2) |

## 2. Network: what is reachable

All outbound HTTPS goes through the agent proxy (`$HTTPS_PROXY`, CA `/root/.ccr/ca-bundle.crt`, already set for git, curl and node).

| Endpoint | Result |
|---|---|
| `git clone/fetch https://github.com/<any public repo>` | OK |
| `https://github.com/<o>/<r>/archive/<ref>.tar.gz`, `codeload.github.com`, `api.github.com` | **403** `"GitHub access to this repository is not enabled for this session. Use add_repo…"`. Works for `StrangerUndead/falloutmp` (attached). `add_repo` for third-party repos was denied by the permission classifier |
| `https://github.com/<o>/<r>/releases/download/...` | OK (vcpkg-tool, Kitware/CMake, Jake-Shadle/xwin, llvm-project, linelint) |
| `raw.githubusercontent.com` | OK |
| `registry.npmjs.org` (direct, NO_PROXY), `archive.ubuntu.com` (apt), `apt.llvm.org`, `pypi.org` | OK |
| `gitlab.com`, `sourceware.org`, `ftp.gnu.org`/`ftpmirror.gnu.org`, `nuget.org`, `boostorg.jfrog.io` | OK |
| `registry-1.docker.io` | Reachable (401 auth challenge), but there is no daemon to use it |
| `aka.ms/vs/17/release/channel` → `download.visualstudio.microsoft.com` | OK (what xwin / msvc-wine use) |
| `sourceforge.net` | 403 |
| `codespaces-vcpkg-binary-cache.b-cdn.net` (devcontainer cache) | curl: OK (404 for unknown keys). vcpkg's HTTP binary provider: `curl error 35 (SSL connect error)` |

### 2.1 Symptom

```
Downloading https://github.com/nic11/antigo/archive/da15297….tar.gz
error: curl operation failed with response code 403.
error: building antigo:x64-linux failed with: BUILD_FAILED
```

### 2.2 Workaround used: a vcpkg asset script that rebuilds GitHub archives with `git`

vcpkg's standard asset-caching hook (`X_VCPKG_ASSET_SOURCES=x-script,…`, meant for restricted networks) runs a script for each download. The script below fetches the pinned ref over git, which the session proxy serves for public repos, and runs `git archive --format=tar | gzip -cn`. **This output was byte-identical to GitHub's tarballs for all 34 GitHub-hosted ports.** The script hands vcpkg a file only if its SHA512 equals the hash pinned in the portfile, and vcpkg verifies the hash again, so integrity is unchanged. For non-GitHub URLs the script exits 1 and vcpkg uses the original URL.

> Judgment call, flagged for the human: this uses the session's sanctioned git read access instead of the HTTPS archive endpoint the proxy blocks. If you'd rather not rely on it, the clean alternatives are: (a) widen the environment's network/GitHub access in the cloud environment settings (Network access → broader level or Custom allowed domains; see https://code.claude.com/docs/en/cloud-environments#network-access), or (b) seed the vcpkg binary cache (§6.3) so sources are never downloaded.

```bash
mkdir -p ~/.local/bin && cat > ~/.local/bin/vcpkg-github-via-git.sh <<'EOF'
#!/usr/bin/env bash
# vcpkg asset source: X_VCPKG_ASSET_SOURCES="x-script,<this> {url} {sha512} {dst}"
# Rebuilds https://github.com/<o>/<r>/archive/<ref>.tar.gz via git (byte-identical) and
# only accepts the result if it matches the SHA512 pinned in the portfile.
set -u
url="$1"; want="${2,,}"; dst="$3"
re='^https://github\.com/([^/]+)/([^/]+)/archive/(.+)\.tar\.gz$'
[[ "$url" =~ $re ]] || exit 1          # not a GitHub archive -> vcpkg uses the original URL
owner="${BASH_REMATCH[1]}"; repo="${BASH_REMATCH[2]}"; ref="${BASH_REMATCH[3]}"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
git -C "$work" init -q || exit 1
git -C "$work" fetch -q --depth 1 --no-tags "https://github.com/$owner/$repo" "$ref" 2>"$work/err" \
  || { echo "vcpkg-github-via-git: fetch failed $owner/$repo@$ref: $(cat "$work/err")" >&2; exit 1; }
# GitHub's top dir is <repo>-<ref> with a leading "v<digit>" stripped and "/" -> "-"
r="${ref#refs/tags/}"; r="${r#refs/heads/}"; cands=()
[[ "$r" =~ ^v[0-9] ]] && cands+=("${r:1}"); cands+=("$r")
for c in "${cands[@]}"; do
  c="${c//\//-}"
  git -C "$work" archive --format=tar --prefix="$repo-$c/" FETCH_HEAD | gzip -cn > "$work/out.tar.gz"
  if [[ "$(sha512sum "$work/out.tar.gz" | cut -d' ' -f1)" == "$want" ]]; then
    mkdir -p "$(dirname "$dst")" && mv "$work/out.tar.gz" "$dst" && exit 0
  fi
done
echo "vcpkg-github-via-git: no candidate matched SHA512 for $url (tried: ${cands[*]})" >&2
exit 1
EOF
chmod +x ~/.local/bin/vcpkg-github-via-git.sh
export X_VCPKG_ASSET_SOURCES="x-script,$HOME/.local/bin/vcpkg-github-via-git.sh {url} {sha512} {dst}"
```

In the log, each fetch shows as `Download successful! Asset cache hit, did not try authoritative source …`. Each one takes about 1-3 s.

## 3. Linux baseline build: what was run, timings, errors and fixes

All commands ran from `/home/user/falloutmp`. `build.sh` picks `clang++-20`, then `clang++-15`, then `clang++`, so here it uses clang 18. It exports CC/CXX/CPP and uses Ninja. On Linux, the CMake config skips the MSVC-only targets by itself: `skyrim-platform`'s `platform_lib`/`platform_se` are inside `if(MSVC)`, `client-deps` is inside `if(WIN32)`, and the CK Papyrus targets print "requires Creation Kit". `skymp5-client` (TypeScript, yarn) **does** build on Linux.

| # | Step / command | Wall time | Result |
|---|---|---|---|
| 1 | `git submodule update --init --depth 1 vcpkg` | 5 s | 72 MB. A shallow clone is fine: the manifest has no `builtin-baseline`, and Windows CI also clones shallow |
| 2 | `VCPKG_DISABLE_METRICS=1 ./vcpkg/bootstrap-vcpkg.sh -disableMetrics` | 1 s | vcpkg-tool binary from GitHub releases |
| 3 | `apt-get update && apt-get install -y autoconf-archive flex` | 10 s | |
| 4 | `VCPKG_BINARY_SOURCES="clear;default,readwrite;http,https://codespaces-vcpkg-binary-cache.b-cdn.net/{sha}.zip,read" ./build.sh --configure -DBUILD_UNIT_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo` | 16 s | **FAIL**: CDN returned 6× `curl error 35`, 0 packages restored, then a **403** on `nic11/antigo` (§2.1) |
| 5 | Same, with `X_VCPKG_ASSET_SOURCES` (§2.2) and no CDN | 131 s | 14 ports OK, then **FAIL libsodium**: `C preprocessor "/usr/bin/clang-cpp" fails sanity check` |
| 6 | Fix: `ln -s /usr/lib/llvm-18/bin/clang-cpp /usr/bin/clang-cpp`, then rerun | 371 s | 13 more ports OK, then **FAIL rsm-binary-io**: `ld: cannot find -lstdc++`. *Self-inflicted*: I installed `g++-14` in parallel, which made clang switch to the GCC 14 directory mid-build. Removed g++-14 again |
| 7 | Rerun configure | 103 s | **OK**: all 41 ports installed, `Configuring done`, `Generating done` |
| 8 | `./build.sh --build` (all targets, 611 Ninja steps, 0 compiler warnings printed) | **659 s** | **OK**: `build/unit/unit` (142 MB, RelWithDebInfo), `build/dist/server` (scam_native.node + dist_back JS), client JS bundle |

The sum of the vcpkg port builds is about 12 min on 4 cores. The largest were openssl 2.4 min, mongo-cxx-driver 1.3 min, libsodium 1.2 min (autotools), catch2 35 s, mongo-c-driver 31 s and cpptrace 27 s. A clean run with the fixes applied up front should take about **13-15 min for configure plus about 11 min for the build, so roughly 25 min total**. Some of the configure time overlapped with Caprica experiments, so treat these numbers as upper bounds.

Disk after the full build: `build/` 1.2 GB (including `vcpkg_installed` 226 MB), `vcpkg/` 276 MB (downloads 176 MB, buildtrees cleaned by `--clean-after-build`), `~/.cache/vcpkg/archives` **52 MB / 41 zips**. Total used went from 8.5 GB to about 12 GB.

Root causes, condensed:

| Error | Cause | Fix |
|---|---|---|
| `curl operation failed with response code 403` on `github.com/…/archive/…` | Session proxy gates GitHub HTTPS per repo | §2.2 asset script (or widen network access / seed binary cache) |
| `C preprocessor "/usr/bin/clang-cpp" fails sanity check` (libsodium configure) | `build.sh` exports `CPP=/usr/bin/clang-cpp`, but Ubuntu 24.04's clang-18 ships only `clang-cpp-18`. The repo Dockerfile creates this symlink itself | `ln -s /usr/lib/llvm-18/bin/clang-cpp /usr/bin/clang-cpp` |
| autotools macro errors (avoided up front) | `misc/deps_linux/ubuntu-2404.sh` lists `autoconf-archive` "for libsodium". It's not in the image | `apt-get install autoconf-archive` |
| `cannot find -lstdc++` | Installing another GCC during a build changes which libstdc++ clang uses | Don't install toolchains while building. Keep the stock GCC 13 |
| Trailing `CMake Error: … "Ninja" … CMAKE_C_COMPILER not set` | Follow-up noise after any vcpkg failure, not a separate problem | Fix the vcpkg error |

## 4. Test results (no game data)

Commands: `cd build && ctest --verbose` (8 s), `./unit/unit "~[espm]"`, and tag runs.

| Run | Result |
|---|---|
| `ctest --verbose` | 1 unit test (`test_unit`) **Failed**. The 11 `integration_*` tests are **Not Run**, because they require the fixture `unit_passed` |
| `./unit/unit` (prints `Skipping tests with [espm] tag`) | **171 test cases: 138 passed, 33 failed**. Assertions: 745 total, 712 passed |
| `./unit/unit "[PartOne]"` | All passed (27 cases, 130 assertions) |
| `./unit/unit "[VirtualMachine]"` | All passed (2 cases) |
| `./unit/unit "[Papyrus]"` | 8 of 9 passed. `PapyrusActorTest.cpp:74` needs data |
| `./unit/unit "[Respawn]"` (the example in `CLAUDE.md`) | **2 of 2 failed**: `Record 0x7 doesn't exist` (player NPC from Skyrim.esm) |
| `./unit/unit "[espm]"` | `No test cases matched '[espm] ~[espm]'`. `main.cpp` appends `~[espm]` when `Skyrim.esm` is missing, and Catch2 ANDs the specs, so `[espm]` tests can't be run without data |

**All 33 failures come from game data missing, not from code bugs.** They throw `Record 0x7 doesn't exist`, `Form with id 0x… doesn't exist` or `FromFormId failed due to invalid file index`, and none of them carry the `[espm]` tag. By file: HitTest 5, ChangeValuesTest 4, MovementValidationTest 3, LoadCellsTest 3, TES5DamageFormulaTest 3, RespawnTest 2, ActivateParentTest 2, and one each in TemplateScriptTest, TemplateInventoryTest, PickUpItemCountTest, PapyrusActorTest, HealthRestorationTest, GetBaseActorValuesTest, DropItemTest, CropRegenerationTest, ConsoleCommandTest, Benchmarks and AnimationSystemTest. Upstream CI always downloads the Skyrim ESMs, so it never sees this.
**Recommended follow-up (small PR): tag these cases `[espm]`** (or make `PartOne` fixtures skip without data). Then `ctest` is green without data and the integration tests run, except that they still need ESMs, see below.

Integration tests (`misc/tests/*.js`) were run with `ctest -FA '.*' -R "integration_test_(crash|registerforsingleupdate)$"`, which bypasses the fixture. The server starts under node 22 and loads `scam_native.node`, then dies with `ENOENT: no such file or directory, open 'data/Skyrim.esm'`. **They cannot pass without Bethesda ESMs.** No Skyrim data was downloaded, on purpose: upstream CI pulls the files from a third-party GitLab mirror, and FalloutMP shouldn't repeat that with `Fallout4.esm`. For FO4, write integration tests against a tiny self-made test plugin, or use private data on a self-hosted or secret-backed runner.

## 5. Inner dev loop

```bash
cd /home/user/falloutmp/build
cmake --build . --target unit            # no-op: 1.6 s
# one edited test .cpp:  ~44 s   (compile + ~30 s link of the 142 MB RelWithDebInfo binary)
# one edited server_guest_lib .cpp (MpActor.cpp): ~33 s
./unit/unit "[PartOne]"                  # tag filter, see TEST_CASE("...", "[Tag]") in unit/*.cpp
./unit/unit "Name of a test case"        # exact name; add -s for passing-assertion output
./unit/unit "~[espm]" --reporter compact # quick list of failures
ctest -R test_unit --output-on-failure
cmake --build . --target skymp5-server   # N-API addon + TS -> build/dist/server
```

- Linking dominates incremental builds. Untested speed-ups: `-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld` (lld 18 is installed), or `-DCMAKE_BUILD_TYPE=Release` for smaller binaries.
- `build/compile_commands.json` is generated, so clangd and `clang-tidy-18 -p build` work.
- Formatting and lint: the CI linter needs **clang-format ≥ 21**. Without one it downloads the whole LLVM 21.1.8 release tarball. Cheaper:
  ```bash
  python3 -m pip install -q clang-format==21.1.8          # 2 s, lands in /usr/local/bin
  mkdir -p .linter && curl -fsSL https://raw.githubusercontent.com/skyrim-multiplayer/linter/b2bddf4b691dc54509cca43b8f2ecdb801692ff5/dist/linter.mjs -o .linter/linter.mjs
  echo "13fba5d3378a4a42d850a86d154c3d780319df66100944a71df9b7fb1f332285  .linter/linter.mjs" | sha256sum -c -
  node .linter/linter.mjs --lint --checks ClangFormat,Linelint,CRLF --files unit/RespawnTest.cpp,unit/PartOneTest.cpp
  ```
  This was verified: it uses `/usr/local/bin/clang-format` 21.1.8 and fetches `linelint` from GitHub releases. Run it from the repo root without `--no-path`, since the CI passes `--no-path` precisely to force the download. `.linter/` is gitignored.

## 6. Bootstrap for future sessions (SessionStart hook proposal; not installed)

### 6.1 Script

Proposed path: `.claude/hooks/session-start.sh`, registered in `.claude/settings.json` under `hooks.SessionStart` with a `"command": "$CLAUDE_PROJECT_DIR/.claude/hooks/session-start.sh"` entry. The hook can read `$CLAUDE_PROJECT_DIR`, `$CLAUDE_ENV_FILE` (append `export …` lines to persist env vars into the session) and `$CLAUDE_CODE_REMOTE` (`true` in cloud sessions). According to the session-start-hook guidance, the cloud container state is cached once the hook completes. That's the main caching lever: whatever the hook builds (apt packages, `vcpkg/`, `~/.cache/vcpkg`, even `build/`) can be reused by later sessions.

```bash
#!/usr/bin/env bash
set -euo pipefail
[ "${CLAUDE_CODE_REMOTE:-}" = "true" ] || exit 0
REPO="${CLAUDE_PROJECT_DIR:-/home/user/falloutmp}"; cd "$REPO"

# 1. system bits missing from the image (~10 s, idempotent)
miss=(); for p in autoconf-archive flex; do dpkg -s "$p" >/dev/null 2>&1 || miss+=("$p"); done
((${#miss[@]})) && { apt-get update -qq; DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${miss[@]}"; }
[ -e /usr/bin/clang-cpp ] || ln -s /usr/lib/llvm-18/bin/clang-cpp /usr/bin/clang-cpp
command -v clang-format >/dev/null && clang-format --version | grep -q 'version 2[1-9]' \
  || python3 -m pip install -q clang-format==21.1.8

# 2. vcpkg (~6 s)
git submodule update --init --depth 1 vcpkg
[ -x vcpkg/vcpkg ] || VCPKG_DISABLE_METRICS=1 ./vcpkg/bootstrap-vcpkg.sh -disableMetrics

# 3. asset script (§2.2 heredoc goes here) + env for the session
#    ... cat > ~/.local/bin/vcpkg-github-via-git.sh <<'EOF' ... EOF ...
AS="x-script,$HOME/.local/bin/vcpkg-github-via-git.sh {url} {sha512} {dst}"
export X_VCPKG_ASSET_SOURCES="$AS" VCPKG_DISABLE_METRICS=1
[ -n "${CLAUDE_ENV_FILE:-}" ] && printf 'export X_VCPKG_ASSET_SOURCES="%s"\nexport VCPKG_DISABLE_METRICS=1\n' "$AS" >> "$CLAUDE_ENV_FILE"

# 4. deps + build (cold ~25 min; with ~/.cache/vcpkg warm: configure ~2 min + build ~11 min)
if [ ! -f build/build.ninja ]; then
  ./build.sh --configure -DBUILD_UNIT_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo > /tmp/falloutmp-configure.log 2>&1
fi
./build.sh --build --target unit > /tmp/falloutmp-build.log 2>&1 || true
```

Two modes:
- **Synchronous** (recommended first): the session is guaranteed ready. A cold start blocks for about 25 min. If the container snapshot is reused later, step 4 becomes incremental (seconds to a few minutes). Check whether the hook timeout allows this. If not, keep steps 1-3 synchronous (under 30 s) and leave step 4 to the agent, run in the background.
- **Async**: print `{"async": true, "asyncTimeout": <ms>}` first. Faster start, but the agent must wait for `/tmp/falloutmp-build.log` to finish before testing.

### 6.2 Expected durations (4 vCPU)

| Phase | Cold | Warm (binary cache present) | Warm (container snapshot with build/) |
|---|---|---|---|
| apt + symlink + submodule + vcpkg bootstrap | ~20 s | ~20 s | ~1 s |
| configure (41 ports + cmake) | ~13-15 min | ~1-2 min (restore 41 zips, estimate) | 0 |
| full build | ~11 min | ~11 min | incremental |
| `unit` target only after configure | most of the 611 steps (server_guest_lib + unit), ~8-9 min (estimate) | same | incremental |

### 6.3 Caching tips

- The vcpkg binary cache lives in `~/.cache/vcpkg/archives` (files provider, default, read-write) and is only **52 MB** for all 41 ports. Hits need the same compiler binary (clang 18.1.3 from this image), triplet, vcpkg commit and port versions. A mismatch just falls back to a source build, so it is safe.
- If container snapshots don't persist, seed the cache: tar `~/.cache/vcpkg/archives`, upload it as a release asset on `StrangerUndead/falloutmp` (named with the vcpkg commit and compiler), and `curl -L` it in the hook. Release downloads are allowed through the proxy, and this avoids vcpkg's HTTP provider, which failed TLS here. The upload needs a human or a token with `contents: write`.
- Don't use `--no-path` for local lint. Don't install other GCC versions mid-build.

## 7. Windows-only parts

### 7.1 What upstream CI does (`.github/workflows/pr-windows-flatrim.yml` → `.github/actions/pr_base`)

`runs-on: windows-2022`, checkout with submodules (depth 1). Steps:
1. `yarn build` of skymp5-client.
2. Install OpenCppCoverage (choco).
3. Move `vcpkg` to `C:/vcpkg`. The top-level CMakeLists **hard-codes** `C:/vcpkg/scripts/buildsystems/vcpkg.cmake` when `CI=true` on Windows.
4. `bootstrap-vcpkg.bat`.
5. NuGet binary cache on GitHub Packages: `VCPKG_BINARY_SOURCES=clear;nuget,GitHub,readwrite`. For pushes the source is `https://nuget.pkg.github.com/skyrim-multiplayer/index.json`, for PRs `…/<github.actor>/…`, both authenticated with `GITHUB_TOKEN`.
6. `cmake -B build -DVCPKG_ROOT=C:/vcpkg -DUNIT_DATA_DIR=skyrim_data_files -DDOWNLOAD_SKYRIM_DATA=ON -DBUILD_NODEJS=OFF -DSKYRIM_VR=… -DCPPCOV_PATH=…`. The generator must be "Visual Studio 17 2022"; CMakeLists fails otherwise under MSVC.
7. `cmake --build build --config Release`, then `ctest -C Release --verbose`.
8. Upload artifacts: dist, client JS, server dist, coverage, SP nexus zip, papyrus-vm nexus.

The default triplet on Windows is `x64-windows-sp` (overlay: static CRT, static libs).

### 7.2 Running CI in the fork `StrangerUndead/falloutmp`

I can't query GitHub from this sub-session; the main session can check with `mcp__github__actions_list`. Requirements:
- Actions in a fork are disabled until the owner enables them in the Actions tab. Scheduled (`cron`) workflows are disabled by default in forks ([GitHub docs](https://docs.github.com/actions/managing-workflow-runs/disabling-and-enabling-a-workflow)).
- `linux-build-base.yml` checks out **`repository: skyrim-multiplayer/skymp`**, so in the fork it would build upstream, not FalloutMP. Change it to `${{ github.repository }}`, or drop the line. Its `auto-merge-action` step only runs with `auto_merge_action_config`.
- `pr_base` pushes the vcpkg NuGet cache to `skyrim-multiplayer`'s feed on push events, which fails with the fork's token. Change it to `${{ github.repository_owner }}` and give the job `permissions: packages: write`. PRs from other forks get a read-only token.
- `SKYMP5_PATCHES_PAT` is only used when `DEPLOY_BRANCH` is set, so the PR jobs don't need it. The deploy/installer workflows (`deploy*.yml`, `get_settings_pins.yml`, `trigger-sp-types-update.yml`, `build-docker-images.yml`) target upstream secrets and repos. Disable or delete them in the fork.
- `DOWNLOAD_SKYRIM_DATA=ON` pulls Skyrim ESMs. Fine for the inherited Skyrim tests, irrelevant for FO4.
- Billing: public repos get free standard runners. A private repo spends included minutes, and Windows minutes are billed at a higher rate than Linux; check current GitHub billing.

### 7.3 What a `fallout4-platform` Windows job needs

- **Runner**: `windows-2022` or `windows-2025` with VS 2022 (v143, a recent 17.x for C++23; CommonLibF4 is C++23). Keep the "Visual Studio 17 2022" generator, or relax the check in the top-level CMakeLists.
- **vcpkg**: the same `C:/vcpkg` move (or remove the hard-coded path), a new manifest feature such as `fallout4-ae` → an overlay port `commonlibf4`, triplet `x64-windows-sp`, and the NuGet cache on the fork owner's feed.
  - `libxse/commonlibf4` (AE 1.11.x IDs) builds with **xmake**: install it via `xmake-io/github-action-setup-xmake` and drive it from the overlay portfile, or vendor a CMake build.
  - `alandtse/CommonLibF4` has CMake/vcpkg but no AE IDs (see `docs/FALLOUT4_PORT_RESEARCH.md` §3.3).
  - The other client deps (cef-prebuilt, node-embedder-api-prebuilt, frida-gum, mhook, directxtk, asio, libarchive) can be reused from the Skyrim feature set.
- **Build only what's needed**: `-DBUILD_SKYRIM_PLATFORM=OFF`, a new `-DBUILD_FALLOUT4_PLATFORM=ON`, `--target fallout4-platform unit`.
- **Tests**: no FO4 data in CI. Keep FO4-data tests tagged and skipped; for ESM tests use a self-hosted runner or an encrypted private artifact (never publish `Fallout4.esm`).
- **Artifacts**: upload `Data/F4SE/Plugins/<Plugin>.dll` + `.pdb`, plus a zip named with the commit SHA for the human tester (§9).
- Sketch:

```yaml
name: PR Windows Fallout 4 platform
on: { pull_request: {}, push: { branches: [main] }, workflow_dispatch: {} }
permissions: { contents: read, packages: write }
env: { VCPKG_BINARY_SOURCES: 'clear;nuget,GitHub,readwrite', BUILD_TYPE: Release }
jobs:
  build:
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4
        with: { submodules: true, fetch-depth: 1 }
      # reuse pr_base's "move vcpkg to C:/vcpkg" + bootstrap + NuGet steps, with
      #   -source https://nuget.pkg.github.com/${{ github.repository_owner }}/index.json
      - run: cmake -B build -G "Visual Studio 17 2022" -A x64 -DVCPKG_ROOT=C:/vcpkg -DBUILD_SKYRIM_PLATFORM=OFF -DBUILD_FALLOUT4_PLATFORM=ON -DBUILD_UNIT_TESTS=ON
      - run: cmake --build build --config Release --target fallout4-platform unit
      - run: ctest --test-dir build -C Release --output-on-failure
      - uses: actions/upload-artifact@v4
        with: { name: fallout4-platform-${{ github.sha }}, path: build/dist/client/Data/F4SE/Plugins/* }
```

### 7.4 Cross-compiling Windows DLLs from Linux (research, not tried)

- Approach: clang-cl + lld-link + llvm-lib/llvm-rc/llvm-mt against the MSVC CRT and Windows SDK, fetched with **xwin** (`xwin --accept-license --arch x86_64 splat --output ~/.xwin`, about 700 MB). This produces real PE/MSVC-ABI DLLs without Wine. Sources: [xwin](https://github.com/Jake-Shadle/xwin) ([guide](https://jake-shadle.github.io/xwin/)), LLVM's [WinMsvc.cmake](https://raw.githubusercontent.com/llvm-mirror/llvm/master/cmake/platforms/WinMsvc.cmake).
- **CommonLibF4 specifically**: [hxef/commonlibf4-linux-cross](https://github.com/hxef/commonlibf4-linux-cross) cross-builds `libxse/commonlibf4` plugins with xmake (`xmake f -p windows -a x64 -m releasedbg --toolchain=xwin-clang-cl`). It ships a custom spdlog package because xmake-repo's cmake path wants Visual Studio. It requires **LLVM ≥ 19**: the MS STL from xwin rejects older clang, and this image has 18. apt.llvm.org is reachable, so newer LLVM can be installed.
- CMake + vcpkg variant: [codepuncher/CommonLibSSE-NG-template](https://github.com/codepuncher/CommonLibSSE-NG-template) uses the `release-linux` preset and a chainloaded `cmake/toolchains/clang-cl-cross.cmake`:
  - `CMAKE_SYSTEM_NAME Windows`
  - `/imsvc` xwin include dirs and `/libpath` lib dirs
  - `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY`
  - `MultiThreadedDLL`, because xwin has no debug CRT
  - TitleCase symlinks for `Advapi32.lib` and similar, since lld-link is case-sensitive
  - an overlay triplet (`VCPKG_CMAKE_SYSTEM_NAME Windows`, `VCPKG_CHAINLOAD_TOOLCHAIN_FILE`, release-only)
- Alternative: [mstorsjo/msvc-wine](https://github.com/mstorsjo/msvc-wine) downloads real MSVC (`vsdownload.py`) and runs `cl.exe` under Wine. Its headers and libs can also feed clang-cl.
- Feasibility here: the tools are present (`clang-cl-18`, `lld-link`, `llvm-lib`, `llvm-rc`, `llvm-mt`). Both xwin release binaries and Microsoft's VS manifest host are reachable.
- **Blockers**: someone has to accept the Microsoft licence; I deliberately did not do that on the user's behalf. LLVM ≥ 19 is needed. And this repo's Windows deps (CEF, node, frida, mhook) were never tried with clang-cl.
- Value: a fast compile check for platform code in a Linux session. Release DLLs should still come from MSVC CI.

## 8. Papyrus toolchain

- **The existing tests don't need the Skyrim PapyrusCompiler.** `unit/papyrus_test_files/pex/*.pex` (3) and `standard_scripts/*.pex` (66) are committed. Skyrim PEX is big-endian, magic `FA57C0DE`, v3.2, game 1. `add_papyrus_library_ck()` only adds a compile step if `${SKYRIM_DIR}/Papyrus compiler/PapyrusCompiler.exe` exists, so it is skipped here. The same applies to `skymp5-scripts`.
- **Caprica** ([Orvid/Caprica](https://github.com/Orvid/Caprica), MIT): upstream is MSVC-only (`Windows.h`, `FindFirstFileA`, `io.h`, MSVC intrinsics, `__m128i` brace-init). A Linux fork exists: [Styyx1/Caprica](https://github.com/Styyx1/Caprica) (tag `1.0.0-linux`, commit `500be9c`, "made possible to compile on linux"; the author built it on CachyOS). Deps come from apt: `libboost-{filesystem,program-options,container}-dev libfmt-dev libpugixml-dev` (10 s).

  | Attempt | Result |
  |---|---|
  | clang 18 or g++ 13 | fails. g++ shows a missing `<algorithm>` include in `FSUtils.cpp`, then a libstdc++ static assertion: the transparent `CaselessIdentifierHasher` deletes `operator()(const char*)`, and libstdc++ ≥ 13 calls it for heterogeneous inserts |
  | g++-14 + `-DCMAKE_CXX_FLAGS="-include algorithm"` | same hasher error |
  | g++-14 + `-include algorithm` + a 2-line patch in `Caprica/common/CaselessStringComparer.h` (lines 75/98: replace the two `= delete` overloads with forwards to the `std::string_view` versions) | **builds in 66 s** (`Caprica` 1.9 MB) |
  | `Caprica -g skyrim -f cmake/TESV_Papyrus_Flags.flg …` | **works**: `.pex` with header `FA57C0DE 0302 0001`, identical in format to the committed test files |
  | `-g fallout4` with our own stub imports (`ScriptObject`/`Form`/`Debug` `.psc`) and a self-written FO4 flags file | **no `.pex` written** (exit 0). `-p` throws `std::runtime_error`, and a trivial script **hung**. The fork's FO4 CLI path doesn't work yet |

  Notes for a retry:
  - The Linux port imports *every* file in import dirs, the cwd included, so keep `.flg` files outside them and use `--ignorecwd`.
  - Caprica only embeds a Starfield flags file, so FO4 needs its own `Institute_Papyrus_Flags.flg` (6 flags: Hidden, Conditional, Default, CollapsedOnRef, CollapsedOnBase, Mandatory).
  - In FO4 mode every script implicitly extends `ScriptObject`, so a stub `ScriptObject.psc` is required. Stubs can be self-written signatures for only the natives we call; don't commit Bethesda's base `.psc` sources.

  **Recommendation**:
  - (a) Short term: compile FO4 test scripts in Windows CI with upstream Caprica (MSVC) or its release exe, then commit the `.pex` like upstream does.
  - (b) Or debug the fork's FO4 path, starting with `main.cpp`'s FO4 base-dir heuristic (`fallout4BaseScriptDirSet`) and the job manager. Wine was not tried.
  - Note that `papyrus-vm` cannot read FO4 PEX (little-endian, v3.9, game 2) yet anyway.

## 9. Verifying Windows code and in-game behaviour (human with Fallout 4)

1. **Compile-level**: every PR runs the Windows job (§7.3). The DLL, PDB and a `BUILD_INFO.txt` (commit SHA, CommonLibF4 commit, targeted runtime) are uploaded as `fallout4-platform-<sha>`.
2. **Plugin logging contract**, to build in:
   - A log at `Documents/My Games/Fallout4/F4SE/FalloutPlatform.log`.
   - First line: `FalloutPlatform <version> <sha> runtime=<1.11.x.y> f4se=<ver>`.
   - Then one line per subsystem: `[INIT] hooks=OK papyrus_natives=OK(n=…) node=OK cef=FAIL(reason)`.
   - An optional `FalloutPlatform.ini` with `[Debug] bSelfTest=1` runs a scripted checklist after the first load, for example a JS `printConsole`, a Papyrus reflection call `Game.GetPlayer().GetPositionX()`, a tick counter after 10 s, and a CEF overlay draw. It writes `falloutmp-selftest-<sha>.json` (`{check, status, detail, ms}` entries).
3. **Human test protocol** (one checklist ID per run, versioned in `docs/falloutmp/testing/`):
   1. Record the game version, F4SE version and Address Library version.
   2. Install the artifact zip from the PR into `Data/F4SE/Plugins/`.
   3. Launch via `f4se_loader.exe` and load the reference save, or start a new game on the self-made test plugin.
   4. Perform the listed steps.
   5. Quit.
4. **Report back** in a GitHub issue using an "In-game test report" template, which the main session can read with the GitHub MCP tools. Include: the checklist ID and commit SHA, pass/fail per step, `FalloutPlatform.log`, `f4se.log`, the self-test JSON and the Buffout 4 crash log if any. Screenshots are optional. Keep raw logs as attachments; Claude parses the JSON.
5. **Multiplayer checks**: the server runs on Linux from `build/dist/server` (`node dist_back/skymp5-server.js`), or in a cloud session if networking allows. Two clients are needed for sync tests, so schedule those as explicit human sessions with a script, for example: "both players at the Sanctuary bridge; player A walks 10 m; B confirms the position within 2 s".

## 10. Appendix

- Scratch artefacts from this run, which survive only as long as the container: `/tmp/claude-0/-home-user-falloutmp/5eb948eb-daed-540d-b329-e0521df78518/scratchpad/`. It holds `timing.log`, `configure{1..4}.log`, `build1.log`, `ctest1.log`, `unit-compact.log`, `vcpkg-github-via-git.sh`, the Caprica clones (`caprica`, `caprica-styyx1`, `caprica-patched`) and `fo4-psc-test/`.
- Port list on x64-linux (41): antigo, brotli, bshoshany-thread-pool, bzip2, catch2, cmakerc, cpp-httplib, cpptrace, directx-headers, directxmath, directxtex, fmt (overlay), libbson, libdwarf, libsodium, libzip, libzippp, lz4, makeid (overlay), mongo-c-driver, mongo-cxx-driver, nlohmann-json, node-addon-api, node-api-headers (overlay), openssl, prometheus-cpp-lite (overlay), robin-hood-hashing, rsm-binary-io, rsm-bsa (overlay), rsm-mmio, simdjson, simpleini, slikenet (overlay), spdlog, utf8proc, vcpkg-cmake, vcpkg-cmake-config, vcpkg-cmake-get-vars, vcpkg-make, zlib, zstd. Non-GitHub sources: bzip2 (sourceware), vcpkg-make (GNU automake), directxmath's `sal.h` (raw.githubusercontent), makeid (local).
- Things not done:
  - nothing committed or modified in tracked files
  - no hook installed
  - no Microsoft licence accepted
  - no game data downloaded
  - Docker not used (daemon start denied)
