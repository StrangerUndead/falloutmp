// F22 workshop natives and build-mode capture; F15/F16 crafting, modding
// and scrapping capture at workbenches.
//
// Natives:
//   enterWorkshopMode / exitWorkshopMode   the vanilla Workshop menu on a
//        workbench (Papyrus StartWorkshop; Workshop::RequestExitWorkshop)
//   spawn/move/deleteWorkshopObject        server-placed objects, linked to
//        the nearest workbench (WorkshopItemKeyword) so the menu can select
//        them; the returned id stays valid across saves (objects are
//        respawned after a save under new form ids and mapped back)
//   setPrePlacedDisabled                   pre-placed references scrapped
//   spawnWire / deleteWire                 power wires (F4SE Papyrus
//        ObjectReference.CreateWire, once both ends have 3D); the returned id
//        is a token (the wire is created asynchronously)
//   setWorkshopRatings                     rating and budget actor values on
//        the workbench
//
// Capture (Workshop::*Event sinks; the vanilla change has already happened):
//   workshopActivated     the vanilla Workshop menu opened without the
//                         server: it is closed again and reported; the
//                         server answers and the client calls
//                         enterWorkshopMode
//   workshopMenuClosed    the granted Workshop menu closed (player, build
//                         area left), not when a native closed it
//   workshopPlaceRequested ItemPlacedEvent: reported, and the local object
//                         removed; the server's WorkshopObjects delta spawns
//                         the real one for everyone (builder included)
//   workshopEditRequested ItemMovedEvent (op 0 Move, batched per frame) and
//                         ItemDestroyedEvent (op 1 Scrap, or 2 Store when
//                         the workbench's stored list grew). Moves and
//                         scraps stay applied locally; the server corrects
//   workshopWireRequested a placed wire (BNDS): its two ends are read from
//                         ExtraPowerLinks, then the local wire is removed
//                         (the client spawns the confirmed one)
//   craftRequested        CookingMenu BuildConfirmed (chemistry, cooking)
//   modRequested          ExamineMenu / PowerArmorModMenu BuildConfirmed
//                         with an object mod; item = the key before the mod
//   scrapRequested        ScrapItemCallback::OnAccept (workbench scrap)
//   The crafting menus apply the change locally as usual (optimistic, like
//   the inventory module); the server's SetInventoryFo4 confirms or
//   reverts it.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "ItemKeys.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"
#include "Puppets.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <mutex>
#include <optional>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fmp::modules {

