// Clean world: FalloutMP is a player-run wasteland with no story and no
// NPCs (STATUS.md decisions). While the client is set up to join a server
// (FalloutMP.json "server-ip"), this module keeps the local game that way:
//
//   - Actors: every actor that isn't the player, another player (puppet) or
//     a FalloutMP object (game::IsNetworkRef: workshop turrets, ...) is
//     disabled as soon as it loads. A sweep of the process lists runs every
//     second; TESObjectLoadedEvent catches actors in between. Humans,
//     ghouls, creatures and robots alike, unless FalloutMP.json has
//     "keep-creatures": true (then only actors with ActorTypeNPC go).
//   - Quests: story, faction, companion, radiant, encounter and dialogue
//     quests are stopped when they run (the new-game intro included), every
//     5 seconds. System quests (workshop, survival, perks, radio, ...) keep
//     running.
//   - Pip-Boy: a player without one (a new game before the vault) gets it
//     added and equipped. It is a local-only item: the client's inventory
//     sync leaves it alone (inventoryService kLocalOnlyItems).
//
// Off with "features": { "cleanWorld": false }. Every stopped quest and the
// number of disabled actors go to FalloutMP.log.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "ItemKeys.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"
#include "Puppets.h"

#include <array>
#include <format>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fmp::modules {

