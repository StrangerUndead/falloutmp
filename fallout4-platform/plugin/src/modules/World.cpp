// F25 time and weather: one server clock and one weather for everyone.
//
// setGameTime / setTimeScale write the Calendar's globals (GameDaysPassed,
// GameHour, GameDay/GameMonth/GameYear, TimeScale). Calendar::GetSingleton
// holds the TESGlobal pointers, so no form ids are looked up. The plugin
// keeps the last server clock and projects it at the time scale; when the
// game's clock drifts away from it (a save loaded, waiting or sleeping, a
// pause, a vanilla time skip), the projection is written again: checked
// every second, rewritten past 0.05 game hours of drift.
//
// forceWeather: Sky::ForceWeather(weather, override) for an instant change.
// With a transition, Papyrus Weather.SetActive(abOverride=true,
// abAccelerate=false) lets the engine blend at the weather's own transition
// speed (the duration itself can't be set). When the engine shows another
// weather outdoors (a save loaded, a region change), the server weather is
// forced again.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <utility>

namespace fmp::modules {

namespace {
constexpr double kClockCheckMs = 1000.0;
constexpr double kMaxDriftHours = 0.05;
constexpr double kWeatherCheckMs = 5000.0;
// After a change the weather is left alone for its transition plus this.
constexpr double kWeatherGraceMs = 10000.0;

// The server clock at the last setGameTime/setTimeScale (main thread).
struct Clock
{
  bool set = false;
  double days = 0;
  double hour = 0;
  double atMs = 0;
  double timeScale = 20;
  bool timeScaleSet = false;
};
Clock g_clock;
double g_lastClockCheckMs = 0;

struct Weather
{
  uint32_t id = 0;
  double appliedMs = 0;
  double transitionSec = 0;
};
Weather g_weather;
double g_lastWeatherCheckMs = 0;

double Wrap24(double hour)
{
  hour = std::fmod(hour, 24.0);
  return hour < 0 ? hour + 24.0 : hour;
}

bool IsLeap(int year)
{
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int DaysIn(int month0, int year)
{
  constexpr int kDays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  return month0 == 1 && IsLeap(year) ? 29 : kDays[month0];
}

// Moves the calendar date by whole days. The engine moves it when the hour
// passes midnight; a jump of the clock has to do it here.
// GameMonth is 0-based (Calendar::Months), GameDay 1-based.
void ShiftDate(RE::Calendar& c, long long days)
{
  if (!days || !c.gameYear || !c.gameMonth || !c.gameDay ||
      std::llabs(days) > 3660000) {
    return;
  }
  int year = static_cast<int>(c.gameYear->value);
  int month = std::clamp(static_cast<int>(c.gameMonth->value), 0, 11);
  int day = std::clamp(static_cast<int>(c.gameDay->value), 1,
                       DaysIn(month, year));
  while (days > 0) {
    const int left = DaysIn(month, year) - day;
    if (days <= left) {
      day += static_cast<int>(days);
      days = 0;
    } else {
      days -= left + 1;
      day = 1;
      if (++month == 12) {
        month = 0;
        ++year;
      }
    }
  }
  while (days < 0) {
    if (-days < day) {
      day += static_cast<int>(days);
      days = 0;
    } else {
      days += day;
      if (--month < 0) {
        month = 11;
        --year;
      }
      day = DaysIn(month, year);
    }
  }
  c.gameYear->value = static_cast<float>(year);
  c.gameMonth->value = static_cast<float>(month);
  c.gameDay->value = static_cast<float>(day);
}

void WriteClock(double days, double hour)
{
  auto c = RE::Calendar::GetSingleton();
  if (!c || !c->gameDaysPassed || !c->gameHour) {
    return;
  }
  hour = Wrap24(hour);
  const double oldDays = c->gameDaysPassed->value;
  const double oldHour = c->gameHour->value;
  // GameDaysPassed at the last midnight: their difference is the number of
  // days the date moves, whatever hour the game started at.
  const auto dateDelta =
    std::llround((days - hour / 24.0) - (oldDays - oldHour / 24.0));
  c->gameDaysPassed->value = static_cast<float>(days);
  c->gameHour->value = static_cast<float>(hour);
  // [verify] rawDaysPassed is the engine's day accumulator behind
  // GameDaysPassed and midnightsPassed counts whole days; without them the
  // next update would restore the old clock or replay the skipped midnights
  c->rawDaysPassed = static_cast<float>(days);
  c->midnightsPassed = static_cast<std::uint32_t>(std::floor(days));
  ShiftDate(*c, dateDelta);
}

// The server clock now, at the time scale.
std::pair<double, double> Projected(double nowMs)
{
  const double gameSec = (nowMs - g_clock.atMs) / 1000.0 * g_clock.timeScale;
  return { g_clock.days + gameSec / 86400.0,
           Wrap24(g_clock.hour + gameSec / 3600.0) };
}

void SetGameTime(Platform& p, double days, double hour)
{
  if (!std::isfinite(days) || !std::isfinite(hour) || days < 0) {
    p.Log("warn", std::format("setGameTime: invalid {} {}", days, hour));
    return;
  }
  if (!g_clock.timeScaleSet) {
    auto c = RE::Calendar::GetSingleton();
    if (c && c->timeScale) {
      g_clock.timeScale = c->timeScale->value;
    }
  }
  g_clock.set = true;
  g_clock.days = days;
  g_clock.hour = Wrap24(hour);
  g_clock.atMs = p.NowMs();
  WriteClock(g_clock.days, g_clock.hour);
}

void SetTimeScale(Platform& p, double scale)
{
  if (!std::isfinite(scale) || scale < 0) {
    p.Log("warn", std::format("setTimeScale: invalid {}", scale));
    return;
  }
  // The projection continues from now at the new scale.
  const auto now = p.NowMs();
  if (g_clock.set) {
    auto [days, hour] = Projected(now);
    g_clock.days = days;
    g_clock.hour = hour;
    g_clock.atMs = now;
  }
  g_clock.timeScale = scale;
  g_clock.timeScaleSet = true;
  auto c = RE::Calendar::GetSingleton();
  if (c && c->timeScale) {
    c->timeScale->value = static_cast<float>(scale);
  }
}

// Keeps the game's clock on the server's.
void CheckClock(Platform& p, double now)
{
  if (!g_clock.set) {
    return;
  }
  auto c = RE::Calendar::GetSingleton();
  if (!c || !c->gameDaysPassed) {
    return;
  }
  if (g_clock.timeScaleSet && c->timeScale &&
      c->timeScale->value != static_cast<float>(g_clock.timeScale)) {
    c->timeScale->value = static_cast<float>(g_clock.timeScale);
  }
  auto [days, hour] = Projected(now);
  const double driftHours =
    std::abs(static_cast<double>(c->gameDaysPassed->value) - days) * 24.0;
  if (driftHours > kMaxDriftHours) {
    WriteClock(days, hour);
    if (driftHours > 1.0) {
      p.Log("info", std::format("Game clock corrected by {:.1f} h", driftHours));
    }
  }
}

void ApplyWeather(RE::TESWeather* weather, double transitionSec)
{
  auto sky = RE::Sky::GetSingleton();
  if (transitionSec > 0 &&
      papyrus::CallMethod(weather, "Weather", "SetActive", nullptr, true,
                          false)) {
    return;
  }
  if (sky) {
    sky->ForceWeather(weather, true);
  }
}

void ForceWeather(Platform& p, uint32_t id, double transitionSec)
{
  if (!id) {
    // No server weather: the engine picks its own again.
    g_weather = {};
    if (auto sky = RE::Sky::GetSingleton()) {
      sky->ReleaseWeatherOverride();
    }
    return;
  }
  auto weather = game::Form<RE::TESWeather>(id);
  if (!weather) {
    p.Log("warn", std::format("forceWeather: unknown weather {:X}", id));
    return;
  }
  if (!std::isfinite(transitionSec) || transitionSec < 0) {
    transitionSec = 0;
  }
  g_weather = { id, p.NowMs(), transitionSec };
  ApplyWeather(weather, transitionSec);
}

// Forces the server weather again when the engine shows another one outdoors.
void CheckWeather(Platform& p, double now)
{
  if (!g_weather.id ||
      now - g_weather.appliedMs <
        g_weather.transitionSec * 1000.0 + kWeatherGraceMs) {
    return;
  }
  auto sky = RE::Sky::GetSingleton();
  auto weather = game::Form<RE::TESWeather>(g_weather.id);
  if (!sky || !weather || sky->mode.get() != RE::Sky::Mode::kFull) {
    return; // interiors keep their lighting
  }
  // [verify] Weather.SetActive/ForceWeather leave the weather in
  // currentWeather or overrideWeather
  if (sky->currentWeather == weather || sky->overrideWeather == weather) {
    return;
  }
  sky->ForceWeather(weather, true);
  g_weather.appliedMs = now;
  g_weather.transitionSec = 0;
  p.Log("info", std::format("Weather {:X} forced again", g_weather.id));
}

void OnFrame(Platform& p)
{
  if (game::Loading()) {
    return;
  }
  const auto now = p.NowMs();
  if (now - g_lastClockCheckMs >= kClockCheckMs) {
    g_lastClockCheckMs = now;
    CheckClock(p, now);
  }
  if (now - g_lastWeatherCheckMs >= kWeatherCheckMs) {
    g_lastWeatherCheckMs = now;
    CheckWeather(p, now);
  }
}
}

void InstallWorld(Platform& p)
{
  p.RegisterNative("setGameTime", [&p](const Json& a) -> Json {
    SetGameTime(p, a.at(0).get<double>(), a.at(1).get<double>());
    return nullptr;
  });
  p.RegisterNative("setTimeScale", [&p](const Json& a) -> Json {
    SetTimeScale(p, a.at(0).get<double>());
    return nullptr;
  });
  p.RegisterNative("forceWeather", [&p](const Json& a) -> Json {
    const auto& id = a.at(0);
    ForceWeather(p, id.is_number() ? id.get<uint32_t>() : 0,
                 a.at(1).get<double>());
    return nullptr;
  });
  p.OnFrame([&p](float) { OnFrame(p); });
}

}
