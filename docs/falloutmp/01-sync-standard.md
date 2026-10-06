# 01 — The SkyMP Sync Standard (SSS) for FalloutMP

> This is the contract every FalloutMP feature must meet. It encodes "sync by the same standards SkyMP uses" precisely. It is derived from the code-level analysis in [reference/skymp-sync-inventory.md](reference/skymp-sync-inventory.md):
> - checklist S1–S23 (§3.1 there);
> - SkyMP's own inconsistencies I1–I22 (§3.2 there);
> - the recipes for adding messages, fields, properties, events, natives and services (§4.5 there).

---

## 1. Principles

1. **The server is the source of truth for anything that can be exploited or that must persist.** Inventory, containers, equipment, actor values, damage, death, progression, crafting, workshop state and world-object state are server-authoritative. Clients send **intents**; the server computes results and sends **authoritative state**. (S3)
2. **Transient, high-frequency presentation state is owner-authoritative but bounded.** This covers movement, animation, aim and anim variables. It is sent unreliably and relayed to grid neighbours. The server validates it *before* relaying (SkyMP-plus, I1).
3. **NPCs are simulated by a hosting client.** The host is trusted only for what a player client is trusted for. Everything else about the NPC stays server-authoritative. (S6, S14)
4. **Visibility is grid-scoped:** 4096-unit cells, 3×3 neighbourhood per worldspace/cell.
   - A late joiner gets a complete snapshot on subscribe (`CreateActor`).
   - Changes go out as deltas (`UpdateProperty` or relayed messages).
   - Nothing gameplay-related is broadcast world-wide. (S7)
5. **Private state goes to the owner only:** inventory, actor values, progression, menus. (S8)
6. **Every gameplay action is gamemode-vetoable** (`GameModeEvent`; returning `false` blocks it) and observable from server Papyrus. (S11, S16)
7. **Every rejection sends a correction** that returns the client to server truth (S12). SkyMP sometimes skips this; FalloutMP never does (I12).
8. **Everything persists through ChangeForms** using `FormDesc` ids, with additive, default-safe schema evolution. (S9, S10)
9. **Every feature is unit-tested at the PartOne level:**
   - the accept path;
   - each reject path, asserting the correction;
   - late-joiner snapshot;
   - persistence round trip. (S23)

## 2. Authority classes

Every piece of synced state belongs to exactly one class. Feature specs must state the class per state item.

| Class | Name | Who decides | Server role | Examples |
|---|---|---|---|---|
| **A** | Server-authoritative | Server | Computes and stores. Client requests are validated and may be rejected with a correction | Inventory, equipment, AVs, damage, death, XP/perks, crafting, workshop objects, container contents, lock state |
| **B** | Owner-authoritative, validated | Owner client (or host for NPCs) | Validates plausibility (speed, teleport, ownership, rate) **before** relaying. Snap-back on violation | Movement, aim, animation events/variables, fire events (with server-side ammo/rate/LOS checks) |
| **C** | Host-simulated | Hosting client's local AI | Grants and revokes hosting. Validates host identity on every NPC-scoped message. Applies class-A rules to the NPC's state | NPC locomotion, combat decisions, companion following |
| **D** | Local cosmetic | Each client independently | None (optionally seeds randomness) | Radio playback, Pip-Boy UI, particles, ambient sounds |

**Rule:** if exploiting it gives an advantage (items, damage, XP, caps, position through walls), it is class A or B, never D.

## 3. Feature levels (Definition of Done)

| Level | Name | Criteria (cumulative) |
|---|---|---|
| **L0** | Not started | — |
| **L1** | Visible | Remote clients see the effect (rendering path works). There may be no persistence or validation. Late joiners may not see it |
| **L2** | Synced | State is consistent for everyone, including **late joiners** (`CreateActor` snapshot) and **stream-in/out**. Owner changes propagate. It may still be trusted |
| **L3** | Authoritative | The server owns or validates the state (per its class). Every reject path sends a correction. The state **persists** across reconnect and server restart. It works for **hosted NPCs** where applicable |
| **L4** | Complete | L3, plus: gamemode events/properties (`mp.get/set`, blockable events), server Papyrus events/natives, documentation in the gamemode API docs, full test matrix (§9), performance budget met, all S-checklist items pass or have documented deviations |

