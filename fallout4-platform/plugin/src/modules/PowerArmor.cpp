// F17 power armor natives and the player's enter/exit requests.
//
// Natives:
//   playPowerArmorEnter(frame)   activates the frame for the player (the
//                                vanilla walk-in and race switch) and
//                                resolves when the player is in power armor
//                                (true) or after a timeout (false)
//   playPowerArmorExit()         resolves when the player is out; triggers
//                                an exit only when the player is still in
//   setInPowerArmor              snap without animation (Actor.
//                                SwitchToPowerArmor); puppets without a
//                                local frame get a proxy frame that is never
//                                saved
//   applyPowerArmorVisual        pieces of a puppet's power armor (the
//                                player's pieces come with its inventory)
//   setFusionCoreCharge          the player's PowerArmorBattery value
//   setJetpackEnabled            recorded only (see SetJetpack)
//
// Capture:
//   powerArmorEnterRequested  TESFurniture::Activate (vfunc 0x40) of a power
//                             armor frame by the player is blocked and
//                             reported; the server answers and the client
//                             calls playPowerArmorEnter. If the hook does not
//                             see the activation, the player's change to "in
//                             power armor" is reported after the fact (the
//                             server confirms or snaps the player back out).
//   powerArmorExitRequested   the player's change to "out of power armor"
//                             without a native asking for it (the vanilla
//                             exit cannot be blocked; the server confirms or
//                             snaps the player back in)
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "ItemKeys.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"
#include "Puppets.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <optional>
#include <unordered_map>
#include <vector>

