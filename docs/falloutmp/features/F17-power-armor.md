# F17 — Power Armor

| Field | Value |
|---|---|
| Tier | T1 |
| Target level | L4 |
| SkyMP analogue | None (closest: furniture occupancy in `Activate` + equipment). FO4_Wrld solved PA only for its scene-graph ghost (reference/prior-art.md §3.1.8, §5.4 Q4) |
| Milestone | M10 |
| Workstreams | SRV, CLI, PLAT, NET, GM |
| Depends on | F01 (PA/jetpack speed model), F02 (graph rebuild, PA descriptor F02-T10), F03 (face kept through the race switch), F04 (item instances: piece health, core charge), F05 (equipment), F06 (frame as container), F07 (activation reach I15), F08 (PA AVs, AP), F11 (piece damage), F12 (death), F16 (station modding), PLAT-084 |
| References | reference/fo4-systems-combat-character.md §12, §1.2 (PA AVs), §1.4 (PowerArmor hooks), §18 R8; reference/prior-art.md §3.1.8, §5.4 Q4; reference/fo4-animation-sync.md §3.4 (Power armor, Jetpack); reference/fo4-data-formats.md §4.17 (FURN 0x2000000, WBDT 8), §4.8 (AMMO health) |

## 1. Summary
Power armor frames are world objects that everyone sees and that persist. A frame has up to six pieces, each with its own condition, OMODs and paint, and may hold a fusion core. A player walks up and enters. The server checks that the frame is free, in reach and theirs (or that stealing is allowed). Their character then switches into the PA race and skeleton on every client, the frame disappears from the world, and the pieces and core move onto the wearer. While worn, the server drains the core by activity (sprint and jetpack faster, standing still free), damages pieces from hits, and forces "unpowered" movement when the core is empty. Exiting leaves the frame at the wearer's feet with its pieces. Frames are modded and repaired at a power armor station through F16. NPCs (Brotherhood, raiders) and companions can also wear PA.

