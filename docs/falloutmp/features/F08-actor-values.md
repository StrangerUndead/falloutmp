# F08 — Actor Values (Health, AP, Rads, Limbs, SPECIAL-derived, Encumbrance)

| Field | Value |
|---|---|
| Tier | T0 |
| Target level | L4 (SkyMP L3; plus cropping of every regenerating AV (I8), restore on load (I7), per-actor idx-scoped messages (I13)) |
| SkyMP analogue | `ChangeValues` (16) Health/Magicka/Stamina percentages, `ActorValues.h`, `CropRegeneration.cpp`, `OnChangeValues` (`ActionListener.cpp:765-825`). SkyMP level L3 (reference/skymp-sync-inventory.md §2 "Actor values & regen") |
| Milestone | M7 |
| Workstreams | SRV, NET, CLI, PLAT, GM |
| Depends on | SRV-010 (AV store), SRV-011 (snapshot/delta encoding), SRV-020 (effects, for temporary modifiers), REF-005 (AV catalogue seam), ESPM-005/ESPM-006/ESPM-016 (AVIF, NPC_/RACE, lookup by EDID), F01-T05 (movement samples for sprint drain), F04 (item weights), F05 (worn armor) |
| References | reference/fo4-systems-combat-character.md §1.2 (AV form ids), §1.7 (`Fo4ActorValueStore`), §6 (AP), §7 (Health), §8 (Rads), §15 (carry weight); reference/skymp-sync-inventory.md §1.9 (crop pattern), §1.15 #17, §3.2 I7/I8/I13; reference/fo4-data-formats.md §4.1 (AVIF); reference/papyrus-api-map.md §5.2 (AVs are forms); reference/skyrim-coupling-index.md §1.5 |

## 1. Summary
Every actor (player or NPC) has one server-side actor-value store keyed by AVIF form id. It covers Health, Action Points, Rads, the seven limb conditions, SPECIAL, resistances, carry weight and every other FO4 AV the server rules use. The server computes base and derived values, applies regeneration and drains, and is the only party that can raise a value beyond what regeneration allows. Clients report what their engine observed; the server crops, accepts or ignores each report per AV policy and always answers a cropped report with a correction. The owner sees all its synced AVs; neighbours see only a public subset (health %, crippled limbs) so health bars and limping are correct for everyone. Values persist and are restored exactly on reconnect and server restart.

## 2. Vanilla Fallout 4 behaviour
- AVs are `ActorValueInfo` forms (AVIF). Hardcoded AVs live at `0x2BC…0x39C` (engine index i → form `0x2BC + i`); others are ESM-defined (e.g. `ArmorPenetration` 0x97341) (fo4-systems §1.2; fo4-data-formats §4.1). Ids are **resolved by EDID** at load (ESPM-016); `RadResistIngestion` 0x2E9 vs 0x2E5 is ambiguous in sources.
- AVIF `AVFL` flags give clamping and semantics: Min1, Max10, Max100, DamageIsPositive, God-mode immune, Hardcoded; `NAM1` type (SPECIAL, Resistance, Condition, Charge, Resource…); `NAM0` default value (fo4-data-formats §4.1).
- Engine storage per actor: `baseValues[(av, float)]` and `modifiers[(av, {permanent, temporary, damage})]`; current = base + permanent + temporary + damage, damage ≤ 0 [src: clf4 A/ActorValueStorage.h; ACTOR_VALUE_MODIFIER.h] (fo4-systems §1.2, current formula [inference]).
- Derived values [web, fo4-systems §6.1/§7.1/§15.1]:
  - Max HP (player) = `floor(80 + 5·END + (Level − 1)·(2.5 + END/2))`, retroactive on END change. NPC max HP from NPC_ data / auto-calc (**VERIFY** with ESPM-006 and D-real).
  - Max AP = `fAVDActionPointsBase (60) + fAVDActionPointsMult (10) × AGI`; regen 6 % of max per second (`ActionPointsRate` 0x2D8 × `ActionPointsRateMult` 0x359).
  - Carry weight = `fAVDCarryWeightBase (200) + fAVDCarryWeightMult (10) × STR`; Survival base 75; perk entry `GetMaxCarryWeight` 0xA; OMODs (pocketed, PA calibrated legs +50).
  - Rads 0–1000 (`Rads` 0x2E1): −1 % max HP per 10 rads; 1000 = death (`RadHealthMax` 0x2EE, entries `SetRadsToHealthMult` 0x65, `ModRadsForRadHealthMax` 0x6A).
  - Sprint drain `(1.05 − 0.05·END) × 12` AP/s (entry `ModSprintAPDrainRate` 0x60).
