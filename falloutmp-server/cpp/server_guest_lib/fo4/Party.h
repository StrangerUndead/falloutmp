#pragma once
// Parties, teams and PvP rules (F32).
#include <array>
#include <cstdint>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fo4 {

using PartyId = uint32_t;
using ProfileId = int32_t;

enum class PartyError : uint8_t
{
  None = 0,
  AlreadyInParty,
  NotInParty,
  NotLeader,
  PartyFull,
  NoInvite,
  InviteExpired,
  CannotTargetSelf,
  TargetNotInParty,
  FlagCooldown,
  InCombat,
  UnknownParty,
};
const char* PartyErrorToString(PartyError e) noexcept;

enum class PvpZoneMode : uint8_t
{
  Safe,    // no player damage
  Open,    // always PvP
  Flagged, // both players must be flagged
};

struct PvpZone
{
  std::array<float, 3> center = { 0, 0, 0 };
  float radius = 0.f;
  uint32_t worldOrCell = 0;
  PvpZoneMode mode = PvpZoneMode::Safe;
};

struct PartySettings
{
  uint32_t maxPartySize = 8;
  int64_t inviteTtlMs = 60000;
  int64_t flagCooldownMs = 300000;
  int64_t combatTagMs = 30000;
  PvpZoneMode defaultMode = PvpZoneMode::Flagged;
  bool friendlyFire = false;
  float xpShareRadius = 4096.f;
  bool defaultFlag = false;
};

struct Party
{
  PartyId id = 0;
  ProfileId leader = -1;
  std::vector<ProfileId> members; // join order (leader succession)
  bool xpShare = true;
};

struct PlayerPvpState
{
  bool flagged = false;
  int64_t lastFlagChangeMs = -1000000000;
  int64_t lastPvpDamageMs = -1000000000;
};

enum class PvpVerdict : uint8_t
{
  Allowed,
  SameParty,
  SafeZone,
  NotFlagged,
};

class PartyService
{
public:
  explicit PartyService(PartySettings settings = {});

  PartyError Create(ProfileId leader, int64_t nowMs, PartyId* outId = nullptr);
  PartyError Invite(ProfileId from, ProfileId to, int64_t nowMs);
  PartyError Accept(ProfileId who, PartyId party, int64_t nowMs);
  PartyError Decline(ProfileId who, PartyId party);
  PartyError Leave(ProfileId who);
  PartyError Kick(ProfileId leader, ProfileId target);
  PartyError Promote(ProfileId leader, ProfileId target);

  std::optional<PartyId> GetPartyOf(ProfileId p) const;
  const Party* Find(PartyId id) const;
  bool SameParty(ProfileId a, ProfileId b) const;

  // PvP
  PartyError SetPvpFlag(ProfileId p, bool flagged, int64_t nowMs);
  void NotePvpDamage(ProfileId p, int64_t nowMs);
  bool IsFlagged(ProfileId p) const;
  void AddZone(PvpZone z) { zones.push_back(z); }
  PvpZoneMode ZoneAt(const std::array<float, 3>& pos,
                     uint32_t worldOrCell) const;
  PvpVerdict CanDamage(ProfileId attacker, ProfileId target,
                       const std::array<float, 3>& targetPos,
                       uint32_t worldOrCell) const;

  // Kill XP split: returns (profile, xp) for party members in range
  std::vector<std::pair<ProfileId, uint32_t>> ShareXp(
    ProfileId killer, uint32_t xp,
    const std::map<ProfileId, std::array<float, 3>>& positions) const;

  nlohmann::json ToJson() const;
  void LoadJson(const nlohmann::json& j);

  PartySettings settings;

private:
  struct PendingInvite
  {
    PartyId party;
    int64_t expiresMs;
  };
  PartyId nextId = 1;
  std::map<PartyId, Party> parties;
  std::map<ProfileId, PartyId> memberOf;
  std::map<ProfileId, std::vector<PendingInvite>> invites;
  std::map<ProfileId, PlayerPvpState> pvp;
  std::vector<PvpZone> zones;
};

}
