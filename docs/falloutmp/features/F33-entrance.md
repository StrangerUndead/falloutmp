# F33 — The entrance: from the main menu into the wasteland

| Field | Value |
|---|---|
| Tier | T0 |
| Target level | L4 |
| SkyMP analogue | Per-player save generated from `assets/template.ess` at the main menu and loaded once (`skyrim-platform/.../LoadGame.cpp`, `LoadGameApi.cpp`, `skymp5-client/.../remoteServer.ts:485-618`) — SkyMP level L4 |
| Milestone | M4 |
| Workstreams | DATA, PLAT, CLI, SRV, QA, DOCS |
| Depends on | F00 (login, persistence, streaming), PLAT-020a (frame tick), the clean-world module (2026-10-06), F03 (LooksMenu for new characters), F25 (clock and weather values) |
| References | [reference/fo4-save-entry.md](../reference/fo4-save-entry.md) (all sections; cited below as "ref §n"), [reference/fo4-data-formats.md](../reference/fo4-data-formats.md) §7, [reference/prior-art.md](../reference/prior-art.md), ADR-007 (amended 2026-10-06) |

Decision (user, 2026-10-06): **generate the entry save per player, like SkyMP** ("option 2"). This spec replaces F00 §4.5 (template save + `MoveTo`). F00 keeps login, persistence and streaming.

## 1. Summary
The player starts Fallout 4 with FalloutMP installed. There is no intro and no splash screen: the main menu appears, FalloutMP connects to the configured server and logs in, and the server answers with an **entry ticket**: where this character stands, the time of day and the weather. The client copies a small validated **template save** (base game only, made once by the maintainer after Vault 111), writes the ticket's position, cell and time into the copy, and loads it. After **one** loading screen the player stands at their server position, under a black curtain that lifts once the server has bound the character and sent its state. There are no NPCs, no story and no vanilla dialogs. The generated save is deleted right after the load, and the player's single-player saves are never touched.

### 1.1 Experience goals
These goals are the acceptance bar for the whole feature. Each has a test in §6.

| # | Goal | Measure |
|---|---|---|
| E1 | No vanilla content, ever: no pre-war intro, Vault 111, Codsworth, spouse or quest objectives | G-self quest and actor dump after entry: no story quest running, no NPC in range |
| E2 | Exactly one loading screen between the main menu and the wasteland | count LoadingMenu openings per entry = 1 (exterior targets; interiors see §4.5.2) |
| E3 | No game dialogs: no "missing content", no "load this save?", no "overwrite?" | G-manual on a setup with every DLC and the free Creations |
| E4 | The status is visible at every step; every failure says what happened and what to do; safe steps retry on their own | every state in §4.4 has a status text; every row of §4.10 has a message |
| E5 | Fast: main menu ready → player control P50 ≤ 20 s, P95 ≤ 45 s on an SSD; writing the save ≤ 250 ms | timings in `entryReady` (§4.13) |
| E6 | Zero clicks with auto-join; one click (Join) without | G-manual |
| E7 | Works for every supported setup: 1.11.240 runtime, any DLC subset, Creations, common mods, MO2 and Vortex | the matrix in §4.12 |
| E8 | The player's single-player saves are never read, written or listed; generated saves disappear after use | G-self: the save folder before and after a session |
| E9 | Rejoin without restarting: a dropped connection reconnects in place; quitting to the main menu returns to the join screen | G-manual |
| E10 | The same result for everyone: every client loads the same template; the player stands where the server says, at the server's time and weather | G-self position check ≤ 64 u; time drift ≤ 1 game minute |

## 2. Vanilla Fallout 4 behaviour (engine facts that matter)
Details and sources are in the reference; these are the facts the design rests on.

- **New game:** the pre-war intro, the Vault 111 sequence and MQ101/MQ102 scripts drive the start. Alternate-start mods describe MQ101 as fragile: UI pop-ups during it cause freezes or endless loading, and the vault exit sets off a burst of scripts (ref §9). Today FalloutMP stops these quests after a new game starts (clean world), but the intro still loads first.
- **The `.fos` format has not changed from Old-Gen to Anniversary** (header version 15; `formVersion` 68, and 69 on 1.11.x). There is no checksum and no whole-file compression; change forms are zlib-compressed one by one (ref §1). Moving the player needs three patches: the 30-byte Player Location block, the player's `MOVE` data and the file location table; time is two floats patched in place (ref §2). FO4_Wrld moves players this way on 1.11.191, and the engine loads the result (ref §2.1, §9). It also moves a worn power-armor frame and copies the sky and audio blocks when a player leaves an interior; FalloutMP's template avoids both cases.
- **Plugin lists:** a save stores its own full and light plugin lists, and the engine maps FormIDs back to plugins by name at load. A plugin the save names but the game lacks gives the "missing content" prompt; extra loaded plugins give none [I]. A base-game template therefore loads without a prompt on every setup (ref §3).
- **Loading by name:** `BGSSaveLoadManager::LoadGame(name, -1, 0, checkForMods, ignoreMissingContent)` (AE ID 2228039) from an F4SE task at the main menu; it blocks the main thread for about 6 s. `kPreLoadGame`/`kPostLoadGame` arrive on the loader thread, not the main thread. Calling it before the main menu is idle gave FO4_Wrld a black screen (ref §4, §5). The function `PLAT-050` named first (`0xBEE760`) is the inner loader and must not be called (ref §4.1).
- **Main menu:** a readiness test exists; status text can go in `StartMenuBase::confirmText`; `MessageMenuManager` shows dialogs at the title menu; menu rows can be added and removed from Scaleform (ref §5). The intro and splash are INI keys (ref §5).
- **Saving and loading during play** (ref §6): autosaves (doors, fast travel, timers), F5/F9, the pause menu, exit saves, and the death reload of the last loaded save, which no longer exists once the generated save is deleted. `Game.SetInChargen(true, true, false)` blocks saving and waiting from Papyrus (papyrus-api-map.md:1065). Steam Cloud syncs the save folder and can bring deleted saves back.
- **Single-player assumptions that break:** the player may save, load, continue or start a new game at any time; death reloads a save; Continue offers the newest save; Creations prompts appear at load.

## 3. SkyMP baseline and prior art

### 3.1 SkyMP
Read at upstream `27cbc0d` (ref §9 has the step-by-step):
1. Login at the main menu.
2. The server calls `SetUserActor` at once, which sends `CreateActor isMe` and streams the actor to neighbours before the client has loaded anything (`PartOne.cpp:192-240`).
3. The client sees the main menu (two `tick`s without an `update`). It calls `loadGame(pos, rot, worldOrCell, npc, loadOrder, time)`, which patches the embedded `template.ess`:
   - replaces the plugin list with the client's;
   - sets GameHour;
   - replaces the player's NPC record (name, race, face);
   - patches Player Location and the player's `MOVE` data.
4. It writes `TESMODPLATFORM-<GUID>.ess` and loads it.
5. After `TESLoadGameEvent` and 50 Papyrus updates it deletes every `TESMODPLATFORM-*` file and blocks saving with `SetInChargen` after every load.

**Kept:**
- generating at the main menu, from a fixed template;
- one load, with the destination baked into the save;
- a unique file prefix, deleted after the load;
- blocking saves after every load;
- the in-game reconnect path with a `MoveTo` loop.

**Changed:**

