// The entry patch (design §5) and the verify step after every write (§6).
#include "Bytes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fmp::fos {

namespace {
[[noreturn]] void NotTemplate(const std::string& why)
{
  throw Error(ErrorCode::NotATemplate, why);
}

[[noreturn]] void Refuse(const std::string& why)
{
  throw Error(ErrorCode::PatchRefused, why);
}

[[noreturn]] void Mismatch(const std::string& where)
{
  throw Error(ErrorCode::VerifyFailed, "unexpected difference in " + where);
}

bool SameBits(float a, float b)
{
  return std::memcmp(&a, &b, 4) == 0;
}

void CheckValues(const EntryPatch& patch)
{
  if (patch.placement) {
    const auto& p = *patch.placement;
    for (int i = 0; i < 3; ++i) {
      if (!std::isfinite(p.pos[i])) {
        Refuse("position is not a finite number");
      }
    }
    if (std::fabs(p.pos[0]) > 500000.f || std::fabs(p.pos[1]) > 500000.f ||
        std::fabs(p.pos[2]) > 100000.f) {
      Refuse("position is outside the world");
    }
    if (!std::isfinite(p.yawRadians) || std::fabs(p.yawRadians) > 7.f) {
      Refuse("yaw must be in radians, within ±7");
    }
    if (!RefId::FromDefaultForm(p.worldspace)) {
      Refuse("worldspace must be a Fallout4.esm form");
    }
  }
  if (patch.gameHour &&
      !(std::isfinite(*patch.gameHour) && *patch.gameHour >= 0.f &&
        *patch.gameHour < 24.f)) {
    Refuse("GameHour must be in [0, 24)");
  }
  if (patch.gameDaysPassed &&
      !(std::isfinite(*patch.gameDaysPassed) &&
        *patch.gameDaysPassed >= 0.f)) {
    Refuse("GameDaysPassed must be a non-negative number");
  }
}

// What Player Location must read after the patch.
PlayerLocation ExpectedLocation(const PlayerLocation& before,
                                const EntryPatch& patch)
{
  PlayerLocation after = before;
  if (patch.placement) {
    const auto& p = *patch.placement;
    const RefId ws = *RefId::FromDefaultForm(p.worldspace);
    after.worldspace = ws;
    after.worldOrCell = ws;
    after.gridX = static_cast<int32_t>(std::floor(p.pos[0] / 4096.f));
    after.gridY = static_cast<int32_t>(std::floor(p.pos[1] / 4096.f));
    after.pos = p.pos;
  }
  return after;
}

std::optional<float> PatchedGlobal(const EntryPatch& patch, uint32_t formId)
{
  if (formId == kGlobalGameHour) {
    return patch.gameHour;
  }
  if (formId == kGlobalGameDaysPassed) {
    return patch.gameDaysPassed;
  }
  return std::nullopt;
}
}

void CheckEntryTemplate(const SaveFile& save)
{
  PlayerLocation loc;
  try {
    loc = PlayerLocation::Read(save);
  } catch (const Error& e) {
    NotTemplate(e.what());
  }
  if (loc.worldspace.GetKind() != RefId::Kind::Default ||
      !(loc.worldOrCell == loc.worldspace)) {
    NotTemplate("the player isn't outdoors in a Fallout4.esm worldspace");
  }
  if (ReadSkyMode(save) != 3u) {
    NotTemplate("the weather block isn't in outdoor mode");
  }
  if (!GetGlobal(save, kGlobalGameHour) ||
      !GetGlobal(save, kGlobalGameDaysPassed)) {
    NotTemplate("GameHour or GameDaysPassed is missing");
  }
  const ChangeForm* player = FindPlayerRecord(save);
  if (!player) {
    NotTemplate("no player record");
  }
  if (!(player->flags & kFlagMove) || (player->flags & kFlagHavokMove)) {
    NotTemplate("the player record must have MOVE and not HAVOK_MOVE");
  }
  const int type = InitialDataType(*player);
  if (type != 4 && type != 6) {
    NotTemplate("unexpected initial data type " + std::to_string(type));
  }
  InitialPrefix prefix;
  try {
    prefix = ReadInitialPrefix(*player);
  } catch (const Error& e) {
    NotTemplate(e.what());
  }
  if (!(prefix.space == loc.worldOrCell)) {
    NotTemplate("the player record and Player Location disagree on the "
                "worldspace");
  }
  for (int i = 0; i < 3; ++i) {
    if (!(std::fabs(prefix.pos[i] - loc.pos[i]) <= 1.f)) {
      NotTemplate("the player record and Player Location disagree on the "
                  "position");
    }
  }
}

SaveFile ApplyEntryPatch(const SaveFile& templateSave, const EntryPatch& patch)
{
  CheckValues(patch);
  CheckEntryTemplate(templateSave);
  SaveFile out = templateSave;

  if (patch.placement) {
    const auto& p = *patch.placement;
    ExpectedLocation(PlayerLocation::Read(out), patch).Write(out);
    ChangeForm* player = FindPlayerRecord(out);
    Bytes body = player->Body();
    detail::WritePrefix(body, *RefId::FromDefaultForm(p.worldspace), p.pos,
                        { 0.f, 0.f, p.yawRadians });
    player->SetBody(body);
  }
  if (patch.gameHour) {
    SetGlobal(out, kGlobalGameHour, *patch.gameHour);
  }
  if (patch.gameDaysPassed) {
    SetGlobal(out, kGlobalGameDaysPassed, *patch.gameDaysPassed);
  }
  return out;
}

