# F07 — Activation, Doors, Load Doors, Elevators, Switches & Furniture

| Field | Value |
|---|---|
| Tier | T0 (power-dependent objects: T1 with F22) |
| Target level | L4 |
| SkyMP analogue | `Activate` (6) → `MpObjectReference::Activate` / `ProcessActivateNormal` (DOOR/teleport, CONT, ACTI, FURN branches) / `ProcessActivateSecond` / `ActivateChilds` [src: MpObjectReference.cpp:440-505, 1419-1638]; `Teleport` (20); `isOpen` UpdateProperty; client `activationService.ts`. SkyMP level L4 (reference/skymp-sync-inventory.md §2, Activation rows) |
| Milestone | M6 |
| Workstreams | SRV, CLI, PLAT, ESPM, PVM, GM |
| Depends on | F00, F01 (server-initiated cell changes, rider allowance), F04, ESPM-004 (VMAD v6), ESPM-005 (REFR `XTEL`/`XESP`/`XAPD`/`XAPR`/`XLKR`), ESPM-010 (DOOR/FURN/ACTI), PLAT-040, PLAT-036 (`blockPapyrusEvents`), SRV-030, SRV-070 (server clock), CLI-030 (`isDisabled` key), F24 (locks), F17 (PA frames), F22 (power) |
| References | reference/fo4-systems-world-economy.md §1 (B3, B4, B6, B8, B17), §2 S16, §3.1, §3.3, §4; reference/fo4-data-formats.md §4.3 (XTEL 36 B, XLOC, XESP, XAPD/XAPR), §4.17 (DOOR, FURN, ACTI); reference/skymp-sync-inventory.md §1.12, §1.13, §1.15 row 9, §1.18, §2, §3.2 (I11, I12, I15); reference/papyrus-api-map.md §0 item 5, §1.5 (Activator, Furniture, Door), §2.3.3, §4.3; reference/prior-art.md §3.1.10; reference/skyrim-coupling-index.md §1.9 (`:1338-1679`); ADR-011 |

## 1. Summary
Activating anything in the world (doors, load doors, elevators and their call buttons, switches and levers, chairs, beds, workbenches, power-armor frames, activators) is a request to the server. The server checks who is activating, whether they are close enough, whether the object is free, locked or powered, and whether the gamemode allows it; then it changes the authoritative state and every nearby player sees the same result: the same doors open, the same elevator car at the same floor, the same lights on, the same person sitting in the chair. Load doors move the player between cells under server control. Activation parents, enable parents and server Papyrus `OnActivate` behave like the vanilla game, without running vanilla door/elevator scripts on each client.

