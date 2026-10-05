#include "Fo4Game.h"
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>


#include <cmath>
#include <format>
#include <fstream>
#include <numbers>
#include <random>

namespace fmp {

using nlohmann::json;

namespace {
constexpr uint32_t kPlayerRef = 0x14;
constexpr uint32_t kXMarkerHeading = 0x34; // static, in every Bethesda game

float ToRad(float deg)
{
  return deg * std::numbers::pi_v<float> / 180.f;
}

float ToDeg(float rad)
{
  float d = rad * 180.f / std::numbers::pi_v<float>;
  d = std::fmod(d, 360.f);
  return d < 0 ? d + 360.f : d;
}

RE::NiPoint3 Point(const json& p)
{
  return { p.at(0).get<float>(), p.at(1).get<float>(), p.at(2).get<float>() };
}

RE::TESObjectREFR* Ref(uint32_t formId)
{
  if (formId == kPlayerRef) {
    return RE::PlayerCharacter::GetSingleton();
  }
  return RE::TESForm::GetFormByID<RE::TESObjectREFR>(formId);
}

// Worldspace or interior cell of a server "worldOrCell" id.
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
  auto cell = ref->GetParentCell();
  if (!cell) {
    return 0;
  }
  if (cell->IsInterior()) {
    return cell->GetFormID();
  }
  return cell->worldSpace ? cell->worldSpace->GetFormID() : 0;
}

// Fire-and-forget Papyrus method call on a reference.
template <class... Args>
bool CallPapyrus(RE::TESObjectREFR* self, const char* script,
                 const char* function, Args... args)
{
  auto gameVm = RE::GameVM::GetSingleton();
  if (!gameVm || !self) {
    return false;
  }
  auto vm = gameVm->GetVM();
  if (!vm) {
    return false;
  }
  auto handle = vm->GetObjectHandlePolicy().GetHandleForObject(
    static_cast<std::uint32_t>(self->GetFormType()), self);
  RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> noCallback;
  return vm->DispatchMethodCall(handle, RE::BSFixedString(script),
                                RE::BSFixedString(function), noCallback,
                                args...);
}

RE::TESObjectREFR* CreateRef(uint32_t baseId, const RE::NiPoint3& pos,
                             float yawDeg, RE::TESWorldSpace* world,
                             RE::TESObjectCELL* interior)
{
  auto base = RE::TESForm::GetFormByID<RE::TESBoundObject>(baseId);
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
  data.initializeScripts = false;
  auto handle = dataHandler->CreateReferenceAtLocation(data);
  auto ref = handle.get();
  return ref ? ref.get() : nullptr;
}
}

PluginConfig PluginConfig::Load(const std::string& path)
{
  PluginConfig c;
  json j = json::object();
  if (std::ifstream f(path); f) {
    j = json::parse(f, nullptr, /*allow_exceptions*/ false);
    if (j.is_discarded() || !j.is_object()) {
      REX::ERROR("{} is not valid JSON, using defaults", path);
      j = json::object();
    }
  }
  c.serverIp = j.value("server-ip", std::string());
  c.serverPort = j.value("server-port", 7777);
  c.profileId = j.value("profile-id", 0u);
  c.puppetMove = j.value("puppet-move", std::string("native"));
  if (j.contains("puppet-base") && j["puppet-base"].is_string()) {
    try {
      c.puppetBaseId = static_cast<uint32_t>(
        std::stoul(j["puppet-base"].get<std::string>(), nullptr, 0));
    } catch (std::exception&) {
      REX::ERROR("puppet-base must be a form id like \"0x7\"");
    }
  }
  if (c.profileId == 0) {
    std::random_device rd;
    std::uniform_int_distribution<uint32_t> dist(1, 0x7fffffff);
    c.profileId = dist(rd);
    j["profile-id"] = c.profileId;
    if (!j.contains("server-ip")) {
      j["server-ip"] = "";
      j["server-port"] = 7777;
    }
    std::ofstream out(path);
    out << j.dump(2) << "\n";
    REX::INFO("Generated profile id {} in {}", c.profileId, path);
  }
  return c;
}

Fo4Game::Fo4Game(PluginConfig config_)
  : config(std::move(config_))
{
}

void Fo4Game::Log(const std::string& level, const std::string& text)
{
  if (level == "error") {
    REX::ERROR("{}", text);
  } else if (level == "warn") {
    REX::WARN("{}", text);
  } else if (level == "trace") {
    REX::DEBUG("{}", text);
  } else {
    REX::INFO("{}", text);
  }
  if (level == "error" || level == "warn") {
    if (auto console = RE::ConsoleLog::GetSingleton()) {
      console->PrintLine("[FalloutMP] %s", text.c_str());
    }
  }
}

bool Fo4Game::CallNative(const std::string& name, const json& a, json& r)
{
  if (name == "getClientConfig") {
    if (config.serverIp.empty()) {
      r = nullptr;
    } else {
      r = { { "serverIp", config.serverIp },
            { "serverPort", config.serverPort },
            { "profileId", config.profileId } };
    }
  } else if (name == "getMovementFo4") {
    r = GetMovement(a.at(0).get<uint32_t>());
  } else if (name == "teleportActor") {
    Teleport(a.at(0), a.at(1), a.at(2).get<float>(), a.at(3));
  } else if (name == "setActorTransform") {
    SetTransform(a.at(0), a.at(1), a.at(2).get<float>());
  } else if (name == "spawnPuppet") {
    r = SpawnPuppet(a.at(0));
  } else if (name == "deletePuppet") {
    DeletePuppet(a.at(0));
  } else if (name == "showNotification") {
    auto text = a.at(0).get<std::string>();
    RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, false, false);
  } else if (name == "setMovementFlags" || name == "setAimAngles") {
    // F01/F02: animation state of puppets is not applied yet
  } else {
    return false;
  }
  return true;
}

