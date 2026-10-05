#include "fo4/Movement.h"
#include <catch2/catch_all.hpp>

using namespace fo4;

namespace {
constexpr ActorId kActor = 0xFF000001;
constexpr uint32_t kWorld = 0x3c;

struct Walker
{
  MovementValidator v;
  std::array<float, 3> serverPos = { 0, 0, 0 };
  uint32_t serverCell = kWorld;
  int64_t now = 1000;
  uint16_t seq = 0;
  MovementContext ctx;

  // Sends a sample dtMs after the previous one; applies accepted samples to
  // the "server position" like Fo4Server does.
  MovementResult Step(std::array<float, 3> pos, int64_t dtMs = 100,
                      uint16_t flags = 0, uint32_t cell = kWorld)
  {
    now += dtMs;
    MovementSample s{ ++seq, cell, pos, 0.f, flags };
    auto r = v.Validate(kActor, s, ctx, serverPos, serverCell, now);
    if (r.verdict == MovementVerdict::Accepted) {
      serverPos = pos;
    } else if (r.verdict == MovementVerdict::Correct) {
      serverPos = r.correctionPos;
      serverCell = r.correctionWorldOrCell;
    }
    return r;
  }
};
}

TEST_CASE("Movement: normal running and sprinting is accepted",
          "[fo4][Movement]")
{
  Walker w;
  float x = 0;
  for (int i = 0; i < 50; ++i) {
    x += 60.f; // 600 u/s sprint
    REQUIRE(w.Step({ x, 0, 0 }, 100, MoveFlag::Sprinting).verdict ==
            MovementVerdict::Accepted);
  }
  auto r = w.Step({ x + 60.f, 0, 0 }, 100, MoveFlag::Sprinting);
  REQUIRE(r.movement == MovementClass::Sprint);
  REQUIRE(r.horizontalSpeed == Catch::Approx(600.f).margin(1));
  // Standing still
  REQUIRE(w.Step({ x + 60.f, 0, 0 }).movement == MovementClass::Idle);
  REQUIRE(w.v.violationCount == 0);
}

TEST_CASE("Movement: packet bunching is absorbed by the jitter allowance",
          "[fo4][Movement]")
{
  Walker w;
  w.Step({ 0, 0, 0 });
  // Three samples of 60 u arrive within 5 ms after a 300 ms gap
  REQUIRE(w.Step({ 60, 0, 0 }, 300).verdict == MovementVerdict::Accepted);
  REQUIRE(w.Step({ 120, 0, 0 }, 2).verdict == MovementVerdict::Accepted);
  REQUIRE(w.Step({ 180, 0, 0 }, 2).verdict == MovementVerdict::Accepted);
}

TEST_CASE("Movement: speed hacks are rejected, then corrected",
          "[fo4][Movement]")
{
  Walker w;
  w.Step({ 0, 0, 0 });
  // 3x sprint speed, sustained. Each step fits the per-sample jitter
  // allowance; the sustained window catches it.
  MovementResult r;
  float x = 0.f, lastGood = 0.f;
  int rejected = 0;
  for (int i = 0; i < 30; ++i) {
    x += 210.f;
    r = w.Step({ x, 0, 0 });
    if (r.verdict == MovementVerdict::Accepted) {
      lastGood = x;
    } else if (r.verdict == MovementVerdict::Rejected) {
      rejected++;
    } else {
      break;
    }
  }
  REQUIRE(r.verdict == MovementVerdict::Correct);
  REQUIRE(rejected >= 1);
  REQUIRE(lastGood < 1000.f);
  REQUIRE(r.correctionPos[0] == lastGood);
  REQUIRE(w.v.correctionCount == 1);
  // Samples still in flight from before the correction are dropped
  REQUIRE(w.Step({ x + 210.f, 0, 0 }).verdict == MovementVerdict::InFlight);
  // The client snapped back and moves normally again
  REQUIRE(w.Step({ lastGood + 50.f, 0, 0 }).verdict ==
          MovementVerdict::Accepted);
}

TEST_CASE("Movement: teleporting yourself across the map is corrected",
          "[fo4][Movement]")
{
  Walker w;
  w.Step({ 0, 0, 0 });
  auto r = w.Step({ 50000, 0, 0 });
  REQUIRE(r.verdict == MovementVerdict::Rejected);
  r = w.Step({ 50000, 0, 0 });
  REQUIRE(r.verdict == MovementVerdict::Correct);
  REQUIRE(r.correctionPos[0] == 0.f);
}

