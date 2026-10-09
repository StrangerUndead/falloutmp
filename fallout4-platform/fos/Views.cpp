// Views: the interpreted parts of a save (design §3), and change form
// bodies.
#include "Bytes.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace fmp::fos {

using detail::Reader;
using detail::Writer;

std::optional<RefId> RefId::FromDefaultForm(uint32_t formId)
{
  if (formId == 0 || formId > 0x3FFFFF) {
    return std::nullopt;
  }
  return RefId{ 0x400000 | formId };
}

uint32_t RefId::Resolve(const std::vector<uint32_t>& formIds) const
{
  switch (GetKind()) {
    case Kind::Index:
      return Value() == 0 || Value() > formIds.size() ? 0
                                                      : formIds[Value() - 1];
    case Kind::Default:
      return Value();
    case Kind::Created:
      return 0xFF000000 | Value();
    default:
      return 0;
  }
}

Bytes ChangeForm::Body() const
{
  if (!Compressed()) {
    return data;
  }
  return detail::Inflate(data, length2);
}

void ChangeForm::SetBody(const Bytes& body)
{
  if (body.size() > detail::kMaxInflated) {
    throw Error(ErrorCode::TooLarge, "change form body too large");
  }
  if (Compressed()) {
    data = detail::Deflate(body);
    length2 = static_cast<uint32_t>(body.size());
  } else {
    data = body;
  }
  // Serialize widens further if needed; keep the stored width in step
  const uint32_t need =
    std::max<uint32_t>(static_cast<uint32_t>(data.size()), length2);
  width = std::max(width, detail::WidthFor(need));
}

namespace {
const GlobalDataBlock* FindBlock(const std::vector<GlobalDataBlock>& t,
                                 uint32_t type)
{
  const GlobalDataBlock* found = nullptr;
  for (auto& b : t) {
    if (b.type == type) {
      if (found) {
        throw Error(ErrorCode::BadLayout,
                    "two global data blocks of type " + std::to_string(type));
      }
      found = &b;
    }
  }
  return found;
}

GlobalDataBlock* FindBlock(std::vector<GlobalDataBlock>& t, uint32_t type)
{
  return const_cast<GlobalDataBlock*>(
    FindBlock(static_cast<const std::vector<GlobalDataBlock>&>(t), type));
}

void PutRef(uint8_t* p, RefId r)
{
  p[0] = static_cast<uint8_t>(r.raw >> 16);
  p[1] = static_cast<uint8_t>(r.raw >> 8);
  p[2] = static_cast<uint8_t>(r.raw);
}

// Floats are stored little-endian, as on every host the game runs on
void PutF32(uint8_t* p, float f)
{
  std::memcpy(p, &f, 4);
}

// The Global Variables block: where the entries start and how many.
struct GlobalsLayout
{
  size_t start = 0;
  size_t count = 0;
};

GlobalsLayout GlobalsLayoutOf(const Bytes& d)
{
  if (d.empty()) {
    throw Error(ErrorCode::BadLayout, "empty Global Variables block");
  }
  const uint8_t code = d[0] & 3;
  auto fits = [&](size_t vsSize, size_t count) {
    return d.size() >= vsSize && d.size() - vsSize == count * 7;
  };
  if (code == 0 && fits(1, d[0] >> 2)) {
    return { 1, static_cast<size_t>(d[0] >> 2) };
  }
  if (code == 1 && d.size() >= 2) {
    size_t n = (d[0] | (d[1] << 8)) >> 2;
    if (fits(2, n)) {
      return { 2, n };
    }
  }
  if (code == 2) {
    // Sources disagree on 3 or 4 bytes: take the one the size confirms
    if (d.size() >= 3) {
      size_t n3 = (d[0] | (d[1] << 8) | (d[2] << 16)) >> 2;
      if (fits(3, n3)) {
        return { 3, n3 };
      }
    }
    if (d.size() >= 4) {
      size_t n4 = (static_cast<size_t>(d[0]) | (d[1] << 8) | (d[2] << 16) |
                   (static_cast<size_t>(d[3]) << 24)) >>
        2;
      if (fits(4, n4)) {
        return { 4, n4 };
      }
    }
  }
  throw Error(ErrorCode::BadLayout,
              "Global Variables count doesn't match the block size");
}
}

PlayerLocation PlayerLocation::Read(const SaveFile& save)
{
  auto b = FindBlock(save.table1, 1);
  if (!b) {
    throw Error(ErrorCode::BadLayout, "no Player Location block");
  }
  if (b->data.size() != kSize) {
    throw Error(ErrorCode::BadLayout,
                "Player Location block is " + std::to_string(b->data.size()) +
                  " bytes, not 30");
  }
  Reader r(b->data);
  PlayerLocation p;
  p.nextObjectId = r.U32();
  p.worldspace = r.Ref();
  p.gridX = r.I32();
  p.gridY = r.I32();
  p.worldOrCell = r.Ref();
  for (auto& v : p.pos) {
    v = r.F32();
  }
  return p;
}

