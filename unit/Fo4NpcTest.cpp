#include "Fo4Messages.h"
#include "Fo4TestData.h"
#include "fo4/Fo4Server.h"
#include "fo4/Npcs.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using namespace fo4;
using namespace fo4test;

namespace {
constexpr FormId kRaiderBase = 0x100, kRaiderTemplate = 0x200,
                 kRaiderLeveled = 0x210, kRaiderBoss = 0x220,
                 kLootList = 0x400, kOutfit = 0x300, kHelmet = 0x3001,
                 kPipeGun = 0x4823;

std::shared_ptr<InMemoryFo4DataSource> NpcWorld()
{
  auto d = std::make_shared<InMemoryFo4DataSource>();
  Build(*d);
  WeaponData pipe;
  pipe.id = kPipeGun;
  pipe.isGun = true;
  pipe.type = ItemType::Weapon;
  pipe.baseDamage = 9;
  pipe.capacity = 8;
  pipe.ammoId = kAmmo10mm;
  d->AddWeapon(pipe);
  ArmorData helmet;
  helmet.id = kHelmet;
  helmet.type = ItemType::Armor;
  helmet.bipedSlots = 1;
  d->AddArmor(helmet);

  NpcData tmpl; // stats and loadout live on the template
  tmpl.id = kRaiderTemplate;
  tmpl.pcLevelMult = true;
  tmpl.levelMult = 1.5f;
  tmpl.calcMinLevel = 2;
  tmpl.calcMaxLevel = 10;
  tmpl.factions = { { 0x1BC, 0 } };
  tmpl.items = { { kLootList, 1 } };
  tmpl.defaultOutfit = kOutfit;
  d->AddNpc(tmpl);

  NpcData boss = tmpl;
  boss.id = kRaiderBoss;
  boss.pcLevelMult = false;
  boss.level = 25;
  boss.items = { { kPipeGun, 1 } };
  d->AddNpc(boss);

  NpcData raider; // placed base: everything from the template
  raider.id = kRaiderBase;
  raider.templateFlags = (1u << NpcData::kStats) |
    (1u << NpcData::kFactions) | (1u << NpcData::kInventory);
  raider.defaultTemplate = kRaiderTemplate;
  raider.flags = 1u << 1; // essential (base data stays on the raider)
  d->AddNpc(raider);

  LeveledListData loot;
  loot.id = kLootList;
  loot.entries = { { 1, kPipeGun, 1 } };
  d->AddLeveledList(loot);
  LeveledListData pick;
  pick.id = kRaiderLeveled;
  pick.entries = { { 1, kRaiderBoss, 1 } };
  d->AddLeveledList(pick);
  d->AddOutfit({ kOutfit, { kHelmet } });
  return d;
}
}

TEST_CASE("NpcResolver: aspects come from templates, levels scale",
          "[fo4][Npcs]")
{
  auto d = NpcWorld();
  NpcResolver res(*d);
  std::mt19937 rng(1);
  auto r = res.Resolve(kRaiderBase, 4, rng);
  REQUIRE(r);
  REQUIRE(r->baseId == kRaiderBase);
  REQUIRE(r->level == 6); // 4 * 1.5
  REQUIRE(r->factions.size() == 1);
  REQUIRE((r->flags & (1u << 1)) != 0); // base data from the raider itself
  REQUIRE(r->outfit == std::vector<FormId>{ kHelmet });
  auto has = [&](FormId id) {
    for (auto& c : r->inventory) {
      if (c.componentId == id) {
        return true;
      }
    }
    return false;
  };
  REQUIRE(has(kPipeGun));
  REQUIRE(has(kHelmet));

  // Clamped by calc min/max
  REQUIRE(res.Resolve(kRaiderBase, 1, rng)->level == 2);
  REQUIRE(res.Resolve(kRaiderBase, 50, rng)->level == 10);

  // A leveled NPC base picks one NPC_
  auto b = res.Resolve(kRaiderLeveled, 30, rng);
  REQUIRE(b);
  REQUIRE(b->baseId == kRaiderBoss);
  REQUIRE(b->level == 25);

  REQUIRE(!res.Resolve(0x999, 1, rng));
}

TEST_CASE("NpcResolver: template cycles terminate", "[fo4][Npcs]")
{
  InMemoryFo4DataSource d;
  NpcData a;
  a.id = 1;
  a.templateFlags = 0xFFFF;
  a.defaultTemplate = 2;
  a.level = 3;
  NpcData b = a;
  b.id = 2;
  b.defaultTemplate = 1;
  b.level = 7;
  d.AddNpc(a);
  d.AddNpc(b);
  NpcResolver res(d);
  std::mt19937 rng(1);
  auto r = res.Resolve(1, 1, rng);
  REQUIRE(r);
  REQUIRE((r->level == 3 || r->level == 7));
}