namespace fmp::modules {

namespace {
// FurnitureTypePowerArmor, on every power armor frame (FURN)
constexpr uint32_t kPowerArmorFrameKeyword = 0x3430B;
// Base of a proxy frame when the server sends no frame base.
// [verify] 0x2079E is the empty power armor frame furniture (FURN)
constexpr uint32_t kEmptyFrameFurniture = 0x2079E;
constexpr double kEnterTimeoutMs = 15000;
constexpr double kExitTimeoutMs = 10000;
constexpr double kSnapTimeoutMs = 10000;
// After a blocked activation the vanilla entry may still happen (hook not
// effective); the polling then does not report the same entry twice.
constexpr double kHookRequestWindowMs = 20000;
constexpr uint64_t kAllowFrames = 180; // ~3 s at 60 fps

bool InPowerArmor(RE::Actor* actor)
{
  return actor && RE::PowerArmor::ActorInPowerArmor(*actor);
}

bool IsPowerArmorFrame(RE::TESObjectREFR* ref)
{
  auto base = ref ? ref->GetObjectReference() : nullptr;
  auto furniture = base ? base->As<RE::TESFurniture>() : nullptr;
  return furniture && furniture->HasKeywordID(kPowerArmorFrameKeyword);
}

// Actor.SwitchToPowerArmor(frame): instant enter. None = instant exit.
// [verify] SwitchToPowerArmor(None) takes the actor out of power armor
void SwitchTo(RE::Actor* actor, RE::TESObjectREFR* frame)
{
  papyrus::CallMethod(actor, "Actor", "SwitchToPowerArmor", nullptr, frame);
}

void RemoveRef(uint32_t id)
{
  auto ref = id ? RE::TESForm::GetFormByID<RE::TESObjectREFR>(id) : nullptr;
  if (!ref) {
    return;
  }
  ref->Disable();
  ref->SetWantsDelete(true);
  papyrus::CallMethod(ref, "ObjectReference", "Delete", nullptr);
}

// --- The player ------------------------------------------------------------

struct Transition
{
  uint32_t asyncId = 0;
  bool enter = false;
  uint32_t frame = 0;
  double startMs = 0;
};

struct Snap
{
  bool inside = false;
  double untilMs = 0;
};

std::optional<Transition> g_transition;
std::optional<Snap> g_snap;
bool g_tracked = false;
bool g_wasInside = false;
double g_hookRequestMs = -kHookRequestWindowMs;
uint32_t g_hookRequestFrame = 0;
bool g_jetpackEnabled = true;

// Read by the activation hook (main thread in practice; atomics anyway)
std::atomic<uint32_t> g_allowFrame{ 0 };
std::atomic<uint64_t> g_allowUntil{ 0 };

void Finish(Platform& p, bool ok)
{
  if (!g_transition) {
    return;
  }
  p.Resolve(g_transition->asyncId, ok);
  g_transition.reset();
  g_allowFrame = 0;
}

Json PlayEnter(Platform& p, uint32_t frameId)
{
  uint32_t id = p.NewAsyncId();
  Finish(p, false); // a newer request supersedes an unfinished one
  auto player = RE::PlayerCharacter::GetSingleton();
  auto frame = frameId == game::kPlayerRef ? nullptr : game::Ref(frameId);
  if (!player || !IsPowerArmorFrame(frame) || game::Loading()) {
    p.Log("warn",
          std::format("playPowerArmorEnter: {:X} is not a power "
                      "armor frame",
                      frameId));
    p.Resolve(id, false);
    return Platform::Pending(id);
  }
  if (InPowerArmor(player)) {
    p.Resolve(id, true);
    return Platform::Pending(id);
  }
  g_transition = Transition{ id, true, frameId, p.NowMs() };
  g_allowFrame = frameId;
  g_allowUntil = p.FrameCount() + kAllowFrames;
  // The same activation as the player's own (walk-in animation, pieces
  // and core move from the frame). [verify] fromScript = false plays the
  // full entry sequence; the return value is logged only
  bool handled = frame->ActivateRef(player, nullptr, 1, false, false, false);
  p.Log(
    "info",
    std::format("Entering power armor {:X} (activate: {})", frameId, handled));
  return Platform::Pending(id);
}

Json PlayExit(Platform& p)
{
  uint32_t id = p.NewAsyncId();
  Finish(p, false);
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!player || game::Loading()) {
    p.Resolve(id, false);
    return Platform::Pending(id);
  }
  // Usually the vanilla exit has already happened: the exit request is
  // reported once the player is out (it cannot be blocked).
  if (!InPowerArmor(player)) {
    p.Resolve(id, true);
    return Platform::Pending(id);
  }
  g_transition = Transition{ id, false, 0, p.NowMs() };
  // No engine entry for the animated exit in CommonLibF4: instant exit.
  SwitchTo(player, nullptr);
  return Platform::Pending(id);
}

void PlayerFrame(Platform& p)
{
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!player) {
    return;
  }
  if (game::Loading()) {
    Finish(p, false);
    g_snap.reset();
    g_tracked = false; // re-baseline after the load
    return;
  }
  double now = p.NowMs();
  bool inside = InPowerArmor(player);
  // Changes we asked for are not player requests
  bool own = g_transition.has_value() || g_snap.has_value();

  if (g_transition) {
    if (inside == g_transition->enter) {
      // [verify] ActorInPowerArmor turns true once the entry sequence is
      // complete (not at its start)
      Finish(p, true);
    } else if (now - g_transition->startMs >
               (g_transition->enter ? kEnterTimeoutMs : kExitTimeoutMs)) {
      p.Log("warn",
            std::format("Power armor {} timed out",
                        g_transition->enter ? "entry" : "exit"));
      Finish(p, false);
    }
  }
  if (g_snap && (inside == g_snap->inside || now > g_snap->untilMs)) {
    g_snap.reset();
  }

  if (!g_tracked) {
    g_tracked = true;
    g_wasInside = inside;
    return;
  }
  if (inside == g_wasInside) {
    return;
  }
  g_wasInside = inside;
  if (own) {
    return;
  }
  if (inside) {
    auto frame = player->lastUsedPowerArmor.get();
    uint32_t frameId = frame ? frame->GetFormID() : g_hookRequestFrame;
    if (now - g_hookRequestMs < kHookRequestWindowMs &&
        frameId == g_hookRequestFrame) {
      return; // already requested by the activation hook
    }
    p.Emit("powerArmorEnterRequested", Json{ { "frame", frameId } });
  } else {
    p.Emit("powerArmorExitRequested", Json::object());
  }
}

