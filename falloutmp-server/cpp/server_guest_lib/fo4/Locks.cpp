#include "Locks.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace fo4 {

int32_t LockTier(uint8_t level)
{
  if (level == 0) {
    return -1;
  }
  if (level <= 25) {
    return 0;
  }
  if (level <= 50) {
    return 1;
  }
  if (level <= 75) {
    return 2;
  }
  if (level <= 100) {
    return 3;
  }
  return -1;
}

const char* LockResultToString(LockResultCode c) noexcept
{
  switch (c) {
#define C(x)                                                                  \
  case LockResultCode::x:                                                     \
    return #x;
    C(Opened)
    C(OpenedWithKey)
    C(BeginLockpick)
    C(NeedsKey)
    C(Barred)
    C(Chained)
    C(NeedsTerminal)
    C(Inaccessible)
    C(PerkTooLow)
    C(NoPins)
    C(Busy)
    C(NoSession)
    C(Unlocked)
    C(Failed)
    C(SessionExpired)
#undef C
  }
  return "Unknown";
}

const char* HackResultToString(HackResultCode c) noexcept
{
  switch (c) {
#define C(x)                                                                  \
  case HackResultCode::x:                                                     \
    return #x;
    C(Open)
    C(BeginHack)
    C(PerkTooLow)
    C(LockedOut)
    C(Busy)
    C(NoSession)
    C(Hacked)
    C(WrongGuess)
    C(LockoutStarted)
#undef C
  }
  return "Unknown";
}

LockService::LockService(LockSettings s)
  : settings(s)
{
}

LockState& LockService::SetLock(FormId refId, LockState s)
{
  originalLocks[refId] = s;
  return locks[refId] = s;
}

const LockState* LockService::GetLock(FormId refId) const
{
  auto it = locks.find(refId);
  return it == locks.end() ? nullptr : &it->second;
}

TerminalState& LockService::SetTerminal(FormId refId, TerminalState s)
{
  return terminals[refId] = s;
}

const TerminalState* LockService::GetTerminal(FormId refId) const
{
  auto it = terminals.find(refId);
  return it == terminals.end() ? nullptr : &it->second;
}

float LockService::LockpickChance(int32_t tier, const LockpickerFacts& a) const
{
  if (tier < 0 || tier > 3) {
    return 0.f;
  }
  float c = settings.baseChance[tier] +
    settings.perPerceptionPoint * a.perception +
    settings.perLocksmithRank * a.locksmithRank;
  return std::clamp(c, settings.minChance, settings.maxChance);
}

float LockService::HackChance(int32_t tier, const HackerFacts& a) const
{
  if (tier < 0 || tier > 3) {
    return 0.f;
  }
  float c = settings.baseChance[tier] +
    settings.perIntelligencePoint * a.intelligence +
    settings.perHackerRank * a.hackerRank;
  return std::clamp(c, settings.minChance, settings.maxChance);
}

LockResult LockService::Activate(FormId refId, const LockpickerFacts& a,
                                 const Fo4Inventory& inv)
{
  LockResult r;
  auto it = locks.find(refId);
  if (it == locks.end() || !it->second.locked) {
    r.code = LockResultCode::Opened;
    return r;
  }
  auto& lock = it->second;
  if (lock.keyId && inv.CountBase(lock.keyId) > 0) {
    lock.locked = false;
    r.code = LockResultCode::OpenedWithKey;
    return r;
  }
  switch (static_cast<LockLevel>(lock.level)) {
    case LockLevel::Barred:
      r.code = LockResultCode::Barred;
      return r;
    case LockLevel::Chained:
      r.code = LockResultCode::Chained;
      return r;
    case LockLevel::RequiresTerminal:
      r.code = LockResultCode::NeedsTerminal;
      return r;
    case LockLevel::Inaccessible:
      r.code = LockResultCode::Inaccessible;
      return r;
    case LockLevel::RequiresKey:
      r.code = LockResultCode::NeedsKey;
      return r;
    default:
      break;
  }
  int32_t tier = LockTier(lock.level);
  if (tier < 0) {
    r.code = LockResultCode::NeedsKey;
    return r;
  }
  if (a.locksmithRank < RequiredRankForTier(tier)) {
    r.code = LockResultCode::PerkTooLow;
    return r;
  }
  if (inv.CountBase(settings.bobbyPinId) == 0) {
    r.code = LockResultCode::NoPins;
    return r;
  }
  for (auto& [id, s] : pickSessions) {
    if (s.refId == refId && s.actorId != a.actorId &&
        a.nowMs - s.issuedMs < settings.sessionTimeoutMs) {
      r.code = LockResultCode::Busy;
      return r;
    }
  }
  uint32_t id = nextSession++;
  pickSessions[id] = { refId, a.actorId, a.nowMs };
  r.code = LockResultCode::BeginLockpick;
  r.sessionId = id;
  return r;
}

