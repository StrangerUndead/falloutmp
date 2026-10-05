#pragma once
// Crafting, scrapping and weapon/armor modding (F15, F16).
//
// All three are server-authoritative (class A in the sync standard): the
// client asks, the server validates the workbench, perks and components,
// then mutates the inventory atomically and replies with the result.
#include "ComponentPlanner.h"
#include "OmodStatResolver.h"
#include <functional>
#include <string>

namespace fo4 {

enum class CraftError : uint8_t
{
  None = 0,
  UnknownRecipe,
  WrongWorkbench,
  MissingPerk,
  MissingComponents,
  UnknownOutput,
  InvalidCount,
  NotScrappable,
  ItemNotFound,
  IncompatibleMod,
  NotModifiable,
};

const char* CraftErrorToString(CraftError e) noexcept;

// Facts about the crafter that the services need but do not own.
struct CrafterContext
{
  // Keywords of the workbench furniture the actor is using. The caller has
  // already checked the actor is in that furniture and within reach (F07).
  std::vector<FormId> workbenchKeywords;
  // Perk rank lookup (perk engine, SRV-021). Returns 0 when not owned.
  std::function<int32_t(FormId perkId)> perkRank;
  // Scrapper perk rank, used by ScrapService for weapons and armor.
  int32_t scrapperRank = 0;
  // Ignore costs (admin / creative gamemodes)
  bool freeCrafting = false;

  bool HasWorkbenchKeyword(FormId kw) const;
  bool MeetsPerks(const std::vector<PerkRequirement>& perks) const;
};

struct CraftResult
{
  CraftError error = CraftError::None;
  ItemKey created;
  uint32_t createdCount = 0;
  std::vector<ComponentShortfall> shortfalls;
  std::vector<InventoryEntry> consumed;
  std::vector<InventoryEntry> returned;
  bool Ok() const { return error == CraftError::None; }
};

class CraftingService
{
public:
  explicit CraftingService(const IFo4DataSource& data);

  // Crafts `times` copies of a recipe into `inv`. On any error `inv` is left
  // unchanged.
  CraftResult Craft(Fo4Inventory& inv, FormId recipeId, uint32_t times,
                    const CrafterContext& ctx) const;

  // The recipes a client may see at a workbench (F15 UI list).
  std::vector<const RecipeData*> GetAvailableRecipes(
    const CrafterContext& ctx) const;

  static constexpr uint32_t kMaxCraftTimes = 100;

private:
  const IFo4DataSource& data;
  ComponentPlanner planner;
};

struct ScrapResult
{
  CraftError error = CraftError::None;
  std::vector<InventoryEntry> produced; // scrap items added
  bool Ok() const { return error == CraftError::None; }
};

class ScrapService
{
public:
  explicit ScrapService(const IFo4DataSource& data);

  // Junk (MISC with components): each unit yields its full component list.
  ScrapResult ScrapJunk(Fo4Inventory& inv, const ItemKey& item,
                        uint32_t count) const;

  // Weapons and armor: yields the components of every attached mod's
  // crafting cost scaled by the component's mod-scrap scalar, plus the
  // base item's own recipe cost at half value. Scrapper ranks add 25% per
  // rank (gameplay rule, configurable by the gamemode; Fallout 4's exact
  // yield table is [verify F15 G-manual]).
  ScrapResult ScrapEquipment(Fo4Inventory& inv, const ItemKey& item,
                             const CrafterContext& ctx) const;

  // Components scrapping would yield, without changing anything (UI).
  std::vector<ComponentCount> PreviewEquipmentYield(
    const ItemKey& item, const CrafterContext& ctx) const;

  float baseItemYieldScale = 0.5f;
  float scrapperBonusPerRank = 0.25f;

private:
  void AddComponentsAsScrap(Fo4Inventory& inv,
                            const std::vector<ComponentCount>& comps,
                            std::vector<InventoryEntry>& produced) const;

  const IFo4DataSource& data;
  ComponentPlanner planner;
  OmodStatResolver resolver;
};

struct ModResult
{
  CraftError error = CraftError::None;
  ItemKey newKey;
  std::vector<FormId> removedMods; // returned as loose mods when possible
  std::vector<ComponentShortfall> shortfalls;
  uint32_t ammoReturned = 0; // rounds unloaded when capacity shrinks
  bool Ok() const { return error == CraftError::None; }
};

class ModdingService
{
public:
  explicit ModdingService(const IFo4DataSource& data);

  // Attaches `modId` to one item of stack `item`. Uses a loose mod from the
  // inventory if present (free), otherwise pays the mod's recipe at a
  // matching workbench. The mod previously in that attach point, and any
  // mods that depended on slots it provided, are removed and returned as
  // loose mods.
  ModResult AttachMod(Fo4Inventory& inv, const ItemKey& item, FormId modId,
                      const CrafterContext& ctx) const;

  // Removes a mod and returns it as a loose mod (if it has one).
  ModResult DetachMod(Fo4Inventory& inv, const ItemKey& item, FormId modId,
                      const CrafterContext& ctx) const;

  // Mods that can attach to this instance right now (UI list).
  std::vector<const ObjectModData*> GetCompatibleMods(
    const ItemKey& item, const std::vector<FormId>& candidateModIds) const;

  bool returnRemovedModsAsLoose = true;

private:
  std::vector<FormId> PruneOrphans(const ItemKey& key,
                                   std::vector<FormId>& removed) const;
  ModResult Commit(Fo4Inventory& inv, const ItemKey& item, ItemKey newKey,
                   std::vector<FormId> removed) const;

  const IFo4DataSource& data;
  ComponentPlanner planner;
  OmodStatResolver resolver;
};

}
