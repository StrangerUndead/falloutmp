# F18 — VATS (T0: disabled; T2: server-resolved VATS-lite)

| Field | Value |
|---|---|
| Tier | T2 (VATS-lite); the T0 "disabled" mode ships with the T0 alpha |
| Target level | L3 (00-vision-scope §4) |
| SkyMP analogue | none (Skyrim has no VATS). Closest patterns: SkyMP's disabled single-player services (`disableFastTravelService.ts`, `disableDifficultySelectionService.ts`) for the off mode; server-resolved RNG outcomes as in leveled lists |
| Milestone | M8 (disabled, CLI-021) / M10 (VATS-lite) |
| Workstreams | CLI, PLAT, SRV, NET, GM |
| Depends on | ADR-013, CLI-021 (VATS menu block), F01-T05 (movement history), F08-T09 (AP spend), F09 (shots, ammo, `flags.vats`), F11 (damage, crits), F19 (Luck, perks), SRV-021 (VATS entry points), SRV-022 (AP cost from OMODs), F20 (slow-time effect rule) |
| References | reference/fo4-systems-combat-character.md §5 (VATS, entire), §4.1 (crit meter fill, crit damage), §1.4 (VATS/VATSCommand/ActionPoints types), §1.6 item 1 (global time scaling), §18 R9/R10; reference/fo4-animation-sync.md §3.4 (VATS and kill cams); reference/prior-art.md §5.4 Q8; 02-architecture.md ADR-013 |

## 1. Summary
Vanilla VATS slows the whole world, which cannot work with one shared server clock. FalloutMP offers two server-selected modes. **Off** (default, T0): the VATS menu cannot be opened, the critical meter is unused, and crits come only from `CritChance` and sneak rules (F11). **Lite** (T2): the player opens a VATS-style targeting view in real time (no slowdown, no kill cams), picks body parts, and the server decides hit chance, AP cost, hits, misses and criticals from its own data (distance, weapon stats, Perception, perks, target movement history). The client then fires predetermined shots at the weapon's real cadence; the server applies the damage. Time-slow chems (Jet) are disabled or converted to non-time effects.

## 2. Vanilla Fallout 4 behaviour
- VATS slows time instead of pausing (`VATS::magicTimeSlowdown`, `playerMagicTimeSlowdown` [src: clf4 V/VATS.h:62-63]); kill cams are a VATS camera mode (fo4-systems §5.1).
- AP cost per attack = WEAP `Action Point Cost` (DNAM 0x70, default 20) with OMOD changes applied **additively** (scopes +30/40/50 %, reflex −15 %, automatic +20 %…); automatics pay per burst (~3 rounds); max 16 queued attacks; entry `ModVATSAttackAP` 0x4F (fo4-systems §5.1).
- Hit chance per body part, capped at 95 %: distance vs weapon range, weapon accuracy, Perception (≈ +3.17 points per PER), BPTD `To Hit Chance`, visibility/cover; perks Awareness, Sniper, Concentrated Fire, Penetrator, Gun Fu, Blitz, Grim Reaper's Sprint, Mysterious Stranger, Critical Banker, Four Leaf Clover, Better Criticals, Quick Hands; legendary VATS Enhanced (+33 % hit, −25 % AP). The exact formula is unknown (**R10**). Engage distance `fVATSMaxEngageDistance` reportedly 1576 + 256/PER (**VERIFY**).
- Critical meter: fill per VATS hit = `(6 + 1.5·Luck + 15·Lucky) % × 1.2^Isodoped`; crits always hit if chance > 0; Critical Banker stores extra crits; `PlayerCharacter::vatsCriticalCharge/vatsCriticalCount/maxVATSCriticalCount` [src: clf4 P/PlayerCharacter.h:472-474] (fo4-systems §4.1, §5.2).
- Restrictions: not the active companion or children; not while jumping or using a jetpack; reveals mines; can shoot grenades in flight.
- Engine data: `VATSCommand{target, limb, hitdata, actionPointCost, damageMult, fireShots, flags}`; `ActionPoints::Action` (Ranged, Reload, Unarmed, …); `ProjectileLaunchData.alwaysHit/intentionalMiss/targetLimb`; `VATSMenu`, `VATSEvents::ModeChange`, `Game.IsVATSPlaybackActive` [src: f4se Game.psc:217], `Game.GetCameraState() == 2` (fo4-systems §5.2–5.3).
- Time-slow effects outside VATS: MGEF archetype **Slow Time 37** (Jet, Deadeye/Resolute legendary) (fo4-systems §1.3, §1.6).
- Single-player assumptions: global slowdown; kill-cam camera; zero-time rotation between queued targets; Blitz teleport; Mysterious Stranger spawns a local NPC; damage reduction while in VATS; local RNG.