## 2. Vanilla Fallout 4 behaviour
- **Doors:** `DOOR.FNAM` flags 0x2 Automatic, 0x4 Hidden, 0x8 Minimal Use, 0x10 Sliding, 0x20 Do Not Open in Combat Search; `TNAM[]` random teleport destinations (fo4-data-formats §4.17). `GetOpenState()` 0 none, 1 open, 2 opening, 3 closed, 4 closing; `SetOpen(bool)` (papyrus-api-map §2.3.3).
- **Load doors:** destination on the REFR `XTEL` (36 bytes in FO4: destination door, pos[3], rot[3] radians, flags 0x1 No Alarm / 0x2 No Load Screen / 0x4 Relative Position, transition-interior CELL). libespm reads the first 28 bytes, which stay compatible (fo4-data-formats §4.3).
- **Locks:** `XLOC` level 0/1/25/50/75/100, 251 Barred, 252 Chained, 253 Requires Terminal, 254 Inaccessible, 255 Requires Key (fo4-data-formats §4.3). Lock state is F24.
- **Elevators:** either load-door style, or scripted cars driven by `ElevatorMasterScript` (call buttons, `MakeElevatorFunctional()`), using `TranslateTo`/`SplineTranslateTo` + `OnTranslationComplete` and `Game.SetPlayerOnElevator(bool)`; Contraptions adds buildable 2–4 floor elevators (world-economy §2 S16).
- **Activation parents/children:** `XAPD` parent-activate-only, `XAPR {ref, delay}`; enable parents `XESP {parent, flags 0x1 opposite, 0x2 pop-in}`; linked refs `XLKR` (fo4-data-formats §4.3).
- **Furniture:** FURN flags 0x4 Has Container, 0x80 Is Perch, **0x2000000 Power Armor**; `MNAM` bits 0–21 interaction points, 0x80000000 sleep furniture; `WBDT` bench type (1 Create Object, 2 Weapons, 5 Alchemy, 7 Armor, 8 Power Armor, 9 Robot Mod) (fo4-data-formats §4.17). `TESFurnitureEvent{actor, targetFurniture, kEnter|kExit}`; F4SE `GetFurnitureReference`.
- **Activators/switches:** ACTI (`FNAM` 0x10 Is a Radio, `KNAM` interaction keyword); switches toggle linked refs (lights, traps, gates); many workshop objects need power (`IsPowered`, `OnPowerOn/Off`).
- **Engine events:** `TESActivateEvent{objectActivated, actionRef}` (AE 2201819), `TESFurnitureEvent` (AE 2201844) (commonlib-port-map §4.2). Papyrus: `Activate(akActivator, abDefaultProcessingOnly)`, `BlockActivation(abBlocked, abHideActivateText)`, `OnActivate`, `OnOpen/OnClose`.
- **Single-player assumptions that break:** scripted movers run locally, so cars sit at different floors per client; power state is local; Papyrus on activators is blocked in SkyMP-style clients; FO4_Wrld found `SetOpenState` fires thousands of times at load and must not be used as a capture signal (prior-art §3.1.10).

## 3. SkyMP baseline
- `Activate`: `CheckInteractionAbility` (same world/cell only, **no distance**, B4/I15); activation-parent-only refs rejected; `ActivateEvent` (gamemode veto); `activationBlocked` gate (not persisted, I11); Papyrus `OnActivate`; `ActivateChilds` with per-child delay [src: MpObjectReference.cpp:440-505, 1607-1638].
- DOOR: teleport door → `Teleport` to `GetActorToSendTo()`, server sets the actor's cell/pos/angle, door `isOpen` with 3 s reloot auto-close; normal door toggles `isOpen` [src: MpObjectReference.cpp:1467-1511].
- FURN: single occupant within 256 u; `OpenContainer` used as "activation granted"; second activation releases; occupancy not persisted, not broadcast [src: MpObjectReference.cpp:1543-1605].
- ACTI: `OpenContainer` "granted" so the client runs the default activation [src: MpObjectReference.cpp:1539-1541].
- Rejections send nothing; the client already blocked the local activation (`blockActivation`, B17).
- **Reuse:** the whole pipeline, teleport math, activation children, gamemode event, "granted" round trip. **Adapt:** FO4 record layouts, reach, corrections, furniture model. **Add:** movers (elevators), switches/power, enable parents, PA-frame hand-off, persistence of `activationBlocked`.

## 4. Design

### 4.1 Authority model
Class A: open/closed state, lock gate (F24), occupancy, mover (elevator) state, switch state, enable state, activation-blocked flag, cell changes through load doors. Door/elevator *animation* is class D, driven from authoritative state and server time. Sitting/getting up animation is class B (F02).

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Door open | bool | existing `isOpen` | yes | ESM (`ONAM` open by default) |
| Activation blocked | bool | `activationBlocked` (existing) | **yes (new, I11)** | false |
| Furniture occupants | `map<markerIdx, actorId>` | `MpObjectReference::occupants` (replaces single occupant for FURN) | no | empty |
| Mover state | `MoverState{floor u8, fromFloor, toFloor, startServerMs u64, durationMs u32}` | `ElevatorService` + `ChangeForm.mover` (new) | yes (resting floor; in-flight moves complete on load) | ESM car position = floor 0 |
| Switch state | bool | `ChangeForm.switchOn` (new) | yes | ESM default |
| Powered | bool | `ChangeForm.powered` (new; computed by F22) | yes | ESM/F22 |
| Enabled state of enable-children | bool | existing `isDisabled` | yes | ESM flags + `XESP` |
| Actor cell/pos after load door | — | F01 fields | yes (immediate on cell change) | — |

