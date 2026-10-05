#include "ItemInstance.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace fo4 {

bool ItemKey::SameStack(const ItemKey& o) const noexcept
{
  return baseId == o.baseId && mods == o.mods && condition == o.condition &&
    stolenFrom == o.stolenFrom;
}

bool operator==(const ItemKey& a, const ItemKey& b)
{
  return a.SameStack(b) && a.ammoLoaded == b.ammoLoaded;
}

bool ItemKey::HasMod(FormId modId) const noexcept
{
  return std::binary_search(mods.begin(), mods.end(), modId);
}

ItemKey ItemKey::WithMods(std::vector<FormId> newMods) const
{
  ItemKey k = *this;
  std::sort(newMods.begin(), newMods.end());
  newMods.erase(std::unique(newMods.begin(), newMods.end()), newMods.end());
  k.mods = std::move(newMods);
  return k;
}

uint64_t ItemKey::InstanceHash() const noexcept
{
  // FNV-1a over identity fields; stable across platforms and runs.
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](uint64_t v) {
    for (int i = 0; i < 8; ++i) {
      h ^= (v >> (i * 8)) & 0xff;
      h *= 1099511628211ull;
    }
  };
  mix(baseId);
  for (auto m : mods) {
    mix(m);
  }
  mix(condition);
  mix(stolenFrom);
  return h;
}

namespace {
ItemKey Normalized(const ItemKey& key)
{
  ItemKey k = key;
  if (!std::is_sorted(k.mods.begin(), k.mods.end())) {
    std::sort(k.mods.begin(), k.mods.end());
  }
  return k;
}
}

void Fo4Inventory::Add(const ItemKey& keyIn, uint32_t count)
{
  if (count == 0) {
    return;
  }
  ItemKey key = Normalized(keyIn);
  for (auto& e : entries) {
    if (e.key.SameStack(key)) {
      e.key.ammoLoaded = std::min(e.key.ammoLoaded, key.ammoLoaded);
      e.count += count;
      return;
    }
  }
  entries.push_back({ key, count });
}

bool Fo4Inventory::Remove(const ItemKey& keyIn, uint32_t count)
{
  ItemKey key = Normalized(keyIn);
  for (size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].key.SameStack(key)) {
      if (entries[i].count < count) {
        return false;
      }
      entries[i].count -= count;
      if (entries[i].count == 0) {
        entries.erase(entries.begin() + i);
      }
      return true;
    }
  }
  return count == 0;
}

bool Fo4Inventory::RemoveAnyOf(FormId baseId, uint32_t count,
                               std::vector<InventoryEntry>* outRemoved)
{
  if (CountBase(baseId) < count) {
    return false;
  }
  // Prefer plain stacks, then modded, then stolen last.
  std::vector<size_t> order;
  for (size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].key.baseId == baseId) {
      order.push_back(i);
    }
  }
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    auto score = [&](const ItemKey& k) {
      return (k.stolenFrom ? 2 : 0) + (k.mods.empty() ? 0 : 1);
    };
    return score(entries[a].key) < score(entries[b].key);
  });
  uint32_t left = count;
  for (size_t idx : order) {
    if (left == 0) {
      break;
    }
    uint32_t take = std::min(left, entries[idx].count);
    if (outRemoved) {
      outRemoved->push_back({ entries[idx].key, take });
    }
    entries[idx].count -= take;
    left -= take;
  }
  entries.erase(
    std::remove_if(entries.begin(), entries.end(),
                   [](const InventoryEntry& e) { return e.count == 0; }),
    entries.end());
  return true;
}

uint32_t Fo4Inventory::Count(const ItemKey& keyIn) const noexcept
{
  auto e = Find(keyIn);
  return e ? e->count : 0;
}

uint32_t Fo4Inventory::CountBase(FormId baseId) const noexcept
{
  uint32_t n = 0;
  for (auto& e : entries) {
    if (e.key.baseId == baseId) {
      n += e.count;
    }
  }
  return n;
}

const InventoryEntry* Fo4Inventory::Find(const ItemKey& keyIn) const noexcept
{
  ItemKey key = Normalized(keyIn);
  for (auto& e : entries) {
    if (e.key.SameStack(key)) {
      return &e;
    }
  }
  return nullptr;
}

InventoryEntry* Fo4Inventory::FindMutable(const ItemKey& key) noexcept
{
  return const_cast<InventoryEntry*>(Find(key));
}

std::vector<const InventoryEntry*> Fo4Inventory::FindAllOf(FormId baseId) const
{
  std::vector<const InventoryEntry*> res;
  for (auto& e : entries) {
    if (e.key.baseId == baseId) {
      res.push_back(&e);
    }
  }
  return res;
}

float Fo4Inventory::TotalWeight(const IFo4DataSource& data) const
{
  float w = 0.f;
  for (auto& e : entries) {
    if (auto item = data.FindItem(e.key.baseId)) {
      w += item->weight * static_cast<float>(e.count);
    }
  }
  return w;
}

nlohmann::json ItemKeyToJson(const ItemKey& key)
{
  nlohmann::json j = { { "baseId", key.baseId } };
  if (!key.mods.empty()) {
    j["mods"] = key.mods;
  }
  if (key.condition) {
    j["condition"] = key.condition;
  }
  if (key.stolenFrom) {
    j["stolenFrom"] = key.stolenFrom;
  }
  if (key.ammoLoaded) {
    j["ammoLoaded"] = key.ammoLoaded;
  }
  return j;
}

ItemKey ItemKeyFromJson(const nlohmann::json& j)
{
  ItemKey k;
  k.baseId = j.at("baseId").get<FormId>();
  if (j.contains("mods")) {
    k.mods = j["mods"].get<std::vector<FormId>>();
    std::sort(k.mods.begin(), k.mods.end());
  }
  k.condition = j.value("condition", uint16_t(0));
  k.stolenFrom = j.value("stolenFrom", FormId(0));
  k.ammoLoaded = j.value("ammoLoaded", uint16_t(0));
  return k;
}

nlohmann::json Fo4Inventory::ToJson() const
{
  auto arr = nlohmann::json::array();
  for (auto& e : entries) {
    auto j = ItemKeyToJson(e.key);
    j["count"] = e.count;
    arr.push_back(std::move(j));
  }
  return nlohmann::json{ { "entries", std::move(arr) } };
}

Fo4Inventory Fo4Inventory::FromJson(const nlohmann::json& j)
{
  Fo4Inventory inv;
  if (!j.is_object() || !j.contains("entries")) {
    return inv;
  }
  for (auto& e : j["entries"]) {
    // One bad entry must not drop the whole inventory (REF-020 rule).
    try {
      inv.Add(ItemKeyFromJson(e), e.at("count").get<uint32_t>());
    } catch (const std::exception&) {
    }
  }
  return inv;
}

bool operator==(const Fo4Inventory& a, const Fo4Inventory& b)
{
  if (a.entries.size() != b.entries.size()) {
    return false;
  }
  for (auto& e : a.entries) {
    auto other = b.Find(e.key);
    if (!other || other->count != e.count ||
        other->key.ammoLoaded != e.key.ammoLoaded) {
      return false;
    }
  }
  return true;
}

}
