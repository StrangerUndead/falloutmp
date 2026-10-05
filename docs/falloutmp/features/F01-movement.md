# F01 — Movement, Aim & Positioning

| Field | Value |
|---|---|
| Tier | T0 |
| Target level | L3+ (SkyMP L3; plus speed-sanity validation and validate-before-relay) |
| SkyMP analogue | `UpdateMovement` (MsgType 2), 130 ms, `movementGet.ts`/`movementApply.ts`, `MovementValidation.cpp`. SkyMP level L3 |
| Milestone | M5 |
| Workstreams | CLI, PLAT, SRV, NET |
| Depends on | F00, F02-T01 (prototype informs the variable set), PLAT-030 (reflection), PLAT-070 (remote-actor AI suppression natives) |
| References | reference/skymp-sync-inventory.md §1.7, §2 (Movement row); reference/fo4-animation-sync.md §1.4, §3.2, §4.5, §7.1 items 10–13; reference/prior-art.md §5.1 C9, §5.3 Adopt 2 |

## 1. Summary
Every client sees every other player and hosted NPC move smoothly and in the right place: walking, running, sprinting, sneaking, jumping, swimming, aiming (pitch/heading), in or out of power armor, and in furniture. Position is owner-authoritative but validated by the server before relay. Remote actors are positioned natively by the platform with an interpolation buffer. FO4 has no `KeepOffsetFromActor` to drive locomotion.

## 2. Vanilla Fallout 4 behaviour
- There is no `Debug.SendAnimationEvent` and no `Actor.KeepOffsetFromActor` in FO4 Papyrus [src: f4se/scripts/vanilla/Actor.psc, Debug.psc].
  - `ObjectReference.TranslateTo`/`SplineTranslateTo` exist (ObjectReference.psc:940-946).
  - The `Actor.ForceMovement*` natives are `debugOnly`, so they are unusable in retail.
- Locomotion is driven by behaviour-graph variables written by the engine (`PopulateGraphVariables`: `Speed`, `iSyncIdleLocomotion`, `iSyncTurnState`, `iSyncForwardState`, `iSyncStrafeState`, `iSyncJumpState`). The bound-channel flush runs for the active graph and overwrites earlier writes of the same names (reference/fo4-animation-sync.md §3.2).
- **In first person the player's 3rd-person graph is parked.** Movement data must therefore come from engine state (position, velocity, movement controller) and the active graph, not from the 3rd-person graph.
- Aim: `Actor.IsInIronSights()`, graph variables `AimPitchCurrent`, `AimHeadingCurrent`, `Pitch`. Remote pitch must go through graph variables, not actor X rotation.
- Exterior cells are 4096 units, the same as Skyrim, so SkyMP's grid applies unchanged [inference; verify with CELL XCLC data].

## 3. SkyMP baseline
- Capture: `getMovement(refr)` (pos, rot, worldOrCell, runMode string, direction, speed, jump, sneak, block, weapon drawn, dead, health %, lookAt).
  - Sent unreliable every 130 ms for the player and each hosted NPC (`sendInputsService.ts:113-135`).
- Server: `OnUpdateMovement` relays raw bytes to neighbours **before** validation. `MovementValidation` rejects a world change or a jump of ≥ 4096 with `Teleport2` (own actor only). Position is saved at most every 30 s.
- Apply: `translateTo` 0.2 s extrapolation plus `keepOffsetFromActor(self)` drives the locomotion animation (not portable).
- Reuse: message plumbing, grid, persistence throttle, Teleport2 correction, host gating. Replace: capture fields, apply mechanism.

## 4. Design

### 4.1 Authority model
Class B: owner (or host) authoritative, server-validated **before** relay (I1). The server stores the last accepted transform per actor.

### 4.2 Server state & persistence
| State | Type | Where | Persisted | Default |
|---|---|---|---|---|
| Position, rotation (yaw), worldOrCell | float3, float3, FormDesc | `MpObjectReference` / ChangeForm `position`,`angle`,`worldOrCellDesc` | yes (≤ 30 s throttle; immediate on cell change) | ESM placement / spawn point |
| Last movement sample (ring buffer, 1 s @ 10 Hz) | struct | `MpActor::movementHistory` (new) | no | — |
| Coarse track (1 Hz × 16 s) for F26 discovery and F25 rest checks | struct | `MpActor::recentPositions` (new) | no | — |
| Flags (sneak, sprint, sighted, drawn, jump, swim, PA, furniture) | bitfield | `MpActor::lastMovementFlags` (new) | no (derived on respawn) | 0 |
| Speed bounds context | — | from GameProfile + actor state (PA, chems, encumbrance) | — | — |

