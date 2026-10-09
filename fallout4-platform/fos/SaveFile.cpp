// Parse and Serialize: the whole .fos file, byte for byte.
#include "Bytes.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>

namespace fmp::fos {

using detail::Reader;
using detail::Writer;

const char* ToString(ErrorCode code)
{
  switch (code) {
    case ErrorCode::Truncated:
      return "Truncated";
    case ErrorCode::BadMagic:
      return "BadMagic";
    case ErrorCode::Unsupported:
      return "Unsupported";
    case ErrorCode::BadLayout:
      return "BadLayout";
    case ErrorCode::Inflate:
      return "Inflate";
    case ErrorCode::TooLarge:
      return "TooLarge";
    case ErrorCode::NotATemplate:
      return "NotATemplate";
    case ErrorCode::PatchRefused:
      return "PatchRefused";
    case ErrorCode::VerifyFailed:
      return "VerifyFailed";
    case ErrorCode::Io:
      return "Io";
  }
  return "Unknown";
}

namespace {
std::string WithOffset(ErrorCode code, const std::string& message,
                       std::optional<size_t> offset)
{
  std::string s = std::string(ToString(code)) + ": " + message;
  if (offset) {
    s += " (at byte " + std::to_string(*offset) + ")";
  }
  return s;
}
}

Error::Error(ErrorCode code_, const std::string& message,
             std::optional<size_t> offset_)
  : std::runtime_error(WithOffset(code_, message, offset_))
  , code(code_)
  , offset(offset_)
{
}

namespace detail {

Bytes Inflate(std::span<const uint8_t> data, uint32_t size)
{
  if (size > kMaxInflated) {
    throw Error(ErrorCode::TooLarge,
                "change form inflates to " + std::to_string(size) + " bytes");
  }
  Bytes out(size);
  uLongf outLen = size;
  int rc = uncompress(out.data(), &outLen, data.data(),
                      static_cast<uLong>(data.size()));
  if (rc != Z_OK || outLen != size) {
    throw Error(ErrorCode::Inflate,
                "zlib error " + std::to_string(rc) + ", got " +
                  std::to_string(outLen) + " of " + std::to_string(size) +
                  " bytes");
  }
  return out;
}

Bytes Deflate(std::span<const uint8_t> data)
{
  uLongf outLen = compressBound(static_cast<uLong>(data.size()));
  Bytes out(outLen);
  int rc = compress2(out.data(), &outLen, data.data(),
                     static_cast<uLong>(data.size()), Z_DEFAULT_COMPRESSION);
  if (rc != Z_OK) {
    throw Error(ErrorCode::Inflate,
                "zlib compress error " + std::to_string(rc));
  }
  out.resize(outLen);
  return out;
}

}

namespace {
constexpr char kMagic[] = "FO4_SAVEGAME";
constexpr size_t kMagicSize = 12;

void Expect(bool ok, const std::string& message, size_t at)
{
  if (!ok) {
    throw Error(ErrorCode::BadLayout, message, at);
  }
}

std::vector<GlobalDataBlock> ReadTable(Reader& r, uint32_t count)
{
  std::vector<GlobalDataBlock> out;
  out.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    GlobalDataBlock b;
    b.type = r.U32();
    uint32_t len = r.U32();
    b.data = r.Take(len);
    out.push_back(std::move(b));
  }
  return out;
}

ChangeForm ReadChangeForm(Reader& r)
{
  const size_t at = r.AbsPos();
  ChangeForm f;
  f.refId = r.Ref();
  Expect(f.refId.GetKind() != RefId::Kind::Invalid,
         "change form with an invalid RefID kind", at);
  f.flags = r.U32();
  uint8_t typeByte = r.U8();
  f.width = typeByte >> 6;
  f.type = typeByte & 0x3F;
  Expect(f.width != 3, "change form with an invalid length width", at);
  f.version = r.U8();
  uint32_t length1, length2;
  switch (f.width) {
    case 0:
      length1 = r.U8();
      length2 = r.U8();
      break;
    case 1:
      length1 = r.U16();
      length2 = r.U16();
      break;
    default:
      length1 = r.U32();
      length2 = r.U32();
      break;
  }
  if (length2 > detail::kMaxInflated) {
    throw Error(
      ErrorCode::TooLarge,
      "change form inflates to " + std::to_string(length2) + " bytes", at);
  }
  f.length2 = length2;
  f.data = r.Take(length1);
  return f;
}

std::vector<uint32_t> ReadU32Array(Reader& r, const char* what)
{
  const size_t at = r.AbsPos();
  uint32_t n = r.U32();
  if (static_cast<uint64_t>(n) * 4 > r.Remaining()) {
    throw Error(
      ErrorCode::Truncated,
      std::string(what) + " claims " + std::to_string(n) + " entries", at);
  }
  std::vector<uint32_t> out(n);
  for (auto& v : out) {
    v = r.U32();
  }
  return out;
}

size_t HeaderFieldsSize(const Header& h)
{
  return 4 + 4 + 2 + h.playerName.size() + 4 + 2 + h.location.size() + 2 +
    h.playTime.size() + 2 + h.race.size() + 2 + 4 + 4 + 8 + 4 + 4;
}

size_t PluginInfoSize(const SaveFile& s)
{
  size_t n = 1;
  for (auto& p : s.plugins) {
    n += 2 + p.size();
  }
  if (s.hasLightPluginList) {
    n += 2;
    for (auto& p : s.lightPlugins) {
      n += 2 + p.size();
    }
  }
  return n;
}

size_t TableSize(const std::vector<GlobalDataBlock>& t)
{
  size_t n = 0;
  for (auto& b : t) {
    n += 8 + b.data.size();
  }
  return n;
}

uint8_t EffectiveWidth(const ChangeForm& f)
{
  uint32_t need =
    std::max<uint32_t>(static_cast<uint32_t>(f.data.size()), f.length2);
  return std::max(f.width, detail::WidthFor(need));
}

size_t ChangeFormSize(const ChangeForm& f)
{
  return 3 + 4 + 1 + 1 + 2 * detail::LengthFieldSize(EffectiveWidth(f)) +
    f.data.size();
}

uint32_t Checked32(size_t v, const char* what)
{
  if (v > 0xFFFFFFFFull) {
    throw Error(ErrorCode::TooLarge, std::string(what) + " exceeds 4 GB");
  }
  return static_cast<uint32_t>(v);
}

void WriteTable(Writer& w, const std::vector<GlobalDataBlock>& t)
{
  for (auto& b : t) {
    w.U32(b.type);
    w.U32(Checked32(b.data.size(), "global data block"));
    w.Raw(b.data);
  }
}
}

