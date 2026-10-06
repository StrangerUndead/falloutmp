#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "AppearanceData.h"
#include "GameUtil.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace fmp::appearance {

namespace {
namespace tint = RE::BGSCharacterTint;
namespace morph = RE::BGSCharacterMorph;

// Protocol limits (F03 §4.2); numHeadParts is an int8 in the engine.
constexpr std::size_t kMaxHeadParts = 16;
constexpr std::size_t kMaxMorphRegions = 16;
constexpr std::size_t kMaxMorphSliders = 128;
constexpr std::size_t kMaxFaceRegions = 64;
constexpr std::size_t kMaxTints = 64;

constexpr uint8_t kMask = 0;
constexpr uint8_t kPalette = 1;
constexpr uint8_t kTexture = 2;

// Face-block objects live on the engine heap: the engine frees and
// reallocates them itself (LooksMenu, save load).
template <class T, class... Args>
T* New(Args&&... args)
{
  auto mem = RE::malloc(sizeof(T));
  return mem
    ? std::construct_at(static_cast<T*>(mem), std::forward<Args>(args)...)
    : nullptr;
}

template <class T>
void Delete(T* p)
{
  if (p) {
    std::destroy_at(p);
    RE::free(p);
  }
}

const Json& Array(const Json& j, const char* key)
{
  auto& v = j.at(key);
  if (!v.is_array()) {
    throw std::invalid_argument(
      std::format("appearance: {} is not an array", key));
  }
  return v;
}

float Finite(const Json& v)
{
  auto f = v.get<float>();
  return std::isfinite(f) ? f : 0.f;
}

std::array<float, 3> Vec3(const Json& v)
{
  return { Finite(v.at(0)), Finite(v.at(1)), Finite(v.at(2)) };
}

template <class T>
void Truncate(std::vector<T>& v, std::size_t max, const char* what)
{
  if (v.size() > max) {
    REX::WARN("appearance: {} has {} entries, keeping {}", what, v.size(),
              max);
    v.resize(max);
  }
}

// Vtables of the tint entry classes, to tell their types apart and to
// create new entries (as F4SE's BGSCharacterTint::Template::Entry::Create
// does). [verify] these VTABLE ids resolve on 1.11.
std::uintptr_t Address(const std::array<REL::ID, 1>& id)
{
  return REL::Relocation<std::uintptr_t>{ id[0] }.address();
}

struct Vtables
{
  std::uintptr_t mask, palette, texture, tMask, tPalette, tTexture;
};

const Vtables& Vt()
{
  static const Vtables v{
    Address(RE::VTABLE::BGSCharacterTint__MaskEntry),
    Address(RE::VTABLE::BGSCharacterTint__PaletteEntry),
    Address(RE::VTABLE::BGSCharacterTint__TextureSetEntry),
    Address(RE::VTABLE::BGSCharacterTint__Template__Mask),
    Address(RE::VTABLE::BGSCharacterTint__Template__Palette),
    Address(RE::VTABLE::BGSCharacterTint__Template__TextureSet),
  };
  return v;
}

std::uintptr_t VtableOf(const void* obj)
{
  return *static_cast<const std::uintptr_t*>(obj);
}

uint8_t TypeOf(tint::Entry* e)
{
  auto vt = VtableOf(e);
  auto& v = Vt();
  if (vt == v.palette) {
    return kPalette;
  }
  if (vt == v.mask) {
    return kMask;
  }
  if (vt == v.texture) {
    return kTexture;
  }
  return static_cast<uint8_t>(std::to_underlying(e->GetType()));
}

uint8_t TemplateTypeOf(const tint::Template::Entry* t, uint8_t fallback)
{
  auto vt = VtableOf(t);
  auto& v = Vt();
  if (vt == v.tPalette) {
    return kPalette;
  }
  if (vt == v.tMask) {
    return kMask;
  }
  if (vt == v.tTexture) {
    return kTexture;
  }
  return fallback;
}

// The race×sex tint template with this uniqueID (TETI index).
const tint::Template::Entry* FindTemplate(RE::TESRace* race, bool female,
                                          uint16_t id)
{
  auto data = race ? race->faceRelatedData[female ? 1 : 0] : nullptr;
  auto groups = data ? data->tintingTemplate : nullptr;
  if (!groups) {
    return nullptr;
  }
  for (auto group : groups->groups) {
    if (!group) {
      continue;
    }
    for (auto entry : group->entries) {
      if (entry && entry->uniqueID == id) {
        return entry;
      }
    }
  }
  return nullptr;
}

tint::Entry* NewEntry(uint8_t type)
{
  std::size_t size = sizeof(tint::MaskEntry);
  std::uintptr_t vtbl = Vt().mask;
  if (type == kPalette) {
    size = sizeof(tint::PaletteEntry);
    vtbl = Vt().palette;
  } else if (type == kTexture) {
    size = sizeof(tint::TextureSetEntry);
    vtbl = Vt().texture;
  }
  auto mem = RE::malloc(size);
  if (!mem) {
    return nullptr;
  }
  std::memset(mem, 0, size);
  *static_cast<std::uintptr_t*>(mem) = vtbl;
  return static_cast<tint::Entry*>(mem);
}

Tint ReadTint(tint::Entry* e)
{
  Tint t;
  t.tintIndex = e->idLink;
  t.dataType = TypeOf(e);
  t.value = e->tintingValue;
  if (t.dataType == kPalette) {
    auto pe = static_cast<tint::PaletteEntry*>(e);
    t.rgba = pe->tintingColor;
    t.templateColorIndex = static_cast<int16_t>(pe->swatchID);
  }
  return t;
}

void WriteHeadParts(RE::TESNPC* npc,
                    const std::vector<RE::BGSHeadPart*>& parts)
{
  const auto n =
    static_cast<std::int8_t>(std::min<std::size_t>(parts.size(), 127));
  RE::BGSHeadPart** arr = nullptr;
  if (n > 0) {
    if (npc->headParts && npc->numHeadParts >= n) {
      arr = npc->headParts; // fits: reuse in place
    } else {
      arr = static_cast<RE::BGSHeadPart**>(
        RE::malloc(sizeof(RE::BGSHeadPart*) * static_cast<std::size_t>(n)));
      if (!arr) {
        return;
      }
    }
    std::copy_n(parts.begin(), n, arr);
  }
  if (npc->headParts && npc->headParts != arr) {
    RE::free(npc->headParts);
  }
  npc->headParts = arr;
  npc->numHeadParts = n;
}
}

