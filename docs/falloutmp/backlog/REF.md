# REF — Game-Pluggability Refactors (Skyrim stays green)

Source: [reference/skyrim-coupling-index.md](../reference/skyrim-coupling-index.md) §5 (interfaces) and §6 (steps A0–A15, B, C, D). Every step builds, keeps Skyrim behaviour identical (unless marked), and passes `./unit/unit "~[espm]"` locally and full `ctest` with Skyrim data in CI. New code goes in new files (`game_profile/`, `libespm/fo4/`, `views/`); edits to upstream files stay as call-site substitutions.

- [ ] **REF-000** Baseline: record the test list and the pass/fail matrix with and without data (A0) — S — Verify: L-unit — Files: docs/falloutmp/STATUS.md
- [ ] **REF-001** Tag every data-dependent test `[espm]` (A1; about 20 untagged files load Skyrim data through `GetPartOne()`) — S — Depends: REF-000 — Verify: L-unit — Files: unit/*
  - Accept: without data, `ctest` / `./unit/unit "~[espm]"` is fully green. Upstreamable.
- [ ] **REF-002** Crash fixes for foreign data (A2): WEAP DATA null checks, CrimeFactionsList null check, noexcept-throw (Effects, GetScriptData), NAVM 4CC, LIGH offsets, Reader opcode bound, LVLO iteration, case-insensitive strings — M — Depends: REF-001 — Verify: L-unit — Files: libespm/src/*, server_guest_lib/*, papyrus-vm/src/papyrus-vm-lib/Reader.cpp
  - Accept: one test per fix. Upstreamable.
- [ ] **REF-003** GameProfile skeleton (A3): `GameId`, `GameProfile` (GetGameId, DefaultLoadOrder, DefaultWorldspace, ProtocolPrefix), `SkyrimGameProfile`, factory, `WorldState::GetGameProfile()`, `"game"` setting — S — Depends: REF-001 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/game_profile/*, WorldState.*, addon/ScampServer.cpp
- [ ] **REF-004** Move well-known IDs and defaults into the profile (A4): 0x14, 0x7, 0xF, 0x1F4, 0x3c, spawn point, banned races, crime list, reloot defaults, reach, vanilla-form threshold computed from the load order — M — Depends: REF-003 — Verify: L-unit
  - Accept: grep finds no `0x14`/`0x3c` literals outside the profiles.
- [ ] **REF-005** Actor-value catalogue (A5): route H/M/S through `ActorValueCatalogue()`; keep the Skyrim JSON keys — M — Depends: REF-003 — Verify: L-unit (ChangeValues, HealthRestoration, CropRegeneration, Respawn tests)
- [ ] **REF-006** Damage formula factory + hit validation params (A6) — S — Depends: REF-003 — Verify: L-unit (HitTest, TES5DamageFormulaTest)
- [ ] **REF-007** Animation rules as data; SweetPie hacks behind `gamemodeHacks.sweetPie` (auto) (A7) — S — Depends: REF-003 — Verify: L-unit (AnimationSystemTest)
- [ ] **REF-008** Equipment/inventory seams (A8): ShieldSlotMask, IsItemType, ValidateEquipment, extra-data schema per game — M — Depends: REF-003 — Verify: L-unit
- [ ] **REF-009** Condition function table from the profile (A9) — S — Depends: REF-003 — Verify: L-unit (CraftTest, condition tests)
- [ ] **REF-010** Movement validation params and speed model in the profile (for F01) — S — Depends: REF-003 — Verify: L-unit
- [ ] **REF-011** Papyrus seams (A10): native class registration per game, record→script-name map, event names/OnHit args, denied scripts, storage options — M — Depends: REF-003 — Verify: L-unit (Papyrus* tests)
- [ ] **REF-012** NPC spawn filter and synced record types from the profile (A11) — S — Depends: REF-003 — Verify: L-unit
- [ ] **REF-013** Crafting rules from the profile (A12) — S — Depends: REF-003 — Verify: L-unit (CraftTest)
- [ ] **REF-015** Behaviour-changing upstream fixes, each in a separate commit (A2b): SNAM rank offset, npcSettings `default`, stamina crop, temper keyword ToGlobalId — M — Depends: REF-002 — Verify: L-unit
- [ ] **REF-020** ChangeForm schema hardening: per-field defensive JSON parsing (one bad key must not drop the form), DB game meta record, additive-field helpers — M — Depends: REF-003 — Verify: L-unit
- [ ] **REF-021** TS server: `game` setting, FO4 start points, settings defaults per game (A14) — S — Depends: REF-003 — Verify: L-unit (`yarn build`) — Files: skymp5-server/ts/settings.ts, systems/spawn.ts
- [ ] **REF-024** Manifest/UI: `.ba2` archive naming in `manifestGen.ts`, UI blocks `.ba2` (A14) — S — Depends: REF-021 — Verify: L-int
- [ ] **REF-030** `views::*` game-neutral record views; move TES5DamageFormula/EquipBestWeapon/GetWeightFromRecord/GetReach to views — M — Depends: ESPM-001 — Verify: L-unit — (= coupling B4 part 2)
