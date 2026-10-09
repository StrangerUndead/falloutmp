// fmp_savetool: inspect, validate, patch and compare Fallout 4 saves with
// the fos library (docs/falloutmp/features/F33-save-editor.md §9).
#include "Fos.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace fmp::fos;
using nlohmann::json;

namespace {

constexpr int kOk = 0;
constexpr int kFailed = 1;
constexpr int kUsage = 2;

const char* kUsageText =
  R"(fmp_savetool: Fallout 4 save files for FalloutMP's entrance

  inspect <save> [--json]
  validate <save> [--rules rules.json]
  roundtrip <save>
  patch <in> <out> (--ticket ticket.json | --worldspace 0x3c --pos X Y Z --yaw DEG)
                   [--hour H] [--days D]
  diff <a> <b> [--json]
  normalize <in> <out> [--name FalloutMP] [--level 1] [--screenshot image.ppm | --black]
  sha256 <file>
  manifest-entry <save> --id ID [--file NAME] [--notes TEXT] [--sex 0|1]

Exit codes: 0 ok, 1 the check failed or an error occurred, 2 bad usage.
)";

Bytes ReadFile(const std::string& path)
{
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    throw Error(ErrorCode::Io, "can't open " + path);
  }
  return Bytes((std::istreambuf_iterator<char>(f)),
               std::istreambuf_iterator<char>());
}

void WriteFile(const std::string& path, const Bytes& data)
{
  const std::string tmp = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
      throw Error(ErrorCode::Io, "can't write " + tmp);
    }
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
    if (!f) {
      throw Error(ErrorCode::Io, "write failed: " + tmp);
    }
  }
  std::remove(path.c_str());
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    throw Error(ErrorCode::Io, "can't rename " + tmp + " to " + path);
  }
}

std::string Hex(uint32_t v, int digits = 8)
{
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%0*X", digits, v);
  return buf;
}

std::string RefText(RefId r, const SaveFile& s)
{
  switch (r.GetKind()) {
    case RefId::Kind::Index:
      return r.IsNull() ? "none" : Hex(r.Resolve(s.formIds));
    case RefId::Kind::Default:
      return Hex(r.Value());
    case RefId::Kind::Created:
      return "FF" + Hex(r.Value(), 6);
    default:
      return "invalid";
  }
}

// Options after the positional arguments: --key value...
struct Args
{
  std::vector<std::string> positional;
  std::map<std::string, std::vector<std::string>> options;

  bool Has(const std::string& k) const { return options.count(k) > 0; }
  std::string Get(const std::string& k, std::string def = "") const
  {
    auto it = options.find(k);
    return it == options.end() || it->second.empty() ? def : it->second[0];
  }
};

Args ParseArgs(int argc, char** argv, int first,
               const std::map<std::string, int>& arity)
{
  Args a;
  for (int i = first; i < argc; ++i) {
    std::string s = argv[i];
    if (s.rfind("--", 0) == 0) {
      auto it = arity.find(s);
      if (it == arity.end()) {
        throw std::invalid_argument("unknown option " + s);
      }
      auto& vals = a.options[s];
      for (int k = 0; k < it->second; ++k) {
        if (++i >= argc) {
          throw std::invalid_argument(
            s + " needs " + std::to_string(it->second) + " value(s)");
        }
        vals.push_back(argv[i]);
      }
    } else {
      a.positional.push_back(s);
    }
  }
  return a;
}

float ToFloat(const std::string& s)
{
  size_t used = 0;
  float v = std::stof(s, &used);
  if (used != s.size()) {
    throw std::invalid_argument("not a number: " + s);
  }
  return v;
}

uint32_t ToU32(const std::string& s)
{
  size_t used = 0;
  unsigned long v = std::stoul(s, &used, 0);
  if (used != s.size() || v > 0xFFFFFFFFul) {
    throw std::invalid_argument("not a number: " + s);
  }
  return static_cast<uint32_t>(v);
}

