#pragma once
// Condition encoding for ItemKey::condition (piece health, core charge).
//   0      = not tracked (treated as full, so full items stack with new ones)
//   1..999 = per-mille of maximum
//   0xFFFF = broken / empty (exactly zero)
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fo4 {

constexpr uint16_t kConditionFull = 0;
constexpr uint16_t kConditionZero = 0xFFFF;

inline float ConditionToFraction(uint16_t c) noexcept
{
  if (c == kConditionFull) {
    return 1.f;
  }
  if (c == kConditionZero) {
    return 0.f;
  }
  return std::min(1.f, static_cast<float>(c) / 1000.f);
}

inline uint16_t FractionToCondition(float f) noexcept
{
  if (!(f > 0.f)) { // also catches NaN
    return kConditionZero;
  }
  if (f >= 0.9995f) {
    return kConditionFull;
  }
  return static_cast<uint16_t>(
    std::clamp(static_cast<int>(std::lround(f * 1000.f)), 1, 999));
}

}