The movement-history ring buffer is used by lag-compensated hit validation (F09/F11) and VATS-lite (F18).

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate |
|---|---|---|---|---|
| `UpdateMovementFo4` (65) | C→S, S→C relay | `idx`, `seq` (u16), `ts` (client ms, u32), `worldOrCell`, `pos[3]`, `yaw`, `aimPitch`, `aimHeading`, `speed`, `direction`, `velZ`, `flags` (u16 bitfield, fixed bit order: 0 sneaking, 1 sprinting, 2 sighted, 3 weaponDrawn, 4 inJump, 5 swimming, 6 inPowerArmor, 7 inFurniture, 8 isDead, 9 isBlocking, 10 encumbered, 11 lightOn (F28), 12 jetpackActive (F17), 13–15 reserved), `healthPercentage` (u8 quantized) | U | 100 ms, plus immediate on flag change; ≤ 64 B |
| `Teleport2` (31) | S→C | correction (unchanged) | R | on violation |

**Relay rewriting:** on relay, the server overwrites `ts` with its own receive time (server ms, used by F09 rewind) and `healthPercentage` with the server-authoritative value (F08-T12). `CreateActorFo4` carries the last accepted `flags`, so late joiners see light, PA and sneak state.
| `Teleport` (20) | S→C | server-initiated move (doors, fast travel, respawn) | R | event |

Locomotion graph variables travel in F02's `UpdateGraphVariables`. `UpdateMovementFo4` carries only what the server needs for validation and interest management.

### 4.4 Client capture (owner side)
- Each `update`, for the player and each hosted NPC, `getMovementFo4(actor)` reads:
  - `GetPositionX/Y/Z`, `GetAngleZ`, `GetWorldSpace() ?? GetParentCell()`;
  - speed and direction from a new native `getMovementState(ref)`, which reads the movement controller velocity. Do not use the 3rd-person graph in first person;
  - flags from `IsSneaking`, `IsSprinting`, `IsInIronSights`, `IsWeaponDrawn`, `IsInPowerArmor`, `IsSwimming` (native), `GetSitState`/furniture ref, `IsDead`;
  - aim pitch/heading from a native (camera/process data for the player, active graph for NPCs).
- Send at 100 ms, or immediately when `flags` change. Sequence number and timestamp on every packet.

### 4.5 Apply
- **Remote actors are never moved by Papyrus AI.** The platform suppresses AI movement for remote actors in the engine:
  - TE-style ActorProcess/SetPosition/Rotate/RunDetection hooks, or FO4_Wrld's character-controller warp (prior-art Adopt 2, C9; `PLAT-070`);
  - plus a DoNothing package on the template NPC.
- **Interpolation:** a 100–150 ms jitter buffer per remote actor (TE uses 300 ms; start at 120 ms, tunable). Each frame the platform native `setActorTransform(ref, pos, yaw)` performs a character-controller warp (no physics fighting). Locomotion variables come from F02.
- **Extrapolation:** at most 200 ms past the last sample, then hold.
- **Snap rules:** > 512 u error or a worldOrCell change → hard teleport (`MoveTo`). A worldOrCell mismatch with the local player → despawn/respawn (SkyMP `RespawnNeededError` semantics).
- **Aim:** apply pitch/heading through F02 variables (`AimPitchCurrent`, `AimHeadingCurrent`) after channel flush.
- **Stream-in:** the initial transform comes from the `CreateActorFo4` snapshot. The first movement sample snaps.
- **Own clone:** keep SkyMP's `show-me`/`show-clones` debug option (CLI-040).

### 4.6 Validation & anti-cheat (server, before relay)
1. Ownership: own actor or hosted NPC (S6). Otherwise `HostStop`/drop.
2. Monotonic `seq`. Drop stale or duplicate packets.
3. World/cell change only via a server-initiated teleport (door, fast travel, respawn) or an adjacent-cell exterior transition. Otherwise drop + `Teleport2`.
4. **Speed check:** distance/Δt ≤ GameProfile max speed for the current state:
   - walk, run, sprint, sneak, swim;
   - power armor and jetpack (vertical allowance);
   - plus effect multipliers (chems, perks) and a 25% tolerance plus a burst allowance.
   - Violations accumulate a score; above a threshold → `Teleport2` to the last good sample and log a metric (`movement_violation_total`).
5. Z sanity: no sustained flight without jetpack/PA. Falling is allowed.
6. Accepted samples go into `movementHistory`, then the packet is relayed to neighbours (not echoed to the sender unless `show-me` is on).

### 4.7 Audience
Grid neighbours (3×3) of the actor, excluding the sender.

### 4.8 NPC parity
Hosts send `UpdateMovementFo4` for hosted NPCs, with the same validation. Hosting rules: F13.

### 4.9 Gamemode API & Papyrus
- `mp.get(id,'pos')`, `angle`, `worldOrCellDesc` (read).
- `mp.set` for `locationalData` (teleport).
- Optional event `onMovementViolation(actorId, details)` (logging/kick policy in the gamemode; return false to ignore).
- No Papyrus events (`OnTranslation*` are for scripted objects).