- Limb condition AVs 0–100: `PerceptionCondition` 0x36C (head), `EnduranceCondition` 0x36D (torso), `LeftAttackCondition`/`RightAttackCondition` 0x36E/0x36F, `LeftMobilityCondition`/`RightMobilityCondition` 0x370/0x371, `BrainCondition` 0x372; creature `AttackConditionAlt1/2/3`. Regen `ConditionRate` 0x2D9 × `ConditionRateMult` 0x35A. Crippled at 0 → Papyrus `OnCripple(akAV, abCrippled)` [src: f4se Actor.psc:895].
- Health regen: none for the player by default; perks/effects only (`HealRate` 0x2D7, `HealRateMult` 0x358, `CombatHealthRegenMult` 0x343). NPC regen behaviour **VERIFY** (fo4-systems §7.1).
- Papyrus: `GetValue/SetValue/ModValue/DamageValue/RestoreValue/GetBaseValue/GetValuePercentage(ActorValue)` on ObjectReference [src: f4se ObjectReference.psc:287,372,510,513,656,777,934] (papyrus-api-map §5.2). `Actor.IsOverEncumbered` [src: f4se Actor.psc:406].
- Single-player assumptions: regen and spending are local, so sprinting at 0 AP is a speed hack; encumbrance is client-only; pausing menus freeze local regen but not the world (fo4-systems §1.6, §6.4).

## 3. SkyMP baseline
- `ActorValues` struct holds H/M/S percentages, values, rates and rate mults [src: server_guest_lib/ActorValues.h]. The client sends percentages in `ChangeValues` (16); the server crops increases by `rate × rateMult × dt` (`CropRegeneration.cpp:21-66`) for Health and Magicka only — **stamina is not cropped (I8)** [src: ActionListener.cpp:765-825].
- `OnChangeValues` ignores `idx`, so hosted-NPC reports overwrite the player's AVs (**I13**) [src: ActionListener.cpp:768; sendInputsService.ts:148].
- `ApplyChangeForm` replaces saved AVs with base values when ESM data is attached (**I7**) [src: MpActor.cpp:603-608].
- A 5 s "potion window" skips restoration checks (`ShouldSkipRestoration`) [src: MpActor.cpp:85-102].
- Neighbours see health only through the owner-reported `UpdateMovement.healthPercentage`.
- Reuse: crop pattern, correction-by-ChangeValues, `restorationTimePoints`, `NetSendChangeValues`, `GetActorToSendTo` routing. Replace: the H/M/S struct (FO4 GameProfile uses the AVIF-keyed store), the potion window (replaced by effect-aware upper bounds).

## 4. Design

### 4.1 Authority model
Class A for every AV. The owner (or host, for NPCs) is a **sensor**: it may report decreases it observed (fall damage, unmodelled hazards) and, for regenerating AVs, increases that the server bounds. Per-AV client policy (GameProfile table `Fo4AvPolicy`, data-driven, S22):

| Policy | AVs | Client increase | Client decrease |
|---|---|---|---|
| `regen` | Health, ActionPoints, limb conditions | cropped to the server upper bound `ub` | accepted |
| `increaseOnly` | Rads | accepted (≤ 1000) | ignored + correction |
| `serverOnly` | SPECIAL, resistances, CarryWeight, max values, CritChance, PowerArmorBattery, Fatigue, all others | ignored + correction | ignored + correction |

