// Headless FalloutMP client: runs the real client bundle in the QuickJS host
// against a fake game, so a server can be joined without Fallout 4.
//
//   fmp_bot --script falloutmp-client.js [--host 127.0.0.1] [--port 7777]
//           [--profile 1] [--seconds 10] [--walk]
//
// --walk moves the fake player in a circle around its spawn point. When it
// exits the bot prints one JSON line with what it saw (fmp_e2e.sh reads it).
#include "Runtime.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

using nlohmann::json;

namespace {

struct Puppet
{
  json spawn;
  std::array<double, 3> pos{};
  int moves = 0;
  json appearance;
  int graphWrites = 0;
  std::vector<std::string> animEvents;
};

class FakeGame : public fmp::Game
{
public:
  std::string host = "127.0.0.1";
  int port = 7777;
  int profile = 1;
  bool walk = false;
  double nowMs = 0;

  bool placed = false;
  uint32_t worldOrCell = 0;
  std::array<double, 3> spawnPos{};
  double yaw = 0;
  std::map<uint32_t, Puppet> puppets;
  uint32_t nextPuppet = 0xff800000;
  int teleports = 0;
  int looksMenuOpened = 0;
  bool emitLooksMenuClosed = false;
  double nextAnimEventMs = 3000;
  std::vector<std::pair<std::string, json>> pendingEvents;
  int puppetsSpawned = 0;
  int puppetsDeleted = 0;
  std::vector<std::string> notifications;

  std::array<double, 3> PlayerPos() const
  {
    if (!walk) {
      return spawnPos;
    }
    double t = nowMs / 1000.0; // 300 u/s on a 300 u radius
    return { spawnPos[0] + 300 * std::cos(t), spawnPos[1] + 300 * std::sin(t),
             spawnPos[2] };
  }

  bool CallNative(const std::string& name, const json& a, json& r) override
  {
    if (name == "getClientConfig") {
      r = { { "serverIp", host },
            { "serverPort", port },
            { "profileId", profile } };
    } else if (name == "getMovementFo4") {
      if (!placed || a.at(0).get<uint32_t>() != 0x14) {
        r = nullptr;
        return true;
      }
      auto p = PlayerPos();
      r = { { "worldOrCell", worldOrCell },
            { "pos", { p[0], p[1], p[2] } },
            { "yaw", yaw },
            { "aimPitch", 0 },
            { "aimHeading", 0 },
            { "speed", walk ? 300 : 0 },
            { "direction", 0 },
            { "velZ", 0 },
            { "flags", 0 } };
    } else if (name == "teleportActor") {
      uint32_t ref = a.at(0);
      if (ref == 0x14) {
        spawnPos = a.at(1).get<std::array<double, 3>>();
        yaw = a.at(2);
        worldOrCell = a.at(3);
        placed = true;
        ++teleports;
      } else if (auto it = puppets.find(ref); it != puppets.end()) {
        it->second.pos = a.at(1).get<std::array<double, 3>>();
        ++it->second.moves;
      }
    } else if (name == "setActorTransform") {
      if (auto it = puppets.find(a.at(0).get<uint32_t>());
          it != puppets.end()) {
        it->second.pos = a.at(1).get<std::array<double, 3>>();
        ++it->second.moves;
      }
    } else if (name == "spawnPuppet") {
      uint32_t ref = nextPuppet++;
      ++puppetsSpawned;
      puppets[ref].spawn = a.at(0);
      puppets[ref].pos = a.at(0).at("pos").get<std::array<double, 3>>();
      r = ref;
    } else if (name == "deletePuppet") {
      puppetsDeleted +=
        static_cast<int>(puppets.erase(a.at(0).get<uint32_t>()));
    } else if (name == "showNotification") {
      notifications.push_back(a.at(0));
      Log("info", "[notification] " + a.at(0).get<std::string>());
    } else if (name == "getAppearanceFo4") {
      // A face for this profile: hair colour and sex differ per bot
      r = { { "isFemale", profile % 2 == 0 },
            { "raceId", 0x13746 },
            { "hairColorId", 0x1000 + profile },
            { "facialHairColorId", 0 },
            { "headTextureSetId", 0 },
            { "headPartIds", json::array({ 0x2000, 0x2001 }) },
            { "bodyMorph", { 0.3, 0.3, 0.4 } },
            { "morphRegions", json::array() },
            { "morphSliders",
              json::array({ { { "key", 7 }, { "value", 0.5 } } }) },
            { "faceRegions", json::array() },
            { "faceMorphIntensity", 1.0 },
            { "tints", json::array() },
            { "skinTone", 0xFFCCAA88 } };
    } else if (name == "applyAppearanceFo4") {
      if (auto it = puppets.find(a.at(0).get<uint32_t>());
          it != puppets.end()) {
        it->second.appearance = a.at(1);
      }
      r = true;
    } else if (name == "openLooksMenu") {
      ++looksMenuOpened;
      emitLooksMenuClosed = true; // the bot "edits" instantly
    } else if (name == "closeLooksMenu") {
    } else if (name == "getGraphVariables") {
      r = json::array();
      if (a.at(0).get<uint32_t>() == 0x14) {
        double speed = walk ? 300.0 : 0.0;
        r.push_back(
          { { "name", "Speed" }, { "type", 0 }, { "value", speed } });
        r.push_back(
          { { "name", "IsSneaking" }, { "type", 2 }, { "value", 0 } });
      }
    } else if (name == "setGraphVariables") {
      if (auto it = puppets.find(a.at(0).get<uint32_t>());
          it != puppets.end()) {
        ++it->second.graphWrites;
      }
    } else if (name == "notifyAnimationGraph") {
      if (auto it = puppets.find(a.at(0).get<uint32_t>());
          it != puppets.end()) {
        it->second.animEvents.push_back(a.at(1));
      }
      r = true;
    } else if (name == "setMovementFlags" || name == "setAimAngles") {
      // nothing to show headless
    } else {
      return false;
    }
    return true;
  }