json Inspect(const SaveFile& s, size_t fileSize)
{
  json j;
  const auto& h = s.header;
  j["file"] = { { "size", fileSize } };
  j["header"] = { { "version", h.version },
                  { "saveNumber", h.saveNumber },
                  { "playerName", h.playerName },
                  { "level", h.level },
                  { "location", h.location },
                  { "playTime", h.playTime },
                  { "race", h.race },
                  { "sex", h.sex },
                  { "screenshot",
                    std::to_string(h.shotWidth) + "x" +
                      std::to_string(h.shotHeight) } };
  j["formVersion"] = s.formVersion;
  j["gameVersion"] = s.gameVersion;
  j["plugins"] = s.plugins;
  j["lightPlugins"] = s.lightPlugins;
  auto types = [](const std::vector<GlobalDataBlock>& t) {
    json a = json::array();
    for (auto& b : t) {
      a.push_back({ { "type", b.type }, { "size", b.data.size() } });
    }
    return a;
  };
  j["table1"] = types(s.table1);
  j["table2"] = types(s.table2);
  j["table3"] = types(s.table3);
  j["formIds"] = s.formIds.size();
  j["visitedWorldspaces"] = s.visitedWorldspaces.size();

  try {
    auto loc = PlayerLocation::Read(s);
    j["playerLocation"] = { { "worldspace", RefText(loc.worldspace, s) },
                            { "worldOrCell", RefText(loc.worldOrCell, s) },
                            { "grid", { loc.gridX, loc.gridY } },
                            { "pos", loc.pos },
                            { "nextObjectId", Hex(loc.nextObjectId) } };
  } catch (const Error& e) {
    j["playerLocation"] = { { "error", e.what() } };
  }
  json time;
  for (auto [name, id] :
       { std::pair{ "gameHour", kGlobalGameHour },
         std::pair{ "gameDaysPassed", kGlobalGameDaysPassed },
         std::pair{ "timeScale", kGlobalTimeScale },
         std::pair{ "gameYear", kGlobalGameYear },
         std::pair{ "gameMonth", kGlobalGameMonth },
         std::pair{ "gameDay", kGlobalGameDay } }) {
    try {
      if (auto v = GetGlobal(s, id)) {
        time[name] = *v;
      }
    } catch (const Error& e) {
      time["error"] = e.what();
    }
  }
  j["time"] = time;
  if (auto sky = ReadSkyMode(s)) {
    j["skyMode"] = *sky;
  }
  if (auto p = FindPlayerRecord(s)) {
    json pr = { { "flags", Hex(p->flags) },
                { "initialDataType", InitialDataType(*p) },
                { "compressed", p->Compressed() } };
    try {
      auto prefix = ReadInitialPrefix(*p);
      pr["space"] = RefText(prefix.space, s);
      pr["pos"] = prefix.pos;
      pr["rot"] = prefix.rot;
    } catch (const Error& e) {
      pr["error"] = e.what();
    }
    j["playerRecord"] = pr;
  }
  std::map<int, int> hist;
  size_t stored = 0;
  for (auto& f : s.changeForms) {
    ++hist[f.type];
    stored += f.Compressed() ? 0 : 1;
  }
  json h2;
  for (auto [t, n] : hist) {
    h2[std::to_string(t)] = n;
  }
  j["changeForms"] = { { "count", s.changeForms.size() },
                       { "stored", stored },
                       { "byType", h2 } };
  return j;
}

void PrintInspect(const json& j)
{
  auto& h = j["header"];
  std::cout << "Header      version " << h["version"] << ", save #"
            << h["saveNumber"] << ", " << h["playerName"].get<std::string>()
            << ", level " << h["level"] << ", "
            << h["location"].get<std::string>() << ", "
            << h["playTime"].get<std::string>() << ", screenshot "
            << h["screenshot"].get<std::string>() << "\n";
  std::cout << "Game        " << j["gameVersion"].get<std::string>()
            << ", formVersion " << j["formVersion"] << ", "
            << j["file"]["size"] << " bytes\n";
  std::cout << "Plugins     ";
  for (auto& p : j["plugins"]) {
    std::cout << p.get<std::string>() << " ";
  }
  std::cout << "\nLight       ";
  for (auto& p : j["lightPlugins"]) {
    std::cout << p.get<std::string>() << " ";
  }
  std::cout << "\nTables      " << j["table1"].size() << " / "
            << j["table2"].size() << " / " << j["table3"].size() << " blocks, "
            << j["changeForms"]["count"] << " change forms ("
            << j["changeForms"]["stored"] << " stored), " << j["formIds"]
            << " FormIDs\n";
  std::cout << "Location    " << j["playerLocation"].dump() << "\n";
  std::cout << "Time        " << j["time"].dump() << "\n";
  if (j.contains("skyMode")) {
    std::cout << "Sky mode    " << j["skyMode"]
              << (j["skyMode"] == 3 ? " (outdoors)" : " (indoors)") << "\n";
  }
  if (j.contains("playerRecord")) {
    std::cout << "Player      " << j["playerRecord"].dump() << "\n";
  }
}

