// Papyrus event blocking (PLAT-036): the game's own scripts don't react to
// anything, the way SkyMP does it (skymp5-client
// blockPapyrusEventsService.ts, skyrim-platform FridaHooks.cpp
// OnSendEventEnter). Quests and their scripts still exist, but no OnLoad,
// OnActivate, OnCellAttach, OnStageSet, story or scene event reaches a
// handler, so nothing advances on its own. Clean world then only has to
// stop the few quests that act without events (CleanWorld.cpp).
//
// How: IVirtualMachine::SendEvent (vfunc 0x2B) is replaced in the live VM's
// vtable (no address id needed). A blocked event goes on under a name no
// script handles, so the VM finds no handler and every caller's callback
// still runs.
// Let through:
//   - events named in "allow-events" (default OnTimer and OnTimerGameTime,
//     Fallout 4's OnUpdate, which SkyMP lets through), and
//   - every event of an object with a script whose name starts with an
//     "allow-scripts" prefix. Empty by default: the check reads the VM's
//     attached-script table (VirtualMachine::attachedScripts, a CommonLibF4
//     layout not yet verified on 1.11.240) on every event. SkyMP's list
//     would be ["defaultDisableHavokOnLoad"] plus SkyUI; ours would be
//     ["Workshop", "defaultDisableHavokOnLoad", "MCM"] once verified.
// FalloutMP's own calls (papyrus::CallMethod) are method calls, not events,
// and are never blocked.
//
// FalloutMP.json:
//   "features": { "papyrusEvents": false }        off
//   "papyrus-events": { "allow-events": ["OnHit"],
//                       "allow-scripts": ["MyMod_"] }   added to the defaults
//
// The hook runs on every thread that sends events: only atomics, the
// read-only lists and a mutex-guarded set of logged names. The first time
// each event name is blocked goes to FalloutMP.log, and the counts every
// 30 s for the first two minutes in game.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"

#include <atomic>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fmp::modules {

