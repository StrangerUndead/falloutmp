# F29 — Stealth: Detection, Invisibility, Sneak Attacks

| Field | Value |
|---|---|
| Tier | T2 |
| Target level | L3 |
| SkyMP analogue | None (Skyrim sneak is local; SkyMP trusts `isSneakAttack` from the client, I9) |
| Milestone | M10 (sneak-attack flag derivation is needed earlier by F11 in M8, with the conservative default) |
| Workstreams | SRV, CLI, PLAT, NET, GM |
| Depends on | F13 (hosting, `NpcAiState` detections), F01 (sneak flag, movement history), F11 (sneak-attack multiplier), F20 (Invisibility/Chameleon effects), F08 (public AVs on puppets), SRV-021 (sneak perks) |
| References | reference/fo4-systems-combat-character.md §14, §1.2 (`Invisibility` 0x2F3, `MovementNoiseMult` 0x319, `DetectionMovementMod` 0xBA43C), §4 (sneak damage); reference/fo4-data-formats.md §4.10 (archetypes 11, 35, 49); 01-sync-standard.md I9 |

## 1. Summary
A sneaking player can hide from NPCs and see the vanilla sneak indicator (Hidden / Caution / Danger / Detected) reflect what the NPCs actually perceive, even though those NPCs' AI runs on another player's machine. Sneak attacks deal bonus damage only when the server agrees the target was unaware. Stealth Boys and Chameleon armor make players near-invisible to NPCs and translucent to other players. PvP stealth follows explicit server rules, so nobody gets a hidden advantage from client-side AI quirks.

## 2. Vanilla Fallout 4 behaviour
- **Detection** is computed per (viewer, target) by the viewer's AI: `Actor::CalculateDetectionFormula(target, DetectionData)`, `DetectionData{detectedLocation, detectionLevel, detectionType}` (combat-character §14.1).
- **Inputs:**
  - light (`ModDetectionLight` 0x2F);
  - movement/noise (`ModDetectionMovement` 0x30, `MovementNoiseMult` 0x319, `IgnoreRunningDuringDetection` 0xF);
  - Agility; Sneak ranks (20–50 % harder, ignore traps/mines, rank 5 "distant enemies lose you");
  - armor mods (Shadowed, Muffled), silencers; `SetUndetectable` 0x80.
- **Indicator states:** Hidden / Caution / Danger (+ Detected).
- **Invisibility:** Stealth Boy = invisible for 30 s (MGEF Invisibility 11 / Cloak 35); Chameleon (archetype 49) = invisible while sneaking and still; `Invisibility` AV 0x2F3.
- **Natives:**
  - `IsDetectedBy`, `HasDetectionLOS`, `CreateDetectionEvent(owner, soundLevel)`;
  - `SetNotShowOnStealthMeter`, `StartSneaking`;
  - `TESEnterSneakingEvent`, `OnEnterSneaking`;
  - ACBS flag 0x40 "doesn't affect stealth meter".
- **Sneak attacks** need the target to be unaware; the multiplier is applied by the damage formula (F11).
- **Single-player assumptions that break:** only NPC AI on the local machine detects; the meter shows the most alert *local* NPC vs the single player; players never detect players through AI (combat-character §14.2). In MP, NPC AI runs only on the host, so on everyone else the local meter would always read "Hidden".

## 3. SkyMP baseline
- SkyMP trusts the client's `isSneakAttack` flag in `OnHit`. FalloutMP derives it from server-known state instead (I9, F10-T04).
- SkyMP has no detection sync.
- **Reuse:**
  - movement flags/history (F01) for sneak and speed;
  - hosted-NPC trust boundaries (F13) for detection claims;
  - SpSnippet/visual routing for invisibility visuals (S15).

## 4. Design

### 4.1 Authority model
- **NPC → player detection** is decided by the NPC's host AI. It is the only place with LOS and lighting, so it is class C, reported in `NpcAiState.detections` (F13). The server stores it with timestamps, sanity-bounds it (§4.6), aggregates it, and uses it.
- **Sneak-attack validity, invisibility effects and PvP stealth rules:** class A.
- **Detection meter presentation:** class D, fed by the server's `DetectionState`.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Per-pair detection | (npc, target) → ring of {ts, level 0–3} (2 s) | `StealthService` (new) | no | 0 (none) |
| Per-player aggregate | {level, sourceIdx, lastSentMs} | `StealthService` | no | 0 |
| Approximation inputs | sneak flag, speed, noise mult, Sneak ranks, Invisibility AV, recent shots | from F01/F08/F09/SRV-021 | no (derived) | — |
| Invisibility / Chameleon effects | active effects | F20 effect system | `fo4Effects` (F20) | — |
| Anomaly counters | per host | metrics | no | 0 |

