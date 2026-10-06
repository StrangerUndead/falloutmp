#include "ComponentPlanner.h"
#include <algorithm>
#include <map>

namespace fo4 {

ComponentPlanner::ComponentPlanner(const IFo4DataSource& data_)
  : data(data_)
{
}

FormId ComponentPlanner::GetScrapItem(FormId componentId) const
{
  if (auto c = data.FindComponent(componentId)) {
    return c->scrapItemId;
  }
  return 0;
}

const std::vector<ComponentCount>* ComponentPlanner::GetJunkComponents(
  FormId miscId) const
{
  auto misc = data.FindMisc(miscId);
  if (!misc || misc->components.empty()) {
    return nullptr;
  }
  return &misc->components;
}

uint32_t ComponentPlanner::CountAvailable(const Fo4Inventory& inv,
                                          FormId componentId) const
{
  uint32_t n = 0;
  FormId scrap = GetScrapItem(componentId);
  for (auto& e : inv.Entries()) {
    if (scrap && e.key.baseId == scrap) {
      n += e.count;
      continue;
    }
    if (auto comps = GetJunkComponents(e.key.baseId)) {
      for (auto& c : *comps) {
        if (c.componentId == componentId) {
          n += c.count * e.count;
        }
      }
    }
  }
  return n;
}

bool ComponentPlanner::TakeComponent(Fo4Inventory& inv,
                                     std::map<FormId, uint32_t>& pool,
                                     FormId componentId, uint32_t need,
                                     ComponentPlan& plan,
                                     uint32_t& outMissing) const
{
  // 1. leftovers from junk already scrapped in this transaction
  auto& pooled = pool[componentId];
  uint32_t fromPool = std::min(pooled, need);
  pooled -= fromPool;
  need -= fromPool;

  // 2. loose scrap items
  FormId scrap = GetScrapItem(componentId);
  if (need > 0 && scrap) {
    uint32_t have = inv.CountBase(scrap);
    uint32_t take = std::min(have, need);
    if (take > 0) {
      inv.RemoveAnyOf(scrap, take, &plan.consumed);
      need -= take;
    }
  }

  // 3. scrap junk, cheapest per unit of the needed component first
  while (need > 0) {
    const InventoryEntry* best = nullptr;
    float bestScore = 0.f;
    uint32_t bestYield = 0;
    for (auto& e : inv.Entries()) {
      auto comps = GetJunkComponents(e.key.baseId);
      if (!comps || e.key.stolenFrom) {
        continue; // never auto-scrap stolen items (keeps crime state honest)
      }
      uint32_t yield = 0;
      for (auto& c : *comps) {
        if (c.componentId == componentId) {
          yield += c.count;
        }
      }
      if (yield == 0) {
        continue;
      }
      auto misc = data.FindMisc(e.key.baseId);
      float score =
        static_cast<float>(misc ? misc->value : 0) / static_cast<float>(yield);
      if (!best || score < bestScore ||
          (score == bestScore && e.key.baseId < best->key.baseId)) {
        best = &e;
        bestScore = score;
        bestYield = yield;
      }
    }
    if (!best) {
      break;
    }
    ItemKey junk = best->key;
    auto comps = *GetJunkComponents(junk.baseId);
    inv.Remove(junk, 1);
    plan.consumed.push_back({ junk, 1 });
    for (auto& c : comps) {
      pool[c.componentId] += c.count;
    }
    uint32_t take = std::min(pool[componentId], need);
    pool[componentId] -= take;
    need -= take;
    (void)bestYield;
  }
  outMissing = need;
  return need == 0;
}

ComponentPlan ComponentPlanner::Plan(const Fo4Inventory& inv,
                                     const std::vector<ComponentCount>& cost,
                                     uint32_t times) const
{
  ComponentPlan plan;
  plan.resultInventory = inv;
  std::map<FormId, uint32_t> pool;

  // Merge duplicate requirements so shortfalls are reported once
  std::map<FormId, uint64_t> required;
  std::vector<FormId> order;
  for (auto& c : cost) {
    if (!required.count(c.componentId)) {
      order.push_back(c.componentId);
    }
    required[c.componentId] += static_cast<uint64_t>(c.count) * times;
  }

  for (FormId id : order) {
    uint64_t need64 = required[id];
    if (need64 > UINT32_MAX) {
      plan.shortfalls.push_back({ id, UINT32_MAX });
      continue;
    }
    uint32_t need = static_cast<uint32_t>(need64);
    if (need == 0) {
      continue;
    }
    if (data.FindComponent(id)) {
      uint32_t missing = 0;
      if (!TakeComponent(plan.resultInventory, pool, id, need, plan,
                         missing)) {
        plan.shortfalls.push_back({ id, missing });
      }
    } else {
      // A concrete item (e.g. a Nuka-Cola for Nuka-Cola Quantum recipes)
      uint32_t have = plan.resultInventory.CountBase(id);
      if (have < need) {
        plan.shortfalls.push_back({ id, need - have });
      } else {
        plan.resultInventory.RemoveAnyOf(id, need, &plan.consumed);
      }
    }
  }

  plan.ok = plan.shortfalls.empty();
  if (!plan.ok) {
    plan.resultInventory = inv;
    plan.consumed.clear();
    return plan;
  }

  // Leftover components from scrapped junk go back as scrap items
  for (auto& [componentId, count] : pool) {
    if (count == 0) {
      continue;
    }
    FormId scrap = GetScrapItem(componentId);
    if (scrap) {
      plan.resultInventory.AddSimple(scrap, count);
      plan.returned.push_back({ ItemKey{ scrap }, count });
    }
  }
  return plan;
}

}
