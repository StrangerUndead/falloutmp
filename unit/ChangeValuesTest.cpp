#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>

#include "ChangeValuesMessage.h"

TEST_CASE("ChangeValues message is being delivered to client",
          "[ChangeValues]")
{
  PartOne partOne;
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000000, { 0, 0, 0 }, 0, 0x3c);
  partOne.SetUserActor(0, 0xff000000);
  auto& ac = partOne.worldState.GetFormAt<MpActor>(0xff000000);
  partOne.Messages().clear();

  ChangeValuesMessage msg;
  msg.idx = ac.GetIdx();
  msg.data.health = 1.f;
  msg.data.magicka = 1.f;
  msg.data.stamina = 1.f;

  ac.SendToUser(msg, true);

  REQUIRE(partOne.Messages().size() == 1);
  nlohmann::json message = partOne.Messages()[0].j;
  REQUIRE(message["data"]["health"] == 1.0f);
  REQUIRE(message["data"]["magicka"] == 1.0f);
  REQUIRE(message["data"]["stamina"] == 1.0f);

  partOne.DestroyActor(0xff000000);
  DoDisconnect(partOne, 0);
}
