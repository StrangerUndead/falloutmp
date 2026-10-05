#pragma once
// Items as the protocol names them (fo4msg::ItemKey, falloutmp-client
// ItemKey): base form + attached object mods + condition + stolen flag +
// loaded rounds. Implemented in ItemKeys.cpp (inventory work package);
// used by the inventory, equipment, power armor, workshop and combat
// modules.
#include <RE/Fallout.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace fmp::items {

using Json = nlohmann::json;

// Condition encoding (skymp5-server/cpp/server_guest_lib/fo4/Condition.h):
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

struct Stack
{
  Key key;
  uint32_t count = 0;
};

// The inventory of a reference with instance data (mods, condition).
std::vector<Stack> Read(RE::TESObjectREFR* ref);

// Adds or removes items with their instance data. `silent` suppresses the
// HUD message and sound. Returns false if nothing could be done.
bool Add(RE::TESObjectREFR* ref, const Key& key, uint32_t count,
         bool silent);
bool Remove(RE::TESObjectREFR* ref, const Key& key, uint32_t count,
            bool silent);

// The game's instance of an item in an inventory that matches the key
// (for equipping): the inventory item and its stack index.
struct Instance
{
  RE::TESBoundObject* object = nullptr;
  uint32_t stackIndex = 0;
};
std::optional<Instance> Find(RE::TESObjectREFR* ref, const Key& key);

}
