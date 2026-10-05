# F20 — Consumables, Magic Effects, Addiction, Radiation & Survival

| Field | Value |
|---|---|
| Tier | T1 (survival-mode needs: T2) |
| Target level | L4 (survival module: L3) |
| SkyMP analogue | Eating/potions: `OnEquip` (11) → `MpActor::OnEquip`/`EatItem` → `ApplyMagicEffect`, `ActiveMagicEffectsMap`, `onEatItem` (`MpActor.cpp:474-553,1061,1805`). SkyMP level L3; spell effects L1–L2 (TODO, I10) |
| Milestone | M7 (UseItem, effect system core, stimpaks, chems, rads), M10 (addiction polish, legendary/enchant effects, survival module) |
| Workstreams | SRV, CLI, PLAT, NET, GM |
| Depends on | F04 (inventory atomics), F08 (AV store, Rads → RadHealthMax, limb AVs), F11 (damage-type effects), F12 (downed/revive), F25 (server clock), SRV-010, SRV-020, SRV-021, ESPM-009, ESPM-011 |
| References | reference/fo4-systems-combat-character.md §8, §9, §10, §1.3 (MGEF archetypes, ALCH), §1.6 items 1–2, §19; reference/fo4-data-formats.md §4.9, §4.10; reference/skymp-sync-inventory.md §2 (Eating/potions row), §1.12 (`onEatItem`), Appendix C item 15 |

## 1. Summary
Players eat, drink and take chems and stimpaks from the Pip-Boy or favourites. They can heal a specific crippled limb or use a stimpak on a downed teammate or companion. The server removes exactly one item and applies the effects through a server-side **magic-effect system**: timed buffs, heal over time, rad gain/removal, DR boosts, and the jet slow-time handled by rule. It rolls addiction, applies withdrawal when the high wears off, and tracks radiation from food, emitters, hazards, water and radstorms with resistance. The same effect system carries legendary armor and weapon effects, perk abilities and creature poisons. A server can turn on survival-mode needs (hunger, thirst, sleep, fatigue, adrenaline, diseases). These run on the server clock and show on the owner's HUD.

## 2. Vanilla Fallout 4 behaviour
- **ALCH** `ENIT`: value, flags (Food 0x2, Medicine 0x10000, Poison 0x20000), addiction SPEL, addiction chance, consume sound; effects `EFID`+`EFIT{magnitude, area, duration}`+CTDA (fo4-data-formats §4.9, §4.10).
- **MGEF** `DATA`: flags (Hostile, Recover, Detrimental, No Duration, Hide in UI, No Death Dispel…), archetype @0x40, primary AVIF @0x44 (a formid in FO4, needs `ToGlobalId`), second AV, perk to apply, equip ability.
- **Archetypes** (fo4-data-formats §4.10): Value Modifier 0, Script 1, Dual Value Modifier 5, Invisibility 11, Paralysis 21, Cure Addiction 28, Stimpak 31, Accumulate Magnitude 32, Stagger 33, Peak Value Modifier 34, Cloak 35, Slow Time 37, Enhance Weapon 39, Spawn Hazard 40, Damage 45, Immunity 46, Jetpack 48, Chameleon 49.
- **SPEL** type 10 = Addiction. **ENCH** = legendary and armor/weapon effects.
- **Addiction** (combat-character §9.1):
  - chances, e.g. Jet 25 % (AGI −1), Psycho 25–35 %, Mentats 10 %, alcohol 15–25 %;
  - ≈3 repeated uses in a short time;
  - entry points `ModifyAddictionChance` 0xB, `ModifyAddictionDuration` 0xC, `ModifyPositiveChemDuration` 0xD, AV `ChemDurationMod` 0xBA447;
  - cures: Addictol, doctors, Refreshing Beverage. Chem Resistant halves the chance.
- **Radiation** (combat-character §8):
  - `Rads` 0–1000; each 10 rads remove 1 % max HP (`RadHealthMax`);
  - resistance curve: exposure equal to RR halves the intake (10 rads/s, RR 10 → 5 rads/s);
  - `RadResistIngestion` 0x2E9 / `RadResistExposure` 0x2EA;
  - sources: HAZD, refs with `ExtraRadiation`, water, food, radstorms, weapons;
  - RadAway −300 over time (Medic up to 1000); Rad-X +100 RR per dose (stackable).
  - Papyrus `OnRadiationDamage(akTarget, abIngested)` (register-based).
