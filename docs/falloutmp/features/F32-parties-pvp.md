# F32 — Parties, Teams & PvP Rules

| Field | Value |
|---|---|
| Tier | T1 |
| Target level | L4 |
| SkyMP analogue | None in core; SweetPie gamemode implements teams in JS. SkyMP level L0 |
| Milestone | M8 (party lifecycle, friendly fire, hostility) / M9 (PvP zones, kill-XP sharing, party quest scope) |
| Workstreams | SRV, NET, GM, FRONT, CLI |
| Depends on | F11-T02 (damage pipeline), F13-T05 (hostility), F19-T03 (XP), SRV-060 (profile record), FRONT-006 |
| References | reference/skymp-sync-inventory.md §1.11–1.12 (properties, gamemode events); features F10, F11, F13, F19, F22, F26, F27, F29 (consumers) |

## 1. Summary
Players can form a party. Party membership decides friendly fire, hostility of hosted NPCs and companions toward members, shared kill XP, party markers on the map and compass, and the `party:<id>` scope for gamemode quests. PvP is governed by one server rule set: zones (safe, open, flagged-only), a per-player PvP flag with a cooldown, and friendly-fire resolution. Several specs already assume this entity (review finding C5); this spec owns it.

## 2. Vanilla Fallout 4 behaviour
- There is no party or team concept. Companions follow the player through the follower system (F21). Hostility is faction-based (F13).
- Friendly fire against companions exists in vanilla (they get annoyed, not hostile). Settlers turn hostile after repeated hits.

## 3. SkyMP baseline
- Core SkyMP has no parties. The private SweetPie gamemode handles teams purely in JS with custom properties.
- Reuse: the custom-property and event-source plumbing (F31) for party UI; `private.*` properties for membership. FalloutMP promotes parties to a server-side record because combat (F11), hostility (F13) and XP (F19) need it in C++ hot paths.

## 4. Design

### 4.1 Authority model
Class A. The server owns party membership, leadership, invitations, PvP flags and zone rules. Clients only send requests.

### 4.2 Server state & persistence
| State | Type | Where | Persisted | Default |
|---|---|---|---|---|
| Party record `{partyId, leaderProfileId, members[profileId], createdAt, settings{lootMode, xpShare}}` | record | ADR-010 record `parties` (SRV-060 storage) | yes | — |
| Invitations `{partyId, toProfileId, expiresAt}` | map | server memory | no | — |
| Player PvP flag + cooldown | bool + ts | profile record (SRV-060) | yes | `pvp.defaultFlag` |
| PvP zones `{shape, mode}` | list | server settings / gamemode | settings | none |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `PartyAction` (120) | C→S | `nonce`, `op` (create, invite, accept, decline, leave, kick, promote, setLootMode, setPvpFlag), `targetProfileId?`, `value?` | R | on action | **new** |
| `PartyAction` (120) | S→C | `nonce?`, `kind` (state, invite, result), `party{partyId, leader, members[{profileId, displayName, idx?, online}]}`, `error?` | R | on change; full state to members on join | **new** |
| `UpdateProperty` (7) | S→C | `partyId` (public), `pvpFlag` (public) on actors | R | on change | reused |
| `MapDiscovery` (103) | S→C | party member positions for the map/compass (F26) | R | 1 Hz to party members | reused |

### 4.4 Client capture
- Party UI in the front (FRONT-006): invite by nameplate/target or `/party invite <name>` (F30). Requests go through `PartyAction`.
- PvP flag toggle in the MCM page or `/pvp` command.

### 4.5 Apply
- Members get the full party state on join and on every change. Non-members only see the public `partyId`/`pvpFlag` properties for nameplate colouring (F31).
- F11 reads `partyId` and the friendly-fire rule in step 0 (immunity table). F10 explosions use the same rule (one setting, `pvp.friendlyFire`).
- F13 hostility: hosted NPCs and companions owned by a member are not hostile to other members; the hostility matrix is pushed to hosts via `NpcAiState` (111).
- F19 kill XP: the ledger (F11) credits the killer; `xpShare` splits XP among members within `pvp.xpShareRadius`.
- F27 quest scope `party:<id>` resolves through the party record.
- Late join/reconnect: party state is restored from the record; a member offline for more than `pvp.partyOfflineTimeoutMin` is dropped.