There is nothing new to persist: detection is transient, and effects persist through F20.

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `NpcAiState` (111) | C→S (host) | `detections[] {targetIdx, level}` for player and companion puppets in range | R | on change, coalesced ≤ 4 Hz per NPC | reused (F13) |
| `DetectionState` (106) | S→C | `level` u8 (0 hidden, 1 caution, 2 danger, 3 detected), `sourceIdx` (most alert NPC), `seq` | U | on change; every 2 s while sneaking | new (registry) |
| `EffectsUpdate` (92) | S→C | visual subset: Invisibility/Chameleon active on an actor | R | F20 | reused |
| `UpdateMovementFo4` (65) | C→S | `flags.sneaking` | U | F01 | reused |

### 4.4 Client capture (owner side)
- **The player:** nothing new. The sneak flag goes through F01, and `TESEnterSneakingEvent` is not needed (the server derives transitions).
- **Host side:** for each hosted NPC, for each player/companion puppet within 8192 u, read the AI detection level:
  - `IsDetectedBy` plus the NPC's combat/alert state;
  - or a platform reader for `DetectionData.detectionLevel` from the `DetectionEvent` sink (PLAT-040) [inference: verify that a per-target level is readable].
  - Report changes in `NpcAiState.detections`.
- **The puppet must look like the real player to the host's AI.** The host applies, on each remote puppet:
  - the sneak state from F01 flags (platform native to set the sneaking movement state on the puppet; F01-T02 family);
  - public AVs from F08: `Invisibility`, `MovementNoiseMult`, `DetectionMovementMod`;
  - worn Muffled/Shadowed OMOD instances (F05);
  - so vanilla detection math sees the right inputs.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Meter:**
  - on `DetectionState`, the owner client overrides the HUD sneak indicator with the server level (platform hook on the HUD stealth meter value, F29-T07; fallback: a front overlay, F28);
  - the local engine's own computation (from suppressed local NPCs) is ignored;
  - all puppets get `SetNotShowOnStealthMeter(true)` so they never drive the local meter.
- **Invisibility visuals:**
  - when F20 marks Invisibility/Chameleon active on an actor, neighbours play the refraction/alpha effect on its puppet (`SetAlpha` with the floor `stealth.pvpMinAlpha`, default 0.15, via SpSnippet to listeners);
  - the host applies the `Invisibility` AV on the puppet so its NPC AI ignores it;
  - on expiry, revert.
- **Stream-in:** the visual subset is in the snapshot (F20). The meter starts at Hidden until the first `DetectionState`.
- **Respawn/reconnect:** the meter resets; detections are rebuilt from host reports.

### 4.6 Validation & anti-cheat
- **Host-claim bounds** (on each detection entry; the sender is the current host of the NPC, F13):
  - the target is in the NPC's grid and within 8192 u;
  - the level is in 0–3;
  - **plausibility vs the approximation model** (below). A host reporting "hidden" (0) for a target the model rates "certainly detected" (e.g. the target is not sneaking, inside 300 u, in front of the NPC's facing ±60°, not invisible) is accepted for meter purposes but **capped at caution** for sneak-attack validity (the target's benefit stays limited).
  - The anomaly is counted per host (`stealth_claim_anomaly_total`). Repeated anomalies (> 20/min) demote the host in F13 election (penalty factor 0.5).
- **Approximation model** (`StealthService::Estimate(npc, target)`), used when there is no host report (unhosted NPC) and as the plausibility bound:
  - inputs: distance; the NPC's facing cone (movement yaw); target sneak flag and speed (F01 history); `MovementNoiseMult` and Sneak ranks (SRV-021 `ModDetectionMovement`); `Invisibility` AV (→ never detected unless within 64 u); recent unsilenced shots by the target (F09) within 2 s (→ at least danger within 2048 u);
  - light is unknown on the server, so a neutral factor is used;
  - GameProfile constants [inference: model to be fitted against host reports collected in G-self].
