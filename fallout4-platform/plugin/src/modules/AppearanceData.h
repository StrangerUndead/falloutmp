#pragma once
// F03 appearance record and the TESNPC face block it maps to.
//
// The record is fo4msg::AppearanceFo4 (skymp5-server/cpp/messages/
// Fo4Messages.h) / AppearanceFo4 (falloutmp-client/src/services/messages/
// fo4Messages.ts); JSON field names and types match both. Form ids are
// runtime (load-order resolved) ids, the same on server and client.
//
// Face block (CommonLibF4 RE/T/TESNPC.h, F4SE GameObjects.h):
//   formRace (TESRaceForm), actorData female flag, headRelatedData
//   (hair colour, facial hair colour, face details TXST), headParts,
//   morphWeight, morphRegionSliderValues, facialBoneRegionSliderValues,
//   morphSliderValues, bodyTintColor RGBA, tintingData.
//
// Colours (tint rgba, skinTone) are the engine's u32 as stored: R in the
// low byte, then G, B, A.
#include <RE/Fallout.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fmp::appearance {

using Json = nlohmann::json;

struct MorphSlider
{
  uint32_t key = 0;
  float value = 0.f;
};

struct FaceRegion
{
  uint32_t index = 0;
  std::array<float, 3> pos = { 0, 0, 0 };
  std::array<float, 3> rot = { 0, 0, 0 };
  float scale = 1.f;
};

struct Tint
{
  uint16_t tintIndex = 0;  // template entry uniqueID (TETI index)
  uint8_t dataType = 0;    // BGSCharacterTint::EntryType: 0 mask, 1 palette, 2 texture
  uint8_t value = 0;       // intensity 0..100 (0 = off)
  uint32_t rgba = 0;       // palette colour
  int16_t templateColorIndex = -1; // palette swatch id, -1 = none
};

struct Appearance
{
  bool isFemale = false;
  uint32_t raceId = 0;
  uint32_t hairColorId = 0;
  uint32_t facialHairColorId = 0;
  uint32_t headTextureSetId = 0;
  std::vector<uint32_t> headPartIds; // sorted
  std::array<float, 3> bodyMorph = { 0, 0, 0 }; // thin, muscular, large
  std::vector<float> morphRegions;
  std::vector<MorphSlider> morphSliders; // sorted by key
  std::vector<FaceRegion> faceRegions;   // sorted by index
  float faceMorphIntensity = 1.f;
  std::vector<Tint> tints; // in the order of the NPC's tint array
  uint32_t skinTone = 0;
};

// Throws (nlohmann::json exceptions) on a malformed record. Lists over
// the protocol limits (F03 §4.2) are truncated.
Appearance FromJson(const Json& j);
Json ToJson(const Appearance& a);

// Reads the face block of `npc` (pass the root face NPC). `tints`
// replaces npc->tintingData when given (the player's actor-side copy);
// `fallbackRace` is used when the NPC has no race.
std::optional<Appearance> Read(RE::TESNPC* npc,
                               const RE::BGSCharacterTint::Entries* tints,
                               RE::TESRace* fallbackRace);

// Writes the whole face block. Unknown forms (head parts, colours) are
// skipped and logged; an unknown race fails the write (`error`).
bool Write(RE::TESNPC* npc, const Appearance& a, std::string& error);

// Makes a tint array hold exactly `tints` (missing entries are created
// from the race×sex tint templates, extra ones are dropped).
void WriteTints(RE::BGSCharacterTint::Entries* entries,
                const std::vector<Tint>& tints, RE::TESRace* race,
                bool female);

// After TESForm::Copy into a runtime NPC: gives `npc` its own face block,
// so writing it can never change `source` (e.g. the player's NPC 0x7).
void Detach(RE::TESNPC* npc, const RE::TESNPC* source);

}
