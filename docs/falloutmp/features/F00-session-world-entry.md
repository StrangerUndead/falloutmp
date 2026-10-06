# F00 — Session Lifecycle, World Entry & Streaming

| Field | Value |
|---|---|
| Tier | T0 |
| Target level | L4 |
| SkyMP analogue | Connect → login → spawn → `CreateActor isMe` → template save load → grid streaming. SkyMP level L4 (reference/skymp-sync-inventory.md §1.3–1.5) |
| Milestone | M4 (connect & spawn), M5 (streaming of others) |
| Workstreams | PLAT, CLI, SRV, NET, GM, DATA |
| Depends on | PLAT-001…PLAT-020b (platform alive), ESPM-001…ESPM-010 (Fallout4.esm loads), REF-003 (GameProfile skeleton), NET-001 (protocol prefix) |
| References | F33 (world entry: the generated entry save); reference/fo4-save-entry.md; reference/skymp-sync-inventory.md §1.3–1.5, §1.10; reference/prior-art.md §5.1 (B13, C1, C2), §5.3 Adopt 1/8; reference/fo4-data-formats.md (.fos); reference/dev-environment.md §9 |

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
- **Main-menu detection:** an explicit `mainMenuReady` lifecycle event from the plugin (F33 §4.4). SkyMP's "`tick` without `update`" trick is not used; it broke after quit-to-menu (skymp issue 892).

### 4.5 Apply: world entry → F33
World entry is specified in [F33 — The entrance](F33-entrance.md) (user decision 2026-10-06; ADR-007 amended):
- at the main menu, the client generates a per-player save from a validated base-game template, with the server's position, cell and time written into it;
- it loads that save once and deletes it;
- the server binds the actor only after the client reports it is in the world (two-phase join).

F00 keeps what happens around it:
1. **In-game reconnects skip the load.** `CreateActor isMe` triggers the existing teleport when the player is more than 256 u away, as SkyMP's `moveRefrToPosition` loop does.
2. **Save/load blocking and autosave suppression:** F33 §4.5.7. The `bSaveOn*` toggles are preferences that Fallout4Custom.ini doesn't set; they are forced in memory.
3. **Quest and world hygiene after every load** (ADR-011): the clean-world module, plus the quests the template capture stops (F33 §4.5.1). F00-T05 verifies it.

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
- Disconnect during the entry load: F33 §4.5.8 (the load finishes; the client reconnects with `inGame: true`).
- Server restart: actors with `profileId != -1` are loaded disabled (SkyMP behaviour) and re-enabled on login.
- Interior spawn: `worldOrCell` is a CELL. MoveTo must target the cell, and the grid uses the cell id (SkyMP behaviour).
- Runtime-version mismatch: the platform refuses to initialize and shows a message box (PLAT-003).

### 4.11 Performance budget
Entry timings: F33 §4.11 (main menu to control ≤ 20 s P50). `CreateActorFo4` snapshot ≤ 4 KB for a neighbour (appearance + equipment + public props); the `isMe` snapshot carries inventory, progression, effects and quest slots and may reach ~24 KB, so it is sent chunked through the deferred channel (SkyMP channel 0 pattern).

## 5. Engine / platform work required
- `moveRefrToPosition(ref, cellOrWorld, pos, rot)` (PLAT-051) for corrections and in-game reconnects. The entry natives, the load call and save blocking are F33 §5.
- `update`/`tick` event semantics on FO4 (`PLAT-020b`), `on('loadGame')`/`postLoadGame` events (`PLAT-041`).
- Main-menu automation and the intro skip: F33 §4.5.6.

## 6. Tests
- `L-unit`:
  - login offline → `spawnAllowed` → `CreateActor isMe` contains the FO4 payload variants (`[F00]`);
  - DB game-identity refusal;
  - disabled-on-load for player actors.
- `L-int`: server with the FO4 GameProfile, offline login of 2 bot clients, both receive each other's `CreateActor`, disconnect → `DestroyActor`.
- `G-self`: the self-test plugin connects to a local server, enters through F33's generated save, lands within 64 u, and reports quest state (no MQ running).
- `G-manual`: two players; join, see each other, quit, rejoin at the persisted position.

## 7. Tasks
- [-] **F00-T01** Create the template save procedure and document it; decide commit vs per-user generation (Q-07) — S — Depends: PLAT-050 — Verify: G-manual — Files: docs/falloutmp/test-scripts/template-save.md — dropped: moved to F33-T06 (template capture); Q-07 now asks how the template is distributed
  - Accept: a reproducible procedure; the save loads with no MQ scripts active.
