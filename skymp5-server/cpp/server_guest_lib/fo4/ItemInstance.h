#pragma once
// Item instances and the Fallout 4 inventory (F04).
//
// Fallout 4 items are not just a base form and a count: weapons and armor
// carry attached object mods (OMODs), power armor pieces carry condition,
// guns carry loaded rounds, and stolen items remember their owner. Two
// items stack only if everything except `ammoLoaded` is equal.
#include "Fo4Data.h"
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <vector>

namespace fo4 {

struct ItemKey
{
  FormId baseId = 0;
  // Attached mods. Kept sorted so that the same set of mods is the same
  // key regardless of attach order (the stat resolver applies them in its
  // own deterministic order).
  std::vector<FormId> mods;
  // Condition in 1/1000 (1000 = 100%). Only used by degradable items such as
  // power armor pieces; 0 means "not tracked".
  uint16_t condition = 0;
  // Owner the item was stolen from (faction or actor). Part of identity, so
  // stolen and legit items don't stack (STATUS deviation F04).
  FormId stolenFrom = 0;
  // Rounds currently in the magazine. Not part of identity.
  uint16_t ammoLoaded = 0;

  bool SameStack(const ItemKey& other) const noexcept;
  bool HasMod(FormId modId) const noexcept;
  ItemKey WithMods(std::vector<FormId> newMods) const;
  // 64-bit hash of the identity fields, sent to clients as instanceHash
  uint64_t InstanceHash() const noexcept;

  static ItemKey Simple(FormId baseId) { return ItemKey{ baseId }; }
};

struct InventoryEntry
{
  ItemKey key;
  uint32_t count = 0;
};

class Fo4Inventory
{
public:
  // Adds count items. Merges into an existing stack when possible; on merge
  // ammoLoaded becomes min(existing, added) so no rounds are created.
  void Add(const ItemKey& key, uint32_t count);
  void AddSimple(FormId baseId, uint32_t count) { Add(ItemKey{ baseId }, count); }

  // Removes exactly `count` items of that stack. Returns false and changes
  // nothing if there are fewer.
  bool Remove(const ItemKey& key, uint32_t count);
  // Removes `count` items of any instances of baseId, preferring plain
  // (unmodded, not stolen) stacks. Returns false and changes nothing if
  // there are fewer in total.
  bool RemoveAnyOf(FormId baseId, uint32_t count,
                   std::vector<InventoryEntry>* outRemoved = nullptr);

  uint32_t Count(const ItemKey& key) const noexcept;
  uint32_t CountBase(FormId baseId) const noexcept;
  const InventoryEntry* Find(const ItemKey& key) const noexcept;
  InventoryEntry* FindMutable(const ItemKey& key) noexcept;
  std::vector<const InventoryEntry*> FindAllOf(FormId baseId) const;

  const std::vector<InventoryEntry>& Entries() const noexcept
  {
    return entries;
  }
  bool IsEmpty() const noexcept { return entries.empty(); }
  void Clear() { entries.clear(); }

  float TotalWeight(const IFo4DataSource& data) const;

  nlohmann::json ToJson() const;
  static Fo4Inventory FromJson(const nlohmann::json& j);

  friend bool operator==(const Fo4Inventory& a, const Fo4Inventory& b);

private:
  std::vector<InventoryEntry> entries;
};

bool operator==(const ItemKey& a, const ItemKey& b);

nlohmann::json ItemKeyToJson(const ItemKey& key);
ItemKey ItemKeyFromJson(const nlohmann::json& j);

}
