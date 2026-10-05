#pragma once
// Fallout 4 actor-value store (F08, SRV-010).
//
// Per actor: base value, permanent and temporary modifiers and damage per
// AVIF form id; current = base + permanent + temporary + damage. Derived
// maxima (Health, AP, CarryWeight) follow the vanilla formulas from SPECIAL
// and level. The server is the only party that raises values beyond
// regeneration; client reports go through ApplyClientReport, which crops
// per AV policy and tells the caller to send a correction.
//
// Form ids come from reference/fo4-systems-combat-character.md §1.2 and are
// resolved by editor id at load in production (ESPM-016); the defaults below
// are used only when the data source has no override.
#include "Fo4Data.h"
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>

namespace fo4 {

namespace Av {
enum : FormId
{
  Strength = 0x2C2,
  Perception = 0x2C3,
  Endurance = 0x2C4,
  Charisma = 0x2C5,
  Intelligence = 0x2C6,
  Agility = 0x2C7,
  Luck = 0x2C8,
  Experience = 0x2C9,
  Health = 0x2D4,
  ActionPoints = 0x2D5,
  HealRate = 0x2D7,
  ActionPointsRate = 0x2D8,
  CarryWeight = 0x2DC,
  CritChance = 0x2DD,
  Rads = 0x2E1,
  DamageResist = 0x2E3,
  PoisonResist = 0x2E4,
  FireResist = 0x2E5,
  ElectricResist = 0x2E6,
  FrostResist = 0x2E7,
  RadResistIngestion = 0x2E9,
  RadResistExposure = 0x2EA,
  EnergyResist = 0x2EB,
  RadHealthMax = 0x2EE,
  PowerArmorBattery = 0x35C,
  PerceptionCondition = 0x36C,  // head
  EnduranceCondition = 0x36D,   // torso
  LeftAttackCondition = 0x36E,  // left arm
  RightAttackCondition = 0x36F, // right arm
  LeftMobilityCondition = 0x370,
  RightMobilityCondition = 0x371,
  BrainCondition = 0x372,
  CriticalHitDamageMult = 0x39C,
};
}

constexpr FormId kSpecial[7] = { Av::Strength,     Av::Perception,
                                 Av::Endurance,    Av::Charisma,
                                 Av::Intelligence, Av::Agility,
                                 Av::Luck };
constexpr FormId kLimbConditions[7] = {
  Av::PerceptionCondition,    Av::EnduranceCondition,
  Av::LeftAttackCondition,    Av::RightAttackCondition,
  Av::LeftMobilityCondition,  Av::RightMobilityCondition,
  Av::BrainCondition,
};

// How client reports for an AV are treated (F08 §4.6 policy table).
enum class AvPolicy : uint8_t
{
  ServerOnly,    // client reports always ignored + correction
  DecreaseOnly,  // decreases accepted (owner took damage), increases cropped
  IncreaseOnly,  // Rads: increases accepted (≤ max), decreases ignored
  RegenCropped,  // increases allowed up to the regen rate
};

struct AvEntry
{
  float base = 0.f;
  float permanent = 0.f;
  float temporary = 0.f;
  float damage = 0.f; // ≤ 0 (Rads: ≥ 0, damage is positive by convention)
  float Current() const { return base + permanent + temporary + damage; }
  float Max() const { return base + permanent + temporary; }
};

struct AvReportResult
{
  bool accepted = false;
  bool correction = false; // caller sends ChangeValuesAv back
  float applied = 0.f;     // value now stored
};

class ActorValueStore
{
public:
  float GetBase(FormId av) const;
  float GetCurrent(FormId av) const;
  float GetMax(FormId av) const;
  const AvEntry* Find(FormId av) const;

  void SetBase(FormId av, float v);
  void ModPermanent(FormId av, float delta);
  void ModTemporary(FormId av, float delta);
  // Negative delta damages, positive restores (never above max).
  void Damage(FormId av, float delta);
  void Restore(FormId av, float amount);
  void SetCurrent(FormId av, float v);

