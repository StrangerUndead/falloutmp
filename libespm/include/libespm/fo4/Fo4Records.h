#pragma once
// Fallout 4 record readers (ESPM-005..010).
//
// Layouts follow xEdit's wbDefinitionsFO4 (Fallout 4 form version 131).
// Every reader is bounds-checked: a short or malformed subrecord leaves the
// affected fields at their defaults instead of reading past the buffer.
//
// The classes overlay RecordHeader exactly like the Skyrim readers, so
// espm::Convert<espm::fo4::WEAP>(rec) works on any looked-up record.
#include "libespm/CTDA.h"
#include "libespm/CompressedFieldsCache.h"
#include "libespm/RecordHeader.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#pragma pack(push, 1)

namespace espm::fo4 {

// Component entry used by COBJ FVPA and MISC CVPA.
struct ComponentCount
{
  uint32_t formId = 0; // CMPO (or any base object for COBJ)
  uint32_t count = 0;
  friend bool operator==(const ComponentCount& a, const ComponentCount& b)
  {
    return a.formId == b.formId && a.count == b.count;
  }
};

// DAMA entry: damage/resistance per damage type (DMGT).
struct DamageTypeValue
{
  uint32_t damageTypeId = 0;
  uint32_t value = 0;
  uint32_t curveTableId = 0; // 0 when the entry has no curve table
};

// Common fields every item-like record has.
struct ItemCommon
{
  std::string editorId;
  std::vector<uint32_t> keywords;
  // FULL: either an inline string (non-localized plugin) or a string-table
  // id (localized plugin such as Fallout4.esm). Exactly one is set.
  std::string fullName;
  std::optional<uint32_t> fullNameStringId;
  std::vector<uint32_t> attachParentSlots; // APPR (keywords)
  uint32_t instanceNamingRules = 0;        // INRD
};

class WEAP final : public RecordHeader
{
public:
  static constexpr auto kType = "WEAP";

  enum Flags : uint32_t
  {
    kPlayerOnly = 0x1,
    kNpcsUseAmmo = 0x2,
    kNoJamAfterReload = 0x4,
    kChargingReload = 0x8,
    kMinorCrime = 0x10,
    kFixedRange = 0x20,
    kNotUsedInNormalCombat = 0x40,
    kCritEffectOnDeath = 0x100,
    kChargingAttack = 0x200,
    kHoldInputToPower = 0x800,
    kNonHostile = 0x1000,
    kBoundWeapon = 0x2000,
    kIgnoresNormalWeaponResistance = 0x4000,
    kAutomatic = 0x8000,
    kRepeatableSingleFire = 0x10000,
    kCantDrop = 0x20000,
    kHideBackpack = 0x40000,
    kEmbeddedWeapon = 0x80000,
    kNotPlayable = 0x100000,
    kHasScope = 0x200000,
    kBoltAction = 0x400000,
    kSecondaryWeapon = 0x800000,
    kDisableShells = 0x1000000,
  };

  enum class AnimationType : uint8_t
  {
    HandToHandMelee = 0,
    OneHandSword,
    OneHandDagger,
    OneHandAxe,
    OneHandMace,
    TwoHandSword,
    TwoHandAxe,
    Bow,
    Staff,
    Gun,
    Grenade,
    Mine
  };

  struct Data
  {
    ItemCommon common;
    // DNAM
    uint32_t ammoId = 0;
    float speed = 1.f;
    float reloadSpeed = 1.f;
    float reach = 1.f;
    float minRange = 0.f;
    float maxRange = 0.f;
    float attackDelay = 0.f;
    float outOfRangeDamageMult = 0.5f;
    uint32_t onHit = 0;
    uint32_t skillAvId = 0;
    uint32_t resistAvId = 0;
    uint32_t flags = 0;
    uint16_t capacity = 0;
    AnimationType animationType = AnimationType::HandToHandMelee;
    float secondaryDamage = 0.f;
    float weight = 0.f;
    uint32_t value = 0;
    uint16_t baseDamage = 0;
    uint32_t soundLevel = 0;
    uint8_t accuracyBonus = 0;
    float animationAttackSeconds = 0.3f;
    float actionPointCost = 20.f;
    float fullPowerSeconds = 0.f;
    float minPowerPerShot = 0.f;
    uint32_t stagger = 0;
    bool hasDnam = false;
    // FNAM
    float animationFireSeconds = 0.f;
    float animationReloadSeconds = 0.f;
    float sightedTransitionSeconds = 0.25f;
    uint8_t numProjectiles = 1;
    uint32_t overrideProjectileId = 0;
    // CRDT
    float critDamageMult = 2.f;
    float critChargeBonus = 0.f;
    uint32_t critEffectSpellId = 0;
    // Other
    std::vector<DamageTypeValue> damageTypes; // DAMA
    uint32_t templateWeaponId = 0;            // CNAM
    uint32_t embeddedModId = 0;               // NNAM
    uint32_t npcAmmoListId = 0;               // LNAM
    bool IsAutomatic() const noexcept { return (flags & kAutomatic) != 0; }
    bool IsGun() const noexcept { return animationType == AnimationType::Gun; }
  };

