# F13 — NPC Spawning, Hosting, AI & Factions

| Field | Value |
|---|---|
| Tier | T0 |
| Target level | L3+ (SkyMP L3; plus server-driven threat-based owner election, combat-state handover and host-claim validation) |
| SkyMP analogue | `AttachEspmRecord` NPC filters (`WorldState.cpp:368-659`), `npcEnabled`/`npcSettings`, `Host`/`HostStart`/`HostStop` (14/26/27), `OnHostAttempt` 2 s rule (`ActionListener.cpp:653-738`), `GetActorToSendTo()`. SkyMP level L3 (reference/skymp-sync-inventory.md §1.6) |
| Milestone | M5 (spawn + basic hosting, no combat), M6 (dead/leveled actors), M8 (combat election, migration, hostility) |
| Workstreams | SRV, CLI, PLAT, NET, ESPM, GM |
| Depends on | F01, F02, F08 (idx-scoped AVs, I13), F11 (damage attribution), F12 (death), F14 (leveled actor evaluation), PLAT-070, PLAT-071, PLAT-073, REF-012, ESPM-006, SRV-040 |
| References | reference/skymp-sync-inventory.md §1.6, §2 (NPC spawning row); reference/prior-art.md §3.1.9, §5.1 B7/C9, §5.3 Adopt 1/2/6, §5.4 Q3/Q9; reference/fo4-systems-combat-character.md §16; reference/fo4-systems-world-economy.md S15 (factions/crime); reference/fo4-data-formats.md §4.4 (ACBS, TPLT/TPTA, LTPT/LTPC), §4.15 (FACT) |

## 1. Summary
The server spawns the Commonwealth's raiders, gunners, super mutants, ghouls, creatures, robots and townsfolk from the ESMs. Leveled actors, gear and legendary status are rolled once on the server, so every client sees the same enemy. One client, the **host**, runs vanilla AI for each NPC: pathing, target choice, cover, attacks, burrowing. Everyone else mirrors it. The server chooses the host by threat: it prefers the player the NPC is fighting, uses hysteresis so the host does not flip-flop, and migrates hosting without resetting the fight when the host leaves. Hostility comes from FO4 factions plus a per-player hostility ledger (FO4 has no bounty), and the hosted AI can target remote players. Health, damage, death and effects stay server-authoritative.

## 2. Vanilla Fallout 4 behaviour
- **Placement:** ACHR refs whose base is NPC_ or LVLN. NPC_ templates: `TPLT` default plus `TPTA` (13 per-flag slots); template flags at ACBS +0x0E. Legendary template + chance: `LTPT`/`LTPC` (fo4-data-formats §4.4).
- **ACBS flags:** Essential 0x2, Unique 0x20, Protected 0x800, PC Level Mult 0x80, Spawns Dead 0x4000000, Respawn 0x8, Doesn't affect stealth meter 0x40.
- Level for PC-level-mult actors = player level × mult, clamped by min/max and by the encounter zone [inference: ECZN via LCTN/CELL].
- **AI:** combat styles (CSTY), `StartCombat/StopCombat`, `GetCombatState` (0 none, 1 combat, 2 searching), `OnCombatStateChanged`; packages (`PKID`, default package list `DPLT`) (combat-character §16.1).
- **Engine state:** InCombat bit `Actor+0x2D0 & 0x4000`, combat target `Actor+0x380`, life state `Actor+0x130` bits 17–20 (7 = essential down) (prior-art §5.1 B7, 1.11.191).
- **Creature specials:** radscorpion and mole rat burrow; mirelurk shell guard; deathclaw paired grabs (not on PA wearers); sentry bot overheat/self-destruct; assaultron charged laser.
- **Legendaries** mutate once below 50 % HP: full heal + `AbLegendary*` spell (combat-character §16.1).
- **Factions:** FACT `XNAM` relations (Neutral/Enemy/Ally/Friend), `CRVA` crime values. **No bounty or jail:** a detected crime makes the owner or faction hostile; most calm down after ~3 game days, and murder of named NPCs is permanent (world-economy S15).
- **Single-player assumptions that break:**
  - AI runs wherever the actor is loaded, so every client would run its own AI;
  - leveled variants are rolled per client: FO4_Wrld saw the same REFR produce a different NPC per client (prior-art §3.1.9);
  - "player enemy" is global to the one `PlayerRef`. Remote players are ordinary actors to the host's AI, so `SetPlayerEnemy` cannot target them;
  - the legendary roll and mutation are local.
