#include "RangedCombat.h"
#include <algorithm>
#include <cmath>

namespace fo4 {

const char* FireErrorToString(FireError e) noexcept
{
  switch (e) {
#define C(x)                                                                  \
  case FireError::x:                                                          \
    return #x;
    C(None)
    C(NotEquipped)
    C(NotAGun)
    C(EmptyMagazine)
    C(TooFast)
    C(NoAmmo)
    C(MagazineFull)
    C(UnknownShot)
    C(ShotTooOld)
    C(ProjectileReused)
    C(OutOfRange)
    C(SelfHit)
    C(TargetDead)
#undef C
  }
  return "Unknown";
}

RangedCombat::RangedCombat(const IFo4DataSource& data_, FireSettings s)
  : settings(s)
  , data(data_)
  , resolver(data_)
{
}

ItemKey RangedCombat::ReplaceInstance(Fo4Inventory& inv, const ItemKey& from,
                                      uint16_t newAmmo)
{
  // The equipped gun is one item; split it off its stack if needed so the
  // other copies keep their own magazine count.
  auto entry = inv.Find(from);
  ItemKey key = entry ? entry->key : from;
  inv.Remove(key, 1);
  ItemKey after = key;
  after.ammoLoaded = newAmmo;
  // Add() would merge with min(ammo) into an identical stack; keep the
  // equipped instance separate by re-adding only when no twin exists.
  inv.Add(after, 1);
  return after;
}

FireResult RangedCombat::Fire(uint32_t shooter, const ItemKey& equipped,
                              Fo4Inventory& inv,
                              const std::array<float, 3>& origin,
                              int64_t nowMs, bool unlimitedAmmo)
{
  FireResult r;
  const bool infinite = settings.infiniteAmmo || unlimitedAmmo;
  auto entry = inv.Find(equipped);
  if (!entry) {
    r.error = FireError::NotEquipped;
    return r;
  }
  auto base = data.FindWeapon(equipped.baseId);
  if (!base || !(base->isGun)) {
    r.error = FireError::NotAGun;
    return r;
  }
  auto stats = resolver.ResolveWeapon(entry->key);
  uint16_t ammo = entry->key.ammoLoaded;
  if (!infinite && stats.capacity > 0 && ammo == 0) {
    r.error = FireError::EmptyMagazine;
    return r;
  }
  auto& st = shooters[shooter];
  if (st.lastShotMs >= 0) {
    float minInterval = 1000.f / stats.MaxShotsPerSecond() *
      (1.f - settings.fireRateTolerance);
    if (static_cast<float>(nowMs - st.lastShotMs) < minInterval) {
      r.error = FireError::TooFast;
      return r;
    }
  }
  uint16_t newAmmo = ammo;
  if (!infinite && stats.capacity > 0) {
    newAmmo = static_cast<uint16_t>(ammo - 1);
  }
  r.weaponAfter = ReplaceInstance(inv, entry->key, newAmmo);
  st.lastShotMs = nowMs;
  ShotRecord shot;
  shot.seq = st.nextSeq++;
  shot.timeMs = nowMs;
  shot.weapon = r.weaponAfter;
  shot.projectiles = static_cast<uint32_t>(stats.numProjectiles);
  shot.origin = origin;
  shot.projectileUsed.assign(shot.projectiles, false);
  st.log.push_back(shot);
  while (st.log.size() > settings.shotLogSize) {
    st.log.pop_front();
  }
  r.seq = shot.seq;
  r.ammoLeft = newAmmo;
  return r;
}

ReloadResult RangedCombat::Reload(uint32_t shooter, const ItemKey& equipped,
                                  Fo4Inventory& inv)
{
  (void)shooter;
  ReloadResult r;
  auto entry = inv.Find(equipped);
  if (!entry) {
    r.error = FireError::NotEquipped;
    return r;
  }
  auto stats = resolver.ResolveWeapon(entry->key);
  if (stats.capacity == 0 || !stats.ammoId) {
    r.error = FireError::NotAGun;
    return r;
  }
  uint16_t have = entry->key.ammoLoaded;
  if (have >= stats.capacity) {
    r.error = FireError::MagazineFull;
    return r;
  }
  uint32_t want = stats.capacity - have;
  uint32_t inPack = inv.CountBase(stats.ammoId);
  uint32_t take = std::min(want, inPack);
  if (take == 0) {
    r.error = FireError::NoAmmo;
    return r;
  }
  ItemKey key = entry->key;
  inv.RemoveAnyOf(stats.ammoId, take);
  r.loaded = static_cast<uint16_t>(have + take);
  r.weaponAfter = ReplaceInstance(inv, key, r.loaded);
  return r;
}

const ShotRecord* RangedCombat::FindShot(uint32_t shooter, uint32_t seq) const
{
  auto it = shooters.find(shooter);
  if (it == shooters.end()) {
    return nullptr;
  }
  for (auto& s : it->second.log) {
    if (s.seq == seq) {
      return &s;
    }
  }
  return nullptr;
}

FireError RangedCombat::ValidateHit(uint32_t shooter, const HitClaim& c)
{
  if (c.targetActorId == shooter) {
    return FireError::SelfHit;
  }
  if (!c.targetAlive) {
    return FireError::TargetDead;
  }
  auto it = shooters.find(shooter);
  if (it == shooters.end()) {
    return FireError::UnknownShot;
  }
  ShotRecord* shot = nullptr;
  for (auto& s : it->second.log) {
    if (s.seq == c.shotSeq) {
      shot = &s;
    }
  }
  if (!shot) {
    return FireError::UnknownShot;
  }
  if (c.claimTimeMs - shot->timeMs > settings.shotLifetimeMs) {
    return FireError::ShotTooOld;
  }
  if (c.projectileIndex >= shot->projectileUsed.size() ||
      shot->projectileUsed[c.projectileIndex]) {
    return FireError::ProjectileReused;
  }
  auto stats = resolver.ResolveWeapon(shot->weapon);
  float range = stats.maxRange > 0.f ? stats.maxRange
                                     : settings.unlimitedRangeFallback;
  float dx = c.targetPos[0] - shot->origin[0];
  float dy = c.targetPos[1] - shot->origin[1];
  float dz = c.targetPos[2] - shot->origin[2];
  float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
  // Beyond max range guns still hit for reduced damage in Fallout 4 up to
  // roughly twice the max range; claims past that are rejected.
  if (dist > range * 2.f * settings.rangeTolerance) {
    return FireError::OutOfRange;
  }
  shot->projectileUsed[c.projectileIndex] = true;
  return FireError::None;
}

}