Appearance FromJson(const Json& j)
{
  Appearance a;
  a.isFemale = j.at("isFemale").get<bool>();
  a.raceId = j.at("raceId").get<uint32_t>();
  a.hairColorId = j.at("hairColorId").get<uint32_t>();
  a.facialHairColorId = j.at("facialHairColorId").get<uint32_t>();
  a.headTextureSetId = j.at("headTextureSetId").get<uint32_t>();
  for (auto& id : Array(j, "headPartIds")) {
    a.headPartIds.push_back(id.get<uint32_t>());
  }
  Truncate(a.headPartIds, kMaxHeadParts, "headPartIds");
  std::ranges::sort(a.headPartIds);
  a.bodyMorph = Vec3(Array(j, "bodyMorph"));
  for (auto& v : Array(j, "morphRegions")) {
    a.morphRegions.push_back(Finite(v));
  }
  Truncate(a.morphRegions, kMaxMorphRegions, "morphRegions");
  for (auto& s : Array(j, "morphSliders")) {
    a.morphSliders.push_back(
      { s.at("key").get<uint32_t>(), Finite(s.at("value")) });
  }
  Truncate(a.morphSliders, kMaxMorphSliders, "morphSliders");
  for (auto& r : Array(j, "faceRegions")) {
    FaceRegion f;
    f.index = r.at("index").get<uint32_t>();
    f.pos = Vec3(r.at("pos"));
    f.rot = Vec3(r.at("rot"));
    f.scale = Finite(r.at("scale"));
    a.faceRegions.push_back(f);
  }
  Truncate(a.faceRegions, kMaxFaceRegions, "faceRegions");
  a.faceMorphIntensity = Finite(j.at("faceMorphIntensity"));
  for (auto& t : Array(j, "tints")) {
    Tint x;
    x.tintIndex = t.at("tintIndex").get<uint16_t>();
    x.dataType = t.at("dataType").get<uint8_t>();
    x.value =
      static_cast<uint8_t>(std::min(t.at("value").get<uint32_t>(), 100u));
    x.rgba = t.at("rgba").get<uint32_t>();
    x.templateColorIndex = t.at("templateColorIndex").get<int16_t>();
    a.tints.push_back(x);
  }
  Truncate(a.tints, kMaxTints, "tints");
  a.skinTone = j.at("skinTone").get<uint32_t>();
  return a;
}

