#include "Puppets.h"

#include <F4SE/F4SE.h>

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>
#include <vector>

namespace fmp::puppets {

namespace {
// Written on the main thread; the guard hooks may run on AI worker threads
// and only read through IsPuppet / KillAllowed under the shared lock.
std::shared_mutex g_lock;
std::unordered_map<uint32_t, State> g_puppets;
std::unordered_set<uint32_t> g_killAllowed;
std::vector<PostUpdate> g_postUpdate;
std::atomic<bool> g_npcsTargetPuppets{ false };
std::atomic<bool> g_localDeath{ false };

bool KillAllowed(uint32_t formId)
{
  std::shared_lock lock(g_lock);
  return g_killAllowed.count(formId) > 0;
}

void RunPostUpdate(RE::Actor* actor, float dt)
{
  if (!actor || g_postUpdate.empty() || !IsPuppet(actor->GetFormID())) {
    return;
  }
  for (auto& fn : g_postUpdate) {
    try {
      fn(actor, dt);
    } catch (std::exception& e) {
      REX::ERROR("Puppet post-update failed: {}", e.what());
    }
  }
}

// Actor::Update (0xCF) runs for AI-processed NPCs, UpdateNoAI
// (0xD0) for actors with AI disabled. Puppets can be either.
struct ActorUpdate
{
  static void Thunk(RE::Actor* a_this, float a_delta)
  {
    original(a_this, a_delta);
    RunPostUpdate(a_this, a_delta);
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

struct ActorUpdateNoAI
{
  static void Thunk(RE::Actor* a_this, float a_delta)
  {
    original(a_this, a_delta);
    RunPostUpdate(a_this, a_delta);
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

// Actor::CheckValidTarget (0xF4): whether this actor may fight a_ref.
// Puppets never pick targets (their attacks are replayed from the
// network); local NPCs leave puppets alone unless configured otherwise.
// The player has its own vtable, so PvP targeting is unaffected.
// [verify] combat target selection goes through this vfunc for NPCs.
struct ActorCheckValidTarget
{
  static bool Thunk(const RE::Actor* a_this, RE::TESObjectREFR& a_ref)
  {
    if (a_this && IsPuppet(a_this->GetFormID())) {
      return false;
    }
    if (!g_npcsTargetPuppets.load(std::memory_order_relaxed) &&
        IsPuppet(a_ref.GetFormID())) {
      return false;
    }
    return original(a_this, a_ref);
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

// Actor::KillImpl (0x117): puppets die when the server says so, not when
// local damage or physics would kill them.
// [verify] every local death path (damage, Kill, kill moves) ends here,
// and skipping it leaves the actor standing without side effects.
struct ActorKillImpl
{
  static void Thunk(RE::Actor* a_this, RE::Actor* a_attacker, float a_damage,
                    bool a_sendEvent, bool a_ragdollInstant)
  {
    if (a_this && !g_localDeath.load(std::memory_order_relaxed)) {
      auto id = a_this->GetFormID();
      if (IsPuppet(id) && !KillAllowed(id)) {
        return;
      }
    }
    original(a_this, a_attacker, a_damage, a_sendEvent, a_ragdollInstant);
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};
}

bool IsPuppet(uint32_t formId)
{
  std::shared_lock lock(g_lock);
  return g_puppets.count(formId) > 0;
}

RE::Actor* Get(uint32_t formId)
{
  return IsPuppet(formId) ? RE::TESForm::GetFormByID<RE::Actor>(formId)
                          : nullptr;
}

State* Find(uint32_t formId)
{
  auto it = g_puppets.find(formId);
  return it == g_puppets.end() ? nullptr : &it->second;
}

const std::unordered_map<uint32_t, State>& All()
{
  return g_puppets;
}

void Add(uint32_t formId, State state)
{
  std::unique_lock lock(g_lock);
  g_puppets[formId] = state;
  g_killAllowed.erase(formId);
}

void Remove(uint32_t formId)
{
  std::unique_lock lock(g_lock);
  g_puppets.erase(formId);
  g_killAllowed.erase(formId);
}

void OnPostUpdate(PostUpdate fn)
{
  g_postUpdate.push_back(std::move(fn));
}

void SetGuards(Guards guards)
{
  g_npcsTargetPuppets = guards.npcsTargetPuppets;
  g_localDeath = guards.localDeath;
}

void AllowKill(uint32_t formId)
{
  std::unique_lock lock(g_lock);
  if (g_puppets.count(formId)) {
    g_killAllowed.insert(formId);
  }
}

void InstallHooks()
{
  static bool installed = false;
  if (installed) {
    return;
  }
  installed = true;
  REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::Actor[0] };
  ActorUpdate::original = vtbl.write_vfunc(0xCF, ActorUpdate::Thunk);
  ActorUpdateNoAI::original =
    vtbl.write_vfunc(0xD0, ActorUpdateNoAI::Thunk);
  ActorCheckValidTarget::original =
    vtbl.write_vfunc(0xF4, ActorCheckValidTarget::Thunk);
  ActorKillImpl::original = vtbl.write_vfunc(0x117, ActorKillImpl::Thunk);
  REX::INFO("Installed the actor update and puppet guard hooks");
}

}
