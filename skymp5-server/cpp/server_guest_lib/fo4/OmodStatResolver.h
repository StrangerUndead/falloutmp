#pragma once
// Object-mod stat resolver (SRV-022).
//
// Computes the effective stats of a weapon or armor instance from its base
// record and attached mods (plus every mod they include). Used by damage
// (F11), fire validation (F09), weight and value (F04, F23), and the
// workbench UI data sent to clients (F16).
//
// Numeric rule per property (documented for Fallout 4, [verify] F16 G-manual
// against the in-game Pip-Boy numbers):
//   result = (lastSet ? lastSet : base) * (1 + sum(MUL+ADD)) + sum(ADD)
// Mods are applied in (priority, formId) order, so the result does not depend
// on attach order. Booleans: SET, then AND, then OR in the same order.
#include "Fo4Data.h"
#include "ItemInstance.h"
#include <set>
#include <string>

namespace fo4 {

// Weapon property indices (OMOD DATA "Property" for form type WEAP).
namespace WeaponProperty {
enum : uint16_t
{
  Speed = 0,
  Reach = 1,
  MinRange = 2,
  MaxRange = 3,
  AttackDelaySec = 4,
  OutOfRangeDamageMult = 6,
  SecondaryDamage = 7,
  CriticalChargeBonus = 8,
  AmmoCapacity = 12,
  IsAutomatic = 25,
  AttackDamage = 28,
  Value = 29,
  Weight = 30,
  Keywords = 31,
  HasScope = 48,
  FireSeconds = 50,
  NumProjectiles = 51,
  Ammo = 61,
  ReloadSpeed = 76,
  DamageTypeValues = 77,
  AccuracyBonus = 78,
  AttackActionPointCost = 79,
  CriticalDamageMult = 90,
};
}

namespace ArmorProperty {
enum : uint16_t
{
  Keywords = 3,
  Weight = 4,
  Value = 5,
  Rating = 6,
  DamageTypeValue = 9,
  Health = 11,
};
}

struct WeaponStats
{
  float damage = 0.f; // physical base damage
  std::vector<DamageTypeValue> damageTypes;
  uint32_t capacity = 0;
  FormId ammoId = 0;
  float speed = 1.f;
  float reloadSpeed = 1.f;
  float attackDelaySec = 0.f;
  float fireSeconds = 0.f;
  float minRange = 0.f;
  float maxRange = 0.f;
  float actionPointCost = 20.f;
  float critDamageMult = 2.f;
  float critChargeBonus = 0.f;
  float accuracyBonus = 0.f;
  float numProjectiles = 1.f;
  float weight = 0.f;
  float value = 0.f;
  bool automatic = false;
  bool hasScope = false;
  std::set<FormId> keywords;

  // Shots per second the server allows (F09 fire-rate validation). Uses the
  // longer of attack delay and fire animation time, scaled by speed.
  float MaxShotsPerSecond() const;
  float TotalDamage() const;
};

struct ArmorStats
{
  float armorRating = 0.f; // physical DR
  std::vector<DamageTypeValue> resistances;
  float weight = 0.f;
  float value = 0.f;
  float health = 0.f;
  std::set<FormId> keywords;
};

class OmodStatResolver
{
public:
  explicit OmodStatResolver(const IFo4DataSource& data);

  // Throws std::runtime_error if the base is not a weapon/armor.
  WeaponStats ResolveWeapon(const ItemKey& key) const;
  ArmorStats ResolveArmor(const ItemKey& key) const;

  // All mods that apply to the instance: attached mods plus their includes
  // (recursively, each at most once), sorted by (priority, formId).
  std::vector<const ObjectModData*> CollectMods(
    const std::vector<FormId>& attached) const;

  // Attach-parent slots an instance currently offers: the base APPR plus
  // the slots added by attached mods.
  std::set<FormId> GetAttachSlots(const ItemKey& key) const;

  // Display name using the base name and mod names (simplified instance
  // naming; full INNR rules are F16-T07).
  std::string GetDisplayName(const ItemKey& key) const;

private:
  const IFo4DataSource& data;
};

}
