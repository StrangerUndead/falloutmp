#pragma once
// Leveled list resolution (F14) with Fallout 4 semantics:
//  - list chance none (LVLD or the LVLG global), then per-entry chance none
//  - "calculate from all levels <= actor level" vs only the highest
//    eligible level
//  - "use all" vs one random entry; "for each item in count" re-rolls the
//    pick count times
//  - nested lists recurse (depth-limited against cycles)
#include "Fo4Data.h"
#include <random>

namespace fo4 {

class LeveledListResolver
{
public:
  explicit LeveledListResolver(const IFo4DataSource& data);

  // Appends resolved (item, count) pairs; `id` may be an item (returned
  // as-is) or a leveled list.
  void Resolve(FormId id, uint32_t count, int32_t actorLevel,
               std::mt19937& rng, std::vector<ComponentCount>& out,
               int depth = 0) const;

  std::vector<ComponentCount> Resolve(FormId id, uint32_t count,
                                      int32_t actorLevel,
                                      std::mt19937& rng) const;

  static constexpr int kMaxDepth = 16;

private:
  const IFo4DataSource& data;
};

}