Json ToJson(const Appearance& a)
{
  auto sliders = Json::array();
  for (auto& s : a.morphSliders) {
    sliders.push_back({ { "key", s.key }, { "value", s.value } });
  }
  auto regions = Json::array();
  for (auto& f : a.faceRegions) {
    regions.push_back({ { "index", f.index },
                        { "pos", f.pos },
                        { "rot", f.rot },
                        { "scale", f.scale } });
  }
  auto tints = Json::array();
  for (auto& t : a.tints) {
    tints.push_back({ { "tintIndex", t.tintIndex },
                      { "dataType", t.dataType },
                      { "value", t.value },
                      { "rgba", t.rgba },
                      { "templateColorIndex", t.templateColorIndex } });
  }
  return { { "isFemale", a.isFemale },
           { "raceId", a.raceId },
           { "hairColorId", a.hairColorId },
           { "facialHairColorId", a.facialHairColorId },
           { "headTextureSetId", a.headTextureSetId },
           { "headPartIds", a.headPartIds },
           { "bodyMorph", a.bodyMorph },
           { "morphRegions", a.morphRegions },
           { "morphSliders", std::move(sliders) },
           { "faceRegions", std::move(regions) },
           { "faceMorphIntensity", a.faceMorphIntensity },
           { "tints", std::move(tints) },
           { "skinTone", a.skinTone } };
}

std::optional<Appearance> Read(RE::TESNPC* npc,
                               const RE::BGSCharacterTint::Entries* tints,
                               RE::TESRace* fallbackRace)
{
  if (!npc) {
    return std::nullopt;
  }
  Appearance a;
  a.isFemale = npc->IsFemale();
  auto race = npc->GetFormRace();
  if (!race) {
    race = fallbackRace;
  }
  a.raceId = race ? race->GetFormID() : 0;
  if (auto h = npc->headRelatedData) {
    a.hairColorId = h->hairColor ? h->hairColor->GetFormID() : 0;
    a.facialHairColorId =
      h->facialHairColor ? h->facialHairColor->GetFormID() : 0;
    a.headTextureSetId = h->faceDetails ? h->faceDetails->GetFormID() : 0;
  }
  // The NPC's own list (not the alternate list used while its race is
  // switched, e.g. by a transformation).
  if (npc->headParts) {
    for (int i = 0; i < npc->numHeadParts; ++i) {
      if (auto part = npc->headParts[i]) {
        a.headPartIds.push_back(part->GetFormID());
      }
    }
  }
  std::ranges::sort(a.headPartIds);
  a.bodyMorph = { npc->morphWeight.x, npc->morphWeight.y, npc->morphWeight.z };
  if (auto r = npc->morphRegionSliderValues) {
    a.morphRegions.assign(r->begin(), r->end());
  }
  if (auto m = npc->morphSliderValues) {
    for (const auto& kv : *m) {
      a.morphSliders.push_back({ kv.first, kv.second });
    }
    std::ranges::sort(a.morphSliders, {}, &MorphSlider::key);
  }
  if (auto m = npc->facialBoneRegionSliderValues) {
    for (const auto& kv : *m) {
      const auto& t = kv.second;
      // FMRS is pos[3], rot[3], scale: the engine keeps the scale in x
      a.faceRegions.push_back({ kv.first,
                                { t.position.x, t.position.y, t.position.z },
                                { t.rotation.x, t.rotation.y, t.rotation.z },
                                t.scale.x });
    }
    std::ranges::sort(a.faceRegions, {}, &FaceRegion::index);
  }
  const float intensity = npc->GetFacialBoneMorphIntensity();
  a.faceMorphIntensity = std::isfinite(intensity) ? intensity : 1.f;
  if (auto src = tints ? tints : npc->tintingData) {
    for (auto e : src->entriesA) {
      if (e) {
        a.tints.push_back(ReadTint(e));
      }
    }
  }
  a.skinTone =
    static_cast<uint32_t>(static_cast<uint8_t>(npc->bodyTintColorR)) |
    static_cast<uint32_t>(static_cast<uint8_t>(npc->bodyTintColorG)) << 8 |
    static_cast<uint32_t>(static_cast<uint8_t>(npc->bodyTintColorB)) << 16 |
    static_cast<uint32_t>(static_cast<uint8_t>(npc->bodyTintColorA)) << 24;
  return a;
}

