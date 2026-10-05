// F01 movement natives (docs/falloutmp/features/F01-movement.md).
//
// Owner side: getMovementFo4 reads the player's (and hosted NPCs') transform,
// speed, direction and MoveFlag bits from engine state. Speed comes from
// position deltas kept per actor (the engine's sampled speed stays high when
// running into walls), averaged over ~150 ms.
//
// Remote side: puppets are driven by the network. setActorTransform stores
// a target that is applied right after the puppet's own engine update
// (puppets::OnPostUpdate), so AI and physics can't fight it.
//   "puppet-move": "native"   (default) warp every update;
//                  "papyrus"  ObjectReference.SetPosition/SetAngle ~10 Hz.
//   "puppet-warp": "controller" (default) Actor::SetPosition with the
//                  character controller + Actor::SetHeading;
//                  "reference" SetLocationOnReference/SetAngleOnReference +
//                  Update3DPosition (fallback if the controller warp jitters).
//
// Units: game units, units/s, angles in degrees. yaw is the engine's angle.z
// (0 = +Y/north, clockwise). direction is the horizontal movement direction
// relative to the yaw in [0, 360) (0 forward, 90 right, 180 back, 270 left;
// SkyMP's 360 * Direction). aimPitch is the look pitch in [-90, 90] with the
// engine's sign (angle.x, positive = down); aimHeading is the aim heading
// relative to the yaw in [-180, 180).
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"
#include "Puppets.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <mutex>
#include <numbers>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fmp::modules {

