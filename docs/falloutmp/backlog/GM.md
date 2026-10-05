# GM — Public Default Gamemode `falloutmp-gamemode` (ADR-018)

Upstream's real gamemode is private (`skymp5-functions-lib` only pulls it). FalloutMP ships a public gamemode, so that:
- every server has a working baseline;
- every feature has a reference integration through the `mp` API.

- [ ] **GM-001** Skeleton: TypeScript project `falloutmp-gamemode/`, build to `gamemode.js`, hot reload works with `skymp5-server/ts/index.ts` — S — Depends: — — Verify: L-int
- [ ] **GM-010** Spawn & first-login flow (M4 version): start points and respawn-at-start-point policy; no appearance step yet — S — Depends: GM-001, F00-T04 — Verify: L-int
  - Accept: a new profile spawns at a configured start point; reconnect restores position.
- [ ] **GM-013** First-login appearance and death policy (M7): `setRaceMenuOpen` → LooksMenu editor (F03), death handling policy hooks (F12) — S — Depends: GM-010, F03-T05, F12-T03 — Verify: L-int
- [ ] **GM-011** Chat: local (grid radius) and global channels, `/commands` framework (help, list, roll, tp, kill, kick, ban), via `makeProperty`/`makeEventSource` + front widgets — M — Depends: GM-001, FRONT-003 — Verify: L-int, G-manual
- [ ] **GM-012** Permissions & admin: roles (admin/mod/player) stored in `private.*` properties, console-command permissions, admin panel events — M — Depends: GM-011 — Verify: L-int
- [ ] **GM-020** Reference handlers for every feature event (`onActivate`, `onPutItem`, `onCraft`, `onModItem`, `onWorkshopPlace`, `onBarter`, …) with sample policies (logging only by default) — M — Depends: per-feature L4 tasks — Verify: L-int
- [ ] **GM-030** Rules config: PvP on/off and zones, loot model, survival toggles, XP rate, respawn timers, workshop limits; exposed via server settings + gamemode config file — S — Depends: SRV-002 — Verify: L-int
- [ ] **GM-040** Example quest/event content (F27 framework demo): a radiant "clear location" task with rewards — M — Depends: F27 — Verify: L-int, G-manual
