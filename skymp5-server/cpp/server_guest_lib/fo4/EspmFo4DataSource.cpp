#include "EspmFo4DataSource.h"
#include "libespm/CombineBrowser.h"
#include "libespm/CompressedFieldsCache.h"
#include "libespm/Convert.h"
#include "libespm/LookupResult.h"
#include "libespm/RecordHeader.h"
#include "libespm/fo4/Fo4Records.h"
#include <cstring>
#include <optional>

namespace fo4 {

struct EspmFo4DataSource::Impl
{
  const espm::CombineBrowser& br;
  espm::CompressedFieldsCache& cache;
  EspmFo4DataConfig cfg;
  std::recursive_mutex m;

  std::unordered_map<FormId, std::unique_ptr<ItemData>> items;
  std::unordered_map<FormId, bool> notItem;
  std::unordered_map<FormId, std::unique_ptr<ComponentData>> components;
  std::unordered_map<FormId, std::unique_ptr<ObjectModData>> mods;
  std::unordered_map<FormId, std::unique_ptr<FurnitureData>> furniture;
  std::unordered_map<FormId, std::unique_ptr<WorkshopObjectData>> workshop;
  std::unordered_map<FormId, std::unique_ptr<LeveledListData>> leveled;
  std::unordered_map<FormId, std::unique_ptr<ContainerData>> containers;
  std::unordered_map<FormId, std::unique_ptr<NpcData>> npcs;
  std::unordered_map<FormId, std::unique_ptr<OutfitData>> outfits;

  bool recipesBuilt = false;
  std::map<FormId, RecipeData> recipes;
  std::multimap<FormId, FormId> recipesByBench, recipesByCreated;

  bool indexBuilt = false;
  std::unordered_map<std::string, FormId> keywordByEdid, avByEdid;
  std::unordered_map<FormId, FormId> componentByScrap;
  std::unordered_map<FormId, PowerArmorSlot> slotByKeyword;
  FormId avFood = 0, avWater = 0, avSafety = 0, avPowerGen = 0,
         avPowerReq = 0, avBeds = 0, avHappiness = 0;

  Impl(const espm::CombineBrowser& b, espm::CompressedFieldsCache& c,
       EspmFo4DataConfig conf)
    : br(b)
    , cache(c)
    , cfg(std::move(conf))
  {
  }

  std::vector<FormId> Map(const espm::LookupResult& lr,
                          const std::vector<uint32_t>& ids)
  {
    std::vector<FormId> res;
    res.reserve(ids.size());
    for (auto id : ids) {
      if (id) {
        res.push_back(lr.ToGlobalId(id));
      }
    }
    return res;
  }

  FormId Map(const espm::LookupResult& lr, uint32_t id)
  {
    return id ? lr.ToGlobalId(id) : 0;
  }

  void BuildIndex()
  {
    if (indexBuilt) {
      return;
    }
    indexBuilt = true;
    for (auto& lr : br.GetDistinctRecordsByType("KYWD")) {
      keywordByEdid[lr.rec->GetEditorId(cache)] = lr.ToGlobalId(lr.rec->GetId());
    }
    for (auto& lr : br.GetDistinctRecordsByType("AVIF")) {
      avByEdid[lr.rec->GetEditorId(cache)] = lr.ToGlobalId(lr.rec->GetId());
    }
    for (auto& lr : br.GetDistinctRecordsByType("CMPO")) {
      auto c = espm::Convert<espm::fo4::CMPO>(lr.rec);
      if (!c) {
        continue;
      }
      auto d = c->GetData(cache);
      if (d.scrapItemId) {
        componentByScrap[Map(lr, d.scrapItemId)] =
          lr.ToGlobalId(lr.rec->GetId());
      }
    }
    for (auto& [edid, slot] : cfg.powerArmorSlotKeywords) {
      if (auto it = keywordByEdid.find(edid); it != keywordByEdid.end()) {
        slotByKeyword[it->second] = slot;
      }
    }
    auto av = [&](const std::string& e) {
      auto it = avByEdid.find(e);
      return it == avByEdid.end() ? FormId(0) : it->second;
    };
    avFood = av(cfg.avFood);
    avWater = av(cfg.avWater);
    avSafety = av(cfg.avSafety);
    avPowerGen = av(cfg.avPowerGenerated);
    avPowerReq = av(cfg.avPowerRequired);
    avBeds = av(cfg.avBeds);
    avHappiness = av(cfg.avHappiness);
  }