| SkyMP | Problem | FalloutMP |
|---|---|---|
| `SetUserActor` before the load | neighbours see the player before the player is in the world; a reload or a crash leaves a bound ghost | two-phase join: ticket first, bind after `entryReady` (§4.0) |
| Overwrites the save's plugin list without remapping FormIDs | broke as soon as DLC order differed: "10,547 of 10,562 entries named another DLC" (Thornswood PR #36, ref §9) | keep the template's own lists; the template is base game only (§4.5.1) |
| Writes the player's NPC record (face) | head parts from plugins can't be written (RefIDs of kind 1/2 only), hence the "retry without appearance" hotfix; FO4's face layout is undocumented (ref §2.7) | appearance is applied after the load through natives (F03), never written into the save |
| `nextObjectId` hard-coded `0xFF0014FE`; grid by truncating division; `CreateRefId` copies `countWas` bytes instead of entries | latent corruption | keep `nextObjectId`; `floor(pos/4096)`; never re-encode tables (§4.5.2) |
| Load-order check only once in the world, warning only | the player loads before learning the setup is wrong | pre-flight before writing anything (§4.4) |
| Unreachable server: "connecting…" forever; 15 s login timeout wipes the saved login | dead ends | explicit states, retries with a countdown, credentials never wiped (§4.10) |
| Unequips the template's iron helmet after the load | template leftovers | the capture strips the template (§4.5.1) |

### 3.2 Other projects

| Project | How players enter | What we take |
|---|---|---|
| FO4_Wrld (FO4, 1.11.191) | A server-named checkpoint or a genesis save that the server has moved; auto-loaded from the main menu after a 4 s idle wait; 7 save/load detours | The patch set and its self-verification (re-parse, diff every untouched byte); the INI keys; the detours for P2; the settle test; the black-screen warning; staging new characters |
| Commonwealth Online 1.1.0 (FO4) | Server browser at the main menu with Cancel and Retry; first join in a custom cell with LooksMenu and SPECIAL; per-server local saves | Visible progress with Cancel/Retry; timeouts and fallbacks around character creation; fenced join attempts so a stale one can't interfere |
| CommonwealthMP (FO4) | JOIN/HOST rows injected into the main menu | The Scaleform injection and title-menu dialogs (§4.5.6) |
| Tilted (Skyrim/Fallout Together) | Own save, connect from an overlay | Structured refusals with actionable text (wrong version, mods mismatch with the list, full, password) |
| vaultmp (FO3) | Launcher checks plugin CRCs, then `coc` into the server's cell | Check the setup before entering, with a message that names the file |
| NV:MP | Separate MP saves through New/Continue with the vanilla intro | What to avoid: "Continue does nothing", crashes after Continue, players mailing saves to admins |
| powerof3 StartOnSaveF4 / reg2k fo4-autoload | Load a named save at startup | The call sequence (route A) and the console fallback (route B); hold Shift to skip |

### 3.3 Why a generated save
| Option | Loading screens | State everyone starts from | Main risk |
|---|---|---|---|
| New game + clean world + teleport (today) | 2+ (intro cell, then teleport) | whatever the intro scripts did before they were stopped | MQ101 fragility; the user saw the intro and NPCs; crashes when cleaning during start-up |
| Template + `MoveTo` (ADR-007 as proposed) | 2 (template, then the move); FO4_Wrld measured 116–339 s for a fast travel after a load and a 50 s freeze on a far direct move (ref §9) | identical template | slow, visible jump |
| **Generated save from the template (chosen)** | **1** | identical template, with only position and time changed | a small writer that must be exact; mitigated by self-verification and FO4_Wrld's evidence |
| Player's own saves (Tilted, F4MP, NV:MP) | 1 | each player's own world | leaks single-player state; needs the intro |

## 4. Design

### 4.0 The join, end to end
```
Client (game at the main menu)               Server
  plugin: kGameDataReady → mainMenuReady
  preflight (runtime, template sha256, save folder, plugins)
  connect ───────────────────────────────▶   (RakNet, fo4-1_ prefix)
  login {profileId, entry:1, inGame:false} ▶ login.ts → spawnAllowed
                                             spawn.ts: find or create the actor, keep it DISABLED
  ◀──────────────────────────────────────── entryTicket {ticketId, target, world, character}
  validate the ticket; write FalloutMP_<id>.fos (worker thread)
  LoadGame(name)   ── one loading screen ──
  kPostLoadGame(ok) → delete the save; curtain stays black
  playable test (menus closed, 3D loaded, stable)
  entryReady {ticketId, landed, timings} ──▶ entry.ts: setEnabled(actor) + setUserActor
  ◀──────────────────────────────────────── CreateActor isMe + full state (+ SetRaceMenuOpen if new)
  apply state; lift the curtain; "Welcome to <server>"            neighbours get CreateActor
```
Phase A is everything at the main menu, phase B the load, phase C the bind. Neighbours see the player only in phase C.

### 4.1 Authority model
- **Class A (server):** which actor a profile gets, where it enters, time and weather, when it is bound, whether a new character opens LooksMenu.
- **The client decides only how to get there:** which template, the file name, route A or B, and when the world is playable. It reports what happened (`entryReady.landed`, timings) but cannot pick its own position: the server's `CreateActor isMe` transform is the truth, and the existing teleport corrects any difference (§4.5.5).

### 4.2 Server state & persistence
| State | Type | Where | Persisted | Default |
|---|---|---|---|---|
| Pending entry | `{userId, actorId, ticketId, isNew, sentAtMs, retries}` | `entry.ts` map by userId | no (session) | — |
| Client entry capability | `{entry: number, inGame: boolean}` from the login packet | `login.ts` per user, passed to spawn | no | `{entry: 0}` (old clients use the old path) |
| Actor enabled flag while pending | bool | `MpActor` `isDisabled` | yes (existing) | disabled until `entryReady` |
| Entry settings | `entry.timeoutSec` (180), `entry.retryLimit` (3), `entry.required` (false) | server-settings.json | config | as shown |

No new persisted actor fields. Position, angle and `worldOrCell` already persist (F00 §4.2).

### 4.3 Protocol
Everything goes through the existing `CustomPacket` (1) with JSON content, so no binary message id is spent (the registry has one free id left, 01-sync-standard.md). All fields are validated on receipt; unknown fields are ignored.

| Message | Dir | Fields | Reliability | Trigger | New or reused |
|---|---|---|---|---|---|
| `loginWithProfileId` (existing) | C→S | `gameData.profileId`, **`gameData.entry`** (entry protocol version, 1), **`gameData.inGame`** (the client is already in a loaded world) | R | after connect | reused, 2 fields added |
| `entryTicket` | S→C | `v` (1); `ticketId` (32 hex, random); `server {name}`; `character {actorId, isNew, sex}`; `target {worldOrCell` FormDesc e.g. `"3c:Fallout4.esm"`, `pos[3]`, `angleZ` degrees`}`; `world {gameDaysPassed, gameHour, timeScale, weather` FormDesc or null`}`; `expiresInSec` | R | after `spawnAllowed` when `entry ≥ 1` and not `inGame`; again on `entryRetry` | new |
| `entryReady` | C→S | `ticketId`; `landed {worldOrCell, pos[3]}`; `timings {preflightMs, connectMs, loginMs, ticketMs, writeMs, loadMs, settleMs}`; `route` ("A"/"B"); `template` (id); `runtime` | R | after the playable test | new |
| `entryFailed` | C→S | `ticketId`; `stage` (`preflight`, `ticket`, `write`, `load`, `settle`); `error` (≤ 200 chars); `fallback` (`"newGame"` or `"none"`) | R | on a failed stage | new |
| `entryRetry` | C→S | — | R | user pressed Retry; or after a load failure | new |
| `entryAborted` | S→C | `reason` (`timeout`, `replaced`, `server`, `required` = client too old) | R | before the server disconnects a pending entry | new |

