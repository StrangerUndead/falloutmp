#include "fo4/Fo4Settings.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using namespace fo4;

TEST_CASE("Fo4 settings: defaults without a block", "[fo4][Fo4Settings]")
{
  auto r = ParseFo4Settings(nlohmann::json());
  REQUIRE(r.worldStatePath == "world/fo4-world.json");
  REQUIRE(r.unknownKeys.empty());
  REQUIRE(r.settings.party.maxPartySize == 8);
  REQUIRE(r.settings.map.fastTravel ==
          MapSettings::FastTravelMode::DiscoveredOnly);
}

TEST_CASE("Fo4 settings: every section maps onto the server settings",
          "[fo4][Fo4Settings]")
{
  auto j = nlohmann::json::parse(R"({
    "worldStatePath": "data/world.json",
    "activationReach": 256,
    "powerArmor": { "allowInCombat": false, "transitionTimeoutMs": 8000,
                    "drainPerSecond": { "jetpack": 0.01 } },
    "workshop": { "claimRule": "adminOnly", "maxObjects": 400,
                  "allowScale": true, "maxScale": 2 },
    "locks": { "hackAttempts": 3, "minChance": 0.1 },
    "combat": { "infiniteAmmo": true },
    "damage": { "pvpDamageMult": 0.5 },
    "pvp": { "friendlyFire": true, "maxPartySize": 4 },
    "progression": { "xpMult": 2, "maxLevel": 50 },
    "containers": { "markStolen": false },
    "map": { "fastTravel": "off", "discoveryRadius": 500 }
  })");
  auto r = ParseFo4Settings(j);
  auto& s = r.settings;
  REQUIRE(r.worldStatePath == "data/world.json");
  REQUIRE(r.unknownKeys.empty());
  REQUIRE(s.activationReach == 256.f);
  REQUIRE(!s.powerArmor.allowInCombat);
  REQUIRE(s.powerArmor.transitionTimeoutMs == 8000);
  REQUIRE(s.powerArmor.drainPerSecond[4] == Catch::Approx(0.01f));
  REQUIRE(s.powerArmor.drainPerSecond[1] == Catch::Approx(1.f / 2400.f));
  REQUIRE(s.workshop.claimRule == WorkshopSettings::ClaimRule::AdminOnly);
  REQUIRE(s.workshop.maxObjects == 400);
  REQUIRE(s.workshop.allowScale);
  REQUIRE(s.locks.hackAttempts == 3);
  REQUIRE(s.locks.minChance == Catch::Approx(0.1f));
  REQUIRE(s.fire.infiniteAmmo);
  REQUIRE(s.damage.pvpDamageMult == Catch::Approx(0.5f));
  REQUIRE(s.party.friendlyFire);
  REQUIRE(s.party.maxPartySize == 4);
  REQUIRE(s.progression.xpMult == Catch::Approx(2.f));
  REQUIRE(s.progression.maxLevel == 50);
  REQUIRE(!s.containers.markStolen);
  REQUIRE(s.map.fastTravel == MapSettings::FastTravelMode::Off);
}

TEST_CASE("Fo4 settings: misspelt keys are reported, bad values rejected",
          "[fo4][Fo4Settings]")
{
  auto r = ParseFo4Settings(nlohmann::json::parse(
    R"({ "workshop": { "maxObjcts": 5 }, "pvpp": {} })"));
  REQUIRE(r.unknownKeys.size() == 2);
  REQUIRE(std::find(r.unknownKeys.begin(), r.unknownKeys.end(),
                    "fo4.workshop.maxObjcts") != r.unknownKeys.end());
  REQUIRE(std::find(r.unknownKeys.begin(), r.unknownKeys.end(),
                    "fo4.pvpp") != r.unknownKeys.end());

  auto bad = [](const char* text) {
    return ParseFo4Settings(nlohmann::json::parse(text));
  };
  REQUIRE_THROWS_WITH(bad(R"({ "pvp": { "friendlyFire": "yes" } })"),
                      Catch::Matchers::ContainsSubstring(
                        "fo4.pvp.friendlyFire' must be a boolean"));
  REQUIRE_THROWS_WITH(bad(R"({ "workshop": { "claimRule": "anyone" } })"),
                      Catch::Matchers::ContainsSubstring("firstClaim"));
  REQUIRE_THROWS_WITH(bad(R"({ "workshop": { "maxObjects": -1 } })"),
                      Catch::Matchers::ContainsSubstring("non-negative"));
  REQUIRE_THROWS_WITH(bad(R"({ "locks": { "maxChance": 2 } })"),
                      Catch::Matchers::ContainsSubstring("between 0 and 1"));
  REQUIRE_THROWS_WITH(
    bad(R"({ "locks": { "minChance": 0.9, "maxChance": 0.5 } })"),
    Catch::Matchers::ContainsSubstring("above"));
  REQUIRE_THROWS_WITH(bad(R"({ "map": [] })"),
                      Catch::Matchers::ContainsSubstring("must be an object"));
  REQUIRE_THROWS(bad(R"({ "worldStatePath": "" })"));
}

TEST_CASE("Fo4 settings: human NPCs are off by default", "[fo4][Fo4Settings]")
{
  auto d = ParseFo4Settings(nlohmann::json());
  REQUIRE(!d.humanNpcs);
  REQUIRE(d.blockedNpcRaces ==
          std::vector<std::string>{ "HumanRace", "HumanChildRace" });
  REQUIRE(!d.settings.movement.enforceSpeed);

  auto on = ParseFo4Settings(
    nlohmann::json::parse(R"({ "npc": { "humanNpcs": true } })"));
  REQUIRE(on.blockedNpcRaces.empty());

  auto more = ParseFo4Settings(nlohmann::json::parse(
    R"({ "npc": { "blockedRaces": ["HumanRace", "GhoulRace"] } })"));
  REQUIRE(more.blockedNpcRaces.size() == 2);
  REQUIRE_THROWS_WITH(
    ParseFo4Settings(
      nlohmann::json::parse(R"({ "npc": { "blockedRaces": [1] } })")),
    Catch::Matchers::ContainsSubstring("array of strings"));
}