## 3. SkyMP baseline
No analogue. Reused patterns: client services that disable single-player features (`disableFastTravelService.ts`), `CombatRng`-style server randomness with results sent to clients, server AP accounting (F08), shot registry (SRV-023).

## 4. Design

### 4.1 Authority model
Class A: mode, eligibility, AP cost, hit chance, every roll, crit meter and banked crits, damage. Class D: the targeting UI. Shots are class B events (F09) carrying `flags.vats`, but their outcome is predetermined by the server.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| `vats.mode` | `off` \| `lite` | server settings (SRV-002) | settings | `off` |
| Crit charge (0–100) | float | `MpActor::vats.critCharge` | yes (`vats.critCharge`) | 0 |
| Banked crits | u8 | `MpActor::vats.critCount` | yes (`vats.critCount`) | 0 |
| Pending sequence `{seq, target, attacks[], results[], resolvedAt, executed}` | struct | `Fo4VatsState` (new) | no | — |
| Preview rate limiter | bucket | `Fo4VatsState` | no | — |
| Hit-model constants | table | `vats.hitModel.*` settings | settings | §4.6 defaults |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `VatsAction` (101) | C→S | `seq` u16, `kind` u8 (preview, request, cancel), `targetIdx`, `instanceHash` u32, `attacks[≤16] {limb u8 (BPTD part), useCrit bool}`, `ts` u32 | R ordered | preview ≤ 4 Hz while VATS is open; request on execute | registry 101 |
| `VatsAction` (101) | S→C (owner) | `seq`, `kind` u8 (preview, result, rejected, state), `reason` u8, `chances[] {limb u8, pct u8}` (preview), `shots[] {limb u8, hit bool, crit bool}` (result), `apAfter` f32, `critCharge` f32, `critCount` u8 | R ordered | reply; `state` on login and on every crit-meter change | registry 101 |
| `WeaponFire` (80) | C→S | normal F09 shot batch with `flags.vats` set and the optional `vatsSeq` u16 | U + seq | real cadence | registry 80 (F09) |
| `DamageApplied` (83) | S→C | `hitKind=vats`, crit flag, limb | R | when each shot's flight time elapses | registry 83 (F11) |
| `ChangeValuesAv` (72) | S→C | AP after deduction | R | on resolution | registry 72 (F08) |

The optional `vatsSeq` u16 is part of the F09 `WeaponFire` layout (present only when `flags.vats` is set); F18-T02 implements it together with `VatsAction` and bumps the FO4 protocol version.

