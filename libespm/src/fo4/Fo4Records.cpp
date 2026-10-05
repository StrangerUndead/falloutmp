#include "libespm/fo4/Fo4Records.h"
#include "libespm/RecordHeaderAccess.h"
#include <cctype>
#include <cstring>

namespace espm::fo4 {

namespace {

bool Is(const char* type, const char (&sig)[5]) noexcept
{
  return std::memcmp(type, sig, 4) == 0;
}

// Bounds-checked little-endian reads from a subrecord.
class FieldReader
{
public:
  FieldReader(const char* data, uint32_t size) noexcept
    : p(data)
    , n(size)
  {
  }

  template <class T>
  bool Read(T& out) noexcept
  {
    if (pos + sizeof(T) > n) {
      ok = false;
      return false;
    }
    std::memcpy(&out, p + pos, sizeof(T));
    pos += sizeof(T);
    return true;
  }

  template <class T>
  T ReadOr(T def) noexcept
  {
    T v = def;
    Read(v);
    return v;
  }

  bool Skip(size_t bytes) noexcept
  {
    if (pos + bytes > n) {
      ok = false;
      return false;
    }
    pos += bytes;
    return true;
  }

  size_t Remaining() const noexcept { return pos <= n ? n - pos : 0; }
  bool Ok() const noexcept { return ok; }

private:
  const char* p;
  uint32_t n;
  size_t pos = 0;
  bool ok = true;
};

std::string ReadZString(const char* data, uint32_t size)
{
  return std::string(data, strnlen(data, size));
}

std::vector<uint32_t> ReadFormIdArray(const char* data, uint32_t size)
{
  std::vector<uint32_t> res(size / 4);
  if (!res.empty()) {
    std::memcpy(res.data(), data, res.size() * 4);
  }
  return res;
}

void ReadFull(const char* data, uint32_t size, std::string& outName,
              std::optional<uint32_t>& outStringId)
{
  // Localized plugins store a 4-byte string-table id. An inline string of
  // 3 printable chars + NUL also has 4 bytes, so look at the content.
  if (size == 4) {
    bool printable = std::isprint(static_cast<unsigned char>(data[0])) &&
      std::isprint(static_cast<unsigned char>(data[1])) &&
      std::isprint(static_cast<unsigned char>(data[2])) && data[3] == 0;
    if (!printable) {
      uint32_t id = 0;
      std::memcpy(&id, data, 4);
      outStringId = id;
      return;
    }
  }
  outName = ReadZString(data, size);
}

std::vector<DamageTypeValue> ReadDama(const char* data, uint32_t size)
{
  // Fallout 4 entries are {DMGT, amount, CURV} (12 bytes). Plugins saved by
  // older CK builds may have 8-byte entries without the curve table.
  // [verify ESPM-015: entry size on Fallout4.esm AE]
  std::vector<DamageTypeValue> res;
  size_t entrySize = (size % 12 == 0) ? 12 : (size % 8 == 0 ? 8 : 0);
  if (entrySize == 0) {
    return res;
  }
  for (size_t off = 0; off + entrySize <= size; off += entrySize) {
    DamageTypeValue v;
    std::memcpy(&v.damageTypeId, data + off, 4);
    std::memcpy(&v.value, data + off + 4, 4);
    if (entrySize == 12) {
      std::memcpy(&v.curveTableId, data + off + 8, 4);
    }
    res.push_back(v);
  }
  return res;
}

std::vector<ComponentCount> ReadComponents(const char* data, uint32_t size)
{
  std::vector<ComponentCount> res;
  for (size_t off = 0; off + 8 <= size; off += 8) {
    ComponentCount c;
    std::memcpy(&c.formId, data + off, 4);
    std::memcpy(&c.count, data + off + 4, 4);
    res.push_back(c);
  }
  return res;
}

std::optional<CTDA> ReadCtda(const char* data, uint32_t size)
{
  if (size < sizeof(CTDA)) {
    return std::nullopt;
  }
  CTDA c;
  std::memcpy(&c, data, sizeof(CTDA));
  return c;
}

// Handles subrecords shared by item records. Returns true if consumed.
bool ReadItemCommon(ItemCommon& c, const char* type, uint32_t size,
                    const char* data, uint32_t& ksiz)
{
  if (Is(type, "EDID")) {
    c.editorId = ReadZString(data, size);
  } else if (Is(type, "FULL")) {
    ReadFull(data, size, c.fullName, c.fullNameStringId);
  } else if (Is(type, "KSIZ") && size >= 4) {
    std::memcpy(&ksiz, data, 4);
  } else if (Is(type, "KWDA")) {
    auto ids = ReadFormIdArray(data, size);
    if (ksiz < ids.size()) {
      ids.resize(ksiz);
    }
    c.keywords = std::move(ids);
  } else if (Is(type, "APPR")) {
    c.attachParentSlots = ReadFormIdArray(data, size);
  } else if (Is(type, "INRD") && size >= 4) {
    std::memcpy(&c.instanceNamingRules, data, 4);
  } else {
    return false;
  }
  return true;
}

uint32_t ReadU32(const char* data, uint32_t size, uint32_t def = 0)
{
  if (size < 4) {
    return def;
  }
  uint32_t v;
  std::memcpy(&v, data, 4);
  return v;
}

} // namespace

float OmodProperty::Value1AsFloat() const noexcept
{
  float f;
  std::memcpy(&f, &value1, 4);
  return f;
}

float OmodProperty::Value2AsFloat() const noexcept
{
  float f;
  std::memcpy(&f, &value2, 4);
  return f;
}

std::vector<uint32_t> GetKeywords(const RecordHeader* rec,
                                  CompressedFieldsCache& cache) noexcept
{
  ItemCommon c;
  uint32_t ksiz = UINT32_MAX;
  RecordHeaderAccess::IterateFields(
    rec,
    [&](const char* type, uint32_t size, const char* data) {
      if (Is(type, "KSIZ") || Is(type, "KWDA")) {
        ReadItemCommon(c, type, size, data, ksiz);
      }
    },
    cache);
  return c.keywords;
}

std::vector<ActorValueProperty> GetActorValueProperties(
  const RecordHeader* rec, CompressedFieldsCache& cache) noexcept
{
  std::vector<ActorValueProperty> res;
  RecordHeaderAccess::IterateFields(
    rec,
    [&](const char* type, uint32_t size, const char* data) {
      if (Is(type, "PRPS")) {
        for (size_t off = 0; off + 8 <= size; off += 8) {
          ActorValueProperty p;
          std::memcpy(&p.actorValueId, data + off, 4);
          std::memcpy(&p.value, data + off + 4, 4);
          res.push_back(p);
        }
      }
    },
    cache);
  return res;
}

WEAP::Data WEAP::GetData(CompressedFieldsCache& cache) const noexcept
{
  Data d;
  uint32_t ksiz = UINT32_MAX;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t size, const char* data) {
      if (ReadItemCommon(d.common, type, size, data, ksiz)) {
        return;
      }
      if (Is(type, "DNAM")) {
        FieldReader r(data, size);
        r.Read(d.ammoId);
        r.Read(d.speed);
        r.Read(d.reloadSpeed);
        r.Read(d.reach);
        r.Read(d.minRange);
        r.Read(d.maxRange);
        r.Read(d.attackDelay);
        r.Skip(4); // unused
        r.Read(d.outOfRangeDamageMult);
        r.Read(d.onHit);
        r.Read(d.skillAvId);
        r.Read(d.resistAvId);
        r.Read(d.flags);
        r.Read(d.capacity);
        uint8_t anim = 0;
        if (r.Read(anim)) {
          d.animationType = static_cast<AnimationType>(anim);
        }
        r.Read(d.secondaryDamage);
        r.Read(d.weight);
        r.Read(d.value);
        r.Read(d.baseDamage);
        r.Read(d.soundLevel);
        r.Skip(8 * 4); // 8 sound descriptors
        r.Read(d.accuracyBonus);
        r.Read(d.animationAttackSeconds);
        r.Skip(2);
        r.Read(d.actionPointCost);
        r.Read(d.fullPowerSeconds);
        r.Read(d.minPowerPerShot);
        r.Read(d.stagger);
        // The base fields up to Value/BaseDamage are what the server needs.
        d.hasDnam = size >= 69;
      } else if (Is(type, "FNAM")) {
        FieldReader r(data, size);
        r.Read(d.animationFireSeconds);
        r.Skip(12); // rumble
        r.Read(d.animationReloadSeconds);
        r.Skip(4); // bolt anim seconds
        r.Read(d.sightedTransitionSeconds);
        r.Read(d.numProjectiles);
        r.Read(d.overrideProjectileId);
      } else if (Is(type, "CRDT")) {
        FieldReader r(data, size);
        r.Read(d.critDamageMult);
        r.Read(d.critChargeBonus);
        r.Read(d.critEffectSpellId);
      } else if (Is(type, "DAMA")) {
        d.damageTypes = ReadDama(data, size);
      } else if (Is(type, "CNAM")) {
        d.templateWeaponId = ReadU32(data, size);
      } else if (Is(type, "NNAM")) {
        d.embeddedModId = ReadU32(data, size);
      } else if (Is(type, "LNAM")) {
        d.npcAmmoListId = ReadU32(data, size);
      }
    },
    cache);
  return d;
}