  Data GetData(CompressedFieldsCache& cache) const noexcept;
};

class ARMO final : public RecordHeader
{
public:
  static constexpr auto kType = "ARMO";

  struct Data
  {
    ItemCommon common;
    uint32_t bipedSlots = 0; // BOD2 first-person flags
    int32_t value = 0;
    float weight = 0.f;
    uint32_t health = 0; // power armor pieces and other degradable armor
    uint16_t armorRating = 0;
    uint16_t baseAddonIndex = 0;
    uint8_t staggerRating = 0;
    std::vector<DamageTypeValue> resistances; // DAMA
    uint32_t templateArmorId = 0;             // TNAM
    uint32_t raceId = 0;                      // RNAM
  };

  Data GetData(CompressedFieldsCache& cache) const noexcept;
};

class AMMO final : public RecordHeader
{
public:
  static constexpr auto kType = "AMMO";

  struct Data
  {
    ItemCommon common;
    uint32_t value = 0;
    float weight = 0.f;
    uint32_t projectileId = 0;
    uint8_t flags = 0;
    float damage = 0.f;
    uint32_t health = 0; // fusion cores: charge
  };

  Data GetData(CompressedFieldsCache& cache) const noexcept;
};

class MISC final : public RecordHeader
{
public:
  static constexpr auto kType = "MISC";

  struct Data
  {
    ItemCommon common;
    int32_t value = 0;
    float weight = 0.f;
    std::vector<ComponentCount> components; // CVPA: what scrapping yields
  };

  Data GetData(CompressedFieldsCache& cache) const noexcept;
};

class CMPO final : public RecordHeader
{
public:
  static constexpr auto kType = "CMPO";

  struct Data
  {
    std::string editorId;
    std::string fullName;
    std::optional<uint32_t> fullNameStringId;
    uint32_t autoCalcValue = 0;    // DATA
    uint32_t scrapItemId = 0;      // MNAM (MISC "c_Steel" scrap item)
    uint32_t modScrapScalarId = 0; // GNAM (GLOB)
  };

  Data GetData(CompressedFieldsCache& cache) const noexcept;
};

class COBJ final : public RecordHeader
{
public:
  static constexpr auto kType = "COBJ";

  struct Data
  {
    std::string editorId;
    std::vector<ComponentCount> components; // FVPA
    std::vector<CTDA> conditions;           // CTDA
    uint32_t createdObjectId = 0;           // CNAM
    uint32_t workbenchKeywordId = 0;        // BNAM
    std::vector<uint32_t> categoryKeywords; // FNAM
    uint16_t createdCount = 1;              // INTV[0]
    uint16_t priority = 0;                  // INTV[1]
  };

  Data GetData(CompressedFieldsCache& cache) const noexcept;
};

// One property change applied by an object mod (24 bytes on disk).
struct OmodProperty
{
  enum class ValueType : uint8_t
  {
    Int = 0,
    Float = 1,
    Bool = 2,
    String = 3,
    FormIdInt = 4,
    Enum = 5,
    FormIdFloat = 6,
  };

  // Meaning depends on the value type (xEdit wbOMODDataFunctionTypeDecider):
  // float/int: SET, MUL+ADD, ADD; bool: SET, AND, OR; form: SET, REM, ADD.
  enum class Function : uint8_t
  {
    Set = 0,
    MulAddOrAndOrRem = 1,
    AddOrOr = 2,
  };

  ValueType valueType = ValueType::Int;
  Function function = Function::Set;
  uint16_t property = 0; // weapon/armor/actor property index (see Omod.h)
  uint32_t value1 = 0;   // raw: int, float bits, bool or form id
  uint32_t value2 = 0;   // raw: int, float bits or bool
  float step = 0.f;

  float Value1AsFloat() const noexcept;
  float Value2AsFloat() const noexcept;
};

class OMOD final : public RecordHeader
{
public:
  static constexpr auto kType = "OMOD";

  enum class TargetFormType : uint32_t
  {
    None = 0,
    Armor = 0x4F4D5241,  // 'ARMO'
    Npc = 0x5F43504E,    // 'NPC_'
    Weapon = 0x50414557, // 'WEAP'
  };

  struct Include
  {
    uint32_t modId = 0;
    uint8_t minimumLevel = 0;
    bool optional = false;
    bool dontUseAll = false;
  };