- **Sneak-attack validity** (`IsSneakAttackValid(attacker, target, shotTs)`, called by F11 for every hit; the client flag is ignored, I9):
  1. the attacker was sneaking at `shotTs` (F01 history);
  2. NPC target: the max host-reported level of the target toward the attacker in [shotTs − 500 ms, shotTs] ≤ caution (1), with no report → the approximation; the target not already in combat with the attacker, unless its reported level toward the attacker is ≤ caution (vanilla allows sneak attacks on searching enemies [inference]);
  3. player target (PvP):
     - `stealth.pvpSneakAttacks` (default false → never valid);
     - if true: the attacker is outside the target's view cone (±70°, rewound yaw), at ≥ 200 u, the target has not damaged or been damaged by the attacker in 5 s, and the attacker is not visible by rule (invisible attackers count as unseen).
  - The result goes into F11's `DamageApplied.sneak` flag.
- **Chameleon:** the effect is active only while sneaking and speed < 20 u/s for ≥ 0.5 s (hysteresis), evaluated server-side from F01 history; F20 toggles the Invisibility AV accordingly.
- **Stealth Boy and attacking:** the effect lasts its duration (vanilla). The gamemode setting `stealth.attackBreaksInvisibility` (default false) can end it on attack.
- **Rate limits:** detections ≤ 16 entries per `NpcAiState`; excess dropped.