SaveFile Parse(std::span<const uint8_t> bytes)
{
  if (bytes.size() > detail::kMaxFileSize) {
    throw Error(ErrorCode::TooLarge,
                "file is " + std::to_string(bytes.size()) + " bytes");
  }
  Reader r(bytes);
  SaveFile s;

  r.Need(kMagicSize);
  if (std::memcmp(bytes.data(), kMagic, kMagicSize) != 0) {
    throw Error(ErrorCode::BadMagic, "not a Fallout 4 save", 0);
  }
  r.Seek(kMagicSize);

  const uint32_t headerSize = r.U32();
  if (headerSize > detail::kMaxHeaderSize) {
    throw Error(ErrorCode::TooLarge,
                "header size " + std::to_string(headerSize), r.AbsPos() - 4);
  }
  const size_t headerStart = r.Pos();
  Header& h = s.header;
  h.version = r.U32();
  if (h.version < 11 || h.version > 15) {
    throw Error(ErrorCode::Unsupported,
                "header version " + std::to_string(h.version), headerStart);
  }
  h.saveNumber = r.U32();
  h.playerName = r.WStr();
  h.level = r.U32();
  h.location = r.WStr();
  h.playTime = r.WStr();
  h.race = r.WStr();
  h.sex = r.U16();
  h.currentXp = r.F32();
  h.nextLevelXp = r.F32();
  h.fileTime = r.U64();
  h.shotWidth = r.U32();
  h.shotHeight = r.U32();
  const size_t consumed = r.Pos() - headerStart;
  Expect(consumed <= headerSize, "header fields are longer than headerSize",
         headerStart);
  h.extra = r.Take(headerSize - consumed);

  if (h.shotWidth > detail::kMaxShotSide ||
      h.shotHeight > detail::kMaxShotSide) {
    throw Error(ErrorCode::TooLarge,
                "screenshot " + std::to_string(h.shotWidth) + "x" +
                  std::to_string(h.shotHeight),
                r.AbsPos());
  }
  s.screenshot = r.Take(static_cast<size_t>(h.shotWidth) * h.shotHeight * 4);

  const size_t fvAt = r.AbsPos();
  s.formVersion = r.U8();
  if (s.formVersion < 60 || s.formVersion > 69) {
    throw Error(ErrorCode::Unsupported,
                "formVersion " + std::to_string(s.formVersion), fvAt);
  }
  s.gameVersion = r.WStr();
  const uint32_t pluginInfoSize = r.U32();
  {
    const size_t at = r.AbsPos();
    Reader p(r.View(pluginInfoSize), at);
    uint8_t n = p.U8();
    for (int i = 0; i < n; ++i) {
      s.plugins.push_back(p.WStr());
    }
    if (!p.AtEnd()) {
      s.hasLightPluginList = true;
      uint16_t m = p.U16();
      Expect(m <= detail::kMaxLightPlugins, "too many light plugins", at);
      for (int i = 0; i < m; ++i) {
        s.lightPlugins.push_back(p.WStr());
      }
    }
    Expect(p.AtEnd(), "plugin block longer than its plugin lists", at);
  }

  std::array<uint32_t, 25> flt;
  const size_t fltAt = r.AbsPos();
  for (auto& v : flt) {
    v = r.U32();
  }
  std::copy(flt.begin() + 10, flt.end(), s.fltUnused.begin());
  for (int i = 6; i <= 8; ++i) {
    Expect(flt[i] <= detail::kMaxBlocksPerTable,
           "global data table " + std::to_string(i - 5) + " count " +
             std::to_string(flt[i]),
           fltAt);
  }
  Expect(flt[9] <= detail::kMaxChangeForms &&
           static_cast<uint64_t>(flt[9]) * 11 <= r.Remaining(),
         "change form count " + std::to_string(flt[9]), fltAt);

  auto at = [&](uint32_t expected, const char* what) {
    Expect(r.AbsPos() == expected,
           std::string(what) + " is at " + std::to_string(r.AbsPos()) +
             ", the file location table says " + std::to_string(expected),
           r.AbsPos());
  };

  at(flt[2], "global data table 1");
  s.table1 = ReadTable(r, flt[6]);
  at(flt[3], "global data table 2");
  s.table2 = ReadTable(r, flt[7]);
  at(flt[4], "the change forms");
  s.changeForms.reserve(flt[9]);
  for (uint32_t i = 0; i < flt[9]; ++i) {
    s.changeForms.push_back(ReadChangeForm(r));
  }
  at(flt[5], "global data table 3");
  s.table3 = ReadTable(r, flt[8]);
  at(flt[0], "the FormID array");
  s.formIds = ReadU32Array(r, "the FormID array");
  s.visitedWorldspaces = ReadU32Array(r, "the visited worldspaces");
  at(flt[1], "unknown table 3");
  const uint32_t unknownSize = r.U32();
  s.unknownTable3 = r.Take(unknownSize);
  Expect(r.AtEnd(),
         std::to_string(r.Remaining()) + " bytes after the last section",
         r.AbsPos());
  return s;
}