namespace {
constexpr uint32_t kWorkshopItemKeyword = 0x54BA6; // WorkshopItemKeyword
constexpr uint32_t kWireTokenBase = 0xFFF00000;    // spawnWire ids
constexpr int kOpMove = 0;                         // WorkshopEditOp
constexpr int kOpScrap = 1;
constexpr int kOpStore = 2;
constexpr double kEnterDeadlineMs = 8000;
constexpr double kEnterRetryMs = 1500;
constexpr uint64_t kWireLinkFrames = 30; // wait for ExtraPowerLinks
constexpr int kWireAttempts = 5;
constexpr double kWireRetryMs = 1000;

Json Rot(const RE::NiPoint3& rad)
{
  return Json::array(
    { game::ToDeg(rad.x), game::ToDeg(rad.y), game::ToDeg(rad.z) });
}

void RemoveRef(RE::TESObjectREFR* ref)
{
  if (!ref) {
    return;
  }
  ref->Disable();
  ref->SetWantsDelete(true);
  papyrus::CallMethod(ref, "ObjectReference", "Delete", nullptr);
}

// [verify] DisconnectSpline unlinks both ends and updates the power grid
void RemoveWireRef(RE::TESObjectREFR* wire)
{
  if (!wire) {
    return;
  }
  RE::SplineUtils::DisconnectSpline(*wire);
  RemoveRef(wire);
}

bool IsActorOrPuppet(RE::TESObjectREFR* ref)
{
  return !ref || ref->IsPlayerRef() || ref->As<RE::Actor>() ||
    puppets::IsPuppet(ref->GetFormID());
}

// Stored objects of a workshop (Workshop::ExtraData::deletedItems).
// [verify] formID is the stored base form
std::unordered_map<uint32_t, uint32_t> StoredCounts(RE::TESObjectREFR* wb)
{
  std::unordered_map<uint32_t, uint32_t> out;
  auto list = wb ? wb->extraList.get() : nullptr;
  auto extra = list ? list->GetByType<RE::Workshop::ExtraData>() : nullptr;
  if (!extra) {
    return out;
  }
  for (auto info : extra->deletedItems) {
    if (info) {
      out[info->formID] += info->count;
    }
  }
  return out;
}

uint32_t CountOf(const std::unordered_map<uint32_t, uint32_t>& m, uint32_t k)
{
  auto it = m.find(k);
  return it == m.end() ? 0 : it->second;
}

// --- Workshop mode ---------------------------------------------------------

struct WantEnter
{
  uint32_t workbench = 0;
  double deadlineMs = 0;
  double nextTryMs = 0;
};

bool g_engineIn = false;   // any Workshop mode (between start and stop)
bool g_inWorkshop = false; // the server-granted one
uint32_t g_active = 0;     // its workbench
uint32_t g_perms = 0;      // WorkshopPerm mask (informational)
bool g_expectStop = false; // exitWorkshopMode closed it
int g_suppressStops = 0;   // stops of vanilla starts we cancelled
std::optional<WantEnter> g_want;
std::unordered_map<uint32_t, uint32_t> g_stored; // snapshot, active bench

// --- Server-placed objects -------------------------------------------------

struct Spawned
{
  uint32_t clientId = 0; // the id the client knows
  uint32_t current = 0;  // the live reference (0 = removed for a save)
  uint32_t baseId = 0;
  RE::NiPoint3 pos;
  RE::NiPoint3 rotDeg;
  float scale = 1.f;
  bool scrapped = false; // scrapped locally in the Workshop menu
};

std::unordered_map<uint32_t, Spawned> g_objects; // by client id
std::unordered_map<uint32_t, uint32_t> g_currentToClient;

uint32_t ClientIdOf(uint32_t local)
{
  auto it = g_currentToClient.find(local);
  return it == g_currentToClient.end() ? local : it->second;
}

// The live reference of a client id (ours, or any other reference)
uint32_t LocalOf(uint32_t clientId)
{
  auto it = g_objects.find(clientId);
  return it == g_objects.end() ? clientId : it->second.current;
}

void LinkToWorkshop(RE::TESObjectREFR* ref)
{
  auto keyword = game::Form<RE::BGSKeyword>(kWorkshopItemKeyword);
  if (!ref || !keyword) {
    return;
  }
  // [verify] FindNearestValidWorkshop works for a reference without 3D
  auto workbench = RE::Workshop::FindNearestValidWorkshop(*ref);
  if (!workbench && g_active) {
    workbench = game::Ref(g_active);
  }
  if (workbench) {
    ref->SetLinkedRef(workbench, keyword);
  }
}

// CreateReferenceAtLocation with the full rotation (GameUtil::CreateRef
// sets the yaw only), in the player's worldspace or interior.
RE::TESObjectREFR* PlaceObject(const Spawned& s)
{
  auto player = RE::PlayerCharacter::GetSingleton();
  auto base = game::Form<RE::TESBoundObject>(s.baseId);
  auto dataHandler = RE::TESDataHandler::GetSingleton();
  RE::TESWorldSpace* world;
  RE::TESObjectCELL* interior;
  if (!player || !base || !dataHandler ||
      !game::ResolveSpace(game::SpaceOf(player), world, interior)) {
    return nullptr;
  }
  RE::NEW_REFR_DATA data;
  data.location = s.pos;
  data.direction = { game::ToRad(s.rotDeg.x), game::ToRad(s.rotDeg.y),
                     game::ToRad(s.rotDeg.z) };
  data.object = base;
  data.world = world;
  data.interior = interior;
  data.initializeScripts = false; // no WorkshopObjectScript on clients
  auto handle = dataHandler->CreateReferenceAtLocation(data);
  auto refPtr = handle.get();
  auto ref = refPtr.get();
  if (!ref) {
    return nullptr;
  }
  game::MarkNetworkRef(ref->GetFormID());
  if (s.scale > 0.f && std::abs(s.scale - 1.f) > 0.001f) {
    ref->SetScale(s.scale);
  }
  LinkToWorkshop(ref);
  // Placed objects stay where the server put them.
  // [verify] keyframed motion keeps physics objects (junk, props) in place
  papyrus::CallMethod(ref, "ObjectReference", "SetMotionType", nullptr, 2,
                      false);
  return ref;
}

bool Respawn(Spawned& s)
{
  auto ref = PlaceObject(s);
  if (!ref) {
    s.current = 0;
    return false;
  }
  s.current = ref->GetFormID();
  s.scrapped = false;
  g_currentToClient[s.current] = s.clientId;
  return true;
}

void Unspawn(Spawned& s)
{
  if (s.current) {
    g_currentToClient.erase(s.current);
    auto ref = game::Ref(s.current);
    if (ref && !ref->IsDeleted()) {
      RemoveRef(ref);
    }
    s.current = 0;
  }
}

uint32_t SpawnObject(Platform& p, const Json& j)
{
  Spawned s;
  s.baseId = j.at("baseId").get<uint32_t>();
  s.pos = game::Point(j.at("pos"));
  s.rotDeg = game::Point(j.at("rot"));
  s.scale = j.value("scale", 1.f);
  if (!Respawn(s)) {
    p.Log("warn",
          std::format("spawnWorkshopObject: unable to place {:X}", s.baseId));
    return 0;
  }
  s.clientId = s.current;
  g_objects[s.clientId] = s;
  return s.clientId;
}

void MoveObject(const uint32_t clientId, const Json& pos, const Json& rot)
{
  auto it = g_objects.find(clientId);
  if (it == g_objects.end()) {
    return; // only objects we placed
  }
  auto& s = it->second;
  s.pos = game::Point(pos);
  s.rotDeg = game::Point(rot);
  auto ref = s.current ? game::Ref(s.current) : nullptr;
  if (!ref || ref->IsDeleted()) {
    // Scrapped locally and restored by the server
    Unspawn(s);
    Respawn(s);
    return;
  }
  // Papyrus moves update the 3D and collision of placed statics
  papyrus::CallMethod(ref, "ObjectReference", "SetPosition", nullptr, s.pos.x,
                      s.pos.y, s.pos.z);
  papyrus::CallMethod(ref, "ObjectReference", "SetAngle", nullptr, s.rotDeg.x,
                      s.rotDeg.y, s.rotDeg.z);
}

void DeleteObject(uint32_t clientId)
{
  auto it = g_objects.find(clientId);
  if (it == g_objects.end()) {
    return;
  }
  Unspawn(it->second);
  g_objects.erase(it);
}

// --- Pre-placed references -------------------------------------------------

std::unordered_set<uint32_t> g_disabledPrePlaced;

void SetPrePlacedDisabled(uint32_t refId, bool disabled)
{
  auto ref = game::Ref(refId);
  if (IsActorOrPuppet(ref) || g_currentToClient.count(refId)) {
    return;
  }
  if (disabled) {
    ref->Disable();
    g_disabledPrePlaced.insert(refId);
  } else {
    ref->Enable(false);
    g_disabledPrePlaced.erase(refId);
  }
}

// --- Wires -----------------------------------------------------------------

struct Wire
{
  uint32_t a = 0, b = 0; // client ids of the ends
  uint32_t spline = 0;
  uint32_t current = 0; // the wire reference once created
  bool inFlight = false;
  int attempts = 0;
  double nextTryMs = 0;
};

std::unordered_map<uint32_t, Wire> g_wires; // by token
uint32_t g_nextWire = 1;

uint32_t SpawnWire(uint32_t a, uint32_t b, uint32_t spline)
{
  if (!a || !b || a == b) {
    return 0;
  }
  uint32_t token = kWireTokenBase + g_nextWire++;
  Wire w;
  w.a = ClientIdOf(a);
  w.b = ClientIdOf(b);
  w.spline = spline;
  g_wires[token] = w;
  return token;
}

void DeleteWire(uint32_t token)
{
  auto it = g_wires.find(token);
  if (it == g_wires.end()) {
    return;
  }
  if (it->second.current) {
    RemoveWireRef(game::Ref(it->second.current));
  }
  // An in-flight CreateWire finds its token gone and removes the wire
  g_wires.erase(it);
}

// CreateWire needs both ends loaded (3D) and the first end linked to a
// workshop (WorkshopItemKeyword); retried until then.
void WiresFrame(Platform& p)
{
  double now = p.NowMs();
  for (auto& [token, w] : g_wires) {
    if (w.current || w.inFlight || w.attempts >= kWireAttempts ||
        now < w.nextTryMs) {
      continue;
    }
    auto refA = game::Ref(LocalOf(w.a));
    auto refB = game::Ref(LocalOf(w.b));
    if (!refA || !refB || !refA->Get3D() || !refB->Get3D()) {
      continue;
    }
    w.inFlight = true;
    ++w.attempts;
    // None = the default spline (WorkshopSplineObject)
    RE::TESForm* spline =
      w.spline ? RE::TESForm::GetFormByID(w.spline) : nullptr;
    uint32_t t = token;
    papyrus::CallMethod(
      refA, "ObjectReference", "CreateWire",
      [t, &p](const Json& result) {
        uint32_t wireId = result.is_object() ? result.value("formId", 0u) : 0;
        auto it = g_wires.find(t);
        if (it == g_wires.end()) {
          RemoveWireRef(game::Ref(wireId)); // deleted meanwhile
          return;
        }
        it->second.inFlight = false;
        if (wireId) {
          it->second.current = wireId;
        } else {
          it->second.nextTryMs = p.NowMs() + kWireRetryMs;
          if (it->second.attempts >= kWireAttempts) {
            p.Log("warn", std::format("Unable to create wire {:X}", t));
          }
        }
      },
      refB, spline);
  }
}

// --- Ratings ---------------------------------------------------------------

RE::ActorValueInfo* AvByEditorId(std::string_view editorId)
{
  auto dataHandler = RE::TESDataHandler::GetSingleton();
  if (!dataHandler) {
    return nullptr;
  }
  for (auto av : dataHandler->GetFormArray<RE::ActorValueInfo>()) {
    auto name = av ? av->formEditorID.c_str() : nullptr;
    if (name && editorId == name) {
      return av;
    }
  }
  return nullptr;
}

// Rating AVs as WorkshopParentScript's rating table uses them on the
// workbench; the budget bar reads triangles and draws, both get the
// server's single budget pair.
// [verify] food/water/safety/beds/happiness ratings are the resource AVs
// (Food, Water, Safety, Beds, Happiness), power = PowerGenerated, load =
// PowerRequired, population = "Population", and the Workshop menu and
// Pip-Boy show them (vanilla workshop scripts must not overwrite them)
void SetRatings(uint32_t workbenchId, const Json& r)
{
  auto ref = game::Ref(workbenchId);
  auto avs = RE::ActorValue::GetSingleton();
  if (IsActorOrPuppet(ref) || !avs) {
    return;
  }
  static RE::ActorValueInfo* population = AvByEditorId("Population");
  auto set = [&](RE::ActorValueInfo* info, const char* field) {
    if (info && r.contains(field)) {
      ref->SetActorValue(*info, r.at(field).get<float>());
    }
  };
  set(avs->resourceFood, "food");
  set(avs->resourceWater, "water");
  set(avs->resourceSafety, "safety");
  set(avs->resourceBed, "beds");
  set(avs->resourceHappiness, "happiness");
  set(avs->powerGenerated, "power");
  set(avs->powerRequired, "powerLoad");
  set(population, "population");
  set(avs->workshopCurrentTriangles, "budgetCurrent");
  set(avs->workshopCurrentDraws, "budgetCurrent");
  set(avs->workshopMaxTriangles, "budgetMax");
  set(avs->workshopMaxDraws, "budgetMax");
}

// --- Workshop mode natives -------------------------------------------------

void TryStartWorkshop(Platform& p)
{
  if (!g_want || g_engineIn) {
    return;
  }
  double now = p.NowMs();
  if (now > g_want->deadlineMs) {
    p.Log(
      "warn",
      std::format("Workshop mode on {:X} did not start", g_want->workbench));
    // The client leaves build mode (WorkshopService.exitBuildMode)
    p.Emit("workshopMenuClosed", Json{ { "workbench", g_want->workbench } });
    g_want.reset();
    return;
  }
  if (now < g_want->nextTryMs) {
    return;
  }
  g_want->nextTryMs = now + kEnterRetryMs;
  if (auto ref = game::Ref(g_want->workbench)) {
    // Papyrus StartWorkshop checks the workshop like the activation does
    papyrus::CallMethod(ref, "ObjectReference", "StartWorkshop", nullptr,
                        true);
  }
}

void EnterWorkshopMode(Platform& p, uint32_t workbench, uint32_t perms)
{
  auto ref = game::Ref(workbench);
  if (IsActorOrPuppet(ref)) {
    p.Log("warn",
          std::format("enterWorkshopMode: no workbench {:X}", workbench));
    return;
  }
  g_perms = perms;
  if (g_inWorkshop && g_active == workbench) {
    return; // permissions update
  }
  if (g_engineIn) {
    // Another workbench (or a vanilla start still closing): leave first;
    // TryStartWorkshop runs once the stop arrives
    g_expectStop = g_inWorkshop;
    RE::Workshop::RequestExitWorkshop(false);
  }
  double now = p.NowMs();
  g_want = WantEnter{ workbench, now + kEnterDeadlineMs, now };
  TryStartWorkshop(p);
}

void ExitWorkshopMode()
{
  g_want.reset();
  if (!g_engineIn) {
    return;
  }
  if (g_inWorkshop) {
    g_expectStop = true;
  }
  // [verify] a_allowReEntry: false for a normal exit
  RE::Workshop::RequestExitWorkshop(false);
}

// --- Capture ---------------------------------------------------------------

struct Captured
{
  enum class Kind
  {
    kModeStart,
    kModeStop,
    kPlaced,
    kMoved,
    kDestroyed
  } kind;
  uint32_t workshop = 0;
  uint32_t ref = 0;
  uint32_t base = 0;
  uint32_t recipe = 0;
  RE::NiPoint3 pos;
  RE::NiPoint3 rot; // radians
  float scale = 1.f;
  bool wire = false;
};

std::mutex g_mutex;
std::vector<Captured> g_captured;

struct PendingWire
{
  uint32_t workshop = 0;
  uint32_t ref = 0;
  uint32_t spline = 0;
  uint64_t sinceFrame = 0;
};
std::vector<PendingWire> g_pendingWires;

Captured Capture(Captured::Kind kind, const RE::TESObjectREFR* workshop,
                 RE::TESObjectREFR* item)
{
  Captured c{ kind };
  c.workshop = workshop ? workshop->GetFormID() : 0;
  if (!item) {
    return c;
  }
  c.ref = item->GetFormID();
  auto base = item->GetObjectReference();
  c.base = base ? base->GetFormID() : 0;
  c.wire = base && base->GetFormType() == RE::ENUM_FORM_ID::kBNDS;
  c.pos = item->GetPosition();
  c.rot = { item->data.angle.x, item->data.angle.y, item->data.angle.z };
  c.scale = item->refScale / 100.f; // [verify] refScale is percent
  return c;
}

// The recipe of the object being placed: the selected Workshop menu node.
// [verify] the current row's selected node is the placed recipe
uint32_t CurrentRecipe(const RE::TESForm* base)
{
  if (auto row = RE::Workshop::GetCurrentRow()) {
    std::uint32_t column = 0;
    auto node = RE::Workshop::GetSelectedWorkshopMenuNode(*row, column);
    if (node && node->recipe) {
      return node->recipe->GetFormID();
    }
    if (node && node->sourceFormListRecipe) {
      return node->sourceFormListRecipe->GetFormID();
    }
  }
  auto recipe = base
    ? RE::BGSConstructibleObject::FindRecipeForCreatedForm(base)
    : nullptr;
  return recipe ? recipe->GetFormID() : 0;
}

void Push(Captured c)
{
  std::lock_guard l(g_mutex);
  g_captured.push_back(std::move(c));
}

class ModeSink final : public RE::BSTEventSink<RE::Workshop::WorkshopModeEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::Workshop::WorkshopModeEvent& e,
    RE::BSTEventSource<RE::Workshop::WorkshopModeEvent>*) override
  {
    Push(
      Capture(e.start ? Captured::Kind::kModeStart : Captured::Kind::kModeStop,
              e.workshop.get(), nullptr));
    return RE::BSEventNotifyControl::kContinue;
  }
};