### 4.3 Protocol
No new message types.

| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `Activate` (6) | C→S | `caster` (0x14 or hosted NPC), `target`, `isSecondActivation` (leave furniture) | R | event | reused |
| `OpenContainer` (21) | S→C | `target` = "activation granted": client runs the default activation (sit, use workbench, flora, activator) | R | on accept | reused (SkyMP semantics) |
| `Teleport` (20) | S→C | `idx, pos, rot, worldOrCell` | R | load door / elevator load door | reused |
| `UpdateProperty` (7) | S→C | `isOpen`, `activationBlocked`, `occupants`, `moverState`, `switchOn`, `powered`, `isDisabled` | R | on change | reused |
| `CreateActorFo4` / `CreateActor` | S→C | same props in the snapshot | R | on subscribe | reused |

`moverState` is a JSON property `{floor, fromFloor, toFloor, startServerMs, durationMs}`; clients convert `startServerMs` with the server clock from `WorldTimeWeather` (105, F25).

### 4.4 Client capture (owner side)
- `activationService` (fork): on `activate` (`TESActivateEvent`) by the player, block the local default for server-controlled types (DOOR, CONT, FURN, ACTI, FLOR, items, NPC_, TERM, PA frames) via `BlockActivation(true, false)` on stream-in, and send `Activate`. Automatic doors (`FNAM` 0x2) are left to local proximity behaviour (class D, no gameplay effect).
- Furniture exit: `TESFurnitureEvent kExit` → `Activate{isSecondActivation:true}`.
- Hosted NPCs: the host sends `Activate` with `caster` = NPC for doors and furniture its AI uses.
- Never capture `SetOpenState` (fires on load, prior-art §3.1.10).

### 4.5 Apply
- **Doors:** `isOpen` → `SetOpen(bool)`; on stream-in snap to the final state without animation.
- **Load doors:** `Teleport` → `moveRefrToPosition` (F00/PLAT-051) with loading screen unless `No Load Screen`. F01 pauses movement sends during loading.
- **Granted activation:** `OpenContainer{target}` for FURN/ACTI → `target.Activate(player, true)` with the local block bypassed once (SkyMP pattern).
- **Remote occupants:** others see the sit through F02 (`furnitureRef`, sit state); `occupants` keeps the "occupied" prompt correct.
- **Elevators:** on `moverState` with `toFloor ≠ floor`, the client computes elapsed = serverNow − `startServerMs` and drives the car with `TranslateTo` to the target marker at the remaining speed; late joiners jump to the interpolated position. Riders keep `Game.SetPlayerOnElevator(true)` (SpSnippet) while inside the car bounds.
- **Switches/powered:** `switchOn` → play the activator's on/off animation and apply linked-ref effects that the server mirrored (enable/disable, `isOpen`); `powered` → platform native that forces the receiver's powered visual state (RE, §5).
- **Reconnect/respawn:** all state comes from the snapshot; no client-side door/elevator memory.

### 4.6 Validation & anti-cheat
1. Caster is the sender's actor or an NPC it hosts (S6); activation of user-controlled actors is refused.
2. **Reach and occupancy (I15, F07-T04):** distance from the caster's server position to the target's bounds ≤ `GameProfile::ActivationReach(type)` (setting `activationReach`, default 300 u, per-type overrides) + latency slack (default 64 u). Shared by F06 (open, peek, pickup), F15/F16 (bench use).
3. Same world/cell (existing), not disabled/deleted, activation-parent-only rule (existing).
4. Locks: a locked DOOR/CONT/TERM consults F24 (key in inventory → server unlock; otherwise the lockpick flow; barred/chained/terminal/inaccessible → refuse with a notification).
5. Power: targets with `PowerRequired > 0` and `powered == false` → refuse (notification).
6. Furniture: free marker exists (popcount of `MNAM` interaction bits; workbenches and PA frames are single-occupant); actor not dead, not in another furniture.
7. Load doors: destination door must resolve (`XTEL`); random `TNAM` destinations are refused for now (§8).
8. Rate limit ≤ 10 activations/s per actor.
9. **Every reject sends a correction (I12):** the target's authoritative state (`isOpen`, `occupants`, `moverState`…) as `UpdateProperty` to the sender plus a `Debug.Notification` SpSnippet with the reason (locked, too far, occupied, no power). Activation vetoed by the gamemode sends the same correction.