The **target level** for each feature is set in [00-vision-scope.md](00-vision-scope.md) and in each feature spec. A milestone exit requires its features at target level, with evidence linked in STATUS.md.

## 4. The checklist (adopted from SkyMP, S1–S23) — FalloutMP form

Each item applies unless a feature spec documents a justified deviation.

| # | Requirement |
|---|---|
| S1 | Messages are defined once as C++ structs with `Serialize(Archive&)` and `kMsgType`, registered in `messages/Messages.h`. They are mirrored in the client's `messages.ts` plus a typed event in `events.ts` |
| S2 | Binary on the wire (client JSON → `MpClientPlugin` conversion). No string-typed legacy messages |
| S3 | The client sends intent; the server computes and sends authoritative state (`SetInventory`, `UpdateProperty`, dedicated messages) |
| S4 | High-frequency transient state: unreliable, relayed to grid neighbours, validated first (I1) |
| S5 | State changes and RPCs: reliable **and ordered** both directions (I2), or carry a `numChanges`/sequence counter |
| S6 | Every handler taking an idx/caster/aggressor/target verifies ownership: own actor, or an NPC the sender hosts |
| S7 | Grid-scoped visibility. New listeners get a full snapshot in `CreateActor`; deltas through `UpdateProperty`/relays |
| S8 | Private data goes to the owner only (`VisitPropertiesMode::All` vs `OnlyPublic`) |
| S9 | Persistent state lives in `MpChangeFormREFR` (or an ADR-010 record), is mutated only via `EditChangeForm`, is serialized with guarded optional reads, and uses `FormDesc` ids |
| S10 | State is restored in `ApplyChangeForm` (timers/effects re-armed) and reflected in `CreateActor` |
| S11 | Each gameplay action is a `GameModeEvent`. The default effect runs in `OnFireSuccess` |
| S12 | Each rejection sends a correction (never silent: I12) |
| S13 | Inventory/currency/ammo mutations are atomic: operate on a copy, commit only if the whole operation is valid |
| S14 | Messages addressed to an NPC route via `GetActorToSendTo()` (owner, else hoster) |
| S15 | Server-side Papyrus natives with visible effects mirror them to clients (SpSnippet or a dedicated message). SpSnippet also routes to hosters for NPCs (I16) |
| S16 | Papyrus events go through `MpForm::SendPapyrusEvent`, so the gamemode sees `onPapyrusEvent:<Name>` |
| S17 | Ids on the wire: `idx` for live refs, long ids (`+0x100000000`) for ESM-id actors, `0x14` alias for self. FO4 additions must follow the same rules |
| S18 | Ordering counters (`numChanges`) on reorderable state |
| S19 | Bursty state uses deferred, coalesced channels (overwrite for snapshots, append for RPCs) |
| S20 | Exceptions are the rejection mechanism inside handlers; `HandlePacket` never crashes or disconnects on bad input. The binary reader is bounds-checked (I20) |
| S21 | The client mutates the game only in the `update` context. State that must survive hot reload lives in `sp.storage` |
| S22 | Tunables are server settings, GameProfile data, or `mp` properties, not hard-coded constants |
| S23 | PartOne-level Catch2 tests cover accept, every reject (with correction), the late-joiner snapshot and the persistence round trip |

## 5. SkyMP-plus decisions (where FalloutMP deliberately exceeds SkyMP)

These resolve SkyMP's inconsistencies (reference §3.2). They apply to all FalloutMP code paths.

**Where the server code is shared with Skyrim, implement each one in one of two ways:**
1. As a bug fix that keeps Skyrim tests green, and is a candidate for upstreaming.
2. Gated by `GameProfile` if it changes Skyrim behaviour.