class PlacedSink final : public RE::BSTEventSink<RE::Workshop::ItemPlacedEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::Workshop::ItemPlacedEvent& e,
    RE::BSTEventSource<RE::Workshop::ItemPlacedEvent>*) override
  {
    auto c =
      Capture(Captured::Kind::kPlaced, e.workshop.get(), e.placedItem.get());
    if (!c.wire && e.placedItem) {
      c.recipe = CurrentRecipe(e.placedItem->GetObjectReference());
    }
    Push(std::move(c));
    return RE::BSEventNotifyControl::kContinue;
  }
};

class MovedSink final : public RE::BSTEventSink<RE::Workshop::ItemMovedEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::Workshop::ItemMovedEvent& e,
    RE::BSTEventSource<RE::Workshop::ItemMovedEvent>*) override
  {
    Push(Capture(Captured::Kind::kMoved, e.workshop.get(), e.movedItem.get()));
    return RE::BSEventNotifyControl::kContinue;
  }
};

class DestroyedSink final
  : public RE::BSTEventSink<RE::Workshop::ItemDestroyedEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::Workshop::ItemDestroyedEvent& e,
    RE::BSTEventSource<RE::Workshop::ItemDestroyedEvent>*) override
  {
    Push(Capture(Captured::Kind::kDestroyed, e.workshop.get(),
                 e.objectDestroyed.get()));
    return RE::BSEventNotifyControl::kContinue;
  }
};

