# F24 — Locks, Lockpicking, Terminals, Hacking, Holotapes & Notes

| Field | Value |
|---|---|
| Tier | T1 (server lock state, keys, lockpicking, terminals with menu actions, hacking with plausibility validation, notes/holotapes read state). T2: server-authoritative hacking (server word list), robot hacking, Automatron lock module |
| Target level | L4 |
| SkyMP analogue | None that works. The SkyMP client **unlocks every locked reference locally** (`skymp5-client/src/extensions/objectReferenceEx.ts:58-59`, `self.lock(false, false)`), so Skyrim locks are effectively removed (B17). Terminals: ESM `TERM` refs are never loaded by the server (B8, `WorldState.cpp:391-437`). SkyMP level L0 |
| Milestone | M9 (spec scope); PLAT-085 is scheduled in M10 (see §8) |
| Workstreams | SRV, PLAT, CLI, ESPM, PVM, NET, GM |
| Depends on | F04 (KEYM/NOTE as items, ESPM-012), F06 (containers), F07 (activation, doors, terminal furniture occupancy), F14 + SRV-080 (cell-reset relock), F19 + SRV-021 (Locksmith/Hacker, XP), F25 (clock), F13/F29 (crime witness), F10 (turrets toggled by terminals), F28 (Pip-Boy holotape playback), PVM (server VM for TERM fragments, ADR-011), PLAT-085, PLAT-036, ESPM-010, ESPM-011, REF-012, SRV-060, NET-007 |
| References | reference/fo4-systems-world-economy.md **S8, S9, S10**, §1 (B6, B8, B17), §3.3–3.6; reference/fo4-data-formats.md §4.3 (`XLOC`), §4.11 (KEYM/NOTE), §4.16 (TERM), §4.21 (VMAD fragments); reference/prior-art.md §3.1.10, §5.1 C12; reference/papyrus-api-map.md §1.2 ObjectReference (Lock natives, `OnHolotapePlay`), Terminal (`OnMenuItemRun`); reference/commonlib-port-map.md §4.2 (`LocksPicked`, `TerminalHacked`), §4.3 (menu names); reference/fo4-systems-combat-character.md §11 (XP) |

## 1. Summary
Doors, safes and containers keep their Fallout 4 locks for everyone. A player opens one with the right key, or picks it in the vanilla lockpicking minigame. The server checks the Locksmith rank, consumes bobby pins, unlocks the object for everyone and awards XP. Terminals show their menus; locked terminals must be hacked (vanilla word minigame, Hacker-gated), and lockouts are enforced. Menu actions (open the safe, disable turrets, read entries) run on the server and change the world for everyone. Notes and holotapes are normal items; whether a player has read or played them follows the character across machines, and scripted holotapes trigger server effects. Lock and hacked state persist until the cell resets.

## 2. Vanilla Fallout 4 behaviour
- **Lock data:**
  - REFR `XLOC {u8 level; formid KEYM; u8 flags (0x4 leveled)}`. Levels: 0 none; 1/25 Novice; 50 Advanced; 75 Expert; 100 Master; 251 Barred; 252 Chained; 253 Requires Terminal; 254 Inaccessible; 255 Requires Key.
  - Runtime `REFR_LOCK{baseLevel, key, flags (kLocked 0x1, kLeveled 0x4), numTries}`; `LOCK_LEVEL {kUnlocked=-1, kEasy, kAverage, kHard, kVeryHard, kRequiresKey, kInaccessible, kTerminal, kBarred, kChained}`.
  [src: xEdit wbDefinitionsFO4.pas:4500-4520, 11651-11672; CLF4 R/REFR_LOCK.h, L/LOCK_LEVEL.h] (data-formats §4.3; world-economy S8(b)).
- **Lockpicking:** bobby pins (`BobbyPin` 0x0000000A) in `LockpickingMenu` (`OpenLockpickingMenu(ref)`, `DamageLockpick()`, `sweetSpotCenter/Length`, `picksBroken`, `crimeDetected`).
  - Locksmith (PER 4): Advanced needs rank 1 (`523FF`), Expert rank 2 (`52400`), Master rank 3 (`52401`); rank 4 (`1D246A`): pins never break.
  - XP: Novice 6, Advanced 12, Expert 17, Master 22 (may already include INT: verify).
  - Events: `LocksPicked::Event` (no payload), story `BGSPickLockEvent{actor, lockObject, isCrime}`.
  [web: wiki:Lock_(Fallout_4), wiki:Locksmith_(Fallout_4)]; [src: CLF4 L/LockpickingMenu.h, L/LocksPicked.h, B/BGSPickLockEvent.h].
