# F12 — Death, Respawn, Bleedout & Downed State

| Field | Value |
|---|---|
| Tier | T0 |
| Target level | L4 (SkyMP L4; plus `isDead` broadcast to listeners (I14), essential/bleedout NPCs, optional downed/revive) |
| SkyMP analogue | `MpActor::Kill` → `SendAndSetDeathState` → `DeathStateContainer` (18) → `DeathEvent` `onDeath` → `RespawnWithDelay(spawnDelay = 25 s)` → `RespawnEvent` `onRespawn` (`MpActor.cpp:1003-1059, 1327-1466`); client `deathService.ts`. SkyMP level L4 (reference/skymp-sync-inventory.md §1.17) |
| Milestone | M7 |
| Workstreams | SRV, NET, CLI, PLAT, GM, PVM |
| Depends on | F08 (Health/limbs/Rads), F11 (kill attribution, `DamageApplied.kill`), F06 (corpse looting), F14 (death items, NPC respawn), F20 (stimpak `UseItem`, effects reset), F00 (spawn points), F13 (hosting), SRV-080 (corpse cleanup) |
| References | reference/fo4-systems-combat-character.md §7 (health, healing, death, respawn), §13.1 (companions essential/downed), §1.1 (SkyMP death row), §1.5 (Papyrus events); reference/skymp-sync-inventory.md §1.17, §2 "Death & respawn", §3.2 I6/I14; reference/fo4-animation-sync.md §3.4 (death/bleedout actions); reference/prior-art.md §3.1.7 (HP funnel clamp, FO4_Wrld abandoned resurrect), §5.3 Adopt 6 |

## 1. Summary
When an actor's Health reaches 0 on the server it dies, unless a rule turns death into bleedout (essential NPCs, companions) or a downed state (optional, for players). Everyone in range sees the death at once, including late joiners, and every client shows the same dismemberment. Corpses are server-owned containers that can be looted (F06). Players respawn after a configurable delay at their respawn point with Health and limbs restored; NPCs stay dead until their cell resets (F14). A downed player or companion can be revived with a stimpak by another player. Death state survives disconnects and server restarts.

## 2. Vanilla Fallout 4 behaviour
- Player death means game over and a save reload; there is no respawn (fo4-systems §7.4).
- Essential NPCs enter bleedout instead of dying (`SetEssential`, `SetProtected`, `SetNoBleedoutRecovery`, `AllowBleedoutDialogue`, `IsBleedingOut`, `OnEnterBleedout`); protected actors can only be killed by the player [src: f4se Actor.psc:53,337,681,714,737,924]. Active companions are essential, get up after combat (Survival: must be healed) (fo4-systems §7.1, §13.1).
- Deferred kill: `StartDeferredKill`/`EndDeferredKill`, `OnDeferredKill`; `ResetHealthAndLimbs`, `Resurrect`, `Kill`, `KillSilent`, `KillEssential` [src: f4se Actor.psc:111,455,458,541,544,770,900].
- Events: `TESDeathEvent{actorDying, actorKiller}`, `TESDeferredKillEvent`, `Bleedout::Event`, `TESEnterBleedoutEvent`; Papyrus `OnDying(akKiller)`, `OnDeath(akKiller)` on the victim, `OnKill(akVictim)` on the killer [src: f4se Actor.psc:904,920,952] (fo4-systems §1.5, §7.2).
- Life states and actions: `ACTOR_LIFE_STATE` (Alive, Dying, Dead, Unconscious, EssentialDown, Bleedout…), `ActionDeath` 0x489ED, `ActionDeathWait` 0x5DD59, `ActionDeferredKill` 0xFFD26, `ActionBleedoutStart/Stop` 0x13EC9/0x13ECA (fo4-animation-sync §3.4).
- Revive: a downed companion is revived with a stimpak; `OnPlayerHealTeammate` [src: f4se Actor.psc:996]; companions get a `playerCanStimpak`-style keyword while active (fo4-systems §7.1).
- Death items: NPC_ death item (LVLI) added at death; corpses are looted as containers; dismemberment from BPTD chances, `Actor.Dismember`, `SetCriticalStage` (fo4-systems §4.1).
- Engine: the `Health ≤ 0 → Kill` check is duplicated in several per-frame checkers; **clamp Health ≥ 1 at the HP funnel instead of gating `Kill`** (prior-art §3.1.7). FO4_Wrld abandoned `Resurrect` on the player (AI process lost its middle-high part; kill-cam is a VATS mode; radiation leaked) and used a checkpoint reload (prior-art §3.1.7).
- Single-player assumptions: game over on death; save reload; companion essential logic in quest scripts; local gore RNG.

