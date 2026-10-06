# F09 — Ranged Combat (Guns, Ammo, Magazines, Reload, Hit Validation)

| Field | Value |
|---|---|
| Tier | T0 |
| Target level | L4 (SkyMP-plus: SkyMP trusts hits with light checks; FalloutMP validates ammo, cadence and geometry with lag compensation, ADR-014) |
| SkyMP analogue | `PlayerBowShot` (22, unreliable, ammo −1) + `OnHit` (17) bow path, `playerBowShotService.ts`, `hitService.ts`, `ActionListener::OnPlayerBowShot` 573-602 / `OnHit` 1006-1112. SkyMP level L3 |
| Milestone | M8 |
| Workstreams | SRV, NET, CLI, PLAT, GM, QA |
| Depends on | F01-T05 (movement history), F02-T03 (remote `Fire`/`Launch` block), F04 (ammo items, `ammoLoaded` extra), F05 (equipped weapon `ItemKey`), F08 (AP, AV mults), F11 (damage), F13 (hosting), SRV-022 (OMOD stat resolver), SRV-023 (shot registry), PLAT-083 (projectile natives) |
| References | reference/fo4-systems-combat-character.md §1.3 (WEAP/AMMO/PROJ/AMDL), §1.4 (hooks), §1.7 (`ShotRegistry`, `MovementHistory`, `CombatRng`), §2 (guns, entire), §18 R1–R3/R9; reference/fo4-animation-sync.md §3.4 (firing/reload actions and events), §2.6 (STR remote Fire/Launch block); reference/prior-art.md §3.2.5 (TE projectile relay, `bUseOrigin`), §5.3 Adopt 5, §5.4 Q2/Q11; reference/fo4-data-formats.md §4.6 (WEAP DNAM/FNAM), §4.18 (PROJ); reference/skymp-sync-inventory.md §1.15 #12/#18 |

## 1. Summary
Players and hosted NPCs fire guns: semi-auto, automatic, shotguns, charging (Gauss), crank-loaded (laser musket), bolt-action, beam and flame weapons. The owner client detects shots and hits with the local engine. The server owns ammo, the magazine, weapon instance stats, fire cadence, hit acceptance and damage. Every shot is announced; every hit claims a shot; the server rewinds the target (≤ 250 ms) using movement history and accepts only geometrically plausible hits. Remote clients see a cosmetic replay (animation, muzzle flash, tracer projectile) that can never cause gameplay. The server has no collision geometry, so line of sight is covered by statistics, not geometry.

## 2. Vanilla Fallout 4 behaviour
- Effective stats = WEAP base + OMODs + legendary OMOD (SRV-022): `iAmmoCapacity` 12, `bAutomatic` 25, `uNumProjectiles` 51, `poAmmo` 61, `fReloadSpeed` 76, `fAttackDelaySec` 4, `fSpeed` 0, `fMin/MaxRange` 2/3, `fFullPowerSeconds` 84 (fo4-systems §1.3; f4se ObjectMod.psc).
- WEAP DNAM: ammo, speed, reload speed, min/max range, attack delay, out-of-range mult (0.5), flags (`Automatic` 0x8000, `Charging Reload` 0x8, `Charging Attack` 0x200, `Hold Input To Power` 0x800, `Bolt Action` 0x400000, `NPCs Use Ammo` 0x2), capacity, animation type (9 Gun); FNAM: fire seconds, reload seconds, `# Projectiles`, override projectile (fo4-data-formats §4.6).
- One AMMO item per shot regardless of projectile count; musket uses one cell per crank; fusion-core weapons draw on the core's charge; *Never Ending* sets capacity to carried ammo. Inventory decrement per shot vs at reload: per shot [inference, **VERIFY R3**]. NPCs don't consume ammo unless `NPCs Use Ammo` (fo4-systems §2.1).
- Real cadence is animation-driven (fire seconds, speed, attack delay, `WeaponSpeedMult` 0x312, *Rapid*) — **VERIFY R1**. Spin-up is part of the attack delay [inference].
- PROJ: `Hitscan` flag; types Missile/Lobber/Beam/Flame/Cone; speed, gravity, range, collision radius (fo4-data-formats §4.18). Vanilla bullets are fast **non-hitscan** missiles — classify every PROJ with D-real (**R2**).
- Range falloff: 100 % to min range, linear to out-of-range mult at max range. Shotgun DR coefficient uses the **total** damage (F11).
- Engine: `Actor::UseAmmo` vfunc 0xF0, `ReloadWeapon` 0xEF, `EquippedWeaponData::ammoCount`, `PlayerAmmoCountEvent{clipAmmo, reserveAmmo}`, `PlayerWeaponReloadEvent`, `GUN_STATE`, `ProjectileLaunchData{origin, angles, power, alwaysHit, intentionalMiss, targetLimb…}` (fo4-systems §1.4). `OnPlayerFireWeapon` is timer-based, not per shot [src: f4se Actor.psc:991-993].
- Animation: `ActionFireSingle` 0x4A5A, `ActionFireAuto` 0x4A5C, `ActionFireCharge` 0x4A5B, `ActionGunChargeStart` 0xC4F72, `ActionBoltCharge` 0xB259D, `ActionReload` 0x4A56; events `weaponFire`, `reloadStart`, `ReloadComplete` (fo4-animation-sync §3.4).
- Single-player assumptions: aim assist (unfair in PvP), hits against stale ghosts, infinite NPC ammo, client-side damage, local RNG for spread.

