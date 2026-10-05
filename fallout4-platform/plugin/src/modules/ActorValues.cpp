// F08 actor values and F12 death: setActorValueCurrent, setActorValueMax,
// setHealthFraction, killActor (falloutPlatform.ts ActorValueNatives).
//
// Actor values are server-owned. The ids are ActorValueInfo form ids
// (codes.ts Av, falloutmp-server fo4/ActorValues.h: Health 0x2D4 ...), written
// through the actor's ActorValueOwner like Papyrus SetValue / DamageValue /
// RestoreValue: current = base + permanent + temporary + damage.
//
// No engine write may kill (F08 §4.5, F12 §3):
//   - Health written by the network never goes below 1. A server death is
//     killActor, or Health 0 on the player;
//   - the HP funnel: Actor::CheckClampDamageModifier (vfunc 0x131, Actor and
//     PlayerCharacter vtables) keeps engine damage (falls, hazards, the
//     player's shots on puppets) from taking the player or a puppet below
//     1 HP, so only the server decides deaths;
//   - the player is kept in deferred kill (Actor.StartDeferredKill) as a
//     second guard.
// The player is never killed in the engine: a vanilla player death ends in
// the game-over flow that reloads the last save, which CommonLibF4 offers no
// way to cancel. As in SkyMP's deathService, a server death ragdolls the
// player and takes control away; the server's respawn (Health above 0 again)
// gives it back.
//
// Hosted NPCs (F13) are not written: the plugin knows the player and puppets
// only (MODULES.md rule 8), and NPCs are off in this project. For the same
// reason hostedValuesChanged is not emitted.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "CombatGuard.h"
#include "GameUtil.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"
#include "Puppets.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <mutex>
#include <unordered_set>

