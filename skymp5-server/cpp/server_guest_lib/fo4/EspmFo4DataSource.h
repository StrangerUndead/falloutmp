#pragma once
// IFo4DataSource backed by the server's load order (libespm).
//
// Records are converted lazily and cached. All form ids that records store
// (ammo, components, keywords, mods, ...) are mapped from plugin-local to
// global ids with the record's file index, exactly as SkyMP's CraftService
// does for Skyrim.
#include "Fo4Data.h"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace espm {
class CombineBrowser;
class CompressedFieldsCache;
class RecordHeader;
struct LookupResult;
}

namespace fo4 {

struct EspmFo4DataConfig
{
  // Condition function index of HasPerk in Fallout 4 (xEdit table: 448)
  uint16_t hasPerkFunctionIndex = 448;
  // Editor ids of the actor values that carry workshop ratings (PRPS)
  std::string avFood = "Food";
  std::string avWater = "Water";
  std::string avSafety = "Safety";
  std::string avPowerGenerated = "PowerGenerated";
  std::string avPowerRequired = "PowerRequired";
  std::string avBeds = "WorkshopRatingBeds";
  std::string avHappiness = "Happiness";
  // Keywords that classify power armor pieces by slot (editor ids)
  std::map<std::string, PowerArmorSlot> powerArmorSlotKeywords = {
    { "ArmorTypePowerHelmet", PowerArmorSlot::Helmet },
    { "ArmorTypePowerTorso", PowerArmorSlot::Torso },
    { "ArmorTypePowerArmL", PowerArmorSlot::LeftArm },
    { "ArmorTypePowerArmR", PowerArmorSlot::RightArm },
    { "ArmorTypePowerLegL", PowerArmorSlot::LeftLeg },
    { "ArmorTypePowerLegR", PowerArmorSlot::RightLeg },
  };
  // Keyword marking workshop build recipes (BNAM); 0 = any
  std::string workshopWorkbenchKeyword = "WorkshopWorkbenchTypeSettlement";
  // Mod-scrap scalar used when CMPO has no GNAM
  float defaultModScrapScalar = 0.5f;
};

class EspmFo4DataSource : public IFo4DataSource
{
public:
  EspmFo4DataSource(const espm::CombineBrowser& browser,
                    espm::CompressedFieldsCache& cache,
                    EspmFo4DataConfig config = {});
  ~EspmFo4DataSource() override;

  const ItemData* FindItem(FormId id) const override;
  const WeaponData* FindWeapon(FormId id) const override;
  const ArmorData* FindArmor(FormId id) const override;
  const AmmoData* FindAmmo(FormId id) const override;
  const MiscData* FindMisc(FormId id) const override;
  const ConsumableData* FindConsumable(FormId id) const override;
  const ComponentData* FindComponent(FormId id) const override;
  const ObjectModData* FindObjectMod(FormId id) const override;
  const RecipeData* FindRecipe(FormId id) const override;
  const FurnitureData* FindFurniture(FormId id) const override;
  const WorkshopObjectData* FindWorkshopObject(FormId id) const override;
  std::vector<const RecipeData*> GetRecipesByWorkbench(
    FormId keyword) const override;
  std::vector<const RecipeData*> GetRecipesCreating(
    FormId createdObjectId) const override;
  FormId GetComponentByScrapItem(FormId miscId) const override;

  // Editor id lookups for keywords and actor values (ESPM-016)
  FormId FindKeywordByEditorId(const std::string& edid) const;
  FormId FindActorValueByEditorId(const std::string& edid) const;

private:
  struct Impl;
  std::unique_ptr<Impl> pImpl;
};

}
