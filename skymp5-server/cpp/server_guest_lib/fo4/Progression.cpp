#include "Progression.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace fo4 {

const char* ProgressionErrorToString(ProgressionError e) noexcept
{
  switch (e) {
    case ProgressionError::None:
      return "None";
    case ProgressionError::NoPerkPoints:
      return "NoPerkPoints";
    case ProgressionError::UnknownPerk:
      return "UnknownPerk";
    case ProgressionError::LevelTooLow:
      return "LevelTooLow";
    case ProgressionError::SpecialTooLow:
      return "SpecialTooLow";
    case ProgressionError::MaxRank:
      return "MaxRank";
    case ProgressionError::RankChain:
      return "RankChain";
    case ProgressionError::AlreadyCreated:
      return "AlreadyCreated";
    case ProgressionError::BadSpecialAllocation:
      return "BadSpecialAllocation";
    case ProgressionError::AlreadyCollected:
      return "AlreadyCollected";
  }
  return "Unknown";
}

uint32_t XpForLevel(int32_t level)
{
  if (level <= 1) {
    return 0;
  }
  double n = level; // XP_n is the threshold to reach level n
  double v = 37.5 * n * n + 87.5 * n - 124.0;
  return static_cast<uint32_t>(std::max(0.0, std::ceil(v)));
}

int32_t LevelForXp(uint64_t xp)
{
  int32_t lvl = 1;
  while (lvl < 65535 && xp >= XpForLevel(lvl + 1)) {
    ++lvl;
  }
  return lvl;
}

Progression::Progression(ProgressionSettings s)
  : settings(s)
{
}

ProgressionError Progression::CreateCharacter(
  const std::array<int32_t, 7>& special, ActorValueStore& avs)
{
  if (created) {
    return ProgressionError::AlreadyCreated;
  }
  int32_t sum = 0;
  for (auto v : special) {
    if (v < 1 || v > settings.specialMax) {
      return ProgressionError::BadSpecialAllocation;
    }
    sum += v;
  }
  if (sum != 7 + settings.creationPoints) {
    return ProgressionError::BadSpecialAllocation;
  }
  for (size_t i = 0; i < 7; ++i) {
    avs.SetBase(kSpecial[i], static_cast<float>(special[i]));
  }
  created = true;
  avs.RecomputeDerived(level);
  avs.SetCurrent(Av::Health, avs.GetMax(Av::Health));
  avs.SetCurrent(Av::ActionPoints, avs.GetMax(Av::ActionPoints));
  return ProgressionError::None;
}

LevelUpEvent Progression::AwardXp(uint32_t amount, bool direct,
                                  ActorValueStore& avs, float perkMult)
{
  LevelUpEvent ev;
  ev.oldLevel = level;
  double mult = settings.xpMult;
  if (!direct) {
    mult *= 1.0 + settings.intelligenceXpBonusPerPoint *
        avs.GetCurrent(Av::Intelligence);
    mult *= perkMult;
  }
  xp += static_cast<uint64_t>(std::llround(amount * mult));
  int32_t newLevel = std::min(LevelForXp(xp), settings.maxLevel);
  if (newLevel > level) {
    int32_t gained = newLevel - level;
    if (settings.perkPointsPerLevel) {
      perkPoints += gained;
      ev.perkPointsGained = gained;
    }
    level = newLevel;
    float hpDamage = avs.GetCurrent(Av::Health) - avs.GetMax(Av::Health);
    avs.RecomputeDerived(level);
    avs.SetCurrent(Av::Health, avs.GetMax(Av::Health) + hpDamage);
  }
  avs.SetBase(Av::Experience, static_cast<float>(xp));
  ev.newLevel = level;
  return ev;
}