Size: a ticket is ≤ 600 bytes; `entryReady` ≤ 400 bytes. A client without `gameData.entry` gets today's path (bind at once), unless `entry.required` is set, in which case it gets `entryAborted{required}` and "Update FalloutMP" (§4.6).

### 4.4 Client: the entry state machine
A new `EntryService` (`falloutmp-client/src/runtime/entryService.ts`) owns everything before `inWorld`. `WorldSession` keeps the connection, login and streaming.

| State | Status text (main menu) | Leaves on | Timeout / retry |
|---|---|---|---|
| `waitingForMenu` | — | `mainMenuReady` | — |
| `idle` | "Press JOIN to enter <server>" (no auto-join), or the reason FalloutMP is off | Join, or auto-join | — |
| `preflight` | "Checking your game…" | ok → `connecting`; problem → `failed` | — |
| `connecting` | "Connecting to <server> (<ip>)…" | connected | 3, 5, 10, then every 30 s, with "Retrying in N s — Esc to cancel" |
| `loggingIn` | "Logging in…" | ticket / refusal | 15 s → `failed(login)`; the stored login is never wiped |
| `awaitingTicket` | "Preparing your character…" | `entryTicket` | 15 s → `entryRetry` once, then `failed` |
| `writing` | "Preparing the world…" | file written | error → `failed(write)` → fallback F1 |
| `loading` | (loading screen) | `kPostLoadGame` | A fails → B once → F1 |
| `settling` | (black curtain) "Joining <server>…" | playable test passed | 30 s → send `entryReady` with `warn`, continue |
| `joining` | (black curtain) | `CreateActor isMe` and the first full state | 10 s after `entryReady` → lift the curtain, show "Waiting for the server…" |
| `inWorld` | HUD "Welcome to <server>" | disconnect / quit to the main menu | §4.5.8 |
| `failed(stage, reason)` | dialog: what happened + Retry / Quit (P2: also "Play without FalloutMP") | user choice; network failures auto-retry | — |

**Pre-flight**, before connecting (all local and fast):
- the runtime is supported and a template exists for it (§4.5.1);
- the template's sha256 matches `templates.json`;
- every plugin the template names is loaded (always true for a base-game template; checked anyway);
- the save folder resolves and is writable, with free space ≥ 2 × the template size;
- leftover `FalloutMP_*` files were removed;
- the INI intro keys are set (warning only: they take effect at the next start);
- P2: the server manifest check (REF-024, F00-T09) runs here instead of after the load.

**Ticket validation:**
- `v` is known, and `ticketId` is 32 hex characters;
- every number is finite, `|pos| ≤ 500 000` and `0 ≤ gameHour < 24`;
- `worldOrCell` resolves through the loaded data to a `WRLD` or `CELL` form from a plugin the template names;
- the ticket has not expired.

A bad ticket → `entryFailed{ticket}` + `entryRetry` once → `failed`.

### 4.5 Apply: the generated save

#### 4.5.1 The template
**What it is:** a save made by the maintainer once per supported runtime, then normalised and validated offline. Every client loads the same file, so every client starts from the same world state.

**Capture procedure** (F33-T06, documented in `docs/falloutmp/test-scripts/entry-template.md`):
1. **Prepare a base-game install.** Fallout 4 1.11.240 with F4SE and FalloutMP in capture mode (`"entry": {"capture": true}`). The game loads the DLC plugins and installed Creations automatically [I], so `DLC*.esm`/`.ba2` and `cc*` files must be moved out of `Data`, or a separate copy of the game used. The validator below checks the result.
2. **Play to the vault exit.** New game, Normal difficulty (not Survival). Play the intro to the Vault 111 exit (about 20 minutes). Don't wear power armor and don't start combat.
3. **Wait at the Vault 111 exterior for 3 minutes.** WorkshopParent needs 30–120 s to register workshops, and MQ102's stage 10→15 turns on the radio stations (ref §9).
4. **Run the capture command.** It:
   - stops story quests and disables nearby actors with the clean-world rules (so every generated save starts with them stopped);
   - strips the inventory to the Pip-Boy;
   - holsters the weapon and leaves first person;
   - waits 10 s for scripts to settle;
   - saves `FalloutMP_Template`.
5. **Normalise offline.** `fmp_savetool normalize` replaces the screenshot with a FalloutMP image (no game imagery is shipped), sets the header name "FalloutMP", level 1 and play time 0, and rebuilds the file location table.
6. **Validate offline.** `fmp_savetool validate` checks:
   - magic `FO4_SAVEGAME`, header version 15, `formVersion` 69, `gameVersion` = the runtime;
   - plugins exactly `[Fallout4.esm]`, no light plugins;
   - the file location table, the block counts and the change-form count are consistent;
   - global data table 1 has a 30-byte Player Location (type 1) in worldspace `0x3C`, and its Global Variables block (type 3) holds `0x38` and `0x39`;
   - the player ACHR (`40 00 14`) has `MOVE` (0x2), no `HAVOK_MOVE` (0x4), and initial data of type 4 or 6;
   - no created reference has the power-armor frame base `0x2079E`;
   - ReSaver opens the file, and its active-script count is recorded.
7. **Publish.** Name the template `fo4-1.11.240-r1`; add its entry to `templates.json`; upload the `.fos` as a release asset.

**Why outside the vault, not just before the elevator** (asked 2026-10-09): a save made inside Vault 111 would need extra patching and gains nothing.
- **Sky and sound.** It carries indoor sky, lighting and sound state. Moved outdoors, it gives FO4_Wrld's "purple sky", with no shadows until the player uses a door (ref §2.4). Every entry would then need the weather and audio blocks of an outdoor save, so the outdoor capture is needed anyway.
- **What the exit turns on.** Stepping out sets MQ102 stage 10 and then 15, which turn on Diamond City Radio, Classical Radio and the Brotherhood signal (ref §9.2). Inside the vault they are still off.
- **Nothing is gained inside.** The intro is already over there: the Pip-Boy is in the inventory, and running comes back when the post-war vault loads [web: Nexus 88418]. The capture stops MQ102 and disables the actors near the exit anyway.

A save made back inside Vault 111 *after* a visit outside is a candidate indoor donor for F33-T18 [I].

**Distribution** (Q-07, recommended default): `templates.json` is committed to git. The `.fos` is a release asset that the client packaging step downloads and checks against the committed sha256. It goes in the client zip as `Data/F4SE/Plugins/FalloutMP/entry/<id>.fos`. A save holds player state and references to game data by FormID, not game data; SkyMP commits its `template.ess`. Keeping the binary out of git follows the project's no-game-data rule anyway.

`templates.json` entry:
```json
{ "id": "fo4-1.11.240-r1", "file": "fo4-1.11.240-r1.fos", "sha256": "…", "size": 0,
  "gameVersion": "1.11.240.0", "headerVersion": 15, "formVersion": 69,
  "plugins": ["Fallout4.esm"], "lightPlugins": [], "sex": 0,
  "spawn": { "worldOrCell": "3c:Fallout4.esm", "pos": [0, 0, 0], "angleZ": 0 },
  "createdAt": "…", "notes": "Vault 111 exterior, MQ stopped, Pip-Boy only" }
```
**Selection at runtime:** pick the newest template whose `gameVersion` ≤ the running runtime and has the same `1.11` family. Newer runtimes load older saves (ref §1); a template newer than the runtime is never used. If none fits, use fallback F1.

**Sex:** one male template at first. If F03 shows that the sex can't be changed reliably after the load (F03 §8), capture a female template too and choose by `character.sex`.