  struct Data
  {
    std::string editorId;
    std::string fullName;
    std::optional<uint32_t> fullNameStringId;
    TargetFormType formType = TargetFormType::None;
    uint8_t maxRank = 0;
    uint8_t levelTierScaledOffset = 0;
    uint32_t attachPointKeywordId = 0;
    std::vector<uint32_t> attachParentSlots;
    std::vector<Include> includes;
    std::vector<OmodProperty> properties;
    std::vector<uint32_t> targetOmodKeywords; // MNAM
    std::vector<uint32_t> filterKeywords;     // FNAM
    uint32_t looseModId = 0;                  // LNAM (MISC)
    uint8_t priority = 0;                     // NAM1
    bool isLegendary = false;     // record flag 0x8 [verify ESPM-015]
    bool isModCollection = false; // record flag 0x40 [verify ESPM-015]
    bool parsedOk = false;        // DATA fully read
  };

  Data GetData(CompressedFieldsCache& cache) const noexcept;
};

class FURN final : public RecordHeader
{
public:
  static constexpr auto kType = "FURN";

  enum class BenchType : uint8_t
  {
    None = 0,
    CreateObject = 1,
    Weapons = 2,
    Alchemy = 5, // chemistry and cooking stations
    Armor = 7,
    PowerArmor = 8,
    RobotMod = 9,
  };

  static constexpr uint32_t kPowerArmorRecordFlag = 0x02000000;

  struct Data
  {
    std::string editorId;
    std::vector<uint32_t> keywords;
    std::optional<BenchType> benchType;         // WBDT, only on workbenches
    bool isPowerArmorFurniture = false;         // record flag 25: PA frames
    std::vector<ComponentCount> containerItems; // CNTO (item id, count)
  };

  Data GetData(CompressedFieldsCache& cache) const noexcept;
};

class GLOB final : public RecordHeader
{
public:
  static constexpr auto kType = "GLOB";

  struct Data
  {
    std::string editorId;
    char type = 'f';
    float value = 0.f;
    bool isConstant = false;
  };

  Data GetData(CompressedFieldsCache& cache) const noexcept;
};

class ALCH final : public RecordHeader
{
public:
  static constexpr auto kType = "ALCH";

  enum Flags : uint32_t
  {
    kNoAutoCalc = 0x1,
    kFoodItem = 0x2,
    kMedicine = 0x10000,
    kPoison = 0x20000,
  };

  struct Effect
  {
    uint32_t effectId = 0; // EFID (MGEF)
    float magnitude = 0.f;
    uint32_t area = 0;
    uint32_t duration = 0;
    std::vector<CTDA> conditions;
  };

  struct Data
  {
    ItemCommon common;
    float weight = 0.f;
    int32_t value = 0;
    uint32_t flags = 0;
    uint32_t addictionId = 0;
    float addictionChance = 0.f;
    std::vector<Effect> effects;
  };

  Data GetData(CompressedFieldsCache& cache) const noexcept;
};

// Leveled lists (LVLI items, LVLN actors) with Fallout 4's per-entry
// chance-none byte inside LVLO.
struct LeveledEntry
{
  uint16_t level = 1;
  uint32_t refId = 0;
  uint16_t count = 1;
  uint8_t chanceNone = 0;
};

struct LeveledListData
{
  std::string editorId;
  uint8_t chanceNone = 0;          // LVLD
  uint8_t flags = 0;               // LVLF
  uint32_t chanceNoneGlobalId = 0; // LVLG
  std::vector<LeveledEntry> entries;

  static constexpr uint8_t kCalcFromAllLevels = 0x1;
  static constexpr uint8_t kCalcForEachItem = 0x2;
  static constexpr uint8_t kUseAll = 0x4;
};

class LVLI final : public RecordHeader
{
public:
  static constexpr auto kType = "LVLI";
  LeveledListData GetData(CompressedFieldsCache& cache) const noexcept;
};

class LVLN final : public RecordHeader
{
public:
  static constexpr auto kType = "LVLN";
  LeveledListData GetData(CompressedFieldsCache& cache) const noexcept;
};

class CONT final : public RecordHeader
{
public:
  static constexpr auto kType = "CONT";
  struct Data
  {
    std::string editorId;
    std::vector<ComponentCount> items; // CNTO (item or leveled list, count)
    bool respawns = false;             // DATA flag 0x2
  };
  Data GetData(CompressedFieldsCache& cache) const noexcept;
};

// Non-player character base (F13). Template flags pick which aspects come
// from a template actor (TPTA per aspect, else TPLT); see NpcResolver.
class NPC_ final : public RecordHeader
{
public:
  static constexpr auto kType = "NPC_";

