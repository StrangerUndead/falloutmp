#pragma once
// Connects Fo4Server to PartOne: implements Fo4Host on top of WorldState and
// ServerState, persists per-actor Fallout 4 state in the actor's change form
// ("fo4State" dynamic field) and the world state in a JSON file.
#include "Fo4Server.h"
#include <memory>
#include <string>

class PartOne;
class MpActor;

namespace fo4 {

class Fo4PartOneGlue
{
public:
  explicit Fo4PartOneGlue(PartOne& partOne);
  ~Fo4PartOneGlue();

  Fo4Server& Server();
  // Replaces the server with one using these settings. Only allowed before
  // any actor or world state was loaded (startup), else throws.
  void ApplySettings(const Fo4ServerSettings& settings);
  // Loads the sender's saved state on first contact, then handles it.
  void OnMessage(uint32_t actorId, MsgType type, const IMessageBase& msg);
  void Tick();
  // Saves every loaded actor and the world now.
  void SaveAll();
  void OnActorDisconnected(MpActor& actor);

  // World state file (workshops, frames, locks, parties). Empty = off.
  void SetWorldStatePath(std::string path);
  // Registers frames, settlements and locks from the load order that the
  // world save doesn't already have. Call after SetWorldStatePath.
  void BootstrapFromLoadOrder();
  int64_t saveIntervalMs = 30000;

  static constexpr const char* kActorStateField = "fo4State";

private:
  void EnsureActorLoaded(uint32_t actorId);
  void ResolveDataIds();
  void SaveActor(uint32_t actorId);
  void LoadWorldFile();

  struct Impl;
  std::unique_ptr<Impl> pImpl;
};

}
