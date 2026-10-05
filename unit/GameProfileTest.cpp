#include "GameId.h"
#include "MsgType.h"
#include "TestUtils.hpp"
#include "WorldState.h"
#include "game_profile/Fallout4GameProfile.h"
#include "game_profile/SkyrimGameProfile.h"
#include <catch2/catch_all.hpp>
#include <string>
#include <vector>

TEST_CASE("ParseGameId accepts setting values", "[GameProfile]")
{
  REQUIRE(ParseGameId("skyrim") == GameId::Skyrim);
  REQUIRE(ParseGameId("fallout4") == GameId::Fallout4);
  REQUIRE(ParseGameId("fo4") == GameId::Fallout4);
  REQUIRE(ParseGameId("oblivion") == std::nullopt);
  REQUIRE(ParseGameId("") == std::nullopt);
}

TEST_CASE("Protocol prefixes differ per game", "[GameProfile]")
{
  REQUIRE(GetProtocolPrefix(GameId::Skyrim) == "7_");
  REQUIRE(GetProtocolPrefix(GameId::Fallout4) == "fo4-1_");
  REQUIRE(GetProtocolPrefix(GameId::Skyrim) !=
          GetProtocolPrefix(GameId::Fallout4));
  // The Skyrim prefix must remain the upstream one (byte-identical protocol)
  REQUIRE(std::string(kMessagingProtocolVersion) == "7_");
}

TEST_CASE("Skyrim profile keeps the upstream constants", "[GameProfile]")
{
  SkyrimGameProfile p;
  REQUIRE(p.GetGameId() == GameId::Skyrim);
  REQUIRE(p.GetDefaultLoadOrder().front() == "Skyrim.esm");
  REQUIRE(p.GetDefaultLoadOrder().size() == 5);
  REQUIRE(p.GetArchiveExtension() == ".bsa");
  REQUIRE(p.GetPlayerBaseFormId() == 0x7);
  REQUIRE(p.GetDefaultWorldspace() == 0x3c);
  REQUIRE(p.GetBannedCharacterRaceIds().size() == 26);
  REQUIRE(p.IsItemRecordType("INGR"));
  REQUIRE_FALSE(p.IsItemRecordType("NOTE"));
}

TEST_CASE("Fallout 4 profile has Fallout 4 values", "[GameProfile]")
{
  Fallout4GameProfile p;
  REQUIRE(p.GetGameId() == GameId::Fallout4);
  REQUIRE(p.GetName() == "fallout4");
  REQUIRE(p.GetProtocolPrefix() == "fo4-1_");
  REQUIRE(p.GetDefaultLoadOrder() ==
          std::vector<std::string>{ "Fallout4.esm" }); // no DLC
  REQUIRE(p.GetArchiveExtension() == ".ba2");
  REQUIRE(p.GetCurrencyFormId() == 0xf);
  REQUIRE(p.IsItemRecordType("NOTE"));
  REQUIRE(p.IsItemRecordType("WEAP"));
  REQUIRE_FALSE(p.IsItemRecordType("SCRL"));
  REQUIRE(p.GetSyncedReferenceBaseTypes().count("TERM") == 1);
  REQUIRE(p.GetActivationReach() < SkyrimGameProfile().GetActivationReach());
}

TEST_CASE("WorldState defaults to Skyrim and can switch profile",
          "[GameProfile]")
{
  WorldState ws;
  REQUIRE(ws.GetGameProfile().GetGameId() == GameId::Skyrim);
  REQUIRE(ws.bannedEspmCharacterRaceIds.size() == 26);

  ws.SetGameProfile(CreateGameProfile(GameId::Fallout4));
  REQUIRE(ws.GetGameProfile().GetGameId() == GameId::Fallout4);
  REQUIRE(ws.bannedEspmCharacterRaceIds.empty());

  REQUIRE_THROWS(ws.SetGameProfile(nullptr));
}

TEST_CASE("Message type ranges follow the registry", "[GameProfile][NET]")
{
  REQUIRE(static_cast<int>(MsgType::CreateActor) == 33);
  REQUIRE(static_cast<int>(MsgType::CreateActorFo4) == 64);
  REQUIRE(static_cast<int>(MsgType::WeaponFire) == 80);
  REQUIRE(static_cast<int>(MsgType::PartyAction) == 120);
  REQUIRE(static_cast<int>(MsgType::Max) <= 123);
  REQUIRE_FALSE(IsFallout4MsgType(MsgType::CreateActor));
  REQUIRE(IsFallout4MsgType(MsgType::PowerArmorTransition));
}
