#pragma once
// Puppets: actors standing in for other players (and NPCs streamed in from
// the server). The network drives them; the engine must not.
//
// Every NPC actor's Actor::Update / UpdateNoAI (vtable of RE::Actor; the player has its own) is hooked; modules add
// post-update callbacks to write state after the engine's own update
// (graph variables, transforms), so engine channels can't overwrite it.
#include <RE/Fallout.h>

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace fmp::puppets {

struct State
{
  uint32_t baseId = 0;  // base NPC the puppet was made from
  uint32_t npcId = 0;   // its own runtime TESNPC (appearance), 0 = shared
  float spawnTimeMs = 0;
};

bool IsPuppet(uint32_t formId);
RE::Actor* Get(uint32_t formId);
State* Find(uint32_t formId);
const std::unordered_map<uint32_t, State>& All();

// Called by the modules that create/delete puppets (Puppets module).
void Add(uint32_t formId, State state);
void Remove(uint32_t formId);

using PostUpdate = std::function<void(RE::Actor* puppet, float dtSec)>;
void OnPostUpdate(PostUpdate fn);

// Installs the actor update hooks (once).
void InstallHooks();

}