json Fo4Game::GetMovement(uint32_t actorId)
{
  if (actorId != kPlayerRef) {
    return nullptr; // hosted NPCs (F13) come later
  }
  auto player = RE::PlayerCharacter::GetSingleton();
  auto ui = RE::UI::GetSingleton();
  if (!player || !player->GetParentCell() ||
      (ui && ui->GetMenuOpen(RE::BSFixedString("LoadingMenu")))) {
    return nullptr;
  }
  uint32_t space = SpaceOf(player);
  if (!space) {
    return nullptr;
  }
  auto pos = player->GetPosition();
  return { { "worldOrCell", space },
           { "pos", { pos.x, pos.y, pos.z } },
           { "yaw", ToDeg(player->data.angle.z) },
           { "aimPitch", ToDeg(player->data.angle.x) },
           { "aimHeading", 0 },
           { "speed", 0 },
           { "direction", 0 },
           { "velZ", 0 },
           { "flags", 0 } };
}

void Fo4Game::Teleport(uint32_t refId, const json& posJson, float yawDeg,
                       uint32_t worldOrCell)
{
  auto ref = Ref(refId);
  RE::TESWorldSpace* world;
  RE::TESObjectCELL* interior;
  if (!ref || !ResolveSpace(worldOrCell, world, interior)) {
    Log("warn",
        std::format("teleport of {:X}: unknown reference or space {:X}",
                    refId, worldOrCell));
    return;
  }
  auto pos = Point(posJson);
  if (refId != kPlayerRef && puppets.count(refId)) {
    // Puppets have no loading to do: move them directly
    if (SpaceOf(ref) != worldOrCell) {
      ref->MoveRefToNewSpace(interior, world);
    }
    ref->SetLocationOnReference(pos);
    ref->SetAngleOnReference({ 0.f, 0.f, ToRad(yawDeg) });
    return;
  }
  // The player: the engine's MoveTo loads the destination. It needs a
  // reference to move to, so a heading marker is placed there first.
  auto marker = CreateRef(kXMarkerHeading, pos, yawDeg, world, interior);
  if (!marker) {
    Log("error", "Unable to place the teleport marker");
    return;
  }
  CallPapyrus(ref, "ObjectReference", "MoveTo", marker, 0.f, 0.f, 0.f,
              true);
  Log("info", std::format("Moving to {:.0f} {:.0f} {:.0f} in {:X}", pos.x,
                          pos.y, pos.z, worldOrCell));
}

void Fo4Game::SetTransform(uint32_t refId, const json& posJson, float yawDeg)
{
  if (!puppets.count(refId)) {
    return; // only puppets are driven by the network
  }
  auto ref = Ref(refId);
  if (!ref) {
    return;
  }
  auto pos = Point(posJson);
  if (config.puppetMove == "papyrus") {
    // ~10 Hz: Papyrus calls are queued, not immediate
    if (++frame % 6 == 0) {
      CallPapyrus(ref, "ObjectReference", "SetPosition", pos.x, pos.y,
                  pos.z);
      CallPapyrus(ref, "ObjectReference", "SetAngle", 0.f, 0.f, yawDeg);
    }
    return;
  }
  ref->SetLocationOnReference(pos);
  ref->SetAngleOnReference({ 0.f, 0.f, ToRad(yawDeg) });
}

uint32_t Fo4Game::SpawnPuppet(const json& s)
{
  RE::TESWorldSpace* world;
  RE::TESObjectCELL* interior;
  uint32_t space = s.at("worldOrCell");
  if (!ResolveSpace(space, world, interior)) {
    Log("warn", std::format("puppet in unknown space {:X}", space));
    return 0;
  }
  uint32_t base = s.value("baseId", 0u);
  if (!base) {
    base = config.puppetBaseId;
  }
  auto ref =
    CreateRef(base, Point(s.at("pos")), s.value("yaw", 0.f), world, interior);
  if (!ref) {
    Log("error", std::format("Unable to create a puppet from {:X}", base));
    return 0;
  }
  // The network drives puppets: no AI, no combat, no dialogue
  CallPapyrus(ref, "Actor", "EnableAI", false, false);
  auto id = ref->GetFormID();
  puppets.insert(id);
  Log("info", std::format("Spawned puppet {:X} ({})", id,
                          s.value("name", std::string())));
  return id;
}

void Fo4Game::DeletePuppet(uint32_t refId)
{
  if (!puppets.erase(refId)) {
    return;
  }
  if (auto ref = Ref(refId)) {
    ref->Disable();
    ref->SetWantsDelete(true);
    CallPapyrus(ref, "ObjectReference", "Delete");
  }
}

void Fo4Game::DeleteAllPuppets()
{
  auto all = puppets;
  for (auto id : all) {
    DeletePuppet(id);
  }
}

}
