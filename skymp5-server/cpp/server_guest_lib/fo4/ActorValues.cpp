#include "ActorValues.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace fo4 {

AvEntry& ActorValueStore::Get(FormId av)
{
  return entries[av];
}

const AvEntry* ActorValueStore::Find(FormId av) const
{
  auto it = entries.find(av);
  return it == entries.end() ? nullptr : &it->second;
}

float ActorValueStore::GetBase(FormId av) const
{
  auto e = Find(av);
  return e ? e->base : 0.f;
}

float ActorValueStore::GetCurrent(FormId av) const
{
  auto e = Find(av);
  return e ? e->Current() : 0.f;
}

float ActorValueStore::GetMax(FormId av) const
{
  auto e = Find(av);
  return e ? e->Max() : 0.f;
}

void ActorValueStore::SetBase(FormId av, float v)
{
  Get(av).base = v;
}

void ActorValueStore::ModPermanent(FormId av, float d)
{
  Get(av).permanent += d;
}

void ActorValueStore::ModTemporary(FormId av, float d)
{
  Get(av).temporary += d;
}

void ActorValueStore::Damage(FormId av, float delta)
{
  auto& e = Get(av);
  if (av == Av::Rads) {
    // Rads accumulate as positive damage, capped at 1000
    e.damage = std::clamp(e.damage - delta, 0.f, 1000.f);
    return;
  }
  e.damage = std::min(0.f, e.damage + delta);
  // never below -max (value 0)
  e.damage = std::max(e.damage, -std::max(0.f, e.Max()));
}

void ActorValueStore::Restore(FormId av, float amount)
{
  if (amount <= 0.f) {
    return;
  }
  auto& e = Get(av);
  if (av == Av::Rads) {
    e.damage = std::max(0.f, e.damage - amount);
    return;
  }
  e.damage = std::min(0.f, e.damage + amount);
}

void ActorValueStore::SetCurrent(FormId av, float v)
{
  auto& e = Get(av);
  if (av == Av::Rads) {
    e.damage = std::clamp(v - e.Max(), 0.f, 1000.f);
    return;
  }
  e.damage = std::clamp(v - e.Max(), -std::max(0.f, e.Max()), 0.f);
}

void ActorValueStore::RecomputeDerived(int32_t level)
{
  float end = GetCurrent(Av::Endurance);
  float agi = GetCurrent(Av::Agility);
  float str = GetCurrent(Av::Strength);
  float lvl = static_cast<float>(std::max(1, level));
  SetBase(Av::Health, 80.f + 5.f * end + (lvl - 1.f) * (2.5f + end / 2.f));
  SetBase(Av::ActionPoints, 60.f + 10.f * agi);
  SetBase(Av::CarryWeight, 200.f + 10.f * str);
  for (auto limb : kLimbConditions) {
    if (!Find(limb)) {
      SetBase(limb, 100.f);
    }
  }
}

float ActorValueStore::GetEffectiveMaxHealth() const
{
  float rads = GetCurrent(Av::Rads);
  return GetMax(Av::Health) * std::max(0.f, 1.f - rads / 1000.f);
}

bool ActorValueStore::IsDead() const
{
  return GetCurrent(Av::Health) <= 0.f || GetCurrent(Av::Rads) >= 1000.f;
}

bool ActorValueStore::IsLimbCrippled(FormId limbAv) const
{
  auto e = Find(limbAv);
  return e && e->Current() <= 0.f;
}

void ActorValueStore::Tick(float dtSec, bool inCombat)
{
  if (dtSec <= 0.f) {
    return;
  }
  float apMax = GetMax(Av::ActionPoints);
  Restore(Av::ActionPoints, apMax * apRegenPerSec / 100.f * dtSec);
  if (!inCombat) {
    for (auto limb : kLimbConditions) {
      if (Find(limb)) {
        Restore(limb, limbRegenPerSec * dtSec);
      }
    }
  }
  // Health can't exceed the rad-reduced maximum
  float hpMax = GetEffectiveMaxHealth();
  if (GetCurrent(Av::Health) > hpMax) {
    SetCurrent(Av::Health, hpMax);
  }
}

AvPolicy ActorValueStore::GetPolicy(FormId av, bool hostedNpc)
{
  switch (av) {
    case Av::Health:
      return hostedNpc ? AvPolicy::DecreaseOnly : AvPolicy::DecreaseOnly;
    case Av::ActionPoints:
      return AvPolicy::RegenCropped;
    case Av::Rads:
      return AvPolicy::IncreaseOnly;
    default:
      for (auto limb : kLimbConditions) {
        if (av == limb) {
          return AvPolicy::DecreaseOnly;
        }
      }
      return AvPolicy::ServerOnly;
  }
}

