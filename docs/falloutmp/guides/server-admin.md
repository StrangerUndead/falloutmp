# Running a FalloutMP server

First-time installation, updates and backups are in [install-guide.md](install-guide.md). This page lists every setting.

This guide covers configuring and running a Fallout 4 server built from this repository. Everything a SkyMP server owner knows still applies. This page lists only what is new or different.

## 1. Game data

The server reads the game's plugins. It never ships them. Copy them from your own Fallout 4 installation into the server's data directory.

- **Required:** `Fallout4.esm`. Nothing else.
- **Default load order:** `Fallout4.esm` only. FalloutMP uses no DLC content by default, so players don't need any DLC either.
- **Adding plugins:** list them in `loadOrder` (section 2) only if you deliberately want them. Every player's game must then match.

Never commit Bethesda data to a repository.

## 2. server-settings.json

A Fallout 4 server needs one new top-level key, plus an optional `fo4` block.

```json
{
  "game": "fallout4",
  "dataDir": "data",
  "loadOrder": ["Fallout4.esm"],
  "name": "My Commonwealth",
  "port": 7777,
  "maxPlayers": 100,
  "fo4": {
    "worldStatePath": "world/fo4-world.json",
    "workshop": { "claimRule": "firstClaim", "maxPerPlayer": 3 },
    "pvp": { "friendlyFire": false, "defaultFlag": false },
    "map": { "fastTravel": "discoveredOnly" }
  }
}
```

- **`game`** is `"fallout4"`, the default and the only value. The connection password carries the protocol prefix (`fo4-1_`), so clients of another protocol version can't join.
- **`fo4`** tunes the Fallout 4 rules. Every key is optional.
- **`master`** is the URL of a server list and account service. There is none by default. Set `"offlineMode": true` (players join by IP, no accounts) unless your gamemode handles logins.

Settings are checked at startup. A value of the wrong type stops the server with the setting's full path:

```
server setting 'fo4.pvp.friendlyFire' must be a boolean
```

A key the server doesn't know, such as a typo, is logged as a warning and ignored:

```
Unknown server setting 'fo4.workshop.maxObjcts' (ignored)
```

### 2.1 The `fo4` block

Defaults are shown. Distances are in game units, where 1 unit is about 1.4 cm. Times are in milliseconds unless the name says otherwise.

**Top level**

| Key | Default | Meaning |
|---|---|---|
| `worldStatePath` | `world/fo4-world.json` | File for settlements, frames, locks, parties, containers, clock and weather |
| `activationReach` | 300 | Max distance to workbenches, frames and locks |
| `reachSlack` | 64 | Latency allowance added to every reach check |
| `killXpBase` | 20 | Kill XP per target level |
| `timeWeatherIntervalMs` | 10000 | How often clocks and weather are re-sent |
| `workshopSnapshotChunk` | 200 | Settlement objects per snapshot message |
| `powerArmorBlockedBipedSlots` | 15872 (0x3E00) | Apparel slots blocked inside power armor |

**`powerArmor`**

| Key | Default | Meaning |
|---|---|---|
| `allowStealing` | false | Players may enter frames someone else owns |
| `allowInCombat` | true | Entering and exiting is allowed in combat |
| `exitWhileFalling` | false | Exiting is allowed while airborne |
| `keepOnDisconnect` | false | A player who logs out stays in the frame |
| `transitionTimeoutMs` | 10000 | Rollback if the client never confirms the animation |
| `pieceDamageMultPlayer` | 1 | Piece condition loss from player attacks |
| `pieceDamageMultNpc` | 3 | Piece condition loss from NPC attacks |
| `fusionCoreId` | 0x75FE4 | Fusion core item |
| `jetpackKeywordId` | 0 | Keyword that marks a jetpack torso mod |
| `drainPerSecond` | idle 0, walk 1/2400, run 1/1200, sprint 1/400, jetpack 1/150 | Fraction of a core used per second |

**`workshop`**