TEST_CASE("Movement: cell changes only happen through the server",
          "[fo4][Movement]")
{
  Walker w;
  w.Step({ 0, 0, 0 });
  auto r = w.Step({ 0, 0, 0 }, 100, 0, 0x1234);
  REQUIRE(r.verdict == MovementVerdict::Correct); // corrected at once
  REQUIRE(std::string(r.reason) == "cell change");

  // A server-side move (door) re-bases the validator; samples from the old
  // place are in flight and dropped silently, the new place is accepted.
  Walker d;
  d.Step({ 0, 0, 0 });
  d.serverPos = { 10, 10, 0 };
  d.serverCell = 0x1234;
  REQUIRE(d.Step({ 30, 0, 0 }).verdict == MovementVerdict::InFlight);
  REQUIRE(d.Step({ 12, 10, 0 }, 100, 0, 0x1234).verdict ==
          MovementVerdict::Accepted);
  REQUIRE(d.v.violationCount == 0);
}

TEST_CASE("Movement: flying needs a working jetpack", "[fo4][Movement]")
{
  Walker w;
  w.Step({ 0, 0, 0 });
  // A normal jump
  REQUIRE(w.Step({ 0, 0, 40 }, 100, MoveFlag::InJump).verdict ==
          MovementVerdict::Accepted);
  // Rising 120 u per 100 ms for a second
  int bad = 0;
  float z = 40;
  for (int i = 0; i < 10; ++i) {
    z += 120.f;
    auto r = w.Step({ 0, 0, z });
    if (r.verdict != MovementVerdict::Accepted) {
      bad++;
    }
    if (r.verdict == MovementVerdict::Correct) {
      break;
    }
  }
  REQUIRE(bad > 0);

  Walker pa;
  pa.ctx.inPowerArmor = true;
  pa.ctx.jetpackAllowed = true;
  pa.Step({ 0, 0, 0 });
  z = 0;
  for (int i = 0; i < 10; ++i) {
    z += 120.f;
    auto r = pa.Step({ 0, 0, z }, 100, MoveFlag::JetpackActive);
    REQUIRE(r.verdict == MovementVerdict::Accepted);
    REQUIRE(r.movement == MovementClass::Jetpack);
  }
  // Falling is always fine
  REQUIRE(pa.Step({ 0, 0, 0 }, 300).verdict == MovementVerdict::Accepted);
}

TEST_CASE("Movement: over-encumbered players can't run", "[fo4][Movement]")
{
  Walker w;
  w.ctx.encumbered = true;
  w.Step({ 0, 0, 0 });
  bool flagged = false;
  float x = 0;
  for (int i = 0; i < 20 && !flagged; ++i) {
    x += 50.f; // 500 u/s
    flagged = w.Step({ x, 0, 0 }).verdict != MovementVerdict::Accepted;
  }
  REQUIRE(flagged);
}

TEST_CASE("Movement: sequence numbers, wrap-around and dead actors",
          "[fo4][Movement]")
{
  MovementValidator v;
  MovementContext ctx;
  std::array<float, 3> p{ 0, 0, 0 };
  MovementSample s{ 65534, kWorld, p, 0, 0 };
  REQUIRE(v.Validate(kActor, s, ctx, p, kWorld, 100).verdict ==
          MovementVerdict::Accepted);
  s.seq = 65535;
  REQUIRE(v.Validate(kActor, s, ctx, p, kWorld, 200).verdict ==
          MovementVerdict::Accepted);
  s.seq = 1; // wrapped
  REQUIRE(v.Validate(kActor, s, ctx, p, kWorld, 300).verdict ==
          MovementVerdict::Accepted);
  REQUIRE(v.Validate(kActor, s, ctx, p, kWorld, 400).verdict ==
          MovementVerdict::Stale);
  s.seq = 65535; // old
  REQUIRE(v.Validate(kActor, s, ctx, p, kWorld, 500).verdict ==
          MovementVerdict::Stale);
  ctx.alive = false;
  s.seq = 2;
  REQUIRE(v.Validate(kActor, s, ctx, p, kWorld, 600).verdict ==
          MovementVerdict::Dead);
}

TEST_CASE("Movement: history gives positions for lag compensation",
          "[fo4][Movement]")
{
  Walker w;
  REQUIRE(!w.v.PositionAt(kActor, 0));
  w.Step({ 0, 0, 0 });   // t = 1100
  w.Step({ 50, 0, 0 });  // t = 1200
  w.Step({ 100, 0, 0 }); // t = 1300
  REQUIRE((*w.v.PositionAt(kActor, 1250))[0] == Catch::Approx(75.f));
  REQUIRE((*w.v.PositionAt(kActor, 0))[0] == 0.f);
  REQUIRE((*w.v.PositionAt(kActor, 9999))[0] == 100.f);
  w.v.Forget(kActor);
  REQUIRE(!w.v.PositionAt(kActor, 1250));
}