bool Write(RE::TESNPC* npc, const Appearance& a, std::string& error)
{
  if (!npc) {
    error = "no NPC";
    return false;
  }
  auto race = game::Form<RE::TESRace>(a.raceId);
  if (!race) {
    error = std::format("unknown race {:X}", a.raceId);
    return false;
  }
  npc->SetFormRace(race);
  if (a.isFemale) {
    npc->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kFemale);
  } else {
    npc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kFemale);
  }

  // Colours and face details
  if (!npc->headRelatedData) {
    npc->headRelatedData = New<RE::TESNPC::HeadRelatedData>();
  }
  if (auto h = npc->headRelatedData) {
    h->hairColor = game::Form<RE::BGSColorForm>(a.hairColorId);
    h->facialHairColor = game::Form<RE::BGSColorForm>(a.facialHairColorId);
    h->faceDetails = game::Form<RE::BGSTextureSet>(a.headTextureSetId);
    if (a.hairColorId && !h->hairColor) {
      REX::WARN("appearance: unknown hair colour {:X}", a.hairColorId);
    }
  }

  std::vector<RE::BGSHeadPart*> parts;
  for (auto id : a.headPartIds) {
    if (auto part = game::Form<RE::BGSHeadPart>(id)) {
      parts.push_back(part);
    } else {
      REX::WARN("appearance: unknown head part {:X}", id);
    }
  }
  WriteHeadParts(npc, parts);

  // Body (thin/muscular/large) and region morphs
  npc->morphWeight = { a.bodyMorph[0], a.bodyMorph[1], a.bodyMorph[2] };
  if (a.morphRegions.empty()) {
    // No MRSV: the engine keeps no array (ESM NPCs without the field)
    Delete(npc->morphRegionSliderValues);
    npc->morphRegionSliderValues = nullptr;
  } else {
    if (!npc->morphRegionSliderValues) {
      npc->morphRegionSliderValues = New<RE::BSTArray<float>>();
    }
    if (auto r = npc->morphRegionSliderValues) {
      r->resize(static_cast<std::uint32_t>(a.morphRegions.size()));
      std::ranges::copy(a.morphRegions, r->begin());
    }
  }

  // Face morph sliders (MSDK/MSDV)
  if (!a.morphSliders.empty() && !npc->morphSliderValues) {
    npc->morphSliderValues = New<RE::BSTHashMap<std::uint32_t, float>>();
  }
  if (auto m = npc->morphSliderValues) {
    m->clear();
    for (auto& s : a.morphSliders) {
      m->emplace(s.key, s.value);
    }
  }

  // Facial bone regions (FMRI/FMRS)
  if (!a.faceRegions.empty() && !npc->facialBoneRegionSliderValues) {
    npc->facialBoneRegionSliderValues =
      New<RE::BSTHashMap<std::uint32_t, morph::Transform>>();
  }
  if (auto m = npc->facialBoneRegionSliderValues) {
    // Only scale.x is FMRS data; y/z (unknown, F4SE's value[7] and
    // padding) are kept as they were. [verify]
    std::unordered_map<uint32_t, RE::NiPoint3> oldScale;
    for (const auto& kv : *m) {
      oldScale.emplace(kv.first, kv.second.scale);
    }
    m->clear();
    for (auto& f : a.faceRegions) {
      morph::Transform t{};
      t.position = { f.pos[0], f.pos[1], f.pos[2] };
      t.rotation = { f.rot[0], f.rot[1], f.rot[2] };
      if (auto it = oldScale.find(f.index); it != oldScale.end()) {
        t.scale = it->second;
      }
      t.scale.x = f.scale;
      m->emplace(f.index, t);
    }
  }

  // FMIN: CommonLibF4 has the getter only (TESNPC::SetFacialBoneMorphIntensity
  // has no address-library id there), so the intensity is not written.
  // [verify] find the setter's 1.11 id; until then remote faces use the
  // intensity of the puppet's base NPC.

  // Tints: the record is the whole list (intensity 0 = off). The engine's
  // own setter re-applies template defaults first (FO4_Wrld); writing the
  // array directly skips that rule. [verify] tints match after Reset3D.
  if (!npc->tintingData) {
    npc->tintingData = New<RE::BGSCharacterTint::Entries>();
  }
  WriteTints(npc->tintingData, a.tints, race, a.isFemale);

  npc->bodyTintColorR = static_cast<std::int8_t>(a.skinTone & 0xFF);
  npc->bodyTintColorG = static_cast<std::int8_t>((a.skinTone >> 8) & 0xFF);
  npc->bodyTintColorB = static_cast<std::int8_t>((a.skinTone >> 16) & 0xFF);
  npc->bodyTintColorA = static_cast<std::int8_t>((a.skinTone >> 24) & 0xFF);
  return true;
}

