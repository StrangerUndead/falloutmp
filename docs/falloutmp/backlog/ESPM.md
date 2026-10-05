# ESPM — libespm Fallout 4 Support

Context: [reference/fo4-data-formats.md](../reference/fo4-data-formats.md) (layouts, ESL redesign, strings, BA2, PluginBuilder, libespm change list); [reference/skyrim-coupling-index.md](../reference/skyrim-coupling-index.md) §2.1, §5.2.
Design: `espm::Game` enum. Separate `espm::fo4::*` zero-copy record structs that read fields according to the record's form version. Game-neutral `views::*` for server consumers. Skyrim records remain in `espm::`.

- [ ] **ESPM-001** `espm::Game` detection (form version 131/44, HEDR 1.0/0.95/1.7x, INCC, master names); `CombineBrowser::GetGame()`; refuse mixed games — S — Depends: REF-001 — Verify: L-fixture — Files: libespm/include/libespm/Game.h, Loader/TES4/CombineBrowser
- [ ] **ESPM-002** `unit/PluginBuilder.h`: an in-memory synthetic plugin builder (TES4 + groups + records + subrecords + zlib compression + ESL flag) with example tests — M — Depends: — — Verify: L-fixture
  - Accept: tests build Skyrim-form and FO4-form plugins in memory and parse them with `Browser`.
- [ ] **ESPM-003** ESL/light plugins: TES4 0x200 / `.esl`, FE+12-bit slot+12-bit id; `IdMapping` redesign (global ids = runtime ids); `FormDesc`/`GetFileIdx` changes — L — Depends: ESPM-002 — Verify: L-fixture, L-unit — Files: Combiner, CombineBrowser, utils, mp_common/FormDesc.cpp, WorldState
  - Accept: Skyrim behaviour is unchanged without light plugins; synthetic ESL tests resolve FE ids; FormDesc round-trips light ids.
- [ ] **ESPM-004** VMAD v6: Struct and Var property types, object format 2, never throw inside `noexcept` — M — Depends: ESPM-002 — Verify: L-fixture
- [ ] **ESPM-005** FO4 records batch 1: TES4 (FO4 fields), GMST, GLOB, KYWD, AVIF, FLST, CELL, WRLD, REFR (XTEL, XLKR, XESP, XOWN, XLOC, XSCL, DATA), ACHR, LCTN — M — Depends: ESPM-001 — Verify: L-fixture
- [ ] **ESPM-006** FO4 NPC_ (ACBS +0x0E template flags, PRPS, TPLT/TPTA, SNAM 5 bytes, CNTO, DOFT, PNAM, MSDK/MSDV, FMRI/FMRS, TETI/TEND, MWGT, HCLF, QNAM) and RACE (200-byte DATA by form version, PRPS, morph groups, tint templates, subgraph data) — L — Depends: ESPM-001 — Verify: L-fixture
- [ ] **ESPM-007** FO4 WEAP (132-byte DNAM, DAMA, CRDT, APPR, OBTE, INRD), ARMO (DATA, FNAM, DAMA, 4-byte BOD2, APPR, OBTE), AMMO (DATA weight +4, DNAM), OMOD (DATA with 24-byte property entries, attach points), INNR, object templates OBTE/OBTS — L — Depends: ESPM-001 — Verify: L-fixture
- [ ] **ESPM-008** FO4 COBJ (FVPA, CNAM, BNAM, FNAM, INTV, CTDA), CMPO, MISC (CVPA), CONT, LVLI/LVLN (LVLO with chance-none byte, LVLM, LVLF, LVLG, LLKC, LVSG), OTFT — M — Depends: ESPM-001 — Verify: L-fixture
- [ ] **ESPM-009** FO4 ALCH (ENIT, addiction), MGEF (AVIF refs), SPEL, ENCH, PERK (ranks, entry points), HAZD, EXPL, PROJ (Hitscan flag), BPTD — M — Depends: ESPM-001 — Verify: L-fixture
- [ ] **ESPM-010** FO4 TERM, NOTE, BOOK, KEYM, DOOR, FURN (WBDT, markers, PA furniture), ACTI, FLOR, LIGH (64-byte DATA), STAT/SCOL/MSTT, PKIN, FACT (vendor data), QUST (brief), WTHR (spells for radstorms) — M — Depends: ESPM-001 — Verify: L-fixture
- [ ] **ESPM-011** FO4 CTDA condition-function index table (complete list from the reference) wired into `ConditionFunctionFactory` via the GameProfile — M — Depends: REF-009, ESPM-008 — Verify: L-unit
- [ ] **ESPM-012** `utils::IsItem` per game (adds KEYM, NOTE, FO4 types); `GetRecordsByType` extensions needed by the server — S — Depends: ESPM-005 — Verify: L-fixture
- [ ] **ESPM-013** Localized strings: read `Strings/Fallout4_<lang>.{STRINGS,DLSTRINGS,ILSTRINGS}` from loose files **and** from `Fallout4 - Interface.ba2`; fix the 1-byte length bug and case-sensitivity in `LocalizationProvider` — M — Depends: BUILD-007 — Verify: L-fixture
- [ ] **ESPM-014** Server BA2 script storage + recursive/namespaced directory storage (`A:B` → `A/B.pex`); synthetic BA2 test — M — Depends: BUILD-007, PVM-006 — Verify: L-fixture — Files: skymp5-server/cpp/server_guest_lib/script_storages/*
- [ ] **ESPM-015** `[fo4data]` real-data suite: load Fallout4.esm + DLCs, check AVIF ids by EDID, sample records per type, CRCs per version — M — Depends: ESPM-005…010 — Verify: D-real
  - Accept: the suite passes on the user's AE install, with results recorded in STATUS.md.
- [ ] **ESPM-016** Look up GMST/FLST/AVIF by EditorID instead of hardcoded form ids (the AV id ambiguity, e.g. RadResistIngestion 0x2E5 vs 0x2E9) — S — Depends: ESPM-005 — Verify: L-fixture
- [ ] **ESPM-017** Performance: index FO4 REFR by position (exterior cells) and persistent cell children; benchmark Fallout4.esm load time and memory — S — Depends: ESPM-005 — Verify: D-real