| Id | SkyMP behaviour | FalloutMP rule | Task |
|---|---|---|---|
| I1 | Movement relayed before validation | Validate, then relay. Violations: drop and `Teleport2` | F01-T05 |
| I2 | Client reliable = unordered | Client uses RELIABLE_ORDERED, or every reliable message carries a counter | NET-004 |
| I3 | Inventory-changing messages sent unreliable | All inventory/ammo-changing messages reliable (FO4 weapon fire is special: see F09) | NET-005 |
| I4 | `SendMessageToActorListeners` ignores `reliable` | Honour it | NET-006 |
| I5 | `isDisabled` key mismatch | Fix the client key. Send UpdateProperty on ESM disable | CLI-030 |
| I6 | UpdateProperty dropped for ESM-id actors | Route by long id | CLI-031 |
| I7 | AV percentages reset on load | Restore saved values | F08-T06 |
| I8 | Stamina increases not cropped | Crop every regenerating AV (FO4: AP, health regen effects) | F08-T07 |
| I9 | Melee reach unchecked; power/sneak flags trusted | Reach check; derive flags from server-known state | F10-T04 |
| I10 | Spell effects TODO | Full magic-effect pipeline for chems, legendary effects, rads, perks | F20-T02 |
| I11 | Some state not persisted | Persist everything listed in feature persistence tables | F00-T08 |
| I12 | Silent rejects | Always correct (SetInventory etc.) | NET-007 |
| I13 | Hosted NPC AVs conflated | Per-actor AV messages with idx and host validation | F08-T08 |
| I14 | Death only to owner/hoster | `isDead` UpdateProperty to listeners | F12-T03 |
| I15 | No interaction distance checks | Distance and occupancy checks for activate/pickup/craft/workbench | F07-T04 |
| I16 | SpSnippet only for player actors | Route to hoster for NPCs | SRV-040 |
| I17 | LVLI global chance-none ignored | Evaluate it | F14-T03 |
| I18 | Server time ≠ client clock | One server clock (F25) | F25-T01 |
| I20 | Reader has no bounds checks | Bounds-checked BitStream reader | NET-008 |
| I21 | Unknown messages: silent drop / client throw | Log on both sides; the client ignores unknown types | NET-009 |
| I22 | `OnItemAdded` only on some paths | Fire for every inventory addition | F04-T09 |

## 6. Protocol registry

**Wire constraints** (from reference/skyrim-coupling-index.md §5):
- Messages are positional binary. Adding a field changes the layout for every client, so any protocol change bumps the protocol version.
- `MsgType` values must stay **below 123 (`'{'`)**, because a `{` second byte means JSON.

**Allocation:**

| Range | Use |
|---|---|
| 0 | Invalid |
| 1–33 | Existing SkyMP messages. Shared by both games; FO4 variants of their payloads are chosen by GameProfile (ADR-009) |
| 34–63 | **Reserved for upstream SkyMP growth.** Never allocate here, to keep merges conflict-free |
| 64–122 | **FalloutMP messages** (allocated below; feature specs own the details) |

**Protocol version:**
- Skyrim keeps `"7_"`.
- FalloutMP uses `"fo4-<n>_"`, starting at `"fo4-1_"`, and bumps `<n>` on every FO4 protocol change (`NET-001`).
- A client of the wrong game or version is refused at the RakNet password step.

**FalloutMP message allocation.** "Planned" = an ID assigned now, so specs can reference it. Fields are defined in the owning spec.

Two sub-ranges:
- **64–79: FO4 twins** of existing SkyMP messages whose payload is game-specific. The Skyrim wire format (1–33, `"7_"`) stays byte-identical; see reference/skyrim-coupling-index.md §5.3.
- **80–122: new FO4 messages.**

`MessageSerializerFactory` registers per game, so a Skyrim server never decodes FO4 types and vice versa (`NET-002`).