Upper bound: `ub = last + Σregen(rate × rateMult × dt) + Σeffect restores (SRV-020) − Σserver-observed costs`, clamped to `[0, max]`. New value = `min(report, ub)` (regen) — no double counting, because reports are absolute values, not deltas.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Base values | `map<avFormId, float>` | `Fo4ActorValueStore::base` | only overrides (player SPECIAL, gamemode `SetValue` on base) in `fo4Avs[edid].base` | RACE / NPC_ PRPS / AVIF `NAM0` |
| Permanent modifiers | float per AV | `::perm` | `fo4Avs[edid].perm` | 0 |
| Temporary modifiers | float per AV | `::temp` | no (re-derived from active effects and equipment, SRV-020/F05) | 0 |
| Damage modifiers (current = base + perm + temp + damage) | float ≤ 0 per AV (Rads: ≥ 0) | `::damage` | `fo4Avs[edid].dmg` | 0 |
| Last regen timestamp per AV | steady_clock | `MpActor::restorationTimePoints` (re-keyed by AV id) | no | now |
| Cost ledger (sprint, VATS, power attack, jetpack) | float per window | `Fo4ActorValueStore::pendingCosts` | no | 0 |
| `isOverEncumbered` | bool | derived (weight vs CarryWeight) | no (derived) | false |
| Last server AV seq sent to owner | u16 | `MpActor::avSeq` | no | 0 |

`fo4Avs` is keyed by **AVIF EDID** (FormDesc for ESM-defined AVs) so load-order changes are safe (01-sync-standard §8.3). Each field is parsed defensively; an unknown EDID is logged and skipped, not the whole form (REF-020).

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `ChangeValuesAv` (72) | C→S | `idx` (own actor, `0x14` alias, or hosted NPC), `seq` u16, `ackSeq` u16 (last server seq applied), `kind`=report, `entries[] {av u32, mode u8 (0 = % of max, 1 = absolute), value f32}` | R ordered | ≤ 2 Hz per actor, on change ≥ 1 % (setting `actorValues.reportThresholdPct`); immediate when a limb reaches 0 | registry 72 |
| `ChangeValuesAv` (72) | S→C | `idx`, `seq` u16, `kind` (1 delta, 2 snapshot, 3 correction), `entries[]` (same layout; `mode` 2 = max value) | R ordered | coalesced per tick per actor (deferred channel 3, merge-by-AV) | registry 72 |
| `CreateActorFo4` (64) | S→C | AV map: owner subset for the owner, public subset for neighbours | R | on subscribe | registry 64 (F00) |
| `UpdateProperty` (7) | S→C | `isOverEncumbered` (owner only) | R | on change | reused |

Subsets: **owner** = Health, AP, Rads, RadHealthMax, all limb conditions, SPECIAL, DamageResist/EnergyResist/RadResist*/PoisonResist/Fire/Frost/ElectricResist (effective incl. armor), CarryWeight, PowerArmorBattery, max values. **Public** = Health (% of effective max, quantized to 1/255), limb conditions (absolute, so remotes limp/flail). Power-armor battery and piece AVs are owner/host only (F17); remotes see pieces through `PowerArmorState` (88). Entry ≈ 9 B; typical delta ≤ 32 B.

The F01 relay of `UpdateMovementFo4` overwrites `healthPercentage` with the server value, so nobody displays an owner-faked health bar.

### 4.4 Client capture (owner side)
- `actorValuesService.ts` polls, for the player and each hosted NPC, the synced AV list (from the session AV table, NET-010) via `GetValue`/`GetValuePercentage`.
- It sends a report only on change ≥ threshold, at ≤ 2 Hz, and only when `ackSeq` equals the last server seq received for that actor (stale reports are never sent).
- Never reports `serverOnly` AVs. Skips reports while a loading screen is up.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Owner:** on delta/correction, set the engine value with `SetValue` (base AVs) or `DamageValue`/`RestoreValue` (current), so that current = server value. The local player stays in deferred-kill / HP ≥ 1 clamp (F12), so a local Health of 0 never kills by itself.
- **Remote actors (ghosts):** apply the public subset: Health (so vanilla enemy health bars read it) and limb conditions (crippled animation). Ghosts keep `AttackDamageMult` 0 (F09).
- **Stream-in:** `CreateActorFo4` AV map is applied before movement (F02 spawn order step 2).
- **Respawn:** `DeathStateContainerFo4` carries the AV map (F12).
- **Reconnect:** the owner gets a `kind=2` snapshot after `CreateActorFo4 isMe`.