void PlayerLocation::Write(SaveFile& save) const
{
  auto b = FindBlock(save.table1, 1);
  if (!b || b->data.size() != kSize) {
    throw Error(ErrorCode::BadLayout, "no 30-byte Player Location block");
  }
  Writer w;
  w.U32(nextObjectId);
  w.Ref(worldspace);
  w.I32(gridX);
  w.I32(gridY);
  w.Ref(worldOrCell);
  for (auto v : pos) {
    w.F32(v);
  }
  b->data = std::move(w.Out());
}

std::vector<GlobalVariable> ReadGlobals(const SaveFile& save)
{
  auto b = FindBlock(save.table1, 3);
  if (!b) {
    return {};
  }
  auto layout = GlobalsLayoutOf(b->data);
  Reader r(b->data);
  r.Seek(layout.start);
  std::vector<GlobalVariable> out(layout.count);
  for (auto& g : out) {
    g.ref = r.Ref();
    g.value = r.F32();
  }
  return out;
}

std::optional<float> GetGlobal(const SaveFile& save, uint32_t formId)
{
  for (auto& g : ReadGlobals(save)) {
    if (g.ref.Resolve(save.formIds) == formId) {
      return g.value;
    }
  }
  return std::nullopt;
}

bool SetGlobal(SaveFile& save, uint32_t formId, float value)
{
  auto b = FindBlock(save.table1, 3);
  if (!b) {
    return false;
  }
  auto layout = GlobalsLayoutOf(b->data);
  for (size_t i = 0; i < layout.count; ++i) {
    uint8_t* p = b->data.data() + layout.start + i * 7;
    RefId ref{ (static_cast<uint32_t>(p[0]) << 16) |
               (static_cast<uint32_t>(p[1]) << 8) | p[2] };
    if (ref.Resolve(save.formIds) == formId) {
      PutF32(p + 3, value);
      return true;
    }
  }
  return false;
}

std::optional<uint32_t> ReadSkyMode(const SaveFile& save)
{
  auto b = FindBlock(save.table1, 6);
  if (!b || b->data.size() < 62) {
    return std::nullopt;
  }
  Reader r(b->data);
  r.Seek(58);
  return r.U32();
}

int InitialDataType(const ChangeForm& form)
{
  if (form.refId.GetKind() == RefId::Kind::Created) {
    return 5;
  }
  if (form.flags & (kFlagPromoted | kFlagCellChanged)) {
    return 6;
  }
  if (form.flags & (kFlagMove | kFlagHavokMove)) {
    return 4;
  }
  return 0;
}

size_t InitialDataSize(int initialType)
{
  switch (initialType) {
    case 4:
      return 27;
    case 5:
      return 31;
    case 6:
      return 34;
    default:
      return 0;
  }
}

const ChangeForm* FindPlayerRecord(const SaveFile& save)
{
  for (auto& f : save.changeForms) {
    if (f.refId.raw == kPlayerRefRaw && f.type == kChangeFormAchr) {
      return &f;
    }
  }
  return nullptr;
}

ChangeForm* FindPlayerRecord(SaveFile& save)
{
  return const_cast<ChangeForm*>(
    FindPlayerRecord(static_cast<const SaveFile&>(save)));
}

InitialPrefix ReadInitialPrefix(const ChangeForm& form)
{
  const int type = InitialDataType(form);
  if (type == 0) {
    throw Error(ErrorCode::BadLayout, "reference has no initial data");
  }
  Bytes body = form.Body();
  if (body.size() < InitialDataSize(type)) {
    throw Error(ErrorCode::Truncated, "initial data cut short");
  }
  Reader r(body);
  InitialPrefix p;
  p.space = r.Ref();
  for (auto& v : p.pos) {
    v = r.F32();
  }
  for (auto& v : p.rot) {
    v = r.F32();
  }
  return p;
}

void SetHeaderText(SaveFile& save, const std::string& playerName,
                   uint32_t level, const std::string& playTime)
{
  save.header.playerName = playerName;
  save.header.level = level;
  save.header.playTime = playTime;
}

void ReplaceScreenshot(SaveFile& save, uint32_t width, uint32_t height,
                       Bytes rgba)
{
  if (width > detail::kMaxShotSide || height > detail::kMaxShotSide) {
    throw Error(ErrorCode::TooLarge, "screenshot too large");
  }
  if (rgba.size() != static_cast<size_t>(width) * height * 4) {
    throw Error(ErrorCode::BadLayout,
                "screenshot data doesn't match its size");
  }
  save.header.shotWidth = width;
  save.header.shotHeight = height;
  save.screenshot = std::move(rgba);
}

namespace detail {
// Used by Patch.cpp
void WritePrefix(Bytes& body, RefId space, const std::array<float, 3>& pos,
                 const std::array<float, 3>& rot)
{
  if (body.size() < 27) {
    throw Error(ErrorCode::Truncated, "initial data cut short");
  }
  PutRef(body.data(), space);
  for (int i = 0; i < 3; ++i) {
    PutF32(body.data() + 3 + 4 * i, pos[i]);
    PutF32(body.data() + 15 + 4 * i, rot[i]);
  }
}
}

}
