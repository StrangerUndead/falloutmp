// Clean world: FalloutMP is a player-run wasteland with no story and no
// NPCs (STATUS.md decisions). This module keeps the local game that way
// (always, except in probe mode; installing FalloutMP is for multiplayer):
//
//   - Actors: every actor that isn't the player, another player (puppet) or
//     a FalloutMP object (game::IsNetworkRef: workshop turrets, ...) is
//     disabled once it loads, one actor per frame, as SkyMP's
//     worldCleanerService does (one random nearby actor per update).
//     Actors that just loaded (TESObjectLoadedEvent) go first, then the
//     loaded actors (high and middle-high process, with 3D); actors that
//     aren't loaded are left until they load. Dead bodies stay and can't be
//     activated (SkyMP: blockActivation); an actor that is talking is left
//     for later (SkyMP: deleting an actor in dialogue crashes the game).
//     Humans, ghouls, creatures and robots alike, unless FalloutMP.json has
//     "keep-creatures": true (then only actors with ActorTypeNPC go).
//   - Quests: with the game's Papyrus events blocked (PapyrusEvents.cpp,
//     SkyMP's approach) quest scripts never react, so only the quests that
//     act on their own are stopped: the main quest intro, companions and
//     followers, random encounters, the Minutemen radiant quests and the
//     Brotherhood's arrival. With blocking off ("papyrusEvents": false),
//     every story, faction, companion, radiant, encounter and dialogue
//     quest is stopped instead. Two per sweep, a sweep every 5 seconds or
//     every half second while some are left. System quests (workshop,
//     survival, perks, radio, ...) always keep running.
//   - Pip-Boy: a player without one (a new game before the vault) gets it
//     added and equipped. It is a local-only item: the client's inventory
//     sync leaves it alone (inventoryService kLocalOnlyItems).
//
// Everything here runs on the main thread (Platform::Tick, hooks::TickNow):
// a first version that disabled ~1,000 actors and stopped ~180 quests in
// one go from an F4SE task thread crashed the game right after a load
// (2026-10-10).
//
// Off with "features": { "cleanWorld": false }. Every stopped quest, and
// the number of disabled actors every 5 seconds, go to FalloutMP.log.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "ItemKeys.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"
#include "Puppets.h"

#include <array>
#include <deque>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fmp::modules {

namespace {
constexpr double kStartDelayMs = 3000.0;
constexpr double kQuestSweepMs = 5000.0;
constexpr double kQuestBacklogSweepMs = 500.0; // while quests are left
constexpr size_t kMaxQuestsPerSweep = 2;
constexpr double kRemovedLogMs = 5000.0;
// Pending (just loaded) actors looked at per frame to find one to remove.
constexpr size_t kMaxPendingChecks = 16;
// Stop() goes through the script VM: a quest asked to stop can still read
// as running for a moment, so it isn't asked again (or counted) this soon.
constexpr double kStopRetryMs = 5000.0;
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
// Quests whose editor id contains one of these keep running too.
constexpr std::array<std::string_view, 4> kKeepParts = {
  "Workshop",
  "Player",
  "ReconScope",
  "Settlement",
};
// With Papyrus events blocked: the quests that act without events.
constexpr std::array<std::string_view, 7> kActiveQuests = {
  "MQ00",                  // main quest timing
  "Followers",             // companion and follower AI
  "DogmeatQuest",          //
  "REParent",              // random encounters (story manager)
  "MinutemenCentralQuest", // Minutemen radiant quests and attacks
  "BoSEnable",             // the Brotherhood's arrival (Prydwen)
  "BoS100Fight",           // the vertibird fight
};
constexpr std::array<std::string_view, 2> kActiveQuestPrefixes = {
  "MQ1", // MQ101..: the intro and the first main quest stages
  "COM", // companion quests (COMCodsworth, COMPreston, ...)
};
// Without blocking: editor id prefixes of story, encounter, companion and
// dialogue quests.
constexpr std::array<std::string_view, 19> kStopPrefixes = {
  "MQ",   "RE",   "DN",    "COM",       "Dialogue", "Followers", "RQ",
  "MS",   "FF",   "BoS",   "RR",        "Inst",     "Min",       "V81",
  "V111", "Cait", "Curie", "Codsworth", "Dog",
};

bool g_keepCreatures = false;
double g_lastQuestSweepMs = 0;
double g_lastPipboyCheckMs = 0;
std::unordered_set<uint32_t> g_disabled; // actors this session
std::unordered_set<uint32_t> g_bodies;   // activation blocked
std::deque<uint32_t> g_pending;          // actors that loaded
uint64_t g_removedSinceLog = 0;
double g_lastRemovedLogMs = 0;
std::unordered_set<uint32_t> g_stoppedLogged;       // quests already logged
std::unordered_map<uint32_t, double> g_stopAskedMs; // quest -> last Stop()
uint64_t g_disabledTotal = 0;

// Only in a loaded game: never during start-up, at the main menu or while
// a save loads (changing the world then crashes the game), and not in the
// first seconds after a load.
bool Active(Platform& p)
{
  return game::OnMainThread() && p.InGame() &&
    p.NowMs() - p.InGameSinceMs() >= kStartDelayMs && !game::Loading() &&
    !game::MenuOpen("MainMenu") && RE::PlayerCharacter::GetSingleton();
}

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
    if (!race || !race->HasKeywordString("ActorTypeNPC")) {
      return false;
    }
  }
  if (actor->IsDead(true)) {
    // Bodies stay, but can't be looted or activated (SkyMP keeps vanilla
    // bodies the same way)
    if (g_bodies.insert(id).second) {
      papyrus::CallMethod(actor, "ObjectReference", "BlockActivation", nullptr,
                          true, true);
    }
    return false;
  }
  // SkyMP: removing an actor in dialogue crashes the game; try again later
  return !actor->IsTalking();
}

