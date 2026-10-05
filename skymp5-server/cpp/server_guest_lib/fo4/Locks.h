#pragma once
// Locks, lockpicking and terminal hacking (F24).
//
// Outcomes are rolled by the server (review finding C3): the vanilla
// minigame is presentation only. Each attempt is a server roll from the
// lock level, Locksmith/Hacker rank and Perception/Intelligence; a failed
// lockpick roll consumes a bobby pin (never at Locksmith 4).
#include "ItemInstance.h"
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <random>

namespace fo4 {

// XLOC level byte values
enum class LockLevel : uint8_t
{
  Unlocked = 0,
  Novice = 1,  // also 25
  Advanced = 50,
  Expert = 75,
  Master = 100,
  Barred = 251,
  Chained = 252,
  RequiresTerminal = 253,
  Inaccessible = 254,
  RequiresKey = 255,
};

// Normalized tiers 0..3 for Novice..Master, -1 for not pickable
int32_t LockTier(uint8_t xlocLevel);

struct LockState
{
  uint8_t level = 0; // XLOC level byte
  FormId keyId = 0;
  bool locked = false;
  FormId ownerFaction = 0; // picking an owned lock is a crime (F13)
};

enum class LockResultCode : uint8_t
{
  Opened = 0,      // not locked
  OpenedWithKey,
  BeginLockpick,   // a session was created
  NeedsKey,
  Barred,
  Chained,
  NeedsTerminal,
  Inaccessible,
  PerkTooLow,
  NoPins,
  Busy,
  NoSession,
  Unlocked,        // attempt succeeded
  Failed,          // attempt failed, pin consumed if applicable
  SessionExpired,
};
const char* LockResultToString(LockResultCode c) noexcept;

struct LockpickerFacts
{
  uint32_t actorId = 0;
  int32_t locksmithRank = 0;
  float perception = 1.f;
  int64_t nowMs = 0;
};

struct LockResult
{
  LockResultCode code = LockResultCode::Opened;
  uint32_t sessionId = 0;
  uint32_t xp = 0;
  bool crime = false; // owned lock picked
};

struct HackerFacts
{
  uint32_t actorId = 0;
  int32_t hackerRank = 0;
  float intelligence = 1.f;
  int64_t nowMs = 0;
};

struct TerminalState
{
  uint8_t level = 0; // same tiers as locks; 0 = not locked
  bool locked = false;
  int64_t lockoutUntilMs = 0; // per terminal (vanilla locks you out)
};

enum class HackResultCode : uint8_t
{
  Open = 0,
  BeginHack,
  PerkTooLow,
  LockedOut,
  Busy,
  NoSession,
  Hacked,
  WrongGuess,
  LockoutStarted,
};
const char* HackResultToString(HackResultCode c) noexcept;

struct HackResult
{
  HackResultCode code = HackResultCode::Open;
  uint32_t sessionId = 0;
  uint8_t attemptsLeft = 0;
  uint32_t xp = 0;
};

struct LockSettings
{
  FormId bobbyPinId = 0xA;
  int64_t sessionTimeoutMs = 120000;
  int64_t lockoutMs = 10000;
  uint8_t hackAttempts = 4;
  // Success chance per attempt by tier (Novice..Master), before bonuses.
  float baseChance[4] = { 0.70f, 0.55f, 0.40f, 0.25f };
  float perPerceptionPoint = 0.02f;
  float perLocksmithRank = 0.05f;
  float perIntelligencePoint = 0.02f;
  float perHackerRank = 0.05f;
  uint32_t xpByTier[4] = { 6, 12, 17, 22 };
  float minChance = 0.05f, maxChance = 0.95f;
};

class LockService
{
public:
  explicit LockService(LockSettings settings = {});

  LockState& SetLock(FormId refId, LockState s);
  const LockState* GetLock(FormId refId) const;
  TerminalState& SetTerminal(FormId refId, TerminalState s);
  const TerminalState* GetTerminal(FormId refId) const;

  // Activation of a locked reference (door/container/safe).
  LockResult Activate(FormId refId, const LockpickerFacts& actor,
                      const Fo4Inventory& inv);
  // One pick attempt; the server rolls the outcome.
  LockResult Attempt(uint32_t sessionId, const LockpickerFacts& actor,
                     Fo4Inventory& inv, std::mt19937& rng);
  void CancelLockpick(uint32_t sessionId);

  // Relock on cell reset (F14)
  void Relock(FormId refId);

  HackResult BeginHack(FormId terminalRefId, const HackerFacts& actor);
  HackResult HackAttempt(uint32_t sessionId, const HackerFacts& actor,
                         std::mt19937& rng);

  float LockpickChance(int32_t tier, const LockpickerFacts& a) const;
  float HackChance(int32_t tier, const HackerFacts& a) const;
  static int32_t RequiredRankForTier(int32_t tier) { return tier; }

  nlohmann::json ToJson() const;
  void LoadJson(const nlohmann::json& j);

  LockSettings settings;

private:
  struct PickSession
  {
    FormId refId = 0;
    uint32_t actorId = 0;
    int64_t issuedMs = 0;
  };
  struct HackSession
  {
    FormId refId = 0;
    uint32_t actorId = 0;
    uint8_t attemptsLeft = 4;
    int64_t issuedMs = 0;
  };
  uint32_t nextSession = 1;
  std::map<FormId, LockState> locks;
  std::map<FormId, LockState> originalLocks;
  std::map<FormId, TerminalState> terminals;
  std::map<uint32_t, PickSession> pickSessions;
  std::map<uint32_t, HackSession> hackSessions;
};

}
