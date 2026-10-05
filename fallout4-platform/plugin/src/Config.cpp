#include "Config.h"

#include <F4SE/F4SE.h>

#include <fstream>
#include <random>

namespace fmp {

using nlohmann::json;

bool PluginConfig::FeatureEnabled(std::string_view name) const
{
  auto it = features.find(std::string(name));
  return it == features.end() || !it->is_boolean() || it->get<bool>();
}

PluginConfig PluginConfig::Load(const std::string& path)
{
  PluginConfig c;
  json j = json::object();
  if (std::ifstream f(path); f) {
    j = json::parse(f, nullptr, /*allow_exceptions*/ false);
    if (j.is_discarded() || !j.is_object()) {
      REX::ERROR("{} is not valid JSON, using defaults", path);
      j = json::object();
    }
  }
  c.raw = j;
  c.serverIp = j.value("server-ip", std::string());
  c.serverPort = j.value("server-port", 7777);
  c.profileId = j.value("profile-id", 0u);
  c.puppetMove = j.value("puppet-move", std::string("native"));
  c.probe = j.value("probe", false);
  if (j.contains("features") && j["features"].is_object()) {
    c.features = j["features"];
  }
  if (j.contains("puppet-base") && j["puppet-base"].is_string()) {
    try {
      c.puppetBaseId = static_cast<uint32_t>(
        std::stoul(j["puppet-base"].get<std::string>(), nullptr, 0));
    } catch (std::exception&) {
      REX::ERROR("puppet-base must be a form id like \"0x7\"");
    }
  }
  if (c.profileId == 0) {
    std::random_device rd;
    std::uniform_int_distribution<uint32_t> dist(1, 0x7fffffff);
    c.profileId = dist(rd);
    j["profile-id"] = c.profileId;
    if (!j.contains("server-ip")) {
      j["server-ip"] = "";
      j["server-port"] = 7777;
    }
    std::ofstream out(path);
    out << j.dump(2) << "\n";
    REX::INFO("Generated profile id {} in {}", c.profileId, path);
  }
  return c;
}

}