- **Papyrus:** `Lock(bool abLock, bool abAsOwner)`, `Unlock`, `IsLocked`, `GetLockLevel`, `SetLockLevel`, `IsLockBroken`, `GetKey`, `AddKeyIfNeeded`, event `OnLockStateChanged` [src: F4SE vanilla/ObjectReference.psc:4-6, 159-167, 419, 436, 612-615, 646, 896, 1055].
- **Terminals:**
  - `TERM` (a `BGSTerminal : TESFurniture`) has header/welcome text, body text with CTDA, holotape `CNTO`, and menu items `{ITXT, RNAM, ANAM type (4 submenu, 5 top, 6 redraw, 8 text, 16 image), ITID u16, UNAM, VNAM, TNAM sub-TERM, CTDA}`.
  - Item execution is a VMAD (PERK-style) fragment keyed by `ITID` `[inference]`. A terminal lock is the terminal ref's `XLOC`; containers/doors can embed a native terminal (`NTRM`).
  [src: xEdit wbDefinitionsFO4.pas:12671-12764; CLF4 B/BGSTerminal.h] (data-formats §4.16).
- **Hacking:** pick the password from a word list; 4 attempts, then a 10 s lockout; brackets remove duds or reset attempts; Intelligence sets the word count. Hacker (INT 4): Advanced rank 1 (`00052403`), Expert rank 2 (`00052404`), Master rank 3 (`00052405`); rank 4 (`001D245D`): no lockout.
  `TerminalMenu` modes `kInit, kHack, kLogin, kList, kText, kImage, kHolotape, kWaitingForPapyrus`; events `TerminalHacked::Event{terminal}`, story `BGSHackTerminal{terminal, success}`; Papyrus `Terminal.OnMenuItemRun(int auiMenuItemID, ObjectReference akTerminalRef)` [web: wiki:Hacking_(Fallout_4), wiki:Hacker_(Fallout_4)]; [src: CLF4 T/TerminalMenu.h, T/TerminalHacked.h; F4SE vanilla/Terminal.psc:1-6].
- **Notes/holotapes:** `NOTE {DNAM type (0 Sound, 1 Voice, 2 Program, 3 Terminal); SNAM sound/scene/TERM; PNAM SWF program}`; CLF4 `NOTE_TYPE` and `hasBeenRead` (naming differs; trust CLF4) (data-formats §4.11; world-economy S10). Menus `HolotapeMenu`, `PipboyHolotapeMenu`, `TerminalHolotapeMenu`; events `OnHolotapePlay(aTerminalRef)`, `OnHolotapeChatter(string, float)`.
- **Keys:** `KEYM` (`TESKey : TESObjectMISC`) [src: CLF4 T/TESKey.h].
- **SP assumptions that break:** lock state, pin use and minigame results are local; hacked state, lockouts and side effects (opened safes, disabled turrets) are local; fragments run on the client; read flags are stored in the save.

## 3. SkyMP baseline
- **Locks:** the client calls `lock(false, false)` on every locked ref it sees (`objectReferenceEx.ts:58-59`) and blocks activation of doors/containers so the server decides (B17). The server has no lock state, and DOOR reloot (3 s) only resets `isOpen` (B6).
- **Terminals:** not instantiated (B8). Vanilla Papyrus events are blocked on the client (`blockPapyrusEventsService.ts`).
- **Reuse:** `Activate` (6) with server-side branching (`ProcessActivateNormal`, `MpObjectReference.cpp:1419`); furniture occupancy for terminals (`kOccupationReach` 256 u, `:1545`); the property system for low-rate per-ref state (`mp.makeProperty`, B18); `ChangeForm` persistence; `GameModeEvent`.
- **Replace:** the client's unconditional unlock, with server-driven `Lock()/SetLockLevel()`; B8 loading for `TERM` (REF-012).
- Prior art: FO4_Wrld syncs locks through ForceLock/ForceUnlock and Papyrus `Lock` with `ai_notify=0`, persisted per `(base, cell)` (prior-art §3.1.10, C12). FalloutMP uses the server-authoritative model instead.

