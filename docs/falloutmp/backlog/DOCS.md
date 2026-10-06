# DOCS — Documentation & Licensing

- [ ] **DOCS-001** Player install guide (F4SE, Address Library, client dist, supported runtimes, troubleshooting) — S — Depends: BUILD-004
- [ ] **DOCS-002** Server admin guide (FO4 settings reference, load order, data dir, DB) — S — Depends: SRV-002
- [ ] **DOCS-003** Gamemode API reference for FO4: property shapes (`appearance`, `equipment`, `inventory`, `actorValues`, `powerArmor`, `progression`, `workshop`), events and args, `ctx.sp` (falloutPlatform) — M — Depends: per-feature L4
- [ ] **DOCS-004** Contributor guide for FalloutMP (links this plan, build/test, conventions) — S
- [ ] **DOCS-010** Licensing audit: `THIRD_PARTY_LICENSES` additions (CommonLibF4, commonlib-shared, rsm-bsa, Caprica-built fixtures), Tilted code decision (PLAT-069), font licenses in the front, Address Library redistribution (BUILD-003), trademark-safe naming (Q-01) — S — **Needs user** for decisions
- [ ] **DOCS-011** Keep `TERMS.md`-equivalent obligations documented for server operators (AGPL source offer) — S
- [ ] **DOCS-005** Compatibility matrix and recommended-mods list (ADR-021, 07-dependencies-and-mods.md): mod, version, runtime, role, status, owner feature; player and server-admin views — S — Depends: PLAT-095 — Verify: — (kept current every milestone)
- [ ] **DOCS-006** Operator privacy and data-retention note: what a server stores (IPs, profileIds, chat and moderation JSONL from F30-T08, crash reports), default retention, how to purge a player, GDPR-style template text — S — Depends: F30-T08, QA-013
