// Protocol items (ItemKey) <-> game inventories.
//
// Add builds the instance data itself instead of AddItem +
// ObjectReference.AttachModToInventoryItem (ambiguous with more than one
// instance, and asynchronous): a new ExtraDataList gets a
// BGSObjectInstanceExtra with every OMOD (BGSObjectInstanceExtra::AddMod),
// the instance data computed from it (ExtraDataList::CreateInstanceData),
// an ExtraHealth for the condition and an ExtraOwnership for stolen items,
// and is handed to TESObjectREFR::AddObjectToContainer (vfunc 0x7A).
// Remove goes through TESObjectREFR::RemoveItem (vfunc 0x6D) with the stack
// index of the matching instance. Both are synchronous and make no HUD
// message; a non-silent call shows one itself.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "ItemKeys.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <limits>
#include <string>

namespace fmp::items {

namespace {
// CommonLibF4 has the RTTI and vtable ids of ExtraOwnership but no class.
// Layout like the other one-pointer extras (ExtraHealth: BSExtraData 0x18 +
// value at 0x18). [verify] owner at 0x18 and sizeof 0x20 (compare with a
// stolen item's extra list in a debugger, or read back with GetActorOwner)
class __declspec(novtable) OwnershipExtra : public RE::BSExtraData
{
public:
  static constexpr auto VTABLE{ RE::VTABLE::ExtraOwnership };
  static constexpr auto TYPE{ RE::EXTRA_DATA_TYPE::kOwnership };

  explicit OwnershipExtra(RE::TESForm* a_owner)
    : BSExtraData(TYPE)
    , owner(a_owner)
  {
    REX::EMPLACE_VTABLE(this);
  }