## 3. SkyMP baseline
- `SetPercentage(Health ≤ 0)` → `Kill(killer)` → `SetIsDead(true)` → `EditChangeForm(isDead)` → `SendAndSetDeathState(true)` → `DeathStateContainer{tTeleport?, tChangeValues?, tIsDead?}` to `GetActorToSendTo` (owner/hoster **only**: neighbours infer death from movement, **I14**) → `DeathEvent` → on success `RespawnWithDelay` → `Respawn()` (`onRespawn`, teleport to `spawnPoint`, `respawnPercentages`, `UpdateProperty isDead=false`). Reconnect/load while dead → `RespawnWithDelay` (`MpActor.cpp:614-618`).
- Persisted: `isDead`, `spawnPoint`, `spawnDelay`, `health/magicka/staminaRespawnPercentage` [src: MpChangeForms.h:85-109].
- NPC with a death item: inventory reset to base on respawn, `SweetCantDrop` kept.
- Client: local player in `startDeferredKill`; `KillMove*` and `staggerStart` blocked on remote actors (`deathService.ts:24-58`); `DeathStateContainer` → kill/ragdoll, resurrect, teleport, AV restore (`deathService.ts:77-102`).
- UpdateProperty for ESM-id actors is dropped client-side (I6, CLI-031) — must be fixed for `isDead` on NPCs.
- Reuse: the whole flow, persistence fields, events. Change: FO4 twin message with the AV map, listener broadcast, NPC respawn policy, bleedout/downed.

## 4. Design

### 4.1 Authority model
Class A: life state (`alive`, `downed`, `bleedout`, `dead`), killer, respawn timer, corpse inventory. The owner/host plays animations only.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| `isDead` | bool | `MpChangeFormREFR.isDead` | yes (existing) | false |
| Life state `downed`/`bleedout` + deadline | enum + ts | `MpActor::lifeState` (new) | no (on restart: downed → dead, bleedout → alive at 1 HP) | alive |
| Downed health pool | float | `MpActor::downedPool` | no | `death.downedHealth` |
| Spawn point / delay | LocationalData, float | `spawnPoint`, `spawnDelay` | yes (existing) | GameProfile start point (F00), 25 s |
| Respawn AV percentages | float | `healthRespawnPercentage` (existing; AP/limbs = 100 %) | yes | 1.0 |
| Killer / damage ledger | ids | `MpActor::damageLedger` (F11) | no | — |
| Death bag ref (player drop policy) | FF container | F06 container + `deathBag {ownerProfileId, expiresAt}` | yes | none |
| Essential / protected | flags | NPC_ ACBS + gamemode override `MpActor::essentialOverride` | override yes | ESM |
| Corpse remove time | datetime | SRV-080 cleanup timer | yes | F14 rules |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `DeathStateContainerFo4` (73) | S→C | `idx`, `isDead` bool, `reason` u8 (death, respawn, revive, bleedoutEnter, bleedoutExit, downedEnter), `killerIdx`, `teleport? {pos, rot, worldOrCell}`, `avs[]` (F08 entry layout) | R | on death/respawn/revive | registry 73 |
| `UpdateProperty` (7) | S→C | `isDead`, `lifeState` (`downed`/`bleedout`/none) to all listeners | R | on change | reused (I14) |
| `DamageApplied` (83) | S→C | `flags.kill`, `flags.downed`, `goreSeed` (fx subset to neighbours) | R | on lethal hit | registry 83 (F11) |
| `UseItem` (91) | C→S | stimpak on a downed target (`targetIdx`) | R | event | registry 91 (F20) |
| `Teleport` (20) | S→C | respawn move | R | on respawn | reused |
| `CreateActorFo4` (64) | S→C | `isDead`, `lifeState` in the snapshot | R | subscribe | registry 64 |

