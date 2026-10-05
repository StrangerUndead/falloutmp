# F30 — Chat, Console Commands & Admin Tools

| Field | Value |
|---|---|
| Tier | T0 |
| Target level | L4 for chat via the public gamemode (SkyMP L4 via its private gamemode); L3 for console commands (SkyMP L3) |
| SkyMP analogue | Chat lives in the gamemode plus the front chat widget (`skymp5-front/src/constructorComponents/chat/index.js:14-21, 78-104`); console commands are hijacked on the client (`skymp5-client/src/services/services/consoleCommandsService.ts:16-108`), sent as `ConsoleCommand` (12) and executed by `ConsoleCommands::Execute` with `EnsureAdmin` (`skymp5-server/cpp/server_guest_lib/ConsoleCommands.cpp:58-193`). SkyMP level L4 (chat) / L3 (console) |
| Milestone | M6 (chat and admin baseline; 03-milestones M6 exit criterion 3) |
| Workstreams | GM, FRONT, SRV, CLI, PLAT, OPS |
| Depends on | GM-001, GM-011, GM-012, FRONT-001, FRONT-003, FRONT-005, PLAT-012 (console API), PLAT-061, CLI-060, CLI-071, F31 (event sources, properties), F00 (login), F04, F08, F13 |
| References | reference/skymp-sync-inventory.md §1.11, §1.12, §1.15 (rows 14, 16, 21), §2 ("Console commands", "Chat" rows), §3.2 I12; reference/commonlib-port-map.md §1.14, §4.10; reference/papyrus-api-map.md §2.4 (`findConsoleCommand`), §5.6; reference/prior-art.md §5.3 (Avoid: console commands built from network data); ADR-018 |

## 1. Summary
Players talk in a chat window drawn in the in-game overlay:
- local chat reaches players nearby;
- global chat reaches everyone;
- whispers and system messages are also supported;
- slash commands (`/help`, `/roll`, `/tp`, …) are typed in the same window.

Following SkyMP, chat is gamemode code. FalloutMP ships it in the **public** `falloutmp-gamemode` (ADR-018), so every server has it out of the box. Admins also use the Fallout 4 console (`~`): `player.additem`, `placeatme`, `disable` and a FalloutMP `mp` command are forwarded to the server and executed there with permissions. Every result is printed back, success or refusal. Other vanilla console commands are blocked locally unless they are harmless. Admin tools are kick, ban, mute, teleport, spawn, heal/kill, time and weather. Every admin action and command goes into a moderation log. Anti-spam rules run on the server.