  // SPECIAL-derived maxima (vanilla formulas):
  //   Health = 80 + 5*END + (level-1)*(2.5 + END/2)
  //   AP     = 60 + 10*AGI
  //   Carry  = 200 + 10*STR
  void RecomputeDerived(int32_t level);
  // Effective max HP after radiation: -1% per 10 rads
  float GetEffectiveMaxHealth() const;
  bool IsDead() const;
  bool IsLimbCrippled(FormId limbAv) const;

  // Regeneration for one server tick (AP regen, limb regen outside combat).
  void Tick(float dtSec, bool inCombat);

  static AvPolicy GetPolicy(FormId av, bool hostedNpc);
  // `maxDecreasePerSec` bounds hosted-NPC decreases (C1, F08-T15).
  AvReportResult ApplyClientReport(FormId av, float reported, float dtSec,
                                   bool hostedNpc,
                                   bool serverKnowsCause = false);

  const std::map<FormId, AvEntry>& Entries() const { return entries; }

  nlohmann::json ToJson() const;
  static ActorValueStore FromJson(const nlohmann::json& j);

  // Rates (vanilla-ish defaults, tunable by the gamemode)
  float apRegenPerSec = 6.f;      // F08: "6 %/s AP" of max
  float limbRegenPerSec = 1.f;    // condition points per second
  float hostedNpcMaxDecreasePerSec = 400.f;

private:
  AvEntry& Get(FormId av);
  std::map<FormId, AvEntry> entries;
};

// ---------------------------------------------------------------- damage

enum class Difficulty : uint8_t
{
  VeryEasy,
  Easy,
  Normal,
  Hard,
  VeryHard,
  Survival,
};

struct DamageSettings
{
  float physicalDamageFactor = 0.15f; // GMST fPhysicalDamageFactor
  float armorReductionExp = 0.365f;   // GMST fPhysicalArmorDmgReductionExp
  float pvpDamageFactor = 0.15f;      // F76 uses 1.5 for PvP; knob
  float pvpDamageMult = 1.f;
  Difficulty difficulty = Difficulty::Normal;
  float headshotMult = 2.f; // BPTD head damage mult [verify per race]
  bool critsIgnoreResistance = false; // [verify] (wiki claims they do)
};

struct HitInput
{
  // Per-type paper damage; damageTypeId 0 = physical (DR)
  std::vector<DamageTypeValue> paperDamage;
  // For multi-projectile weapons the coefficient uses the total damage
  uint32_t projectiles = 1;
  bool attackerIsPlayer = true;
  bool targetIsPlayer = false;
  bool isPvp = false;
  bool headshot = false;
  bool sneak = false;
  bool melee = false;
  bool critical = false;
  float critMult = 2.f;
  float baseDamageForCrit = 0.f;
  float sneakMultRanged = 2.f;
  float sneakMultMelee = 3.f;
  float armorPenetration = 0.f; // fraction of resistance ignored
  float extraMult = 1.f;        // perks, legendary, power attack
};

struct TargetResistances
{
  // DR (physical) and per-damage-type resistances, armor included
  float damageResist = 0.f;
  std::map<FormId, float> byType; // damage type -> resistance
  std::map<FormId, bool> immune;  // e.g. radiation immunity for robots
};

struct HitOutcome
{
  float total = 0.f;
  std::vector<DamageTypeValue> perType;
};

// coeff = clamp((factor * D / R) ^ exp, 0.01, 0.99); R = 0 -> 0.99
float ResistanceCoefficient(float damage, float resistance, float factor,
                            float exponent);
float DifficultyMult(Difficulty d, bool attackerIsPlayer,
                     bool targetIsPlayer);

class DamageModel
{
public:
  explicit DamageModel(DamageSettings settings);
  HitOutcome Resolve(const HitInput& hit, const TargetResistances& r) const;
  DamageSettings settings;
};

}
