#include "Fo4PartOneGlue.h"
#include "EspmFo4DataSource.h"
#include "Fo4WorldBootstrap.h"
#include "MpActor.h"
#include "PartOne.h"
#include "gamemode_events/GameModeEvent.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace fo4 {

namespace {
int64_t SteadyMs()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
           std::chrono::steady_clock::now().time_since_epoch())
    .count();
}
}

// A gamemode event raised by the Fallout 4 layer (mp.onFo4...).
class Fo4GamemodeEvent : public GameModeEvent
{
public:
  Fo4GamemodeEvent(std::string name_, std::string argsJsonArray_)
    : name(std::move(name_))
    , args(std::move(argsJsonArray_))
  {
  }
  const char* GetName() const override { return name.c_str(); }
  std::string GetArgumentsJsonArray() const override { return args; }

private:
  void OnFireSuccess(WorldState*) override {}
  std::string name;
  std::string args;
};

class PartOneFo4Host : public Fo4Host
{
public:
  explicit PartOneFo4Host(PartOne& p)
    : partOne(p)
  {
  }

  MpObjectReference* Ref(FormId id)
  {
    auto& f = partOne.worldState.LookupFormById(id);
    return f ? f->AsObjectReference() : nullptr;
  }
  MpActor* ActorPtr(ActorId id)
  {
    auto& f = partOne.worldState.LookupFormById(id);
    return f ? f->AsActor() : nullptr;
  }

  std::array<float, 3> GetActorPos(ActorId a) override
  {
    if (auto r = Ref(a)) {
      auto& p = r->GetPos();
      return { p.x, p.y, p.z };
    }
    return { 0, 0, 0 };
  }
  uint32_t GetActorWorldOrCell(ActorId a) override
  {
    if (auto r = Ref(a)) {
      return r->GetCellOrWorld().ToFormId(partOne.worldState.espmFiles);
    }
    return 0;
  }
  bool IsActorAlive(ActorId a) override
  {
    auto ac = ActorPtr(a);
    return ac && !ac->IsDead();
  }
  ProfileId GetProfileId(ActorId a) override
  {
    auto ac = ActorPtr(a);
    return ac ? ac->GetProfileId() : -1;
  }
  bool IsNpc(ActorId a) override
  {
    auto ac = ActorPtr(a);
    return !ac ||
      partOne.serverState.UserByActor(ac) == Networking::InvalidUserId;
  }
  std::optional<std::array<float, 3>> GetRefPos(FormId id) override
  {
    if (auto r = Ref(id)) {
      auto& p = r->GetPos();
      return std::array<float, 3>{ p.x, p.y, p.z };
    }
    return std::nullopt;
  }
  FormId GetRefBaseId(FormId id) override
  {
    auto r = Ref(id);
    return r ? r->GetBaseId() : 0;
  }
  FormId AllocateFormId() override
  {
    return partOne.worldState.GenerateFormId();
  }
  void SendTo(ActorId a, const IMessageBase& msg, bool reliable) override
  {
    auto ac = ActorPtr(a);
    if (!ac) {
      return;
    }
    if (partOne.serverState.UserByActor(ac) != Networking::InvalidUserId) {
      ac->SendToUser(msg, reliable);
      return;
    }
    // A hosted NPC's owner copy goes to its host (F13, S14)
    if (auto hostActor = ActorPtr(GetHostOf(a))) {
      if (partOne.serverState.UserByActor(hostActor) !=
          Networking::InvalidUserId) {
        hostActor->SendToUser(msg, reliable);
      }
    }
  }

  ActorId GetHostOf(ActorId a) override
  {
    auto it = partOne.worldState.hosters.find(a);
    return it == partOne.worldState.hosters.end() ? 0 : it->second;
  }

