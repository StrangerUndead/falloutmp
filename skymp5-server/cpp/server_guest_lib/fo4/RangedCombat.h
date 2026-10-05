#pragma once
// Ranged combat validation (F09): server-owned magazines and reloads,
// fire-rate checks from resolved weapon stats, and hit claims matched to
// recorded shots with lag-compensated range checks (SRV-023).
#include "ItemInstance.h"
#include "OmodStatResolver.h"
#include <array>
#include <deque>
#include <map>

namespace fo4 {

enum class FireError : uint8_t
{
  None = 0,
  NotEquipped,
  NotAGun,
  EmptyMagazine,
  TooFast,
  NoAmmo,
  MagazineFull,
  UnknownShot,
  ShotTooOld,
  ProjectileReused,
  OutOfRange,
  SelfHit,
  TargetDead,
};
const char* FireErrorToString(FireError e) noexcept;

struct FireSettings
{
  float fireRateTolerance = 0.15f; // allow 15% faster than the stat model
  int64_t maxRewindMs = 250;
  int64_t shotLifetimeMs = 3000;   // projectiles in flight
  float rangeTolerance = 1.25f;
  float unlimitedRangeFallback = 20000.f; // maxRange 0 in data
  size_t shotLogSize = 64;
  bool infiniteAmmo = false; // gamemode setting
};

struct ShotRecord
{
  uint32_t seq = 0;
  int64_t timeMs = 0;
  ItemKey weapon;
  uint32_t projectiles = 1;
  std::array<float, 3> origin = { 0, 0, 0 };
  std::vector<bool> projectileUsed;
};

struct FireResult
{
  FireError error = FireError::None;
  uint32_t seq = 0;
  uint16_t ammoLeft = 0;
  ItemKey weaponAfter; // inventory key after the shot (ammoLoaded changed)
  bool Ok() const { return error == FireError::None; }
};

struct ReloadResult
{
  FireError error = FireError::None;
  uint16_t loaded = 0;
  ItemKey weaponAfter;
  bool Ok() const { return error == FireError::None; }
};

struct HitClaim
{
  uint32_t shotSeq = 0;
  uint32_t projectileIndex = 0;
  uint32_t targetActorId = 0;
  bool targetAlive = true;
  // Rewound target position at the shot time (from F01 history)
  std::array<float, 3> targetPos = { 0, 0, 0 };
  int64_t claimTimeMs = 0;
};

class RangedCombat
{
public:
  RangedCombat(const IFo4DataSource& data, FireSettings settings = {});

  // `equipped` is the weapon instance the server has as equipped.
  FireResult Fire(uint32_t shooter, const ItemKey& equipped,
                  Fo4Inventory& inv, const std::array<float, 3>& origin,
                  int64_t nowMs);
  ReloadResult Reload(uint32_t shooter, const ItemKey& equipped,
                      Fo4Inventory& inv);
  FireError ValidateHit(uint32_t shooter, const HitClaim& claim);

  const ShotRecord* FindShot(uint32_t shooter, uint32_t seq) const;
  void Forget(uint32_t shooter) { shooters.erase(shooter); }

  FireSettings settings;

private:
  struct ShooterState
  {
    uint32_t nextSeq = 1;
    int64_t lastShotMs = -1;
    std::deque<ShotRecord> log;
  };
  ItemKey ReplaceInstance(Fo4Inventory& inv, const ItemKey& from,
                          uint16_t newAmmo);

  const IFo4DataSource& data;
  OmodStatResolver resolver;
  std::map<uint32_t, ShooterState> shooters;
};

}