### 4.6 Validation & anti-cheat
1. Ownership: `idx` is the sender's actor or an NPC it hosts (`worldState.hosters`) (S6, I13). Otherwise drop + `HostStop`.
2. Stale: `ackSeq` older than `MpActor::avSeq` → drop silently (a correction is already in flight).
3. Per-AV policy (§4.1); any crop/ignore → `ChangeValuesAv kind=3` to the sender with the server values (S12), rate-limited to 4/s.
4. **Regeneration crop (I8)** for every `regen` AV with its own rate and mult AVs: Health (HealRate × HealRateMult; × CombatHealthRegenMult in combat), AP (ActionPointsRate × ActionPointsRateMult; × 0.5 with Lethargy etc. via effects), limbs (ConditionRate × ConditionRateMult; 0 in survival per F20).
5. **AP drains** the server applies itself: sprint seconds from accepted F01 samples with the `sprinting` flag; VATS (F18), power attacks (F10), jetpack (F17), via `Fo4ActorValueStore::Spend(av, amount, reason)`. A spend that would go below 0 is refused and the caller rejects the action. `CanSprint()` (AP > 0, not overencumbered unless `SetRunWhileOverEncumbered` 0x83, not unpowered in PA per F17) feeds the F01 speed model; `Spend` refuses every AP action while F17 reports the wearer unpowered.
6. Clamp every write to AVIF flags (Max10/Max100/Min1) and to `[0, max]`. Health max = `MaxHP × (1 − rads × radsToHealthMult)`; on change current Health is clamped.
7. Metrics: `av_correction_total{av}`, `av_report_dropped_total{reason}` (SRV-050).

### 4.7 Audience / visibility
Owner/host: full owner subset (`GetActorToSendTo`, S14). Grid neighbours: public subset on change (S7/S8). Nothing world-wide.

### 4.8 NPC parity
Each NPC has its own store, built from NPC_/RACE (ESPM-006) and template chain. The host reports with `idx` = NPC. **Hosted NPCs use a stricter policy than the owner's own actor** (review finding C1): a host may only *decrease* NPC Health/limb conditions or *increase* NPC Rads when the server can attribute a cause it knows about (a fall in the F01 movement history: `velZ` ≥ `actorValues.fallVelocityThreshold` within the last second; an active environmental radiation source from F20), bounded per second by `actorValues.npcClientDecreaseMaxPerSec`, and **never across 0 HP or 1000 rads** (clamped to ≥ 1 HP / ≤ 999 rads with a correction). Only F11 damage and F20 effects may kill or irradiate an NPC past those bounds. Everything else for NPCs is `serverOnly`. Host migration does not lose AVs (they are server state). NPCs never consume AP for sprint unless `actorValues.npcApRules` is on (default off: vanilla NPCs don't sprint-drain [inference]).

### 4.9 Gamemode API & server Papyrus
- `mp.get(id, 'actorValues')` → `{ [edid]: {base, current, max, pct} }`; `mp.set(id, 'actorValues', {edid: {base?|current?}})` (validated, triggers derived recompute and a delta). Existing `healthRespawnPercentage` properties keep working.
- Events: `onActorValueChange(actorId, edid, old, new, source)` (observe only; throttled), `onCripple(actorId, limbEdid, crippled)` (observe), `onOverEncumbered(actorId, isOver)`.
- Server Papyrus (PVM-013): `GetValue/SetValue/ModValue/DamageValue/RestoreValue/GetBaseValue/GetValuePercentage(ActorValue)` map onto the store; events `OnCripple`, `OnPartialCripple` via `MpForm::SendPapyrusEvent` (S16).

### 4.10 Edge cases & failure modes
- Pip-Boy pause: the world keeps running; server regen continues by elapsed time; healing items still act through timed effects (F20).
- END/level change: max HP recomputed; current scaled to keep the same damage, not the same percentage (vanilla retroactive rule) [inference, VERIFY in G-self].
- Rads ≥ 1000 → F12 `Kill(reason=radiation)`.
- Server restart: `fo4Avs` loaded; temporary modifiers re-derived after effects (F20) and equipment (F05) are restored, then derived values recomputed. **No reset to base (I7).**
- Hot reload of the gamemode: store untouched.
- Power armor enter/exit swaps STR base to 11 and carry rules (F17 calls `SetBaseOverride`).

### 4.11 Performance budget
Store ≈ 40 AVs × 16 B per actor. Report ≤ 2 Hz × ≤ 40 B. Server handling ≤ 3 µs per report. Neighbour public deltas ≤ 1 per actor per tick, ≤ 16 B.

