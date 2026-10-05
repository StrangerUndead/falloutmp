// F01 movement natives (baseline; extended by the movement work package).
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
Json GetMovement(uint32_t actorId)
{
  if (actorId != game::kPlayerRef) {
    return nullptr;
  }
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!player || !player->GetParentCell() || game::Loading()) {
    return nullptr;
  }
  uint32_t space = game::SpaceOf(player);
  if (!space) {
    return nullptr;
  }
  auto pos = player->GetPosition();
  return { { "worldOrCell", space },
           { "pos", game::ToJson(pos) },
           { "yaw", game::ToDeg(player->data.angle.z) },
           { "aimPitch", game::ToDeg(player->data.angle.x) },
           { "aimHeading", 0 },
           { "speed", 0 },
           { "direction", 0 },
           { "velZ", 0 },
           { "flags", 0 } };
}

void Teleport(Platform& p, uint32_t refId, const Json& posJson, float yawDeg,
              uint32_t worldOrCell)
{
  auto ref = game::Ref(refId);
  RE::TESWorldSpace* world;
  RE::TESObjectCELL* interior;
  if (!ref || !game::ResolveSpace(worldOrCell, world, interior)) {
    p.Log("warn",
          std::format("teleport of {:X}: unknown reference or space {:X}",
                      refId, worldOrCell));
    return;
  }
  auto pos = game::Point(posJson);
  if (puppets::IsPuppet(refId)) {
    if (game::SpaceOf(ref) != worldOrCell) {
      ref->MoveRefToNewSpace(interior, world);
    }
    ref->SetLocationOnReference(pos);
    ref->SetAngleOnReference({ 0.f, 0.f, game::ToRad(yawDeg) });
    return;
  }
  // The player: MoveTo loads the destination; it needs a reference to move
  // to, so a heading marker is placed there first.
  constexpr uint32_t kXMarkerHeading = 0x34;
  auto marker =
    game::CreateRef(kXMarkerHeading, pos, yawDeg, world, interior);
  if (!marker) {
    p.Log("error", "Unable to place the teleport marker");
    return;
  }
  papyrus::CallMethod(ref, "ObjectReference", "MoveTo", nullptr, marker, 0.f,
                      0.f, 0.f, true);
  p.Log("info", std::format("Moving to {:.0f} {:.0f} {:.0f} in {:X}", pos.x,
                            pos.y, pos.z, worldOrCell));
}

void SetTransform(Platform& p, uint32_t refId, const Json& posJson,
                  float yawDeg)
{
  auto ref = puppets::Get(refId);
  if (!ref) {
    return; // only puppets are driven by the network
  }
  auto pos = game::Point(posJson);
  if (p.Config().puppetMove == "papyrus") {
    if (p.FrameCount() % 6 == 0) { // ~10 Hz: Papyrus calls are queued
      papyrus::CallMethod(ref, "ObjectReference", "SetPosition", nullptr,
                          pos.x, pos.y, pos.z);
      papyrus::CallMethod(ref, "ObjectReference", "SetAngle", nullptr, 0.f,
                          0.f, yawDeg);
    }
    return;
  }
  ref->SetLocationOnReference(pos);
  ref->SetAngleOnReference({ 0.f, 0.f, game::ToRad(yawDeg) });
}
}

void InstallMovement(Platform& p)
{
  p.RegisterNative("getMovementFo4", [](const Json& a) -> Json {
    return GetMovement(a.at(0));
  });
  p.RegisterNative("teleportActor", [&p](const Json& a) -> Json {
    Teleport(p, a.at(0), a.at(1), a.at(2).get<float>(), a.at(3));
    return nullptr;
  });
  p.RegisterNative("setActorTransform", [&p](const Json& a) -> Json {
    SetTransform(p, a.at(0), a.at(1), a.at(2).get<float>());
    return nullptr;
  });
  p.RegisterNative("setMovementFlags",
                   [](const Json&) -> Json { return nullptr; });
  p.RegisterNative("setAimAngles", [](const Json&) -> Json { return nullptr; });
}

}