### 4.7 Audience / visibility
State changes go to all grid listeners of the object (reliability honoured, NET-006). `Teleport` goes to the activator's owner/hoster only (`GetActorToSendTo`). Notifications go to the sender only.

### 4.8 NPC parity
Hosted NPCs may activate doors, load doors and furniture (`caster` = NPC). A load door moves the NPC on the server (cell/pos) and sends `Teleport` to the hoster, which then re-grids the NPC; other clients see `DestroyActor`/`CreateActor` across the cell boundary. NPC furniture use (sandbox sitting) occupies markers like players. NPCs never use elevators' call buttons; their AI rides only load-door elevators.

### 4.9 Gamemode API & server Papyrus
- Events: `onActivate(refId, casterId)` (existing, blockable; veto now corrected), new `onFurnitureEnter(actorId, furnId) [blockable]`, `onFurnitureExit` (observe), `onMoverStart(refId, fromFloor, toFloor) [blockable]`, `onSwitch(refId, actorId, newState) [blockable]`.
- Properties: `isOpen` (get/set), `activationBlocked` (get/set, persisted), `occupants` (get), `moverState` (get), `switchOn` (get/set), `powered` (get; set by F22).
- Settings: `activationReach`, `activationReachByType`, `activationSlack`, `elevators.speedUnitsPerSec` (default from script data, else 200).
- Papyrus events: `OnActivate(akActionRef)`; `SkympOnActivateClose` moves to a **native stub** script (FO4 forbids event declarations in non-native scripts, papyrus-api-map §0 item 5); `OnOpen`/`OnClose`; `OnSit`/`OnGetUp`/`OnExitFurniture` (P1); `OnTranslationComplete` for server movers; `OnPowerOn`/`OnPowerOff` (with F22).
- Papyrus natives: `Activate`, `BlockActivation`, `IsActivationBlocked`, `SetOpen`, `GetOpenState` (0–4 from server state), `TranslateTo`/`SplineTranslateTo`/`StopTranslation` on server-owned refs → `MoverState`, `IsFurnitureInUse`, `Game.SetPlayerOnElevator` (SpSnippet to the owner), `Enable`/`Disable` with enable-children propagation.
- **ADR-011 allow-list:** each vanilla script found on DOOR/ACTI/FURN refs is classified as (S) runs in the server VM (natives available), (N) re-implemented natively (elevators), (C) allowed on clients (pure cosmetics), or (D) denied. The client list (`blockPapyrusEvents` allow-list, PLAT-036) and the server list (`GameProfile::DeniedScripts`/allowed scripts, REF-011) are generated from one table, `falloutmp-scripts/policy/activation-scripts.json`.

### 4.10 Edge cases & failure modes
- **Disconnect while seated or riding:** occupancy released; the rider's last position persists; reconnect places the actor at the car's current floor if the saved position is inside the car shaft.
- **Two players press call buttons on different floors:** queued; one car, FIFO per car, each move broadcast.
- **Server restart mid-ride:** the move completes on load (car at `toFloor`), riders stay where their position was saved.
- **Door closed on a player:** cosmetic; collision is local.
- **Teleport door auto-close** (3 s reloot) must not touch lock state (B6 note; F24).
- **Enable parent toggled while children are subscribed:** children get `isDisabled` UpdateProperty (requires CLI-030 key fix).
- **Hot reload of the gamemode:** properties keep their persisted values.

### 4.11 Performance budget
Activation handling ≤ 20 µs server time (reach check + branch). `UpdateProperty` ≤ 200 B. Elevator: one property update per move, no per-frame traffic. Activation-children timers bounded by the ESM data.

