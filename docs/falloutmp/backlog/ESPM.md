# ESPM — libespm Fallout 4 Support

Context: [reference/fo4-data-formats.md](../reference/fo4-data-formats.md) (layouts, ESL redesign, strings, BA2, PluginBuilder, libespm change list); [reference/skyrim-coupling-index.md](../reference/skyrim-coupling-index.md) §2.1, §5.2.
Design: `espm::Game` enum. Separate `espm::fo4::*` zero-copy record structs that read fields according to the record's form version. Game-neutral `views::*` for server consumers. Skyrim records remain in `espm::`.

- [ ] **ESPM-001** `espm::Game` detection (form version 131/44, HEDR 1.0/0.95/1.7x, INCC, master names); `CombineBrowser::GetGame()`; refuse mixed games — S — Depends: REF-001 — Verify: L-fixture — Files: libespm/include/libespm/Game.h, Loader/TES4/CombineBrowser
  - Accept: fixtures for Skyrim and FO4 headers detect the right game; a mixed load order throws a clear error.
- [ ] **ESPM-002** `unit/PluginBuilder.h`: an in-memory synthetic plugin builder (TES4 + groups + records + subrecords + zlib compression + ESL flag) with example tests — M — Depends: — — Verify: L-fixture
  - Accept: tests build Skyrim-form and FO4-form plugins in memory and parse them with `Browser`.
- [ ] **ESPM-003** ESL/light plugins: TES4 0x200 / `.esl`, FE+12-bit slot+12-bit id; `IdMapping` redesign (global ids = runtime ids); `FormDesc`/`GetFileIdx` changes — L — Depends: ESPM-002 — Verify: L-fixture, L-unit — Files: Combiner, CombineBrowser, utils, mp_common/FormDesc.cpp, WorldState
  - Accept: Skyrim behaviour is unchanged without light plugins; synthetic ESL tests resolve FE ids; FormDesc round-trips light ids.
- [ ] **ESPM-004** VMAD v6: Struct and Var property types, object format 2, never throw inside `noexcept` — M — Depends: ESPM-002 — Verify: L-fixture
  - Accept: VMAD v6 fixtures with Struct and Var properties parse; malformed VMAD returns an error instead of terminating.
- [ ] **ESPM-005** FO4 records batch 1: TES4 (FO4 fields), GMST, GLOB, KYWD, AVIF, FLST, CELL, WRLD, REFR (XTEL, XLKR, XESP, XOWN, XLOC, XSCL, DATA), ACHR, LCTN — M — Depends: ESPM-001 — Verify: L-fixture
  - Accept: every listed record type parses from a synthetic fixture with field-by-field assertions; unknown subrecords are skipped, not fatal.
- [ ] **ESPM-006** FO4 NPC_ (ACBS +0x0E template flags, PRPS, TPLT/TPTA, SNAM 5 bytes, CNTO, DOFT, PNAM, MSDK/MSDV, FMRI/FMRS, TETI/TEND, MWGT, HCLF, QNAM, LTPT/LTPC legendary template & chance) and RACE (200-byte DATA by form version, PRPS, morph groups, tint templates, subgraph data) — L — Depends: ESPM-001 — Verify: L-fixture
  - Accept: NPC_ template flags, PRPS and legendary fields parse; RACE DATA size switches by form version; fixtures cover both.
- [ ] **ESPM-007** FO4 WEAP (132-byte DNAM, DAMA, CRDT, APPR, OBTE, INRD), ARMO (DATA, FNAM, DAMA, 4-byte BOD2, APPR, OBTE), AMMO (DATA weight +4, DNAM), OMOD (DATA with 24-byte property entries, attach points), INNR, object templates OBTE/OBTS — L — Depends: ESPM-001 — Verify: L-fixture
  - Accept: WEAP/ARMO/AMMO/OMOD fixtures parse; OMOD property entries decode to typed values with the correct function (MUL/ADD/SET) and order.
- [ ] **ESPM-008** FO4 COBJ (FVPA, CNAM, BNAM, FNAM, INTV, CTDA), CMPO, MISC (CVPA), CONT, LVLI/LVLN (LVLO with chance-none byte, LVLM, LVLF, LVLG, LLKC, LVSG), OTFT — M — Depends: ESPM-001 — Verify: L-fixture
  - Accept: COBJ components and conditions, LVLI/LVLN entries with chance-none and flags parse from fixtures.
