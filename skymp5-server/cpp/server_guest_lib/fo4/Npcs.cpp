#include "Npcs.h"
#include "LeveledLists.h"
#include <algorithm>
#include <cmath>

namespace fo4 {

NpcResolver::NpcResolver(const IFo4DataSource& data_)
  : data(data_)
{
}

const NpcData* NpcResolver::Pick(FormId id, int32_t playerLevel,
                                 std::mt19937& rng, Memo& memo) const
{
  if (!id) {
    return nullptr;
  }
  if (auto it = memo.find(id); it != memo.end()) {
    return it->second; // one choice per list per spawn
  }
  const NpcData* picked = data.FindNpc(id);
  if (!picked && data.FindLeveledList(id)) {
    LeveledListResolver lists(data);
    for (auto& c : lists.Resolve(id, 1, playerLevel, rng)) {
      if (auto n = data.FindNpc(c.componentId)) {
        picked = n;
        break;
      }
    }
  }
  memo[id] = picked;
  return picked;
}

const NpcData* NpcResolver::AspectSource(const NpcData* npc, uint8_t aspect,
                                         int32_t playerLevel,
                                         std::mt19937& rng, Memo& memo) const
{
  const NpcData* cur = npc;
  for (int depth = 0; cur && depth < kMaxDepth; ++depth) {
    if (!cur->UsesTemplate(aspect)) {
      return cur;
    }
    FormId next = cur->templateActors[aspect] ? cur->templateActors[aspect]
                                              : cur->defaultTemplate;
    const NpcData* n = Pick(next, playerLevel, rng, memo);
    if (!n || n == cur) {
      return cur; // broken chain: use what we have
    }
    cur = n;
  }
  return cur;
}

std::optional<ResolvedNpc> NpcResolver::Resolve(FormId baseOrLeveled,
                                                int32_t playerLevel,
                                                std::mt19937& rng) const
{
  Memo memo;
  const NpcData* base = Pick(baseOrLeveled, playerLevel, rng, memo);
  if (!base) {
    return std::nullopt;
  }
  ResolvedNpc r;
  r.baseId = base->id;

  auto* stats = AspectSource(base, NpcData::kStats, playerLevel, rng, memo);
  if (stats->pcLevelMult) {
    int32_t lvl = static_cast<int32_t>(
      std::lround(static_cast<float>(std::max(1, playerLevel)) *
                  stats->levelMult));
    if (stats->calcMinLevel > 0) {
      lvl = std::max<int32_t>(lvl, stats->calcMinLevel);
    }
    if (stats->calcMaxLevel > 0) {
      lvl = std::min<int32_t>(lvl, stats->calcMaxLevel);
    }
    r.level = std::max(1, lvl);
  } else {
    r.level = std::max<int32_t>(1, stats->level);
  }
  r.calculatedHealth = stats->calculatedHealth;

  r.factions =
    AspectSource(base, NpcData::kFactions, playerLevel, rng, memo)->factions;
  r.race = AspectSource(base, NpcData::kTraits, playerLevel, rng, memo)->race;
  auto* baseData =
    AspectSource(base, NpcData::kBaseData, playerLevel, rng, memo);
  r.flags = baseData->flags;
  r.deathItem = baseData->deathItem;

  auto* inv =
    AspectSource(base, NpcData::kInventory, playerLevel, rng, memo);
  LeveledListResolver lists(data);
  std::vector<ComponentCount> items;
  for (auto& c : inv->items) {
    lists.Resolve(c.componentId, c.count, r.level, rng, items);
  }
  if (auto outfit = data.FindOutfit(inv->defaultOutfit)) {
    for (auto id : outfit->items) {
      std::vector<ComponentCount> worn;
      lists.Resolve(id, 1, r.level, rng, worn);
      for (auto& w : worn) {
        r.outfit.push_back(w.componentId);
        items.push_back(w);
      }
    }
  }
  // Merge duplicate stacks
  for (auto& c : items) {
    auto it = std::find_if(r.inventory.begin(), r.inventory.end(),
                           [&](auto& m) { return m.componentId == c.componentId; });
    if (it == r.inventory.end()) {
      r.inventory.push_back(c);
    } else {
      it->count += c.count;
    }
  }
  return r;
}

}
