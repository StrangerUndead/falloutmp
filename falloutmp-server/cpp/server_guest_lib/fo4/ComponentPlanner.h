#pragma once
// Component consumption with Fallout 4 auto-scrapping (F15).
//
// A recipe cost is a list of (component or item, count). When the player
// lacks loose scrap items for a component, Fallout 4 breaks down junk that
// contains it, uses what it needs and returns the leftovers as scrap items.
// ComponentPlanner reproduces that on a copy of the inventory, so callers
// either commit the whole transaction or nothing (atomicity, F15/F22).
#include "Fo4Data.h"
#include "ItemInstance.h"
#include <map>

namespace fo4 {

struct ComponentShortfall
{
  FormId componentOrItemId = 0;
  uint32_t missing = 0;
};

struct ComponentPlan
{
  bool ok = false;
  std::vector<ComponentShortfall> shortfalls;
  // Inventory after paying the cost (only meaningful when ok).
  Fo4Inventory resultInventory;
  // For logs and the client UI.
  std::vector<InventoryEntry> consumed; // items removed (incl. scrapped junk)
  std::vector<InventoryEntry> returned; // leftover scrap items added back
};

class ComponentPlanner
{
public:
  explicit ComponentPlanner(const IFo4DataSource& data);

  // Plans paying `cost` multiplied by `times` from `inv`.
  ComponentPlan Plan(const Fo4Inventory& inv,
                     const std::vector<ComponentCount>& cost,
                     uint32_t times = 1) const;

  // How many units of a component the inventory can provide in total
  // (scrap items plus everything obtainable by scrapping junk).
  uint32_t CountAvailable(const Fo4Inventory& inv, FormId componentId) const;

  // Components a single junk item yields when scrapped.
  const std::vector<ComponentCount>* GetJunkComponents(FormId miscId) const;

  FormId GetScrapItem(FormId componentId) const;

private:
  bool TakeComponent(Fo4Inventory& inv, std::map<FormId, uint32_t>& pool,
                     FormId componentId, uint32_t need, ComponentPlan& plan,
                     uint32_t& outMissing) const;

  const IFo4DataSource& data;
};

}
