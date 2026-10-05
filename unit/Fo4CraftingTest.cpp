#include "Fo4TestData.h"
#include "fo4/ComponentPlanner.h"
#include "fo4/Crafting.h"
#include "fo4/ItemInstance.h"
#include "fo4/OmodStatResolver.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using namespace fo4;
using namespace fo4test;

namespace {
struct World
{
  InMemoryFo4DataSource data;
  World() { Build(data); }
};

CrafterContext Bench(FormId kw, int gunNut = 0)
{
  CrafterContext c;
  c.workbenchKeywords = { kw };
  c.perkRank = [gunNut](FormId perk) {
    return perk == kPerkGunNut ? gunNut : 0;
  };
  return c;
}
}

TEST_CASE("Fo4Inventory stacks by identity and merges ammo to min",
          "[fo4][F04]")
{
  Fo4Inventory inv;
  ItemKey a{ k10mm };
  a.ammoLoaded = 12;
  ItemKey b{ k10mm };
  b.ammoLoaded = 3;
  inv.Add(a, 1);
  inv.Add(b, 1);
  REQUIRE(inv.Entries().size() == 1);
  REQUIRE(inv.Entries()[0].count == 2);
  REQUIRE(inv.Entries()[0].key.ammoLoaded == 3); // no rounds created

  ItemKey modded = ItemKey{ k10mm }.WithMods({ kModMagLarge });
  inv.Add(modded, 1);
  ItemKey stolen{ k10mm };
  stolen.stolenFrom = 0x1234;
  inv.Add(stolen, 1);
  REQUIRE(inv.Entries().size() == 3);
  REQUIRE(inv.CountBase(k10mm) == 4);

  // Mod order does not matter for identity
  REQUIRE(ItemKey{ k10mm }
            .WithMods({ kModScope, kModMount })
            .SameStack(ItemKey{ k10mm }.WithMods({ kModMount, kModScope })));
  REQUIRE(ItemKey{ k10mm }.WithMods({ kModScope }).InstanceHash() !=
          ItemKey{ k10mm }.InstanceHash());

  REQUIRE_FALSE(inv.Remove(modded, 2));
  REQUIRE(inv.Count(modded) == 1);
  REQUIRE(inv.Remove(modded, 1));
  REQUIRE(inv.Count(modded) == 0);

  // RemoveAnyOf prefers plain stacks and keeps stolen for last
  std::vector<InventoryEntry> removed;
  REQUIRE(inv.RemoveAnyOf(k10mm, 2, &removed));
  REQUIRE(inv.CountBase(k10mm) == 1);
  REQUIRE(inv.Entries()[0].key.stolenFrom == 0x1234);
  REQUIRE_FALSE(inv.RemoveAnyOf(k10mm, 5));
  REQUIRE(inv.CountBase(k10mm) == 1);
}

TEST_CASE("Fo4Inventory JSON round trip tolerates a bad entry", "[fo4][F04]")
{
  Fo4Inventory inv;
  ItemKey k = ItemKey{ k10mm }.WithMods({ kModMagLarge, kModMount });
  k.ammoLoaded = 7;
  inv.Add(k, 1);
  inv.AddSimple(kWrench, 3);
  auto j = inv.ToJson();
  REQUIRE(Fo4Inventory::FromJson(j) == inv);

  j["entries"].push_back({ { "count", 1 } }); // no baseId
  auto restored = Fo4Inventory::FromJson(j);
  REQUIRE(restored == inv);
  REQUIRE(Fo4Inventory::FromJson(nlohmann::json(42)).IsEmpty());
}