namespace {

// UpdateMovementFo4.flags (falloutmp-client codes.ts MoveFlag,
// skymp5-server fo4/Movement.h)
namespace MoveFlag {
enum : uint32_t
{
  Sneaking = 1 << 0,
  Sprinting = 1 << 1,
  Sighted = 1 << 2,
  WeaponDrawn = 1 << 3,
  InJump = 1 << 4,
  Swimming = 1 << 5,
  InPowerArmor = 1 << 6,
  InFurniture = 1 << 7,
  IsDead = 1 << 8,
  IsBlocking = 1 << 9,
  Encumbered = 1 << 10,
  LightOn = 1 << 11,
  JetpackActive = 1 << 12,
};
}

constexpr double kVelocityWindowMs = 150;  // speed averaged over this
constexpr double kMinVelocityDtMs = 20;
constexpr float kTeleportSpeed = 20000.f;  // faster than this = a teleport
constexpr double kTrackForMs = 3000;       // history while the client asks
constexpr double kEncumberedPollMs = 1000; // Papyrus IsOverEncumbered
constexpr float kMovingSpeed = 5.f;        // below: no direction
// Jetpack: no engine flag; in power armor, airborne and rising for longer
// than a jump rises. [verify] thresholds against a real jetpack burn.
constexpr float kJetpackMinVelZ = 80.f;
constexpr double kJetpackRiseMs = 600;
constexpr double kMarkerDeleteDelayMs = 1000;
constexpr double kFlagAssertMs = 1000;

// RE::GUN_STATE / RE::WEAPON_STATE values, read from the ActorState
// bitfields as unsigned (an enum bitfield of a signed type reads 8 as -8).
constexpr uint32_t kGunSighted = 6, kGunFireSighted = 8;
constexpr uint32_t kWeapWantToDraw = 1, kWeapDrawing = 2, kWeapDrawn = 3;

float SignedDeg(float rad)
{
  float d = game::ToDeg(rad); // [0, 360)
  return d >= 180.f ? d - 360.f : d;
}

float Dist(const RE::NiPoint3& a, const RE::NiPoint3& b)
{
  float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---- owner side ------------------------------------------------------

struct Sample
{
  double t = 0;
  RE::NiPoint3 pos;
};

struct Owned
{
  std::deque<Sample> samples;
  uint32_t space = 0;
  double lastQueryMs = 0;
  double lastEncumberedPollMs = -1e9;
  bool encumbered = false;
  double risingSinceMs = -1;
};

std::unordered_map<uint32_t, Owned> g_owned;

void Record(Owned& o, RE::Actor* actor, double now)
{
  auto pos = actor->GetPosition();
  auto space = game::SpaceOf(actor);
  if (space != o.space) {
    o.samples.clear();
    o.space = space;
    o.risingSinceMs = -1;
  }
  if (!o.samples.empty()) {
    auto& last = o.samples.back();
    double dt = now - last.t;
    if (dt < 1.0) {
      return; // already sampled this frame
    }
    if (Dist(pos, last.pos) / static_cast<float>(dt / 1000.0) >
        kTeleportSpeed) {
      o.samples.clear(); // teleported: don't report a huge speed
    }
  }
  o.samples.push_back({ now, pos });
  // Keep the youngest sample that is at least a window old as the front
  while (o.samples.size() > 2 &&
         now - o.samples[1].t >= kVelocityWindowMs) {
    o.samples.pop_front();
  }
}

RE::NiPoint3 VelocityOf(const Owned& o)
{
  if (o.samples.size() < 2) {
    return {};
  }
  auto& a = o.samples.front();
  auto& b = o.samples.back();
  double dt = b.t - a.t;
  if (dt < kMinVelocityDtMs) {
    return {};
  }
  float k = static_cast<float>(1000.0 / dt);
  return { (b.pos.x - a.pos.x) * k, (b.pos.y - a.pos.y) * k,
           (b.pos.z - a.pos.z) * k };
}

void PollEncumbered(uint32_t id, RE::Actor* actor, Owned& o, double now)
{
  if (now - o.lastEncumberedPollMs < kEncumberedPollMs) {
    return;
  }
  o.lastEncumberedPollMs = now;
  // No engine accessor in CommonLibF4; Actor.IsOverEncumbered() is cheap
  papyrus::CallMethod(actor, "Actor", "IsOverEncumbered",
                      [id](const Json& r) {
                        auto it = g_owned.find(id);
                        if (it != g_owned.end()) {
                          it->second.encumbered =
                            r.is_boolean() && r.get<bool>();
                        }
                      });
}

uint32_t FlagsOf(RE::Actor* a, bool isPlayer, Owned& o, float velZ,
                 double now)
{
  using CS = RE::IMovementState::CHARACTER_STATE;
  uint32_t f = 0;
  // [verify] Actor::IsSneaking (ID 2207655) for the player and NPCs
  if (a->IsSneaking()) {
    f |= MoveFlag::Sneaking;
  }
  if (a->DoGetSprinting()) {
    f |= MoveFlag::Sprinting;
  }
  // [verify] gunState is kSighted/kFireSighted while aiming down sights
  uint32_t gun = static_cast<uint32_t>(a->gunState) & 0xF;
  if (gun == kGunSighted || gun == kGunFireSighted) {
    f |= MoveFlag::Sighted;
  }
  // Drawn from the moment the draw starts, so remote draws start together
  uint32_t weap = static_cast<uint32_t>(a->weaponState) & 0x7;
  if (weap == kWeapWantToDraw || weap == kWeapDrawing || weap == kWeapDrawn) {
    f |= MoveFlag::WeaponDrawn;
  }
  auto cs = a->DoGetCharacterState();
  bool airborne = cs == CS::kJumping || cs == CS::kInAir;
  if (airborne) {
    f |= MoveFlag::InJump; // jumping or falling
  }
  if (a->IsSwimming() || cs == CS::kSwimming) {
    f |= MoveFlag::Swimming;
  }
  bool inPowerArmor = RE::PowerArmor::ActorInPowerArmor(*a);
  if (inPowerArmor) {
    f |= MoveFlag::InPowerArmor;
  }
  if (a->DoGetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
    f |= MoveFlag::InFurniture;
  }
  auto life = static_cast<RE::ACTOR_LIFE_STATE>(a->lifeState);
  if (life == RE::ACTOR_LIFE_STATE::kDying ||
      life == RE::ACTOR_LIFE_STATE::kDead) {
    f |= MoveFlag::IsDead;
  }
  // [verify] wantBlocking is set while the block button is held
  if (a->wantBlocking) {
    f |= MoveFlag::IsBlocking;
  }
  if (o.encumbered) {
    f |= MoveFlag::Encumbered;
  }
  if (isPlayer) {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (player && player->IsPipboyLightOn()) {
      f |= MoveFlag::LightOn;
    }
  }
  if (inPowerArmor && (airborne || cs == CS::kFlying) &&
      velZ > kJetpackMinVelZ) {
    if (o.risingSinceMs < 0) {
      o.risingSinceMs = now;
    }
    if (now - o.risingSinceMs >= kJetpackRiseMs) {
      f |= MoveFlag::JetpackActive;
    }
  } else {
    o.risingSinceMs = -1;
  }
  return f;
}

void AimOf(RE::Actor* a, bool isPlayer, float yawRad, float& pitchDeg,
           float& headingDeg)
{
  // The player's camera pitch is the actor's X angle
  pitchDeg = std::clamp(SignedDeg(a->data.angle.x), -90.f, 90.f);
  headingDeg = 0;
  // GetAimVector reads process data: only with a high process and 3D
  auto proc = a->currentProcess;
  if (!proc || !proc->high || !a->Get3D()) {
    return;
  }
  RE::NiPoint3 v;
  a->GetAimVector(v); // [verify] unit vector of the current aim
  float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  if (!(len > 1e-3f)) {
    return;
  }
  if (!isPlayer) {
    float s = std::clamp(-v.z / len, -1.f, 1.f);
    pitchDeg = std::asin(s) * 180.f / std::numbers::pi_v<float>;
  }
  headingDeg = SignedDeg(std::atan2(v.x, v.y) - yawRad);
}

Json GetMovement(Platform& p, uint32_t actorId)
{
  if (game::Loading() || puppets::IsPuppet(actorId)) {
    return nullptr;
  }
  bool isPlayer = actorId == game::kPlayerRef;
  auto actor = game::ActorOf(actorId);
  if (!actor || !actor->GetParentCell() || actor->IsDisabled() ||
      actor->IsDeleted()) {
    return nullptr;
  }
  if (!isPlayer && !actor->Get3D()) {
    return nullptr; // not loaded
  }
  uint32_t space = game::SpaceOf(actor);
  if (!space) {
    return nullptr;
  }
  double now = p.NowMs();
  auto& o = g_owned[actorId];
  o.lastQueryMs = now;
  Record(o, actor, now); // no-op if the frame callback sampled already
  PollEncumbered(actorId, actor, o, now);

  auto vel = VelocityOf(o);
  float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
  float yawRad = actor->data.angle.z;
  float direction = 0;
  if (speed >= kMovingSpeed) {
    direction = game::ToDeg(std::atan2(vel.x, vel.y) - yawRad);
  }
  float aimPitch = 0, aimHeading = 0;
  AimOf(actor, isPlayer, yawRad, aimPitch, aimHeading);
  return { { "worldOrCell", space },
           { "pos", game::ToJson(actor->GetPosition()) },
           { "yaw", game::ToDeg(yawRad) },
           { "aimPitch", aimPitch },
           { "aimHeading", aimHeading },
           { "speed", speed },
           { "direction", direction },
           { "velZ", vel.z },
           { "flags", FlagsOf(actor, isPlayer, o, vel.z, now) } };
}

// ---- remote side -----------------------------------------------------

enum class MoveMode
{
  Native,
  Papyrus
};
MoveMode g_moveMode = MoveMode::Native;
bool g_controllerWarp = true;

struct Target
{
  RE::NiPoint3 pos;
  float yawRad = 0;
  uint64_t postUpdateFrame = 0; // last frame the post-update applied it
};

// Post-update callbacks may run outside the main thread (actor updates):
// the targets are shared under a lock.
std::mutex g_targetMutex;
std::unordered_map<uint32_t, Target> g_targets;

struct FlagState
{
  double nextAssertMs = 0;
};
std::unordered_map<uint32_t, FlagState> g_flagState;

struct PendingMarker
{
  uint32_t id = 0;
  double notBeforeMs = 0;
};
std::vector<PendingMarker> g_pendingMarkers;

void Warp(RE::Actor* a, const RE::NiPoint3& pos, float yawRad)
{
  bool loaded = a->Get3D() != nullptr;
  if (g_controllerWarp && loaded) {
    // [verify] moves the character controller with the actor (no physics
    // pull-back, no jitter on stairs/slopes)
    a->SetPosition(pos, true);
    a->SetHeading(yawRad);
    return;
  }
  // Without 3D there is no controller: the reference data is enough
  a->SetLocationOnReference(pos);
  a->SetAngleOnReference({ 0.f, 0.f, yawRad });
  if (loaded) {
    a->Update3DPosition(true);
  }
}

void SetTarget(uint32_t id, const RE::NiPoint3& pos, float yawRad)
{
  std::lock_guard lock(g_targetMutex);
  auto& t = g_targets[id];
  t.pos = pos;
  t.yawRad = yawRad;
}

bool Dead(RE::Actor* a)
{
  auto life = static_cast<RE::ACTOR_LIFE_STATE>(a->lifeState);
  return life == RE::ACTOR_LIFE_STATE::kDying ||
         life == RE::ACTOR_LIFE_STATE::kDead;
}

// After the puppet's engine update: hold it at the newest target. A dead
// puppet is left to its ragdoll.
void ApplyTarget(Platform& p, RE::Actor* a)
{
  if (Dead(a)) {
    return;
  }
  Target t;
  {
    std::lock_guard lock(g_targetMutex);
    auto it = g_targets.find(a->GetFormID());
    if (it == g_targets.end()) {
      return;
    }
    it->second.postUpdateFrame = p.FrameCount();
    t = it->second;
  }
  Warp(a, t.pos, t.yawRad);
}

void DeleteRef(uint32_t id)
{
  auto ref = game::Form<RE::TESObjectREFR>(id);
  if (!ref) {
    return;
  }
  ref->Disable();
  ref->SetWantsDelete(true);
  papyrus::CallMethod(ref, "ObjectReference", "Delete", nullptr);
}

void TeleportPlayer(Platform& p, const RE::NiPoint3& pos, float yawDeg,
                    uint32_t worldOrCell, RE::TESWorldSpace* world,
                    RE::TESObjectCELL* interior)
{
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!player) {
    return;
  }
  // MoveTo loads the destination; it needs a reference to move to, so a
  // heading marker is placed there and deleted once the move is done.
  constexpr uint32_t kXMarkerHeading = 0x34;
  auto marker =
    game::CreateRef(kXMarkerHeading, pos, yawDeg, world, interior);
  if (!marker) {
    p.Log("error", "Unable to place the teleport marker");
    return;
  }
  uint32_t markerId = marker->GetFormID();
  // The done callback runs on the main thread; the marker goes after the
  // loading screen, in case the engine still reads it.
  auto done = [&p, markerId](const Json&) {
    g_pendingMarkers.push_back({ markerId, p.NowMs() + kMarkerDeleteDelayMs });
  };
  if (!papyrus::CallMethod(player, "ObjectReference", "MoveTo", done, marker,
                           0.f, 0.f, 0.f, true)) {
    p.Log("error", "ObjectReference.MoveTo could not be dispatched");
    DeleteRef(markerId);
    return;
  }
  g_owned.erase(game::kPlayerRef); // speed history restarts there
  p.Log("info", std::format("Moving to {:.0f} {:.0f} {:.0f} in {:X}", pos.x,
                            pos.y, pos.z, worldOrCell));
}

void Teleport(Platform& p, uint32_t refId, const Json& posJson, float yawDeg,
              uint32_t worldOrCell)
{
  RE::TESWorldSpace* world;
  RE::TESObjectCELL* interior;
  if (!game::ResolveSpace(worldOrCell, world, interior)) {
    p.Log("warn", std::format("teleport of {:X}: unknown space {:X}", refId,
                              worldOrCell));
    return;
  }
  auto pos = game::Point(posJson);
  if (refId == game::kPlayerRef) {
    TeleportPlayer(p, pos, yawDeg, worldOrCell, world, interior);
    return;
  }
  // Puppets and hosted NPCs: moved directly. Only actors, never arbitrary
  // references a script names.
  auto actor = game::ActorOf(refId);
  if (!actor || actor->IsDeleted()) {
    p.Log("warn", std::format("teleport of {:X}: not a loaded actor", refId));
    return;
  }
  float yawRad = game::ToRad(yawDeg);
  if (game::SpaceOf(actor) != worldOrCell) {
    actor->MoveRefToNewSpace(interior, world);
  }
  actor->SetLocationOnReference(pos);
  actor->SetAngleOnReference({ 0.f, 0.f, yawRad });
  if (actor->Get3D()) {
    Warp(actor, pos, yawRad); // the character controller follows
  }
  if (puppets::IsPuppet(refId)) {
    // Hold it there until the next setActorTransform
    SetTarget(refId, pos, yawRad);
  } else {
    g_owned.erase(refId);
  }
}

void SetTransform(Platform& p, uint32_t refId, const Json& posJson,
                  float yawDeg)
{
  auto ref = puppets::Get(refId);
  if (!ref) {
    return; // only puppets are driven by the network
  }
  auto pos = game::Point(posJson);
  if (g_moveMode == MoveMode::Papyrus) {
    if (p.FrameCount() % 6 == 0) { // ~10 Hz: Papyrus calls are queued
      papyrus::CallMethod(ref, "ObjectReference", "SetPosition", nullptr,
                          pos.x, pos.y, pos.z);
      papyrus::CallMethod(ref, "ObjectReference", "SetAngle", nullptr, 0.f,
                          0.f, yawDeg);
    }
    return;
  }
  // Applied after the puppet's next engine update (or by the frame
  // callback if the engine doesn't update it)
  SetTarget(refId, pos, game::ToRad(yawDeg));
}

// Engine state that follows the flags cheaply. Locomotion, sighted and
// aim poses come from the animation module's graph variables.
void ApplyFlags(RE::Actor* a, uint32_t flags)
{
  if (!a->currentProcess || !a->Get3D()) {
    return;
  }
  bool wantSneak = (flags & MoveFlag::Sneaking) != 0;
  if (a->IsSneaking() != wantSneak) {
    a->SetSneaking(wantSneak); // [verify] works on an AI-driven puppet
  }
  if (flags & MoveFlag::IsDead) {
    return; // death belongs to the actor values / combat modules
  }
  bool wantDrawn = (flags & MoveFlag::WeaponDrawn) != 0;
  uint32_t weap = static_cast<uint32_t>(a->weaponState) & 0x7;
  bool settled = weap == 0 || weap == kWeapDrawn; // not mid draw/sheathe
  if (settled && (weap == kWeapDrawn) != wantDrawn) {
    // [verify] plays the draw/sheathe animation on a puppet
    a->DrawWeaponMagicHands(wantDrawn);
  }
}

void SetFlags(Platform& p, uint32_t refId, uint32_t flags)
{
  auto st = puppets::Find(refId);
  auto actor = puppets::Get(refId);
  if (!st || !actor) {
    return;
  }
  st->moveFlags = flags;
  ApplyFlags(actor, flags);
  g_flagState[refId].nextAssertMs = p.NowMs() + kFlagAssertMs;
}

void SetAim(uint32_t refId, float pitch, float heading)
{
  // Stored for other modules (combat replay); the aim pose itself is the
  // AimPitchCurrent/AimHeadingCurrent graph variables (animation module)
  if (auto st = puppets::Find(refId)) {
    st->aimPitch = pitch;
    st->aimHeading = heading;
  }
}

void Tick(Platform& p)
{
  double now = p.NowMs();
  bool loading = game::Loading();

  // Owner history, sampled every frame for the actors the client asks
  // about (the player update runs this before the client's tick)
  for (auto it = g_owned.begin(); it != g_owned.end();) {
    if (now - it->second.lastQueryMs > kTrackForMs) {
      it = g_owned.erase(it);
      continue;
    }
    auto actor = game::ActorOf(it->first);
    if (loading || !actor || !actor->GetParentCell()) {
      it->second.samples.clear();
    } else {
      Record(it->second, actor, now);
    }
    ++it;
  }

  // Teleport markers whose move is done
  if (!loading) {
    std::erase_if(g_pendingMarkers, [now](const PendingMarker& m) {
      if (now < m.notBeforeMs) {
        return false;
      }
      DeleteRef(m.id);
      return true;
    });
  }

  // Drops targets of deleted puppets. Puppets the engine hasn't updated
  // for two frames (low process level) are moved from here instead.
  uint64_t frame = p.FrameCount();
  std::vector<std::pair<uint32_t, Target>> stale;
  {
    std::lock_guard lock(g_targetMutex);
    for (auto it = g_targets.begin(); it != g_targets.end();) {
      if (!puppets::IsPuppet(it->first)) {
        it = g_targets.erase(it);
        continue;
      }
      auto& t = it->second;
      if (t.postUpdateFrame + 2 < frame) {
        stale.emplace_back(it->first, t);
      }
      ++it;
    }
  }
  if (g_moveMode == MoveMode::Native && !loading) {
    for (auto& [id, t] : stale) {
      auto actor = puppets::Get(id);
      if (actor && actor->GetParentCell() && !Dead(actor)) {
        Warp(actor, t.pos, t.yawRad);
      }
    }
  }
  // Re-assert sneak / weapon state now and then: puppet AI may change it
  for (auto it = g_flagState.begin(); it != g_flagState.end();) {
    auto st = puppets::Find(it->first);
    auto actor = puppets::Get(it->first);
    if (!st || !actor) {
      it = g_flagState.erase(it);
      continue;
    }
    if (!loading && now >= it->second.nextAssertMs) {
      it->second.nextAssertMs = now + kFlagAssertMs;
      ApplyFlags(actor, st->moveFlags);
    }
    ++it;
  }
}
}