void WriteTints(RE::BGSCharacterTint::Entries* entries,
                const std::vector<Tint>& tints, RE::TESRace* race, bool female)
{
  if (!entries) {
    return;
  }
  auto& arr = entries->entriesA;
  std::unordered_map<uint16_t, tint::Entry*> existing;
  for (auto e : arr) {
    if (e) {
      existing.emplace(e->idLink, e);
    }
  }
  std::vector<tint::Entry*> result;
  result.reserve(tints.size());
  for (auto& t : tints) {
    auto tmpl = FindTemplate(race, female, t.tintIndex);
    const uint8_t type = tmpl ? TemplateTypeOf(tmpl, t.dataType) : t.dataType;
    tint::Entry* e = nullptr;
    if (auto it = existing.find(t.tintIndex);
        it != existing.end() && TypeOf(it->second) == type) {
      e = it->second;
      existing.erase(it);
    } else if (tmpl) {
      e = NewEntry(type);
      if (e) {
        e->idLink = t.tintIndex;
      }
    } else {
      REX::WARN("appearance: tint {} has no template in race {:X}",
                t.tintIndex, race ? race->GetFormID() : 0);
      continue;
    }
    if (!e) {
      continue;
    }
    if (tmpl) {
      // The header types it as Entry*; it is the template entry (F4SE)
      e->templateEntry = reinterpret_cast<tint::Entry*>(
        const_cast<tint::Template::Entry*>(tmpl));
    }
    e->tintingValue = std::min<uint8_t>(t.value, 100);
    if (type == kPalette) {
      auto pe = static_cast<tint::PaletteEntry*>(e);
      pe->tintingColor = t.rgba;
      pe->swatchID = static_cast<std::uint16_t>(t.templateColorIndex);
    }
    result.push_back(e);
  }
  // Entries left out are taken off the array but not freed: the face
  // compositor may still reference them until the next rebuild (a few
  // bytes per change).
  arr.clear();
  for (auto e : result) {
    arr.push_back(e);
  }
}

void Detach(RE::TESNPC* npc, const RE::TESNPC* source)
{
  if (!npc || !source) {
    return;
  }
  // The face comes from this NPC, not from a template
  npc->faceNPC = nullptr;
  npc->actorData.templateUseFlags.reset(
    RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG::kTraits);

  // Copy() may have copied pointers: duplicate whatever is shared
  if (npc->headRelatedData &&
      npc->headRelatedData == source->headRelatedData) {
    npc->headRelatedData =
      New<RE::TESNPC::HeadRelatedData>(*source->headRelatedData);
  }
  if (npc->headParts && npc->headParts == source->headParts) {
    const int n = std::max<int>(npc->numHeadParts, 0);
    auto arr = n ? static_cast<RE::BGSHeadPart**>(RE::malloc(
                     sizeof(RE::BGSHeadPart*) * static_cast<std::size_t>(n)))
                 : nullptr;
    if (arr) {
      std::copy_n(source->headParts, n, arr);
    }
    npc->headParts = arr;
    npc->numHeadParts = arr ? npc->numHeadParts : 0;
  }
  if (npc->morphRegionSliderValues &&
      npc->morphRegionSliderValues == source->morphRegionSliderValues) {
    npc->morphRegionSliderValues =
      New<RE::BSTArray<float>>(*source->morphRegionSliderValues);
  }
  if (npc->morphSliderValues &&
      npc->morphSliderValues == source->morphSliderValues) {
    npc->morphSliderValues =
      New<RE::BSTHashMap<std::uint32_t, float>>(*source->morphSliderValues);
  }
  if (npc->facialBoneRegionSliderValues &&
      npc->facialBoneRegionSliderValues ==
        source->facialBoneRegionSliderValues) {
    npc->facialBoneRegionSliderValues =
      New<RE::BSTHashMap<std::uint32_t, morph::Transform>>(
        *source->facialBoneRegionSliderValues);
  }
  // Tint entries can be shared even when the array itself was copied:
  // start from an empty array of our own (the first apply fills it).
  npc->tintingData = New<RE::BGSCharacterTint::Entries>();
}

}