ARMO::Data ARMO::GetData(CompressedFieldsCache& cache) const noexcept
{
  Data d;
  uint32_t ksiz = UINT32_MAX;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t size, const char* data) {
      if (ReadItemCommon(d.common, type, size, data, ksiz)) {
        return;
      }
      if (Is(type, "BOD2")) {
        d.bipedSlots = ReadU32(data, size);
      } else if (Is(type, "DATA")) {
        FieldReader r(data, size);
        r.Read(d.value);
        r.Read(d.weight);
        r.Read(d.health);
      } else if (Is(type, "FNAM")) {
        FieldReader r(data, size);
        r.Read(d.armorRating);
        r.Read(d.baseAddonIndex);
        r.Read(d.staggerRating);
      } else if (Is(type, "DAMA")) {
        d.resistances = ReadDama(data, size);
      } else if (Is(type, "TNAM")) {
        d.templateArmorId = ReadU32(data, size);
      } else if (Is(type, "RNAM")) {
        d.raceId = ReadU32(data, size);
      }
    },
    cache);
  return d;
}

AMMO::Data AMMO::GetData(CompressedFieldsCache& cache) const noexcept
{
  Data d;
  uint32_t ksiz = UINT32_MAX;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t size, const char* data) {
      if (ReadItemCommon(d.common, type, size, data, ksiz)) {
        return;
      }
      if (Is(type, "DATA")) {
        FieldReader r(data, size);
        r.Read(d.value);
        r.Read(d.weight);
      } else if (Is(type, "DNAM")) {
        FieldReader r(data, size);
        r.Read(d.projectileId);
        r.Read(d.flags);
        r.Skip(3);
        r.Read(d.damage);
        r.Read(d.health);
      }
    },
    cache);
  return d;
}