  void Log(const std::string& level, const std::string& text) override
  {
    if (level != "trace") {
      std::fprintf(stderr, "[%s] %s\n", level.data(), text.data());
    }
  }
};

}

int main(int argc, char** argv)
{
  FakeGame game;
  std::string scriptPath;
  double seconds = 10;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error("missing value for " + arg);
      }
      return argv[++i];
    };
    if (arg == "--script") {
      scriptPath = next();
    } else if (arg == "--host") {
      game.host = next();
    } else if (arg == "--port") {
      game.port = std::stoi(next());
    } else if (arg == "--profile") {
      game.profile = std::stoi(next());
    } else if (arg == "--seconds") {
      seconds = std::stod(next());
    } else if (arg == "--walk") {
      game.walk = true;
    } else {
      std::cerr << "unknown argument " << arg << "\n";
      return 2;
    }
  }
  std::ifstream f(scriptPath, std::ios::binary);
  if (!f) {
    std::cerr << "unable to read --script " << scriptPath << "\n";
    return 2;
  }
  std::stringstream ss;
  ss << f.rdbuf();

  fmp::Runtime runtime(game, {});
  if (!runtime.LoadScript(ss.str(), scriptPath)) {
    return 1;
  }
  runtime.Emit("gameReady", json::object());

  auto start = std::chrono::steady_clock::now();
  while (true) {
    auto elapsed = std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - start)
                     .count();
    if (elapsed > seconds * 1000) {
      break;
    }
    game.nowMs = elapsed;
    if (game.emitLooksMenuClosed) {
      game.emitLooksMenuClosed = false;
      runtime.Emit(
        "platformEvent",
        { { "name", "looksMenuClosed" }, { "data", json::object() } });
    }
    if (game.walk && game.placed && elapsed >= game.nextAnimEventMs) {
      game.nextAnimEventMs = elapsed + 2000;
      runtime.Emit(
        "platformEvent",
        { { "name", "animationEvent" },
          { "data", { { "actor", 0x14 }, { "name", "jumpStart" } } } });
    }
    runtime.Tick(elapsed);
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }

  json summary = { { "profile", game.profile },
                   { "connected", runtime.IsConnected() },
                   { "placed", game.placed },
                   { "worldOrCell", game.worldOrCell },
                   { "spawnPos", game.spawnPos },
                   { "teleports", game.teleports },
                   { "puppetsSpawned", game.puppetsSpawned },
                   { "puppetsDeleted", game.puppetsDeleted },
                   { "jsErrors", runtime.Js().ErrorCount() },
                   { "looksMenuOpened", game.looksMenuOpened },
                   { "notifications", game.notifications },
                   { "puppets", json::array() } };
  for (auto& [ref, p] : game.puppets) {
    summary["puppets"].push_back({ { "ref", ref },
                                   { "pos", p.pos },
                                   { "moves", p.moves },
                                   { "appearance", p.appearance },
                                   { "graphWrites", p.graphWrites },
                                   { "animEvents", p.animEvents } });
  }
  std::cout << summary.dump() << std::endl;
  return 0;
}