TEST_CASE("OmodStatResolver applies SET, MUL+ADD and ADD", "[fo4][F16]")
{
  World w;
  OmodStatResolver r(w.data);
  auto base = r.ResolveWeapon(ItemKey{ k10mm });
  REQUIRE(base.damage == 18.f);
  REQUIRE(base.capacity == 12);
  REQUIRE(base.ammoId == kAmmo10mm);
  REQUIRE_FALSE(base.hasScope);
  REQUIRE(base.MaxShotsPerSecond() == Catch::Approx(4.f));

  auto modded = r.ResolveWeapon(ItemKey{ k10mm }.WithMods(
    { kModReceiverHardened, kModMagLarge, kModMount, kModScope }));
  REQUIRE(modded.damage == Catch::Approx(22.5f));
  REQUIRE(modded.capacity == 18);
  REQUIRE(modded.value == Catch::Approx(73.f));
  REQUIRE(modded.hasScope);

  auto caliber =
    r.ResolveWeapon(ItemKey{ k10mm }.WithMods({ kModReceiver45 }));
  REQUIRE(caliber.ammoId == kAmmo45);

  auto slots = r.GetAttachSlots(ItemKey{ k10mm }.WithMods({ kModMount }));
  REQUIRE(slots.count(kApScope) == 1);
  REQUIRE(r.GetAttachSlots(ItemKey{ k10mm }).count(kApScope) == 0);

  REQUIRE_THROWS(r.ResolveWeapon(ItemKey{ kWrench }));
  auto armor = r.ResolveArmor(ItemKey{ kT45Torso });
  REQUIRE(armor.armorRating == 110.f);
  REQUIRE(armor.health == 450.f);
}

TEST_CASE("CollectMods follows includes once and ignores cycles", "[fo4][F16]")
{
  World w;
  ObjectModData a;
  a.id = 0xA1;
  a.includes = { 0xA2 };
  a.priority = 2;
  ObjectModData b;
  b.id = 0xA2;
  b.includes = { 0xA1 }; // cycle
  b.priority = 1;
  w.data.AddObjectMod(a);
  w.data.AddObjectMod(b);
  OmodStatResolver r(w.data);
  auto mods = r.CollectMods({ 0xA1 });
  REQUIRE(mods.size() == 2);
  REQUIRE(mods[0]->id == 0xA2); // lower priority first
}

TEST_CASE("ComponentPlanner uses scrap first, then cheapest junk, and "
          "returns leftovers",
          "[fo4][F15]")
{
  World w;
  ComponentPlanner p(w.data);
  Fo4Inventory inv;
  inv.AddSimple(kSteelScrap, 1);
  inv.AddSimple(kWrench, 1);
  inv.AddSimple(kDeskFan, 1);

  REQUIRE(p.CountAvailable(inv, kSteel) == 4);
  REQUIRE(p.CountAvailable(inv, kScrew) == 2);

  auto plan = p.Plan(inv, { { kSteel, 2 } });
  REQUIRE(plan.ok);
  // 1 loose steel + scrap the wrench (cheapest per steel: 10/2 = 5 < 15/1)
  REQUIRE(plan.resultInventory.CountBase(kSteelScrap) == 1); // leftover
  REQUIRE(plan.resultInventory.CountBase(kWrench) == 0);
  REQUIRE(plan.resultInventory.CountBase(kDeskFan) == 1);

  auto multi = p.Plan(inv, { { kScrew, 1 }, { kSteel, 4 } });
  REQUIRE(multi.ok);
  REQUIRE(multi.resultInventory.CountBase(kDeskFan) == 0);
  REQUIRE(multi.resultInventory.CountBase(kWrench) == 0);
  REQUIRE(multi.resultInventory.CountBase(kScrewScrap) == 1);
  REQUIRE(multi.resultInventory.CountBase(kCircuitryScrap) == 1);
  REQUIRE(multi.resultInventory.CountBase(kSteelScrap) == 0);

  auto fail = p.Plan(inv, { { kSteel, 5 }, { kAdhesive, 1 } });
  REQUIRE_FALSE(fail.ok);
  REQUIRE(fail.shortfalls.size() == 2);
  REQUIRE(fail.shortfalls[0].missing == 1);
  REQUIRE(fail.resultInventory == inv);
}

TEST_CASE("ComponentPlanner never auto-scraps stolen junk", "[fo4][F15]")
{
  World w;
  ComponentPlanner p(w.data);
  Fo4Inventory inv;
  ItemKey stolen{ kWrench };
  stolen.stolenFrom = 0x55;
  inv.Add(stolen, 1);
  REQUIRE_FALSE(p.Plan(inv, { { kSteel, 1 } }).ok);
}

