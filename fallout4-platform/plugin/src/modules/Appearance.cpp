// F03 appearance: get/applyAppearanceFo4, open/closeLooksMenu and the
// looksMenuClosed event (docs/falloutmp/features/F03-appearance-chargen.md
// §4.4, §4.5).
//
// Every puppet gets its own runtime TESNPC (PLAT-070), copied from its
// base the first time a face is applied, so a face is never written to a
// shared base (never to the player's NPC 0x7). Its id is kept in
// puppets::State::npcId; NPCs of deleted puppets are reused.
//
// An apply writes the face block (AppearanceData.h), switches the race if
// needed (Papyrus Actor.SetRace), then rebuilds the 3D with
// Actor::Reset3D(true, 0, true, 0) while bUseFaceGenPreprocessedHeads is
// off (TE, prior-art §3.2.3), and resolves once the 3D is back. At most
// two rebuilds start per frame (stream-in bursts, F03 §4.11).
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "AppearanceData.h"
#include "GameUtil.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"
#include "Puppets.h"

#include <format>
#include <list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fmp::modules {

namespace {
constexpr auto kLooksMenu = "LooksMenu";
constexpr int kMaxRebuildsPerFrame = 2;
constexpr double kRaceTimeoutMs = 5000;
constexpr double kSettleMs = 1500;
constexpr double kLoadTimeoutMs = 10000;
constexpr uint64_t kReclaimFrames = 120;

// TESNPC change flags (prior-art §5.1 B3)
constexpr uint32_t kChangeFace = 0x800; // head data, parts, skin colour, tints
constexpr uint32_t kChangeMorphs = 0x4000; // weights and morphs
constexpr uint32_t kChangeRace = 0x2000000;

enum class Stage
{
  Queued,
  WaitRace,
  WaitLoad
};

struct Job
{
  uint64_t serial = 0;
  uint32_t actorId = 0;
  std::vector<uint32_t> asyncIds; // all resolved with the result
  appearance::Appearance data;
  Stage stage = Stage::Queued;
  double stageStartMs = 0;
  bool raceDone = false;
  const RE::NiAVObject* old3D = nullptr; // compared only
  bool sawUnload = false;
  uint64_t resetFrame = 0;
};

std::list<Job> g_jobs;
uint64_t g_nextSerial = 1;

// Runtime NPCs made for puppets: owner puppet (0 = free) and the base
// they were copied from (a free one is reused for a puppet of that base).
struct RuntimeNpc
{
  uint32_t puppetId = 0;
  uint32_t sourceId = 0;
};
std::unordered_map<uint32_t, RuntimeNpc> g_npcs;

// --- FaceGen ---------------------------------------------------------------

// With preprocessed heads on, the engine looks for FaceGen files by form
// id instead of building the head from the TESNPC data.
RE::Setting* FaceGenSetting()
{
  static RE::Setting* setting = [] {
    constexpr std::string_view key = "bUseFaceGenPreprocessedHeads:General";
    RE::Setting* s = nullptr;
    if (auto ini = RE::INISettingCollection::GetSingleton()) {
      s = ini->GetSetting(key);
    }
    if (!s) {
      if (auto pref = RE::INIPrefSettingCollection::GetSingleton()) {
        s = pref->GetSetting(key);
      }
    }
    if (!s) {
      REX::WARN("{} not found: faces may not rebuild from data", key);
    }
    return s;
  }();
  return setting;
}

// Off while rebuilds run, restored when the queue is empty.
// [verify] the head is built from data with the setting off only during
// the rebuild (TE also forced it off every frame).
void OverrideFaceGen(bool on)
{
  static bool active = false;
  static bool original = true;
  if (on == active) {
    return;
  }
  auto s = FaceGenSetting();
  if (!s) {
    return;
  }
  if (on) {
    original = s->GetBinary();
    s->SetBinary(false);
  } else {
    s->SetBinary(original);
  }
  active = on;
}

// --- Runtime NPCs ----------------------------------------------------------

// Only the player and puppets are written (MODULES.md rule 8)
RE::Actor* JobActor(uint32_t actorId)
{
  return actorId == game::kPlayerRef ? game::ActorOf(actorId)
                                     : puppets::Get(actorId);
}

uint32_t FreeRuntimeId()
{
  static uint32_t next = 0xFF7F0000;
  auto dataHandler = RE::TESDataHandler::GetSingleton();
  for (uint32_t i = 0; i < 0x10000 && next < 0xFFFFFFFF; ++i, ++next) {
    if (!RE::TESForm::GetFormByID(next) &&
        (!dataHandler || !dataHandler->IsFormIDInuse(next))) {
      return next++;
    }
  }
  return 0;
}

RE::TESNPC* CreateRuntimeNpc(RE::TESNPC* source)
{
  auto factory = RE::IFormFactory::GetFormFactories()[std::to_underlying(
    RE::ENUM_FORM_ID::kNPC_)];
  auto form = factory ? factory->DoCreate() : nullptr;
  auto npc = form ? form->As<RE::TESNPC>() : nullptr;
  if (!npc) {
    REX::ERROR("Unable to create a runtime NPC");
    return nullptr;
  }
  // [verify] runtime forms get an 0xFF id from the factory; if not, one
  // is assigned here (SetFormID registers it).
  if (!npc->GetFormID()) {
    if (auto id = FreeRuntimeId()) {
      npc->SetFormID(id, false);
    }
  }
  if (!npc->GetFormID() || RE::TESForm::GetFormByID(npc->GetFormID()) != npc) {
    REX::ERROR("The runtime NPC has no registered form id");
    return nullptr;
  }
  // [verify] TESForm::Copy (vfunc 0x36) copies race, skin, class, outfit,
  // attack data and the face block of the source NPC.
  npc->Copy(source);
  appearance::Detach(npc, source);
  // No change flags and no references at save time (puppets leave the
  // world before saves): the runtime NPC is not written to the save.
  // [verify] it isn't saved as a created form.
  REX::INFO("Runtime NPC {:X} from {:X}", npc->GetFormID(),
            source->GetFormID());
  return npc;
}

// The puppet's own TESNPC, created (or reused) and made its base.
RE::TESNPC* PuppetNpc(uint32_t puppetId, RE::Actor* actor)
{
  auto st = puppets::Find(puppetId);
  auto base = actor->GetObjectReference();
  if (!st || !base) {
    return nullptr;
  }
  RE::TESNPC* npc = st->npcId ? game::Form<RE::TESNPC>(st->npcId) : nullptr;
  if (!npc) {
    auto source = base->As<RE::TESNPC>();
    if (!source) {
      return nullptr;
    }
    const uint32_t sourceId = source->GetFormID();
    for (auto& [id, rn] : g_npcs) {
      if (!rn.puppetId && rn.sourceId == sourceId) {
        npc = game::Form<RE::TESNPC>(id);
        if (npc) {
          break;
        }
      }
    }
    if (!npc) {
      npc = CreateRuntimeNpc(source);
    }
    if (!npc) {
      return nullptr;
    }
    g_npcs[npc->GetFormID()] = { puppetId, sourceId };
    st->npcId = npc->GetFormID();
  }
  if (base != npc) {
    // [verify] the actor takes the new base (TESObjectREFR vfunc 0xAA);
    // the Reset3D that follows reloads the model from it.
    actor->SetObjectReference(npc);
  }
  return npc;
}

// Runtime NPCs whose puppet is gone become free.
void Reclaim()
{
  for (auto& [id, rn] : g_npcs) {
    if (!rn.puppetId) {
      continue;
    }
    auto st = puppets::Find(rn.puppetId);
    if (!st || st->npcId != id) {
      rn.puppetId = 0;
    }
  }
}

// --- Apply jobs ------------------------------------------------------------

Job* FindJob(uint64_t serial)
{
  for (auto& job : g_jobs) {
    if (job.serial == serial) {
      return &job;
    }
  }
  return nullptr;
}

// Another apply on the same actor is running (jobs run one at a time per
// actor).
bool Busy(const Job& job)
{
  for (auto& other : g_jobs) {
    if (&other != &job && other.actorId == job.actorId &&
        other.stage != Stage::Queued) {
      return true;
    }
  }
  return false;
}

void Queue(uint32_t actorId, uint32_t asyncId, appearance::Appearance data)
{
  // A newer face replaces one still waiting for the same actor
  for (auto& job : g_jobs) {
    if (job.actorId == actorId && job.stage == Stage::Queued) {
      job.data = std::move(data);
      job.asyncIds.push_back(asyncId);
      return;
    }
  }
  Job job;
  job.serial = g_nextSerial++;
  job.actorId = actorId;
  job.asyncIds.push_back(asyncId);
  job.data = std::move(data);
  g_jobs.push_back(std::move(job));
}

void Reset(Platform& p, Job& job, RE::Actor* actor)
{
  OverrideFaceGen(true);
  job.old3D = actor->Get3D(false);
  job.sawUnload = false;
  actor->Reset3D(true, 0, true, 0); // queued, asynchronous
  job.stage = Stage::WaitLoad;
  job.stageStartMs = p.NowMs();
  job.resetFrame = p.FrameCount();
}

// Writes the face; nullopt = in progress, else the result.
std::optional<bool> Begin(Platform& p, Job& job, int& rebuilds)
{
  auto actor = JobActor(job.actorId);
  if (!actor) {
    return false;
  }
  const bool isPlayer = job.actorId == game::kPlayerRef;
  RE::TESNPC* npc = nullptr;
  if (isPlayer) {
    auto base = actor->GetObjectReference();
    npc = base ? base->As<RE::TESNPC>() : nullptr;
  } else {
    npc = PuppetNpc(job.actorId, actor);
  }
  if (!npc) {
    p.Log("warn", std::format("appearance: no NPC for {:X}", job.actorId));
    return false;
  }
  auto race = game::Form<RE::TESRace>(job.data.raceId);
  const bool raceChanged = npc->GetFormRace() != race;
  std::string error;
  if (!appearance::Write(npc, job.data, error)) {
    p.Log("warn", std::format("appearance of {:X}: {}", job.actorId, error));
    return false;
  }
  if (isPlayer) {
    // The player's face belongs in its save. Puppets' runtime NPCs get no
    // change flags so they stay out of saves.
    npc->AddChange(kChangeFace | kChangeMorphs |
                   (raceChanged ? kChangeRace : 0));
    // The actor-side tint array the face compositor reads (FO4_Wrld:
    // "two tint arrays"). [verify] it is a separate array on 1.11.
    auto pc = RE::PlayerCharacter::GetSingleton();
    if (pc && pc->tintingData && pc->tintingData != npc->tintingData) {
      appearance::WriteTints(pc->tintingData, job.data.tints, race,
                             job.data.isFemale);
    }
  }
  if (race && actor->race != race) {
    // Actor.SetRace(Race akRace = None)
    job.stage = Stage::WaitRace;
    job.stageStartMs = p.NowMs();
    job.raceDone = false;
    const auto serial = job.serial;
    if (papyrus::CallMethod(
          actor, "Actor", "SetRace",
          [serial](const Json&) {
            if (auto j = FindJob(serial)) {
              j->raceDone = true;
            }
          },
          race)) {
      return std::nullopt;
    }
  }
  Reset(p, job, actor);
  ++rebuilds;
  return std::nullopt;
}

// The 3D is back after Reset3D.
std::optional<bool> Poll(Platform& p, Job& job)
{
  auto actor = JobActor(job.actorId);
  if (!actor) {
    return false;
  }
  const RE::NiAVObject* cur = actor->Get3D(false);
  if (!cur) {
    job.sawUnload = true;
  }
  const double elapsed = p.NowMs() - job.stageStartMs;
  if (cur && p.FrameCount() > job.resetFrame &&
      (job.sawUnload || cur != job.old3D)) {
    return true;
  }
  // Same root after a while: rebuilt in place. [verify] Reset3D with
  // reloadAll replaces the root node (else this settle time decides).
  if (cur && elapsed >= kSettleMs) {
    return true;
  }
  if (elapsed >= kLoadTimeoutMs) {
    return false; // no 3D (actor not loaded); the data is written anyway
  }
  return std::nullopt;
}

void Tick(Platform& p)
{
  if (p.FrameCount() % kReclaimFrames == 0) {
    Reclaim();
  }
  int rebuilds = 0;
  for (auto it = g_jobs.begin(); it != g_jobs.end();) {
    auto& job = *it;
    std::optional<bool> result;
    switch (job.stage) {
      case Stage::Queued:
        // The player's face is left alone while the editor is open
        if (rebuilds < kMaxRebuildsPerFrame && !Busy(job) &&
            !(job.actorId == game::kPlayerRef && game::MenuOpen(kLooksMenu))) {
          result = Begin(p, job, rebuilds);
        }
        break;
      case Stage::WaitRace:
        if ((job.raceDone || p.NowMs() - job.stageStartMs >= kRaceTimeoutMs) &&
            rebuilds < kMaxRebuildsPerFrame) {
          if (auto actor = JobActor(job.actorId)) {
            Reset(p, job, actor);
            ++rebuilds;
          } else {
            result = false;
          }
        }
        break;
      case Stage::WaitLoad:
        result = Poll(p, job);
        break;
    }
    if (result) {
      if (!*result) {
        p.Log("warn",
              std::format("appearance of {:X} not applied", job.actorId));
      }
      for (auto id : job.asyncIds) {
        p.Resolve(id, *result);
      }
      it = g_jobs.erase(it);
    } else {
      ++it;
    }
  }
  if (g_jobs.empty()) {
    OverrideFaceGen(false);
  }
}

// --- Natives ---------------------------------------------------------------

Json GetAppearance(uint32_t actorId)
{
  auto actor = game::ActorOf(actorId);
  auto base = actor ? actor->GetObjectReference() : nullptr;
  auto npc = base ? base->As<RE::TESNPC>() : nullptr;
  if (!npc) {
    return nullptr;
  }
  const RE::BGSCharacterTint::Entries* tints = nullptr;
  if (actorId == game::kPlayerRef) {
    // The player's actor-side tint array is the one LooksMenu edits and
    // the compositor reads. [verify] against npc->tintingData after edits.
    auto pc = RE::PlayerCharacter::GetSingleton();
    if (pc && pc->tintingData && !pc->tintingData->entriesA.empty()) {
      tints = pc->tintingData;
    }
  }
  auto data = appearance::Read(npc->GetRootFaceNPC(), tints, actor->race);
  return data ? appearance::ToJson(*data) : Json(nullptr);
}

void OpenLooksMenu(Platform& p, std::int32_t mode)
{
  // LooksMenuMode: 0 create, 1 remake, 2 haircut, 3 surgery, 4 face paint
  if (mode < 0 || mode > 4) {
    p.Log("warn", std::format("openLooksMenu: unknown mode {}", mode));
    return;
  }
  if (game::MenuOpen(kLooksMenu)) {
    return;
  }
  // Game.ShowRaceMenu(ObjectReference akMenuTarget = None, int uiMode = 0,
  //   ObjectReference akMenuSpouseFemale = None,
  //   ObjectReference akMenuSpouseMale = None,
  //   ObjectReference akVendor = None)
  // [verify] uiMode 0 without spouse references works outside MQ101
  // (F03 §8; else the client falls back to 1).
  RE::TESObjectREFR* const none = nullptr;
  papyrus::CallStatic("Game", "ShowRaceMenu", nullptr, none, mode, none, none,
                      none);
}

void CloseLooksMenu()
{
  if (!game::MenuOpen(kLooksMenu)) {
    return;
  }
  // [verify] LooksMenu accepts kHide (else kForceHide); the client puts
  // the server's face back after the close.
  if (auto queue = RE::UIMessageQueue::GetSingleton()) {
    queue->AddMessage(RE::BSFixedString(kLooksMenu),
                      RE::UI_MESSAGE_TYPE::kHide);
  }
}

class LooksMenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::MenuOpenCloseEvent& e,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
  {
    // UI thread: only Emit (thread safe). The client reads the face on
    // the next frame. [verify] the engine has written the player's NPC by
    // then.
    if (!e.opening && std::string_view(e.menuName) == kLooksMenu) {
      Platform::Get().Emit("looksMenuClosed", Json::object());
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};
}

void InstallAppearance(Platform& p)
{
  p.RegisterNative("getAppearanceFo4", [](const Json& a) -> Json {
    return GetAppearance(a.at(0).get<uint32_t>());
  });
  p.RegisterNative("applyAppearanceFo4", [&p](const Json& a) -> Json {
    const auto actorId = a.at(0).get<uint32_t>();
    auto data = appearance::FromJson(a.at(1));
    if (actorId != game::kPlayerRef && !puppets::IsPuppet(actorId)) {
      p.Log("warn",
            std::format("applyAppearanceFo4: {:X} is not a puppet", actorId));
      return false;
    }
    const auto id = p.NewAsyncId();
    Queue(actorId, id, std::move(data));
    return Platform::Pending(id);
  });
  p.RegisterNative("openLooksMenu", [&p](const Json& a) -> Json {
    OpenLooksMenu(p, a.at(0).get<std::int32_t>());
    return nullptr;
  });
  p.RegisterNative("closeLooksMenu", [](const Json&) -> Json {
    CloseLooksMenu();
    return nullptr;
  });
  p.OnFrame([&p](float) { Tick(p); });

  static LooksMenuSink looksMenuSink;
  if (auto ui = RE::UI::GetSingleton()) {
    ui->RegisterSink<RE::MenuOpenCloseEvent>(&looksMenuSink);
  }
}

}