Json EditItem(uint32_t ref, const RE::NiPoint3& pos, const RE::NiPoint3& rot)
{
  return Json{ { "ref", ClientIdOf(ref) },
               { "pos", game::ToJson(pos) },
               { "rot", Rot(rot) } };
}

struct EditBatch
{
  Json moves = Json::array();
  Json scraps = Json::array();
  Json stores = Json::array();
  std::unordered_set<uint32_t> moved;
};

void FlushEdits(Platform& p, EditBatch& b)
{
  auto emit = [&](int op, Json& list) {
    if (!list.empty()) {
      p.Emit("workshopEditRequested",
             Json{ { "workbench", g_active },
                   { "op", op },
                   { "items", std::move(list) } });
      list = Json::array();
    }
  };
  emit(kOpMove, b.moves);
  emit(kOpScrap, b.scraps);
  emit(kOpStore, b.stores);
  b.moved.clear();
}

void OnModeStart(Platform& p, uint32_t workbench)
{
  g_engineIn = true;
  if (g_want && g_want->workbench == workbench) {
    g_want.reset();
    g_inWorkshop = true;
    g_active = workbench;
    g_expectStop = false;
    g_stored = StoredCounts(game::Ref(workbench));
    p.Log("info", std::format("Workshop mode on {:X}", workbench));
    return;
  }
  if (g_inWorkshop && g_active == workbench) {
    return;
  }
  // The player opened the menu: the server decides (WorkshopMode request)
  p.Emit("workshopActivated", Json{ { "workbench", workbench } });
  ++g_suppressStops;
  // [verify] a_allowReEntry = true lets StartWorkshop follow right after
  RE::Workshop::RequestExitWorkshop(true);
}