## 2. Vanilla Fallout 4 behaviour (engine facts that matter for sync)
- **Console menu:** `Console` (papyrus-api-map §5.6).
- **Console log:** `ConsoleLog::GetSingleton()` (4797437), `PrintLine(fmt, …)`, `AddString(const char*)` (2248593). Every printed line passes through `AddString` (commonlib-port-map §1.14).
- **Command tables:**
  - `SCRIPT_FUNCTION::GetConsoleFunctions()` (span[523], ID 901511), `GetScriptFunctions()` (span[818], ID 75173), `LocateConsoleCommand(name)`;
  - entries have `functionName`, `shortName`, `paramCount`, `parameters`, `executeFunction`;
  - `ExecuteFunction_t` returns `float&` (not Skyrim's `double&`);
  - `SCRIPT_PARAM_TYPE` values match Skyrim, and `0x1A` = `kContainerRef` (commonlib-port-map §1.14).
- **Ids typed in the console:** the selected reference (`prid` or a click) is a **local** form id. Typed form ids are load-order ids, and ESL ids look like `FE xxx yyy` (ESPM-003).
- **Exploitable commands** in single player: `tgm`, `tcl`, `coc`, `player.additem`, `setstage`, `player.setav`, `kill`, `resurrect`, `unlock`, `tfc`, `tai`, `tcai`, `set timescale`. In MP each one either desyncs the client from the server or is a cheat attempt.
- **Prior art:** Commonwealth Online removed "unsafe network-triggered weather console execution". **Never run console strings received from the network** (prior-art §5.3 Avoid).

## 3. SkyMP baseline
- **Chat:** the front widget enforces `MAX_LENGTH = 2000`, `TIME_LIMIT = 1 s`, `MAX_LINES = 10`, `SHOUT_LIMIT = 180 s` on the client only [src: chat/index.js:14-21]. Transport is gamemode-defined:
  - client to server: a `makeEventSource` snippet calls `ctx.sendEvent`, which becomes `CustomEvent` (15) `_<name>` on the server (only `_`-prefixed names, skymp-sync-inventory §1.15 row 16);
  - server to client: `mp.sendCustomPacket`, or a signed `customPacketType: 'eval'` packet [src: sweetTaffyEvalService.ts:37-80].
  - The real chat system lives in SkyMP's private gamemode.
- **Console, client:**
  - `findConsoleCommand` hijacks `additem`, `equipitem`, `placeatme`, `disable`;
  - it repurposes `" ConfigureUM"` or `"test"` as `mp`;
  - it converts ObjectReference args with `localIdToRemoteId`;
  - it sends `ConsoleCommand{commandName, args[]}` reliable and prints `"sent"`.
- **Console, server:**
  - `EnsureAdmin` (`consoleCommandsAllowed` flag or `enableConsoleCommandsForAll`);
  - `AddItem`, `EquipItem`, `PlaceAtMe`, `Disable` (FF refs and actors only), `Mp disable`;
  - errors are exceptions that get logged, and **no result reaches the user** (I12).
- **Admin primitives:** `mp.kick`, `mp.getUserIp`, `mp.onLoginAttempt`, `discordBanSystem.ts`, the persisted `consoleCommandsAllowed` property.
- **Reuse:** all of the above. **Add:** results, permissions per command, FO4 commands, local blocking, the public chat implementation.

## 4. Design

### 4.1 Authority model
- **Class A:** chat delivery (the gamemode decides who receives what), every console command (executed on the server), roles, mutes and bans.
- **Class D:** console commands on the `console.localAllowList` (e.g. `help`, `fov`, `tm`), which run locally and affect only the local view.
- **Blocked:** everything else.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Admin flag | bool | built-in `consoleCommandsAllowed` binding | yes | false |
| Role | `"admin"|"mod"|"player"` | `private.role` custom property (GM-012) | yes (`dynamicFields`) | `player` |
| Mute | `{untilMs, reason, by}` | `private.mute` | yes | none |
| Ban | `{profileId, ip?, discordId?, untilMs, reason, by}` | `private.indexed.ban` on the actor + `data/bans.json` for IP bans (gamemode) | yes | — |
| Chat limiter | token bucket per user | gamemode memory | no | — |
| Chat history | last 20 global lines | gamemode memory | no | — |
| Moderation log | JSON lines | `logs/moderation-YYYY-MM-DD.jsonl` (gamemode) + spdlog `[console]` lines (core) | file | — |
| Command registry | name → handler, arg schema, enabled | `ConsoleCommands` (per GameProfile) + settings `console.commands` | no | GameProfile |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `ConsoleCommand` (12) | C→S | `commandName`, `args[]` (int or string) | R | per command (≤ 10/s) | reused (game-agnostic payload) |
| `ConsoleCommandResult` (116) | S→C | `commandName str≤32`, `ok bool`, `code u8` (ok, unknown, permission, badArgs, notFound, vetoed, error, rateLimited), `text str≤512` | R | exactly one per `ConsoleCommand` | new (116, this spec) |
| `CustomEvent` (15) `_onChatInput` | C→S | JSON args `[text, channel, target?]` | R | per message | reused (gamemode) |
| `CustomPacket` (1) `customPacketType: "browserEvent"` | S→C | `{eventName: "chatMessage", payload: {id, channel, fromName, fromId, text, ts, color?}}` | R | per delivered line | reused; handled by the new client `BrowserEventPacketService` (no code execution) |
| `Teleport` (20), `SetInventoryFo4` (68), `ChangeValuesAv` (72), `UpdateProperty` (7) | S→C | effects of admin commands | R | per effect | reused |

`ConsoleCommand` (12) stays byte-identical to Skyrim. Its payload is game-neutral, so it needs no FO4 twin.

### 4.4 Client capture (owner side)
**Console (`ConsoleService`, FO4 port of `consoleCommandsService.ts`):**
- At start, walk `GetConsoleFunctions()` (PLAT-012) and assign every entry one of three treatments:
  - (a) **forwarded:** the server command set (`additem`, `equipitem`, `placeatme`, `disable`, `enable`, `kill`, `resurrect`, `setav`, `moveto`, `mp`). The executor is replaced by one that converts args (ObjectReference: `localIdToRemoteId`; BaseForm: local load-order id → server global id via the client's load-order map, ESL-aware) and sends `ConsoleCommand`;
  - (b) **local:** `console.localAllowList`, default `help`, `fov`, `tm`, `showinventory`, `getav`, `getpos` [inference: verify the names in the dump];
  - (c) **blocked:** everything else. The executor prints "Not available in multiplayer" and returns.
- `mp` reuses an unused debug command entry. Choose the candidate from the G-self dump, the way SkyMP uses `" ConfigureUM"`.

**Chat:**
- The front widget calls `window.falloutPlatform.sendMessage('chatInput', text, channel, target)`. The FO4 front keeps SkyMP's `skyrimPlatform.sendMessage` shim name as an alias.
- The gamemode's event source `_onChatInput` (`ctx.sp.on('browserMessage')` → `ctx.sendEvent`) forwards it.
- `Enter` opens chat focus and `F6` toggles focus (CLI-060), unless a menu is open.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **`ConsoleCommandResult`:** `printConsole("[mp] " + text)`. On `ok = false` a red line is shown, and a HUD notification is shown if the console is closed.
- **`BrowserEventPacketService`:**
  - receives `CustomPacket` with `customPacketType: "browserEvent"`;
  - JSON-encodes the payload and dispatches `window.dispatchEvent(new CustomEvent('falloutmp:' + eventName, {detail}))` into the overlay;
  - `eventName` must match `^[a-zA-Z0-9_]{1,32}$`;
  - nothing is evaluated.
- **Front chat:**
  - renders text as **text nodes**, never HTML (CEF XSS);
  - colours come from a server-side palette id;
  - local/global tabs; history on join (last 20 global lines).
- **Reconnect:** chat history is re-sent. The console needs no state.

### 4.6 Validation & anti-cheat
| Check | Reject → correction |
|---|---|
| `ConsoleCommand`: sender has a user actor; rate ≤ 10/s per user (S20) | `ConsoleCommandResult{rateLimited}` |
| Command known and enabled in `console.commands` | `{unknown}` |
| Permission: `consoleCommandsAllowed` (or `enableConsoleCommandsForAll`), then gamemode `onConsoleCommand(actorId, name, args)` (GM-012 checks the role per command) | `{permission}` / `{vetoed}` |
| Arg schema: count and types; target ref exists (`GetFormAt`); base form exists, and its record type is allowed for the command (items for `additem`, NPC_/item/ACTI/CONT for `placeatme`); count 1…`console.maxCount` (10 000); AV EDID exists (F08) | `{badArgs}` / `{notFound}` |
| Handler throws | `{error, text = exception message}`; logged |

Every command, accepted or rejected, is logged as a spdlog `[console]` line with profileId, actor, command, args, code and duration, and counted in `console_commands_total{command,code}`.

**Chat** (gamemode, GM-011):
- the sender must not be muted;
- token bucket: burst 5, refill 1/s (`chat.rate`);
- length ≤ `chat.maxLength` (500) and ≤ 10 lines;
- control characters and bidi overrides stripped; NFC normalization;
- the same text within 10 s is suppressed;
- optional profanity hook;
- channel `local`: recipients are users whose actors share the sender's `worldOrCell` within `chat.localRadius` (2048 u), using `actorNeighbors` for the candidates;
- `whisper` needs an online target;
- `global` can be disabled.
- Rejects send a system line to the sender only, e.g. "You are muted (12 min)". Never silent (I12).

### 4.7 Audience / visibility
- `ConsoleCommandResult`: the issuer only.
- Chat: per channel as above. System and admin broadcasts go to everyone. Mod-channel lines go to mods and admins only.
- The moderation log is never sent to clients. The admin panel (FRONT-005) reads a filtered view through a gamemode event source.

### 4.8 NPC parity
- Commands that target NPCs (`kill`, `resurrect`, `disable`, `setav`, `moveto`) go through F12/F08/F13 server paths. Hosts receive the results through the normal channels (S14).
- `placeatme <NPC_>` creates a server actor that gets a host by F13 rules.
- Chat has no NPC role. A gamemode may make NPCs "say" lines as system messages.

### 4.9 Gamemode API & server Papyrus
- **Event (new):** `onConsoleCommand(actorId, commandName, args)` [blockable] fires after the core permission check and before execution.
- **Existing:** `mp.kick(userId)`, `mp.getUserIp`, `mp.onLoginAttempt` (ban check), `mp.sendCustomPacket`, `mp.makeEventSource`, `mp.set(actor, 'consoleCommandsAllowed', bool)`, `mp.set(actor, 'locationalData', …)` (teleport).
- **Public gamemode modules** (GM-011/GM-012):
  - `chat/` (channels, limiter, history, `/` command router);
  - `admin/` (roles; `/kick`, `/ban [duration]`, `/unban`, `/mute`, `/tp <player|marker|x y z>`, `/bring`, `/spawn <base> [count]`, `/heal`, `/kill`, `/time <hour>` and `/weather <id>` via F25 `mp.setWorldClock` / `mp.setWeather`, `/list`, `/roll`, `/help`);
  - `moderationLog.ts`.
- Mods get `consoleCommandsAllowed` only if `admin.modsUseConsole`. Otherwise mods use slash commands only.
- **Server console subcommands** (`mp …`, executed in C++): `mp disable`, `mp tp <target> <x> <y> <z> [worldOrCellDesc]`, `mp settime <hour>`, `mp setweather <formId>`, `mp kick <target>`, `mp kill <target>`, `mp resurrect <target>`.
- **Papyrus:** no new natives. Server scripts write chat lines through the gamemode (`mp.registerPapyrusFunction` bridge) if needed.

### 4.10 Edge cases & failure modes
- **Gamemode hot reload:** chat handlers and event sources are re-registered. In-memory limiters reset, which is acceptable.
- **A command targets an actor that streams out mid-command:** it executes on the server state regardless.
- **Ban of an online user:** kick, then a login veto on the next attempt.
- **A ban by IP only** also blocks other profiles from that IP. This is opt-in.
- **Chat logging** contains personal data: `chat.log` defaults to false, and server owners must disclose it (DOCS-002).
- **Console open while being attacked:** the world is not paused (F28-T01).
- **Unknown future FO4 runtime with a different command table:** the walk uses names, not indices, and logs commands that are missing.

### 4.11 Performance budget
- A console command costs ≤ 1 ms of server CPU, excluding the effects themselves (e.g. a spawn).
- A chat line costs O(neighbours) for local and O(users) for global. ≤ 300 B per delivered line.
- At 100 players and 1 msg/s global this is ≈ 30 KB/s server egress, within the §7 budget per client (≤ 0.3 KB/s).

## 5. Engine / platform work required
- PLAT-012: `printConsole` (ConsoleLog), `findConsoleCommand`, and a full console table walk with a writable `executeFunction` (FO4 `float&` signature).
- A table dump for the self-test (names, short names, param types).
- PLAT-061: browser focus for chat input.
- Nothing new in the engine for chat.

## 6. Tests
- `L-unit` `[F30][ConsoleCommand]` (extends `unit/ConsoleCommandTest.cpp`):
  - admin accept for each FO4 command (AddItem with an item instance, PlaceAtMe of an NPC creates an actor, SetAV through F08, `mp tp` sends `Teleport`);
  - non-admin → `ConsoleCommandResult{permission}`;
  - unknown command, bad arg types, missing forms;
  - rate limit;
  - `onConsoleCommand` veto (FakeListener) → `{vetoed}` and no state change;
  - exactly one result per command;
  - `ConsoleCommandResult` round trip.
- `L-ts`:
  - console table classification (forward, local, block);
  - id conversion including ESL ids;
  - `BrowserEventPacketService` rejects bad event names and never evaluates;
  - gamemode chat unit tests (limiter, duplicate suppression, local radius filter, mute, whisper) in `falloutmp-gamemode` with vitest.
- `L-int`: three bots. Local chat reaches only the near bot; global chat reaches all; spam is limited; `/kick` disconnects; a banned profile's login is refused; an admin `mp settime` is seen by every bot.
- `G-self`: console table dump; `mp` command entry found; a blocked command prints the refusal.
- `G-manual`: chat between two players (local out of range, global); an admin uses `player.additem`, `placeatme` and `/tp`; a non-admin gets "Not enough permissions".

## 7. Tasks
- [ ] **F30-T01** FO4 `ConsoleCommands` port: GameProfile command registry, `console.commands` settings, FO4 handlers (AddItem with item instances via F04, EquipItem, PlaceAtMe incl. NPC_ via F13, Disable/Enable, Kill/Resurrect via F12, SetAV via F08, MoveTo, `mp` subcommands tp/settime/setweather/kick/kill/resurrect), arg validation, rate limit, `[console]` logging + metric — M — Depends: REF-003, F04, F08 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/ConsoleCommands.{h,cpp}, unit/ConsoleCommandTest.cpp
  - Accept: the `[ConsoleCommand]` cases pass. Skyrim commands behave as before.
- [ ] **F30-T02** `ConsoleCommandResult` (116) message, a result on every path (fixes I12 for console), client printing — S — Depends: NET-002, F30-T01 — Verify: L-unit, L-ts — Files: skymp5-server/cpp/messages/ConsoleCommandResultMessage.h, Messages.h, falloutmp-client/src/services/messages/
- [ ] **F30-T03** `onConsoleCommand` gamemode event — S — Depends: F30-T01 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/gamemode_events/ConsoleCommandEvent.cpp
- [ ] **F30-T04** Client `ConsoleService` FO4: table walk (PLAT-012), forward/local/block classification, `mp` entry, local→remote and ESL id conversion — M — Depends: PLAT-012, CLI-050 — Verify: L-ts, G-self — Files: falloutmp-client/src/services/services/consoleCommandsService.ts, falloutmp-client/src/config/consolePolicy.ts
- [ ] **F30-T05** `BrowserEventPacketService` (`CustomPacket` `browserEvent` → overlay event, no eval) — S — Depends: CLI-001 — Verify: L-ts — Files: falloutmp-client/src/services/services/browserEventPacketService.ts
- [ ] **F30-T06** Public gamemode chat (implements GM-011): channels, `/` router, event source, limiter, mute, history, system messages — M — Depends: GM-001, F30-T05, F31-T05 — Verify: L-int — Files: falloutmp-gamemode/src/chat/
- [ ] **F30-T07** Front chat widget FO4 (FRONT-003): text-only rendering, tabs, focus handling, Pip-Boy theme — M — Depends: FRONT-001, F30-T05 — Verify: G-manual — Files: falloutmp-front/src/constructorComponents/chat/
- [ ] **F30-T08** Roles and admin commands (implements GM-012): roles, `consoleCommandsAllowed` sync, kick/ban/unban/mute/tp/bring/spawn/heal/kill/time/weather, login ban check, `moderationLog.ts` JSONL — M — Depends: F30-T06, F30-T03, F25-T05 — Verify: L-int — Files: falloutmp-gamemode/src/admin/
- [ ] **F30-T09** Admin panel wiring (FRONT-005): player list, actions through gamemode events, log viewer — M — Depends: F30-T08 — Verify: G-manual
- [ ] **F30-T10** Docs (DOCS-002 admin and moderation section, privacy note) + G-manual script — S — Depends: F30-T08 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F30-chat-admin.md

## 8. Open questions & risks
- Which unused FO4 console command can safely be repurposed as `mp`? Decide from the G-self table dump.
- The local allow-list content needs a security review. `tfc` (free camera) works like a scouting cheat, so it is blocked by default.
- Moderators with the console (`admin.modsUseConsole`): GM-012 must map roles to commands precisely.
- An external admin channel (RCON/HTTP via `mp.onHttpRpcRunAttempt`) is not covered yet. It belongs to OPS if the user wants it.
