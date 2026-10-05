#pragma once
// What the client runtime needs from the game. The F4SE plugin implements
// it on top of CommonLibF4; tests and the headless bot use fakes.
//
// Natives are called by name from JavaScript (FalloutPlatform contract,
// falloutmp-client/src/platform/falloutPlatform.ts). Arguments arrive as a
// JSON array in the order of the TypeScript signature.
#include <nlohmann/json.hpp>
#include <string>

namespace fmp {

class Game
{
public:
  virtual ~Game() = default;

  // Returns false if the native isn't implemented; throws on bad arguments.
  virtual bool CallNative(const std::string& name, const nlohmann::json& args,
                          nlohmann::json& result) = 0;

  virtual void Log(const std::string& level, const std::string& text) = 0;
};

}