  void SendToNeighboursExcept(ActorId a, ActorId except,
                              const IMessageBase& msg, bool reliable) override
  {
    auto r = Ref(a);
    if (!r) {
      return;
    }
    for (auto listener : r->GetListeners()) {
      auto ac = listener->AsActor();
      if (ac && ac != r && ac->GetFormId() != except &&
          partOne.serverState.UserByActor(ac) != Networking::InvalidUserId) {
        ac->SendToUser(msg, reliable);
      }
    }
  }
  void SendToNeighbours(ActorId a, const IMessageBase& msg,
                        bool reliable) override
  {
    auto r = Ref(a);
    if (!r) {
      return;
    }
    for (auto listener : r->GetListeners()) {
      auto ac = listener->AsActor();
      if (ac && ac != r &&
          partOne.serverState.UserByActor(ac) != Networking::InvalidUserId) {
        ac->SendToUser(msg, reliable);
      }
    }
  }
  int64_t NowMs() override { return SteadyMs(); }
  bool TeleportActor(ActorId a, const std::array<float, 3>& pos,
                     uint32_t worldOrCell) override
  {
    auto ac = ActorPtr(a);
    if (!ac) {
      return false;
    }
    LocationalData loc;
    loc.pos = { pos[0], pos[1], pos[2] };
    loc.rot = ac->GetAngle();
    loc.cellOrWorldDesc =
      FormDesc::FromFormId(worldOrCell, partOne.worldState.espmFiles);
    ac->Teleport(loc);
    return true;
  }
  void SetActorTransform(ActorId a, const std::array<float, 3>& pos,
                         float yawDeg) override
  {
    if (auto ac = ActorPtr(a)) {
      ac->SetPos({ pos[0], pos[1], pos[2] },
                 SetPosMode::CalledByUpdateMovement);
      auto rot = ac->GetAngle();
      ac->SetAngle({ rot.x, rot.y, yawDeg },
                   SetAngleMode::CalledByUpdateMovement);
      // Upstream host election treats an actor without movement for 2 s as
      // unhosted; Fallout 4 movement must refresh the same clock.
      auto idx = ac->GetIdx();
      auto& last = partOne.worldState.lastMovUpdateByIdx;
      if (last.size() <= idx) {
        last.resize(static_cast<size_t>(idx) + 1);
      }
      last[idx] = std::chrono::system_clock::now();
    }
  }

  void SetRaceMenuOpen(ActorId a, bool open) override
  {
    if (auto ac = ActorPtr(a); ac && ac->IsRaceMenuOpen() != open) {
      ac->SetRaceMenuOpen(open);
    }
  }

  bool IsRaceMenuOpen(ActorId a) override
  {
    auto ac = ActorPtr(a);
    return ac && ac->IsRaceMenuOpen();
  }

  bool FireGamemodeEvent(const std::string& name,
                         const nlohmann::json& args) override
  {
    Fo4GamemodeEvent event(name, args.dump());
    return event.Fire(&partOne.worldState);
  }

  void OnActorKilled(ActorId victim, ActorId killer) override
  {
    auto v = ActorPtr(victim);
    auto k = ActorPtr(killer);
    if (v && !v->IsDead()) {
      v->Kill(k);
    }
  }

private:
  PartOne& partOne;
};

struct Fo4PartOneGlue::Impl
{
  PartOne& partOne;
  std::unique_ptr<PartOneFo4Host> host;
  std::shared_ptr<IFo4DataSource> data;
  std::unique_ptr<Fo4Server> server;
  std::set<uint32_t> loadedActors;
  std::string worldPath;
  int64_t lastSaveMs = 0;
  explicit Impl(PartOne& p)
    : partOne(p)
  {
  }
};

Fo4PartOneGlue::Fo4PartOneGlue(PartOne& partOne)
  : pImpl(std::make_unique<Impl>(partOne))
{
  pImpl->host = std::make_unique<PartOneFo4Host>(partOne);
  if (partOne.HasEspm()) {
    pImpl->data = std::make_shared<EspmFo4DataSource>(
      partOne.GetEspm().GetBrowser(), partOne.worldState.GetEspmCache());
  } else {
    pImpl->data = std::make_shared<InMemoryFo4DataSource>();
  }
  pImpl->server = std::make_unique<Fo4Server>(pImpl->data, *pImpl->host);
  ResolveDataIds();
  pImpl->lastSaveMs = SteadyMs();
}

void Fo4PartOneGlue::ResolveDataIds()
{
  // Ids the server can look up by editor id in the load order
  auto espmData = dynamic_cast<EspmFo4DataSource*>(pImpl->data.get());
  auto& mv = pImpl->server->settings.movement;
  if (espmData && mv.speedMultAvId == 0) {
    mv.speedMultAvId = espmData->FindActorValueByEditorId("SpeedMult");
  }
}

Fo4PartOneGlue::~Fo4PartOneGlue()
{
  try {
    SaveAll();
  } catch (const std::exception& e) {
    spdlog::error("Fo4PartOneGlue: final save failed: {}", e.what());
  }
}

Fo4Server& Fo4PartOneGlue::Server()
{
  return *pImpl->server;
}

void Fo4PartOneGlue::ApplySettings(const Fo4ServerSettings& settings)
{
  if (!pImpl->loadedActors.empty() || !pImpl->worldPath.empty()) {
    throw std::runtime_error(
      "Fo4PartOneGlue::ApplySettings must run before any state is loaded");
  }
  pImpl->server =
    std::make_unique<Fo4Server>(pImpl->data, *pImpl->host, settings);
  ResolveDataIds();
}

