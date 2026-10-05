#include "Fo4TestData.h"
#include "fo4/ActorValues.h"
#include "fo4/Effects.h"
#include "fo4/Progression.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using namespace fo4;
using namespace fo4test;

namespace {
ActorValueStore MakeActor(std::array<int, 7> special = { 1, 1, 1, 1, 1, 1, 1 },
                          int level = 1)
{
  ActorValueStore s;
  for (size_t i = 0; i < 7; ++i) {
    s.SetBase(kSpecial[i], static_cast<float>(special[i]));
  }
  s.RecomputeDerived(level);
  return s;
}
}

TEST_CASE("Derived actor values follow the vanilla formulas", "[fo4][F08]")
{
  auto a = MakeActor({ 5, 1, 4, 1, 1, 6, 1 }, 10);
  // 80 + 5*4 + 9*(2.5 + 2) = 140.5
  REQUIRE(a.GetMax(Av::Health) == Catch::Approx(140.5f));
  REQUIRE(a.GetMax(Av::ActionPoints) == 120.f);
  REQUIRE(a.GetMax(Av::CarryWeight) == 250.f);
  REQUIRE(a.GetMax(Av::PerceptionCondition) == 100.f);

  // Rads: -1% max HP per 10 rads; 1000 rads = death
  a.Damage(Av::Rads, -250.f);
  REQUIRE(a.GetCurrent(Av::Rads) == 250.f);
  REQUIRE(a.GetEffectiveMaxHealth() == Catch::Approx(140.5f * 0.75f));
  a.Tick(1.f, false);
  REQUIRE(a.GetCurrent(Av::Health) <= a.GetEffectiveMaxHealth());
  a.Damage(Av::Rads, -5000.f);
  REQUIRE(a.GetCurrent(Av::Rads) == 1000.f);
  REQUIRE(a.IsDead());
}

TEST_CASE("Damage never goes below zero or above max", "[fo4][F08]")
{
  auto a = MakeActor();
  float max = a.GetMax(Av::Health);
  a.Damage(Av::Health, -1e6f);
  REQUIRE(a.GetCurrent(Av::Health) == 0.f);
  REQUIRE(a.IsDead());
  a.Restore(Av::Health, 1e6f);
  REQUIRE(a.GetCurrent(Av::Health) == max);
  a.ModTemporary(Av::Health, 20.f);
  REQUIRE(a.GetMax(Av::Health) == max + 20.f);
}

TEST_CASE("Client actor-value reports are cropped per policy", "[fo4][F08]")
{
  auto a = MakeActor({ 1, 1, 1, 1, 1, 5, 1 });
  // Owner took damage: accepted
  auto r = a.ApplyClientReport(Av::Health, 50.f, 0.1f, false);
  REQUIRE(r.accepted);
  REQUIRE_FALSE(r.correction);
  REQUIRE(a.GetCurrent(Av::Health) == 50.f);
  // Owner claims a heal: rejected with correction
  r = a.ApplyClientReport(Av::Health, 80.f, 0.1f, false);
  REQUIRE(r.correction);
  REQUIRE(a.GetCurrent(Av::Health) == 50.f);
  // SPECIAL is server-only
  r = a.ApplyClientReport(Av::Strength, 10.f, 0.1f, false);
  REQUIRE(r.correction);
  REQUIRE(a.GetCurrent(Av::Strength) == 1.f);
  // AP regen is cropped to 6%/s of max (110 AP -> 6.6/s)
  a.SetCurrent(Av::ActionPoints, 10.f);
  r = a.ApplyClientReport(Av::ActionPoints, 110.f, 1.f, false);
  REQUIRE(r.correction);
  REQUIRE(a.GetCurrent(Av::ActionPoints) == Catch::Approx(17.1f));
  // Rads: increases accepted, decreases ignored
  REQUIRE(a.ApplyClientReport(Av::Rads, 30.f, 0.1f, false).accepted);
  REQUIRE(a.ApplyClientReport(Av::Rads, 0.f, 0.1f, false).correction);
  REQUIRE(a.GetCurrent(Av::Rads) == 30.f);
  // NaN never gets in
  REQUIRE(a.ApplyClientReport(Av::Health, NAN, 0.1f, false).correction);
}