## 2. Vanilla Fallout 4 behaviour
- **Frame:**
  - furniture with the FURN flag 0x2000000 Power Armor (fo4-data-formats §4.17);
  - empty frame 0x2079E; frame ARMO `ArmorPoweredFrame` 0x3E577 (combat-character §12.1) [web: https://fallout.fandom.com/wiki/Power_armor_frame];
  - frame alone: 60 DR / 60 ER, base STR set to 11, pieces weightless, no fall damage (landing hurts nearby actors).
- **Enter/exit engine machine** (1.11.191 RE by FO4_Wrld) [src: fo4wrld fw_native/src/hooks/pa_pipeline_trace.h:9-33]:
  - creates extra `kPowerArmor` 0xBB, pushes the six condition AVs, links the frame handle;
  - strips conflicting equipment;
  - switches the race to `PowerArmorRace` 0x1D31E;
  - transfers pieces from the frame inventory and equips them;
  - rebuilds the skeleton/3D, makes the frame persistent and **disables** it.
  - `Actor.SwitchToPowerArmor(frame)` pops in without an animation; `IsInPowerArmor()` = has perk 0x1F8A9 [src: f4se Actor.psc:390-393,784-785].
- **Pieces:** ARMO with `DATA.Health` (broken at 0, weight 0, unequippable until repaired).
  - Condition AVs: Head 0x2EF, Torso 0x2F0, LeftArm 0x2F1, RightArm 0x35D, LeftLeg 0x35E, RightLeg 0x388.
  - Each worn piece gives 5 % damage reduction (player only) and 14 % radiation reduction. NPC pieces take ×3 damage. A full matching paint set grants a bonus. Tiers and misc mods (jetpack torso OMODs 0x183575/0x183576/0x125C85/0x183578, calibrated legs +50 carry) are OMODs.
- **Fusion core:** `AmmoFusionCore` 0x75FE4, charge 100 (AMMO `Health`); ~20 min of jogging; no drain standing still or during fast travel; AP actions (sprint, VATS, hold breath, jetpack) drain faster.
  - Worn charge AV `PowerArmorBattery` 0x35C; GMST `fNewBatteryCapacity`; perks: Nuclear Physicist up to +100 %, bobblehead +10 % (entry points `ModAmmoHealthMult` 0x7D, `ModIncomingBatteryDamage` 0x76).
  - An empty core means slow walk and no AP actions. A targeted hit on the core can eject it (explosion, wearer forced out).
  - Companions and NPCs do not drain cores.
- **Jetpack:** `ActionJetpackStart/Stop` 0x125C7E/0x125C7F (fo4-animation-sync §3.4), MGEF archetype 48; AP cost plus faster drain.
- **Station:** furniture `WBDT` bench type 8; `PowerArmorModMenu` (not ExamineMenu; prior-art §3.1.8); `SyncFurnitureVisualsToInventory`.
- **HUD:** `PowerArmorHUDMenu`. `PlayerCharacter::lastUsedPowerArmor`.
- **Single-player assumptions that break:**
  - one "last used PA" pointer;
  - the frame is disabled only on the wearer's machine;
  - the race/skeleton swap, core drain and piece damage are local;
  - NPCs take frames;
  - the PA janitor deletes frames whose cell unloads (prior-art §3.1.8 pitfalls);
  - a TEMPORARY bit on a worn frame hung loads (FO4_Wrld).

## 3. SkyMP baseline
There is no PA analogue. Reused building blocks:
- `Activate` occupancy and reach (F07, I15);
- container operations on an object inventory (F06);
- equipment slot validation (F05);
- AV storage and cropping (F08);
- `SpSnippet` to the owner;
- `isDisabled` UpdateProperty (I5);
- server-placed object persistence (`MpChangeFormREFR`).

FO4_Wrld design facts adopted:
- the frame is a world ref tracked server-side with `worn_by`, handed back on disconnect (prior-art §3.1.8);
- a piece = an ARMO in the frame inventory (mounted ⇔ present);
- condition and core charge are `ExtraHealth`.

## 4. Design

### 4.1 Authority model
- **Class A:** frame existence/position/ownership, pieces (base, OMODs, health), core charge, wearer, occupancy, unpowered state, drain, piece damage.
- **Class B:** enter/exit animation and race switch on the owner (owner-run engine pipeline, server-commanded); movement with the PA flag (F01).
- **Class D:** HUD presentation.
- NPC wearers are class C for animation and behaviour; everything else stays class A.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Frame ref (pos, cell, enabled) | ref | frame `MpObjectReference` | `position`, `worldOrCellDesc`, `isDisabled` (existing) | ESM placement or `place()` |
| Frame owner | profileId | `paOwnerProfileId` (new) | yes | −1 (unowned); set on first entry if unowned |
| Wearer | actor FormDesc | `paWornBy` (new) | yes | none |
| Pieces (when not worn) | inventory entries `{baseId, omods[], health}` | frame inventory | `inv` (F04 extras `health`) | ESM `CNTO`/LVLI evaluated once (F14) |
| Core (when not worn) | AMMO entry with `charge` | frame inventory | `inv` extra `health` | ESM |
| Worn frame | FormDesc | actor `wornFrameRefId` (new) | yes | none |
| Worn pieces | inventory entries flagged `paPiece`, equipped | actor inventory/equipment | `inv`, equipment (F04/F05) | from frame |
| Worn core charge | AV `PowerArmorBattery` 0x35C | F08 store | `fo4Avs` (F08) | from core item |
| Piece condition | AVs 0x2EF/0x2F0/0x2F1/0x35D/0x35E/0x388 | F08 store, mirrored to piece health | from piece health | — |
| Transition | {phase, nonce, startedMs} | `MpActor::paTransition` (new) | no (resolved on restart: §4.10) | none |
| Unpowered | bool | derived (charge = 0 and no spare core) | no | — |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `PowerArmorTransition` (87) | C→S | `nonce`, `actorIdx` (own or hosted NPC/companion), `frameRefId`, `kind` (enter, exit, ack, ejectCore) | R | user action / pipeline completion | new (registry) |
| `PowerArmorTransition` (87) | S→C | `nonce`, `ok`, `error`, `actorIdx`, `frameRefId`, `phase` (entering, in, exiting, out), `exitPos[3]`, `exitYaw` | R | result to the requester; command to the owner or host | new (registry) |
| `PowerArmorState` (88) | S→C | `actorIdx` or `frameRefId`, `state`, `frameBaseId`, `pieces[] {slot u8, baseId, omods[], healthPct u8}`, `coreBaseId`, `coreChargePct` (owner: exact float; neighbours: omitted), `flags` (unpowered, jetpackCapable, paintSet) | R | on state/piece change; in the snapshot | new (registry) |
| `CreateActorFo4` (64) | S→C | `props.powerArmor` = the `PowerArmorState` payload | R | subscribe | reused |
| `ChangeValuesAv` (72) | S→C | battery and piece AVs (owner); AP (F08) | R | on change, ≤ 2 Hz for battery | reused |
| `UpdateMovementFo4` (65) | C→S | `flags.inPowerArmor`; jetpack flag (see §8) | U | F01 | reused |
| `ModItem` (85), `TakeItemFo4`/`PutItemFo4` (70/69) | C→S | frame as the target container/item holder | R | station use | reused (F16/F06) |

### 4.4 Client capture (owner side)
- Block vanilla activation of PA furniture (frames have the FURN 0x2000000 flag / `FurnitureTypePowerArmor` keyword 0x3430B). Send `PowerArmorTransition{enter}` instead of `Activate`.
- **Exit:** intercept the exit input (hold activate while in PA; `PowerArmorHUDMenu` context) → `PowerArmorTransition{exit}`.
- **Ack:** after the engine pipeline reports completion, send `kind: ack` with the same nonce. The platform event is `TESSwitchRaceCompleteEvent` / `OnRaceSwitchComplete`, or `ExitPowerArmor` (fo4-animation-sync §3.4).
- **Jetpack:** set the movement flag while the jetpack action is active (F02 action `ActionJetpackStart`); AP and drain are computed on the server.
- **Station:** the F16 workbench flow with the frame ref as the item holder.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
**Owner, on `PowerArmorTransition{phase: entering}`:**
1. `enterPowerArmor(actor, frameRef, animated=true)` (PLAT-084) on the client's copy of the frame. The pieces are already in the frame's local inventory (from the frame's `SetInventoryFo4`).
2. The engine runs its pipeline. The client sends `ack`.
3. The server then sets `in`, disables the frame for everyone (`isDisabled`), and broadcasts `PowerArmorState in`.
4. Timeout 10 s without an ack → the server rolls back (§4.10).

**Remote puppets** (each neighbour):
1. On `PowerArmorState entering|in`, create a local, non-saved proxy frame ref at the puppet (`CreateReferenceAtLocation`, skip-save flag, never persisted).
2. Fill the proxy with the pieces as item instances (`addItemEx` with OMODs and health, PLAT-080).
3. Call `enterPowerArmor(puppet, proxy, animated = state==entering)`.

This performs the race switch to `PowerArmorRace` and a real PA skeleton, so remote hit capsules match (combat-character §12.3). F02 invalidates its graph-variable cache on the race switch. The world frame copy is hidden on `in`.

**Stream-in / late join:** the snapshot `props.powerArmor` with state `in` → apply instantly (`SwitchToPowerArmor` on a proxy, no animation; the F02 instant variant `InitializePowerArmorGraphBase`), *after* appearance (F03) and *before* equipment of non-PA items (F05).

**Exit:** the owner gets `PowerArmorTransition{phase: exiting, exitPos}` and runs `exitPowerArmor(actor)`. After the ack, the server re-enables the frame at `exitPos` and broadcasts `out`. Remotes run the exit on the puppet and delete the proxy.

**Correction:** a rejected enter → `ok:false` + `error`, and the frame stays enabled. A client that entered locally anyway (race condition) gets `exitPowerArmor` forced plus `Teleport2`.

**HUD:** the owner's `PowerArmorHUDMenu` runs locally. Battery and condition values come from `ChangeValuesAv`. Unpowered: the platform caps speed and disables sprint locally (F01 also enforces it on the server).

**Reconnect while worn:** not possible by default (disconnect forces exit, §4.10). With `powerArmor.keepOnDisconnect`, the `isMe` snapshot carries `in` and the client applies it instantly after world entry.

### 4.6 Validation & anti-cheat
**Enter**, in order. Each failure → `ok:false` + correction:
1. ownership of `actorIdx`: own actor, or a **companion** via `CompanionCommand{enterPA}` (F21). Plain hosted NPCs cannot be put into frames by their host (review finding M14); NPC power armor comes only from server-driven spawns (F13/F14);
2. frame exists, is enabled, `paWornBy` empty, not in another transition;
3. reach ≤ `activationReach` (F07, I15) using rewound position;
4. actor alive, not in PA, not in furniture, not ragdolled;
5. `powerArmor.allowInCombat` (default true);
6. ownership rule: unowned → claim; owned by another profile → allowed only if `powerArmor.allowStealing` (default false; fires `onSteal`, F06/F13 crime);
7. gamemode `onPowerArmorEnter` (blockable).

Then apply atomically (S13) on copies of the frame and actor inventories:
- move pieces and core to the actor;
- unequip conflicting apparel (armor slots 41–45 and body outfits that conflict, per F05 slot rules; under-armor stays);
- set `paWornBy`, `wornFrameRefId`, battery AV = core charge; F08 `SetBaseOverride(Strength, 11)` and PA carry rules (removed again on exit);
- commit, or roll back.

**Exit:**
- actor in PA, not mid-transition, not airborne (Z velocity from F01 history, `powerArmor.exitWhileFalling` false);
- `exitPos` = the actor position, snapped to the last accepted F01 sample;
- `onPowerArmorExit` (blockable, except forced exits);
- reverse transfer atomically; battery AV → core item `health`.

**Drain** (server, per accepted F01 sample while in PA, not for NPCs):
- `drain = Δt × rate(state) × mult`;
- rates per state (idle 0, walk, run, sprint, jetpack, plus per AP action from F08) in the GameProfile table, calibrated by G-self (combat-character §18 R8);
- `mult` from SRV-021 `ModAmmoHealthMult` (Nuclear Physicist) and the bobblehead;
- no drain during server-initiated teleports (fast travel, F26).

At 0, auto-swap the next core from the actor inventory, vanilla style. The depleted core is removed (vanilla behaviour [inference: verify depleted-core handling]). With no spare core → unpowered.

**Unpowered:** F01 speed table switches to `paUnpowered` (walk only); F08 rejects AP actions; F09 is unaffected.

**Jetpack:** allowed only if the worn torso has a jetpack OMOD keyword, AP > 0 and the actor is powered. Otherwise the jetpack flag is a movement violation (F01 Z sanity → `Teleport2`). AP drain per second of jetpack time is applied server-side (F08).

**Piece damage** (F11 callback): the hit limb maps to a piece slot. Piece health −= damage × `pieceDamageMult` (×3 for NPC wearers). At 0 the piece breaks: its resistances drop out of F11 sums and `PowerArmorState` is updated. A missing piece exposes the limb (F11 uses actor limb AVs).

**Core eject** (F11/F18 targeted core hit, or `kind: ejectCore` by the wearer):
- the server removes the core;
- if from a hit: spawns the core explosion (F10) and forces an exit;
- the wearer's own eject is a normal exit with the core dropped as a live grenade only if `powerArmor.coreEjectExplodes`.

**Frame inventory rules** (F06 hooks for a frame container):
- only items with the PA piece keyword; one piece per slot;
- broken pieces can be removed but not attached (combat-character §12.3 test);
- one core at most;
- no access while worn;
- taking from another profile's frame follows `allowStealing`.

### 4.7 Audience / visibility
- `PowerArmorTransition` results: requester + owner/host.
- `PowerArmorState`: grid neighbours of the wearer, or of the frame when not worn. Exact core charge goes to the owner only (S8).
- Frame `isDisabled`/position: neighbours.
- Battery and piece AVs: owner (and the host for NPCs).

### 4.8 NPC parity
- **NPCs in PA** (BoS, raider bosses): at first spawn (F13), if the NPC_ has a PA outfit (pieces in `DOFT`/`CNTO`) or a `PFRN` stand, the server creates a server-owned frame object linked to the NPC (`paWornBy`, not placed in the world) with the evaluated pieces. The snapshot carries `in`, and the host and all clients apply it on the NPC.
- NPC wearers do not drain cores.
- On NPC death (F12), the server performs a forced exit: the frame appears at the corpse with its remaining pieces (lootable/enterable per `allowStealing` = unowned).
- **Companions** (F21): `CompanionCommand{enterPA, frameRefId}` → the server validates owner access to the frame, then sends `PowerArmorTransition{phase: entering}` to the host. The host's ack and messages are checked with S6 host rules.

### 4.9 Gamemode API & server Papyrus
- **Properties:** `mp.get(frame,'paWornBy')`, `mp.get/set(frame,'paOwner')`, `mp.get(actor,'powerArmor')` (state payload), `mp.set(actor,'powerArmor', {frame}|null)` (server-forced enter/exit).
- **Settings:** `powerArmor.allowStealing`, `allowInCombat`, `exitWhileFalling`, `keepOnDisconnect` (false), `coreEjectExplodes`, `abandonDays` (unowned frames reset by F14 after N days), `drainRates` (GameProfile override).
- **Events:** `onPowerArmorEnter`, `onPowerArmorExit` (blockable), `onCoreDepleted` (observe), `onPowerArmorPieceBroken` (observe).
- **Papyrus:**
  - `Actor.SwitchToPowerArmor(frame)` → server enter without animation (S15 to the owner/host);
  - `IsInPowerArmor` [S] = `wornFrameRefId` set;
  - events `OnActivate` (frame), `OnItemEquipped` (pieces), `OnExitFurniture`, `OnRaceSwitchComplete`.

### 4.10 Edge cases & failure modes
- **Disconnect while worn** (default): the server performs a forced exit at the last accepted position, and the frame keeps its pieces and owner (FO4_Wrld behaviour).
- **Disconnect mid-transition:** roll back to the pre-enter state (frame enabled, pieces in the frame).
- **Server restart:** a persisted `paWornBy` with an offline wearer is resolved by a forced exit at load. A transition in progress is never persisted.
- **Death in PA** (player): forced exit at the corpse; the pieces stay on the frame; the corpse is out of PA (F12).
- **Two players enter the same frame at once:** the first message wins (server order); the second gets `error: occupied`.
- **Frame cell unloads on clients:** the engine's PA janitor must not delete server frames. The platform keeps frames persistent while loaded and recreates them from the server on stream-in; frame refs are never in the save (prior-art §3.1.8).
- **Fast travel in PA:** allowed (F26); no drain.
- **Cell reset:** owned frames are never reset; unowned ESM frames re-roll pieces after `abandonDays` with no listeners (F14).

### 4.11 Performance budget
- `PowerArmorState` ≤ 200 B (6 pieces × ~24 B + header), sent on change only.
- Drain integration ≤ 1 µs per movement sample.
- Remote proxy-frame creation is a one-off; enter/exit pipelines are queued so at most 1 per frame.

## 5. Engine / platform work required
- PLAT-084:
  - `enterPowerArmor(actor, frame, animated)` / `exitPowerArmor(actor)` working on **any** actor (owner, NPC, remote puppet), with completion events;
  - frame piece inventory read/write with OMODs and health;
  - core charge read/write (`PowerArmorBattery`, `ExtraHealth` on the core);
  - unpowered speed/AP clamp on the local player;
  - suppression of the PA janitor for server-tracked frames;
  - proxy-frame creation with skip-save (`CreateReferenceAtLocation`, prior-art C1, B13).
- RE (prior-art §5.4 Q4): the PA pipeline on a puppet actor (FO4_Wrld only did the ghost). Measure that the remote hit capsule matches the PA skeleton.
- F02-T10: PA behaviour-graph descriptor.

## 6. Tests
- `L-unit` (`[F17]`, `[PowerArmor]`):
  - message round trips (87/88);
  - enter accept: `paWornBy` set, pieces/core moved atomically, apparel conflicts unequipped, battery AV = core charge, neighbours get `PowerArmorState`;
  - rejects (occupied, out of reach, dead, owned + stealing off, gamemode veto), each with a correction;
  - second actor cannot enter;
  - no ack within 10 s → rollback;
  - exit restores the frame at the new position with pieces and charge written back;
  - drain only while moving, sprint/jetpack multipliers, Nuclear Physicist mult;
  - auto core swap; empty without spare → unpowered → F01 speed cap and AP actions rejected;
  - jetpack without OMOD → movement violation;
  - piece broken at health 0 drops resistances;
  - a broken piece cannot be attached to a frame;
  - disconnect forced exit; restart with a stale `paWornBy` resolved;
  - NPC in PA spawns with a linked frame; death drops the frame;
  - companion enter via host ack (wrong host rejected);
  - persistence round trip of all new fields;
  - late-joiner snapshot contains `props.powerArmor`.
- `L-int`: two bots; one enters, walks and sprints; the observer bot receives states; drain matches the model ± 1 %.
- `G-self`: enter/exit on the player, an NPC and a remote puppet; the race is `PowerArmorRace`; the proxy frame is not saved (save-file check with DATA-020).
- `G-manual`: two players: enter, walk, sprint, jetpack, exit; the other player sees the frame vanish/reappear; mod at a station; core runs dry; disconnect in PA.

## 7. Tasks
- [ ] **F17-T01** Messages `PowerArmorTransition` (87) and `PowerArmorState` (88), snapshot payload — S — Depends: NET-002 — Verify: L-unit — Files: falloutmp-server/cpp/messages/{PowerArmorTransitionMessage,PowerArmorStateMessage}.h, Messages.h; falloutmp-client/src/services/messages/
- [ ] **F17-T02** `PowerArmorService`: enter/exit/ack/rollback, validation order, atomic transfers, corrections, ownership/stealing — L — Depends: F17-T01, F04, F05, F07-T04 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/fo4/PowerArmorService.{h,cpp}, MpChangeForms.{h,cpp}; unit/PowerArmorTest.cpp
  - Accept: all §6 enter/exit cases pass.
- [ ] **F17-T03** Core drain model, auto swap, unpowered state (F01 speed table + F08 AP gate), GameProfile rates — M — Depends: F17-T02, F01-T05, F08, SRV-021 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/fo4/PowerArmorDrain.{h,cpp}, game_profile/fallout4/pa_drain.json
- [ ] **F17-T04** Piece damage, break and core-eject hooks with F11/F10 — M — Depends: F17-T02, F11 — Verify: L-unit
- [ ] **F17-T05** Frame persistence, frames as containers (F06 rules), abandon/reset with F14 — M — Depends: F17-T02, F06 — Verify: L-unit
- [ ] **F17-T06** Disconnect/death/restart handling — S — Depends: F17-T02, F12 — Verify: L-unit
- [ ] **F17-T07** PLAT-084 natives on any actor + proxy frame + janitor guard; prototype on a puppet (prior-art §5.4 Q4) — XL — Depends: PLAT-084, PLAT-070, PLAT-080 — Verify: G-self — Files: fallout4-platform/src/platform_fo4/PowerArmorApi.cpp
  - Accept: the self-test enters/exits PA on a remote puppet 20 times without a crash; the puppet race reads `PowerArmorRace`.
- [ ] **F17-T08** Client owner flow: activation intercept, requests, pipeline + ack, HUD AVs, unpowered clamp — M — Depends: F17-T07, F17-T01 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/powerArmorService.ts
- [ ] **F17-T09** Client remote rendering: proxy frames, instant apply on stream-in, F02 graph-cache invalidation, spawn order — M — Depends: F17-T07, F02-T08 — Verify: G-manual — Files: falloutmp-client/src/sync/powerArmor.ts, falloutmp-client/src/view/formView.ts
- [ ] **F17-T10** Jetpack: capability check, movement flag, AP + drain, F01 vertical allowance — S — Depends: F17-T03, F01-T07 — Verify: L-unit, G-manual
- [ ] **F17-T11** Station integration: frame as the `ModItem` target (F16), repair/paint/attach rules, station proximity — M — Depends: F17-T05, F16 — Verify: L-unit
- [ ] **F17-T12** NPC PA: linked frames at spawn, death drop, companion enter via host — M — Depends: F17-T02, F13-T03, F21 — Verify: L-unit, G-manual
- [ ] **F17-T13** Gamemode/Papyrus surface and docs (§4.9) — S — Depends: F17-T02 — Verify: L-int — Files: falloutmp-server/ts typings, script_classes/PapyrusActor.cpp
- [ ] **F17-T14** Drain-rate calibration (R8) and `G-manual` script — S — Depends: F17-T08 — Verify: G-self, G-manual — Files: docs/falloutmp/test-scripts/F17-power-armor.md

## 8. Open questions & risks
- Hold-breath (scoped aim) AP drain is an F08 AP rule (F08-T09), not a PA rule; PA only changes the drain multiplier.
- Jetpack state travels as F01 flag bit 12 `jetpackActive` (already reserved in F01 §4.3).
- Does `SwitchToPowerArmor` on a puppet need the frame to be a real persistent ref? If so, proxies must be persistent but skip-save (risk of the TEMPORARY-bit load hang; prior-art §3.1.8).
- Drain-rate constants and GMST names are unknown (R8); the GameProfile table needs measurement.
- Depleted-core and auto-swap behaviour must be verified in game.
- PvP balance: PA DR stacking in PvP is a gamemode concern (F11 multipliers).