EntryPatch PatchFromArgs(const Args& a)
{
  EntryPatch p;
  if (a.Has("--ticket")) {
    const Bytes raw = ReadFile(a.Get("--ticket"));
    json t = json::parse(std::string(raw.begin(), raw.end()));
    if (t.contains("target")) {
      auto& target = t["target"];
      std::string desc = target.value("worldOrCell", "3c:Fallout4.esm");
      auto colon = desc.find(':');
      std::string file =
        colon == std::string::npos ? "Fallout4.esm" : desc.substr(colon + 1);
      if (file != "Fallout4.esm") {
        throw Error(ErrorCode::PatchRefused,
                    "the target is in " + file + ", not Fallout4.esm");
      }
      Placement pl;
      pl.worldspace =
        static_cast<uint32_t>(std::stoul(desc.substr(0, colon), nullptr, 16));
      auto pos = target.at("pos");
      for (int i = 0; i < 3; ++i) {
        pl.pos[i] = pos.at(i).get<float>();
      }
      pl.yawRadians = target.value("angleZ", 0.f) * 3.14159265f / 180.f;
      p.placement = pl;
    }
    if (t.contains("world")) {
      auto& w = t["world"];
      if (w.contains("gameHour")) {
        p.gameHour = w["gameHour"].get<float>();
      }
      if (w.contains("gameDaysPassed")) {
        p.gameDaysPassed = w["gameDaysPassed"].get<float>();
      }
    }
  }
  if (a.Has("--pos")) {
    Placement pl;
    pl.worldspace = ToU32(a.Get("--worldspace", "0x3c"));
    auto& v = a.options.at("--pos");
    for (int i = 0; i < 3; ++i) {
      pl.pos[i] = ToFloat(v[i]);
    }
    pl.yawRadians = ToFloat(a.Get("--yaw", "0")) * 3.14159265f / 180.f;
    p.placement = pl;
  }
  if (a.Has("--hour")) {
    p.gameHour = ToFloat(a.Get("--hour"));
  }
  if (a.Has("--days")) {
    p.gameDaysPassed = ToFloat(a.Get("--days"));
  }
  return p;
}

TemplateRules RulesFromFile(const std::string& path)
{
  const Bytes raw = ReadFile(path);
  json j = json::parse(std::string(raw.begin(), raw.end()));
  TemplateRules r = FalloutMpTemplateRules();
  r.headerVersion = j.value("headerVersion", r.headerVersion);
  if (j.contains("formVersions")) {
    r.formVersions = j["formVersions"].get<std::vector<uint8_t>>();
  }
  r.gameVersion = j.value("gameVersion", r.gameVersion);
  if (j.contains("plugins")) {
    r.plugins = j["plugins"].get<std::vector<std::string>>();
  }
  if (j.contains("lightPlugins")) {
    r.lightPlugins = j["lightPlugins"].get<std::vector<std::string>>();
  }
  if (j.contains("worldspace")) {
    r.worldspace = ToU32(j["worldspace"].get<std::string>());
  }
  return r;
}

