# Fxx — <Feature name>

| Field | Value |
|---|---|
| Tier | T0 / T1 / T2 (see 00-vision-scope.md §5) |
| Target level | L0–L4 (see 01-sync-standard.md §3) |
| SkyMP analogue | <feature> — SkyMP level Lx — code: `path:line` |
| Milestone | Mx |
| Workstreams | SRV, CLI, PLAT, ESPM, PVM, NET, FRONT, GM… |
| Depends on | Fyy, PLAT-0nn, … |
| References | reference/<doc>.md §…, … |

## 1. Summary
What the player experiences and what must be the same for everyone, in 3–6 sentences.

## 2. Vanilla Fallout 4 behaviour (engine facts that matter for sync)
- Records/fields, actor values, keywords, globals, GMSTs
- Menus, Papyrus natives/events, engine events/hooks (CommonLibF4 types)
- Single-player assumptions that break in multiplayer

## 3. SkyMP baseline
How SkyMP handles the analogous feature (code pointers), and what is reused, adapted or replaced.

## 4. Design

### 4.1 Authority model
Server-authoritative / owner-authoritative-with-validation / host-simulated / local-cosmetic. Say who decides what.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|

### 4.4 Client capture (owner side)
Hooks/events/polls used to detect the local action, with filters.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
How the state is shown on other clients, and how it is restored when an actor streams in, respawns, or the owner reconnects.

### 4.6 Validation & anti-cheat
Checks the server performs, and the corrective action for each.

### 4.7 Audience / visibility
Who receives which messages (owner, neighbours via grid, everyone).

### 4.8 NPC parity
How the feature works for hosted NPCs (host sends on the NPC's behalf; the server validates the host relationship).

### 4.9 Gamemode API & server Papyrus
- `mp` properties (read/write), gamemode events (blockable?), `ctx.sp` usage
- Papyrus events fired on the server, natives needed

### 4.10 Edge cases & failure modes
Disconnect mid-action, host migration, concurrent access, cell/worldspace transitions, server restart, hot reload.

### 4.11 Performance budget
Message sizes/rates, server CPU per event, memory per entity.

## 5. Engine / platform work required
New natives in the FO4 `TESModPlatform`, hooks, RE needs (with reference links).

## 6. Tests
- `L-unit`: …
- `L-int`: …
- `L-fixture` / `L-ts`: …
- `G-self` checks: …
- `G-manual` scenario: …

## 7. Tasks
- [ ] **Fxx-T01** <title> — Size S/M/L — Depends: … — Verify: L-unit/W-ci/G-… — Files: …
  - Accept: …

## 8. Open questions & risks
- …