### 4.6 Validation & anti-cheat
- Only the leader can invite, kick, promote, change settings. Invitations expire after 60 s. Max party size `pvp.maxPartySize` (default 8).
- PvP flag changes have a cooldown (`pvp.flagCooldownSec`, default 300) and cannot be turned off while in combat (F13 combat state) or within `pvp.combatTagSec` of dealing/receiving player damage.
- Zone rules evaluated server-side from the rewound position (F01 history): `safe` rejects all player damage (`result=immune`), `open` allows, `flagged` requires both players flagged.
- Every reject returns `PartyAction{kind: result, error}` (S12).

### 4.7 Audience
Party state to members only; `partyId`/`pvpFlag` properties to grid neighbours.

### 4.8 NPC parity
Companions and hosted NPCs inherit the owner's party for hostility. NPCs are never party members.

### 4.9 Gamemode API & Papyrus
- `mp.get(actor, 'partyId')`, `mp.get(actor, 'pvpFlag')`; `mp.createParty`, `mp.setPartyMembers` for gamemode-driven teams (arena modes).
- Events: `onPartyChange(partyId, op, actors)`, `onPvpFlagChange(actor, flag)` (blockable), `onPvpDamageBlocked(attacker, target, reason)` (observe).
- Server Papyrus: `FalloutMP.GetPartyId(Actor)`, `FalloutMP.IsSameParty(Actor, Actor)` natives (PVM-015).

### 4.10 Edge cases
- Leader disconnects → promote the longest-standing online member; empty party → deleted.
- Party member in a different worldspace: markers show "elsewhere".
- Server restart: records reload; invitations are lost.

### 4.11 Performance budget
Party state messages are rare; member position pushes ≤ 8 × 1 Hz × ~16 B.

## 5. Engine / platform work required
None beyond F31 nameplates and FRONT-006 UI.

## 6. Tests
- `L-unit`: create/invite/accept/leave/kick/promote; leader-only ops rejected; size cap; flag cooldown; combat-tag refusal; zone evaluation; friendly-fire immunity via F11 step 0; hostility matrix push; XP share split; persistence round trip; late-join state.
- `L-int`: two bots form a party, one shoots the other → immune; leave → damage applies.
- `G-manual`: party UI, markers, PvP flag flow.

## 7. Tasks
- [ ] **F32-T01** Party record + `PartyAction` (120) message, C++ struct + TS mirror, registration, round trip — S — Depends: NET-002, SRV-060 — Verify: L-unit — Files: skymp5-server/cpp/messages/PartyActionMessage.h, falloutmp-client/src/services/messages/partyActionMessage.ts
- [ ] **F32-T02** `PartyService`: lifecycle ops, invitations, leader rules, size cap, persistence, properties `partyId`/`pvpFlag` — M — Depends: F32-T01 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/PartyService.{h,cpp}, unit/PartyServiceTest.cpp
  - Accept: all §6 unit cases pass; state restored after restart.
- [ ] **F32-T03** PvP rules: flag with cooldown and combat tag, zones (settings + gamemode polygons via `geo`), friendly-fire setting consumed by F11 step 0 and F10 — M — Depends: F32-T02, F11-T02 — Verify: L-unit, L-int
- [ ] **F32-T04** Hostility integration: party-aware `IsHostile` in F13 and companion hostility in F21; matrix push via `NpcAiState` — S — Depends: F32-T02, F13-T05 — Verify: L-unit
- [ ] **F32-T05** Kill-XP sharing (F19) and party quest scope (F27) hooks — S — Depends: F32-T02, F19-T03 — Verify: L-unit
- [ ] **F32-T06** Map/compass party markers (F26 `MapDiscovery` push) and nameplate colouring (F31) — S — Depends: F32-T02, F26-T02 — Verify: G-manual
- [ ] **F32-T07** Client/front party UI (FRONT-006), `/party` and `/pvp` commands (F30), MCM toggle (CLI-080) — M — Depends: F32-T01, FRONT-006 — Verify: G-manual
- [ ] **F32-T08** Gamemode API (`mp.createParty`, events) + Papyrus natives + docs (DOCS-003) — S — Depends: F32-T02 — Verify: L-int
- [ ] **F32-T09** `G-manual` script and sign-off — S — Depends: F32-T07 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F32-parties-pvp.md

## 8. Open questions & risks
- Default PvP mode for public servers (open vs flagged) is a gamemode/policy decision (Q-06).
- Team-based modes (arena, capture) are gamemode content on top of `mp.setPartyMembers`.