TEST_CASE("Hosts cannot kill hosted NPCs through AV reports (C1)",
          "[fo4][F08]")
{
  auto npc = MakeActor();
  float hp = npc.GetCurrent(Av::Health);
  // No server-known cause: rejected
  auto r = npc.ApplyClientReport(Av::Health, 0.f, 1.f, true, false);
  REQUIRE(r.correction);
  REQUIRE(npc.GetCurrent(Av::Health) == hp);
  // A fall the server saw: accepted but clamped to >= 1 HP
  r = npc.ApplyClientReport(Av::Health, 0.f, 1.f, true, true);
  REQUIRE(npc.GetCurrent(Av::Health) == 1.f);
  REQUIRE(r.correction);
  // Rads clamped at 999
  r = npc.ApplyClientReport(Av::Rads, 1000.f, 10.f, true, true);
  REQUIRE(npc.GetCurrent(Av::Rads) == 999.f);
  REQUIRE_FALSE(npc.IsDead());
}

TEST_CASE("Actor-value JSON round trip", "[fo4][F08]")
{
  auto a = MakeActor({ 3, 3, 3, 3, 3, 3, 3 }, 7);
  a.Damage(Av::Health, -12.f);
  a.ModPermanent(Av::Luck, 1.f);
  a.Damage(Av::Rads, -40.f);
  auto b = ActorValueStore::FromJson(a.ToJson());
  REQUIRE(b.GetCurrent(Av::Health) == a.GetCurrent(Av::Health));
  REQUIRE(b.GetCurrent(Av::Luck) == 4.f);
  REQUIRE(b.GetCurrent(Av::Rads) == 40.f);
}

TEST_CASE("Damage curve matches the wiki vectors", "[fo4][F11]")
{
  auto final = [](float d, float r) {
    return d * ResistanceCoefficient(d, r, 0.15f, 0.365f);
  };
  REQUIRE(final(25, 10) == Catch::Approx(17.5f).margin(0.1));
  REQUIRE(final(25, 25) == Catch::Approx(12.5f).margin(0.1));
  REQUIRE(final(50, 50) == Catch::Approx(25.0f).margin(0.1));
  REQUIRE(final(50, 10) == Catch::Approx(45.0f).margin(0.2));
  REQUIRE(final(100, 250) == Catch::Approx(35.8f).margin(0.2));
  REQUIRE(final(250, 100) == Catch::Approx(174.8f).margin(0.3));
  REQUIRE(final(1000, 1000) == Catch::Approx(500.3f).margin(0.6));
  REQUIRE(ResistanceCoefficient(10, 0, 0.15f, 0.365f) == 0.99f);
  REQUIRE(ResistanceCoefficient(1, 1e6f, 0.15f, 0.365f) == 0.01f);
  REQUIRE(ResistanceCoefficient(0, 10, 0.15f, 0.365f) == 0.f);
}

TEST_CASE("DamageModel applies types, immunity, headshot, sneak, "
          "difficulty and PvP",
          "[fo4][F11]")
{
  DamageSettings s;
  DamageModel m(s);
  constexpr FormId kEnergy = 0x60A81, kRad = 0x60A85;
  TargetResistances r;
  r.damageResist = 10;
  r.byType[kEnergy] = 25;
  r.immune[kRad] = true;

  HitInput hit;
  hit.paperDamage = { { 0, 25 }, { kEnergy, 25 }, { kRad, 50 } };
  auto out = m.Resolve(hit, r);
  REQUIRE(out.perType.size() == 2); // radiation ignored (immune)
  REQUIRE(out.total == Catch::Approx(17.5f + 12.5f).margin(0.2));

  hit.paperDamage = { { 0, 25 } };
  hit.headshot = true;
  REQUIRE(m.Resolve(hit, r).total == Catch::Approx(35.f).margin(0.3));
  hit.headshot = false;
  hit.sneak = true;
  REQUIRE(m.Resolve(hit, r).total == Catch::Approx(35.f).margin(0.3));
  hit.melee = true;
  REQUIRE(m.Resolve(hit, r).total == Catch::Approx(52.5f).margin(0.4));
  hit.sneak = false;
  hit.melee = false;

  // Difficulty: Very Hard, player deals half; enemies deal double
  m.settings.difficulty = Difficulty::VeryHard;
  REQUIRE(m.Resolve(hit, r).total == Catch::Approx(8.75f).margin(0.1));
  hit.attackerIsPlayer = false;
  hit.targetIsPlayer = true;
  REQUIRE(m.Resolve(hit, r).total == Catch::Approx(35.f).margin(0.3));
  // PvP ignores difficulty and uses its own factor/multiplier
  hit.attackerIsPlayer = true;
  hit.isPvp = true;
  m.settings.pvpDamageMult = 0.5f;
  REQUIRE(m.Resolve(hit, r).total == Catch::Approx(8.75f).margin(0.1));

  // Shotgun: coefficient from total pellet damage, per-pellet result
  m.settings = DamageSettings{};
  HitInput pellet;
  pellet.paperDamage = { { 0, 10 } };
  pellet.projectiles = 8;
  float single = 10 * ResistanceCoefficient(10, 10, 0.15f, 0.365f);
  float total = m.Resolve(pellet, r).total;
  REQUIRE(total > single); // better penetration than a lone 10-dmg pellet

  // Armor penetration lowers effective resistance
  HitInput pen;
  pen.paperDamage = { { 0, 25 } };
  pen.armorPenetration = 1.f;
  REQUIRE(m.Resolve(pen, r).total == Catch::Approx(25 * 0.99f));
}