| Key | Default | Meaning |
|---|---|---|
| `claimRule` | `"firstClaim"` | `"firstClaim"`, `"adminOnly"` or `"gamemode"` (only through the gamemode) |
| `maxPerPlayer` | 5 | Settlements one player may own |
| `maxObjects` | 1000 | Objects per settlement |
| `defaultBudget` | 100 | Build budget of a new settlement |
| `maxBuildDistance` | 4096 | How far from the builder an object may be placed |
| `allowScale` | false | Scaled placement allowed |
| `minScale`, `maxScale` | 0.1, 10 | Scale range when allowed |
| `maxRequestsPerSecond` | 10 | Build requests per player per second |
| `maxWireLength` | 1500 | Longest wire |
| `maxWiresPerConnector` | 6 | Wires per connection point |
| `buildModeLeaveAreaMs` | 5000 | Time outside the area before build mode ends |
| `foodItemId`, `waterItemId` | 0 | Items settlements produce daily (0 = none) |

**`locks`**

| Key | Default | Meaning |
|---|---|---|
| `bobbyPinId` | 0xA | Bobby pin item |
| `sessionTimeoutMs` | 120000 | Abandoned lockpick or hack sessions close |
| `lockoutMs` | 10000 | Terminal lockout after the last failed attempt |
| `hackAttempts` | 4 | Attempts per terminal session |
| `perPerceptionPoint`, `perLocksmithRank` | 0.02, 0.05 | Lockpick chance bonuses |
| `perIntelligencePoint`, `perHackerRank` | 0.02, 0.05 | Hacking chance bonuses |
| `minChance`, `maxChance` | 0.05, 0.95 | Bounds of every roll |

**`combat`**

| Key | Default | Meaning |
|---|---|---|
| `fireRateTolerance` | 0.15 | Allowed fire rate above the weapon's stats |
| `maxRewindMs` | 250 | Lag compensation window for hit claims |
| `shotLifetimeMs` | 3000 | How long a shot may still register hits |
| `rangeTolerance` | 1.25 | Allowed hit distance over the weapon's range |
| `infiniteAmmo` | false | Shots don't use ammo |

**`damage`**

| Key | Default | Meaning |
|---|---|---|
| `physicalDamageFactor` | 0.15 | Vanilla damage curve factor |
| `armorReductionExp` | 0.365 | Vanilla damage curve exponent |
| `pvpDamageFactor` | 0.15 | Damage curve factor for player-vs-player hits |
| `pvpDamageMult` | 1 | Extra multiplier on player-vs-player damage |
| `headshotMult` | 2 | Head hit multiplier |
| `critsIgnoreResistance` | false | Critical hits ignore resistances |

**`pvp`**

| Key | Default | Meaning |
|---|---|---|
| `maxPartySize` | 8 | Party size |
| `inviteTtlMs` | 60000 | Invites expire |
| `flagCooldownMs` | 300000 | Time between PvP flag changes |
| `combatTagMs` | 30000 | Flag can't be dropped this long after PvP damage |
| `friendlyFire` | false | Party members can hurt each other |
| `xpShareRadius` | 4096 | Party members this close share kill XP |
| `defaultFlag` | false | New players start flagged for PvP |

PvP zones (`safe`, `open`, `flagged`) are added by the gamemode with `mp.fo4.addPvpZone`.

**`progression`**

| Key | Default | Meaning |
|---|---|---|
| `xpMult` | 1 | Multiplier on all XP |
| `intelligenceXpBonusPerPoint` | 0.03 | Vanilla INT bonus |
| `maxLevel` | 65535 | Level cap |
| `creationPoints` | 21 | SPECIAL points at character creation |
| `specialMax`, `specialMaxWithBobblehead` | 10, 11 | SPECIAL caps |
| `perkPointsPerLevel` | true | One perk point per level |

**`containers`**

| Key | Default | Meaning |
|---|---|---|
| `reach` | 364 | Container reach |
| `markStolen` | true | Items taken from owned containers are marked stolen |

**`npc`**

| Key | Default | Meaning |
|---|---|---|
| `humanNpcs` | false | Spawn human NPCs from the load order (settlers, raiders, named characters) |
| `blockedRaces` | `["HumanRace", "HumanChildRace"]` | Race editor ids that never spawn; add `"GhoulRace"` or synth races to block more |

NPCs only load when SkyMP's top-level `"npcEnabled": true` is set. With it on, creatures, robots and super mutants spawn, but human NPCs don't unless `humanNpcs` is true. The race is read through each NPC's template chain, so templated raiders and settlers are caught too.