| Id | Name | Twin of / new | Dir | Rel | Owner spec | Purpose |
|---|---|---|---|---|---|---|
| 64 | `CreateActorFo4` | CreateActor (33) | S→C | R | F00 | Snapshot with FO4 appearance, equipment (OMOD instances), AV map, anim state, PA state |
| 65 | `UpdateMovementFo4` | UpdateMovement (2) | both | U | F01 | pos/rot/worldOrCell, speed/direction, flags bitfield (sneak, sprint, sighted, weapon drawn, jump, PA, swim, furniture, dead, blocking, encumbered, bit 11 lightOn, bit 12 jetpackActive), aim pitch/yaw, health % |
| 66 | `UpdateAppearanceFo4` | UpdateAppearance (4) | both | R | F03 | FO4 appearance (morph sliders, regions, tints, head parts, body morph) |
| 67 | `UpdateEquipmentFo4` | UpdateEquipment (5) | both | R | F05 | Worn items as item instances (base + OMODs), weapon/grenade slots, numChanges |
| 68 | `SetInventoryFo4` | SetInventory (28) | S→C | R | F04 | Inventory with FO4 extra data (OMOD list, legendary, health, name). Optional `refId` + `version` fields carry container/workshop contents (F06, F15, F22) |
| 69 | `PutItemFo4` | PutItem (8) | C→S | R | F06 | Put item instance into a container |
| 70 | `TakeItemFo4` | TakeItem (9) | C→S | R | F06 | Take item instance from a container |
| 71 | `DropItemFo4` | DropItem (19) | C→S | R | F06 | Drop item instance |
| 72 | `ChangeValuesAv` | ChangeValues (16) | both | R | F08 | Map of AV form id → current/percentage (incl. AP, Rads, limb conditions); idx-scoped for hosted NPCs (I13) |
| 73 | `DeathStateContainerFo4` | DeathStateContainer (18) | S→C | R | F12 | Death/respawn bundle with AV map |
| 74 | `UpdateActions` | UpdateAnimation (3) | both | R ordered (keyframes may be coalesced) | F02 | Anim-state keyframe + BGSAction replay + whitelisted events with sequence numbers and timestamps |
| 75 | `UpdateGraphVariables` | UpdateAnimVariables (24) | both | U | F02 | Name-resolved behaviour-graph variable snapshot/delta |
| 76 | `CraftItemFo4` | CraftItem (13) | C→S | R | F15 | Craft by COBJ id at a workbench (server consumes components) |
| 77–79 | reserved twins | | | | | |
| 80 | `WeaponFire` | new | C→S, S→C relay | U+seq | F09 | Shot(s) fired: weapon instance, origin, dir, timestamp, seq; full-auto batched |
| 81 | `WeaponReload` | new | both | R | F09 | Reload start/complete, magazine count |
| 82 | `HitReport` | new (supersedes OnHit 17 for FO4) | C→S | R | F09/F10/F11/F18 | Hit claim with rewind timestamp, limb, projectile/explosion id |
| 83 | `DamageApplied` | new | S→C | R | F11 | Authoritative damage result: target, per-type amounts, limb, crit, kill |
| 84 | `ExplosionEvent` | new | both | R | F10 | Thrown/placed explosive lifecycle (spawn, arm, detonate) |
| 85 | `ModItem` | new | C→S | R | F16 | Attach/detach OMODs on an inventory item at a workbench |
| 86 | `ScrapItem` | new | C→S | R | F15 | Scrap items into components |
| 87 | `PowerArmorTransition` | new | both | R | F17 | Enter/exit frame request and authoritative result |
| 88 | `PowerArmorState` | new | S→C | R | F17 | Frame pieces, piece health, paint/mods, core charge |
| 89 | `ProgressionUpdate` | new | S→C | R | F19 | XP, level, SPECIAL, perk ranks, points (owner only) |
| 90 | `ProgressionRequest` | new | C→S | R | F19 | Spend perk/SPECIAL points |
| 91 | `UseItem` | new (supersedes OnEquip 11 for consumables) | C→S | R | F20 | Consume chem/food/drink/stimpak on self/limb/other |
| 92 | `EffectsUpdate` | new | S→C | R | F20/F08 | Active effects (chems, rads, addictions, legendary, perks) |
| 93 | `WorkshopMode` | new | both | R | F22 | Enter/exit build mode at a workshop |
| 94 | `WorkshopPlace` | new | C→S | R | F22 | Place object intent (base, transform, snap/attach info) |
| 95 | `WorkshopEdit` | new | C→S | R | F22 | Move/rotate/scrap/store placed object |
| 96 | `WorkshopState` | new | S→C | R | F22 | Ratings, budget, ownership, power graph deltas |
| 97 | `WorkshopWire` | new | both | R | F22 | Wire connect/disconnect |
| 98 | `Barter` | new | both | R | F23 | Buy/sell transaction request/result |
| 99 | `LockpickAttempt` | new | both | R | F24 | Lockpick request/result |
| 100 | `TerminalAction` | new | both | R | F24 | Hack attempt, menu item selection, result |
| 101 | `VatsAction` | new | both | R | F18 | VATS-lite target selection and server resolution |
| 102 | `CompanionCommand` | new | C→S | R | F21 | Companion commands |
| 103 | `MapDiscovery` | new | both | R | F26 | Location discovered / marker state |
| 104 | `FastTravelRequest` | new | C→S | R | F26 | Fast travel request (server validates and teleports) |
| 105 | `WorldTimeWeather` | new | S→C | R | F25 | Server clock, timescale, weather (incl. radstorm) |
| 106 | `DetectionState` | new | S→C | U | F29 | Sneak/detection feedback (optional) |
| 107 | `RequestResult` | new | S→C | R | F15 (shared by F16, F22) | Nonce-keyed result of a C→S request without its own reply (CraftItemFo4, ScrapItem, ModItem, WorkshopPlace/Edit/Manage): ok, error code, created refId, item list |
| 108 | `WorkshopObjects` | new | S→C | R | F22 | Chunked, versioned snapshot and deltas of a settlement's decor objects and scrapped pre-placed refs |
| 109 | `WorkshopManage` | new | C→S | R | F22 | Claim/abandon, ACL changes, settler/bed/job assignment, supply line (T2) |
| 110 | `NoteAction` | new | C→S | R | F24 | Note read / holotape play / holotape chatter outside a terminal |
| 111 | `NpcAiState` | new | both | R | F13 (consumed by F29) | Host → server: hosted NPC combat state/target, special states (burrowed…), per-target detection levels, crime reports. Server → new host: AI re-seed bundle on host migration |
| 112 | `CompanionState` | new | S→C | R | F21 | Companion follower state, pending command for the host, owner's affinity/threshold, downed; command results (nonce/error) |
| 113 | `RestAction` | new | both | R | F25 | Wait/sleep request/cancel (C→S) and accepted/denied/finished result (S→C) in server-mediated rest mode |
| 114 | `QuestUpdate` | new | S→C | R | F27 | Snapshot of one gamemode quest slot: title, stage, flags, objectives, targets (owner or scope members) |
| 115 | `DialogueAction` | new | both | R | F27 | Gamemode dialogue: open/choose/close (C→S); show line + choices / close / denied (S→C) |
| 116 | `ConsoleCommandResult` | new | S→C | R | F30 | One result per `ConsoleCommand` (12): ok, code, text (no silent rejects, I12) |
| 117 | `ContainerPeek` | new | C→S | R | F06 | Start/stop a quick-loot peek subscription on a container/corpse (`target`, 0 = stop); contents come back as `SetInventoryFo4` with `refId` |
| 118 | `SetFavorites` | new | C→S | R | F04 | Owner's 12 favorite slots (`ItemKey`) and tagged components (T1), persisted in the player profile |
| 119 | `PickpocketAttempt` | new | both | R | F29 | Pickpocket request (target, item key) and authoritative result |
| 120 | `PartyAction` | new | both | R | F32 | Party lifecycle ops (create/invite/accept/leave/kick/promote/settings/PvP flag) and authoritative party state |
| 121 | reserved | | | | F04 | Delta form of `SetInventoryFo4` (per-entry add/remove/update), allocate when F04-T04 measures that full snapshots exceed budget |
| 122 | free | | | | | Last free ID. If the range runs out, discuss a 2-byte type extension (NET) |

