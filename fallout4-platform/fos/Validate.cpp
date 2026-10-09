// Template validation (design §9) and section diffs.
#include "Bytes.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace fmp::fos {

namespace {
std::string Lower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

std::string Join(const std::vector<std::string>& v)
{
  std::string s;
  for (auto& x : v) {
    s += (s.empty() ? "" : ", ") + x;
  }
  return s.empty() ? "(none)" : s;
}

std::string Hex(uint32_t v)
{
  static const char* d = "0123456789ABCDEF";
  std::string s;
  for (int i = 28; i >= 0; i -= 4) {
    s += d[(v >> i) & 0xF];
  }
  return s;
}

void Add(std::vector<Finding>& out, Finding::Severity sev, std::string code,
         std::string message)
{
  out.push_back({ sev, std::move(code), std::move(message) });
}
}

TemplateRules FalloutMpTemplateRules()
{
  TemplateRules r;
  r.lightPlugins = {
    "ccBGSFO4044-HellfirePowerArmor.esl",
    "ccBGSFO4115-X02.esl",
    "ccBGSFO4116-HeavyFlamer.esl",
    "ccBGSFO4110-WS_Enclave.esl",
    "ccBGSFO4096-AS_Enclave.esl",
    "ccFSVFO4007-Halloween.esl",
    "ccBGSFO4046-TesCan.esl",
    "ccSBJFO4003-Grenade.esl",
    "ccOTMFO4001-Remnants.esl",
  };
  return r;
}

std::vector<Finding> ValidateTemplate(const SaveFile& save,
                                      const TemplateRules& rules)
{
  using S = Finding::Severity;
  std::vector<Finding> out;

  if (save.header.version != rules.headerVersion) {
    Add(out, S::Error, "header-version",
        "header version " + std::to_string(save.header.version) +
          ", expected " + std::to_string(rules.headerVersion));
  }
  if (std::find(rules.formVersions.begin(), rules.formVersions.end(),
                save.formVersion) == rules.formVersions.end()) {
    Add(out, S::Error, "form-version",
        "formVersion " + std::to_string(save.formVersion) +
          " is not one the rules allow");
  }
  if (!rules.gameVersion.empty() && save.gameVersion != rules.gameVersion) {
    Add(out, S::Error, "game-version",
        "made on " + save.gameVersion + ", expected " + rules.gameVersion);
  }

  std::vector<std::string> plugins, expected;
  for (auto& p : save.plugins) {
    plugins.push_back(Lower(p));
  }
  for (auto& p : rules.plugins) {
    expected.push_back(Lower(p));
  }
  if (plugins != expected) {
    Add(out, S::Error, "plugins",
        "plugins are " + Join(save.plugins) + "; expected exactly " +
          Join(rules.plugins));
  }
  std::set<std::string> light, expectedLight;
  for (auto& p : save.lightPlugins) {
    light.insert(Lower(p));
  }
  for (auto& p : rules.lightPlugins) {
    expectedLight.insert(Lower(p));
  }
  if (light != expectedLight) {
    std::vector<std::string> extra, missing;
    for (auto& p : light) {
      if (!expectedLight.count(p)) {
        extra.push_back(p);
      }
    }
    for (auto& p : expectedLight) {
      if (!light.count(p)) {
        missing.push_back(p);
      }
    }
    Add(out, S::Error, "light-plugins",
        "light plugins differ; not allowed: " + Join(extra) +
          "; missing: " + Join(missing));
  }

  bool entryOk = true;
  try {
    CheckEntryTemplate(save);
  } catch (const Error& e) {
    entryOk = false;
    Add(out, S::Error, "entry", e.what());
  }
  if (entryOk) {
    auto loc = PlayerLocation::Read(save);
    if (loc.worldspace.Value() != rules.worldspace) {
      Add(out, S::Error, "worldspace",
          "the player is in worldspace " + Hex(loc.worldspace.Value()) +
            ", expected " + Hex(rules.worldspace));
    }
  }

  // References into the FormID array, and the array into the plugin lists
  size_t badIndex = 0;
  for (auto& f : save.changeForms) {
    if (f.refId.GetKind() == RefId::Kind::Index &&
        (f.refId.Value() == 0 || f.refId.Value() > save.formIds.size())) {
      ++badIndex;
    }
  }
  if (badIndex) {
    Add(out, S::Error, "refid-index",
        std::to_string(badIndex) +
          " change forms point past the FormID array");
  }
  size_t badPlugin = 0;
  for (auto id : save.formIds) {
    const uint32_t hb = id >> 24;
    if (id == 0) {
      continue;
    }
    if (hb == 0xFE) {
      if (((id >> 12) & 0xFFF) >= save.lightPlugins.size()) {
        ++badPlugin;
      }
    } else if (hb >= save.plugins.size()) {
      ++badPlugin;
    }
  }
  if (badPlugin) {
    Add(out, S::Error, "formid-plugin",
        std::to_string(badPlugin) +
          " FormID array entries name a plugin the save doesn't list");
  }

  // No power-armor frame in the world
  size_t unreadable = 0;
  for (auto& f : save.changeForms) {
    if (f.refId.GetKind() != RefId::Kind::Created ||
        f.type != kChangeFormRefr) {
      continue;
    }
    try {
      Bytes body = f.Body();
      if (body.size() < 31) {
        ++unreadable;
        continue;
      }
      RefId base{ (static_cast<uint32_t>(body[28]) << 16) |
                  (static_cast<uint32_t>(body[29]) << 8) | body[30] };
      if (base.Resolve(save.formIds) == kPowerArmorFrameBase) {
        Add(out, S::Error, "power-armor",
            "a power armor frame exists (created reference FF" +
              Hex(f.refId.Value()).substr(2) + ")");
      }
    } catch (const Error&) {
      ++unreadable;
    }
  }
  if (unreadable) {
    Add(out, S::Warning, "created-refs",
        std::to_string(unreadable) +
          " created references couldn't be checked for power armor");
  }

  if (save.header.playerName != "FalloutMP") {
    Add(out, S::Note, "header",
        "header not normalized yet (fmp_savetool normalize)");
  }
  Add(out, S::Note, "capture",
      "quests, inventory and nearby actors can only be checked in game "
      "(the capture command, F33-T06)");
  return out;
}

