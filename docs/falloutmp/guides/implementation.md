# FalloutMP implementation guide (as built)

This page describes the code as it exists, for contributors. The plan in `docs/falloutmp/` describes the target. This page describes what is in the tree today and how to extend it.

## 1. Layers

```
Fallout 4 + F4SE ── fallout4-platform plugin (Windows, not written yet)
                         │  implements src/platform/falloutPlatform.ts
falloutmp-client ── services (TypeScript, engine-free, Node-tested)
                         │  JSON messages 64-120 over the SkyMP client plugin
skymp5-server ───── PacketParser → ActionListener → PartOne::GetFo4()
                         │
                    Fo4PartOneGlue (Fo4Host on WorldState, persistence)
                         │
                    Fo4Server facade → fo4:: services (engine-free C++)
                         │
                    IFo4DataSource ── EspmFo4DataSource (libespm FO4 readers)
```

The fo4 services never touch PartOne, the network or files. Everything goes through two interfaces:

- **`Fo4Host`** (`Fo4Server.h`) supplies positions, profiles, sending, time, teleports, kills and gamemode events.
- **`IFo4DataSource`** (`Fo4Data.h`) supplies weapons, armor, recipes, mods, furniture, leveled lists and containers.

Unit tests use fakes of both.

## 2. Where things are

| Area | Server (C++) | Client (TS) |
|---|---|---|
| Game selection | `game_profile/`, `mp_common/GameId.h` | — |
| Protocol | `messages/MsgType.h`, `messages/Fo4Messages.h` | `services/messages/` |
| Plugin data | `libespm/src/fo4/Fo4Records.cpp`, `fo4/EspmFo4DataSource.cpp` | — |
| Items and inventory | `fo4/ItemInstance`, `fo4/Containers`, `fo4/LeveledLists` | `inventoryService` |
| Crafting, mods, scrap | `fo4/Crafting`, `fo4/ComponentPlanner`, `fo4/OmodStatResolver` | `craftingService` |
| Power armor | `fo4/PowerArmor` | `powerArmorService` |
| Settlements | `fo4/Workshop` | `workshopService` |
| Movement | `fo4/Movement` (validator, history) | `movementService` |
| Combat | `fo4/RangedCombat`, `fo4/ActorValues` (DamageModel) | `combatService` |
| Character | `fo4/ActorValues`, `fo4/Progression`, `fo4/Effects` | `actorValueService`, `progressionService` |
| Economy | `fo4/Barter` | `barterService` |
| Locks | `fo4/Locks` | `lockService` |
| Social | `fo4/Party` | `partyService` |
| World | `fo4/WorldServices` (clock, weather, map) | `mapService`, `worldTimeWeatherService` |
| Wiring | `fo4/Fo4Server`, `fo4/Fo4PartOneGlue`, `fo4/Fo4WorldBootstrap`, `fo4/Fo4Settings` | `falloutMpClient.ts` |
| Gamemode | `fo4/Fo4GamemodeApi`, `addon/ScampServer.cpp` (`fo4Call`), `ts/fo4.ts` | — |

## 3. The request pattern

Every player action follows one shape, so the systems behave alike in an MMO setting:

1. The client service sends a request with a **nonce**. The request is built with `buildMessage`, so every field is present, because the server rejects JSON with missing fields.
2. `Fo4Server::OnMessage` validates reach, ownership, permissions, perks, components and rate limits, then changes the server state.
3. The server answers the sender. That is either `RequestResult` (107) with the nonce, or the request's own type when the reply carries data (`Barter`, `PowerArmorTransition`, `WeaponReload`, `PartyAction`).
4. The new state is pushed to everyone who needs it: `SetInventoryFo4` to the owner, `WorkshopObjects` deltas to neighbours, `PartyAction` state to members, `PowerArmorState` to neighbours.
5. The client applies the server state to the game. It never applies its own guess. A failed request shows a readable reason (`describeError`) and leaves the game unchanged.

Timeouts on the client are driven by its tick: 10 s, then `"Timeout"`. A disconnect fails every pending request with `"Disconnected"`.

## 4. Authority choices worth knowing