## 5. Engine / platform work required
- None new beyond PLAT-031/PLAT-032 (Papyrus calls with `ActorValue` form args) and PLAT-035 typings (AV EDID enum).
- `getActorValuesBulk(ref, avIds[])` fast path (one native call per update instead of ~40 Papyrus calls) — add to `fallout4-platform` AV API (task F08-T10).

## 6. Tests
- `L-unit` (`unit/Fo4ActorValuesTest.cpp`, `[F08]`):
  - derived formulas: max HP table (END 1/5/10 × level 1/10/50), max AP from AGI, carry weight from STR, survival base 75;
  - rads 500 → effective max HP halved, current clamped; rads 1000 → kill request;
  - crop: Health/AP/limb increases cropped at their rates (6 %/s AP); decrease accepted; Rads decrease ignored + correction; SPECIAL report ignored + correction;
  - sprint drain over 1 s of samples per END; `Spend` refuses below 0;
  - stale `ackSeq` dropped; wrong owner dropped with `HostStop`; hosted NPC report changes only the NPC (I13);
  - late joiner: `CreateActorFo4` owner subset vs neighbour public subset;
  - persistence round trip of `fo4Avs` (base override, perm, dmg) and restore without base reset (I7); unknown EDID skipped;
  - gamemode `mp.set` + `onCripple` fired; Papyrus `OnCripple` observed as `onPapyrusEvent:OnCripple`.
- `L-int`: two bots; bot A sprints until AP 0, server refuses further sprint (F01 `Teleport2`), bot B sees A's health bar after damage.
- `L-ts`: report throttling and `ackSeq` gating in `actorValuesService`.
- `G-self`: read/write every synced AV on the player and a spawned NPC; compare engine max HP/AP with the server formula for 5 SPECIAL combos.
- `G-manual`: two players; one takes fall damage and cripples a leg, the other sees the limp and health bar; reconnect keeps values.

