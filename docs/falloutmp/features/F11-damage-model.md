# F11 — Damage Model (FO4 Formula, Resistances, Limbs, Crits, Sneak, Perks, Difficulty)

| Field | Value |
|---|---|
| Tier | T0 |
| Target level | L4 (SkyMP L3 with `TES5DamageFormula`; FalloutMP computes every hit on the server with the FO4 rules) |
| SkyMP analogue | `IDamageFormula` / `TES5DamageFormula` (power ×2, blocked ×0.1, sneak ×1.3) and decorators `DamageMultFormula`, `DamageMultConditionalFormula`; `PartOne::SetDamageFormula`; `OnWeaponHit` → `NetSetPercentages`. SkyMP level L3 (reference/skymp-sync-inventory.md §2 "Melee hits & damage") |
| Milestone | M8 |
| Workstreams | SRV, NET, CLI, GM, PVM |
| Depends on | F08 (AV store, limb AVs), SRV-020 (effects: DoTs, legendary enchantments), SRV-021 (perk entry points), SRV-022 (OMOD stat resolver), REF-006 (damage formula factory), F05 (worn armor), F09/F10 (validated hits and explosions), F29 (detection state for sneak) |
| References | reference/fo4-systems-combat-character.md §4 (entire), §1.3 (DMGT/BPTD/ARMO/MGEF), §1.4 (`HitData`, `CombatFormulas`), §1.7 (`PerkEngine`, `CombatRng`), §16.3 (legendary mutation), §18 R4/R6/R7/R11; reference/skyrim-coupling-index.md §1.13; reference/prior-art.md §3.1.7 (HP funnel, server HP pool), §3.2.5 (TE damage hook); reference/fo4-data-formats.md §4.7, §4.10, §4.18 |

## 1. Summary
Every hit (bullet, pellet, melee swing, bash, explosion, damage-over-time tick, trap) is turned into damage by one server function, `Fo4DamageFormula`, using FO4's per-damage-type resistance curve, armor penetration, body-part multipliers, limb damage and crippling, criticals, sneak multipliers, perk entry points, legendary effects and the server difficulty. Clients never apply damage to anything; they render the server result (`DamageApplied` + `ChangeValuesAv`). PvP is tunable separately from PvE. Every outcome that depends on randomness (crit roll, dismemberment) comes from a server RNG and is sent to clients, so all clients show the same result.

