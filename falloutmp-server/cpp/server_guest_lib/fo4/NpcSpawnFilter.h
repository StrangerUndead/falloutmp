#pragma once
// Which load-order NPCs the server spawns on a Fallout 4 server (F13).
//
// Human NPCs are off by default (fo4.npc.humanNpcs): settlers, raiders,
// gunners and named characters need quest, dialogue and AI work before they
// behave in a shared world. Creatures, robots and other races still spawn
// when the upstream "npcEnabled" setting is on.
//
// The race is found the way the game does it: through the traits template
// chain (TPTA traits entry, else TPLT), following leveled NPC templates. A
// leveled template counts as blocked if any of its entries is.
#include "libespm/CombineBrowser.h"
#include "libespm/RecordHeader.h"
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace fo4 {

class NpcSpawnFilter
{
public:
  NpcSpawnFilter(const espm::CombineBrowser& br,
                 std::vector<std::string> blockedRaceEditorIds);

  // baseId: global form id of a reference's base record
  bool IsAllowed(uint32_t baseId);

  static std::vector<std::string> DefaultHumanRaces()
  {
    return { "HumanRace", "HumanChildRace" };
  }

private:
  bool IsBlocked(uint32_t id, int depth);
  bool IsBlockedRace(uint32_t raceId);

  const espm::CombineBrowser& br;
  std::set<std::string> blockedRaces;
  espm::CompressedFieldsCache cache;
  std::unordered_map<uint32_t, bool> memo;
  std::unordered_map<uint32_t, bool> raceMemo;
};

}
