# Gamemode API for Fallout 4: `mp.fo4`

On a Fallout 4 server, gamemode scripts get `mp.fo4` in addition to everything SkyMP's `mp` offers. It is a typed wrapper (`skymp5-server/ts/fo4.ts`) over one native call, `fo4Call(command, argsJson)`. The command table lives in `skymp5-server/cpp/server_guest_lib/fo4/Fo4GamemodeApi.cpp`.

## Conventions

- **Actors** are identified by their server form id (the `actorId` SkyMP gives you). **Players** are identified by profile id where noted.
- **Items** are `Fo4ItemKey` objects: `{ baseId, mods?, condition?, stolenFrom?, ammoLoaded? }`. Two items stack only if everything except `ammoLoaded` matches.
- **Errors.** A call that fails throws `Fo4Error` with `command` and `code`, for example `ItemNotFound`. Calls never fail silently.
- **Side effects.** Every change is applied on the server and sent to the affected clients. You don't need to resend state yourself.

```ts
import { Fo4Error } from "./fo4";

mp.onActivate = (target, caster) => {
  try {
    mp.fo4.addItem(caster, { baseId: 0x0000000f }, 50); // 50 caps
  } catch (e) {
    if (e instanceof Fo4Error) console.log(e.command, e.code);
  }
  return true;
};
```

## Items and equipment

| Function | Notes |
|---|---|
| `getInventory(actorId)` | Every stack with mods, condition and count |
| `addItem(actorId, item, count = 1)` | Ammo merges into existing stacks |
| `removeItem(actorId, item, count = 1)` | Throws `ItemNotFound` if not enough |
| `equip(actorId, item, equip = true)` | Validated like a player request: in inventory, not broken, no outer apparel over power armor |
| `equipWeapon(actorId, item \| null)` | Sets the weapon directly, without validation |
| `setEquippedArmor(actorId, items)` | Sets the apparel list directly |
| `getWeaponStats(item)` | Damage, capacity, ammo, automatic, weight, value and max shots per second, after mods |

## Actor values and progression

| Function | Notes |
|---|---|
| `getActorValue(actorId, avId)` | `{ current, max, base }` |
| `setActorValue(actorId, avId, { base?, current? })` | Use the vanilla AVIF ids (Strength 0x2C2 ... Luck 0x2C8, Health 0x2D4) |
| `getProgression(actorId)` | Level, XP, perk points, perks, collected bobbleheads and magazines |
| `awardXp(actorId, amount, direct = false)` | `direct` skips multipliers; returns `{ level, perkPointsGained }` |
| `definePerk({ key, special, specialRequired?, ranks })` | Adds a perk chart entry players can buy |
| `defineEffect({ effectId, kind, avId? })` | Defines what a consumable effect does: `restoreOverTime`, `restoreInstant`, `damageOverTime`, `valueModifier`, `removeRads` or `addRads` |

## Power armor

| Function | Notes |
|---|---|
| `addPowerArmorFrame({ refId, pos, baseId?, worldOrCell?, ownerProfileId?, contents? })` | Places or registers a frame; `contents` are the pieces and the core |
| `getPowerArmorFrame(frameRefId)` | Frame state, pieces, core and wearer |
| `getWornPowerArmor(actorId)` | The frame an actor wears, or `null` |

## Settlements

| Function | Notes |
|---|---|
| `addWorkshop({ refId, locationId?, areas?, budgetMax? })` | Areas are boxes or spheres; build mode ends outside them |
| `getWorkshop(refId)` | Owner, permissions, objects, wires, settlers, ratings, budget |
| `setWorkshopOwner(refId, { type: "none" \| "profile" \| "group", id? })` | Use with `claimRule: "gamemode"` to run your own claiming |
| `addSettler(refId, actorId)` | Settlers count towards population and can be assigned to jobs |

## Vendors, locks and terminals

| Function | Notes |
|---|---|
| `addVendor({ vendorId, caps?, inventory?, buysTypes?, acceptsStolen?, hours? })` | Prices follow the vanilla Charisma formula |
| `setLock(refId, { level, keyId?, locked?, ownerFaction? })` | Level 0, 25, 50, 75, 100, or 251–255 for barred, chained, terminal, inaccessible or key-only |
| `getLock(refId)` | `{ level, keyId, locked }` or `null` |
| `setTerminalLock(refId, level, locked = true)` | Hacking difficulty |

## Parties, PvP, time, weather and the map

