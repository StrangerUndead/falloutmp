#pragma once
// Consumables and active effects (F20): stimpaks, chems, food, RadAway,
// Rad-X, addiction rolls, and environmental radiation.
#include "ActorValues.h"
#include "ItemInstance.h"
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <random>

namespace fo4 {

// Effect archetypes the server simulates (subset of MGEF archetypes).
enum class EffectKind : uint8_t
{
  RestoreOverTime, // stimpak: magnitude per second of the AV, for duration
  RestoreInstant,  // food: magnitude once
  DamageOverTime,  // poison, bleed
  ValueModifier,   // chems: +magnitude to the AV while active (temporary)
  RemoveRads,      // RadAway: magnitude rads per second
  AddRads,         // irradiated food/water
};

struct EffectDefinition
{
  FormId effectId = 0;
  EffectKind kind = EffectKind::ValueModifier;
  FormId actorValue = 0;
};

struct ActiveEffect
{
  FormId effectId = 0;
  FormId sourceItem = 0;
  EffectKind kind = EffectKind::ValueModifier;
  FormId actorValue = 0;
  float magnitude = 0.f;
  float remainingSec = 0.f;
};

enum class UseItemError : uint8_t
{
  None = 0,
  NotInInventory,
  NotConsumable,
  Dead,
};

struct UseItemResult
{
  UseItemError error = UseItemError::None;
  bool becameAddicted = false;
  std::vector<ActiveEffect> started;
  bool Ok() const { return error == UseItemError::None; }
};

class EffectSystem
{
public:
  explicit EffectSystem(const IFo4DataSource& data);

  void DefineEffect(EffectDefinition def);
  const EffectDefinition* FindEffect(FormId id) const;

  // Consumes one item from the inventory and starts its effects.
  UseItemResult UseItem(FormId itemId, Fo4Inventory& inv, ActorValueStore& avs,
                        std::mt19937& rng);
  // Advances all effects by dt seconds.
  void Tick(float dtSec, ActorValueStore& avs);
  // Exposure (radiation zones, F20 §env): rads per second before
  // RadResistExposure, which reduces it 1% per point up to 85%.
  void ApplyRadiationExposure(float radsPerSec, float dtSec,
                              ActorValueStore& avs);

  const std::vector<ActiveEffect>& Active() const { return active; }
  const std::map<FormId, bool>& Addictions() const { return addictions; }
  void CureAddictions() { addictions.clear(); }

  nlohmann::json ToJson() const;
  void LoadJson(const nlohmann::json& j, ActorValueStore& avs);

private:
  void Start(const ActiveEffect& e, ActorValueStore& avs);
  void End(const ActiveEffect& e, ActorValueStore& avs);

  const IFo4DataSource& data;
  std::map<FormId, EffectDefinition> defs;
  std::vector<ActiveEffect> active;
  std::map<FormId, bool> addictions;
};

}
