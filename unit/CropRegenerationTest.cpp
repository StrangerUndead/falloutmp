#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>

#include "CropRegeneration.h"

TEST_CASE("CropRegeneration function is working correctly",
          "[CropRegeneration]")
{
  float secondsAfterLastRegen = 1.0f;
  float attributeRate = 0.7f;
  float attributeRateMult = 100.0f;
  float oldAttributeValue = 0.6f;

  float validAttributeValueRegeneration = attributeRate / 100.0f *
    attributeRateMult / 100.0f * secondsAfterLastRegen;

  float newAttributeValue =
    oldAttributeValue + validAttributeValueRegeneration;

  REQUIRE(CropRegeneration(newAttributeValue, 1.0f, 0.7f, 100.0f, 0.6f,
                           false) == newAttributeValue);
}

TEST_CASE(
  "CropRegeneration returns oldAttributeValue if regeneration is not positive",
  "[CropRegeneration]")
{
  float oldAttributeValue = 0.6f;

  float newAttributeValue = oldAttributeValue + 0.007f;

  REQUIRE(CropRegeneration(newAttributeValue, 1.0f, 0.7f, -100.0f,
                           oldAttributeValue, false) == oldAttributeValue);
}

TEST_CASE(
  "CropRegeneration returns 1 if regeneration is enough to restore attribute",
  "[CropRegeneration]")
{
  REQUIRE(CropRegeneration(1.0f, 1.0f, 5.0f, 100.0f, 0.97f, false) == 1.0f);
}

TEST_CASE("CropRegeneration returns 1 if newAttributeValue is more then 1 "
          "when oldAttributeValue = 1",
          "[CropRegeneration]")
{
  REQUIRE(CropRegeneration(1.05f, 1.0f, 5.0f, 100.0f, 1.0f, false) == 1.0f);
}

TEST_CASE("CropRegeneration returns the correct value if newAttributeValue is "
          "too large but oldAttributeValue is equal to zero",
          "[CropRegeneration]")
{
  REQUIRE(CropRegeneration(1.0f, 1.0f, 5.0f, 100.0f, 0.0f, false) == 0.05f);
}

TEST_CASE("CropPeriodAfterLastRegen returns 0 if period < 0",
          "[CropRegeneration]")
{
  REQUIRE(CropPeriodAfterLastRegen(-1.0f) == 0.0f);
}

TEST_CASE(
  "CropPeriodAfterLastRegen returns defaultPeriod if period > maxValidPeriod",
  "[CropRegeneration]")
{
  float defaultPeriod = 1.0f;
  float maxValidPeriod = 2.0f;
  REQUIRE(CropPeriodAfterLastRegen(2.5f, maxValidPeriod, defaultPeriod) ==
          1.0f);
}

TEST_CASE("CropPeriodAfterLastRegen returns correct value if period is in "
          "0...maxValidPeriod interval",
          "[CropRegeneration]")
{
  float defaultPeriod = 1.0f;
  float maxValidPeriod = 2.0f;
  REQUIRE(CropPeriodAfterLastRegen(1.3f, maxValidPeriod, defaultPeriod) ==
          1.3f);
}
