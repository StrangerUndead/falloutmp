#pragma once
// Progression (F19): XP, levels, SPECIAL, perks, bobbleheads, magazines.
// Every change is a server decision; client choices are requests.
#include "ActorValues.h"
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <set>
#include <string>

namespace fo4 {

// XP needed to reach level n (vanilla curve, F19 §2):
//   XP_n = 37.5 n^2 + 87.5 n - 124  (level 2 at 201 XP, to-next 75n + 125)
uint32_t XpForLevel(int32_t level);
int32_t LevelForXp(uint64_t xp);

struct PerkRankData
{
  FormId perkId = 0; // each rank is its own PERK form in Fallout 4
  int32_t levelRequired = 1;
};

struct PerkChartEntry
{
  std::string name;
  FormId special = 0; // AV of the SPECIAL that gates the perk
  int32_t specialRequired = 1;
  std::vector<PerkRankData> ranks;
};

enum class ProgressionError : uint8_t
{
  None = 0,
  NoPerkPoints,
  UnknownPerk,
  LevelTooLow,
  SpecialTooLow,
  MaxRank,
  RankChain,
  AlreadyCreated,
  BadSpecialAllocation,
  AlreadyCollected,
};
const char* ProgressionErrorToString(ProgressionError e) noexcept;

struct ProgressionSettings
{
  float xpMult = 1.f;
  float intelligenceXpBonusPerPoint = 0.03f; // +3% per INT point
  int32_t maxLevel = 65535;
  int32_t creationPoints = 21; // spend on top of 1 in each SPECIAL
  int32_t specialMax = 10;
  int32_t specialMaxWithBobblehead = 11;
  bool perkPointsPerLevel = true;
};

struct LevelUpEvent
{
  int32_t oldLevel = 1;
  int32_t newLevel = 1;
  int32_t perkPointsGained = 0;
};

class Progression
{
public:
  explicit Progression(ProgressionSettings settings = {});

  // One-time SPECIAL creation: values 1..10 each, sum = 7 + creationPoints.
  ProgressionError CreateCharacter(const std::array<int32_t, 7>& special,
                                   ActorValueStore& avs);

  // Awards XP after multipliers. `direct` skips INT and perk multipliers
  // (Game.RewardPlayerXP(amount, true)).
  LevelUpEvent AwardXp(uint32_t amount, bool direct, ActorValueStore& avs,
                       float perkMult = 1.f);

  // Buys the next rank of a perk from the chart.
  ProgressionError BuyPerk(const std::string& chartKey,
                           const std::map<std::string, PerkChartEntry>& chart,
                           ActorValueStore& avs);
  // SPECIAL point at level-up instead of a perk (vanilla allows it)
  ProgressionError BuySpecial(FormId specialAv, ActorValueStore& avs);

  ProgressionError CollectBobblehead(FormId bobbleheadId, FormId specialAv,
                                     ActorValueStore& avs);
  ProgressionError CollectMagazine(FormId magazineId);

  int32_t GetPerkRank(FormId perkIdAnyRank) const;
  int32_t GetChartRank(const std::string& chartKey) const;
  bool HasPerk(FormId perkId) const { return ownedPerks.count(perkId) > 0; }

  int32_t level = 1;
  uint64_t xp = 0;
  int32_t perkPoints = 0;
  bool created = false;
  std::set<FormId> ownedPerks;               // each rank form owned
  std::map<std::string, int32_t> chartRanks; // chartKey -> rank
  std::set<FormId> bobbleheads;
  std::set<FormId> magazines;

  nlohmann::json ToJson() const;
  static Progression FromJson(const nlohmann::json& j,
                              ProgressionSettings settings = {});

  ProgressionSettings settings;
};

}
