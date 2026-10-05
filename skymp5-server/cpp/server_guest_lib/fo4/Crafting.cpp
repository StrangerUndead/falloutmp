#include "Crafting.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace fo4 {

const char* CraftErrorToString(CraftError e) noexcept
{
  switch (e) {
    case CraftError::None:
      return "None";
    case CraftError::UnknownRecipe:
      return "UnknownRecipe";
    case CraftError::WrongWorkbench:
      return "WrongWorkbench";
    case CraftError::MissingPerk:
      return "MissingPerk";
    case CraftError::MissingComponents:
      return "MissingComponents";
    case CraftError::UnknownOutput:
      return "UnknownOutput";
    case CraftError::InvalidCount:
      return "InvalidCount";
    case CraftError::NotScrappable:
      return "NotScrappable";
    case CraftError::ItemNotFound:
      return "ItemNotFound";
    case CraftError::IncompatibleMod:
      return "IncompatibleMod";
    case CraftError::NotModifiable:
      return "NotModifiable";
  }
  return "Unknown";
}

bool CrafterContext::HasWorkbenchKeyword(FormId kw) const
{
  return kw == 0 ||
    std::find(workbenchKeywords.begin(), workbenchKeywords.end(), kw) !=
    workbenchKeywords.end();
}

bool CrafterContext::MeetsPerks(
  const std::vector<PerkRequirement>& perks) const
{
  for (auto& p : perks) {
    int32_t rank = perkRank ? perkRank(p.perkId) : 0;
    if (rank < p.minRank) {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------- crafting

CraftingService::CraftingService(const IFo4DataSource& data_)
  : data(data_)
  , planner(data_)
{
}

CraftResult CraftingService::Craft(Fo4Inventory& inv, FormId recipeId,
                                   uint32_t times,
                                   const CrafterContext& ctx) const
{
  CraftResult res;
  if (times == 0 || times > kMaxCraftTimes) {
    res.error = CraftError::InvalidCount;
    return res;
  }
  auto recipe = data.FindRecipe(recipeId);
  if (!recipe) {
    res.error = CraftError::UnknownRecipe;
    return res;
  }
  if (!ctx.HasWorkbenchKeyword(recipe->workbenchKeywordId)) {
    res.error = CraftError::WrongWorkbench;
    return res;
  }
  if (!ctx.MeetsPerks(recipe->perks)) {
    res.error = CraftError::MissingPerk;
    return res;
  }

  // Output: an item, or an OMOD (crafted as its loose mod item)
  FormId outputItem = 0;
  if (data.FindItem(recipe->createdObjectId)) {
    outputItem = recipe->createdObjectId;
  } else if (auto mod = data.FindObjectMod(recipe->createdObjectId)) {
    outputItem = mod->looseModId;
  }
  if (!outputItem) {
    res.error = CraftError::UnknownOutput;
    return res;
  }

  Fo4Inventory after = inv;
  if (!ctx.freeCrafting) {
    auto plan = planner.Plan(inv, recipe->components, times);
    if (!plan.ok) {
      res.error = CraftError::MissingComponents;
      res.shortfalls = plan.shortfalls;
      return res;
    }
    after = std::move(plan.resultInventory);
    res.consumed = std::move(plan.consumed);
    res.returned = std::move(plan.returned);
  }

  res.created = ItemKey{ outputItem };
  res.createdCount = static_cast<uint32_t>(recipe->createdCount) * times;
  after.Add(res.created, res.createdCount);
  inv = std::move(after);
  return res;
}

std::vector<const RecipeData*> CraftingService::GetAvailableRecipes(
  const CrafterContext& ctx) const
{
  std::vector<const RecipeData*> res;
  for (auto kw : ctx.workbenchKeywords) {
    for (auto r : data.GetRecipesByWorkbench(kw)) {
      res.push_back(r);
    }
  }
  std::sort(res.begin(), res.end(),
            [](const RecipeData* a, const RecipeData* b) {
              return a->priority != b->priority ? a->priority < b->priority
                                                : a->id < b->id;
            });
  res.erase(std::unique(res.begin(), res.end()), res.end());
  return res;
}

// --------------------------------------------------------------- scrapping

ScrapService::ScrapService(const IFo4DataSource& data_)
  : data(data_)
  , planner(data_)
  , resolver(data_)
{
}

void ScrapService::AddComponentsAsScrap(
  Fo4Inventory& inv, const std::vector<ComponentCount>& comps,
  std::vector<InventoryEntry>& produced) const
{
  for (auto& c : comps) {
    if (c.count == 0) {
      continue;
    }
    FormId scrap = planner.GetScrapItem(c.componentId);
    FormId add = scrap ? scrap : c.componentId;
    if (!data.FindItem(add)) {
      continue;
    }
    inv.AddSimple(add, c.count);
    produced.push_back({ ItemKey{ add }, c.count });
  }
}

ScrapResult ScrapService::ScrapJunk(Fo4Inventory& inv, const ItemKey& item,
                                    uint32_t count) const
{
  ScrapResult res;
  if (count == 0) {
    res.error = CraftError::InvalidCount;
    return res;
  }
  auto comps = planner.GetJunkComponents(item.baseId);
  if (!comps) {
    res.error = CraftError::NotScrappable;
    return res;
  }
  if (inv.Count(item) < count) {
    res.error = CraftError::ItemNotFound;
    return res;
  }
  Fo4Inventory after = inv;
  after.Remove(item, count);
  std::vector<ComponentCount> total;
  for (auto& c : *comps) {
    total.push_back({ c.componentId, c.count * count });
  }
  AddComponentsAsScrap(after, total, res.produced);
  inv = std::move(after);
  return res;
}

std::vector<ComponentCount> ScrapService::PreviewEquipmentYield(
  const ItemKey& item, const CrafterContext& ctx) const
{
  std::map<FormId, float> acc;
  float bonus = 1.f + scrapperBonusPerRank * std::max(0, ctx.scrapperRank);
  auto addRecipe = [&](FormId created, float scale) {
    auto recipes = data.GetRecipesCreating(created);
    if (recipes.empty()) {
      return;
    }
    for (auto& c : recipes.front()->components) {
      auto comp = data.FindComponent(c.componentId);
      float scalar = comp ? comp->modScrapScalar : 1.f;
      acc[c.componentId] += c.count * scalar * scale;
    }
  };
  addRecipe(item.baseId, baseItemYieldScale);
  for (auto modId : item.mods) {
    addRecipe(modId, 1.f);
  }
  std::vector<ComponentCount> res;
  for (auto& [id, v] : acc) {
    auto n = static_cast<uint32_t>(std::floor(v * bonus + 1e-4f));
    if (n > 0) {
      res.push_back({ id, n });
    }
  }
  return res;
}

ScrapResult ScrapService::ScrapEquipment(Fo4Inventory& inv,
                                         const ItemKey& item,
                                         const CrafterContext& ctx) const
{
  ScrapResult res;
  if (!data.FindWeapon(item.baseId) && !data.FindArmor(item.baseId)) {
    res.error = CraftError::NotScrappable;
    return res;
  }
  if (inv.Count(item) < 1) {
    res.error = CraftError::ItemNotFound;
    return res;
  }
  Fo4Inventory after = inv;
  after.Remove(item, 1);
  // Loaded rounds are returned, never destroyed (C4 rule)
  if (item.ammoLoaded > 0) {
    if (auto w = data.FindWeapon(item.baseId)) {
      FormId ammo = resolver.ResolveWeapon(item).ammoId;
      if (ammo) {
        after.AddSimple(ammo, item.ammoLoaded);
        res.produced.push_back({ ItemKey{ ammo }, item.ammoLoaded });
      }
      (void)w;
    }
  }
  AddComponentsAsScrap(after, PreviewEquipmentYield(item, ctx), res.produced);
  inv = std::move(after);
  return res;
}

// ----------------------------------------------------------------- modding

ModdingService::ModdingService(const IFo4DataSource& data_)
  : data(data_)
  , planner(data_)
  , resolver(data_)
{
}

std::vector<FormId> ModdingService::PruneOrphans(
  const ItemKey& key, std::vector<FormId>& removed) const
{
  // Drop mods whose attach point is no longer offered (e.g. a scope mount
  // removed together with the receiver that provided its slot).
  std::vector<FormId> mods = key.mods;
  bool changed = true;
  while (changed) {
    changed = false;
    ItemKey tmp = key.WithMods(mods);
    auto slots = resolver.GetAttachSlots(tmp);
    for (size_t i = 0; i < mods.size(); ++i) {
      auto mod = data.FindObjectMod(mods[i]);
      if (!mod || mod->attachPointKeywordId == 0) {
        continue;
      }
      // A mod must not count the slots it adds itself
      ItemKey without = key.WithMods([&] {
        auto m = mods;
        m.erase(m.begin() + i);
        return m;
      }());
      auto slotsWithout = resolver.GetAttachSlots(without);
      if (!slotsWithout.count(mod->attachPointKeywordId)) {
        removed.push_back(mods[i]);
        mods.erase(mods.begin() + i);
        changed = true;
        break;
      }
      (void)slots;
    }
  }
  return mods;
}

ModResult ModdingService::Commit(Fo4Inventory& inv, const ItemKey& item,
                                 ItemKey newKey,
                                 std::vector<FormId> removed) const
{
  ModResult res;
  // Magazine shrink: unload excess rounds back into the inventory
  if (data.FindWeapon(item.baseId)) {
    auto stats = resolver.ResolveWeapon(newKey);
    auto oldStats = resolver.ResolveWeapon(item);
    if (stats.ammoId != oldStats.ammoId) {
      res.ammoReturned = item.ammoLoaded; // caliber change unloads all
      newKey.ammoLoaded = 0;
    } else if (newKey.ammoLoaded > stats.capacity) {
      res.ammoReturned = newKey.ammoLoaded - stats.capacity;
      newKey.ammoLoaded = static_cast<uint16_t>(stats.capacity);
    }
    if (res.ammoReturned && oldStats.ammoId) {
      inv.AddSimple(oldStats.ammoId, res.ammoReturned);
    }
  }
  inv.Remove(item, 1);
  inv.Add(newKey, 1);
  if (returnRemovedModsAsLoose) {
    for (auto id : removed) {
      if (auto mod = data.FindObjectMod(id); mod && mod->looseModId) {
        inv.AddSimple(mod->looseModId, 1);
      }
    }
  }
  res.newKey = newKey;
  res.removedMods = std::move(removed);
  return res;
}

ModResult ModdingService::AttachMod(Fo4Inventory& inv, const ItemKey& item,
                                    FormId modId,
                                    const CrafterContext& ctx) const
{
  ModResult res;
  auto base = data.FindItem(item.baseId);
  if (!base || (base->type != ItemType::Weapon &&
                base->type != ItemType::Armor)) {
    res.error = CraftError::NotModifiable;
    return res;
  }
  if (inv.Count(item) < 1) {
    res.error = CraftError::ItemNotFound;
    return res;
  }
  auto mod = data.FindObjectMod(modId);
  OmodTarget want =
    base->type == ItemType::Weapon ? OmodTarget::Weapon : OmodTarget::Armor;
  if (!mod || (mod->target != want && mod->target != OmodTarget::None) ||
      item.HasMod(modId)) {
    res.error = CraftError::IncompatibleMod;
    return res;
  }

  // Replace whatever occupies the same attach point
  std::vector<FormId> removed;
  std::vector<FormId> mods;
  for (auto m : item.mods) {
    auto existing = data.FindObjectMod(m);
    if (existing && mod->attachPointKeywordId &&
        existing->attachPointKeywordId == mod->attachPointKeywordId) {
      removed.push_back(m);
    } else {
      mods.push_back(m);
    }
  }
  // The new mod's attach point must be offered by base + remaining mods
  if (mod->attachPointKeywordId &&
      !resolver.GetAttachSlots(item.WithMods(mods))
         .count(mod->attachPointKeywordId)) {
    res.error = CraftError::IncompatibleMod;
    return res;
  }
  mods.push_back(modId);
  ItemKey candidate = item.WithMods(mods);
  candidate.mods = PruneOrphans(candidate, removed);
  candidate = candidate.WithMods(candidate.mods);

  Fo4Inventory after = inv;
  // Pay: a loose mod is free, otherwise the mod's recipe
  bool usedLoose = false;
  if (mod->looseModId && after.CountBase(mod->looseModId) > 0) {
    after.RemoveAnyOf(mod->looseModId, 1);
    usedLoose = true;
  }
  if (!usedLoose && !ctx.freeCrafting) {
    auto recipes = data.GetRecipesCreating(modId);
    const RecipeData* usable = nullptr;
    for (auto r : recipes) {
      if (ctx.HasWorkbenchKeyword(r->workbenchKeywordId)) {
        usable = r;
        break;
      }
    }
    if (!usable) {
      res.error = recipes.empty() ? CraftError::UnknownRecipe
                                  : CraftError::WrongWorkbench;
      return res;
    }
    if (!ctx.MeetsPerks(usable->perks)) {
      res.error = CraftError::MissingPerk;
      return res;
    }
    auto plan = planner.Plan(after, usable->components, 1);
    if (!plan.ok) {
      res.error = CraftError::MissingComponents;
      res.shortfalls = plan.shortfalls;
      return res;
    }
    after = std::move(plan.resultInventory);
  }
  auto committed = Commit(after, item, candidate, removed);
  inv = std::move(after);
  return committed;
}

ModResult ModdingService::DetachMod(Fo4Inventory& inv, const ItemKey& item,
                                    FormId modId,
                                    const CrafterContext& ctx) const
{
  ModResult res;
  (void)ctx;
  if (inv.Count(item) < 1) {
    res.error = CraftError::ItemNotFound;
    return res;
  }
  if (!item.HasMod(modId)) {
    res.error = CraftError::IncompatibleMod;
    return res;
  }
  std::vector<FormId> removed{ modId };
  std::vector<FormId> mods;
  for (auto m : item.mods) {
    if (m != modId) {
      mods.push_back(m);
    }
  }
  ItemKey candidate = item.WithMods(mods);
  candidate.mods = PruneOrphans(candidate, removed);
  candidate = candidate.WithMods(candidate.mods);
  Fo4Inventory after = inv;
  auto committed = Commit(after, item, candidate, removed);
  inv = std::move(after);
  return committed;
}

std::vector<const ObjectModData*> ModdingService::GetCompatibleMods(
  const ItemKey& item, const std::vector<FormId>& candidateModIds) const
{
  std::vector<const ObjectModData*> res;
  auto base = data.FindItem(item.baseId);
  if (!base) {
    return res;
  }
  // Slots offered by base + mods, excluding what each candidate replaces
  for (auto id : candidateModIds) {
    auto mod = data.FindObjectMod(id);
    if (!mod || item.HasMod(id)) {
      continue;
    }
    std::vector<FormId> keep;
    for (auto m : item.mods) {
      auto e = data.FindObjectMod(m);
      if (!(e && e->attachPointKeywordId == mod->attachPointKeywordId)) {
        keep.push_back(m);
      }
    }
    if (mod->attachPointKeywordId == 0 ||
        resolver.GetAttachSlots(item.WithMods(keep))
          .count(mod->attachPointKeywordId)) {
      res.push_back(mod);
    }
  }
  return res;
}

}