## 5. Engine / platform work required
- Event sinks `TESActivateEvent`, `TESFurnitureEvent` (PLAT-040); `BlockActivation` semantics on remote/ESM refs; one-shot bypass for granted activation.
- Mover driver: `TranslateTo` with server-time catch-up, `SetPlayerOnElevator`; a native to force an object's powered visual state (RE, world-economy §5 item 2).
- Load-door teleport via `moveRefrToPosition` (PLAT-051).
- D-real tool: dump VMAD of all DOOR/ACTI/FURN refs in Fallout4.esm + DLCs (script names, properties) with the FO4 PEX/VMAD reader (ESPM-004, PVM-002), for the ADR-011 table and elevator configs.

## 6. Tests
- `L-unit` (`[F07]`, `[Activate]`):
  - accept paths: door toggles `isOpen` to listeners; load door sends `Teleport` and moves the actor's cell (F01 accepts the change); furniture grant via `OpenContainer`; ACTI grant; activation children fire with delays;
  - reject + correction: too far (I15), parent-only child, occupied furniture, locked door without key, unpowered object, gamemode veto;
  - wrong caster / wrong host → drop;
  - multi-marker bench: two occupants allowed, third refused; workbench single occupant; release on exit, distance, death, disconnect;
  - enable parent propagates `isDisabled` to children, including initially disabled refs loaded via the parent (B8 part);
  - `activationBlocked` persistence round trip (I11);
  - elevator: call → `moverState` with correct start/duration; queued calls; restart mid-move resolves to `toFloor`; F01 rider allowance active only while moving;
  - late joiner gets `isOpen`, `occupants`, `moverState`, `switchOn`;
  - `onPapyrusEvent:OnActivate`, `OnOpen` observed; `SkympOnActivateClose` from the native stub.
- `L-fixture`: synthetic REFR with 36-byte `XTEL` (transition interior, flags) and FURN with `MNAM`/`WBDT`.
- `L-ts`: mover interpolation from server time; granted-activation bypass.
- `L-int`: two bots open the same door; one rides an elevator while the other calls it.
- `G-self`: TESActivate/TESFurniture sinks fire; `TranslateTo` on a car with catch-up.
- `G-manual`: doors, load doors (interior↔exterior), Vault 111-style elevator ride with two players, chairs/benches, workbench occupancy, switches.

## 7. Tasks
- [ ] **F07-T01** FO4 activation pipeline: per-base-type branch table via GameProfile (DOOR, CONT, FURN, ACTI, FLOR, items, dead NPC_, TERM → F24, PA FURN → F17), caster checks, corrections on every reject (I12) — M — Depends: REF-012, NET-007 — Verify: L-unit — Files: MpObjectReference.cpp (`Activate`, `ProcessActivateNormal`), skymp5-server/cpp/server_guest_lib/game_profile/fallout4/Fo4ActivationRules.{h,cpp}, unit/PartOne_ActivateFo4Test.cpp
- [ ] **F07-T02** Doors: `isOpen`, FO4 `FNAM` flags (Automatic = local), lock gate hook (F24), 3 s teleport-door auto-close without lock changes — S — Depends: F07-T01, ESPM-010 — Verify: L-unit
- [ ] **F07-T03** Load doors: 36-byte `XTEL` (transition interior, flags), `Teleport`, server cell change accepted by F01, hosted-NPC teleport, `TNAM` refusal — M — Depends: F07-T01, ESPM-005, F01-T05 — Verify: L-unit, L-fixture — Files: MpObjectReference.cpp, libespm/src/REFR.cpp
- [ ] **F07-T04** Distance & occupancy checks (I15) for activate/pickup/peek/craft/workbench: `GameProfile::ActivationReach(type)`, bounds-aware distance, slack, occupancy query API for F06/F15/F16 — M — Depends: REF-004 — Verify: L-unit — Files: MpObjectReference.cpp (`CheckInteractionAbility`), skymp5-server/cpp/server_guest_lib/InteractionRules.{h,cpp}
  - Accept: activation from 2000 u in the same cell is rejected with a correction; Skyrim default reach values unchanged under `SkyrimGameProfile`.