// TESFurniture::Activate (TESForm vfunc 0x40): the player activating a
// power armor frame is reported instead of entering.
// [verify] power armor frames are entered through this vfunc
struct FurnitureActivate
{
  static bool Thunk(RE::TESFurniture* a_this, RE::TESObjectREFR* a_item,
                    RE::TESObjectREFR* a_action,
                    RE::TESBoundObject* a_objectToGet, std::int32_t a_count)
  {
    auto& p = Platform::Get();
    if (a_item && a_action && a_action->IsPlayerRef() && a_this &&
        a_this->HasKeywordID(kPowerArmorFrameKeyword)) {
      uint32_t frameId = a_item->GetFormID();
      bool allowed = g_allowFrame.load() == frameId &&
        p.FrameCount() <= g_allowUntil.load();
      if (!allowed) {
        double now = p.NowMs();
        // Activation repeats while the key is held: one request a second
        if (frameId != g_hookRequestFrame || now - g_hookRequestMs > 1000) {
          p.Emit("powerArmorEnterRequested", Json{ { "frame", frameId } });
          g_hookRequestFrame = frameId;
          g_hookRequestMs = now;
        }
        return false;
      }
    }
    return original(a_this, a_item, a_action, a_objectToGet, a_count);
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

// --- Puppets ---------------------------------------------------------------

struct PuppetPowerArmor
{
  uint32_t frame = 0; // the frame the puppet enters (world or proxy)
  uint32_t proxy = 0; // our proxy frame (0 = none)
  bool proxyFilled = false;
  bool wantInside = false;
  uint64_t switchAtFrame = 0; // > 0: SwitchToPowerArmor due
  uint32_t frameBaseId = 0;
  std::vector<items::Key> pieces;  // wanted (applyPowerArmorVisual)
  std::vector<items::Key> applied; // on the puppet
  bool unpowered = false;
};

std::unordered_map<uint32_t, PuppetPowerArmor> g_puppets;

bool Contains(const std::vector<items::Key>& keys, const items::Key& k)
{
  return std::any_of(keys.begin(), keys.end(), [&](const items::Key& x) {
    return items::SameStack(x, k);
  });
}

void Equip(RE::Actor* actor, const items::Key& key)
{
  auto mgr = RE::ActorEquipManager::GetSingleton();
  auto inst = items::Find(actor, key, false);
  if (!mgr || !inst || !inst->object) {
    return;
  }
  RE::BGSObjectInstance object(inst->object,
                               items::InstanceDataOf(actor, *inst));
  uint32_t id = actor->GetFormID();
  items::ApplyGuard guard;
  items::NotifyOwnChange(id, true);
  mgr->EquipObject(actor, object, inst->stackIndex, 1, nullptr, false, true,
                   false, true, false);
  items::NotifyOwnChange(id, false);
}

// Pieces changed while the puppet is inside: swap the items it wears.
// items::Add / Remove hold the ApplyGuard themselves.
void SyncPieces(RE::Actor* actor, PuppetPowerArmor& st)
{
  for (auto& k : st.applied) {
    if (!Contains(st.pieces, k)) {
      items::Remove(actor, k, 1, true); // unequips it too
    }
  }
  for (auto& k : st.pieces) {
    if (!Contains(st.applied, k)) {
      if (items::Add(actor, k, 1, true)) {
        Equip(actor, k);
      }
    }
  }
  st.applied = st.pieces;
}

RE::TESObjectREFR* PlaceProxy(RE::Actor* actor, uint32_t baseId)
{
  RE::TESWorldSpace* world;
  RE::TESObjectCELL* interior;
  if (!game::ResolveSpace(game::SpaceOf(actor), world, interior)) {
    return nullptr;
  }
  auto ref =
    game::CreateRef(baseId, actor->GetPosition(),
                    game::ToDeg(actor->data.angle.z), world, interior);
  if (ref && !IsPowerArmorFrame(ref)) {
    RemoveRef(ref->GetFormID()); // not a frame base
    return nullptr;
  }
  return ref;
}

void SetInPowerArmor(Platform& p, uint32_t actorId, uint32_t frameId,
                     bool inside)
{
  auto actor = game::ActorOf(actorId);
  bool isPlayer = actorId == game::kPlayerRef;
  if (!actor || (!isPlayer && !puppets::IsPuppet(actorId))) {
    return; // only the player and puppets
  }
  if (isPlayer) {
    if (InPowerArmor(actor) == inside) {
      return;
    }
    RE::TESObjectREFR* frame = nullptr;
    if (inside) {
      frame = game::Ref(frameId);
      if (!IsPowerArmorFrame(frame)) {
        p.Log("warn", std::format("setInPowerArmor: no frame {:X}", frameId));
        return;
      }
    }
    g_snap = Snap{ inside, p.NowMs() + kSnapTimeoutMs };
    SwitchTo(actor, frame);
    return;
  }
  auto& st = g_puppets[actorId];
  st.wantInside = inside;
  // Next frame: applyPowerArmorVisual (same script tick) fills the proxy
  st.switchAtFrame = p.FrameCount() + 1;
  if (!inside) {
    return;
  }
  auto world = frameId ? game::Ref(frameId) : nullptr;
  if (IsPowerArmorFrame(world) && frameId != st.proxy) {
    // The streamed-in frame: the engine hides it while worn
    if (st.proxy) {
      RemoveRef(st.proxy);
      st.proxy = 0;
    }
    st.frame = frameId;
    return;
  }
  if (!st.proxy) {
    uint32_t base = st.frameBaseId ? st.frameBaseId : kEmptyFrameFurniture;
    auto proxy = PlaceProxy(actor, base);
    if (!proxy) {
      p.Log("warn",
            std::format("No proxy frame {:X} for puppet {:X}", base, actorId));
      st.frame = 0;
      return;
    }
    st.proxy = st.frame = proxy->GetFormID();
    st.proxyFilled = false;
  }
}

void ApplyVisual(uint32_t actorId, const Json& state)
{
  // The player's pieces and core come with its inventory (SetInventoryFo4)
  if (!puppets::IsPuppet(actorId)) {
    return;
  }
  auto& st = g_puppets[actorId];
  st.frameBaseId = state.value("frameBaseId", 0u);
  st.unpowered = state.value("unpowered", false);
  std::vector<items::Key> pieces;
  for (auto& piece : state.at("pieces")) {
    auto key = items::FromJson(piece.at("item"));
    if (key.baseId) {
      pieces.push_back(std::move(key));
    }
  }
  st.pieces = std::move(pieces);
  auto actor = puppets::Get(actorId);
  if (actor && st.wantInside && st.switchAtFrame == 0 && InPowerArmor(actor)) {
    SyncPieces(actor, st);
  }
}

void PuppetsFrame(Platform& p)
{
  for (auto it = g_puppets.begin(); it != g_puppets.end();) {
    auto& st = it->second;
    auto actor = puppets::Get(it->first);
    if (!actor) {
      RemoveRef(st.proxy);
      it = g_puppets.erase(it);
      continue;
    }
    bool inside = InPowerArmor(actor);
    if (st.switchAtFrame && p.FrameCount() >= st.switchAtFrame) {
      st.switchAtFrame = 0;
      if (st.wantInside && !inside) {
        if (auto frame = game::Ref(st.frame)) {
          if (st.proxy && st.proxy == st.frame && !st.proxyFilled) {
            for (auto& k : st.pieces) {
              items::Add(frame, k, 1, true);
            }
            st.proxyFilled = true;
          }
          // [verify] the frame's pieces move onto the puppet and the puppet
          // switches to PowerArmorRace (F17 §8: proxy frames may need to be
          // persistent)
          SwitchTo(actor, frame);
          st.applied = st.pieces;
        }
      } else if (!st.wantInside && inside) {
        SwitchTo(actor, nullptr);
      }
    }
    // The proxy drops at the puppet's feet on exit: remove it
    if (!st.wantInside && st.proxy && !inside) {
      RemoveRef(st.proxy);
      st.proxy = st.frame = 0;
      st.proxyFilled = false;
      st.applied.clear();
    }
    ++it;
  }
}

// --- Fusion core and jetpack -----------------------------------------------

// The worn core's charge (0..1) as the PowerArmorBattery value.
// [verify] PowerArmorBattery counts charge points with a full core at
// fNewBatteryCapacity, and the engine drains it as damage
void SetCoreCharge(float charge)
{
  auto player = RE::PlayerCharacter::GetSingleton();
  auto avs = RE::ActorValue::GetSingleton();
  auto info = avs ? avs->powerArmorBattery : nullptr;
  if (!player || !info) {
    return;
  }
  float capacity = 100.f;
  if (auto s = RE::PowerArmor::GetNewBatteryCapacity(); s &&
      s->GetType() == RE::Setting::SETTING_TYPE::kFloat &&
      s->GetFloat() > 0.f) {
    capacity = s->GetFloat();
  }
  float target = std::clamp(charge, 0.f, 1.f) * capacity;
  float current = player->GetActorValue(*info);
  if (target < current) {
    player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *info,
                          target - current);
  } else if (target > current) {
    player->RestoreActorValue(*info, target - current);
    float after = player->GetActorValue(*info);
    if (after < target) {
      // Damage healed and still short: raise the base
      player->SetBaseActorValue(
        *info, player->GetBaseActorValue(*info) + (target - after));
    }
  }
}

// The server allows the jetpack only for a jetpack torso, a powered suit
// and AP > 0. The vanilla rules match while the pieces (inventory) and the
// core charge (setFusionCoreCharge) mirror the server, so this only records
// the flag. [verify] no local jetpack use while disabled; a hard block
// would need a jump-disable input layer limited to jetpack torsos
void SetJetpack(Platform& p, bool enabled)
{
  if (enabled != g_jetpackEnabled) {
    g_jetpackEnabled = enabled;
    p.Log("info", std::format("Jetpack {}", enabled ? "enabled" : "disabled"));
  }
}
}