  void FillCommon(ItemData& d, const espm::LookupResult& lr,
                  const espm::fo4::ItemCommon& c, float weight, int32_t value)
  {
    d.id = lr.ToGlobalId(lr.rec->GetId());
    d.editorId = c.editorId;
    d.name = c.fullName.empty() ? c.editorId : c.fullName;
    d.keywords = Map(lr, c.keywords);
    d.attachParentSlots = Map(lr, c.attachParentSlots);
    d.instanceNamingRules = Map(lr, c.instanceNamingRules);
    d.weight = weight;
    d.value = value;
  }

  std::vector<DamageTypeValue> MapDama(
    const espm::LookupResult& lr,
    const std::vector<espm::fo4::DamageTypeValue>& v)
  {
    std::vector<DamageTypeValue> res;
    for (auto& x : v) {
      res.push_back({ Map(lr, x.damageTypeId), static_cast<float>(x.value) });
    }
    return res;
  }

  const ItemData* LoadItem(FormId id)
  {
    if (auto it = items.find(id); it != items.end()) {
      return it->second.get();
    }
    if (notItem.count(id)) {
      return nullptr;
    }
    auto lr = br.LookupById(id);
    if (!lr.rec) {
      notItem[id] = true;
      return nullptr;
    }
    BuildIndex();
    std::unique_ptr<ItemData> out;
    if (auto w = espm::Convert<espm::fo4::WEAP>(lr.rec)) {
      auto d = w->GetData(cache);
      auto r = std::make_unique<WeaponData>();
      FillCommon(*r, lr, d.common, d.weight, static_cast<int32_t>(d.value));
      r->type = ItemType::Weapon;
      r->ammoId = Map(lr, d.ammoId);
      r->baseDamage = d.baseDamage;
      r->damageTypes = MapDama(lr, d.damageTypes);
      r->capacity = d.capacity;
      r->speed = d.speed;
      r->reloadSpeed = d.reloadSpeed;
      r->minRange = d.minRange;
      r->maxRange = d.maxRange;
      r->attackDelaySec = d.attackDelay;
      r->fireSeconds = d.animationFireSeconds;
      r->actionPointCost = d.actionPointCost;
      r->critDamageMult = d.critDamageMult;
      r->critChargeBonus = d.critChargeBonus;
      r->numProjectiles = d.numProjectiles;
      r->accuracyBonus = d.accuracyBonus;
      r->automatic = d.IsAutomatic();
      r->isGun = d.IsGun();
      using A = espm::fo4::WEAP::AnimationType;
      r->isThrown = d.animationType == A::Grenade ||
        d.animationType == A::Mine;
      r->isMelee = !r->isGun && !r->isThrown;
      r->embeddedModId = Map(lr, d.embeddedModId);
      out = std::move(r);
    } else if (auto a = espm::Convert<espm::fo4::ARMO>(lr.rec)) {
      auto d = a->GetData(cache);
      auto r = std::make_unique<ArmorData>();
      FillCommon(*r, lr, d.common, d.weight, d.value);
      r->type = ItemType::Armor;
      r->bipedSlots = d.bipedSlots;
      r->armorRating = d.armorRating;
      r->resistances = MapDama(lr, d.resistances);
      r->health = d.health;
      for (auto kw : r->keywords) {
        if (auto it = slotByKeyword.find(kw); it != slotByKeyword.end()) {
          r->powerArmorSlot = it->second;
        }
      }
      out = std::move(r);
    } else if (auto m = espm::Convert<espm::fo4::AMMO>(lr.rec)) {
      auto d = m->GetData(cache);
      auto r = std::make_unique<AmmoData>();
      FillCommon(*r, lr, d.common, d.weight, static_cast<int32_t>(d.value));
      r->type = ItemType::Ammo;
      r->projectileId = Map(lr, d.projectileId);
      r->damage = d.damage;
      r->charge = d.health;
      out = std::move(r);
    } else if (auto mi = espm::Convert<espm::fo4::MISC>(lr.rec)) {
      auto d = mi->GetData(cache);
      auto r = std::make_unique<MiscData>();
      FillCommon(*r, lr, d.common, d.weight, d.value);
      r->type = ItemType::Misc;
      for (auto& c : d.components) {
        r->components.push_back({ Map(lr, c.formId), c.count });
      }
      if (auto it = componentByScrap.find(r->id);
          it != componentByScrap.end()) {
        r->scrapItemForComponent = it->second;
      }
      out = std::move(r);
    } else if (auto al = espm::Convert<espm::fo4::ALCH>(lr.rec)) {
      auto d = al->GetData(cache);
      auto r = std::make_unique<ConsumableData>();
      FillCommon(*r, lr, d.common, d.weight, d.value);
      r->type = ItemType::Consumable;
      r->isFood = (d.flags & espm::fo4::ALCH::kFoodItem) != 0;
      r->isMedicine = (d.flags & espm::fo4::ALCH::kMedicine) != 0;
      r->addictionId = Map(lr, d.addictionId);
      r->addictionChance = d.addictionChance;
      for (auto& e : d.effects) {
        r->effects.push_back(
          { Map(lr, e.effectId), e.magnitude, e.duration });
      }
      out = std::move(r);
    } else {
      auto t = lr.rec->GetType();
      ItemType type = t == "BOOK" ? ItemType::Book
        : t == "KEYM"             ? ItemType::Key
        : t == "NOTE"             ? ItemType::Note
                                  : ItemType::Unknown;
      if (type == ItemType::Unknown) {
        notItem[id] = true;
        return nullptr;
      }
      auto r = std::make_unique<ItemData>();
      r->id = id;
      r->type = type;
      r->editorId = lr.rec->GetEditorId(cache);
      r->name = r->editorId;
      r->keywords = Map(lr, espm::fo4::GetKeywords(lr.rec, cache));
      out = std::move(r);
    }
    auto* raw = out.get();
    items[id] = std::move(out);
    return raw;
  }