TEST_CASE("XP curve and level-ups grant perk points", "[fo4][F19]")
{
  REQUIRE(XpForLevel(1) == 0);
  REQUIRE(XpForLevel(2) == 201);
  REQUIRE(XpForLevel(3) == 476);
  REQUIRE(XpForLevel(3) - XpForLevel(2) == 275); // 75*2 + 125
  REQUIRE(LevelForXp(200) == 1);
  REQUIRE(LevelForXp(201) == 2);
  REQUIRE(LevelForXp(10000) > 10);

  auto avs = MakeActor({ 1, 1, 1, 1, 10, 1, 1 });
  Progression p;
  // INT 10 gives +30% XP
  auto ev = p.AwardXp(400, false, avs);
  REQUIRE(p.xp == 520);
  REQUIRE(ev.oldLevel == 1);
  REQUIRE(ev.newLevel == 3);
  REQUIRE(ev.perkPointsGained == 2);
  REQUIRE(p.perkPoints == 2);
  // Direct XP ignores INT
  p.AwardXp(100, true, avs);
  REQUIRE(p.xp == 620);
  // Max HP grew with the level, damage kept
  REQUIRE(avs.GetMax(Av::Health) == Catch::Approx(80 + 5 + 2 * 3.f));
}

TEST_CASE("Character creation and perk purchase rules", "[fo4][F19]")
{
  ActorValueStore avs;
  Progression p;
  REQUIRE(p.CreateCharacter({ 10, 1, 1, 1, 1, 1, 1 }, avs) ==
          ProgressionError::BadSpecialAllocation); // sum 16, need 28
  REQUIRE(p.CreateCharacter({ 11, 1, 1, 1, 1, 1, 12 }, avs) ==
          ProgressionError::BadSpecialAllocation);
  REQUIRE(p.CreateCharacter({ 4, 4, 4, 4, 4, 4, 4 }, avs) ==
          ProgressionError::None);
  REQUIRE(p.CreateCharacter({ 4, 4, 4, 4, 4, 4, 4 }, avs) ==
          ProgressionError::AlreadyCreated);
  REQUIRE(avs.GetCurrent(Av::Health) == avs.GetMax(Av::Health));

  std::map<std::string, PerkChartEntry> chart;
  chart["GunNut"] = { "Gun Nut", Av::Intelligence, 3,
                      { { 0x4D9B1, 1 }, { 0x4D9B2, 13 }, { 0x4D9B3, 25 } } };
  chart["Rifleman"] = { "Rifleman", Av::Perception, 6, { { 0x4A0D1, 1 } } };

  REQUIRE(p.BuyPerk("GunNut", chart, avs) == ProgressionError::NoPerkPoints);
  p.perkPoints = 3;
  REQUIRE(p.BuyPerk("Nope", chart, avs) == ProgressionError::UnknownPerk);
  REQUIRE(p.BuyPerk("Rifleman", chart, avs) ==
          ProgressionError::SpecialTooLow);
  REQUIRE(p.BuyPerk("GunNut", chart, avs) == ProgressionError::None);
  REQUIRE(p.HasPerk(0x4D9B1));
  REQUIRE(p.BuyPerk("GunNut", chart, avs) == ProgressionError::LevelTooLow);
  REQUIRE(p.perkPoints == 2);

  // SPECIAL point instead of a perk, capped at 10
  REQUIRE(p.BuySpecial(Av::Perception, avs) == ProgressionError::None);
  REQUIRE(avs.GetBase(Av::Perception) == 5.f);
  avs.SetBase(Av::Perception, 10.f);
  REQUIRE(p.BuySpecial(Av::Perception, avs) == ProgressionError::MaxRank);

  // Bobbleheads: once each, may raise to 11
  REQUIRE(p.CollectBobblehead(0xB0B1, Av::Perception, avs) ==
          ProgressionError::None);
  REQUIRE(avs.GetCurrent(Av::Perception) == 11.f);
  REQUIRE(p.CollectBobblehead(0xB0B1, Av::Perception, avs) ==
          ProgressionError::AlreadyCollected);
  REQUIRE(p.CollectMagazine(0xA0A1) == ProgressionError::None);
  REQUIRE(p.CollectMagazine(0xA0A1) == ProgressionError::AlreadyCollected);

  auto restored = Progression::FromJson(p.ToJson());
  REQUIRE(restored.HasPerk(0x4D9B1));
  REQUIRE(restored.GetChartRank("GunNut") == 1);
  REQUIRE(restored.perkPoints == p.perkPoints);
  REQUIRE(restored.bobbleheads.count(0xB0B1));
}