#### 4.5.2 Writer rules
The writer lives in a new library `fallout4-platform/fos` (C++, no game headers, built for Linux tests, the offline tool and the plugin; needs zlib). It is a pure function: `template bytes + EntryPatch → new bytes, or an error`. It never re-encodes anything it does not change.

| What | Patch | Size change | Ref |
|---|---|---|---|
| Global data table 1, type 1: Player Location (30 bytes) | worldspace RefID, `gridX/Y = floor(x/4096), floor(y/4096)`, `worldOrCell` RefID, `x, y, z`; **keep `nextObjectId`** | none (in place) | §2.4 |
| Global data table 1, type 3: Global Variables | the floats of GameHour (`0x38`) and GameDaysPassed (`0x39`), found by RefID; other entries untouched; `vsval` never re-encoded | none | §2.4, F25 §2 |
| Change form of the player ACHR (`40 00 14`) | inflate; patch the first 27 bytes of the initial data: RefID cell or worldspace, `pos[3]`, `rot = (0, 0, yaw radians)`; deflate; widen the length field if needed | yes | §2.5, §2.6 |
| File location table | after the change forms grew or shrank by Δ: FLT[0], [1], [5] += Δ | none | §2.3 |
| Header, plugin block, screenshot, every other block of global data tables 1–3 (Papyrus included), the other change forms, FormID array, visited worldspaces, unknown table 3 | **untouched** | — | — |

- **RefIDs:** the writer accepts targets from `Fallout4.esm` only, written as kind-1 RefIDs `0x400000 | formId` (every `Fallout4.esm` id is below `0x400000`), so the FormID array never changes. A target from another plugin takes the interior path below: the client enters at the template's spawn, and the bind's teleport moves the player. The server warns at startup about start points outside `Fallout4.esm`.
- **Interiors (v1):** the template is outdoors. An outdoor save moved indoors has unknown sky and audio state, and FO4_Wrld saw "a purple sky" in the reverse move (ref §2.4). For an interior target, v1 patches only the time and enters at the template's own spawn (the vault exit); after the bind, the existing teleport moves the player into the interior (two loading screens, only for players who logged out indoors). P2 (F33-T18) copies weather and audio blocks from an interior donor capture and goes direct.
- **Other exterior worldspaces of `Fallout4.esm`:** patched like the Commonwealth (the weather block keeps the template's climate; F25 forces the server's weather after the bind). [G] in §9; if the sky is wrong, they take the interior path until T18.
- **Weather:** not written in v1 (the template's weather; F25 applies the server's on bind under the curtain).
- **Refusals** (return an error, never a partial file):
  - unknown magic, header version or `formVersion`;
  - an inconsistent file location table;
  - any read past the end of a block;
  - zlib errors;
  - no Player Location block, or one that isn't 30 bytes;
  - no player ACHR, or initial data other than type 4 or 6;
  - `HAVOK_MOVE` set;
  - a target RefID that can't be kind 1.
