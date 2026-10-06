# F02 — Animation Sync (State, Actions, Events, Graph Variables)

| Field | Value |
|---|---|
| Tier | T0 |
| Target level | L3 |
| SkyMP analogue | `UpdateAnimation` (3) event replay + `UpdateAnimVariables` (24), `animation.ts`, Frida `SendAnimation` hook, `AnimationSystem.cpp` (stamina by event name). SkyMP level L2–L3 |
| Milestone | M5 (locomotion, draw/holster, jump, sneak), M8 (combat actions) |
| Workstreams | PLAT, CLI, SRV, NET |
| Depends on | F01, PLAT-030, PLAT-075 (animation hooks), RE of the hkb layouts (PLAT-076) |
| References | **reference/fo4-animation-sync.md** (entire; §6 decision, §7 tasks, §4.5 variable subset, §5 actions); ADR-008 |

## 1. Summary
Remote players and NPCs animate like the real actor:
- locomotion blends, draw/holster, aim (sighted), fire/reload poses, grenade throws, melee and bash, block, jump, sneak, sprint;
- furniture enter/exit, power armor enter/exit, jetpack, stagger/knockdown, death, swimming.

The local player's view (1st or 3rd person) must not matter. The approach is the hybrid strategy D (ADR-008): engine **state** keyframes + **BGSAction** replay + **whitelisted events** + **name-resolved graph variables**.

## 2. Vanilla Fallout 4 behaviour
Details are in the reference doc. Key points:
- The player has two graphs (3rd/1st person). The inactive one is parked: no events, no pose. Holder-level variable setters reach both (§3.2).
- `ActorMediator::PerformAction` resolves an action through the actor's own idle tree, so actions are perspective-independent (§2.1, §6).
- Weapon subgraphs depend on actor keywords and **weapon keywords, including those added by OMODs**. The remote actor must carry the same item instance before replay, and replay must wait until the subgraph is loaded (§3.4).
- Power armor switches the race to `PowerArmorRace` 0x1D31E, which rebuilds the graph and invalidates cached variable indices.
- There are 173 `AACT` action forms in Fallout4.esm (§5.1). The animation event names are listed in §5.2.

## 3. SkyMP baseline
- Only the latest event per frame is sent, unreliably and without timestamps. Variables are synced only for magic.
- The server's `AnimationSystem` charges stamina by event name.
- Reuse: the capture-hook concept (`hooks.sendAnimationEvent`), the replay filters and allow/deny tables structure, and the server's per-event callbacks (re-keyed by action form id).

## 4. Design

### 4.1 Authority model
- **Class B:** owner/host-authoritative presentation.
- **Class A for gameplay consequences:**
  - firing, ammo, AP drain, hits, damage, death, PA entry and furniture occupancy are decided by the server through explicit messages (F09, F12, F17, F07);
  - replayed animations never cause gameplay on remote clients (remote `Fire`/`Launch` blocked).

### 4.2 Server state & persistence
| State | Type | Where | Persisted | Default |
|---|---|---|---|---|
| Anim-state keyframe (ActorState words, sit/furniture, PA, sighted, weapon state) | struct | `MpActor::animState` (new) | no (re-derived; furniture/PA state comes from their own persisted features) | idle |
| Action replay cache (STR-dev refined chain) | bounded list | `MpActor::actionReplayCache` (new) | no | empty |
| Last animation (SkyMP `lastAnimation`, objects) | string | ChangeForm `lastAnimation` | **yes** (fix I11) | — |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate |
|---|---|---|---|---|
| `UpdateActions` (74) | C→S, relay | `idx`, `stateKeyframe?` (ActorState words, sitState, furnitureRef, inPA, sighted, gunState, weaponState, equippedInstanceHash, archetype/flavor kw ids), `actions[]` `{seq, ts, actionFormId, targetIdx, priority, resolvedEvent?, idleFormId?, perspective}`, `events[]` `{seq, ts, nameId (NET-010 string table), graphIndex}` | R ordered for actions/events; keyframe piggybacks | on change; batched per frame; keyframe every 1 s |
| `UpdateGraphVariables` (75) | C→S, relay | `idx`, `seq`, `ts`, `raceGraphKey`, `values[]` `{nameId, type, value}` (delta vs last acked; full every 1 s) | U sequenced | 15–20 Hz while changing |