Rules for adding a message:
1. Follow recipe (a) in reference/skymp-sync-inventory.md §4.5.
2. Allocate the next free ID in this table.
3. Bump the FO4 protocol version.
4. Add a unit test (serialize round trip, plus handler accept/reject).
5. Add the client handler.
6. An unknown server→client type must not crash the client (I21).

## 7. Rates, sizes and budgets (defaults; feature specs may tighten)

| Stream | Default rate | Reliability | Budget |
|---|---|---|---|
| Movement (player, hosted NPCs) | every 100 ms (SkyMP 130 ms; FO4 gunplay needs tighter) plus on flag change | U | ≤ 64 B/msg |

Rates are maxima for the sender. SRV-091 may lower the *relay* rate per recipient (distance, occlusion, hot-spot cap) without changing the sender rate.

| Stream | Default rate | Reliability | Budget |
|---|---|---|---|
| Graph variables / aim | ≤ 10 Hz while changing | U | ≤ 96 B/msg |
| Animation/action events | on event | U (state-critical ones R) | ≤ 48 B/msg |
| Weapon fire | per shot (coalesce full-auto bursts into ≤ 20 Hz batches) | U with sequence numbers; ammo reconciled reliably | ≤ 64 B/batch |
| State deltas (UpdateProperty, inventory, equipment) | on change, coalesced per tick | R | — |
| Snapshot (`CreateActor`) | on subscribe | R | ≤ 4 KB typical (appearance + equipment + props) |