TEST_CASE("CraftingService validates bench, perks and components "
          "atomically",
          "[fo4][F15]")
{
  World w;
  CraftingService cs(w.data);
  Fo4Inventory inv;
  inv.AddSimple(kDuctTape, 1);
  inv.AddSimple(kWrench, 1);
  Fo4Inventory before = inv;

  auto wrongBench = cs.Craft(inv, kRecipeStimpak, 1, Bench(kWorkbenchWeapons));
  REQUIRE(wrongBench.error == CraftError::WrongWorkbench);
  REQUIRE(inv == before);

  REQUIRE(cs.Craft(inv, 0xDEAD, 1, Bench(kWorkbenchChem)).error ==
          CraftError::UnknownRecipe);
  REQUIRE(cs.Craft(inv, kRecipeStimpak, 0, Bench(kWorkbenchChem)).error ==
          CraftError::InvalidCount);

  auto twice = cs.Craft(inv, kRecipeStimpak, 2, Bench(kWorkbenchChem));
  REQUIRE(twice.error == CraftError::MissingComponents);
  REQUIRE(inv == before);

  auto ok = cs.Craft(inv, kRecipeStimpak, 1, Bench(kWorkbenchChem));
  REQUIRE(ok.Ok());
  REQUIRE(ok.createdCount == 1);
  REQUIRE(inv.CountBase(kStimpak) == 1);
  REQUIRE(inv.CountBase(kWrench) == 0);
  REQUIRE(inv.CountBase(kDuctTape) == 0);

  // Crafting a mod at the weapons bench needs Gun Nut 1 and yields the
  // loose mod item
  Fo4Inventory mats;
  mats.AddSimple(kSteelScrap, 4);
  mats.AddSimple(kScrewScrap, 2);
  REQUIRE(
    cs.Craft(mats, kRecipeHardened, 1, Bench(kWorkbenchWeapons, 0)).error ==
    CraftError::MissingPerk);
  auto modCraft =
    cs.Craft(mats, kRecipeHardened, 1, Bench(kWorkbenchWeapons, 1));
  REQUIRE(modCraft.Ok());
  REQUIRE(mats.CountBase(kLooseHardened) == 1);
  REQUIRE(mats.CountBase(kSteelScrap) == 0);

  auto list = cs.GetAvailableRecipes(Bench(kWorkbenchWeapons));
  REQUIRE(list.size() == 6);
}

TEST_CASE("ScrapService scraps junk and modded weapons", "[fo4][F15]")
{
  World w;
  ScrapService ss(w.data);
  Fo4Inventory inv;
  inv.AddSimple(kDeskFan, 2);
  REQUIRE(ss.ScrapJunk(inv, ItemKey{ kDeskFan }, 3).error ==
          CraftError::ItemNotFound);
  auto r = ss.ScrapJunk(inv, ItemKey{ kDeskFan }, 2);
  REQUIRE(r.Ok());
  REQUIRE(inv.CountBase(kScrewScrap) == 4);
  REQUIRE(inv.CountBase(kSteelScrap) == 2);
  REQUIRE(inv.CountBase(kCircuitryScrap) == 2);
  REQUIRE(ss.ScrapJunk(inv, ItemKey{ kStimpak }, 1).error ==
          CraftError::NotScrappable);

  ItemKey gun = ItemKey{ k10mm }.WithMods({ kModReceiverHardened });
  gun.ammoLoaded = 5;
  inv.Add(gun, 1);
  CrafterContext ctx = Bench(kWorkbenchWeapons);
  auto preview = ss.PreviewEquipmentYield(gun, ctx);
  // base recipe 8 steel * 0.5 scalar * 0.5 yield = 2; hardened 4*0.5=2,
  // screws 2*0.5=1
  REQUIRE(preview ==
          std::vector<ComponentCount>{ { kSteel, 4 }, { kScrew, 1 } });
  uint32_t steelBefore = inv.CountBase(kSteelScrap);
  auto sr = ss.ScrapEquipment(inv, gun, ctx);
  REQUIRE(sr.Ok());
  REQUIRE(inv.Count(gun) == 0);
  REQUIRE(inv.CountBase(kSteelScrap) == steelBefore + 4);
  REQUIRE(inv.CountBase(kAmmo10mm) == 5); // rounds returned

  ctx.scrapperRank = 2;
  auto bonus = ss.PreviewEquipmentYield(gun, ctx);
  REQUIRE(bonus[0].count == 6);
}

