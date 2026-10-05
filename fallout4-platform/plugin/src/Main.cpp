// FalloutMP.dll: the F4SE entry point.
//
// Data/F4SE/Plugins/FalloutMP.dll        this plugin
// Data/F4SE/Plugins/FalloutMP.json       server address and profile id
// Data/F4SE/Plugins/FalloutMP/falloutmp-client.js   the client script
//
// The client starts once a save is loaded (or a new game started) and
// runs every frame from PlayerCharacter::Update on the main thread.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "Fo4Game.h"
#include "Runtime.h"

#include <chrono>
#include <fstream>
#include <memory>
#include <sstream>

namespace {
constexpr auto kConfigPath = "Data\\F4SE\\Plugins\\FalloutMP.json";
constexpr auto kScriptPath = "Data\\F4SE\\Plugins\\FalloutMP\\falloutmp-client.js";

std::unique_ptr<fmp::Fo4Game> g_game;
std::unique_ptr<fmp::Runtime> g_runtime;
bool g_ready = false;
const auto g_start = std::chrono::steady_clock::now();

double NowMs()
{
  return std::chrono::duration<double, std::milli>(
           std::chrono::steady_clock::now() - g_start)
    .count();
}

void StartRuntime()
{
  std::ifstream f(kScriptPath, std::ios::binary);
  if (!f) {
    REX::ERROR("Missing {}: reinstall FalloutMP", kScriptPath);
    RE::SendHUDMessage::ShowHUDMessage(
      "FalloutMP: client script missing, see FalloutMP.log", nullptr, false,
      true);
    return;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  g_game = std::make_unique<fmp::Fo4Game>(fmp::PluginConfig::Load(kConfigPath));
  g_runtime = std::make_unique<fmp::Runtime>(*g_game, fmp::Runtime::Options{});
  if (!g_runtime->LoadScript(ss.str(), "falloutmp-client.js")) {
    REX::ERROR("The client script failed to load");
  }
}

// PlayerCharacter::Update (Actor vfunc 0xCF) runs once per frame while a
// game is loaded.
struct PlayerUpdateHook
{
  static void Thunk(RE::PlayerCharacter* a_this, float a_delta)
  {
    original(a_this, a_delta);
    if (g_runtime && g_ready) {
      try {
        g_runtime->Tick(NowMs());
      } catch (std::exception& e) {
        REX::ERROR("Tick failed: {}", e.what());
      }
    }
  }
  static inline REL::Relocation<decltype(&Thunk)> original;

  static void Install()
  {
    REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::PlayerCharacter[0] };
    original = vtbl.write_vfunc(0xCF, Thunk);
    REX::INFO("Installed the player update hook");
  }
};

void OnMessage(F4SE::MessagingInterface::Message* a_msg)
{
  switch (a_msg->type) {
    case F4SE::MessagingInterface::kGameDataReady:
      if (static_cast<bool>(a_msg->data) && !g_runtime) {
        StartRuntime();
        PlayerUpdateHook::Install();
      }
      break;
    case F4SE::MessagingInterface::kPostLoadGame:
    case F4SE::MessagingInterface::kNewGame:
      if (g_runtime && !g_ready) {
        g_ready = true;
        g_runtime->Emit("gameReady", nlohmann::json::object());
      }
      break;
    case F4SE::MessagingInterface::kPreSaveGame:
      // Puppets must not end up in the player's save; the session
      // recreates them from the server's next updates.
      if (g_game) {
        g_game->DeleteAllPuppets();
      }
      break;
    case F4SE::MessagingInterface::kPostSaveGame:
      if (g_runtime) {
        g_runtime->Emit("saveFinished", nlohmann::json::object());
      }
      break;
    default:
      break;
  }
}
}

F4SE_PLUGIN_VERSION = []() noexcept {
  F4SE::PluginVersionData v{};
  v.PluginVersion({ FMP_VERSION_MAJOR, FMP_VERSION_MINOR, FMP_VERSION_PATCH, 0 });
  v.PluginName("FalloutMP");
  v.AuthorName("FalloutMP");
  v.UsesAddressLibrary(true);
  v.UsesSigScanning(false);
  v.IsLayoutDependent(true);
  v.HasNoStructUse(false);
  v.CompatibleVersions({ F4SE::RUNTIME_LATEST });
  return v;
}();

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
  F4SE::Init(a_f4se, { .logName = "FalloutMP" });
  REX::INFO("FalloutMP {}.{}.{} loading", FMP_VERSION_MAJOR, FMP_VERSION_MINOR,
            FMP_VERSION_PATCH);
  auto messaging = F4SE::GetMessagingInterface();
  if (!messaging || !messaging->RegisterListener(OnMessage)) {
    REX::ERROR("Unable to register for F4SE messages");
    return false;
  }
  return true;
}