namespace {
constexpr size_t kSendEventIndex = 0x2B; // IVirtualMachine::SendEvent
constexpr size_t kMaxLoggedNames = 200;
constexpr double kStatsEveryMs = 30000.0;
constexpr int kStatsReports = 4;

// The arguments are passed through untouched, so their game types (the
// BSTThreadScrapFunction and smart pointer references) stay opaque.
using SendEventFn = void (*)(RE::BSScript::IVirtualMachine*, std::uint64_t,
                             const RE::BSFixedString&, const void*,
                             const void*, const void*);

SendEventFn g_original = nullptr;
std::atomic<bool> g_blocking{ false };
std::vector<std::string> g_allowEvents;  // lower case
std::vector<std::string> g_allowScripts; // lower-case prefixes
std::atomic<uint64_t> g_blocked{ 0 };
std::atomic<uint64_t> g_allowed{ 0 };
std::mutex g_loggedLock;
std::unordered_set<std::string> g_loggedNames;

std::string Lower(std::string_view s)
{
  std::string out(s);
  for (auto& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

bool EventAllowed(std::string_view name)
{
  const auto lower = Lower(name);
  for (auto& e : g_allowEvents) {
    if (lower == e) {
      return true;
    }
  }
  return false;
}

// One of the object's scripts is on the allow list.
bool ScriptAllowed(RE::BSScript::IVirtualMachine* a_vm, std::uint64_t a_handle)
{
  if (g_allowScripts.empty()) {
    return false;
  }
  auto vm = static_cast<RE::BSScript::Internal::VirtualMachine*>(a_vm);
  bool allowed = false;
  vm->attachedScriptsLock.lock();
  if (auto it = vm->attachedScripts.find(a_handle);
      it != vm->attachedScripts.end()) {
    for (auto& attached : it->second) {
      auto object = attached.get();
      auto info = object ? object->GetTypeInfo() : nullptr;
      const char* scriptName = info ? info->GetName() : nullptr;
      if (!scriptName) {
        continue;
      }
      const auto lower = Lower(scriptName);
      for (auto& prefix : g_allowScripts) {
        if (lower.starts_with(prefix)) {
          allowed = true;
          break;
        }
      }
      if (allowed) {
        break;
      }
    }
  }
  vm->attachedScriptsLock.unlock();
  return allowed;
}

void LogFirstBlock(std::string_view name)
{
  std::lock_guard l(g_loggedLock);
  if (g_loggedNames.size() >= kMaxLoggedNames) {
    return;
  }
  if (g_loggedNames.emplace(name).second) {
    REX::INFO("Papyrus events: blocking '{}' (first time)", name);
  }
}

void SendEventThunk(RE::BSScript::IVirtualMachine* a_this,
                    std::uint64_t a_handle, const RE::BSFixedString& a_name,
                    const void* a_args, const void* a_filter,
                    const void* a_callback)
{
  if (g_blocking.load(std::memory_order_relaxed) && !a_name.empty()) {
    const std::string_view name = a_name;
    if (!EventAllowed(name) && !ScriptAllowed(a_this, a_handle)) {
      g_blocked.fetch_add(1, std::memory_order_relaxed);
      LogFirstBlock(name);
      // As SkyMP (which sends it on with an empty name): the event goes on
      // under a name no script handles, so the caller's callback still
      // runs. Not "": an empty BSFixedString can be a null pointer.
      static const RE::BSFixedString kNoEvent("FalloutMPBlockedEvent");
      g_original(a_this, a_handle, kNoEvent, a_args, a_filter, a_callback);
      return;
    }
    g_allowed.fetch_add(1, std::memory_order_relaxed);
  }
  g_original(a_this, a_handle, a_name, a_args, a_filter, a_callback);
}

void ReadList(const nlohmann::json& settings, const char* key,
              std::vector<std::string>& out)
{
  auto it = settings.find(key);
  if (it == settings.end() || !it->is_array()) {
    return;
  }
  for (auto& v : *it) {
    if (v.is_string() && !v.get<std::string>().empty()) {
      out.push_back(Lower(v.get<std::string>()));
    }
  }
}
}

bool PapyrusEventsBlocked()
{
  return g_blocking.load();
}

void InstallPapyrusEvents(Platform& p)
{
  if (p.Config().probe) {
    REX::INFO("Papyrus events are not blocked in probe mode");
    return;
  }
  g_allowEvents = { "ontimer", "ontimergametime" };
  g_allowScripts.clear(); // opt-in (see the top of this file)
  const auto& raw = p.Config().raw;
  if (auto it = raw.find("papyrus-events");
      it != raw.end() && it->is_object()) {
    ReadList(*it, "allow-events", g_allowEvents);
    ReadList(*it, "allow-scripts", g_allowScripts);
  }

  auto vm = papyrus::Vm();
  if (!vm) {
    REX::ERROR("Papyrus events: no script VM; events stay unblocked");
    return;
  }
  // The live VM's vtable: the original is stored before the slot changes,
  // since other threads may send events at any moment
  auto vtbl = *reinterpret_cast<std::uintptr_t**>(vm.get());
  auto slot = &vtbl[kSendEventIndex];
  g_original = reinterpret_cast<SendEventFn>(*slot);
  if (!g_original) {
    REX::ERROR("Papyrus events: empty SendEvent slot; events stay unblocked");
    return;
  }
  REL::WriteSafeData(reinterpret_cast<std::uintptr_t>(slot),
                     reinterpret_cast<std::uintptr_t>(&SendEventThunk));
  g_blocking = true;

  std::string events, scripts;
  for (auto& e : g_allowEvents) {
    events += (events.empty() ? "" : ", ") + e;
  }
  for (auto& s : g_allowScripts) {
    scripts += (scripts.empty() ? "" : ", ") + s + "*";
  }
  REX::INFO("Papyrus events: blocked except events [{}] and scripts [{}]",
            events, scripts.empty() ? "none" : scripts);

  p.OnFrame([&p](float) {
    static double lastMs = -1;
    static int reports = 0;
    if (reports >= kStatsReports || !p.InGame()) {
      return;
    }
    const double now = p.NowMs();
    if (lastMs < 0) {
      lastMs = now;
      return;
    }
    if (now - lastMs < kStatsEveryMs) {
      return;
    }
    lastMs = now;
    ++reports;
    REX::INFO("Papyrus events (last {:.0f} s): blocked {}, let through {}",
              kStatsEveryMs / 1000, g_blocked.exchange(0),
              g_allowed.exchange(0));
  });
}

}
