# F26 — Map Markers, Discovery & Fast Travel

| Field | Value |
|---|---|
| Tier | T1 (per-player discovery, server fast travel), T2 (vertibird signal travel) |
| Target level | L3 (SkyMP L0: fast travel disabled, no marker state) |
| SkyMP analogue | `DisableFastTravelService` calls `Game.enableFastTravel(false)` every frame; map discovery is local to the (template) save. SkyMP level L0 — code: `skymp5-client/src/services/services/disableFastTravelService.ts:9-11` |
| Milestone | M9 (discovery + fast travel), M12 (vertibird) |
| Workstreams | SRV, CLI, PLAT, ESPM, NET, GM |
| Depends on | SRV-060 (per-player profile record), ESPM-005 (REFR), F01 (`Teleport`, server-initiated teleports), F08-T11 (encumbrance), F11 (combat state), F13 (hosted NPC combat targets), F19-T05 (discovery XP), F21 (companion carry), CLI-021, PLAT-040 |
| References | reference/fo4-systems-world-economy.md §2 S13, S11 (`PipboyMapData`), S20 ("HUD markers & compass"), §3.2, §3.3 (`PlayerProfile`), §3.7, §5 item 4; reference/papyrus-api-map.md §1.2 InputEnableLayer, §5.6; reference/skymp-sync-inventory.md §1.15 row 21; reference/prior-art.md §5.3 |

## 1. Summary
Each player has their own set of discovered map locations. It is stored on the server, survives reconnects, and appears on the Pip-Boy map and the compass. Discovery happens the vanilla way, by walking near a location; the server checks that the player really was there. Fast travel is a server rule. Off, discovered-only, settlements-only or any is a server setting. When allowed, the client asks, the server checks combat, encumbrance, cooldown and gamemode rules, and the server teleports the player. Time never skips for the world (F25); travel time can instead cost survival needs (F20). At T2 a vertibird signal grenade summons a ride.

## 2. Vanilla Fallout 4 behaviour (engine facts that matter for sync)
- **Map marker REFRs:** `XMRK` plus `FNAM` flags Visible 0x01, Can Travel To 0x02, "Show All" Hidden 0x04, Use Location Name 0x08; `FULL` name; `TNAM.type` (0 Cave, 1 City, 2 Diamond City, … 13 Settlement, 15 Vault, … 69–80 DLC types, 81–99 custom) [src: xEdit wbDefinitionsFO4.pas:11714-11820].
- **Runtime marker data:** `ExtraMapMarker{MapMarkerData*}`. **`MapMarkerData` is undefined in every CommonLibF4 fork (RE gap)** [src: CLF4 E/ExtraMapMarker.h].
- **Papyrus:**
  - `ObjectReference.AddToMap(abAllowFastTravel)`, `CanFastTravelToMarker`, `EnableFastTravel`, `IsMapMarkerVisible`;
  - `Game.FastTravel(ref)`, `Game.ShowAllMapMarkers`;
  - `InputEnableLayer.EnableFastTravel(bool)` (FO4 has no `Game.EnableFastTravel`);
  - `Cell.EnableFastTravel`;
  - event `ScriptObject.OnPlayerTeleport` (load door, fast travel, moveto), `Actor.OnPlayerEnterVertibird(ref)` [src: F4SE vanilla/ObjectReference.psc:212, 250, 317, 618; Game.psc:28, 94; InputEnableLayer.psc:28; Actor.psc:984].
- **Pip-Boy map data:** `PipboyMapData` sinks `TravelMarkerStateChange`, `LocationMarkerArrayUpdate`, `CustomMarkerUpdate` and `TESLocationClearedEvent` [src: CLF4 P/PipboyMapData.h]. These are usable capture points.
- **Menus:** `PipboyMenu` (the map is a tab), `VertibirdMenu` [src: CLF4 IDs_RTTI.h]. `VertibirdMenu` is missing from papyrus-api-map §5.6.
- **Rules:**
  - time passes in proportion to distance (about 14 h corner to corner at timescale 20);
  - travel is blocked when over-encumbered or when enemies are near;
  - Survival disables it, except by vertibird;
  - interiors are travel targets only in a few places [web: wiki:Fast_travel].
