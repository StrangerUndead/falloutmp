// F09/F10/F11 combat on the client: the player's shots, hits and reloads
// go to the script; the server decides all damage; remote shots and hit
// results are replayed as cosmetics.
//
// Capture (the player only; hosted NPC shooters are not reported, see
// below):
//   weaponFired      PlayerCharacter::UseAmmo (Actor vfunc 0xF0): one call
//                    per trigger pull, also for automatic fire and
//                    shotguns (the server knows the pellet count). Origin =
//                    Actor::GetCurrentFireLocation (muzzle), direction =
//                    Actor::GetAimVector; the eye vector when they fail.
//   projectileHit    TESHitEvent: the player's gun or melee hit on a
//                    puppet. A hit can't be mapped to the client's shot id
//                    (TESHitEvent names no projectile reference), so
//                    localShotId is 0 and the client uses its last shot;
//                    projectileIndex counts the hits since that shot
//                    (pellets). Explosions are left to F10.
//   reloadRequested  the player's GUN_STATE turning to kReloading (polled
//                    every frame).
//
// Damage authority (CombatGuard.h):
//   - puppets: every damage-modifier decrease on their actor values is
//     clamped to 0 (Actor::CheckClampDamageModifier, vfunc 0x131) unless
//     the server's values are being written; the engine's reaction to local
//     health damage (HandleHealthDamage, 0x110) is skipped. They never die
//     from local hits; killActor still works.
//   - what puppets fire never hurts: AttackDamageMult 0 on every puppet,
//     and every projectile a puppet shoots is neutralized in its update
//     (damage, explosion, spell cleared; Projectile::UpdateImpl 0xCF on
//     each projectile class). Projectiles that playRemoteShot did not ask
//     for (e.g. from replayed attack graph events) are removed outright.
//   - the local player: health damage whose attacker is a puppet is
//     restored (PlayerCharacter::HandleHealthDamage, 0x110; TESHitEvent as
//     a fallback).
//
// Remote shots (playRemoteShot): Papyrus Weapon.Fire(puppet) on the weapon
// base, so the engine plays the muzzle flash, the fire sound and a real
// tracer/projectile from the puppet's weapon, neutralized as above. The
// origin/direction arguments can't steer Weapon.Fire (the puppet's own
// pose aims it, F01/F02); the direction is kept for hit reactions.
// CommonLibF4 exposes no projectile launch function (and TESObjectWEAP::
// Fire only as an ID without a declaration), so neither a launch with
// bUseOrigin nor a launch block is possible yet.
//
// Hit reactions (playHitReaction): the local player gets screen blood, and
// on criticals a camera shake and a stagger (relayed to others by F02 like
// any graph event). Puppets get a blood impact on the hit limb unless the
// engine already showed one locally; their stagger comes from their owner
// through F02.
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
#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <format>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// --- CombatGuard.h ---------------------------------------------------------
namespace fmp::combat {

namespace {
thread_local int t_serverWrite = 0;
std::mutex g_allowMutex;
std::unordered_map<uint32_t, int> g_allowed;
}

ServerWrite::ServerWrite() noexcept
{
  ++t_serverWrite;
}

ServerWrite::~ServerWrite()
{
  --t_serverWrite;
}

void AllowWrites(uint32_t formId, bool allow)
{
  std::lock_guard lock(g_allowMutex);
  int n = g_allowed[formId] + (allow ? 1 : -1);
  if (n <= 0) {
    g_allowed.erase(formId);
  } else {
    g_allowed[formId] = n;
  }
}

bool WritesAllowed(uint32_t formId)
{
  if (t_serverWrite > 0) {
    return true;
  }
  std::lock_guard lock(g_allowMutex);
  return g_allowed.count(formId) > 0;
}

}

