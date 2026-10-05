#include "LeveledLists.h"
#include <algorithm>

namespace fo4 {

LeveledListResolver::LeveledListResolver(const IFo4DataSource& data_)
  : data(data_)
{
}

std::vector<ComponentCount> LeveledListResolver::Resolve(
  FormId id, uint32_t count, int32_t actorLevel, std::mt19937& rng) const
{
  std::vector<ComponentCount> out;
  Resolve(id, count, actorLevel, rng, out, 0);
  // Merge duplicates
  std::vector<ComponentCount> merged;
  for (auto& c : out) {
    auto it = std::find_if(merged.begin(), merged.end(), [&](auto& m) {
      return m.componentId == c.componentId;
    });
    if (it == merged.end()) {
      merged.push_back(c);
    } else {
      it->count += c.count;
    }
  }
  return merged;
}

void LeveledListResolver::Resolve(FormId id, uint32_t count,
                                  int32_t actorLevel, std::mt19937& rng,
                                  std::vector<ComponentCount>& out,
                                  int depth) const
{
  if (count == 0 || id == 0) {
    return;
  }
  auto list = data.FindLeveledList(id);
  if (!list) {
    out.push_back({ id, count });
    return;
  }
  if (depth >= kMaxDepth) {
    return;
  }
  std::uniform_real_distribution<float> pct(0.f, 100.f);

  // Eligible entries
  std::vector<const LeveledEntryData*> eligible;
  int32_t best = -1;
  for (auto& e : list->entries) {
    if (e.level <= actorLevel) {
      best = std::max(best, e.level);
    }
  }
  for (auto& e : list->entries) {
    if (e.level > actorLevel) {
      continue;
    }
    if (!list->calcFromAllLevels && e.level != best) {
      continue;
    }
    eligible.push_back(&e);
  }
  if (eligible.empty()) {
    return;
  }

  auto emit = [&](const LeveledEntryData& e, uint32_t times) {
    if (e.chanceNone > 0 && pct(rng) < e.chanceNone) {
      return;
    }
    Resolve(e.refId, e.count * times, actorLevel, rng, out, depth + 1);
  };

  uint32_t rolls = list->calcForEachItem ? count : 1;
  uint32_t perRoll = list->calcForEachItem ? 1 : count;
  for (uint32_t r = 0; r < rolls; ++r) {
    if (list->chanceNone > 0 && pct(rng) < list->chanceNone) {
      continue;
    }
    if (list->useAll) {
      for (auto e : eligible) {
        emit(*e, perRoll);
      }
    } else {
      std::uniform_int_distribution<size_t> pick(0, eligible.size() - 1);
      emit(*eligible[pick(rng)], perRoll);
    }
  }
}

}
