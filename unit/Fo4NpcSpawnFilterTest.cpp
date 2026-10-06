#include "PluginBuilder.h"
#include "fo4/NpcSpawnFilter.h"
#include "libespm/Browser.h"
#include "libespm/CombineBrowser.h"
#include "libespm/Combiner.h"
#include <catch2/catch_all.hpp>

using namespace test_espm;

namespace {
FieldWriter Acbs(uint16_t templateFlags)
{
  FieldWriter w;
  w.Add<uint32_t>(0)
    .Add<int16_t>(0)
    .Add<uint16_t>(1)
    .Add<uint16_t>(0)
    .Add<uint16_t>(0)
    .Add<int16_t>(0)
    .Add<uint16_t>(templateFlags)
    .Add<uint16_t>(0)
    .Zeros(2);
  return w;
}
}

TEST_CASE("NpcSpawnFilter: blocks human races through templates",
          "[fo4][Npcs][NpcSpawnFilter]")
{
  PluginBuilder b;
  b.AddRecord("RACE", 0x13746).EditorId("HumanRace");
  b.AddRecord("RACE", 0x1A009).EditorId("FeralGhoulRace");
  b.AddRecord("NPC_", 0x100)
    .EditorId("Settler")
    .Add("ACBS", Acbs(0))
    .AddValue<uint32_t>("RNAM", 0x13746);
  b.AddRecord("NPC_", 0x101)
    .EditorId("Ghoul")
    .Add("ACBS", Acbs(0))
    .AddValue<uint32_t>("RNAM", 0x1A009);
  // Race from a template (traits flag), placeholder race on the record
  b.AddRecord("NPC_", 0x102)
    .EditorId("RaiderTemplated")
    .Add("ACBS", Acbs(1))
    .AddValue<uint32_t>("TPLT", 0x100)
    .AddValue<uint32_t>("RNAM", 0x1A009);
  // Template is a leveled NPC containing a human
  FieldWriter lvlo;
  lvlo.Add<uint16_t>(1).Zeros(2).Add<uint32_t>(0x100).Add<uint16_t>(1).Zeros(
    2);
  b.AddRecord("LVLN", 0x200).EditorId("LCharMixed").Add("LVLO", lvlo);
  b.AddRecord("NPC_", 0x103)
    .EditorId("FromLeveled")
    .Add("ACBS", Acbs(1))
    .AddValue<uint32_t>("TPLT", 0x200);

  auto bytes = b.Build();
  auto browser = std::make_unique<espm::Browser>(bytes.data(), bytes.size());
  espm::Combiner combiner;
  combiner.AddSource(browser.get(), "a.esm");
  auto br = combiner.Combine();

  fo4::NpcSpawnFilter filter(*br, fo4::NpcSpawnFilter::DefaultHumanRaces());
  REQUIRE(!filter.IsAllowed(0x100));
  REQUIRE(filter.IsAllowed(0x101));
  REQUIRE(!filter.IsAllowed(0x102));
  REQUIRE(!filter.IsAllowed(0x103));
  REQUIRE(filter.IsAllowed(0x999)); // unknown: not an NPC

  fo4::NpcSpawnFilter none(*br, {});
  REQUIRE(none.IsAllowed(0x100));
}
