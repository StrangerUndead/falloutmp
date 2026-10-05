# F10 — Melee, Blocking, Explosives, Traps & Turrets

| Field | Value |
|---|---|
| Tier | T0 (melee, unarmed, bash, block, grenades) / T1 (mines, traps, missiles, Fat Man, turrets) |
| Target level | L4 (SkyMP L3 for melee/blocking; SkyMP-plus: reach check and server-derived power/sneak flags (I9), server-authoritative explosions) |
| SkyMP analogue | `OnHit` melee path (`ActionListener.cpp:1006-1408`): `CanHit` cooldown 975-990, splash 0.1 s/4 targets 1269-1299, `ShouldBeBlocked` 992-1003, reach check **commented out** 1315-1334; `AnimationSystem` block state; `OnTriggerEnter` primitives (`MpObjectReference.cpp:548-595`). SkyMP level L3 |
| Milestone | M8 (melee, block, grenades, missiles) / M10–M11 (mines, traps, turrets with F22) |
| Workstreams | SRV, NET, CLI, PLAT, GM |
| Depends on | F01-T05 (movement history), F02-T09 (action-id state: power attack, block), F05 (equipped grenade/mine slots), F06 (disarmed mine into inventory), F07 (activation distance), F08-T09 (AP spend), F09 (shot registry for missiles), F11 (damage, `DamageApplied`), F13 (hosting), F14 (trap reset), F22 (turret ownership), SRV-023 |
| References | reference/fo4-systems-combat-character.md §3 (melee), §17 (explosions/traps/turrets), §1.3 (EXPL/HAZD/PROJ), §2.1 (explosive weapons table); reference/fo4-animation-sync.md §3.4 (melee/bash/block/throw actions, stagger/knockdown); reference/skymp-sync-inventory.md §1.15 #18, §3.2 I9; reference/fo4-data-formats.md §4.18 (EXPL/HAZD/PROJ layouts); reference/prior-art.md §3.1.9 (threat-based owner election) |

## 1. Summary
Melee weapons, unarmed attacks, power attacks, gun bashes and blocking work between players and NPCs with the same rules on every client: the server checks reach against the rewound target position, derives the power/sneak/blocked flags itself, charges AP for power attacks and decides stagger and knockdown. Grenades, molotovs, mines, missiles and Fat Man shells are server entities with a lifecycle (spawn → arm → detonate); the server decides where and when they explode and computes the damage area itself. Traps and tripwires are server objects that trigger on server-side volumes. Turrets are hosted NPCs whose hostility follows their owner (settlements, F22).

