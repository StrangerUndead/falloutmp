# FalloutMP

FalloutMP is an open-source multiplayer framework for Fallout 4: a server, a game client (an F4SE plugin) and a gamemode API. Its server engine, networking, persistence and scripting started as a fork of [SkyMP](https://github.com/skyrim-multiplayer/skymp), the Skyrim multiplayer mod; the Skyrim parts have been removed and everything game-specific is Fallout 4.

The target is a fully player-run wasteland. There are no NPC characters and no game factions. Traders, landlords, settlement owners and governments are all real players.

> **Status: first test build.** The server runs on real Fallout 4 data. The game client connects every system to the game (movement, animation, appearance, inventory, combat, power armor, workshops, locks, map, time and weather) but hasn't been tested in the game yet. Details are in [docs/falloutmp/STATUS.md](docs/falloutmp/STATUS.md).

## Contents

1. [What works today](#1-what-works-today)
2. [Design decisions](#2-design-decisions)
3. [Repository layout](#3-repository-layout)
4. [Building](#4-building)
5. [Running the tests](#5-running-the-tests)
6. [Running a Fallout 4 server](#6-running-a-fallout-4-server)
7. [Writing a gamemode](#7-writing-a-gamemode)
8. [The game client](#8-the-game-client)
9. [Documentation](#9-documentation)
10. [Contributing](#10-contributing)
11. [Licenses and credits](#11-licenses-and-credits)

## 1. What works today

| Area | State |
|---|---|
| Fallout 4 protocol and message registry | Done |
| Reading Fallout 4 plugins: weapons, armor, ammo, junk, components, recipes, mods, furniture, leveled lists, containers, NPCs, outfits, placed references | Done, tested with synthetic plugins |
| Inventory with weapon and armor mods, condition, loaded ammo and stolen items | Done |
| Crafting, auto-scrapping of junk, scrapping, weapon and armor modding at workbenches | Done |
| Power armor: frames, pieces, fusion core drain, jetpack, enter and exit handshake | Done |
| Settlements: claims, per-player permissions, build mode, placing, moving, scrapping, storing, power wiring, ratings, daily production | Done |
| Character: SPECIAL, health, AP, limbs, XP and levels, perks, bobbleheads, magazines | Done |
| Chems, stimpaks, radiation, addictions | Done |
| Ranged combat checks: fire rate, ammo, range, shot log, lag-compensated hits, damage and resistances | Done |
| Movement relay and replay; optional speed checks (off by default) | Done |
| Barter, server-rolled lockpicking and terminal hacking | Done |
| Parties and PvP flags and zones | Done |
| Containers, corpse loot, ground drops, leveled loot | Done |
| Shared clock and weather, map discovery, fast travel | Done |
| Saving characters and the world | Done |
| Gamemode API `mp.fo4.*` and `mp.onFo4...` events | Done |
| TypeScript client services for all of the above (`falloutmp-client`) | Done, tested against a fake game |
| Game client core: connection, login, message codec, JavaScript host (`fallout4-platform/core`) | Done; two headless clients join a test server and see each other move |
| F4SE plugin (`fallout4-platform/plugin`): every system above, other players as puppets, a diagnostics probe | Written, built by CI, **not tested in game yet** |
| Rent, player shops, raid windows | Not started; design under discussion |

## 2. Design decisions

These choices are current and recorded in [STATUS.md](docs/falloutmp/STATUS.md):

- **Player-run world.** There are no NPC characters or game factions. NPCs stay off with `"npcEnabled": false`. If NPCs are ever turned on, human NPCs are still blocked by default (`fo4.npc.humanNpcs: false`).
- **Server authority.** The server decides inventory, crafting, damage, locks, trades and ownership. Clients send requests, and the game changes only when the server answers.
- **Speed limits off.** Movement isn't checked against speeds until real speeds are measured in game (`fo4.movement.enforceSpeed: false`). The server still drops out-of-order packets, ignores dead players, and corrects cell changes that didn't go through the server.
- **Ownership.** Settlement claims and permissions exist. City rent and wasteland ownership rules are still being decided.

## 3. Repository layout

| Path | What it is |
|---|---|
| `falloutmp-server/` | The server: C++ core (`cpp/`) and the Node.js wrapper and gamemode API (`ts/`) |
| `falloutmp-server/cpp/server_guest_lib/fo4/` | The Fallout 4 game layer: every system in section 1 |
| `falloutmp-server/cpp/server_guest_lib/game_profile/` | The Fallout 4 game profile (constants the server engine needs) |
| `falloutmp-server/cpp/messages/Fo4Messages.h` | Fallout 4 network messages (ids 64–120) |
| `falloutmp-server/ts/fo4.ts` | The typed `mp.fo4` gamemode API |
| `libespm/` | Plugin reader; Fallout 4 records live in `src/fo4/` |
| `papyrus-vm/` | The server's Papyrus virtual machine |
| `falloutmp-client/` | The client script: session and sync services in TypeScript, and the `falloutPlatform` native API contract |
| `fallout4-platform/` | The game side: client core (network, codec, QuickJS), the F4SE plugin, the headless test bot |
| `unit/` | C++ unit tests (Catch2) |
| `docs/falloutmp/` | The FalloutMP plan, feature specs, guides and status |
| `tools/` | Helper scripts, including the vcpkg download workaround |

## 4. Building

The server and the tests build on Linux and Windows. The F4SE plugin needs Windows and MSVC.

### 4.1 Requirements

All systems:
- 64-bit Node.js 18 or newer (22 is tested), and Yarn: `npm install --global yarn`.
- CMake 3.19 or newer.
- About 4 GB RAM and 22 GB disk.

Linux:
- Ubuntu 22.04 or 24.04. Alpine and Arch are known not to work.
- Clang 15 or newer (18 is tested). GCC isn't supported.
- Ninja, autoconf, automake, libtool, `autoconf-archive`, `pkg-config`, bison and flex.

```sh
sudo apt install clang ninja-build autoconf automake autoconf-archive libtool \
  pkg-config bison flex
```

Windows:
- Visual Studio 2022 with C++ and .NET desktop development.
- Python 3.9 or newer.

### 4.2 Get the code

```sh
git clone https://github.com/StrangerUndead/falloutmp.git
cd falloutmp
git checkout claude/fallout4-port-research   # current development branch
git submodule update --init --recursive
```

### 4.3 Build on Linux

The `build.sh` wrapper picks Clang and Ninja for you:

```sh
./build.sh --configure -DCMAKE_BUILD_TYPE=RelWithDebInfo
./build.sh --build --parallel $(nproc)
```

The first configure builds the vcpkg dependencies, which takes 10–25 minutes. Later builds are incremental. Build only the unit tests with `./build.sh --build --target=unit`.

After adding new source files, re-run the configure step, because CMake collects sources with globs.

If vcpkg can't download GitHub archives (HTTP 403 behind a restricted proxy), use the asset script in `tools/`:

```sh
export X_VCPKG_ASSET_SOURCES="x-script,$PWD/tools/vcpkg-github-via-git.sh {url} {sha512} {dst}"
./build.sh --configure -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

If configure stops with an `OpenMP_C` error, install `libomp-dev` and configure again. More environment notes are in [docs/falloutmp/reference/dev-environment.md](docs/falloutmp/reference/dev-environment.md).

### 4.4 Build on Windows

```sh
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

Alternatively, open `build/falloutmp.sln` in Visual Studio.

Everything the build produces goes to `build/dist/`. The server is in `build/dist/server/`.

## 5. Running the tests

C++ tests, from the build directory:

```sh
cd build
./unit/unit                  # every test
./unit/unit "[Fo4Server]"    # one area, by tag
ctest --verbose              # the full CTest run
```

The tests use synthetic plugins built in memory, so no Bethesda files are needed.

Client tests:

```sh
cd falloutmp-client
npm install
npm test        # builds, then runs Node's test runner
```

The client tests include a parity test that reads the C++ message headers. It fails if the TypeScript messages, fields or enum values drift from the server's.

Server TypeScript typecheck:

```sh
cd falloutmp-server
yarn install
npx tsc --noEmit -p .
```

## 6. Running a Fallout 4 server

### 6.1 Game data

Copy one file from your own Fallout 4 installation (`Fallout 4/Data/`) into `build/dist/server/data/`:

- `Fallout4.esm`

That's all. FalloutMP uses no DLC content: the default load order is `Fallout4.esm` alone, so neither the server nor the players need any DLC. Don't commit Bethesda files to any repository.

### 6.2 server-settings.json

Edit `build/dist/server/server-settings.json`:

```json
{
  "game": "fallout4",
  "dataDir": "data",
  "loadOrder": ["Fallout4.esm"],
  "name": "My Commonwealth",
  "port": 7777,
  "maxPlayers": 100,
  "offlineMode": true,
  "npcEnabled": false,
  "fo4": {
    "worldStatePath": "world/fo4-world.json",
    "workshop": { "claimRule": "firstClaim", "maxPerPlayer": 3 },
    "pvp": { "friendlyFire": false, "defaultFlag": false },
    "map": { "fastTravel": "discoveredOnly" },
    "movement": { "enforceSpeed": false }
  }
}
```

- **`game`** is `"fallout4"` (the default and the only value).
- **`fo4`** tunes every Fallout 4 rule: power armor, workshops, locks, combat, damage, PvP, progression, containers, map, movement and NPCs. Every key is optional.
- **Validation.** A wrong type stops the server with the setting's full path. Unknown keys, such as typos, are logged as warnings.

The full list with defaults is in [guides/server-admin.md](docs/falloutmp/guides/server-admin.md). The engine settings, such as `ip`, `master` and database options, are in [docs_server_configuration_reference.md](docs/docs_server_configuration_reference.md).

### 6.3 Start it

```sh
cd build/dist/server
node dist_back/falloutmp-server.js
```

On startup, the log should show these lines:

```
Game profile is 'fallout4', protocol prefix 'fo4-1_'
Fallout 4 world state file is 'world/fo4-world.json'
```

### 6.4 Saving and backups

| What | Where | When |
|---|---|---|
| Characters: inventory, values, level, perks, effects, equipment, map | The server database, in each character's `fo4State` field | Every 30 s and on disconnect |
| World: settlements, power armor frames, locks, parties, containers, clock, weather | The `fo4.worldStatePath` file | Every 30 s and on shutdown |

The world file is written to a temporary file and then renamed, so a crash can't leave it half-written. Back it up together with the server database.

## 7. Writing a gamemode

Gamemodes are JavaScript or TypeScript files loaded by the server. The Fallout 4 API is `mp.fo4`:

```ts
// A stimpak on every respawn, if the player has none
const kStimpak = 0x00023736;
mp.onRespawn = (actorId) => {
  if (!mp.fo4.getInventory(actorId).some((e) => e.baseId === kStimpak)) {
    mp.fo4.addItem(actorId, { baseId: kStimpak }, 1);
  }
};

// No power armor inside a safe zone; returning false blocks the action
mp.onFo4PowerArmorEnter = (actorId) => !isInSafeZone(actorId); // your own check
```

The API covers items, actor values, progression, effects, power armor, settlements, vendors, locks, parties, PvP zones, time, weather and map markers. There are 17 events, and 7 of them can block the action. A failed call throws `Fo4Error` with an error code. See [guides/gamemode-api.md](docs/falloutmp/guides/gamemode-api.md).

## 8. The game client

The client has three parts:

| Part | What it is | Runs on |
|---|---|---|
| `falloutmp-client/` | All client logic in TypeScript: the session (connect, login, other players) and the sync services. Bundled into one script, `falloutmp-client.js` | Any JavaScript engine |
| `fallout4-platform/core/` | C++: the network connection, the message codec and a QuickJS engine that runs the script | Windows and Linux |
| `fallout4-platform/plugin/` | The F4SE plugin `FalloutMP.dll`: connects the core to the game through CommonLibF4 | Windows, Fallout 4 1.11.x |

**Getting the plugin.** The workflow *FalloutMP client (Windows)* (`.github/workflows/falloutmp-client-windows.yml`) builds it on every push that touches the client. The workflow *FalloutMP server (Linux)* builds the server, runs every test and uploads the server package. Download the `FalloutMP-client` artifact from the run's page. Installation steps for players are in [fallout4-platform/plugin/INSTALL.txt](fallout4-platform/plugin/INSTALL.txt).

**Building it yourself on Windows** (Visual Studio 2022, CMake 3.24+):

```bat
git submodule update --init vcpkg
vcpkg\bootstrap-vcpkg.bat
cmake -S fallout4-platform/plugin -B build-plugin -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=%CD%\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static -DVCPKG_OVERLAY_PORTS=%CD%\overlay_ports
cmake --build build-plugin --config Release --target FalloutMP
cd falloutmp-client && npm install && npm run bundle
```

**Testing a server without the game.** `fmp_bot` runs the real client script against a fake game. Two checks use it:

```sh
# In the repository: a local test server on synthetic data, two bots
(cd falloutmp-client && npm run bundle)
fallout4-platform/tools/fmp_e2e.sh build

# On a server machine with the server package: joins the running server
/opt/falloutmp-server/tools/test-join.sh
```

### 8.1 Client services

`falloutmp-client/` holds the client-side sync services: inventory, equipment, actor values, progression, effects, power armor, settlements, crafting, movement, combat, barter, locks, parties, map, and time and weather. It talks to the game only through the `FalloutPlatform` interface in `src/platform/falloutPlatform.ts`. That interface is the full list of natives the F4SE plugin implements.

```sh
cd falloutmp-client
npm install
npm run typecheck
npm test
```

How it is put together, and what still needs the game, is in [falloutmp-client/README.md](falloutmp-client/README.md).

## 9. Documentation

| Document | Read it for |
|---|---|
| [docs/falloutmp/README.md](docs/falloutmp/README.md) | The plan: how the docs are organised and how work is done |
| [docs/falloutmp/STATUS.md](docs/falloutmp/STATUS.md) | Current progress, decisions, deviations, things to verify in game |
| [docs/falloutmp/PLAN.md](docs/falloutmp/PLAN.md) | One-file summary of the whole plan |
| [guides/server-admin.md](docs/falloutmp/guides/server-admin.md) | Every server setting, persistence, logs |
| [guides/gamemode-api.md](docs/falloutmp/guides/gamemode-api.md) | `mp.fo4` functions and events |
| [guides/implementation.md](docs/falloutmp/guides/implementation.md) | How the code is put together and how to extend it |
| [docs/falloutmp/features/](docs/falloutmp/features/) | One spec per game system |
| [docs/](docs/) | Server engine reference (configuration, scripting, database drivers) |

## 10. Contributing

1. Read [docs/falloutmp/README.md](docs/falloutmp/README.md) and [STATUS.md](docs/falloutmp/STATUS.md).
2. Work on a branch, and keep the tests green.
3. Add tests with every change. C++ tests go in `unit/`, client tests in `falloutmp-client/test/`.
4. New network messages follow the checklist in [guides/implementation.md](docs/falloutmp/guides/implementation.md) §6. The parity test enforces it.
5. Update STATUS.md when you finish something.

Code style and review rules are in [CONTRIBUTING.md](CONTRIBUTING.md).

The most useful help right now is testing the client in Fallout 4 and sending the probe file and logs (see [INSTALL.txt](fallout4-platform/plugin/INSTALL.txt)).

## 11. Licenses and credits

- FalloutMP is a fork of SkyMP and keeps its licenses. Most components are under GPLv3 or AGPLv3. Each subproject has its own license file; for example, `falloutmp-server` is AGPL-3.0 and `falloutmp-client` is GPL-3.0-only. Read [TERMS.md](TERMS.md); in short, publish the source of your forks.
- Third-party code licenses are in [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES).
- Fallout 4 is © Bethesda Softworks. This project ships no Bethesda game files and needs a legal copy of the game.
- Thanks to the [SkyMP](https://github.com/skyrim-multiplayer/skymp) team, whose work this builds on.
