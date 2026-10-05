# falloutmp-client

The FalloutMP client layer. These are the TypeScript services that keep a Fallout 4 game in sync with a FalloutMP server.

The package is engine-free. Every game call goes through the `FalloutPlatform` interface in `src/platform/falloutPlatform.ts`. That interface is the contract the `fallout4-platform` F4SE plugin implements. Because of it, the whole client runs and is tested in Node against a scripted fake platform.

## Layout

| Path | What it holds |
|---|---|
| `src/services/messages/` | Mirrors of `skymp5-server/cpp/messages/MsgType.h` and `Fo4Messages.h`, plus the server enums and error texts |
| `src/core/` | Typed event bus, message router, nonce request tracker, item-stack helpers |
| `src/platform/falloutPlatform.ts` | The native API contract and the player-side capture events |
| `src/services/services/` | One service per system (see below) |
| `src/falloutMpClient.ts` | Composition root: wires the services, platform events and lifecycle |
| `src/integration/skympClientBridge.ts` | Hooks into the forked skymp5-client networking |
| `test/` | Node tests with `fakePlatform.ts`, plus the C++/TS protocol parity test |

## Services

| Service | Systems | Server authority |
|---|---|---|
| `InventoryService` | Inventory, containers, corpse loot, drops, consumables | Snapshots diffed into the game silently, with a 5 s reconcile |
| `ActorValueService` | Health, AP, SPECIAL, limb conditions | Owner gets every value; puppets get a health fraction |
| `EquipmentService` | Weapons and apparel | The game changes only when the server's state arrives |
| `ProgressionService` | Level, XP, perks, SPECIAL, character creation | Requests are pre-checked locally and re-validated by the server |
| `PowerArmorService` | Entering and exiting power armor, pieces, fusion cores, jetpack | Request, animation, then Ack; the server rolls back on timeout |
| `WorkshopService` | Build mode, placing, moving, scrapping, storing, wiring, claims, permissions, ratings | Chunked snapshots and versioned deltas mirrored to spawned refs |
| `CraftingService` | Crafting, weapon and armor mods, scrapping | Server checks the bench, perks and components |
| `MovementService` | Player transform capture, remote replay | 10 Hz unreliable; validated by the server before relay; 120 ms interpolation buffer |
| `CombatService` | Shots, hit claims, reloads, damage, kills | Shots carry `clientShotId` so hits name the server sequence |
| `BarterService` | Vendor quotes and trades | A trade must match the quoted caps delta |
| `LockService` | Lockpicking and terminal hacking | The server rolls every attempt (review finding C3) |
| `PartyService` | Parties, invites, PvP flag | Invites and state are pushed to every member |
| `MapService` | Map markers, discovery, fast travel | Discovery is server-side by proximity |
| `WorldTimeWeatherService` | Shared clock and weather | Only corrects drift above 3 game minutes |

## Commands

```bash
npm install
npm run typecheck   # src only, without Node types
npm test            # builds src + tests and runs node --test
```

The protocol parity test reads the C++ headers from the repository. Run it from inside a full checkout.

## Using it in the game client

The forked skymp5-client networking service routes the Fallout 4 message range before its own dispatch chain:

```ts
const client = new FalloutMpClient(falloutPlatform, new EmitterTransport(controller.emitter));
const bridge = new SkympClientBridge(client);
// in networkingService, before the MsgType if/else chain:
if (bridge.handleIncoming(msgAny)) break;
```

The world view calls `bridge.onCreateActor`, `onDestroyActor` and `client.onActorStreamedIn` as actors stream. On disconnect, `bridge.onConnectionLost()` fails every pending request with `Disconnected` and clears the mirrored state.

## What needs the game to verify

Everything here is tested against the fake platform only. The natives in `falloutPlatform.ts` still have to be implemented in the F4SE plugin and checked in game. That covers the power armor sequence, the workshop menu hooks and the minigame menus. See `docs/falloutmp/STATUS.md`.
