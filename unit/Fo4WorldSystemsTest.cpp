#include "Fo4TestData.h"
#include "fo4/Barter.h"
#include "fo4/Locks.h"
#include "fo4/Party.h"
#include "fo4/RangedCombat.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using namespace fo4;
using namespace fo4test;

namespace {
constexpr FormId kCaps = 0xF;
struct Data
{
  InMemoryFo4DataSource d;
  Data()
  {
    Build(d);
    MiscData caps;
    caps.id = kCaps;
    caps.value = 1;
    d.AddMisc(caps);
  }
};
}

TEST_CASE("Barter prices follow the Charisma formula", "[fo4][F23]")
{
  PriceModifiers m;
  m.charisma = 1;
  REQUIRE(BuyPrice(100, m) == 335); // 3.35x
  REQUIRE(SellPrice(100, m) == 29); // 1/3.35
  m.charisma = 10;
  REQUIRE(BuyPrice(100, m) == 200);
  REQUIRE(SellPrice(100, m) == 50);
  // Clamps: buy never below 1.2x, sell never above 0.8x
  m.buyMult = 0.1f;
  m.sellMult = 10.f;
  REQUIRE(BuyPrice(100, m) == 120);
  REQUIRE(SellPrice(100, m) == 80);
  REQUIRE(BuyPrice(0, m) == 0);
}

TEST_CASE("Vendor trades are atomic and validated", "[fo4][F23]")
{
  Data data;
  VendorService vs(data.d, kCaps);
  Vendor v;
  v.vendorId = 0x1000;
  v.factionId = 0xFAC;
  v.caps = 100;
  v.inventory.AddSimple(kStimpak, 5);
  v.openHour = 8;
  v.closeHour = 20;
  vs.AddVendor(v);

  Fo4Inventory player;
  player.AddSimple(kCaps, 10);
  player.AddSimple(kWrench, 2); // value 10 each
  PriceModifiers m;
  m.charisma = 5; // 2.75x / 0.36x

  BarterRequest req;
  req.vendorId = 0x1000;
  req.sell = { { ItemKey{ kWrench }, 2 } };
  BarterError err;
  req.expectedCapsDelta = vs.QuoteCapsDelta(req, m, err);
  REQUIRE(err == BarterError::None);
  REQUIRE(req.expectedCapsDelta == 6); // floor(3.63) * 2

  REQUIRE(vs.Trade(req, player, m, 22.f).error == BarterError::VendorClosed);
  auto stale = req;
  stale.expectedCapsDelta = 50;
  REQUIRE(vs.Trade(stale, player, m, 12.f).error == BarterError::PriceChanged);
  auto r = vs.Trade(req, player, m, 12.f);
  REQUIRE(r.Ok());
  REQUIRE(player.CountBase(kCaps) == 16);
  REQUIRE(player.CountBase(kWrench) == 0);
  REQUIRE(vs.Find(0x1000)->caps == 94);
  REQUIRE(vs.Find(0x1000)->inventory.CountBase(kWrench) == 2);

  // Buying a stimpak (value 0 in test data -> give it a value)
  ConsumableData stim = *data.d.FindConsumable(kStimpak);
  stim.value = 50;
  data.d.AddConsumable(stim);
  BarterRequest buy;
  buy.vendorId = 0x1000;
  buy.buy = { { ItemKey{ kStimpak }, 1 } };
  buy.expectedCapsDelta = vs.QuoteCapsDelta(buy, m, err);
  REQUIRE(buy.expectedCapsDelta == -138);
  Fo4Inventory before = player;
  REQUIRE(vs.Trade(buy, player, m, 12.f).error == BarterError::NotEnoughCaps);
  REQUIRE(player == before);
  REQUIRE(vs.Find(0x1000)->inventory.CountBase(kStimpak) == 5);

  // Stolen goods from the vendor's own faction are refused
  ItemKey stolen{ kWrench };
  stolen.stolenFrom = 0xFAC;
  player.Add(stolen, 1);
  BarterRequest fence;
  fence.vendorId = 0x1000;
  fence.sell = { { stolen, 1 } };
  vs.QuoteCapsDelta(fence, m, err);
  REQUIRE(err == BarterError::StolenFromVendor);

  // Vendor restock and persistence
  vs.Restock(3.0, 2.0, [](Vendor& v) {
    v.inventory.AddSimple(kWrench, 9);
    return 500u;
  });
  REQUIRE(vs.Find(0x1000)->caps == 500);
  auto j = vs.VendorToJson(*vs.Find(0x1000));
  auto back = VendorService::VendorFromJson(j);
  REQUIRE(back.inventory.CountBase(kWrench) == 9);
  REQUIRE(back.openHour == 8.f);
}