### 4.4 Client capture (owner side)
None for death decisions. The client keeps the local player (and hosted NPCs) out of engine death: HP funnel clamp at 1 (F12-T02) plus `StartDeferredKill` as a second guard. The owner reports nothing but the normal movement/animation stream.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Owner, death:** `DeathStateContainerFo4 isDead=true` → `EndDeferredKill` + `Kill(killer)` on the local actor (ragdoll/death anim), disable input, show the respawn timer UI. Gore from the `DamageApplied` seed.
- **Remote actors:** `UpdateProperty isDead=true` (long-id routing fixed by CLI-031) → `Kill` the ghost with loot disabled locally (looting goes through server containers, F06), `ActionDeath`/ragdoll, dismember with the seed.
- **Late joiner:** `CreateActorFo4 isDead=true` → spawn dead (`KillSilent` + `ActionDeathWait`), at the server corpse position.
- **Bleedout/downed:** `lifeState` property → the owner/host plays `ActionBleedoutStart`; ghosts mirror through F02 relay; clearing → `ActionBleedoutStop`.
- **Respawn (player):** `DeathStateContainerFo4 reason=respawn` with teleport + AV map → primary path `Resurrect()` + `ResetHealthAndLimbs()` + `MoveTo`, then AVs; fallback (setting `death.respawnMode = "reload"`) re-runs the F33 entry at the spawn point. The choice is made by the F12-T05 prototype.
- **Reconnect while dead:** SkyMP behaviour kept: `RespawnWithDelay` on load/login.

### 4.6 Validation & anti-cheat
1. Only the server kills: Health 0 from F11 damage, F20 effects (Rads ≥ 1000), the owner's *own* fall damage reported as an AV decrease (F08), `mp.set(isDead)`, Papyrus `Kill`. A **host can never kill a hosted NPC through the AV path** (F08 §4.8 / F08-T15 clamps NPC Health to ≥ 1 and Rads to ≤ 999).
2. Death resolution order at Health ≤ 0: protected/essential NPC (ESM flag or override, F21 companions) → `bleedout` (Health 1, recovers after `death.bleedoutRecoverySeconds` out of combat unless `NoBleedoutRecovery`); player with `death.downedSeconds > 0` (and PvP/PvE scope `death.downedScope`) → `downed` with `downedPool`; else **dead**.
3. Damage to a downed actor reduces `downedPool`; 0 → dead. Downed deadline → dead.
4. Revive (`UseItem` with a stimpak on a downed/bleeding target, F20): healer alive and not downed, owns the item, distance ≤ `death.reviveDistance` (200 u, rewound positions), target downed → consume item (S13), clear downed, apply the stimpak effect, `onRevive` (blockable → `SetInventoryFo4` correction). Wrong state → correction.
5. `onDeath` gamemode veto (SkyMP `DeathEvent`) → actor stays at 1 HP alive, correction `ChangeValuesAv`.
6. Respawn timer is server-only; the client cannot request an early respawn unless the gamemode exposes it (`mp.respawn(actorId)`).
7. Ownership checks on every idx-bearing message (S6).

### 4.7 Audience / visibility
`DeathStateContainerFo4` → owner/host (`GetActorToSendTo`, S14). `isDead`/`lifeState` `UpdateProperty` → all grid listeners on death **and** respawn (I14). Kill feed is a gamemode concern (`onDeath` + chat).

### 4.8 NPC parity
- NPC death: same decision path; the host plays the death; the host's last movement sample sets the corpse position, after which movement for a dead NPC is ignored except ragdoll settle for 3 s.
- **NPCs do not use `RespawnWithDelay`** in the FO4 profile: they stay dead (lootable corpse) until F14's cell reset (SRV-080) respawns them from ESM. Gamemode-spawned NPCs (`mp.createActor`) keep SkyMP semantics (`spawnDelay` respected) unless `npcRespawn.mode (owned by F14) = "cellReset"`.
- Death items: at death the server evaluates the NPC_ death item LVLI (F14) into the corpse inventory, then the corpse is a container for F06.
- Essential NPCs and companions (F21) use bleedout; companion revive uses the same `UseItem` path.