- **Vertibird Signal Grenade:** `BoSVertibirdGrenade 00056917` summons a vertibird that flies the player to a chosen marker, with a minigun seat [web: wiki:Vertibird_signal_grenade].
- **Single-player assumptions that break:** discovery is stored per save; fast travel advances the shared clock; the destination is reached through a local load.

## 3. SkyMP baseline
- **Fast travel:** disabled on every frame [src: disableFastTravelService.ts:9-11]. There is no marker state on the server.
- **Server teleports:** `MpActor::Teleport` → `Teleport` (20) to the owner. The client handles it with `moveRefrToPosition` or a load (reference/skymp-sync-inventory.md §2, door row).
- **Reuse:** the server teleport path, `HeuristicPolicy` for Papyrus defaults, and the property system for per-player data.
- **New:** marker catalogue, per-player discovery, fast-travel rules, vertibird.

## 4. Design

### 4.1 Authority model
- **Class A:** the discovered set, the travel-enabled set, and every fast-travel outcome (destination, side effects).
- **Class B (validated claim):** "I discovered marker X". The owner reports it, and the server checks it against its own position history.
- **Class D:** custom player markers, quest-target rendering, map UI, compass drawing.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Marker catalogue | `{ref FormDesc, pos, worldOrCell, type u8, flags u8, name}` | `MapService` (built at load from ESM; includes initially disabled refs, unlike world-economy B8) | no | ESM `XMRK` refs |
| Discovered markers | `set<FormDesc>` | `PlayerProfile.discoveredMarkers` (SRV-060) | yes | `map.defaultDiscovered` |
| Travel-enabled markers | `set<FormDesc>` | `PlayerProfile.travelMarkers` | yes | discovered ∩ FNAM CanTravelTo |
| Gamemode overrides | `{granted[], revoked[]}` | `PlayerProfile.mapOverrides` | yes | empty |
| Marker-level travel switch | bool per marker | `MapService` (Papyrus `EnableFastTravel`) | yes (world record `mapState`) | true |
| Last fast travel | u64 ms | `PlayerProfile.lastFastTravelMs` | yes | 0 |
| Pending travel | `{nonce, dest, startMs}` | `MpActor` (memory) | no | — |
| Vertibird session (T2) | `{vertibirdRef, passengers[], dest, phase}` | `VertibirdService` | no (despawned on restart) | — |

**`map.defaultDiscovered`:**
- defaults to the markers already visible in the template save (F00), which Papyrus cannot hide (§4.5);
- is computed once with G-self and stored in GameProfile data.

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `MapDiscovery` (103) | C→S | `markerRefIds[]` (u32 server ids, ≤ 16 per message) | R | on local discovery, batched per 1 s | registry 103 |
| `MapDiscovery` (103) | S→C | `mode u8` (snapshot, delta), `discovered[]`, `travel[]`, `revoked[]` | R | snapshot after `CreateActorFo4 isMe`; delta on change; ≈ 1.2 KB snapshot for 300 markers | registry 103 |
| `FastTravelRequest` (104) | C→S | `nonce u32`, `markerRefId u32`, `mode u8` (map, vertibird) | R | on map confirm | registry 104 |
| `Teleport` (20) | S→C | destination (server-initiated) | R | on accept | reused |
| `SpSnippet` (30) `Debug.Notification` | S→C | localized denial reason | R | on deny | reused |

A denial changes no client state, because the engine travel was cancelled before it started. The notification is therefore the whole correction. If the denial comes from a marker-state mismatch, a `MapDiscovery` delta follows (S12).

### 4.4 Client capture (owner side)
- **Discovery:**
  - sink `PipboyMapData`'s `LocationMarkerArrayUpdate` / `TravelMarkerStateChange` (PLAT-040 extension);
  - fallback: every 2 s, poll `IsMapMarkerVisible()` on catalogue markers within 2 grid cells;
  - newly visible markers that are not in the server set are sent in one `MapDiscovery` batch.