void InstallMovement(Platform& p)
{
  auto& cfg = p.Config();
  g_moveMode =
    cfg.puppetMove == "papyrus" ? MoveMode::Papyrus : MoveMode::Native;
  try {
    g_controllerWarp =
      cfg.raw.value("puppet-warp", std::string("controller")) != "reference";
  } catch (std::exception&) {
    REX::WARN("FalloutMP.json: 'puppet-warp' must be a string");
  }
  REX::INFO("Puppet movement: {} ({})", cfg.puppetMove,
            g_controllerWarp ? "controller warp" : "reference warp");
  puppets::InstallHooks();

  p.RegisterNative("getMovementFo4", [&p](const Json& a) -> Json {
    return GetMovement(p, a.at(0).get<uint32_t>());
  });
  p.RegisterNative("teleportActor", [&p](const Json& a) -> Json {
    Teleport(p, a.at(0).get<uint32_t>(), a.at(1), a.at(2).get<float>(),
             a.at(3).get<uint32_t>());
    return nullptr;
  });
  p.RegisterNative("setActorTransform", [&p](const Json& a) -> Json {
    SetTransform(p, a.at(0).get<uint32_t>(), a.at(1), a.at(2).get<float>());
    return nullptr;
  });
  p.RegisterNative("setMovementFlags", [&p](const Json& a) -> Json {
    SetFlags(p, a.at(0).get<uint32_t>(), a.at(1).get<uint32_t>());
    return nullptr;
  });
  p.RegisterNative("setAimAngles", [](const Json& a) -> Json {
    SetAim(a.at(0).get<uint32_t>(), a.at(1).get<float>(),
           a.at(2).get<float>());
    return nullptr;
  });

  p.OnFrame([&p](float) { Tick(p); });

  puppets::OnPostUpdate([&p](RE::Actor* a, float) {
    if (g_moveMode == MoveMode::Native) {
      ApplyTarget(p, a);
    }
  });
}

}