## 2. Vanilla Fallout 4 behaviour
- **Melee:** damage × `(1 + 0.1·STR)`; Big Leagues/Iron Fist +20 %/rank, rank-4 sweep (`SetSweepAttack` 0x32); WEAP `MASE` speed, Reach, animation type HandToHand/OneHand*/TwoHand* (fo4-systems §3.1–3.2).
- **Power attack:** paper ×1.5; AP cost (`ModPowerAttackActionPoints` 0x1B), `ModPowerAttackDamage` 0x1C. **Bash:** `ModBashingDamage` 0x1A, `ModOutgoingLimbBashDamage` 0x82, `ModBashCriticalChance` 0x9A.
- **Block:** timed guard (~1–2 s), reduces melee damage by an unknown amount (`ModPercentageBlocked` 0x27, `HitData.percentBlocked`), staggers the attacker; **projectiles cannot be blocked** [web: Blocking, fo4-systems §3.1].
- **Stagger:** magnitudes None/Small/Medium/Large/ExtraLarge from weapon `Stagger`, explosion `Stagger`, armor `Stagger Rating`; entries `ModIncomingStagger` 0x21, `ModTargetStagger` 0x22. **Knockdown:** EXPL flags "Knock Down Always/By Formula"; `PushActorAway`, `KnockAreaEffect`.
- Actions/events: `ActionRightAttack` 0x13005, `ActionRightPowerAttack` 0x13383, `ActionMelee` 0x4A59 (bash), `ActionBlockHit` 0x13AF4, `ActionThrow` 0x4E32, `ActionStaggerStart` 0x138D2, `ActionKnockDown` 0xD1FDC, `ActionGetUp` 0xD1FDD; events `meleeattackStart`, `blockStart`, `grenadeThrowStart`, `mineThrowStart`, `throwEnd`, `weaponSwing`; vars `IsBlocking`, `staggerMagnitude`, `staggerDirection` (fo4-animation-sync §3.4).
- **Explosions** (EXPL `DATA`): damage, inner (full) / outer (falloff) radius, force, stagger, flags (Knock Down, Ignore LOS, Push Source Only, Chain), placed object (e.g. HAZD), spawn projectiles (MIRV/cluster) (fo4-data-formats §4.18). Explosion damage is separate from the 1-damage impact; Demolition Expert/bobblehead +15 %; padded/dense armor −25 %/−50 %.
- **Grenades** are Lobber projectiles (timer or impact); **mines** have `Can Be Disabled`, a proximity trigger and countdown; disarming returns the item; Sneak rank 3 ignores enemy mines; `CalculateMineExplodeChance` 0x3, `ModifyMaxPlaceableMines` 0x13, `MineTriggerRangeMod` AV 0xBA43F (fo4-systems §17.1). Slots `kWeaponGrenade` 42 / `kWeaponMine` 43.
- **Hazards** (HAZD): radius, lifetime, target interval 0.3 s, effect spell (molotov fire, mini-nuke radiation).
- **Traps:** tripwires, grenade bouquets, makeshift bombs, jury-rigged guns, flamethrower traps, baseball pitchers, swinging/shotgun traps, environmental (gas, cars); Papyrus `ProcessTrapHit`, `OnTrapHitStart/Stop`, `OnTriggerEnter/Leave`, `Weapon.Fire` for gun traps (fo4-systems §17.1).
- **Turrets:** actors with turret races, faction-owned (settlements), disabled by terminals (F24) [inference].
- Single-player assumptions: every client resolves explosion damage it simulates; mines trigger on local actors only; trap scripts run per client; paired kill moves and executions; engine-applied stagger on ghosts from local hits.

## 3. SkyMP baseline
- Melee hits use `OnHit` with ownership, same cell, distance ≤ 4096, attacker alive, weapon equipped or unarmed (`0x1f4`, Skyrim), `CanHit` = `t ≥ 1.1/speed − …` cooldown, splash 0.1 s / max 4 targets, block if `IsBlockActive` and facing < 1 rad. Power/sneak flags are **trusted** and reach is unchecked (I9) [src: ActionListener.cpp:885-1003, 1269-1334].
- Kill moves and `staggerStart` on remote actors are blocked client-side (`deathService.ts:24-58`).
- No explosion, trap or turret support beyond what Skyrim's engine does locally.
- Reuse: `CanHit`, splash window, `ShouldBeBlocked`, block bookkeeping (re-keyed by action id, F02-T09), primitives for trigger volumes, hosting. Replace: trusted flags, the disabled reach check, local explosions.

## 4. Design