- **Fast travel:**
  - the engine path is intercepted (F26-T06): a platform hook on the player fast-travel entry cancels the local travel and emits `fastTravelAttempt(markerRef)`;
  - `InputEnableLayer.EnableFastTravel(false)` stays as the block when `fastTravel.mode = "off"`;
  - fallback without RE: a FalloutMP map list in the front (discovered markers, click to request).
- **Vertibird (T2):** `OnPlayerEnterVertibird` and the `VertibirdMenu` selection are captured the same way.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Snapshot or delta:** call `ref.AddToMap(abAllowFastTravel = inTravelSet)` for every discovered marker.
- **Revocation and hiding:**
  - this needs `setMapMarkerState(ref, visible, canTravel)`, which depends on the MapMarkerData RE (F26-T08);
  - until then, revocations apply on the server only: travel is refused, and the marker stays drawn until the next session;
  - for the same reason, template-save markers are part of `map.defaultDiscovered`.
- **Accepted travel:**
  - the server sends `Teleport` (20);
  - the client runs the F00/F01 MoveTo path (a loading screen when it crosses cells);
  - the server treats it as a server-initiated teleport, so F01 §4.6 rule 3 accepts the jump;
  - an optional `fastTravel.realDelayMs` keeps the player on a fade before the teleport.
- **Companion:** the owner's active companion (F21) is teleported with the owner.
- **Reconnect and respawn:** a fresh snapshot follows `CreateActorFo4 isMe`.
- **HUD compass:**
  - the engine draws discovered markers and the nearby undiscovered ones locally;
  - quest targets come from F27;
  - party markers come from FRONT-006.

### 4.6 Validation & anti-cheat
| Check | Reject → correction |
|---|---|
| Discovery: the marker is in the catalogue and enabled; same `worldOrCell` as the actor; the actor's server-known position (F01 history) was within `map.discoveryRadius` (default 2048 u [inference]) during the last 10 s; ≤ 16 claims/s | ignored + metric `map_discovery_reject_total`; the next delta omits it |
| Gamemode `onMarkerDiscovered` returns false | not recorded; a delta with the marker in `revoked[]` |
| Accepted discovery | recorded; delta sent; F19 awards discovery XP (`AwardXp`, source `discover`, F19-T05) |
| Fast travel: `fastTravel.mode` (`off` / `discoveredOnly` / `settlementsOnly` / `any`; default `discoveredOnly`), plus the marker-level switch | notification "fast travel disabled" |
| Marker in `travelMarkers` (unless `any`); for `settlementsOnly`, TNAM 13 (Settlement) or a workshop owned by the profile (F22) | notification + `MapDiscovery` delta |
| Not in combat: no damage dealt or taken within `fastTravel.combatLockSec` (10 s, F11), and no hosted NPC targeting the actor within 4096 u (F13) | notification "enemies nearby" |
| Not over-encumbered (F08-T11 `isOverEncumbered`); alive; not in furniture or dialogue; not a container occupant; not in a rest session (F25) | notification with reason |
| Cooldown `fastTravel.cooldownSec` (0); survival rule (F20: off unless `mode = vertibird`); same worldspace unless `fastTravel.crossWorldspace` | notification |
| Gamemode `onFastTravel` returns false | notification "blocked" |

- **Destination:** the marker ref's position and rotation [inference: verify on D-real whether markers link a separate travel point].
- **World clock:** never advanced.
- **`fastTravel.timeCost`:**
  - `"none"` (default);
  - `"needs"`: F20 survival needs advance by the vanilla travel hours, computed as distance / walk speed × timescale.

### 4.7 Audience / visibility
- **`MapDiscovery`:** owner only (private state, S8).
- **`Teleport`:** owner only. Neighbours at the source and destination see the actor through grid `DestroyActor`/`CreateActor`.

### 4.8 NPC parity
- NPCs do not discover markers.
- Companions travel with their owner (F21).
- The vertibird (T2) is a server-spawned NPC actor hosted by the summoner (F13). Its occupancy is furniture (F07). F01 notes that vertibird movement is owned by its host.