  // members
  RE::TESForm* owner; // 18
};
static_assert(sizeof(OwnershipExtra) == 0x20);

// Owners that don't make an item stolen: the player (base NPC and
// reference) and PlayerFaction. [verify] items the player bought or
// crafted carry no ownership (or one of these)
constexpr uint32_t kPlayerBase = 0x7;
constexpr uint32_t kPlayerRef = 0x14;
constexpr uint32_t kPlayerFaction = 0x1C21C;

// Mods are added at attach index 0, rank 1 (the protocol carries the OMOD
// ids only). [verify] the engine re-slots them by attach point keyword and
// the instance shows every mod (Pip-Boy, inspect) for a 3-OMOD weapon
constexpr uint8_t kModAttachIndex = 0;
constexpr uint8_t kModRank = 1;

std::atomic<int> g_applying{ 0 };
std::vector<ChangeListener> g_listeners;

uint32_t JsonU32(const Json& j, const char* name, uint32_t def = 0)
{
  auto it = j.find(name);
  if (it == j.end() || it->is_null()) {
    return def;
  }
  if (it->is_number_unsigned()) {
    return it->get<uint32_t>();
  }
  if (it->is_number_integer()) {
    auto v = it->get<int64_t>();
    return v < 0 ? def : static_cast<uint32_t>(v);
  }
  if (it->is_number_float()) {
    auto v = it->get<double>();
    return v < 0 || !std::isfinite(v) ? def : static_cast<uint32_t>(v);
  }
  return def;
}

uint16_t ClampU16(uint32_t v)
{
  return static_cast<uint16_t>(std::min<uint32_t>(v, 0xFFFF));
}

bool IsPlayerOwner(uint32_t owner)
{
  return owner == kPlayerBase || owner == kPlayerRef ||
    owner == kPlayerFaction;
}

void ShowMessage(const RE::TESBoundObject* object, uint32_t count,
                 bool added)
{
  if (!object) {
    return;
  }
  auto name = std::string(RE::TESFullName::GetFullName(*object));
  if (name.empty()) {
    return;
  }
  auto text = count > 1
    ? std::format("{} ({}) {}", name, count, added ? "added" : "removed")
    : std::format("{} {}", name, added ? "added" : "removed");
  RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, true, false);
}

// The extra data of a new instance, or null for a plain item.
RE::BSTSmartPointer<RE::ExtraDataList> BuildExtra(RE::TESBoundObject* object,
                                                  const Key& key)
{
  std::vector<RE::BGSMod::Attachment::Mod*> mods;
  for (auto id : key.mods) {
    auto mod = RE::TESForm::GetFormByID<RE::BGSMod::Attachment::Mod>(id);
    if (!mod) {
      REX::WARN("Item {:X}: unknown object mod {:X} skipped", key.baseId, id);
      continue;
    }
    mods.push_back(mod);
  }
  RE::TESForm* owner = key.stolenFrom
    ? RE::TESForm::GetFormByID(key.stolenFrom)
    : nullptr;
  bool health = key.condition != kConditionFull;
  if (mods.empty() && !owner && !health) {
    return {};
  }

  RE::BSTSmartPointer<RE::ExtraDataList> extra{ new RE::ExtraDataList() };
  if (!mods.empty()) {
    // [verify] AddMod allocates the index buffer of an empty instance
    // (values == nullptr after the default constructor)
    auto instance = new RE::BGSObjectInstanceExtra();
    for (auto mod : mods) {
      instance->AddMod(*mod, kModAttachIndex, kModRank, false);
    }
    extra->AddExtra(instance);
    // Effective stats (ExtraInstanceData) and the instance name ("Hardened
    // 10mm Pistol"). [verify] the inventory shows the modded stats; if the
    // engine builds them lazily this call is harmless
    extra->CreateInstanceData(object, true);
  }
  if (health) {
    // [verify] ExtraHealth holds a 0..1 fraction for power armor pieces
    // and fusion cores (as ExtraDataList::Get/SetHealthPerc suggest)
    extra->AddExtra(new RE::ExtraHealth(ConditionToFraction(key.condition)));
  }
  if (owner) {
    extra->AddExtra(new OwnershipExtra(owner));
  }
  return extra;
}

void RemoveImpl(RE::TESObjectREFR* ref, const Key& key, uint32_t count,
                uint32_t& removed)
{
  removed = 0;
  // One stack per call: removing a whole stack shifts the indices of the
  // next ones, so the match is searched again each time. Unequipped stacks
  // go first so worn items stay on.
  for (int guard = 0; removed < count && guard < 64; ++guard) {
    auto inst = Find(ref, key, false);
    if (!inst || !inst->object || inst->count == 0) {
      break;
    }
    uint32_t n = std::min(count - removed, inst->count);
    RE::TESObjectREFR::RemoveItemData data{ inst->object,
                                            static_cast<std::int32_t>(n) };
    data.stackData.push_back(inst->stackIndex);
    data.reason = RE::ITEM_REMOVE_REASON::kNone;
    // No destination container and no drop location: the items are
    // destroyed. [verify] no world reference appears; if one does it is
    // deleted here
    auto dropped = ref->RemoveItem(data);
    if (auto d = dropped.get()) {
      d->Disable();
      d->SetWantsDelete(true);
    }
    removed += n;
  }
}

bool AddImpl(RE::TESObjectREFR* ref, const Key& key, uint32_t count)
{
  auto object = key.baseId
    ? RE::TESForm::GetFormByID<RE::TESBoundObject>(key.baseId)
    : nullptr;
  if (!object) {
    REX::WARN("addItem: {:X} is not an item", key.baseId);
    return false;
  }
  constexpr auto kMaxCount =
    static_cast<uint32_t>(std::numeric_limits<std::int32_t>::max());
  count = std::min(count, kMaxCount);

  // Adding a weapon may drag a few rounds of its ammo along (prior-art
  // §3.1.11); the server inventory has none of them, so they are stripped.
  // [verify] whether AddObjectToContainer does this at all (Papyrus
  // AddItem does)
  RE::TESBoundObject* ammo = nullptr;
  uint32_t ammoBefore = 0;
  if (auto weap = object->As<RE::TESObjectWEAP>()) {
    ammo = weap->weaponData.ammo;
    if (ammo == object) {
      ammo = nullptr;
    }
    if (ammo) {
      ammoBefore = ref->GetInventoryObjectCount(ammo);
    }
  }

  // [verify] a plain weapon (null extra) gets no random template roll
  // (OBTE default mods); if it does, pass the default combination as mods
  auto extra = BuildExtra(object, key);
  ref->AddObjectToContainer(object, extra, static_cast<std::int32_t>(count),
                            nullptr, RE::ITEM_REMOVE_REASON::kNone);

  if (ammo) {
    uint32_t after = ref->GetInventoryObjectCount(ammo);
    if (after > ammoBefore) {
      Key ammoKey;
      ammoKey.baseId = ammo->GetFormID();
      uint32_t removed = 0;
      RemoveImpl(ref, ammoKey, after - ammoBefore, removed);
    }
  }
  return true;
}
}

