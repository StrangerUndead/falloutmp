#include "OmodStatResolver.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace fo4 {

namespace {

struct NumericAccumulator
{
  std::optional<float> set;
  float mul = 0.f;
  float add = 0.f;

  void Apply(const OmodPropertyData& p)
  {
    switch (p.function) {
      case OmodFunction::Set:
        set = p.value1;
        break;
      case OmodFunction::MulAdd:
        mul += p.value1;
        break;
      case OmodFunction::Add:
        add += p.value1;
        break;
    }
  }

  float Result(float base) const
  {
    return (set ? *set : base) * (1.f + mul) + add;
  }
};

struct BoolAccumulator
{
  std::vector<std::pair<OmodFunction, bool>> ops;
  void Apply(const OmodPropertyData& p)
  {
    ops.push_back({ p.function, p.value1 != 0.f });
  }
  bool Result(bool base) const
  {
    bool v = base;
    for (auto& [fn, b] : ops) {
      if (fn == OmodFunction::Set) {
        v = b;
      }
    }
    for (auto& [fn, b] : ops) {
      if (fn == OmodFunction::MulAdd) {
        v = v && b; // AND
      }
    }
    for (auto& [fn, b] : ops) {
      if (fn == OmodFunction::Add) {
        v = v || b; // OR
      }
    }
    return v;
  }
};

void ApplyKeywordOp(std::set<FormId>& kws, const OmodPropertyData& p)
{
  switch (p.function) {
    case OmodFunction::Set:
    case OmodFunction::Add:
      kws.insert(p.formValue);
      break;
    case OmodFunction::MulAdd: // REM
      kws.erase(p.formValue);
      break;
  }
}

// Damage-type lists: FormID,Float pairs. ADD adds to that type, MUL+ADD
// scales the base value of that type, SET replaces it, all per damage type.
class DamageTypeAccumulator
{
public:
  explicit DamageTypeAccumulator(const std::vector<DamageTypeValue>& base)
  {
    for (auto& v : base) {
      baseValues[v.damageTypeId] += v.value;
    }
  }
  void Apply(const OmodPropertyData& p)
  {
    FormId type = p.formValue;
    float v = p.value2 != 0.f ? p.value2 : p.value1;
    acc[type].Apply(OmodPropertyData{ p.valueType, p.function, p.property, v });
  }
  std::vector<DamageTypeValue> Result() const
  {
    std::map<FormId, float> out = baseValues;
    for (auto& [type, a] : acc) {
      out[type] = a.Result(baseValues.count(type) ? baseValues.at(type) : 0.f);
    }
    std::vector<DamageTypeValue> res;
    for (auto& [type, value] : out) {
      if (value != 0.f) {
        res.push_back({ type, value });
      }
    }
    return res;
  }

private:
  std::map<FormId, float> baseValues;
  std::map<FormId, NumericAccumulator> acc;
};

}

float WeaponStats::MaxShotsPerSecond() const
{
  float cycle = std::max(attackDelaySec, fireSeconds);
  float s = speed > 0.f ? speed : 1.f;
  if (cycle <= 0.f) {
    // No timing data: Fallout 4's fastest player guns fire ~15 shots/s.
    return 15.f;
  }
  return s / cycle;
}

float WeaponStats::TotalDamage() const
{
  float total = damage;
  for (auto& d : damageTypes) {
    total += d.value;
  }
  return total;
}

OmodStatResolver::OmodStatResolver(const IFo4DataSource& data_)
  : data(data_)
{
}

std::vector<const ObjectModData*> OmodStatResolver::CollectMods(
  const std::vector<FormId>& attached) const
{
  std::vector<const ObjectModData*> res;
  std::set<FormId> seen;
  std::vector<FormId> stack(attached.rbegin(), attached.rend());
  while (!stack.empty()) {
    FormId id = stack.back();
    stack.pop_back();
    if (!seen.insert(id).second) {
      continue; // cycle or duplicate
    }
    auto mod = data.FindObjectMod(id);
    if (!mod) {
      continue;
    }
    res.push_back(mod);
    for (auto inc : mod->includes) {
      stack.push_back(inc);
    }
  }
  std::sort(res.begin(), res.end(),
            [](const ObjectModData* a, const ObjectModData* b) {
              if (a->priority != b->priority) {
                return a->priority < b->priority;
              }
              return a->id < b->id;
            });
  return res;
}

