#include "PluginBuilder.h"
#include "libespm/Browser.h"
#include "libespm/Convert.h"
#include "libespm/Game.h"
#include "libespm/fo4/Fo4Records.h"
#include <catch2/catch_all.hpp>
#include <memory>

using namespace test_espm;

namespace {
struct ParsedPlugin
{
  Bytes bytes;
  std::unique_ptr<espm::Browser> browser;
  espm::CompressedFieldsCache cache;

  explicit ParsedPlugin(const PluginBuilder& b)
    : bytes(b.Build())
  {
    browser = std::make_unique<espm::Browser>(bytes.data(), bytes.size());
  }

  const espm::RecordHeader* Tes4() const
  {
    // TES4 is the first record; its header follows the 8-byte type+size
    return reinterpret_cast<const espm::RecordHeader*>(bytes.data() + 8);
  }

  template <class T>
  typename T::Data Get(uint32_t id)
  {
    auto rec = browser->LookupById(id);
    REQUIRE(rec);
    auto typed = espm::Convert<T>(rec);
    REQUIRE(typed);
    return typed->GetData(cache);
  }
};
}

TEST_CASE("DetectGame recognizes Fallout 4 and Skyrim plugins",
          "[libespm][fo4]")
{
  {
    ParsedPlugin p(PluginBuilder(131, 1.0f).SetFlags(0x201));
    auto info = espm::DetectGame(p.Tes4(), p.cache);
    REQUIRE(info.game == espm::Game::Fallout4);
    REQUIRE(info.isLight);
    REQUIRE(info.isMaster);
  }
  {
    ParsedPlugin p(PluginBuilder(44, 1.71f));
    REQUIRE(espm::DetectGame(p.Tes4(), p.cache).game == espm::Game::Skyrim);
  }
  {
    // Unknown form version: fall back to masters
    PluginBuilder b(0, 9.f);
    b.Master("Fallout4.esm");
    ParsedPlugin p(b);
    auto info = espm::DetectGame(p.Tes4(), p.cache);
    REQUIRE(info.game == espm::Game::Fallout4);
    REQUIRE(info.masters == std::vector<std::string>{ "Fallout4.esm" });
  }
  REQUIRE(
    espm::DetectGame(nullptr, *std::make_unique<espm::CompressedFieldsCache>())
      .game == espm::Game::Unknown);
}

TEST_CASE("RequireSingleGame refuses mixed load orders", "[libespm][fo4]")
{
  using G = espm::Game;
  REQUIRE(espm::RequireSingleGame({ { "Fallout4.esm", G::Fallout4 },
                                    { "Mod.esp", G::Unknown },
                                    { "DLCRobot.esm", G::Fallout4 } }) ==
          G::Fallout4);
  REQUIRE_THROWS_WITH(
    espm::RequireSingleGame(
      { { "Fallout4.esm", G::Fallout4 }, { "Skyrim.esm", G::Skyrim } }),
    Catch::Matchers::ContainsSubstring("Skyrim.esm"));
}

