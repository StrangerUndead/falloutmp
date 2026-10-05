#pragma once
// Server clock and weather (F25), map markers, discovery and fast travel
// (F26).
#include "Fo4Data.h"
#include <array>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fo4 {

// One clock for the whole server. Game time advances at `timeScale` game
// seconds per real second (vanilla 20). Persisted, so restarts continue.
class WorldClock
{
public:
  double GameDays(int64_t nowMs) const;
  float GameHour(int64_t nowMs) const;
  void SetGameDays(double days, int64_t nowMs);
  void SetTimeScale(float scale, int64_t nowMs);
  float TimeScale() const { return timeScale; }

  nlohmann::json ToJson(int64_t nowMs) const;
  void LoadJson(const nlohmann::json& j, int64_t nowMs);

private:
  double daysAtAnchor = 8.0 / 24.0; // day 0, 08:00
  int64_t anchorMs = 0;
  float timeScale = 20.f;
};

struct WeatherState
{
  FormId weatherId = 0;
  float transitionSec = 10.f;
  bool radstorm = false;
};

struct MapMarker
{
  FormId refId = 0;
  std::array<float, 3> pos = { 0, 0, 0 };
  uint32_t worldOrCell = 0x3c;
  std::string name;
  uint8_t type = 0;
  bool canTravel = true;
  bool visibleByDefault = false;
};

enum class FastTravelError : uint8_t
{
  None = 0,
  UnknownMarker,
  NotDiscovered,
  InCombat,
  OverEncumbered,
  InBuildMode,
  Disabled,
  CannotTravelHere,
  Dead,
};
const char* FastTravelErrorToString(FastTravelError e) noexcept;

struct MapSettings
{
  float discoveryRadius = 1000.f; // game units
  // F26 default: travel only to discovered markers (Q-15)
  enum class FastTravelMode : uint8_t
  {
    Off,
    DiscoveredOnly,
    Any,
  } fastTravel = FastTravelMode::DiscoveredOnly;
  int64_t combatCooldownMs = 30000;
};

struct FastTravelFacts
{
  bool alive = true;
  bool inBuildMode = false;
  int64_t lastCombatMs = -1000000000;
  float carriedWeight = 0.f;
  float carryWeight = 1e9f;
  int64_t nowMs = 0;
};

class MapService
{
public:
  explicit MapService(MapSettings settings = {});

  void AddMarker(MapMarker m);
  const MapMarker* Find(FormId refId) const;
  const std::map<FormId, MapMarker>& All() const { return markers; }

  // Markers newly discovered by an actor at `pos` (added to `discovered`).
  std::vector<FormId> Discover(const std::array<float, 3>& pos,
                               uint32_t worldOrCell,
                               std::set<FormId>& discovered) const;
  FastTravelError CanFastTravel(FormId markerRefId,
                                const std::set<FormId>& discovered,
                                const FastTravelFacts& facts) const;

  MapSettings settings;

private:
  std::map<FormId, MapMarker> markers;
};

}