## 4. Design

### 4.1 Authority model
- **Class A:** lock state (level, locked, key), pin consumption, Locksmith/Hacker gates, hacked state, lockouts and attempt counts, terminal menu effects, XP, note read/played sets.
- **Class B (bounded):** minigame outcomes (lockpick success, hack success). The minigames stay local (latency makes a server sweet spot impractical). The server validates plausibility (rank, pins, time, attempts) and may reject. T2 makes hacking class A with a server-generated word list.
- **Class D:** minigame visuals, terminal text/image pages, holotape audio/program playback.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| `LockState{level u8, locked, keyId, leveled}` | struct | `MpChangeFormREFR.lock` (new optional) + built-in property `lock` | yes (`lock`; absent = ESM) | ESM `XLOC` |
| `TerminalState{hacked, lockouts{profileId → untilMs}, attempts{profileId → used}, itemFlags}` | struct | `MpChangeFormREFR.terminal` (new optional) | yes (`terminal`) | not hacked |
| Lockpick/hack sessions `{sessionId, actorId, refId, issuedMs, pinsAtStart}` | map | `LockService` (new) | no | — |
| Read notes / played holotapes | set<FormDesc> | `PlayerProfile.readNotes` (SRV-060) + private property `readNotes` | yes | empty |
| Bobby pins, keys, notes | inventory | actor `inv` | yes | — |
| Terminal action table (TERM formid, ITID) → native action (pre-VM fallback) | data | `data/fo4/terminalActions.json` loaded by GameProfile | no | shipped defaults |
| Counters `locksPicked`, `terminalsHacked` | map | `PlayerProfile.stats` | yes | 0 |

### 4.3 Protocol
`LockpickAttempt` (99) and `TerminalAction` (100) are both directions, R, with an `op` discriminator.

| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `Activate` (6) | C→S | locked door/container, terminal | R | user action | reused |
| `LockpickAttempt` op `begin` | S→C | `refId`, `sessionId` u32, `level` u8, `pins` u16 | R | after a valid activation of a pickable lock | new (registry 99) |
| `LockpickAttempt` op `denied` | S→C | `refId`, `reason` u8 (`NeedsKey, Barred, Chained, NeedsTerminal, Inaccessible, PerkTooLow, NoPins, Busy`) | R | reply | new |
| `LockpickAttempt` op `result` | C→S | `sessionId`, `refId`, `success`, `pinsBroken` u16, `elapsedMs` u32 | R | menu success/exit | new |
| `LockpickAttempt` op `outcome` | S→C | `sessionId`, `ok`, `error` u16, `unlocked` | R | reply | new |
| `TerminalAction` op `open` | S→C | `refId`, `sessionId`, `hacked`, `needsHack`, `lockoutUntilMs`, `attemptsLeft` | R | after a valid activation | new (registry 100) |
| `TerminalAction` op `hackResult` | C→S | `sessionId`, `success`, `attemptsUsed` u8, `resets` u8, `elapsedMs` | R | hack minigame end | new |
| `TerminalAction` op `menuItem` | C→S | `sessionId`, `terminalFormId` (current page TERM), `itemId` u16 | R | item selected | new |
| `TerminalAction` op `holotape` | C→S | `sessionId`, `noteId` | R | holotape inserted | new |
| `TerminalAction` op `close` | both | `sessionId` | R | menu closed / server forces | new |
| `TerminalAction` op `outcome` | S→C | `sessionId`, `ok`, `error`, `hacked`, `lockoutUntilMs` | R | reply | new |
| `TerminalAction` ops `guess`/`likeness` (T2) | C→S / S→C | `sessionId`, `word` / `likeness`, `attemptsLeft`, word list on `open` | R | per guess | new (T2) |
| `NoteAction` (110) | C→S | `op` (`read`, `play`, `chatter`), `noteId`, `terminalRefId?`, `text?`, `number?` | R | note opened / holotape played / program chatter (≤ 2/s) | **new (allocated here)** |
| `UpdateProperty` `lock` | S→C | `{level, locked}` | R | on change, to neighbours | reused (property system) |
| `UpdateProperty` `readNotes` (owner-only) | S→C | form id list (delta) | R | on login / change | reused |
| `SetInventoryFo4` (68) | S→C | pins/keys | R | after pick; on every reject | reused (F04) |
| `ProgressionUpdate` (89) | S→C | XP | R | on pick/hack | reused (F19) |