- [ ] **F07-T05** Furniture & workbenches: multi-marker occupancy, single-occupant benches, release paths, `occupants` property, bed hook for F25 — M — Depends: F07-T04, ESPM-010 — Verify: L-unit
- [ ] **F07-T06** PA frame furniture routing to F17 (`PowerArmorTransition`), no generic occupancy — S — Depends: F07-T01, F17 — Verify: L-unit
- [ ] **F07-T07** Activation parents/children and enable parents (`XAPD`/`XAPR`/`XESP`), loading initially-disabled refs that have an enable parent (B8 part), persisted `activationBlocked` (I11, with F00-T08) — M — Depends: F07-T01, ESPM-005 — Verify: L-unit — Files: WorldState.cpp (`AttachEspmRecord`), MpObjectReference.cpp, MpChangeForms.{h,cpp}
- [ ] **F07-T08** Elevators: load-door elevators via T03; `ElevatorService` for scripted cars (config from VMAD dump / `data/fo4/elevators.json`, call buttons, FIFO queue, `MoverState`, door interlock, rider allowance exported to F01, restart completion) — L — Depends: F07-T10, SRV-070, F01-T05 — Verify: L-unit, G-manual — Files: skymp5-server/cpp/server_guest_lib/fo4/ElevatorService.{h,cpp}
- [ ] **F07-T09** Switches & powered objects: `switchOn`, linked-ref effects (server VM script if allow-listed, else native mapping), power gate and `powered` property fed by F22 — M — Depends: F07-T10, SRV-030 — Verify: L-unit
- [ ] **F07-T10** ADR-011 allow-list for door/elevator/switch/furniture scripts: D-real VMAD dump tool, classification table, generated client and server lists — M — Depends: ESPM-004, PVM-002, PLAT-036, REF-011 — Verify: D-real, L-unit — Files: tools/fo4-vmad-dump/, falloutmp-scripts/policy/activation-scripts.json
  - Accept: every script on DOOR/ACTI/FURN refs in Fallout4.esm has a class; the lists are generated, not hand-edited.
- [ ] **F07-T11** Server Papyrus: FO4 `OnActivate`, `SkympOnActivateClose` native stub, `OnOpen/OnClose`, `OnSit/OnGetUp/OnExitFurniture`, `OnTranslationComplete`, natives listed in §4.9 — M — Depends: PVM-013, PVM-014, PVM-007 — Verify: L-unit — Files: script_classes/PapyrusObjectReference.cpp, falloutmp-scripts/
- [ ] **F07-T12** Client: `activationService` fork (FO4 block list, granted-activation bypass, furniture exit capture), door/furniture/switch/mover apply — M — Depends: F07-T13, CLI-050 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/activationService.ts, falloutmp-client/src/view/modelApplyUtils.ts
- [ ] **F07-T13** Platform: activation/furniture sinks, `BlockActivation` behaviour, mover driver with server time, powered-visual native (RE) — M — Depends: PLAT-040, PLAT-051 — Verify: W-ci, G-self — Files: fallout4-platform/src/platform_fo4/ActivationApi.cpp
- [ ] **F07-T14** Gamemode: `onFurnitureEnter/Exit`, `onMoverStart`, `onSwitch`, properties of §4.9, docs — S — Depends: F07-T05, F07-T08, F07-T09 — Verify: L-unit, L-int
- [ ] **F07-T15** `G-manual` world-interaction scenario script and sign-off — S — Depends: F07-T12 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F07-activation.md

## 8. Open questions & risks
- Elevator configuration depends on `ElevatorMasterScript` property names, which are only visible in the VMAD dump (D-real); if the data is irregular, a hand-maintained `elevators.json` is the fallback.
- Riders on moving cars combine owner-authoritative movement with a server mover; jitter at floor arrival is likely (F01 validation slack).
- Random-destination doors (`DOOR.TNAM`) semantics are unverified; refused at T0.
- Forcing powered visuals on clients needs RE (world-economy §5 item 2); until then power-dependent objects may look unpowered on some clients.
