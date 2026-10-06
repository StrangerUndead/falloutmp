#include "Effects.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace fo4 {

EffectSystem::EffectSystem(const IFo4DataSource& data_)
  : data(data_)
{
}

void EffectSystem::DefineEffect(EffectDefinition def)
{
  defs[def.effectId] = def;
}

const EffectDefinition* EffectSystem::FindEffect(FormId id) const
{
  auto it = defs.find(id);
  return it == defs.end() ? nullptr : &it->second;
}

void EffectSystem::Start(const ActiveEffect& e, ActorValueStore& avs)
{
  switch (e.kind) {
    case EffectKind::RestoreInstant:
      avs.Restore(e.actorValue, e.magnitude);
      break;
    case EffectKind::AddRads:
      avs.Damage(Av::Rads, -e.magnitude);
      break;
    case EffectKind::ValueModifier:
      avs.ModTemporary(e.actorValue, e.magnitude);
      break;
    default:
      break;
  }
}

void EffectSystem::End(const ActiveEffect& e, ActorValueStore& avs)
{
  if (e.kind == EffectKind::ValueModifier) {
    avs.ModTemporary(e.actorValue, -e.magnitude);
  }
}

UseItemResult EffectSystem::UseItem(FormId itemId, Fo4Inventory& inv,
                                    ActorValueStore& avs, std::mt19937& rng)
{
  UseItemResult r;
  if (avs.IsDead()) {
    r.error = UseItemError::Dead;
    return r;
  }
  auto item = data.FindConsumable(itemId);
  if (!item) {
    r.error = UseItemError::NotConsumable;
    return r;
  }
  if (!inv.RemoveAnyOf(itemId, 1)) {
    r.error = UseItemError::NotInInventory;
    return r;
  }
  for (auto& e : item->effects) {
    auto def = FindEffect(e.effectId);
    if (!def) {
      continue;
    }
    ActiveEffect a;
    a.effectId = e.effectId;
    a.sourceItem = itemId;
    a.kind = def->kind;
    a.actorValue = def->actorValue;
    a.magnitude = e.magnitude;
    a.remainingSec = static_cast<float>(e.durationSec);
    Start(a, avs);
    bool lasting = a.kind != EffectKind::RestoreInstant &&
      a.kind != EffectKind::AddRads && a.remainingSec > 0.f;
    if (lasting) {
      active.push_back(a);
    }
    r.started.push_back(a);
  }
  if (item->addictionId && item->addictionChance > 0.f &&
      !addictions.count(item->addictionId)) {
    std::uniform_real_distribution<float> roll(0.f, 1.f);
    if (roll(rng) < item->addictionChance) {
      addictions[item->addictionId] = true;
      r.becameAddicted = true;
    }
  }
  return r;
}

void EffectSystem::Tick(float dtSec, ActorValueStore& avs)
{
  if (dtSec <= 0.f) {
    return;
  }
  for (auto& e : active) {
    float dt = std::min(dtSec, e.remainingSec);
    switch (e.kind) {
      case EffectKind::RestoreOverTime:
        avs.Restore(e.actorValue, e.magnitude * dt);
        break;
      case EffectKind::DamageOverTime:
        avs.Damage(e.actorValue, -e.magnitude * dt);
        break;
      case EffectKind::RemoveRads:
        avs.Restore(Av::Rads, e.magnitude * dt);
        break;
      default:
        break;
    }
    e.remainingSec -= dtSec;
  }
  for (auto& e : active) {
    if (e.remainingSec <= 0.f) {
      End(e, avs);
    }
  }
  active.erase(std::remove_if(
                 active.begin(), active.end(),
                 [](const ActiveEffect& e) { return e.remainingSec <= 0.f; }),
               active.end());
}

void EffectSystem::ApplyRadiationExposure(float radsPerSec, float dtSec,
                                          ActorValueStore& avs)
{
  if (radsPerSec <= 0.f || dtSec <= 0.f) {
    return;
  }
  float resist = std::clamp(avs.GetCurrent(Av::RadResistExposure), 0.f, 85.f);
  avs.Damage(Av::Rads, -radsPerSec * dtSec * (1.f - resist / 100.f));
}

nlohmann::json EffectSystem::ToJson() const
{
  auto arr = nlohmann::json::array();
  for (auto& e : active) {
    arr.push_back({ e.effectId, e.sourceItem, static_cast<int>(e.kind),
                    e.actorValue, e.magnitude, e.remainingSec });
  }
  std::vector<FormId> addicted;
  for (auto& [id, v] : addictions) {
    if (v)
      addicted.push_back(id);
  }
  return { { "active", arr }, { "addictions", addicted } };
}

void EffectSystem::LoadJson(const nlohmann::json& j, ActorValueStore& avs)
{
  (void)avs; // temporary modifiers are persisted inside the AV store
  active.clear();
  addictions.clear();
  if (!j.is_object()) {
    return;
  }
  for (auto& e : j.value("active", nlohmann::json::array())) {
    try {
      ActiveEffect a;
      a.effectId = e.at(0).get<FormId>();
      a.sourceItem = e.at(1).get<FormId>();
      a.kind = static_cast<EffectKind>(e.at(2).get<int>());
      a.actorValue = e.at(3).get<FormId>();
      a.magnitude = e.at(4).get<float>();
      a.remainingSec = e.at(5).get<float>();
      active.push_back(a);
    } catch (const std::exception&) {
    }
  }
  for (auto& id : j.value("addictions", std::vector<FormId>{})) {
    addictions[id] = true;
  }
}

}
