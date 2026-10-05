#pragma once
// Gamemode API for the Fallout 4 layer (F31 extensibility).
//
// One JSON command entry point keeps the native (N-API) surface tiny and
// lets every command be unit-tested without Node. The TypeScript wrapper
// in skymp5-server/ts/fo4 gives gamemodes typed functions on top.
//
//   result = Fo4Call(server, "addItem", {"actorId":..., "item":{...},
//                                        "count":3})
//   result is {"ok":true, ...} or {"ok":false, "error":"..."}
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace fo4 {

class Fo4Server;

nlohmann::json Fo4Call(Fo4Server& server, const std::string& command,
                       const nlohmann::json& args);

// Command names, for documentation and the TypeScript wrapper test.
std::vector<std::string> Fo4ListCommands();

}