### 4.10 Edge cases
- Interior coordinate collisions: the grid is keyed by cell id, so there are no collisions across interiors (prior-art open question 9 is solved by SkyMP's per-cell grid).
- Elevators and moving platforms: the server allows the relative motion while the actor is on an elevator ref (F07 state).
- Vertibird rides (if supported, F26): movement is owned by the vertibird host.
- Loading screens: the client pauses sending. On resume it sends a full sample with `flags` set.

### 4.11 Performance budget
- ≤ 64 B per sample at 10 Hz → ~640 B/s per actor up.
- Relay cost O(neighbours). Server validation ≤ 2 µs per sample.

## 5. Engine / platform work required
- `getMovementState(ref)` (velocity, speed, direction, swimming), `getAimState(ref)`, `setActorTransform(ref, pos, yaw)` (controller warp), remote-actor AI/motion suppression hooks, the DoNothing package template (`PLAT-070…073`).

## 6. Tests
- `L-unit`:
  - serialize round trip;
  - accept path relays to neighbours (not the sender);
  - speed violation → `Teleport2` and no relay;
  - world change without server teleport → `Teleport2`;
  - wrong owner → drop;
  - hosted NPC accepted;
  - stale `seq` dropped;
  - history buffer filled;
  - persistence throttle (30 s) and immediate save on cell change.
- `L-int`: two bots walking, with jitter injection; the receiver observes monotonic positions.
- `L-ts`: the interpolation buffer unit tests (pure TS, mocked clock).
- `G-manual`: two players walk, run, sprint, sneak, jump, swim and enter PA. Each scenario is checked in 1st and 3rd person on both sides; no rubber-banding at RTT 100 ms.

## 7. Tasks
- [ ] **F01-T01** Define `UpdateMovementFo4` (C++ struct + TS interface), register for FO4 — S — Depends: NET-002 — Verify: L-unit — Files: skymp5-server/cpp/messages/UpdateMovementFo4Message.h, Messages.h; falloutmp-client/src/services/messages/
  - Accept: binary and JSON round trip, ≤ 64 B binary.
- [ ] **F01-T02** Platform natives `getMovementState`, `getAimState`, `setActorTransform` — M — Depends: PLAT-030 — Verify: W-ci, G-self — Files: fallout4-platform/.../MovementApi.cpp
  - Accept: the self-test reads velocity while walking and teleports an NPC smoothly without physics jitter.
- [ ] **F01-T03** Remote-actor AI/motion suppression (engine hooks + template NPC package) — L — Depends: PLAT-070 — Verify: G-self — Files: fallout4-platform hooks
  - Accept: a spawned remote actor does not wander, flee or react to combat for 5 minutes while positioned by the client.
- [ ] **F01-T04** Client capture `movementGetFo4.ts` + send service (100 ms, flags-change trigger, seq/ts) — M — Depends: F01-T01, F01-T02 — Verify: L-ts — Files: falloutmp-client/src/sync/movementGet.ts, services/movementService.ts
- [ ] **F01-T05** Server: validate-before-relay, seq, speed model from GameProfile (incl. the jetpack allowance when bit 12 is set), `movementHistory` ring buffer, relay rewriting of `ts`/`healthPercentage`, metrics — M — Depends: F01-T01, REF-010 — Verify: L-unit — Files: ActionListener.cpp (FO4 handler), MovementValidation.cpp, MpActor.h
  - Accept: all §6 unit cases pass.
- [ ] **F01-T06** Client apply with jitter buffer and snap rules (`movementApplyFo4.ts`) — M — Depends: F01-T02, F02-T05 — Verify: L-ts, G-manual — Files: falloutmp-client/src/sync/movementApply.ts
- [ ] **F01-T07** GameProfile speed table (walk/run/sprint/sneak/swim/PA/jetpack + effect multipliers) — S — Depends: REF-010, F08 (AV access) — Verify: L-unit
- [ ] **F01-T08** Gamemode event `onMovementViolation` + docs — S — Depends: F01-T05 — Verify: L-int
- [ ] **F01-T09** `G-manual` movement scenario script and sign-off — S — Depends: F01-T06 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F01-movement.md

## 8. Open questions & risks
- Movement speed and fall physics vary with frame rate in vanilla FO4; **High FPS Physics Fix** (07-dependencies-and-mods.md) is recommended so the server speed model matches clients.
- The exact source of player velocity/direction in first person needs the F02-T01 prototype.
- Character-controller warp stability with physics (falling, stairs) on remote actors. Fallback: keyframed motion type while remote (FO4 keyframed motion type is 2, not SP's 4; papyrus-api-map §5).