TEST_CASE("Player trade needs both confirmations of the same offer",
          "[fo4][F23]")
{
  Fo4Inventory a, b;
  a.AddSimple(kWrench, 3);
  b.AddSimple(kCaps, 100);
  PlayerTrade t(1, 2, kCaps);
  REQUIRE(t.SetOffer(1, { { ItemKey{ kWrench }, 2 } }, 0) ==
          BarterError::None);
  REQUIRE(t.SetOffer(2, {}, 40) == BarterError::None);
  REQUIRE(t.Commit(a, b) == BarterError::NotConfirmed);
  REQUIRE(t.Confirm(1, t.Version() - 1) == BarterError::StaleOffer);
  REQUIRE(t.Confirm(1, t.Version()) == BarterError::None);
  // B changes the offer: A's confirmation is reset
  REQUIRE(t.SetOffer(2, {}, 30) == BarterError::None);
  REQUIRE(t.Confirm(2, t.Version()) == BarterError::None);
  REQUIRE(t.Commit(a, b) == BarterError::NotConfirmed);
  REQUIRE(t.Confirm(1, t.Version()) == BarterError::None);
  REQUIRE(t.Commit(a, b) == BarterError::None);
  REQUIRE(a.CountBase(kWrench) == 1);
  REQUIRE(a.CountBase(kCaps) == 30);
  REQUIRE(b.CountBase(kWrench) == 2);
  REQUIRE(b.CountBase(kCaps) == 70);
  REQUIRE(t.SetOffer(3, {}, 0) == BarterError::NotTrading);

  // Items that disappeared before commit: nothing moves
  PlayerTrade t2(1, 2, kCaps);
  t2.SetOffer(1, { { ItemKey{ kWrench }, 5 } }, 0);
  t2.Confirm(1, t2.Version());
  t2.Confirm(2, t2.Version());
  auto a2 = a, b2 = b;
  REQUIRE(t2.Commit(a, b) == BarterError::ItemNotAvailable);
  REQUIRE(a == a2);
  REQUIRE(b == b2);
}

TEST_CASE("Locks: keys, perk gates, pins and server-rolled picks",
          "[fo4][F24]")
{
  LockService ls;
  constexpr FormId kDoor = 0xD001, kSafe = 0xD002, kBarred = 0xD003,
                   kKey = 0x9999;
  ls.SetLock(kDoor, { 25, 0, true, 0 });
  ls.SetLock(kSafe, { 75, kKey, true, 0xFAC });
  ls.SetLock(kBarred, { 251, 0, true, 0 });
  Fo4Inventory inv;
  LockpickerFacts a{ 1, 0, 5.f, 1000 };

  REQUIRE(ls.Activate(kDoor, a, inv).code == LockResultCode::NoPins);
  REQUIRE(ls.Activate(kBarred, a, inv).code == LockResultCode::Barred);
  REQUIRE(ls.Activate(kSafe, a, inv).code == LockResultCode::PerkTooLow);
  inv.AddSimple(kKey, 1);
  REQUIRE(ls.Activate(kSafe, a, inv).code == LockResultCode::OpenedWithKey);
  REQUIRE_FALSE(ls.GetLock(kSafe)->locked);

  inv.AddSimple(0xA, 100); // bobby pins
  auto begin = ls.Activate(kDoor, a, inv);
  REQUIRE(begin.code == LockResultCode::BeginLockpick);
  // Another player cannot pick the same lock at the same time
  LockpickerFacts b{ 2, 3, 5.f, 1000 };
  REQUIRE(ls.Activate(kDoor, b, inv).code == LockResultCode::Busy);
  // A forged session id gets nothing
  std::mt19937 rng(3);
  REQUIRE(ls.Attempt(begin.sessionId + 77, a, inv, rng).code ==
          LockResultCode::NoSession);
  REQUIRE(ls.Attempt(begin.sessionId, b, inv, rng).code ==
          LockResultCode::NoSession);

  int fails = 0;
  LockResult r;
  do {
    r = ls.Attempt(begin.sessionId, a, inv, rng);
    if (r.code == LockResultCode::Failed) {
      ++fails;
    }
  } while (r.code == LockResultCode::Failed);
  REQUIRE(r.code == LockResultCode::Unlocked);
  REQUIRE(r.xp == 6);
  REQUIRE(inv.CountBase(0xA) == 100u - fails); // one pin per failure
  REQUIRE_FALSE(ls.GetLock(kDoor)->locked);

  // Cell reset relocks
  ls.Relock(kDoor);
  REQUIRE(ls.GetLock(kDoor)->locked);

  // Locksmith 4: pins never break
  LockpickerFacts master{ 3, 4, 1.f, 5000 };
  ls.SetLock(0xD004, { 100, 0, true, 0 });
  auto s = ls.Activate(0xD004, master, inv);
  REQUIRE(s.code == LockResultCode::BeginLockpick);
  uint32_t pins = inv.CountBase(0xA);
  for (int i = 0; i < 20; ++i) {
    if (ls.Attempt(s.sessionId, master, inv, rng).code !=
        LockResultCode::Failed) {
      break;
    }
  }
  REQUIRE(inv.CountBase(0xA) == pins);

  // Sessions expire
  LockpickerFacts late = a;
  ls.SetLock(0xD005, { 25, 0, true, 0 });
  auto s5 = ls.Activate(0xD005, a, inv);
  late.nowMs += 200000;
  REQUIRE(ls.Attempt(s5.sessionId, late, inv, rng).code ==
          LockResultCode::SessionExpired);
}