- **Survival mode** (combat-character §10):
  - hunger stages at ≈6/12/24/36/64 h, thirst at ≈4/9/18/30/45 h, sleep 14 h;
  - fatigue reduces max AP;
  - Adrenaline +5 % damage per 5 kills up to 50 %, sleep removes ranks;
  - diseases with a risk pool (food +1…+20 %, chems +7 %, infected hits +5 %, swimming/rain +3 %, −1 %/h decay, rolls from 25 %, 24 h grace);
  - healing slowed, no limb auto-heal, ammo weight, base carry weight 75.
  - Implemented by quest `HC_Manager` scripts (names to verify).
- **Single-player assumptions that break:**
  - consumption is local and immediate;
  - **pausing menus**: the Pip-Boy freezes the local game but not the server world (combat-character §1.6 item 2);
  - **Slow Time** (Jet, legendary) is global time scaling (item 1);
  - rad exposure is computed by the client from local proximity (cheatable);
  - survival is quest scripts tied to the one player.

## 3. SkyMP baseline
- **Capture:** equip of ALCH/INGR → `OnEquip{baseId}`, sent **unreliable** (I3).
- **Server:** `MpActor::OnEquip` checks ownership → `EatItemEvent` (`onEatItem`) → `ApplyMagicEffect` per effect (AV restore/modify; 5 s restoration window), removes 1.
  - Bug: **a blocked event still removes the item** (Appendix C item 15).
  - INGR effects are commented out.
  - `ActiveMagicEffectsMap` is keyed by actor value: one effect per AV, no stacking, no archetype logic [src: ActiveMagicEffectsMap.h:1-60].
- **Persistence:** `activeMagicEffects` in the ChangeForm.
- **Reuse:** the ownership check, the event, the timer infrastructure (`WorldState::SetTimer`), the JSON persistence pattern.
- **Replace:**
  - the message → `UseItem` (91), reliable, with targets;
  - the effect store → SRV-020's list-based `Fo4EffectSystem` (stacking, archetypes, conditions, I10);
  - add `EffectsUpdate` (92), addiction, radiation and survival modules.

## 4. Design

### 4.1 Authority model
Class A:
- item consumption, effect application, magnitudes/durations, AV changes, addiction, rads, survival needs, diseases.
- **Exception:** Rads *increases* the server cannot model may be reported by the client through `ChangeValuesAv` (increase-only, rate-limited, F08). Decreases from clients are always rejected.
- Visual effect presentation (shaders, imagespaces) is class D, seeded by server state.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Active effects | list `ActiveEffect {effectId u32, sourceForm, mgef, archetype, av, av2, magnitude, remainingMs, tickMs, casterId, flags, conditionsOk}` | `MpActor` via SRV-020 `Fo4EffectSystem` | `fo4Effects` (remaining durations; replaces `activeMagicEffects` for FO4) | empty |
| Constant effects (equipped ENCH, perk abilities, survival stage effects) | list, derived | effect system, source-tagged | no (re-derived from equipment/perks/needs on load) | — |
| Addictions | map addictionSpell → {recentUses[≤8 ts], addicted, since} | `addictions` (new) | yes | empty |
| Withdrawal flags | derived (addicted && chem effect not active) | effect system | no | — |
| Survival needs | {hungerH, thirstH, sleepH, fatigue, adrenalineKills, diseaseRisk, diseases[], lastTickGameTime} | `survival` (new) | yes (when module on) | zeros |
| Use cooldown | last use ms per actor | transient | no | — |
| Radiation zones | emitters (REFR radiation data), HAZD refs, water heights | `RadiationService` (WorldState, from ESM + F10 hazards) | no | ESM |

Every effect is ticked on the **server clock** (F25-T01). Durations are simulation seconds, which do not slow when a client is paused.

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `UseItem` (91) | C→S | `nonce`, `actorIdx` (own or hosted NPC), `item` (F04 `ItemKey`: baseId + extras), `target` (self, limb, other), `limbAv?` (AVIF of the limb condition), `targetIdx?` | R | user action; ≤ 4/s | new (registry) |
| `EffectsUpdate` (92) | S→C | `idx`, `seq`, `full`, `effects[] {effectId, sourceForm, mgef, magnitude, remainingMs, flags (visual, detrimental, hideInUI)}`, `removed[]`, `addictions[] {spell, addicted}`, `needs? {hungerStage, thirstStage, sleepStage, fatigue, adrenalineRank, diseases[]}`, `nonce?`, `error?` | R | on change, coalesced per tick; full on subscribe/login | new (registry) |
| `SetInventoryFo4` (68) | S→C | after consumption, or as a correction | R | on change | reused (F04) |
| `ChangeValuesAv` (72) | both | S→C: resulting AVs. C→S: Rads increase only (client-only sources) | R | F08 | reused |
| `CreateActorFo4` (64) | S→C | owner: full effects; others: visual subset | R | subscribe | reused |