namespace {
class NpcHost : public Fo4Host
{
public:
  std::map<ActorId, std::array<float, 3>> pos;
  std::map<FormId, FormId> base;
  std::map<ActorId, ActorId> hostOf;
  std::vector<std::pair<int, nlohmann::json>> sent;
  int64_t now = 1000;
  std::array<float, 3> GetActorPos(ActorId a) override { return pos[a]; }
  uint32_t GetActorWorldOrCell(ActorId) override { return 0x3c; }
  bool IsActorAlive(ActorId) override { return true; }
  ProfileId GetProfileId(ActorId a) override
  {
    return a == 0xFF000001 ? 1 : -1;
  }
  bool IsNpc(ActorId a) override { return a != 0xFF000001; }
  std::optional<std::array<float, 3>> GetRefPos(FormId) override
  {
    return std::nullopt;
  }
  FormId GetRefBaseId(FormId r) override { return base[r]; }
  FormId AllocateFormId() override { return 0xFF900000; }
  void SendTo(ActorId, const IMessageBase& m, bool) override
  {
    nlohmann::json j;
    m.WriteJson(j);
    sent.push_back({ j.value("t", -1), j });
  }
  void SendToNeighbours(ActorId, const IMessageBase&, bool) override {}
  int64_t NowMs() override { return now; }
  void OnActorKilled(ActorId, ActorId) override {}
  ActorId GetHostOf(ActorId a) override
  {
    auto it = hostOf.find(a);
    return it == hostOf.end() ? 0 : it->second;
  }
};
}

TEST_CASE("Fo4Server: NPCs are seeded from data and fire their own guns",
          "[fo4][Npcs][F13]")
{
  auto d = NpcWorld();
  NpcHost host;
  Fo4Server server(d, host);
  constexpr ActorId kPlayer = 0xFF000001, kRaider = 0x0001A000;
  host.pos[kPlayer] = { 0, 0, 0 };
  host.pos[kRaider] = { 100, 0, 0 };
  host.base[kRaider] = kRaiderBase;
  host.hostOf[kRaider] = kPlayer;
  server.Actor(kPlayer).progression.level = 6;

  auto& raider = server.Actor(kRaider);
  REQUIRE(raider.npcInitialized);
  REQUIRE(raider.level == 9); // 6 * 1.5 from the nearby player
  REQUIRE(raider.actorLevelForXp == 9);
  REQUIRE(raider.equippedWeapon);
  REQUIRE(raider.equippedWeapon->baseId == kPipeGun);
  REQUIRE(raider.equippedWeapon->ammoLoaded == 8);
  REQUIRE(raider.equippedArmor.size() == 1);
  REQUIRE(raider.avs.GetCurrent(Av::Health) ==
          Catch::Approx(server.settings.npcHealthBase +
                        server.settings.npcHealthPerLevel * 9));

  // Its own gun fires; a gun it doesn't carry is refused
  WeaponFireMessage fire;
  fire.shooterIdx = kRaider;
  fire.weaponBaseId = kPipeGun;
  server.OnMessage(kPlayer, MsgType::WeaponFire, fire);
  REQUIRE(host.sent.back().first == static_cast<int>(MsgType::WeaponFire));
  size_t n = host.sent.size();
  host.now += 2000;
  fire.weaponBaseId = k10mm;
  server.OnMessage(kPlayer, MsgType::WeaponFire, fire);
  REQUIRE(host.sent.size() == n);
  REQUIRE(raider.equippedWeapon->baseId == kPipeGun);
}

TEST_CASE("Fo4Server: essential NPCs can't die, invulnerable take nothing",
          "[fo4][Npcs][F13]")
{
  auto d = NpcWorld();
  NpcHost host;
  Fo4Server server(d, host);
  constexpr ActorId kPlayer = 0xFF000001, kRaider = 0x0001A000,
                    kGhost = 0x0001A001;
  host.pos[kRaider] = { 100, 0, 0 };
  host.pos[kGhost] = { 100, 0, 0 };
  host.base[kRaider] = kRaiderBase; // essential
  NpcData ghost;
  ghost.id = 0x110;
  ghost.flags = 1u << 31;
  d->AddNpc(ghost);
  host.base[kGhost] = 0x110;

  auto& player = server.Actor(kPlayer);
  ItemKey gun{ k10mm };
  gun.ammoLoaded = 12;
  player.inventory.Add(gun, 1);
  player.equippedWeapon = gun;

  auto shootAt = [&](ActorId target) {
    host.now += 2000;
    WeaponFireMessage fire;
    server.OnMessage(kPlayer, MsgType::WeaponFire, fire);
    uint32_t seq = 0;
    for (auto it = host.sent.rbegin(); it != host.sent.rend(); ++it) {
      if (it->first == static_cast<int>(MsgType::WeaponFire)) {
        seq = it->second["seq"];
        break;
      }
    }
    HitReportMessage hit;
    hit.shotSeq = seq;
    hit.targetIdx = target;
    hit.limb = 1;
    server.OnMessage(kPlayer, MsgType::HitReport, hit);
  };
  auto& raider = server.Actor(kRaider);
  raider.avs.SetCurrent(Av::Health, 2.f);
  for (int i = 0; i < 5; ++i) {
    shootAt(kRaider);
  }
  REQUIRE(raider.avs.GetCurrent(Av::Health) == Catch::Approx(1.f));
  REQUIRE(!raider.avs.IsDead());

  auto& g = server.Actor(kGhost);
  float hp = g.avs.GetCurrent(Av::Health);
  shootAt(kGhost);
  REQUIRE(g.avs.GetCurrent(Av::Health) == hp);
}
