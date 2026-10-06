// Probe: records what the other modules can only learn in the game, into
// Data/F4SE/Plugins/FalloutMP-probe.json. Off unless FalloutMP.json has
// "probe": true. Needs no server.
//
// After a save loads it runs a self-test of the natives (read-only calls
// on the player, then a test puppet next to the player), and records for
// 60 seconds:
//   - the player's movement (getMovementFo4) and graph variables every
//     200 ms, so locomotion values can be compared with what the player did;
//   - every platform event the modules emit (animation events, equip,
//     shots, hits, menus), with timestamps.
// The player is asked (HUD) to walk, run, sprint, sneak, jump, draw, fire,
// reload and open the Pip-Boy meanwhile.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "Modules.h"
#include "Platform.h"

#include <cmath>
#include <format>
#include <fstream>

namespace fmp::modules {

namespace {
constexpr auto kProbePath = "Data\\F4SE\\Plugins\\FalloutMP-probe.json";
constexpr double kRecordMs = 60000;
constexpr double kSampleMs = 200;

// Superset of the animation service's list (reference §4.5, T0 + T1)
const char* const kProbeVariables[] = {
  "Speed",
  "SpeedSmoothed",
  "Direction",
  "TurnDelta",
  "VelocityZ",
  "AimPitchCurrent",
  "AimHeadingCurrent",
  "Pitch",
  "iSyncIdleLocomotion",
  "iSyncTurnState",
  "iSyncWalkRun",
  "iSyncSneakWalkRun",
  "iSyncLocomotionSpeed",
  "iSyncDirection",
  "iSyncForwardBackward",
  "iSyncJumpState",
  "iSyncSprintState",
  "bInJumpState",
  "IsSneaking",
  "iIsInSneak",
  "IsSprinting",
  "iSyncSightedState",
  "iSyncGunDown",
  "iSyncReadyAlertRelaxed",
  "bAimActive",
  "bAimEnabled",
  "isFiring",
  "isReloading",
  "IsAttackReady",
  "iAttackState",
  "iMeleeState",
  "bIsThrowing",
  "IsBlocking",
  "iWantBlock",
  "IsStaggering",
  "staggerMagnitude",
  "staggerDirection",
  "iGetUpType",
  "weaponSpeedMult",
  "ReloadSpeedMult",
  "iWeaponChargeMode",
};

enum class Phase
{
  WaitForGame,
  SelfTest,
  PuppetCheck,
  Recording,
  Done
};

struct State
{
  Phase phase = Phase::WaitForGame;
  double phaseStartMs = 0;
  double lastSampleMs = 0;
  uint32_t puppet = 0;
  Json report = Json::object();
} g;

// Calls a native through the registry and records the outcome.
Json Try(Platform& p, Json& results, const std::string& name, const Json& args)
{
  Json entry = { { "native", name }, { "args", args } };
  if (!p.HasNative(name)) {
    entry["status"] = "missing";
    results.push_back(entry);
    return nullptr;
  }
  try {
    Json r;
    p.CallNative(name, args, r);
    entry["status"] = "ok";
    entry["result"] = r;
    results.push_back(entry);
    return r;
  } catch (std::exception& e) {
    entry["status"] = "error";
    entry["error"] = e.what();
    results.push_back(entry);
    return nullptr;
  }
}

Json PlayerSample(Platform& p)
{
  Json vars = Json::array();
  for (auto name : kProbeVariables) {
    vars.push_back(name);
  }
  Json s = { { "tMs", p.NowMs() - g.phaseStartMs } };
  Json r;
  try {
    if (p.HasNative("getMovementFo4")) {
      p.CallNative("getMovementFo4", Json::array({ game::kPlayerRef }), r);
      s["movement"] = r;
    }
    if (p.HasNative("getGraphVariables")) {
      p.CallNative("getGraphVariables",
                   Json::array({ game::kPlayerRef, vars }), r);
      s["graph"] = r;
    }
  } catch (std::exception& e) {
    s["error"] = e.what();
  }
  s["firstPerson"] = [] {
    auto camera = RE::PlayerCamera::GetSingleton();
    return camera && camera->QCameraEquals(RE::CameraStates::kFirstPerson);
  }();
  return s;
}

void Header(Platform& p)
{
  auto& r = g.report;
  r["runtime"] = p.RuntimeVersion();
  r["f4se"] = p.F4seVersion();
  r["plugin"] = std::format("{}.{}.{}", FMP_VERSION_MAJOR, FMP_VERSION_MINOR,
                            FMP_VERSION_PATCH);
  r["natives"] = p.NativeNames();
  r["config"] = p.Config().raw;
  r["config"].erase("profile-id");
  Json plugins = Json::array();
  if (auto dh = RE::TESDataHandler::GetSingleton()) {
    for (auto file : dh->compiledFileCollection.files) {
      if (file) {
        plugins.push_back(std::string(file->GetFilename()));
      }
    }
    for (auto file : dh->compiledFileCollection.smallFiles) {
      if (file) {
        plugins.push_back(std::string(file->GetFilename()) + " (light)");
      }
    }
  }
  r["gamePlugins"] = plugins;
  r["events"] = Json::array();
  r["samples"] = Json::array();
}

void SelfTest(Platform& p)
{
  Json results = Json::array();
  const uint32_t me = game::kPlayerRef;
  Try(p, results, "getClientConfig", Json::array());
  Try(p, results, "getMovementFo4", Json::array({ me }));
  Try(p, results, "getInventoryEx", Json::array({ me }));
  Try(p, results, "getEquippedItems", Json::array({ me }));
  Try(p, results, "getAppearanceFo4", Json::array({ me }));
  Try(
    p, results, "getGraphVariables",
    Json::array({ me, Json::array({ "Speed", "Direction", "IsSneaking" }) }));

  // A test puppet 150 units in front of the player
  auto player = RE::PlayerCharacter::GetSingleton();
  if (player && p.HasNative("spawnPuppet")) {
    float yaw = player->data.angle.z;
    auto pos = player->GetPosition();
    Json at = Json::array(
      { pos.x + 150.f * std::sin(yaw), pos.y + 150.f * std::cos(yaw), pos.z });
    Json spawn = { { "pos", at },
                   { "yaw", game::ToDeg(yaw) + 180.f },
                   { "worldOrCell", game::SpaceOf(player) },
                   { "baseId", 0 },
                   { "name", "FalloutMP probe" },
                   { "isFemale", false } };
    auto id = Try(p, results, "spawnPuppet", Json::array({ spawn }));
    if (id.is_number_unsigned() && id.get<uint32_t>()) {
      g.puppet = id.get<uint32_t>();
    }
  }
  g.report["selfTest"] = results;
}

void PuppetCheck(Platform& p)
{
  Json results = Json::array();
  auto actor = game::ActorOf(g.puppet);
  Json info = { { "id", g.puppet }, { "exists", actor != nullptr } };
  if (actor) {
    info["has3D"] = actor->Get3D() != nullptr;
    info["pos"] = game::ToJson(actor->GetPosition());
    auto moved = actor->GetPosition();
    moved.x += 50.f;
    Try(p, results, "setActorTransform",
        Json::array({ g.puppet, game::ToJson(moved), 0.f }));
    Try(p, results, "getGraphVariables",
        Json::array({ g.puppet, Json::array({ "Speed", "Direction" }) }));
    Try(p, results, "setGraphVariables",
        Json::array({ g.puppet,
                      Json::array({ { { "name", "Speed" },
                                      { "type", 0 },
                                      { "value", 150.f } } }) }));
    Try(p, results, "notifyAnimationGraph",
        Json::array({ g.puppet, "g_archetypeBaseStateStartInstant" }));
    Try(p, results, "getEquippedItems", Json::array({ g.puppet }));
  }
  g.report["puppet"] = info;
  g.report["puppetTests"] = results;
}

void Finish(Platform& p)
{
  if (g.puppet && p.HasNative("deletePuppet")) {
    Json r;
    try {
      p.CallNative("deletePuppet", Json::array({ g.puppet }), r);
    } catch (...) {
    }
  }
  std::ofstream out(kProbePath);
  out << g.report.dump(1) << "\n";
  REX::INFO("Probe written to {}", kProbePath);
  RE::SendHUDMessage::ShowHUDMessage(
    "FalloutMP probe finished: send Data\\F4SE\\Plugins\\FalloutMP-probe.json",
    nullptr, false, false);
}
}

void InstallProbe(Platform& p)
{
  if (!p.Config().probe) {
    return;
  }
  REX::INFO("Probe enabled: it runs after the next save loads");
  p.OnEmitted([&p](const std::string& name, const Json& data) {
    if (g.phase != Phase::Recording || g.report["events"].size() > 5000) {
      return;
    }
    g.report["events"].push_back({ { "tMs", p.NowMs() - g.phaseStartMs },
                                   { "name", name },
                                   { "data", data } });
  });
  p.OnFrame([&p](float) {
    double now = p.NowMs();
    auto next = [&](Phase ph) {
      g.phase = ph;
      g.phaseStartMs = now;
    };
    switch (g.phase) {
      case Phase::WaitForGame: {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (player && player->GetParentCell() && !game::Loading()) {
          Header(p);
          next(Phase::SelfTest);
        }
        break;
      }
      case Phase::SelfTest:
        if (now - g.phaseStartMs > 3000) { // let the world settle
          SelfTest(p);
          next(Phase::PuppetCheck);
        }
        break;
      case Phase::PuppetCheck:
        if (now - g.phaseStartMs > 2000) {
          PuppetCheck(p);
          RE::SendHUDMessage::ShowHUDMessage(
            "FalloutMP probe: recording 60 s. Walk, run, sprint, sneak, jump, "
            "draw, fire, reload, open the Pip-Boy.",
            nullptr, false, false);
          next(Phase::Recording);
        }
        break;
      case Phase::Recording:
        if (now - g.lastSampleMs >= kSampleMs) {
          g.lastSampleMs = now;
          g.report["samples"].push_back(PlayerSample(p));
        }
        if (now - g.phaseStartMs >= kRecordMs) {
          Finish(p);
          next(Phase::Done);
        }
        break;
      case Phase::Done:
        break;
    }
  });
}

}