float ConditionToFraction(uint16_t c)
{
  if (c == kConditionFull) {
    return 1.f;
  }
  if (c == kConditionZero) {
    return 0.f;
  }
  return std::min(1.f, static_cast<float>(c) / 1000.f);
}

uint16_t FractionToCondition(float f)
{
  if (!(f > 0.f)) { // also catches NaN
    return kConditionZero;
  }
  if (f >= 0.9995f) {
    return kConditionFull;
  }
  return static_cast<uint16_t>(
    std::clamp(static_cast<int>(std::lround(f * 1000.f)), 1, 999));
}

Key FromJson(const Json& j)
{
  Key k;
  k.baseId = j.at("baseId").get<uint32_t>();
  if (auto it = j.find("mods"); it != j.end() && it->is_array()) {
    for (auto& m : *it) {
      if (m.is_number()) {
        k.mods.push_back(m.get<uint32_t>());
      }
    }
    std::sort(k.mods.begin(), k.mods.end());
  }
  k.condition = ClampU16(JsonU32(j, "condition"));
  k.stolenFrom = JsonU32(j, "stolenFrom");
  k.ammoLoaded = ClampU16(JsonU32(j, "ammoLoaded"));
  return k;
}

Json ToJson(const Key& k)
{
  return Json{ { "baseId", k.baseId },
               { "mods", k.mods },
               { "condition", k.condition },
               { "stolenFrom", k.stolenFrom },
               { "ammoLoaded", k.ammoLoaded } };
}

bool SameStack(const Key& a, const Key& b)
{
  return a.baseId == b.baseId && a.condition == b.condition &&
    a.stolenFrom == b.stolenFrom && a.mods == b.mods;
}

bool IsPlain(const Key& k)
{
  return k.mods.empty() && k.condition == kConditionFull &&
    k.stolenFrom == 0;
}

Key KeyOf(const RE::TESBoundObject* object, const RE::ExtraDataList* extra)
{
  Key k;
  if (!object) {
    return k;
  }
  k.baseId = object->GetFormID();
  if (!extra) {
    return k;
  }
  if (auto inst = extra->GetByType<RE::BGSObjectInstanceExtra>();
      inst && inst->values && inst->values->buffer) {
    for (auto& d : inst->GetIndexData()) {
      // [verify] disabled entries are mods switched off by another mod,
      // not part of the item
      if (d.objectID && !d.disabled) {
        k.mods.push_back(d.objectID);
      }
    }
    std::sort(k.mods.begin(), k.mods.end());
  }
  if (auto health = extra->GetByType<RE::ExtraHealth>()) {
    k.condition = FractionToCondition(health->health);
  }
  if (auto own = static_cast<const OwnershipExtra*>(
        extra->GetByType(RE::EXTRA_DATA_TYPE::kOwnership))) {
    if (own->owner && !IsPlayerOwner(own->owner->GetFormID())) {
      k.stolenFrom = own->owner->GetFormID();
    }
  }
  if (auto ammo = extra->GetByType<RE::ExtraAmmo>()) {
    k.ammoLoaded = ClampU16(ammo->count);
  }
  return k;
}

std::vector<Stack> Read(RE::TESObjectREFR* ref)
{
  std::vector<Stack> out;
  auto list = ref ? ref->inventoryList : nullptr;
  if (!list) {
    return out;
  }
  RE::BSAutoReadLock lock{ list->rwLock };
  for (auto& item : list->data) {
    if (!item.object) {
      continue;
    }
    for (auto s = item.stackData.get(); s; s = s->nextStack.get()) {
      if (s->count == 0) {
        continue;
      }
      Stack st;
      st.key = KeyOf(item.object, s->extra.get());
      st.count = s->count;
      st.equipped = s->IsEquipped();
      st.object = item.object;
      out.push_back(std::move(st));
    }
  }
  return out;
}