### 4.1 Authority model
- Melee **event** class B (owner/host detects the hit); acceptance, flags, AP, damage, stagger class A.
- Explosive entities, mines, traps: class A (server owns lifecycle and state). The originator only **claims** detonation for thrown/fired explosives it simulates; the server validates the claim.
- Turrets: class C (host-simulated AI, F13) with class-A state (health, ownership, enabled).

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Block active, last attack action `{actionId, ts}` | struct | `MpActor::animState` (F02) | no | — |
| Last melee hit times per target (cooldown, splash) | map | `MpActor` (SkyMP `lastHitTime` reuse) | no | — |
| Live explosive `{explosionId, ownerIdx, sourceForm, explForm, phase, spawnTs, origin, vel, fuseMs, shotId}` | map | `ExplosiveRegistry` (new, `fo4/`) | no (thrown/fired live ≤ 30 s) | — |
| Placed mine ref (owner, armed, expl form) | FF `MpObjectReference` | ChangeForm + new `explosive {ownerProfileId, armed}` | yes | — |
| Trap state (armed/fired/disarmed, reset time) | per trap ref | ChangeForm new `trapState` | yes | ESM (armed) |
| Hazard ref (HAZD, remaining lifetime) | FF ref | ChangeForm `hazard {form, expiresAt}` | yes (until expiry) | — |
| Turret owner faction / enabled | NPC fields | F13/F22 (factions), `isDisabled` | yes | ESM / F22 |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `HitReport` (82) | C→S | `kind` = melee / bash / thrownImpact, `aggressorIdx`, `targetIdx`, `clientTs`, `rewindTs`, `hitPosLocal`, `limb`, `claimFlags` (advisory only) — layout in F09 | R ordered | per swing hit | registry 82 (F09) |
| `ExplosionEvent` (84) | C→S | `explosionId` u32 (shooter-scoped: `idx` << 16 \| counter), `phase` (spawn, detonate), `ownerIdx`, `sourceKind` (thrown, placed, projectile), `sourceForm` u32 (grenade/mine WEAP or PROJ), `shotId` u16 (projectile), `pos` f32[3], `vel` f32[3] (spawn), `ts` u32 | R ordered | on throw/place/impact | registry 84 |
| `ExplosionEvent` (84) | S→C | `explosionId`, `phase` (spawn, arm, detonate, disarm, dud), `explForm` u32, `pos`, `ownerIdx`, `serverTs`, `seed` u32 | R | to neighbours of `pos` | registry 84 |
| `DamageApplied` (83) | S→C | per target, `hitKind` melee/bash/explosion/trap; `stagger`, `knockdown` fields (F11) | R | per resolved hit | registry 83 (F11) |
| `Activate` (6) | C→S | disarm a mine/trap ref | R | event | reused (F07) |
| `CreateActorFo4` (64) / `UpdateProperty` (7) | S→C | placed mine/trap/hazard refs and `trapState`, `explosiveArmed` props | R | subscribe / change | reused |

### 4.4 Client capture (owner side)
- **Melee:** `TESHitEvent` with `kMeleeAttack`/`kBash` and the aggressor = player or hosted NPC → `HitReport kind=melee|bash`. The swing itself is already sent as F02 actions (`ActionRightAttack`, `ActionRightPowerAttack`, `ActionMelee`), which the server uses to derive flags.
- **Block:** F02 actions/vars (`blockStart`, `IsBlocking`, `iWantBlock`).
- **Throw/place:** `ActionThrow` + projectile launch hook on grenade/mine projectiles (PLAT-083) → `ExplosionEvent spawn` with the launch velocity; the projectile impact/fuse on the originator → `ExplosionEvent detonate`. Fired explosive projectiles (missile, Fat Man) reference the F09 `shotId`.
- The client never applies explosion damage: local explosions on the originator are spawned with zero damage (`placeCosmeticExplosion`) or their hits are dropped by the HP funnel clamp (F12).

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Stagger/knockdown:** from `DamageApplied.stagger/knockdown`, the target's owner/host plays `ActionStaggerStart` with `staggerMagnitude`/`staggerDirection`, or `PushActorAway` for knockdown, on its own actor; neighbours see it through the owner's F02 relay. Engine stagger on ghosts from local hits stays blocked.
- **Explosions:** neighbours receive `ExplosionEvent detonate` → `placeCosmeticExplosion(explForm, pos, seed)` (FX, sound, physics impulse on clutter; no damage). `spawn` shows a cosmetic grenade/missile for non-originators (PLAT-083 cosmetic projectile); `arm` lights the mine; `disarm`/`dud` remove it.
- **Mines/traps/hazards** are refs streamed by the grid: late joiners see placed mines (armed state) and hazards in `CreateActorFo4`/object snapshots; traps show their fired/disarmed state.
- Kill moves, executions and paired grabs are disabled on all actors.