TEST_CASE("fo4::WEAP reads DNAM, CRDT, DAMA and keywords", "[libespm][fo4]")
{
  PluginBuilder b;
  FieldWriter dnam;
  dnam
    .Add<uint32_t>(0x1F66B) // ammo .10mm
    .Add(1.0f)              // speed
    .Add(1.25f)             // reload speed
    .Add(1.0f)              // reach
    .Add(0.f)               // min range
    .Add(1024.f)            // max range
    .Add(0.f)               // attack delay
    .Add(0.f)               // unused
    .Add(0.5f)              // out of range mult
    .Add<uint32_t>(0)       // on hit
    .Add<uint32_t>(0x2C2)   // skill
    .Add<uint32_t>(0)       // resist
    .Add<uint32_t>(espm::fo4::WEAP::kAutomatic)
    .Add<uint16_t>(12) // capacity
    .Add<uint8_t>(9)   // Gun
    .Add(0.f)          // secondary damage
    .Add(4.2f)         // weight
    .Add<uint32_t>(53) // value
    .Add<uint16_t>(18) // base damage
    .Add<uint32_t>(0)  // sound level
    .Zeros(32)         // sounds
    .Add<uint8_t>(5)   // accuracy bonus
    .Add(0.3f)
    .Zeros(2)
    .Add(25.f) // AP cost
    .Add(0.f)
    .Add(0.f)
    .Add<uint32_t>(0)
    .Zeros(4);
  REQUIRE(dnam.bytes.size() == 132);

  FieldWriter crdt;
  crdt.Add(2.5f).Add(0.1f).Add<uint32_t>(0x1234);
  FieldWriter dama;
  dama.Add<uint32_t>(0x60A87).Add<uint32_t>(10).Add<uint32_t>(0);

  b.AddRecord("WEAP", 0x4822)
    .EditorId("10mm")
    .AddString("FULL", "10mm Pistol")
    .Keywords({ 0x92A86, 0x4A0A1 })
    .Add("DNAM", dnam)
    .Add("CRDT", crdt)
    .Add("DAMA", dama);

  ParsedPlugin p(b);
  auto d = p.Get<espm::fo4::WEAP>(0x4822);
  REQUIRE(d.common.editorId == "10mm");
  REQUIRE(d.common.fullName == "10mm Pistol");
  REQUIRE(!d.common.fullNameStringId);
  REQUIRE(d.common.keywords == std::vector<uint32_t>{ 0x92A86, 0x4A0A1 });
  REQUIRE(d.hasDnam);
  REQUIRE(d.ammoId == 0x1F66B);
  REQUIRE(d.reloadSpeed == 1.25f);
  REQUIRE(d.maxRange == 1024.f);
  REQUIRE(d.IsAutomatic());
  REQUIRE(d.IsGun());
  REQUIRE(d.capacity == 12);
  REQUIRE(d.weight == 4.2f);
  REQUIRE(d.value == 53);
  REQUIRE(d.baseDamage == 18);
  REQUIRE(d.accuracyBonus == 5);
  REQUIRE(d.actionPointCost == 25.f);
  REQUIRE(d.critDamageMult == 2.5f);
  REQUIRE(d.critEffectSpellId == 0x1234);
  REQUIRE(d.damageTypes.size() == 1);
  REQUIRE(d.damageTypes[0].damageTypeId == 0x60A87);
  REQUIRE(d.damageTypes[0].value == 10);
}

TEST_CASE("fo4::WEAP survives a truncated DNAM", "[libespm][fo4]")
{
  PluginBuilder b;
  FieldWriter dnam;
  dnam.Add<uint32_t>(0x1F66B).Add(1.0f);
  b.AddRecord("WEAP", 0x10).Add("DNAM", dnam);
  ParsedPlugin p(b);
  auto d = p.Get<espm::fo4::WEAP>(0x10);
  REQUIRE(d.ammoId == 0x1F66B);
  REQUIRE_FALSE(d.hasDnam);
  REQUIRE(d.baseDamage == 0);
}

TEST_CASE("fo4::FULL with a string-table id", "[libespm][fo4]")
{
  PluginBuilder b;
  b.AddRecord("MISC", 0x20).AddValue<uint32_t>("FULL", 0x0001A2B3);
  ParsedPlugin p(b);
  auto d = p.Get<espm::fo4::MISC>(0x20);
  REQUIRE(d.common.fullName.empty());
  REQUIRE(d.common.fullNameStringId == 0x0001A2B3u);
}

TEST_CASE("fo4::ARMO and AMMO", "[libespm][fo4]")
{
  PluginBuilder b;
  FieldWriter data;
  data.Add<int32_t>(200).Add(12.f).Add<uint32_t>(450);
  FieldWriter fnam;
  fnam.Add<uint16_t>(110).Add<uint16_t>(0).Add<uint8_t>(2).Zeros(3);
  b.AddRecord("ARMO", 0x30)
    .EditorId("Armor_Power_T45_Torso")
    .AddValue<uint32_t>("BOD2", 0x800)
    .Add("DATA", data)
    .Add("FNAM", fnam)
    .AddValue<uint32_t>("TNAM", 0x31);

  FieldWriter ammoData;
  ammoData.Add<uint32_t>(200).Add(4.f);
  FieldWriter ammoDnam;
  ammoDnam.Add<uint32_t>(0x99).Add<uint8_t>(0).Zeros(3).Add(0.f).Add<uint32_t>(
    10000);
  b.AddRecord("AMMO", 0x75FE4)
    .EditorId("AmmoFusionCore")
    .Add("DATA", ammoData)
    .Add("DNAM", ammoDnam);

  ParsedPlugin p(b);
  auto a = p.Get<espm::fo4::ARMO>(0x30);
  REQUIRE(a.bipedSlots == 0x800);
  REQUIRE(a.value == 200);
  REQUIRE(a.weight == 12.f);
  REQUIRE(a.health == 450);
  REQUIRE(a.armorRating == 110);
  REQUIRE(a.staggerRating == 2);
  REQUIRE(a.templateArmorId == 0x31);

  auto m = p.Get<espm::fo4::AMMO>(0x75FE4);
  REQUIRE(m.value == 200);
  REQUIRE(m.weight == 4.f);
  REQUIRE(m.projectileId == 0x99);
  REQUIRE(m.health == 10000);
}

