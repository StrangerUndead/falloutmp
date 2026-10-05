#pragma once
// The plugin's side of the client: owns the Runtime (QuickJS + network),
// and is the fmp::Game the script calls into.
//
// Feature modules (modules/*.cpp) plug in through three things:
//   - RegisterNative(name, fn): a FalloutPlatform native
//     (falloutmp-client/src/platform/falloutPlatform.ts). `fn` gets the
//     arguments as a JSON array in TypeScript order and returns the result
//     (null for void / undefined).
//   - Emit(name, data): a PlatformEvents event to the script.
//   - OnFrame(fn): work every frame on the main thread.
//
// Threading: natives and frame callbacks run on the game's main thread.
// Emit, Resolve and QueueTask may be called from any thread (event sinks,
// Papyrus callbacks); they are delivered on the next frame.
//
// No windows.h here or in modules: CommonLibF4 declares its own Win32 API
// (REX::W32) and REX::ERROR clashes with the ERROR macro.
#include "Config.h"
#include "Game.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fmp {

class Runtime;

using Json = nlohmann::json;
using NativeFn = std::function<Json(const Json& args)>;

class Platform final : public Game
{
public:
  static Platform& Get();

  // Creates the runtime and loads the client script (kGameDataReady).
  bool Start(PluginConfig config, const std::string& scriptPath);
  bool Started() const { return runtime != nullptr; }
  const PluginConfig& Config() const { return config; }
  bool Feature(std::string_view name) const
  {
    return config.FeatureEnabled(name);
  }

  void RegisterNative(const std::string& name, NativeFn fn);
  bool HasNative(const std::string& name) const;
  std::vector<std::string> NativeNames() const;

  // Async natives: a NativeFn returns Pending(NewAsyncId()) and later
  // calls Resolve(id, value). The script's Promise settles then.
  uint32_t NewAsyncId();
  static Json Pending(uint32_t id) { return Json{ { "pending", id } }; }
  void Resolve(uint32_t id, Json value);

  // A PlatformEvents event (name and payload as in falloutPlatform.ts).
  void Emit(const std::string& name, Json data);
  // A lifecycle event for runtime/main.ts ("gameReady", "saveFinished").
  void EmitLifecycle(const std::string& kind, Json payload = Json::object());
  void QueueTask(std::function<void()> task);
  void OnFrame(std::function<void(float dtSec)> fn);
  // Around game saves (kPreSaveGame / kPostSaveGame): network-only
  // references (puppets, server-placed objects) leave the world before the
  // save and come back after it.
  void OnBeforeSave(std::function<void()> fn);
  void OnAfterSave(std::function<void()> fn);
  void RunBeforeSave();
  void RunAfterSave();

  // From the player update hook, once per frame.
  void Tick(float dtSec);

  // Game
  bool CallNative(const std::string& name, const Json& args,
                  Json& result) override;
  void Log(const std::string& level, const std::string& text) override;

  // Seconds since the plugin started (monotonic).
  double NowMs() const;
  uint64_t FrameCount() const { return frame; }

private:
  Platform() = default;
  void Drain();

  PluginConfig config;
  std::unique_ptr<Runtime> runtime;
  std::unordered_map<std::string, NativeFn> natives;
  std::vector<std::function<void(float)>> frameCallbacks;
  std::vector<std::function<void()>> beforeSave, afterSave;

  struct Queued
  {
    enum class Kind
    {
      Event,
      Lifecycle,
      Resolve,
      Task
    } kind;
    std::string name;
    Json data;
    uint32_t id = 0;
    std::function<void()> task;
  };
  std::mutex queueMutex;
  std::vector<Queued> queue;
  uint32_t nextAsyncId = 1;
  uint64_t frame = 0;
};

}