**Server CPU:** O(neighbours) per relayed message. No per-message JSON re-parsing on hot paths (SkyMP parses JSON up to 33 times; binary is the norm).

## 8. Persistence rules

1. Every persisted field has:
   - a default;
   - an entry in its feature spec's persistence table;
   - a JSON round-trip test;
   - backward-compatible loading (absent → default).
2. A field that fails to parse must **not** discard the whole form. Parse each field defensively; SkyMP skips the whole form today (`FileDatabase.cpp:119-124`).
3. Ids are stored as `FormDesc`, never raw load-order ids. The database records which game it belongs to (`"game": "fallout4"` meta record). The server refuses to open a database of another game (`SRV-012`).
4. Position-style high-churn fields are throttled, as SkyMP does (30 s). Inventory, currency, XP and workshop changes are saved on the next tick.
5. New non-reference state (workshops, vendors, progression if separate) uses ADR-010 records through the same `ISaveStorage` drivers.

## 9. Test matrix required per level

| Test | L2 | L3 | L4 |
|---|---|---|---|
| Message serialize/deserialize round trip (binary + JSON) | ✓ | ✓ | ✓ |
| Late-joiner snapshot contains the state (`CreateActor` assertions) | ✓ | ✓ | ✓ |
| Accept path changes server state and notifies the correct audience | ✓ | ✓ | ✓ |
| Every reject path asserts the correction message | | ✓ | ✓ |
| Ownership/hosting checks (wrong user, wrong host) | | ✓ | ✓ |
| Persistence round trip (save → restart → load → same state) | | ✓ | ✓ |
| Hosted-NPC variant | | ✓ (if applicable) | ✓ |
| Gamemode veto (FakeListener returns false → no state change, correction sent) | | | ✓ |
| Papyrus event fired (`onPapyrusEvent:X` observed) | | | ✓ |
| Integration test (`L-int`: server + bot clients) | | | ✓ |
| `G-self` / `G-manual` in-game check | ✓ (rendering) | ✓ | ✓ |

## 10. Review checklist (paste into a PR/commit description for each feature)

```
Feature: Fxx <name>   Target level: Lx   Achieved: Lx
[ ] Authority class stated for every state item (A/B/C/D)
[ ] Messages registered (IDs from 01-sync-standard §6), protocol version bumped
[ ] Client capture filters documented; apply path handles stream-in, respawn, reconnect
[ ] Validation + correction for every reject path (no silent rejects)
[ ] Ownership/host checks on every idx-bearing message
[ ] Persistence table updated; JSON round-trip + backward-compat tests
[ ] Audience correct (owner-only vs neighbours)
[ ] Hosted-NPC parity (or N/A with reason)
[ ] GameModeEvent(s) + mp properties + onPapyrusEvent hooks (L4)
[ ] Unit tests per §9; L-int/G-* evidence linked in STATUS.md
[ ] Performance within §7 budgets
[ ] Deviations from S1–S23 listed with reasons
```