TEST_CASE("Lockpick success rate matches the configured table", "[fo4][F24]")
{
  LockService ls;
  LockpickerFacts a{ 1, 0, 5.f, 0 };
  float chance = ls.LockpickChance(0, a); // 0.70 + 0.10
  REQUIRE(chance == Catch::Approx(0.8f));
  std::mt19937 rng(12345);
  int ok = 0;
  const int n = 10000;
  Fo4Inventory inv;
  inv.AddSimple(0xA, n * 2);
  for (int i = 0; i < n; ++i) {
    ls.SetLock(0xE000, { 25, 0, true, 0 });
    auto s = ls.Activate(0xE000, a, inv);
    if (ls.Attempt(s.sessionId, a, inv, rng).code ==
        LockResultCode::Unlocked) {
      ++ok;
    } else {
      ls.CancelLockpick(s.sessionId);
    }
  }
  REQUIRE(std::abs(ok / float(n) - chance) < 0.02f);
}

TEST_CASE("Terminal hacking: Hacker gates, attempts and lockout", "[fo4][F24]")
{
  LockService ls;
  ls.settings.baseChance[2] = 0.f; // make expert terminals hard to roll
  ls.settings.perIntelligencePoint = 0.f;
  ls.settings.perHackerRank = 0.f;
  ls.settings.minChance = 0.f;
  ls.SetTerminal(0x7E01, { 75, true, 0 });
  HackerFacts noob{ 1, 0, 3.f, 1000 };
  REQUIRE(ls.BeginHack(0x7E01, noob).code == HackResultCode::PerkTooLow);
  HackerFacts hacker{ 1, 2, 3.f, 1000 };
  auto s = ls.BeginHack(0x7E01, hacker);
  REQUIRE(s.code == HackResultCode::BeginHack);
  REQUIRE(s.attemptsLeft == 4);
  std::mt19937 rng(9);
  HackResult r;
  for (int i = 0; i < 4; ++i) {
    r = ls.HackAttempt(s.sessionId, hacker, rng);
  }
  REQUIRE(r.code == HackResultCode::LockoutStarted);
  REQUIRE(ls.BeginHack(0x7E01, hacker).code == HackResultCode::LockedOut);
  hacker.nowMs += 11000;
  REQUIRE(ls.BeginHack(0x7E01, hacker).code == HackResultCode::BeginHack);

  // Success path with a guaranteed roll
  ls.settings.baseChance[0] = 1.f;
  ls.settings.maxChance = 1.f; // no 95% cap
  ls.SetTerminal(0x7E02, { 25, true, 0 });
  auto s2 = ls.BeginHack(0x7E02, hacker);
  auto ok = ls.HackAttempt(s2.sessionId, hacker, rng);
  REQUIRE(ok.code == HackResultCode::Hacked);
  REQUIRE(ok.xp == 6);
  REQUIRE(ls.BeginHack(0x7E02, hacker).code == HackResultCode::Open);

  auto j = ls.ToJson();
  LockService restored;
  restored.LoadJson(j);
  REQUIRE_FALSE(restored.GetTerminal(0x7E02)->locked);
  REQUIRE(restored.GetTerminal(0x7E01)->locked);
}