MISC::Data MISC::GetData(CompressedFieldsCache& cache) const noexcept
{
  Data d;
  uint32_t ksiz = UINT32_MAX;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t size, const char* data) {
      if (ReadItemCommon(d.common, type, size, data, ksiz)) {
        return;
      }
      if (Is(type, "DATA")) {
        FieldReader r(data, size);
        r.Read(d.value);
        r.Read(d.weight);
      } else if (Is(type, "CVPA")) {
        d.components = ReadComponents(data, size);
      }
    },
    cache);
  return d;
}

CMPO::Data CMPO::GetData(CompressedFieldsCache& cache) const noexcept
{
  Data d;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t size, const char* data) {
      if (Is(type, "EDID")) {
        d.editorId = ReadZString(data, size);
      } else if (Is(type, "FULL")) {
        ReadFull(data, size, d.fullName, d.fullNameStringId);
      } else if (Is(type, "DATA")) {
        d.autoCalcValue = ReadU32(data, size);
      } else if (Is(type, "MNAM")) {
        d.scrapItemId = ReadU32(data, size);
      } else if (Is(type, "GNAM")) {
        d.modScrapScalarId = ReadU32(data, size);
      }
    },
    cache);
  return d;
}

COBJ::Data COBJ::GetData(CompressedFieldsCache& cache) const noexcept
{
  Data d;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t size, const char* data) {
      if (Is(type, "EDID")) {
        d.editorId = ReadZString(data, size);
      } else if (Is(type, "FVPA")) {
        d.components = ReadComponents(data, size);
      } else if (Is(type, "CTDA")) {
        if (auto c = ReadCtda(data, size)) {
          d.conditions.push_back(*c);
        }
      } else if (Is(type, "CNAM")) {
        d.createdObjectId = ReadU32(data, size);
      } else if (Is(type, "BNAM")) {
        d.workbenchKeywordId = ReadU32(data, size);
      } else if (Is(type, "FNAM")) {
        d.categoryKeywords = ReadFormIdArray(data, size);
      } else if (Is(type, "INTV")) {
        FieldReader r(data, size);
        r.Read(d.createdCount);
        r.Read(d.priority);
        if (d.createdCount == 0) {
          d.createdCount = 1;
        }
      }
    },
    cache);
  return d;
}

