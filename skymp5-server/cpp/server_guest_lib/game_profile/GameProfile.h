#pragma once
#include "GameId.h"
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

// GameProfile collects every game-specific constant and rule that shared
// server code needs (REF-003, REF-004, SRV-001). Shared code asks the
// profile instead of hardcoding Skyrim values, so the same server core can
// run Skyrim or Fallout 4.
//
// Rules for adding members:
// - Only add what shared code needs. Feature code that is Fallout-4-only
//   lives in server_guest_lib/fo4 and does not go through the profile.
// - Keep the Skyrim values identical to the literals they replace, so
//   Skyrim behaviour stays byte-identical.
class GameProfile
{
public:
  struct StartPoint
  {
    uint32_t worldOrCell = 0;
    float pos[3] = { 0, 0, 0 };
    float angleZ = 0;
  };

  virtual ~GameProfile() = default;

  virtual GameId GetGameId() const noexcept = 0;
  std::string_view GetProtocolPrefix() const noexcept
  {
    return ::GetProtocolPrefix(GetGameId());
  }
  std::string_view GetName() const noexcept
  {
    return GameIdToString(GetGameId());
  }

  // Master files loaded when the server settings have no "loadOrder".
  virtual std::vector<std::string> GetDefaultLoadOrder() const = 0;

  // File extension of the game's archives (".bsa" or ".ba2").
  virtual std::string_view GetArchiveExtension() const noexcept = 0;

  // Well-known form ids.
  virtual uint32_t GetPlayerBaseFormId() const noexcept = 0; // NPC_ "Player"
  virtual uint32_t GetPlayerRefFormId() const noexcept = 0;  // PlayerRef
  virtual uint32_t GetDefaultWorldspace() const noexcept = 0;
  virtual uint32_t GetCurrencyFormId() const noexcept = 0; // Gold / Caps

  // Start point used when the gamemode does not choose one.
  virtual StartPoint GetDefaultStartPoint() const noexcept = 0;

  // Races players must not pick (creature races, mannequins, ...).
  virtual const std::vector<uint32_t>& GetBannedCharacterRaceIds()
    const noexcept = 0;

  // Record types that become inventory items.
  virtual bool IsItemRecordType(std::string_view type) const noexcept = 0;

  // Record types the server loads as interactive references.
  virtual const std::set<std::string>& GetSyncedReferenceBaseTypes()
    const noexcept = 0;

  // Maximum distance (game units) between an actor and a reference it
  // activates, before validation tolerance is added.
  virtual float GetActivationReach() const noexcept = 0;

  // Multiplier applied to the movement speed model (REF-010).
  virtual float GetMaxRunSpeed() const noexcept = 0;
};

std::shared_ptr<GameProfile> CreateGameProfile(GameId game);