void VerifyEntryPatch(const SaveFile& t, const SaveFile& o,
                      const EntryPatch& patch)
{
  if (!(t.header == o.header)) {
    Mismatch("the header");
  }
  if (t.screenshot != o.screenshot) {
    Mismatch("the screenshot");
  }
  if (t.formVersion != o.formVersion || t.gameVersion != o.gameVersion ||
      t.plugins != o.plugins || t.hasLightPluginList != o.hasLightPluginList ||
      t.lightPlugins != o.lightPlugins) {
    Mismatch("the plugin block");
  }
  if (t.fltUnused != o.fltUnused) {
    Mismatch("the file location table");
  }
  if (!(t.table2 == o.table2)) {
    Mismatch("global data table 2");
  }
  if (!(t.table3 == o.table3)) {
    Mismatch("global data table 3");
  }
  if (t.formIds != o.formIds) {
    Mismatch("the FormID array");
  }
  if (t.visitedWorldspaces != o.visitedWorldspaces) {
    Mismatch("the visited worldspaces");
  }
  if (t.unknownTable3 != o.unknownTable3) {
    Mismatch("unknown table 3");
  }

  // Table 1: Player Location and two globals may change
  if (t.table1.size() != o.table1.size()) {
    Mismatch("global data table 1 (block count)");
  }
  for (size_t i = 0; i < t.table1.size(); ++i) {
    const auto& a = t.table1[i];
    const auto& b = o.table1[i];
    if (a.type != b.type) {
      Mismatch("global data table 1 (block order)");
    }
    if (a.type == 1 || a.type == 3) {
      continue;
    }
    if (a.data != b.data) {
      Mismatch("global data table 1, type " + std::to_string(a.type));
    }
  }
  if (!(PlayerLocation::Read(o) ==
        ExpectedLocation(PlayerLocation::Read(t), patch))) {
    Mismatch("Player Location");
  }
  const auto ga = ReadGlobals(t);
  const auto gb = ReadGlobals(o);
  if (ga.size() != gb.size()) {
    Mismatch("the Global Variables (count)");
  }
  for (size_t i = 0; i < ga.size(); ++i) {
    if (!(ga[i].ref == gb[i].ref)) {
      Mismatch("the Global Variables (order)");
    }
    const auto expected = PatchedGlobal(patch, ga[i].ref.Resolve(t.formIds));
    if (!SameBits(gb[i].value, expected ? *expected : ga[i].value)) {
      Mismatch("global " + std::to_string(ga[i].ref.Resolve(t.formIds)));
    }
  }

  // Change forms: only the player record may change, in its first 27 bytes
  if (t.changeForms.size() != o.changeForms.size()) {
    Mismatch("the change forms (count)");
  }
  const ChangeForm* tp = FindPlayerRecord(t);
  for (size_t i = 0; i < t.changeForms.size(); ++i) {
    const auto& a = t.changeForms[i];
    const auto& b = o.changeForms[i];
    if (&a != tp) {
      if (!(a == b)) {
        Mismatch("change form " + std::to_string(i));
      }
      continue;
    }
    if (!(a.refId == b.refId) || a.flags != b.flags || a.type != b.type ||
        a.version != b.version || a.Compressed() != b.Compressed()) {
      Mismatch("the player record header");
    }
    const Bytes ba = a.Body();
    const Bytes bb = b.Body();
    if (ba.size() != bb.size() ||
        !std::equal(ba.begin() + 27, ba.end(), bb.begin() + 27)) {
      Mismatch("the player record body");
    }
    if (patch.placement) {
      const auto& p = *patch.placement;
      const auto prefix = ReadInitialPrefix(b);
      const std::array<float, 3> rot{ 0.f, 0.f, p.yawRadians };
      if (!(prefix.space == *RefId::FromDefaultForm(p.worldspace))) {
        Mismatch("the player record worldspace");
      }
      for (int k = 0; k < 3; ++k) {
        if (!SameBits(prefix.pos[k], p.pos[k]) ||
            !SameBits(prefix.rot[k], rot[k])) {
          Mismatch("the player record position");
        }
      }
    } else if (!std::equal(ba.begin(), ba.begin() + 27, bb.begin())) {
      Mismatch("the player record position");
    }
  }
}

Bytes WriteEntrySave(std::span<const uint8_t> templateBytes,
                     const EntryPatch& patch)
{
  const SaveFile tmpl = Parse(templateBytes);
  Bytes out = Serialize(ApplyEntryPatch(tmpl, patch));
  SaveFile back;
  try {
    back = Parse(out);
  } catch (const Error& e) {
    throw Error(ErrorCode::VerifyFailed,
                std::string("the written file doesn't parse: ") + e.what());
  }
  VerifyEntryPatch(tmpl, back, patch);
  if (ReadFileLocationTable(out) != ComputeFileLocationTable(back)) {
    throw Error(ErrorCode::VerifyFailed,
                "the file location table doesn't match the sections");
  }
  return out;
}

}
