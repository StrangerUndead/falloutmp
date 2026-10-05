// F02 animation sync (docs/falloutmp/features/F02-animation.md,
// reference/fo4-animation-sync.md §3, §4.5, §5.2):
//   - getGraphVariables: typed reads by name from an actor's graph;
//   - setGraphVariables: sticky per-puppet values, written after every
//     engine update of the puppet (puppets::OnPostUpdate);
//   - notifyAnimationGraph: an animation event sent to a puppet's graph;
//   - "animationEvent": whitelisted graph events of the player, captured by
//     two hooks on the PlayerCharacter vtables (events sent to the graph and
//     events the graph sends back);
//   - puppets' graphs are woken once they exist (and again after a rebuild).
//
// No CommonLibF4 API registers a sink on a graph (BShkbAnimationGraph is
// only forward-declared), so capture hooks the player's own sub-vtables
// instead (MODULES.md rule 6.2). They belong to the PlayerCharacter class,
// not to a graph, so nothing has to be re-registered when the player's
// graph is rebuilt (power armor, loading a save, race change).
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "Modules.h"
#include "Platform.h"
#include "Puppets.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fmp::modules {

namespace {

// GraphVariable.type (falloutmp-client fo4Messages.ts)
enum class VarType : int
{
  kFloat = 0,
  kInt = 1,
  kBool = 2
};

constexpr size_t kMaxNameLength = 128;
constexpr size_t kMaxStickyPerPuppet = 128;
constexpr size_t kMaxNameCache = 512;
// Puppet graph wake: give up after this many post-updates (~5 s at 60 fps)
constexpr int kMaxWakeAttempts = 300;
// Captured events older than this since the last frame are dropped (loading
// screens, main menu: Platform::Tick doesn't run then).
constexpr double kStaleTickMs = 1000.0;
// The same event reported by both hooks (or twice by one) within this
// window is emitted once.
constexpr double kDedupeMs = 50.0;

// ---------------------------------------------------------------------------
// Variable types
//
// Havok stores one 32-bit word per variable, and the typed getters don't
// report the variable's real type (CommonLibF4 has no hkbBehaviorGraphData
// layout). The type comes from the Bethesda naming convention, with a few
// known exceptions (reference §4.1/§4.2 names).
// [verify] Compare the guessed types with a dump of the human 3rd-person
// graph's variable infos (F02-T01); a wrong type reads garbage.
VarType GuessType(std::string_view n)
{
  struct Known
  {
    std::string_view name;
    VarType type;
  };
  static constexpr Known kKnown[] = {
    { "m_bEnablePitchTwistModifier", VarType::kBool },
    { "LeftHandIKOn", VarType::kBool },
    { "RightHandIKOn", VarType::kBool },
    { "FootIKDisable", VarType::kBool },
    { "Enable_bEquipOK", VarType::kBool },
    { "TEMPIsPlayer", VarType::kBool },
    { "BoolVariable", VarType::kBool },
    { "CurrentJumpState", VarType::kInt },
    { "Int32Variable00", VarType::kInt },
  };
  for (auto& k : kKnown) {
    if (k.name == n) {
      return k.type;
    }
  }
  auto upperAt = [&](size_t i) {
    return n.size() > i && n[i] >= 'A' && n[i] <= 'Z';
  };
  if (n.size() > 1 && n[0] == 'i' && upperAt(1)) {
    return VarType::kInt; // iSyncWalkRun, iAttackState
  }
  if (n.size() > 1 && n[0] == 'b' && upperAt(1)) {
    return VarType::kBool; // bInJumpState, bAimActive
  }
  if ((n.starts_with("Is") || n.starts_with("is")) && upperAt(2)) {
    return VarType::kBool; // IsSneaking, isFiring
  }
  return VarType::kFloat; // Speed, Direction, staggerMagnitude
}

// ---------------------------------------------------------------------------
// Graphs

struct GraphInfo
{
  // graph[0] (the 3rd-person graph; the only one of an NPC). Compared by
  // value only, to notice a rebuilt graph; never dereferenced.
  std::uintptr_t key = 0;
  uint32_t count = 0;  // 2 for the player (3rd + 1st person), else 1
  uint32_t active = 0; // BSAnimationGraphManager::activeGraph
};

GraphInfo ReadGraph(RE::Actor* actor)
{
  GraphInfo info;
  if (!actor) {
    return info;
  }
  RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
  auto holder = static_cast<const RE::IAnimationGraphManagerHolder*>(actor);
  if (!holder->GetAnimationGraphManagerImpl(manager) || !manager) {
    return info;
  }
  info.count = manager->graph.size();
  info.active = manager->activeGraph;
  if (info.count > 0) {
    info.key = reinterpret_cast<std::uintptr_t>(manager->graph[0].get());
  }
  return info;
}

RE::IAnimationGraphManagerHolder* Holder(RE::Actor* actor)
{
  return static_cast<RE::IAnimationGraphManagerHolder*>(actor);
}

// Engine strings are created on first use (the string pool doesn't exist at
// DLL load) and never released (no destructor at process exit).
const RE::BSFixedString& WakeBaseState()
{
  static const auto* s =
    new RE::BSFixedString("g_archetypeBaseStateStartInstant");
  return *s;
}

const RE::BSFixedString& WakeMoveStop()
{
  static const auto* s = new RE::BSFixedString("MoveStop");
  return *s;
}

// ---------------------------------------------------------------------------
// State (heap-allocated and never freed: it holds BSFixedStrings).
//
// Natives and frame callbacks run on the main thread; puppet post-updates
// are expected there too, but the lock keeps this correct if the engine
// updates actors on job threads.

struct StickyVar
{
  std::string key;
  RE::BSFixedString name;
  VarType type = VarType::kFloat;
  double value = 0;
};

struct PuppetAnim
{
  float spawnTimeMs = 0; // puppets::State of the puppet this belongs to
  std::vector<StickyVar> vars;
  std::uintptr_t wokenGraph = 0; // graph key the wake events went to
  std::uintptr_t lastGraph = 0;
  int wakeAttempts = 0;
};

struct AnimState
{
  std::mutex lock;
  std::unordered_map<uint32_t, PuppetAnim> puppets;
  std::unordered_map<std::string, RE::BSFixedString> names; // main thread
};

AnimState& S()
{
  static auto* s = new AnimState();
  return *s;
}

// The PuppetAnim of a live puppet; reset when the form id now belongs to a
// different puppet (references created at runtime can reuse form ids).
PuppetAnim* AnimOf(uint32_t id)
{
  auto st = puppets::Find(id);
  if (!st) {
    return nullptr;
  }
  auto& e = S().puppets[id];
  if (e.spawnTimeMs != st->spawnTimeMs) {
    e = PuppetAnim{};
    e.spawnTimeMs = st->spawnTimeMs;
  }
  return &e;
}

const RE::BSFixedString& CachedName(const std::string& name,
                                    RE::BSFixedString& scratch)
{
  auto& cache = S().names;
  if (auto it = cache.find(name); it != cache.end()) {
    return it->second;
  }
  if (cache.size() >= kMaxNameCache) {
    scratch = RE::BSFixedString(name.c_str());
    return scratch;
  }
  return cache.emplace(name, RE::BSFixedString(name.c_str())).first->second;
}

void Write(RE::IAnimationGraphManagerHolder* h, const StickyVar& v)
{
  switch (v.type) {
    case VarType::kFloat:
      h->SetGraphVariableFloat(v.name, static_cast<float>(v.value));
      break;
    case VarType::kInt:
      h->SetGraphVariableInt(
        v.name,
        static_cast<int>(std::clamp(v.value, -2147483648.0, 2147483647.0)));
      break;
    case VarType::kBool:
      h->SetGraphVariableBool(v.name, v.value != 0);
      break;
  }
}

// ---------------------------------------------------------------------------
// getGraphVariables
//
// The holder's typed getters read the graph the manager currently uses.
// For NPCs and puppets that is their only (3rd-person) graph. For the
// player in 3rd person it is the 3rd-person graph too. For the player in
// 1st person the active graph is the 1st-person one (reference §3.2): the
// parked 3rd-person graph is not updated then (its state machine and the
// PopulateGraphVariables locomotion snapshot are stale), so it is not read.
// The values come from the live 1st-person graph instead; names it lacks
// (iSyncWalkRun, iSyncTurnState, IsSneaking, bInJumpState …, reference
// §4.2) are left out, except two that engine state answers below.
// [verify] The getters read graph[activeGraph] (not graph[0]) for the
// player in 1st person, and return false for a name the graph lacks.

std::atomic<bool> g_playerFirstPersonGraph{ false };

bool PlayerFallback(RE::Actor* actor, std::string_view name, Json& out)
{
  bool value = false;
  if (name == "IsSneaking") {
    value = actor->IsSneaking();
  } else if (name == "bInJumpState") {
    value = actor->IsJumping();
  } else {
    return false;
  }
  out = Json{ { "name", std::string(name) },
              { "type", static_cast<int>(VarType::kBool) },
              { "value", value ? 1 : 0 } };
  return true;
}

Json GetGraphVariables(uint32_t actorId, const Json& names)
{
  Json result = Json::array();
  auto actor = game::ActorOf(actorId);
  if (!actor || !names.is_array()) {
    return result;
  }
  auto holder = static_cast<const RE::IAnimationGraphManagerHolder*>(actor);
  const bool isPlayer = actorId == game::kPlayerRef;
  const bool firstPerson = isPlayer && g_playerFirstPersonGraph.load();
  RE::BSFixedString scratch;
  for (auto& n : names) {
    if (!n.is_string()) {
      continue;
    }
    auto& name = n.get_ref<const std::string&>();
    if (name.empty() || name.size() > kMaxNameLength) {
      continue;
    }
    auto& fixed = CachedName(name, scratch);
    auto type = GuessType(name);
    Json value;
    bool found = false;
    switch (type) {
      case VarType::kFloat: {
        float f = 0;
        found =
          holder->GetGraphVariableImplFloat(fixed, f) && std::isfinite(f);
        value = f;
        break;
      }
      case VarType::kInt: {
        std::int32_t i = 0;
        found = holder->GetGraphVariableImplInt(fixed, i);
        value = i;
        break;
      }
      case VarType::kBool: {
        bool b = false;
        found = holder->GetGraphVariableImplBool(fixed, b);
        value = b ? 1 : 0;
        break;
      }
    }
    if (found) {
      result.push_back(Json{ { "name", name },
                             { "type", static_cast<int>(type) },
                             { "value", std::move(value) } });
      continue;
    }
    Json fallback;
    if (firstPerson && PlayerFallback(actor, name, fallback)) {
      result.push_back(std::move(fallback));
    }
  }
  return result;
}

// ---------------------------------------------------------------------------
// setGraphVariables / notifyAnimationGraph (puppets only)

void SetGraphVariables(uint32_t actorId, const Json& values)
{
  auto actor = puppets::Get(actorId);
  if (!actor || !values.is_array()) {
    return;
  }
  std::lock_guard l(S().lock);
  auto anim = AnimOf(actorId);
  if (!anim) {
    return;
  }
  auto holder = Holder(actor);
  for (auto& v : values) {
    auto name = v.at("name").get<std::string>();
    int typeNum = v.at("type").get<int>();
    double value = v.at("value").get<double>();
    if (name.empty() || name.size() > kMaxNameLength || typeNum < 0 ||
        typeNum > 2 || !std::isfinite(value)) {
      continue;
    }
    auto type = static_cast<VarType>(typeNum);
    auto it = std::find_if(anim->vars.begin(), anim->vars.end(),
                           [&](const StickyVar& s) { return s.key == name; });
    if (it == anim->vars.end()) {
      if (anim->vars.size() >= kMaxStickyPerPuppet) {
        continue;
      }
      StickyVar s;
      s.name = RE::BSFixedString(name.c_str());
      s.key = std::move(name);
      anim->vars.push_back(std::move(s));
      it = anim->vars.end() - 1;
    }
    it->type = type;
    it->value = value;
    // Now as well as after every update: a puppet the engine updates rarely
    // (low process level) still gets the value.
    Write(holder, *it);
  }
}

bool NotifyGraph(uint32_t actorId, const std::string& eventName)
{
  auto actor = puppets::Get(actorId);
  if (!actor || eventName.empty() || eventName.size() > kMaxNameLength) {
    return false;
  }
  return Holder(actor)->NotifyAnimationGraphImpl(
    RE::BSFixedString(eventName.c_str()));
}

// After the engine's update of a puppet: wake a new graph, then write the
// sticky variables so this frame's locomotion channels don't win.
// [verify] Actor::Update/UpdateNoAI run after the puppet's graph channel
// flush in the same frame. If Speed/Direction still read 0 on the puppet,
// move the writes to the Actor IPostAnimationChannelUpdateFunctor
// (Actor+0x168, VTABLE::Actor[16], vfunc 1 DoPostAnimationChannelUpdate),
// which may run on animation job threads.
void PostUpdate(RE::Actor* actor, float)
{
  if (!actor) {
    return;
  }
  std::lock_guard l(S().lock);
  auto anim = AnimOf(actor->GetFormID());
  if (!anim) {
    return;
  }
  auto graph = ReadGraph(actor);
  if (!graph.key) {
    return; // no 3D / graph yet
  }
  auto holder = Holder(actor);
  if (graph.key != anim->lastGraph) {
    anim->lastGraph = graph.key;
    anim->wakeAttempts = 0;
  }
  // A fresh graph sits in its initial state (T-pose) until an event moves
  // it (reference §3.2 point 6): base state, then stop moving.
  // [verify] The puppet leaves the T-pose after spawn and after a cell
  // reload; NotifyAnimationGraphImpl returns true once the graph is ready.
  if (anim->wokenGraph != graph.key && actor->Get3D()) {
    if (holder->NotifyAnimationGraphImpl(WakeBaseState())) {
      holder->NotifyAnimationGraphImpl(WakeMoveStop());
      anim->wokenGraph = graph.key;
    } else if (++anim->wakeAttempts >= kMaxWakeAttempts) {
      REX::WARN("Puppet {:X}: the graph refused the wake events",
                actor->GetFormID());
      anim->wokenGraph = graph.key;
    }
  }
  for (auto& v : anim->vars) {
    Write(holder, v);
  }
}

// Drops the state of puppets that no longer exist (main thread).
void Sweep()
{
  std::lock_guard l(S().lock);
  auto& all = S().puppets;
  for (auto it = all.begin(); it != all.end();) {
    auto st = puppets::Find(it->first);
    if (!st || st->spawnTimeMs != it->second.spawnTimeMs) {
      it = all.erase(it);
    } else {
      ++it;
    }
  }
}

// ---------------------------------------------------------------------------
// animationEvent capture (player)
//
// Replayable events only (reference §5.2): no locomotion (MoveStart/Stop,
// turns, footsteps, sound annotations), no gameplay-only outputs (weaponFire,
// ReloadComplete, throwEnd, weaponSwing, Event00: shots, reloads and throws
// are shown by the combat service), no state annotations that duplicate an
// event (sneakStateEnter/Exit, attackStateEnter/Exit).
// [verify] Names not found in any source (stagger/get-up, idles, blockStop)
// are Skyrim-style guesses: log the player's events in game and adjust.
constexpr std::string_view kReplayable[] = {
  // jumps
  "jumpStart",
  "jumpStartFromWalk",
  "jumpFall",
  "jumpLand",
  "jumpLandSoft",
  "jumpLandToWalk",
  "jumpLandToRun",
  "jumpEnd",
  "jumpEndToRun",
  // sneak
  "sneakStart",
  "sneakStop",
  // reload start
  "reloadStart",
  "reloadReserveStart",
  "reloadSequentialStart",
  "reloadSequentialReserveStart",
  // attacks / melee
  "attackStart",
  "attackStartAuto",
  "attackRelease",
  "attackStop",
  "attackInterrupt",
  "meleeattackStart",
  "meleeattackSprintStart",
  "meleeAttackGun",
  // throws
  "grenadeThrowStart",
  "mineThrowStart",
  // block
  "blockStart",
  "blockStop",
  // equip / unequip (draw / holster)
  "weapEquip",
  "weapUnequip",
  "weapForceEquip",
  // sighted
  "sightedStateEnter",
  "sightedStateExit",
  "rifleSightedStart",
  "rifleSightedEnd",
  // stagger / get up
  "staggerStart",
  "GetUpStart",
  "GetUpBegin",
  "GetUpEnd",
  // idles / pose
  "IdleStop",
  "IdleForceDefaultState",
  "g_archetypeBaseStateStart",
  "g_archetypeRelaxedStateStart",
};
constexpr size_t kReplayableCount = std::size(kReplayable);

std::array<std::atomic<double>, kReplayableCount> g_lastEmitMs{};
std::atomic<double> g_lastTickMs{ -1e12 };

bool EqualsNoCase(std::string_view a, std::string_view b)
{
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    char x = a[i], y = b[i];
    if (x >= 'A' && x <= 'Z') {
      x = static_cast<char>(x - 'A' + 'a');
    }
    if (y >= 'A' && y <= 'Z') {
      y = static_cast<char>(y - 'A' + 'a');
    }
    if (x != y) {
      return false;
    }
  }
  return true;
}