### 4.4 Client capture (owner side)
- **Block local consumption:** hook equip of ALCH on the local player (PLAT-081 equip API filter; Pip-Boy, favourites hotkeys, quick stimpak). Cancel the engine consumption and send `UseItem{self}`.
- **Pip-Boy limb page "use stimpak on <limb>":** the hook reads the targeted limb (engine passes it to the stimpak use [inference: verify the hook point]) → `target: limb, limbAv`.
- **Use on others:** crosshair target (downed player/companion, F12) plus the activate-with-stimpak prompt, or a front widget → `target: other, targetIdx`.
- **Rads from unmodelled sources:** the client sums engine-reported rad damage (`BGSRadiationDamageEvent`, `CurrentRadsDisplayMagnitude`) that the server did not attribute, and sends `ChangeValuesAv{Rads: +Δ}` at ≤ 2/s.
- **Sleep for survival:** reported through F07 furniture occupancy of a bed (no new message). Vanilla `SleepWaitMenu` is disabled (CLI-021, F25).

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Owner:**
  - AV results arrive by `ChangeValuesAv` (F08 mirror; the engine never runs the consumable's effects locally);
  - `EffectsUpdate` drives the HUD/Pip-Boy effects list (F28 data feed) and cosmetic local visuals: imagespace modifiers, `EffectShader`/`VisualEffect` via SpSnippet, consume sounds;
  - survival needs show as HUD warnings (`Game.ShowFatigueWarningOnHUD`-style natives, F28).
- **Remote:** only effects flagged `visual` (Invisibility/Chameleon refraction (F29), hit shaders, Stealth Boy, legendary aura) are sent to neighbours and played on the puppet. Consume animations come from F02.
- **Correction:**
  - a rejected `UseItem` → `SetInventoryFo4` (restores the count) + `EffectsUpdate{nonce, error, full}`;
  - a client-side rad decrease → `ChangeValuesAv` with the server value.
- **Stream-in/late join:** the visual subset is in the snapshot.
- **Reconnect:** the full list is in the `isMe` snapshot. Timers resume from `remainingMs` (S10).
- **Death** (F12): remove effects without `No Death Dispel`; addictions and diseases persist (vanilla).
- **Respawn:** the gamemode setting `respawn.clearRads` (combat-character §19).

### 4.6 Validation & anti-cheat
**`UseItem`** (atomic, S13):
1. ownership of `actorIdx` (own, or a hosted NPC for NPC self-use, S6);
2. the item exists in the actor's inventory (count ≥ 1) and is ALCH (or a configured usable MISC);
3. target rules:
   - `self`: the actor is alive and not downed;
   - `limb`: the item has a Stimpak (31) or limb-restore effect, and `limbAv` is a limb condition AV of the actor's race (BPTD);
   - `other`: the target is within `healing.otherReach` (default 200 u, rewound position), alive or downed, and `healing.allowOthers` (default true; PvP rules via the gamemode); medicine only;
4. cooldown `healing.aidCooldownInCombatMs` (default 0) when in combat;
5. gamemode `onUseItem(actor, item, target)` (blockable; `onEatItem` kept as an alias for food).
   - **Blocked → no removal** (fixes SkyMP Appendix C 15) + correction.

On accept: remove 1 on a copy, commit, then apply the effects:
- each `EFID` → SRV-020 `Apply(effect, caster, target, source)` with magnitude/duration modified by SRV-021 (Medic for stimpak/RadAway; Chemist via `ModifyPositiveChemDuration`; `ChemDurationMod` AV);
- conditions (CTDA) are evaluated at apply and every 1 s for conditional constant effects.
- Healing others fires `onHealOther` and feeds F21 affinity events and F12 revive.

**Effect system** (SRV-020, archetype handlers):

| Archetype | Server behaviour |
|---|---|
| Value Modifier 0, Dual 5, Peak Value Modifier 34 | Temporary AV modifier for the duration; with `Recover`, removed on expiry |
| Accumulate Magnitude 32 | Ramps the magnitude per tick |
| Stimpak 31 | Restore Health (and the target limb) over the duration |
| Damage 45 | Damage-over-time ticks through F11 (resistances apply) |
| Cure Addiction 28 | Clears addictions matching the effect's keywords |
| Invisibility 11, Chameleon 49 | Set `Invisibility` AV (F29 rules; Chameleon only while sneaking and still) |
| Paralysis 21, Stagger 33 | Hands off to F11/F12 (paralysis = forced ragdoll state via F02) |
| Spawn Hazard 40 | F10 spawns a server hazard ref |
| Jetpack 48 | F17 |
| Cloak 35 | Periodic aura: applies the associated spell to actors within radius (grid query) every 1 s |
| Enhance Weapon 39, equip abilities | Constant while equipped (F05/F16 legendary ENCH); on-hit parts in F11 |
| Slow Time 37 | Rule `effects.slowTime = disable` (default) or `convert`: aim stability + damage bonus for the duration; never global time scaling |
| Script 1 | Runs the MGEF's Papyrus script in the server VM if it is on the ADR-011 allow-list; otherwise no-op (logged once) |
| Others (Immunity 46, Rally 38, …) | Implemented as data where trivial; otherwise no-op with a metric |

**Addiction:**
- on each use of an ALCH with an addiction SPEL, append the timestamp;
- if `addiction.requireOverlap` (default true: a previous dose of the same chem is still active, approximating vanilla's ≈3 uses), roll `chance × PerkEngine(ModifyAddictionChance)` with `CombatRng`;
- on success set `addicted`, fire `onAddiction` (blockable);
- withdrawal = the addiction SPEL's effects applied while addicted and no effect from that chem is active (re-evaluated on effect expiry);
- cures: archetype 28, or gamemode `mp.cureAddiction` (doctor services).

**Radiation intake** (`RadiationService`, server tick 1 s per actor in a loaded cell):
- emitters (ESM REFR radiation data, radius and rate), placed hazards (F10), water (actor below the cell water height, F01 swim flag), radstorm (F25 weather);
- each exposure source: `taken = rate × rate/(rate + RR_exposure)` [inference: a curve fitted to the vanilla data points of combat-character §8.1; calibrate with G-self];
- ingestion uses `RadResistIngestion`;
- results feed `Rads` (F08 recomputes `RadHealthMax`);
- `onRadiationDamage(actor, amount, ingested)` may modify it;
- client-reported increases are accepted up to `radiation.clientMaxPerSec` (default 50).

**Survival module** (`survival.enabled` + per-rule toggles, combat-character §10.3):
- needs advance with server game time (F25);
- food/drink restore per item (GameProfile table from ALCH keywords/value);
- stage effects as constant effects;
- sleep in an owned/allowed bed (F07 occupancy) for real time × `survival.sleepTimeScale` converts to sleep hours (Well Rested via F19/F25);
- adrenaline counter from kills (F12), ranks removed on sleep per the table;
- diseases: risk pool updated by events (food, chems, infected hits from F11, swimming, rain from F25), rolled on sleep/hour ticks with `CombatRng`, cured by antibiotics/herbal remedies (effects);
- healing slowdown: Stimpak/food durations × the setting;
- no limb auto-heal (F08 regen rule);
- carry 75 and ammo weight via F04/F08.

### 4.7 Audience / visibility
- `EffectsUpdate` full: owner only (S8). For hosted NPCs, routed to the host via `GetActorToSendTo()` (S14).
- Visual subset: grid neighbours (and in the snapshot).
- Needs and addictions: owner only.

### 4.8 NPC parity
- The effect system treats NPCs identically: creature poisons/rads (F11 Damage archetype), legendary mutation spell (F13), NPC perk abilities, Cloak auras (e.g. glowing ones).
- A host may send `UseItem` for a hosted NPC (NPC stimpak AI [inference: verify which NPCs self-heal]); same validation.
- NPC effects persist only while the NPC exists (cell reset clears them, F14).

### 4.9 Gamemode API & server Papyrus
- **Properties:** `mp.get(actor,'activeEffects')` (private), `mp.get(actor,'addictions')`, `mp.get(actor,'survival')`.
- **Natives:** `mp.addEffect(actor, spellOrMgef, magnitude, durationMs)`, `mp.removeEffect(actor, effectId|source)`, `mp.cureAddiction(actor, chem|all)`.
- **Settings:** `healing.*`, `effects.slowTime`, `addiction.requireOverlap`, `radiation.environmental` (on), `radiation.clientMaxPerSec`, `respawn.clearRads`, `survival.enabled`, `survival.{needs, diseases, adrenaline, slowHealing, carryWeight, ammoWeight, sleepTimeScale}`.
- **Events:**
  - blockable: `onUseItem`, `onEatItem` (alias), `onAddiction`, `onDiseaseContracted`;
  - modifiable: `onEffectApply` (block or scale), `onRadiationDamage`;
  - observe: `onHealOther`, `onWithdrawal`.
- **Server Papyrus:**
  - `Actor.AddSpell/RemoveSpell/DispelSpell/HasMagicEffect/HasMagicEffectWithKeyword`;
  - `Actor.EquipItem(potion)` → the UseItem path;
  - `ActiveMagicEffect` `OnEffectStart/OnEffectFinish` for allow-listed MGEF scripts;
  - registration-based `OnMagicEffectApply` and `OnRadiationDamage` (PVM-007);
  - visible effects mirrored to clients (S15).

### 4.10 Edge cases & failure modes
- **Pip-Boy paused while healing:** effects keep ticking on the server; the player may die while paused (vanilla MP expectation; the gamemode can set an aid cooldown).
- **Disconnect mid-effect:** `remainingMs` is persisted at disconnect; timers resume on reconnect (`effects.pauseWhileOffline`, default true).
- **Server restart:** effects re-armed from `fo4Effects` (S10, I7). Constant effects are re-derived.
- **Duplicate taps:** nonces make repeated `UseItem` with the same nonce idempotent; the inventory count protects against over-use.
- **Stacking:** same-source effects refresh duration (vanilla); different sources stack. Per-MGEF rules are in the GameProfile.
- **Load-order change:** unknown MGEF/spell effects are dropped on load, logged.

### 4.11 Performance budget
- Effect tick: ≤ 2 µs per active effect per tick; the scheduler is bucketed (one timer wheel, not one timer per effect).
- Target ≤ 20 active effects per actor.
- `EffectsUpdate` delta ≤ 64 B; full ≤ 1 KB.
- Radiation: ≤ 5 µs per actor per second (grid-indexed emitters).

## 5. Engine / platform work required
- An ALCH consumption hook with cancel and limb read-out (extends PLAT-081 equip filtering; F20-T08).
- Radiation event sinks (`BGSRadiationDamageEvent`, CurrentRads*) via PLAT-040.
- Cosmetic effect playback natives (imagespace, effect shaders on puppets) via PLAT-088 / SpSnippet (PVM-016).
- A Pip-Boy effects data feed (F28).

## 6. Tests
- `L-unit` (`[F20]`, `[Effects]`, `[Chems]`, `[Radiation]`, `[Survival]`):
  - `UseItem` round trip;
  - stimpak on self restores HP over the duration; on a limb restores that limb's AV; on another actor in reach accepted, out of reach rejected with `SetInventoryFo4` correction;
  - blocked `onUseItem` keeps the item (regression of SkyMP bug 15);
  - missing item rejected;
  - hosted-NPC use by a non-host rejected;
  - Buffout raises STR/HP for the duration, then expires (fake clock);
  - Medic perk scales the stimpak;
  - Value Modifier with Recover removed on expiry;
  - Damage 45 ticks with resistance;
  - Cloak applies to neighbours;
  - Slow Time converted/disabled;
  - three overlapping Jet uses with forced RNG → addicted; withdrawal after expiry; Addictol cures; Chem Resistant halves the chance;
  - 10 rads/s with RR 10 → ~5 rads/s;
  - standing in an emitter primitive for 2 s adds rads; RadAway over time;
  - client rad decrease rejected; increase above the cap clipped;
  - rads lower `RadHealthMax` (with F08);
  - survival: hunger stage advances with game time, eating returns to Well Fed, adrenaline +1 rank per 5 kills, sleep removes per table, disease roll with forced RNG;
  - persistence round trip of `fo4Effects`, `addictions`, `survival` incl. remaining durations; backward compat;
  - late-joiner snapshot contains only the visual subset for neighbours;
  - gamemode veto and modification;
  - `OnMagicEffectApply` fired for a registered script.
- `L-int`: two bots; one heals the other's downed actor; Jet addiction across reconnect.
- `L-fixture`: ALCH/MGEF/SPEL parsing with FO4 layouts (AVIF formid @0x44) on synthetic records.
- `G-self`: the consumption hook cancels the engine effect; limb targeting detected; radiation curve calibration (stand in water with known RR).
- `G-manual`: use chems from the Pip-Boy while another player watches the visual effects; stimpak a downed teammate; survival needs over 1 game day at accelerated timescale.

## 7. Tasks
- [ ] **F20-T01** `UseItem` (91) message + handler: atomic consumption, targets, block fix, corrections, nonce — M — Depends: F04, NET-002, NET-007 — Verify: L-unit — Files: skymp5-server/cpp/messages/UseItemMessage.h, skymp5-server/cpp/server_guest_lib/fo4/UseItemService.{h,cpp}, gamemode_events/EatItemEvent.cpp; unit/UseItemTest.cpp
- [ ] **F20-T02** **Magic-effect pipeline (SRV-020, I10):** list-based `Fo4EffectSystem`, archetype handlers (§4.6 table), conditions, timer wheel on the server clock, persistence/re-arm — L — Depends: SRV-010, SRV-020, ESPM-009, ESPM-011, F25-T01 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/fo4/EffectSystem.{h,cpp}, fo4/effects/*.cpp; unit/Fo4EffectSystemTest.cpp
  - Accept: every archetype row has a test; restart re-arms remaining durations within 100 ms.
- [ ] **F20-T03** `EffectsUpdate` (92): owner full vs neighbour visual subset, coalescing, snapshot fields — S — Depends: F20-T02, NET-002 — Verify: L-unit — Files: skymp5-server/cpp/messages/EffectsUpdateMessage.h; falloutmp-client/src/services/messages/effectsUpdateMessage.ts
- [ ] **F20-T04** Perk/AV modifiers on effects via SRV-021 (Medic, Chemist, `ChemDurationMod`, addiction entry points) — S — Depends: F20-T02, SRV-021 — Verify: L-unit
- [ ] **F20-T05** Addiction & withdrawal — M — Depends: F20-T02, F20-T04 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/fo4/AddictionService.{h,cpp}
- [ ] **F20-T06** `RadiationService`: emitters/hazards/water/radstorm, resistance curve, ingestion, client increase-only path with F08 — M — Depends: F20-T02, F08, F25, ESPM-005 — Verify: L-unit, G-self — Files: skymp5-server/cpp/server_guest_lib/fo4/RadiationService.{h,cpp}
- [ ] **F20-T07** Legendary/enchantment constant and equip-ability effects, Cloak auras (with F05/F16/F11) — M — Depends: F20-T02, F05, F16 — Verify: L-unit
- [ ] **F20-T08** Client capture: ALCH consumption hook (cancel + limb), use-on-other UI, rad-delta reporter — M — Depends: PLAT-081, PLAT-040, F20-T01 — Verify: G-self, L-ts — Files: fallout4-platform/src/platform_fo4/ConsumeHook.cpp, falloutmp-client/src/services/services/useItemService.ts
- [ ] **F20-T09** Client apply: effects list feed (F28), cosmetic visuals on self and puppets, correction handling — M — Depends: F20-T03, F28 — Verify: G-manual — Files: falloutmp-client/src/sync/effects.ts
- [ ] **F20-T10** Slow Time policy (`disable`/`convert`) — S — Depends: F20-T02 — Verify: L-unit
- [ ] **F20-T11** Survival module: needs, stage effects, sleep via bed occupancy, healing slowdown, carry/ammo weight hooks — L — Depends: F20-T02, F25, F07, F04 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/fo4/SurvivalService.{h,cpp}
- [ ] **F20-T12** Survival diseases (risk pool) and adrenaline — M — Depends: F20-T11, F12 — Verify: L-unit
- [ ] **F20-T13** Gamemode/Papyrus surface (§4.9), registration-based events, docs — M — Depends: F20-T02, PVM-007 — Verify: L-int — Files: skymp5-server/ts typings, script_classes/PapyrusActor.cpp
- [ ] **F20-T14** `G-manual` script and sign-off — S — Depends: F20-T09 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F20-consumables.md

## 8. Open questions & risks
- The exact vanilla addiction rule (per-use chance vs the "≈3 uses" accumulation) is unclear. `addiction.requireOverlap` is an approximation; verify with D-real perk/MGEF data.
- Can the native Pip-Boy effects list be fed with server effects, or does F28 need an overlay?
- The radiation emitter data layout on REFR (radius/rate fields) needs ESPM confirmation (D-real).
- Survival `HC_Manager` balance values need a D-real dump (R12-style). Until then, wiki values in the GameProfile.
