// F05 equipment natives and the player's equip requests.
//
// Natives: equipItemEx / unequipItemEx go through ActorEquipManager
// (EquipObject / UnequipObject) with a real BGSObjectInstance (the
// inventory stack's instance data), so OMOD meshes and material swaps come
// from the engine. getEquippedItems lists the worn weapon and apparel
// stacks (power armor pieces excluded: the power armor module owns them).
// Puppets only hold what they wear: the item is added silently before it
// is equipped and removed after it is unequipped. The player's inventory
// is the inventory service's business: nothing is added or removed there.
//
// Capture: equipRequested. EquipmentService sends the request and the game
// only follows the server's state; the engine change stays optimistic and a
// refused request comes back as a correction (the service then unequips).
// TESEquipEvent on the player marks the equipment dirty; after 100 ms
// without further events the worn set is compared with the last known one,
// which coalesces Pip-Boy bursts and the transient unequip/re-equip when a
// weapon is readied. Changes made by natives (items::ApplyGuard, and the
// re-baseline in items::OnOwnChange) are never reported; neither are power
// armor transitions (F17).
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "ItemKeys.h"
#include "Modules.h"
#include "Platform.h"
#include "Puppets.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <optional>
#include <string_view>
#include <vector>

namespace fmp::modules {

namespace {
using items::Key;

constexpr double kCoalesceMs = 100;

bool BoolArg(const Json& j)
{
  if (j.is_boolean()) {
    return j.get<bool>();
  }
  return j.is_number() && j.get<double>() != 0;
}

bool IsPowerArmorPiece(const RE::TESBoundObject* object)
{
  auto armor = object ? object->As<RE::TESObjectARMO>() : nullptr;
  auto keyword = RE::PowerArmor::GetArmorKeyword();
  return armor && keyword && armor->HasKeywordID(keyword->GetFormID());
}

// Worn weapon and apparel stacks of an actor, with the loaded rounds of
// the weapon in hand.
std::vector<Key> Worn(RE::Actor* actor, bool apparel = true)
{
  std::vector<Key> out;
  if (!actor) {
    return out;
  }
  RE::BGSEquipIndex hand{ 0 };
  for (auto& s : items::Read(actor)) {
    if (!s.equipped || !s.object) {
      continue;
    }
    auto type = s.object->GetFormType();
    if (type == RE::ENUM_FORM_ID::kWEAP) {
      auto key = s.key;
      auto weap = s.object->As<RE::TESObjectWEAP>();
      // [verify] equip index 0 holds the drawn gun (grenades use another
      // slot) and GetCurrentAmmoCount is the magazine, not the reserve
      if (weap && weap->IsGunWeapon() && actor->GetCurrentAmmo(hand)) {
        key.ammoLoaded = static_cast<uint16_t>(
          std::min<std::uint32_t>(actor->GetCurrentAmmoCount(hand), 0xFFFF));
      }
      out.push_back(std::move(key));
    } else if (apparel && type == RE::ENUM_FORM_ID::kARMO &&
               !IsPowerArmorPiece(s.object)) {
      out.push_back(s.key);
    }
  }
  return out;
}

// Actors the network may dress: the player and puppets.
RE::Actor* Target(Platform& p, uint32_t actorId, const char* what)
{
  auto actor = game::ActorOf(actorId);
  if (!actor ||
      (actorId != game::kPlayerRef && !puppets::IsPuppet(actorId))) {
    p.Log("warn", std::format("{}: {:X} is not the player or a puppet", what,
                              actorId));
    return nullptr;
  }
  return actor;
}

void Equip(Platform& p, uint32_t actorId, const Key& key,
           bool preventRemoval, bool silent)
{
  auto actor = Target(p, actorId, "equipItemEx");
  auto manager = RE::ActorEquipManager::GetSingleton();
  if (!actor || !manager) {
    return;
  }
  bool puppet = puppets::IsPuppet(actorId);
  items::NotifyOwnChange(actorId, true);
  {
    items::ApplyGuard guard;
    auto inst = items::Find(actor, key, true);
    if (!inst && puppet) {
      items::Add(actor, key, 1, true);
      inst = items::Find(actor, key, true);
    }
    if (!inst) {
      p.Log("warn", std::format("equipItemEx: {:X} has no {:X} with this "
                                "instance",
                                actorId, key.baseId));
    } else if (!inst->equipped) {
      RE::BGSObjectInstance object{ inst->object,
                                    items::InstanceDataOf(actor, *inst) };
      // queueEquip false + applyNow true: equipped before this returns,
      // so getEquippedItems sees it at once. [verify] the default slot
      // (null) is right for weapons and apparel, and a puppet with AI off
      // shows the weapon and the OMOD meshes
      bool ok = manager->EquipObject(actor, object, inst->stackIndex, 1,
                                     nullptr, false, true, !silent, true,
                                     preventRemoval);
      if (!ok) {
        p.Log("warn", std::format("equipItemEx: the engine refused {:X} on "
                                  "{:X}",
                                  key.baseId, actorId));
      }
    }
  }
  items::NotifyOwnChange(actorId, false);
}

void Unequip(Platform& p, uint32_t actorId, const Key& key, bool silent)
{
  auto actor = Target(p, actorId, "unequipItemEx");
  auto manager = RE::ActorEquipManager::GetSingleton();
  if (!actor || !manager) {
    return;
  }
  bool puppet = puppets::IsPuppet(actorId);
  items::NotifyOwnChange(actorId, true);
  {
    items::ApplyGuard guard;
    auto inst = items::Find(actor, key, true);
    if (inst && inst->equipped) {
      if (inst->locked) {
        // Equipped with preventRemoval: unlock first. [verify] the engine
        // unequips after this (forceEquip alone may not override the lock)
        actor->SetEquipStateLocked(inst->object, false);
      }
      RE::BGSObjectInstance object{ inst->object,
                                    items::InstanceDataOf(actor, *inst) };
      manager->UnequipObject(actor, &object, 1, nullptr, inst->stackIndex,
                             false, true, !silent, true, nullptr);
    }
    if (puppet && inst) {
      items::Remove(actor, key, 1, true); // puppets carry what they wear
    }
  }
  items::NotifyOwnChange(actorId, false);
}

Json GetEquipped(uint32_t actorId)
{
  auto out = Json::array();
  for (auto& k : Worn(game::ActorOf(actorId))) {
    out.push_back(items::ToJson(k));
  }
  return out;
}

// --- Capture ---------------------------------------------------------------

std::atomic<uint64_t> g_eventSeq{ 0 };     // player equip events (sink)
std::atomic<bool> g_resetBaseline{ false }; // a loading screen came up
uint64_t g_seenSeq = 0;
double g_lastEventMs = 0;
bool g_pending = false;
std::optional<std::vector<Key>> g_baseline; // last known worn set
bool g_baselineInPA = false;

void Rebaseline()
{
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!player || game::Loading()) {
    g_baseline.reset();
    return;
  }
  g_baseline = Worn(player);
  g_baselineInPA = RE::PowerArmor::ActorInPowerArmor(*player);
}

// Compares the worn set with the baseline and reports the differences.
void Capture(Platform& p)
{
  g_pending = false;
  g_seenSeq = g_eventSeq.load();
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!player || game::Loading()) {
    g_baseline.reset();
    return;
  }
  bool inPA = RE::PowerArmor::ActorInPowerArmor(*player);
  if (!g_baseline || inPA != g_baselineInPA) {
    Rebaseline(); // entering/leaving power armor is F17's
    return;
  }
  auto current = Worn(player);
  auto removed = *g_baseline;
  std::vector<Key> added;
  for (auto& k : current) {
    auto it = std::find_if(removed.begin(), removed.end(), [&](auto& r) {
      return items::SameStack(r, k);
    });
    if (it != removed.end()) {
      removed.erase(it);
    } else {
      added.push_back(k);
    }
  }
  // In power armor the apparel is the frame's; only weapons are reported
  auto skip = [&](const Key& k) {
    if (!inPA) {
      return false;
    }
    auto form = RE::TESForm::GetFormByID(k.baseId);
    return !form || !form->IsWeapon();
  };
  for (auto& k : removed) {
    if (!skip(k)) {
      p.Emit("equipRequested",
             Json{ { "item", items::ToJson(k) }, { "equip", false } });
    }
  }
  for (auto& k : added) {
    if (!skip(k)) {
      p.Emit("equipRequested",
             Json{ { "item", items::ToJson(k) }, { "equip", true } });
    }
  }
  g_baseline = std::move(current);
}