- **Verify after write:**
  - re-parse the output;
  - check that every untouched block is byte-identical to the template, and the player ACHR body too apart from bytes 0–26;
  - recompute the file location table independently and compare;
  - read back position, cell and time and compare them with the patch.

  Any difference is an error (FO4_Wrld's `savepos.py` does the same).

#### 4.5.3 Writing the file
- **Name:** `FalloutMP_<8 hex>`. The 8 hex characters are a client-side random; nothing from the server goes into the path.
- **Folder:** the game's own save folder from `BGSSaveLoadManager::GetSaveDirectoryPath` (2228028). That includes `sLocalSavePath`, OneDrive-redirected Documents and MO2 profile saves (ref §6). Fallback: `Documents\My Games\` + `F4SE::GetSaveFolderName()` + `Saves\`.
- **How:** write on a worker thread (no frame hitch), to `<name>.fos.tmp`, flush, then rename to `.fos`. Then call `BuildSaveGameList` (2228053) on the main thread so the manager knows the file [G].
- **Cleanup:**
  - delete the `.fos` and any `.f4se` co-save once `kPostLoadGame` reports success;
  - at every start, and before each write, delete leftover `FalloutMP_*.fos`, `.fos.tmp` and `.f4se` files (Steam Cloud can bring deleted ones back);
  - never touch any other file.
- **Debug:** `"entry": {"keepSave": true}` copies the generated save and the ticket to `Documents\My Games\Fallout4\F4SE\FalloutMP-entry\`, outside the save folder so Continue never lists them.

#### 4.5.4 Loading it
- **When:**
  - MainMenu is open and ready (`GetIsMenuReady`, the game data ready flag @0x220, first advance done);
  - it has been idle for `entry.menuSettleMs` (1500 ms; FO4_Wrld waits 4 s; tune with [G]);
  - no load is in flight.

  The call runs in an F4SE task on the main thread, never inside a menu registration callback (black screen, ref §5).
- **Route A (default):**
  1. Set `loadInFlight`.
  2. `DoBeforeNewOrLoad()` (2228951).
  3. `*QGameSystemsShouldUpdate = true` (2698031).
  4. `BGSSaveLoadManager::LoadGame(name, -1, 0, checkForMods = true, ignoreMissingContent = true)` (2228039).

  This is po3's sequence for AE (ref §4.3). `ignoreMissingContent` is safe because the pre-flight proved nothing is missing; it only stops a prompt we have ruled out from blocking an unattended load.
- **Route B (fallback):** `Console::ExecuteCommand("load FalloutMP_xxxxxxxx")` (2248537). Used when an ID check failed at startup, or when route A returned false.
- **ID checks at startup:**
  - each ID above, plus 2228040, 2228033 and 2228028, must be found exactly in the Address Library database (not a nearest match, ref §4.2);
  - log each one's offset and first 16 bytes, and compare them with the expected 1.11.240 offsets (an inference until [G]).

  A missing ID disables route A; an offset mismatch only logs a warning until the G check confirms the expected values.
- **Thread rules:** the `kPreLoadGame` and `kPostLoadGame` handlers (loader thread) only flip atomics and queue tasks. Modules that change the world stay gated on `InGame` (CleanWorld's `Active()` already is). The game pumps window messages during the ~6 s call; `loadInFlight` makes every entry native refuse re-entry.

#### 4.5.5 After the load
1. **The curtain.** As early as a fade sticks after `kPostLoadGame` [G], call `Game.FadeOutGame(true, true, 0, 0.1, true)` and add an `InputEnableLayer` that disables movement, fighting, the Pip-Boy and menus.
   - The player never sees the template face, the template's gear, the first clean-world sweep, or time and weather snapping.
   - Every load also calls `Game.SetInChargen(true, true, false)` (§4.5.7).
2. **The playable test.**
   - Conditions: not loading; LoadingMenu and MainMenu closed (FaderMenu may stay open while the curtain holds it [G]); the player has a parent cell and 3D; CleanWorld has run its first actor sweep.
   - These must hold for 60 frames in a row and ≥ 1.5 s after `kPostLoadGame` (FO4_Wrld measures stability the same way, ref §4.4).
3. **`entryReady`.** It carries the landed position and the timings.
4. **The bind.**
   - The server binds and sends `CreateActor isMe` with the full state (inventory, equipment, actor values, progression, effects, F25 time and weather).
   - `WorldSession.onCreateActor(isMe)` skips the teleport when the player is in the same space and within 256 u; otherwise it teleports as today (the interior path and corrections).
5. **Lifting the curtain.** Lift it 750 ms after the bind, once the first state has been applied, or 10 s after `entryReady` at the latest (then with "Waiting for the server…", §4.4): fade in over 0.5 s and delete the input layer. HUD: "Welcome to <server>".
6. **New characters.** `SetRaceMenuOpen` opens LooksMenu (F03) after the curtain. F03 should keep a new character hidden from neighbours until its first `UpdateAppearanceFo4` (Thornswood PR #14 saw bodyless "somebody" actors, ref §9).
7. **P3 (F33-T22).** The ticket also carries the stored appearance, so it is applied under the curtain before `entryReady`.

#### 4.5.6 The main menu
- **Intro and splash.** The install guide and the client package set `Fallout4Custom.ini [General]`: `sIntroSequence=`, `bSkipSplash=1`, `uMainMenuDelayBeforeAllowSkip=0`, `fChancesToPlayAlternateIntro=0`, `bShowCompanionAppMain=0`, `uCompanionAppWarnings=0` (ref §5). The pre-flight warns if they are missing. If 1.11.240 shows a "press any button" screen, apply TE's patch (ref §9) [G].
- **P1 (MVP):**
  - auto-join (default on when a server is configured);
  - status text in `StartMenuBase::confirmText` [G];
  - errors in a `MessageMenuManager` dialog with OK;
  - hold Shift at startup to skip FalloutMP for this run.
- **P2 (F33-T15/T16):**
  - Scaleform injection into `Interface/MainMenu.swf` (CommonwealthMP's method, ref §5);
  - a **JOIN <server>** row first; **CONTINUE, NEW, LOAD and CREATIONS/MODS** removed (they lead to single-player saves, the intro, or load-order changes); SETTINGS, CREDITS and QUIT kept;
  - a status line under the menu;
  - dialogs with Retry / Quit / Play without FalloutMP.

  The pause menu uses the same movie (`root.Menu_mc.PauseMode`), so the injection also removes SAVE and LOAD there and turns QUIT into **Disconnect** (§4.5.8).
- **Single-player mode.** Shift at startup, `"auto-join": false` with no Join click, or no server configured: FalloutMP stays idle and the clean world stays off. The game is vanilla for that run. (This narrows the 2026-10-06 rule "clean world always on" to "on while FalloutMP is engaged"; see §8.)

#### 4.5.7 Saving during a session
| Phase | Measure | Covers |
|---|---|---|
| P1 | `Game.SetInChargen(true, true, false)` after every load | menu saves, waiting/sleeping (F25 owns time) |
| P1 | INI in memory after `kGameDataReady` (`RE::GetINISetting(...)->SetBinary`): `bDisableAutoSave:SaveGame=1`, `fAutosaveEveryXMins:SaveGame=999999`, `bAllowScriptedAutosave:SaveGame=0`, `bAllowScriptedForceSave:SaveGame=0`, `bSaveGameOnQuitToMainMenu:General=0`, `bWorkshopAutoSaveOnExit:Workshop=0`; prefs `bSaveOnPause/Travel/Wait/Rest:MAIN=0` through `INIPrefSettingCollection` (FO4_Wrld found these are not read from Fallout4Custom.ini and sets them in memory) | autosaves on timers, doors, travel, exit |
| P1 | Disable `MenuControls::quickSaveLoadHandler` | F5 / F9 |
| P1 | Existing: puppets and network references leave the world before any save (`Platform::RunBeforeSave`) | a save that slips through stays clean |
| P2 | FO4_Wrld's detours (ref §6): drop save flags in `QueueSaveLoadTask` (2228080); `SaveGame` (2228036) returns false ("You cannot save right now"); refuse loads the entry did not start (return 0 in the load job, then "PauseMenuBackOutFromLoadGame"); needs an xbyak trampoline (`THook` only rewrites call sites) | everything else |
| P2 | Death reload: intercept the reload of `mostRecentSaveGame` (flag 0x4); F12's Health ≥ 1 clamp means the local player shouldn't die at all | death |

#### 4.5.8 Rejoin, quit, death and unexpected loads
- **Connection lost in the world:**
  - `WorldSession` reconnects as today, logging in with `inGame: true`;
  - the server binds at once (no ticket);
  - `CreateActor isMe` corrects the position if needed.

  No reload.
- **Connection lost during phase B:**
  - the load finishes (it is local);
  - after the playable test, the client reconnects with `inGame: true`;
  - the player is already at the ticket's position.
- **Server restart during phase A:** the client is at the main menu; it reconnects and gets a new ticket.
- **Quit to the main menu** (pause menu, or P2's Disconnect):
  - `InGame` goes false when MainMenu opens;
  - the session disconnects (the server disables the actor);
  - `EntryService` returns to `idle`, with no auto-join after an explicit quit.
- **Death:**
  - F12's respawn path (the server decides; Health ≥ 1 clamp, then `Resurrect` and `MoveTo`);
  - until P2 intercepts it, a local death reloads `mostRecentSaveGame`, which no longer exists. What the game does then is [G], probably a return to the main menu;
  - if the player lands at the main menu, `EntryService` re-enters through phase A at the server's position.
- **A load FalloutMP did not start** (console `load`, P1 only):
  - treated as leaving the world: disconnect, then reconnect with `inGame: true`;
  - the server's state wins (inventory, actor values, position);
  - P2 refuses such loads.

### 4.6 Validation, anti-cheat & security
- **The server picks the position.** The client cannot choose where it enters: `entryReady.landed` is only logged and checked. A distance > 256 u from the ticket is logged as a writer bug, and the bind's teleport corrects it. Movement validation (F01) starts from the server's position.
- **Tickets are single-use and fenced.**
  - Only the pending ticket of that user is accepted.
  - A new ticket (`entryRetry`, at most `entry.retryLimit` per 5 min) invalidates the old one.
  - Replays and stale `entryReady`s are ignored.
  - With no `entryReady` within `entry.timeoutSec` (180 s), the server sends `entryAborted{timeout}` and disconnects.
- **The writer trusts nothing.**
  - Ticket values are range-checked (§4.4); the file name is client-generated.
  - The parser is bounds-checked everywhere and fuzz-tested (§6).
  - The writer only edits the fixed template, never a file the server sends.
  - No server data is ever written to disk except the validated numbers.
- **Template integrity:** sha256 is checked at startup against the committed `templates.json`. On a mismatch the client refuses ("Reinstall FalloutMP: the entry template is damaged") and uses fallback F1 if allowed.
- **Old clients:** without `gameData.entry` they get the old path. `entry.required: true` refuses them with `entryAborted{required}`: "This server needs FalloutMP x.y or newer".
- **Single-player saves are never opened.** Only `FalloutMP_*` names are written, loaded or deleted.

### 4.7 Audience
- `entryTicket` and `entryAborted` go to the joining user only.
- Neighbours learn about the player only at the bind: `CreateActor` through the grid, as today.
- While pending, the actor is disabled, so nobody sees a placeholder.

### 4.8 NPC parity
N/A: the world has no vanilla NPCs, and server NPCs (F13) are streamed after the bind as today.

### 4.9 Gamemode API & settings
- **`spawnAllowed` is unchanged:** it still chooses or creates the actor. The ticket is built from the actor's position after the handlers have run, so a gamemode moves the entry point by moving the actor.
- **New events:**
  - `entryStarted(userId, actorId, isNew)` (not blockable);
  - `entryCompleted(userId, actorId, timings)`;
  - `entryFailed(userId, stage, error)`.

  They are documented in `guides/gamemode-api.md`.
- **Server settings:** `entry.timeoutSec`, `entry.retryLimit`, `entry.required` (server-settings.example.json).
- **Client settings** (FalloutMP.json):
  - `"auto-join"` (default true);
  - `"entry": {"enabled": true, "menuSettleMs": 1500, "keepSave": false, "capture": false, "allowFallback": true}`.

### 4.10 Edge cases & failure modes
| Situation | The player sees | Recovery |
|---|---|---|
| No server configured | menu status: "No server set: put the server address in Data\F4SE\Plugins\FalloutMP.json" | vanilla menu (single-player mode) |
| Server unreachable | "Can't reach <server> — retrying in N s (Esc to cancel)" | backoff 3/5/10/30 s |
| Refused: banned, Discord, password, full | dialog with the reason | no auto-retry; full retries every 30 s |
| Protocol or version mismatch, or `entry.required` | "This server needs FalloutMP x.y — update FalloutMP" | none |
| Runtime not supported by the plugin | F4SE doesn't load the plugin (version check): the install guide explains | update FalloutMP |
| No template for this runtime | dialog: "FalloutMP has no entry save for game version x yet; starting a new game instead" | fallback F1 |
| Template sha256 mismatch | "Reinstall FalloutMP (the entry save is damaged)" | F1 if allowed |
| Template names a plugin that isn't loaded | "Your game is missing <file>" | refuse (cannot happen with a base-game template) |
| Bad ticket | — (retried once), then a dialog | `entryRetry`, then `failed` |
| Writer error | "Couldn't prepare the world (code)"; log has the details | `entryFailed{write}` → F1 |
| Save folder unwritable or disk full | "Can't write to <folder>: <error>" | Retry / Quit |
| Route A returns false | — | route B once, then F1 with a dialog |
| `kPostLoadGame(false)` | back at the main menu with a dialog | `entryRetry` once, then F1 |
| Load hangs (no `kPostLoadGame` in 120 s) | the loading screen | nothing locally; the server times out; the log says where it stopped; the user restarts the game |
| Crash during the load | Buffout 4 log | next start: leftover cleanup; a new ticket |
| Disconnect during the load | — | finish; reconnect with `inGame: true` |
| Playable test fails for 30 s | the curtain lifts anyway | `entryReady{warn}`; log |
| No bind 10 s after `entryReady` | "Waiting for the server…" | the server timeout decides |
| Landed > 256 u from the ticket | a teleport (second loading screen) | logged as a writer bug |
| Two clients with the same profile | the older one gets `entryAborted{replaced}` | the newest ticket wins (fenced) |
| Alt-Tab during the load | the load continues | `bAlwaysActive=1` in the package INI (ref §4.4) |
| Shift held at startup | vanilla menu | single-player run |

**Fallback F1 (no usable template):** today's path. The client starts a new game with the clean world on, connects once in game with `inGame: true`, and the server binds and teleports. It is slower and shows the intro cell briefly; the dialog says so. With `"entry": {"allowFallback": false}` the client stops at the dialog instead.

### 4.11 Performance budget
| Step | P50 | P95 | Notes |
|---|---|---|---|
| Pre-flight | 50 ms | 200 ms | sha256 of ~5 MB on a worker thread |
| Connect | 0.3 s | 2 s | retries excluded |
| Login → ticket | 0.2 s | 1 s | offline mode; online adds the master lookup |
| Write the save | 60 ms | 250 ms | one inflate/deflate of a small change form + a ~5 MB copy and write |
| Load (loading screen) | 8 s | 25 s | FO4_Wrld: ~6 s for its template; disk- and FPS-bound; High FPS Physics Fix's load accelerator helps (ref §4.4) |
| Settle | 1.5 s | 4 s | playable test |
| Bind + first state | 0.3 s | 1.5 s | |
| **Main menu ready → control** | **≤ 20 s** | **≤ 45 s** | E5 |

Memory: one transient template buffer (~5–8 MB). Network: ticket ≤ 600 B, `entryReady` ≤ 400 B. Server: one pending record and one timer per joining user.

### 4.12 Compatibility matrix
| Setup | Expected | Notes |
|---|---|---|
| Steam 1.11.240 + F4SE 0.7.9 | supported | template r1 |
| Later 1.11.x | supported after a plugin rebuild | the 1.11.240 template loads on newer runtimes (ref §1) |
| OG 1.10.163 / NG 1.10.984 | not supported | the plugin targets AE only (ADR-001) |
| Game Pass | not supported | no F4SE; saves are WGS containers (ref §6) |
| Any DLC subset | supported | the template doesn't name them; their start-up quests begin at load as for DLC installed mid-game; the clean world stops story ones [G] |
| Free and owned Creations (`cc*.esl`) | supported | new to the save; any AE Creations prompt is a [G] |
| Other mods | allowed by the server's manifest policy (REF-024) | their start-game quests run at load |
| Alternate-start mods (SKK Fast Start, Start Me Up, Skip) | unnecessary; the pre-flight warns | they act on new games |
| Other multiplayer mods (F4MP, Commonwealth Online, FO4_Wrld) | conflict | the pre-flight refuses with the file name |
| MO2 (profile-local saves), Vortex | supported | the game's own save path function |
| OneDrive-redirected Documents | supported | same |
| Steam Deck / Proton | expected [U] | |
| Survival difficulty | n/a | the template is Normal; survival rules are F20's |
| High FPS Physics Fix, Buffout 4 NG | recommended | faster loads; crash logs |
| "Skip Missing Mod Warning" | harmless | |

### 4.13 Observability
- **Client log**, one line per stage with its duration, for example:
  ```
  entry: template fo4-1.11.240-r1 ok (sha256 3f2a…) | connect 312 ms | login 120 ms
  ticket 7c91… 40 ms | write 85 ms (5.1 MB) | load A 6120 ms | settle 2010 ms
  bind 400 ms | total 9.1 s
  ```
  The startup lines add the ID-check results, and every failure line names its stage.
- **Server:** logs `entryTicket`, `entryReady` and `entryFailed` with the timings. `metricsSystem.ts` exports:
  - `falloutmp_entry_stage_seconds{stage}` (histogram);
  - `falloutmp_entry_total{result}`;
  - `falloutmp_entry_failures_total{stage}`.
- **Probe** (`"probe": true`): after entry it dumps the running quests, the open menus, the input layers, the player's cell and position, and the actors in range.
- **Bug reports:** `keepSave` (§4.5.3) plus FalloutMP.log are enough to reproduce a writer problem offline with `fmp_savetool diff`.

## 5. Engine / platform work required
- **`fallout4-platform/fos`** (new static library):
  - reader, patcher and verify;
  - `SaveBuilder` for synthetic test saves;
  - zlib added to `fallout4-platform/plugin/vcpkg.json` (the server build already has it).
- **`fmp_savetool`** (CLI, Linux and Windows): `inspect`, `validate`, `normalize`, `patch --ticket t.json`, `diff a b`.
- **Plugin module `modules/Entry.cpp`:**
  - startup: ID checks, leftover cleanup, INI forcing, the template sha256 on a worker thread;
  - main-menu readiness → the `mainMenuReady` lifecycle event;
  - natives:
    - `entryPreflight()`;
    - `entryWriteSave(ticket)` (async, worker thread);
    - `entryLoad(name)` (routes A/B, `loadInFlight`);
    - `entryDeleteSave(name)`;
    - `entryCurtain(on, text)` (`FadeOutGame` + `InputEnableLayer`);
    - `entryStatus(text)` (`confirmText`);
    - `entryDialog(title, body, buttons)` (`MessageMenuManager`, async result);
  - events `loadStarted`, `loadFinished {ok}`, `playable`, `mainMenuOpened`;
  - capture mode (F33-T06).
- **`Main.cpp`:**
  - `gameReady` stays for fallback F1;
  - with entry enabled, `main.ts` starts the session at `mainMenuReady`;
  - the load-message handlers stay atomics-only;
  - `InGame` also goes false when MainMenu opens (quit to the main menu), not only at `kPreLoadGame`.
- **`Hooks.cpp`:** the frame tick must also run at the main menu (today it needs `PlayerCharacter::GetSingleton()`; [G] whether that exists there). It must never run during a loading screen.
- **CleanWorld:** expose "first sweep done" for the playable test; switch it off in single-player mode (§4.5.6).
- **P2:**
  - Scaleform main-menu and pause-menu injection;
  - the save/load detours and the death-reload interception (xbyak trampoline);
  - interior donor blocks.
- **Server:**
  - `ts/systems/entry.ts` (new);
  - `spawn.ts` keeps finding or creating the actor but no longer binds when the client supports entry;
  - `login.ts` keeps the client's `entry`/`inGame` flags;
  - clock and weather getters for the ticket (`mp.fo4`, add if missing);
  - start-point validation at startup (`Fallout4.esm` targets only).
- **Client:**
  - `runtime/entryService.ts` (new);
  - `worldSession.ts` (no teleport within 256 u, `inGame` flag in the login packet);
  - `main.ts` (new lifecycle events);
  - `nativePlatform.ts` and `falloutPlatform.ts` (entry natives).
- **Packaging:** the client zip gets `entry/<id>.fos` (downloaded and checked against `templates.json`), `templates.json`, and the INI keys of §4.5.6. The install guide gets an "Entrance" section.

## 6. Tests
- **`L-unit` (`unit/`, tags `[Fos]`, `[FosPatch]`):**
  - `SaveBuilder` produces synthetic saves covering every layout the writer touches: compressed and stored change forms, length widths u8/u16/u32, initial-data types 4 and 6, empty and non-empty FormID arrays, light plugins present or absent;
  - read → write with no patch is byte-identical;
  - patch → re-parse gives the patched values, with every untouched block byte-identical;
  - FLT[0,1,5] shift by Δ, including a width change u8→u16;
  - each refusal of §4.5.2 is an error with a message;
  - 10 000 fuzz cases (truncations, bit flips, bogus offsets) never crash or read out of bounds (ASan in CI).
- **`L-ts` (falloutmp-client):** `EntryService` against the fake platform:
  - the happy path with auto-join and with Join;
  - every failure row of §4.10;
  - a disconnect at every state;
  - Shift / single-player mode;
  - ticket validation;
  - rejoin with `inGame: true`;
  - no teleport within 256 u.
- **`L-int` (server):**
  - `spawnAllowed` with `entry: 1` → an `entryTicket` and no `CreateActor`;
  - `entryReady` → `CreateActor isMe`, and neighbours get `CreateActor` only then;
  - a wrong or stale `ticketId` is ignored;
  - the timeout → `entryAborted` + disconnect;
  - `inGame: true` binds at once;
  - an old client (no `entry`) takes the old path;
  - `entry.required` refuses it.
- **`L-int` e2e:** `fmp_bot` advertises `entry: 1`, answers tickets with `entryReady`, and `fmp_e2e.sh` still passes, including "the second bot sees the first only after its entryReady".
- **`W-ci`:**
  - the release template downloads and its sha256 matches;
  - `fmp_savetool validate` passes;
  - patched copies at five targets re-read through ReSaver (headless JDK 21 shim, ref §8) with the same change-form count and Papyrus parse.
- **`G-self` (entry probe):**
  - the probe enters at three targets: the Sanctuary start point, a far Commonwealth exterior, and a second `Fallout4.esm` exterior worldspace;
  - it reports position error, the running quests (no story), the actors in range (none), Pip-Boy equipped, input enabled after the curtain, the time against the ticket, the stage timings, and the save folder before and after (only `FalloutMP_*` changed, and it is gone).
- **`G-manual`:**
  - two players;
  - a new character (LooksMenu) and a returning one;
  - pull the network cable in each phase;
  - quit to the main menu and rejoin;
  - F5, F9 and a door autosave do nothing;
  - all DLC and the free Creations installed (no prompt);
  - an HDD install for the P95 budget.

## 7. Tasks

**Phase 0 — the writer (Linux, no game needed)**
- [ ] **F33-T01** `fos` library: reader for header, plugin block, file location table, global data blocks, change-form index, FormID array, visited worldspaces and unknown table 3, with byte-exact round trip; `SaveBuilder` fixture — M — Depends: — — Verify: L-unit — Files: fallout4-platform/fos/{Fos.h,Reader.cpp,SaveBuilder.h}, unit/FosTest.cpp
  - Accept: every synthetic layout of §6 parses and round-trips byte-identically; the fuzz suite passes under ASan.
- [ ] **F33-T02** Entry patcher with verify-after-write (Player Location, time globals, player `MOVE` data, FLT shift) and the refusals of §4.5.2 — M — Depends: F33-T01 — Verify: L-unit — Files: fallout4-platform/fos/Patch.cpp, unit/FosPatchTest.cpp
  - Accept: patched values read back; every untouched block is byte-identical; each refusal has a test.
- [ ] **F33-T03** `fmp_savetool` (`inspect`, `validate`, `normalize`, `patch`, `diff`) — S — Depends: F33-T02 — Verify: L-unit, D-real — Files: fallout4-platform/tools/fmp_savetool.cpp
  - Accept: `inspect` on any real 1.11.x save prints header, plugins, FLT, block sizes and a change-form histogram; `validate` implements the template rules of §4.5.1.

**Phase 1 — engine checks (one session in the user's game)**
- [ ] **F33-T04** Entry module skeleton: exact-ID checks with offsets and first bytes logged, leftover cleanup, INI forcing, main-menu readiness and `mainMenuReady`, frame tick at the main menu — S — Depends: PLAT-020a — Verify: G-self — Files: fallout4-platform/plugin/src/modules/Entry.cpp, Hooks.cpp
  - Accept: FalloutMP.log lists every ID as found with its offset; the JS tick runs at the main menu; the readiness time is logged.
- [ ] **F33-T05** Load a named save from the main menu: route A, route B fallback, `loadInFlight`, lifecycle events, timings and thread ids (replaces PLAT-050) — M — Depends: F33-T04 — Verify: G-self — Files: modules/Entry.cpp, Main.cpp
  - Accept: the probe loads a copy of one of the user's saves by name from the main menu with no black screen; the log shows the route, return value, duration and the threads of `kPreLoadGame`/`kPostLoadGame`.
- [ ] **F33-T06** Capture mode, the capture procedure, and template r1 for 1.11.240 — M — Depends: F33-T03, F33-T04 — Verify: G-manual — Files: modules/Entry.cpp, docs/falloutmp/test-scripts/entry-template.md, fallout4-platform/plugin/entry/templates.json
  - Accept: `fmp_savetool validate` passes on r1; ReSaver opens it; its sha256 is committed and the file is a release asset.

**Phase 2 — the entrance (MVP)**
- [ ] **F33-T07** Server entry system: tickets, `entryReady`, `entryFailed`, `entryRetry`, timeout, `inGame` rejoin, old-client path, `entry.*` settings, start-point validation, gamemode events — M — Depends: F00-T06 — Verify: L-int — Files: falloutmp-server/ts/systems/{entry,spawn,login}.ts, ts/settings.ts
  - Accept: every L-int case of §6 passes.
- [ ] **F33-T08** Client `EntryService` state machine, pre-flight and ticket validation, auto-join, Shift skip, status texts — M — Depends: F33-T07 — Verify: L-ts — Files: falloutmp-client/src/runtime/{entryService,worldSession,main}.ts
  - Accept: every L-ts case of §6 passes.
- [ ] **F33-T09** Plugin entry natives (`entryPreflight`, `entryWriteSave`, `entryLoad`, `entryDeleteSave`, `entryStatus`, `entryDialog`), and the template shipped in the client zip — M — Depends: F33-T02, F33-T05, F33-T06 — Verify: G-self — Files: modules/Entry.cpp, platform/falloutPlatform.ts, runtime/nativePlatform.ts, the client packaging workflow
  - Accept: the entry probe lands at three targets within 64 u, with one loading screen each.
- [ ] **F33-T10** Curtain and playable test — S — Depends: F33-T09 — Verify: G-manual — Files: modules/Entry.cpp, modules/CleanWorld.cpp
  - Accept: the player never sees the template's face, gear or NPCs; on a LAN server control returns ≤ 2 s after `entryReady`, and never later than 10 s.
- [ ] **F33-T11** Save policy P1: `SetInChargen` after every load, INI keys in memory, quicksave handler off (replaces PLAT-052 for P1) — S — Depends: F33-T04 — Verify: G-self — Files: modules/Entry.cpp
  - Accept: menu save, F5, F9, door and timer autosaves create no file during a session; the log shows each blocked attempt.
- [ ] **F33-T12** Fallback F1 (new game + clean world + teleport) with its dialog — S — Depends: F33-T08 — Verify: G-manual — Files: runtime/entryService.ts
  - Accept: with the template removed, the player still reaches the server position, and the dialog explains why.
- [ ] **F33-T13** Observability: stage timings, server logs and metrics, `keepSave` — S — Depends: F33-T07, F33-T08 — Verify: L-ts, L-int — Files: runtime/entryService.ts, ts/systems/{entry,metricsSystem}.ts
  - Accept: one log line per stage on both sides; the histogram appears on the metrics endpoint.
- [ ] **F33-T14** `fmp_bot` entry support and the e2e case — S — Depends: F33-T07 — Verify: L-int — Files: fallout4-platform/bot/main.cpp, fallout4-platform/tools/fmp_e2e.sh
  - Accept: `fmp_e2e.sh` passes, and the second bot sees the first only after its `entryReady`.

**Phase 3 — polish**
- [ ] **F33-T15** Main-menu and pause-menu Scaleform: JOIN row, hidden single-player rows, status line, Disconnect (replaces CLI-012) — M — Depends: F33-T09 — Verify: G-manual — Files: modules/Entry.cpp (Scaleform callbacks), a small injected SWF
  - Accept: the menu shows JOIN / SETTINGS / CREDITS / QUIT while FalloutMP is engaged, and the vanilla menu in single-player mode.
- [ ] **F33-T16** Error dialogs with Retry / Quit / Play without FalloutMP — S — Depends: F33-T15 — Verify: G-manual — Files: modules/Entry.cpp, runtime/entryService.ts
  - Accept: each row of §4.10 that has a dialog shows it, and its buttons work.
- [ ] **F33-T17** Full save/load guard: FO4_Wrld-style detours, refusing loads the entry did not start, death-reload interception (replaces PLAT-052 for P2) — M — Depends: F33-T11 — Verify: G-self — Files: modules/Entry.cpp, Hooks.cpp
  - Accept: no save or foreign load succeeds during a session; a local death never reloads.
- [ ] **F33-T18** Interior targets: donor weather and audio blocks from an interior capture; direct interior entry — M — Depends: F33-T06, F33-T09 — Verify: L-unit, G-self — Files: fallout4-platform/fos/Patch.cpp, templates.json
  - Accept: entering an interior takes one loading screen, with correct sky and sound inside and after walking out.
- [ ] **F33-T19** Server manifest check during pre-flight, before the load (with REF-024 and F00-T09) — S — Depends: F33-T08, REF-024 — Verify: L-int — Files: runtime/entryService.ts
  - Accept: a mismatched plugin is reported before anything loads, naming the file.
- [ ] **F33-T20** Quit-to-menu flow: disconnect, back to the join screen, no auto-join after an explicit quit — S — Depends: F33-T08 — Verify: G-manual — Files: runtime/entryService.ts, modules/Entry.cpp
  - Accept: quit to the menu, then Join, enters again with one loading screen.
- [ ] **F33-T21** ReSaver CI oracle job for the release template — S — Depends: F33-T06 — Verify: W-ci — Files: .github/workflows/falloutmp-client-windows.yml or a Linux job, tools/resaver-shim/
  - Accept: CI fails if a patched copy doesn't re-read with the same change-form count.
- [ ] **F33-T22** Appearance in the ticket, applied under the curtain before `entryReady` (with F03) — S — Depends: F33-T10, F03 MVP — Verify: G-manual — Files: ts/systems/entry.ts, runtime/entryService.ts
  - Accept: a returning player sees their own face at the first frame after the curtain.

## 8. Open questions & risks
**Decisions with a default** (go ahead unless the user says otherwise):
- **Q-07, template distribution:** a release asset checked against a committed `templates.json` (§4.5.1). The alternative is committing the `.fos` to git, as SkyMP does.
- **Single-player escape hatch:** Shift at startup gives a vanilla run with the clean world off. This narrows the 2026-10-06 rule ("clean world always on") to "on while FalloutMP is engaged".
- **Auto-join** is on by default when a server is configured.
- **One template sex** (male); a second template only if F03 can't change sex after the load.

**Risks:**

| Risk | Likelihood / impact | Mitigation |
|---|---|---|
| The engine rejects or mis-loads patched saves on 1.11.240 | L / H | FO4_Wrld's evidence on 1.11.191; verify-after-write; Phase 1 checks before any UX work; fallback F1 (R27) |
| Route A's ID resolves to the wrong function (nearest-match lookup) | L / H | exact-ID check at startup; route B |
| Main-menu timing gives a black screen | M / M | readiness test + settle delay, tuned in G; route B |
| `formVersion` 69 changed something inside a block we patch | L / M | we patch only blocks FO4_Wrld patches on fv 69; the ReSaver oracle in CI |
| DLC or Creations quests start on every entry (new to the save) | M / L | the clean world stops them; G-manual with every DLC |
| A Creations prompt appears at load on AE | L / M | `ignoreMissingContent`; G check; the Creations menu is hidden in P2 |
| Bethesda changes the save format in an update | L / M | the template stays loadable (older saves load); re-capture per format change; the plugin rebuild is needed anyway |
| Changing the world during the load crashes (as on 2026-10-06) | M / H | `loadInFlight`; every world change gated on `InGame` |
| Distributing a save file | L / L | no game data; the screenshot is replaced; release asset |

## 9. In-game checks (for the user, in order)
The general list is ref §10. These are the ones this feature needs, grouped by session:

1. **Probe session (F33-T04/T05, about 15 minutes):**
   - the version line of any 1.11.240 save (expect 15 and 69);
   - the ID-check lines;
   - JS runs at the main menu;
   - `confirmText` and a `MessageMenuManager` dialog show at the main menu;
   - loading a copy of a save by name from the main menu: result, black screen or not, duration, threads;
   - the order of `kPostLoadGame`, `TESLoadGameEvent` and the menus closing;
   - deleting the save after the load (Continue doesn't offer it);
   - whether a "press any button" screen exists.
2. **Template capture (F33-T06, about 30 minutes):** the procedure of §4.5.1.
3. **Entrance session (F33-T09…T12):**
   - three targets, including a second exterior worldspace (sky and weather right?);
   - time and weather match;
   - no NPCs, no quest markers;
   - Pip-Boy on the arm and usable;
   - the curtain hides everything;
   - F5, F9 and door autosaves do nothing;
   - what a local death does before F12's clamp exists (expected: back to the main menu, then re-entry);
   - pull the network during the load;
   - all DLC installed: no prompt.
4. **Two-player session:** a new character and a returning one; each sees the other only after the other has entered.
