#include "Puppets.h"

#include <F4SE/F4SE.h>

#include <vector>

namespace fmp::puppets {

namespace {
std::unordered_map<uint32_t, State> g_puppets;
std::vector<PostUpdate> g_postUpdate;

void RunPostUpdate(RE::Actor* actor, float dt)
{
  if (!actor || g_postUpdate.empty() ||
      !g_puppets.count(actor->GetFormID())) {
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
}

bool IsPuppet(uint32_t formId)
{
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
  g_puppets[formId] = state;
}

void Remove(uint32_t formId)
{
  g_puppets.erase(formId);
}

void OnPostUpdate(PostUpdate fn)
{
  g_postUpdate.push_back(std::move(fn));
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
  REX::INFO("Installed the actor update hooks");
}

}