### 4.4 Client capture (owner side)
- **Activation** of locked refs and terminals stays blocked locally (SkyMP `blockActivation`); the client sends `Activate`.
- **Lockpicking:**
  - on `begin`, the native `openLockpickingMenu(ref)` opens the vanilla minigame on the local ref (PLAT-085);
  - a hook on `LockpickingMenu` success/exit reads `picksBroken` and the elapsed time, then sends `result`;
  - the local unlock the engine performs on success is reverted until the server's `lock` property arrives.
- **Terminals:**
  - on `open`, `showTerminal(ref)` (`BGSTerminal::Show`) opens `TerminalMenu`; hack mode runs locally;
  - a `TerminalMenu` mode-transition hook (`kHack` → `kList` = success; lockout screen = failure) sends `hackResult`;
  - `OnMenuItemRun` is captured by the platform (event allowed for capture, but **not** delivered to local Papyrus, PLAT-036) → `menuItem`;
  - holotape insertion → `holotape`.
- **Notes:** opening a note (`BookMenu`/Pip-Boy) or playing a holotape in the Pip-Boy (`PipboyHolotapeMenu`) → `NoteAction read/play`; `OnHolotapeChatter` → `NoteAction chatter`.
- The client never runs terminal fragments: vanilla TERM scripts stay blocked on the client (ADR-011).

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Lock state:** `CreateActorFo4` props and `UpdateProperty lock` call `Lock(locked, false)` and `SetLockLevel(level)` on the local ref. This replaces SkyMP's unconditional unlock in `dealWithRef`. Doors and containers opened after an unlock follow F07/F06.
- **Owner correction:**
  - failed or denied picks restore pins (`SetInventoryFo4`) and the lock (`lock` property);
  - a rejected hack forces `close` and closes `TerminalMenu`;
  - lockout timers display from `lockoutUntilMs`.
- **Remotes** see the world effects of terminal actions through normal channels: lock, `isDisabled`, `isOpen`, factions (F13 turrets), `powered` (F22).
- **Read notes:** `readNotes` sets `BGSNote::hasBeenRead` on the local forms after login (native), so Pip-Boy "unread" markers survive machines.
- **Stream-in / reconnect / restart:** lock and terminal state come from change forms and `CreateActorFo4`; open sessions are dropped (the client closes the menu).

### 4.6 Validation & anti-cheat
Every failure sends the outcome op (`ok=false`, `error`) plus the correction set: `lock` property to the owner, `SetInventoryFo4`, `TerminalAction close` where relevant (S12).
1. **Identity:** own actor; distance ≤ the F07 activation reach (`GameProfile::ActivationReach(type)`); same world/cell (F07-T04); rate ≤ 1 lockpick session per 2 s and ≤ 4 terminal ops/s (S6, S22).
2. **Activation of a locked ref:**
   - actor holds the KEYM `keyId` → unlock (method `key`), and the activation proceeds;
   - level 251–255 → `denied`;
   - a Locksmith rank below the level requirement → `PerkTooLow`;
   - pins = 0 → `NoPins`;
   - another actor has an open session on the ref → `Busy`.
3. **Lockpick result:**
   - the session exists, belongs to the actor, is not expired (≤ 5 min), and the ref is still locked;
   - `pinsBroken` ≤ pins held; Locksmith 4 → broken pins are ignored (not consumed);
   - `success` requires `elapsedMs` ≥ `locks.minPickMs` (default 800);
   - pins are consumed atomically (S13).
   - On success: unlock; XP per level via F19 (`source="lockpick"`); `OnLockStateChanged`; `onUnlock(method pick)`. A crime flag goes to F29 when the ref is owned and not by the actor (witnessing is reported by hosts).
   - `onLockpick` veto → still locked, pins not consumed, correction.