void Fo4PartOneGlue::OnMessage(uint32_t actorId, MsgType type,
                               const IMessageBase& msg)
{
  EnsureActorLoaded(actorId);
  pImpl->server->OnMessage(actorId, type, msg);
}

void Fo4PartOneGlue::OnSubscribe(uint32_t listenerActorId,
                                 uint32_t emitterActorId)
{
  EnsureActorLoaded(listenerActorId);
  EnsureActorLoaded(emitterActorId);
  try {
    pImpl->server->OnStreamIn(listenerActorId, emitterActorId);
  } catch (const std::exception& e) {
    spdlog::error("Fo4PartOneGlue: stream-in of {:x} to {:x} failed: {}",
                  emitterActorId, listenerActorId, e.what());
  }
}

void Fo4PartOneGlue::EnsureActorLoaded(uint32_t actorId)
{
  if (!pImpl->loadedActors.insert(actorId).second) {
    return;
  }
  auto& f = pImpl->partOne.worldState.LookupFormById(actorId);
  auto ac = f ? f->AsActor() : nullptr;
  if (!ac) {
    return;
  }
  auto& dump = ac->GetDynamicFields().GetValueDump(kActorStateField);
  if (dump == "null") {
    return;
  }
  try {
    pImpl->server->LoadActor(actorId, nlohmann::json::parse(dump));
  } catch (const std::exception& e) {
    spdlog::error("Fo4PartOneGlue: bad saved state for {:x}: {}", actorId,
                  e.what());
  }
}

void Fo4PartOneGlue::SaveActor(uint32_t actorId)
{
  auto& f = pImpl->partOne.worldState.LookupFormById(actorId);
  auto ac = f ? f->AsActor() : nullptr;
  if (!ac || !pImpl->server->FindActor(actorId)) {
    return;
  }
  try {
    ac->SetDynamicFieldSilent(kActorStateField,
                              pImpl->server->ActorToJson(actorId).dump());
  } catch (const std::exception& e) {
    spdlog::error("Fo4PartOneGlue: can't save actor {:x}: {}", actorId,
                  e.what());
  }
}

void Fo4PartOneGlue::SaveAll()
{
  for (auto id : pImpl->loadedActors) {
    SaveActor(id);
  }
  if (pImpl->worldPath.empty()) {
    return;
  }
  auto tmp = pImpl->worldPath + ".tmp";
  std::error_code mkdirEc;
  auto parent = std::filesystem::path(pImpl->worldPath).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, mkdirEc);
  }
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << pImpl->server->WorldToJson().dump();
  }
  std::error_code ec;
  std::filesystem::rename(tmp, pImpl->worldPath, ec); // atomic replace
  if (ec) {
    spdlog::error("Fo4PartOneGlue: can't write {}: {}", pImpl->worldPath,
                  ec.message());
  }
}

void Fo4PartOneGlue::SetWorldStatePath(std::string path)
{
  pImpl->worldPath = std::move(path);
  LoadWorldFile();
}

void Fo4PartOneGlue::BootstrapFromLoadOrder()
{
  if (!pImpl->partOne.HasEspm()) {
    return;
  }
  BootstrapWorld(pImpl->partOne.GetEspm().GetBrowser(),
                 pImpl->partOne.worldState.GetEspmCache(), *pImpl->server);
}

void Fo4PartOneGlue::LoadWorldFile()
{
  if (pImpl->worldPath.empty() || !std::filesystem::exists(pImpl->worldPath)) {
    return;
  }
  try {
    std::ifstream in(pImpl->worldPath, std::ios::binary);
    pImpl->server->LoadWorld(nlohmann::json::parse(in));
  } catch (const std::exception& e) {
    spdlog::error("Fo4PartOneGlue: can't load {}: {}", pImpl->worldPath,
                  e.what());
  }
}

void Fo4PartOneGlue::OnActorDisconnected(MpActor& actor)
{
  auto id = actor.GetFormId();
  pImpl->server->RemoveActor(id);
  SaveActor(id);
}

void Fo4PartOneGlue::Tick()
{
  // Load saved state for any actor that just showed up
  auto& ss = pImpl->partOne.serverState;
  for (size_t i = 0; i < ss.userInfo.size(); ++i) {
    if (!ss.userInfo[i]) {
      continue;
    }
    if (auto ac = ss.ActorByUser(static_cast<Networking::UserId>(i))) {
      EnsureActorLoaded(ac->GetFormId());
    }
  }
  pImpl->server->Tick();
  if (SteadyMs() - pImpl->lastSaveMs >= saveIntervalMs) {
    pImpl->lastSaveMs = SteadyMs();
    SaveAll();
  }
}

}