  // ACBS flags
  static constexpr uint32_t kFlagEssential = 1u << 1;
  static constexpr uint32_t kFlagRespawn = 1u << 3;
  static constexpr uint32_t kFlagPcLevelMult = 1u << 7;
  static constexpr uint32_t kFlagProtected = 1u << 11;
  static constexpr uint32_t kFlagNoLoot = 1u << 12;
  static constexpr uint32_t kFlagSpawnsDead = 1u << 26;
  static constexpr uint32_t kFlagInvulnerable = 1u << 31;

  // Template flag / TPTA index per aspect
  enum Aspect : uint8_t
  {
    kTraits = 0,
    kStats = 1,
    kFactions = 2,
    kSpellList = 3,
    kAiData = 4,
    kAiPackages = 5,
    kModelAnimation = 6,
    kBaseData = 7,
    kInventory = 8,
    kScript = 9,
    kDefPackList = 10,
    kAttackData = 11,
    kKeywords = 12,
    kAspectCount = 13,
  };

  struct Faction
  {
    uint32_t factionId = 0;
    int8_t rank = 0;
  };

  struct Data
  {
    std::string editorId;
    uint32_t flags = 0;
    int16_t xpValueOffset = 0;
    uint16_t level = 1;    // when not kFlagPcLevelMult
    float levelMult = 1.f; // when kFlagPcLevelMult
    uint16_t calcMinLevel = 0;
    uint16_t calcMaxLevel = 0; // 0 = no cap
    uint16_t templateFlags = 0;
    std::vector<Faction> factions;
    uint32_t deathItem = 0;
    uint32_t defaultTemplate = 0;                        // TPLT (LVLN or NPC_)
    uint32_t legendaryTemplate = 0;                      // LTPT
    uint32_t legendaryChance = 0;                        // LTPC (GLOB)
    std::array<uint32_t, kAspectCount> templateActors{}; // TPTA
    uint32_t race = 0;
    uint32_t classId = 0;
    uint32_t defaultOutfit = 0; // DOFT
    uint32_t combatStyle = 0;
    uint16_t calculatedHealth = 0;     // DNAM
    std::vector<ComponentCount> items; // CNTO
  };
  Data GetData(CompressedFieldsCache& cache) const noexcept;
};

class OTFT final : public RecordHeader
{
public:
  static constexpr auto kType = "OTFT";
  struct Data
  {
    std::string editorId;
    std::vector<uint32_t> items; // ARMO or LVLI
  };
  Data GetData(CompressedFieldsCache& cache) const noexcept;
};

// Placed reference (REFR/ACHR) fields used by the server world bootstrap.
class REFR final : public RecordHeader
{
public:
  static constexpr auto kType = "REFR";

  enum class PrimitiveType : uint32_t
  {
    None = 0,
    Box = 1,
    Sphere = 2,
    Plane = 3,
    Line = 4,
    Ellipsoid = 5,
  };

  struct LinkedRef
  {
    uint32_t keywordId = 0;
    uint32_t refId = 0;
  };

  struct Data
  {
    std::string editorId;
    uint32_t baseId = 0;
    bool hasPlacement = false;
    std::array<float, 3> pos = { 0, 0, 0 };
    std::array<float, 3> rotRadians = { 0, 0, 0 };
    float scale = 1.f;
    // XLOC
    std::optional<uint8_t> lockLevel;
    uint32_t lockKeyId = 0;
    bool leveledLock = false;
    // XPRM
    std::optional<PrimitiveType> primitiveType;
    std::array<float, 3> primitiveBounds = { 0, 0, 0 }; // half extents
    std::vector<LinkedRef> linkedRefs;                  // XLKR
    uint32_t ownerId = 0;                               // XOWN
    uint32_t persistLocationId = 0;                     // XLCN
    bool initiallyDisabled = false;
    bool deleted = false;
    // Map marker (XMRK + FNAM flags + FULL + TNAM type)
    bool isMapMarker = false;
    uint8_t mapFlags = 0; // 0x1 visible, 0x2 can travel to
    uint8_t mapMarkerType = 0;
    std::string mapMarkerName;
    std::optional<uint32_t> mapMarkerNameId;
  };

  Data GetData(CompressedFieldsCache& cache) const noexcept;
};

// Workshop-relevant data that FO4 stores on any base object: actor value
// properties (PRPS) such as workshop ratings, power generated/required
// (ESPM-018).
struct ActorValueProperty
{
  uint32_t actorValueId = 0;
  float value = 0.f;
};

std::vector<ActorValueProperty> GetActorValueProperties(
  const RecordHeader* rec, CompressedFieldsCache& cache) noexcept;

// Bounds-checked KSIZ/KWDA reader (the Skyrim helper trusts KSIZ).
std::vector<uint32_t> GetKeywords(const RecordHeader* rec,
                                  CompressedFieldsCache& cache) noexcept;

}

#pragma pack(pop)