| Function | Notes |
|---|---|
| `getParty(profileId)` | `{ id, leader, members }` or `null` |
| `addPvpZone({ center, radius, mode, worldOrCell? })` | `safe` blocks all player damage, `open` allows it, `flagged` needs both players flagged |
| `getTime()` / `setTime({ gameDays?, gameHour?, timeScale? })` | One clock for everyone; changes are sent immediately |
| `setWeather({ weatherId, transitionSec?, radstorm? })` | One weather for everyone |
| `addMapMarker({ refId, pos, name?, type?, canTravel?, visibleByDefault? })` | Load-order markers are registered automatically |
| `discoverMarker(actorId, refId)` / `getDiscoveredMarkers(actorId)` | Discovery also happens by proximity (+20 XP) |

## State

| Function | Notes |
|---|---|
| `sendFullState(actorId)` | Resends inventory, values, progression, equipment, map, party, settlement snapshots and power armor to the player |
| `getActorState(actorId)` | The persisted per-character JSON |
| `getWorldState()` | The persisted world JSON |

## Events

The Fallout 4 layer raises `mp.onFo4...` events like SkyMP's `mp.onActivate`. Assign a function to handle one. Returning `false` from a **blockable** event cancels the action, and the player gets `Vetoed`. Returning anything else, or having no handler, allows it.

| Event | Arguments | Blockable |
|---|---|---|
| `onFo4PowerArmorEnter` | `actorId, frameRefId` | yes |
| `onFo4PowerArmorExit` | `actorId, frameRefId` | yes |
| `onFo4WorkshopAction` | `actorId, workshopRefId, action` (`"claim"`, `"enter"`, `"place"`, `"move"`, `"scrap"`, `"store"`, `"repair"`, `"assign"`) | yes |
| `onFo4UseItem` | `actorId, baseId` | yes |
| `onFo4FastTravel` | `actorId, markerRefId` | yes |
| `onFo4PvpFlagChange` | `actorId, flag` | yes |
| `onFo4WorkshopPlace` | `actorId, workshopRefId, refId, baseId` | no |
| `onFo4Craft` | `actorId, workbenchRefId, recipeId, createdBaseId, count` | no |
| `onFo4ModItem` | `actorId, baseId, modId, attached` | no |
| `onFo4Scrap` | `actorId, baseId, count` | no |
| `onFo4Trade` | `actorId, vendorId, capsDelta` | no |
| `onFo4Unlock` | `actorId, refId, "lockpick" \| "hack"` | no |
| `onFo4PerkBought` | `actorId, chartKey` | no |
| `onFo4LevelUp` | `actorId, newLevel` | no |
| `onFo4LocationDiscovered` | `actorId, markerRefId` | no |
| `onFo4PartyChange` | `partyId, op, profileId, targetProfileId` (`op` is `"invite"`, `"accept"`, `"decline"`, `"leave"`, `"kick"` or `"promote"`) | no |

With `"claimRule": "gamemode"`, players can never claim a workshop themselves. Use `setWorkshopOwner` instead.

```ts
// No power armor inside Diamond City, and a log line for every new level
mp.onFo4PowerArmorEnter = (actorId) => !isInDiamondCity(actorId);
mp.onFo4LevelUp = (actorId, level) => console.log(`${actorId} reached level ${level}`);
```

Kills still raise SkyMP's `mp.onDeath(actorId, killerId)`.

## Examples

**A stimpak on every respawn, if the player has none**

```ts
const kStimpak = 0x00023736;
mp.onRespawn = (actorId) => {
  const stims = mp.fo4.getInventory(actorId).filter((e) => e.baseId === kStimpak);
  if (stims.length === 0) {
    mp.fo4.addItem(actorId, { baseId: kStimpak }, 1);
  }
};
```

**A safe zone around Diamond City, and night-time radstorms**

```ts
mp.fo4.addPvpZone({ center: [-30000, 30000, 0], radius: 6000, mode: "safe" });
setInterval(() => {
  const t = mp.fo4.getTime();
  const night = t.gameHour >= 22 || t.gameHour < 5;
  if (night !== t.radstorm) {
    mp.fo4.setWeather({ weatherId: night ? 0x001c1d7c : 0x0001e7e0, transitionSec: 30, radstorm: night });
  }
}, 60000);
```

The form ids in the examples are for illustration. Look them up in your load order with xEdit.

## Admin-claimed settlements

```json
{ "fo4": { "workshop": { "claimRule": "gamemode" } } }
```

```ts
// Hand Sanctuary to the winning faction (a gamemode group id)
mp.fo4.setWorkshopOwner(0x000250fe, { type: "group", id: 3 });
```
