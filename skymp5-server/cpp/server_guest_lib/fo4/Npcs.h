#pragma once
// NPC resolution from plugin data (F13/F14).
//
// A placed NPC's base is an NPC_ or a leveled NPC (LVLN). Each NPC_ may take
// individual aspects (stats, factions, inventory, traits, base data) from a
// template actor: TPTA names one per aspect, TPLT is the fallback, and either
// can itself be a leveled list. The server resolves the chain once per spawn
// so every client sees the same NPC, level and loadout.
#include "Fo4Data.h"
#include <map>
#include <optional>
#include <random>

namespace fo4 {

struct ResolvedNpc
{
  FormId baseId = 0; // the NPC_ after leveled-list selection
  int32_t level = 1;
  uint32_t flags = 0; // ACBS flags from the base-data source
  FormId race = 0;
  uint16_t calculatedHealth = 0;
  FormId deathItem = 0;
  std::vector<NpcData::Faction> factions;
  std::vector<ComponentCount> inventory; // leveled lists resolved
  std::vector<FormId> outfit;            // worn apparel, also in inventory
};

class NpcResolver
{
public:
  explicit NpcResolver(const IFo4DataSource& data);

  // playerLevel drives PC-level-mult NPCs and leveled lists.
  std::optional<ResolvedNpc> Resolve(FormId baseOrLeveled,
                                     int32_t playerLevel,
                                     std::mt19937& rng) const;

  static constexpr int kMaxDepth = 8;

private:
  using Memo = std::map<FormId, const NpcData*>;
  const NpcData* Pick(FormId id, int32_t playerLevel, std::mt19937& rng,
                      Memo& memo) const;
  const NpcData* AspectSource(const NpcData* npc, uint8_t aspect,
                              int32_t playerLevel, std::mt19937& rng,
                              Memo& memo) const;
  const IFo4DataSource& data;
};

}