## 7. Tasks
- [ ] **F08-T01** `Fo4ActorValueStore` (base/perm/temp/damage, AVIF flag clamping, EDID-resolved catalogue via REF-005) behind the FO4 GameProfile — L — Depends: SRV-010, REF-005, ESPM-016 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/fo4/Fo4ActorValueStore.{h,cpp}, game_profile/fallout4/*; unit/Fo4ActorValuesTest.cpp
  - Accept: get/set/mod/damage/restore semantics match §2; all clamping cases tested.
- [ ] **F08-T02** Derived values: max HP, max AP, carry weight, RadHealthMax, effective resistances (AV + armor from F05/SRV-022), recompute on SPECIAL/level/perk/effect/equipment change — M — Depends: F08-T01, SRV-022 — Verify: L-unit
  - Accept: formula tables in §6 pass; GMSTs read by EDID, not hard-coded.
- [ ] **F08-T03** `ChangeValuesAv` message (C++ struct + TS mirror), `kind`/`mode` enums, owner and public subset encoders (SRV-011) — S — Depends: NET-002, SRV-011 — Verify: L-unit — Files: falloutmp-server/cpp/messages/ChangeValuesAvMessage.h, Messages.h; falloutmp-client/src/services/messages/changeValuesAvMessage.ts
  - Accept: binary + JSON round trip; typical delta ≤ 32 B.
- [ ] **F08-T04** `CreateActorFo4` AV map (owner vs public) and deferred channel 3 (merge-by-AV) — S — Depends: F08-T03, NET-003 — Verify: L-unit
  - Accept: late-joiner tests for both audiences.
- [ ] **F08-T05** `OnChangeValuesAv` handler: per-AV policy table, `ackSeq` gating, corrections (rate-limited), metrics — M — Depends: F08-T03, NET-007 — Verify: L-unit — Files: ActionListener.cpp (FO4 handler), fo4/Fo4AvPolicy.{h,cpp}
  - Accept: every reject path asserts a `kind=3` correction.
- [ ] **F08-T06** Restore saved AVs on load (I7): `fo4Avs` ChangeForm field, defensive parsing, recompute order (base → effects → equipment → derived) in `ApplyChangeForm` — M — Depends: F08-T02, REF-020 — Verify: L-unit — Files: MpChangeForms.{h,cpp}, MpActor.cpp
  - Accept: save → restart → load restores current Health/AP/Rads/limbs exactly; Skyrim tests unchanged.
- [ ] **F08-T07** Crop every regenerating AV (I8): generalised `CropRegeneration(av, rate, mult)` keyed by AV id, effect-aware upper bound replacing the potion window; Skyrim stamina crop behind the profile — M — Depends: F08-T05, SRV-020 — Verify: L-unit — Files: CropRegeneration.{h,cpp}, MpActor.cpp
  - Accept: Health/AP/limb crop tests; Skyrim `[CropRegeneration]` still green (stamina fix gated per REF-015).
- [ ] **F08-T08** Per-actor idx-scoped AVs (I13): handler uses `idx` with host validation; client sends per hosted NPC — S — Depends: F08-T05, F13 (hosting) — Verify: L-unit
  - Accept: a host report for NPC X changes only X; a non-host report is dropped with `HostStop`.
- [ ] **F08-T09** AP spend API and drains: sprint (from F01 accepted samples), hooks for F10 power attack, F17 jetpack, F18 VATS; `CanSprint()` for the F01 speed model — M — Depends: F08-T01, F01-T05 — Verify: L-unit
  - Accept: sprint drain per END; sprint at 0 AP flagged by the F01 speed model (F01-T07 consumes `CanSprint()`).
- [ ] **F08-T10** Client `actorValuesService.ts` + `sync/actorValues.ts` (capture, apply owner/ghost subsets) and platform `getActorValuesBulk` — M — Depends: F08-T03, CLI-001, PLAT-031 — Verify: L-ts, W-ci, G-self — Files: falloutmp-client/src/services/services/actorValuesService.ts, falloutmp-client/src/sync/actorValues.ts, fallout4-platform/src/.../ActorValueApi.cpp
  - Accept: the self-test reads/writes all synced AVs; L-ts throttling suite green.
- [ ] **F08-T11** Encumbrance: inventory weight (F04, survival ammo weight via F20 rule), capacity, `isOverEncumbered` property, hand-off to F01 speed model and F26 fast-travel check — S — Depends: F08-T02, F04 — Verify: L-unit
  - Accept: overloaded flag flips at capacity; run samples rejected while overloaded.
- [ ] **F08-T12** Server-stamped health % in the F01 relay; public subset to neighbours — S — Depends: F08-T04, F01-T05 — Verify: L-unit
  - Accept: a faked `healthPercentage` from the owner never reaches neighbours.
- [ ] **F08-T13** Gamemode `actorValues` property, `onActorValueChange`/`onCripple`/`onOverEncumbered`, Papyrus AV natives and `OnCripple`, docs (DOCS-003) — M — Depends: F08-T05, PVM-013 — Verify: L-unit, L-int
- [ ] **F08-T15** Hosted-NPC AV policy (C1): cause-bounded host decreases/increases for NPC idx, per-second caps, hard clamp at 1 HP / 999 rads, corrections; metrics `av_npc_host_reject_total` — M — Depends: F08-T08, F01-T05, F20-T06 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/ActorValuePolicy.{h,cpp}, unit/ActorValuePolicyTest.cpp
  - Accept: tests "host reports Health 0 for hosted NPC → clamped to 1 + ChangeValuesAv correction", "host reports Rads 1000 → clamped 999", "fall with velZ history → decrease accepted up to cap", "no cause → rejected".
- [ ] **F08-T14** `G-self` AV parity check and `G-manual` script — S — Depends: F08-T10 — Verify: G-self, G-manual — Files: docs/falloutmp/test-scripts/F08-actor-values.md

## 8. Open questions & risks
- AP cost rules (sprint, hold-breath, VATS, jetpack, melee power attack) are owned here (F08-T09); F02/F17/F18 only report the action.
- NPC max HP/AP source (NPC_ auto-calc vs stored values) and NPC regen rules: D-real + G-self (R4/R5-style research tasks in fo4-systems §18).
- `FatigueAPMax` and related AV ids are unknown (probably 0x350–0x354) — resolve by EDID.
- Hold-breath AP drain is not visible to the server (no movement flag); the client may under-report it. Accepted deviation at L4, revisit if exploited.
- Whether the engine lets `SetValue` push HUD max values for the local player without side effects (PA, rads bar) needs G-self.