  void BuildRecipes()
  {
    if (recipesBuilt) {
      return;
    }
    recipesBuilt = true;
    for (auto& lr : br.GetDistinctRecordsByType("COBJ")) {
      auto c = espm::Convert<espm::fo4::COBJ>(lr.rec);
      if (!c) {
        continue;
      }
      auto d = c->GetData(cache);
      RecipeData r;
      r.id = lr.ToGlobalId(lr.rec->GetId());
      r.editorId = d.editorId;
      r.createdObjectId = Map(lr, d.createdObjectId);
      r.createdCount = d.createdCount;
      r.workbenchKeywordId = Map(lr, d.workbenchKeywordId);
      r.categoryKeywords = Map(lr, d.categoryKeywords);
      r.priority = d.priority;
      for (auto& comp : d.components) {
        r.components.push_back({ Map(lr, comp.formId), comp.count });
      }
      for (auto& ctda : d.conditions) {
        if (ctda.functionIndex == cfg.hasPerkFunctionIndex) {
          uint32_t perk = 0;
          std::memcpy(&perk, ctda.functionData, 4);
          if (perk && ctda.comparisonValue >= 1.f) {
            r.perks.push_back({ Map(lr, perk), 1 });
          }
        }
      }
      recipesByBench.emplace(r.workbenchKeywordId, r.id);
      recipesByCreated.emplace(r.createdObjectId, r.id);
      recipes[r.id] = std::move(r);
    }
  }
};

EspmFo4DataSource::EspmFo4DataSource(const espm::CombineBrowser& browser,
                                     espm::CompressedFieldsCache& cache,
                                     EspmFo4DataConfig config)
  : pImpl(std::make_unique<Impl>(browser, cache, std::move(config)))
{
}

EspmFo4DataSource::~EspmFo4DataSource() = default;

const ItemData* EspmFo4DataSource::FindItem(FormId id) const
{
  std::lock_guard l(pImpl->m);
  return pImpl->LoadItem(id);
}

#define TYPED_FIND(Name, Type, Enum)                                          \
  const Type* EspmFo4DataSource::Name(FormId id) const                        \
  {                                                                           \
    auto item = FindItem(id);                                                 \
    return item && item->type == ItemType::Enum                               \
      ? static_cast<const Type*>(item)                                        \
      : nullptr;                                                              \
  }
TYPED_FIND(FindWeapon, WeaponData, Weapon)
TYPED_FIND(FindArmor, ArmorData, Armor)
TYPED_FIND(FindAmmo, AmmoData, Ammo)
TYPED_FIND(FindMisc, MiscData, Misc)
TYPED_FIND(FindConsumable, ConsumableData, Consumable)
#undef TYPED_FIND

const ComponentData* EspmFo4DataSource::FindComponent(FormId id) const
{
  std::lock_guard l(pImpl->m);
  auto& cache = pImpl->components;
  if (auto it = cache.find(id); it != cache.end()) {
    return it->second.get();
  }
  auto lr = pImpl->br.LookupById(id);
  auto c = lr.rec ? espm::Convert<espm::fo4::CMPO>(lr.rec) : nullptr;
  if (!c) {
    cache[id] = nullptr;
    return nullptr;
  }
  auto d = c->GetData(pImpl->cache);
  auto r = std::make_unique<ComponentData>();
  r->id = id;
  r->editorId = d.editorId;
  r->name = d.fullName.empty() ? d.editorId : d.fullName;
  r->scrapItemId = pImpl->Map(lr, d.scrapItemId);
  r->value = d.autoCalcValue;
  r->modScrapScalar = pImpl->cfg.defaultModScrapScalar;
  if (d.modScrapScalarId) {
    auto g = pImpl->br.LookupById(pImpl->Map(lr, d.modScrapScalarId));
    if (auto glob = g.rec ? espm::Convert<espm::fo4::GLOB>(g.rec) : nullptr) {
      r->modScrapScalar = glob->GetData(pImpl->cache).value;
    }
  }
  auto* raw = r.get();
  cache[id] = std::move(r);
  return raw;
}

const ObjectModData* EspmFo4DataSource::FindObjectMod(FormId id) const
{
  std::lock_guard l(pImpl->m);
  auto& cache = pImpl->mods;
  if (auto it = cache.find(id); it != cache.end()) {
    return it->second.get();
  }
  auto lr = pImpl->br.LookupById(id);
  auto o = lr.rec ? espm::Convert<espm::fo4::OMOD>(lr.rec) : nullptr;
  if (!o) {
    cache[id] = nullptr;
    return nullptr;
  }
  auto d = o->GetData(pImpl->cache);
  auto r = std::make_unique<ObjectModData>();
  r->id = id;
  r->editorId = d.editorId;
  r->name = d.fullName.empty() ? d.editorId : d.fullName;
  using T = espm::fo4::OMOD::TargetFormType;
  r->target = d.formType == T::Weapon ? OmodTarget::Weapon
    : d.formType == T::Armor          ? OmodTarget::Armor
    : d.formType == T::Npc            ? OmodTarget::Actor
                                      : OmodTarget::None;
  r->attachPointKeywordId = pImpl->Map(lr, d.attachPointKeywordId);
  r->attachParentSlots = pImpl->Map(lr, d.attachParentSlots);
  for (auto& inc : d.includes) {
    r->includes.push_back(pImpl->Map(lr, inc.modId));
  }
  for (auto& p : d.properties) {
    OmodPropertyData q;
    q.valueType = static_cast<OmodValueType>(p.valueType);
    q.function = static_cast<OmodFunction>(p.function);
    q.property = p.property;
    switch (q.valueType) {
      case OmodValueType::Float:
        q.value1 = p.Value1AsFloat();
        break;
      case OmodValueType::FormIdInt:
        q.formValue = pImpl->Map(lr, p.value1);
        q.value2 = static_cast<float>(p.value2);
        break;
      case OmodValueType::FormIdFloat:
        q.formValue = pImpl->Map(lr, p.value1);
        q.value2 = p.Value2AsFloat();
        break;
      default:
        q.value1 = static_cast<float>(static_cast<int32_t>(p.value1));
        break;
    }
    r->properties.push_back(q);
  }
  r->looseModId = pImpl->Map(lr, d.looseModId);
  r->priority = d.priority;
  r->legendary = d.isLegendary;
  auto* raw = r.get();
  cache[id] = std::move(r);
  return raw;
}

const RecipeData* EspmFo4DataSource::FindRecipe(FormId id) const
{
  std::lock_guard l(pImpl->m);
  pImpl->BuildRecipes();
  auto it = pImpl->recipes.find(id);
  return it == pImpl->recipes.end() ? nullptr : &it->second;
}

std::vector<const RecipeData*> EspmFo4DataSource::GetRecipesByWorkbench(
  FormId keyword) const
{
  std::lock_guard l(pImpl->m);
  pImpl->BuildRecipes();
  std::vector<const RecipeData*> res;
  auto [a, b] = pImpl->recipesByBench.equal_range(keyword);
  for (auto it = a; it != b; ++it) {
    res.push_back(&pImpl->recipes.at(it->second));
  }
  return res;
}

std::vector<const RecipeData*> EspmFo4DataSource::GetRecipesCreating(
  FormId createdObjectId) const
{
  std::lock_guard l(pImpl->m);
  pImpl->BuildRecipes();
  std::vector<const RecipeData*> res;
  auto [a, b] = pImpl->recipesByCreated.equal_range(createdObjectId);
  for (auto it = a; it != b; ++it) {
    res.push_back(&pImpl->recipes.at(it->second));
  }
  return res;
}

FormId EspmFo4DataSource::GetComponentByScrapItem(FormId miscId) const
{
  std::lock_guard l(pImpl->m);
  pImpl->BuildIndex();
  auto it = pImpl->componentByScrap.find(miscId);
  return it == pImpl->componentByScrap.end() ? 0 : it->second;
}

const FurnitureData* EspmFo4DataSource::FindFurniture(FormId id) const
{
  std::lock_guard l(pImpl->m);
  auto& cache = pImpl->furniture;
  if (auto it = cache.find(id); it != cache.end()) {
    return it->second.get();
  }
  auto lr = pImpl->br.LookupById(id);
  auto f = lr.rec ? espm::Convert<espm::fo4::FURN>(lr.rec) : nullptr;
  if (!f) {
    cache[id] = nullptr;
    return nullptr;
  }
  auto d = f->GetData(pImpl->cache);
  auto r = std::make_unique<FurnitureData>();
  r->id = id;
  r->editorId = d.editorId;
  r->keywords = pImpl->Map(lr, d.keywords);
  if (d.benchType) {
    r->workbench = static_cast<WorkbenchType>(*d.benchType);
  }
  r->isPowerArmorFrame = d.isPowerArmorFurniture;
  auto* raw = r.get();
  cache[id] = std::move(r);
  return raw;
}

const WorkshopObjectData* EspmFo4DataSource::FindWorkshopObject(
  FormId id) const
{
  std::lock_guard l(pImpl->m);
  auto& cache = pImpl->workshop;
  if (auto it = cache.find(id); it != cache.end()) {
    return it->second.get();
  }
  auto lr = pImpl->br.LookupById(id);
  if (!lr.rec) {
    cache[id] = nullptr;
    return nullptr;
  }
  pImpl->BuildIndex();
  auto r = std::make_unique<WorkshopObjectData>();
  r->baseId = id;
  for (auto& p : espm::fo4::GetActorValueProperties(lr.rec, pImpl->cache)) {
    FormId av = pImpl->Map(lr, p.actorValueId);
    if (av == 0) {
      continue;
    }
    if (av == pImpl->avFood)
      r->food += p.value;
    else if (av == pImpl->avWater)
      r->water += p.value;
    else if (av == pImpl->avSafety)
      r->defense += p.value;
    else if (av == pImpl->avPowerGen)
      r->powerGenerated += p.value;
    else if (av == pImpl->avPowerReq)
      r->powerRequired += p.value;
    else if (av == pImpl->avBeds)
      r->beds += p.value;
    else if (av == pImpl->avHappiness)
      r->happiness += p.value;
  }
  auto* raw = r.get();
  cache[id] = std::move(r);
  return raw;
}

FormId EspmFo4DataSource::FindKeywordByEditorId(const std::string& e) const
{
  std::lock_guard l(pImpl->m);
  pImpl->BuildIndex();
  auto it = pImpl->keywordByEdid.find(e);
  return it == pImpl->keywordByEdid.end() ? 0 : it->second;
}

FormId EspmFo4DataSource::FindActorValueByEditorId(const std::string& e) const
{
  std::lock_guard l(pImpl->m);
  pImpl->BuildIndex();
  auto it = pImpl->avByEdid.find(e);
  return it == pImpl->avByEdid.end() ? 0 : it->second;
}

}