### 4.9 Gamemode API & server Papyrus
- Properties: `isDead` (rw, existing), `lifeState` (r), `spawnPoint`, `spawnDelay` (rw, existing), `essential` (rw override).
- Events: `onDeath(actorId, killerId)` (blockable, existing), `onRespawn(actorId)` (existing), `onDowned(actorId, attackerId)` (blockable → dead instead), `onRevive(targetId, healerId)` (blockable), `onBleedout(actorId)`.
- Settings: `death.downedSeconds` (0 = off), `death.downedHealth` (50), `death.downedScope` (`pve|pvp|all`), `death.reviveDistance` (200), `death.bleedoutRecoverySeconds` (10), `death.playerDrop` (`none|bag|all`, default `none`), `death.bagLifetimeMinutes` (30), `death.respawnMode` (`resurrect|reload`), `respawn.clearRads` (true), `respawn.clearEffects` (`negative|all|none`, default `negative`), `npcRespawn.mode (owned by F14)` (`cellReset|delay`).
- Papyrus: `OnDying`, `OnDeath(akKiller)` on the victim; `OnKill(akVictim)` on the killer; `OnEnterBleedout`; `OnPlayerHealTeammate` on the healer; natives `Kill`, `KillSilent`, `KillEssential`, `Resurrect`, `ResetHealthAndLimbs`, `SetEssential`, `SetProtected`, `IsBleedingOut`, `IsDead` (PVM-014).

### 4.10 Edge cases & failure modes
- Death in power armor: F17 exits the frame at the corpse position first. Death in furniture: occupancy released (F07). Death while hosting NPCs: hosting unaffected (hosts are users, not actors).
- Disconnect while dead or downed: dead persists (respawn on login); downed becomes dead.
- Killed twice in one tick: the second hit gets `result=ignoredDead`.
- Death bag: items moved atomically from the player to the bag (S13, F06 rules); the bag despawns after its lifetime with its contents.
- Radiation death (Rads ≥ 1000): `killerIdx` = 0, `reason=death`; `respawn.clearRads` resets Rads.
- Server restart during the respawn delay: `isDead` is persisted, `RespawnWithDelay` re-armed on load (SkyMP).

### 4.11 Performance budget
Death/respawn are rare events: one `DeathStateContainerFo4` (≤ 400 B with AV map) to the owner, one `UpdateProperty` per listener. No per-tick cost except downed/bleedout deadlines (timer wheel).

## 5. Engine / platform work required
- HP funnel detour (prior-art C8, AE ID by RE): clamp Health ≥ 1 for the local player and hosted NPCs; shared with F11.
- Remote-death helpers: `killGhost(ref, killer, ragdoll)`, `spawnDead(ref)`, `dismember(ref, part, seed)`; disable local looting of ghost corpses (activation filter, F07).
- Respawn prototype: `Resurrect` + `ResetHealthAndLimbs` on the local player (checks the FO4_Wrld pitfalls), else the template reload path from F00.

## 6. Tests
- `L-unit` (`unit/Fo4DeathTest.cpp` `[F12]`, existing `[Respawn]`):
  - Health 0 → dead, `DeathStateContainerFo4` to the owner, `isDead` `UpdateProperty` to **every listener** (I14), late joiner `CreateActorFo4.isDead`;
  - respawn after `spawnDelay`: teleport to spawn point, Health/AP/limbs 100 %, Rads cleared per setting, `isDead=false` broadcast;
  - essential NPC → bleedout at 1 HP, recovers; protected NPC killed only by a player;
  - downed: enter, pool damage → dead, timeout → dead, revive with stimpak accepted (item consumed), revive from 400 u rejected with correction, revive without the item rejected;
  - NPC death: death item evaluated into the corpse, no `RespawnWithDelay` in `cellReset` mode;
  - `onDeath` veto keeps the actor alive with a correction; `onRevive` veto;
  - Papyrus `OnDeath`/`OnDying`/`OnKill` observed as `onPapyrusEvent:*`;
  - persistence: dead player reloaded → respawn re-armed; death bag persists and expires;
  - wrong host for an NPC message → `HostStop`.
- `L-int`: bot A kills bot B; bot C (neighbour) receives `isDead`; B respawns and C sees `isDead=false`.
- `L-ts`: `deathService` ordering (death → respawn → AV apply), dead spawn for late join.
- `G-self`: HP funnel clamp holds the player at 1 HP under engine damage; `Resurrect` respawn path vs reload path (prototype report).
- `G-manual`: PvP kill, respawn, loot the death bag; companion downed and revived by another player; dismember seen identically on two clients.