std::array<uint32_t, 25> ComputeFileLocationTable(const SaveFile& s)
{
  std::array<uint32_t, 25> flt{};
  size_t pos = kMagicSize + 4 + HeaderFieldsSize(s.header) +
    s.header.extra.size() + s.screenshot.size() + 1 + 2 +
    s.gameVersion.size() + 4 + PluginInfoSize(s) + 100;
  flt[2] = Checked32(pos, "file");
  pos += TableSize(s.table1);
  flt[3] = Checked32(pos, "file");
  pos += TableSize(s.table2);
  flt[4] = Checked32(pos, "file");
  for (auto& f : s.changeForms) {
    pos += ChangeFormSize(f);
  }
  flt[5] = Checked32(pos, "file");
  pos += TableSize(s.table3);
  flt[0] = Checked32(pos, "file");
  pos += 4 + 4 * s.formIds.size() + 4 + 4 * s.visitedWorldspaces.size();
  flt[1] = Checked32(pos, "file");
  flt[6] = static_cast<uint32_t>(s.table1.size());
  flt[7] = static_cast<uint32_t>(s.table2.size());
  flt[8] = static_cast<uint32_t>(s.table3.size());
  flt[9] = static_cast<uint32_t>(s.changeForms.size());
  std::copy(s.fltUnused.begin(), s.fltUnused.end(), flt.begin() + 10);
  return flt;
}