// --- The module ------------------------------------------------------------
namespace fmp::modules {

namespace {
using Flag = RE::HitData::Flag;
using Limb = RE::BGSBodyPartDefs::LIMB_ENUM;

// Protocol limbs (falloutmp-client codes.ts Limb, F11 §4.3)
constexpr int kLimbTorso = 0;
constexpr int kLimbHead = 1;
constexpr int kLimbLeftArm = 2;
constexpr int kLimbRightArm = 3;
constexpr int kLimbLeftLeg = 4;
constexpr int kLimbRightLeg = 5;

// A puppet replays at most one shot per this interval (Papyrus load)
constexpr double kMinReplaySec = 0.03;
// How long a playRemoteShot call lets the puppet launch projectiles
constexpr double kAllowanceSec = 2.0;
// A local engine hit on a puppet this recent already showed its impact
constexpr double kEngineHitSec = 0.4;

double NowSec()
{
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool Normalize(RE::NiPoint3& v)
{
  float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  if (!(len > 1e-4f)) {
    return false;
  }
  v.x /= len;
  v.y /= len;
  v.z /= len;
  return true;
}

// --- Puppets, readable from any thread ---
// Hooks and event sinks may run off the main thread, where the puppet
// registry must not be read. A copy is refreshed every frame.
std::mutex g_puppetMutex;
std::unordered_set<uint32_t> g_puppetIds;

void RefreshPuppets()
{
  std::unordered_set<uint32_t> ids;
  for (auto& [id, st] : puppets::All()) {
    ids.insert(id);
  }
  std::lock_guard lock(g_puppetMutex);
  g_puppetIds.swap(ids);
}

bool IsPuppetId(uint32_t id)
{
  std::lock_guard lock(g_puppetMutex);
  return g_puppetIds.count(id) > 0;
}

bool IsPuppetRef(const RE::TESObjectREFR* ref)
{
  return ref && IsPuppetId(ref->GetFormID());
}

// --- Shared state of capture and reactions ---
std::atomic<uint32_t> g_hitsSinceShot{ 0 };
std::atomic<double> g_playerRestoredAt{ 0.0 };
thread_local double t_puppetClampAt = 0.0;

struct Reactions
{
  uint32_t impactSetId = 0; // impact set of the last weapon seen firing
  RE::NiPoint3 remoteDir{ 0.f, 0.f, -1.f };
  double remoteDirAt = 0.0;
  std::unordered_map<uint32_t, double> engineHitAt; // puppet -> last local hit
  std::unordered_map<uint32_t, double> replayAt;    // puppet -> last replay
};
std::mutex g_reactMutex;
Reactions g_react;

void RememberImpactSet(const RE::BGSImpactDataSet* set)
{
  if (!set) {
    return;
  }
  std::lock_guard lock(g_reactMutex);
  g_react.impactSetId = set->GetFormID();
}

// --- Projectiles of puppets ---
struct Allowance
{
  int count = 0;
  double until = 0;
};
struct Judged
{
  bool allowed = false;
  double at = 0;
};
std::mutex g_projMutex;
std::unordered_map<uint32_t, std::vector<Allowance>> g_allowances; // puppet
std::unordered_map<uint32_t, Judged> g_judged; // projectile reference

bool ConsumeAllowance(uint32_t shooterId, double now)
{
  auto it = g_allowances.find(shooterId);
  if (it == g_allowances.end()) {
    return false;
  }
  auto& list = it->second;
  std::erase_if(list, [now](const Allowance& a) {
    return a.until < now || a.count <= 0;
  });
  if (list.empty()) {
    g_allowances.erase(it);
    return false;
  }
  if (--list.front().count <= 0) {
    list.erase(list.begin());
  }
  if (list.empty()) {
    g_allowances.erase(it);
  }
  return true;
}

void RemoveProjectile(uint32_t id)
{
  auto proj = RE::TESForm::GetFormByID<RE::Projectile>(id);
  if (!proj || proj->IsDisabled()) {
    return;
  }
  auto shooter = proj->shooter.get();
  if (!IsPuppetRef(shooter.get())) {
    return; // the reference id was reused
  }
  proj->Disable();
  proj->SetWantsDelete(true);
}

// Returns false to skip this update of the projectile.
bool OnProjectileUpdate(RE::Projectile* proj)
{
  if (!proj) {
    return true;
  }
  auto shooterPtr = proj->shooter.get();
  auto shooter = shooterPtr.get();
  if (!IsPuppetRef(shooter)) {
    return true;
  }
  // Nothing a puppet fires does damage on this client
  // [verify] Projectile::damage is what the hit uses (vs a recomputation
  // from the weapon instance); AttackDamageMult 0 covers the other case
  proj->damage = 0.f;
  proj->explosion = nullptr;
  proj->spell = nullptr;
  proj->avEffect = nullptr;

  const uint32_t projId = proj->GetFormID();
  const double now = NowSec();
  bool allowed = false;
  bool first = false;
  {
    std::lock_guard lock(g_projMutex);
    auto it = g_judged.find(projId);
    if (it != g_judged.end()) {
      allowed = it->second.allowed;
    } else {
      first = true;
      allowed = ConsumeAllowance(shooter->GetFormID(), now);
      g_judged[projId] = { allowed, now };
    }
  }
  if (allowed) {
    return true;
  }
  // Not from playRemoteShot (replayed attack events, AI): out of range at
  // once, and removed on the main thread.
  // [verify] a projectile with range 0 dies on its next update without
  // impacts, and Disable/SetWantsDelete removes it cleanly
  proj->range = 0.f;
  if (first) {
    Platform::Get().QueueTask([projId] { RemoveProjectile(projId); });
    return false;
  }
  return true;
}

template <std::size_t I>
struct ProjectileUpdate
{
  static void Thunk(RE::Projectile* a_this, float a_delta)
  {
    bool run = true;
    try {
      run = OnProjectileUpdate(a_this);
    } catch (std::exception& e) {
      REX::ERROR("Projectile guard failed: {}", e.what());
    }
    if (run) {
      original(a_this, a_delta);
    }
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

template <std::size_t I>
void HookProjectileClass(REL::ID vtableId)
{
  // [verify] Projectile::UpdateImpl (0xCF) runs every frame for every
  // projectile class hooked here
  REL::Relocation<std::uintptr_t> vtbl{ vtableId };
  ProjectileUpdate<I>::original =
    vtbl.write_vfunc(0xCF, ProjectileUpdate<I>::Thunk);
}

// --- Health ---
void RestoreHealth(RE::Actor* actor, float amount)
{
  auto avs = RE::ActorValue::GetSingleton();
  if (!actor || !avs || !avs->health || !(amount > 0.f)) {
    return;
  }
  actor->RestoreActorValue(*avs->health, amount);
}

// What puppets fire must not hurt: AttackDamageMult 0 (effective value).
// [verify] AttackDamageMult scales the projectile and melee damage of the
// actor in Fallout 4
void NeutralizeAttacks(RE::Actor* puppet)
{
  auto avs = RE::ActorValue::GetSingleton();
  auto info = avs ? avs->attackDamageMult : nullptr;
  if (!puppet || !info) {
    return;
  }
  float current = puppet->GetActorValue(*info);
  if (current != 0.f) {
    puppet->SetBaseActorValue(*info,
                              puppet->GetBaseActorValue(*info) - current);
  }
}

// Actor::CheckClampDamageModifier (0x131): the delta a damage modifier
// change applies to an actor value. Puppets lose nothing from local hits.
// [verify] hits (health and limb conditions) reach the actor value through
// this function, and its result is the delta applied
struct ActorClampDamage
{
  static float Thunk(RE::Actor* a_this, RE::ActorValueInfo& a_info,
                     float a_delta)
  {
    float delta = original(a_this, a_info, a_delta);
    if (delta < 0.f && a_this && IsPuppetId(a_this->GetFormID()) &&
        !combat::WritesAllowed(a_this->GetFormID())) {
      t_puppetClampAt = NowSec();
      return 0.f;
    }
    return delta;
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

// Actor::HandleHealthDamage (0x110) on NPC actors: puppets don't react to
// local hits (no bleedout, kill or combat from local damage).
// [verify] whether the engine calls this after applying the damage (then
// the restore below undoes it when the clamp didn't run) or to apply it
struct ActorHealthDamage
{
  static void Thunk(RE::Actor* a_this, RE::Actor* a_attacker, float a_damage)
  {
    if (a_this && a_attacker && IsPuppetId(a_this->GetFormID()) &&
        !combat::WritesAllowed(a_this->GetFormID())) {
      if (NowSec() - t_puppetClampAt > 0.05) {
        RestoreHealth(a_this, std::fabs(a_damage));
      }
      return;
    }
    original(a_this, a_attacker, a_damage);
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

// PlayerCharacter::HandleHealthDamage (0x110): damage from a puppet (a
// cosmetic replayed shot) is undone before anything reacts to it.
// [verify] as above; and that a_attacker is the shooter for projectiles
struct PlayerHealthDamage
{
  static void Thunk(RE::PlayerCharacter* a_this, RE::Actor* a_attacker,
                    float a_damage)
  {
    if (a_this && a_attacker && IsPuppetId(a_attacker->GetFormID())) {
      g_playerRestoredAt = NowSec();
      RestoreHealth(a_this, std::fabs(a_damage));
      return;
    }
    original(a_this, a_attacker, a_damage);
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

// --- Shots of the player ---
void OnPlayerShot(RE::PlayerCharacter* player,
                  const RE::BGSObjectInstanceT<RE::TESObjectWEAP>& weapon,
                  RE::BGSEquipIndex equipIndex, std::uint32_t shotCount)
{
  if (!player || shotCount == 0 || !weapon.object) {
    return;
  }
  auto weap = weapon.object->As<RE::TESObjectWEAP>();
  if (!weap || !weap->IsGunWeapon()) {
    return; // grenades and mines are F10 explosives
  }
  RE::NiPoint3 eyeOrigin(0.f, 0.f, 0.f);
  RE::NiPoint3 eyeDir(0.f, 0.f, 0.f);
  player->GetEyeVector(eyeOrigin, eyeDir, true);
  // [verify] the fire location is the muzzle of the drawn weapon (first
  // and third person) and the aim vector a direction, at UseAmmo time
  RE::NiPoint3 origin(0.f, 0.f, 0.f);
  if (!player->GetCurrentFireLocation(equipIndex, origin)) {
    origin = eyeOrigin;
  }
  RE::NiPoint3 dir(0.f, 0.f, 0.f);
  player->GetAimVector(dir);
  if (!Normalize(dir)) {
    dir = eyeDir;
    if (!Normalize(dir)) {
      return;
    }
  }

  const RE::BGSImpactDataSet* impacts = weap->weaponData.impactDataSet;
  if (auto inst = weapon.instanceData.get()) {
    auto data = static_cast<const RE::TESObjectWEAP::InstanceData*>(inst);
    if (data->impactDataSet) {
      impacts = data->impactDataSet;
    }
  }
  RememberImpactSet(impacts);

  g_hitsSinceShot = 0;
  Platform::Get().Emit("weaponFired",
                       Json{ { "weaponBaseId", weap->GetFormID() },
                             { "origin", game::ToJson(origin) },
                             { "direction", game::ToJson(dir) } });
}

// [verify] UseAmmo runs once per shot of the player (automatic fire:
// every round; shotguns: once per trigger pull) through the vtable
struct PlayerUseAmmo
{
  static std::uint32_t Thunk(
    RE::PlayerCharacter* a_this,
    const RE::BGSObjectInstanceT<RE::TESObjectWEAP>& a_weapon,
    RE::BGSEquipIndex a_equipIndex, std::uint32_t a_shotCount)
  {
    auto used = original(a_this, a_weapon, a_equipIndex, a_shotCount);
    try {
      OnPlayerShot(a_this, a_weapon, a_equipIndex, a_shotCount);
    } catch (std::exception& e) {
      REX::ERROR("weaponFired failed: {}", e.what());
    }
    return used;
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

// --- Hits ---
int ToProtocolLimb(Limb limb)
{
  switch (limb) {
    case Limb::kHead1:
    case Limb::kHead2:
    case Limb::kEye1:
    case Limb::kLookAt1:
    case Limb::kBrain:
      return kLimbHead;
    case Limb::kLeftArm1:
    case Limb::kLeftArm2:
      return kLimbLeftArm;
    case Limb::kRightArm1:
    case Limb::kRightArm2:
      return kLimbRightArm;
    case Limb::kLeftLeg1:
    case Limb::kLeftLeg2:
    case Limb::kLeftLeg3:
    case Limb::kLeftFoot:
      return kLimbLeftLeg;
    case Limb::kRightLeg1:
    case Limb::kRightLeg2:
    case Limb::kRightLeg3:
    case Limb::kRightFoot:
      return kLimbRightLeg;
    default:
      return kLimbTorso; // torso, pelvis, weapon, none
  }
}

const RE::TESObjectWEAP* HitWeapon(const RE::TESHitEvent& e)
{
  if (auto source = RE::TESForm::GetFormByID(e.sourceFormID)) {
    if (auto weap = source->As<RE::TESObjectWEAP>()) {
      return weap;
    }
  }
  if (e.usesHitData && e.hitData.weapon.object) {
    return e.hitData.weapon.object->As<RE::TESObjectWEAP>();
  }
  return nullptr;
}

void OnHit(const RE::TESHitEvent& e)
{
  auto target = e.target.get();
  auto cause = e.cause.get();
  if (!target || !cause) {
    return;
  }
  const uint32_t targetId = target->GetFormID();
  const uint32_t causeId = cause->GetFormID();
  const bool targetPuppet = IsPuppetId(targetId);
  if (targetPuppet) {
    std::lock_guard lock(g_reactMutex);
    g_react.engineHitAt[targetId] = NowSec();
  }

  // A puppet's (cosmetic) hit on the player: undo its damage, unless the
  // HandleHealthDamage hook already did
  if (targetId == game::kPlayerRef && IsPuppetId(causeId)) {
    float damage = e.usesHitData ? e.hitData.healthDamage : 0.f;
    // [verify] healthDamage is the health the hit took
    if (damage > 0.f && NowSec() - g_playerRestoredAt.load() > 0.25) {
      Platform::Get().QueueTask([damage] {
        RestoreHealth(RE::PlayerCharacter::GetSingleton(), damage);
      });
    }
    return;
  }

  // The player hitting a puppet: a hit claim for the server
  // [verify] cause is the shooter (not the projectile) for gun hits
  if (causeId != game::kPlayerRef || !targetPuppet) {
    return;
  }
  const auto& flags = e.hitData.flags;
  if (e.usesHitData && flags.any(Flag::kExplosion)) {
    return; // F10: explosions are server entities
  }
  auto weap = HitWeapon(e);
  if (!weap) {
    return; // spells, hazards, enchantment hits
  }
  const bool melee =
    !weap->IsGunWeapon() ||
    (e.usesHitData &&
     flags.any(Flag::kMeleeAttack, Flag::kBash, Flag::kTimedBash));
  uint32_t index = 0;
  if (!melee) {
    index = std::min<uint32_t>(g_hitsSinceShot.fetch_add(1), 255u);
  }
  // [verify] damageLimb names the BPTD part hit on the human skeleton
  int limb = e.usesHitData ? ToProtocolLimb(e.hitData.damageLimb.get())
                           : kLimbTorso;
  Platform::Get().Emit("projectileHit", Json{ { "localShotId", 0 },
                                              { "projectileIndex", index },
                                              { "target", targetId },
                                              { "limb", limb } });
}

class HitSink final : public RE::BSTEventSink<RE::TESHitEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::TESHitEvent& e, RE::BSTEventSource<RE::TESHitEvent>*) override
  {
    try {
      OnHit(e);
    } catch (std::exception& ex) {
      REX::ERROR("Hit event failed: {}", ex.what());
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};

// --- Natives ---
void PlayRemoteShot(Platform& p, uint32_t shooterId, uint32_t weaponId,
                    const Json& originJson, const Json& directionJson)
{
  (void)game::Point(originJson); // validated; Weapon.Fire can't take it
  auto dir = game::Point(directionJson);
  auto puppet = puppets::Get(shooterId);
  auto weap = game::Form<RE::TESObjectWEAP>(weaponId);
  if (!puppet || !weap) {
    return; // only puppets replay shots
  }
  const double now = NowSec();
  {
    std::lock_guard lock(g_reactMutex);
    if (Normalize(dir)) {
      g_react.remoteDir = dir;
      g_react.remoteDirAt = now;
    }
    if (weap->weaponData.impactDataSet) {
      g_react.impactSetId = weap->weaponData.impactDataSet->GetFormID();
    }
    auto& last = g_react.replayAt[shooterId];
    if (now - last < kMinReplaySec) {
      return;
    }
    last = now;
  }
  if (puppet->IsDisabled() || puppet->IsDead(false) || !puppet->Get3D()) {
    return; // not visible: nothing to show
  }
  NeutralizeAttacks(puppet);
  int projectiles = 1;
  if (auto ranged = weap->weaponData.rangedData) {
    projectiles = std::max<int>(1, ranged->numProjectiles);
  }
  {
    std::lock_guard lock(g_projMutex);
    g_allowances[shooterId].push_back({ projectiles, now + kAllowanceSec });
  }
  // Weapon.Fire(ObjectReference akSource, Ammo akAmmo = None)
  // [verify] fires from the puppet's drawn weapon along its aim, with
  // muzzle flash, sound and tracer; the projectile's shooter is the puppet
  if (!papyrus::CallMethod(weap, "Weapon", "Fire", nullptr,
                           static_cast<RE::TESObjectREFR*>(puppet),
                           static_cast<RE::TESAmmo*>(nullptr))) {
    p.Log("warn", std::format("Unable to replay a shot of {:X}", shooterId));
  }
}

const char* LimbNode(int limb)
{
  // [verify] node names of the Fallout 4 human skeleton
  switch (limb) {
    case kLimbHead:
      return "Head";
    case kLimbLeftArm:
      return "LArm_ForeArm1";
    case kLimbRightArm:
      return "RArm_ForeArm1";
    case kLimbLeftLeg:
      return "LLeg_Calf";
    case kLimbRightLeg:
      return "RLeg_Calf";
    default:
      return "Chest";
  }
}

void PlayHitReaction(uint32_t targetId, int limb, float total, bool critical)
{
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!player) {
    return;
  }
  if (targetId == game::kPlayerRef || targetId == player->GetFormID()) {
    if (total > 0.f) {
      auto splatters =
        static_cast<std::int32_t>(std::clamp(total / 15.f + 1.f, 1.f, 5.f));
      // Game.TriggerScreenBlood(int aiValue)
      papyrus::CallStatic("Game", "TriggerScreenBlood", nullptr, splatters);
    }
    if (critical) {
      // Game.ShakeCamera(ObjectReference akSource = None,
      //                  float afStrength = 0.5, float afDuration = 0.0)
      papyrus::CallStatic("Game", "ShakeCamera", nullptr,
                          static_cast<RE::TESObjectREFR*>(nullptr), 0.5f,
                          0.3f);
      // [verify] the player's graph staggers on staggerStart with these
      // variables (third person; first person may ignore it)
      player->SetGraphVariableFloat(RE::BSFixedString("staggerMagnitude"),
                                    0.25f);
      player->SetGraphVariableFloat(RE::BSFixedString("staggerDirection"),
                                    0.f);
      player->NotifyAnimationGraphImpl(RE::BSFixedString("staggerStart"));
    }
    return;
  }

  auto target = puppets::Get(targetId);
  if (!target || total <= 0.f || target->IsDisabled() || !target->Get3D()) {
    return; // only puppets; nothing to show for blocked or immune hits
  }
  const double now = NowSec();
  uint32_t setId = 0;
  RE::NiPoint3 pick(0.f, 0.f, -1.f);
  bool havePick = false;
  {
    std::lock_guard lock(g_reactMutex);
    auto hit = g_react.engineHitAt.find(targetId);
    if (hit != g_react.engineHitAt.end() && now - hit->second < kEngineHitSec) {
      return; // the engine showed this hit already
    }
    setId = g_react.impactSetId;
    if (now - g_react.remoteDirAt < 1.0) {
      pick = g_react.remoteDir;
      havePick = true;
    }
  }
  auto set = game::Form<RE::BGSImpactDataSet>(setId);
  if (!set) {
    return; // no weapon seen firing yet: no impact set to use
  }
  if (!havePick) {
    pick = target->GetPosition() - player->GetPosition();
    if (!Normalize(pick)) {
      pick = RE::NiPoint3(0.f, 0.f, -1.f);
    }
  }
  // ObjectReference.PlayImpactEffect(ImpactDataSet akImpactEffect,
  //   string asNodeName = "", float afPickDirX = 0.0, float afPickDirY = 0.0,
  //   float afPickDirZ = -1.0, float afPickLength = 512.0,
  //   bool abApplyNodeRotation = false, bool abUseNodeLocalRotation = false)
  // [verify] the pick ray from the limb node finds flesh (blood) or the
  // surface behind the puppet (exit splatter)
  papyrus::CallMethod(target, "ObjectReference", "PlayImpactEffect", nullptr,
                      set, std::string(LimbNode(limb)), pick.x, pick.y,
                      pick.z, 512.f, false, false);
}

// --- Every frame ---
bool g_wasReloading = false;

void PollReload(Platform& p)
{
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!player) {
    return;
  }
  // [verify] the player's gun state is kReloading for the whole reload
  // animation (first and third person)
  const bool reloading = player->gunState == RE::GUN_STATE::kReloading;
  if (reloading && !g_wasReloading) {
    p.Emit("reloadRequested", Json::object());
  }
  g_wasReloading = reloading;
}

void Prune()
{
  const double now = NowSec();
  {
    std::lock_guard lock(g_projMutex);
    std::erase_if(g_judged,
                  [now](const auto& kv) { return now - kv.second.at > 10.0; });
    for (auto it = g_allowances.begin(); it != g_allowances.end();) {
      std::erase_if(it->second,
                    [now](const Allowance& a) { return a.until < now; });
      it = it->second.empty() ? g_allowances.erase(it) : std::next(it);
    }
  }
  std::lock_guard lock(g_reactMutex);
  std::erase_if(g_react.engineHitAt,
                [now](const auto& kv) { return now - kv.second > 5.0; });
  std::erase_if(g_react.replayAt,
                [now](const auto& kv) { return now - kv.second > 10.0; });
}

void InstallHooks()
{
  static bool installed = false;
  if (installed) {
    return;
  }
  installed = true;

  REL::Relocation<std::uintptr_t> player{ RE::VTABLE::PlayerCharacter[0] };
  PlayerUseAmmo::original = player.write_vfunc(0xF0, PlayerUseAmmo::Thunk);
  PlayerHealthDamage::original =
    player.write_vfunc(0x110, PlayerHealthDamage::Thunk);

  REL::Relocation<std::uintptr_t> actor{ RE::VTABLE::Actor[0] };
  ActorClampDamage::original =
    actor.write_vfunc(0x131, ActorClampDamage::Thunk);
  ActorHealthDamage::original =
    actor.write_vfunc(0x110, ActorHealthDamage::Thunk);

  HookProjectileClass<0>(RE::VTABLE::MissileProjectile[0]);
  HookProjectileClass<1>(RE::VTABLE::BeamProjectile[0]);
  HookProjectileClass<2>(RE::VTABLE::FlameProjectile[0]);
  HookProjectileClass<3>(RE::VTABLE::ConeProjectile[0]);
  HookProjectileClass<4>(RE::VTABLE::GrenadeProjectile[0]);
  HookProjectileClass<5>(RE::VTABLE::ArrowProjectile[0]);
  REX::INFO("Installed the combat hooks");
}
}

void InstallCombat(Platform& p)
{
  InstallHooks();
  puppets::InstallHooks();

  static HitSink hitSink;
  if (auto source = RE::TESHitEvent::GetEventSource()) {
    source->RegisterSink(&hitSink);
  } else {
    p.Log("error", "No hit event source: hits are not reported");
  }

  puppets::OnPostUpdate(
    [](RE::Actor* puppet, float) { NeutralizeAttacks(puppet); });

  p.OnFrame([&p](float) {
    RefreshPuppets();
    PollReload(p);
    if (p.FrameCount() % 300 == 0) {
      Prune();
    }
  });

  p.RegisterNative("playRemoteShot", [&p](const Json& a) -> Json {
    PlayRemoteShot(p, a.at(0).get<uint32_t>(), a.at(1).get<uint32_t>(),
                   a.at(2), a.at(3));
    return nullptr;
  });
  p.RegisterNative("playHitReaction", [](const Json& a) -> Json {
    PlayHitReaction(a.at(0).get<uint32_t>(), a.at(1).get<int>(),
                    a.at(2).get<float>(), a.at(3).get<bool>());
    return nullptr;
  });
}

}