WeaponStats OmodStatResolver::ResolveWeapon(const ItemKey& key) const
{
  auto base = data.FindWeapon(key.baseId);
  if (!base) {
    throw std::runtime_error("ResolveWeapon: not a weapon");
  }
  WeaponStats s;
  s.keywords.insert(base->keywords.begin(), base->keywords.end());

  std::map<uint16_t, NumericAccumulator> num;
  BoolAccumulator automatic, scope;
  DamageTypeAccumulator damageTypes(base->damageTypes);
  std::optional<FormId> ammo;

  std::vector<FormId> mods = key.mods;
  if (base->embeddedModId) {
    mods.push_back(base->embeddedModId);
  }
  for (auto mod : CollectMods(mods)) {
    if (mod->target != OmodTarget::Weapon && mod->target != OmodTarget::None) {
      continue;
    }
    for (auto& p : mod->properties) {
      switch (p.property) {
        case WeaponProperty::IsAutomatic:
          automatic.Apply(p);
          break;
        case WeaponProperty::HasScope:
          scope.Apply(p);
          break;
        case WeaponProperty::Keywords:
          ApplyKeywordOp(s.keywords, p);
          break;
        case WeaponProperty::DamageTypeValues:
          damageTypes.Apply(p);
          break;
        case WeaponProperty::Ammo:
          if (p.function != OmodFunction::MulAdd) {
            ammo = p.formValue;
          }
          break;
        default:
          num[p.property].Apply(p);
          break;
      }
    }
  }

  auto R = [&](uint16_t prop, float baseValue) {
    auto it = num.find(prop);
    return it == num.end() ? baseValue : it->second.Result(baseValue);
  };

  s.damage = std::max(0.f, R(WeaponProperty::AttackDamage, base->baseDamage));
  s.damageTypes = damageTypes.Result();
  s.capacity = static_cast<uint32_t>(std::max(
    0.f, std::round(R(WeaponProperty::AmmoCapacity, base->capacity))));
  s.ammoId = ammo ? *ammo : base->ammoId;
  s.speed = R(WeaponProperty::Speed, base->speed);
  s.reloadSpeed = R(WeaponProperty::ReloadSpeed, base->reloadSpeed);
  s.attackDelaySec = R(WeaponProperty::AttackDelaySec, base->attackDelaySec);
  s.fireSeconds = R(WeaponProperty::FireSeconds, base->fireSeconds);
  s.minRange = R(WeaponProperty::MinRange, base->minRange);
  s.maxRange = R(WeaponProperty::MaxRange, base->maxRange);
  s.actionPointCost =
    R(WeaponProperty::AttackActionPointCost, base->actionPointCost);
  s.critDamageMult = R(WeaponProperty::CriticalDamageMult, base->critDamageMult);
  s.critChargeBonus =
    R(WeaponProperty::CriticalChargeBonus, base->critChargeBonus);
  s.accuracyBonus = R(WeaponProperty::AccuracyBonus, base->accuracyBonus);
  s.numProjectiles = std::max(
    1.f, std::round(R(WeaponProperty::NumProjectiles, base->numProjectiles)));
  s.weight = std::max(0.f, R(WeaponProperty::Weight, base->weight));
  s.value = std::max(0.f, R(WeaponProperty::Value, float(base->value)));
  s.automatic = automatic.Result(base->automatic);
  s.hasScope = scope.Result(false);
  return s;
}

ArmorStats OmodStatResolver::ResolveArmor(const ItemKey& key) const
{
  auto base = data.FindArmor(key.baseId);
  if (!base) {
    throw std::runtime_error("ResolveArmor: not an armor");
  }
  ArmorStats s;
  s.keywords.insert(base->keywords.begin(), base->keywords.end());
  std::map<uint16_t, NumericAccumulator> num;
  DamageTypeAccumulator res(base->resistances);
  for (auto mod : CollectMods(key.mods)) {
    if (mod->target != OmodTarget::Armor && mod->target != OmodTarget::None) {
      continue;
    }
    for (auto& p : mod->properties) {
      switch (p.property) {
        case ArmorProperty::Keywords:
          ApplyKeywordOp(s.keywords, p);
          break;
        case ArmorProperty::DamageTypeValue:
          res.Apply(p);
          break;
        default:
          num[p.property].Apply(p);
          break;
      }
    }
  }
  auto R = [&](uint16_t prop, float baseValue) {
    auto it = num.find(prop);
    return it == num.end() ? baseValue : it->second.Result(baseValue);
  };
  s.armorRating = std::max(0.f, R(ArmorProperty::Rating, base->armorRating));
  s.resistances = res.Result();
  s.weight = std::max(0.f, R(ArmorProperty::Weight, base->weight));
  s.value = std::max(0.f, R(ArmorProperty::Value, float(base->value)));
  s.health = std::max(0.f, R(ArmorProperty::Health, float(base->health)));
  return s;
}

std::set<FormId> OmodStatResolver::GetAttachSlots(const ItemKey& key) const
{
  std::set<FormId> slots;
  if (auto item = data.FindItem(key.baseId)) {
    slots.insert(item->attachParentSlots.begin(),
                 item->attachParentSlots.end());
  }
  for (auto mod : CollectMods(key.mods)) {
    slots.insert(mod->attachParentSlots.begin(),
                 mod->attachParentSlots.end());
  }
  return slots;
}

std::string OmodStatResolver::GetDisplayName(const ItemKey& key) const
{
  auto item = data.FindItem(key.baseId);
  std::string name = item ? item->name : std::string();
  std::string prefix;
  for (auto mod : CollectMods(key.mods)) {
    if (mod->legendary && !mod->name.empty()) {
      prefix = mod->name + " ";
    }
  }
  return prefix + name;
}

}
