#include "Containers.h"
#include <cmath>
#include <nlohmann/json.hpp>

namespace fo4 {

const char* ContainerErrorToString(ContainerError e) noexcept
{
  switch (e) {
#define C(x)                                                                  \
  case ContainerError::x:                                                     \
    return #x;
    C(None)
    C(NoSuchContainer)
    C(OutOfReach)
    C(Locked)
    C(ItemNotFound)
    C(InvalidCount)
    C(NotACorpse)
    C(Busy)
#undef C
  }
  return "Unknown";
}

ContainerService::ContainerService(const IFo4DataSource& data_,
                                   ContainerSettings s)
  : settings(s)
  , data(data_)
  , leveled(data_)
{
}

WorldContainer& ContainerService::Register(FormId refId, FormId baseId,
                                           std::array<float, 3> pos,
                                           FormId ownerFaction)
{
  auto& c = containers[refId];
  if (c.refId == 0) {
    c.refId = refId;
    c.baseId = baseId;
    c.pos = pos;
    c.ownerFaction = ownerFaction;
  }
  return c;
}

WorldContainer* ContainerService::Find(FormId refId)
{
  auto it = containers.find(refId);
  return it == containers.end() ? nullptr : &it->second;
}

WorldContainer* ContainerService::Open(FormId refId, int32_t level,
                                       std::mt19937& rng)
{
  auto c = Find(refId);
  if (!c) {
    return nullptr;
  }
  if (!c->initialized) {
    c->initialized = true;
    if (auto base = data.FindContainer(c->baseId)) {
      for (auto& i : base->items) {
        for (auto& r : leveled.Resolve(i.componentId, i.count, level, rng)) {
          c->inventory.AddSimple(r.componentId, r.count);
        }
      }
    }
    ++c->version;
  }
  return c;
}

bool ContainerService::InReach(const WorldContainer& c,
                               const std::array<float, 3>& p) const
{
  float dx = c.pos[0] - p[0], dy = c.pos[1] - p[1], dz = c.pos[2] - p[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz) <= settings.reach;
}

ContainerError ContainerService::Take(FormId refId, const ItemKey& item,
                                      uint32_t count,
                                      const std::array<float, 3>& actorPos,
                                      Fo4Inventory& actorInv,
                                      bool actorIsOwner, bool locked)
{
  if (count == 0) {
    return ContainerError::InvalidCount;
  }
  auto c = Find(refId);
  if (!c) {
    return ContainerError::NoSuchContainer;
  }
  if (!InReach(*c, actorPos)) {
    return ContainerError::OutOfReach;
  }
  if (locked) {
    return ContainerError::Locked;
  }
  auto entry = c->inventory.Find(item);
  if (!entry || entry->count < count) {
    return ContainerError::ItemNotFound;
  }
  ItemKey taken = entry->key; // keep the stored ammoLoaded
  c->inventory.Remove(taken, count);
  if (settings.markStolen && c->ownerFaction && !actorIsOwner &&
      !taken.stolenFrom) {
    taken.stolenFrom = c->ownerFaction;
  }
  actorInv.Add(taken, count);
  ++c->version;
  return ContainerError::None;
}

ContainerError ContainerService::Put(FormId refId, const ItemKey& item,
                                     uint32_t count,
                                     const std::array<float, 3>& actorPos,
                                     Fo4Inventory& actorInv, bool locked)
{
  if (count == 0) {
    return ContainerError::InvalidCount;
  }
  auto c = Find(refId);
  if (!c) {
    return ContainerError::NoSuchContainer;
  }
  if (!InReach(*c, actorPos)) {
    return ContainerError::OutOfReach;
  }
  if (locked) {
    return ContainerError::Locked;
  }
  auto entry = actorInv.Find(item);
  if (!entry || entry->count < count) {
    return ContainerError::ItemNotFound;
  }
  ItemKey key = entry->key;
  actorInv.Remove(key, count);
  c->inventory.Add(key, count);
  ++c->version;
  return ContainerError::None;
}

ContainerError ContainerService::Drop(FormId newRefId, const ItemKey& item,
                                      uint32_t count,
                                      const std::array<float, 3>& pos,
                                      Fo4Inventory& actorInv, FormId* outRefId)
{
  if (count == 0) {
    return ContainerError::InvalidCount;
  }
  auto entry = actorInv.Find(item);
  if (!entry || entry->count < count) {
    return ContainerError::ItemNotFound;
  }
  ItemKey key = entry->key;
  actorInv.Remove(key, count);
  auto& c = containers[newRefId];
  c.refId = newRefId;
  c.baseId = key.baseId;
  c.pos = pos;
  c.isGroundStack = true;
  c.initialized = true;
  c.inventory.Add(key, count);
  ++c.version;
  if (outRefId) {
    *outRefId = newRefId;
  }
  return ContainerError::None;
}

std::vector<FormId> ContainerService::CollectEmptyGroundStacks()
{
  std::vector<FormId> res;
  for (auto it = containers.begin(); it != containers.end();) {
    if (it->second.isGroundStack && it->second.inventory.IsEmpty()) {
      res.push_back(it->first);
      it = containers.erase(it);
    } else {
      ++it;
    }
  }
  return res;
}

nlohmann::json ContainerService::ToJson() const
{
  auto arr = nlohmann::json::array();
  for (auto& [id, c] : containers) {
    if (!c.initialized) {
      continue; // untouched containers re-resolve from the plugin
    }
    arr.push_back({ { "refId", c.refId },
                    { "baseId", c.baseId },
                    { "owner", c.ownerFaction },
                    { "pos", c.pos },
                    { "ground", c.isGroundStack },
                    { "inventory", c.inventory.ToJson() } });
  }
  return arr;
}

void ContainerService::LoadJson(const nlohmann::json& j)
{
  if (!j.is_array()) {
    return;
  }
  for (auto& jc : j) {
    try {
      WorldContainer c;
      c.refId = jc.at("refId");
      c.baseId = jc.value("baseId", 0u);
      c.ownerFaction = jc.value("owner", 0u);
      c.pos = jc.value("pos", std::array<float, 3>{ 0, 0, 0 });
      c.isGroundStack = jc.value("ground", false);
      c.inventory = Fo4Inventory::FromJson(jc.at("inventory"));
      c.initialized = true;
      containers[c.refId] = std::move(c);
    } catch (const std::exception&) {
    }
  }
}

}
