// F04/F06 inventory natives and the player's inventory requests.
//
// Natives: getInventoryEx, addItemEx, removeItemEx (ItemKeys.h does the
// engine work), setAmmoLoaded.
//
// Capture (the server inventory is the truth; the engine change stays
// optimistic and the server's SetInventoryFo4 confirms or reverts it):
//   useItemRequested            TESEquipEvent of a consumable (ALCH) on the
//                               player (Pip-Boy, favorites, quick keys)
//   dropRequested               TESContainerChangedEvent player -> world
//                               with a new reference; the key is read from
//                               that reference, which is then deleted (the
//                               server places the dropped item for
//                               everyone, the dropper included)
//   containerTransferRequested  TESContainerChangedEvent player <->
//                               container while the ContainerMenu is open;
//                               the moved instances come from a diff of the
//                               container against a snapshot taken when the
//                               menu opened (the event carries no instance
//                               data), so menu transfers and "take all"
//                               are both covered
// Echo loops: changes made by natives hold items::ApplyGuard (the sinks
// skip them) and refresh the snapshot (items::OnOwnChange), so server state
// never comes back as a request. Puppets are never reported.
//
// Puppets (other players' actors) are never looted: a ContainerMenu opened
// on a puppet is closed at once, and anything moved before it closed is
// moved back.
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
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace fmp::modules {

namespace {
using items::Key;

constexpr auto kContainerMenu = "ContainerMenu";

// --- Natives ---------------------------------------------------------------

// A count argument: a positive integer, at most INT32_MAX (engine counts
// are signed). 0 = invalid.
uint32_t CountArg(const Json& j)
{
  if (!j.is_number()) {
    return 0;
  }
  double v = j.get<double>();
  if (!std::isfinite(v) || v < 1) {
    return 0;
  }
  constexpr double kMax = std::numeric_limits<std::int32_t>::max();
  return static_cast<uint32_t>(std::min(std::floor(v), kMax));
}

bool BoolArg(const Json& j)
{
  if (j.is_boolean()) {
    return j.get<bool>();
  }
  return j.is_number() && j.get<double>() != 0;
}

// Inventories the network may change: containers, the player, puppets and
// corpses (the server sends corpse contents). Never a living NPC a script
// names.
bool MayChange(RE::TESObjectREFR* ref)
{
  if (!ref) {
    return false;
  }
  auto id = ref->GetFormID();
  if (!ref->As<RE::Actor>()) {
    return true;
  }
  return id == game::kPlayerRef || puppets::IsPuppet(id) || ref->IsDead(false);
}

Json GetInventory(uint32_t refId)
{
  auto out = Json::array();
  auto ref = game::Ref(refId);
  if (!ref) {
    return out;
  }
  // One entry per stack identity (equal game stacks, e.g. a worn and a
  // spare copy of the same instance, are summed)
  std::vector<items::Stack> merged;
  for (auto& s : items::Read(ref)) {
    auto it = std::find_if(merged.begin(), merged.end(), [&](auto& m) {
      return items::SameStack(m.key, s.key);
    });
    if (it == merged.end()) {
      merged.push_back(std::move(s));
    } else {
      it->count += s.count;
    }
  }
  for (auto& m : merged) {
    out.push_back(
      Json{ { "item", items::ToJson(m.key) }, { "count", m.count } });
  }
  return out;
}

void ChangeItems(Platform& p, const Json& a, bool add)
{
  uint32_t refId = a.at(0).get<uint32_t>();
  auto key = items::FromJson(a.at(1));
  uint32_t count = CountArg(a.at(2));
  bool silent = a.size() > 3 ? BoolArg(a.at(3)) : true;
  auto ref = game::Ref(refId);
  if (!count || !MayChange(ref)) {
    p.Log("warn",
          std::format("{}ItemEx: refused for {:X} (item {:X} x{})",
                      add ? "add" : "remove", refId, key.baseId, count));
    return;
  }
  bool ok = add ? items::Add(ref, key, count, silent)
                : items::Remove(ref, key, count, silent);
  if (!ok) {
    p.Log("warn",
          std::format("{}ItemEx: {:X} x{} on {:X} failed",
                      add ? "add" : "remove", key.baseId, count, refId));
  }
}

void SetAmmoLoaded(uint32_t actorId, const Json& roundsJson)
{
  auto actor = game::ActorOf(actorId);
  if (!actor || (actorId != game::kPlayerRef && !puppets::IsPuppet(actorId))) {
    return;
  }
  double v = roundsJson.is_number() ? roundsJson.get<double>() : 0;
  if (!std::isfinite(v) || v < 0) {
    v = 0;
  }
  auto rounds = static_cast<std::uint32_t>(std::min(v, 65535.0));
  // [verify] equip index 0 is the weapon in hand, and the count shows in
  // the HUD ammo counter right away (also after a reload animation)
  RE::BGSEquipIndex index{ 0 };
  if (!actor->GetCurrentAmmo(index)) {
    return; // no weapon that uses ammo
  }
  items::ApplyGuard guard;
  actor->SetCurrentAmmoCount(index, rounds);
}

// --- Capture ---------------------------------------------------------------

struct DropEvent
{
  uint32_t ref = 0;
  uint32_t base = 0;
  std::int32_t count = 0;
};

struct TransferEvent
{
  uint32_t container = 0;
  uint32_t base = 0;
};

// Filled by the sinks (any thread), drained on the main thread
std::mutex g_mutex;
std::vector<DropEvent> g_drops;
std::vector<TransferEvent> g_transfers;
std::vector<uint32_t> g_uses;

// The container whose menu is open (main thread only)
struct OpenContainer
{
  uint32_t refId = 0;
  bool puppet = false;
  std::vector<items::Stack> snapshot;
};
std::optional<OpenContainer> g_open;

// Net change per stack identity between two reads, for the given bases
// (after - before: negative = left the container).
std::vector<std::pair<Key, int64_t>> Diff(
  const std::vector<items::Stack>& before,
  const std::vector<items::Stack>& after, const std::vector<uint32_t>& bases)
{
  std::vector<std::pair<Key, int64_t>> acc;
  auto add = [&](const items::Stack& s, int64_t sign) {
    if (std::find(bases.begin(), bases.end(), s.key.baseId) == bases.end()) {
      return;
    }
    for (auto& [k, n] : acc) {
      if (items::SameStack(k, s.key)) {
        n += sign * static_cast<int64_t>(s.count);
        return;
      }
    }
    acc.emplace_back(s.key, sign * static_cast<int64_t>(s.count));
  };
  for (auto& s : before) {
    add(s, -1);
  }
  for (auto& s : after) {
    add(s, 1);
  }
  std::erase_if(acc, [](auto& e) { return e.second == 0; });
  return acc;
}

void CloseContainerMenu()
{
  if (auto queue = RE::UIMessageQueue::GetSingleton()) {
    queue->AddMessage(RE::BSFixedString(kContainerMenu),
                      RE::UI_MESSAGE_TYPE::kHide);
  }
}

// Moves items taken from (or put into) a puppet back where they were.
void RevertPuppetTransfer(RE::TESObjectREFR* puppet, const Key& key,
                          int64_t delta)
{
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!player || !puppet || delta == 0) {
    return;
  }
  auto n = static_cast<uint32_t>(std::min<int64_t>(
    delta < 0 ? -delta : delta, std::numeric_limits<std::int32_t>::max()));
  if (delta < 0) { // taken from the puppet
    items::Remove(player, key, n, true);
    items::Add(puppet, key, n, true);
  } else {
    items::Remove(puppet, key, n, true);
    items::Add(player, key, n, true);
  }
}

void ProcessTransfers(Platform& p)
{
  std::vector<TransferEvent> events;
  {
    std::lock_guard l(g_mutex);
    events.swap(g_transfers);
  }
  if (events.empty() || !g_open) {
    return;
  }
  std::vector<uint32_t> bases;
  for (auto& e : events) {
    if (e.container == g_open->refId &&
        std::find(bases.begin(), bases.end(), e.base) == bases.end()) {
      bases.push_back(e.base);
    }
  }
  if (bases.empty()) {
    return;
  }
  auto ref = game::Ref(g_open->refId);
  if (!ref) {
    return;
  }
  auto now = items::Read(ref);
  auto deltas = Diff(g_open->snapshot, now, bases);
  uint32_t container = g_open->refId;
  bool puppet = g_open->puppet;
  for (auto& [key, delta] : deltas) {
    if (puppet) {
      p.Log("warn",
            std::format("Reverting a transfer with puppet {:X}", container));
      RevertPuppetTransfer(ref, key, delta);
      continue;
    }
    p.Emit("containerTransferRequested",
           Json{ { "container", container },
                 { "item", items::ToJson(key) },
                 { "count", delta < 0 ? -delta : delta },
                 { "take", delta < 0 } });
  }
  if (g_open) { // the reverts may have run the listeners
    g_open->snapshot = puppet ? items::Read(ref) : std::move(now);
  }
}

void ProcessDrops(Platform& p)
{
  std::vector<DropEvent> drops;
  {
    std::lock_guard l(g_mutex);
    drops.swap(g_drops);
  }
  for (auto& d : drops) {
    // Placing items in workshop mode belongs to the workshop module
    if (d.count <= 0 || game::MenuOpen("WorkshopMenu")) {
      continue;
    }
    Key key;
    key.baseId = d.base;
    auto ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>(d.ref);
    auto object = ref ? ref->GetObjectReference() : nullptr;
    if (object && object->GetFormID() == d.base) {
      key = items::KeyOf(object, ref->extraList.get());
    } else {
      ref = nullptr; // not the dropped item (anymore)
    }
    p.Emit("dropRequested",
           Json{ { "item", items::ToJson(key) }, { "count", d.count } });
    if (ref) {
      // The server places the dropped item (CreateActor for neighbours and
      // the dropper); a refused drop gives the item back through
      // SetInventoryFo4. [verify] TESContainerChangedEvent names the new
      // world reference in referenceFormID for Pip-Boy drops
      ref->Disable();
      ref->SetWantsDelete(true);
      papyrus::CallMethod(ref, "ObjectReference", "Delete", nullptr);
    }
  }
}

void ProcessUses(Platform& p)
{
  std::vector<uint32_t> uses;
  {
    std::lock_guard l(g_mutex);
    uses.swap(g_uses);
  }
  for (auto base : uses) {
    auto form = RE::TESForm::GetFormByID(base);
    if (!form || !form->IsAlchemyItem()) {
      continue;
    }
    // Report only: the vanilla consumption still runs (the server's
    // inventory update then matches it). Cancelling it (F20) needs a hook
    // of the equip path that CommonLibF4 has no safe site for.
    // [verify] TESEquipEvent fires for chems/food used from the Pip-Boy,
    // the favorites bar and the quick stimpak key
    p.Emit("useItemRequested", Json{ { "baseId", base } });
  }
}

void OnContainerMenu(Platform& p, bool opening)
{
  if (!opening) {
    ProcessTransfers(p);
    g_open.reset();
    return;
  }
  RE::NiPointer<RE::TESObjectREFR> ref;
  if (auto ui = RE::UI::GetSingleton()) {
    if (auto menu = ui->GetMenu<RE::ContainerMenu>()) {
      ref = menu->containerRef.get();
    }
  }
  if (!ref) {
    g_open.reset();
    return;
  }
  {
    std::lock_guard l(g_mutex);
    g_transfers.clear(); // anything before the open is not a menu transfer
  }
  OpenContainer open;
  open.refId = ref->GetFormID();
  open.puppet = puppets::IsPuppet(open.refId);
  open.snapshot = items::Read(ref.get());
  g_open = std::move(open);
  if (g_open->puppet) {
    p.Log(
      "info",
      std::format("Closing the container menu of puppet {:X}", g_open->refId));
    CloseContainerMenu();
  }
}

class ContainerSink final
  : public RE::BSTEventSink<RE::TESContainerChangedEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::TESContainerChangedEvent& e,
    RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
  {
    constexpr uint32_t player = game::kPlayerRef;
    uint32_t from = e.oldContainerFormID;
    uint32_t to = e.newContainerFormID;
    std::lock_guard l(g_mutex);
    if (from == player && to == 0) {
      // Consumed or destroyed items have no reference; a drop creates one
      if (e.referenceFormID != 0 && !items::Applying()) {
        g_drops.push_back(
          { e.referenceFormID, e.baseObjectFormID, e.itemCount });
      }
    } else if (from == player && to != 0 && to != player) {
      g_transfers.push_back({ to, e.baseObjectFormID });
    } else if (to == player && from != 0 && from != player) {
      g_transfers.push_back({ from, e.baseObjectFormID });
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};

class UseSink final : public RE::BSTEventSink<RE::TESEquipEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::TESEquipEvent& e,
    RE::BSTEventSource<RE::TESEquipEvent>*) override
  {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (e.equipped && player && e.actor.get() == player &&
        !items::Applying()) {
      std::lock_guard l(g_mutex);
      g_uses.push_back(e.baseObject);
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};

class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::MenuOpenCloseEvent& e,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
  {
    if (e.menuName == std::string_view(kContainerMenu)) {
      bool opening = e.opening;
      auto& p = Platform::Get();
      p.QueueTask([&p, opening] { OnContainerMenu(p, opening); });
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};
}

void InstallInventory(Platform& p)
{
  p.RegisterNative("getInventoryEx", [](const Json& a) -> Json {
    return GetInventory(a.at(0).get<uint32_t>());
  });
  p.RegisterNative("addItemEx", [&p](const Json& a) -> Json {
    ChangeItems(p, a, true);
    return nullptr;
  });
  p.RegisterNative("removeItemEx", [&p](const Json& a) -> Json {
    ChangeItems(p, a, false);
    return nullptr;
  });
  p.RegisterNative("setAmmoLoaded", [](const Json& a) -> Json {
    SetAmmoLoaded(a.at(0).get<uint32_t>(), a.at(1));
    return nullptr;
  });

  // The open container's snapshot follows the changes natives make, so
  // only the engine's own transfers show up in the diff. Engine transfers
  // still pending are reported first.
  items::OnOwnChange([&p](uint32_t refId, bool before) {
    if (!g_open || g_open->refId != refId) {
      return;
    }
    if (before) {
      ProcessTransfers(p);
      return;
    }
    if (auto ref = game::Ref(refId); ref && g_open) {
      g_open->snapshot = items::Read(ref);
    }
  });

  p.OnFrame([&p](float) {
    ProcessUses(p);
    ProcessDrops(p);
    ProcessTransfers(p);
  });

  static ContainerSink containerSink;
  static UseSink useSink;
  static MenuSink menuSink;
  if (auto src = RE::TESContainerChangedEvent::GetEventSource()) {
    src->RegisterSink(&containerSink);
  } else {
    p.Log("error",
          "No TESContainerChangedEvent source: drops and container "
          "transfers are not reported");
  }
  if (auto src = RE::TESEquipEvent::GetEventSource()) {
    src->RegisterSink(&useSink);
  }
  if (auto ui = RE::UI::GetSingleton()) {
    ui->RegisterSink<RE::MenuOpenCloseEvent>(&menuSink);
  }
}

}
