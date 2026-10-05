# F00 — Session Lifecycle, World Entry & Streaming

| Field | Value |
|---|---|
| Tier | T0 |
| Target level | L4 |
| SkyMP analogue | Connect → login → spawn → `CreateActor isMe` → template save load → grid streaming. SkyMP level L4 (reference/skymp-sync-inventory.md §1.3–1.5) |
| Milestone | M4 (connect & spawn), M5 (streaming of others) |
| Workstreams | PLAT, CLI, SRV, NET, GM, DATA |
| Depends on | PLAT-001…PLAT-020b (platform alive), ESPM-001…ESPM-010 (Fallout4.esm loads), REF-003 (GameProfile skeleton), NET-001 (protocol prefix) |
| References | reference/skymp-sync-inventory.md §1.3–1.5, §1.10; reference/prior-art.md §5.1 (B13, C1, C2), §5.3 Adopt 1/8; reference/fo4-data-formats.md (.fos); reference/dev-environment.md §9 |

## 1. Summary
A player launches Fallout 4 with F4SE and the FalloutMP client. The client connects to the server and logs in (offline `profileId` or online session). The player then lands in the Commonwealth at their persisted position, or at a start point for new characters, with their persisted inventory, appearance and actor values. While moving, nearby players, NPCs and world objects stream in and out exactly like SkyMP: grid 4096, 3×3. Disconnecting disables the actor; reconnecting restores it.

## 2. Vanilla Fallout 4 behaviour (relevant facts)
- A new game starts with the pre-war intro and Vault 111. The main quest (MQ101/MQ102) and its scripts drive everything around Sanctuary/Vault 111. They must not run in MP (ADR-011).
- Save files are `.fos` (`FO4_SAVEGAME`). Loading goes through `BGSSaveLoadManager` (singleton OG 1247320 / NG-AE 2697802 per reference/prior-art.md §5.2). Saves can be refused or forced by hooking the save/load lifecycle (prior-art Adopt 8).
- The player is `PlayerRef` 0x14 with base 0x7 (verify in Fallout4.esm: coupling index lists them as `[inference]`). Commonwealth worldspace is `0x3C` in Fallout4.esm (verify).
- Menus such as MainMenu and LoadingMenu exist. The game pauses in many menus; unpause by clearing IMenu flags (prior-art Adopt 10).

## 3. SkyMP baseline
- Login: `ts/systems/login.ts`, `ts/systems/spawn.ts`. Offline mode emits `spawnAllowed` directly.
- `PartOne::SetUserActor` → `ForceSubscriptionsUpdate` → `CreateActor isMe=true` (skymp-sync-inventory §1.3 step 8).
- The client: `remoteServer.ts` `onCreateActorMessage`. If in the main menu, `LoadGameService.loadGame(pos, rot, worldOrCell, changeFormNpc, loadOrder, time)` writes a patched `template.ess` with `savefile` and loads it. If in game, `moveRefrToPosition` loops until within 256 u.
- Streaming: `GridImpl`, lazy ESM chunk loading in `WorldState::GetNeighborsByPosition`, `CreateActor`/`DestroyActor`.
- Reuse: everything server-side and network-side. Replace the client's world-entry mechanism.

## 4. Design

### 4.1 Authority model
Class A for actor existence, position at spawn, enabled state and subscription sets. Class B for subsequent movement (F01).

### 4.2 Server state & persistence
| State | Type | Where | Persisted | Default |
|---|---|---|---|---|
| User↔actor binding | map | `ActorsMap` | no (session) | — |
| `profileId` | int | `MpChangeFormREFR.profileId` | yes | -1 |
| Spawn point | pos/rot/worldOrCell | `spawnPoint` | yes | GameProfile start point |
| Position/rotation/worldOrCell | | `position`, `angle`, `worldOrCellDesc` | yes (≤ 30 s throttle) | spawn point |
| `isDisabled` (offline players) | bool | `isDisabled` | yes | false |
| Game identity of DB | string | DB meta record (`SRV-012`) | yes | "fallout4" |

### 4.3 Protocol
**Owns `CreateActorFo4` (64)**, the FO4 twin of `CreateActor` (33): `idx`, `isMe`, `refrId` (long id for ESM actors), `baseId`, `transform{worldOrCell,pos,rot}`, `movementFlags` (F01 bitfield), and a `props` map of optional per-feature sections (appearance F03, equipment F05, inventory F04 owner-only, actor values F08, anim state F02, hosting F13, leveled/template F14, power armor F17, progression F19 owner-only, effects F20, lock state F24, light F28, names/custom properties F31). Other specs add sections; F00-T11 defines the base struct and registration. Reused unchanged: `CustomPacket` (login), `DestroyActor`, `Teleport`/`Teleport2`, `SetRaceMenuOpen` (opens LooksMenu in FO4, see F03), `UpdateGamemodeData`. The protocol prefix is `fo4-1_` (NET-001).

