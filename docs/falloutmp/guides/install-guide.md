# Installing and updating FalloutMP

This guide covers two jobs:

- **Part 1, the server:** installing a FalloutMP server on a Linux machine (a VPS or your own PC), keeping it running, backing it up and updating it.
- **Part 2, the client:** getting Fallout 4 ready to join a server, and updating it.

> **Test build.** The game client has not been tested in the game yet. Expect problems, and use a separate save for multiplayer. The probe (§2.6) records what the developers need to fix things.

## Contents

- [Part 1: the server](#part-1-the-server)
  - [1.1 What you need](#11-what-you-need)
  - [1.2 Download the server package](#12-download-the-server-package)
  - [1.3 First install](#13-first-install)
  - [1.4 Settings](#14-settings)
  - [1.5 Firewall](#15-firewall)
  - [1.6 First start](#16-first-start)
  - [1.7 Test it without the game](#17-test-it-without-the-game)
  - [1.8 Run it as a service](#18-run-it-as-a-service)
  - [1.9 Everyday commands](#19-everyday-commands)
  - [1.10 Backups](#110-backups)
  - [1.11 Updating the server](#111-updating-the-server)
  - [1.12 Troubleshooting the server](#112-troubleshooting-the-server)
- [Part 2: the client](#part-2-the-client)
  - [2.1 What you need](#21-what-you-need)
  - [2.2 Download the client](#22-download-the-client)
  - [2.3 Install](#23-install)
  - [2.4 Point it at a server](#24-point-it-at-a-server)
  - [2.5 Join](#25-join)
  - [2.6 The probe: run it once](#26-the-probe-run-it-once)
  - [2.7 Client settings](#27-client-settings)
  - [2.8 Updating the client](#28-updating-the-client)
  - [2.9 Moving to another PC, uninstalling](#29-moving-to-another-pc-uninstalling)
  - [2.10 Troubleshooting the client](#210-troubleshooting-the-client)
- [Sharing the client with your players](#sharing-the-client-with-your-players)

---

# Part 1: the server

## 1.1 What you need

| | |
|---|---|
| Machine | Linux x86-64: Ubuntu 24.04 or newer, or Debian 13. 2 CPU cores and 4 GB RAM are plenty for a small server |
| Software | Node.js 18 or newer, and the OpenMP runtime (`libomp5`) |
| Game data | `Fallout4.esm`, copied from **your own** Fallout 4 installation. No DLC is used or needed |
| Network | One open UDP port (7777 by default) |

Never upload Bethesda files (such as `Fallout4.esm`) to GitHub or anywhere public.

## 1.2 Download the server package

The server is built automatically by GitHub on every change.

1. Sign in to GitHub. Downloads from Actions need an account.
2. Open the repository, then **Actions** → **FalloutMP server (Linux)**.
3. Open the newest run on `main` with a green tick.
4. Scroll to **Artifacts** and download **falloutmp-server-linux**.
5. That zip contains `falloutmp-server-linux-x64.tar.gz`. Unzip it on your PC.

Upload the `.tar.gz` to the server, for example to your home folder. From your PC:

```sh
scp falloutmp-server-linux-x64.tar.gz youruser@your-server-ip:~
```

An SFTP program (WinSCP, FileZilla) works too.

The package unpacks to a folder called `falloutmp-server/` containing:

| Path | What it is |
|---|---|
| `start.sh` | Starts the server (checks your settings and game data first) |
| `falloutmp.service` | systemd unit, to run the server in the background (§1.8) |
| `server-settings.example.json` | Example settings; your real settings go in `server-settings.json` |
| `dist_back/`, `scam_native.node`, `package.json` | The server program |
| `data/` | Put `Fallout4.esm` here |
| `tools/test-join.sh` | Joins the server with two test clients, no game needed (§1.7) |
| `INSTALL.md` | This guide |
| `VERSION` | The commit the package was built from |

The package never contains `server-settings.json`, game data or saves. Unpacking a newer package over an install therefore updates the program and leaves everything else alone.

## 1.3 First install

Run these on the server. They install to `/opt/falloutmp-server` and run the server as its own user, `falloutmp`.

```sh
# 1. Runtime (on Ubuntu 24.04 the OpenMP package may be called libomp5-18)
sudo apt update
sudo apt install -y nodejs libomp5
node --version                      # must be v18 or newer

# 2. Unpack and create the service user
sudo tar -xzf ~/falloutmp-server-linux-x64.tar.gz -C /opt
sudo useradd --system --home /opt/falloutmp-server falloutmp
cd /opt/falloutmp-server
sudo cp server-settings.example.json server-settings.json

# 3. Game data: upload Fallout4.esm from your PC, then move it into place
#    (on your PC, the file is in Steam/steamapps/common/Fallout 4/Data/)
#    scp "Fallout4.esm" youruser@your-server-ip:~
sudo mv ~/Fallout4.esm /opt/falloutmp-server/data/

# 4. Give the service user the folder
sudo chown -R falloutmp:falloutmp /opt/falloutmp-server
```

## 1.4 Settings

Edit the settings with `sudo nano /opt/falloutmp-server/server-settings.json`. The example looks like this:

```json
{
  "game": "fallout4",
  "dataDir": "data",
  "loadOrder": ["Fallout4.esm"],
  "name": "My Commonwealth",
  "port": 7777,
  "maxPlayers": 100,
  "master": "",
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

| Setting | Meaning |
|---|---|
| `name` | Your server's name |
| `port` | The UDP port players connect to. If you change it, open that port instead of 7777 (§1.5) and tell players |
| `maxPlayers` | Player limit |
| `offlineMode` | `true`: players join by IP, with no accounts. Each player is recognised by the `profile-id` in their client settings. Keep this `true` unless your gamemode handles logins |
| `npcEnabled` | `false`: no NPCs. FalloutMP is a player-run world |
| `fo4` | Fallout 4 rules: workshops, PvP, fast travel, power armor, combat and more. Every key is optional |
| `startPoints` | Where new characters spawn. The default is Sanctuary Hills |

Every setting, with its default, is listed in [server-admin.md](server-admin.md). A wrong value stops the server with the setting's name in the log.

A gamemode (your own rules, written in JavaScript) goes in `/opt/falloutmp-server/gamemode.js`. It is optional and reloaded on change. See [gamemode-api.md](gamemode-api.md).

## 1.5 Firewall

Players need one UDP port:

```sh
sudo ufw allow 7777/udp
sudo ufw status
```

If your VPS provider has its own firewall (a "security group" or "network firewall" in the control panel), open UDP 7777 there too. If the server runs at home, forward UDP 7777 on your router to the server's local IP.

## 1.6 First start

Start it in the foreground first, so you see any problem straight away:

```sh
sudo -u falloutmp /opt/falloutmp-server/start.sh
```

A healthy start shows lines like these:

```
Game profile is 'fallout4', protocol prefix 'fo4-1_'
Fallout 4 world state file is 'world/fo4-world.json'
... workshops ...
listening on ...:7777
```

Stop it with **Ctrl+C**. `start.sh` refuses to start, with a message, if `server-settings.json` or `Fallout4.esm` is missing.

## 1.7 Test it without the game

While the server runs (in the foreground in another terminal, or as a service), run:

```sh
/opt/falloutmp-server/tools/test-join.sh
```

Two headless test clients join, get characters, and check that they see each other move. It takes about 15 seconds and ends with:

```
PASS: the server is joinable
```

To test from another machine through the firewall, copy the `tools/` folder there and run `tools/test-join.sh your-server-ip`. The tools are Linux programs.

## 1.8 Run it as a service

As a service, the server starts at boot, restarts if it crashes, and saves the world when stopped.

```sh
sudo cp /opt/falloutmp-server/falloutmp.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now falloutmp
systemctl status falloutmp          # should say "active (running)"
```

If you installed somewhere other than `/opt/falloutmp-server`, or as another user, edit `User`, `WorkingDirectory` and `ExecStart` in `/etc/systemd/system/falloutmp.service` first.

## 1.9 Everyday commands

| Task | Command |
|---|---|
| Live log | `journalctl -u falloutmp -f` |
| Log since the last start | `journalctl -u falloutmp -b` |
| Stop / start / restart | `sudo systemctl stop falloutmp` / `start` / `restart` |
| Apply new settings | Edit `server-settings.json`, then `sudo systemctl restart falloutmp` |
| Installed version | `cat /opt/falloutmp-server/VERSION` |

## 1.10 Backups

Everything worth keeping is in the install folder:

| Path | What it holds |
|---|---|
| `world/` | Characters (inventory, levels, perks, appearance) and `fo4-world.json` (settlements, power armor, locks, parties, containers, time and weather) |
| `server-settings.json` | Your settings |
| `gamemode.js` | Your gamemode, if you have one |

The server saves every 30 seconds and on stop. Back up with a short stop, so the files are consistent:

```sh
sudo systemctl stop falloutmp
sudo tar -czf ~/falloutmp-backup-$(date +%F).tar.gz -C /opt/falloutmp-server world server-settings.json $(cd /opt/falloutmp-server && ls gamemode.js 2>/dev/null)
sudo systemctl start falloutmp
```

To restore, stop the server, unpack the backup into `/opt/falloutmp-server`, run `sudo chown -R falloutmp:falloutmp /opt/falloutmp-server`, and start it again.

## 1.11 Updating the server

1. Download the newest package (§1.2) and upload it to the server.
2. Stop the server and take a backup (§1.10).
3. Unpack the new package over the install. Your settings, data and saves are not in the package, so they stay:

   ```sh
   sudo systemctl stop falloutmp
   sudo tar -xzf ~/falloutmp-server-linux-x64.tar.gz -C /opt
   sudo chown -R falloutmp:falloutmp /opt/falloutmp-server
   sudo cp /opt/falloutmp-server/falloutmp.service /etc/systemd/system/
   sudo systemctl daemon-reload
   sudo systemctl start falloutmp
   ```

4. Check the log (`journalctl -u falloutmp -f`) and run `tools/test-join.sh`.

**Coming from the first test package** (the one whose `start.sh` ran `dist_back/skymp5-server.js`): follow the same steps. The new package replaces `start.sh` with one that runs `dist_back/falloutmp-server.js`. Afterwards you may delete the old files `dist_back/skymp5-server.js` and `dist_back/skymp5-server.js.map`.

**Clients and servers must match.** A client and a server only connect if they speak the same protocol version. When an update changes the protocol, players also need the new client (§2.8), and old clients are refused with "Invalid password". To be safe, update the server and hand out the matching client together.

## 1.12 Troubleshooting the server

| Problem | What to do |
|---|---|
| `Missing data/Fallout4.esm` | Copy `Fallout4.esm` into `/opt/falloutmp-server/data/` and run the `chown` command again |
| `error while loading shared libraries: libomp.so.5` | `sudo apt install -y libomp5` (or `libomp5-18`) |
| `SyntaxError` or `Unexpected token` at start | Node.js is too old. Install Node 18 or newer (`node --version`) |
| `'... setting must be ...'` and the server stops | A setting has the wrong type. The message names it; fix it in `server-settings.json` |
| `test-join.sh` passes on the server but players can't connect | The firewall: open UDP 7777 in `ufw` **and** in your provider's control panel, or forward it on your router |
| `Permission denied` on saves | Run `sudo chown -R falloutmp:falloutmp /opt/falloutmp-server` |
| The service keeps restarting | `journalctl -u falloutmp -b` shows the error just before each restart |

---

# Part 2: the client

## 2.1 What you need

| | |
|---|---|
| Game | Fallout 4, current Steam or GOG version (1.11.x, the Anniversary Edition update). The older 1.10.984 and 1.10.163 versions are not supported |
| F4SE | Fallout 4 Script Extender for your game version: <https://f4se.silverlock.org> |
| Address Library | Address Library for F4SE Plugins, the 1.11.x version: <https://www.nexusmods.com/fallout4/mods/47327> |
| Recommended | Buffout 4 NG (its crash logs name the failing function) |
| System | Windows. The plugin is a Windows DLL |

Install F4SE by copying its files next to `Fallout4.exe`. Install the Address Library like any mod, so its files end up in `Data\F4SE\Plugins\`.

## 2.2 Download the client

1. Sign in to GitHub.
2. Open the repository, then **Actions** → **FalloutMP client (Windows)**.
3. Open the newest run on `main` with a green tick.
4. Under **Artifacts**, download **FalloutMP-client** and unzip it.

The zip contains:

```
INSTALL.txt
Data\F4SE\Plugins\FalloutMP.dll
Data\F4SE\Plugins\FalloutMP.example.json
Data\F4SE\Plugins\FalloutMP\falloutmp-client.js
```

## 2.3 Install

1. Find your Fallout 4 folder, the one with `Fallout4.exe`. On Steam: right-click Fallout 4 → **Manage** → **Browse local files**.
2. Copy the zip's `Data` folder into it. Merge it with the existing `Data` folder when Windows asks.
3. In `Data\F4SE\Plugins\`, rename `FalloutMP.example.json` to `FalloutMP.json`. This is your settings file.

With Mod Organizer 2 or Vortex, install the zip as a mod instead, then do step 3 inside the mod. Manual install is simpler while testing.

## 2.4 Point it at a server

Open `Data\F4SE\Plugins\FalloutMP.json` in Notepad and fill in the server's address:

```json
{
  "server-ip": "203.0.113.10",
  "server-port": 7777,
  ...
}
```

Leave the rest as it is. On the first start, the plugin adds a `"profile-id"` line. That number is **your character** on the server: keep it, and back up `FalloutMP.json`.

## 2.5 Join

1. Start the game with `f4se_loader.exe` in the Fallout 4 folder. On Steam you can launch it through Steam once F4SE is set up.
2. Load a save you don't mind, or start a new game. Use a separate save for multiplayer.
3. The client connects when the save has loaded. A new character on the server opens the character editor first. Other players appear as you get close to them.

The game's console (`~` key) shows `[FalloutMP]` messages if something goes wrong.

## 2.6 The probe: run it once

The probe records how the game behaves on your PC: movement values, animations, what the plugin can do. Developers use it to fix things. It needs no server.

1. In `FalloutMP.json`, set `"probe": true`.
2. Start the game and load a save. After about 5 seconds a test character appears next to you for a moment.
3. When the message **"recording 60 s"** appears: walk, run, sprint, sneak, jump, draw your weapon, aim, fire, reload, and open the Pip-Boy.
4. When it says **"probe finished"**, send these two files to the developers:
   - `Data\F4SE\Plugins\FalloutMP-probe.json`
   - `Documents\My Games\Fallout4\F4SE\FalloutMP.log`
5. Set `"probe"` back to `false`.

## 2.7 Client settings

All settings live in `Data\F4SE\Plugins\FalloutMP.json`.

| Setting | Meaning |
|---|---|
| `server-ip`, `server-port` | The server to join |
| `profile-id` | Your character on the server. Generated once; don't change it |
| `probe` | `true` runs the probe (§2.6) instead of joining |
| `features` | Switch any system off with `false`, e.g. `"combat": false`. Names: `session`, `movement`, `puppets`, `animation`, `appearance`, `inventory`, `equipment`, `actorValues`, `progression`, `effects`, `combat`, `powerArmor`, `workshop`, `locks`, `map`, `world` |
| `puppet-move` | How other players move on your screen: `"native"` (default) or `"papyrus"`. Try `"papyrus"` if other players don't move |
| `puppet-ai` | How other players' characters are kept still: `"package"` (default), `"restrained"` or `"off"` |
| `puppet-warp` | `"controller"` (default) or `"reference"` |
| `puppet-base` | Base NPC used for other players, default `"0x7"` |

If one system misbehaves, switch it off in `features` and keep playing. Send the log so it can be fixed.

## 2.8 Updating the client

1. Download the newest client (§2.2).
2. Copy its `Data` folder into the Fallout 4 folder again and overwrite the files.

The zip has no `FalloutMP.json`, only the example, so your settings and character are kept. If a new version adds settings, compare `FalloutMP.example.json` with your file.

## 2.9 Moving to another PC, uninstalling

- **Another PC:** install as above, then copy your old `FalloutMP.json` over the new one. The `profile-id` in it brings your character along.
- **Uninstall:** delete `Data\F4SE\Plugins\FalloutMP.dll` and the `Data\F4SE\Plugins\FalloutMP\` folder. Keep `FalloutMP.json` if you might come back.

## 2.10 Troubleshooting the client

| Problem | What to do |
|---|---|
| The game crashes on load | Remove `FalloutMP.dll` and check that F4SE and the Address Library match your game version. Send the Buffout 4 crash log |
| Nothing happens, no FalloutMP messages | Check you started with `f4se_loader.exe`. Look in `Documents\My Games\Fallout4\F4SE\FalloutMP.log`; if it doesn't exist, F4SE didn't load the plugin (version mismatch) |
| "Can't reach the server" | Check `server-ip` and `server-port`. Ask the server owner to check the firewall (UDP 7777) |
| "The server refused the connection: Invalid password" | The client and server are different versions. Update both to builds from the same day (§1.11, §2.8) |
| "Client script missing" | `Data\F4SE\Plugins\FalloutMP\falloutmp-client.js` is missing. Copy the `Data` folder again |
| Other players stand still | Set `"puppet-move": "papyrus"` and try again |
| One system misbehaves | Switch it off in `features` (§2.7) and send the log |

**Logs to send with a bug report:**

- `Documents\My Games\Fallout4\F4SE\FalloutMP.log` (the plugin);
- the Buffout 4 crash log, after a crash;
- the Papyrus log, if asked. To turn it on, add this to `Documents\My Games\Fallout4\Fallout4Custom.ini`:

  ```ini
  [Papyrus]
  bEnablePapyrusLogging=1
  bEnableLogging=1
  bEnableTrace=1
  bLoadDebugInformation=1
  ```

  The log is then in `Documents\My Games\Fallout4\Logs\Script\Papyrus.0.log`.

---

# Sharing the client with your players

Players need three things from you: the client zip, the server's IP and port, and this guide.

- GitHub Actions downloads need a GitHub account and expire after 90 days. To share with players, download the zip yourself and host it somewhere they can reach, such as a Discord channel or a GitHub Release.
- Hand out a client built from the same `main` commit as your server (compare the run dates, or the server's `VERSION` file with the commit of the client run).
