# CLI — `falloutmp-client` Core

Context: [reference/skymp-sync-inventory.md](../reference/skymp-sync-inventory.md) §1.1; [reference/skyrim-coupling-index.md](../reference/skyrim-coupling-index.md) §3.1 (per-file coupling levels of `skymp5-client/src`); [reference/papyrus-api-map.md](../reference/papyrus-api-map.md) §2 (client API mapping).

**Fork policy:**
- Copy `skymp5-client/` into `falloutmp-client/`.
- Keep the game-agnostic files close to upstream: `spApiInteractor`, `clientListener`, `events`, `networkingService`, `messages`, `idManager`, `model`, `hostAttempts`, auth/settings/timers. This keeps porting fixes easy.
- Rewrite the High-coupling files per feature spec.

- [ ] **CLI-001** Fork and build: `falloutmp-client` webpack build against `falloutPlatform` typings (PLAT-035); CMake target; output `Data/Platform/Plugins/falloutmp-client.js` — M — Depends: PLAT-035 — Verify: L-ts, W-ci
- [ ] **CLI-002** Settings: `falloutmp-client-settings.txt` (server-ip, port, profileId for offline, master URL), `SettingsService` port — S — Depends: CLI-001 — Verify: L-ts
- [ ] **CLI-003** Message types mirror: all FO4 twins/new messages in `src/services/messages/*` and typed events; generated from the registry where possible (NET-012) — M — Depends: NET-002 — Verify: L-ts
- [ ] **CLI-010** First-update game setup for FO4: INI/game settings (`bAlwaysActive`, autosave off, intro skip, difficulty 2), equivalent of SkyMP `index.ts` tweaks — S — Depends: CLI-001 — Verify: G-self
- [ ] **CLI-012** Main-menu UX: auto-hide main menu or a "Connect" flow; intro skip — S — Depends: CLI-001, PLAT-061 — Verify: G-manual
- [ ] **CLI-020** `WorldCleanerService` for FO4: disable/delete vanilla actors not spawned by the server, protect FormViews, skip dialogue/scene actors, keep pre-placed corpses (server decides, F13) — M — Depends: CLI-001 — Verify: G-self
- [ ] **CLI-021** Disable single-player systems: fast travel (unless F26 allows), XP gain (server-driven F19), difficulty selection, save/load (F00), VATS menu (F18), wait/sleep menus (F25), kill moves — M — Depends: CLI-001 — Verify: G-self
- [ ] **CLI-030** Fix the `isDisabled` property key and handle ESM disable updates (I5) — S — Depends: CLI-001 — Verify: L-ts
- [ ] **CLI-031** Route UpdateProperty for ESM-id actors by long id (I6) — S — Depends: CLI-001 — Verify: L-ts
- [ ] **CLI-040** Debug tooling: `show-me`/`show-clones`, net info overlay, anim debug overlay (F02-T11), profiling service — S — Depends: CLI-001 — Verify: G-manual
- [ ] **CLI-050** L-ts harness: Jest/vitest with a mocked `falloutPlatform` module (fake actors/refs/events/clock) so sync logic can be unit-tested on Linux — M — Depends: CLI-001 — Verify: L-ts
  - Accept: the movement interpolation, inventory diff and animation filter suites run in CI.
- [ ] **CLI-060** `BrowserService` FO4 menu list (hide the browser on Pip-Boy, container, barter, workshop, looks, terminal, loading, pause, console), key bindings (F1/F2/F6) — S — Depends: CLI-001, PLAT-061 — Verify: G-manual
- [ ] **CLI-070** `SpSnippetService` FO4 (class/function mapping via `sp3`, Struct/Var args) — S — Depends: PLAT-032 — Verify: L-ts, G-self
- [ ] **CLI-071** `GamemodeUpdateService`/`GamemodeEventSourceService`: pass the `falloutPlatform` module as `ctx.sp`; keep the signature verification — S — Depends: CLI-001 — Verify: L-ts
- [ ] **CLI-080** MCM settings page (ADR-021): server address/port, offline profile, keybinds (nameplates, browser focus), debug overlays; falls back to `falloutmp-client-settings.txt` when MCM is absent — M — Depends: CLI-002, PLAT-095 — Verify: G-manual
- [ ] **CLI-090** Remote-actor budget: server-side interest priority (SRV-091) + client LOD; at most `client.maxVisibleActors` (default 40) fully rendered remotes, chosen by distance/recent-combat/party; beyond that nameplate-only or hidden; distance and occlusion culling; FPS captured at 20/40/64 actors in a G-manual script — M — Depends: F01-T06, SRV-091 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/actorBudgetService.ts
  - Accept: 00 §7 client frame-rate row met on the reference GPU; the 65th actor in a hot spot is nameplate-only.
