#include "GameProfile.h"
#include "Fallout4GameProfile.h"
#include "SkyrimGameProfile.h"
#include <stdexcept>

std::shared_ptr<GameProfile> CreateGameProfile(GameId game)
{
  switch (game) {
    case GameId::Skyrim:
      return std::make_shared<SkyrimGameProfile>();
    case GameId::Fallout4:
      return std::make_shared<Fallout4GameProfile>();
  }
  throw std::runtime_error("CreateGameProfile: unknown game");
}

// ---------------------------------------------------------------- Skyrim

std::vector<std::string> SkyrimGameProfile::GetDefaultLoadOrder() const
{
  return { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
           "Dragonborn.esm" };
}

GameProfile::StartPoint SkyrimGameProfile::GetDefaultStartPoint()
  const noexcept
{
  // Same as skymp5-server/ts/settings.ts default spawn
  StartPoint p;
  p.worldOrCell = 0x3c;
  p.pos[0] = 133857;
  p.pos[1] = -61130;
  p.pos[2] = 14662;
  p.angleZ = 72;
  return p;
}

const std::vector<uint32_t>& SkyrimGameProfile::GetBannedCharacterRaceIds()
  const noexcept
{
  static const std::vector<uint32_t> kIds = {
    0x000e7713, 0x00012e82, 0x001052a3, 0x00088884, 0x0008883a, 0x00088846,
    0x00108272, 0x000a82b9, 0x0008883c, 0x00088794, 0x00088845, 0x0008883d,
    0x00088844, 0x00088840, 0x000a82ba,

    /* Playable races from ArgonianRace to WoodElfRace */
    0x00013740, 0x00013741, 0x00013742, 0x00013743, 0x00013744, 0x00013745,
    0x00013746, 0x00013747, 0x00013748, 0x00013749,

    /* Mannequin */
    0x0010760a
  };
  return kIds;
}

bool SkyrimGameProfile::IsItemRecordType(std::string_view t) const noexcept
{
  return t == "WEAP" || t == "ARMO" || t == "AMMO" || t == "ALCH" ||
    t == "INGR" || t == "MISC" || t == "BOOK" || t == "SCRL" || t == "SLGM" ||
    t == "KEYM" || t == "LIGH";
}

const std::set<std::string>& SkyrimGameProfile::GetSyncedReferenceBaseTypes()
  const noexcept
{
  static const std::set<std::string> kTypes = {
    "CONT", "DOOR", "ACTI", "FURN", "FLOR", "TREE", "NPC_",
    "WEAP", "ARMO", "AMMO", "ALCH", "INGR", "MISC", "BOOK",
    "SCRL", "SLGM", "KEYM", "LIGH", "SPEL"
  };
  return kTypes;
}

// -------------------------------------------------------------- Fallout 4

std::vector<std::string> Fallout4GameProfile::GetDefaultLoadOrder() const
{
  // Base game plus the six story/workshop DLCs. Servers without a DLC list
  // their own "loadOrder".
  return { "Fallout4.esm",          "DLCRobot.esm",
           "DLCworkshop01.esm",     "DLCCoast.esm",
           "DLCworkshop02.esm",     "DLCworkshop03.esm",
           "DLCNukaWorld.esm" };
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