- **Items** are keyed by base id, sorted mods, condition and stolen owner. Loaded ammo is not part of identity. The client mirrors the same identity (`core/itemKeys.ts`).
- **Power armor** pieces and the core move between the frame and the wearer only on the client's Ack, after the animation. Without an Ack within `transitionTimeoutMs`, the server rolls back.
- **Workshops** send a chunked snapshot on build-mode entry and on join, and versioned deltas afterwards. The client discards deltas older than its snapshot.
- **Movement** is validated before relay against a server-known speed model, per sample and over a one-second window. Out-of-model samples are dropped and scored, and a sustained violation teleports the player back. The history (about 3 s) rewinds hit targets to where they were when the shot arrived. Accepted movement also drives fusion core drain.
- **Shots** are relayed with a server sequence number. The shooter also gets its own `clientShotId` back, so hit claims name the server shot. Hits are validated against the shot log, range, rewind window and PvP rules.
- **Hosted NPCs** (F13) use SkyMP's host election. The host's movement, shots, hit claims and actor value reports for its NPCs pass the same checks as a player's, and value reports can never kill. Owner copies of an NPC's state go to its host, and neighbour sends skip the host.
- **Locks and terminals** are rolled on the server. The client minigame is only a presentation.
- **Hosted NPC actor values** reported by a host can never kill (review C1).

## 5. Persistence

- **Per actor:** `Fo4Server::ActorToJson` and `LoadActor`, stored in the `fo4State` dynamic field of the actor's change form. Each part loads in its own try/catch, so one corrupt section doesn't lose the rest.
- **World:** `WorldToJson` and `LoadWorld`, written to `fo4.worldStatePath` through a temporary file and a rename.
- **Load order:** `Fo4WorldBootstrap` registers frames, workshops with build areas, locks and terminals, and map markers. It is idempotent.

## 6. Adding a message

1. Add the id to `MsgType.h` in the 64–120 range, and add the struct to `Fo4Messages.h` with a `Serialize` covering every field. Register it in `REGISTER_FO4_MESSAGES`.
2. Add a JSON and binary round trip in `unit/Fo4MessagesTest.cpp`.
3. Handle it in `Fo4Server::OnMessage`, with a test in `unit/Fo4ServerTest.cpp` using the `FakeHost`.
4. Mirror it in `falloutmp-client/src/services/messages/fo4Messages.ts`: the interface, the `Fo4MessageMap` entry, `kMessageDefaults`, and `kOptionalFields` for `std::optional` fields.
5. Run `npm test` in `falloutmp-client`. The parity test fails until the TypeScript fields equal the C++ `Serialize` keys.

## 7. Adding a gamemode function or event

- **Function:** add a command to the table in `Fo4GamemodeApi.cpp`, a test in `unit/Fo4GamemodeApiTest.cpp`, and the typed wrapper in `skymp5-server/ts/fo4.ts`.
- **Event:** call `host.FireGamemodeEvent("onFo4Name", json::array({...}))`. For a blockable event, act only when it returns true. Document it in `guides/gamemode-api.md`.

## 8. Tests

```bash
# C++ (inside build/)
cmake --build . && ./unit/unit "~[espm]"
./unit/unit "[Fo4Server]"          # one area

# Client
cd falloutmp-client && npm install && npm test

# Server TypeScript
cd skymp5-server && npx tsc --noEmit -p .
```

`[espm]` tests need Skyrim data. `[fo4data]` tests would need Fallout 4 data, and none exist yet. Everything else runs on a bare Linux container.

## 9. What the platform plugin must provide

`falloutmp-client/src/platform/falloutPlatform.ts` lists every native with its contract. The plugin also has to emit the capture events in `PlatformEvents`, cancelling the vanilla effect and reporting the intent. The biggest pieces are:

- **Inventory with instance data:** add and remove items with OMODs and condition (`BGSObjectInstanceExtra`).
- **Power armor:** the enter and exit sequence as a Promise; snapping actors in and out of frames.
- **Workshop:** opening the workshop menu with a permission mask; intercepting place, move, scrap, store and wire; spawning objects and wires from the server.
- **Minigames:** opening the lockpick and hacking menus with the outcome decided by the server.
- **Combat:** muzzle and tracer replay for remote shots; per-projectile hit capture.