### 4.6 Validation & anti-cheat
Melee (`HitReport kind=melee|bash`):
1. SkyMP checks kept: ownership/host (S6), same cell/world, attacker alive, weapon equipped (melee weapon, unarmed form from the GameProfile, or a gun for bash).
2. Cooldown `CanHit` from `MASE`/attack seconds × `WeaponSpeedMult`; splash window 0.1 s, max 4 targets (max 8 with the Big Leagues sweep entry).
3. **Reach check (I9):** `dist(aggressorPos(t), targetBounds(t)) ≤ reach × fCombatDistance (GMST by EDID) × combat.meleeReachTolerance (1.25) + speedSlack`, with both positions from `MovementHistory.Sample(rewindTs)`. Reject → `DamageApplied result=rejected`.
4. **Server-derived flags (I9):** power = an `ActionRightPowerAttack` from this actor within `combat.powerAttackWindowMs` (1500) and `Spend(AP, cost)` succeeds (else the hit counts as a normal attack); bash = `ActionMelee` or `kind=bash` with a gun equipped; sneak = attacker sneaking (F01) and target unaware (F29); blocked = target block active (F02) and facing the attacker within 1 rad, and the source is not a projectile.
5. Block deflect: reduced damage (`ModPercentageBlocked`, GMST base) and a stagger on the attacker (`DamageApplied` with 0 damage to the attacker).
Explosives (`ExplosionEvent`):
6. `spawn`: grenade/mine item owned and equipped in its slot → removed atomically (S13); throw rate ≤ one per throw animation (`ActionThrow` seen); mine count ≤ `ModifyMaxPlaceableMines`. Fired explosives: the `shotId` must be in the F09 registry with an Explosion-flagged PROJ.
7. `detonate` claim: entity exists and is owned by the sender; time since spawn ≤ fuse (`altTriggerTimer`) + tol, or impact position consistent with the trajectory (`origin + vel·t + ½g·t²`, PROJ gravity) within `explosions.positionToleranceUnits` (256); else the server detonates at its own predicted position (fuse) or marks a dud.
8. The server **computes the damage area**: every actor whose rewound position at the detonation time is inside `outerRadius` gets an F11 `hitKind=explosion` hit with falloff (full inside `innerRadius`, linear to 0 at `outerRadius` [inference, VERIFY]); LOS ignored (no server geometry; `explosions.requireClientLos` optional flag-only). MIRV/cluster spawns follow EXPL spawn data with server `seed`. Placed objects (HAZD) become server hazard refs ticking effects every target interval.
9. Mines: proximity primitive per mine; triggers on accepted movement samples of actors not ignored by rules (owner, Sneak rank 3 vs enemy mines); countdown then server detonation. Disarm: `Activate` on the mine, F07 distance, success chance from perks → item to inventory (F06), entity removed.
10. Traps: trigger primitives (tripwire line, pressure plates) from the trap ref; on trigger the server fires the trap effect (explosion, server-originated shot, hazard) and sets `trapState=fired`; disarm via `Activate`; reset by F14 cell reset.
Turrets:
11. Turret shots/hits come from the host as F09 `WeaponFire`/`HitReport` with `idx` = turret, validated like any hosted NPC; disabled turrets (terminal, F24) cannot fire.

### 4.7 Audience / visibility
`ExplosionEvent` S→C: grid neighbours of the explosion position (3×3). Melee and explosion damage: F11 audiences. Mine/trap/hazard refs: grid listeners as normal objects.

### 4.8 NPC parity
Hosted NPCs melee, block, throw grenades and trigger mines through their host, with the same checks; NPC grenade inventory follows `NPCs Use Ammo`-like rules (NPCs consume their grenades). Turrets are hosted NPCs; host election for turrets prefers the client the turret is fighting (threat-based election with hysteresis, prior-art §3.1.9, F13).

