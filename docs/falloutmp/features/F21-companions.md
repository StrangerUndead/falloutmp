# F21 — Companions & Dogmeat

| Field | Value |
|---|---|
| Tier | T2 |
| Target level | L3 |
| SkyMP analogue | None. Built on NPC hosting (F13; SkyMP §1.6) and containers (F06) |
| Milestone | M10 |
| Workstreams | SRV, CLI, PLAT, NET, GM |
| Depends on | F13 (hosting, owner override), F06 (container trade), F05 (equipment), F12 (downed/revive), F20 (`UseItem` on others), F19 (owner perks), F17 (companion PA), F01/F02 |
| References | reference/fo4-systems-combat-character.md §13, §1.2 (`CA_Affinity` 0xA1B80, `FollowerState/Distance/Stance` 0x344–0x346); reference/prior-art.md §5.4 Q7; 02-architecture.md ADR-011; reference/skymp-sync-inventory.md §1.6 |

## 1. Summary
A player can recruit a companion (Piper, Nick, Codsworth…) or Dogmeat. The companion follows its owner, fights for them, carries items, takes commands (wait, follow, attack that, go there, pick that up, use that), and builds up affinity with that owner. Other players see it and its owner's name. Only the owner can command or trade with it. The companion's AI runs on the owner's client. Health, inventory, death and affinity stay with the server. When downed, any player (by default) can revive it with a stimpak. Which companions exist and how they are recruited is up to the gamemode. Vanilla companion quests and scripts never run.