- [ ] **ESPM-009** FO4 ALCH (ENIT, addiction), MGEF (AVIF refs), SPEL, ENCH, PERK (ranks, entry points), HAZD, EXPL, PROJ (Hitscan flag), BPTD — M — Depends: ESPM-001 — Verify: L-fixture
  - Accept: ALCH addiction, MGEF archetypes, PERK ranks/entry points, PROJ hitscan flag parse from fixtures.
- [ ] **ESPM-010** FO4 TERM, NOTE, BOOK, KEYM, DOOR, FURN (WBDT, markers, PA furniture), ACTI, FLOR, LIGH (64-byte DATA), STAT/SCOL/MSTT, PKIN, FACT (vendor data), QUST (brief), WTHR (spells for radstorms) — M — Depends: ESPM-001 — Verify: L-fixture
  - Accept: each listed type parses from a fixture; FURN PA-furniture and TERM menu items are checked field by field.
- [ ] **ESPM-011** FO4 CTDA condition-function index table (complete list from the reference) wired into `ConditionFunctionFactory` via the GameProfile — M — Depends: REF-009, ESPM-008 — Verify: L-unit
  - Accept: the full FO4 CTDA index table is in code; a condition test evaluates three FO4-only functions through the profile.
- [ ] **ESPM-012** `utils::IsItem` per game (adds KEYM, NOTE, FO4 types); `GetRecordsByType` extensions needed by the server — S — Depends: ESPM-005 — Verify: L-fixture
  - Accept: `IsItem` returns true for KEYM/NOTE on FO4 and unchanged results on Skyrim; `GetRecordsByType` serves the new types.
- [ ] **ESPM-013** Localized strings: read `Strings/Fallout4_<lang>.{STRINGS,DLSTRINGS,ILSTRINGS}` from loose files **and** from `Fallout4 - Interface.ba2`; fix the 1-byte length bug and case-sensitivity in `LocalizationProvider` — M — Depends: BUILD-007 — Verify: L-fixture
  - Accept: strings resolve from loose files and from a synthetic BA2; the 1-byte length and case bugs have regression tests.
- [ ] **ESPM-014** Server BA2 script storage + recursive/namespaced directory storage (`A:B` → `A/B.pex`); synthetic BA2 test — M — Depends: BUILD-007, PVM-006 — Verify: L-fixture — Files: skymp5-server/cpp/server_guest_lib/script_storages/*
  - Accept: a script inside a synthetic BA2 and a namespaced `A/B.pex` both load through the storage.
- [ ] **ESPM-015** `[fo4data]` real-data suite: load Fallout4.esm + DLCs, check AVIF ids by EDID, sample records per type, CRCs per version — M — Depends: ESPM-005…010 — Verify: D-real
  - Accept: the suite passes on the user's AE install, with results recorded in STATUS.md.
- [ ] **ESPM-016** Look up GMST/FLST/AVIF by EditorID instead of hardcoded form ids (the AV id ambiguity, e.g. RadResistIngestion 0x2E5 vs 0x2E9) — S — Depends: ESPM-005 — Verify: L-fixture
  - Accept: no hardcoded AVIF/GMST/FLST form ids remain outside the profile; EDID lookup has a test on fixtures.
- [ ] **ESPM-017** Performance: index FO4 REFR by position (exterior cells) and persistent cell children; benchmark Fallout4.esm load time and memory — M — Depends: ESPM-005 — Verify: D-real
  - Accept: load time and peak memory for Fallout4.esm + DLC recorded in STATUS (D-real); position queries for a 3×3 grid run in < 1 ms on the index.
- [ ] **ESPM-018** Workshop/power record data: `BNDS` (wire splines), REFR `XPRM` (primitive), `XWPG`/`XWPN` (workshop power grid/nodes), `XPLK`, `PRPS` on non-NPC base objects (workshop ratings, power) — M — Depends: ESPM-005 — Verify: L-fixture — (needed by F22; F22-T01 tracks it until done)
  - Accept: workshop power records parse from fixtures; `XWPG`/`XWPN` link resolution tested.
- [ ] **ESPM-019** Climate/region weather: `CLMT` (weather list with chances, sun/timing), `REGN` (region weather entries) — S — Depends: ESPM-010 — Verify: L-fixture — (needed by F25-T03)
  - Accept: CLMT/REGN fixtures parse; weather chances sum per region are exposed to F25.
- [ ] **ESPM-020** Encounter zones `ECZN` (min/max level, flags) and location-level links used by leveled actors — S — Depends: ESPM-005 — Verify: L-fixture — (needed by F14-T06)
  - Accept: ECZN fixtures parse; level clamp helper tested.