OMOD::Data OMOD::GetData(CompressedFieldsCache& cache) const noexcept
{
  Data d;
  d.isLegendary = (GetFlags() & 0x8) != 0;
  d.isModCollection = (GetFlags() & 0x40) != 0;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t size, const char* data) {
      if (Is(type, "EDID")) {
        d.editorId = ReadZString(data, size);
      } else if (Is(type, "FULL")) {
        ReadFull(data, size, d.fullName, d.fullNameStringId);
      } else if (Is(type, "DATA")) {
        FieldReader r(data, size);
        uint32_t includeCount = 0, propertyCount = 0;
        r.Read(includeCount);
        r.Read(propertyCount);
        r.Skip(2); // two unknown bools
        uint32_t formType = 0;
        r.Read(formType);
        d.formType = static_cast<TargetFormType>(formType);
        r.Read(d.maxRank);
        r.Read(d.levelTierScaledOffset);
        r.Read(d.attachPointKeywordId);
        uint32_t apsCount = 0;
        r.Read(apsCount);
        for (uint32_t i = 0; i < apsCount && r.Ok(); ++i) {
          uint32_t kw = 0;
          if (r.Read(kw)) {
            d.attachParentSlots.push_back(kw);
          }
        }
        uint32_t itemCount = 0;
        r.Read(itemCount);
        r.Skip(static_cast<size_t>(itemCount) * 8);
        for (uint32_t i = 0; i < includeCount && r.Ok(); ++i) {
          Include inc;
          uint8_t opt = 0, dontUseAll = 0;
          r.Read(inc.modId);
          r.Read(inc.minimumLevel);
          r.Read(opt);
          r.Read(dontUseAll);
          inc.optional = opt != 0;
          inc.dontUseAll = dontUseAll != 0;
          if (r.Ok()) {
            d.includes.push_back(inc);
          }
        }
        for (uint32_t i = 0; i < propertyCount && r.Ok(); ++i) {
          OmodProperty p;
          uint8_t vt = 0, fn = 0;
          r.Read(vt);
          r.Skip(3);
          r.Read(fn);
          r.Skip(3);
          r.Read(p.property);
          r.Skip(2);
          r.Read(p.value1);
          r.Read(p.value2);
          r.Read(p.step);
          p.valueType = static_cast<OmodProperty::ValueType>(vt);
          p.function = static_cast<OmodProperty::Function>(fn);
          if (r.Ok()) {
            d.properties.push_back(p);
          }
        }
        d.parsedOk = r.Ok();
      } else if (Is(type, "MNAM")) {
        d.targetOmodKeywords = ReadFormIdArray(data, size);
      } else if (Is(type, "FNAM")) {
        d.filterKeywords = ReadFormIdArray(data, size);
      } else if (Is(type, "LNAM")) {
        d.looseModId = ReadU32(data, size);
      } else if (Is(type, "NAM1") && size >= 1) {
        d.priority = static_cast<uint8_t>(data[0]);
      }
    },
    cache);
  return d;
}