### 4.4 Client capture (owner side)
- **Off:** `vatsService.ts` closes `VATSMenu` on open and swallows the VATS key (CLI-021); `VatsAction` is never sent.
- **Lite:** on `VATSMenu` open, the platform forces the slowdown values to 1.0 every frame and disables kill cams; the client sends `preview` with the targeted actor and shows the server chances (written over the engine's numbers if hookable, else a CEF overlay list). On execute, the queued `VATSCommand`s are converted to `request` attacks. VATS is refused locally while jumping, in a jetpack burn, or in third person if the platform cannot fix the camera.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- On `result`: the client plays each attack at the weapon's real cadence (no zero-time rotation; it turns at the normal aim speed), launching projectiles with `alwaysHit`/`intentionalMiss` and `targetLimb` from `shots[]`, and sends them as `WeaponFire` with `flags.vats`/`vatsSeq`. The crit display uses `SetVATSCriticalCount` with the server `critCount`.
- On `rejected`: close the menu, set AP from `apAfter`, show the reason.
- Remote clients see ordinary F09 fire relays and animations; there is no camera effect for them.
- Stream-in/reconnect: `kind=state` restores the crit meter; pending sequences are dropped on disconnect.

### 4.6 Validation & anti-cheat
1. `vats.mode == lite`, else `rejected(reason=disabled)`.
2. Shooter alive, not downed, not jumping/jetpacking (F01 flags), weapon is a gun (melee VATS rejected in v1, `reason=unsupportedWeapon`), `instanceHash` equipped (F05).
3. Target: alive, not the shooter's companion (F21), not a child race, not a player unless `vats.allowVsPlayers`; within `fVATSMaxEngageDistance` (GMST by EDID, PER-adjusted per R10) of the rewound positions; inside the shooter's front half-space (yaw from the latest F01 sample).
4. AP: `cost = WEAP AP × (1 + Σ additive OMOD percentages) × P(ModVATSAttackAP) × (VATS Enhanced ? 0.75 : 1)`, automatics per burst; `Σ cost ≤ AP` via F08 `Spend` (deducted at resolution). Ammo: `Σ shots ≤ loaded` (F09 model).
5. **Hit chance** per attack (placeholder model, constants calibrated by F18-T05, cap `vats.hitModel.cap` 0.95):
   `p = BPTD.toHit(limb) × rangeFactor(d / weaponMaxRange) × accuracyFactor(AMDL, accuracy bonus) × moveFactor(target lateral speed from MovementHistory over 500 ms) × (1 + PER × vats.hitModel.perPerception) × P(ModVATSHitChance) × (limb == head ? P(ModVATSHeadShotChance) : 1) × (VATS Enhanced ? 1.33 : 1)`; PvP × `vats.vsPlayerHitMult` (0.5).
6. Rolls from `CombatRng` (seeded per server; `seq` recorded for audits). `useCrit` with `critCount > 0` (or full meter) → that attack hits and crits; crit count/charge decremented. Each non-crit hit fills the meter by the Luck formula; overflow banks a crit up to the Critical Banker maximum.
7. Execution: each predetermined shot must arrive as a `WeaponFire` with matching `vatsSeq` within `vats.executeTimeoutMs` (5000) and pass F09 checks 1–7 (cadence, ammo). The server applies F11 damage for hit shots when `distance / projectile speed` has elapsed after the shot (target dead or out of range by then → miss). `HitReport`s for VATS shots are ignored. Extra or mismatched VATS shots → F09 correction and anomaly score.
8. `preview` requests are rate-limited (4 Hz); excess → dropped.

### 4.7 Audience / visibility
`VatsAction` → owner only. Fire relays and `DamageApplied` follow F09/F11 audiences.

### 4.8 NPC parity
NPCs do not use VATS. Hosted NPCs are valid targets with the same rules. Companions (F21) cannot be targeted by their owner.

### 4.9 Gamemode API & server Papyrus
- Settings (SRV-002): `vats.mode` (`off|lite`, default `off`), `vats.allowVsPlayers` (false), `vats.vsPlayerHitMult` (0.5), `vats.executeTimeoutMs` (5000), `vats.hitModel.*`, `effects.slowTime` (`disable|convert`, default `disable`; owned and applied by F20).
- Events: `onVatsAttack(actorId, targetId, attacks)` (blockable → `rejected(reason=gamemode)`), `onVatsCritical(actorId, targetId)` (observe).
- Properties: `mp.get(actorId, 'vatsCritCharge' | 'vatsCritCount')` (rw).
- Papyrus: none VATS-specific on the server; `Game.IsVATSPlaybackActive` stays client-local.
- **Slow Time effects (MGEF archetype 37):** rule owned by F20 (§4.6 archetype table): `disable` (default) = the effect has no time effect; `convert` = aim stability + damage bonus for the effect's duration, never global time scaling. The client never applies a local time slow (F20 skips archetype 37 when mirroring effects). F18 only requires that no code path slows world or player time.

### 4.10 Edge cases & failure modes
- Target dies mid-sequence: remaining shots miss; unused AP is not refunded (vanilla spends AP on execute).
- Shooter takes damage or gets staggered mid-sequence: the client may abort; unexecuted attacks are forfeited after the timeout.
- Shooter disconnects with a pending sequence: dropped; crit meter state persisted as of resolution.
- Mode switched to `off` at runtime: pending sequences cancelled, clients get `state`, menu block re-enabled.
- PA core drain from VATS (F17) is applied with the AP spend.

### 4.11 Performance budget
`request` ≤ 60 B, `result` ≤ 64 B; resolution ≤ 50 µs for 16 attacks. Previews ≤ 4 Hz × ≤ 24 B per player in VATS.

## 5. Engine / platform work required
- `VATSMenu` open/close interception and VATS key swallow (CLI-021 off mode).
- RE (R9): VATS slowdown write points; force `magicTimeSlowdown`/`playerMagicTimeSlowdown` to 1.0; disable kill-cam camera mode and Blitz teleport.
- Read the queued `VATSCommand` list on execute; launch predetermined projectiles with `alwaysHit`/`intentionalMiss`/`targetLimb` (extends PLAT-083).
- `SetVATSCriticalCount` / crit charge display; optional override of displayed hit chances.

## 6. Tests
- `L-unit` (`unit/Fo4VatsTest.cpp`, `[F18]`):
  - mode `off` rejects every request with `rejected(disabled)` and an AP state;
  - AP cost with additive OMOD percentages (scope +40 % and reflex −15 % → ×1.25), VATS Enhanced −25 %, insufficient AP rejected;
  - hit chance never > 95 %; moving target lowers chance; out of engage distance rejected;
  - companion and child targets rejected; player target rejected unless `allowVsPlayers`;
  - crit meter fill: Luck 1 → full after 14 hits, Luck 10 → after 5 hits; banked crit consumed by `useCrit`; Critical Banker cap;
  - seeded `CombatRng` gives deterministic results for a fixed `seq`;
  - damage applied after flight time; target dead before impact → miss; VATS `HitReport` ignored; missing `WeaponFire` within timeout → forfeited;
  - `onVatsAttack` veto; persistence round trip of `vats.critCharge/critCount`;
  - `VatsAction` serialize round trip; `WeaponFire` with `vatsSeq`.
- `L-ts`: preview throttling; conversion of queued commands to attacks.
- `G-self`: VATS opens with no world slowdown (frame-time and NPC speed unchanged); kill cams never trigger; VATS key is swallowed in `off` mode.
- `G-manual`: two players, lite mode, one uses VATS on a raider; both see the same hits and limb crippling; Jet (both `effects.slowTime` modes) does not slow anyone.

## 7. Tasks
- [ ] **F18-T01** Off mode: client `vatsService.ts` blocks `VATSMenu` and the VATS key (with CLI-021); server rejects `VatsAction` when off — S — Depends: CLI-021, F18-T02 — Verify: L-unit, G-self — Files: falloutmp-client/src/services/services/vatsService.ts
  - Accept: the self-test cannot open VATS; a forged request is rejected with a correction.
- [ ] **F18-T02** `VatsAction` message (C++ + TS) and the `WeaponFire.vatsSeq` optional field — S — Depends: NET-002, F09-T01 — Verify: L-unit — Files: falloutmp-server/cpp/messages/VatsActionMessage.h, WeaponFireMessage.h; falloutmp-client/src/services/messages/vatsActionMessage.ts
- [ ] **F18-T03** Eligibility checks (mode, shooter state, target rules, engage distance, front cone) — S — Depends: F18-T02, F01-T05 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/fo4/Fo4VatsService.{h,cpp}; unit/Fo4VatsTest.cpp
- [ ] **F18-T04** AP cost model (WEAP AP + additive OMOD percentages via SRV-022, entry points via SRV-021, burst cost) and F08 spend — S — Depends: F18-T03, SRV-022, F08-T09 — Verify: L-unit
- [ ] **F18-T05** Hit-chance model with settings constants, movement-history speed, BPTD to-hit, perks; calibration procedure against vanilla (R10) — M — Depends: F18-T03, SRV-021 — Verify: L-unit, G-self
  - Accept: unit cases pass; a G-self calibration table (engine-displayed vs server chance) is recorded and constants updated.
- [ ] **F18-T06** Resolution: `CombatRng` rolls, crit meter fill/banking, `result`/`state` replies, persistence of `vats.*` — M — Depends: F18-T04, F18-T05, F11-T03 — Verify: L-unit
- [ ] **F18-T07** Execution path: match `WeaponFire` shots by `vatsSeq`, timeout/forfeit, apply F11 damage after flight time, ignore VATS `HitReport`s — M — Depends: F18-T06, F09-T04 — Verify: L-unit
- [ ] **F18-T08** Platform: VATS slowdown neutralisation, kill-cam/Blitz disable, `VATSCommand` read, predetermined projectile launch, crit count display (RE R9) — L — Depends: PLAT-083, PLAT-040 — Verify: W-ci, G-self
- [ ] **F18-T09** Client lite mode in `vatsService.ts`: preview, execute, cadence playback, rejections — M — Depends: F18-T07, F18-T08, CLI-050 — Verify: L-ts, G-manual
- [ ] **F18-T10** Verify no time slow anywhere: client skip of MGEF archetype 37 when mirroring effects, kill-cam/VATS slowdown off in both modes; uses the F20 `effects.slowTime` rule — S — Depends: F20-T02, F18-T08 — Verify: L-unit, G-self
- [ ] **F18-T11** Gamemode settings/events/properties + docs (DOCS-003) — S — Depends: F18-T06 — Verify: L-unit, L-int
- [ ] **F18-T12** `G-manual` VATS-lite script — S — Depends: F18-T09 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F18-vats.md

## 8. Open questions & risks
- R9: whether the vanilla VATS UI works with the slowdown forced to 1.0 (it may assume slow time for camera transitions). Fallback: a CEF targeting overlay driven by server previews, with `VATSMenu` blocked in both modes.
- R10: the vanilla hit-chance formula is unknown; the placeholder model is tuned empirically.
- Melee VATS and Blitz are out of scope for v1; Mysterious Stranger, Grim Reaper's Sprint and Gun Fu need server rules (F19 perks) before they are honoured.
- PvP VATS is disabled by default; balance needs playtests.