- Settlers, provisioners and companions are quest/script-managed (`WorkshopNPCScript`, `FollowersScript`), and those scripts are blocked (ADR-011).

## 3. SkyMP baseline
- **Spawn filters** (`AttachEspmRecord`) skip:
  - initially disabled, deleted and starts-dead refs (`// TODO: Load dead references`);
  - essential/protected/unique bases;
  - members of `CrimeFactionsList` 0x26953 (Skyrim id);
  - banned races;
  - per-file interior/exterior rules (`npcSettings`).
  - NPCs are off unless `npcEnabled` [src: WorldState.cpp:368-659; ScampServer.cpp:235-247].
- **Template chain:** evaluated once on the server (`EvaluateTemplateChain`, follows `TPLT` only) and persisted as `templateChain`. The client builds a clone with `TESModPlatform.CreateNpc`/`EvaluateLeveledNpc`. The leveled *visual* variant is not synced (skymp-sync-inventory §2).
- **Hosting:** first-come `Host` attempts (client, unreliable, ≤ 1/s/NPC); granted if there is no host or no movement for 2 s; `HostStart`/`HostStop`; `isHostedByOther` property; `EquipBestWeapon` on host change; AVs re-sent after 1 s. There is no release on stream-out or disconnect (2 s staleness only), and hosted-NPC movement and AI state are trusted (skymp-sync-inventory §1.6).
- **Reuse:** filters (made GameProfile data, REF-012), templateChain persistence, the Host/HostStart/HostStop messages and `isHostedByOther`, `GetActorToSendTo()` routing, faction persistence (`MpActor.cpp:262-349`).
- **Replace:**
  - first-come election → server election (Host becomes a *readiness* signal);
  - the TPLT-only chain → per-flag TPTA (F14);
  - add AI state reporting, migration re-seeding and hostility.

## 4. Design

