#include "WorldServices.h"
#include <cmath>
#include <nlohmann/json.hpp>

namespace fo4 {

double WorldClock::GameDays(int64_t nowMs) const
{
  double realSec = static_cast<double>(nowMs - anchorMs) / 1000.0;
  return daysAtAnchor + realSec * timeScale / 86400.0;
}

float WorldClock::GameHour(int64_t nowMs) const
{
  double d = GameDays(nowMs);
  return static_cast<float>((d - std::floor(d)) * 24.0);
}

void WorldClock::SetGameDays(double days, int64_t nowMs)
{
  daysAtAnchor = days;
  anchorMs = nowMs;
}

void WorldClock::SetTimeScale(float scale, int64_t nowMs)
{
  daysAtAnchor = GameDays(nowMs); // re-anchor so time does not jump
  anchorMs = nowMs;
  timeScale = scale < 0.f ? 0.f : scale;
}

nlohmann::json WorldClock::ToJson(int64_t nowMs) const
{
  return { { "gameDays", GameDays(nowMs) }, { "timeScale", timeScale } };
}

void WorldClock::LoadJson(const nlohmann::json& j, int64_t nowMs)
{
  if (!j.is_object()) {
    return;
  }
  timeScale = j.value("timeScale", 20.f);
  SetGameDays(j.value("gameDays", 8.0 / 24.0), nowMs);
}

const char* FastTravelErrorToString(FastTravelError e) noexcept
{
  switch (e) {
#define C(x)                                                                  \
  case FastTravelError::x:                                                    \
    return #x;
    C(None)
    C(UnknownMarker)
    C(NotDiscovered)
    C(InCombat)
    C(OverEncumbered)
    C(InBuildMode)
    C(Disabled)
    C(CannotTravelHere)
    C(Dead)
#undef C
  }
  return "Unknown";
}

MapService::MapService(MapSettings s)
  : settings(s)
{
}

void MapService::AddMarker(MapMarker m)
{
  FormId id = m.refId;
  markers[id] = std::move(m);
}

const MapMarker* MapService::Find(FormId refId) const
{
  auto it = markers.find(refId);
  return it == markers.end() ? nullptr : &it->second;
}

std::vector<FormId> MapService::Discover(const std::array<float, 3>& pos,
                                         uint32_t worldOrCell,
                                         std::set<FormId>& discovered) const
{
  std::vector<FormId> res;
  float r2 = settings.discoveryRadius * settings.discoveryRadius;
  for (auto& [id, m] : markers) {
    if (discovered.count(id) || (m.worldOrCell && worldOrCell &&
                                 m.worldOrCell != worldOrCell)) {
      continue;
    }
    float dx = m.pos[0] - pos[0], dy = m.pos[1] - pos[1],
          dz = m.pos[2] - pos[2];
    if (dx * dx + dy * dy + dz * dz <= r2) {
      discovered.insert(id);
      res.push_back(id);
    }
  }
  return res;
}

FastTravelError MapService::CanFastTravel(FormId markerRefId,
                                          const std::set<FormId>& discovered,
                                          const FastTravelFacts& f) const
{
  if (settings.fastTravel == MapSettings::FastTravelMode::Off) {
    return FastTravelError::Disabled;
  }
  auto m = Find(markerRefId);
  if (!m) {
    return FastTravelError::UnknownMarker;
  }
  if (!m->canTravel) {
    return FastTravelError::CannotTravelHere;
  }
  if (!f.alive) {
    return FastTravelError::Dead;
  }
  if (settings.fastTravel == MapSettings::FastTravelMode::DiscoveredOnly &&
      !discovered.count(markerRefId) && !m->visibleByDefault) {
    return FastTravelError::NotDiscovered;
  }
  if (f.nowMs - f.lastCombatMs < settings.combatCooldownMs) {
    return FastTravelError::InCombat;
  }
  if (f.carriedWeight > f.carryWeight) {
    return FastTravelError::OverEncumbered;
  }
  if (f.inBuildMode) {
    return FastTravelError::InBuildMode;
  }
  return FastTravelError::None;
}

}
