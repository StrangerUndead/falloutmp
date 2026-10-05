#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

// Identifies the game a server or client is running. FalloutMP supports
// Fallout 4 only; the id stays so the protocol prefix and the "game" server
// setting have one place to live.
enum class GameId : uint8_t
{
  Fallout4 = 1,
};

// Protocol version prefix. A client and a server must use the same prefix.
// Bump it on every incompatible protocol change.
constexpr std::string_view kProtocolPrefixFallout4 = "fo4-1_";

constexpr std::string_view GetProtocolPrefix(GameId) noexcept
{
  return kProtocolPrefixFallout4;
}

constexpr std::string_view GameIdToString(GameId) noexcept
{
  return "fallout4";
}

// Accepts the values of the "game" server setting.
inline std::optional<GameId> ParseGameId(std::string_view s) noexcept
{
  if (s == "fallout4" || s == "fo4" || s == "Fallout4") {
    return GameId::Fallout4;
  }
  return std::nullopt;
}
