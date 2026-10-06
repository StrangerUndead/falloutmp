# DATA — Save Files & Archive Utilities

Context: [reference/fo4-data-formats.md](../reference/fo4-data-formats.md) §6–7 (BA2, `.fos`).

- [ ] **DATA-010** Server-side BA2 utilities (list/extract by path; used by ESPM-013 strings and ESPM-014 scripts), built on rsm-bsa master — S — Depends: BUILD-007 — Verify: L-fixture
- [ ] **DATA-020** Read-only `.fos` parser (header, plugin lists incl. light plugins, file location table, global data types, change-form index) for diagnostics and template validation — M — Depends: — — Verify: L-fixture (synthetic) + D-real (template save) — done as F33-T01
  - Accept: it dumps the template save's header, plugin list and quest-related globals.
- [ ] **DATA-030** `.fos` entry patcher, **the primary entry path since ADR-007's amendment (2026-10-06)**: player position/cell/worldspace and game time, with verify-after-write; weather later; the template's own plugin lists are kept, not replaced (reference/fo4-save-entry.md §3). Do **not** patch NPC appearance (undocumented). Specified in F33 §4.5.2 — M — Depends: DATA-020 — Verify: G-self — done as F33-T02
  - Accept: a patched save loads at the target position with the correct time/weather, and quests stay idle.
