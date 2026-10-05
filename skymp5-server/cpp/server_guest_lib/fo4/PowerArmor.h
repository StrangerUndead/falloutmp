#pragma once
// Power armor (F17): frames as persistent world objects with pieces and a
// fusion core, a server-driven enter/exit transition machine, core drain,
// piece damage and the frame inventory rules.
//
// The service is engine-free: positions, reach, alive/combat facts and the
// actor inventory are passed in by the caller (PartOne glue), so every rule
// is unit-testable.
#include "Condition.h"
#include "Fo4Data.h"
#include "ItemInstance.h"
#include "OmodStatResolver.h"
#include <array>
#include <functional>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>

namespace fo4 {

using ActorId = uint32_t;
using ProfileId = int32_t;

enum class PaError : uint8_t
{
  None = 0,
  NoSuchFrame,
  Occupied,
  InTransition,
  OutOfReach,
  ActorDead,
  AlreadyInPowerArmor,
  NotInPowerArmor,
  InFurniture,
  InCombat,
  OwnedByOther,
  Vetoed,
  Airborne,
  WrongNonce,
  NotAPiece,
  SlotTaken,
  PieceBroken,
  CoreAlreadyPresent,
  FrameInUse,
};
const char* PaErrorToString(PaError e) noexcept;

enum class PaPhase : uint8_t
{
  Out = 0,
  Entering,
  In,
  Exiting,
};

enum class PaMovement : uint8_t
{
  Idle = 0,
  Walk,
  Run,
  Sprint,
  Jetpack,
};

struct PowerArmorSettings
{
  bool allowStealing = false;
  bool allowInCombat = true;
  bool exitWhileFalling = false;
  bool keepOnDisconnect = false;
  int64_t transitionTimeoutMs = 10000;
  float pieceDamageMultPlayer = 1.f;
  float pieceDamageMultNpc = 3.f;
  // Fraction of a full core used per second, by movement state. A full core
  // lasts ~20 minutes of jogging in vanilla; calibrate by G-self (R8).
  std::array<float, 5> drainPerSecond = {
    0.f,               // idle
    1.f / 2400.f,      // walk
    1.f / 1200.f,      // run
    1.f / 400.f,       // sprint
    1.f / 150.f,       // jetpack
  };
  FormId fusionCoreId = 0x75FE4;
  // Keyword on torso OMODs/pieces that enable the jetpack
  FormId jetpackKeywordId = 0;
};

struct PowerArmorFrame
{
  FormId refId = 0;
  FormId baseId = 0;
  ProfileId ownerProfileId = -1; // -1 = unowned
  std::optional<ActorId> wornBy;
  bool enabled = true;
  float pos[3] = { 0, 0, 0 };
  uint32_t worldOrCell = 0;
  Fo4Inventory contents; // pieces and at most one core when not worn
};

struct WornPowerArmor
{
  FormId frameRefId = 0;
  std::array<std::optional<ItemKey>, kPowerArmorSlotCount> pieces;
  std::optional<ItemKey> core; // charge in key.condition
  PaPhase phase = PaPhase::Out;
  uint32_t nonce = 0;
  int64_t transitionStartedMs = 0;
  bool isNpc = false;
  // Drain not yet visible in the quantized core condition (signed). The
  // exact charge is CoreCharge() - pendingDrain. Not persisted.
  float pendingDrain = 0.f;

  bool Unpowered() const;
  float CoreCharge() const; // 0..1
};

// Facts about the actor, gathered by the caller at request time.
struct PaActorFacts
{
  ActorId actorId = 0;
  ProfileId profileId = -1;
  bool alive = true;
  bool inFurniture = false;
  bool inCombat = false;
  bool airborne = false;
  bool isNpc = false;
  float distanceToFrame = 0.f;
  float reach = 300.f;
};

struct PaStateSnapshot
{
  struct Piece
  {
    PowerArmorSlot slot = PowerArmorSlot::None;
    ItemKey key;
    uint8_t healthPct = 100;
  };
  PaPhase phase = PaPhase::Out;
  FormId frameRefId = 0;
  FormId frameBaseId = 0;
  std::vector<Piece> pieces;
  FormId coreBaseId = 0;
  float coreCharge = 0.f; // owner only
  bool unpowered = false;
  bool jetpackCapable = false;
};

struct PaEvents
{
  bool coreDepleted = false;
  bool coreSwapped = false;
  bool becameUnpowered = false;
  std::vector<PowerArmorSlot> piecesBroken;
};

class PowerArmorService
{
public:
  PowerArmorService(const IFo4DataSource& data, PowerArmorSettings settings);

