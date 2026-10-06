# FRONT — `falloutmp-front` (in-game browser UI)

Context: skymp5-front is a React/Redux app with Skyrim-themed components. The bridge uses `window.skyrimPlatform.sendMessage` and `executeJavaScript` widgets (reference/skymp-sync-inventory.md; coupling index §3.2).

- [ ] **FRONT-001** Fork `skymp5-front` → `falloutmp-front`; rename the `window.skyrimPlatform` bridge to `window.falloutPlatform` (keep an alias); CMake output `Data/Platform/UI` — S — Depends: — — Verify: L-ts (build)
- [ ] **FRONT-002** Pip-Boy/terminal visual theme: replace `SkyrimFrame`/`SkyrimButton`/`SkyrimInput`/`SkyrimHint`/`SkyrimSlider`, fonts (licensing check), sounds — M — Depends: FRONT-001 — Verify: G-manual
- [ ] **FRONT-003** Login widgets (offline/online), chat widget parity (local/global channels, commands), notifications — M — Depends: FRONT-001, GM-011 — Verify: G-manual
- [ ] **FRONT-004** Server list / connect screen (with OPS-002) — M — Depends: OPS-002 — Verify: G-manual
- [ ] **FRONT-005** Admin panel (player list, kick/ban, teleport) for gamemode admins — M — Depends: GM-012 — Verify: G-manual
- [ ] **FRONT-006** HUD extras: nameplates styling, party/team indicators (F32), PvP status, workshop permission prompts; accessibility: font scale setting and colour-blind-safe nameplate palettes — S — Depends: F31 — Verify: G-manual
- [ ] **FRONT-007** UI localization: string table for `falloutmp-front` and the client HUD texts, language from the game INI, English + community translations — S — Depends: FRONT-003 — Verify: L-ts