TEST_CASE("fo4::MISC components, CMPO scrap item, COBJ recipe",
          "[libespm][fo4]")
{
  PluginBuilder b;
  FieldWriter miscData;
  miscData.Add<int32_t>(5).Add(1.5f);
  FieldWriter cvpa;
  cvpa.Add<uint32_t>(0x1FA8C)
    .Add<uint32_t>(3)
    .Add<uint32_t>(0x106D98)
    .Add<uint32_t>(1);
  b.AddRecord("MISC", 0x40)
    .EditorId("Junk_Wrench")
    .Add("DATA", miscData)
    .Add("CVPA", cvpa);

  b.AddRecord("CMPO", 0x1FA8C)
    .EditorId("c_Steel")
    .AddValue<uint32_t>("DATA", 1)
    .AddValue<uint32_t>("MNAM", 0x731A4)
    .AddValue<uint32_t>("GNAM", 0x50);

  FieldWriter fvpa;
  fvpa.Add<uint32_t>(0x1FA8C).Add<uint32_t>(4);
  FieldWriter intv;
  intv.Add<uint16_t>(2).Add<uint16_t>(7);
  FieldWriter fnam;
  fnam.Add<uint32_t>(0xAAA).Add<uint32_t>(0xBBB);
  b.AddRecord("COBJ", 0x50)
    .EditorId("co_StimpakRecipe")
    .Add("FVPA", fvpa)
    .AddValue<uint32_t>("CNAM", 0x23736)
    .AddValue<uint32_t>("BNAM", 0x102158)
    .Add("FNAM", fnam)
    .Add("INTV", intv);

  ParsedPlugin p(b);
  auto misc = p.Get<espm::fo4::MISC>(0x40);
  REQUIRE(misc.value == 5);
  REQUIRE(misc.components.size() == 2);
  REQUIRE(misc.components[0] == espm::fo4::ComponentCount{ 0x1FA8C, 3 });

  auto cmpo = p.Get<espm::fo4::CMPO>(0x1FA8C);
  REQUIRE(cmpo.editorId == "c_Steel");
  REQUIRE(cmpo.scrapItemId == 0x731A4);
  REQUIRE(cmpo.modScrapScalarId == 0x50);

  auto cobj = p.Get<espm::fo4::COBJ>(0x50);
  REQUIRE(cobj.components.size() == 1);
  REQUIRE(cobj.components[0].count == 4);
  REQUIRE(cobj.createdObjectId == 0x23736);
  REQUIRE(cobj.workbenchKeywordId == 0x102158);
  REQUIRE(cobj.categoryKeywords.size() == 2);
  REQUIRE(cobj.createdCount == 2);
  REQUIRE(cobj.priority == 7);
}

TEST_CASE("fo4::OMOD reads includes and 24-byte properties", "[libespm][fo4]")
{
  PluginBuilder b;
  FieldWriter data;
  data
    .Add<uint32_t>(1) // include count
    .Add<uint32_t>(2) // property count
    .Add<uint8_t>(0)
    .Add<uint8_t>(0)           // unknown bools
    .Add<uint32_t>(0x50414557) // 'WEAP'
    .Add<uint8_t>(0)
    .Add<uint8_t>(0)     // max rank, level tier offset
    .Add<uint32_t>(0x1A) // attach point keyword
    .Add<uint32_t>(1)
    .Add<uint32_t>(0x1B) // attach parent slots
    .Add<uint32_t>(0)    // items
    .Add<uint32_t>(0x60)
    .Add<uint8_t>(0)
    .Add<uint8_t>(1)
    .Add<uint8_t>(0)
    // property 1: AmmoCapacity (12) int ADD 6
    .Add<uint8_t>(0)
    .Zeros(3)
    .Add<uint8_t>(2)
    .Zeros(3)
    .Add<uint16_t>(12)
    .Zeros(2)
    .Add<uint32_t>(6)
    .Add<uint32_t>(0)
    .Add(0.f)
    // property 2: Speed (0) float MUL+ADD 0.25
    .Add<uint8_t>(1)
    .Zeros(3)
    .Add<uint8_t>(1)
    .Zeros(3)
    .Add<uint16_t>(0)
    .Zeros(2)
    .Add(0.25f)
    .Add<uint32_t>(0)
    .Add(0.f);

  b.AddRecord("OMOD", 0x61)
    .EditorId("mod_10mm_Mag_Large")
    .Add("DATA", data)
    .AddValue<uint32_t>("LNAM", 0x62)
    .AddValue<uint8_t>("NAM1", 3);

  ParsedPlugin p(b);
  auto d = p.Get<espm::fo4::OMOD>(0x61);
  REQUIRE(d.parsedOk);
  REQUIRE(d.formType == espm::fo4::OMOD::TargetFormType::Weapon);
  REQUIRE(d.attachPointKeywordId == 0x1A);
  REQUIRE(d.attachParentSlots == std::vector<uint32_t>{ 0x1B });
  REQUIRE(d.includes.size() == 1);
  REQUIRE(d.includes[0].modId == 0x60);
  REQUIRE(d.includes[0].optional);
  REQUIRE(d.properties.size() == 2);
  REQUIRE(d.properties[0].property == 12);
  REQUIRE(d.properties[0].function ==
          espm::fo4::OmodProperty::Function::AddOrOr);
  REQUIRE(d.properties[0].value1 == 6);
  REQUIRE(d.properties[1].valueType ==
          espm::fo4::OmodProperty::ValueType::Float);
  REQUIRE(d.properties[1].Value1AsFloat() == 0.25f);
  REQUIRE(d.looseModId == 0x62);
  REQUIRE(d.priority == 3);
}

