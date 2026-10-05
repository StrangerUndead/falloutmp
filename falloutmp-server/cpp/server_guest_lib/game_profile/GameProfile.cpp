#include "GameProfile.h"
#include "Fallout4GameProfile.h"
#include <stdexcept>

std::shared_ptr<GameProfile> CreateGameProfile(GameId game)
{
  switch (game) {
    case GameId::Fallout4:
      return std::make_shared<Fallout4GameProfile>();
  }
  throw std::runtime_error("CreateGameProfile: unknown game");
}

// -------------------------------------------------------------- Fallout 4

std::vector<std::string> Fallout4GameProfile::GetDefaultLoadOrder() const
{
  // Base game only: FalloutMP uses no DLC content by default, so a server
  // needs nothing beyond Fallout4.esm. Servers may still list their own
  // "loadOrder".
  return { "Fallout4.esm" };
}

GameProfile::StartPoint Fallout4GameProfile::GetDefaultStartPoint()
  const noexcept
{
  // Sanctuary Hills bridge area in the Commonwealth. Servers normally set
  // "startPoints"; this default only has to be a valid exterior location.
  StartPoint p;
  p.worldOrCell = 0x3c;
  p.pos[0] = -79800;
  p.pos[1] = 90500;
  p.pos[2] = 7800;
  p.angleZ = 180;
  return p;
}

const std::vector<uint32_t>& Fallout4GameProfile::GetBannedCharacterRaceIds()
  const noexcept
{
  // Players are HumanRace (0x13746) only. Everything else is banned by the
  // character validator through an allow-list in F03; this list stays empty
  // so shared code never needs a Fallout 4 deny-list.
  static const std::vector<uint32_t> kIds = {};
  return kIds;
}

bool Fallout4GameProfile::IsItemRecordType(std::string_view t) const noexcept
{
  return t == "WEAP" || t == "ARMO" || t == "AMMO" || t == "ALCH" ||
    t == "MISC" || t == "BOOK" || t == "KEYM" || t == "NOTE" || t == "INGR";
}

const std::set<std::string>& Fallout4GameProfile::GetSyncedReferenceBaseTypes()
  const noexcept
{
  static const std::set<std::string> kTypes = {
    "CONT", "DOOR", "ACTI", "FURN", "FLOR", "TERM", "NPC_", "WEAP",
    "ARMO", "AMMO", "ALCH", "MISC", "BOOK", "KEYM", "NOTE", "LIGH"
  };
  return kTypes;
}