void OnModeStop(Platform& p, uint32_t workbench)
{
  g_engineIn = false;
  if (g_suppressStops > 0) {
    --g_suppressStops;
    return;
  }
  bool ours = g_inWorkshop;
  uint32_t active = g_active;
  g_inWorkshop = false;
  g_active = 0;
  g_stored.clear();
  if (ours && !g_expectStop) {
    p.Emit("workshopMenuClosed",
           Json{ { "workbench", active ? active : workbench } });
  }
  g_expectStop = false;
}

void OnPlaced(Platform& p, const Captured& c,
              const std::unordered_map<uint32_t, uint32_t>& stored)
{
  if (c.wire) {
    g_pendingWires.push_back({ c.workshop, c.ref, c.base, p.FrameCount() });
    return;
  }
  // The stored count went down: placed from the stored list
  bool fromStored = CountOf(g_stored, c.base) > CountOf(stored, c.base);
  p.Emit("workshopPlaceRequested",
         Json{ { "workbench", c.workshop },
               { "recipeId", c.recipe },
               { "baseId", c.base },
               { "fromStored", fromStored },
               { "pos", game::ToJson(c.pos) },
               { "rot", Rot(c.rot) },
               { "scale", c.scale },
               // [verify] no snap information in ItemPlacedEvent
               { "snapTarget", 0 } });
  // The server's object replaces this one (WorkshopObjects delta)
  if (auto ref = game::Ref(c.ref); ref && !g_currentToClient.count(c.ref)) {
    RemoveRef(ref);
  }
}