TEST_CASE("fo4::OMOD with a lying property count stays in bounds",
          "[libespm][fo4]")
{
  PluginBuilder b;
  FieldWriter data;
  data.Add<uint32_t>(0)
    .Add<uint32_t>(1000)
    .Zeros(2)
    .Add<uint32_t>(0)
    .Zeros(2)
    .Add<uint32_t>(0)
    .Add<uint32_t>(0)
    .Add<uint32_t>(0);
  b.AddRecord("OMOD", 0x70).Add("DATA", data);
  ParsedPlugin p(b);
  auto d = p.Get<espm::fo4::OMOD>(0x70);
  REQUIRE_FALSE(d.parsedOk);
  REQUIRE(d.properties.empty());
}

TEST_CASE("fo4::FURN workbench and power armor flags, GLOB, ALCH, PRPS",
          "[libespm][fo4]")
{
  PluginBuilder b;
  b.AddRecord("FURN", 0x80)
    .EditorId("PowerArmorStation")
    .AddValue<uint16_t>("WBDT", 0x0008);
  auto& frame = b.AddRecord("FURN", 0x81).EditorId("PowerArmorFurniture");
  frame.flags = espm::fo4::FURN::kPowerArmorRecordFlag;

  b.AddRecord("GLOB", 0x90)
    .EditorId("TimeScale")
    .AddValue<uint8_t>("FNAM", 'f')
    .AddValue("FLTV", 20.f);

  FieldWriter enit;
  enit.Add<int32_t>(50)
    .Add<uint32_t>(espm::fo4::ALCH::kMedicine)
    .Add<uint32_t>(0)
    .Add(0.f)
    .Add<uint32_t>(0);
  FieldWriter efit;
  efit.Add(40.f).Add<uint32_t>(0).Add<uint32_t>(10);
  b.AddRecord("ALCH", 0x23736)
    .EditorId("Stimpak")
    .AddValue("DATA", 0.1f)
    .Add("ENIT", enit)
    .AddValue<uint32_t>("EFID", 0x2B)
    .Add("EFIT", efit);

  FieldWriter prps;
  prps.Add<uint32_t>(0x32E).Add(5.f).Add<uint32_t>(0x32F).Add(2.f);
  b.AddRecord("STAT", 0xA0).Add("PRPS", prps);

  ParsedPlugin p(b);
  auto station = p.Get<espm::fo4::FURN>(0x80);
  REQUIRE(station.benchType == espm::fo4::FURN::BenchType::PowerArmor);
  REQUIRE_FALSE(station.isPowerArmorFurniture);
  auto paFrame = p.Get<espm::fo4::FURN>(0x81);
  REQUIRE(paFrame.isPowerArmorFurniture);
  REQUIRE(!paFrame.benchType);

  auto g = p.Get<espm::fo4::GLOB>(0x90);
  REQUIRE(g.type == 'f');
  REQUIRE(g.value == 20.f);

  auto stim = p.Get<espm::fo4::ALCH>(0x23736);
  REQUIRE(stim.weight == 0.1f);
  REQUIRE(stim.value == 50);
  REQUIRE((stim.flags & espm::fo4::ALCH::kMedicine) != 0);
  REQUIRE(stim.effects.size() == 1);
  REQUIRE(stim.effects[0].effectId == 0x2B);
  REQUIRE(stim.effects[0].magnitude == 40.f);
  REQUIRE(stim.effects[0].duration == 10);

  auto props =
    espm::fo4::GetActorValueProperties(p.browser->LookupById(0xA0), p.cache);
  REQUIRE(props.size() == 2);
  REQUIRE(props[1].actorValueId == 0x32F);
  REQUIRE(props[1].value == 2.f);
}

