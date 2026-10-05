# BUILD — Build System, CommonLibF4, Packaging

Context: [reference/dev-environment.md](../reference/dev-environment.md) §7; [reference/commonlib-port-map.md](../reference/commonlib-port-map.md) (build integration); [reference/skyrim-coupling-index.md](../reference/skyrim-coupling-index.md) §4.

- [ ] **BUILD-001** CMake `GAME` variable (`skyrim` default | `fallout4`), `FALLOUT4_DIR` (validates `Fallout4.exe`), `BUILD_FALLOUT4_PLATFORM`, per-game dist layout and unit data dir — M — Depends: — — Verify: L-unit, W-ci — Files: CMakeLists.txt, cmake/*, unit/CMakeLists.txt
  - Accept: Skyrim configuration is unchanged by default; `-DGAME=fallout4` configures server + libs + unit on Linux.
- [ ] **BUILD-002** vcpkg feature `fallout4-ae` → overlay port `commonlibf4` (libxse, pinned commit; wraps xmake or provides a CMake shim; C++23 limited to platform targets) — L — Depends: BUILD-001 — Verify: W-ci — Files: vcpkg.json, overlay_ports/commonlibf4/
  - Accept: a minimal F4SE plugin target links against CommonLibF4 in Windows CI.
- [ ] **BUILD-003** Address Library handling: check its redistribution license; if forbidden, document user install instead of shipping `version-1-11-*.bin` — S — Depends: — — Verify: — — Files: falloutmp-client-deps/README.md
  - Accept: a decision is recorded in STATUS.md.
- [ ] **BUILD-004** FO4 client packaging: `dist/client/Data/F4SE/Plugins/{FalloutPlatform.dll,MpClientPlugin.dll}`, `Data/Platform/...`, `TESModPlatform.pex` (FO4), template save; `dev_service` launches `f4se_loader.exe` and kills `Fallout4.exe` — M — Depends: PLAT-001 — Verify: W-ci — Files: fallout4-platform/tools/dev_service/*, cmake/*
  - Accept: a CI artifact can be unzipped into a Fallout 4 folder and loads (G-self).
- [ ] **BUILD-005** FO4 Papyrus compile helper: `add_papyrus_library_fo4()` with `Institute_Papyrus_Flags.flg` (6 flags) via CK compiler path or Caprica — S — Depends: ENV-013 — Verify: W-ci — Files: cmake/add_papyrus_library_fo4.cmake
- [ ] **BUILD-006** `generate_server_settings.cmake` FO4 variant: load order (Fallout4.esm + DLCs), `"game": "fallout4"`, start points — S — Depends: REF-021 — Verify: L-unit — Files: cmake/scripts/generate_server_settings.cmake
- [ ] **BUILD-007** Bump overlay port `rsm-bsa` to master (≥ `04d1fdf`) so BA2 v7/v8 can be read — S — Depends: — — Verify: L-unit — Files: overlay_ports/rsm-bsa/
  - Accept: the existing Skyrim BSA tests pass; the synthetic BA2 v8 test (ESPM-014) passes.
- [ ] **BUILD-008** Release packaging for FalloutMP (client zip, server Docker/zip, version stamping, `BUILD_INFO.txt`) — M — Depends: BUILD-004, OPS-001 — Verify: W-ci
- [ ] **BUILD-009** Generate `falloutmp-client-settings.txt` (server ip/port, offline profile) analogous to `generate_client_settings.cmake` — S — Depends: CLI-002 — Verify: L-unit