- [-] **F00-T02** Integrate and test the platform `loadGame(path)` + `postLoadGame` event + `MoveTo` native (implemented by PLAT-050/PLAT-051; this task owns the F00 self-test checks and the client wiring only) — S — Depends: PLAT-050, PLAT-051, PLAT-020b — Verify: G-self — Files: falloutmp-client/src/services/services/loadGameService.ts, selftest checks — dropped: moved to F33-T05 and F33-T09
  - Accept: the self-test loads the template and teleports to 3 positions (exterior, interior, other worldspace).
- [-] **F00-T03** Integrate the save/load lifecycle guard (hooks implemented by PLAT-052; INI forcing and client-side policy here) — S — Depends: PLAT-052 — Verify: G-self — Files: falloutmp-client/src/index.ts — dropped: moved to F33-T11 (P1) and F33-T17 (full guard)
  - Accept: F5/quicksave, menu save and autosave do nothing while connected; a log line shows the blocked attempt.
- [-] **F00-T04** Client world-entry flow (fork `remoteServer.onCreateActorMessage` + `LoadGameService` for the template approach) — M — Depends: F00-T02, CLI-001 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/{remoteServer,loadGameService}.ts — dropped: moved to F33-T07 and F33-T08 (two-phase join)
  - Accept: a new player spawns at the start point; a returning player spawns at the persisted position.
- [ ] **F00-T05** Quest-contamination guard (stop/disable template quests, allow-list over FO4's quest set, G-self dump of running quests) — M — Depends: F33-T09, F27-T01 (pulled into M4, T0) — Verify: G-self
  - Accept: after spawn the running-quest dump contains only allow-listed quests; MQ101/MQ102 are stopped; the guard re-runs after every load. — Files: falloutmp-client/src/services/services/questGuardService.ts
  - Accept: the self-test reports MQ101/MQ102 not running, and no vanilla quest objectives on the HUD.
- [ ] **F00-T06** GameProfile start points, default worldspace FormDesc `3c:Fallout4.esm`, settings defaults — S — Depends: REF-004 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/game_profile/fallout4/*, ts/settings.ts
  - Accept: unit test spawns a new actor at the configured start point in the Commonwealth.
- [ ] **F00-T07** F00 integration test for the DB game-identity refusal implemented by SRV-012 (open a Skyrim DB with `game: fallout4` → refuse with the documented message) — S — Depends: SRV-012 — Verify: L-int — Files: falloutmp-server/ts/test/
  - Accept: opening a Skyrim DB with game=fallout4 fails with a clear error.
- [ ] **F00-T08** Persistence completeness audit for FO4 (I11: lastAnimation, activationBlocked, SetPosition, quests, factions, effects in `ToTuple`) — M — Depends: REF-020 — Verify: L-unit — Files: MpChangeForms.{h,cpp}
  - Accept: round-trip tests for every field.
- [ ] **F00-T09** Client `LoadOrderVerificationService` FO4 port + Buffout 4 NG presence hint in the manifest error path (manifest generation itself is REF-024) — S — Depends: REF-024, PLAT-095 — Verify: L-int — Files: falloutmp-server/ts/manifestGen.ts, falloutmp-client loadOrderVerificationService.ts
  - Accept: a mismatched plugin CRC produces a client error dialog.
- [ ] **F00-T11** Define `CreateActorFo4` (64): base struct (idx, isMe, refrId, baseId, transform, movementFlags, `props` map with optional per-feature sections), TS mirror, per-game serializer registration (NET-002/NET-003), binary + JSON round trip, size budget test, chunked send for `isMe` — M — Depends: NET-002, NET-003 — Verify: L-unit — Files: falloutmp-server/cpp/messages/CreateActorFo4Message.h, Messages.h, falloutmp-client/src/services/messages/createActorFo4Message.ts
  - Accept: round trip passes with every optional section absent and present; neighbour snapshot ≤ 4 KB in the test fixture; `isMe` snapshot > 16 KB is split and reassembled.
- [-] **F00-T10** Intro skip and main-menu "Connect" UX — S — Depends: CLI-012 — Verify: G-manual — dropped: moved to F33-T15 and F33 §4.5.6
  - Accept: launching the game reaches the connect UI in ≤ 3 clicks, with no intro.

## 8. Open questions & risks
- Q-07 (how the entry template is distributed) and the template's open checks: F33 §8 and §9.
- Steam auto-updates can change the runtime; the platform must refuse unknown runtimes gracefully (PLAT-003, PLAT-006 runbook).
- The template-save assumptions (missing-content prompt, the F4SE co-save, High FPS Physics Fix) are checked in F33 §9.
