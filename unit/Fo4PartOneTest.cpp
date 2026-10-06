#include "Fo4Messages.h"
#include "TestUtils.hpp"
#include "fo4/Fo4PartOneGlue.h"
#include "game_profile/GameProfile.h"
#include <catch2/catch_all.hpp>

namespace {
std::vector<nlohmann::json> MessagesOfType(PartOne& p, MsgType t,
                                           Networking::UserId user)
{
  std::vector<nlohmann::json> res;
  for (auto& m : p.Messages()) {
    if (m.userId == user && m.j.is_object() && m.j.value("t", -1) == int(t)) {
      res.push_back(m.j);
    }
  }
  return res;
}
}

TEST_CASE("PartOne routes Fallout 4 messages to the Fallout 4 layer",
          "[fo4][Fo4PartOne]")
{
  PartOne p;
  p.worldState.SetGameProfile(CreateGameProfile(GameId::Fallout4));
  DoConnect(p, 0);
  p.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c, 1);
  p.SetUserActor(0, 0xff000000);
  p.Messages().clear();

  REQUIRE(p.GetFo4() != nullptr);

  // JSON packet: set PvP flag through PartyAction (120)
  DoMessage(p, 0,
            nlohmann::json{ { "t", MsgType::PartyAction },
                            { "nonce", 1 },
                            { "op", PartyActionMessage::kSetPvpFlag },
                            { "targetProfileId", -1 },
                            { "partyId", 0 },
                            { "value", true },
                            { "leader", -1 },
                            { "members", nlohmann::json::array() },
                            { "error", "" } });
  auto replies = MessagesOfType(p, MsgType::PartyAction, 0);
  REQUIRE(replies.size() == 1);
  REQUIRE(replies[0]["value"] == true);
  REQUIRE(p.GetFo4()->Server().Parties().IsFlagged(1));

  // Actor state is saved into the actor's change form and survives
  auto& st = p.GetFo4()->Server().Actor(0xff000000);
  st.inventory.AddSimple(0xA, 5);
  p.GetFo4()->SaveAll();
  auto& actor = p.worldState.GetFormAt<MpActor>(0xff000000);
  INFO("fields: " << actor.GetDynamicFields().GetAsJson().dump());
  auto dump = actor.GetDynamicFields().GetValueDump(
    fo4::Fo4PartOneGlue::kActorStateField);
  REQUIRE(dump != "null");
  REQUIRE(nlohmann::json::parse(dump)["inventory"]["entries"].size() == 1);

  DoDisconnect(p, 0);
}