ProgressionError Progression::BuyPerk(
  const std::string& key, const std::map<std::string, PerkChartEntry>& chart,
  ActorValueStore& avs)
{
  auto it = chart.find(key);
  if (it == chart.end() || it->second.ranks.empty()) {
    return ProgressionError::UnknownPerk;
  }
  if (perkPoints <= 0) {
    return ProgressionError::NoPerkPoints;
  }
  auto& entry = it->second;
  int32_t rank = GetChartRank(key);
  if (rank >= static_cast<int32_t>(entry.ranks.size())) {
    return ProgressionError::MaxRank;
  }
  if (avs.GetCurrent(entry.special) < entry.specialRequired) {
    return ProgressionError::SpecialTooLow;
  }
  auto& next = entry.ranks[rank];
  if (level < next.levelRequired) {
    return ProgressionError::LevelTooLow;
  }
  // Each previous rank must be owned (rank chain)
  for (int32_t i = 0; i < rank; ++i) {
    if (!ownedPerks.count(entry.ranks[i].perkId)) {
      return ProgressionError::RankChain;
    }
  }
  ownedPerks.insert(next.perkId);
  chartRanks[key] = rank + 1;
  --perkPoints;
  return ProgressionError::None;
}

ProgressionError Progression::BuySpecial(FormId av, ActorValueStore& avs)
{
  if (perkPoints <= 0) {
    return ProgressionError::NoPerkPoints;
  }
  if (std::find(std::begin(kSpecial), std::end(kSpecial), av) ==
      std::end(kSpecial)) {
    return ProgressionError::UnknownPerk;
  }
  if (avs.GetBase(av) >= settings.specialMax) {
    return ProgressionError::MaxRank;
  }
  avs.SetBase(av, avs.GetBase(av) + 1.f);
  --perkPoints;
  avs.RecomputeDerived(level);
  return ProgressionError::None;
}

ProgressionError Progression::CollectBobblehead(FormId id, FormId specialAv,
                                                ActorValueStore& avs)
{
  if (!bobbleheads.insert(id).second) {
    return ProgressionError::AlreadyCollected;
  }
  if (specialAv) {
    // Bobbleheads may exceed 10, up to 11
    if (avs.GetCurrent(specialAv) < settings.specialMaxWithBobblehead) {
      avs.ModPermanent(specialAv, 1.f);
      avs.RecomputeDerived(level);
    }
  }
  return ProgressionError::None;
}

ProgressionError Progression::CollectMagazine(FormId id)
{
  if (!magazines.insert(id).second) {
    return ProgressionError::AlreadyCollected;
  }
  return ProgressionError::None;
}

int32_t Progression::GetPerkRank(FormId perkId) const
{
  return ownedPerks.count(perkId) ? 1 : 0;
}

int32_t Progression::GetChartRank(const std::string& key) const
{
  auto it = chartRanks.find(key);
  return it == chartRanks.end() ? 0 : it->second;
}

nlohmann::json Progression::ToJson() const
{
  return { { "level", level },
           { "xp", xp },
           { "perkPoints", perkPoints },
           { "created", created },
           { "perks", ownedPerks },
           { "chartRanks", chartRanks },
           { "bobbleheads", bobbleheads },
           { "magazines", magazines } };
}

Progression Progression::FromJson(const nlohmann::json& j,
                                  ProgressionSettings settings)
{
  Progression p(settings);
  if (!j.is_object()) {
    return p;
  }
  p.level = j.value("level", 1);
  p.xp = j.value("xp", uint64_t(0));
  p.perkPoints = j.value("perkPoints", 0);
  p.created = j.value("created", false);
  if (j.contains("perks"))
    p.ownedPerks = j["perks"].get<std::set<FormId>>();
  if (j.contains("chartRanks"))
    p.chartRanks = j["chartRanks"].get<std::map<std::string, int32_t>>();
  if (j.contains("bobbleheads"))
    p.bobbleheads = j["bobbleheads"].get<std::set<FormId>>();
  if (j.contains("magazines"))
    p.magazines = j["magazines"].get<std::set<FormId>>();
  return p;
}

}