FURN::Data FURN::GetData(CompressedFieldsCache& cache) const noexcept
{
  Data d;
  d.isPowerArmorFurniture = (GetFlags() & kPowerArmorRecordFlag) != 0;
  ItemCommon c;
  uint32_t ksiz = UINT32_MAX;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t size, const char* data) {
      if (Is(type, "EDID")) {
        d.editorId = ReadZString(data, size);
      } else if (Is(type, "KSIZ") || Is(type, "KWDA")) {
        ReadItemCommon(c, type, size, data, ksiz);
      } else if (Is(type, "WBDT") && size >= 1) {
        d.benchType = static_cast<BenchType>(static_cast<uint8_t>(data[0]));
      } else if (Is(type, "CNTO") && size >= 8) {
        ComponentCount c;
        std::memcpy(&c.formId, data, 4);
        int32_t count = 0;
        std::memcpy(&count, data + 4, 4);
        c.count = count > 0 ? static_cast<uint32_t>(count) : 0;
        d.containerItems.push_back(c);
      }
    },
    cache);
  d.keywords = std::move(c.keywords);
  return d;
}

namespace {
LeveledListData ReadLeveled(const RecordHeader* rec,
                            CompressedFieldsCache& cache) noexcept
{
  LeveledListData d;
  RecordHeaderAccess::IterateFields(
    rec,
    [&](const char* type, uint32_t size, const char* data) {
      if (Is(type, "EDID")) {
        d.editorId = ReadZString(data, size);
      } else if (Is(type, "LVLD") && size >= 1) {
        d.chanceNone = static_cast<uint8_t>(data[0]);
      } else if (Is(type, "LVLF") && size >= 1) {
        d.flags = static_cast<uint8_t>(data[0]);
      } else if (Is(type, "LVLG")) {
        d.chanceNoneGlobalId = ReadU32(data, size);
      } else if (Is(type, "LVLO") && size >= 12) {
        // u16 level; u8[2]; formid; u16 count; u8 chanceNone; u8
        LeveledEntry e;
        std::memcpy(&e.level, data, 2);
        std::memcpy(&e.refId, data + 4, 4);
        std::memcpy(&e.count, data + 8, 2);
        e.chanceNone = static_cast<uint8_t>(data[10]);
        d.entries.push_back(e);
      }
    },
    cache);
  return d;
}
}

LeveledListData LVLI::GetData(CompressedFieldsCache& cache) const noexcept
{
  return ReadLeveled(this, cache);
}

LeveledListData LVLN::GetData(CompressedFieldsCache& cache) const noexcept
{
  return ReadLeveled(this, cache);
}

CONT::Data CONT::GetData(CompressedFieldsCache& cache) const noexcept
{
  Data d;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t size, const char* data) {
      if (Is(type, "EDID")) {
        d.editorId = ReadZString(data, size);
      } else if (Is(type, "CNTO") && size >= 8) {
        ComponentCount c;
        std::memcpy(&c.formId, data, 4);
        int32_t count = 0;
        std::memcpy(&count, data + 4, 4);
        c.count = count > 0 ? static_cast<uint32_t>(count) : 0;
        d.items.push_back(c);
      } else if (Is(type, "DATA") && size >= 1) {
        d.respawns = (static_cast<uint8_t>(data[0]) & 0x2) != 0;
      }
    },
    cache);
  return d;
}