### 4.9 Gamemode API & server Papyrus
**JS properties:**
- `discoveredMarkers` and `travelMarkers`: owner-visible arrays of FormDesc;
- `mp.set` grants or revokes, and sends a delta;
- per-actor `fastTravelAllowed` (optional bool override of the server mode).

**Events:**
- `onMarkerDiscovered(actorId, markerDesc)` [blockable];
- `onFastTravel(actorId, markerDesc, mode)` [blockable].

**Settings (S22):** `map.{discoveryRadius, defaultDiscovered}`, `fastTravel.{mode, combatLockSec, cooldownSec, crossWorldspace, timeCost, realDelayMs}`.

**Papyrus natives (server):**
- `ObjectReference.AddToMap(abAllowFastTravel)` grants to the `HeuristicPolicy` actor, or to all players when `map.addToMapScope = "all"`;
- `IsMapMarkerVisible` and `CanFastTravelToMarker` read the default actor's profile;
- `EnableFastTravel(bool)` sets the marker-level switch;
- `Game.FastTravel(dest)` does a server fast travel of the default actor and bypasses the rules (scripts are trusted);
- `InputEnableLayer.EnableFastTravel` becomes a client SpSnippet (PVM-016).

**Papyrus events:** `OnPlayerTeleport` after every fast travel; `OnPlayerEnterVertibird` (T2).

### 4.10 Edge cases & failure modes
- **Disconnect right after accept:** the teleport has already been applied on the server, and the new position is persisted immediately (cell change, F01).
- **Markers enabled later by quests** (initially disabled refs): kept in the catalogue, but discoverable only once the server considers them enabled.
- **Claiming a settlement** (F22) grants its marker.
- **DLC worldspaces:** each has its own catalogue. Cross-worldspace travel goes through boats and doors unless `crossWorldspace` is set.
- **Hot reload:** no JS-held state.
- **Server restart:** profiles reload.
- **Pending vertibird sessions** are dropped, and passengers are teleported to the destination.

### 4.11 Performance budget
- Snapshot ≈ 4 B per marker (≤ 2 KB with DLC).
- Discovery validation O(1) per claim against the movement history.
- Fast-travel validation ≤ 20 µs.
- Client poll fallback: ≤ 60 `IsMapMarkerVisible` calls every 2 s.

## 5. Engine / platform work required
- `PipboyMapData` event sinks (PLAT-040 extension).
- Fast-travel interception hook: player fast-travel entry, cancel plus capture (RE, F26-T06).
- `MapMarkerData` layout RE and a `setMapMarkerState` native (F26-T08).
- A `VertibirdMenu` selection hook (T2).
- `AddToMap` and `IsMapMarkerVisible` already exist in Papyrus (reflection, PLAT-031).

## 6. Tests
- `L-unit` `[F26][MapMarker]`:
  - catalogue build from a fixture;
  - discovery accept, including the F19 discovery XP call;
  - discovery reject (too far, wrong cell, unknown marker, rate);
  - `onMarkerDiscovered` veto with a `revoked` delta;
  - snapshot on spawn;
  - persistence round trip and backward-compatible load of a profile without map fields.
- `L-unit` `[F26][FastTravel]`:
  - the full rule matrix (mode × discovered × combat × encumbrance × cooldown × survival);
  - each denial sends a notification SpSnippet;
  - accept sends `Teleport` and moves the companion;
  - `onFastTravel` veto;
  - Papyrus `Game.FastTravel` bypass;
  - `OnPlayerTeleport` observed;
  - message round trips.
- `L-fixture`: REFR `XMRK`/`FNAM`/`TNAM` parsing via PluginBuilder.
- `L-ts`: discovery batching and de-duplication; the fallback poll window.
- `L-int`: a bot walks to a marker → discovered → fast travels back; a second bot that never visited is denied.
- `G-self`: dump the visible markers of the template save (feeds `map.defaultDiscovered`); `AddToMap` makes a marker appear.
- `G-manual`: discover Concord, reconnect, fast travel Sanctuary ↔ Concord, denial while in combat.

