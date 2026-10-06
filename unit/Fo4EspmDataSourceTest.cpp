#include "PluginBuilder.h"
#include "fo4/Crafting.h"
#include "fo4/EspmFo4DataSource.h"
#include "fo4/Workshop.h"
#include "libespm/Browser.h"
#include "libespm/Combiner.h"
#include <catch2/catch_all.hpp>

using namespace test_espm;
using namespace fo4;

namespace {
// Builds a tiny "Fallout4.esm": keywords, AVIFs, components, junk, a gun,
// a mod with its recipe gated by HasPerk, a workshop object with PRPS.
Bytes BuildPlugin()
{
  PluginBuilder b;
  b.AddRecord("KYWD", 0x100).EditorId("WorkbenchWeapons");
  b.AddRecord("KYWD", 0x101).EditorId("ap_Mag");
  b.AddRecord("KYWD", 0x102).EditorId("ArmorTypePowerTorso");
  b.AddRecord("AVIF", 0x200).EditorId("Food");
  b.AddRecord("AVIF", 0x201).EditorId("PowerRequired");

  b.AddRecord("GLOB", 0x300)
    .EditorId("ModScrapScalar_Common")
    .AddValue("FLTV", 0.75f);
  b.AddRecord("MISC", 0x401).EditorId("c_Steel_scrap");
  b.AddRecord("CMPO", 0x400)
    .EditorId("c_Steel")
    .AddValue<uint32_t>("MNAM", 0x401)
    .AddValue<uint32_t>("GNAM", 0x300);
  {
    FieldWriter data, cvpa;
    data.Add<int32_t>(10).Add(2.f);
    cvpa.Add<uint32_t>(0x400).Add<uint32_t>(3);
    b.AddRecord("MISC", 0x410)
      .EditorId("Wrench")
      .Add("DATA", data)
      .Add("CVPA", cvpa);
  }
  b.AddRecord("AMMO", 0x500).EditorId("Ammo10mm");
  {
    FieldWriter dnam;
    dnam.Add<uint32_t>(0x500)
      .Add(1.f)
      .Add(1.f)
      .Add(1.f)
      .Add(0.f)
      .Add(500.f)
      .Add(0.f)
      .Add(0.f)
      .Add(0.5f)
      .Add<uint32_t>(0)
      .Add<uint32_t>(0)
      .Add<uint32_t>(0)
      .Add<uint32_t>(0)
      .Add<uint16_t>(12)
      .Add<uint8_t>(9)
      .Add(0.f)
      .Add(4.f)
      .Add<uint32_t>(50)
      .Add<uint16_t>(18);
    b.AddRecord("WEAP", 0x600)
      .EditorId("Pistol10mm")
      .Add("DNAM", dnam)
      .Add("APPR", FieldWriter().Add<uint32_t>(0x101));
  }
  {
    FieldWriter data;
    data.Add<uint32_t>(0)
      .Add<uint32_t>(1)
      .Zeros(2)
      .Add<uint32_t>(0x50414557)
      .Zeros(2)
      .Add<uint32_t>(0x101)
      .Add<uint32_t>(0)
      .Add<uint32_t>(0)
      .Add<uint8_t>(0)
      .Zeros(3)
      .Add<uint8_t>(2)
      .Zeros(3)
      .Add<uint16_t>(12)
      .Zeros(2)
      .Add<uint32_t>(6)
      .Add<uint32_t>(0)
      .Add(0.f);
    b.AddRecord("OMOD", 0x700).EditorId("mod_Mag_Large").Add("DATA", data);
  }
  b.AddRecord("PERK", 0x800).EditorId("GunNut01");
  {
    FieldWriter fvpa, ctda;
    fvpa.Add<uint32_t>(0x400).Add<uint32_t>(2);
    // CTDA: op/flags, unknown[3], comparison 1.0, func 448, pad, perk id,
    // second param, run-on, reference, unknown
    ctda.Add<uint8_t>(0)
      .Zeros(3)
      .Add(1.f)
      .Add<uint16_t>(448)
      .Zeros(2)
      .Add<uint32_t>(0x800)
      .Add<uint32_t>(0)
      .Add<uint32_t>(0)
      .Add<int32_t>(0)
      .Add<int32_t>(-1);
    b.AddRecord("COBJ", 0x900)
      .EditorId("co_mod_Mag_Large")
      .Add("FVPA", fvpa)
      .Add("CTDA", ctda)
      .AddValue<uint32_t>("CNAM", 0x700)
      .AddValue<uint32_t>("BNAM", 0x100);
  }
  {
    FieldWriter prps;
    prps.Add<uint32_t>(0x200).Add(2.f).Add<uint32_t>(0x201).Add(1.f);
    b.AddRecord("STAT", 0xA00).EditorId("WorkshopCorn").Add("PRPS", prps);
  }
  {
    FieldWriter data;
    data.Add<int32_t>(100).Add(10.f).Add<uint32_t>(500);
    b.AddRecord("ARMO", 0xB00)
      .EditorId("PA_T45_Torso")
      .Keywords({ 0x102 })
      .Add("DATA", data);
  }
  b.AddRecord("FURN", 0xC00)
    .EditorId("WeaponsBench")
    .Keywords({ 0x100 })
    .AddValue<uint16_t>("WBDT", 2);
  return b.Build();
}

struct Loaded
{
  Bytes bytes = BuildPlugin();
  espm::Browser browser{ bytes.data(), bytes.size() };
  espm::Combiner combiner;
  std::unique_ptr<espm::CombineBrowser> combined;
  espm::CompressedFieldsCache cache;
  std::unique_ptr<EspmFo4DataSource> src;
  Loaded()
  {
    combiner.AddSource(&browser, "Fallout4.esm");
    combined = combiner.Combine();
    src = std::make_unique<EspmFo4DataSource>(*combined, cache);
  }
};
}