## 7. Tasks
- [ ] **F12-T01** FO4 death pipeline in `MpActor` (life-state machine alive/bleedout/downed/dead, kill reasons, ledger hand-off to F19 kill XP) — M — Depends: F08-T01, F11-T07 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/MpActor.{h,cpp}, fo4/Fo4LifeState.{h,cpp}; unit/Fo4DeathTest.cpp
- [ ] **F12-T02** Client HP funnel clamp + deferred-kill guard for the local player and hosted NPCs — L — Depends: PLAT-002, PLAT-040 — Verify: W-ci, G-self — Files: fallout4-platform/src/.../hooks/HealthFunnelHook.cpp
  - Accept: the self-test survives 10× lethal engine damage at 1 HP until the server kills.
- [ ] **F12-T03** `isDead`/`lifeState` `UpdateProperty` to all listeners on death and respawn (I14), client long-id routing via CLI-031 — S — Depends: F12-T01, CLI-031 — Verify: L-unit, L-ts
  - Accept: neighbour receives `isDead=true` without any movement packet.
- [ ] **F12-T04** `DeathStateContainerFo4` message (C++ + TS) with the F08 AV map — S — Depends: NET-002, F08-T03 — Verify: L-unit — Files: falloutmp-server/cpp/messages/DeathStateContainerFo4Message.h; falloutmp-client/src/services/messages/
- [ ] **F12-T05** Player respawn: spawn points/delay (existing fields), AV restore, `respawn.clearRads`/`clearEffects`, and the client respawn prototype (`Resurrect` vs template reload, `death.respawnMode`) — M — Depends: F12-T04, F33-T08, F20 (effects) — Verify: L-unit, G-self
  - Accept: unit tests pass; prototype result recorded in STATUS.md and this spec updated.
- [ ] **F12-T06** Essential/protected NPCs and bleedout recovery — S — Depends: F12-T01, ESPM-006 — Verify: L-unit
- [ ] **F12-T07** Downed state and revive via `UseItem` stimpak (distance, item, timeout, pool) — M — Depends: F12-T01, F20 (`UseItem`) — Verify: L-unit, L-int
- [ ] **F12-T08** Death items and loot hand-off: NPC death item LVLI (F14), corpse as F06 container, player drop policy and death bag — M — Depends: F12-T01, F06, F14 — Verify: L-unit
- [ ] **F12-T09** NPC death policy: no delay respawn in `cellReset` mode, corpse position from host, corpse cleanup (SRV-080) — S — Depends: F12-T01, SRV-080 — Verify: L-unit
- [ ] **F12-T10** Client `deathService.ts` FO4 fork: owner death/respawn, ghost kill, dead spawn on stream-in, bleedout anims, gore seed — M — Depends: F12-T03, F12-T04, CLI-050 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/deathService.ts
- [ ] **F12-T11** Gamemode events/properties/settings (`onDowned`, `onRevive`, `onBleedout`, `lifeState`), Papyrus `OnDying`/`OnDeath`/`OnKill`/`OnEnterBleedout`/`OnPlayerHealTeammate` and natives; docs — M — Depends: F12-T07, PVM-014 — Verify: L-unit, L-int
- [ ] **F12-T12** `G-manual` death/respawn/revive script — S — Depends: F12-T10 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F12-death-respawn.md

## 8. Open questions & risks
- **Life-state vocabulary (owned here, review finding M6):** `lifeState ∈ {alive, downed, bleedout, dead}`. `bleedout` = essential NPCs and companions (server-invulnerable, recover after `death.bleedoutRecoverySeconds` with `death.bleedoutRecoverHealthPct`, default 25%). `downed` = the optional player state (`death.downedSeconds`). Protected NPCs are mortal to players only (`npc.protectedPolicy`, F13). F21 uses `lifeState == bleedout`, not a separate `isDowned`.
- `Resurrect` on the FO4 player may be unusable (FO4_Wrld); the reload fallback costs a loading screen per death.
- Should PvP deaths drop items by default? Default `none` (SkyMP parity); gamemode decides (Q-06 PvP/PvE focus).
- Corpse ragdoll positions differ per client; only the server position is authoritative for looting distance.
