#pragma once
// Data/F4SE/Plugins/FalloutMP.json
//
//   {
//     "server-ip": "203.0.113.10", "server-port": 7777,
//     "profile-id": 123456,                 // generated on first start
//     "puppet-base": "0x7",                 // base NPC of other players
//     "puppet-move": "native",              // or "papyrus"
//     "puppet-warp": "controller",          // or "reference"
//     "puppet-ai": "package",               // or "restrained", "off"
//     "puppet-npc-targets": false,          // local NPCs may target puppets
//     "puppet-local-death": false,          // puppets may die from local hits
//     "probe": false,                       // write FalloutMP-probe.json
//     "features": { "combat": false }       // switch a module off
//   }
//
// Every feature module (modules/Modules.h) can be switched off; its natives
// are then missing and the client script falls back to defaults.
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace fmp {

struct PluginConfig
{
  std::string serverIp;
  int serverPort = 7777;
  uint32_t profileId = 0;
  uint32_t puppetBaseId = 0x7;
  std::string puppetMove = "native";
  bool probe = false;
  nlohmann::json features = nlohmann::json::object();
  nlohmann::json raw = nlohmann::json::object();

  bool FeatureEnabled(std::string_view name) const;

  // Reads the file; a missing profile id is generated once and written
  // back so the player keeps the same character on this server.
  static PluginConfig Load(const std::string& path);
};

}