namespace fmp::modules {

namespace {
using Modifier = RE::ACTOR_VALUE_MODIFIER;
using AvFlags = RE::ActorValue::Flags;

constexpr uint32_t kHealth = 0x2D4;     // Av.Health
constexpr uint32_t kExperience = 0x2C9; // Av.Experience: Progression's
constexpr float kMinHealth = 1.f;
constexpr float kEpsilon = 0.001f;

// The player's death as the server sees it (the engine never kills it).
struct PlayerState
{
  bool dead = false;
  bool deferredKill = false; // StartDeferredKill issued since the last load
};
PlayerState g_player;
std::atomic<bool> g_gameLoaded = false; // set by the load sink, any thread
// Only in a multiplayer session (a server in FalloutMP.json): without one
// the player must stay mortal.
std::atomic<bool> g_guardPlayer = false;

// Puppets the server killed: no HP clamp, no health writes.
std::unordered_set<uint32_t> g_killed;

// Puppets under the HP clamp. The damage hook may run off the main thread,
// so it reads this copy, rebuilt every frame, instead of the puppet registry.
std::mutex g_guardMutex;
std::unordered_set<uint32_t> g_guarded;

RE::ActorValueOwner& Values(RE::Actor* actor)
{
  return *actor;
}

bool IsHealth(const RE::ActorValueInfo& info)
{
  return info.GetFormID() == kHealth;
}

// Rads count up: their damage modifier is positive.
bool DamageIsPositive(const RE::ActorValueInfo& info)
{
  return info.flags.all(AvFlags::kDamageIsPositive);
}

// base + permanent + temporary modifiers: the server's AvEntry::Max.
// [verify] GetPermanentActorValue is base + permanent modifier (Skyrim
// semantics); log both against Papyrus GetBaseValue/GetValue once.
float MaxOf(const RE::ActorValueOwner& v, const RE::ActorValueInfo& info)
{
  return v.GetPermanentActorValue(info) +
    v.GetModifier(Modifier::kTemporary, info);
}

// The current value through the damage modifier (DamageValue/RestoreValue),
// so max stays where SetMax put it.
// [verify] ModActorValue(kDamage, +x) restores like RestoreActorValue, and
// Rads carry the kDamageIsPositive flag.
void WriteCurrent(RE::Actor* actor, const RE::ActorValueInfo& info,
                  float value)
{
  // Server values pass the combat module's damage guard on puppets
  combat::ServerWrite serverWrite;
  if (IsHealth(info)) {
    value = std::max(value, kMinHealth); // deaths are killActor's
  }
  auto& v = Values(actor);
  float delta = value - v.GetActorValue(info);
  float damage = v.GetModifier(Modifier::kDamage, info);
  if (DamageIsPositive(info)) {
    delta = std::max(delta, -damage); // damage >= 0 shrinks down to 0
  } else {
    delta = std::min(delta, -damage); // damage <= 0 restores up to max
  }
  if (std::fabs(delta) > kEpsilon) {
    v.ModActorValue(Modifier::kDamage, info, delta);
  }
}

// Max first (the client's order), keeping the current value: the client
// sends current only when it changed.
void WriteMax(RE::Actor* actor, const RE::ActorValueInfo& info, float value)
{
  auto& v = Values(actor);
  float before = v.GetActorValue(info);
  float delta = value - MaxOf(v, info);
  if (std::fabs(delta) <= kEpsilon) {
    return;
  }
  // Plain AVs (SPECIAL, limb conditions) keep their base. Derived ones
  // (Health, AP, CarryWeight) compute it from SPECIAL and level, so what
  // the base can't carry goes to the permanent modifier.
  // [verify] Health/ActionPoints/CarryWeight carry kDerived, and the
  // Pip-Boy shows the new maxima.
  if (info.flags.none(AvFlags::kDerived)) {
    v.SetBaseActorValue(info, v.GetBaseActorValue(info) + delta);
  }
  float rest = value - MaxOf(v, info);
  if (std::fabs(rest) > kEpsilon) {
    v.ModActorValue(Modifier::kPermanent, info, rest);
  }
  WriteCurrent(actor, info,
               DamageIsPositive(info) ? before : std::min(before, value));
}

// The player or a puppet; anything else a script names is left alone.
RE::Actor* Target(uint32_t formId)
{
  if (formId == game::kPlayerRef) {
    return RE::PlayerCharacter::GetSingleton();
  }
  return puppets::Get(formId);
}

bool Guarded(uint32_t formId)
{
  if (formId == game::kPlayerRef) {
    return g_guardPlayer;
  }
  std::lock_guard lock(g_guardMutex);
  return g_guarded.contains(formId);
}

// A script object of `type` bound to a form. Actor parameters need it:
// CommonLibF4 packs an RE::Actor* as an ObjectReference (Actor has no
// TYPE_ID of its own), which an Actor parameter does not accept.
RE::BSTSmartPointer<RE::BSScript::Object> ScriptObject(RE::TESForm* form,
                                                       const char* type)
{
  auto vm = papyrus::Vm();
  if (!vm || !form) {
    return nullptr;
  }
  auto handle = papyrus::HandleOf(form);
  if (handle == vm->GetObjectHandlePolicy().EmptyHandle()) {
    return nullptr;
  }
  RE::BSTSmartPointer<RE::BSScript::Object> object;
  if (!vm->FindBoundObject(handle, type, false, object, false) &&
      vm->CreateObject(RE::BSFixedString(type), object) && object) {
    vm->GetObjectBindPolicy().BindObject(object, handle);
  }
  return object;
}

// The HP funnel. CheckClampDamageModifier(info, delta) returns the change
// of the damage modifier the engine will apply; for Health on a guarded
// actor it may not take the value below 1.
// [verify] every Health damage path (hits, falls, hazards, Papyrus
// DamageValue) goes through this vfunc and a_delta is a delta, not the new
// modifier: shoot a puppet with a lethal weapon and jump from a height;
// both must stop at 1 HP.
template <int Vtable>
struct ClampDamage
{
  static float Thunk(RE::Actor* a_this, RE::ActorValueInfo& a_info,
                     float a_delta)
  {
    float delta = original(a_this, a_info, a_delta);
    if (delta < 0.f && a_this && IsHealth(a_info) &&
        Guarded(a_this->GetFormID())) {
      float current = Values(a_this).GetActorValue(a_info);
      delta = std::max(delta, std::min(0.f, kMinHealth - current));
    }
    return delta;
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

void InstallHooks()
{
  static bool installed = false;
  if (installed) {
    return;
  }
  installed = true;
  constexpr std::size_t kCheckClampDamageModifier = 0x131;
  REL::Relocation<std::uintptr_t> actorVtbl{ RE::VTABLE::Actor[0] };
  ClampDamage<0>::original =
    actorVtbl.write_vfunc(kCheckClampDamageModifier, ClampDamage<0>::Thunk);
  REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE::PlayerCharacter[0] };
  ClampDamage<1>::original =
    playerVtbl.write_vfunc(kCheckClampDamageModifier, ClampDamage<1>::Thunk);
  REX::INFO("Installed the health clamp hooks");
}

class LoadSink final : public RE::BSTEventSink<RE::TESLoadGameEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::TESLoadGameEvent&,
    RE::BSTEventSource<RE::TESLoadGameEvent>*) override
  {
    g_gameLoaded = true;
    return RE::BSEventNotifyControl::kContinue;
  }
};

// A server death of the local player, without the engine's death.
// [verify] PushActorAway(player, 0) ragdolls the player in third person and
// it stays down while AI driven (SkyMP's killWithPush). If it gets up on its
// own, try Actor.SetUnconscious(true) here and SetUnconscious(false) in
// RevivePlayer (F12-T05 prototype).
void KillPlayer(Platform& p, RE::PlayerCharacter* player)
{
  if (!player || g_player.dead) {
    return;
  }
  g_player.dead = true;
  p.Log("info", "The server killed the player");
  if (auto health = game::Form<RE::ActorValueInfo>(kHealth)) {
    WriteCurrent(player, *health, kMinHealth);
  }
  papyrus::CallStatic("Game", "ForceThirdPerson", nullptr);
  papyrus::CallStatic("Game", "SetPlayerAIDriven", nullptr, true);
  papyrus::CallMethod(player, "ObjectReference", "PushActorAway", nullptr,
                      ScriptObject(player, "Actor"), 0.f);
}

// The server's respawn: the client then teleports the player and restores
// the values (DeathStateContainer / ChangeValuesAv).
void RevivePlayer(Platform& p)
{
  if (!g_player.dead) {
    return;
  }
  g_player.dead = false;
  p.Log("info", "The player is alive again");
  papyrus::CallStatic("Game", "SetPlayerAIDriven", nullptr, false);
}

void SetCurrent(Platform& p, uint32_t actorId, uint32_t avId, float value)
{
  auto actor = Target(actorId);
  auto info = game::Form<RE::ActorValueInfo>(avId);
  // XP is written by setPlayerLevelAndXp, below the level-up threshold
  if (!actor || !info || !std::isfinite(value) || avId == kExperience) {
    REX::DEBUG("setActorValueCurrent {:X} {:X}: no puppet or AV", actorId,
               avId);
    return;
  }
  if (IsHealth(*info)) {
    if (actorId == game::kPlayerRef) {
      if (value <= 0.f) {
        KillPlayer(p, RE::PlayerCharacter::GetSingleton());
        return;
      }
      RevivePlayer(p);
    } else if (g_killed.contains(actorId)) {
      return; // dead until the server recreates the puppet
    }
  }
  WriteCurrent(actor, *info, value);
}

void SetMax(uint32_t actorId, uint32_t avId, float value)
{
  auto actor = Target(actorId);
  auto info = game::Form<RE::ActorValueInfo>(avId);
  // XP is written by setPlayerLevelAndXp, below the level-up threshold
  if (!actor || !info || !std::isfinite(value) || avId == kExperience) {
    REX::DEBUG("setActorValueMax {:X} {:X}: no puppet or AV", actorId, avId);
    return;
  }
  if (IsHealth(*info) && g_killed.contains(actorId)) {
    return;
  }
  WriteMax(actor, *info, value);
}

// Remote actors get Health as a 0..1 fraction of their own maximum: only
// the health bar reads it. A fraction of 0 shows 1 HP; the death itself
// comes as killActor.
void SetHealthFraction(uint32_t actorId, float fraction)
{
  auto actor = puppets::Get(actorId);
  auto health = game::Form<RE::ActorValueInfo>(kHealth);
  if (!actor || !health || !std::isfinite(fraction) ||
      g_killed.contains(actorId) || actor->IsDead(false)) {
    return;
  }
  float max = MaxOf(Values(actor), *health);
  WriteCurrent(actor, *health, std::clamp(fraction, 0.f, 1.f) * max);
}

void Kill(Platform& p, uint32_t actorId, uint32_t killerId)
{
  if (actorId == game::kPlayerRef) {
    KillPlayer(p, RE::PlayerCharacter::GetSingleton());
    return;
  }
  auto actor = puppets::Get(actorId);
  if (!actor || g_killed.contains(actorId)) {
    return;
  }
  g_killed.insert(actorId);
  {
    std::lock_guard lock(g_guardMutex);
    g_guarded.erase(actorId);
  }
  // Lets the death through the puppets' KillImpl guard (Puppets.h)
  puppets::AllowKill(actorId);
  // The killer is named only when it is another puppet. With the local
  // player as the killer the engine would award vanilla kill XP (and a
  // possible level-up) and crime for a death the server already scored.
  RE::Actor* killer =
    killerId == game::kPlayerRef ? nullptr : puppets::Get(killerId);
  // KillEssential: SetEssential(false), SetProtected(false), Kill(killer).
  // Puppets of base 0x7 (Player) may carry the essential flag.
  // [verify] the death animation/ragdoll plays on a puppet in either AI
  // mode (puppet-ai "package": AI on with a DoNothing package; or AI off).
  papyrus::CallMethod(actor, "Actor", "KillEssential", nullptr,
                      ScriptObject(killer, "Actor"));
  p.Log("info", std::format("Puppet {:X} killed by {:X}", actorId, killerId));
}

void OnFrame()
{
  // Forget puppets that are gone, then rebuild the clamp set
  std::erase_if(g_killed, [](uint32_t id) { return !puppets::IsPuppet(id); });
  {
    std::lock_guard lock(g_guardMutex);
    g_guarded.clear();
    for (auto& [id, state] : puppets::All()) {
      if (!g_killed.contains(id)) {
        g_guarded.insert(id);
      }
    }
  }

  if (!g_guardPlayer) {
    return;
  }
  // A loaded save brings a fresh player: no deferred kill, alive.
  if (g_gameLoaded.exchange(false)) {
    g_player = {};
  }
  if (g_player.deferredKill || game::Loading()) {
    return;
  }
  auto player = RE::PlayerCharacter::GetSingleton();
  if (player && player->GetParentCell()) {
    // [verify] StartDeferredKill on the player keeps Health <= 0 from
    // killing it (the second guard behind the clamp hook).
    g_player.deferredKill =
      papyrus::CallMethod(player, "Actor", "StartDeferredKill", nullptr);
  }
}
}

