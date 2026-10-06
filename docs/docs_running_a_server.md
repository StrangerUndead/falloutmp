# Running a Server

The full Fallout 4 server guide is [falloutmp/guides/server-admin.md](falloutmp/guides/server-admin.md). This page covers the basics.

## Installation

Build the project from source (see the repository README) or take the `falloutmp-server-linux` artifact of the *FalloutMP server (Linux)* workflow. The server is in `build/dist/server`. It runs on Linux and Windows (64-bit); on Windows use `launch_server.bat`, elsewhere `node dist_back/falloutmp-server.js`.

## Configuration

Copy `Fallout4.esm` from your game's `Data` folder into the server's `data` folder, then edit `server-settings.json` in `build/dist/server`:

```json5
{
  "game": "fallout4",
  "dataDir": "data",
  "loadOrder": ["Fallout4.esm"],
  "offlineMode": true,
  "name": "My Server"
}
```

- You may find out your public IP here http://api.ipify.org
- You need to have ports open. Talk to your Internet provider support if you want to open ports. Status of each port can be checked here https://www.yougetsignal.com/tools/open-ports/. You can learn about ports that are really used by the server on [Server Configuration Reference](docs_server_configuration_reference.md) page or to simplify think that it may use any of available ports and protocols.
- If you use `LogMeIn Hamachi` or similar software then just type an IP address you got assigned from it. Your friends who share a "local" network with you will be able to connect, players from the Internet will not.