4. **Terminal open:**
   - furniture occupancy (one user per terminal, F07);
   - if the terminal is locked and not hacked: Hacker rank ≥ the requirement, else `PerkTooLow`;
   - lockout active (`lockouts[profileId] > now`) → `outcome` with `lockoutUntilMs`.
5. **Hack result:**
   - an open session in hack state;
   - `elapsedMs` ≥ `terminals.minHackMs` (default 2000);
   - `attemptsUsed` ≤ 4 × (1 + `resets`), with `resets` ≤ `terminals.maxResets` (default 4);
   - failure with 4 attempts used → lockout `terminals.lockoutMs` (default 10 000), unless Hacker 4.
   - Success → `hacked = true` for everyone (`terminals.hackScope = global`; `perPlayer` optional), XP via F19 (`source="hack"`, amount per level `[inference: same tiers as locks]`), `onTerminalHack` veto first.
6. **Menu item:**
   - the session is open, and the terminal is hacked or unlocked;
   - `terminalFormId` is reachable from the root TERM through `TNAM` submenus;
   - the item `ITID` exists on that page and its CTDA passes (ConditionsEvaluator, ESPM-011);
   - `onTerminalMenuItem` not vetoed.
   - Execution: (a) page/text/image items: no server effect; (b) a fragment: run the TERM VMAD fragment on the server VM (PVM, ADR-011); (c) until the VM supports it, the native action table (`unlockLinkedRef`, `toggleLinkedRefEnabled`, `setTurretsFriendly`/`disableTurrets` via F13 factions, `openLinkedDoor`) keyed by `(TERM, ITID)`.
7. **Holotape in terminal / NoteAction:**
   - the actor holds the NOTE;
   - `play` of a TERM-type note opens that sub-terminal; Program/Voice/Sound are local playback;
   - scripted holotapes (`OnHolotapePlay`) run on the server;
   - `onHolotapePlay` veto → the client stops playback.
   - `chatter`: text ≤ 256 B, rate ≤ 2/s, forwarded to the gamemode only.

### 4.7 Audience / visibility
- `lock` property → grid neighbours (public state, S7).
- Lockpick/terminal sessions, outcomes, `SetInventoryFo4`, XP and `readNotes` → owner only (S8).
- Terminal effects → whoever the affected objects' normal channels reach.

### 4.8 NPC parity
- Hosted NPCs never pick locks or hack in vanilla. Hosts must still apply lock state, so NPC AI pathing treats locked doors as locked. Doors an NPC must pass have keys in its inventory (ESM), and the server validates host-sent `Activate` for doors with the same key check.
- Companions opening locks (Automatron lockpick module) and Robotics Expert robot hacking are T2 with F21.

### 4.9 Gamemode API & server Papyrus
- **Events:**
  - `onLockpick(actorId, refId, success, pinsBroken)` **blockable**;
  - `onUnlock(actorId, refId, method: "key"|"pick"|"terminal"|"script")` **not blockable**;
  - `onTerminalOpen(actorId, refId)` **blockable**;
  - `onTerminalHack(actorId, refId, success)` **blockable**;
  - `onTerminalMenuItem(actorId, refId, terminalFormId, itemId)` **blockable**;
  - `onHolotapePlay(actorId, noteId, terminalRefId)` **blockable**;
  - `onNoteRead(actorId, noteId)` and `onHolotapeChatter(actorId, noteId, text, number)` **not blockable**.
- **Properties:** `mp.get/set(refId, "lock")` (`set` applies and broadcasts); `mp.get/set(refId, "terminal")` (hacked, clear lockouts); `mp.get(actorId, "readNotes")`.
- **Settings** (S22): `locks.minPickMs`, `terminals.minHackMs`, `terminals.maxResets`, `terminals.lockoutMs`, `terminals.hackScope`, `terminals.serverHacking` (T2).
- **Papyrus natives** (PVM-013/014): `Lock(abLock, abAsOwner)`, `Unlock`, `IsLocked`, `GetLockLevel`, `SetLockLevel`, `IsLockBroken` (always false in FO4 `[inference]`), `GetKey`, `AddKeyIfNeeded`, `Terminal.ShowOnPipboy` (→ client SpSnippet, S15).
- **Papyrus events:** `OnLockStateChanged`, `Terminal.OnMenuItemRun(itemId, terminalRef)` on the TERM form script, `OnHolotapePlay(terminalRef)`, `OnHolotapeChatter`, `OnActivate`.