### 4.9 Gamemode API & server Papyrus
- Events: `onMeleeHit` → `onHit` (F11, flags power/bash/blocked), `onStagger(targetId, magnitude)` (blockable), `onExplosion(ownerId, explFormId, pos)` (blockable → dud, item already spent), `onMineArmed`, `onMineDisarmed(actorId, mineId)` (blockable), `onTrapTriggered(trapId, actorId)` (blockable).
- Settings: `combat.meleeReachTolerance` (1.25), `combat.powerAttackWindowMs` (1500), `explosions.positionToleranceUnits` (256), `explosions.requireClientLos` (false), `explosions.friendlyFire` (true), `traps.enabled` (true).
- Papyrus (server VM): `OnTrapHitStart/Stop`, `OnTriggerEnter/Leave` on trap refs, `OnActivate` on mines/traps, `ObjectReference.PlaceAtMe(Explosion)` → server explosion, `PushActorAway`/`KnockAreaEffect` → owner via SpSnippet (S15).

### 4.10 Edge cases & failure modes
- Originator disconnects with a grenade in flight: the server detonates at the predicted fuse position.
- Mine owner disconnects: the mine stays armed (persisted); owner-friendly rule uses `ownerProfileId`.
- Explosion at a cell border: damage targets from the server grid query, not the originator's cell.
- Grenade thrown in VATS: VATS-lite disallows throws (F18).
- Blocked hit while the blocker is staggered: block inactive.
- Turret in an unclaimed settlement: ESM faction hostility; F22 changes ownership.

### 4.11 Performance budget
Melee hit ≤ 15 µs. Explosion resolution O(actors in 3×3 grid) ≤ 50 µs. `ExplosionEvent` ≤ 48 B. Mine proximity: primitives are checked only on accepted movement samples (SkyMP `SetPos` path).

## 5. Engine / platform work required
- `placeCosmeticExplosion(explForm, pos, seed)` (explosion with damage/force on actors suppressed) and cosmetic grenade/mine projectiles (extend PLAT-083).
- Capture grenade/mine launch velocity and impact/fuse callbacks (PLAT-083 launch hook + projectile impact).
- `applyStagger(actor, magnitude, dirYaw)` (graph vars + `ActionStaggerStart`), `pushActorAway` on the local actor.
- Disable kill moves/paired idles on all actors (extend F02 blocked-idle tables).

## 6. Tests
- `L-unit` (`unit/Fo4MeleeTest.cpp` `[F10][Melee]`, `unit/Fo4ExplosionTest.cpp` `[F10][Explosion]`, `[Trap]`):
  - power attack ×1.5 and AP deducted; power claim without `ActionRightPowerAttack` → normal hit; power attack at 0 AP → normal hit;
  - reach exceeded (rewind-aware: target moved away 200 ms ago) → rejected with `DamageApplied result=rejected`; within reach accepted;
  - frontal blocked hit reduced and attacker staggered; rear hit not blocked; projectile never blocked;
  - claimed sneak without detection state ignored; bash limb damage; `DamageApplied.stagger` magnitude taken from the weapon `Stagger` vs armor `Stagger Rating`;
  - grenade spawn consumes the item; spawn without the item rejected with `SetInventoryFo4` correction; detonate far off the trajectory → server position used;
  - frag grenade damage at 0, inner, mid, outer radius; target that moved away 200 ms before detonation uses the rewound position;
  - mine triggers on primitive entry, not by its owner; disarm adds the item; disarm from 400 u rejected;
  - trap fires once, persists `fired`, resets on cell reset; hazard ticks every 0.3 s until expiry;
  - missile detonation requires a registered explosive shot;
  - hosted NPC melee requires the host; turret fire while disabled rejected;
  - gamemode vetoes (`onExplosion` → dud, `onStagger`); Papyrus `OnTrapHitStart` observed;
  - persistence of mines/trap state/hazards; late joiner snapshot contains armed mines.
- `L-int`: bot throws a grenade at two bots; both get the server damage; a third bot outside the outer radius gets none.
- `G-self`: cosmetic explosion spawns without damaging a test NPC; stagger API.
- `G-manual`: melee duel with block and power attacks; grenade, molotov, mine, missile launcher, Fat Man between two players; tripwire in a dungeon; settlement turret vs raider.