// A binary PPM (P6, maxval 255) into RGBA.
void LoadPpm(const std::string& path, uint32_t& w, uint32_t& h, Bytes& rgba)
{
  const Bytes raw = ReadFile(path);
  std::string text(raw.begin(), raw.end());
  std::istringstream in(text);
  std::string magic;
  int maxval = 0;
  in >> magic >> w >> h >> maxval;
  if (magic != "P6" || maxval != 255 || w == 0 || h == 0 || w > 8192 ||
      h > 8192) {
    throw Error(ErrorCode::Io, path + " is not a binary PPM (P6, 8-bit)");
  }
  in.get();
  const size_t start = static_cast<size_t>(in.tellg());
  if (raw.size() < start + static_cast<size_t>(w) * h * 3) {
    throw Error(ErrorCode::Io, path + " is cut short");
  }
  rgba.resize(static_cast<size_t>(w) * h * 4);
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    rgba[4 * i] = raw[start + 3 * i];
    rgba[4 * i + 1] = raw[start + 3 * i + 1];
    rgba[4 * i + 2] = raw[start + 3 * i + 2];
    rgba[4 * i + 3] = 255;
  }
}

int CmdInspect(const Args& a)
{
  const Bytes raw = ReadFile(a.positional.at(0));
  const SaveFile s = Parse(raw);
  const json j = Inspect(s, raw.size());
  if (a.Has("--json")) {
    std::cout << j.dump(2) << "\n";
  } else {
    PrintInspect(j);
  }
  return kOk;
}

int CmdValidate(const Args& a)
{
  const SaveFile s = Parse(ReadFile(a.positional.at(0)));
  const TemplateRules rules = a.Has("--rules")
    ? RulesFromFile(a.Get("--rules"))
    : FalloutMpTemplateRules();
  int errors = 0;
  for (auto& f : ValidateTemplate(s, rules)) {
    const char* sev = f.severity == Finding::Severity::Error ? "ERROR"
      : f.severity == Finding::Severity::Warning             ? "WARN "
                                                             : "note ";
    errors += f.severity == Finding::Severity::Error;
    std::cout << sev << " " << f.code << ": " << f.message << "\n";
  }
  std::cout << (errors ? "Not a valid template: " : "Valid template")
            << (errors ? std::to_string(errors) + " error(s)" : "") << "\n";
  return errors ? kFailed : kOk;
}

int CmdRoundtrip(const Args& a)
{
  const Bytes raw = ReadFile(a.positional.at(0));
  const auto t0 = std::chrono::steady_clock::now();
  const Bytes out = Serialize(Parse(raw));
  const auto ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0)
                    .count();
  if (out == raw) {
    std::cout << "identical (" << raw.size() << " bytes, " << ms << " ms)\n";
    return kOk;
  }
  size_t i = 0;
  while (i < std::min(out.size(), raw.size()) && out[i] == raw[i]) {
    ++i;
  }
  std::cout << "DIFFERENT: first difference at byte " << i << " (sizes "
            << raw.size() << " and " << out.size() << ")\n";
  return kFailed;
}

int CmdPatch(const Args& a)
{
  if (a.positional.size() < 2) {
    throw std::invalid_argument("patch needs <in> <out>");
  }
  const EntryPatch p = PatchFromArgs(a);
  if (!p.placement && !p.gameHour && !p.gameDaysPassed) {
    throw std::invalid_argument("nothing to patch");
  }
  const Bytes raw = ReadFile(a.positional[0]);
  const auto t0 = std::chrono::steady_clock::now();
  const Bytes out = WriteEntrySave(raw, p);
  const auto ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0)
                    .count();
  WriteFile(a.positional[1], out);
  std::cout << "wrote " << a.positional[1] << " (" << out.size()
            << " bytes, patched and verified in " << ms << " ms)\n";
  return kOk;
}

int CmdDiff(const Args& a)
{
  if (a.positional.size() < 2) {
    throw std::invalid_argument("diff needs <a> <b>");
  }
  const auto d =
    Diff(Parse(ReadFile(a.positional[0])), Parse(ReadFile(a.positional[1])));
  if (a.Has("--json")) {
    json j = json::array();
    for (auto& x : d) {
      j.push_back({ { "where", x.where }, { "what", x.what } });
    }
    std::cout << j.dump(2) << "\n";
  } else {
    for (auto& x : d) {
      std::cout << x.where << ": " << x.what << "\n";
    }
    std::cout << (d.empty() ? "no differences\n"
                            : std::to_string(d.size()) + " difference(s)\n");
  }
  return d.empty() ? kOk : kFailed;
}

