#include "Fo4TestData.h"
#include "fo4/Fo4GamemodeApi.h"
#include "fo4/Fo4Server.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using namespace fo4;
using namespace fo4test;
using json = nlohmann::json;

namespace {
class QuietHost : public Fo4Host
{
public:
  int sends = 0;
  std::array<float, 3> GetActorPos(ActorId) override { return {}; }
  uint32_t GetActorWorldOrCell(ActorId) override { return 0x3c; }
  bool IsActorAlive(ActorId) override { return true; }
  ProfileId GetProfileId(ActorId a) override { return a & 0xFF; }
  bool IsNpc(ActorId) override { return false; }
  std::optional<std::array<float, 3>> GetRefPos(FormId) override
  {
    return std::nullopt;
  }
  FormId GetRefBaseId(FormId) override { return 0; }
  FormId AllocateFormId() override { return 1; }
  void SendTo(ActorId, const IMessageBase&, bool) override { ++sends; }
  void SendToNeighbours(ActorId, const IMessageBase&, bool) override {}
  int64_t NowMs() override { return 0; }
  void OnActorKilled(ActorId, ActorId) override {}
};
}

TEST_CASE("Fo4 gamemode API commands", "[fo4][GamemodeApi]")
{
  auto data = std::make_shared<InMemoryFo4DataSource>();
  Build(*data);
  QuietHost host;
  Fo4Server s(data, host);
  constexpr ActorId kA = 0xFF000001;

  REQUIRE(Fo4ListCommands().size() >= 25);
  REQUIRE(Fo4Call(s, "nope", {})["ok"] == false);
  REQUIRE(Fo4Call(s, "cureAddictions", { { "actorId", kA } })["ok"] == true);
  REQUIRE(Fo4Call(s, "getEffects", { { "actorId", kA } })["ok"] == true);
  REQUIRE(Fo4Call(s, "addRads", { { "actorId", kA }, { "amount", 50 } })["ok"] ==
          true);
  REQUIRE(s.Actor(kA).avs.GetCurrent(Av::Rads) != 0.f);
  REQUIRE(Fo4Call(s, "addItem", { { "actorId", kA } })["ok"] == false);

  json item = { { "baseId", k10mm }, { "mods", { kModMagLarge } } };
  REQUIRE(Fo4Call(s, "addItem", { { "actorId", kA }, { "item", item },
                                   { "count", 2 } })["ok"] == true);
  REQUIRE(host.sends > 0); // inventory pushed to the owner
  auto inv = Fo4Call(s, "getInventory", { { "actorId", kA } });
  REQUIRE(inv["entries"].size() == 1);
  REQUIRE(inv["entries"][0]["count"] == 2);
  REQUIRE(Fo4Call(s, "equipWeapon", { { "actorId", kA }, { "item", item } })
            ["ok"] == true);
  auto stats = Fo4Call(s, "getWeaponStats", { { "item", item } });
  REQUIRE(stats["capacity"] == 18);
  REQUIRE(Fo4Call(s, "removeItem", { { "actorId", kA }, { "item", item },
                                     { "count", 5 } })["error"] == "NotEnough");

  REQUIRE(Fo4Call(s, "setActorValue", { { "actorId", kA }, { "avId", 0x2C4 },
                                        { "base", 6 } })["ok"] == true);
  auto hp = Fo4Call(s, "getActorValue", { { "actorId", kA }, { "avId", 0x2D4 } });
  REQUIRE(hp["max"] == 110.0); // 80 + 5*6

  auto xp = Fo4Call(s, "awardXp", { { "actorId", kA }, { "amount", 500 },
                                    { "direct", true } });
  REQUIRE(xp["level"] == 3);

  REQUIRE(Fo4Call(s, "definePerk",
                  { { "key", "Locksmith" },
                    { "special", 0x2C3 },
                    { "specialRequired", 1 },
                    { "ranks", { { { "perkId", 0x523FF } } } } })["ok"] == true);
  REQUIRE(s.PerkChart().count("Locksmith"));
  REQUIRE(Fo4Call(s, "defineEffect", { { "effectId", 5 }, { "kind", "bogus" } })
            ["ok"] == false);
  REQUIRE(Fo4Call(s, "defineEffect",
                  { { "effectId", 5 }, { "kind", "restoreOverTime" },
                    { "avId", 0x2D4 } })["ok"] == true);

  REQUIRE(Fo4Call(s, "addPowerArmorFrame",
                  { { "refId", 0xFF00F000 },
                    { "pos", { 1, 2, 3 } },
                    { "contents", { { { "baseId", kT45Torso } } } } })["ok"] ==
          true);
  auto frame = Fo4Call(s, "getPowerArmor", { { "frameRefId", 0xFF00F000 } });
  REQUIRE(frame["frame"]["pos"][2] == 3.0);

  REQUIRE(Fo4Call(s, "addWorkshop",
                  { { "refId", 0xFF00A000 },
                    { "areas",
                      { { { "shape", "sphere" },
                          { "center", { 0, 0, 0 } },
                          { "radius", 2000 } } } } })["ok"] == true);
  REQUIRE(Fo4Call(s, "setWorkshopOwner",
                  { { "refId", 0xFF00A000 }, { "type", "profile" },
                    { "id", 1 } })["ok"] == true);
  auto ws = Fo4Call(s, "getWorkshop", { { "refId", 0xFF00A000 } });
  REQUIRE(ws["workshop"]["owner"][1] == 1);
  REQUIRE(ws["workshop"]["ratings"]["happiness"] == 50.0);

  REQUIRE(Fo4Call(s, "setLock", { { "refId", 0x99 }, { "level", 50 } })["ok"] ==
          true);
  REQUIRE(Fo4Call(s, "getLock", { { "refId", 0x99 } })["lock"]["level"] == 50);
  REQUIRE(Fo4Call(s, "getLock", { { "refId", 0x98 } })["lock"].is_null());

  REQUIRE(Fo4Call(s, "addPvpZone", { { "center", { 0, 0, 0 } },
                                     { "radius", 500 },
                                     { "mode", "safe" } })["ok"] == true);
  REQUIRE(Fo4Call(s, "getParty", { { "profileId", 1 } })["party"].is_null());

  REQUIRE(Fo4Call(s, "addVendor", { { "vendorId", 0x1000 }, { "caps", 300 } })
            ["ok"] == true);
  REQUIRE(s.Vendors().Find(0x1000)->caps == 300);

  auto state = Fo4Call(s, "getActorState", { { "actorId", kA } });
  REQUIRE(state["state"]["inventory"]["entries"].size() == 1);
  REQUIRE(Fo4Call(s, "getWorldState", {})["world"]["workshops"].size() == 1);
}