### 4.4 Client capture
- **Connect/login:** reuse `SkympClient`, `AuthService`, `NetworkingService`, `SettingsService` (fork, rename settings file to `falloutmp-client-settings.txt`).
- **Main-menu detection:** `tick` without `update` means the main menu, as in SkyMP. FO4 platform must replicate SP's `update`/`tick` semantics (PLAT-020b).

### 4.5 Apply: world entry (ADR-007 "template save + MoveTo")
1. Ship `falloutmp-template.fos`. It is created once by the maintainer: a new character, past the intro, standing outside Vault 111, MQ suppressed, minimal inventory. Commit it only if the save contains no copyrighted assets beyond the game's own data references. Otherwise generate it per user with a scripted first-run (Q-07).
2. On `CreateActor isMe` while in the main menu:
   - `loadGame` (platform native, `PLAT-050`) loads the template save via `BGSSaveLoadManager`;
   - after `postLoadGame`, `MoveTo` the server position (platform native wrapping `TESObjectREFR::MoveTo`/`SetLocationOnReference`, AE 2201138);
   - apply appearance (F03), inventory and equipment (F04/F05), AVs (F08) and progression (F19).
3. When already in game (reconnect), skip the load and do 2's MoveTo loop as SkyMP does (`moveRefrToPosition` until ≤ 256 u).
4. Block all user-initiated saves/loads (prior-art Adopt 8: refuse uncommanded saves). Disable autosave via INI (`bSaveOnTravel`, `bSaveOnWait`, `bSaveOnRest`, `bSaveOnPause`, `fAutosaveEveryXMins`). Force the INI keys at startup (`index.ts`-style first-update tweaks, CLI-010).
5. Quest/world contamination guard: on load, stop the template save's running quests via an allow-list policy (ADR-011) and verify with `G-self` that MQ scripts are idle.
6. **Fallback (if template-state leakage is unacceptable):** a `.fos` patcher, analogous to SkyMP's `template.ess` modification (`DATA-030`). See reference/fo4-data-formats.md §7 for feasibility.

### 4.6 Validation & anti-cheat
- RakNet password = protocol prefix (+ server password). Wrong game/version is refused.
- The login flow is unchanged (guid-unchanged check across await; gamemode `onLoginAttempt` veto).
- The manifest check: the server publishes CRC32/size of each load-order plugin and its BA2s (`manifestGen.ts` must handle `.ba2` names: `REF-024`). The client refuses or warns on mismatch (`LoadOrderVerificationService`).
- The client must not be able to load a non-MP save while connected (SkyMP `SinglePlayerService` semantics).

### 4.7 Audience
`CreateActor isMe` goes to the owner. Neighbours get `CreateActor` via grid subscription.

### 4.8 NPC parity
N/A for login. Streaming covers NPCs identically (F13).

### 4.9 Gamemode API & Papyrus
- Unchanged API: `mp.createActor`, `setUserActor`, `getUserActor`, `getActorsByProfileId`, `setEnabled`, `onLoginAttempt`, `connect`/`disconnect` events.
- Default gamemode (ADR-018, GM-010) implements spawn points and first-login character creation (F03).

### 4.10 Edge cases
- Disconnect during template load: the server disables the actor on disconnect; reconnect re-enters normally.
- Server restart: actors with `profileId != -1` are loaded disabled (SkyMP behaviour) and re-enabled on login.
- Interior spawn: `worldOrCell` is a CELL. MoveTo must target the cell, and the grid uses the cell id (SkyMP behaviour).
- Runtime-version mismatch: the platform refuses to initialize and shows a message box (PLAT-003).

### 4.11 Performance budget
Template load ≤ 15 s on SSD. Spawn-to-playable ≤ 20 s. `CreateActorFo4` snapshot ≤ 4 KB for a neighbour (appearance + equipment + public props); the `isMe` snapshot carries inventory, progression, effects and quest slots and may reach ~24 KB, so it is sent chunked through the deferred channel (SkyMP channel 0 pattern).

## 5. Engine / platform work required
- `loadGame(savePath)`, `moveRefrToPosition(ref, cellOrWorld, pos, rot)`, save/load blocking hooks, autosave suppression (`PLAT-050…054`).
- `update`/`tick` event semantics on FO4 (`PLAT-020b`), `on('loadGame')`/`postLoadGame` events (`PLAT-041`).
- Main-menu automation: skip intro movies (INI `SIntroSequence=`), auto-close the main menu or offer "Connect" (CLI-012).

## 6. Tests
- `L-unit`:
  - login offline → `spawnAllowed` → `CreateActor isMe` contains the FO4 payload variants (`[F00]`);
  - DB game-identity refusal;
  - disabled-on-load for player actors.
- `L-int`: server with the FO4 GameProfile, offline login of 2 bot clients, both receive each other's `CreateActor`, disconnect → `DestroyActor`.
- `G-self`: the self-test plugin connects to a local server, loads the template, reaches position within 256 u, and reports quest state (no MQ running).
- `G-manual`: two players; join, see each other, quit, rejoin at the persisted position.

