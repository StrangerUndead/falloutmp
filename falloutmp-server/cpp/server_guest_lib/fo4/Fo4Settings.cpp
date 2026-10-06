#include "Fo4Settings.h"
#include <algorithm>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace fo4 {
namespace {

// Reads one object level and remembers which keys were consumed.
class Section
{
public:
  Section(const nlohmann::json& j, std::string path,
          std::vector<std::string>& unknown)
    : j(j)
    , path(std::move(path))
    , unknown(unknown)
  {
    if (!j.is_object()) {
      throw std::runtime_error(
        fmt::format("'{}' must be an object", this->path));
    }
  }

  ~Section()
  {
    for (auto it = j.begin(); it != j.end(); ++it) {
      if (!used.count(it.key())) {
        unknown.push_back(path + "." + it.key());
      }
    }
  }

  template <class T>
  void Get(const char* key, T& out)
  {
    used.insert(key);
    auto it = j.find(key);
    if (it == j.end()) {
      return;
    }
    if constexpr (std::is_same_v<T, bool>) {
      if (!it->is_boolean()) {
        Fail(key, "a boolean");
      }
    } else if constexpr (std::is_arithmetic_v<T>) {
      if (!it->is_number()) {
        Fail(key, "a number");
      }
      if constexpr (std::is_unsigned_v<T>) {
        if (it->get<double>() < 0) {
          Fail(key, "a non-negative number");
        }
      }
    } else if constexpr (std::is_same_v<T, std::string>) {
      if (!it->is_string()) {
        Fail(key, "a string");
      }
    } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
      if (!it->is_array()) {
        Fail(key, "an array of strings");
      }
      for (auto& e : *it) {
        if (!e.is_string()) {
          Fail(key, "an array of strings");
        }
      }
    }
    out = it->get<T>();
  }

  // Positive numbers only (radii, limits, timeouts).
  template <class T>
  void GetPositive(const char* key, T& out)
  {
    T v = out;
    Get(key, v);
    if (!(v > 0)) {
      Fail(key, "greater than 0");
    }
    out = v;
  }

  void GetFraction(const char* key, float& out)
  {
    float v = out;
    Get(key, v);
    if (v < 0.f || v > 1.f) {
      Fail(key, "between 0 and 1");
    }
    out = v;
  }

  template <class Enum>
  void GetEnum(const char* key, Enum& out,
               std::initializer_list<std::pair<const char*, Enum>> names)
  {
    used.insert(key);
    auto it = j.find(key);
    if (it == j.end()) {
      return;
    }
    if (it->is_string()) {
      for (auto& [name, value] : names) {
        if (it->get<std::string>() == name) {
          out = value;
          return;
        }
      }
    }
    std::string list;
    for (auto& [name, value] : names) {
      list += (list.empty() ? "" : ", ") + std::string("\"") + name + "\"";
    }
    Fail(key, "one of " + list);
  }

  // Calls fn(Section&) for a nested object if present.
  template <class Fn>
  void Child(const char* key, Fn fn)
  {
    used.insert(key);
    auto it = j.find(key);
    if (it == j.end()) {
      return;
    }
    Section child(*it, path + "." + key, unknown);
    fn(child);
  }

private:
  [[noreturn]] void Fail(const char* key, const std::string& what)
  {
    throw std::runtime_error(
      fmt::format("server setting '{}.{}' must be {}", path, key, what));
  }

  const nlohmann::json& j;
  std::string path;
  std::vector<std::string>& unknown;
  std::set<std::string> used;
};

}