**`movement`** (anti-cheat speed model, F01)

| Key | Default | Meaning |
|---|---|---|
| `enforceSpeed` | false | Check movement against the speed limits below. Off until the speeds are measured in game |
| `walkSpeed` | 200 | Walking speed |
| `sprintSpeed` | 700 | Fastest normal movement (sprint) |
| `encumberedSpeed` | 200 | Limit while carrying too much |
| `powerArmorSprintSpeed` | 600 | Fastest movement in power armor |
| `jumpUpSpeed` | 450 | Rising speed of jumps and steep climbs |
| `jetpackUpSpeed` | 900 | Extra rising speed with a working jetpack |
| `terminalFallSpeed` | 6000 | Fastest fall |
| `speedTolerance` | 1.25 | Multiplier on every limit |
| `distanceSlack` | 96 | Units allowed on top of the limit |
| `jitterMs` | 150 | Allowance for bunched packets |
| `violationThreshold` | 3 | Score that triggers a correction |
| `scoreDecayPerSec` | 0.5 | How fast the score falls |
| `teleportGraceMs` | 2000 | Old packets ignored after a teleport or door |
| `speedMultAvId` | 0 | Actor value scaling the limits (chems, perks); 0 = look up `SpeedMult` in the load order |

Speed limits are off by default (`enforceSpeed: false`). Out-of-order packets, dead actors and client-side cell changes are still rejected. When limits are on, speeds are in game units per second. A sample outside the model is dropped and scored, and sustained speed is also checked over the last second. Once the score passes the threshold, the player is teleported back to the last accepted position. Cell changes only happen through the server (doors, fast travel, respawn), so a client-side cell change is corrected at once.

**`map`**

| Key | Default | Meaning |
|---|---|---|
| `discoveryRadius` | 1000 | Players discover map markers this close |
| `fastTravel` | `"discoveredOnly"` | `"off"`, `"discoveredOnly"` or `"any"` |
| `combatCooldownMs` | 30000 | No fast travel this long after combat |

## 3. Persistence

| What | Where | When |
|---|---|---|
| Per-character state: inventory with mods and condition, actor values, level, perks, effects, equipment, discovered markers | The actor's change form, dynamic field `fo4State` | Every 30 s and on disconnect |
| World: settlements and their objects, wires and settlers, power armor frames, locks and terminals, parties, opened and ground containers, clock, weather | `fo4.worldStatePath` | Every 30 s and on shutdown |

The world file is written to a temporary file and then renamed, so a crash never leaves a half-written file. Back it up together with the SkyMP database.

On the first start, settlements, power armor frames, locks, terminals and map markers are registered from the load order. Later starts keep what the world file holds and only add new records, for example after you add a plugin.

## 4. What players experience

- **Settlements.** A workbench can be claimed by the first player who asks, subject to `claimRule` and `maxPerPlayer`. The owner grants other players permissions: build, scrap, container, craft, assign and admin. Everyone nearby sees placed objects, including players who arrive later.
- **Power armor.** A frame is a world object. Pieces stay on the frame or on its wearer, and fusion cores drain on the server. Other players see the frame and the pieces.
- **Crafting and modding.** Workbenches check reach, perks and components on the server, and junk is scrapped automatically as in vanilla.
- **Locks and terminals.** The server rolls every attempt. A modified client can't open locks.
- **Barter.** Prices follow the vanilla Charisma formula. A trade fails if the vendor's stock or prices changed since the quote.
- **Time and weather** are the same for everyone.

## 5. Logs worth watching

| Message | Meaning |
|---|---|
| `Game profile is 'fallout4'` | The `game` setting was read |
| `Fallout 4 world state file is '...'` | Persistence is on |
| `Unknown server setting ...` | A typo in the `fo4` block |
| `WeaponFire rejected ...` (debug) | A client fired faster than its weapon allows, or without ammo |
| `Equip rejected ...` (debug) | A client tried to equip something it doesn't have |
| `Movement correction for ...` | A client moved outside the speed model and was put back |

## 6. Not yet available

The server systems above are tested on Linux without the game. The in-game client still needs the `fallout4-platform` plugin (Windows). Until it exists, a Fallout 4 server can't be joined from the game. See `docs/falloutmp/STATUS.md` for the current state.