void OnDestroyed(const Captured& c,
                 const std::unordered_map<uint32_t, uint32_t>& stored,
                 EditBatch& b)
{
  if (c.wire) {
    // A server wire (spawned through spawnWire) removed in the menu: the
    // client disconnects it by the token it knows. Vanilla wires that were
    // never confirmed have no token and need nothing.
    for (auto& [token, w] : g_wires) {
      if (w.current == c.ref) {
        Platform::Get().Emit(
          "workshopWireRemoveRequested",
          Json{ { "workbench", g_active }, { "wire", token } });
        break;
      }
    }
    return;
  }
  if (auto it = g_currentToClient.find(c.ref); it != g_currentToClient.end()) {
    if (auto o = g_objects.find(it->second); o != g_objects.end()) {
      o->second.scrapped = true;
    }
  }
  bool store = CountOf(stored, c.base) > CountOf(g_stored, c.base);
  (store ? b.stores : b.scraps).push_back(EditItem(c.ref, c.pos, c.rot));
}

void PendingWiresFrame(Platform& p)
{
  for (auto it = g_pendingWires.begin(); it != g_pendingWires.end();) {
    auto wire = game::Ref(it->ref);
    auto list = wire ? wire->extraList.get() : nullptr;
    auto links = list ? list->GetByType<RE::ExtraPowerLinks>() : nullptr;
    bool linked = links && links->powerLinks.size() >= 2;
    if (!linked && p.FrameCount() - it->sinceFrame < kWireLinkFrames) {
      ++it;
      continue;
    }
    if (linked && it->workshop == g_active && g_inWorkshop) {
      uint32_t a = links->powerLinks[0].formID;
      uint32_t b = links->powerLinks[1].formID;
      p.Emit("workshopWireRequested",
             Json{ { "workbench", it->workshop },
                   { "a", ClientIdOf(a) },
                   { "b", ClientIdOf(b) },
                   { "splineBaseId", it->spline } });
    }
    // The client spawns the confirmed wire (spawnWire)
    RemoveWireRef(wire);
    it = g_pendingWires.erase(it);
  }
}

void CaptureFrame(Platform& p)
{
  std::vector<Captured> events;
  {
    std::lock_guard l(g_mutex);
    events.swap(g_captured);
  }
  auto workbench = g_active ? game::Ref(g_active) : nullptr;
  auto stored = StoredCounts(workbench);
  EditBatch batch;
  for (auto& c : events) {
    switch (c.kind) {
      case Captured::Kind::kModeStart:
        FlushEdits(p, batch);
        OnModeStart(p, c.workshop);
        stored = g_stored;
        break;
      case Captured::Kind::kModeStop:
        FlushEdits(p, batch);
        OnModeStop(p, c.workshop);
        break;
      default:
        if (!g_inWorkshop || c.workshop != g_active) {
          break; // not the server-granted workshop
        }
        if (c.kind == Captured::Kind::kPlaced) {
          OnPlaced(p, c, stored);
        } else if (c.kind == Captured::Kind::kDestroyed) {
          OnDestroyed(c, stored, batch);
        } else if (!c.wire && batch.moved.insert(c.ref).second) {
          // The transform once the move is done
          auto ref = game::Ref(c.ref);
          RE::NiPoint3 pos = ref ? ref->GetPosition() : c.pos;
          RE::NiPoint3 rot = ref
            ? RE::NiPoint3{ ref->data.angle.x, ref->data.angle.y,
                            ref->data.angle.z }
            : c.rot;
          batch.moves.push_back(EditItem(c.ref, pos, rot));
          // Keep our record in step with the local transform
          if (auto it = g_currentToClient.find(c.ref);
              it != g_currentToClient.end()) {
            if (auto o = g_objects.find(it->second); o != g_objects.end()) {
              o->second.pos = pos;
              o->second.rotDeg = { game::ToDeg(rot.x), game::ToDeg(rot.y),
                                   game::ToDeg(rot.z) };
            }
          }
        }
        break;
    }
  }
  FlushEdits(p, batch);
  PendingWiresFrame(p);
  if (g_inWorkshop) {
    g_stored = StoredCounts(game::Ref(g_active));
  }
}

// --- Crafting, modding, scrapping ------------------------------------------

const RE::BGSInventoryItem::Stack* StackAt(const RE::BGSInventoryItem& item,
                                           uint32_t index)
{
  const RE::BGSInventoryItem::Stack* s = item.stackData.get();
  for (uint32_t i = 0; s && i < index; ++i) {
    s = s->nextStack.get();
  }
  return s;
}