void InstallPowerArmor(Platform& p)
{
  p.RegisterNative("playPowerArmorEnter", [&p](const Json& a) -> Json {
    return PlayEnter(p, a.at(0).get<uint32_t>());
  });
  p.RegisterNative("playPowerArmorExit",
                   [&p](const Json&) -> Json { return PlayExit(p); });
  p.RegisterNative("setInPowerArmor", [&p](const Json& a) -> Json {
    SetInPowerArmor(p, a.at(0).get<uint32_t>(), a.at(1).get<uint32_t>(),
                    a.at(2).get<bool>());
    return nullptr;
  });
  p.RegisterNative("applyPowerArmorVisual", [](const Json& a) -> Json {
    ApplyVisual(a.at(0).get<uint32_t>(), a.at(1));
    return nullptr;
  });
  p.RegisterNative("setFusionCoreCharge", [](const Json& a) -> Json {
    SetCoreCharge(a.at(0).get<float>());
    return nullptr;
  });
  p.RegisterNative("setJetpackEnabled", [&p](const Json& a) -> Json {
    SetJetpack(p, a.at(0).get<bool>());
    return nullptr;
  });

  p.OnFrame([&p](float) {
    PlayerFrame(p);
    PuppetsFrame(p);
  });
  // Proxy frames are network-only: not in the save. Puppets leave before
  // the save too (Puppets module); the client re-applies their power armor
  // when it recreates them (PowerArmorService.applyTo).
  p.OnBeforeSave([] {
    for (auto& [id, st] : g_puppets) {
      RemoveRef(st.proxy);
    }
    g_puppets.clear();
  });

  static bool hooked = false;
  if (!hooked) {
    hooked = true;
    REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::TESFurniture[0] };
    FurnitureActivate::original =
      vtbl.write_vfunc(0x40, FurnitureActivate::Thunk);
    REX::INFO("Installed the power armor activation hook");
  }
}

}
