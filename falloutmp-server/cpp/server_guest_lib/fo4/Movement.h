#pragma once
// Server-side movement validation for Fallout 4 (F01 §4.6).
//
// The owner's client reports its transform about 10 times a second. Before a
// sample is accepted and relayed, it is checked against the server's last
// accepted position with a speed model built from what the server knows
// (power armor, jetpack, encumbrance, speed multiplier), not from what the
// client claims. Out-of-model samples are dropped and scored; a sustained
// violation teleports the actor back to its last good position.
//
// The validator also keeps a short movement history per actor, used for
// lag-compensated hit validation (F09/F11).
#include "Fo4Data.h"
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>

namespace fo4 {

using ActorId = uint32_t;

// Bit order of UpdateMovementFo4.flags (F01 §4.3)
namespace MoveFlag {
enum : uint16_t
{
  Sneaking = 1 << 0,
  Sprinting = 1 << 1,
  Sighted = 1 << 2,
  WeaponDrawn = 1 << 3,
  InJump = 1 << 4,
  Swimming = 1 << 5,
  InPowerArmor = 1 << 6,
  InFurniture = 1 << 7,
  IsDead = 1 << 8,
  IsBlocking = 1 << 9,
  Encumbered = 1 << 10,
  LightOn = 1 << 11,
  JetpackActive = 1 << 12,
};
}

// Speeds in game units per second. [verify G-manual] against measured
// vanilla speeds; the defaults are generous upper bounds.
struct MovementSettings
{
  float walkSpeed = 200.f;
  float sprintSpeed = 700.f;
  float encumberedSpeed = 200.f; // over-encumbered players can't run
  float powerArmorSprintSpeed = 600.f;
  float jumpUpSpeed = 450.f; // vertical speed of a jump
  float jetpackUpSpeed = 900.f;
  float terminalFallSpeed = 6000.f;
  // Off until the speeds are measured in game: samples are not checked
  // against the speed model (sequence, death and cell rules still apply).
  bool enforceSpeed = false;
  float speedTolerance = 1.25f; // multiplier on every limit
  float distanceSlack = 96.f;   // units allowed on top of the limit
  int64_t jitterMs = 150;       // packet bunching allowance
  int64_t maxDtMs = 2000;       // longer gaps don't buy more distance
  // Sustained-speed check over the recent history (catches speeds that
  // hide inside the per-sample jitter allowance).
  int64_t windowMs = 1000;
  int64_t minWindowMs = 300;
  float violationThreshold = 3.f; // score that triggers a correction
  float scoreDecayPerSec = 0.5f;
  float maxScorePerSample = 2.f;
  int64_t teleportGraceMs = 2000; // in-flight samples after a server move
  size_t historySize = 32;        // ~3 s at 10 Hz
  float idleSpeedBelow = 10.f;    // classification for power armor drain
  float walkSpeedBelow = 260.f;
  // SpeedMult actor value form id for chems/perks that change speed;
  // 0 = not used. [verify D-real: the AVIF id of SpeedMult]
  uint32_t speedMultAvId = 0;
};

// What the server knows about the actor right now.
struct MovementContext
{
  bool alive = true;
  bool inPowerArmor = false;
  bool jetpackAllowed = false; // PA worn, jetpack mod, powered, AP left
  bool encumbered = false;
  float speedMult = 1.f; // SpeedMult actor value / 100
};

struct MovementSample
{
  uint16_t seq = 0;
  uint32_t worldOrCell = 0;
  std::array<float, 3> pos = { 0, 0, 0 };
  float yaw = 0.f;
  uint16_t flags = 0;
};

enum class MovementVerdict : uint8_t
{
  Accepted,
  Stale,    // old or duplicate sequence number
  InFlight, // sent before a server-side move; dropped silently
  Dead,
  Rejected, // outside the model; scored, not applied
  Correct,  // teleport the actor back to the baseline
};
const char* MovementVerdictToString(MovementVerdict v) noexcept;

enum class MovementClass : uint8_t
{
  Idle,
  Walk,
  Run,
  Sprint,
  Jetpack,
};

struct MovementResult
{
  MovementVerdict verdict = MovementVerdict::Accepted;
  const char* reason = "";
  float dtSec = 0.f;           // since the previous accepted sample
  float horizontalSpeed = 0.f; // measured, units/s
  float verticalSpeed = 0.f;
  MovementClass movement = MovementClass::Idle;
  float score = 0.f;
  // Where to put the actor on Correct
  std::array<float, 3> correctionPos = { 0, 0, 0 };
  uint32_t correctionWorldOrCell = 0;
};

struct MovementHistoryEntry
{
  int64_t serverMs = 0;
  std::array<float, 3> pos = { 0, 0, 0 };
  uint32_t worldOrCell = 0;
};

class MovementValidator
{
public:
  explicit MovementValidator(MovementSettings settings = {});

  // serverPos/serverCell: where the server has the actor now. If it differs
  // from the last accepted sample (door, respawn, teleport), the validator
  // re-bases there and opens the in-flight grace window.
  MovementResult Validate(ActorId actor, const MovementSample& sample,
                          const MovementContext& ctx,
                          const std::array<float, 3>& serverPos,
                          uint32_t serverCell, int64_t nowMs);

  // Accept the sample regardless (gamemode overrode a correction).
  void ForceAccept(ActorId actor, const MovementSample& sample, int64_t nowMs);

  // Lag compensation: position at a server time (linear interpolation of
  // the history), or nullopt without history.
  std::optional<std::array<float, 3>> PositionAt(ActorId actor,
                                                 int64_t serverMs) const;
  const std::deque<MovementHistoryEntry>* History(ActorId actor) const;
  uint16_t LastFlags(ActorId actor) const;
  void Forget(ActorId actor);

  uint64_t violationCount = 0; // movement_violation_total
  uint64_t correctionCount = 0;
  MovementSettings settings;

private:
  struct State
  {
    bool initialized = false;
    uint16_t lastSeq = 0;
    std::array<float, 3> pos = { 0, 0, 0 };
    uint32_t worldOrCell = 0;
    int64_t lastMs = 0;
    int64_t graceUntilMs = 0;
    float score = 0.f;
    uint16_t flags = 0;
    std::deque<MovementHistoryEntry> history;
  };
  void Rebase(State& s, const std::array<float, 3>& pos, uint32_t cell,
              int64_t nowMs, bool grace);
  void Accept(State& s, const MovementSample& sample, int64_t nowMs);
  std::map<ActorId, State> states;
};

}
