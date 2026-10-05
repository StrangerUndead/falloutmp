// spawnPuppet / deletePuppet: the actors standing in for other players.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"
#include "Puppets.h"

#include <format>

namespace fmp::modules {

namespace {
uint32_t Spawn(Platform& p, const Json& s)
{
  RE::TESWorldSpace* world;
  RE::TESObjectCELL* interior;
  uint32_t space = s.at("worldOrCell");
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
    return 0;
  }
  // The network drives puppets: no AI, no combat, no dialogue
  papyrus::CallMethod(actor, "Actor", "EnableAI", nullptr, false, false);
  auto id = actor->GetFormID();
  puppets::Add(id, { base, 0, static_cast<float>(p.NowMs()) });
  p.Log("info", std::format("Spawned puppet {:X} ({})", id,
                            s.value("name", std::string())));
  return id;
}

void Delete(uint32_t id)
{
  if (!puppets::IsPuppet(id)) {
    return;
  }
  puppets::Remove(id);
  if (auto ref = game::Ref(id)) {
    ref->Disable();
    ref->SetWantsDelete(true);
    papyrus::CallMethod(ref, "ObjectReference", "Delete", nullptr);
  }
}
}

void InstallPuppets(Platform& p)
{
  puppets::InstallHooks();
  p.RegisterNative("spawnPuppet",
                   [&p](const Json& a) -> Json { return Spawn(p, a.at(0)); });
  p.RegisterNative("deletePuppet", [](const Json& a) -> Json {
    Delete(a.at(0));
    return nullptr;
  });
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
