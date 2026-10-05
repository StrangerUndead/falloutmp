#include "Fo4Messages.h"
#include "Fo4TestData.h"
#include "PluginBuilder.h"
#include "fo4/Containers.h"
#include "fo4/EspmFo4DataSource.h"
#include "fo4/LeveledLists.h"
#include "libespm/Browser.h"
#include "libespm/Combiner.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using namespace fo4;
using namespace fo4test;

TEST_CASE("Leveled lists: levels, use all, chance none, nesting", "[fo4][F14]")
{
  InMemoryFo4DataSource d;
  Build(d);
  // Ammo list: level 1 -> 10mm x10, level 10 -> .45 x5
  d.AddLeveledList({ 0x100,
                     0,
                     false,
                     false,
                     false,
                     { { 1, kAmmo10mm, 10, 0 }, { 10, kAmmo45, 5, 0 } } });
  // Use all with one entry always none
  d.AddLeveledList({ 0x101,
                     0,
                     true,
                     false,
                     true,
                     { { 1, kWrench, 1, 0 }, { 1, kDuctTape, 1, 100 } } });
  // Nested and for-each
  d.AddLeveledList({ 0x102, 0, true, true, false, { { 1, 0x101, 1, 0 } } });
  // Self-referencing list must terminate
  d.AddLeveledList({ 0x103, 0, true, false, false, { { 1, 0x103, 1, 0 } } });
  d.AddLeveledList(
    { 0x104, 100, true, false, true, { { 1, kWrench, 1, 0 } } });

  LeveledListResolver r(d);
  std::mt19937 rng(1);
  auto low = r.Resolve(0x100, 1, 5, rng);
  REQUIRE(low == std::vector<ComponentCount>{ { kAmmo10mm, 10 } });
  auto high = r.Resolve(0x100, 1, 20, rng); // only the highest level entry
  REQUIRE(high == std::vector<ComponentCount>{ { kAmmo45, 5 } });
  REQUIRE(r.Resolve(0x100, 1, 0, rng).empty()); // nothing eligible

  auto all = r.Resolve(0x101, 1, 1, rng);
  REQUIRE(all == std::vector<ComponentCount>{ { kWrench, 1 } });
  auto each = r.Resolve(0x102, 3, 1, rng);
  REQUIRE(each == std::vector<ComponentCount>{ { kWrench, 3 } });
  REQUIRE(r.Resolve(0x103, 1, 1, rng).empty());
  REQUIRE(r.Resolve(0x104, 1, 1, rng).empty()); // 100% chance none
  // A plain item resolves to itself
  REQUIRE(r.Resolve(kStimpak, 2, 1, rng) ==
          std::vector<ComponentCount>{ { kStimpak, 2 } });
}