// Any thread: no game objects here (MODULES.md rule 4).
void Capture(const RE::BSFixedString& event) noexcept
{
  try {
    std::string_view name = event;
    if (name.empty()) {
      return;
    }
    size_t idx = kReplayableCount;
    for (size_t i = 0; i < kReplayableCount; ++i) {
      if (EqualsNoCase(name, kReplayable[i])) {
        idx = i;
        break;
      }
    }
    if (idx == kReplayableCount) {
      return;
    }
    auto& p = Platform::Get();
    const double now = p.NowMs();
    if (now - g_lastTickMs.load() > kStaleTickMs) {
      return;
    }
    if (now - g_lastEmitMs[idx].exchange(now) < kDedupeMs) {
      return;
    }
    p.Emit(
      "animationEvent",
      Json{ { "actor", game::kPlayerRef }, { "name", std::string(name) } });
  } catch (...) {
  }
}

// Events sent TO the player's graph (input events: jumpStart, sneakStart,
// attackStart, reloadStart …). IAnimationGraphManagerHolder vfunc 0x01 on
// the player's holder sub-vtable (TESObjectREFR+0x48).
// [verify] The engine sends the player's input events through this vfunc
// (not straight to BSAnimationGraphManager), in 1st and 3rd person.
struct PlayerNotifyGraph
{
  static bool Thunk(RE::IAnimationGraphManagerHolder* a_this,
                    const RE::BSFixedString& a_eventName)
  {
    const bool accepted = original(a_this, a_eventName);
    // A refused event did nothing here; in 1st person the 1st-person graph
    // may refuse events only the 3rd-person graph knows, so keep those.
    // [verify] Which events the 1st-person graph refuses.
    if (accepted || g_playerFirstPersonGraph.load()) {
      Capture(a_eventName);
    }
    return accepted;
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

// Events the player's graph sends out (annotations: sightedStateEnter,
// weapEquip …), delivered to the reference's BSTEventSink<BSAnimationGraph
// Event> (TESObjectREFR+0x38), vfunc 0x01 ProcessEvent. May run on
// animation job threads.
// [verify] The player reference receives its graph's output events here
// (SetupAnimEventSinks registers it), in 1st and 3rd person.
struct PlayerGraphEvent
{
  static RE::BSEventNotifyControl Thunk(
    RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_this,
    const RE::BSAnimationGraphEvent& a_event,
    RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_source)
  {
    auto result = original(a_this, a_event, a_source);
    Capture(a_event.tag);
    return result;
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

std::uintptr_t VtableOf(const void* subobject)
{
  return *static_cast<const std::uintptr_t*>(subobject);
}

void InstallCaptureHooks()
{
  static bool installed = false;
  if (installed) {
    return;
  }
  installed = true;
  // The sub-vtables are read from the live player (the PlayerCharacter
  // exists at kGameDataReady), so no vtable array index has to be right.
  // Fallback: VTABLE::PlayerCharacter[3] / [5], i.e. the 4th and 6th
  // polymorphic bases in offset order (TESObjectREFR: TESForm 0x00,
  // BSHandleRefObject 0x20, sink<BSActiveGraphIfInactiveEvent> 0x30,
  // sink<BSAnimationGraphEvent> 0x38, sink<InventoryList> 0x40,
  // IAnimationGraphManagerHolder 0x48 …: 8 vtables, the 8 TESObjectREFR
  // IDs).
  REL::Relocation<std::uintptr_t> sinkVtbl;
  REL::Relocation<std::uintptr_t> holderVtbl;
  if (auto player = RE::PlayerCharacter::GetSingleton()) {
    sinkVtbl = REL::Relocation<std::uintptr_t>{ VtableOf(
      static_cast<RE::BSTEventSink<RE::BSAnimationGraphEvent>*>(player)) };
    holderVtbl = REL::Relocation<std::uintptr_t>{ VtableOf(
      static_cast<RE::IAnimationGraphManagerHolder*>(player)) };
  } else {
    REX::WARN("No player at install: using the vtable IDs");
    sinkVtbl =
      REL::Relocation<std::uintptr_t>{ RE::VTABLE::PlayerCharacter[3] };
    holderVtbl =
      REL::Relocation<std::uintptr_t>{ RE::VTABLE::PlayerCharacter[5] };
  }
  if (!sinkVtbl.address() || !holderVtbl.address()) {
    REX::ERROR("Player graph vtables not found: no animation capture");
    return;
  }
  PlayerNotifyGraph::original =
    holderVtbl.write_vfunc(0x01, PlayerNotifyGraph::Thunk);
  PlayerGraphEvent::original =
    sinkVtbl.write_vfunc(0x01, PlayerGraphEvent::Thunk);
  REX::INFO("Installed the player animation capture hooks");
}

// Every frame (main thread): the tick time for the capture hooks, whether
// the player's 1st-person graph is active, puppet state cleanup.
void Frame(Platform& p)
{
  g_lastTickMs.store(p.NowMs());

  static std::uintptr_t lastPlayerGraph = 0;
  auto player = RE::PlayerCharacter::GetSingleton();
  auto graph = ReadGraph(player);
  // The player's manager holds two graphs, 1 = 1st person (reference §3.2).
  // [verify] activeGraph is 1 in 1st person and 0 in 3rd person.
  g_playerFirstPersonGraph.store(graph.count >= 2 && graph.active == 1);
  if (graph.key && graph.key != lastPlayerGraph) {
    // Rebuilt (load, power armor, race change). The capture hooks are on
    // the class vtables and need nothing; logged for the probe.
    REX::DEBUG("Player animation graph (re)built: {} graph(s)", graph.count);
  }
  lastPlayerGraph = graph.key;

  if (p.FrameCount() % 60 == 0) {
    Sweep();
  }
}

}

void InstallAnimation(Platform& p)
{
  p.RegisterNative("getGraphVariables", [](const Json& a) -> Json {
    return GetGraphVariables(a.at(0).get<uint32_t>(), a.at(1));
  });
  p.RegisterNative("setGraphVariables", [](const Json& a) -> Json {
    SetGraphVariables(a.at(0).get<uint32_t>(), a.at(1));
    return nullptr;
  });
  p.RegisterNative("notifyAnimationGraph", [](const Json& a) -> Json {
    return NotifyGraph(a.at(0).get<uint32_t>(), a.at(1).get<std::string>());
  });

  p.OnFrame([&p](float) { Frame(p); });
  puppets::OnPostUpdate(PostUpdate);
  InstallCaptureHooks();
}

}
