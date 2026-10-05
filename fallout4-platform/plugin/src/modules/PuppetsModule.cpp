// spawnPuppet / deletePuppet: the actors standing in for other players and
// for NPCs streamed in from the server (spawn.baseId > 0: an NPC of that
// base; 0: "puppet-base", a player-like actor).
//
// The network drives puppets; the engine must not. "puppet-ai" in
// FalloutMP.json chooses how their AI is kept out of the way:
//   "package"    (default) AI stays on, so Actor::Update keeps running the
//                behaviour graph and the animation module's graph variables
//                play; the puppet runs the engine's DoNothing package,
//                ignores combat and has no aggression.
//   "restrained" as "package", plus Actor.SetRestrained(true).
//   "off"        Actor.EnableAI(false, false), the old behaviour: the actor
//                only gets UpdateNoAI and its graph may freeze.
// In every mode a puppet can't be talked to, activated, looted or
// pickpocketed (BlockActivation), isn't on the stealth meter, doesn't turn
// hostile on friendly fire and never picks combat targets. Two guards
// (puppets::SetGuards) can be relaxed:
//   "puppet-npc-targets": true   local NPCs may attack puppets (hosted NPCs
//                                fighting remote players); default false;
//   "puppet-local-death": true   puppets may die from local damage; default
//                                false, the server's kill goes through
//                                puppets::AllowKill.
// Pushes are overridden by the per-update warp (Movement module); knockdown
// ragdolls are not suppressed. [verify] explosions near puppets.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"
#include "Puppets.h"

#include <format>
#include <string>
#include <unordered_map>
#include <vector>

