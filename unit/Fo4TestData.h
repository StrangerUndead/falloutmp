#pragma once
// Small synthetic Fallout 4 data set shared by the fo4 server tests.
// Form ids are invented; names mirror vanilla content for readability.
#include "fo4/Fo4Data.h"
#include "fo4/OmodStatResolver.h"

namespace fo4test {
using namespace fo4;

// Components and their scrap items
constexpr FormId kSteel = 0x1FA8C, kSteelScrap = 0x731A4;
constexpr FormId kScrew = 0x1FA97, kScrewScrap = 0x731A5;
constexpr FormId kAdhesive = 0x1FAA5, kAdhesiveScrap = 0x731A6;
constexpr FormId kCircuitry = 0x1FA9A, kCircuitryScrap = 0x731A7;
// Junk
constexpr FormId kWrench = 0x59AE4;   // steel 2
constexpr FormId kDeskFan = 0x59AE5;  // screw 2, steel 1, circuitry 1
constexpr FormId kDuctTape = 0x59AE6; // adhesive 1
// Weapons, ammo, armor
constexpr FormId k10mm = 0x4822, kAmmo10mm = 0x1F276, kAmmo45 = 0x1F66A;
constexpr FormId kT45Torso = 0x30A0, kFusionCore = 0x75FE4;
// Keywords
constexpr FormId kApReceiver = 0x9001, kApMagazine = 0x9002, kApScope = 0x9003,
                 kApMount = 0x9004;
constexpr FormId kWorkbenchWeapons = 0x8001, kWorkbenchChem = 0x8002;
// Mods
constexpr FormId kModReceiverStd = 0x7001, kModReceiverHardened = 0x7002,
                 kModMagLarge = 0x7003, kModMount = 0x7004, kModScope = 0x7005,
                 kModReceiver45 = 0x7006;
constexpr FormId kLooseHardened = 0x7102, kLooseMagLarge = 0x7103,
                 kLooseMount = 0x7104, kLooseScope = 0x7105;
// Recipes, perks, consumables
constexpr FormId kRecipeStimpak = 0x6001, kRecipeHardened = 0x6002,
                 kRecipeMagLarge = 0x6003, kRecipeMount = 0x6004,
                 kRecipeScope = 0x6005, kRecipe10mm = 0x6006,
                 kRecipeReceiver45 = 0x6007;
constexpr FormId kPerkGunNut = 0x4D9B1, kStimpak = 0x23736;

inline OmodPropertyData NumProp(uint16_t prop, OmodFunction fn, float v)
{
  OmodPropertyData p;
  p.valueType = OmodValueType::Float;
  p.function = fn;
  p.property = prop;
  p.value1 = v;
  return p;
}

inline void Build(InMemoryFo4DataSource& d)
{
  d.AddComponent({ kSteel, "c_Steel", "Steel", kSteelScrap, 0.5f, 1 });
  d.AddComponent({ kScrew, "c_Screws", "Screw", kScrewScrap, 0.5f, 4 });
  d.AddComponent(
    { kAdhesive, "c_Adhesive", "Adhesive", kAdhesiveScrap, 0.5f, 5 });
  d.AddComponent(
    { kCircuitry, "c_Circuitry", "Circuitry", kCircuitryScrap, 0.5f, 10 });
  auto scrap = [&](FormId id, FormId comp, const char* name) {
    MiscData m;
    m.id = id;
    m.name = name;
    m.weight = 0.1f;
    m.value = 1;
    m.scrapItemForComponent = comp;
    d.AddMisc(m);
  };
  scrap(kSteelScrap, kSteel, "Steel");
  scrap(kScrewScrap, kScrew, "Screw");
  scrap(kAdhesiveScrap, kAdhesive, "Adhesive");
  scrap(kCircuitryScrap, kCircuitry, "Circuitry");

  auto junk = [&](FormId id, const char* name, int value, float weight,
                  std::vector<ComponentCount> comps) {
    MiscData m;
    m.id = id;
    m.name = name;
    m.value = value;
    m.weight = weight;
    m.components = std::move(comps);
    d.AddMisc(m);
  };
  junk(kWrench, "Wrench", 10, 2.f, { { kSteel, 2 } });
  junk(kDeskFan, "Desk Fan", 15, 3.f,
       { { kScrew, 2 }, { kSteel, 1 }, { kCircuitry, 1 } });
  junk(kDuctTape, "Duct Tape", 10, 0.2f, { { kAdhesive, 1 } });

  AmmoData a10;
  a10.id = kAmmo10mm;
  a10.name = "10mm Round";
  d.AddAmmo(a10);
  AmmoData a45;
  a45.id = kAmmo45;
  a45.name = ".45 Round";
  d.AddAmmo(a45);

  WeaponData w;
  w.id = k10mm;
  w.name = "10mm Pistol";
  w.ammoId = kAmmo10mm;
  w.baseDamage = 18;
  w.capacity = 12;
  w.weight = 4.2f;
  w.value = 53;
  w.attackDelaySec = 0.f;
  w.fireSeconds = 0.25f;
  w.isGun = true;
  w.attachParentSlots = { kApReceiver, kApMagazine, kApMount };
  d.AddWeapon(w);

  ObjectModData std;
  std.id = kModReceiverStd;
  std.name = "Standard Receiver";
  std.target = OmodTarget::Weapon;
  std.attachPointKeywordId = kApReceiver;
  d.AddObjectMod(std);

  ObjectModData hardened;
  hardened.id = kModReceiverHardened;
  hardened.name = "Hardened Receiver";
  hardened.target = OmodTarget::Weapon;
  hardened.attachPointKeywordId = kApReceiver;
  hardened.looseModId = kLooseHardened;
  hardened.properties = {
    NumProp(WeaponProperty::AttackDamage, OmodFunction::MulAdd, 0.25f),
    NumProp(WeaponProperty::Value, OmodFunction::Add, 20.f),
  };
  d.AddObjectMod(hardened);

  ObjectModData r45;
  r45.id = kModReceiver45;
  r45.name = ".45 Receiver";
  r45.target = OmodTarget::Weapon;
  r45.attachPointKeywordId = kApReceiver;
  OmodPropertyData ammoProp;
  ammoProp.valueType = OmodValueType::FormIdInt;
  ammoProp.function = OmodFunction::Set;
  ammoProp.property = WeaponProperty::Ammo;
  ammoProp.formValue = kAmmo45;
  r45.properties = { ammoProp };
  d.AddObjectMod(r45);

  ObjectModData mag;
  mag.id = kModMagLarge;
  mag.name = "Large Magazine";
  mag.target = OmodTarget::Weapon;
  mag.attachPointKeywordId = kApMagazine;
  mag.looseModId = kLooseMagLarge;
  mag.properties = { NumProp(WeaponProperty::AmmoCapacity, OmodFunction::Add,
                             6.f) };
  d.AddObjectMod(mag);

  ObjectModData mount;
  mount.id = kModMount;
  mount.name = "Scope Mount";
  mount.target = OmodTarget::Weapon;
  mount.attachPointKeywordId = kApMount;
  mount.attachParentSlots = { kApScope };
  mount.looseModId = kLooseMount;
  d.AddObjectMod(mount);

  ObjectModData scope;
  scope.id = kModScope;
  scope.name = "Short Scope";
  scope.target = OmodTarget::Weapon;
  scope.attachPointKeywordId = kApScope;
  scope.looseModId = kLooseScope;
  OmodPropertyData hasScope;
  hasScope.valueType = OmodValueType::Bool;
  hasScope.function = OmodFunction::Set;
  hasScope.property = WeaponProperty::HasScope;
  hasScope.value1 = 1.f;
  scope.properties = { hasScope };
  d.AddObjectMod(scope);

  for (auto [id, name] : std::vector<std::pair<FormId, const char*>>{
         { kLooseHardened, "Hardened Receiver (mod)" },
         { kLooseMagLarge, "Large Magazine (mod)" },
         { kLooseMount, "Scope Mount (mod)" },
         { kLooseScope, "Short Scope (mod)" } }) {
    MiscData m;
    m.id = id;
    m.name = name;
    d.AddMisc(m);
  }

  ArmorData torso;
  torso.id = kT45Torso;
  torso.name = "T-45 Torso";
  torso.armorRating = 110;
  torso.health = 450;
  torso.weight = 12.f;
  torso.powerArmorSlot = PowerArmorSlot::Torso;
  d.AddArmor(torso);

  AmmoData core;
  core.id = kFusionCore;
  core.name = "Fusion Core";
  core.charge = 10000;
  d.AddAmmo(core);

  ConsumableData stim;
  stim.id = kStimpak;
  stim.name = "Stimpak";
  stim.isMedicine = true;
  stim.weight = 0.1f;
  d.AddConsumable(stim);

  auto recipe = [&](FormId id, FormId created, FormId bench,
                    std::vector<ComponentCount> cost,
                    std::vector<PerkRequirement> perks = {}) {
    RecipeData r;
    r.id = id;
    r.createdObjectId = created;
    r.workbenchKeywordId = bench;
    r.components = std::move(cost);
    r.perks = std::move(perks);
    d.AddRecipe(r);
  };
  recipe(kRecipeStimpak, kStimpak, kWorkbenchChem,
         { { kAdhesive, 1 }, { kSteel, 2 } });
  recipe(kRecipeHardened, kModReceiverHardened, kWorkbenchWeapons,
         { { kSteel, 4 }, { kScrew, 2 } }, { { kPerkGunNut, 1 } });
  recipe(kRecipeMagLarge, kModMagLarge, kWorkbenchWeapons,
         { { kSteel, 2 }, { kScrew, 1 } });
  recipe(kRecipeMount, kModMount, kWorkbenchWeapons, { { kSteel, 1 } });
  recipe(kRecipeScope, kModScope, kWorkbenchWeapons,
         { { kSteel, 2 }, { kCircuitry, 1 } });
  recipe(kRecipe10mm, k10mm, kWorkbenchWeapons, { { kSteel, 8 } });
  recipe(kRecipeReceiver45, kModReceiver45, kWorkbenchWeapons,
         { { kSteel, 1 } });
}
}