### 4.1 Authority model
- **Class C** for AI decisions, locomotion and animation (the host's AI), validated per F01/F02.
- **Class A** for existence, base/template/level/legendary roll, inventory, AVs, damage, death, effects, factions/hostility and **who hosts**.
- The host's AI-state reports (combat target, detection, special states) are claims. The server stores them, sanity-checks them and uses them for election, F29 and F11, but never lets them change class-A state directly.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Template chain (per-flag) | `FormDesc[]` | `templateChain` | yes (existing) | F14 evaluation at first load |
| NPC level | u16 | `npcLevel` (new) | yes, until cell reset (F14) | ACBS level / PC-mult rule §4.6 |
| Legendary | {isLegendary, templateDesc, mutated} | `legendary` (new) | yes | `LTPT`/`LTPC` roll at first load |
| Factions | `Faction[]` | `factions` (existing) | yes | NPC_ `SNAM` (5-byte layout, ESPM-006) |
| Host | actor id | `WorldState::hosters` | no | — |
| Host candidates | {userId, readySince, score} | `NpcHostingService` (new) | no | — |
| AI state | {combatState, targetId, lastTargetPos, untargetable, special flags, ts} | `MpActor::npcAi` (new) | no (re-derived) | idle |
| Threat table | target → {engage, damage(decay 5 s)} | `NpcHostingService` | no | — |
| Per-player hostility ledger | factionDesc → {untilGameTime, permanent} | profile record (SRV-060) `hostility` | yes | empty |
| Starts-dead actor | `isDead` | existing | yes | ACBS 0x4000000 / ACHR flag |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `Host` (14) | C→S | `remoteId` = "I have this NPC loaded and can host it" (readiness) | U | on stream-in / 3D ready; ≤ 1/s/NPC | reused, new meaning |
| `HostStart` / `HostStop` (26/27) | S→C | `target` | R | on election result | reused |
| `NpcAiState` (111) | C→S (host) | `npcIdx`, `seq`, `ts`, `combatState` u8, `combatTargetIdx`, `flags` (burrowed, shellGuard, overheated, fleeing, inKillMove), `detections[] {targetIdx, level 0–3}` (F29), `events[] {kind (crimeWitnessed, alarm), targetIdx, refId}` | R ordered | on change, ≤ 4 Hz per NPC; detections coalesced | **new** |
| `NpcAiState` (111) | S→C (new host) | same fields = re-seed bundle after `HostStart` | R | on migration | **new** |
| `CreateActorFo4` (64) | S→C | `templateChain`, `npcLevel`, `legendary`, `factions` incl. MP hostility factions, `isHostedByOther`, `isDead` | R | subscribe | reused |
| `UpdateMovementFo4` / `UpdateActions` / `UpdateGraphVariables` (65/74/75) | C→S host | NPC idx | per F01/F02 | per F01/F02 | reused |
| `HitReport` (82) | C→S host | aggressor = hosted NPC | R | per F09/F10 | reused |
| `ChangeValuesAv` (72) | S→host | idx-scoped AV snapshot (I13) | R | on host change, then on change | reused |

### 4.4 Client capture (owner side)
- **All clients:** on NPC stream-in, after the FormView finishes `applyAll` and the 3D is loaded, send `Host{remoteId}` once as readiness, and again every 5 s while the NPC stays loaded.
- **Host only, per hosted NPC:**
  - movement/actions/variables as F01/F02;
  - `NpcAiState` on change of `GetCombatState()`, `GetCombatTarget()`, InCombat bit, burrow/special flags (from the F02 anim state and `ActorState` words, per-race rules in `npcSpecialStates.json`), and `IsDetectedBy`-equivalent levels from `TESCombatEvent`/`DetectionEvent` sinks (PLAT-040) for each player puppet in range (F29);
  - `TESDeathEvent` is **not** authoritative: death comes from F12;
  - hits by the NPC go out as `HitReport` with the NPC as aggressor.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Spawn on clients:** the platform builds a runtime NPC from `templateChain`, applying each template flag from its `TPTA` slot or `TPLT` (PLAT-070 extension), so every client gets the same race, appearance, outfit and stats. Level and legendary flag come from the snapshot, never from the local engine roll. Legendary actors show the vanilla star in the name (F31).
- **Non-hosts:** AI and engine motion stay suppressed in the engine (PLAT-071) plus the DoNothing package (PLAT-073). Transform from F01, animation from F02. The local engine never applies damage to NPCs: hits go to the server (F09/F11).
- **On `HostStart`:**
  1. lift the suppression;
  2. apply the AV snapshot (`ChangeValuesAv`, idx-scoped) and effects (F20 via `GetActorToSendTo`);
  3. apply the `NpcAiState` re-seed: `StartCombat(target)` when combatState ≠ 0, `SetPosition` to the last server transform, burrow/shell state via F02 instant variants;
  4. `EvaluatePackage()`.
- **On `HostStop`:** re-suppress, stop sending, and keep the last transform until the new host's samples arrive.
- **Stream-in of a dead NPC:** spawn already dead (F12 `isDead` snapshot → `KillSilent`-style pose without a death event).
- **Respawn** (F12/F14): a new chain/level/legendary roll is pushed as a fresh `CreateActorFo4`.

### 4.6 Validation & anti-cheat (server)
**Spawn rules** (`Fo4NpcSpawnPolicy`, GameProfile data, REF-012). Each rule is a server setting:

| Category | How detected | Default |
|---|---|---|
| Initially disabled / deleted | REFR flags 0x800 / 0x20 | skip (SkyMP) |
| Starts dead | ACHR flag / ACBS 0x4000000 | **load as dead, lootable** (`npc.loadStartsDead`, resolves SkyMP TODO) |
| Essential / protected (named townsfolk, vendors) | ACBS 0x2 / 0x800 | spawn, server-invulnerable: downed instead of dead (`npc.essentialPolicy = invulnerable\|skip\|mortal`) |
| Unique (non-essential) | ACBS 0x20 | spawn, mortal, no respawn until cell reset (F14) |
| Companions | GameProfile companion list (CommonPropertiesScript refs) | skip: F21 spawns them |
| Workshop NPCs (settlers, provisioners) | VMAD script `WorkshopNPCScript` or keyword list | skip: F22 spawns them |
| Synths | race/keyword | spawn as normal enemies; infiltrator replacements are quest-only, so they are skipped by the disabled filter |
| Banned races (vertibird, Liberty Prime, PA race) | GameProfile list | skip |
| Crime-factions skip (SkyMP) | n/a in FO4 | off |

- **Leveled actor level:** for PC-level-mult actors, `npc.levelRule = firstObserver` (default: the level of the player whose subscription loaded the chunk), `fixed`, or `zoneMax`. The result is clamped by ACBS min/max and the encounter zone, and persisted until cell reset.
- **Election** (`NpcHostingService`, every 500 ms per NPC with listeners):
  - **Candidates:** users subscribed to the NPC's grid cell who sent `Host` readiness within 10 s, are in the same worldOrCell, not loading, and alive (dead players are allowed with a penalty).
  - **Out of combat score:** proximity `p = 1 − d/4096`.
  - **In combat score:** threat = `10·engage + 6·p + 30·damageShare`. `engage` = 1 if the NPC's reported target is the candidate's actor; `damageShare` = the candidate's share of damage dealt to/by the NPC in the last 5 s (F11 attribution) (FO4_Wrld formula, prior-art §3.1.9).
  - **Hysteresis:** keep the current host for at least 2 s; switch only if the best score > 1.3 × current and the difference ≥ 3. Owner preference: F21 companions are forced to their owner.
  - **Revocation:** host disconnect, host leaves the grid, host not ready, or no NPC movement from the host for 2 s (SkyMP timeout) → immediate re-election.
- **Host-claim checks** on every NPC-scoped message (S6):
  - the sender is the current host, else drop + `HostStop`;
  - `NpcAiState.seq` is monotonic;
  - `combatTargetIdx` exists, is in the NPC's grid, is alive and is hostile under §4.6 hostility (else ignore the field and count `npc_claim_reject_total`);
  - detections only for targets within 8192 u;
  - rate limits.
- **Hostility evaluation** `IsHostile(npc, actor)`:
  1. faction reactions (`XNAM`) between the NPC's factions and the actor's factions;
  2. for players: the per-player ledger (`hostility[faction]` not expired on the F25 clock, or permanent);
  3. gamemode override.
  - Players carry `PlayerFaction`-equivalent membership on their puppets [inference: verify the FO4 player faction id] so vanilla enemy relations apply.
  - A per-player hostile faction is realised on clients as a runtime faction `MP_Hostile_<factionDesc>` that is an enemy of `<faction>`. The server pushes membership in `factions` (snapshot/UpdateProperty). Clients create these runtime factions locally on first use (platform native, F13-T09).
- **Crime reports:** `events.crimeWitnessed` from the host is accepted only if the witness is alive, within 2048 u of the criminal and not hostile already. On accept the server sets the ledger entry (3 game days, or permanent for murder) and fires `onCrime` (blockable).
- **Legendary mutation hook** (called by F11 after damage is applied): if `legendary.isLegendary && !mutated && alive && health < 50 %`:
  1. restore Health to full (F08);
  2. apply the mutation spell (`AbLegendary*`, SRV-020);
  3. set `mutated`;
  4. set `DamageApplied.flags.legendaryMutate` (F11 step 12).
  - Gamemode `onLegendaryMutate` can block. The legendary item drop is F14.
- **Kill moves / paired grabs:** blocked on all clients (F02 §4.10). Deathclaw grabs are replaced by normal attacks.
- **Untargetable states:** while the host reports `burrowed`, F11 rejects hits on the NPC. The server limits burrow time (`npc.maxBurrowSec` 10) to stop a malicious host from making an NPC permanently immune.

### 4.7 Audience / visibility
- `CreateActorFo4`, movement and animation: grid neighbours.
- `HostStart`/`HostStop`, the `NpcAiState` re-seed and idx-scoped AVs: the host only (S14).
- Hostility-faction membership of a player: neighbours, because their hosted NPCs need it.
- The ledger itself is private to the owner.

### 4.8 NPC parity
This spec *is* the NPC path. Creatures use the same model with per-race graph descriptors (F02-T10) and per-race special-state rules (`npcSpecialStates.json`). Turrets placed by workshops are hosted actors with faction ownership (F22). NPCs in power armor follow F17 §4.8.

### 4.9 Gamemode API & server Papyrus
- **Settings:** `npcEnabled`, `npcSettings` (existing), `npc.essentialPolicy`, `npc.loadStartsDead`, `npc.levelRule`, `npc.maxHostedPerClient` (40), `npc.electionIntervalMs` (500), `npc.maxBurrowSec`.
- **Properties:**
  - `mp.get(npc,'host')`, `mp.set(npc,'hostOverride', actorId|0)`;
  - `mp.get(npc,'combatTarget')`, `combatState`, `npcLevel`, `isLegendary`;
  - `mp.get/set(actor,'hostility')`.
- **Events:**
  - `onNpcSpawn(npc)`: blockable. Blocking skips the spawn for this load.
  - `onHostChange(npc, newHost, oldHost)`: observe.
  - `onCombatStart(npc, target)`: blockable. Blocking makes the server send an `NpcAiState` re-seed with combatState 0, so the host calls `StopCombat`.
  - `onLegendaryMutate`: blockable.
  - `onCrime(actor, faction, crime)`: blockable.
- **Papyrus:**
  - `StartCombat/StopCombat/EvaluatePackage/SetCombatStyle` are forwarded to the host via SpSnippet (SRV-040, I16);
  - `GetCombatState/GetCombatTarget/IsInCombat` read server state;
  - `OnCombatStateChanged` is fired on `NpcAiState` changes (S16);
  - `OnLoad`/`OnUnload` fire on first listener / last listener.

### 4.10 Edge cases & failure modes
- **Host migration mid-fight:** the re-seed carries the target and last target position, so the new host restarts combat at once. Expect at most one AI decision gap of ~200 ms.
- **No candidates:** the NPC freezes server-side (no movement). Effects and regeneration still tick server-side (F08/F20). Combat state decays to 0 after 10 s.
- **Interiors:** the grid is per cell, so FO4_Wrld's "ownership sphere sees other cells' NPCs" bug cannot happen (prior-art §5.4 Q9).
- **Host is the target and dies:** the threat drops the dead candidate's engage term. Re-elect after the hold time.
- **Load storms:** election runs incrementally (bounded work per tick). The cap `npc.maxHostedPerClient` spreads NPCs across clients.
- **Server restart:** level, legendary and chain persist, so an enemy keeps its identity. Host state is rebuilt from readiness.

### 4.11 Performance budget
- Target 300 hosted NPCs per server (00-vision §7).
- Election ≤ 5 µs per NPC per evaluation → ≤ 3 ms/s.
- `NpcAiState` ≤ 48 B typical, ≤ 4 Hz in combat.
- Host upstream per NPC ≈ movement 640 B/s + animation ~1 KB/s. Hosted NPCs farther than 2048 u from the host send movement at 200 ms.

## 5. Engine / platform work required
- PLAT-070 extension: build a runtime NPC from a per-flag template chain (TPTA-aware) with a server-given level.
- PLAT-071/073: suppression on/off per actor at runtime (toggle on HostStart/HostStop).
- PLAT-040: sinks for `TESCombatEvent`, `DetectionEvent`, `TESDeathEvent`; readers for InCombat/target/life state (prior-art B7 offsets → AE IDs).
- New natives (F13-T09): `createRuntimeFaction(key, enemyOf[])`, `setActorFactions(actor, list)`.

## 6. Tests
- `L-unit` (`[F13]`, `[NpcHosting]`, `[NpcSpawn]`, `[NpcCombat]`):
  - spawn-policy table per row (synthetic plugin);
  - starts-dead actor loaded dead with inventory;
  - essential NPC downed, not killed;
  - PC-level-mult level clamped and persisted;
  - legendary roll persisted and identical in two snapshots;
  - election: nearest candidate wins out of combat; the threat winner wins in combat; hysteresis holds at 1.2×; switch at 1.4× and Δ ≥ 3;
  - disconnect → immediate re-election and `HostStop`/`HostStart` messages;
  - `NpcAiState` from a non-host dropped + `HostStop`;
  - stale `seq` dropped;
  - non-hostile combat target ignored;
  - re-seed bundle sent to the new host;
  - legendary mutates exactly once below 50 %;
  - burrowed NPC rejects hits and burrow is capped;
  - crime report sets the ledger, which expires after 3 game days with a fake clock;
  - persistence round trip for `npcLevel`, `legendary`, `hostility`;
  - gamemode veto of `onCombatStart` and `onNpcSpawn`.
- `L-int`: 3 bots around a raider camp; election follows the attacked bot; kill the host bot → migration within 2.5 s; bandwidth per host measured.
- `L-fixture`: per-flag template evaluation against synthetic NPC_/LVLN chains (shared with F14).
- `G-self`: suppression toggle on a test NPC (does not move while unhosted; fights when hosted); runtime faction makes a neutral NPC attack a puppet.
- `G-manual`: two players fight a raider group and a radscorpion; drop the host's connection mid-fight; both players see the same legendary enemy mutate.

## 7. Tasks
- [ ] **F13-T01** FO4 NPC spawn policy (GameProfile table, settings, categories of §4.6) — M — Depends: REF-012, ESPM-006 — Verify: L-unit, L-fixture — Files: skymp5-server/cpp/server_guest_lib/game_profile/fallout4/NpcSpawnPolicy.{h,cpp}, WorldState.cpp; unit/NpcSpawnPolicyTest.cpp
  - Accept: each category row has a passing test; the Skyrim filters are unchanged (`~[espm]` green).
- [ ] **F13-T02** Load starts-dead actors as dead, lootable MpActors — S — Depends: F13-T01, F12-T03 — Verify: L-unit — Files: WorldState.cpp, MpActor.cpp
- [ ] **F13-T03** Leveled identity: per-flag chain (with F14), `npcLevel` rule, legendary roll; persistence; snapshot fields — M — Depends: F14, F13-T01 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/fo4/NpcIdentity.{h,cpp}, MpChangeForms.{h,cpp}
  - Accept: the same ACHR gives an identical snapshot across restart; reroll only on cell reset.
- [ ] **F13-T04** Platform: runtime NPC from a per-flag chain with a server level (PLAT-070 extension); per-actor suppression toggle (PLAT-071) — L — Depends: PLAT-070, PLAT-071 — Verify: G-self — Files: fallout4-platform/src/platform_fo4/NpcApi.cpp
- [ ] **F13-T05** `NpcHostingService`: readiness, candidates, threat score, hysteresis, revocation, budget cap, metrics — M — Depends: F11 (attribution), F13-T06 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/fo4/NpcHostingService.{h,cpp}, ActionListener.cpp (`OnHostAttempt` FO4 path)
  - Accept: §6 election cases pass; `npc_host_changes_total` metric present.
- [ ] **F13-T06** `NpcAiState` (111) message both directions + host validation + storage — M — Depends: NET-002 — Verify: L-unit — Files: skymp5-server/cpp/messages/NpcAiStateMessage.h, Messages.h; falloutmp-client/src/services/messages/npcAiStateMessage.ts
- [ ] **F13-T07** Migration re-seed (AV snapshot I13, effects, `NpcAiState` S→C) — S — Depends: F13-T05, F08-T08 — Verify: L-unit
- [ ] **F13-T08** Client hosting service: readiness, HostStart/Stop toggling, AI-state capture via event sinks, re-seed apply — L — Depends: F13-T04, F13-T06, PLAT-040 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/npcHostingService.ts, falloutmp-client/src/view/formView.ts
- [ ] **F13-T09** Hostility: faction model, per-player ledger (SRV-060), crime reports, runtime MP hostility factions (server push + platform natives) — L — Depends: SRV-060, F25-T01, F13-T06 — Verify: L-unit, G-self — Files: skymp5-server/cpp/server_guest_lib/fo4/HostilityService.{h,cpp}; fallout4-platform/src/platform_fo4/FactionApi.cpp
- [ ] **F13-T10** Creature special states (burrow, shell, overheat) rules + F11 untargetable hook + burrow cap — M — Depends: F13-T06, F02-T10 — Verify: L-unit, G-manual — Files: skymp5-server/cpp/server_guest_lib/game_profile/fallout4/npcSpecialStates.json
- [ ] **F13-T11** Legendary mutation hook in the damage pipeline (F11) + effect (SRV-020) + event — S — Depends: F13-T03, F20-T02 — Verify: L-unit
- [ ] **F13-T12** Gamemode/Papyrus surface (§4.9), docs — M — Depends: F13-T05, F13-T09 — Verify: L-int — Files: skymp5-server/ts typings, script_classes/PapyrusActor.cpp
- [ ] **F13-T13** Load test: 300 hosted NPCs / 100 bots; election CPU and host bandwidth within §4.11 — M — Depends: F13-T05, QA-020 — Verify: L-int
- [ ] **F13-T14** `G-manual` script (raider camp, radscorpion, host drop, legendary) — S — Depends: F13-T08 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F13-npc-hosting.md

## 8. Open questions & risks
- Does a remote-player puppet (PLAT-070) get targeted by vanilla AI like the player? Needs the right faction membership and possibly `SetPlayerTeammate`-free detection (G-self in F13-T04).
- Runtime faction creation via `IFormFactory` is unverified [inference]. Fallback: a fixed pool of MP factions in a small FalloutMP plugin (only if shipping our own `.esp` is accepted).
- Essential-policy default (`invulnerable`) changes town gameplay versus SkyMP's skip. Confirm with the user.
- Host-reported AI is trusted for targeting and burrow. A cheating host can make its NPCs passive toward itself; mitigation is election by threat (other players take over when engaged) plus anomaly metrics.