TEST_CASE("Consumables: stimpak, chems, RadAway, addiction, exposure",
          "[fo4][F20]")
{
  InMemoryFo4DataSource data;
  Build(data);
  constexpr FormId kRestoreHealth = 0xE1, kBuffStr = 0xE2, kRemoveRads = 0xE3;
  constexpr FormId kBuffout = 0xC1, kRadAway = 0xC2, kAddiction = 0xAD;
  // Stimpak: 40 HP over 10 s (from Fo4TestData; add its effect)
  ConsumableData stim = *data.FindConsumable(kStimpak);
  stim.effects = { { kRestoreHealth, 4.f, 10 } };
  data.AddConsumable(stim);
  ConsumableData buffout;
  buffout.id = kBuffout;
  buffout.effects = { { kBuffStr, 3.f, 60 } };
  buffout.addictionId = kAddiction;
  buffout.addictionChance = 1.f;
  data.AddConsumable(buffout);
  ConsumableData radaway;
  radaway.id = kRadAway;
  radaway.effects = { { kRemoveRads, 10.f, 30 } };
  data.AddConsumable(radaway);

  EffectSystem fx(data);
  fx.DefineEffect({ kRestoreHealth, EffectKind::RestoreOverTime, Av::Health });
  fx.DefineEffect({ kBuffStr, EffectKind::ValueModifier, Av::Strength });
  fx.DefineEffect({ kRemoveRads, EffectKind::RemoveRads, Av::Rads });

  auto avs = MakeActor({ 1, 1, 10, 1, 1, 1, 1 });
  avs.Damage(Av::Health, -100.f);
  float hp = avs.GetCurrent(Av::Health);
  Fo4Inventory inv;
  std::mt19937 rng(7);

  REQUIRE(fx.UseItem(kStimpak, inv, avs, rng).error ==
          UseItemError::NotInInventory);
  REQUIRE(fx.UseItem(kWrench, inv, avs, rng).error ==
          UseItemError::NotConsumable);
  inv.AddSimple(kStimpak, 1);
  REQUIRE(fx.UseItem(kStimpak, inv, avs, rng).Ok());
  REQUIRE(inv.CountBase(kStimpak) == 0);
  fx.Tick(5.f, avs);
  REQUIRE(avs.GetCurrent(Av::Health) == Catch::Approx(hp + 20.f));
  fx.Tick(100.f, avs);
  REQUIRE(avs.GetCurrent(Av::Health) == Catch::Approx(hp + 40.f));
  REQUIRE(fx.Active().empty());

  inv.AddSimple(kBuffout, 1);
  auto r = fx.UseItem(kBuffout, inv, avs, rng);
  REQUIRE(r.becameAddicted);
  REQUIRE(avs.GetCurrent(Av::Strength) == 4.f);
  auto saved = fx.ToJson();
  fx.Tick(61.f, avs);
  REQUIRE(avs.GetCurrent(Av::Strength) == 1.f); // buff ended
  REQUIRE(fx.Addictions().count(kAddiction));

  // Persistence keeps active effects and addictions
  EffectSystem restored(data);
  restored.DefineEffect({ kBuffStr, EffectKind::ValueModifier, Av::Strength });
  restored.LoadJson(saved, avs);
  REQUIRE(restored.Active().size() == 1);
  REQUIRE(restored.Addictions().count(kAddiction));

  // Exposure with 50% resistance, then RadAway removes it
  avs.SetBase(Av::RadResistExposure, 50.f);
  fx.ApplyRadiationExposure(10.f, 10.f, avs);
  REQUIRE(avs.GetCurrent(Av::Rads) == Catch::Approx(50.f));
  inv.AddSimple(kRadAway, 1);
  REQUIRE(fx.UseItem(kRadAway, inv, avs, rng).Ok());
  fx.Tick(30.f, avs);
  REQUIRE(avs.GetCurrent(Av::Rads) == 0.f);

  avs.Damage(Av::Health, -1e6f);
  inv.AddSimple(kStimpak, 1);
  REQUIRE(fx.UseItem(kStimpak, inv, avs, rng).error == UseItemError::Dead);
  REQUIRE(inv.CountBase(kStimpak) == 1);
}