TEST_CASE("EspmFo4DataSource converts plugin records", "[fo4][espmsource]")
{
  Loaded l;
  auto& s = *l.src;
  auto gun = s.FindWeapon(0x600);
  REQUIRE(gun);
  REQUIRE(gun->editorId == "Pistol10mm");
  REQUIRE(gun->ammoId == 0x500);
  REQUIRE(gun->capacity == 12);
  REQUIRE(gun->baseDamage == 18);
  REQUIRE(gun->isGun);
  REQUIRE(gun->attachParentSlots == std::vector<FormId>{ 0x101 });
  REQUIRE_FALSE(s.FindArmor(0x600));

  auto junk = s.FindMisc(0x410);
  REQUIRE(junk);
  REQUIRE(junk->components == std::vector<ComponentCount>{ { 0x400, 3 } });
  REQUIRE(s.FindMisc(0x401)->scrapItemForComponent == 0x400);
  REQUIRE(s.GetComponentByScrapItem(0x401) == 0x400);

  auto steel = s.FindComponent(0x400);
  REQUIRE(steel);
  REQUIRE(steel->scrapItemId == 0x401);
  REQUIRE(steel->modScrapScalar == 0.75f);

  auto mod = s.FindObjectMod(0x700);
  REQUIRE(mod);
  REQUIRE(mod->target == OmodTarget::Weapon);
  REQUIRE(mod->attachPointKeywordId == 0x101);
  REQUIRE(mod->properties.size() == 1);
  REQUIRE(mod->properties[0].value1 == 6.f);

  auto recipes = s.GetRecipesCreating(0x700);
  REQUIRE(recipes.size() == 1);
  REQUIRE(recipes[0]->perks.size() == 1);
  REQUIRE(recipes[0]->perks[0].perkId == 0x800);
  REQUIRE(s.GetRecipesByWorkbench(0x100).size() == 1);

  auto corn = s.FindWorkshopObject(0xA00);
  REQUIRE(corn);
  REQUIRE(corn->food == 2.f);
  REQUIRE(corn->powerRequired == 1.f);

  auto torso = s.FindArmor(0xB00);
  REQUIRE(torso);
  REQUIRE(torso->powerArmorSlot == PowerArmorSlot::Torso);
  REQUIRE(torso->health == 500);

  auto bench = s.FindFurniture(0xC00);
  REQUIRE(bench);
  REQUIRE(bench->workbench == WorkbenchType::Weapons);
  REQUIRE(bench->keywords == std::vector<FormId>{ 0x100 });

  REQUIRE(s.FindKeywordByEditorId("ap_Mag") == 0x101);
  REQUIRE(s.FindActorValueByEditorId("Food") == 0x200);
  REQUIRE_FALSE(s.FindItem(0xDEADBEEF));
}

TEST_CASE("Modding works end to end on plugin data", "[fo4][espmsource]")
{
  Loaded l;
  ModdingService ms(*l.src);
  Fo4Inventory inv;
  inv.Add(ItemKey{ 0x600 }, 1);
  inv.AddSimple(0x410, 1); // a wrench: 3 steel
  CrafterContext ctx;
  ctx.workbenchKeywords = l.src->FindFurniture(0xC00)->keywords;
  ctx.perkRank = [](FormId p) { return p == 0x800 ? 1 : 0; };
  auto r = ms.AttachMod(inv, ItemKey{ 0x600 }, 0x700, ctx);
  REQUIRE(r.Ok());
  REQUIRE(r.newKey.HasMod(0x700));
  REQUIRE(inv.CountBase(0x401) == 1); // leftover steel
  OmodStatResolver res(*l.src);
  REQUIRE(res.ResolveWeapon(r.newKey).capacity == 18);
}