TEST_CASE("Containers: open resolves contents once; take, put, stolen, "
          "lock, reach",
          "[fo4][F06]")
{
  InMemoryFo4DataSource d;
  Build(d);
  d.AddLeveledList(
    { 0x100, 0, true, false, true, { { 1, kAmmo10mm, 12, 0 } } });
  d.AddContainer({ 0xC0, { { 0x100, 1 }, { kStimpak, 2 } }, false });
  ContainerService cs(d);
  std::mt19937 rng(4);
  cs.Register(0xFF00C001, 0xC0, { 0, 0, 0 }, 0xFAC);
  auto c = cs.Open(0xFF00C001, 5, rng);
  REQUIRE(c);
  REQUIRE(c->inventory.CountBase(kAmmo10mm) == 12);
  REQUIRE(c->inventory.CountBase(kStimpak) == 2);
  cs.Open(0xFF00C001, 5, rng); // second open does not add again
  REQUIRE(c->inventory.CountBase(kStimpak) == 2);

  Fo4Inventory me;
  std::array<float, 3> near{ 100, 0, 0 }, far{ 1000, 0, 0 };
  REQUIRE(cs.Take(0xFF00C001, ItemKey{ kStimpak }, 1, far, me, false, false) ==
          ContainerError::OutOfReach);
  REQUIRE(cs.Take(0xFF00C001, ItemKey{ kStimpak }, 1, near, me, false, true) ==
          ContainerError::Locked);
  REQUIRE(cs.Take(0xFF00C001, ItemKey{ kStimpak }, 3, near, me, false,
                  false) == ContainerError::ItemNotFound);
  REQUIRE(cs.Take(0xFF00C001, ItemKey{ kStimpak }, 1, near, me, false,
                  false) == ContainerError::None);
  // Owned container: the item is marked stolen
  REQUIRE(me.Entries()[0].key.stolenFrom == 0xFAC);
  REQUIRE(cs.Take(0xFF00C001, ItemKey{ kStimpak }, 1, near, me, true, false) ==
          ContainerError::None);
  REQUIRE(me.CountBase(kStimpak) == 2);
  REQUIRE(me.Entries().size() == 2); // stolen and owned stacks

  // Put back
  ItemKey stolen{ kStimpak };
  stolen.stolenFrom = 0xFAC;
  REQUIRE(cs.Put(0xFF00C001, stolen, 1, near, me, false) ==
          ContainerError::None);
  REQUIRE(cs.Find(0xFF00C001)->inventory.Count(stolen) == 1);

  // Drop creates a ground stack; emptying it removes it
  ItemKey gun{ k10mm };
  gun.ammoLoaded = 7;
  me.Add(gun, 1);
  FormId ground = 0;
  REQUIRE(cs.Drop(0xFF00D001, ItemKey{ k10mm }, 1, near, me, &ground) ==
          ContainerError::None);
  REQUIRE(ground == 0xFF00D001);
  REQUIRE(cs.Find(ground)->inventory.Find(ItemKey{ k10mm })->key.ammoLoaded ==
          7);
  REQUIRE(cs.Take(ground, ItemKey{ k10mm }, 1, near, me, false, false) ==
          ContainerError::None);
  REQUIRE(me.Find(ItemKey{ k10mm })->key.ammoLoaded == 7); // no dup, no loss
  REQUIRE(cs.CollectEmptyGroundStacks() == std::vector<FormId>{ ground });

  // Persistence keeps touched containers
  ContainerService restored(d);
  restored.LoadJson(cs.ToJson());
  REQUIRE(restored.Find(0xFF00C001)->inventory.CountBase(kStimpak) == 1);
  REQUIRE(restored.Find(0xFF00C001)->initialized);
}

TEST_CASE("libespm reads FO4 LVLO chance none and CONT", "[fo4][F14]")
{
  using namespace test_espm;
  PluginBuilder b;
  b.AddRecord("MISC", 0x10).EditorId("Junk");
  {
    FieldWriter lvlo;
    lvlo.Add<uint16_t>(3)
      .Zeros(2)
      .Add<uint32_t>(0x10)
      .Add<uint16_t>(4)
      .Add<uint8_t>(25)
      .Zeros(1);
    b.AddRecord("LVLI", 0x20)
      .EditorId("LL_Junk")
      .AddValue<uint8_t>("LVLD", 10)
      .AddValue<uint8_t>("LVLF", 0x5)
      .Add("LVLO", lvlo);
  }
  {
    FieldWriter cnto;
    cnto.Add<uint32_t>(0x20).Add<int32_t>(2);
    b.AddRecord("CONT", 0x30)
      .EditorId("Chest")
      .Add("CNTO", cnto)
      .AddValue<uint8_t>("DATA", 0x2);
  }
  auto bytes = b.Build();
  espm::Browser browser(bytes.data(), bytes.size());
  espm::Combiner combiner;
  combiner.AddSource(&browser, "Fallout4.esm");
  auto combined = combiner.Combine();
  espm::CompressedFieldsCache cache;
  EspmFo4DataSource src(*combined, cache);
  auto ll = src.FindLeveledList(0x20);
  REQUIRE(ll);
  REQUIRE(ll->chanceNone == 10.f);
  REQUIRE(ll->calcFromAllLevels);
  REQUIRE(ll->useAll);
  REQUIRE_FALSE(ll->calcForEachItem);
  REQUIRE(ll->entries.size() == 1);
  REQUIRE(ll->entries[0].level == 3);
  REQUIRE(ll->entries[0].count == 4);
  REQUIRE(ll->entries[0].chanceNone == 25);
  auto cont = src.FindContainer(0x30);
  REQUIRE(cont);
  REQUIRE(cont->respawns);
  REQUIRE(cont->items == std::vector<ComponentCount>{ { 0x20, 2 } });
  REQUIRE_FALSE(src.FindLeveledList(0x10));
}