void InstallActorValues(Platform& p)
{
  g_guardPlayer = !p.Config().serverIp.empty();
  InstallHooks();

  static LoadSink loadSink;
  if (auto source = RE::TESLoadGameEvent::GetEventSource()) {
    source->RegisterSink(&loadSink);
  }

  p.RegisterNative("setActorValueCurrent", [&p](const Json& a) -> Json {
    SetCurrent(p, a.at(0).get<uint32_t>(), a.at(1).get<uint32_t>(),
               a.at(2).get<float>());
    return nullptr;
  });
  p.RegisterNative("setActorValueMax", [](const Json& a) -> Json {
    SetMax(a.at(0).get<uint32_t>(), a.at(1).get<uint32_t>(),
           a.at(2).get<float>());
    return nullptr;
  });
  p.RegisterNative("setHealthFraction", [](const Json& a) -> Json {
    SetHealthFraction(a.at(0).get<uint32_t>(), a.at(1).get<float>());
    return nullptr;
  });
  p.RegisterNative("killActor", [&p](const Json& a) -> Json {
    uint32_t killer =
      a.size() > 1 && a[1].is_number() ? a[1].get<uint32_t>() : 0;
    Kill(p, a.at(0).get<uint32_t>(), killer);
    return nullptr;
  });
  p.OnFrame([](float) { OnFrame(); });
}

}
