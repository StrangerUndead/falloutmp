#include "GameId.h"
#include "MsgType.h"
#include "TestUtils.hpp"
#include "WorldState.h"
#include "game_profile/Fallout4GameProfile.h"
#include <catch2/catch_all.hpp>
#include <string>
#include <vector>

TEST_CASE("ParseGameId accepts setting values", "[GameProfile]")
{
  REQUIRE(ParseGameId("skyrim") == std::nullopt); // Fallout 4 only
  REQUIRE(ParseGameId("fallout4") == GameId::Fallout4);
  REQUIRE(ParseGameId("fo4") == GameId::Fallout4);
  REQUIRE(ParseGameId("oblivion") == std::nullopt);
  REQUIRE(ParseGameId("") == std::nullopt);
}

TEST_CASE("The protocol prefix is the Fallout 4 one", "[GameProfile]")
{
  REQUIRE(GetProtocolPrefix(GameId::Fallout4) == "fo4-1_");
  REQUIRE(std::string(kMessagingProtocolVersion) == "fo4-1_");
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
  REQUIRE(p.GetActivationReach() == 300.f);
}

TEST_CASE("WorldState defaults to Fallout 4", "[GameProfile]")
{
  WorldState ws;
  REQUIRE(ws.GetGameProfile().GetGameId() == GameId::Fallout4);
  REQUIRE(ws.bannedEspmCharacterRaceIds.empty());

  ws.SetGameProfile(CreateGameProfile(GameId::Fallout4));
  REQUIRE(ws.GetGameProfile().GetGameId() == GameId::Fallout4);

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
