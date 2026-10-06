#include "Party.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace fo4 {

const char* PartyErrorToString(PartyError e) noexcept
{
  switch (e) {
#define C(x)                                                                  \
  case PartyError::x:                                                         \
    return #x;
    C(None)
    C(AlreadyInParty)
    C(NotInParty)
    C(NotLeader)
    C(PartyFull)
    C(NoInvite)
    C(InviteExpired)
    C(CannotTargetSelf)
    C(TargetNotInParty)
    C(FlagCooldown)
    C(InCombat)
    C(UnknownParty)
#undef C
  }
  return "Unknown";
}

PartyService::PartyService(PartySettings s)
  : settings(s)
{
}

PartyError PartyService::Create(ProfileId leader, int64_t nowMs,
                                PartyId* outId)
{
  (void)nowMs;
  if (memberOf.count(leader)) {
    return PartyError::AlreadyInParty;
  }
  Party p;
  p.id = nextId++;
  p.leader = leader;
  p.members = { leader };
  memberOf[leader] = p.id;
  if (outId) {
    *outId = p.id;
  }
  parties[p.id] = std::move(p);
  return PartyError::None;
}

PartyError PartyService::Invite(ProfileId from, ProfileId to, int64_t nowMs)
{
  if (from == to) {
    return PartyError::CannotTargetSelf;
  }
  auto it = memberOf.find(from);
  if (it == memberOf.end()) {
    // Inviting without a party creates one, like most MMOs
    PartyId id;
    if (auto e = Create(from, nowMs, &id); e != PartyError::None) {
      return e;
    }
    it = memberOf.find(from);
  }
  auto& party = parties[it->second];
  if (party.leader != from) {
    return PartyError::NotLeader;
  }
  if (memberOf.count(to)) {
    return PartyError::AlreadyInParty;
  }
  if (party.members.size() >= settings.maxPartySize) {
    return PartyError::PartyFull;
  }
  invites[to].push_back({ party.id, nowMs + settings.inviteTtlMs });
  return PartyError::None;
}

PartyError PartyService::Accept(ProfileId who, PartyId partyId, int64_t nowMs)
{
  auto inv = invites.find(who);
  if (inv == invites.end()) {
    return PartyError::NoInvite;
  }
  auto& list = inv->second;
  auto it = std::find_if(list.begin(), list.end(),
                         [&](auto& i) { return i.party == partyId; });
  if (it == list.end()) {
    return PartyError::NoInvite;
  }
  bool expired = nowMs > it->expiresMs;
  list.erase(it);
  if (expired) {
    return PartyError::InviteExpired;
  }
  if (memberOf.count(who)) {
    return PartyError::AlreadyInParty;
  }
  auto p = parties.find(partyId);
  if (p == parties.end()) {
    return PartyError::UnknownParty;
  }
  if (p->second.members.size() >= settings.maxPartySize) {
    return PartyError::PartyFull;
  }
  p->second.members.push_back(who);
  memberOf[who] = partyId;
  invites.erase(who);
  return PartyError::None;
}

PartyError PartyService::Decline(ProfileId who, PartyId partyId)
{
  auto inv = invites.find(who);
  if (inv == invites.end()) {
    return PartyError::NoInvite;
  }
  auto& list = inv->second;
  auto before = list.size();
  list.erase(std::remove_if(list.begin(), list.end(),
                            [&](auto& i) { return i.party == partyId; }),
             list.end());
  return before == list.size() ? PartyError::NoInvite : PartyError::None;
}

PartyError PartyService::Leave(ProfileId who)
{
  auto it = memberOf.find(who);
  if (it == memberOf.end()) {
    return PartyError::NotInParty;
  }
  auto& party = parties[it->second];
  party.members.erase(
    std::remove(party.members.begin(), party.members.end(), who),
    party.members.end());
  memberOf.erase(it);
  if (party.members.empty()) {
    parties.erase(party.id);
  } else if (party.leader == who) {
    party.leader = party.members.front(); // longest-standing member
  }
  return PartyError::None;
}

PartyError PartyService::Kick(ProfileId leader, ProfileId target)
{
  auto it = memberOf.find(leader);
  if (it == memberOf.end()) {
    return PartyError::NotInParty;
  }
  if (parties[it->second].leader != leader) {
    return PartyError::NotLeader;
  }
  if (leader == target) {
    return PartyError::CannotTargetSelf;
  }
  auto t = memberOf.find(target);
  if (t == memberOf.end() || t->second != it->second) {
    return PartyError::TargetNotInParty;
  }
  return Leave(target);
}

PartyError PartyService::Promote(ProfileId leader, ProfileId target)
{
  auto it = memberOf.find(leader);
  if (it == memberOf.end()) {
    return PartyError::NotInParty;
  }
  auto& party = parties[it->second];
  if (party.leader != leader) {
    return PartyError::NotLeader;
  }
  auto t = memberOf.find(target);
  if (t == memberOf.end() || t->second != party.id) {
    return PartyError::TargetNotInParty;
  }
  party.leader = target;
  return PartyError::None;
}

std::optional<PartyId> PartyService::GetPartyOf(ProfileId p) const
{
  auto it = memberOf.find(p);
  if (it == memberOf.end()) {
    return std::nullopt;
  }
  return it->second;
}

const Party* PartyService::Find(PartyId id) const
{
  auto it = parties.find(id);
  return it == parties.end() ? nullptr : &it->second;
}