namespace {
constexpr double kActorSweepMs = 1000.0;
constexpr double kQuestSweepMs = 5000.0;
constexpr double kPipboyCheckMs = 5000.0;
constexpr uint32_t kPipboy = 0x00021B3B; // ARMO "Pipboy"
// [verify] TESObjectREFR form flag "disabled" (0x800, as in other
// Bethesda games)
constexpr uint32_t kDisabledFlag = 0x800;
// [verify] QUEST_DATA flag 0x1 is "running" (Skyrim's kEnabled)
constexpr uint16_t kQuestRunning = 0x1;

// Quests that keep running whatever else matches.
constexpr std::array<std::string_view, 9> kKeepPrefixes = {
  "Workshop", "HC_",         "Perk",     "Player",   "Pipboy",
  "Radio",    "Achievement", "Holotape", "Tutorial",
};
// Editor id prefixes of story, encounter, companion and dialogue quests.
constexpr std::array<std::string_view, 19> kStopPrefixes = {
  "MQ",   "RE",   "DN",    "COM",       "Dialogue", "Followers", "RQ",
  "MS",   "FF",   "BoS",   "RR",        "Inst",     "Min",       "V81",
  "V111", "Cait", "Curie", "Codsworth", "Dog",
};

bool g_keepCreatures = false;
double g_lastActorSweepMs = 0;
double g_lastQuestSweepMs = 0;
double g_lastPipboyCheckMs = 0;
std::unordered_set<uint32_t> g_disabled;      // actors this session
std::unordered_set<uint32_t> g_stoppedLogged; // quests already logged
uint64_t g_disabledTotal = 0;

bool StartsWithAny(std::string_view s, auto const& prefixes)
{
  for (auto p : prefixes) {
    if (s.starts_with(p)) {
      return true;
    }
  }
  return false;
}

bool ShouldRemove(RE::Actor* actor)
{
  const uint32_t id = actor->GetFormID();
  if (id == game::kPlayerRef || puppets::IsPuppet(id) ||
      game::IsNetworkRef(id)) {
    return false;
  }
  if ((actor->GetFormFlags() & kDisabledFlag) || g_disabled.count(id)) {
    return false;
  }
  if (g_keepCreatures) {
    auto race = actor->race;
    return race && race->HasKeywordString("ActorTypeNPC");
  }
  return true;
}

void RemoveActors(Platform& p, const std::vector<RE::Actor*>& actors)
{
  size_t n = 0;
  for (auto actor : actors) {
    if (!ShouldRemove(actor)) {
      continue;
    }
    g_disabled.insert(actor->GetFormID());
    actor->Disable();
    ++n;
  }
  if (n) {
    g_disabledTotal += n;
    p.Log("info",
          std::format("Clean world: removed {} actors ({} so far)", n,
                      g_disabledTotal));
  }
}

void SweepActors(Platform& p)
{
  auto lists = RE::ProcessLists::GetSingleton();
  if (!lists) {
    return;
  }
  // Collected first: Disable changes the process lists.
  std::vector<RE::Actor*> actors;
  for (auto* handles :
       { &lists->highActorHandles, &lists->middleHighActorHandles,
         &lists->middleLowActorHandles, &lists->lowActorHandles }) {
    for (auto& handle : *handles) {
      auto ptr = handle.get();
      if (auto actor = ptr.get()) {
        actors.push_back(actor);
      }
    }
  }
  RemoveActors(p, actors);
}

bool ShouldStop(RE::TESQuest* quest)
{
  std::string_view id = quest->formEditorID.c_str();
  if (StartsWithAny(id, kKeepPrefixes)) {
    return false;
  }
  // Quest types 1..14: main quest, factions, misc, side, DLC
  return quest->data.questType != 0 || StartsWithAny(id, kStopPrefixes);
}

void SweepQuests(Platform& p)
{
  auto dataHandler = RE::TESDataHandler::GetSingleton();
  if (!dataHandler) {
    return;
  }
  for (auto quest : dataHandler->GetFormArray<RE::TESQuest>()) {
    if (!quest || !(quest->data.flags & kQuestRunning) || !ShouldStop(quest)) {
      continue;
    }
    // Stop()
    papyrus::CallMethod(quest, "Quest", "Stop", nullptr);
    if (g_stoppedLogged.insert(quest->GetFormID()).second) {
      p.Log("info",
            std::format("Clean world: stopped quest {} ({:08X})",
                        quest->formEditorID.c_str(), quest->GetFormID()));
    }
  }
}

void EnsurePipboy(Platform& p)
{
  auto player = RE::PlayerCharacter::GetSingleton();
  auto pipboy = game::Form<RE::TESBoundObject>(kPipboy);
  auto manager = RE::ActorEquipManager::GetSingleton();
  if (!player || !pipboy || !manager ||
      player->GetInventoryObjectCount(pipboy) > 0) {
    return;
  }
  items::Key key;
  key.baseId = kPipboy;
  items::ApplyGuard guard;
  items::Add(player, key, 1, true);
  if (auto inst = items::Find(player, key, true); inst && !inst->equipped) {
    RE::BGSObjectInstance object{ inst->object,
                                  items::InstanceDataOf(player, *inst) };
    // [verify] the Pip-Boy shows on the arm and the Pip-Boy menu opens
    manager->EquipObject(player, object, inst->stackIndex, 1, nullptr, false,
                         true, false, true, false);
  }
  p.Log("info", "Clean world: gave the player a Pip-Boy");
}

class LoadedSink final : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::TESObjectLoadedEvent& e,
    RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
  {
    if (e.loaded) {
      const uint32_t id = e.formID;
      auto& p = Platform::Get();
      p.QueueTask([&p, id] {
        if (auto actor = game::ActorOf(id)) {
          RemoveActors(p, { actor });
        }
      });
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};
}

void InstallCleanWorld(Platform& p)
{
  if (p.Config().serverIp.empty() || p.Config().probe) {
    REX::INFO("Clean world is off: no server-ip (or probe mode)");
    return;
  }
  g_keepCreatures = p.Config().raw.value("keep-creatures", false);

  static LoadedSink loadedSink;
  if (auto source = RE::TESObjectLoadedEvent::GetEventSource()) {
    source->RegisterSink(&loadedSink);
  }

  p.OnFrame([&p](float) {
    static bool wasLoading = false;
    if (game::Loading() || !RE::PlayerCharacter::GetSingleton()) {
      wasLoading = true;
      return;
    }
    if (wasLoading) {
      // Another save (or cell) loaded: its actors come back as saved
      wasLoading = false;
      g_disabled.clear();
      g_lastActorSweepMs = 0;
    }
    const double now = p.NowMs();
    if (now - g_lastActorSweepMs >= kActorSweepMs) {
      g_lastActorSweepMs = now;
      SweepActors(p);
    }
    if (now - g_lastQuestSweepMs >= kQuestSweepMs) {
      g_lastQuestSweepMs = now;
      SweepQuests(p);
    }
    if (now - g_lastPipboyCheckMs >= kPipboyCheckMs) {
      g_lastPipboyCheckMs = now;
      EnsurePipboy(p);
    }
  });
  REX::INFO("Clean world is on (keep-creatures: {})", g_keepCreatures);
}

}
