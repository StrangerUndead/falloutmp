// FalloutMP.dll: the F4SE entry point.
//
// Data/F4SE/Plugins/FalloutMP.dll        this plugin
// Data/F4SE/Plugins/FalloutMP.json       settings (Config.h)
// Data/F4SE/Plugins/FalloutMP/falloutmp-client.js   the client script
//
// At kGameDataReady the client script loads and the feature modules
// install (modules/Modules.h). The session starts when a save is loaded
// or a new game begins, and runs every frame from PlayerCharacter::Update.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "Hooks.h"
#include "Platform.h"
#include "modules/Modules.h"

namespace {
constexpr auto kConfigPath = "Data\\F4SE\\Plugins\\FalloutMP.json";
constexpr auto kScriptPath =
  "Data\\F4SE\\Plugins\\FalloutMP\\falloutmp-client.js";

bool g_ready = false;

void OnMessage(F4SE::MessagingInterface::Message* a_msg)
{
  auto& p = fmp::Platform::Get();
  switch (a_msg->type) {
    case F4SE::MessagingInterface::kGameDataReady:
      if (static_cast<bool>(a_msg->data) && !p.Started()) {
        if (p.Start(fmp::PluginConfig::Load(kConfigPath), kScriptPath)) {
          fmp::modules::InstallAll(p);
          fmp::hooks::InstallPlayerUpdate();
          REX::INFO("{} natives registered", p.NativeNames().size());
        }
      }
      break;
    case F4SE::MessagingInterface::kPostLoadGame:
    case F4SE::MessagingInterface::kNewGame:
      if (p.Started() && !g_ready) {
        g_ready = true;
        p.EmitLifecycle("gameReady");
      }
      break;
    case F4SE::MessagingInterface::kPreSaveGame:
      if (p.Started()) {
        p.RunBeforeSave();
      }
      break;
    case F4SE::MessagingInterface::kPostSaveGame:
      if (p.Started()) {
        p.RunAfterSave();
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