bool PartyService::SameParty(ProfileId a, ProfileId b) const
{
  auto pa = GetPartyOf(a), pb = GetPartyOf(b);
  return pa && pb && *pa == *pb;
}

PartyError PartyService::SetPvpFlag(ProfileId p, bool flagged, int64_t nowMs)
{
  auto& st = pvp[p];
  if (st.flagged == flagged) {
    return PartyError::None;
  }
  if (nowMs - st.lastFlagChangeMs < settings.flagCooldownMs) {
    return PartyError::FlagCooldown;
  }
  if (!flagged && nowMs - st.lastPvpDamageMs < settings.combatTagMs) {
    return PartyError::InCombat;
  }
  st.flagged = flagged;
  st.lastFlagChangeMs = nowMs;
  return PartyError::None;
}

void PartyService::NotePvpDamage(ProfileId p, int64_t nowMs)
{
  pvp[p].lastPvpDamageMs = nowMs;
}

bool PartyService::IsFlagged(ProfileId p) const
{
  auto it = pvp.find(p);
  return it == pvp.end() ? settings.defaultFlag : it->second.flagged;
}

PvpZoneMode PartyService::ZoneAt(const std::array<float, 3>& pos,
                                 uint32_t worldOrCell) const
{
  // Later zones override earlier ones (gamemode stacks specific zones)
  PvpZoneMode mode = settings.defaultMode;
  for (auto& z : zones) {
    if (z.worldOrCell && z.worldOrCell != worldOrCell) {
      continue;
    }
    float dx = pos[0] - z.center[0], dy = pos[1] - z.center[1],
          dz = pos[2] - z.center[2];
    if (std::sqrt(dx * dx + dy * dy + dz * dz) <= z.radius) {
      mode = z.mode;
    }
  }
  return mode;
}

PvpVerdict PartyService::CanDamage(ProfileId attacker, ProfileId target,
                                   const std::array<float, 3>& targetPos,
                                   uint32_t worldOrCell) const
{
  if (!settings.friendlyFire && SameParty(attacker, target)) {
    return PvpVerdict::SameParty;
  }
  switch (ZoneAt(targetPos, worldOrCell)) {
    case PvpZoneMode::Safe:
      return PvpVerdict::SafeZone;
    case PvpZoneMode::Open:
      return PvpVerdict::Allowed;
    case PvpZoneMode::Flagged:
      return IsFlagged(attacker) && IsFlagged(target) ? PvpVerdict::Allowed
                                                      : PvpVerdict::NotFlagged;
  }
  return PvpVerdict::Allowed;
}

std::vector<std::pair<ProfileId, uint32_t>> PartyService::ShareXp(
  ProfileId killer, uint32_t xp,
  const std::map<ProfileId, std::array<float, 3>>& positions) const
{
  auto pid = GetPartyOf(killer);
  if (!pid || !parties.at(*pid).xpShare || !positions.count(killer)) {
    return { { killer, xp } };
  }
  auto& kpos = positions.at(killer);
  std::vector<ProfileId> eligible;
  for (auto m : parties.at(*pid).members) {
    auto it = positions.find(m);
    if (it == positions.end()) {
      continue;
    }
    float dx = it->second[0] - kpos[0], dy = it->second[1] - kpos[1],
          dz = it->second[2] - kpos[2];
    if (std::sqrt(dx * dx + dy * dy + dz * dz) <= settings.xpShareRadius) {
      eligible.push_back(m);
    }
  }
  // Each eligible member gets an equal share; the killer gets the
  // remainder so no XP is lost to rounding.
  std::vector<std::pair<ProfileId, uint32_t>> res;
  uint32_t share = xp / static_cast<uint32_t>(eligible.size());
  uint32_t rest = xp - share * static_cast<uint32_t>(eligible.size());
  for (auto m : eligible) {
    res.push_back({ m, share + (m == killer ? rest : 0) });
  }
  return res;
}

nlohmann::json PartyService::ToJson() const
{
  auto arr = nlohmann::json::array();
  for (auto& [id, p] : parties) {
    arr.push_back({ { "id", p.id },
                    { "leader", p.leader },
                    { "members", p.members },
                    { "xpShare", p.xpShare } });
  }
  auto flags = nlohmann::json::object();
  for (auto& [p, st] : pvp) {
    flags[std::to_string(p)] = st.flagged;
  }
  return { { "parties", arr }, { "pvpFlags", flags }, { "nextId", nextId } };
}

void PartyService::LoadJson(const nlohmann::json& j)
{
  parties.clear();
  memberOf.clear();
  if (!j.is_object()) {
    return;
  }
  nextId = j.value("nextId", PartyId(1));
  for (auto& jp : j.value("parties", nlohmann::json::array())) {
    Party p;
    p.id = jp.at("id").get<PartyId>();
    p.leader = jp.at("leader").get<ProfileId>();
    p.members = jp.at("members").get<std::vector<ProfileId>>();
    p.xpShare = jp.value("xpShare", true);
    for (auto m : p.members) {
      memberOf[m] = p.id;
    }
    nextId = std::max(nextId, p.id + 1);
    parties[p.id] = std::move(p);
  }
  const auto jFlags = j.value("pvpFlags", nlohmann::json::object());
  for (auto& [k, v] : jFlags.items()) {
    pvp[std::stoi(k)].flagged = v.get<bool>();
  }
}

}