## 3. SkyMP baseline
- `PlayerBowShot` is unreliable and only removes one arrow (`ActionListener.cpp:573-602`), so ammo desyncs (I3). `OnHit` checks ownership, same cell, ≤ 4096 u (unless bow), attacker alive, weapon equipped, `CanHit` cooldown (975-990); the reach check is commented out (1315-1334). Damage is `TES5DamageFormula`.
- The client sends hits only when the aggressor is the player or a hosted NPC (`hitService.ts:15-35`); ghosts' projectiles never cause hits.
- Reuse: ownership/host gate, same-cell check, `hitService` filter, `IDamageFormula` plumbing, the HitTest harness. Replace: bow shot message, trust model, ammo accounting.

## 4. Design

### 4.1 Authority model
- Class B for the fire **event** (owner/host detects it; server validates before relay).
- Class A for ammo, `ammoLoaded`, reload state, cadence budget, hit acceptance, damage (F11).
- Hosted NPC shooters: the host is the shooter; `hosters[npc] == sender` on every message (S6).

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Loaded rounds per weapon instance | u16 | inventory entry extra `ammoLoaded` (F04 schema) | yes (`inv` entry extra) | 0 on pickup; capacity for NPC spawn |
| Ammo total (incl. loaded) | count | inventory (F04) | yes | — |
| Reload in progress `{start, expectedEnd}`, crank count, charge start | struct | `Fo4GunState` per actor (new) | no | — |
| Cadence tracker (next allowed client ts, click-rate bucket, clock-skew estimate) | struct | `Fo4GunState` | no | — |
| Shot registry `{shotId, serverTs, clientTs, instanceHash, origin, dir, projectiles, power, hitsLeft}` (2 s TTL) | ring buffer | `ShotRegistry` (SRV-023) | no | — |
| Anomaly stats (hit ratio by range bucket, headshot ratio, aim-snap score) | rolling | `ShotRegistry::stats` | no | — |

