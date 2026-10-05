#pragma once
// Damage authority guard of the combat module (Combat.cpp, F11).
//
// The server decides all damage. Puppets never take damage from local hits
// and never die from them: Combat.cpp blocks every decrease of a damage
// modifier on a puppet's actor values (Actor::CheckClampDamageModifier),
// except while the server's own values are being written.
//
// Modules that write a puppet's health or limb conditions from server
// values (actor values: setHealthFraction, setActorValueCurrent) must
// open the guard:
//   - direct engine calls (ModActorValue, RestoreActorValue, ...) on the
//     main thread: inside a `combat::ServerWrite` scope;
//   - Papyrus writes (DamageValue, ...), which run later on a VM thread:
//     `AllowWrites(id, true)` before the call and `AllowWrites(id, false)`
//     in its done callback.
// Healing (positive deltas), base values and Kill are never blocked.
#include <cstdint>

namespace fmp::combat {

class ServerWrite
{
public:
  ServerWrite() noexcept;
  ~ServerWrite();
  ServerWrite(const ServerWrite&) = delete;
  ServerWrite& operator=(const ServerWrite&) = delete;
};

// Counted: every `true` needs its `false`.
void AllowWrites(uint32_t formId, bool allow);

// True inside a ServerWrite scope (this thread) or while AllowWrites is
// open for the reference.
bool WritesAllowed(uint32_t formId);

}