REFR::Data REFR::GetData(CompressedFieldsCache& cache) const noexcept
{
  Data d;
  d.initiallyDisabled = (GetFlags() & 0x800) != 0;
  d.deleted = (GetFlags() & 0x20) != 0;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t size, const char* data) {
      if (Is(type, "EDID")) {
        d.editorId = ReadZString(data, size);
      } else if (Is(type, "NAME")) {
        d.baseId = ReadU32(data, size);
      } else if (Is(type, "DATA") && size >= 24) {
        std::memcpy(d.pos.data(), data, 12);
        std::memcpy(d.rotRadians.data(), data + 12, 12);
        d.hasPlacement = true;
      } else if (Is(type, "XSCL") && size >= 4) {
        std::memcpy(&d.scale, data, 4);
      } else if (Is(type, "XLOC") && size >= 1) {
        FieldReader r(data, size);
        uint8_t level = 0;
        r.Read(level);
        d.lockLevel = level;
        r.Skip(3);
        r.Read(d.lockKeyId);
        uint8_t flags = 0;
        if (r.Read(flags)) {
          d.leveledLock = (flags & 0x4) != 0;
        }
      } else if (Is(type, "XPRM") && size >= 32) {
        std::memcpy(d.primitiveBounds.data(), data, 12);
        uint32_t t = 0;
        std::memcpy(&t, data + 28, 4);
        d.primitiveType = static_cast<PrimitiveType>(t);
      } else if (Is(type, "XLKR") && size >= 8) {
        LinkedRef l;
        std::memcpy(&l.keywordId, data, 4);
        std::memcpy(&l.refId, data + 4, 4);
        d.linkedRefs.push_back(l);
      } else if (Is(type, "XOWN") && size >= 4) {
        d.ownerId = ReadU32(data, size);
      } else if (Is(type, "XLCN") && size >= 4) {
        d.persistLocationId = ReadU32(data, size);
      }
    },
    cache);
  return d;
}

GLOB::Data GLOB::GetData(CompressedFieldsCache& cache) const noexcept
{
  Data d;
  d.isConstant = (GetFlags() & 0x40) != 0;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t size, const char* data) {
      if (Is(type, "EDID")) {
        d.editorId = ReadZString(data, size);
      } else if (Is(type, "FNAM") && size >= 1) {
        d.type = data[0];
      } else if (Is(type, "FLTV") && size >= 4) {
        std::memcpy(&d.value, data, 4);
      }
    },
    cache);
  return d;
}

ALCH::Data ALCH::GetData(CompressedFieldsCache& cache) const noexcept
{
  Data d;
  uint32_t ksiz = UINT32_MAX;
  RecordHeaderAccess::IterateFields(
    this,
    [&](const char* type, uint32_t size, const char* data) {
      if (ReadItemCommon(d.common, type, size, data, ksiz)) {
        return;
      }
      if (Is(type, "DATA") && size >= 4) {
        std::memcpy(&d.weight, data, 4);
      } else if (Is(type, "ENIT")) {
        FieldReader r(data, size);
        r.Read(d.value);
        r.Read(d.flags);
        r.Read(d.addictionId);
        r.Read(d.addictionChance);
      } else if (Is(type, "EFID")) {
        Effect e;
        e.effectId = ReadU32(data, size);
        d.effects.push_back(e);
      } else if (Is(type, "EFIT") && !d.effects.empty()) {
        FieldReader r(data, size);
        r.Read(d.effects.back().magnitude);
        r.Read(d.effects.back().area);
        r.Read(d.effects.back().duration);
      } else if (Is(type, "CTDA") && !d.effects.empty()) {
        if (auto c = ReadCtda(data, size)) {
          d.effects.back().conditions.push_back(*c);
        }
      }
    },
    cache);
  return d;
}

}
