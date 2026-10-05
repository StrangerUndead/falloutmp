#pragma once
#include <array>
// Static game data used by the Fallout 4 server systems.
//
// The systems in server_guest_lib/fo4 never touch libespm directly. They
// read plain structs from an IFo4DataSource. EspmFo4DataSource fills them
// from the load order; InMemoryFo4DataSource is used by tests and by
// gamemodes that define custom content without plugins.
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fo4 {

using FormId = uint32_t;

struct ComponentCount
{
  FormId componentId = 0;
  uint32_t count = 0;
  friend bool operator==(const ComponentCount& a, const ComponentCount& b)
  {
    return a.componentId == b.componentId && a.count == b.count;
  }
};

struct DamageTypeValue
{
  FormId damageTypeId = 0; // 0 = physical (DR) when no DMGT is set
  float value = 0.f;
  friend bool operator==(const DamageTypeValue& a, const DamageTypeValue& b)
  {
    return a.damageTypeId == b.damageTypeId && a.value == b.value;
  }
};

enum class ItemType : uint8_t
{
  Unknown = 0,
  Weapon,
  Armor,
  Ammo,
  Misc,       // junk, scrap items, holotapes-as-misc, keys-as-misc
  Consumable, // ALCH: aid, chems, food, drink
  Book,       // magazines, skill books
  Key,
  Note,
};

struct ItemData
{
  FormId id = 0;
  ItemType type = ItemType::Unknown;
  std::string editorId;
  std::string name;
  float weight = 0.f;
  int32_t value = 0;
  std::vector<FormId> keywords;
  std::vector<FormId> attachParentSlots; // APPR: which mods can attach
  FormId instanceNamingRules = 0;
  bool HasKeyword(FormId kw) const;
};

struct WeaponData : ItemData
{
  FormId ammoId = 0;
  uint16_t baseDamage = 0;
  std::vector<DamageTypeValue> damageTypes;
  uint16_t capacity = 0;
  float speed = 1.f;
  float reloadSpeed = 1.f;
  float minRange = 0.f;
  float maxRange = 0.f;
  float attackDelaySec = 0.f;
  float fireSeconds = 0.f; // FNAM animation fire seconds
  float actionPointCost = 20.f;
  float critDamageMult = 2.f;
  float critChargeBonus = 0.f;
  uint8_t numProjectiles = 1;
  uint8_t accuracyBonus = 0;
  bool automatic = false;
  bool isGun = false;
  bool isMelee = false;
  bool isThrown = false; // grenades and mines
  FormId embeddedModId = 0;
};

enum class PowerArmorSlot : uint8_t
{
  None = 0,
  Helmet,
  Torso,
  LeftArm,
  RightArm,
  LeftLeg,
  RightLeg,
};
constexpr size_t kPowerArmorSlotCount = 6;

struct ArmorData : ItemData
{
  uint32_t bipedSlots = 0;
  uint16_t armorRating = 0; // physical DR
  std::vector<DamageTypeValue> resistances;
  uint32_t health = 0; // > 0 for degradable pieces (power armor)
  PowerArmorSlot powerArmorSlot = PowerArmorSlot::None;
};

struct AmmoData : ItemData
{
  FormId projectileId = 0;
  float damage = 0.f;
  uint32_t charge = 0; // fusion core charge (AMMO DNAM health)
};

struct MiscData : ItemData
{
  std::vector<ComponentCount> components; // what scrapping yields
  // If set, this MISC *is* the scrap item for that component
  // (e.g. "Steel" for c_Steel), worth 1 component per unit.
  FormId scrapItemForComponent = 0;
};

struct ConsumableEffect
{
  FormId effectId = 0;
  float magnitude = 0.f;
  uint32_t durationSec = 0;
};

struct ConsumableData : ItemData
{
  bool isFood = false;
  bool isMedicine = false;
  FormId addictionId = 0;
  float addictionChance = 0.f;
  std::vector<ConsumableEffect> effects;
};

struct ComponentData
{
  FormId id = 0;
  std::string editorId;
  std::string name;
  FormId scrapItemId = 0; // MISC that represents one unit of this component
  float modScrapScalar = 1.f;
  uint32_t value = 0;
};

// Requirement checked by the recipe validator. Perk requirements come from
// COBJ conditions (HasPerk); the server evaluates them through the perk
// engine (SRV-021) via a callback.
struct PerkRequirement
{
  FormId perkId = 0;
  int32_t minRank = 1;
};

struct RecipeData
{
  FormId id = 0;
  std::string editorId;
  FormId createdObjectId = 0; // item, OMOD, or workshop base object
  uint16_t createdCount = 1;
  FormId workbenchKeywordId = 0;
  std::vector<FormId> categoryKeywords;
  std::vector<ComponentCount> components; // component or item ids
  std::vector<PerkRequirement> perks;
  uint16_t priority = 0;
};

enum class OmodValueType : uint8_t
{
  Int,
  Float,
  Bool,
  String,
  FormIdInt,
  Enum,
  FormIdFloat,
};

enum class OmodFunction : uint8_t
{
  Set,    // value = v1
  MulAdd, // float/int: value += base * v1 ; bool: AND ; form: REM
  Add,    // float/int: value += v1        ; bool: OR  ; form: ADD
};

struct OmodPropertyData
{
  OmodValueType valueType = OmodValueType::Int;
  OmodFunction function = OmodFunction::Set;
  uint16_t property = 0;
  // Numeric value (int/float/enum). For form-id types: form id.
  float value1 = 0.f;
  FormId formValue = 0;
  float value2 = 0.f; // second value of FormID,Int / FormID,Float pairs
};

enum class OmodTarget : uint8_t
{
  None,
  Weapon,
  Armor,
  Actor,
};

struct ObjectModData
{
  FormId id = 0;
  std::string editorId;
  std::string name;
  OmodTarget target = OmodTarget::None;
  FormId attachPointKeywordId = 0;
  std::vector<FormId> attachParentSlots; // slots this mod adds
  std::vector<FormId> includes;          // mods this mod pulls in
  std::vector<OmodPropertyData> properties;
  FormId looseModId = 0; // MISC used when the mod is removed
  uint8_t priority = 0;
  bool legendary = false;
};

enum class WorkbenchType : uint8_t
{
  None = 0,
  CreateObject = 1,
  Weapons = 2,
  Chemistry = 5, // and cooking
  Armor = 7,
  PowerArmor = 8,
  RobotMod = 9,
  Workshop = 100, // settlement workbench (not a WBDT value)
};

struct FurnitureData
{
  FormId id = 0;
  std::string editorId;
  std::vector<FormId> keywords;
  std::optional<WorkbenchType> workbench;
  bool isPowerArmorFrame = false;
};

// Values a base object contributes to a settlement (from PRPS, ESPM-018)
// plus the build-budget cost.
struct WorkshopObjectData
{
  FormId baseId = 0;
  float food = 0.f;
  float water = 0.f;
  float defense = 0.f;
  float beds = 0.f;
  float powerGenerated = 0.f;
  float powerRequired = 0.f;
  float happiness = 0.f;
  float budgetCost = 1.f;  // abstract units, see F22 budget model
  bool isPowerConnector = false; // pylons, conduits, switches
  bool requiresWorker = false;   // crops, scavenging stations, shops
  bool isRecruitmentBeacon = false;
  bool isBed = false;
  bool isGenerator() const { return powerGenerated > 0.f; }
};

struct LeveledEntryData
{
  int32_t level = 1;
  FormId refId = 0; // item, or a nested leveled list
  uint32_t count = 1;
  uint8_t chanceNone = 0; // percent, per entry
};

struct LeveledListData
{
  FormId id = 0;
  float chanceNone = 0.f; // percent (LVLD, or the LVLG global's value)
  bool calcFromAllLevels = false;
  bool calcForEachItem = false;
  bool useAll = false;
  std::vector<LeveledEntryData> entries;
};

struct ContainerData
{
  FormId id = 0;
  std::vector<ComponentCount> items; // item or leveled list ids
  bool respawns = false;
};

// NPC_ base (F13); aspect indices match espm::fo4::NPC_::Aspect
struct NpcData
{
  enum Aspect : uint8_t
  {
    kTraits = 0,
    kStats = 1,
    kFactions = 2,
    kBaseData = 7,
    kInventory = 8,
    kAspectCount = 13,
  };
  FormId id = 0;
  std::string editorId;
  uint32_t flags = 0; // ACBS flags (espm::fo4::NPC_::kFlag*)
  uint16_t level = 1;
  bool pcLevelMult = false;
  float levelMult = 1.f;
  uint16_t calcMinLevel = 0;
  uint16_t calcMaxLevel = 0;
  uint16_t templateFlags = 0;
  std::array<FormId, kAspectCount> templateActors{};
  FormId defaultTemplate = 0;
  struct Faction
  {
    FormId factionId = 0;
    int8_t rank = 0;
  };
  std::vector<Faction> factions;
  FormId race = 0;
  FormId defaultOutfit = 0;
  FormId deathItem = 0;
  uint16_t calculatedHealth = 0;
  std::vector<ComponentCount> items; // item or leveled list ids

  bool UsesTemplate(uint8_t aspect) const
  {
    return (templateFlags & (1u << aspect)) != 0;
  }
  bool Essential() const { return flags & (1u << 1); }
  bool Protected() const { return flags & (1u << 11); }
  bool SpawnsDead() const { return flags & (1u << 26); }
  bool Invulnerable() const { return flags & (1u << 31); }
};

struct OutfitData
{
  FormId id = 0;
  std::vector<FormId> items; // ARMO or LVLI
};

class IFo4DataSource
{
public:
  virtual ~IFo4DataSource() = default;

  virtual const ItemData* FindItem(FormId id) const = 0;
  virtual const WeaponData* FindWeapon(FormId id) const = 0;
  virtual const ArmorData* FindArmor(FormId id) const = 0;
  virtual const AmmoData* FindAmmo(FormId id) const = 0;
  virtual const MiscData* FindMisc(FormId id) const = 0;
  virtual const ConsumableData* FindConsumable(FormId id) const = 0;
  virtual const ComponentData* FindComponent(FormId id) const = 0;
  virtual const ObjectModData* FindObjectMod(FormId id) const = 0;
  virtual const RecipeData* FindRecipe(FormId id) const = 0;
  virtual const FurnitureData* FindFurniture(FormId id) const = 0;
  virtual const WorkshopObjectData* FindWorkshopObject(FormId id) const = 0;

  // Recipes whose workbench keyword equals `keyword`.
  virtual std::vector<const RecipeData*> GetRecipesByWorkbench(
    FormId keyword) const = 0;
  // Recipes that create `createdObjectId` (used for OMOD attach costs and
  // workshop build costs).
  virtual std::vector<const RecipeData*> GetRecipesCreating(
    FormId createdObjectId) const = 0;
  // Component -> scrap MISC lookup, and the reverse.
  virtual FormId GetComponentByScrapItem(FormId miscId) const = 0;

  // Leveled item lists and container bases (F14, F06)
  virtual const LeveledListData* FindLeveledList(FormId) const
  {
    return nullptr;
  }
  virtual const ContainerData* FindContainer(FormId) const { return nullptr; }
  virtual const NpcData* FindNpc(FormId) const { return nullptr; }
  virtual const OutfitData* FindOutfit(FormId) const { return nullptr; }
};

// Simple map-backed data source for tests and gamemode-defined content.
class InMemoryFo4DataSource : public IFo4DataSource
{
public:
  WeaponData& AddWeapon(WeaponData d);
  ArmorData& AddArmor(ArmorData d);
  AmmoData& AddAmmo(AmmoData d);
  MiscData& AddMisc(MiscData d);
  ConsumableData& AddConsumable(ConsumableData d);
  ComponentData& AddComponent(ComponentData d);
  ObjectModData& AddObjectMod(ObjectModData d);
  RecipeData& AddRecipe(RecipeData d);
  FurnitureData& AddFurniture(FurnitureData d);
  WorkshopObjectData& AddWorkshopObject(WorkshopObjectData d);
  LeveledListData& AddLeveledList(LeveledListData d);
  ContainerData& AddContainer(ContainerData d);
  NpcData& AddNpc(NpcData d);
  OutfitData& AddOutfit(OutfitData d);
  const NpcData* FindNpc(FormId id) const override;
  const OutfitData* FindOutfit(FormId id) const override;
  const LeveledListData* FindLeveledList(FormId id) const override;
  const ContainerData* FindContainer(FormId id) const override;

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

private:
  std::map<FormId, WeaponData> weapons;
  std::map<FormId, ArmorData> armors;
  std::map<FormId, AmmoData> ammos;
  std::map<FormId, MiscData> miscs;
  std::map<FormId, ConsumableData> consumables;
  std::map<FormId, ComponentData> components;
  std::map<FormId, ObjectModData> mods;
  std::map<FormId, RecipeData> recipes;
  std::map<FormId, FurnitureData> furniture;
  std::map<FormId, WorkshopObjectData> workshopObjects;
  std::map<FormId, LeveledListData> leveledLists;
  std::map<FormId, ContainerData> containers;
  std::map<FormId, NpcData> npcs;
  std::map<FormId, OutfitData> outfits;
};

}