## 2. Vanilla Fallout 4 behaviour
- One human/robot companion **or** Dogmeat at a time [src: f4se FollowersScript.psc:761-815]. Quest `Followers` 0x289E4; `CommonPropertiesScript` 0xA7D73 lists the companion refs (combat-character §13.1).
- **Activation script effects:** `IgnoreFriendlyHits`, `SetPlayerTeammate(…, canDoFavor, givePlayerXP)`, `SetNotShowOnStealthMeter(true)`, keywords while current (stimpak-able), hit registrations [src: f4se FollowersScript.psc:1061-1110].
- **Commands** (CommandMode, `SetCanDoCommand`/`SetCommandState`): None 0, Call 1, Follow 2, Move 3, Attack 4, Inspect 5, Retrieve 6, Stay 7, Release 8, Heal 9 [src: f4se Actor.psc:847-883]. Events `TESCommandMode{Enter,Exit,GiveCommand,CompleteCommand}Event`, `PlayerCommandTypeEvent`.
- **Affinity** −1100…1100 [src: f4se CompanionActorScript.psc:120-123]:
  - thresholds Hatred −1000, Disdain −500, Neutral 0, Friend 250, Admiration 500, Confidant 750, Infatuation 1000 (companion perk);
  - like +15 / love +35 / dislike −15 / hate −35, scaled by size 0.5/1/1.5 [web: https://fallout.fandom.com/wiki/Affinity];
  - passive gain `40 − 0.033 × affinity` every 10 real minutes if the player gained XP within 10 000 u;
  - reactions only within 2500 u [src: CompanionActorScript.psc:276-300,659-660,669-700].
- **Essential:** active companions are essential, get up after combat, and can be revived with a stimpak (Survival: must be healed or they leave). Carry ≥ 150 (Strong 200). They use given weapons (need ammo) and can enter PA (no core drain).
- **Dogmeat:** `DogmeatActorScript`; no PA; does not break Lone Wanderer.
- **Single-player assumptions that break:** everything references `Game.GetPlayer()` (proximity, XP gain, perks, murder, PA reactions); quest aliases are global singletons; a companion belongs to "the" player (combat-character §13.2).

## 3. SkyMP baseline
SkyMP has no follower sync. Reused:
- F13 hosting (`HostStart`/`HostStop`, `GetActorToSendTo`), extended with an owner override;
- the container flow (`OpenContainer`, `TakeItemFo4`/`PutItemFo4`) on an NPC;
- `GameModeEvent` vetoes;
- per-NPC persistence in `MpChangeFormREFR` (inventory, position, factions).

Vanilla follower scripts are blocked (ADR-011). The parts we need are re-implemented server-side (affinity) or as a small client service (follow behaviour).

## 4. Design

### 4.1 Authority model
- **Class A:** ownership, recruitment/dismissal, affinity per player, inventory/equipment, essential/downed state, command validation, the cell-follow teleport.
- **Class C:** following, combat, command execution (the owner's client is the host).
- **Class D:** dialogue barks/reactions (none synced at L3).

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Owner | profileId | `companionOwnerProfileId` (new, on the companion NPC) | yes | −1 |
| Follower state | enum follow/wait/dismissed/home | `companionState` (new) | yes | dismissed |
| Wait position | pos/worldOrCell | `companionWaitPos` (new) | yes | — |
| Home | FormDesc (ref/location) | `companionHome` (new) | yes | ESM placement |
| Affinity | map profileId → {value f32, threshold u8, perkGranted bool} | `affinity` (new) | yes | 0 |
| Passive-gain clock | per owner last tick ms | transient | no | — |
| Follower AVs | `FollowerState/Distance/Stance` | F08 store | `fo4Avs` | ESM |
| Bleedout | enum | F12 `lifeState == bleedout` (F12 owns the enum) | no (transient) | alive |
| Inventory/equipment | existing | `inv`, equipment | yes | ESM `CNTO` |
| Companion catalogue | base → {kind human/robot/dog, reactionTable} | `CompanionService` (from GameProfile + ESM VMAD) | no | ESM |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `CompanionCommand` (102) | C→S | `nonce`, `companionIdx`, `command` (0–9 vanilla + 16 dismiss, 17 trade, 18 equipItem, 19 enterPA, 20 exitPA, 21 recruit), `targetRefId?`, `pos?[3]`, `item?` (ItemKey) | R | user action; ≤ 4/s | new (registry) |
| `CompanionState` (112) | S→C | `companionIdx`, `nonce?`, `error?`, `state`, `pendingCommand? {command, targetRefId, pos}`, `affinity`, `threshold`, `downed`, `waitPos?` | R | on change; to the owner (and the host if different) | **new** |
| `UpdateProperty` (7) | S→C | public `companionOwner` (owner actor id, for nameplates/F31) | R | on recruit/dismiss | reused |
| `NpcAiState` (111) | both | combat/AI state of the companion (F13) | R | F13 | reused |
| `OpenContainer` (21), `TakeItemFo4`/`PutItemFo4` (70/69) | C→S | companion as the container | R | trade | reused (F06) |
| `UseItem` (91) | C→S | stimpak on the downed companion | R | F20 | reused |
| `PowerArmorTransition` (87) | S→C | companion enter/exit command to the host | R | F17 | reused |

### 4.4 Client capture (owner side)
- **Command mode:** hold activate on the owned companion → the vanilla command UI. Sinks for `TESCommandModeGiveCommandEvent` / `PlayerCommandTypeEvent` (PLAT-040) give the command type and target. Cancel the local engine command and send `CompanionCommand`.
- **Trade:** the "Trade" choice opens the container menu → `CompanionCommand{trade}`. The server replies by opening the container (F06). The equip button in trade → `equipItem`.
- **Recruit:** activation/dialogue on a recruitable companion → `CompanionCommand{recruit}` (the gamemode decides).
- **Others' companions:** the client never shows command mode for them (`companionOwner` ≠ me).

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Host = owner** (§4.6). On `HostStart` for an owned companion, the owner client:
  - applies `SetPlayerTeammate(true)` (local only), `IgnoreFriendlyHits`, `SetNotShowOnStealthMeter(true)`;
  - applies the FalloutMP follow behaviour: platform follow package on the runtime NPC (PLAT-073-style package template targeting the local player), with distance/stance from the follower AVs;
  - executes `pendingCommand` with the vanilla command natives (`SetCommandState`, package override for Move/Stay, `StartCombat(target)` for Attack, server-mediated pickup for Retrieve).
- **Non-owner clients:** an ordinary hosted NPC (F13): suppressed AI, F01/F02 mirroring. The nameplate shows "<name> (<owner>)" (F31).
- **Stream-in:** the snapshot carries `companionOwner` and downed state. A companion of an offline owner is disabled (not streamed).
- **Owner cell change** (load door, fast travel, F07/F26):
  - if `companionState == follow`, the server teleports the companion with the owner (to the owner's destination + offset) and keeps hosting with the owner;
  - with `wait`, it stays at `waitPos`;
  - dismissed companions walk home (abstract: teleported home after 60 s without listeners).
- **Correction:** a rejected command → `CompanionState{nonce, error}`. The owner client cancels any local command UI state.
- **Reconnect:** the companion is re-enabled near the owner (follow) or at `waitPos`.

### 4.6 Validation & anti-cheat
- **Hosting:** F13 election override: if the owner is subscribed to the companion's cell, the owner is the only candidate. Otherwise normal election (e.g. the owner is in another interior while the companion waits).
- **Commands:**
  1. the sender's actor profile = `companionOwnerProfileId`;
  2. companion alive (downed allows only `heal`/revive by others via F20);
  3. distance owner→companion ≤ `companions.commandRange` (default 4096 u);
  4. targets valid:
     - Attack: target in grid, alive, and hostile to the owner or the gamemode allows (PvP rule `companions.attackPlayers`, default false);
     - Move: pos within 4096 u of the companion;
     - Retrieve: item/ref exists and takeable by the owner under F06 rules (the item goes to the companion's inventory through the server);
     - Inspect/use: activatable by the owner (F07 rules);
  5. gamemode `onCompanionCommand` (blockable).
  - Accepted commands go to the host in `CompanionState.pendingCommand`.
- **Recruit:**
  - the companion is recruitable (GameProfile catalogue);
  - `companions.instancing`: `perPlayer` (default: the server creates an owner-specific copy of the base via `createActor`), or `unique` (one actor; first come; `companionOwnerProfileId` must be −1);
  - the owner is under `companions.maxPerPlayer` (default 1; `companions.dogmeatSeparateSlot` false = vanilla one-or-the-other);
  - `onCompanionRecruit` (blockable).
- **Dismiss:** owner only; `onCompanionDismiss`.
- **Trade/equip:** only the owner (or `companions.othersCanTrade`); F06/F05 validation applies (weights: companion carry weight AV; equip slot rules; ammo).
- **Essential/downed:** a companion with an owner is essential:
  - F12 puts it into downed instead of dead;
  - it gets up when the host reports combatState 0 for 10 s (F13 `NpcAiState`), restoring F12 `death.bleedoutRecoverHealthPct` (default 25 %; no separate companion setting);
  - revive by `UseItem` (stimpak) from any player within reach (`companions.othersCanRevive` true);
  - the survival rule: an unhealed companion goes home after the fight;
  - PvP damage to companions: `companions.pvpDamage` (default false: hits from non-hostile players are rejected by F11).
- **Affinity** (`AffinityService`, a server re-implementation of CompanionActorScript rules):
  - **events** come from server systems for the owner: kills of non-hostiles (F12), lockpick/hack (F24), chem use (F20), PA entry (F17), workshop build (F22), item donation, gamemode `mp.affinityEvent(actor, eventKeyword)` for dialogue;
  - each event keyword maps to a reaction per companion (the reaction table is read from the companion ref's VMAD script properties via ESPM-004; GameProfile fallback table) × size;
  - applied only if the companion is within 2500 u of the owner;
  - **passive gain** every 10 real minutes when the owner gained XP (F19 event) and is within 10 000 u: `40 − 0.033 × affinity`;
  - threshold crossing fires `onAffinityChange`; Infatuation 1000 → F19 `mp.addPerk(owner, companionPerk)` once (`perkGranted`);
  - Hatred → the companion leaves (dismissed, home).

### 4.7 Audience / visibility
- `CompanionState`: owner + host (usually the same) (S8).
- `companionOwner`, appearance, movement and animation: grid neighbours.
- The affinity of other players is never sent.

### 4.8 NPC parity
Companions *are* hosted NPCs. Their hits, movement and actions use F13's host-validated paths (S6), and their XP credit goes to the owner (F19 kill ledger). Companion PA uses F17 §4.8. NPCs cannot own companions.

### 4.9 Gamemode API & server Papyrus
- **Properties:** `mp.get/set(npc,'companionOwner')`, `mp.get(npc,'companionState')`, `mp.get(npc,'affinity')` (map), `mp.set(npc,'affinity', {profileId, value})`.
- **Natives:** `mp.recruitCompanion(actor, npc)`, `mp.dismissCompanion(npc)`, `mp.affinityEvent(actor, keyword, size?)`.
- **Settings:** `companions.maxPerPlayer`, `instancing`, `dogmeatSeparateSlot`, `commandRange`, `attackPlayers`, `othersCanTrade`, `othersCanRevive`, `pvpDamage`, `getUpHealthPct`.
- **Events:** `onCompanionRecruit`, `onCompanionDismiss`, `onCompanionCommand` (blockable); `onAffinityChange` (observe).
- **Server Papyrus:** vanilla follower quests are blocked. `SetPlayerTeammate`-like natives take an explicit owner: `Actor.SetPlayerTeammate(abTeammate)` called by a server script on a companion uses the script's context actor as the owner, or no-op (documented). `OnCommandModeGiveCommand` is fired from accepted commands (S16).

### 4.10 Edge cases & failure modes
- **Owner disconnects:** the companion is disabled at its current position (persisted); re-enabled on the owner's login.
- **Owner dies:** the companion stays; on owner respawn far away, follow teleports it on the next cell change (or immediately if `companions.teleportOnRespawn`).
- **`unique` instancing contention:** two players recruit at once → the first message wins.
- **Host ≠ owner** (owner in another cell): commands are still validated against the owner. Pending commands are delivered to the current host.
- **Server restart:** the state is persisted. The passive-gain clock resets (acceptable).

### 4.11 Performance budget
- At most `maxPerPlayer` extra hosted NPCs per player (≤ 1,000 extra across 1,000 players, counted in the 2,000-NPC budget) inside F13's budget.
- `CompanionState` ≤ 48 B.
- Affinity evaluation ≤ 5 µs per event.

## 5. Engine / platform work required
- `TESCommandMode*Event` sinks and command cancellation (PLAT-040).
- A follow-package template and a command-execution helper for runtime NPCs (extends PLAT-073).
- Local `SetPlayerTeammate`/`IgnoreFriendlyHits` on the host.

## 6. Tests
- `L-unit` (`[F21]`, `[Companion]`):
  - `CompanionCommand`/`CompanionState` round trips;
  - recruit sets the owner (`perPlayer` creates a copy; `unique` rejects a second owner);
  - `maxPerPlayer` enforced;
  - a command from a non-owner rejected with `CompanionState{error}`;
  - command out of range rejected; Attack on a non-hostile player rejected unless the setting allows;
  - accepted command forwarded to the host only;
  - host forced to the owner while subscribed;
  - follow teleport on the owner's cell change; wait stays;
  - affinity reaction only within 2500 u; passive gain formula; threshold 1000 grants the perk once;
  - downed instead of dead; get up after combat state 0 for 10 s; revive by a stimpak from another player;
  - trade by a non-owner rejected;
  - persistence round trip (owner, state, affinity map);
  - gamemode veto of a command;
  - snapshot carries `companionOwner`.
- `L-int`: owner bot commands its companion; a second bot's command is rejected; owner disconnect disables the companion.
- `G-self`: follow package and command execution on a runtime companion NPC.
- `G-manual`: two players, each with a companion; command, trade, revive the other's companion, load door follow.

## 7. Tasks
- [ ] **F21-T01** Messages `CompanionCommand` (102) and `CompanionState` (112), `companionOwner` property — S — Depends: NET-002 — Verify: L-unit — Files: falloutmp-server/cpp/messages/{CompanionCommandMessage,CompanionStateMessage}.h, Messages.h; falloutmp-client/src/services/messages/
- [ ] **F21-T02** `CompanionService`: catalogue, recruit/dismiss, instancing, limits, persistence fields — M — Depends: F21-T01, F13-T01 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/fo4/CompanionService.{h,cpp}, MpChangeForms.{h,cpp}, game_profile/fallout4/companions.json; unit/CompanionTest.cpp
- [ ] **F21-T03** Owner-forced hosting in `NpcHostingService`; cell-follow teleports; offline disable — S — Depends: F13-T05, F21-T02 — Verify: L-unit
- [ ] **F21-T04** Command validation and forwarding (`pendingCommand`), Retrieve via F06 — M — Depends: F21-T02, F06, F07 — Verify: L-unit
- [ ] **F21-T05** Client: command-mode capture/cancel, follow package + command execution on the host, nameplate owner tag — L — Depends: F21-T01, PLAT-040, PLAT-073, F13-T08 — Verify: G-self, G-manual — Files: falloutmp-client/src/services/services/companionService.ts, fallout4-platform/src/platform_fo4/CompanionApi.cpp
- [ ] **F21-T06** `AffinityService` + reaction tables from VMAD (ESPM-004) + passive gain + perk grant — M — Depends: F21-T02, F19-T09, ESPM-004 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/fo4/AffinityService.{h,cpp}
- [ ] **F21-T07** Trade and equip on companions (F06/F05 rules, owner-only) — S — Depends: F21-T04, F05, F06 — Verify: L-unit
- [ ] **F21-T08** Essential/downed/get-up/revive integration (F12, F20) — S — Depends: F21-T02, F12, F20-T01 — Verify: L-unit
- [ ] **F21-T09** Gamemode/Papyrus surface (§4.9); default-gamemode recruitment rules — M — Depends: F21-T04 — Verify: L-int — Files: falloutmp-gamemode/src/systems/companions.ts, falloutmp-server/ts typings
- [ ] **F21-T10** `G-manual` script and sign-off — S — Depends: F21-T05 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F21-companions.md

## 8. Open questions & risks
- Can follow behaviour work without the `Followers` quest alias packages? Fallback: drive following from the host service with `PathToReference`-style natives each second.
- Can affinity reaction tables be extracted reliably from VMAD properties of CompanionActorScript (struct arrays, ESPM-004)? Fallback: a hand-made GameProfile table.
- `perPlayer` instancing means several Pipers in one world. That fits a sandbox but not lore; it is the server owner's choice.
- Companion dialogue (recruit, affinity scenes) is out of scope at L3 (F27).