## 7. Tasks
- [ ] **F00-T01** Create the template save procedure and document it; decide commit vs per-user generation (Q-07) — S — Depends: PLAT-050 — Verify: G-manual — Files: docs/falloutmp/test-scripts/template-save.md
  - Accept: a reproducible procedure; the save loads with no MQ scripts active.
- [ ] **F00-T02** Integrate and test the platform `loadGame(path)` + `postLoadGame` event + `MoveTo` native (implemented by PLAT-050/PLAT-051; this task owns the F00 self-test checks and the client wiring only) — S — Depends: PLAT-050, PLAT-051, PLAT-020b — Verify: G-self — Files: falloutmp-client/src/services/services/loadGameService.ts, selftest checks
  - Accept: the self-test loads the template and teleports to 3 positions (exterior, interior, other worldspace).
- [ ] **F00-T03** Integrate the save/load lifecycle guard (hooks implemented by PLAT-052; INI forcing and client-side policy here) — S — Depends: PLAT-052 — Verify: G-self — Files: falloutmp-client/src/index.ts
  - Accept: F5/quicksave, menu save and autosave do nothing while connected; a log line shows the blocked attempt.
- [ ] **F00-T04** Client world-entry flow (fork `remoteServer.onCreateActorMessage` + `LoadGameService` for the template approach) — M — Depends: F00-T02, CLI-001 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/{remoteServer,loadGameService}.ts
  - Accept: a new player spawns at the start point; a returning player spawns at the persisted position.
- [ ] **F00-T05** Quest-contamination guard (stop/disable template quests, allow-list over FO4's quest set, G-self dump of running quests) — M — Depends: F00-T02, F27-T01 (pulled into M4, T0) — Verify: G-self
  - Accept: after spawn the running-quest dump contains only allow-listed quests; MQ101/MQ102 are stopped; the guard re-runs after every load. — Files: falloutmp-client/src/services/services/questGuardService.ts
  - Accept: the self-test reports MQ101/MQ102 not running, and no vanilla quest objectives on the HUD.
- [ ] **F00-T06** GameProfile start points, default worldspace FormDesc `3c:Fallout4.esm`, settings defaults — S — Depends: REF-004 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/game_profile/fallout4/*, ts/settings.ts
  - Accept: unit test spawns a new actor at the configured start point in the Commonwealth.
- [ ] **F00-T07** F00 integration test for the DB game-identity refusal implemented by SRV-012 (open a Skyrim DB with `game: fallout4` → refuse with the documented message) — S — Depends: SRV-012 — Verify: L-int — Files: skymp5-server/ts/test/
  - Accept: opening a Skyrim DB with game=fallout4 fails with a clear error.
- [ ] **F00-T08** Persistence completeness audit for FO4 (I11: lastAnimation, activationBlocked, SetPosition, quests, factions, effects in `ToTuple`) — M — Depends: REF-020 — Verify: L-unit — Files: MpChangeForms.{h,cpp}
  - Accept: round-trip tests for every field.
- [ ] **F00-T09** Client `LoadOrderVerificationService` FO4 port + Buffout 4 NG presence hint in the manifest error path (manifest generation itself is REF-024) — S — Depends: REF-024, PLAT-095 — Verify: L-int — Files: skymp5-server/ts/manifestGen.ts, falloutmp-client loadOrderVerificationService.ts
  - Accept: a mismatched plugin CRC produces a client error dialog.
- [ ] **F00-T11** Define `CreateActorFo4` (64): base struct (idx, isMe, refrId, baseId, transform, movementFlags, `props` map with optional per-feature sections), TS mirror, per-game serializer registration (NET-002/NET-003), binary + JSON round trip, size budget test, chunked send for `isMe` — M — Depends: NET-002, NET-003 — Verify: L-unit — Files: skymp5-server/cpp/messages/CreateActorFo4Message.h, Messages.h, falloutmp-client/src/services/messages/createActorFo4Message.ts
  - Accept: round trip passes with every optional section absent and present; neighbour snapshot ≤ 4 KB in the test fixture; `isMe` snapshot > 16 KB is split and reassembled.
- [ ] **F00-T10** Intro skip and main-menu "Connect" UX — S — Depends: CLI-012 — Verify: G-manual
  - Accept: launching the game reaches the connect UI in ≤ 3 clicks, with no intro.

## 8. Open questions & risks
- Q-07: can a template `.fos` be committed (it references game data only), or must it be generated per user?
- The template-save approach may leave world flags from the template; a `.fos` patcher may be needed (DATA-030).
- Steam auto-updates can change the runtime; the platform must refuse unknown runtimes gracefully (PLAT-003, PLAT-006 runbook).
- **Template-save assumptions to verify in F00-T01 (G-self):** (a) `BGSSaveLoadManager` loads a save whose plugin list differs from the server load order without the "missing content" dialog or with it suppressed; (b) the F4SE co-save (`.f4se`) of the template is harmless or can be deleted; (c) High FPS Physics Fix's load accelerator does not break the load path (07 §5).
