#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

// Identifies which Bethesda game a server or client is running.
// Shared by the server, the client plugin and tests (REF-003, NET-001).
enum class GameId : uint8_t
{
  Skyrim = 0,
  Fallout4 = 1,
};

// Protocol version prefixes. A client and a server must use the same prefix,
// so a Skyrim client can never join a Fallout 4 server and vice versa.
// Bump the Fallout 4 prefix on every incompatible protocol change.
constexpr std::string_view kProtocolPrefixSkyrim = "7_";
constexpr std::string_view kProtocolPrefixFallout4 = "fo4-1_";

constexpr std::string_view GetProtocolPrefix(GameId game) noexcept
{
  switch (game) {
    case GameId::Fallout4:
      return kProtocolPrefixFallout4;
    case GameId::Skyrim:
    default:
      return kProtocolPrefixSkyrim;
  }
}

constexpr std::string_view GameIdToString(GameId game) noexcept
{
  switch (game) {
    case GameId::Fallout4:
      return "fallout4";
    case GameId::Skyrim:
    default:
      return "skyrim";
  }
}

// Accepts the values of the "game" server setting.
inline std::optional<GameId> ParseGameId(std::string_view s) noexcept
{
  if (s == "skyrim" || s == "skyrimse" || s == "Skyrim") {
    return GameId::Skyrim;
  }
  if (s == "fallout4" || s == "fo4" || s == "Fallout4") {
    return GameId::Fallout4;
  }
  return std::nullopt;
}