TEST_CASE("Server-owned magazines, reload and fire rate", "[fo4][F09]")
{
  Data data;
  RangedCombat rc(data.d);
  Fo4Inventory inv;
  ItemKey gun{ k10mm };
  inv.Add(gun, 1);
  inv.AddSimple(kAmmo10mm, 30);
  std::array<float, 3> o{ 0, 0, 0 };

  auto empty = rc.Fire(1, gun, inv, o, 0);
  REQUIRE(empty.error == FireError::EmptyMagazine);
  auto rl = rc.Reload(1, gun, inv);
  REQUIRE(rl.Ok());
  REQUIRE(rl.loaded == 12);
  REQUIRE(inv.CountBase(kAmmo10mm) == 18);
  REQUIRE(rc.Reload(1, rl.weaponAfter, inv).error == FireError::MagazineFull);

  ItemKey cur = rl.weaponAfter;
  auto s1 = rc.Fire(1, cur, inv, o, 1000);
  REQUIRE(s1.Ok());
  REQUIRE(s1.ammoLeft == 11);
  cur = s1.weaponAfter;
  // 4 shots/s max; 15% tolerance -> 212 ms minimum
  REQUIRE(rc.Fire(1, cur, inv, o, 1100).error == FireError::TooFast);
  auto s2 = rc.Fire(1, cur, inv, o, 1250);
  REQUIRE(s2.Ok());
  REQUIRE(inv.Find(s2.weaponAfter)->key.ammoLoaded == 10);

  // Hit claims must match a recorded shot and projectile, once
  HitClaim hit;
  hit.shotSeq = s1.seq;
  hit.targetActorId = 2;
  hit.targetPos = { 500, 0, 0 };
  hit.claimTimeMs = 1100;
  REQUIRE(rc.ValidateHit(1, hit) == FireError::None);
  REQUIRE(rc.ValidateHit(1, hit) == FireError::ProjectileReused);
  hit.shotSeq = 999;
  REQUIRE(rc.ValidateHit(1, hit) == FireError::UnknownShot);
  hit.shotSeq = s2.seq;
  hit.targetActorId = 1;
  REQUIRE(rc.ValidateHit(1, hit) == FireError::SelfHit);
  hit.targetActorId = 2;
  hit.claimTimeMs = 9000;
  REQUIRE(rc.ValidateHit(1, hit) == FireError::ShotTooOld);
  hit.claimTimeMs = 1300;
  hit.targetPos = { 1e6f, 0, 0 };
  REQUIRE(rc.ValidateHit(1, hit) == FireError::OutOfRange);
  hit.targetAlive = false;
  REQUIRE(rc.ValidateHit(1, hit) == FireError::TargetDead);

  // Non-guns and weapons not in the inventory are rejected
  REQUIRE(rc.Fire(1, ItemKey{ kWrench }, inv, o, 5000).error ==
          FireError::NotEquipped);
  inv.AddSimple(kWrench, 1);
  REQUIRE(rc.Fire(1, ItemKey{ kWrench }, inv, o, 5000).error ==
          FireError::NotAGun);
}

