#pragma once
// Puppets: actors standing in for other players (and NPCs streamed in from
// the server). The network drives them; the engine must not.
//
// Every NPC actor's Actor::Update / UpdateNoAI (vtable of RE::Actor; the
// player has its own) is hooked; modules add post-update callbacks to write
// state after the engine's own update (graph variables, transforms), so
// engine channels can't overwrite it.
//
// Two more Actor vfuncs are hooked as guards (see SetGuards):
//   CheckValidTarget: puppets never pick combat targets, and local NPCs
//                     don't pick puppets;
//   KillImpl:         puppets don't die locally unless the server said so
//                     (AllowKill).
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
  // Last network movement state (Movement module), for other modules:
  uint32_t moveFlags = 0;  // MoveFlag bits (setMovementFlags)
  float aimPitch = 0;      // degrees (setAimAngles)
  float aimHeading = 0;    // degrees, relative to the yaw
};

// Main thread only (State pointers stay valid until Remove).
bool IsPuppet(uint32_t formId); // any thread
RE::Actor* Get(uint32_t formId);
State* Find(uint32_t formId);
const std::unordered_map<uint32_t, State>& All();

// Called by the modules that create/delete puppets (Puppets module).
void Add(uint32_t formId, State state);
void Remove(uint32_t formId);

using PostUpdate = std::function<void(RE::Actor* puppet, float dtSec)>;
void OnPostUpdate(PostUpdate fn);

struct Guards
{
  // Local NPCs may pick puppets as combat targets (hosted NPCs fighting
  // remote players). Off: puppets are never attacked by local NPCs.
  bool npcsTargetPuppets = false;
  // Puppets may die from local damage. Off: KillImpl is ignored for
  // puppets until AllowKill (the server's kill order) was called.
  bool localDeath = false;
};
void SetGuards(Guards guards);

// The server killed this puppet: the next local kill goes through. Call
// it right before Actor.Kill / KillImpl on a puppet.
void AllowKill(uint32_t formId);

// Installs the actor update and guard hooks (once).
void InstallHooks();

}