std::optional<items::Key> KeyOfHandle(uint32_t handleId, uint32_t stackIndex)
{
  auto inv = RE::BGSInventoryInterface::GetSingleton();
  auto item = inv ? inv->RequestInventoryItem(handleId) : nullptr;
  if (!item || !item->object) {
    return std::nullopt;
  }
  auto stack = StackAt(*item, stackIndex);
  return items::KeyOf(item->object, stack ? stack->extra.get() : nullptr);
}

// The mod in the menu's current slot (attach point keyword).
uint32_t ModInSlot(const items::Key& key, const RE::BGSKeyword* slot)
{
  if (!slot) {
    return 0;
  }
  for (auto id : key.mods) {
    auto mod = game::Form<RE::BGSMod::Attachment::Mod>(id);
    if (mod &&
        RE::detail::BGSKeywordGetTypedKeywordByIndex(
          RE::KeywordType::kAttachPoint, mod->attachPoint.keywordIndex) ==
          slot) {
      return id;
    }
  }
  return 0;
}

// BuildConfirmed, before the menu applies it. `examine` is null for the
// CookingMenu (recipes only).
void ReportBuild(Platform& p, RE::WorkbenchMenuBase& menu,
                 RE::ExamineMenu* examine)
{
  auto bench = menu.workbenchRef.get();
  if (menu.repairing || !bench) {
    return; // repairs are not requests; Pip-Boy inspect has no bench
  }
  uint32_t benchId = bench->GetFormID();
  auto choice = menu.QCurrentModChoiceData();
  if (!choice) {
    return;
  }
  const RE::BGSConstructibleObject* recipe = choice->recipe;
  const RE::TESForm* created = recipe ? recipe->createdItem : nullptr;
  // The choice's union holds a mod (modding) or an object (crafting);
  // both start with their TESForm, the form type tells them apart
  const RE::TESForm* chosen = choice->mod;
  const RE::TESForm* mod = nullptr;
  if (created && created->GetFormType() == RE::ENUM_FORM_ID::kOMOD) {
    mod = created;
  } else if (!created && chosen &&
             chosen->GetFormType() == RE::ENUM_FORM_ID::kOMOD) {
    mod = chosen;
  }
  // The slot's "no mod" choice (ExamineMenu::nullMod, which may be null)
  // removes the mod there
  bool nullMod = false;
  if (examine && choice->mod == examine->nullMod &&
      (choice->mod || !created)) {
    nullMod = true;
    mod = nullptr;
  }

  if (!examine || (!mod && !nullMod)) {
    if (recipe && !nullMod) {
      p.Emit("craftRequested",
             Json{ { "workbench", benchId },
                   { "recipeId", recipe->GetFormID() },
                   { "count", 1 } });
    }
    return;
  }
  // [verify] modItem / modStack name the item being modded
  auto key = KeyOfHandle(examine->modItem.id, examine->modStack);
  if (!key) {
    return;
  }
  bool attach = !nullMod;
  uint32_t modId =
    attach && mod ? mod->GetFormID() : ModInSlot(*key, examine->keyword);
  if (!modId) {
    REX::WARN("Mod change at {:X} without a mod in the slot", benchId);
    return;
  }
  p.Emit("modRequested",
         Json{ { "workbench", benchId },
               { "item", items::ToJson(*key) },
               { "modId", modId },
               { "attach", attach } });
}