TEST_CASE("Parties: lifecycle, leader rules, size cap, persistence",
          "[fo4][F32]")
{
  PartyService ps;
  REQUIRE(ps.Invite(1, 1, 0) == PartyError::CannotTargetSelf);
  REQUIRE(ps.Invite(1, 2, 0) == PartyError::None); // creates the party
  auto pid = *ps.GetPartyOf(1);
  REQUIRE(ps.Accept(3, pid, 0) == PartyError::NoInvite);
  REQUIRE(ps.Accept(2, pid, 1000) == PartyError::None);
  REQUIRE(ps.SameParty(1, 2));
  REQUIRE(ps.Invite(2, 3, 0) == PartyError::NotLeader);
  REQUIRE(ps.Invite(1, 3, 0) == PartyError::None);
  REQUIRE(ps.Accept(3, pid, 70000) == PartyError::InviteExpired);

  ps.settings.maxPartySize = 3;
  REQUIRE(ps.Invite(1, 3, 100000) == PartyError::None);
  REQUIRE(ps.Invite(1, 4, 100000) == PartyError::None);
  REQUIRE(ps.Accept(3, pid, 100001) == PartyError::None);
  REQUIRE(ps.Accept(4, pid, 100001) == PartyError::PartyFull);

  REQUIRE(ps.Kick(2, 3) == PartyError::NotLeader);
  REQUIRE(ps.Kick(1, 3) == PartyError::None);
  REQUIRE_FALSE(ps.GetPartyOf(3));
  REQUIRE(ps.Promote(1, 2) == PartyError::None);
  REQUIRE(ps.Find(pid)->leader == 2);

  auto j = ps.ToJson();
  PartyService restored;
  restored.LoadJson(j);
  REQUIRE(restored.SameParty(1, 2));
  REQUIRE(restored.Find(pid)->leader == 2);

  // Leader leaves: the longest-standing member leads; empty party removed
  REQUIRE(ps.Leave(2) == PartyError::None);
  REQUIRE(ps.Find(pid)->leader == 1);
  REQUIRE(ps.Leave(1) == PartyError::None);
  REQUIRE_FALSE(ps.Find(pid));
  REQUIRE(ps.Leave(1) == PartyError::NotInParty);
}

TEST_CASE("PvP rules: friendly fire, zones, flags and cooldowns", "[fo4][F32]")
{
  PartyService ps;
  ps.Invite(1, 2, 0);
  ps.Accept(2, *ps.GetPartyOf(1), 0);
  std::array<float, 3> here{ 0, 0, 0 };

  REQUIRE(ps.CanDamage(1, 2, here, 0x3c) == PvpVerdict::SameParty);
  REQUIRE(ps.CanDamage(1, 3, here, 0x3c) == PvpVerdict::NotFlagged);
  REQUIRE(ps.SetPvpFlag(1, true, 0) == PartyError::None); // first change
  REQUIRE(ps.SetPvpFlag(1, false, 1000) == PartyError::FlagCooldown);
  REQUIRE(ps.IsFlagged(1));
  REQUIRE(ps.SetPvpFlag(3, true, 400000) == PartyError::None);
  REQUIRE(ps.CanDamage(1, 3, here, 0x3c) == PvpVerdict::Allowed);

  ps.AddZone({ { 0, 0, 0 }, 1000, 0x3c, PvpZoneMode::Safe });
  REQUIRE(ps.CanDamage(1, 3, here, 0x3c) == PvpVerdict::SafeZone);
  REQUIRE(ps.CanDamage(1, 3, { 5000, 0, 0 }, 0x3c) == PvpVerdict::Allowed);
  ps.AddZone({ { 5000, 0, 0 }, 1000, 0x3c, PvpZoneMode::Open });
  ps.SetPvpFlag(3, false, 800000);
  REQUIRE(ps.CanDamage(1, 3, { 5000, 0, 0 }, 0x3c) == PvpVerdict::Allowed);

  // Can't unflag right after PvP damage (combat tag)
  ps.NotePvpDamage(1, 900000);
  REQUIRE(ps.SetPvpFlag(1, false, 910000) == PartyError::InCombat);

  ps.settings.friendlyFire = true;
  REQUIRE(ps.CanDamage(1, 2, { 5000, 0, 0 }, 0x3c) == PvpVerdict::Allowed);
}

TEST_CASE("Party XP sharing splits among members in range", "[fo4][F32]")
{
  PartyService ps;
  ps.Invite(1, 2, 0);
  ps.Accept(2, *ps.GetPartyOf(1), 0);
  ps.Invite(1, 3, 0);
  ps.Accept(3, *ps.GetPartyOf(1), 0);
  std::map<ProfileId, std::array<float, 3>> pos{ { 1, { 0, 0, 0 } },
                                                 { 2, { 100, 0, 0 } },
                                                 { 3, { 99999, 0, 0 } } };
  auto shares = ps.ShareXp(1, 101, pos);
  REQUIRE(shares.size() == 2);
  uint32_t total = 0;
  for (auto& [p, xp] : shares) {
    total += xp;
  }
  REQUIRE(total == 101);
  REQUIRE(ps.ShareXp(7, 50, pos) ==
          std::vector<std::pair<ProfileId, uint32_t>>{ { 7, 50 } });
}
