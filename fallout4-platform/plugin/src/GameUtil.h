#pragma once
// Small helpers every module uses. Include <RE/Fallout.h> before this.
#include <RE/Fallout.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace fmp::game {

using Json = nlohmann::json;

// The player reference (PlayerRef) in every Fallout 4 game.
constexpr uint32_t kPlayerRef = 0x14;

float ToRad(float deg);
// Normalized to [0, 360)
float ToDeg(float rad);

RE::NiPoint3 Point(const Json& p); // [x, y, z]
Json ToJson(const RE::NiPoint3& p);

// A loaded reference by runtime form id (0x14 = the player).
RE::TESObjectREFR* Ref(uint32_t formId);
RE::Actor* ActorOf(uint32_t formId);

template <class T>
T* Form(uint32_t formId)
{
  return formId ? RE::TESForm::GetFormByID<T>(formId) : nullptr;
}

// Server "worldOrCell": a worldspace (exterior) or an interior cell.
bool ResolveSpace(uint32_t worldOrCell, RE::TESWorldSpace*& world,
                  RE::TESObjectCELL*& interior);
// worldOrCell of a reference (its worldspace outdoors, else its cell).
uint32_t SpaceOf(RE::TESObjectREFR* ref);

// Places a new reference of a base form (no save persistence forced).
RE::TESObjectREFR* CreateRef(uint32_t baseId, const RE::NiPoint3& pos,
                             float yawDeg, RE::TESWorldSpace* world,
                             RE::TESObjectCELL* interior,
                             bool initializeScripts = false);

// References FalloutMP created (puppets, workshop objects, power armor
// frames, markers). The clean-world sweep leaves them alone. Any thread.
void MarkNetworkRef(uint32_t formId);
void UnmarkNetworkRef(uint32_t formId);
bool IsNetworkRef(uint32_t formId);

// The game's main thread. F4SE loads plugins on it, so F4SE_PLUGIN_LOAD
// records it. F4SE's tasks run on whichever thread pumps the game's
// message queue (often a worker thread once in game), so code that changes
// the world checks OnMainThread() first.
void SetMainThread();
bool OnMainThread();
uint32_t MainThreadId();
uint32_t CurrentThreadId();

bool MenuOpen(const char* menuName);
// A loading screen is up (no movement, no spawning).
bool Loading();

}