template <class Menu>
struct BuildConfirmed
{
  static void Thunk(Menu* a_this, bool a_ownerIsWorkbench)
  {
    try {
      if (a_this) {
        RE::ExamineMenu* examine = nullptr;
        if constexpr (std::is_base_of_v<RE::ExamineMenu, Menu>) {
          examine = a_this;
        }
        ReportBuild(Platform::Get(), *a_this, examine);
      }
    } catch (std::exception& e) {
      REX::ERROR("Workbench capture failed: {}", e.what());
    }
    original(a_this, a_ownerIsWorkbench);
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

// The workbench's scrap confirmation.
struct ScrapAccept
{
  static void Thunk(RE::ScrapItemCallback* a_this)
  {
    try {
      auto menu = a_this ? a_this->thisMenu : nullptr;
      auto bench = menu ? menu->workbenchRef.get() : nullptr;
      // [verify] itemIndex indexes the menu's stacked entries and one item
      // is scrapped per confirmation
      if (bench &&
          a_this->itemIndex < menu->invInterface.stackedEntries.size()) {
        auto& entry = menu->invInterface.stackedEntries[a_this->itemIndex];
        uint32_t stack = entry.stackIndex.size() ? entry.stackIndex[0] : 0;
        if (auto key = KeyOfHandle(entry.invHandle.id, stack)) {
          Platform::Get().Emit("scrapRequested",
                               Json{ { "workbench", bench->GetFormID() },
                                     { "item", items::ToJson(*key) },
                                     { "count", 1 } });
        }
      }
    } catch (std::exception& e) {
      REX::ERROR("Scrap capture failed: {}", e.what());
    }
    original(a_this);
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

void InstallCraftingHooks()
{
  static bool installed = false;
  if (installed) {
    return;
  }
  installed = true;
  // WorkbenchMenuBase::BuildConfirmed is vfunc 0x17 of the primary vtable
  REL::Relocation<std::uintptr_t> examine{ RE::VTABLE::ExamineMenu[0] };
  BuildConfirmed<RE::ExamineMenu>::original =
    examine.write_vfunc(0x17, BuildConfirmed<RE::ExamineMenu>::Thunk);
  REL::Relocation<std::uintptr_t> paStation{
    RE::VTABLE::PowerArmorModMenu[0]
  };
  BuildConfirmed<RE::PowerArmorModMenu>::original =
    paStation.write_vfunc(0x17, BuildConfirmed<RE::PowerArmorModMenu>::Thunk);
  // [verify] CookingMenu derives from WorkbenchMenuBase (no CommonLibF4
  // header; its ModChoiceData object/recipe union suggests so)
  REL::Relocation<std::uintptr_t> cooking{ RE::VTABLE::CookingMenu[0] };
  BuildConfirmed<RE::WorkbenchMenuBase>::original =
    cooking.write_vfunc(0x17, BuildConfirmed<RE::WorkbenchMenuBase>::Thunk);
  REL::Relocation<std::uintptr_t> scrap{ RE::VTABLE::__ScrapItemCallback[0] };
  ScrapAccept::original = scrap.write_vfunc(0x1, ScrapAccept::Thunk);
  REX::INFO("Installed the workbench hooks");
}

// --- Saves -----------------------------------------------------------------

void BeforeSave()
{
  for (auto& [token, w] : g_wires) {
    if (w.current) {
      RemoveWireRef(game::Ref(w.current));
      w.current = 0;
    }
    w.attempts = 0;
    w.nextTryMs = 0;
  }
  for (auto& [id, s] : g_objects) {
    Unspawn(s);
  }
  for (auto id : g_disabledPrePlaced) {
    if (auto ref = game::Ref(id)) {
      ref->Enable(false);
    }
  }
}

void AfterSave()
{
  for (auto& [id, s] : g_objects) {
    if (!s.scrapped && !s.current) {
      Respawn(s);
    }
  }
  for (auto id : g_disabledPrePlaced) {
    if (auto ref = game::Ref(id)) {
      ref->Disable();
    }
  }
  // Wires come back through WiresFrame once their ends have 3D
}
}

void InstallWorkshop(Platform& p)
{
  p.RegisterNative("enterWorkshopMode", [&p](const Json& a) -> Json {
    EnterWorkshopMode(p, a.at(0).get<uint32_t>(), a.at(1).get<uint32_t>());
    return nullptr;
  });
  p.RegisterNative("exitWorkshopMode", [](const Json&) -> Json {
    ExitWorkshopMode();
    return nullptr;
  });
  p.RegisterNative("spawnWorkshopObject", [&p](const Json& a) -> Json {
    return SpawnObject(p, a.at(0));
  });
  p.RegisterNative("moveWorkshopObject", [](const Json& a) -> Json {
    MoveObject(a.at(0).get<uint32_t>(), a.at(1), a.at(2));
    return nullptr;
  });
  p.RegisterNative("deleteWorkshopObject", [](const Json& a) -> Json {
    DeleteObject(a.at(0).get<uint32_t>());
    return nullptr;
  });
  p.RegisterNative("setPrePlacedDisabled", [](const Json& a) -> Json {
    SetPrePlacedDisabled(a.at(0).get<uint32_t>(), a.at(1).get<bool>());
    return nullptr;
  });
  p.RegisterNative("spawnWire", [](const Json& a) -> Json {
    return SpawnWire(a.at(0).get<uint32_t>(), a.at(1).get<uint32_t>(),
                     a.at(2).get<uint32_t>());
  });
  p.RegisterNative("deleteWire", [](const Json& a) -> Json {
    DeleteWire(a.at(0).get<uint32_t>());
    return nullptr;
  });
  p.RegisterNative("setWorkshopRatings", [](const Json& a) -> Json {
    SetRatings(a.at(0).get<uint32_t>(), a.at(1));
    return nullptr;
  });

  p.OnFrame([&p](float) {
    CaptureFrame(p);
    TryStartWorkshop(p);
    WiresFrame(p);
  });
  // Server-placed objects and wires are network-only: they leave the world
  // for the save and come back after it under new form ids (the client
  // keeps its ids). Pre-placed references disabled for the server are saved
  // enabled.
  p.OnBeforeSave([] { BeforeSave(); });
  p.OnAfterSave([&p] { p.QueueTask([] { AfterSave(); }); });

  static ModeSink modeSink;
  static PlacedSink placedSink;
  static MovedSink movedSink;
  static DestroyedSink destroyedSink;
  RE::Workshop::RegisterForWorkshopModeEvent(&modeSink);
  RE::Workshop::RegisterForItemPlaced(&placedSink);
  RE::Workshop::RegisterForItemMoved(&movedSink);
  RE::Workshop::RegisterForItemDestroyed(&destroyedSink);

  InstallCraftingHooks();
}

}
