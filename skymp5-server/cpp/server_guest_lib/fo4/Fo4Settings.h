#pragma once
// The "fo4" block of server-settings.json (docs/falloutmp/guides/server-admin.md).
//
// Every rule a server owner may want to tune maps onto Fo4ServerSettings.
// A wrong type is a startup error naming the setting path, so a typo never
// silently runs the server with defaults. Unknown keys are reported as
// warnings, which catches misspelt names.
#include "Fo4Server.h"
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace fo4 {

struct Fo4SettingsParseResult
{
  Fo4ServerSettings settings;
  std::string worldStatePath = "world/fo4-world.json";
  // fo4.npc: human NPCs are off by default
  bool humanNpcs = false;
  std::vector<std::string> blockedNpcRaces = { "HumanRace",
                                               "HumanChildRace" };
  // Paths like "fo4.workshop.maxObjcts" that no setting matches
  std::vector<std::string> unknownKeys;
};

// Throws std::runtime_error on a value of the wrong type.
Fo4SettingsParseResult ParseFo4Settings(const nlohmann::json& fo4Block);

}
