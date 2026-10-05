#pragma once
// Items as the protocol names them (fo4msg::ItemKey, falloutmp-client
// ItemKey): base form + attached object mods + condition + stolen flag +
// loaded rounds. Implemented in ItemKeys.cpp (inventory work package);
// used by the inventory, equipment, power armor, workshop and combat
// modules.
//
// Stack identity (falloutmp-client core/itemKeys.ts stackKey, server
// fo4::ItemKey::SameStack): base, sorted mods, condition, stolenFrom.
// ammoLoaded is not part of it.
//
// Game side (BGSInventoryList → BGSInventoryItem → Stack → ExtraDataList):
//   mods       BGSObjectInstanceExtra (kObjectInstance) index data
//   condition  ExtraHealth (kHealth), a 0..1 fraction
//   stolenFrom ExtraOwnership (kOwnership) owner, unless the player owns it
//   ammoLoaded ExtraAmmo (kAmmo), read only (setAmmoLoaded sets the drawn
//              weapon's rounds)
#include <RE/Fallout.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace fmp::items {

using Json = nlohmann::json;

// Condition encoding (falloutmp-server/cpp/server_guest_lib/fo4/Condition.h):
//   0 = full / not tracked, 1..999 = per-mille of maximum, 0xFFFF = zero.
constexpr uint16_t kConditionFull = 0;
constexpr uint16_t kConditionZero = 0xFFFF;
float ConditionToFraction(uint16_t condition);
uint16_t FractionToCondition(float fraction);

struct Key
{
  uint32_t baseId = 0;
  std::vector<uint32_t> mods; // OMOD form ids, sorted
  uint16_t condition = kConditionFull;
  uint32_t stolenFrom = 0;
  uint16_t ammoLoaded = 0;
};

Key FromJson(const Json& j);
Json ToJson(const Key& k);

// Same stack: everything but ammoLoaded.
bool SameStack(const Key& a, const Key& b);
// No instance data (no mods, full condition, not stolen).
bool IsPlain(const Key& k);

// The key of a game item: its base object and the extra data of its
// inventory stack or of its world reference (may be null).
Key KeyOf(const RE::TESBoundObject* object, const RE::ExtraDataList* extra);

struct Stack
{
  Key key;
  uint32_t count = 0;
  bool equipped = false;                // worn / wielded (kSlotMask)
  RE::TESBoundObject* object = nullptr; // the base form
};

// The inventory of a reference with instance data (mods, condition), one
// entry per game stack (equal keys are not merged).
std::vector<Stack> Read(RE::TESObjectREFR* ref);

// Adds or removes items with their instance data. `silent` suppresses the
// HUD message and sound. Returns false if nothing could be done.
bool Add(RE::TESObjectREFR* ref, const Key& key, uint32_t count, bool silent);
bool Remove(RE::TESObjectREFR* ref, const Key& key, uint32_t count,
            bool silent);

// The game's instance of an item in an inventory that matches the key
// (for equipping): the inventory item and its stack index.
struct Instance
{
  RE::TESBoundObject* object = nullptr;
  uint32_t stackIndex = 0;
  uint32_t count = 0;
  bool equipped = false;
  bool locked = false; // kEquipStateLocked (equipped with preventRemoval)
};
std::optional<Instance> Find(RE::TESObjectREFR* ref, const Key& key);
// Same, choosing an equipped stack first (preferEquipped) or an unequipped
// one first.
std::optional<Instance> Find(RE::TESObjectREFR* ref, const Key& key,
                             bool preferEquipped);
// The instance data of that stack (effective stats with the mods), for
// BGSObjectInstance. Null for items without instance data.
RE::TBO_InstanceData* InstanceDataOf(RE::TESObjectREFR* ref,
                                     const Instance& inst);

// Reentrancy guard: held while a native applies server state to the game,
// so the capture sinks don't report the changes back as player requests.
// Add / Remove hold it; modules hold it around their own engine calls.
class ApplyGuard
{
public:
  ApplyGuard();
  ~ApplyGuard();
  ApplyGuard(const ApplyGuard&) = delete;
  ApplyGuard& operator=(const ApplyGuard&) = delete;
};
// True while any ApplyGuard is alive (any thread may ask).
bool Applying();

// Called around every Add / Remove (main thread): before = true just
// before the inventory of `refId` changes, false right after. The capture
// code uses it to flush pending engine changes first and re-baseline after.
using ChangeListener = std::function<void(uint32_t refId, bool before)>;
void OnOwnChange(ChangeListener fn);
// Lets other code that changes inventories directly notify the listeners.
void NotifyOwnChange(uint32_t refId, bool before);

}
