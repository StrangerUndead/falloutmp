// fallout4-platform/fos: the save editor (F33). Saves here are built byte
// by byte by RawSave below, independently of fos::Serialize. Real saves are
// tested when FMP_FOS_SAMPLES names a folder of .fos files (never
// committed).
#ifdef WITH_FMP_FOS
#  include "Fos.h"
#  include <catch2/catch_all.hpp>
#  include <zlib.h>

#  include <algorithm>
#  include <chrono>
#  include <cmath>
#  include <cstdlib>
#  include <cstring>
#  include <filesystem>
#  include <fstream>
#  include <functional>
#  include <iterator>
#  include <random>

using namespace fmp::fos;

namespace {

// --- A save written by hand ---------------------------------------------

struct RawForm
{
  uint32_t ref = 0;
  uint32_t flags = 0;
  uint8_t type = 0;
  uint8_t version = 69;
  int width = -1; // -1: smallest that fits
  bool compress = false;
  Bytes body;
};

struct RawSave
{
  uint32_t headerVersion = 15;
  uint8_t formVersion = 69;
  std::string gameVersion = "1.11.240.0";
  std::string playerName = "Nate";
  std::vector<std::string> plugins{ "Fallout4.esm" };
  bool lightList = true;
  std::vector<std::string> lightPlugins{ "ccA.esl" };
  Bytes pluginTail; // junk inside the plugin block
  Bytes headerExtra;
  uint32_t shotW = 2, shotH = 1;
  std::vector<std::pair<uint32_t, Bytes>> t1, t2, t3;
  std::vector<RawForm> forms;
  std::vector<uint32_t> formIds{ 0x00012345, 0xFE000800 };
  std::vector<uint32_t> visited{ 0x3C };
  Bytes unknown3{ 0, 0, 0, 0 };
};

struct Out
{
  Bytes b;
  void U8(uint8_t v) { b.push_back(v); }
  void U16(uint16_t v)
  {
    U8(v & 0xFF);
    U8(v >> 8);
  }
  void U32(uint32_t v)
  {
    for (int i = 0; i < 4; ++i) {
      U8((v >> (8 * i)) & 0xFF);
    }
  }
  void F32(float f)
  {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    U32(v);
  }
  void Ref(uint32_t r)
  {
    U8((r >> 16) & 0xFF);
    U8((r >> 8) & 0xFF);
    U8(r & 0xFF);
  }
  void Str(const std::string& s)
  {
    U16(static_cast<uint16_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
  }
  void Raw(const Bytes& x) { b.insert(b.end(), x.begin(), x.end()); }
  void Set32(size_t at, uint32_t v)
  {
    for (int i = 0; i < 4; ++i) {
      b[at + i] = (v >> (8 * i)) & 0xFF;
    }
  }
};

Bytes Zip(const Bytes& in)
{
  uLongf n = compressBound(static_cast<uLong>(in.size()));
  Bytes out(n);
  REQUIRE(compress2(out.data(), &n, in.data(), static_cast<uLong>(in.size()),
                    6) == Z_OK);
  out.resize(n);
  return out;
}

Bytes Build(const RawSave& s)
{
  Out o;
  const char* magic = "FO4_SAVEGAME";
  o.b.insert(o.b.end(), magic, magic + 12);
  const size_t hs = o.b.size();
  o.U32(0);
  const size_t h0 = o.b.size();
  o.U32(s.headerVersion);
  o.U32(7);
  o.Str(s.playerName);
  o.U32(1);
  o.Str("Commonwealth");
  o.Str("0d.0h.19m");
  o.Str("HumanRace");
  o.U16(0);
  o.F32(0);
  o.F32(100);
  o.U32(0x11223344);
  o.U32(0x01D9AABB);
  o.U32(s.shotW);
  o.U32(s.shotH);
  o.Raw(s.headerExtra);
  o.Set32(hs, static_cast<uint32_t>(o.b.size() - h0));
  for (uint32_t i = 0; i < s.shotW * s.shotH * 4; ++i) {
    o.U8(static_cast<uint8_t>(i * 7));
  }
  o.U8(s.formVersion);
  o.Str(s.gameVersion);
  const size_t pis = o.b.size();
  o.U32(0);
  const size_t p0 = o.b.size();
  o.U8(static_cast<uint8_t>(s.plugins.size()));
  for (auto& p : s.plugins) {
    o.Str(p);
  }
  if (s.lightList) {
    o.U16(static_cast<uint16_t>(s.lightPlugins.size()));
    for (auto& p : s.lightPlugins) {
      o.Str(p);
    }
  }
  o.Raw(s.pluginTail);
  o.Set32(pis, static_cast<uint32_t>(o.b.size() - p0));
  const size_t flt = o.b.size();
  for (int i = 0; i < 25; ++i) {
    o.U32(0);
  }
  auto table = [&](const std::vector<std::pair<uint32_t, Bytes>>& t) {
    for (auto& [type, data] : t) {
      o.U32(type);
      o.U32(static_cast<uint32_t>(data.size()));
      o.Raw(data);
    }
  };
  std::array<uint32_t, 25> v{};
  v[2] = static_cast<uint32_t>(o.b.size());
  table(s.t1);
  v[3] = static_cast<uint32_t>(o.b.size());
  table(s.t2);
  v[4] = static_cast<uint32_t>(o.b.size());
  for (auto& f : s.forms) {
    Bytes data = f.compress ? Zip(f.body) : f.body;
    uint32_t len2 = f.compress ? static_cast<uint32_t>(f.body.size()) : 0;
    uint32_t need =
      std::max<uint32_t>(static_cast<uint32_t>(data.size()), len2);
    int w = f.width >= 0 ? f.width : need <= 0xFF ? 0 : need <= 0xFFFF ? 1 : 2;
    o.Ref(f.ref);
    o.U32(f.flags);
    o.U8(static_cast<uint8_t>((w << 6) | f.type));
    o.U8(f.version);
    if (w == 0) {
      o.U8(static_cast<uint8_t>(data.size()));
      o.U8(static_cast<uint8_t>(len2));
    } else if (w == 1) {
      o.U16(static_cast<uint16_t>(data.size()));
      o.U16(static_cast<uint16_t>(len2));
    } else {
      o.U32(static_cast<uint32_t>(data.size()));
      o.U32(len2);
    }
    o.Raw(data);
  }
  v[5] = static_cast<uint32_t>(o.b.size());
  table(s.t3);
  v[0] = static_cast<uint32_t>(o.b.size());
  o.U32(static_cast<uint32_t>(s.formIds.size()));
  for (auto id : s.formIds) {
    o.U32(id);
  }
  o.U32(static_cast<uint32_t>(s.visited.size()));
  for (auto id : s.visited) {
    o.U32(id);
  }
  v[1] = static_cast<uint32_t>(o.b.size());
  o.U32(static_cast<uint32_t>(s.unknown3.size()));
  o.Raw(s.unknown3);
  v[6] = static_cast<uint32_t>(s.t1.size());
  v[7] = static_cast<uint32_t>(s.t2.size());
  v[8] = static_cast<uint32_t>(s.t3.size());
  v[9] = static_cast<uint32_t>(s.forms.size());
  for (int i = 0; i < 25; ++i) {
    o.Set32(flt + 4 * i, v[i]);
  }
  return o.b;
}

Bytes Pseudo(size_t n, uint32_t seed)
{
  std::mt19937 rng(seed);
  Bytes b(n);
  for (auto& x : b) {
    x = static_cast<uint8_t>(rng());
  }
  return b;
}

Bytes PlayerLocationBlock(uint32_t space, float x, float y, float z,
                          uint32_t worldspace = 0x40003C)
{
  Out o;
  o.U32(0xFF001657);
  o.Ref(worldspace);
  o.U32(static_cast<uint32_t>(static_cast<int32_t>(std::floor(x / 4096))));
  o.U32(static_cast<uint32_t>(static_cast<int32_t>(std::floor(y / 4096))));
  o.Ref(space);
  o.F32(x);
  o.F32(y);
  o.F32(z);
  return o.b;
}

Bytes GlobalsBlock(const std::vector<std::pair<uint32_t, float>>& g)
{
  Out o;
  if (g.size() < 64) {
    o.U8(static_cast<uint8_t>(g.size() << 2));
  } else {
    o.U16(static_cast<uint16_t>((g.size() << 2) | 1));
  }
  for (auto& [ref, v] : g) {
    o.Ref(ref);
    o.F32(v);
  }
  return o.b;
}

Bytes WeatherBlock(uint32_t skyMode)
{
  Bytes b = Pseudo(92, 3);
  for (int i = 0; i < 4; ++i) {
    b[58 + i] = (skyMode >> (8 * i)) & 0xFF;
  }
  return b;
}

Bytes PrefixBody(uint32_t space, float x, float y, float z, size_t rest,
                 uint32_t seed)
{
  Out o;
  o.Ref(space);
  o.F32(x);
  o.F32(y);
  o.F32(z);
  o.F32(0.1f);
  o.F32(0);
  o.F32(3.0f);
  o.Raw(Pseudo(rest, seed));
  return o.b;
}

constexpr float kX = -88360.f, kY = 90635.f, kZ = 8955.f;

// Shaped like a real entry template (the user's 1.11.240 save outside
// Vault 111, ref §1.1)
RawSave TemplateSave(bool compressedPlayer = true)
{
  RawSave s;
  s.t1 = {
    { 0, Pseudo(40, 1) },
    { 1, PlayerLocationBlock(0x40003C, kX, kY, kZ) },
    { 2, Pseudo(101, 2) },
    { 3,
      GlobalsBlock({ { 0x400035, 287.f },
                     { 0x400038, 9.5f },
                     { 0x400039, 0.4f },
                     { 0x40003A, 20.f } }) },
    { 6, WeatherBlock(3) },
    { 7, Pseudo(12, 4) },
    { 11, Pseudo(77, 5) },
  };
  s.t2 = { { 100, Pseudo(33, 6) }, { 101, Pseudo(9, 7) } };
  s.t3 = { { 1000, Pseudo(20, 8) }, { 1001, Pseudo(500, 9) } };
  RawForm refr{
    0x401234, 0x2, 0, 69, -1, false, PrefixBody(0x40003C, 1, 2, 3, 10, 10)
  };
  RawForm player{ 0x400014,
                  0xB0000823,
                  1,
                  69,
                  -1,
                  compressedPlayer,
                  PrefixBody(0x40003C, kX, kY, kZ, 400, 11) };
  Bytes createdBody = PrefixBody(0x40003C, 5, 6, 7, 0, 12);
  createdBody.push_back(0);
  Out base;
  base.Ref(0x401111); // not a power armor frame
  createdBody.insert(createdBody.end(), base.b.begin(), base.b.end());
  RawForm created{ 0x800005, 0x2, 0, 69, -1, true, createdBody };
  RawForm indexed{ 0x000001, 0x0, 8, 69, 2, false, Pseudo(5, 13) };
  RawForm big{ 0x402222, 0x0, 9, 69, -1, false, Pseudo(300, 14) };
  s.forms = { refr, player, created, indexed, big };
  return s;
}

Placement Somewhere()
{
  Placement p;
  p.worldspace = kCommonwealth;
  p.pos = { -79800.f, 90500.f, 7800.f };
  p.yawRadians = 3.14159f;
  return p;
}

ErrorCode CodeOf(const std::function<void()>& fn)
{
  try {
    fn();
  } catch (const Error& e) {
    return e.Code();
  }
  FAIL("no fos::Error thrown");
  return ErrorCode::Io;
}

}

// --- Parse and Serialize ----------------------------------------------------

TEST_CASE("fos: a hand-built save parses field by field", "[Fos]")
{
  RawSave raw = TemplateSave();
  raw.headerExtra = { 0xAA, 0xBB };
  const Bytes bytes = Build(raw);
  const SaveFile s = Parse(bytes);
  REQUIRE(s.header.version == 15);
  REQUIRE(s.header.saveNumber == 7);
  REQUIRE(s.header.playerName == "Nate");
  REQUIRE(s.header.location == "Commonwealth");
  REQUIRE(s.header.race == "HumanRace");
  REQUIRE(s.header.extra == Bytes{ 0xAA, 0xBB });
  REQUIRE(s.screenshot.size() == 8);
  REQUIRE(s.formVersion == 69);
  REQUIRE(s.gameVersion == "1.11.240.0");
  REQUIRE(s.plugins == std::vector<std::string>{ "Fallout4.esm" });
  REQUIRE(s.hasLightPluginList);
  REQUIRE(s.lightPlugins == std::vector<std::string>{ "ccA.esl" });
  REQUIRE(s.table1.size() == 7);
  REQUIRE(s.table2.size() == 2);
  REQUIRE(s.table3.size() == 2);
  REQUIRE(s.changeForms.size() == 5);
  REQUIRE(s.changeForms[1].refId.raw == kPlayerRefRaw);
  REQUIRE(s.changeForms[1].Compressed());
  REQUIRE(s.changeForms[3].width == 2);
  REQUIRE(s.changeForms[4].width == 1);
  REQUIRE(s.formIds == raw.formIds);
  REQUIRE(s.visitedWorldspaces == raw.visited);
  REQUIRE(s.unknownTable3 == raw.unknown3);
}

TEST_CASE("fos: Serialize reproduces hand-built saves byte for byte", "[Fos]")
{
  SECTION("version 15 with light plugins")
  {
    const Bytes b = Build(TemplateSave());
    REQUIRE(Serialize(Parse(b)) == b);
  }
  SECTION("version 11 without a light plugin list, launch-era formVersion")
  {
    RawSave raw = TemplateSave(false);
    raw.headerVersion = 11;
    raw.formVersion = 61;
    raw.lightList = false;
    raw.lightPlugins.clear();
    const Bytes b = Build(raw);
    const SaveFile s = Parse(b);
    REQUIRE_FALSE(s.hasLightPluginList);
    REQUIRE(Serialize(s) == b);
  }
  SECTION("empty tables, no forms, empty arrays, no screenshot")
  {
    RawSave raw;
    raw.shotW = raw.shotH = 0;
    raw.formIds.clear();
    raw.visited.clear();
    raw.unknown3.clear();
    const Bytes b = Build(raw);
    REQUIRE(Serialize(Parse(b)) == b);
  }
  SECTION("an empty light plugin list is kept")
  {
    RawSave raw = TemplateSave();
    raw.lightPlugins.clear();
    const Bytes b = Build(raw);
    const SaveFile s = Parse(b);
    REQUIRE(s.hasLightPluginList);
    REQUIRE(Serialize(s) == b);
  }
}

TEST_CASE("fos: the file location table is recomputed exactly", "[Fos]")
{
  const Bytes b = Build(TemplateSave());
  const SaveFile s = Parse(b);
  REQUIRE(ComputeFileLocationTable(s) == ReadFileLocationTable(b));
}

TEST_CASE("fos: damaged files raise the right errors", "[Fos]")
{
  const Bytes good = Build(TemplateSave());

  SECTION("magic")
  {
    Bytes b = good;
    b[0] = 'X';
    REQUIRE(CodeOf([&] { Parse(b); }) == ErrorCode::BadMagic);
  }
  SECTION("versions")
  {
    RawSave raw = TemplateSave();
    raw.headerVersion = 10;
    REQUIRE(CodeOf([&] { Parse(Build(raw)); }) == ErrorCode::Unsupported);
    raw = TemplateSave();
    raw.formVersion = 70;
    REQUIRE(CodeOf([&] { Parse(Build(raw)); }) == ErrorCode::Unsupported);
    raw.formVersion = 59;
    REQUIRE(CodeOf([&] { Parse(Build(raw)); }) == ErrorCode::Unsupported);
  }
  SECTION("every truncation")
  {
    for (size_t n = 0; n < good.size(); n += 13) {
      Bytes b(good.begin(), good.begin() + n);
      auto code = CodeOf([&] { Parse(b); });
      REQUIRE((code == ErrorCode::Truncated || code == ErrorCode::BadLayout ||
               code == ErrorCode::BadMagic));
    }
  }
  SECTION("trailing bytes")
  {
    Bytes b = good;
    b.push_back(0);
    REQUIRE(CodeOf([&] { Parse(b); }) == ErrorCode::BadLayout);
  }
  SECTION("a file location table that disagrees with the sections")
  {
    const auto flt = ReadFileLocationTable(good);
    const size_t fltAt = flt[2] - 100;
    for (int word : { 0, 1, 2, 3, 4, 5 }) {
      Bytes b = good;
      b[fltAt + 4 * word] ^= 0x10;
      REQUIRE(CodeOf([&] { Parse(b); }) == ErrorCode::BadLayout);
    }
  }
  SECTION("an invalid RefID kind")
  {
    RawSave raw = TemplateSave();
    raw.forms[0].ref = 0xC00001;
    REQUIRE(CodeOf([&] { Parse(Build(raw)); }) == ErrorCode::BadLayout);
  }
  SECTION("an invalid length width")
  {
    RawSave raw = TemplateSave();
    raw.forms[0].width = 3;
    // Build writes 4-byte lengths for width 3; the reader must refuse it
    REQUIRE(CodeOf([&] { Parse(Build(raw)); }) == ErrorCode::BadLayout);
  }
  SECTION("a plugin block longer than its lists")
  {
    RawSave raw = TemplateSave();
    raw.pluginTail = { 0x01 };
    REQUIRE(CodeOf([&] { Parse(Build(raw)); }) == ErrorCode::BadLayout);
    raw.lightList = false;
    raw.lightPlugins.clear();
    raw.pluginTail = { 0x05, 0x00, 0x01 }; // a light list that runs short
    auto code = CodeOf([&] { Parse(Build(raw)); });
    REQUIRE((code == ErrorCode::Truncated || code == ErrorCode::BadLayout));
  }
  SECTION("bad zlib data is found when the body is read")
  {
    SaveFile s = Parse(good);
    s.changeForms[1].data[5] ^= 0xFF;
    s.changeForms[1].data[6] ^= 0xFF;
    auto code = CodeOf([&] { s.changeForms[1].Body(); });
    REQUIRE(code == ErrorCode::Inflate);
  }
}

// --- Views ----------------------------------------------------------------

TEST_CASE("fos: RefID kinds and encoding", "[Fos]")
{
  REQUIRE(RefId::FromDefaultForm(0x3C)->raw == 0x40003C);
  REQUIRE_FALSE(RefId::FromDefaultForm(0));
  REQUIRE_FALSE(RefId::FromDefaultForm(0x400000));
  std::vector<uint32_t> ids{ 0x01000800 };
  REQUIRE(RefId{ 0x000001 }.Resolve(ids) == 0x01000800);
  REQUIRE(RefId{ 0x000002 }.Resolve(ids) == 0);
  REQUIRE(RefId{ 0x000000 }.Resolve(ids) == 0);
  REQUIRE(RefId{ 0x40003C }.Resolve(ids) == 0x3C);
  REQUIRE(RefId{ 0x800005 }.Resolve(ids) == 0xFF000005);
  REQUIRE(RefId{ 0xC00001 }.Resolve(ids) == 0);
}

TEST_CASE("fos: Player Location, globals, sky mode, player record", "[Fos]")
{
  SaveFile s = Parse(Build(TemplateSave()));
  auto loc = PlayerLocation::Read(s);
  REQUIRE(loc.nextObjectId == 0xFF001657);
  REQUIRE(loc.worldspace.raw == 0x40003C);
  REQUIRE(loc.worldOrCell.raw == 0x40003C);
  REQUIRE(loc.gridX == -22);
  REQUIRE(loc.gridY == 22);
  REQUIRE(loc.pos[0] == kX);

  REQUIRE(GetGlobal(s, kGlobalGameHour) == 9.5f);
  REQUIRE(GetGlobal(s, kGlobalGameDaysPassed) == 0.4f);
  REQUIRE(GetGlobal(s, kGlobalTimeScale) == 20.f);
  REQUIRE_FALSE(GetGlobal(s, 0x99));
  REQUIRE(SetGlobal(s, kGlobalGameHour, 13.f));
  REQUIRE(GetGlobal(s, kGlobalGameHour) == 13.f);
  REQUIRE_FALSE(SetGlobal(s, 0x99, 1.f));
  REQUIRE(ReadSkyMode(s) == 3u);

  auto player = FindPlayerRecord(s);
  REQUIRE(player);
  REQUIRE(InitialDataType(*player) == 4);
  auto prefix = ReadInitialPrefix(*player);
  REQUIRE(prefix.space.raw == 0x40003C);
  REQUIRE(prefix.pos[1] == kY);
  REQUIRE(prefix.rot[2] == 3.0f);

  loc.pos[2] = 1.f;
  loc.Write(s);
  REQUIRE(PlayerLocation::Read(s).pos[2] == 1.f);
}

TEST_CASE("fos: globals with a two-byte count", "[Fos]")
{
  RawSave raw = TemplateSave();
  std::vector<std::pair<uint32_t, float>> many;
  for (uint32_t i = 0; i < 900; ++i) {
    many.push_back({ 0x401000 + i, static_cast<float>(i) });
  }
  many.push_back({ 0x400038, 7.25f });
  raw.t1[3].second = GlobalsBlock(many);
  SaveFile s = Parse(Build(raw));
  REQUIRE(ReadGlobals(s).size() == 901);
  REQUIRE(GetGlobal(s, kGlobalGameHour) == 7.25f);
  REQUIRE(GetGlobal(s, 0x1000 + 899) == 899.f);
}

TEST_CASE("fos: a globals block whose count lies is refused", "[Fos]")
{
  RawSave raw = TemplateSave();
  raw.t1[3].second.push_back(0);
  SaveFile s = Parse(Build(raw));
  REQUIRE(CodeOf([&] { ReadGlobals(s); }) == ErrorCode::BadLayout);
}

TEST_CASE("fos: initial data types follow the change flags", "[Fos]")
{
  ChangeForm f;
  f.refId = RefId{ 0x401234 };
  REQUIRE(InitialDataType(f) == 0);
  f.flags = kFlagMove;
  REQUIRE(InitialDataType(f) == 4);
  f.flags = kFlagHavokMove;
  REQUIRE(InitialDataType(f) == 4);
  f.flags = kFlagMove | kFlagCellChanged;
  REQUIRE(InitialDataType(f) == 6);
  f.flags = kFlagPromoted;
  REQUIRE(InitialDataType(f) == 6);
  f.refId = RefId{ 0x800001 };
  REQUIRE(InitialDataType(f) == 5);
  REQUIRE(InitialDataSize(4) == 27);
  REQUIRE(InitialDataSize(5) == 31);
  REQUIRE(InitialDataSize(6) == 34);
}

TEST_CASE("fos: SetBody keeps compression and widens the length field",
          "[Fos]")
{
  ChangeForm stored;
  stored.data = Pseudo(200, 1);
  stored.width = 0;
  stored.SetBody(Pseudo(300, 2));
  REQUIRE_FALSE(stored.Compressed());
  REQUIRE(stored.width == 1);
  REQUIRE(stored.Body() == Pseudo(300, 2));

  ChangeForm zipped;
  zipped.length2 = 10;
  zipped.data = Zip(Bytes(10, 1));
  zipped.SetBody(Pseudo(70000, 3));
  REQUIRE(zipped.Compressed());
  REQUIRE(zipped.length2 == 70000);
  REQUIRE(zipped.width == 2);
  REQUIRE(zipped.Body() == Pseudo(70000, 3));

  // The widened form serializes and parses back
  SaveFile s = Parse(Build(TemplateSave()));
  s.changeForms[0] = stored;
  s.changeForms[0].refId = RefId{ 0x401234 };
  const Bytes b = Serialize(s);
  REQUIRE(Parse(b) == s);
}

// --- The entry patch ------------------------------------------------------

namespace {
// Undo the expected edits; what remains must equal the template.
SaveFile Unpatched(SaveFile out, const SaveFile& tmpl)
{
  PlayerLocation::Read(tmpl).Write(out);
  for (auto id : { kGlobalGameHour, kGlobalGameDaysPassed }) {
    SetGlobal(out, id, *GetGlobal(tmpl, id));
  }
  *FindPlayerRecord(out) = *FindPlayerRecord(tmpl);
  return out;
}
}

TEST_CASE("fos: the entry patch writes position and time, nothing else",
          "[FosPatch]")
{
  for (bool compressed : { true, false }) {
    const Bytes tmplBytes = Build(TemplateSave(compressed));
    const SaveFile tmpl = Parse(tmplBytes);
    EntryPatch patch;
    patch.placement = Somewhere();
    patch.gameHour = 13.5f;
    patch.gameDaysPassed = 12.25f;

    const Bytes outBytes = WriteEntrySave(tmplBytes, patch);
    const SaveFile out = Parse(outBytes);

    auto loc = PlayerLocation::Read(out);
    REQUIRE(loc.worldspace.raw == 0x40003C);
    REQUIRE(loc.worldOrCell.raw == 0x40003C);
    REQUIRE(loc.gridX == static_cast<int>(std::floor(-79800.f / 4096)));
    REQUIRE(loc.gridY == 22);
    REQUIRE(loc.pos == std::array<float, 3>{ -79800.f, 90500.f, 7800.f });
    REQUIRE(loc.nextObjectId == 0xFF001657);
    REQUIRE(GetGlobal(out, kGlobalGameHour) == 13.5f);
    REQUIRE(GetGlobal(out, kGlobalGameDaysPassed) == 12.25f);
    REQUIRE(GetGlobal(out, kGlobalTimeScale) == 20.f);
    auto prefix = ReadInitialPrefix(*FindPlayerRecord(out));
    REQUIRE(prefix.pos == loc.pos);
    REQUIRE(prefix.rot == std::array<float, 3>{ 0.f, 0.f, 3.14159f });
    REQUIRE(FindPlayerRecord(out)->Compressed() == compressed);

    // Independently of VerifyEntryPatch: everything else is unchanged
    REQUIRE(Unpatched(out, tmpl) == tmpl);
    const Bytes body = FindPlayerRecord(out)->Body();
    const Bytes before = FindPlayerRecord(tmpl)->Body();
    REQUIRE(Bytes(body.begin() + 27, body.end()) ==
            Bytes(before.begin() + 27, before.end()));

    // Only the offsets after the change forms move, by the growth
    const auto a = ReadFileLocationTable(tmplBytes);
    const auto b = ReadFileLocationTable(outBytes);
    const int64_t growth = static_cast<int64_t>(outBytes.size()) -
      static_cast<int64_t>(tmplBytes.size());
    for (int i : { 2, 3, 4 }) {
      REQUIRE(a[i] == b[i]);
    }
    for (int i : { 0, 1, 5 }) {
      REQUIRE(static_cast<int64_t>(b[i]) - a[i] == growth);
    }
    if (!compressed) {
      REQUIRE(growth == 0);
    }
  }
}

TEST_CASE("fos: a time-only patch leaves the position alone", "[FosPatch]")
{
  const Bytes tmplBytes = Build(TemplateSave());
  EntryPatch patch;
  patch.gameHour = 22.f;
  const SaveFile out = Parse(WriteEntrySave(tmplBytes, patch));
  const SaveFile tmpl = Parse(tmplBytes);
  REQUIRE(PlayerLocation::Read(out) == PlayerLocation::Read(tmpl));
  REQUIRE(*FindPlayerRecord(out) == *FindPlayerRecord(tmpl));
  REQUIRE(GetGlobal(out, kGlobalGameHour) == 22.f);
  REQUIRE(GetGlobal(out, kGlobalGameDaysPassed) == 0.4f);
}

TEST_CASE("fos: patching is deterministic and repeatable", "[FosPatch]")
{
  const Bytes tmplBytes = Build(TemplateSave());
  EntryPatch patch;
  patch.placement = Somewhere();
  REQUIRE(WriteEntrySave(tmplBytes, patch) ==
          WriteEntrySave(tmplBytes, patch));
  // A patched save is itself a valid template
  const Bytes once = WriteEntrySave(tmplBytes, patch);
  patch.placement->pos[0] = 1000.f;
  const SaveFile twice = Parse(WriteEntrySave(once, patch));
  REQUIRE(PlayerLocation::Read(twice).pos[0] == 1000.f);
}

TEST_CASE("fos: bad patch values are refused", "[FosPatch]")
{
  const Bytes t = Build(TemplateSave());
  auto refused = [&](EntryPatch p) {
    return CodeOf([&] { WriteEntrySave(t, p); }) == ErrorCode::PatchRefused;
  };
  EntryPatch p;
  p.placement = Somewhere();
  p.placement->pos[0] = NAN;
  REQUIRE(refused(p));
  p.placement = Somewhere();
  p.placement->pos[1] = 600000.f;
  REQUIRE(refused(p));
  p.placement = Somewhere();
  p.placement->pos[2] = -200000.f;
  REQUIRE(refused(p));
  p.placement = Somewhere();
  p.placement->yawRadians = 180.f; // degrees by mistake
  REQUIRE(refused(p));
  p.placement = Somewhere();
  p.placement->worldspace = 0;
  REQUIRE(refused(p));
  p.placement->worldspace = 0x01000ABC; // another plugin
  REQUIRE(refused(p));
  p = {};
  p.gameHour = 24.f;
  REQUIRE(refused(p));
  p.gameHour = -1.f;
  REQUIRE(refused(p));
  p = {};
  p.gameDaysPassed = -0.5f;
  REQUIRE(refused(p));
  p.gameDaysPassed = INFINITY;
  REQUIRE(refused(p));
}

TEST_CASE("fos: saves that can't be entry templates are refused", "[FosPatch]")
{
  EntryPatch patch;
  patch.placement = Somewhere();
  auto refused = [&](const RawSave& raw) {
    return CodeOf([&] { WriteEntrySave(Build(raw), patch); }) ==
      ErrorCode::NotATemplate;
  };
  RawSave raw = TemplateSave();
  raw.t1[1].second = PlayerLocationBlock(0x4016D8, kX, kY, kZ, 0x40A7F4);
  REQUIRE(refused(raw)); // indoors (Vault 111)
  raw = TemplateSave();
  raw.t1[4].second = WeatherBlock(1);
  REQUIRE(refused(raw)); // indoor sky
  raw = TemplateSave();
  raw.t1[1].second.push_back(0);
  REQUIRE(refused(raw)); // 31-byte Player Location
  raw = TemplateSave();
  raw.t1[3].second = GlobalsBlock({ { 0x400038, 9.f } });
  REQUIRE(refused(raw)); // no GameDaysPassed
  raw = TemplateSave();
  raw.forms.erase(raw.forms.begin() + 1);
  REQUIRE(refused(raw)); // no player record
  raw = TemplateSave();
  raw.forms[1].flags |= kFlagHavokMove;
  REQUIRE(refused(raw));
  raw = TemplateSave();
  raw.forms[1].flags &= ~kFlagMove;
  REQUIRE(refused(raw));
  raw = TemplateSave();
  raw.forms[1].body = PrefixBody(0x40003C, kX + 500, kY, kZ, 400, 11);
  REQUIRE(refused(raw)); // record and Player Location disagree
  raw = TemplateSave();
  raw.forms[1].body.resize(20);
  REQUIRE(refused(raw)); // initial data cut short
}

TEST_CASE("fos: verify catches every unexpected difference", "[FosPatch]")
{
  const SaveFile tmpl = Parse(Build(TemplateSave()));
  EntryPatch patch;
  patch.placement = Somewhere();
  patch.gameHour = 13.f;
  const SaveFile good = ApplyEntryPatch(tmpl, patch);
  REQUIRE_NOTHROW(VerifyEntryPatch(tmpl, good, patch));

  auto failsWith = [&](const std::function<void(SaveFile&)>& tamper) {
    SaveFile bad = good;
    tamper(bad);
    return CodeOf([&] { VerifyEntryPatch(tmpl, bad, patch); }) ==
      ErrorCode::VerifyFailed;
  };
  REQUIRE(failsWith([](SaveFile& s) { s.header.level = 2; }));
  REQUIRE(failsWith([](SaveFile& s) { s.screenshot[0] ^= 1; }));
  REQUIRE(failsWith([](SaveFile& s) { s.lightPlugins.push_back("x.esl"); }));
  REQUIRE(failsWith([](SaveFile& s) { s.table2[0].data[0] ^= 1; }));
  REQUIRE(failsWith([](SaveFile& s) { s.table3[1].data[9] ^= 1; }));
  REQUIRE(failsWith([](SaveFile& s) { s.table1[0].data[0] ^= 1; }));
  REQUIRE(failsWith([](SaveFile& s) { s.table1[4].data[0] ^= 1; }));
  REQUIRE(failsWith([](SaveFile& s) { s.changeForms[0].flags ^= 1; }));
  REQUIRE(failsWith([](SaveFile& s) { s.changeForms[4].data[0] ^= 1; }));
  REQUIRE(failsWith([](SaveFile& s) { s.formIds.push_back(1); }));
  REQUIRE(failsWith([](SaveFile& s) { s.visitedWorldspaces.clear(); }));
  REQUIRE(failsWith([](SaveFile& s) { s.unknownTable3.push_back(1); }));
  REQUIRE(failsWith(
    [](SaveFile& s) { SetGlobal(s, kGlobalGameHour, 14.f); })); // wrong
  REQUIRE(failsWith(
    [](SaveFile& s) { SetGlobal(s, kGlobalTimeScale, 1.f); })); // other
  REQUIRE(failsWith([](SaveFile& s) {
    auto loc = PlayerLocation::Read(s);
    loc.gridX += 1;
    loc.Write(s);
  }));
  REQUIRE(failsWith([](SaveFile& s) {
    auto p = FindPlayerRecord(s);
    Bytes body = p->Body();
    body.back() ^= 1;
    p->SetBody(body);
  }));
  REQUIRE(failsWith([](SaveFile& s) {
    auto p = FindPlayerRecord(s);
    Bytes body = p->Body();
    body[5] ^= 1; // position
    p->SetBody(body);
  }));
  REQUIRE(failsWith([](SaveFile& s) { s.changeForms.pop_back(); }));
}

// --- Fuzzing ----------------------------------------------------------------

namespace {
// One random kind of damage to a save
void Mutate(Bytes& b, std::mt19937& rng, const std::array<uint32_t, 25>& flt)
{
  switch (rng() % 5) {
    case 0: // truncate
      b.resize(rng() % b.size());
      break;
    case 1: // flip a few bytes
      for (int k = 0, n = 1 + rng() % 8; k < n; ++k) {
        b[rng() % b.size()] ^= static_cast<uint8_t>(1 + rng() % 255);
      }
      break;
    case 2: { // a random FLT word
      const size_t at = flt[2] - 100 + 4 * (rng() % 25);
      uint32_t v = rng();
      std::memcpy(&b[at], &v, 4);
      break;
    }
    case 3: { // a random u32 anywhere (lengths, counts)
      const size_t at = rng() % (b.size() - 4);
      uint32_t v = rng() % 2 ? rng() : rng() % 1024;
      std::memcpy(&b[at], &v, 4);
      break;
    }
    default: // damage inside the change forms (zlib data, lengths)
      for (int k = 0; k < 4; ++k) {
        b[flt[4] + rng() % (flt[5] - flt[4])] ^= 0x5A;
      }
      break;
  }
}

// Parse, serialize and patch damaged copies; only fos::Error may escape.
// Returns how many still parsed.
int Fuzz(const Bytes& good, int iterations, uint32_t seed)
{
  const auto flt = ReadFileLocationTable(good);
  std::mt19937 rng(seed);
  EntryPatch patch;
  patch.placement = Somewhere();
  patch.gameHour = 5.f;
  int parsed = 0;
  for (int i = 0; i < iterations; ++i) {
    Bytes b = good;
    Mutate(b, rng, flt);
    try {
      SaveFile s = Parse(b);
      ++parsed;
      Serialize(s);
      ValidateTemplate(s, FalloutMpTemplateRules());
      WriteEntrySave(b, patch);
    } catch (const Error&) {
      // expected
    } catch (const std::exception& e) {
      FAIL("a non-fos exception escaped: " << e.what());
    }
  }
  return parsed;
}

int FuzzIterations(int fallback)
{
  const char* v = std::getenv("FMP_FOS_FUZZ_ITERATIONS");
  return v ? std::max(1, std::atoi(v)) : fallback;
}
}

TEST_CASE("fos: damaged input never escapes as anything but fos::Error",
          "[FosFuzz]")
{
  const int n = FuzzIterations(5000);
  const int parsed = Fuzz(Build(TemplateSave()), n, 20261009);
  INFO(parsed << " of " << n << " damaged files still parsed");
  REQUIRE(parsed < n);
}

// --- Validation, diff -----------------------------------------------------

TEST_CASE("fos: template validation", "[Fos]")
{
  TemplateRules rules;
  rules.lightPlugins = { "CCA.ESL" }; // case-insensitive
  auto errors = [&](const RawSave& raw) {
    std::vector<std::string> out;
    for (auto& f : ValidateTemplate(Parse(Build(raw)), rules)) {
      if (f.severity == Finding::Severity::Error) {
        out.push_back(f.code);
      }
    }
    return out;
  };
  REQUIRE(errors(TemplateSave()).empty());

  RawSave raw = TemplateSave();
  raw.plugins.push_back("DLCRobot.esm");
  REQUIRE(errors(raw) == std::vector<std::string>{ "plugins" });
  raw = TemplateSave();
  raw.lightPlugins.push_back("ccExtra.esl");
  REQUIRE(errors(raw) == std::vector<std::string>{ "light-plugins" });
  raw = TemplateSave();
  raw.formVersion = 68;
  REQUIRE(errors(raw) == std::vector<std::string>{ "form-version" });
  raw = TemplateSave();
  raw.formIds.push_back(0x05000001); // plugin 5 doesn't exist
  REQUIRE(errors(raw) == std::vector<std::string>{ "formid-plugin" });
  raw = TemplateSave();
  raw.forms[3].ref = 0x000009; // past the FormID array
  REQUIRE(errors(raw) == std::vector<std::string>{ "refid-index" });
  raw = TemplateSave();
  {
    Bytes body = PrefixBody(0x40003C, 5, 6, 7, 0, 12);
    body.push_back(0);
    Out base;
    base.Ref(0x400000 | kPowerArmorFrameBase);
    body.insert(body.end(), base.b.begin(), base.b.end());
    raw.forms[2].body = body;
  }
  REQUIRE(errors(raw) == std::vector<std::string>{ "power-armor" });
  raw = TemplateSave();
  raw.t1[4].second = WeatherBlock(1);
  REQUIRE(errors(raw) == std::vector<std::string>{ "entry" });

  const auto real = FalloutMpTemplateRules();
  REQUIRE(real.plugins == std::vector<std::string>{ "Fallout4.esm" });
  REQUIRE(real.lightPlugins.size() == 9);
}

TEST_CASE("fos: diff names the sections that differ", "[Fos]")
{
  const SaveFile a = Parse(Build(TemplateSave()));
  REQUIRE(Diff(a, a).empty());
  EntryPatch patch;
  patch.placement = Somewhere();
  patch.gameHour = 1.f;
  const auto d = Diff(a, ApplyEntryPatch(a, patch));
  REQUIRE(d.size() == 3);
  REQUIRE(d[0].where == "table1[type 1]");
  REQUIRE(d[1].where == "table1[type 3]");
  REQUIRE(d[2].where.rfind("changeForm 400014", 0) == 0);
}

TEST_CASE("fos: header text and screenshot can be normalized", "[Fos]")
{
  SaveFile s = Parse(Build(TemplateSave()));
  SetHeaderText(s, "FalloutMP", 1, "0d.0h.0m.0 days.0 hours.0 minutes");
  ReplaceScreenshot(s, 4, 2, Bytes(32, 0));
  const SaveFile back = Parse(Serialize(s));
  REQUIRE(back.header.playerName == "FalloutMP");
  REQUIRE(back.header.shotWidth == 4);
  REQUIRE(back.screenshot.size() == 32);
  REQUIRE(back == s);
  REQUIRE(CodeOf([&] { ReplaceScreenshot(s, 4, 2, Bytes(31, 0)); }) ==
          ErrorCode::BadLayout);
}

// --- Integrity and templates.json ---------------------------------------

TEST_CASE("fos: SHA-256 test vectors", "[Fos]")
{
  auto hex = [](const std::string& s) {
    return Sha256Hex(
      std::span(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
  };
  REQUIRE(hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  REQUIRE(hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  REQUIRE(hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  REQUIRE(hex(std::string(1000000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  // Every tail length around the block boundary
  for (size_t n = 50; n < 70; ++n) {
    REQUIRE(hex(std::string(n, 'x')).size() == 64);
  }
}

TEST_CASE("fos: templates.json and runtime selection", "[Fos]")
{
  const char* json = R"([
    {"id":"a","file":"a.fos","gameVersion":"1.11.191.0","sex":0},
    {"id":"b","file":"b.fos","gameVersion":"1.11.240.0","sex":0,
     "sha256":"00","size":5,"plugins":["Fallout4.esm"],
     "lightPlugins":["ccA.esl"]},
    {"id":"c","file":"c.fos","gameVersion":"1.11.240.0","sex":1},
    {"id":"d","file":"d.fos","gameVersion":"1.10.163.0","sex":0}
  ])";
  const auto list = ParseTemplates(json);
  REQUIRE(list.size() == 4);
  REQUIRE(list[1].lightPlugins == std::vector<std::string>{ "ccA.esl" });
  REQUIRE(list[1].size == 5);

  REQUIRE(SelectTemplate(list, "1.11.240.0", 0)->id == "b");
  REQUIRE(SelectTemplate(list, "1.11.240.0", 1)->id == "c");
  REQUIRE(SelectTemplate(list, "1.11.250.0")->gameVersion == "1.11.240.0");
  REQUIRE(SelectTemplate(list, "1.11.200.0")->id == "a");
  REQUIRE(SelectTemplate(list, "1.10.984.0")->id == "d");
  REQUIRE(SelectTemplate(list, "1.11.100.0") == nullptr);
  REQUIRE(SelectTemplate(list, "1.12.0.0") == nullptr);
  REQUIRE(SelectTemplate(list, "garbage") == nullptr);
  // No template of that sex: any sex rather than none
  REQUIRE(SelectTemplate(list, "1.11.200.0", 1)->id == "a");

  REQUIRE(CompareVersions("1.11.240.0", "1.11.240") == 0);
  REQUIRE(CompareVersions("1.11.240", "1.11.30") > 0);
  REQUIRE(CompareVersions("1.9", "1.10") < 0);

  REQUIRE(ParseTemplates(R"({"templates":[]})").empty());
  REQUIRE(CodeOf([] { ParseTemplates("{"); }) == ErrorCode::BadLayout);
  REQUIRE(CodeOf([] { ParseTemplates(R"([{"id":"x"}])"); }) ==
          ErrorCode::BadLayout);

  // TemplateToJson writes what ParseTemplates reads
  const auto again = ParseTemplates("[" + TemplateToJson(list[1]) + "]");
  REQUIRE(again[0].id == "b");
  REQUIRE(again[0].lightPlugins == list[1].lightPlugins);
}

// --- Real saves (FMP_FOS_SAMPLES) ---------------------------------------

TEST_CASE("fos: real saves round-trip and patch", "[FosReal]")
{
  const char* dir = std::getenv("FMP_FOS_SAMPLES");
  if (!dir || !std::filesystem::is_directory(dir)) {
    SKIP("set FMP_FOS_SAMPLES to a folder of .fos files");
  }
  int files = 0;
  for (auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
    if (entry.path().extension() != ".fos") {
      continue;
    }
    ++files;
    INFO(entry.path().string());
    std::ifstream f(entry.path(), std::ios::binary);
    const Bytes raw((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
    const SaveFile s = Parse(raw);
    REQUIRE(Serialize(s) == raw);
    REQUIRE(PlayerLocation::Read(s).worldspace.GetKind() !=
            RefId::Kind::Invalid);
    REQUIRE(GetGlobal(s, kGlobalGameHour));
    REQUIRE(FindPlayerRecord(s));

    bool isTemplate = true;
    try {
      CheckEntryTemplate(s);
    } catch (const Error&) {
      isTemplate = false;
    }
    EntryPatch patch;
    patch.placement = Somewhere();
    patch.gameHour = 12.f;
    patch.gameDaysPassed = 3.f;
    if (!isTemplate) {
      REQUIRE(CodeOf([&] { WriteEntrySave(raw, patch); }) ==
              ErrorCode::NotATemplate);
      continue;
    }
    Fuzz(raw, FuzzIterations(5000) / 25, 7);
    for (auto pos : { std::array<float, 3>{ -79800.f, 90500.f, 7800.f },
                      std::array<float, 3>{ 25000.f, -40000.f, 2000.f },
                      std::array<float, 3>{ 0.5f, -0.5f, 0.f } }) {
      patch.placement->pos = pos;
      const auto t0 = std::chrono::steady_clock::now();
      const Bytes out = WriteEntrySave(raw, patch);
      const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
      REQUIRE(ms < 250.0);
      const SaveFile back = Parse(out);
      REQUIRE(PlayerLocation::Read(back).pos == pos);
      REQUIRE(ReadInitialPrefix(*FindPlayerRecord(back)).pos == pos);
      REQUIRE(GetGlobal(back, kGlobalGameHour) == 12.f);
      REQUIRE(Diff(s, back).size() == 3);
    }
  }
  REQUIRE(files > 0);
}

#endif