`nameId` indexes a per-session string table. The server sends the table in `UpdateGamemodeData`, or a dedicated table message if needed. The client resolves names to graph indices at runtime per graph (re-resolved on graph rebuild).

### 4.4 Client capture
- **Hooks** (platform, PLAT-075):
  - `PerformAction` enter/leave (action, target, priority, resolved event, idle, result);
  - the holder `NotifyAnimationGraph` (event, result, graph index, isPlayerFirstPerson);
  - `SetActiveGraph`;
  - a reentrancy guard, so platform-initiated replays are not captured.
- **Filters** (per-race tables): drop `ActionMoveStart/Stop/Turn*` (locomotion is variable-driven); never replay `Fire*` as gameplay; perspective map for 1st-person-only events; instant-variant overrides for late join.
- **Variables:** read by name from the **active** graph (T0/T1 subsets from the reference §4.5). Send only names that exist in the remote 3rd-person graph config.

### 4.5 Apply
**Spawn order on stream-in:**
1. appearance (F03);
2. equipment with OMOD instance (F05);
3. archetype/flavor;
4. wait for the subgraph to load (`IsSubgraphLoaded`);
5. state keyframe using instant variants: `SnapIntoInteraction`, `SwitchToPowerArmor`, `ActionInstantInitializeGraphToBaseState`, `g_archetypeBaseStateStartInstant`;
6. movement;
7. short timed action replay from the cache.

**Runtime:**
- Actions replay via `performAction(ref, actionFormId, target, priority)`. Fall back to `notifyAnimationGraph(ref, resolvedEvent)`.
- Whitelisted events go through `notifyAnimationGraph`.
- Variables are written after the bound-channel flush (post-channel functor), or the process direction/velocity is fed so channels don't overwrite them.

**Death/ragdoll/stagger:** follow server state (F12) and replay `ActionRagdollInstant`/`ActionKnockDown`/`ActionGetUp`.

**Optional fidelity layer** (setting): drive the parked 3rd-person graph on the sender (FO4_Wrld technique) for higher-fidelity variables.

### 4.6 Validation & anti-cheat
- Ownership/host checks.
- `seq` monotonic. Rate limits per stream (actions ≤ 30/s, events ≤ 60/s, variables ≤ 25 Hz).
- Server `AnimationSystem` re-keyed by **action form id** (event-name fallback):
  - AP drain for sprint;
  - AP cost for power attacks;
  - jump/sprint not allowed when AP is 0 (GameProfile rules);
  - an action implying a state the server denies (e.g. `ActionFire*` with no weapon equipped) is dropped, and a state keyframe correction is sent.
- Fire, reload and throw gameplay is validated in F09/F10, not here.

### 4.7 Audience
Grid neighbours, excluding the sender.

### 4.8 NPC parity
Hosts capture and send for hosted NPCs, using the same path. NPC graphs are 3rd-person only, so there is no perspective issue. Creature graphs use per-race name configs (`graphDescriptors/<race>.json`).

### 4.9 Gamemode API & Papyrus
- `mp.get(id,'animState')` (read) and `lastAnimEvent` (existing).
- Server Papyrus `PlayIdle`/`PlayIdleAction`/`PlayAnimation` on actors/objects → SpSnippet or `UpdateActions` broadcast (S15).
- Event `onAnimationAction(actorId, actionFormId)` (observe only; not blockable at L3).

### 4.10 Edge cases
- Graph rebuild (PA enter/exit, race change, `RevertAnimationGraphManager`): invalidate the name→index cache and resend a full keyframe.
- Kill cams and paired idles: disabled for MP (blank `KillMove*`/`PairedKill*` on remote actors, as SkyMP does).
- VATS: disabled at T0 (F18).
- Loading screens: discard queued actions older than 1 s.

### 4.11 Performance budget
~1–2 KB/s per remote actor (reference §6). Client per-frame cost ≤ 0.1 ms per remote actor for variable writes.

## 5. Engine / platform work required
From the reference §7.1, items 1–7:
- Offsets/IDs for the holder notify, `ProcessGraphEvent`, `SetActiveGraph`, `ActorMediator::PerformAction`, `Actor::PerformAction` (AE 2231177), weapon `Fire`, `Projectile::Launch`, draw/sheathe.
- Frida hooks.
- `AnimApi.cpp`: `getGraphVariables`, `setGraphVariables(afterChannelFlush)`, `dumpGraphVariables`, `notifyAnimationGraph`, `performAction`, `getActorAnimState`, `getCameraState`, `on('animationGraphEvent')`.
- A `GraphVariableTable` cache.
- Local definitions of the missing hkb/ActorMediator/BGSActionData types (RE).