### 4.10 Edge cases & failure modes
- **Relock:** only by cell reset (F14/SRV-080 restores ESM `XLOC`) or Papyrus/gamemode. SkyMP's DOOR 3 s reloot must not touch `lock` (B6).
- **Two players at one lock:** one session at a time (`Busy`). The second player may still use a key.
- **Lock picked while a key holder opens it:** the first committed unlock wins; the other session ends with `outcome ok=false`.
- **Disconnect mid-minigame:** the session is dropped and no pins are consumed. Since the engine consumed pins locally, the reconnect inventory snapshot restores them.
- **Terminal that unlocks a linked door** (level 253 "Requires Terminal"): unlocked only via that terminal's action.
- **Leveled locks** (`XLOC` 0x4): the server resolves a fixed level on first load from `XLOC` (no player-level scaling) `[inference]`.
- **Plugin removed:** an unknown key or terminal in a change form falls back to the ESM default.

### 4.11 Performance budget
- All messages ≤ 32 B except holotape chatter (≤ 256 B).
- Lock state is one property per ref (≤ 8 B) and is included in `CreateActorFo4` only for locked refs.
- Validation ≤ 20 µs; terminal fragment execution is bounded by the PVM step limit.

## 5. Engine / platform work required
- **PLAT-085:**
  - lock read/write natives (`REFR_LOCK::SetLocked` NG/AE 2191020, `setLockLevel`);
  - `openLockpickingMenu(ref)` and `LockpickingMenu` result hook (`picksBroken`, success/exit);
  - `showTerminal(ref)`, `TerminalMenu` mode hook (hack success/fail, lockout), `OnMenuItemRun` capture, `closeTerminal()`;
  - `setNoteRead(form, bool)`.
- **Client:** remove the unconditional unlock in the FO4 fork of `objectReferenceEx.ts` (`dealWithRef`).
- **T2:** a Scaleform injection or CEF overlay to render a server-provided hacking word list.

## 6. Tests
- `L-unit` (`[F24]`, `[Lock]`, `[Terminal]`):
  - round trips for `LockpickAttempt`, `TerminalAction`, `NoteAction`;
  - key unlock;
  - perk gate per level (Locksmith 0–3) and barred/chained/terminal/key-only refusal;
  - pin accounting (normal, Locksmith 4, more broken than held → reject);
  - `minPickMs` reject; session expiry; `Busy`;
  - XP awarded via F19;
  - gamemode veto with correction;
  - `lock` property in the late-joiner `CreateActorFo4` and to neighbours on change;
  - DOOR reloot leaves `lock` untouched; cell reset restores the ESM lock (fake clock);
  - Hacker gate; lockout timing (fake clock) and Hacker 4 exemption;
  - menu item reachability and CTDA; native action table effects (linked door unlocked, turrets made friendly);
  - holotape and `NoteAction` persistence in `PlayerProfile`;
  - persistence round trip of `lock`/`terminal`; backward-compatible load.
- `L-fixture`: synthetic REFR with `XLOC` levels, KEYM, TERM with submenus/ITIDs/CTDA, NOTE types.
- `L-int`: bot A picks a safe, bot B (neighbour) sees it unlocked; A hacks a terminal that unlocks a door; B opens the door; server restart keeps both.
- `L-ts`: capture state machines (lockpick menu, terminal mode transitions); `dealWithRef` no longer unlocks.
- `G-self`: open the lockpicking and terminal menus through the natives; hooks report `picksBroken` and the hack result.
- `G-manual`: two players: key door, lockpick with pin breaks, hack with lockout, terminal disables turrets, holotape read flag survives a reconnect on another PC.

