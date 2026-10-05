#include "GameUtil.h"

#include <cmath>
#include <numbers>

namespace fmp::game {

float ToRad(float deg)
{
  return deg * std::numbers::pi_v<float> / 180.f;
}

float ToDeg(float rad)
{
  float d = std::fmod(rad * 180.f / std::numbers::pi_v<float>, 360.f);
  return d < 0 ? d + 360.f : d;
}

RE::NiPoint3 Point(const Json& p)
{
  return { p.at(0).get<float>(), p.at(1).get<float>(), p.at(2).get<float>() };
}

Json ToJson(const RE::NiPoint3& p)
{
  return Json::array({ p.x, p.y, p.z });
}

RE::TESObjectREFR* Ref(uint32_t formId)
{
  if (formId == kPlayerRef) {
    return RE::PlayerCharacter::GetSingleton();
  }
  return Form<RE::TESObjectREFR>(formId);
}

RE::Actor* ActorOf(uint32_t formId)
{
  if (formId == kPlayerRef) {
    return RE::PlayerCharacter::GetSingleton();
  }
  return Form<RE::Actor>(formId);
}

bool ResolveSpace(uint32_t worldOrCell, RE::TESWorldSpace*& world,
                  RE::TESObjectCELL*& interior)
{
  world = nullptr;
  interior = nullptr;
  auto form = RE::TESForm::GetFormByID(worldOrCell);
  if (!form) {
    return false;
  }
  if (auto w = form->As<RE::TESWorldSpace>()) {
    world = w;
    return true;
  }
  if (auto c = form->As<RE::TESObjectCELL>(); c && c->IsInterior()) {
    interior = c;
    return true;
  }
  return false;
}

uint32_t SpaceOf(RE::TESObjectREFR* ref)
{
  auto cell = ref ? ref->GetParentCell() : nullptr;
  if (!cell) {
    return 0;
  }
  if (cell->IsInterior()) {
    return cell->GetFormID();
  }
  return cell->worldSpace ? cell->worldSpace->GetFormID() : 0;
}

RE::TESObjectREFR* CreateRef(uint32_t baseId, const RE::NiPoint3& pos,
                             float yawDeg, RE::TESWorldSpace* world,
                             RE::TESObjectCELL* interior,
                             bool initializeScripts)
{
  auto base = Form<RE::TESBoundObject>(baseId);
  auto dataHandler = RE::TESDataHandler::GetSingleton();
  if (!base || !dataHandler) {
    return nullptr;
  }
  RE::NEW_REFR_DATA data;
  data.location = pos;
  data.direction = { 0.f, 0.f, ToRad(yawDeg) };
  data.object = base;
  data.world = world;
  data.interior = interior;
  data.initializeScripts = initializeScripts;
  auto handle = dataHandler->CreateReferenceAtLocation(data);
  auto ref = handle.get();
  return ref ? ref.get() : nullptr;
}

bool MenuOpen(const char* menuName)
{
  auto ui = RE::UI::GetSingleton();
  return ui && ui->GetMenuOpen(RE::BSFixedString(menuName));
}

bool Loading()
{
  return MenuOpen("LoadingMenu");
}

}
