#pragma once
// fmp::Game on top of Fallout 4 (CommonLibF4). Every method runs on the
// game's main thread: Runtime::Tick is called from the player's update.
#include "Game.h"
// No windows.h here: CommonLibF4 declares its own Win32 API (REX::W32)
// and REX::ERROR clashes with the ERROR macro.

#include <cstdint>
#include <string>
#include <unordered_set>

namespace fmp {

struct PluginConfig
{
  std::string serverIp;
  int serverPort = 7777;
  uint32_t profileId = 0;
  // Base NPC the puppets of other players are created from. 0x7 (the
  // player's own base) gives a player-like clone until appearance sync.
  uint32_t puppetBaseId = 0x7;
  // "native" moves puppets with SetLocationOnReference every frame;
  // "papyrus" uses ObjectReference.SetPosition (slower, but goes through
  // the engine's own path) in case the native move doesn't show in game.
  std::string puppetMove = "native";

  // Data/F4SE/Plugins/FalloutMP.json. A missing profile id is generated
  // once and written back, so a player keeps the same character.
  static PluginConfig Load(const std::string& path);
};

class Fo4Game : public Game
{
public:
  explicit Fo4Game(PluginConfig config);

  bool CallNative(const std::string& name, const nlohmann::json& args,
                  nlohmann::json& result) override;
  void Log(const std::string& level, const std::string& text) override;

  // Removes every puppet (before a save, on disconnect of the plugin).
  void DeleteAllPuppets();

private:
  nlohmann::json GetMovement(uint32_t actor);
  void Teleport(uint32_t ref, const nlohmann::json& pos, float yawDeg,
                uint32_t worldOrCell);
  void SetTransform(uint32_t ref, const nlohmann::json& pos, float yawDeg);
  uint32_t SpawnPuppet(const nlohmann::json& spawn);
  void DeletePuppet(uint32_t ref);

  PluginConfig config;
  std::unordered_set<uint32_t> puppets;
  uint64_t frame = 0;
};

}