Ammo model [inference, revisit after R3]: the inventory count of the ammo item includes the loaded rounds. A shot removes `ammoPerShot` from the inventory and 1 from `ammoLoaded`. Reload only sets `ammoLoaded = min(capacity, total)`. No `SetInventoryFo4` is pushed per shot: the client engine already predicted it (D3).

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `WeaponFire` (80) | C→S | `idx`, `seq` u16, `ts` u32 (client ms), `instanceHash` u32 (CRC32 of the F04 canonical `ItemKey` encoding of the equipped weapon; also the SRV-022 cache key), `flags` u8 (sighted, automatic, vats, chargeRelease, empty), `firstShotId` u16, `vatsSeq` u16 (present only when `flags.vats`, F18), `shots[] {dtMs u8, originOfs i16[3] (1/32 u from the shooter's position at `ts`), dir i16[2] (yaw, pitch), power u8, projectiles u8, cranks u8}` | U + seq | batched ≤ 50 ms (≤ 20 Hz); ≤ 64 B for ≤ 3 shots (ammo form derived server-side from `instanceHash`, M10), larger batches split | registry 80 |
| `WeaponFire` (80) | S→C relay | `idx`, `serverTs`, `instanceHash`, `flags`, `shots[] {dtMs, originOfs, dir, projectiles}` (no ammo data) | U | after validation | registry 80 |
| `WeaponReload` (81) | C→S | `idx`, `seq`, `ts`, `instanceHash`, `phase` u8 (start, complete, crank, cancel), `clientLoaded` u16 | R | on event | registry 81 |
| `WeaponReload` (81) | S→C (owner/host) | `idx`, `instanceHash`, `kind` u8 (ack, correction), `loaded` u16, `total` u32 | R | on reload complete; on every ammo/cadence reject (this is the ammo correction, S12) | registry 81 |
| `HitReport` (82) | C→S | `seq` u16, `aggressorIdx`, `targetIdx` (long id for ESM actors, S17), `kind` u8 (projectile, melee, bash, thrownImpact), `shotId` u16, `pelletIndex` u8, `clientTs` u32, `rewindTs` u32 (server clock of the target sample being rendered), `hitPosLocal` i16[3], `limb` u8 (BPTD part), `claimFlags` u8 (advisory), optional `shotDesc {origin f32[3], dir i16[2]}` | R ordered | per hit; pellets of one shot coalesced into one report with a pellet mask | registry 82 |
| `DamageApplied` (83) | S→C | see F11; `result=rejected` + `rejectReason` back to the aggressor for rejected hits | R | per accepted/rejected hit | registry 83 (F11) |

`rewindTs`: the F01 relay copy of `UpdateMovementFo4` carries the server receive time in `ts`; the client keeps it per remote sample and reports the sample time it rendered minus its interpolation delay. `shotDesc` lets the server register a shot whose unreliable `WeaponFire` batch was lost; the same ammo/cadence checks run on it.