int CmdNormalize(const Args& a)
{
  if (a.positional.size() < 2) {
    throw std::invalid_argument("normalize needs <in> <out>");
  }
  SaveFile s = Parse(ReadFile(a.positional[0]));
  SetHeaderText(s, a.Get("--name", "FalloutMP"), ToU32(a.Get("--level", "1")),
                "0d.0h.0m.0 days.0 hours.0 minutes");
  if (a.Has("--screenshot")) {
    uint32_t w = 0, h = 0;
    Bytes rgba;
    LoadPpm(a.Get("--screenshot"), w, h, rgba);
    ReplaceScreenshot(s, w, h, std::move(rgba));
  } else if (a.Has("--black")) {
    Bytes rgba(
      static_cast<size_t>(s.header.shotWidth) * s.header.shotHeight * 4, 0);
    for (size_t i = 3; i < rgba.size(); i += 4) {
      rgba[i] = 255;
    }
    ReplaceScreenshot(s, s.header.shotWidth, s.header.shotHeight,
                      std::move(rgba));
  }
  const Bytes out = Serialize(s);
  Parse(out); // must still parse
  WriteFile(a.positional[1], out);
  std::cout << "wrote " << a.positional[1] << " (" << out.size()
            << " bytes)\n";
  return kOk;
}

int CmdSha256(const Args& a)
{
  const Bytes raw = ReadFile(a.positional.at(0));
  std::cout << Sha256Hex(raw) << "  " << a.positional[0] << "\n";
  return kOk;
}

int CmdManifestEntry(const Args& a)
{
  const std::string path = a.positional.at(0);
  const Bytes raw = ReadFile(path);
  const SaveFile s = Parse(raw);
  TemplateInfo t;
  t.id = a.Get("--id");
  if (t.id.empty()) {
    throw std::invalid_argument("--id is required");
  }
  t.file = a.Get("--file", t.id + ".fos");
  t.sha256 = Sha256Hex(raw);
  t.size = raw.size();
  t.gameVersion = s.gameVersion;
  t.headerVersion = s.header.version;
  t.formVersion = s.formVersion;
  t.plugins = s.plugins;
  t.lightPlugins = s.lightPlugins;
  t.sex =
    static_cast<int>(ToU32(a.Get("--sex", std::to_string(s.header.sex))));
  t.notes = a.Get("--notes");
  std::time_t now = std::time(nullptr);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
  t.createdAt = buf;
  std::cout << TemplateToJson(t) << "\n";
  return kOk;
}

}

int main(int argc, char** argv)
{
  if (argc < 3) {
    std::cerr << kUsageText;
    return kUsage;
  }
  const std::string cmd = argv[1];
  const std::map<std::string, int> arity = {
    { "--json", 0 },       { "--rules", 1 },      { "--ticket", 1 },
    { "--worldspace", 1 }, { "--pos", 3 },        { "--yaw", 1 },
    { "--hour", 1 },       { "--days", 1 },       { "--name", 1 },
    { "--level", 1 },      { "--screenshot", 1 }, { "--black", 0 },
    { "--id", 1 },         { "--file", 1 },       { "--notes", 1 },
    { "--sex", 1 },
  };
  try {
    const Args a = ParseArgs(argc, argv, 2, arity);
    if (a.positional.empty()) {
      throw std::invalid_argument("missing file argument");
    }
    if (cmd == "inspect") {
      return CmdInspect(a);
    }
    if (cmd == "validate") {
      return CmdValidate(a);
    }
    if (cmd == "roundtrip") {
      return CmdRoundtrip(a);
    }
    if (cmd == "patch") {
      return CmdPatch(a);
    }
    if (cmd == "diff") {
      return CmdDiff(a);
    }
    if (cmd == "normalize") {
      return CmdNormalize(a);
    }
    if (cmd == "sha256") {
      return CmdSha256(a);
    }
    if (cmd == "manifest-entry") {
      return CmdManifestEntry(a);
    }
    std::cerr << "unknown command " << cmd << "\n\n" << kUsageText;
    return kUsage;
  } catch (const std::invalid_argument& e) {
    std::cerr << "fmp_savetool: " << e.what() << "\n\n" << kUsageText;
    return kUsage;
  } catch (const std::exception& e) {
    std::cerr << "fmp_savetool: " << e.what() << "\n";
    return kFailed;
  }
}