  // ----- frames
  PowerArmorFrame& AddFrame(PowerArmorFrame frame);
  PowerArmorFrame* FindFrame(FormId refId);
  const PowerArmorFrame* FindFrame(FormId refId) const;
  const std::map<FormId, PowerArmorFrame>& Frames() const { return frames; }

  // Frame container rules (F06 hook): may `item` be put into the frame?
  PaError CanPutIntoFrame(const PowerArmorFrame& frame,
                          const ItemKey& item) const;

  // ----- transitions
  // Gamemode veto hook; return false to block (onPowerArmorEnter/Exit).
  std::function<bool(ActorId, FormId frameRefId, bool entering)> veto;

  PaError RequestEnter(const PaActorFacts& actor, FormId frameRefId,
                       uint32_t nonce, int64_t nowMs);
  // Owner/host confirms the engine pipeline finished. Moves pieces onto the
  // actor (enter) or back to the frame (exit).
  PaError Ack(ActorId actor, uint32_t nonce, Fo4Inventory& actorInventory,
              int64_t nowMs);
  PaError RequestExit(const PaActorFacts& actor, const float exitPos[3],
                      uint32_t nonce, int64_t nowMs);
  // Disconnect, death, server restart: exit immediately without an ack.
  void ForceExit(ActorId actor, const float exitPos[3],
                 Fo4Inventory& actorInventory);
  // Rolls back transitions that were not acknowledged in time. Returns the
  // actors that were rolled back.
  std::vector<ActorId> Tick(int64_t nowMs);

  const WornPowerArmor* GetWorn(ActorId actor) const;
  PaPhase GetPhase(ActorId actor) const;

  // ----- worn state
  PaEvents Drain(ActorId actor, float dtSec, PaMovement movement,
                 float durationMult, Fo4Inventory& actorInventory);
  PaEvents DamagePiece(ActorId actor, PowerArmorSlot slot, float damage);
  bool IsJetpackAllowed(ActorId actor, float actionPoints) const;
  // Physical DR and per-type resistances from intact worn pieces.
  ArmorStats GetWornProtection(ActorId actor) const;

  PaStateSnapshot Snapshot(ActorId actor) const;
  PaStateSnapshot FrameSnapshot(FormId frameRefId) const;

  // ----- persistence
  nlohmann::json FrameToJson(const PowerArmorFrame& f) const;
  static PowerArmorFrame FrameFromJson(const nlohmann::json& j);
  nlohmann::json WornToJson(const WornPowerArmor& w) const;
  static WornPowerArmor WornFromJson(const nlohmann::json& j);
  // Restart: a frame whose wearer is not loaded gets a forced exit.
  void RestoreWorn(ActorId actor, WornPowerArmor worn);

  PowerArmorSettings settings;

private:
  PowerArmorSlot SlotOf(const ItemKey& key) const;
  float MaxHealth(const ItemKey& key) const;
  void MovePiecesToActor(PowerArmorFrame& frame, WornPowerArmor& worn,
                         Fo4Inventory& actorInventory);
  void MovePiecesToFrame(PowerArmorFrame& frame, WornPowerArmor& worn,
                         Fo4Inventory& actorInventory);
  bool TrySwapCore(WornPowerArmor& worn, Fo4Inventory& actorInventory);

  const IFo4DataSource& data;
  OmodStatResolver resolver;
  std::map<FormId, PowerArmorFrame> frames;
  std::map<ActorId, WornPowerArmor> worn;
  std::map<ActorId, std::array<float, 3>> pendingExitPos;
};

}
