// templates.json (F33 §4.5.1): the shipped entry templates and which one
// a runtime uses.
#include "Fos.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace fmp::fos {

namespace {
std::vector<uint64_t> VersionParts(std::string_view v)
{
  std::vector<uint64_t> parts;
  uint64_t cur = 0;
  bool any = false;
  for (char c : v) {
    if (c == '.') {
      parts.push_back(cur);
      cur = 0;
      any = false;
    } else if (std::isdigit(static_cast<unsigned char>(c))) {
      cur = cur * 10 + static_cast<uint64_t>(c - '0');
      any = true;
      if (cur > 0xFFFFFFFFull) {
        break;
      }
    } else {
      break;
    }
  }
  if (any || !parts.empty()) {
    parts.push_back(cur);
  }
  return parts;
}

std::vector<std::string> Strings(const nlohmann::json& j, const char* key)
{
  std::vector<std::string> out;
  if (j.contains(key) && j[key].is_array()) {
    for (auto& v : j[key]) {
      if (v.is_string()) {
        out.push_back(v.get<std::string>());
      }
    }
  }
  return out;
}
}

int CompareVersions(std::string_view a, std::string_view b)
{
  auto pa = VersionParts(a);
  auto pb = VersionParts(b);
  for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
    uint64_t x = i < pa.size() ? pa[i] : 0;
    uint64_t y = i < pb.size() ? pb[i] : 0;
    if (x != y) {
      return x < y ? -1 : 1;
    }
  }
  return 0;
}

std::vector<TemplateInfo> ParseTemplates(std::string_view text)
{
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(text);
  } catch (const std::exception& e) {
    throw Error(ErrorCode::BadLayout,
                std::string("templates.json: ") + e.what());
  }
  if (j.is_object() && j.contains("templates")) {
    j = j["templates"];
  }
  if (!j.is_array()) {
    throw Error(ErrorCode::BadLayout, "templates.json: expected an array");
  }
  std::vector<TemplateInfo> out;
  for (auto& e : j) {
    if (!e.is_object()) {
      continue;
    }
    TemplateInfo t;
    t.id = e.value("id", "");
    t.file = e.value("file", "");
    t.sha256 = e.value("sha256", "");
    t.size = e.value("size", uint64_t{ 0 });
    t.gameVersion = e.value("gameVersion", "");
    t.headerVersion = e.value("headerVersion", 0u);
    t.formVersion = e.value("formVersion", 0u);
    t.plugins = Strings(e, "plugins");
    t.lightPlugins = Strings(e, "lightPlugins");
    t.sex = e.value("sex", 0);
    t.notes = e.value("notes", "");
    t.createdAt = e.value("createdAt", "");
    if (t.id.empty() || t.file.empty() || t.gameVersion.empty()) {
      throw Error(ErrorCode::BadLayout,
                  "templates.json: an entry needs id, file and gameVersion");
    }
    out.push_back(std::move(t));
  }
  return out;
}

std::string TemplateToJson(const TemplateInfo& t)
{
  nlohmann::json j = {
    { "id", t.id },
    { "file", t.file },
    { "sha256", t.sha256 },
    { "size", t.size },
    { "gameVersion", t.gameVersion },
    { "headerVersion", t.headerVersion },
    { "formVersion", t.formVersion },
    { "plugins", t.plugins },
    { "lightPlugins", t.lightPlugins },
    { "sex", t.sex },
    { "createdAt", t.createdAt },
    { "notes", t.notes },
  };
  return j.dump(2);
}

const TemplateInfo* SelectTemplate(const std::vector<TemplateInfo>& list,
                                   std::string_view runtime, int sex)
{
  const auto rt = VersionParts(runtime);
  const TemplateInfo* best = nullptr;
  for (int pass = 0; pass < 2 && !best; ++pass) {
    for (auto& t : list) {
      const auto tv = VersionParts(t.gameVersion);
      // Same major.minor family, not newer than the runtime
      if (rt.size() < 2 || tv.size() < 2 || tv[0] != rt[0] || tv[1] != rt[1] ||
          CompareVersions(t.gameVersion, runtime) > 0) {
        continue;
      }
      if (pass == 0 && sex >= 0 && t.sex != sex) {
        continue;
      }
      if (!best || CompareVersions(t.gameVersion, best->gameVersion) > 0) {
        best = &t;
      }
    }
  }
  return best;
}

}
