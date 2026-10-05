#include "Fo4Data.h"
#include <algorithm>

namespace fo4 {

bool ItemData::HasKeyword(FormId kw) const
{
  return std::find(keywords.begin(), keywords.end(), kw) != keywords.end();
}

namespace {
template <class M>
auto* FindIn(const M& m, FormId id)
{
  auto it = m.find(id);
  return it == m.end() ? nullptr : &it->second;
}
}

WeaponData& InMemoryFo4DataSource::AddWeapon(WeaponData d)
{
  d.type = ItemType::Weapon;
  return weapons[d.id] = std::move(d);
}
ArmorData& InMemoryFo4DataSource::AddArmor(ArmorData d)
{
  d.type = ItemType::Armor;
  return armors[d.id] = std::move(d);
}
AmmoData& InMemoryFo4DataSource::AddAmmo(AmmoData d)
{
  d.type = ItemType::Ammo;
  return ammos[d.id] = std::move(d);
}
MiscData& InMemoryFo4DataSource::AddMisc(MiscData d)
{
  d.type = ItemType::Misc;
  return miscs[d.id] = std::move(d);
}
ConsumableData& InMemoryFo4DataSource::AddConsumable(ConsumableData d)
{
  d.type = ItemType::Consumable;
  return consumables[d.id] = std::move(d);
}
ComponentData& InMemoryFo4DataSource::AddComponent(ComponentData d)
{
  return components[d.id] = std::move(d);
}
ObjectModData& InMemoryFo4DataSource::AddObjectMod(ObjectModData d)
{
  return mods[d.id] = std::move(d);
}
RecipeData& InMemoryFo4DataSource::AddRecipe(RecipeData d)
{
  return recipes[d.id] = std::move(d);
}
FurnitureData& InMemoryFo4DataSource::AddFurniture(FurnitureData d)
{
  return furniture[d.id] = std::move(d);
}
WorkshopObjectData& InMemoryFo4DataSource::AddWorkshopObject(
  WorkshopObjectData d)
{
  return workshopObjects[d.baseId] = std::move(d);
}

const ItemData* InMemoryFo4DataSource::FindItem(FormId id) const
{
  if (auto p = FindIn(weapons, id))
    return p;
  if (auto p = FindIn(armors, id))
    return p;
  if (auto p = FindIn(ammos, id))
    return p;
  if (auto p = FindIn(miscs, id))
    return p;
  if (auto p = FindIn(consumables, id))
    return p;
  return nullptr;
}
const WeaponData* InMemoryFo4DataSource::FindWeapon(FormId id) const
{
  return FindIn(weapons, id);
}
const ArmorData* InMemoryFo4DataSource::FindArmor(FormId id) const
{
  return FindIn(armors, id);
}
const AmmoData* InMemoryFo4DataSource::FindAmmo(FormId id) const
{
  return FindIn(ammos, id);
}
const MiscData* InMemoryFo4DataSource::FindMisc(FormId id) const
{
  return FindIn(miscs, id);
}
const ConsumableData* InMemoryFo4DataSource::FindConsumable(FormId id) const
{
  return FindIn(consumables, id);
}
const ComponentData* InMemoryFo4DataSource::FindComponent(FormId id) const
{
  return FindIn(components, id);
}
const ObjectModData* InMemoryFo4DataSource::FindObjectMod(FormId id) const
{
  return FindIn(mods, id);
}
const RecipeData* InMemoryFo4DataSource::FindRecipe(FormId id) const
{
  return FindIn(recipes, id);
}
const FurnitureData* InMemoryFo4DataSource::FindFurniture(FormId id) const
{
  return FindIn(furniture, id);
}
const WorkshopObjectData* InMemoryFo4DataSource::FindWorkshopObject(
  FormId id) const
{
  return FindIn(workshopObjects, id);
}

std::vector<const RecipeData*> InMemoryFo4DataSource::GetRecipesByWorkbench(
  FormId keyword) const
{
  std::vector<const RecipeData*> res;
  for (auto& [id, r] : recipes) {
    if (r.workbenchKeywordId == keyword) {
      res.push_back(&r);
    }
  }
  return res;
}

std::vector<const RecipeData*> InMemoryFo4DataSource::GetRecipesCreating(
  FormId createdObjectId) const
{
  std::vector<const RecipeData*> res;
  for (auto& [id, r] : recipes) {
    if (r.createdObjectId == createdObjectId) {
      res.push_back(&r);
    }
  }
  return res;
}

FormId InMemoryFo4DataSource::GetComponentByScrapItem(FormId miscId) const
{
  for (auto& [id, c] : components) {
    if (c.scrapItemId == miscId) {
      return id;
    }
  }
  return 0;
}

LeveledListData& InMemoryFo4DataSource::AddLeveledList(LeveledListData d)
{
  return leveledLists[d.id] = std::move(d);
}
NpcData& InMemoryFo4DataSource::AddNpc(NpcData d)
{
  return npcs[d.id] = std::move(d);
}

OutfitData& InMemoryFo4DataSource::AddOutfit(OutfitData d)
{
  return outfits[d.id] = std::move(d);
}

const NpcData* InMemoryFo4DataSource::FindNpc(FormId id) const
{
  return FindIn(npcs, id);
}

const OutfitData* InMemoryFo4DataSource::FindOutfit(FormId id) const
{
  return FindIn(outfits, id);
}

ContainerData& InMemoryFo4DataSource::AddContainer(ContainerData d)
{
  return containers[d.id] = std::move(d);
}
const LeveledListData* InMemoryFo4DataSource::FindLeveledList(FormId id) const
{
  return FindIn(leveledLists, id);
}
const ContainerData* InMemoryFo4DataSource::FindContainer(FormId id) const
{
  return FindIn(containers, id);
}

}