TEST_CASE("GetKeywords trusts KWDA size, not KSIZ", "[libespm][fo4]")
{
  PluginBuilder b;
  auto& r = b.AddRecord("MISC", 0xB0);
  r.AddValue<uint32_t>("KSIZ", 1000);
  r.AddValue<uint32_t>("KWDA", 0x77);
  ParsedPlugin p(b);
  auto kws = espm::fo4::GetKeywords(p.browser->LookupById(0xB0), p.cache);
  REQUIRE(kws == std::vector<uint32_t>{ 0x77 });
}

TEST_CASE("fo4::NPC_ reads ACBS, factions, templates, loadout; OTFT items",
          "[libespm][fo4]")
{
  PluginBuilder b;
  FieldWriter acbs;
  acbs
    .Add<uint32_t>(espm::fo4::NPC_::kFlagPcLevelMult |
                   espm::fo4::NPC_::kFlagEssential)
    .Add<int16_t>(-5)
    .Add<uint16_t>(1500) // level mult 1.5
    .Add<uint16_t>(3)
    .Add<uint16_t>(30)
    .Add<int16_t>(35)
    .Add<uint16_t>(0x0102) // stats + inventory from templates
    .Add<uint16_t>(0)
    .Zeros(2);
  FieldWriter faction;
  faction.Add<uint32_t>(0x1BC).Add<int8_t>(2); // 5-byte FO4 layout
  FieldWriter tpta;
  for (int i = 0; i < 13; ++i) {
    tpta.Add<uint32_t>(i == 8 ? 0x5000 : 0);
  }
  FieldWriter cnto;
  cnto.Add<uint32_t>(0x4822C).Add<int32_t>(1);
  FieldWriter dnam;
  dnam.Add<uint16_t>(120)
    .Add<uint16_t>(80)
    .Add<uint16_t>(0)
    .Add<uint8_t>(0)
    .Zeros(1);
  b.AddRecord("NPC_", 0x100)
    .EditorId("RaiderTemplate")
    .Add("ACBS", acbs)
    .Add("SNAM", faction)
    .AddValue<uint32_t>("TPLT", 0x200)
    .Add("TPTA", tpta)
    .AddValue<uint32_t>("RNAM", 0x13746)
    .AddValue<uint32_t>("DOFT", 0x300)
    .Add("CNTO", cnto)
    .Add("DNAM", dnam);
  FieldWriter items;
  items.Add<uint32_t>(0x1234).Add<uint32_t>(0x5678);
  b.AddRecord("OTFT", 0x300).EditorId("RaiderOutfit").Add("INAM", items);

  ParsedPlugin p(b);
  auto n = p.Get<espm::fo4::NPC_>(0x100);
  REQUIRE(n.editorId == "RaiderTemplate");
  REQUIRE(n.xpValueOffset == -5);
  REQUIRE(n.levelMult == Catch::Approx(1.5f));
  REQUIRE(n.calcMinLevel == 3);
  REQUIRE(n.calcMaxLevel == 30);
  REQUIRE(n.templateFlags == 0x0102);
  REQUIRE(n.factions.size() == 1);
  REQUIRE(n.factions[0].factionId == 0x1BC);
  REQUIRE(n.factions[0].rank == 2);
  REQUIRE(n.defaultTemplate == 0x200);
  REQUIRE(n.templateActors[espm::fo4::NPC_::kInventory] == 0x5000);
  REQUIRE(n.templateActors[espm::fo4::NPC_::kStats] == 0);
  REQUIRE(n.race == 0x13746);
  REQUIRE(n.defaultOutfit == 0x300);
  REQUIRE(n.items.size() == 1);
  REQUIRE(n.items[0].formId == 0x4822C);
  REQUIRE(n.calculatedHealth == 120);
  auto o = p.Get<espm::fo4::OTFT>(0x300);
  REQUIRE(o.items == std::vector<uint32_t>{ 0x1234, 0x5678 });
}
