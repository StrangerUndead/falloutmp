#include "NpcSpawnFilter.h"
#include "libespm/Convert.h"
#include "libespm/fo4/Fo4Records.h"

namespace fo4 {

NpcSpawnFilter::NpcSpawnFilter(const espm::CombineBrowser& br_,
                               std::vector<std::string> blocked)
  : br(br_)
  , blockedRaces(blocked.begin(), blocked.end())
{
}

bool NpcSpawnFilter::IsAllowed(uint32_t baseId)
{
  if (blockedRaces.empty()) {
    return true;
  }
  return !IsBlocked(baseId, 0);
}

bool NpcSpawnFilter::IsBlockedRace(uint32_t raceId)
{
  if (auto it = raceMemo.find(raceId); it != raceMemo.end()) {
    return it->second;
  }
  auto lr = br.LookupById(raceId);
  bool blocked = lr.rec && blockedRaces.count(lr.rec->GetEditorId(cache));
  raceMemo[raceId] = blocked;
  return blocked;
}

bool NpcSpawnFilter::IsBlocked(uint32_t id, int depth)
{
  if (!id || depth > 8) {
    return false;
  }
  if (auto it = memo.find(id); it != memo.end()) {
    return it->second;
  }
  memo[id] = false; // breaks template cycles
  bool blocked = false;
  auto lr = br.LookupById(id);
  if (auto npc = lr.rec ? espm::Convert<espm::fo4::NPC_>(lr.rec) : nullptr) {
    auto d = npc->GetData(cache);
    auto map = [&](uint32_t local) {
      return local ? lr.ToGlobalId(local) : 0u;
    };
    bool raceFromTemplate =
      (d.templateFlags & (1u << espm::fo4::NPC_::kTraits)) != 0;
    uint32_t tmpl = d.templateActors[espm::fo4::NPC_::kTraits]
      ? map(d.templateActors[espm::fo4::NPC_::kTraits])
      : map(d.defaultTemplate);
    if (raceFromTemplate && tmpl) {
      blocked = IsBlocked(tmpl, depth + 1);
    } else {
      blocked = d.race && IsBlockedRace(map(d.race));
    }
  } else if (auto lvln =
               lr.rec ? espm::Convert<espm::fo4::LVLN>(lr.rec) : nullptr) {
    for (auto& e : lvln->GetData(cache).entries) {
      if (IsBlocked(lr.ToGlobalId(e.refId), depth + 1)) {
        blocked = true;
        break;
      }
    }
  }
  memo[id] = blocked;
  return blocked;
}

}