## 2. Vanilla Fallout 4 behaviour
- **Damage types**: physical (resisted by armor rating + `DamageResist` 0x2E3), energy (`EnergyResist` 0x2EB), radiation (poisoning adds Rads, resisted by `RadResistExposure` with the same curve), poison (`PoisonResist`, DoT ~10 s), bleed (no resistance), explosive (a modifier on another type). Fire/cryo/electric are energy + a DMGT spell. A multi-type hit resolves **one independent hit per type** (fo4-systems §4.1).
- **Curve** [web: Damage Resistance, fo4-systems §4.1]: `coeff = clamp((fPhysicalDamageFactor · D / R) ^ fPhysicalArmorDmgReductionExp, 0.01, 0.99)`, GMSTs 0.15 and 0.365 (read from the ESM by EDID); `R = 0 → 0.99`; `final = D · coeff`.
- **D in the coefficient**: physical uses paper damage; energy uses weapon base × range mult; multi-projectile weapons use the **total** of all projectiles; explosion and impact are separate hits.
- **Pipeline**: `Final = Paper × coeff × bodyPartMult × sneakMult`, then difficulty. Total worn DR applies regardless of hit location.
- **Difficulty** (player deals / enemy deals): VeryEasy ×2/×0.5, Easy ×1.5/×0.75, Normal ×1/×1, Hard ×0.75/×1.5, VeryHard ×0.5/×2, Survival ×0.75/×4 (+Adrenaline). Companions unmodified. Enum: 0–4, 5 defunct, 6 TrueSurvival (fo4-systems §1.4, §4.1).
- **Penetration**: `ArmorPenetration` AV 0x97341, entry `ModTargetDamageResistance` 0x25, Rifleman rank 5, *Penetrating* 30 % (**VERIFY** perk data).
- **Sneak**: base ranged ×2 / melee ×3 [inference, **VERIFY R11**]; Ninja ranks 2.5/3/3.5 and 4/5/10; Mister Sandman +15/30/50 % silenced; vanilla add-vs-multiply ordering quirk.
- **Crits**: from VATS crit meter (F18), sneak, or `CritChance` 0x2DD. Ranged `Paper + Base × CritMult`; melee `Paper × 1.5 + Base × CritMult`; `CritMult` from WEAP `CRDT` (2.0), `CriticalHitDamageMult` 0x39C, perks `CalculateCriticalHitDamageMult` 0x2. Crits ignoring DR is **VERIFY R11**.
- **Limbs**: BPTD part `Damage Mult`, `Health Percent`, limb `Actor Value`, flags; engine `CombatFormulas::CalcTargetedLimbDamage`; limb formula not public (**VERIFY R4**); crippled at 0 → `OnCripple`; entries `ModIncomingLimbDamage` 0x4, `ModOutgoingLimbDamage` 0x73; head ×2 typical [inference].
- **Perk entry points** touching damage: 0x23 ModAttackDamage, 0x24 ModIncomingDamage, 0x25, 0x27 ModPercentageBlocked, 0x5D/0x5E typed, 0x1C power, 0x1A bashing, 0x12 sneak, 0x1/0x2 crit, 0x63/0x7C explosion, 0x33 ApplyCombatHitSpell, 0x88 Bloody Mess (fo4-systems §4.1).
- **Legendary weapon effects** are OMODs (Two Shot, Explosive, Instigating, Wounding, Furious, Kneecapper, Penetrating, Staggering, race-group +50 %, Bloodied, Junkie's…) (fo4-systems §4.1). **Legendary enemies** mutate once below 50 % HP (heal + spell) (fo4-systems §16.1).
- **Engine hooks**: `TESHitEvent.hitData` breakdown, `CombatFormulas::CalcWeaponDamage/CalcResistedPercentage/CalcTargetedLimbDamage` (golden vectors in G-self); the HP funnel is the single chokepoint for health deltas — clamp Health ≥ 1 there, don't gate `Kill` (prior-art §3.1.7).
- Single-player assumptions: per-player difficulty; player-vs-NPC asymmetric multipliers; local RNG for crits/gore; the engine computes damage independently (must never double-apply).

## 3. SkyMP baseline
- `IDamageFormula::CalculateDamage(aggressor, target, hitData)` [src: formulas/IDamageFormula.h:7-19]; `TES5DamageFormula.cpp:127-166`; decorators `DamageMultFormula`, `DamageMultConditionalFormula.h:92-117`; installed by `PartOne::SetDamageFormula` [src: PartOne.cpp:131,484-500]. Result → `NetSetPercentages(health)` → `ChangeValues` to the target owner; Papyrus 7-arg `OnHit` [src: ActionListener.cpp:1250-1425].
- Reuse: the interface, the decorator chain (gamemode PvP/zone multipliers), `GetActorToSendTo`. Replace: the formula, the thin `HitData` (→ `HitContext`), the Skyrim OnHit signature.

## 4. Design

### 4.1 Authority model
Class A. The server computes all damage. `HitReport.clientDamage`/engine `hitData` are logged only (formula-drift metric). Ghosts are immortal locally; the local player is held at HP ≥ 1 by the funnel clamp (F12).

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Health, limb conditions, Rads | AV | `Fo4ActorValueStore` (F08) | yes (`fo4Avs`) | F08 |
| DoT/legendary/crit effects | effects | `Fo4EffectSystem` (SRV-020) | yes (F20 `fo4Effects`) | — |
| Damage attribution `{aggressor → damage, lastHitTs}` per victim (single source for the F12 killer, F13 threat `damageShare` and F19 kill XP) | map, 30 s TTL per entry | `MpActor::damageLedger` (new) | no (cleared after F19 distribution on death, and on respawn) | — |
| Damage-type table (DMGT → resist AV, spell) | table | `Fo4DamageTypes` (GameProfile, from ESM) | — | ESM |
| Difficulty and PvP settings | settings | `difficulty`, `pvp`, `combat.*` (SRV-002) | server settings | §4.9 |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `DamageApplied` (83) | S→C | `seq` u32, `targetIdx`, `aggressorIdx`, `hitKind` u8 (projectile, melee, bash, explosion, dot, trap, fall, script, vats), `hitRef` (`shotId` u16 / `explosionId` u32 / `hitSeq` u16), `result` u8 (applied, blocked, immune, rejected, ignoredDead), `rejectReason` u8, `sourceForm` u32, `amounts[] {dmgType u8, value f32}` (post-resistance), `total` f32, `limb` u8 (0xFF none), `limbDelta` f32, `flags` u16 (crit, sneak, power, bash, headshot, explosion, kill, crippled, dismember, legendaryMutate, downed), `crippledMask` u16, `stagger {magnitude u8, dirYaw i16}`, `knockdown` bool, `goreSeed` u32, `healthAfterPct` u8 | R ordered | coalesced per (aggressor, target, shot) per tick (pellets summed) | registry 83 |
| `ChangeValuesAv` (72) | S→C | new Health/limb/Rads values (F08) | R | coalesced per tick | registry 72 (F08) |

Audience variants: **full** to the target's owner/host and the aggressor's owner/host (the aggressor gets `amounts`, `total`, `flags`, `result`, `rejectReason`); **fx** subset (`targetIdx`, `flags`, `limb`, `goreSeed`, `stagger`) to grid neighbours only when `flags` has kill/dismember/crippled/crit/explosion.

### 4.4 Client capture (owner side)
None for damage: hits come from F09/F10 `HitReport` and `ExplosionEvent`. The client logs its engine `TESHitEvent.hitData` (`totalDamage`, `resistedPhysicalDamage`…) with `hitSeq` for the drift metric (debug setting).

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- Target owner: Health/limbs from `ChangeValuesAv`; `DamageApplied` drives hit reactions: cripple FX, stagger (F10), knockdown, crit/sneak HUD message, kill. Engine damage on the local player is neutralised at the HP funnel (clamp + restore to server value).
- Aggressor: hit marker / "sneak attack ×2" message from `flags`; rejected hits show nothing.
- Neighbours: `fx` subset → dismember the same part with `goreSeed` on death (`Actor.Dismember`, `SetCriticalStage`), crippled FX.
- Stream-in/reconnect: state comes from F08 snapshots; no replay of past hits.

### 4.6 Validation & anti-cheat

**Step 0 — target immunity table (review finding M8).** Before any damage math the server returns `result=immune` when: the target's character editor is open (`isRaceMenuOpen`, F03 `appearance.protectWhileEditing`); the target NPC is in a special untargetable state reported and verified by F13 (`burrowed`, `inTransit`); the target is a companion and `companions.pvpDamage` is false for a non-hostile attacker (F21); the target has the god-mode/invulnerable AVIF flag or `lifeState == bleedout` with `death.bleedoutInvulnerable`; the attacker and target are party members and friendly fire is off (F32). Tests cover each row.
The damage computation itself is the main control: clients cannot influence any input except through validated hits.

`Fo4DamageFormula : IDamageFormula`, input `HitContext {aggressor, target, weaponInstance, ammo, projectileIndex, projectileCount, power, cranks, limb, distance, flags (power, bash, blocked, sneak, crit, explosion, ricochet — all **server-derived**), explosionDistance, innerR, outerR}`:
1. Instance stats (SRV-022): per-type base `base_t` (Damage-Base + `DAMA` + ammo damage + legendary). Range mult `rm`: 1 up to min range, linear to out-of-range mult at max range. Charge factor (Gauss power, musket cranks).
2. `paper_t = base_t × (melee|unarmed ? 1 + 0.1·STR : 1) × P(ModAttackDamage) × P(ModTypedAttackDamage, t) × AttackDamageMult × (power ? 1.5 × P(ModPowerAttackDamage) : 1) × (bash ? P(ModBashingDamage) : 1) × rm × charge`, where `P(ep)` = `PerkEngine.Apply(ep, ctx, 1.0)` (SRV-021).
3. `D_t` = paper (physical) or `base_t × rm` (energy, radiation), summed over all projectiles of the shot.
4. `R_t = (resistAV_t(target) + armor_t(target worn, F05)) × (1 − penetration)`, then `P(ModTargetDamageResistance)`; immunity flags honoured (radiation immunity).
5. `coeff_t` per the curve with `f = GMST` (PvE) or `combat.pvpResistFactor` (PvP, default 0.15 = vanilla; 1.5 = FO76-style).
6. Crit (server roll or VATS banked crit): add `base_t × CritMult` (melee: paper ×1.5 first) — before the curve, or after it when `combat.critBypassesResistance` is true (default false until R11).
7. `final_t = paper_t × coeff_t × bodyPartMult(BPTD) × sneakMult × P_target(ModIncomingDamage) × P_target(ModTypedIncomingDamage, t) × (explosion ? P(ModIncomingExplosionDamage) × falloff : 1)`.
8. Difficulty: aggressor player → NPC: `playerDeals[difficulty]`; NPC → player: `enemyDeals[difficulty]`; player → player: `combat.pvpDamageMult` (default 1.0), never difficulty; companions: ×1. Then gamemode decorators (`DamageMultConditionalFormula`) and `onDamage`.
9. Radiation type → `Rads += final` (F08) instead of Health. Poison/bleed/fire DMGT spells → effects (SRV-020); each DoT tick is a resisted `hitKind=dot` hit.
10. Limb: `limbDelta = total × limbScale(BPTD Health Percent) × P(ModOutgoingLimbDamage) × P_target(ModIncomingLimbDamage)` [inference, R4]; limb AV −= limbDelta; at 0 → crippled (F08 `onCripple`, Papyrus `OnCripple`).
11. Legendary hooks (data table keyed by OMOD/ENCH): Two Shot (+1 projectile, F09), Explosive (spawns an F10 explosion at the hit position), Instigating (×2 at full target health), Wounding (bleed effect), Kneecapper (20 % leg cripple roll), Staggering (stagger roll), race-group ×1.5 via keywords; others through the generic perk/effect path.
12. Apply: Health −= Σ final; ledger update; kill/downed → F12; then the F13 legendary-mutation hook (F13-T11: first time < 50 % and alive → full heal + mutation spell, `flags.legendaryMutate`).
Validation: flags are never taken from the client — power from F02 actions + AP spend (F10), sneak from F29 detection + F01 sneak flag, blocked from F10, crit from `CombatRng`/F18.

### 4.7 Audience / visibility
See §4.3. `ChangeValuesAv` follows F08 (owner full, neighbours public subset).

### 4.8 NPC parity
NPC targets and NPC aggressors use the same function. NPC perks come from NPC_ `PRKR`; NPC legendary status from F14. Hits by hosted NPCs arrive through the host (F09/F10) and are validated as such.

### 4.9 Gamemode API & server Papyrus
- `onHit(aggressorId, targetId, hitInfo)` blockable (→ `DamageApplied result=blocked` to the aggressor; no state change); `onDamage(aggressorId, targetId, info) → number | false` (replace or veto the total); `onCripple` (F08); `onLegendaryMutate` is owned by F13 (called from step 12).
- Settings (SRV-002): `difficulty` (0–4, 6; default 2), `pvp` (bool), `combat.pvpDamageMult` (1.0), `combat.pvpResistFactor` (0.15), `combat.critBypassesResistance` (false), `combat.friendlyFire` (false for party members, gamemode-defined parties).
- Papyrus: FO4 9-arg `OnHit(akTarget, akAggressor, akSource, akProjectile, abPowerAttack, abSneakAttack, abBashAttack, abHitBlocked, asMaterialName)` delivered **only to registered scripts** (`RegisterForHitEvent` filters, single-shot) (fo4-systems §1.5; PVM-007); `ApplyCombatHitSpell` perk spells; `OnCripple`/`OnPartialCripple`.

### 4.10 Edge cases & failure modes
- Several hits on one target in one tick: applied in arrival order; the kill goes to the hit that crosses 0.
- Target already dead → `result=ignoredDead`.
- Shotgun pellets of one shot coalesce into one `DamageApplied`; the coefficient uses the shot total.
- Explosion damage to the thrower: allowed (vanilla), gamemode can veto in `onDamage`.
- A perk's condition needs a CTDA function not yet implemented: entry skipped with a once-per-perk warning (R7 tracks coverage).
- GMST missing from a modded load order: use the vanilla default and log.

### 4.11 Performance budget
≤ 30 µs per hit with ≤ 6 damage types and ≤ 20 perk entries; perk entry lists cached per actor and invalidated on perk/effect change. `DamageApplied` full ≤ 96 B (amounts[] capped at 3 damage types per message; M10), fx ≤ 16 B.

## 5. Engine / platform work required
- Client HP funnel detour (prior-art C8): clamp Health ≥ 1 for the local player and hosted NPCs, record engine deltas for the drift metric — shared with F12-T02.
- G-self tool calling `CombatFormulas::CalcWeaponDamage/CalcResistedPercentage/CalcTargetedLimbDamage` to produce golden vectors (`data/fo4/damage-golden.json`, generated on the user's machine, not committed if it contains game data — numbers only are fine).
- `dismember(actor, part, seed)`, `setCriticalStage` natives for consistent gore.

## 6. Tests
- `L-unit` (`unit/Fo4DamageFormulaTest.cpp`, `[F11]`, `[Fo4DamageFormula]`):
  - curve vectors: D25/R10 → 17.5; D25/R25 → 12.5; D50/R50 → 25.0; D50/R10 → 45.0; D100/R250 → 35.8; D250/R100 → 174.8; D1000/R1000 → 500.3; R = 0 → 0.99·D; D100/R1 → 99 (cap);
  - shotgun 8 × 6.25 vs R10: coefficient on total 50 → 45.0 total (per-pellet coefficient would give ≈ 21.1);
  - explosion and impact resolved independently; multi-type hit = independent hits;
  - power attack ×1.5; melee STR 10 → ×2; crit ranged (paper 30, base 30, CritMult 2 → 90 before curve) vs melee (→ 105);
  - sneak ×2 ranged / ×3 melee; Ninja rank 1 → 2.5/4; ordering quirk reproduced by perk priority;
  - difficulty table both directions; PvP ignores difficulty and uses `pvpDamageMult`; companion ×1;
  - limb reaches 0 → crippled flag + `onPapyrusEvent:OnCripple`; poison DoT ticks resisted each tick; radiation type adds Rads not Health;
  - Instigating ×2 only at full health; legendary NPC mutates once below 50 %;
  - `onHit` veto → no AV change and `DamageApplied result=blocked`; `onDamage` override;
  - flags claimed by the client are ignored (claimed sneak without detection state → no sneak mult);
  - OnHit delivered only to a registered script, once.
- `L-int`: bot PvP duel, both see the same health after N hits; kill attribution.
- `D-real` (`[fo4data]`): GMST values, DMGT table, BPTD head mult per race.
- `G-self`: golden vectors from `CombatFormulas` for 20 weapon/armor combos; server within 1 %.
- `G-manual`: player vs raider with pistol, shotgun, power attack, sneak attack; crippled legs visible to both players.

## 7. Tasks
- [ ] **F11-T01** `HitContext`, damage-type table from DMGT, `Fo4DamageFormula` steps 1–8 with the FO4 profile factory (REF-006) — L — Depends: REF-006, SRV-022, F08-T02 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/formulas/Fo4DamageFormula.{h,cpp}, fo4/HitContext.h, fo4/Fo4DamageTypes.{h,cpp}; unit/Fo4DamageFormulaTest.cpp
  - Accept: all curve, shotgun, difficulty and multiplier vectors pass.
- [ ] **F11-T02** Perk entry-point integration (damage entries of §2) via SRV-021, with per-actor caching — M — Depends: F11-T01, SRV-021 — Verify: L-unit
  - Accept: Ninja/Rifleman/Big Leagues fixtures; priority ordering test.
- [ ] **F11-T03** Crits and sneak: `CombatRng`, crit chance/mult, server-derived sneak (F29/F01), `combat.critBypassesResistance` — M — Depends: F11-T01, F29 (detection) — Verify: L-unit
- [ ] **F11-T04** Limb damage, crippling, `crippledMask`, `OnCripple`, BPTD tables — M — Depends: F11-T01, F08-T01, ESPM-009 — Verify: L-unit, D-real
- [ ] **F11-T05** DoTs and radiation damage via SRV-020 effects (poison, bleed, fire/cryo/electric spells, rad poisoning → Rads) — M — Depends: F11-T01, F20-T02 — Verify: L-unit
- [ ] **F11-T06** Legendary weapon effects table (OMOD/ENCH handlers) and the post-damage call into the F13-T11 mutation hook — M — Depends: F11-T02, F13-T11 — Verify: L-unit
- [ ] **F11-T07** `DamageApplied` message (full/fx variants), coalescing per tick, damage ledger — M — Depends: NET-002, F11-T01 — Verify: L-unit — Files: falloutmp-server/cpp/messages/DamageAppliedMessage.h; falloutmp-client/src/services/messages/damageAppliedMessage.ts
  - Accept: round trip; pellet coalescing test; audience test (fx only for kill/dismember/crit).
- [ ] **F11-T08** PvP/difficulty settings and gamemode `onHit`/`onDamage`, decorator chain kept — S — Depends: F11-T01, SRV-002 — Verify: L-unit, L-int
- [ ] **F11-T09** FO4 `OnHit` (9 args, registration-gated) on the server VM — S — Depends: PVM-007, F11-T07 — Verify: L-unit
- [ ] **F11-T10** Client apply: `damageService.ts` (hit reactions, crit/sneak messages, gore with seed), drift logging — M — Depends: F11-T07, CLI-050 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/damageService.ts
- [ ] **F11-T11** G-self golden-vector tool and comparison report — S — Depends: PLAT-031 — Verify: G-self
  - Accept: ≥ 20 combos within 1 % of `CombatFormulas`, or the deltas explained and the plan updated.

## 8. Open questions & risks
- R4 (limb formula, head mult per race), R11 (base sneak mults, crit vs DR), R7 (CTDA coverage of vanilla perks) need data before final tolerances.
- Survival-difficulty Adrenaline and damage multipliers are F20 survival rules; this spec exposes the hook only.
- Bloody Mess/gore determinism depends on `dismember` natives behaving the same with the same seed on all clients (G-self).