LockResult LockService::Attempt(uint32_t sessionId, const LockpickerFacts& a,
                                Fo4Inventory& inv, std::mt19937& rng)
{
  LockResult r;
  auto it = pickSessions.find(sessionId);
  if (it == pickSessions.end() || it->second.actorId != a.actorId) {
    r.code = LockResultCode::NoSession;
    return r;
  }
  if (a.nowMs - it->second.issuedMs > settings.sessionTimeoutMs) {
    pickSessions.erase(it);
    r.code = LockResultCode::SessionExpired;
    return r;
  }
  auto lockIt = locks.find(it->second.refId);
  if (lockIt == locks.end() || !lockIt->second.locked) {
    pickSessions.erase(it);
    r.code = LockResultCode::Opened;
    return r;
  }
  if (inv.CountBase(settings.bobbyPinId) == 0) {
    r.code = LockResultCode::NoPins;
    return r;
  }
  int32_t tier = LockTier(lockIt->second.level);
  std::uniform_real_distribution<float> roll(0.f, 1.f);
  r.sessionId = sessionId;
  if (roll(rng) < LockpickChance(tier, a)) {
    lockIt->second.locked = false;
    r.code = LockResultCode::Unlocked;
    r.xp = settings.xpByTier[std::clamp(tier, 0, 3)];
    r.crime = lockIt->second.ownerFaction != 0;
    pickSessions.erase(it);
  } else {
    r.code = LockResultCode::Failed;
    if (a.locksmithRank < 4) { // Locksmith 4: pins never break
      inv.RemoveAnyOf(settings.bobbyPinId, 1);
    }
  }
  return r;
}

void LockService::CancelLockpick(uint32_t sessionId)
{
  pickSessions.erase(sessionId);
}

void LockService::Relock(FormId refId)
{
  auto it = originalLocks.find(refId);
  if (it != originalLocks.end()) {
    locks[refId] = it->second;
  }
}

HackResult LockService::BeginHack(FormId refId, const HackerFacts& a)
{
  HackResult r;
  auto it = terminals.find(refId);
  if (it == terminals.end() || !it->second.locked) {
    r.code = HackResultCode::Open;
    return r;
  }
  if (a.nowMs < it->second.lockoutUntilMs) {
    r.code = HackResultCode::LockedOut;
    return r;
  }
  int32_t tier = LockTier(it->second.level);
  if (tier < 0 || a.hackerRank < RequiredRankForTier(tier)) {
    r.code = HackResultCode::PerkTooLow;
    return r;
  }
  for (auto& [id, s] : hackSessions) {
    if (s.refId == refId && s.actorId != a.actorId &&
        a.nowMs - s.issuedMs < settings.sessionTimeoutMs) {
      r.code = HackResultCode::Busy;
      return r;
    }
  }
  uint32_t id = nextSession++;
  hackSessions[id] = { refId, a.actorId, settings.hackAttempts, a.nowMs };
  r.code = HackResultCode::BeginHack;
  r.sessionId = id;
  r.attemptsLeft = settings.hackAttempts;
  return r;
}

HackResult LockService::HackAttempt(uint32_t sessionId, const HackerFacts& a,
                                    std::mt19937& rng)
{
  HackResult r;
  auto it = hackSessions.find(sessionId);
  if (it == hackSessions.end() || it->second.actorId != a.actorId ||
      a.nowMs - it->second.issuedMs > settings.sessionTimeoutMs) {
    r.code = HackResultCode::NoSession;
    return r;
  }
  auto term = terminals.find(it->second.refId);
  if (term == terminals.end() || !term->second.locked) {
    hackSessions.erase(it);
    r.code = HackResultCode::Open;
    return r;
  }
  int32_t tier = LockTier(term->second.level);
  std::uniform_real_distribution<float> roll(0.f, 1.f);
  r.sessionId = sessionId;
  if (roll(rng) < HackChance(tier, a)) {
    term->second.locked = false;
    r.code = HackResultCode::Hacked;
    r.xp = settings.xpByTier[std::clamp(tier, 0, 3)];
    hackSessions.erase(it);
    return r;
  }
  r.attemptsLeft = --it->second.attemptsLeft;
  if (r.attemptsLeft == 0) {
    // Hacker 4 in vanilla removes lockouts
    if (a.hackerRank < 4) {
      term->second.lockoutUntilMs = a.nowMs + settings.lockoutMs;
      r.code = HackResultCode::LockoutStarted;
    } else {
      r.code = HackResultCode::WrongGuess;
    }
    hackSessions.erase(it);
    return r;
  }
  r.code = HackResultCode::WrongGuess;
  return r;
}

nlohmann::json LockService::ToJson() const
{
  auto jl = nlohmann::json::object();
  for (auto& [id, l] : locks) {
    jl[std::to_string(id)] = { l.level, l.keyId, l.locked, l.ownerFaction };
  }
  auto jt = nlohmann::json::object();
  for (auto& [id, t] : terminals) {
    jt[std::to_string(id)] = { t.level, t.locked };
  }
  return { { "locks", jl }, { "terminals", jt } };
}

void LockService::LoadJson(const nlohmann::json& j)
{
  if (!j.is_object()) {
    return;
  }
  const auto jLocks = j.value("locks", nlohmann::json::object());
  for (auto& [k, v] : jLocks.items()) {
    try {
      LockState l{ v.at(0).get<uint8_t>(), v.at(1).get<FormId>(),
                   v.at(2).get<bool>(), v.at(3).get<FormId>() };
      locks[static_cast<FormId>(std::stoul(k))] = l;
    } catch (const std::exception&) {
    }
  }
  const auto jTerminals = j.value("terminals", nlohmann::json::object());
  for (auto& [k, v] : jTerminals.items()) {
    try {
      TerminalState t;
      t.level = v.at(0).get<uint8_t>();
      t.locked = v.at(1).get<bool>();
      terminals[static_cast<FormId>(std::stoul(k))] = t;
    } catch (const std::exception&) {
    }
  }
}

}