## 7. Tasks
- [ ] **F10-T01** Melee `HitReport` path: SkyMP checks with FO4 unarmed id, `CanHit` from `MASE`, splash/sweep — M — Depends: F09-T01, REF-004 — Verify: L-unit — Files: ActionListener.cpp (FO4 hit handler), fo4/Fo4MeleeRules.{h,cpp}; unit/Fo4MeleeTest.cpp
- [ ] **F10-T02** Block: block state from F02 actions, facing check, deflect stagger, no projectile blocking — S — Depends: F10-T01, F02-T09 — Verify: L-unit
- [ ] **F10-T03** Power attack AP spend and server-derived power/bash flags — S — Depends: F10-T01, F08-T09 — Verify: L-unit
- [ ] **F10-T04** Reach check with rewound positions (I9) and derived sneak flag — M — Depends: F10-T01, F01-T05, F29 (detection) — Verify: L-unit
  - Accept: §6 reach cases pass; reach/power/sneak flags are never taken from the client.
- [ ] **F10-T05** Stagger/knockdown decision (weapon/explosion stagger vs armor rating, perk entries) into `DamageApplied`; client `applyStagger`/`pushActorAway` — M — Depends: F11-T07 — Verify: L-unit, G-self
- [ ] **F10-T06** `ExplosionEvent` message + `ExplosiveRegistry` (spawn/detonate validation, trajectory model, fuse, dud) — M — Depends: NET-002, F09-T04 — Verify: L-unit — Files: skymp5-server/cpp/messages/ExplosionEventMessage.h, fo4/ExplosiveRegistry.{h,cpp}
- [ ] **F10-T07** Server area damage (rewound positions, falloff, MIRV/cluster, HAZD placed objects as server hazard refs) — M — Depends: F10-T06, F11-T01, SRV-020 — Verify: L-unit
- [ ] **F10-T08** Mines: placed mine refs, arming, server proximity primitives, owner/Sneak rules, disarm via `Activate` — M — Depends: F10-T06, F07-T04, F06 — Verify: L-unit
- [ ] **F10-T09** Traps module: data table of vanilla trap types (by keyword/script name), trigger primitives, firing effects, `trapState` persistence, F14 reset — L — Depends: F10-T07, F14 (cell reset), SRV-080 — Verify: L-unit, D-real
- [ ] **F10-T10** Turrets: hosted turret NPCs, disabled state (F24 terminals), owner-faction hostility hook for F22, threat-based host preference — M — Depends: F13 (hosting), F09-T10 — Verify: L-unit, G-manual
- [ ] **F10-T11** Platform: cosmetic explosion, grenade/mine launch + impact capture, kill-move blocking — M — Depends: PLAT-083, F02-T03 — Verify: W-ci, G-self
- [ ] **F10-T12** Client services: `meleeService.ts` (block/bash capture glue), `explosionService.ts` (spawn/detonate capture, remote cosmetic replay) — M — Depends: F10-T06, F10-T11, CLI-050 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/{meleeService,explosionService}.ts
- [ ] **F10-T13** Gamemode events/settings, server Papyrus trap/trigger events, `PlaceAtMe(Explosion)`; docs — M — Depends: F10-T07, PVM-014 — Verify: L-unit, L-int
- [ ] **F10-T14** `G-manual` melee/explosives/traps/turrets script — S — Depends: F10-T12 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F10-melee-explosives.md

## 8. Open questions & risks
- Explosion falloff curve between inner and outer radius is unverified (G-self measurement against engine damage).
- Vanilla trap scripts are stripped server-side (ADR-011); the traps table must cover the vanilla trap kinds without running their scripts. Unknown trap kinds stay inert (logged).
- No server LOS: explosions through walls hit. Mitigation: most EXPL already ignore LOS in vanilla; optional client LOS flag later.
- Shooting grenades in flight and mines revealed by VATS are T2.