## 6. Tests
- `L-unit`: message round trips; `AnimationSystem` AP rules by action id; replay-cache refinement; rate limits; ownership.
- `L-ts`: capture filters, perspective map, sequence ordering, the variable delta encoder.
- `G-self`:
  - dump the variable tables (both player graphs, a human NPC, PA, available creatures) to JSON;
  - `PerformAction` acceptance on a test NPC for the action list.
- `G-manual`: the scenario matrix of reference §7.2 (each in 1st, 3rd and switching view), two clients side by side, video.

## 7. Tasks
- [ ] **F02-T01** **Prototype probe** (standalone F4SE plugin, CommonLibF4): log holder/manager notify, PerformAction, graph output events, SetActiveGraph/camera, subgraph changes; 20 Hz variable sampler for both graphs; the scenario matrix; replay strategies (a)–(e) on an AI-disabled NPC — L — Depends: BUILD-002 only (standalone plugin; it resolves the ~10 IDs listed in reference/fo4-animation-sync.md §7 itself and must start in week 1 of M3) — Verify: G-manual (user runs it, Claude analyses the CSVs) — Files: tools/fo4-anim-probe/
  - Accept: the questions 1–5 of reference §7.2 are answered in a dated update to reference/fo4-animation-sync.md, and ADR-008 is marked Accepted or revised.
- [ ] **F02-T02** RE + local types: BShkbAnimationGraph, hkbBehaviorGraph, hkbVariableValueSet, ActorMediator, BGSActionData; map OG/1.11.191 IDs to the target AE build — L — Depends: PLAT-002 — Verify: W-ci, G-self — Files: fallout4-platform/src/.../game/
- [ ] **F02-T03** Animation hooks (holder notify, PerformAction, SetActiveGraph, remote Fire/Launch block) with a reentrancy guard — L — Depends: F02-T02 — Verify: G-self
- [ ] **F02-T04** `AnimApi` natives + typings in Definitions.txt — M — Depends: F02-T03 — Verify: W-ci, G-self
- [ ] **F02-T05** `GraphVariableTable` (name→index→type per graph, CRC64 key, invalidation on rebuild) + post-channel write path — M — Depends: F02-T04 — Verify: G-self
- [ ] **F02-T06** Messages `UpdateActions`, `UpdateGraphVariables` + name table distribution — M — Depends: NET-002 — Verify: L-unit
- [ ] **F02-T07** Client `sync/animState.ts`, `sync/actions.ts`, `sync/graphVars.ts`, `graphDescriptors/human3p.json` (from reference §4.1/§4.5) — L — Depends: F02-T04, F02-T06 — Verify: L-ts, G-manual
- [ ] **F02-T08** Client spawn ordering in `formView.ts` (appearance → equipment → archetype → subgraph wait → keyframe → movement → replay) — M — Depends: F02-T07, F03, F05 — Verify: G-manual
- [ ] **F02-T09** Server: `AnimationSystem` re-keyed by action id (state/action validation only; AP cost rules move to F08-T09 in M7), `animState`, `actionReplayCache`, CreateActorFo4 fields — M — Depends: F02-T06, F00-T11 — Verify: L-unit
- [ ] **F02-T10** Creature/PA descriptor configs from probe dumps (deathclaw, mole rat, dog, radroach, ghoul, super mutant, robots, PA) — M — Depends: F02-T01 — Verify: G-manual
- [ ] **F02-T11** Debug overlay: graph-variable inspector + port of `animDebugService.ts` — S — Depends: F02-T07 — Verify: G-manual
- [ ] **F02-T12** Optional parked-graph driver (fidelity layer, behind a setting) — L — Depends: F02-T01 results — Verify: G-manual

## 8. Open questions & risks
- `UpdateActions` is reliable-ordered; when a client's send queue backs up, older keyframes are dropped and the latest keyframe is kept (drop + keyframe), so the remote never replays stale state.
- R-ANIM-1: first-person capture fidelity. Mitigated by the prototype, with the parked-graph fallback.
- No public descriptors for power armor, robots or mirelurks; they must be dumped.
- AE IDs for the hkb internals must be re-derived (OG/1.11.191 sources only).
