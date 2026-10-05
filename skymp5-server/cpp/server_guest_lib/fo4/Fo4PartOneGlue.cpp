#include "Fo4PartOneGlue.h"
#include "EspmFo4DataSource.h"
#include "Fo4WorldBootstrap.h"
#include "MpActor.h"
#include "PartOne.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <spdlog/spdlog.h>

namespace fo4 {

namespace {
int64_t SteadyMs()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
           std::chrono::steady_clock::now().time_since_epoch())
    .count();
}
}

class PartOneFo4Host : public Fo4Host
{
public:
  explicit PartOneFo4Host(PartOne& p)
    : partOne(p)
    , startMs(SteadyMs())
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
    if (auto ac = ActorPtr(a)) {
      if (partOne.serverState.UserByActor(ac) != Networking::InvalidUserId) {
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
  // Until the server clock service (SRV-070/F25) lands: timescale 20,
  // the world starts at 08:00 on day 0.
  double GameDays() override
  {
    double realSec = static_cast<double>(SteadyMs() - startMs) / 1000.0;
    return (8.0 * 3600.0 + realSec * 20.0) / 86400.0;
  }
  float GameHour() override
  {
    double d = GameDays();
    return static_cast<float>((d - std::floor(d)) * 24.0);
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
  int64_t startMs;
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
  pImpl->lastSaveMs = SteadyMs();
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

void Fo4PartOneGlue::OnMessage(uint32_t actorId, MsgType type,
                               const IMessageBase& msg)
{
  EnsureActorLoaded(actorId);
  pImpl->server->OnMessage(actorId, type, msg);
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
  if (pImpl->worldPath.empty() ||
      !std::filesystem::exists(pImpl->worldPath)) {
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