void Remove(RE::Actor* actor)
{
  g_disabled.insert(actor->GetFormID());
  actor->Disable();
  ++g_removedSinceLog;
  ++g_disabledTotal;
}

// One actor per frame: a just-loaded one first, else the first loaded
// actor that should go.
void RemoveOneActor()
{
  for (size_t i = 0; i < kMaxPendingChecks && !g_pending.empty(); ++i) {
    const uint32_t id = g_pending.front();
    g_pending.pop_front();
    auto actor = game::ActorOf(id);
    if (actor && actor->Get3D() && ShouldRemove(actor)) {
      Remove(actor);
      return;
    }
  }
  auto lists = RE::ProcessLists::GetSingleton();
  if (!lists) {
    return;
  }
  for (auto* handles :
       { &lists->highActorHandles, &lists->middleHighActorHandles }) {
    for (auto& handle : *handles) {
      auto ptr = handle.get();
      auto actor = ptr.get();
      if (actor && actor->Get3D() && ShouldRemove(actor)) {
        Remove(actor);
        return;
      }
    }
  }
}

void LogRemoved(Platform& p, double now)
{
  if (g_removedSinceLog && now - g_lastRemovedLogMs >= kRemovedLogMs) {
    p.Log("info",
          std::format("Clean world: removed {} actors ({} so far)",
                      g_removedSinceLog, g_disabledTotal));
    g_removedSinceLog = 0;
    g_lastRemovedLogMs = now;
  }
}

bool ShouldStop(RE::TESQuest* quest)
{
  std::string_view id = quest->formEditorID.c_str();
  if (StartsWithAny(id, kKeepPrefixes)) {
    return false;
  }
  // Workshop and player systems in any game or DLC quest
  for (auto part : kKeepParts) {
    if (id.find(part) != std::string_view::npos) {
      return false;
    }
  }
  if (PapyrusEventsBlocked()) {
    for (auto active : kActiveQuests) {
      if (id == active) {
        return true;
      }
    }
    return StartsWithAny(id, kActiveQuestPrefixes);
  }
  // Quest types 1..14: main quest, factions, misc, side, DLC
  return quest->data.questType != 0 || StartsWithAny(id, kStopPrefixes);
}

// True when running quests that should stop are left for the next sweep.
bool SweepQuests(Platform& p)
{
  auto dataHandler = RE::TESDataHandler::GetSingleton();
  if (!dataHandler) {
    return false;
  }
  const double now = p.NowMs();
  size_t n = 0;
  for (auto quest : dataHandler->GetFormArray<RE::TESQuest>()) {
    if (!quest || !(quest->data.flags & kQuestRunning) || !ShouldStop(quest)) {
      continue;
    }
    auto asked = g_stopAskedMs.find(quest->GetFormID());
    if (asked != g_stopAskedMs.end() && now - asked->second < kStopRetryMs) {
      continue;
    }
    if (n++ >= kMaxQuestsPerSweep) {
      return true;
    }
    g_stopAskedMs[quest->GetFormID()] = now;
    // Stop()
    papyrus::CallMethod(quest, "Quest", "Stop", nullptr);
    if (g_stoppedLogged.insert(quest->GetFormID()).second) {
      p.Log("info",
            std::format("Clean world: stopped quest {} ({:08X})",
                        quest->formEditorID.c_str(), quest->GetFormID()));
    }
  }
  return false;
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
      // Queued for the frame loop (main thread), which removes one actor
      // per frame
      p.QueueTask([id] {
        if (game::ActorOf(id)) { // the event covers every reference
          g_pending.push_back(id);
        }
      });
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};
}

void CleanWorldSweepNow(Platform& p)
{
  if (!game::OnMainThread()) {
    return;
  }
  RemoveOneActor();
  SweepQuests(p);
  EnsurePipboy(p);
}

void InstallCleanWorld(Platform& p)
{
  if (p.Config().probe) {
    REX::INFO("Clean world is off in probe mode");
    return;
  }
  g_keepCreatures = p.Config().raw.value("keep-creatures", false);

  static LoadedSink loadedSink;
  if (auto source = RE::TESObjectLoadedEvent::GetEventSource()) {
    source->RegisterSink(&loadedSink);
  }

  p.OnFrame([&p](float) {
    static bool wasLoading = false;
    if (!Active(p)) {
      wasLoading = true;
      return;
    }
    if (wasLoading) {
      // Another save (or cell) loaded: its actors come back as saved
      wasLoading = false;
      g_disabled.clear();
      g_bodies.clear();
      g_pending.clear();
    }
    const double now = p.NowMs();
    RemoveOneActor();
    LogRemoved(p, now);
    if (now - g_lastQuestSweepMs >= kQuestSweepMs) {
      g_lastQuestSweepMs = now;
      if (SweepQuests(p)) {
        g_lastQuestSweepMs = now - kQuestSweepMs + kQuestBacklogSweepMs;
      }
    }
    if (now - g_lastPipboyCheckMs >= kPipboyCheckMs) {
      g_lastPipboyCheckMs = now;
      EnsurePipboy(p);
    }
  });
  REX::INFO("Clean world is on (keep-creatures: {})", g_keepCreatures);
}

}