## 7. Tasks
- [ ] **F26-T01** Map-marker catalogue: ESPM `XMRK`/`FNAM`/`TNAM`/`FULL` on REFR (including initially disabled refs) + `MapService` — M — Depends: ESPM-005, ESPM-002 — Verify: L-fixture, D-real — Files: libespm/src/, falloutmp-server/cpp/server_guest_lib/MapService.{h,cpp} (new), unit/MapServiceTest.cpp
  - Accept: the fixture catalogue matches. D-real counts the Commonwealth markers.
- [ ] **F26-T02** `PlayerProfile` map fields (SRV-060) with defaults and back-compat — S — Depends: SRV-060 — Verify: L-unit
  - Accept: JSON round trip; loading a profile with no map fields gives the defaults.
- [ ] **F26-T03** `MapDiscovery` (103) and `FastTravelRequest` (104) messages + client mirrors — S — Depends: NET-002 — Verify: L-unit — Files: falloutmp-server/cpp/messages/{MapDiscoveryMessage,FastTravelRequestMessage}.h, Messages.h, falloutmp-client/src/services/messages/
- [ ] **F26-T04** Server discovery validation, `onMarkerDiscovered`, `discoveredMarkers`/`travelMarkers` bindings, snapshot on spawn, Papyrus `AddToMap`/`IsMapMarkerVisible`/`CanFastTravelToMarker` — M — Depends: F26-T01…T03, F01-T05 — Verify: L-unit
  - Accept: the `[MapMarker]` cases pass.
- [ ] **F26-T05** Client `MapService`: capture (PipboyMapData sink, poll fallback), apply via `AddToMap`, `defaultDiscovered` seeding — M — Depends: F26-T03, PLAT-040, CLI-050 — Verify: L-ts, G-self — Files: falloutmp-client/src/services/services/mapService.ts
- [ ] **F26-T06** Fast-travel interception hook + `InputEnableLayer` policy (replaces `disableFastTravelService.ts`) — M — Depends: PLAT-031, PLAT-040 — Verify: W-ci, G-self — Files: fallout4-platform/src/.../FastTravelHook.cpp, falloutmp-client/src/services/services/fastTravelService.ts
  - Accept: clicking a marker in the Pip-Boy produces a request and no local travel.
- [ ] **F26-T07** Server `FastTravelService`: rule matrix, `Teleport`, companion carry, `onFastTravel`, `Game.FastTravel`, `OnPlayerTeleport`, `timeCost` hook — M — Depends: F26-T04, F11, F04 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/FastTravelService.{h,cpp} (new), unit/FastTravelTest.cpp
  - Accept: the `[FastTravel]` cases pass.
- [ ] **F26-T08** `MapMarkerData` RE + `setMapMarkerState` native (hide, revoke) — M — Depends: PLAT-002 — Verify: W-ci, G-self
  - Accept: a revoked marker disappears from the map without a reload.
- [ ] **F26-T09** (T2) Vertibird signal travel: summon on grenade detonation (F10), hosted vertibird (F13), board (F07), `VertibirdMenu` → `FastTravelRequest{mode=vertibird}`, flight phase, then teleport and despawn — L — Depends: F26-T07, F13, F10 — Verify: L-unit, G-manual
- [ ] **F26-T10** Docs (DOCS-003 map section) + G-manual script — S — Depends: F26-T07 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F26-map-fast-travel.md

## 8. Open questions & risks
- Without the `MapMarkerData` RE, revocation and per-player hiding cannot be shown on the client (world-economy §5 item 4).
- The discovery radius per marker type is unknown. Measure it in vanilla (G-manual) and refine `discoveryRadius` per `TNAM` type.
- Default `fastTravel.mode`: `discoveredOnly` is proposed here, while SkyMP parity would be `off`. Confirm with the user.
- Survival's vertibird exception requires F26-T09. Until then, Survival servers have no fast travel.