namespace {
void DiffTable(std::vector<Difference>& out, const char* name,
               const std::vector<GlobalDataBlock>& a,
               const std::vector<GlobalDataBlock>& b)
{
  if (a.size() != b.size()) {
    out.push_back({ name,
                    std::to_string(a.size()) + " vs " +
                      std::to_string(b.size()) + " blocks" });
  }
  for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
    if (a[i].type != b[i].type) {
      out.push_back({ std::string(name) + "[" + std::to_string(i) + "]",
                      "type " + std::to_string(a[i].type) + " vs " +
                        std::to_string(b[i].type) });
    } else if (a[i].data != b[i].data) {
      out.push_back(
        { std::string(name) + "[type " + std::to_string(a[i].type) + "]",
          std::to_string(a[i].data.size()) + " vs " +
            std::to_string(b[i].data.size()) + " bytes, contents differ" });
    }
  }
}
}

std::vector<Difference> Diff(const SaveFile& a, const SaveFile& b)
{
  std::vector<Difference> out;
  auto field = [&](const char* where, bool same, std::string what) {
    if (!same) {
      out.push_back({ where, std::move(what) });
    }
  };
  const Header &ha = a.header, &hb = b.header;
  field("header.version", ha.version == hb.version,
        std::to_string(ha.version) + " vs " + std::to_string(hb.version));
  field("header.playerName", ha.playerName == hb.playerName,
        ha.playerName + " vs " + hb.playerName);
  field("header.level", ha.level == hb.level,
        std::to_string(ha.level) + " vs " + std::to_string(hb.level));
  field("header.location", ha.location == hb.location,
        ha.location + " vs " + hb.location);
  field("header.playTime", ha.playTime == hb.playTime,
        ha.playTime + " vs " + hb.playTime);
  field("header (other fields)",
        ha.saveNumber == hb.saveNumber && ha.race == hb.race &&
          ha.sex == hb.sex && ha.fileTime == hb.fileTime &&
          ha.extra == hb.extra && ha.currentXp == hb.currentXp &&
          ha.nextLevelXp == hb.nextLevelXp,
        "differ");
  field("screenshot", a.screenshot == b.screenshot,
        std::to_string(ha.shotWidth) + "x" + std::to_string(ha.shotHeight) +
          " vs " + std::to_string(hb.shotWidth) + "x" +
          std::to_string(hb.shotHeight));
  field("formVersion", a.formVersion == b.formVersion,
        std::to_string(a.formVersion) + " vs " +
          std::to_string(b.formVersion));
  field("gameVersion", a.gameVersion == b.gameVersion,
        a.gameVersion + " vs " + b.gameVersion);
  field("plugins", a.plugins == b.plugins,
        Join(a.plugins) + " vs " + Join(b.plugins));
  field("lightPlugins",
        a.lightPlugins == b.lightPlugins &&
          a.hasLightPluginList == b.hasLightPluginList,
        Join(a.lightPlugins) + " vs " + Join(b.lightPlugins));
  field("fileLocationTable (unused)", a.fltUnused == b.fltUnused, "differ");
  DiffTable(out, "table1", a.table1, b.table1);
  DiffTable(out, "table2", a.table2, b.table2);
  DiffTable(out, "table3", a.table3, b.table3);
  if (a.changeForms.size() != b.changeForms.size()) {
    out.push_back({ "changeForms",
                    std::to_string(a.changeForms.size()) + " vs " +
                      std::to_string(b.changeForms.size()) });
  }
  size_t shown = 0, more = 0;
  for (size_t i = 0; i < std::min(a.changeForms.size(), b.changeForms.size());
       ++i) {
    const auto& x = a.changeForms[i];
    const auto& y = b.changeForms[i];
    if (x == y) {
      continue;
    }
    if (shown++ >= 50) {
      ++more;
      continue;
    }
    std::string what;
    if (!(x.refId == y.refId)) {
      what = "refId " + Hex(x.refId.raw).substr(2) + " vs " +
        Hex(y.refId.raw).substr(2);
    } else if (x.flags != y.flags) {
      what = "flags " + Hex(x.flags) + " vs " + Hex(y.flags);
    } else {
      what = "data differs (" + std::to_string(x.data.size()) + " vs " +
        std::to_string(y.data.size()) + " bytes stored)";
    }
    out.push_back(
      { "changeForm " + Hex(x.refId.raw).substr(2) + " #" + std::to_string(i),
        what });
  }
  if (more) {
    out.push_back({ "changeForms", std::to_string(more) + " more differ" });
  }
  field("formIds", a.formIds == b.formIds,
        std::to_string(a.formIds.size()) + " vs " +
          std::to_string(b.formIds.size()) + " entries");
  field("visitedWorldspaces", a.visitedWorldspaces == b.visitedWorldspaces,
        "differ");
  field("unknownTable3", a.unknownTable3 == b.unknownTable3, "differs");
  return out;
}

}