std::array<uint32_t, 25> ReadFileLocationTable(std::span<const uint8_t> bytes)
{
  Reader r(bytes);
  r.Seek(kMagicSize);
  const uint32_t headerSize = r.U32();
  const size_t headerStart = r.Pos();
  r.U32(); // version
  r.U32(); // save number
  r.WStr();
  r.U32(); // level
  r.WStr();
  r.WStr();
  r.WStr();
  r.U16();
  r.U64(); // xp, next xp
  r.U64(); // FILETIME
  const uint32_t w = r.U32();
  const uint32_t h = r.U32();
  r.Seek(headerStart + headerSize);
  r.Seek(r.Pos() + static_cast<size_t>(w) * h * 4 + 1);
  uint16_t gv = r.U16();
  r.Seek(r.Pos() + gv);
  uint32_t pis = r.U32();
  r.Seek(r.Pos() + pis);
  std::array<uint32_t, 25> flt;
  for (auto& v : flt) {
    v = r.U32();
  }
  return flt;
}

Bytes Serialize(const SaveFile& s)
{
  const Header& h = s.header;
  if (s.screenshot.size() !=
      static_cast<size_t>(h.shotWidth) * h.shotHeight * 4) {
    throw Error(ErrorCode::BadLayout,
                "screenshot size doesn't match its width and height");
  }
  if (s.plugins.size() > 255) {
    throw Error(ErrorCode::TooLarge, "more than 255 plugins");
  }
  if (s.lightPlugins.size() > detail::kMaxLightPlugins ||
      (!s.hasLightPluginList && !s.lightPlugins.empty())) {
    throw Error(ErrorCode::BadLayout, "light plugins without a light list");
  }

  Writer w;
  w.Raw(kMagic, kMagicSize);
  const size_t headerSizeAt = w.Size();
  w.U32(0);
  const size_t headerStart = w.Size();
  w.U32(h.version);
  w.U32(h.saveNumber);
  w.WStr(h.playerName);
  w.U32(h.level);
  w.WStr(h.location);
  w.WStr(h.playTime);
  w.WStr(h.race);
  w.U16(h.sex);
  w.F32(h.currentXp);
  w.F32(h.nextLevelXp);
  w.U64(h.fileTime);
  w.U32(h.shotWidth);
  w.U32(h.shotHeight);
  w.Raw(h.extra);
  w.PatchU32(headerSizeAt, Checked32(w.Size() - headerStart, "header"));
  w.Raw(s.screenshot);

  w.U8(s.formVersion);
  w.WStr(s.gameVersion);
  const size_t pisAt = w.Size();
  w.U32(0);
  const size_t pisStart = w.Size();
  w.U8(static_cast<uint8_t>(s.plugins.size()));
  for (auto& p : s.plugins) {
    w.WStr(p);
  }
  if (s.hasLightPluginList) {
    w.U16(static_cast<uint16_t>(s.lightPlugins.size()));
    for (auto& p : s.lightPlugins) {
      w.WStr(p);
    }
  }
  w.PatchU32(pisAt, Checked32(w.Size() - pisStart, "plugin block"));

  const auto flt = ComputeFileLocationTable(s);
  for (auto v : flt) {
    w.U32(v);
  }
  WriteTable(w, s.table1);
  WriteTable(w, s.table2);
  for (auto& f : s.changeForms) {
    w.Ref(f.refId);
    w.U32(f.flags);
    const uint8_t width = EffectiveWidth(f);
    w.U8(static_cast<uint8_t>((width << 6) | (f.type & 0x3F)));
    w.U8(f.version);
    const auto length1 = static_cast<uint32_t>(f.data.size());
    switch (width) {
      case 0:
        w.U8(static_cast<uint8_t>(length1));
        w.U8(static_cast<uint8_t>(f.length2));
        break;
      case 1:
        w.U16(static_cast<uint16_t>(length1));
        w.U16(static_cast<uint16_t>(f.length2));
        break;
      default:
        w.U32(length1);
        w.U32(f.length2);
        break;
    }
    w.Raw(f.data);
  }
  WriteTable(w, s.table3);
  w.U32(static_cast<uint32_t>(s.formIds.size()));
  for (auto v : s.formIds) {
    w.U32(v);
  }
  w.U32(static_cast<uint32_t>(s.visitedWorldspaces.size()));
  for (auto v : s.visitedWorldspaces) {
    w.U32(v);
  }
  w.U32(Checked32(s.unknownTable3.size(), "unknown table 3"));
  w.Raw(s.unknownTable3);

  // The table was computed from the content; check it against where the
  // sections actually landed.
  if (w.Size() != flt[1] + 4 + s.unknownTable3.size()) {
    throw Error(ErrorCode::VerifyFailed,
                "file location table doesn't match the written sections");
  }
  return std::move(w.Out());
}

}