void OnFrame(Platform& p)
{
  if (g_resetBaseline.exchange(false) || game::Loading()) {
    g_baseline.reset();
    g_pending = false;
    g_seenSeq = g_eventSeq.load();
    return;
  }
  if (auto seq = g_eventSeq.load(); seq != g_seenSeq) {
    g_seenSeq = seq;
    g_lastEventMs = p.NowMs();
    g_pending = true;
  }
  if (!g_baseline) {
    // First frame of a session (or after a load): what the save had on is
    // not a request
    g_pending = false;
    Rebaseline();
    return;
  }
  if (g_pending && p.NowMs() - g_lastEventMs >= kCoalesceMs) {
    Capture(p);
  }
}

class EquipSink final : public RE::BSTEventSink<RE::TESEquipEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::TESEquipEvent& e,
    RE::BSTEventSource<RE::TESEquipEvent>*) override
  {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (player && e.actor.get() == player && !items::Applying()) {
      ++g_eventSeq;
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};

class LoadingSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::MenuOpenCloseEvent& e,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
  {
    if (e.opening && e.menuName == std::string_view("LoadingMenu")) {
      g_resetBaseline = true;
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};
}

void InstallEquipment(Platform& p)
{
  p.RegisterNative("equipItemEx", [&p](const Json& a) -> Json {
    bool preventRemoval = a.size() > 2 && BoolArg(a.at(2));
    bool silent = a.size() > 3 ? BoolArg(a.at(3)) : true;
    Equip(p, a.at(0).get<uint32_t>(), items::FromJson(a.at(1)),
          preventRemoval, silent);
    return nullptr;
  });
  p.RegisterNative("unequipItemEx", [&p](const Json& a) -> Json {
    Unequip(p, a.at(0).get<uint32_t>(), items::FromJson(a.at(1)),
            a.size() > 2 ? BoolArg(a.at(2)) : true);
    return nullptr;
  });
  p.RegisterNative("getEquippedItems", [](const Json& a) -> Json {
    return GetEquipped(a.at(0).get<uint32_t>());
  });

  // Around every change natives make to the player: report pending engine
  // changes first, then take the result as the new baseline.
  items::OnOwnChange([&p](uint32_t refId, bool before) {
    if (refId != game::kPlayerRef) {
      return;
    }
    if (before) {
      if (g_baseline && (g_pending || g_eventSeq.load() != g_seenSeq)) {
        Capture(p);
      }
    } else {
      Rebaseline();
    }
  });

  p.OnFrame([&p](float) { OnFrame(p); });

  static EquipSink equipSink;
  static LoadingSink loadingSink;
  if (auto src = RE::TESEquipEvent::GetEventSource()) {
    src->RegisterSink(&equipSink);
  } else {
    p.Log("error", "No TESEquipEvent source: equip requests are not reported");
  }
  if (auto ui = RE::UI::GetSingleton()) {
    ui->RegisterSink<RE::MenuOpenCloseEvent>(&loadingSink);
  }
}

}
