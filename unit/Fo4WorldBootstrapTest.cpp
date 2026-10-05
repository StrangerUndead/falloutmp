#include "PluginBuilder.h"
#include "fo4/EspmFo4DataSource.h"
#include "fo4/Fo4WorldBootstrap.h"
#include "libespm/Browser.h"
#include "libespm/Combiner.h"
#include "libespm/fo4/Fo4Records.h"
#include <catch2/catch_all.hpp>

using namespace test_espm;
using namespace fo4;

namespace {
class NullHost : public Fo4Host
{
public:
  std::array<float, 3> GetActorPos(ActorId) override { return {}; }
  uint32_t GetActorWorldOrCell(ActorId) override { return 0; }
  bool IsActorAlive(ActorId) override { return true; }
  ProfileId GetProfileId(ActorId) override { return -1; }
  bool IsNpc(ActorId) override { return true; }
  std::optional<std::array<float, 3>> GetRefPos(FormId) override
  {
    return std::nullopt;
  }
  FormId GetRefBaseId(FormId) override { return 0; }
  FormId AllocateFormId() override { return 1; }
  void SendTo(ActorId, const IMessageBase&, bool) override {}
  void SendToNeighbours(ActorId, const IMessageBase&, bool) override {}
  int64_t NowMs() override { return 0; }
  void OnActorKilled(ActorId, ActorId) override {}
};

Record& Refr(PluginBuilder& b, uint32_t id, uint32_t base,
             std::array<float, 3> pos)
{
  FieldWriter data;
  data.Add(pos[0]).Add(pos[1]).Add(pos[2]).Add(0.f).Add(0.f).Add(0.f);
  return b.AddRecord("REFR", id).AddValue<uint32_t>("NAME", base).Add("DATA",
                                                                    data);
}
}

TEST_CASE("World bootstrap registers frames, workshops, areas and locks",
          "[fo4][Bootstrap]")
{
  PluginBuilder b;
  b.AddRecord("KYWD", 0x10).EditorId("ArmorTypePowerTorso");
  b.AddRecord("KYWD", 0x11).EditorId("WorkshopLinkedPrimitive");
  {
    FieldWriter data;
    data.Add<int32_t>(100).Add(10.f).Add<uint32_t>(450);
    b.AddRecord("ARMO", 0x20).EditorId("T45Torso").Keywords({ 0x10 }).Add(
      "DATA", data);
  }
  b.AddRecord("AMMO", 0x75FE4).EditorId("AmmoFusionCore");
  {
    FieldWriter cnto1, cnto2;
    cnto1.Add<uint32_t>(0x20).Add<int32_t>(1);
    cnto2.Add<uint32_t>(0x75FE4).Add<int32_t>(1);
    auto& frame = b.AddRecord("FURN", 0x30).EditorId("PowerArmorFurniture")
                    .Add("CNTO", cnto1).Add("CNTO", cnto2);
    frame.flags = espm::fo4::FURN::kPowerArmorRecordFlag;
  }
  b.AddRecord("FURN", 0x31).EditorId("WorkshopWorkbench");
  b.AddRecord("DOOR", 0x40).EditorId("Door");
  b.AddRecord("TERM", 0x41).EditorId("Terminal");
  b.AddRecord("STAT", 0x42).EditorId("PrimitiveBox");

  Refr(b, 0x1000, 0x30, { 10, 20, 30 });
  {
    FieldWriter xlkr;
    xlkr.Add<uint32_t>(0x11).Add<uint32_t>(0x1002);
    Refr(b, 0x1001, 0x31, { 0, 0, 0 }).Add("XLKR", xlkr);
  }
  {
    FieldWriter xprm;
    xprm.Add(4000.f).Add(3000.f).Add(1000.f).Zeros(16).Add<uint32_t>(1);
    Refr(b, 0x1002, 0x42, { 100, 100, 0 }).Add("XPRM", xprm);
  }
  {
    FieldWriter xloc;
    xloc.Add<uint8_t>(50).Zeros(3).Add<uint32_t>(0).Add<uint8_t>(0).Zeros(3);
    Refr(b, 0x1003, 0x40, { 0, 0, 0 }).Add("XLOC", xloc);
    FieldWriter xloc2;
    xloc2.Add<uint8_t>(75).Zeros(3).Add<uint32_t>(0).Add<uint8_t>(0).Zeros(3);
    Refr(b, 0x1004, 0x41, { 0, 0, 0 }).Add("XLOC", xloc2);
  }

  auto bytes = b.Build();
  espm::Browser browser(bytes.data(), bytes.size());
  espm::Combiner combiner;
  combiner.AddSource(&browser, "Fallout4.esm");
  auto combined = combiner.Combine();
  espm::CompressedFieldsCache cache;
  auto data = std::make_shared<EspmFo4DataSource>(*combined, cache);
  NullHost host;
  Fo4Server server(data, host);

  auto rep = BootstrapWorld(*combined, cache, server);
  REQUIRE(rep.frames == 1);
  REQUIRE(rep.workshops == 1);
  REQUIRE(rep.buildAreas == 1);
  REQUIRE(rep.locks == 1);
  REQUIRE(rep.terminals == 1);

  auto frame = server.PowerArmor().FindFrame(0x1000);
  REQUIRE(frame);
  REQUIRE(frame->pos[2] == 30.f);
  REQUIRE(frame->contents.Entries().size() == 2);

  auto shop = server.Workshops().Find(0x1001);
  REQUIRE(shop);
  REQUIRE(shop->areas.size() == 1);
  REQUIRE(shop->areas[0].Contains({ 100, 3000, 0 }, 0));
  REQUIRE_FALSE(shop->areas[0].Contains({ 100, 3200, 0 }, 0));

  REQUIRE(server.Locks().GetLock(0x1003)->level == 50);
  REQUIRE(server.Locks().GetTerminal(0x1004)->locked);

  // Running again (after a world load) does not duplicate or reset
  server.PowerArmor().FindFrame(0x1000)->ownerProfileId = 7;
  auto again = BootstrapWorld(*combined, cache, server);
  REQUIRE(again.frames == 0);
  REQUIRE(again.workshops == 0);
  REQUIRE(server.PowerArmor().FindFrame(0x1000)->ownerProfileId == 7);
}