### 4.7 Audience / visibility
- `DetectionState`: owner only (S8).
- Host detection reports: server only.
- Invisibility visuals: neighbours (via F20's visual subset).
- No player learns which NPCs detect *another* player.

### 4.8 NPC parity
- NPCs detecting NPCs (e.g. a companion sneaking past raiders) is local to the host and not synced. Companion detection matters only for its owner's gameplay and is not shown on any meter.
- NPC targets of sneak attacks use the host-report path. NPC attackers sneak-attacking players use the same `IsSneakAttackValid` with the host's NPC sneak flag (F01 flags for hosted NPCs).
- Stealth Boys on NPCs (Institute coursers, assaultron Dominator) are F20 effects, visible to all.

### 4.9 Gamemode API & server Papyrus
- **Properties:** `mp.get(actor,'detectionLevel')` (the server aggregate), `mp.get(npc,'detects', targetId)`.
- **Settings:** `stealth.pvpSneakAttacks` (false), `stealth.pvpMinAlpha` (0.15), `stealth.attackBreaksInvisibility` (false), `stealth.meter` (on), `stealth.approximation.*` (GameProfile constants).
- **Events:** `onDetectionChange(actor, level, sourceNpc)` (observe; rate-limited to changes), `onSneakAttack(attacker, target)` (blockable: blocking removes the sneak bonus, not the hit).
- **Server Papyrus:**
  - `Actor.IsDetectedBy(akViewer)` reads the stored pair level (≥ 3);
  - `IsSneaking` [S] from F01 flags;
  - `OnEnterSneaking` fired on the server-derived transition (S16);
  - `SetNotShowOnStealthMeter` mirrored to the owner (S15);
  - `CreateDetectionEvent` forwarded to the host of nearby NPCs via SpSnippet (SRV-040).

### 4.10 Edge cases & failure modes
- **Host migration:** the per-pair history is kept server-side, so the new host's first report overwrites it after ≤ 250 ms. Until then the old level is used (decays to the approximation after 2 s).
- **Unhosted NPC:** approximation only.
- **Lag:** sneak-attack validity uses timestamps within the rewind window (≤ 250 ms, F09/SRV-023). Older reports do not count.
- **Loading screen / pause:** the owner's `DetectionState` stops; the meter shows the last value.
- **Server restart:** nothing to restore.

### 4.11 Performance budget
- Detection entries ≤ 6 B each; ≤ 16 per `NpcAiState`.
- Aggregation O(NPCs reporting the player) per change.
- `DetectionState` ≤ 8 B, at most 4 Hz per player while levels change.
- The approximation runs only on demand (sneak-attack checks, unhosted pairs): ≤ 1 µs.

## 5. Engine / platform work required
- A per-target detection-level reader on the host (`DetectionData` via a `DetectionEvent` sink or `CalculateDetectionFormula` hook; PLAT-040).
- A puppet sneak-state setter (movement state + graph) and puppet AV writes (F08 public AVs) for detection inputs.
- A HUD stealth-meter override (HUDMenu value write, or hiding plus an overlay).
- `SetAlpha`/refraction on puppets (PLAT-088 / SpSnippet).

## 6. Tests
- `L-unit` (`[F29]`, `[Stealth]`):
  - `DetectionState` round trip;
  - detections from a non-host dropped;
  - aggregation picks the max over NPCs and names the source;
  - sending on change only plus the 2 s refresh while sneaking;
  - a sneak attack is valid when the host reported caution and the attacker was sneaking at `shotTs`, and rejected when the host reported danger 300 ms before;
  - an attacker not sneaking (rewound) → invalid; the client flag ignored (I9);
  - PvP sneak attacks off by default; with the setting on, the view-cone rule;
  - an implausible "hidden" claim is capped at caution and counted, and repeated anomalies lower the host's election score;
  - Chameleon toggles only while still and sneaking (hysteresis);
  - the Stealth Boy effect sets Invisibility for 30 s (with F20);
  - an unhosted NPC uses the approximation;
  - gamemode `onSneakAttack` veto removes the bonus only;
  - `IsDetectedBy` Papyrus reads the pair state.
- `L-int`: a host bot reports detections for a sneaking bot; the sneaking bot receives `DetectionState` transitions.
- `G-self`: the detection-level reader returns plausible values on the host for a puppet; the meter override shows the forced level.
- `G-manual`: player A sneaks around raiders hosted by player B; A's meter matches the raiders' behaviour; a sneak-attack crit lands; a Stealth Boy makes A translucent to B and ignored by raiders.

## 7. Tasks
- [ ] **F29-T01** `DetectionState` (106) message + client mirror — S — Depends: NET-002 — Verify: L-unit — Files: falloutmp-server/cpp/messages/DetectionStateMessage.h, Messages.h; falloutmp-client/src/services/messages/detectionStateMessage.ts
- [ ] **F29-T02** `StealthService`: ingest `NpcAiState.detections` (F13), per-pair history, aggregation and send policy — M — Depends: F13-T06, F29-T01 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/fo4/StealthService.{h,cpp}; unit/StealthTest.cpp
- [ ] **F29-T03** Approximation model + plausibility cap + anomaly metric + F13 election penalty — M — Depends: F29-T02, F01-T05, F13-T05 — Verify: L-unit
- [ ] **F29-T04** (M8, with F11; the rest of F29 is 1.x) `IsSneakAttackValid` for F11 (NPC and PvP rules), replacing the client flag (I9) — S — Depends: F29-T02, F11, F10-T04 — Verify: L-unit
- [ ] **F29-T05** Invisibility/Chameleon rules with F20; puppet visuals and host AV application — M — Depends: F20-T02, F08 — Verify: L-unit, G-manual
- [ ] **F29-T06** Host capture: per-target detection reader and puppet sneak-state/AV setters — L — Depends: PLAT-040, F13-T08 — Verify: G-self — Files: fallout4-platform/src/platform_fo4/StealthApi.cpp, falloutmp-client/src/services/services/npcHostingService.ts
- [ ] **F29-T07** Owner meter override (HUD hook or overlay fallback) + `SetNotShowOnStealthMeter` on puppets — M — Depends: F29-T01, PLAT-060 — Verify: G-self — Files: falloutmp-client/src/services/services/stealthMeterService.ts
- [ ] **F29-T08** Gamemode/Papyrus surface and PvP settings (§4.9) — S — Depends: F29-T04 — Verify: L-int
- [ ] **F29-T09** `G-manual` script and sign-off; fit approximation constants from collected host reports — S — Depends: F29-T06, F29-T07 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F29-stealth.md
- [ ] **F29-T10** Pickpocketing (owned here because FO4 pickpocketing is a sneak interaction): `PickpocketAttempt` (119) request → server computes success from the sneak/detection state, the perk entry points (SRV-021) and item weight/value, moves the item atomically (F04, F06 rules), and flags crime via `NpcAiState` (111) on failure. Players can only be pickpocketed when the gamemode allows it (setting `pvp.pickpocket`, default off) — M — Depends: F29-T04, F06-T01, SRV-021 — Verify: L-unit, G-manual — Files: falloutmp-server/cpp/server_guest_lib/PickpocketService.{h,cpp}, falloutmp-client/src/services/services/pickpocketService.ts
  - Accept: unit tests cover success, failure (crime flag, hostility), reject when not sneaking or out of reach (with correction), and the gamemode veto.

## 8. Open questions & risks
- Is a per-target detection level readable on the host without heavy RE (`DetectionData` per pair)? Fallback: report only `IsDetectedBy` (detected/not) plus combat state, i.e. levels 0/3 with caution derived from the NPC's alert state.
- Puppet sneak state: does the host AI apply sneak detection bonuses to a non-player actor that is sneaking? Verify in G-self.
- The light level is not modelled on the server, so the approximation may disagree with hosts in dark interiors. That is why it only caps claims and never overrides the host's "detected".