Fo4SettingsParseResult ParseFo4Settings(const nlohmann::json& block)
{
  Fo4SettingsParseResult res;
  auto& s = res.settings;
  if (block.is_null()) {
    return res;
  }
  // Scoped so the root section reports unknown keys before returning
  {
    Section root(block, "fo4", res.unknownKeys);
    root.Get("worldStatePath", res.worldStatePath);
    if (res.worldStatePath.empty()) {
      throw std::runtime_error("server setting 'fo4.worldStatePath' is empty");
    }
    root.GetPositive("activationReach", s.activationReach);
    root.Get("reachSlack", s.reachSlack);
    root.Get("killXpBase", s.killXpBase);
    root.Get("capsId", s.capsId);
    root.GetPositive("timeWeatherIntervalMs", s.timeWeatherIntervalMs);
    root.GetPositive("workshopSnapshotChunk", s.workshopSnapshotChunk);
    root.Get("powerArmorBlockedBipedSlots", s.powerArmorBlockedBipedSlots);

    root.Child("powerArmor", [&](Section& pa) {
      auto& p = s.powerArmor;
      pa.Get("allowStealing", p.allowStealing);
      pa.Get("allowInCombat", p.allowInCombat);
      pa.Get("exitWhileFalling", p.exitWhileFalling);
      pa.Get("keepOnDisconnect", p.keepOnDisconnect);
      pa.GetPositive("transitionTimeoutMs", p.transitionTimeoutMs);
      pa.Get("pieceDamageMultPlayer", p.pieceDamageMultPlayer);
      pa.Get("pieceDamageMultNpc", p.pieceDamageMultNpc);
      pa.Get("fusionCoreId", p.fusionCoreId);
      pa.Get("jetpackKeywordId", p.jetpackKeywordId);
      pa.Child("drainPerSecond", [&](Section& d) {
        d.Get("idle", p.drainPerSecond[0]);
        d.Get("walk", p.drainPerSecond[1]);
        d.Get("run", p.drainPerSecond[2]);
        d.Get("sprint", p.drainPerSecond[3]);
        d.Get("jetpack", p.drainPerSecond[4]);
      });
    });

    root.Child("workshop", [&](Section& ws) {
      auto& w = s.workshop;
      ws.GetEnum("claimRule", w.claimRule,
                 { { "firstClaim", WorkshopSettings::ClaimRule::FirstClaim },
                   { "adminOnly", WorkshopSettings::ClaimRule::AdminOnly },
                   { "gamemode", WorkshopSettings::ClaimRule::Gamemode } });
      ws.Get("maxPerPlayer", w.maxPerPlayer);
      ws.GetPositive("maxObjects", w.maxObjects);
      ws.GetPositive("defaultBudget", w.defaultBudget);
      ws.GetPositive("maxBuildDistance", w.maxBuildDistance);
      ws.Get("allowScale", w.allowScale);
      ws.GetPositive("minScale", w.minScale);
      ws.GetPositive("maxScale", w.maxScale);
      ws.GetPositive("maxRequestsPerSecond", w.maxRequestsPerSecond);
      ws.GetPositive("maxWireLength", w.maxWireLength);
      ws.GetPositive("maxWiresPerConnector", w.maxWiresPerConnector);
      ws.GetPositive("buildModeLeaveAreaMs", w.buildModeLeaveAreaMs);
      ws.Get("foodItemId", w.foodItemId);
      ws.Get("waterItemId", w.waterItemId);
    });
    if (s.workshop.minScale > s.workshop.maxScale) {
      throw std::runtime_error(
        "server setting 'fo4.workshop.minScale' is above 'maxScale'");
    }

    root.Child("locks", [&](Section& ls) {
      auto& l = s.locks;
      ls.Get("bobbyPinId", l.bobbyPinId);
      ls.GetPositive("sessionTimeoutMs", l.sessionTimeoutMs);
      ls.Get("lockoutMs", l.lockoutMs);
      ls.GetPositive("hackAttempts", l.hackAttempts);
      ls.Get("perPerceptionPoint", l.perPerceptionPoint);
      ls.Get("perLocksmithRank", l.perLocksmithRank);
      ls.Get("perIntelligencePoint", l.perIntelligencePoint);
      ls.Get("perHackerRank", l.perHackerRank);
      ls.GetFraction("minChance", l.minChance);
      ls.GetFraction("maxChance", l.maxChance);
    });
    if (s.locks.minChance > s.locks.maxChance) {
      throw std::runtime_error(
        "server setting 'fo4.locks.minChance' is above 'maxChance'");
    }

    root.Child("combat", [&](Section& cs) {
      auto& f = s.fire;
      cs.Get("fireRateTolerance", f.fireRateTolerance);
      cs.GetPositive("maxRewindMs", f.maxRewindMs);
      cs.GetPositive("shotLifetimeMs", f.shotLifetimeMs);
      cs.GetPositive("rangeTolerance", f.rangeTolerance);
      cs.Get("infiniteAmmo", f.infiniteAmmo);
    });

    root.Child("damage", [&](Section& ds) {
      auto& d = s.damage;
      ds.GetPositive("physicalDamageFactor", d.physicalDamageFactor);
      ds.GetPositive("armorReductionExp", d.armorReductionExp);
      ds.GetPositive("pvpDamageFactor", d.pvpDamageFactor);
      ds.Get("pvpDamageMult", d.pvpDamageMult);
      ds.GetPositive("headshotMult", d.headshotMult);
      ds.Get("critsIgnoreResistance", d.critsIgnoreResistance);
    });

    root.Child("pvp", [&](Section& ps) {
      auto& p = s.party;
      ps.GetPositive("maxPartySize", p.maxPartySize);
      ps.GetPositive("inviteTtlMs", p.inviteTtlMs);
      ps.Get("flagCooldownMs", p.flagCooldownMs);
      ps.Get("combatTagMs", p.combatTagMs);
      ps.Get("friendlyFire", p.friendlyFire);
      ps.Get("xpShareRadius", p.xpShareRadius);
      ps.Get("defaultFlag", p.defaultFlag);
    });

    root.Child("progression", [&](Section& pr) {
      auto& p = s.progression;
      pr.Get("xpMult", p.xpMult);
      pr.Get("intelligenceXpBonusPerPoint", p.intelligenceXpBonusPerPoint);
      pr.GetPositive("maxLevel", p.maxLevel);
      pr.Get("creationPoints", p.creationPoints);
      pr.GetPositive("specialMax", p.specialMax);
      pr.GetPositive("specialMaxWithBobblehead", p.specialMaxWithBobblehead);
      pr.Get("perkPointsPerLevel", p.perkPointsPerLevel);
    });

    root.Child("containers", [&](Section& cs) {
      cs.GetPositive("reach", s.containers.reach);
      cs.Get("markStolen", s.containers.markStolen);
    });

    root.Child("npc", [&](Section& ns) {
      ns.Get("humanNpcs", res.humanNpcs);
      ns.Get("blockedRaces", res.blockedNpcRaces);
    });
    if (res.humanNpcs) {
      auto& r = res.blockedNpcRaces;
      r.erase(std::remove_if(r.begin(), r.end(),
                             [](const std::string& e) {
                               return e == "HumanRace" ||
                                 e == "HumanChildRace";
                             }),
              r.end());
    }

    root.Child("movement", [&](Section& mv) {
      auto& m = s.movement;
      mv.Get("enforceSpeed", m.enforceSpeed);
      mv.GetPositive("walkSpeed", m.walkSpeed);
      mv.GetPositive("sprintSpeed", m.sprintSpeed);
      mv.GetPositive("encumberedSpeed", m.encumberedSpeed);
      mv.GetPositive("powerArmorSprintSpeed", m.powerArmorSprintSpeed);
      mv.GetPositive("jumpUpSpeed", m.jumpUpSpeed);
      mv.GetPositive("jetpackUpSpeed", m.jetpackUpSpeed);
      mv.GetPositive("terminalFallSpeed", m.terminalFallSpeed);
      mv.GetPositive("speedTolerance", m.speedTolerance);
      mv.Get("distanceSlack", m.distanceSlack);
      mv.Get("jitterMs", m.jitterMs);
      mv.GetPositive("maxDtMs", m.maxDtMs);
      mv.GetPositive("violationThreshold", m.violationThreshold);
      mv.Get("scoreDecayPerSec", m.scoreDecayPerSec);
      mv.GetPositive("teleportGraceMs", m.teleportGraceMs);
      mv.Get("speedMultAvId", m.speedMultAvId);
    });

    root.Child("map", [&](Section& ms) {
      ms.GetPositive("discoveryRadius", s.map.discoveryRadius);
      ms.GetEnum(
        "fastTravel", s.map.fastTravel,
        { { "off", MapSettings::FastTravelMode::Off },
          { "discoveredOnly", MapSettings::FastTravelMode::DiscoveredOnly },
          { "any", MapSettings::FastTravelMode::Any } });
      ms.Get("combatCooldownMs", s.map.combatCooldownMs);
    });
  }
  return res;
}

}