namespace fmp::modules {

namespace {
enum class AiMode
{
  Package,
  Restrained,
  Off
};
AiMode g_aiMode = AiMode::Package;

constexpr double kBehaviourAssertMs = 2000;

// Engine-side behaviour state of a puppet, asserted from the frame
// callback once its AI process exists (it is created with the 3D).
struct Behaviour
{
  double nextAssertMs = 0;
  bool doNothingStarted = false;
  RE::TESPackage* doNothing = nullptr; // what the engine runs after it
};
std::unordered_map<uint32_t, Behaviour> g_behaviour;

template <class T>
T RawValue(const PluginConfig& c, const char* key, T fallback)
{
  try {
    return c.raw.value(key, fallback);
  } catch (std::exception&) {
    REX::WARN("FalloutMP.json: '{}' has the wrong type, using the default",
              key);
    return fallback;
  }
}

AiMode ParseAiMode(const std::string& s)
{
  if (s == "off") {
    return AiMode::Off;
  }
  if (s == "restrained") {
    return AiMode::Restrained;
  }
  if (s != "package") {
    REX::WARN("puppet-ai '{}' is unknown, using \"package\"", s);
  }
  return AiMode::Package;
}

// Unaggressive, never flees, helps nobody (AI attributes of this actor)
void SetAiValues(RE::Actor* a)
{
  auto avs = RE::ActorValue::GetSingleton();
  if (!avs) {
    return;
  }
  if (avs->aggression) {
    a->SetActorValue(*avs->aggression, 0.f);
  }
  if (avs->confidence) {
    a->SetActorValue(*avs->confidence, 4.f); // foolhardy
  }
  if (avs->assistance) {
    a->SetActorValue(*avs->assistance, 0.f);
  }
}

void ConfigureAtSpawn(RE::Actor* a, const std::string& name)
{
  papyrus::CallMethod(a, "Actor", "AllowPCDialogue", nullptr, false);
  papyrus::CallMethod(a, "ObjectReference", "BlockActivation", nullptr, true,
                      true);
  papyrus::CallMethod(a, "Actor", "SetNotShowOnStealthMeter", nullptr, true);
  papyrus::CallMethod(a, "ObjectReference", "IgnoreFriendlyHits", nullptr,
                      true);
  papyrus::CallMethod(a, "ObjectReference", "SetNoFavorAllowed", nullptr,
                      true);
  switch (g_aiMode) {
    case AiMode::Off:
      papyrus::CallMethod(a, "Actor", "EnableAI", nullptr, false, false);
      break;
    case AiMode::Restrained:
      papyrus::CallMethod(a, "Actor", "SetRestrained", nullptr, true);
      break;
    case AiMode::Package:
      break;
  }
  SetAiValues(a);
  // A per-reference name: the base may be shared (0x7 is the player's own
  // TESNPC, so Form.SetName on it would rename the player).
  // [verify] the HUD and crosshair show the override name on actors
  if (auto extra = a->extraList.get(); extra && !name.empty()) {
    extra->SetOverrideName(name.c_str());
  }
}

void AssertBehaviour(Behaviour& b, RE::Actor* a)
{
  auto proc = a->currentProcess;
  if (!proc || !a->Get3D()) {
    return;
  }
  proc->ignoringCombat = true;
  if (a->IsInCombat()) {
    a->StopCombat();
  }
  if (g_aiMode == AiMode::Off) {
    return;
  }
  // DoNothing keeps the AI from walking or running packages; started once
  // and restarted if the engine evaluates another package.
  // [verify] the graph keeps animating and the package sticks
  auto current = proc->currentPackage.package;
  if (!b.doNothingStarted) {
    a->InitiateDoNothingPackage();
    b.doNothingStarted = true;
    b.doNothing = nullptr;
  } else if (!b.doNothing) {
    b.doNothing = current;
  } else if (current != b.doNothing) {
    a->InitiateDoNothingPackage();
    b.doNothing = nullptr;
  }
}

uint32_t Spawn(Platform& p, const Json& s)
{
  RE::TESWorldSpace* world;
  RE::TESObjectCELL* interior;
  auto space = s.at("worldOrCell").get<uint32_t>();
  if (!game::ResolveSpace(space, world, interior)) {
    p.Log("warn", std::format("puppet in unknown space {:X}", space));
    return 0;
  }
  uint32_t base = s.value("baseId", 0u);
  if (!base) {
    base = p.Config().puppetBaseId;
  }
  auto ref = game::CreateRef(base, game::Point(s.at("pos")),
                             s.value("yaw", 0.f), world, interior);
  auto actor = ref ? ref->As<RE::Actor>() : nullptr;
  if (!actor) {
    p.Log("error", std::format("Unable to create a puppet from {:X}", base));
    if (ref) {
      ref->Disable();
      ref->SetWantsDelete(true);
    }
    return 0;
  }
  auto id = actor->GetFormID();
  puppets::Add(id, { base, 0, static_cast<float>(p.NowMs()) });
  g_behaviour[id] = {};
  auto name = s.value("name", std::string());
  ConfigureAtSpawn(actor, name);
  p.Log("info", std::format("Spawned puppet {:X} ({})", id, name));
  return id;
}

void Delete(uint32_t id)
{
  if (!puppets::IsPuppet(id)) {
    return;
  }
  puppets::Remove(id);
  g_behaviour.erase(id);
  if (auto ref = game::Ref(id)) {
    ref->Disable();
    ref->SetWantsDelete(true);
    papyrus::CallMethod(ref, "ObjectReference", "Delete", nullptr);
  }
}

void Tick(Platform& p)
{
  if (g_behaviour.empty() || game::Loading()) {
    return;
  }
  double now = p.NowMs();
  for (auto it = g_behaviour.begin(); it != g_behaviour.end();) {
    auto actor = puppets::Get(it->first);
    if (!puppets::IsPuppet(it->first)) {
      it = g_behaviour.erase(it);
      continue;
    }
    if (actor && now >= it->second.nextAssertMs) {
      it->second.nextAssertMs = now + kBehaviourAssertMs;
      AssertBehaviour(it->second, actor);
    }
    ++it;
  }
}
}

void InstallPuppets(Platform& p)
{
  auto& cfg = p.Config();
  g_aiMode =
    ParseAiMode(RawValue(cfg, "puppet-ai", std::string("package")));
  puppets::Guards guards;
  guards.npcsTargetPuppets = RawValue(cfg, "puppet-npc-targets", false);
  guards.localDeath = RawValue(cfg, "puppet-local-death", false);
  puppets::SetGuards(guards);
  puppets::InstallHooks();
  REX::INFO("Puppet AI: {}, NPC targets {}, local death {}",
            RawValue(cfg, "puppet-ai", std::string("package")),
            guards.npcsTargetPuppets, guards.localDeath);

  p.RegisterNative("spawnPuppet",
                   [&p](const Json& a) -> Json { return Spawn(p, a.at(0)); });
  p.RegisterNative("deletePuppet", [](const Json& a) -> Json {
    Delete(a.at(0).get<uint32_t>());
    return nullptr;
  });
  p.OnFrame([&p](float) { Tick(p); });
  // Puppets must not end up in the player's save; the client script
  // recreates them after the save ("saveFinished").
  p.OnBeforeSave([] {
    std::vector<uint32_t> ids;
    for (auto& [id, st] : puppets::All()) {
      ids.push_back(id);
    }
    for (auto id : ids) {
      Delete(id);
    }
  });
}

}