bool Add(RE::TESObjectREFR* ref, const Key& key, uint32_t count, bool silent)
{
  if (!ref || count == 0) {
    return false;
  }
  ApplyGuard guard;
  auto id = ref->GetFormID();
  NotifyOwnChange(id, true);
  bool ok = AddImpl(ref, key, count);
  NotifyOwnChange(id, false);
  if (ok && !silent) {
    ShowMessage(RE::TESForm::GetFormByID<RE::TESBoundObject>(key.baseId),
                count, true);
  }
  return ok;
}

bool Remove(RE::TESObjectREFR* ref, const Key& key, uint32_t count,
            bool silent)
{
  if (!ref || count == 0) {
    return false;
  }
  ApplyGuard guard;
  auto id = ref->GetFormID();
  NotifyOwnChange(id, true);
  uint32_t removed = 0;
  RemoveImpl(ref, key, count, removed);
  NotifyOwnChange(id, false);
  if (removed && !silent) {
    ShowMessage(RE::TESForm::GetFormByID<RE::TESBoundObject>(key.baseId),
                removed, false);
  }
  return removed > 0;
}

std::optional<Instance> Find(RE::TESObjectREFR* ref, const Key& key)
{
  return Find(ref, key, true);
}

std::optional<Instance> Find(RE::TESObjectREFR* ref, const Key& key,
                             bool preferEquipped)
{
  using Flag = RE::BGSInventoryItem::Stack::Flag;
  auto list = ref ? ref->inventoryList : nullptr;
  if (!list) {
    return std::nullopt;
  }
  std::optional<Instance> first;
  RE::BSAutoReadLock lock{ list->rwLock };
  for (auto& item : list->data) {
    if (!item.object || item.object->GetFormID() != key.baseId) {
      continue;
    }
    uint32_t index = 0;
    for (auto s = item.stackData.get(); s; s = s->nextStack.get(), ++index) {
      if (s->count == 0 || !SameStack(KeyOf(item.object, s->extra.get()), key)) {
        continue;
      }
      Instance inst;
      inst.object = item.object;
      inst.stackIndex = index;
      inst.count = s->count;
      inst.equipped = s->IsEquipped();
      inst.locked = s->flags.any(Flag::kEquipStateLocked);
      if (inst.equipped == preferEquipped) {
        return inst;
      }
      if (!first) {
        first = inst;
      }
    }
  }
  return first;
}

RE::TBO_InstanceData* InstanceDataOf(RE::TESObjectREFR* ref,
                                     const Instance& inst)
{
  auto list = ref ? ref->inventoryList : nullptr;
  if (!list || !inst.object) {
    return nullptr;
  }
  const RE::BGSInventoryItem* found = nullptr;
  {
    RE::BSAutoReadLock lock{ list->rwLock };
    for (auto& item : list->data) {
      if (item.object != inst.object) {
        continue;
      }
      uint32_t stacks = 0;
      for (auto s = item.stackData.get(); s; s = s->nextStack.get()) {
        ++stacks;
      }
      if (inst.stackIndex < stacks) {
        found = &item;
      }
      break;
    }
  }
  // Main thread only: the item can't move between the lookup and here
  return found ? found->GetInstanceData(inst.stackIndex) : nullptr;
}

ApplyGuard::ApplyGuard()
{
  ++g_applying;
}

ApplyGuard::~ApplyGuard()
{
  --g_applying;
}

bool Applying()
{
  return g_applying.load() > 0;
}

void OnOwnChange(ChangeListener fn)
{
  g_listeners.push_back(std::move(fn));
}

void NotifyOwnChange(uint32_t refId, bool before)
{
  for (auto& fn : g_listeners) {
    try {
      fn(refId, before);
    } catch (std::exception& e) {
      REX::ERROR("Inventory change listener failed: {}", e.what());
    }
  }
}

}