### 4.4 Client capture (owner side)
- `weaponFireService.ts`: hook `Actor::UseAmmo` (exact shot count, also for hosted NPCs) and the projectile launch (origin, angles, power, count) via PLAT-083; read `GUN_STATE` for sighted/reloading. Do not use `OnPlayerFireWeapon`.
- Reload: `PlayerWeaponReloadEvent` / `reloadStart`, `ReloadComplete` graph events (F02-T03 hooks) → `WeaponReload start/complete`; musket cranks → `crank`.
- Hits: `TESHitEvent` with `usesHitData`; filter like SkyMP (aggressor = player or hosted NPC; target ≠ self); map the projectile to its `shotId` (projectile handle → shot table). Hits from ghost projectiles are ignored.
- Aim assist off against remote players (`Disable Combat Aim Correction` forced on ghosts' targets) [inference].

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Remote shooters:** F02 replays `ActionFire*`/`ActionReload` for the pose; PLAT-083 blocks the engine `Fire`/`Projectile::Launch` on ghosts. On a relayed `WeaponFire`, the client calls `launchCosmeticProjectile(ghost, instance, origin, dir, power, projectiles)` with `bUseOrigin = true` (prior-art §3.2.5), plus muzzle flash and fire sound. The cosmetic projectile has zero damage, the ghost has `AttackDamageMult` 0, and its impacts are never reported.
- **Owner correction:** `WeaponReload kind=correction` → `SetCurrentAmmoCount` and an inventory re-apply (F04); the local HUD shows server counts.
- **Stream-in / respawn / reconnect:** `ammoLoaded` is in the inventory snapshot; the client sets the equipped weapon's ammo count after equip (F05). Transient reload/cadence state is reset.

### 4.6 Validation & anti-cheat
Server checks, in order. Every failure is logged, raises the shooter's anomaly score and sends a correction.
1. Ownership/host (S6); monotonic `seq`; shooter alive, not in a menu-locked state (F07 furniture rules).
2. `instanceHash` matches the hash of the equipped weapon `ItemKey` (F05). Else drop + `WeaponReload correction` + `UpdateEquipmentFo4` correction.
3. Ammo: `total ≥ shots × ammoPerShot` and `ammoLoaded ≥ shots` (Never Ending: total only; NPC without `NPCs Use Ammo`: skip). Computed on a copy (S13) and committed only after step 8.
4. Cadence: per-shot client Δt ≥ `minInterval(instance, WeaponSpeedMult, perks, Rapid) × (1 − hitValidation.cadenceTolerance)`; semi-auto additionally ≤ `combat.maxSemiAutoHz` (15); automatic only with the instance `Automatic` flag. Client clock may not run faster than the server clock by > 5 % + 100 ms over a 5 s window.
5. Reload: shots between `start` and `complete` rejected; `complete` earlier than `reloadSeconds / (reloadSpeed × WeapReloadSpeedMult) × (1 − tol)` → correction with old `loaded`.
6. Projectiles per shot ≤ instance `uNumProjectiles` (+1 Two Shot); `power ≤ min(1, hold/fullPowerSeconds) + tol` using the F02 `ActionGunChargeStart` timestamp; cranks ≤ capacitor max.
7. Origin within `hitValidation.originToleranceUnits` (150) of the shooter's rewound position; `|dir| = 1`.
8. Accepted shots → `ShotRegistry`, relay to neighbours, `onWeaponFire` (blockable: blocked → no relay, no ammo committed, `WeaponReload correction` restores the client's counts).
9. **Hit (`hitValidation.level = rewind`):** shot exists with hits left (pellets, penetration) or `shotDesc` passes 1–7; `t = clamp(rewindTs, now − rtt − maxRewindMs, now)`; target capsule from RACE bounds (× PA scale, F17) at `MovementHistory.Sample(t)`, inflated by `speed × jitter + 30 u`; ray/hitscan/Beam/fast Missile: the ray passes the capsule; Lobber/slow Missile: time of flight `|hitPos − origin| / speed ≈ hitTime − shotTime` with gravity; Flame/Cone: within cone and range; distance ≤ PROJ range and weapon max range × 1.1; angle(dir, hitPos − origin) ≤ cone/2 + tol; limb consistent with the hit height; same `worldOrCell` (the worldspace for exteriors, so cross-cell shots are allowed; SkyMP check). Reject → `DamageApplied result=rejected` to the shooter.
10. `basic` level = SkyMP checks + steps 2–7 + range, no rewind. `off` = SkyMP checks only.
11. **No line of sight** (no server geometry, risk R6). Statistical anomaly detection: hit ratio per range bucket, headshot ratio vs distance, aim-snap (dir angular velocity spike then hit < 100 ms), hits through known-closed doors (F07 state). Scores feed the metric `combat_anomaly_score` and gamemode `onCombatAnomaly` (gamemode decides kick/ban).

### 4.7 Audience / visibility
`WeaponFire` relay: grid neighbours of the shooter except the sender. `WeaponReload` S→C: shooter's owner/host only. `HitReport`: server only. `DamageApplied`: see F11.

### 4.8 NPC parity
Hosts send `WeaponFire`/`WeaponReload`/`HitReport` for hosted NPCs with the same checks. NPC magazines are tracked (reload animations stay plausible); reserve is infinite unless `NPCs Use Ammo`. Host migration: the new host gets `ammoLoaded` through the inventory snapshot. Turrets are NPCs (F10).

### 4.9 Gamemode API & server Papyrus
- Events: `onWeaponFire(actorId, weaponId, shotCount)` (blockable), `onWeaponReload(actorId, weaponId)` (blockable → correction), `onCombatAnomaly(actorId, kind, score)` (observe). `onHit`/`onDamage` are owned by F11.
- Properties: `mp.get(actorId, 'ammoLoaded')` (equipped weapon), `mp.set` (refill; validated against capacity).
- Settings (SRV-002): `hitValidation.level` (`off|basic|rewind`, default `rewind`), `hitValidation.maxRewindMs` (250), `hitValidation.originToleranceUnits`, `hitValidation.cadenceTolerance` (0.1), `combat.maxSemiAutoHz` (15).
- Papyrus: `OnPlayerFireWeapon(akBaseObject)` with vanilla timer semantics (out of combat, throttled); `Weapon.Fire` on the server VM → server-originated shot (trap/script) with `idx` = the ref.

### 4.10 Edge cases & failure modes
- Lost `WeaponFire` batch: the hit's `shotDesc` registers it; ammo stays consistent because the server decrements on registration.
- Weapon switch mid-reload: reload cancelled, `loaded` unchanged.
- **Transfers (drop/put/sell/scrap/trade):** the server moves `ammoLoaded` AMMO items out of the source inventory *with* the weapon (a companion AMMO stack in the dropped/transferred entry, mirroring vanilla `ExtraAmmo`), so rounds are never duplicated (review finding C4). If the source lacks the rounds (desync), `ammoLoaded` is set to 0. On merge of two stacks of the same weapon key, `ammoLoaded = min(values)` (F04). QA-040 includes a drop/pick-up ammo duplication test (F06-T13).
- Shooter disconnects with shots in flight: registry entries stay valid for 2 s (late hits from the shooter are impossible; nothing to do).
- VATS shots (F18) carry `flags.vats` and are resolved by the server; their `HitReport`s are ignored.
- Explosive projectiles (missiles, Fat Man): the shot spawns an explosive entity; detonation and area damage are F10.

### 4.11 Performance budget
- Up: ≤ 20 batches/s × ≤ 64 B per firing actor. Down: relay O(neighbours).
- Server per shot ≤ 5 µs (checks 1–8); per hit ≤ 20 µs (rewind + ray-capsule). `ShotRegistry` ≤ 64 entries per shooter.

## 5. Engine / platform work required
- PLAT-083: block `TESObjectWEAP::Fire` (NG/AE 2198960) and `Projectile::Launch` for ghost shooters; `launchCosmeticProjectile` with `bUseOrigin`; muzzle/impact FX; launch and `UseAmmo` capture hooks (AE IDs need RE, R9).
- `setCurrentAmmoCount(actor, equipIndex, n)` and `PlayerAmmoCountEvent` forwarding.
- `ProjectileLaunchData` layout for AE (prior-art B14 is OG).

## 6. Tests
- `L-unit` (`unit/Fo4GunTest.cpp`, `[F09]`, `[Rewind]`):
  - fire decrements loaded + total; empty magazine rejected with `WeaponReload correction`;
  - cadence faster than min interval rejected; automatic without the flag rejected; semi-auto click cap;
  - reload refills from total; shots during reload rejected; early `complete` rejected;
  - Two Shot accepts N+1 projectiles, N+2 rejected; Gauss power above hold time rejected; musket cranks > capacitor rejected;
  - origin 400 u from the shooter rejected; wrong `instanceHash` rejected;
  - rewind accepts a hit on the position 200 ms back, rejects 400 ms back; ray outside the cone rejected; beyond PROJ range rejected; Lobber time-of-flight mismatch rejected;
  - lost batch + `shotDesc` hit accepted once, duplicate pellet rejected;
  - hosted NPC shooter requires the host; wrong host → `HostStop`;
  - `onWeaponFire` veto → no relay; persistence of `ammoLoaded`; late joiner sees `ammoLoaded` in the owner inventory snapshot;
  - serialize round trips of 80/81/82.
- `L-int`: 2 bots, one fires full auto at the other moving at sprint speed with 150 ms RTT and 2 % loss (QA-021); false-reject rate < 2 % (00-vision §7).
- `L-ts`: shot batching/splitting, projectile-to-shotId mapping, `rewindTs` computation.
- `G-self`: log `UseAmmo` timestamps per weapon/mod set (R1), inventory vs clip counts (R3), cosmetic projectile spawn on a test NPC.
- `G-manual`: two players with pistol, combat rifle (auto), combat shotgun, Gauss, laser musket, minigun; each sees the other's fire/reload; hits register at 100 ms RTT.

## 7. Tasks
- [ ] **F09-T01** Messages `WeaponFire`, `WeaponReload`, `HitReport` (C++ + TS), quantization helpers — M — Depends: NET-002 — Verify: L-unit — Files: falloutmp-server/cpp/messages/{WeaponFireMessage,WeaponReloadMessage,HitReportMessage}.h, Messages.h; falloutmp-client/src/services/messages/
  - Accept: round trips; a 3-shot batch ≤ 64 B.
- [ ] **F09-T02** Weapon instance stats for guns from SRV-022 (capacity, ammo, projectiles, automatic, min interval, reload time, ranges, power) — M — Depends: SRV-022 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/fo4/Fo4WeaponStats.{h,cpp}
  - Accept: synthetic WEAP + OMOD fixtures give the expected stats.
- [ ] **F09-T03** Server ammo/magazine model (`ammoLoaded` extra, atomic decrement, reload) + `WeaponReload` handler and corrections — M — Depends: F09-T01, F09-T02, F04 — Verify: L-unit — Files: fo4/Fo4GunState.{h,cpp}, ActionListener.cpp
  - Accept: §6 ammo/reload cases pass; persistence round trip.
- [ ] **F09-T04** `WeaponFire` handler: checks 1–8, cadence tracker with clock-skew guard, relay — M — Depends: F09-T03, SRV-023 — Verify: L-unit
- [ ] **F09-T05** `HitReport` handler for projectiles: shot matching, `shotDesc` registration, rewind ray/time-of-flight/cone checks, `hitValidation.level` — L — Depends: F09-T04, SRV-023, F01-T05 — Verify: L-unit
  - Accept: §6 rewind cases pass; each reject sends `DamageApplied result=rejected`.
- [ ] **F09-T06** Server timestamp in the F01 relay (`ts` = server receive time) and client `rewindTs` bookkeeping — S — Depends: F01-T05, F01-T06 — Verify: L-unit, L-ts
- [ ] **F09-T07** Anomaly statistics, metrics and `onCombatAnomaly` — M — Depends: F09-T05, SRV-050 — Verify: L-unit, L-int
- [ ] **F09-T08** PLAT-083 integration: capture hooks (`UseAmmo`, launch), cosmetic projectile, ammo count setter — L — Depends: PLAT-083, F02-T03 — Verify: W-ci, G-self
- [ ] **F09-T09** Client `weaponFireService.ts` (capture, batching, reload, hit capture with shot mapping) and remote replay — L — Depends: F09-T01, F09-T08, CLI-050 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/weaponFireService.ts, hitService.ts (FO4 fork)
- [ ] **F09-T10** Hosted-NPC shooter path (host checks, NPC ammo rules) — S — Depends: F09-T05, F13 (hosting) — Verify: L-unit
- [ ] **F09-T11** Gamemode events/properties/settings + Papyrus `OnPlayerFireWeapon`, `Weapon.Fire` server native; docs — M — Depends: F09-T04, PVM-014 — Verify: L-unit, L-int
- [ ] **F09-T12** `L-int` gunfight simulation with netem (QA-021) measuring false rejects — M — Depends: F09-T05, QA-020, QA-021 — Verify: L-int
  - Accept: false-reject rate < 2 % at RTT 150 ms, 2 % loss; result recorded in STATUS.md.
- [ ] **F09-T13** `G-self` research checks R1/R3 and `G-manual` gunfight script — S — Depends: F09-T09 — Verify: G-self, G-manual — Files: docs/falloutmp/test-scripts/F09-ranged-combat.md

- [ ] **F09-T14** (1.x) Navmesh/cell-geometry line-of-sight check for hit validation (R6): load NAVM/collision proxies server-side, ray test from the rewound shooter position; `hitValidation.level = 3` — L — Depends: F09-T07, ESPM-017 — Verify: L-unit

## 8. Open questions & risks
- Fire cadence and time-of-flight checks assume frame-rate-independent physics: **High FPS Physics Fix** is recommended (07-dependencies-and-mods.md); servers may require it via SRV-003 (`clientMods.required`) for PvP.
- R6: no line of sight on the server. Later option: server navmesh/heightfield LOS from ESM NAVM (out of scope here).
- R1–R3 (cadence formula, PROJ classification, ammo decrement timing) must be measured before tolerances are tightened.
- Victim-side LOS confirmation (target client raycasts and flags impossible hits) is a possible T2 extension; it only flags, never decides.
- Fusion-core weapons (Gatling laser) consume core charge, not items: modelled as `ammoLoaded` = shots left in the inserted core; confirm with F17.