AvReportResult ActorValueStore::ApplyClientReport(FormId av, float reported,
                                                  float dtSec, bool hostedNpc,
                                                  bool serverKnowsCause)
{
  AvReportResult r;
  if (!std::isfinite(reported)) {
    r.correction = true;
    r.applied = GetCurrent(av);
    return r;
  }
  float cur = GetCurrent(av);
  auto policy = GetPolicy(av, hostedNpc);

  // C1: a host can only lower NPC values for a cause the server knows, at
  // a bounded rate, and never kill through this path.
  if (hostedNpc && policy != AvPolicy::RegenCropped) {
    if (!serverKnowsCause) {
      r.correction = true;
      r.applied = cur;
      return r;
    }
    float maxDelta = hostedNpcMaxDecreasePerSec * std::max(dtSec, 0.f);
    if (av == Av::Rads) {
      reported = std::min({ reported, cur + maxDelta, 999.f });
    } else {
      reported = std::max({ reported, cur - maxDelta, 1.f });
    }
    r.correction = reported != cur;
  }

  switch (policy) {
    case AvPolicy::ServerOnly:
      r.correction = true;
      r.applied = cur;
      return r;
    case AvPolicy::DecreaseOnly:
      if (reported < cur) {
        SetCurrent(av, std::max(0.f, reported));
        r.accepted = true;
      } else if (reported > cur) {
        r.correction = true;
      }
      break;
    case AvPolicy::IncreaseOnly:
      if (reported > cur) {
        SetCurrent(av, std::min(1000.f, reported));
        r.accepted = true;
      } else if (reported < cur) {
        r.correction = true;
      }
      break;
    case AvPolicy::RegenCropped: {
      float maxRegen =
        GetMax(av) * apRegenPerSec / 100.f * std::max(dtSec, 0.f) + 0.5f;
      float allowed = std::min(reported, cur + maxRegen);
      allowed = std::min(allowed, GetMax(av));
      if (allowed != reported) {
        r.correction = true;
      }
      SetCurrent(av, std::max(0.f, allowed));
      r.accepted = true;
      break;
    }
  }
  r.applied = GetCurrent(av);
  return r;
}

nlohmann::json ActorValueStore::ToJson() const
{
  nlohmann::json j = nlohmann::json::object();
  for (auto& [id, e] : entries) {
    j[std::to_string(id)] = { e.base, e.permanent, e.temporary, e.damage };
  }
  return j;
}

ActorValueStore ActorValueStore::FromJson(const nlohmann::json& j)
{
  ActorValueStore s;
  if (!j.is_object()) {
    return s;
  }
  for (auto& [k, v] : j.items()) {
    try {
      AvEntry e{ v.at(0).get<float>(), v.at(1).get<float>(),
                 v.at(2).get<float>(), v.at(3).get<float>() };
      s.entries[static_cast<FormId>(std::stoul(k))] = e;
    } catch (const std::exception&) {
    }
  }
  return s;
}

// ---------------------------------------------------------------- damage

float ResistanceCoefficient(float damage, float resistance, float factor,
                            float exponent)
{
  if (damage <= 0.f) {
    return 0.f;
  }
  if (resistance <= 0.f) {
    return 0.99f;
  }
  float c = std::pow(factor * damage / resistance, exponent);
  return std::clamp(c, 0.01f, 0.99f);
}

float DifficultyMult(Difficulty d, bool attackerIsPlayer, bool targetIsPlayer)
{
  // Player deals / enemy deals (wiki table). PvP is neither: x1.
  static const float playerDeals[] = { 2.f, 1.5f, 1.f, 0.75f, 0.5f, 0.75f };
  static const float enemyDeals[] = { 0.5f, 0.75f, 1.f, 1.5f, 2.f, 4.f };
  auto i = static_cast<size_t>(d);
  if (attackerIsPlayer && !targetIsPlayer) {
    return playerDeals[i];
  }
  if (!attackerIsPlayer && targetIsPlayer) {
    return enemyDeals[i];
  }
  return 1.f;
}

DamageModel::DamageModel(DamageSettings s)
  : settings(s)
{
}

HitOutcome DamageModel::Resolve(const HitInput& hit,
                                const TargetResistances& r) const
{
  HitOutcome out;
  float factor =
    hit.isPvp ? settings.pvpDamageFactor : settings.physicalDamageFactor;
  float pen = std::clamp(hit.armorPenetration, 0.f, 1.f);
  float mult = hit.extraMult;
  if (hit.headshot) {
    mult *= settings.headshotMult;
  }
  if (hit.sneak) {
    mult *= hit.melee ? hit.sneakMultMelee : hit.sneakMultRanged;
  }
  mult *= DifficultyMult(settings.difficulty, hit.attackerIsPlayer,
                         hit.targetIsPlayer);
  if (hit.isPvp) {
    mult *= settings.pvpDamageMult;
  }

  for (auto& d : hit.paperDamage) {
    if (d.value <= 0.f) {
      continue;
    }
    if (auto im = r.immune.find(d.damageTypeId);
        im != r.immune.end() && im->second) {
      continue;
    }
    float resist = d.damageTypeId == 0
      ? r.damageResist
      : (r.byType.count(d.damageTypeId) ? r.byType.at(d.damageTypeId) : 0.f);
    resist *= (1.f - pen);
    // Multi-projectile weapons: coefficient from the total of all pellets
    float coeffDamage = d.value * std::max<uint32_t>(1, hit.projectiles);
    float coeff = ResistanceCoefficient(coeffDamage, resist, factor,
                                        settings.armorReductionExp);
    float dealt = d.value * coeff;
    if (hit.critical && d.damageTypeId == 0) {
      float critBonus = hit.baseDamageForCrit * hit.critMult;
      float paper = hit.melee ? d.value * 1.5f : d.value;
      dealt = settings.critsIgnoreResistance
        ? paper + critBonus
        : (paper + critBonus) * coeff;
    }
    dealt *= mult;
    out.perType.push_back({ d.damageTypeId, dealt });
    out.total += dealt;
  }
  return out;
}

}