## 7. Tasks
- [ ] **F24-T01** Messages `LockpickAttempt` (99), `TerminalAction` (100), `NoteAction` (110) + TS mirrors — S — Depends: NET-002 — Verify: L-unit, L-ts — Files: skymp5-server/cpp/messages/{LockpickAttemptMessage.h,TerminalActionMessage.h,NoteActionMessage.h}, Messages.h; falloutmp-client/src/services/messages/
- [ ] **F24-T02** `LockState` ChangeForm field + built-in `lock` property + `CreateActorFo4` prop; ESM `XLOC` default — S — Depends: ESPM-005, REF-020 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/{MpChangeForms.{h,cpp},MpObjectReference.{h,cpp}}, property bindings
- [ ] **F24-T03** `LockService`: activation branch (key, refusal levels, perk gate, pins), sessions, pick result, XP, crime flag, `onLockpick`/`onUnlock`, corrections — M — Depends: F24-T01, F24-T02, SRV-021, F19, F07-T04, NET-007 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/fo4/locks/LockService.{h,cpp}, gamemode_events/LockpickEvent.{h,cpp}
  - Accept: every lock case in §6 passes.
- [ ] **F24-T04** Relock rules: cell-reset restore (SRV-080), DOOR reloot exclusion — S — Depends: F24-T02, SRV-080 — Verify: L-unit
- [ ] **F24-T05** Load `TERM` refs and `NTRM` embedded terminals (REF-012); `TerminalState` field — S — Depends: REF-012, ESPM-010 — Verify: L-unit, L-fixture
- [ ] **F24-T06** `TerminalService`: open/occupancy, hack plausibility, lockouts, menu-item reachability + CTDA, `onTerminal*` events — M — Depends: F24-T01, F24-T05, ESPM-011, F07 — Verify: L-unit — Files: fo4/terminals/TerminalService.{h,cpp}
- [ ] **F24-T07** Terminal action execution: native action table (`data/fo4/terminalActions.json`) now; server-VM fragment dispatch when PVM supports TERM fragments — M — Depends: F24-T06, PVM-007, PVM-014, F13 — Verify: L-unit
- [ ] **F24-T08** Notes/holotapes: `NoteAction`, `PlayerProfile.readNotes`, owner-only `readNotes` property, `OnHolotapePlay` dispatch, chatter forwarding — S — Depends: F24-T01, SRV-060 — Verify: L-unit
- [ ] **F24-T09** Server Papyrus lock natives, `OnLockStateChanged`, `OnMenuItemRun`, `ShowOnPipboy` snippet — S — Depends: PVM-013, PVM-014, F24-T03 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/script_classes/{PapyrusObjectReference.cpp,PapyrusTerminal.cpp}
- [ ] **F24-T10** Platform natives and hooks (PLAT-085 scope in §5) — M — Depends: PLAT-085, PLAT-036, PLAT-042 — Verify: W-ci, G-self — Files: fallout4-platform/src/.../LockTerminalApi.cpp
  - Accept: the self-test opens both menus on a test ref and reports hook payloads.
- [ ] **F24-T11** Client `lockService.ts`/`terminalService.ts`/`noteService.ts`; remove the unlock in `objectReferenceEx.ts` (FO4 fork); apply the `lock` property — M — Depends: F24-T01, F24-T10, CLI-050 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/{lockService,terminalService,noteService}.ts, falloutmp-client/src/extensions/objectReferenceEx.ts
- [ ] **F24-T12** `G-manual` locks/terminals scenario script and sign-off — S — Depends: F24-T11 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F24-locks-terminals.md
- [ ] **F24-T13** (T2) Server-authoritative hacking (word list, `guess`/`likeness`, custom UI) — L — Depends: F24-T06, FRONT-002 — Verify: L-unit, G-manual
- [ ] **F24-T14** (T2) Robot hacking (Robotics Expert) and the Automatron lockpick module — M — Depends: F21, F24-T03 — Verify: L-unit, G-manual

## 8. Open questions & risks
- **Milestone mismatch:** F24 is in M9, but PLAT-085 (lock/terminal natives) is listed under M10 in 03-milestones.md. Either move PLAT-085 to M9 or accept server-only F24 work in M9.
- Is a reliable hack success/failure signal available from `TerminalMenu` mode transitions or `TerminalHacked::Event` (lx getter exists, commonlib-port-map §4.2)? Is the failure/lockout state observable?
- Lock XP values may already include INT; hack XP tiers are `[inference]` (G-self logging, F19).
- Which terminal fragments matter most for the native action table, before server-VM fragments are available: measure on D-real (`TERM` VMAD scan).
- Can the engine's local unlock on minigame success be suppressed, or only reverted? A revert can show a brief open-able window; the server still refuses activation until unlocked.
