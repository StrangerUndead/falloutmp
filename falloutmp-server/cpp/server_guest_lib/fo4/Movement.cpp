#include "Movement.h"
#include <algorithm>
#include <cmath>

namespace fo4 {

const char* MovementVerdictToString(MovementVerdict v) noexcept
{
  switch (v) {
    case MovementVerdict::Accepted:
      return "Accepted";
    case MovementVerdict::Stale:
      return "Stale";
    case MovementVerdict::InFlight:
      return "InFlight";
    case MovementVerdict::Dead:
      return "Dead";
    case MovementVerdict::Rejected:
      return "Rejected";
    case MovementVerdict::Correct:
      return "Correct";
  }
  return "Unknown";
}

MovementValidator::MovementValidator(MovementSettings s)
  : settings(s)
{
}

namespace {
float Horizontal(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
  float dx = a[0] - b[0], dy = a[1] - b[1];
  return std::sqrt(dx * dx + dy * dy);
}

bool Near(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
  return std::fabs(a[0] - b[0]) < 1.f && std::fabs(a[1] - b[1]) < 1.f &&
    std::fabs(a[2] - b[2]) < 1.f;
}

// u16 sequence comparison with wrap-around
bool Newer(uint16_t a, uint16_t b)
{
  return static_cast<int16_t>(static_cast<uint16_t>(a - b)) > 0;
}
}

void MovementValidator::Rebase(State& s, const std::array<float, 3>& pos,
                               uint32_t cell, int64_t nowMs, bool grace)
{
  s.pos = pos;
  s.worldOrCell = cell;
  s.lastMs = nowMs;
  s.score = 0.f;
  if (grace) {
    s.graceUntilMs = nowMs + settings.teleportGraceMs;
  }
}

void MovementValidator::Accept(State& s, const MovementSample& sample,
                               int64_t nowMs)
{
  s.pos = sample.pos;
  s.worldOrCell = sample.worldOrCell;
  s.lastMs = nowMs;
  s.flags = sample.flags;
  s.graceUntilMs = 0;
  s.history.push_back({ nowMs, sample.pos, sample.worldOrCell });
  while (s.history.size() > settings.historySize) {
    s.history.pop_front();
  }
}

MovementResult MovementValidator::Validate(
  ActorId actor, const MovementSample& sample, const MovementContext& ctx,
  const std::array<float, 3>& serverPos, uint32_t serverCell, int64_t nowMs)
{
  MovementResult r;
  auto& s = states[actor];
  if (!s.initialized) {
    s.initialized = true;
    s.lastSeq = static_cast<uint16_t>(sample.seq - 1);
    Rebase(s, serverPos, serverCell, nowMs, false);
  } else if (!Near(s.pos, serverPos) || s.worldOrCell != serverCell) {
    // Moved by the server since the last sample (door, respawn, teleport)
    Rebase(s, serverPos, serverCell, nowMs, true);
    s.history.clear();
  }

  if (!Newer(sample.seq, s.lastSeq)) {
    r.verdict = MovementVerdict::Stale;
    r.reason = "stale sequence";
    return r;
  }
  s.lastSeq = sample.seq;

  if (!ctx.alive) {
    r.verdict = MovementVerdict::Dead;
    r.reason = "dead";
    return r;
  }

  bool inGrace = nowMs < s.graceUntilMs;
  int64_t dtMs = std::clamp<int64_t>(nowMs - s.lastMs, 0, settings.maxDtMs);
  float dt = static_cast<float>(dtMs) / 1000.f;
  float dtAllowed =
    static_cast<float>(std::min(dtMs + settings.jitterMs, settings.maxDtMs)) /
    1000.f;
  r.dtSec = dt;

  // Score decays with time
  s.score =
    std::max(0.f, s.score - settings.scoreDecayPerSec * std::max(dt, 0.1f));

  auto violate = [&](const char* reason, float excess,
                     bool immediate = false) {
    if (inGrace) {
      r.verdict = MovementVerdict::InFlight;
      r.reason = "in flight after a server move";
      return r;
    }
    violationCount++;
    s.score += std::clamp(excess, 0.5f, settings.maxScorePerSample);
    r.score = s.score;
    r.reason = reason;
    if (immediate || s.score >= settings.violationThreshold) {
      r.verdict = MovementVerdict::Correct;
      r.correctionPos = s.pos;
      r.correctionWorldOrCell = s.worldOrCell;
      correctionCount++;
      Rebase(s, s.pos, s.worldOrCell, nowMs, true);
    } else {
      r.verdict = MovementVerdict::Rejected;
    }
    return r;
  };

  if (sample.worldOrCell != s.worldOrCell) {
    // Cell changes only happen through the server (doors, teleports)
    return violate("cell change", settings.maxScorePerSample, true);
  }

  // Server-known speed limit
  float limit = ctx.encumbered ? settings.encumberedSpeed
    : ctx.inPowerArmor         ? settings.powerArmorSprintSpeed
                               : settings.sprintSpeed;
  limit *= std::max(ctx.speedMult, 0.1f) * settings.speedTolerance;
  float allowedH = limit * dtAllowed + settings.distanceSlack;

  float h = Horizontal(sample.pos, s.pos);
  float dz = sample.pos[2] - s.pos[2];
  r.horizontalSpeed = dt > 0.f ? h / dt : 0.f;
  r.verticalSpeed = dt > 0.f ? dz / dt : 0.f;

  // Climbing: slopes and stairs rise no faster than walking speed; jumps
  // and the jetpack add their own vertical allowance.
  float upSpeed = settings.jumpUpSpeed;
  if (ctx.jetpackAllowed) {
    upSpeed += settings.jetpackUpSpeed;
  }
  upSpeed *= std::max(ctx.speedMult, 0.1f);
  float allowedUp =
    upSpeed * settings.speedTolerance * dtAllowed + settings.distanceSlack;
  float allowedDown =
    settings.terminalFallSpeed * dtAllowed + settings.distanceSlack;

  // Speed limits are off by default until they are measured in game
  // (fo4.movement.enforceSpeed). Sequence, death and cell rules still apply.
  if (settings.enforceSpeed && h > allowedH) {
    return violate("too fast", h / allowedH);
  }
  if (settings.enforceSpeed && dz > allowedUp) {
    return violate("rising too fast", dz / allowedUp);
  }
  if (settings.enforceSpeed && -dz > allowedDown) {
    return violate("falling too fast", -dz / allowedDown);
  }

  // Sustained speed over the window: the jitter allowance is granted once
  // per window, not once per sample.
  for (auto& e : s.history) {
    if (!settings.enforceSpeed) {
      break;
    }
    int64_t elapsedMs = nowMs - e.serverMs;
    if (elapsedMs > settings.windowMs || e.worldOrCell != s.worldOrCell) {
      continue;
    }
    if (elapsedMs < settings.minWindowMs) {
      break; // history is ordered; the rest is newer
    }
    float windowSec =
      static_cast<float>(elapsedMs + settings.jitterMs) / 1000.f;
    float wh = Horizontal(sample.pos, e.pos);
    float wAllowedH = limit * windowSec + settings.distanceSlack;
    if (wh > wAllowedH) {
      return violate("too fast (sustained)", wh / wAllowedH);
    }
    float wdz = sample.pos[2] - e.pos[2];
    float wAllowedUp =
      upSpeed * settings.speedTolerance * windowSec + settings.distanceSlack;
    if (wdz > wAllowedUp) {
      return violate("rising too fast (sustained)", wdz / wAllowedUp);
    }
    break; // the oldest sample inside the window is enough
  }

  // Classification for power armor core drain (measured, not claimed)
  bool jetpackUsed = ctx.jetpackAllowed &&
    ((sample.flags & MoveFlag::JetpackActive) ||
     r.verticalSpeed > settings.jumpUpSpeed);
  if (jetpackUsed) {
    r.movement = MovementClass::Jetpack;
  } else if (r.horizontalSpeed < settings.idleSpeedBelow) {
    r.movement = MovementClass::Idle;
  } else if (r.horizontalSpeed < settings.walkSpeedBelow) {
    r.movement = MovementClass::Walk;
  } else if (sample.flags & MoveFlag::Sprinting) {
    r.movement = MovementClass::Sprint;
  } else {
    r.movement = MovementClass::Run;
  }

  Accept(s, sample, nowMs);
  r.verdict = MovementVerdict::Accepted;
  r.score = s.score;
  return r;
}

void MovementValidator::ForceAccept(ActorId actor,
                                    const MovementSample& sample,
                                    int64_t nowMs)
{
  auto& s = states[actor];
  s.initialized = true;
  s.lastSeq = sample.seq;
  s.score = 0.f;
  Accept(s, sample, nowMs);
}

std::optional<std::array<float, 3>> MovementValidator::PositionAt(
  ActorId actor, int64_t serverMs) const
{
  auto it = states.find(actor);
  if (it == states.end() || it->second.history.empty()) {
    return std::nullopt;
  }
  auto& h = it->second.history;
  if (serverMs <= h.front().serverMs) {
    return h.front().pos;
  }
  if (serverMs >= h.back().serverMs) {
    return h.back().pos;
  }
  for (size_t i = 1; i < h.size(); ++i) {
    if (h[i].serverMs >= serverMs) {
      auto& a = h[i - 1];
      auto& b = h[i];
      if (a.worldOrCell != b.worldOrCell) {
        return b.pos;
      }
      float t = static_cast<float>(serverMs - a.serverMs) /
        static_cast<float>(std::max<int64_t>(1, b.serverMs - a.serverMs));
      return std::array<float, 3>{ a.pos[0] + (b.pos[0] - a.pos[0]) * t,
                                   a.pos[1] + (b.pos[1] - a.pos[1]) * t,
                                   a.pos[2] + (b.pos[2] - a.pos[2]) * t };
    }
  }
  return h.back().pos;
}

const std::deque<MovementHistoryEntry>* MovementValidator::History(
  ActorId actor) const
{
  auto it = states.find(actor);
  return it == states.end() ? nullptr : &it->second.history;
}

uint16_t MovementValidator::LastFlags(ActorId actor) const
{
  auto it = states.find(actor);
  return it == states.end() ? 0 : it->second.flags;
}

void MovementValidator::Forget(ActorId actor)
{
  states.erase(actor);
}

}