TEST_CASE("ModdingService attaches, replaces and prunes dependent mods",
          "[fo4][F16]")
{
  World w;
  ModdingService ms(w.data);
  Fo4Inventory inv;
  ItemKey gun{ k10mm };
  gun.ammoLoaded = 12;
  inv.Add(gun, 1);
  inv.AddSimple(kSteelScrap, 20);
  inv.AddSimple(kScrewScrap, 5);
  inv.AddSimple(kCircuitryScrap, 1);
  auto ctx = Bench(kWorkbenchWeapons, 1);

  // Scope needs the mount's slot first
  REQUIRE(ms.AttachMod(inv, gun, kModScope, ctx).error ==
          CraftError::IncompatibleMod);

  auto mount = ms.AttachMod(inv, gun, kModMount, ctx);
  REQUIRE(mount.Ok());
  auto scope = ms.AttachMod(inv, mount.newKey, kModScope, ctx);
  REQUIRE(scope.Ok());
  REQUIRE(scope.newKey.HasMod(kModScope));
  REQUIRE(inv.CountBase(kCircuitryScrap) == 0);

  // Detaching the mount also removes the scope; both come back as loose
  auto detached = ms.DetachMod(inv, scope.newKey, kModMount, ctx);
  REQUIRE(detached.Ok());
  REQUIRE(detached.newKey.mods.empty());
  REQUIRE(detached.removedMods.size() == 2);
  REQUIRE(inv.CountBase(kLooseMount) == 1);
  REQUIRE(inv.CountBase(kLooseScope) == 1);

  // Re-attach using the loose mount: free
  auto steel = inv.CountBase(kSteelScrap);
  auto again = ms.AttachMod(inv, detached.newKey, kModMount, ctx);
  REQUIRE(again.Ok());
  REQUIRE(inv.CountBase(kSteelScrap) == steel);
  REQUIRE(inv.CountBase(kLooseMount) == 0);

  // Receivers replace each other in the same attach point
  auto hardened = ms.AttachMod(inv, again.newKey, kModReceiverHardened, ctx);
  REQUIRE(hardened.Ok());
  auto r45 = ms.AttachMod(inv, hardened.newKey, kModReceiver45, ctx);
  REQUIRE(r45.Ok());
  REQUIRE_FALSE(r45.newKey.HasMod(kModReceiverHardened));
  REQUIRE(inv.CountBase(kLooseHardened) == 1);
  // Caliber change unloads the magazine into the inventory
  REQUIRE(r45.ammoReturned == 12);
  REQUIRE(r45.newKey.ammoLoaded == 0);
  REQUIRE(inv.CountBase(kAmmo10mm) == 12);
  REQUIRE(inv.CountBase(k10mm) == 1);
}

TEST_CASE("ModdingService refuses without perk or components and leaves "
          "the inventory unchanged",
          "[fo4][F16]")
{
  World w;
  ModdingService ms(w.data);
  Fo4Inventory inv;
  inv.Add(ItemKey{ k10mm }, 1);
  inv.AddSimple(kSteelScrap, 1);
  Fo4Inventory before = inv;
  REQUIRE(ms.AttachMod(inv, ItemKey{ k10mm }, kModReceiverHardened,
                       Bench(kWorkbenchWeapons, 0))
            .error == CraftError::MissingPerk);
  auto r = ms.AttachMod(inv, ItemKey{ k10mm }, kModMagLarge,
                        Bench(kWorkbenchWeapons, 1));
  REQUIRE(r.error == CraftError::MissingComponents);
  REQUIRE(inv == before);
  REQUIRE(
    ms.AttachMod(inv, ItemKey{ k10mm }, kModMagLarge, Bench(kWorkbenchChem, 1))
      .error == CraftError::WrongWorkbench);
  REQUIRE(ms.AttachMod(inv, ItemKey{ kWrench }, kModMagLarge,
                       Bench(kWorkbenchWeapons, 1))
            .error == CraftError::NotModifiable);
}