namespace fo4 {

const LeveledListData* EspmFo4DataSource::FindLeveledList(FormId id) const
{
  std::lock_guard l(pImpl->m);
  auto& cache = pImpl->leveled;
  if (auto it = cache.find(id); it != cache.end()) {
    return it->second.get();
  }
  auto lr = pImpl->br.LookupById(id);
  std::optional<espm::fo4::LeveledListData> d;
  if (lr.rec) {
    if (auto li = espm::Convert<espm::fo4::LVLI>(lr.rec)) {
      d = li->GetData(pImpl->cache);
    } else if (auto ln = espm::Convert<espm::fo4::LVLN>(lr.rec)) {
      d = ln->GetData(pImpl->cache);
    }
  }
  if (!d) {
    cache[id] = nullptr;
    return nullptr;
  }
  auto r = std::make_unique<LeveledListData>();
  r->id = id;
  r->chanceNone = d->chanceNone;
  if (d->chanceNoneGlobalId) {
    auto g = pImpl->br.LookupById(pImpl->Map(lr, d->chanceNoneGlobalId));
    if (auto glob = g.rec ? espm::Convert<espm::fo4::GLOB>(g.rec) : nullptr) {
      r->chanceNone = glob->GetData(pImpl->cache).value;
    }
  }
  using L = espm::fo4::LeveledListData;
  r->calcFromAllLevels = (d->flags & L::kCalcFromAllLevels) != 0;
  r->calcForEachItem = (d->flags & L::kCalcForEachItem) != 0;
  r->useAll = (d->flags & L::kUseAll) != 0;
  for (auto& e : d->entries) {
    r->entries.push_back(
      { e.level, pImpl->Map(lr, e.refId), e.count, e.chanceNone });
  }
  auto* raw = r.get();
  cache[id] = std::move(r);
  return raw;
}

const ContainerData* EspmFo4DataSource::FindContainer(FormId id) const
{
  std::lock_guard l(pImpl->m);
  auto& cache = pImpl->containers;
  if (auto it = cache.find(id); it != cache.end()) {
    return it->second.get();
  }
  auto lr = pImpl->br.LookupById(id);
  auto c = lr.rec ? espm::Convert<espm::fo4::CONT>(lr.rec) : nullptr;
  if (!c) {
    cache[id] = nullptr;
    return nullptr;
  }
  auto d = c->GetData(pImpl->cache);
  auto r = std::make_unique<ContainerData>();
  r->id = id;
  r->respawns = d.respawns;
  for (auto& i : d.items) {
    r->items.push_back({ pImpl->Map(lr, i.formId), i.count });
  }
  auto* raw = r.get();
  cache[id] = std::move(r);
  return raw;
}

const NpcData* EspmFo4DataSource::FindNpc(FormId id) const
{
  std::lock_guard l(pImpl->m);
  auto& cache = pImpl->npcs;
  if (auto it = cache.find(id); it != cache.end()) {
    return it->second.get();
  }
  auto lr = pImpl->br.LookupById(id);
  auto n = lr.rec ? espm::Convert<espm::fo4::NPC_>(lr.rec) : nullptr;
  if (!n) {
    cache[id] = nullptr;
    return nullptr;
  }
  auto d = n->GetData(pImpl->cache);
  auto r = std::make_unique<NpcData>();
  r->id = id;
  r->editorId = d.editorId;
  r->flags = d.flags;
  r->level = d.level;
  r->pcLevelMult = (d.flags & espm::fo4::NPC_::kFlagPcLevelMult) != 0;
  r->levelMult = d.levelMult;
  r->calcMinLevel = d.calcMinLevel;
  r->calcMaxLevel = d.calcMaxLevel;
  r->templateFlags = d.templateFlags;
  for (size_t i = 0; i < r->templateActors.size(); ++i) {
    r->templateActors[i] = pImpl->Map(lr, d.templateActors[i]);
  }
  r->defaultTemplate = pImpl->Map(lr, d.defaultTemplate);
  for (auto& f : d.factions) {
    r->factions.push_back({ pImpl->Map(lr, f.factionId), f.rank });
  }
  r->race = pImpl->Map(lr, d.race);
  r->defaultOutfit = pImpl->Map(lr, d.defaultOutfit);
  r->deathItem = pImpl->Map(lr, d.deathItem);
  r->calculatedHealth = d.calculatedHealth;
  for (auto& i : d.items) {
    r->items.push_back({ pImpl->Map(lr, i.formId), i.count });
  }
  auto* raw = r.get();
  cache[id] = std::move(r);
  return raw;
}

const OutfitData* EspmFo4DataSource::FindOutfit(FormId id) const
{
  std::lock_guard l(pImpl->m);
  auto& cache = pImpl->outfits;
  if (auto it = cache.find(id); it != cache.end()) {
    return it->second.get();
  }
  auto lr = pImpl->br.LookupById(id);
  auto o = lr.rec ? espm::Convert<espm::fo4::OTFT>(lr.rec) : nullptr;
  if (!o) {
    cache[id] = nullptr;
    return nullptr;
  }
  auto d = o->GetData(pImpl->cache);
  auto r = std::make_unique<OutfitData>();
  r->id = id;
  for (auto i : d.items) {
    r->items.push_back(pImpl->Map(lr, i));
  }
  auto* raw = r.get();
  cache[id] = std::move(r);
  return raw;
}

}
