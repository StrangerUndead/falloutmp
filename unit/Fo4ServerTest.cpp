#include "Fo4Messages.h"
#include "Fo4TestData.h"
#include "fo4/Fo4Server.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using namespace fo4;
using namespace fo4test;

namespace {
constexpr ActorId kAlice = 0xFF000001, kBob = 0xFF000002;
constexpr FormId kBenchRef = 0xFF00B001, kBenchBase = 0xB000;
constexpr FormId kFrameRef = 0xFF00F001;
constexpr FormId kShopRef = 0xFF00C001;

struct Sent
{
  ActorId to;
  bool neighbours;
  int type;
  nlohmann::json json;
};

class FakeHost : public Fo4Host
{
public:
  std::map<ActorId, std::array<float, 3>> pos;
  std::map<FormId, std::array<float, 3>> refPos;
  std::map<FormId, FormId> refBase;
  std::map<ActorId, bool> dead;
  std::vector<Sent> sent;
  std::vector<std::pair<ActorId, ActorId>> kills;
  int64_t now = 100000;
  FormId nextId = 0xFF200000;

  std::array<float, 3> GetActorPos(ActorId a) override { return pos[a]; }
  uint32_t GetActorWorldOrCell(ActorId) override { return 0x3c; }
  bool IsActorAlive(ActorId a) override { return !dead[a]; }
  ProfileId GetProfileId(ActorId a) override
  {
    return a == kAlice ? 1 : a == kBob ? 2 : -1;
  }
  bool IsNpc(ActorId a) override { return a != kAlice && a != kBob; }
  std::optional<std::array<float, 3>> GetRefPos(FormId r) override
  {
    auto it = refPos.find(r);
    if (it == refPos.end())
      return std::nullopt;
    return it->second;
  }
  FormId GetRefBaseId(FormId r) override { return refBase[r]; }
  FormId AllocateFormId() override { return nextId++; }
  void SendTo(ActorId a, const IMessageBase& m, bool) override
  {
    nlohmann::json j;
    m.WriteJson(j);
    sent.push_back({ a, false, j.value("t", -1), j });
  }
  void SendToNeighbours(ActorId a, const IMessageBase& m, bool) override
  {
    nlohmann::json j;
    m.WriteJson(j);
    sent.push_back({ a, true, j.value("t", -1), j });
  }
  int64_t NowMs() override { return now; }
  float GameHour() override { return 12.f; }
  double GameDays() override { return 1.0; }
  void OnActorKilled(ActorId v, ActorId k) override
  {
    kills.push_back({ v, k });
  }

  // Last message of a type sent to an actor (not to neighbours)
  nlohmann::json Last(ActorId a, MsgType t)
  {
    for (auto it = sent.rbegin(); it != sent.rend(); ++it) {
      if (it->to == a && !it->neighbours && it->type == int(t)) {
        return it->json;
      }
    }
    return nullptr;
  }
  bool SentToNeighbours(ActorId a, MsgType t)
  {
    for (auto& s : sent) {
      if (s.to == a && s.neighbours && s.type == int(t)) {
        return true;
      }
    }
    return false;
  }
};

struct ServerWorld
{
  std::shared_ptr<InMemoryFo4DataSource> data =
    std::make_shared<InMemoryFo4DataSource>();
  FakeHost host;
  std::unique_ptr<Fo4Server> server;

  ServerWorld()
  {
    Build(*data);
    FurnitureData bench;
    bench.id = kBenchBase;
    bench.keywords = { kWorkbenchWeapons };
    bench.workbench = WorkbenchType::Weapons;
    data->AddFurniture(bench);
    server = std::make_unique<Fo4Server>(data, host);
    host.pos[kAlice] = { 0, 0, 0 };
    host.pos[kBob] = { 500, 0, 0 };
    host.refPos[kBenchRef] = { 100, 0, 0 };
    host.refBase[kBenchRef] = kBenchBase;
  }
};
}

TEST_CASE("Fo4Server: crafting and modding through messages",
          "[fo4][Fo4Server]")
{
  ServerWorld w;
  auto& alice = w.server->Actor(kAlice);
  alice.inventory.AddSimple(kSteelScrap, 10);
  alice.inventory.AddSimple(kScrewScrap, 5);
  alice.inventory.Add(ItemKey{ k10mm }, 1);

  ModItemMessage mod;
  mod.nonce = 1;
  mod.workbenchRefId = kBenchRef;
  mod.item.baseId = k10mm;
  mod.modId = kModMagLarge;
  w.server->OnMessage(kAlice, MsgType::ModItem, mod);
  auto res = w.host.Last(kAlice, MsgType::RequestResult);
  REQUIRE(res["ok"] == true);
  REQUIRE(alice.inventory.Find(ItemKey{ k10mm }.WithMods({ kModMagLarge })));
  auto inv = w.host.Last(kAlice, MsgType::SetInventoryFo4);
  REQUIRE(inv["entries"].size() > 0);

  // Too far from the workbench
  w.host.pos[kAlice] = { 5000, 0, 0 };
  mod.nonce = 2;
  mod.modId = kModMount;
  w.server->OnMessage(kAlice, MsgType::ModItem, mod);
  res = w.host.Last(kAlice, MsgType::RequestResult);
  REQUIRE(res["ok"] == false);
  REQUIRE(res["error"] == "OutOfReach");
}

TEST_CASE("Fo4Server: power armor enter is broadcast and acked",
          "[fo4][Fo4Server]")
{
  ServerWorld w;
  PowerArmorFrame f;
  f.refId = kFrameRef;
  f.baseId = 0x2079E;
  f.pos[0] = 50;
  f.contents.AddSimple(kT45Torso, 1);
  f.contents.AddSimple(kFusionCore, 1);
  w.server->PowerArmor().AddFrame(f);

  PowerArmorTransitionMessage enter;
  enter.nonce = 11;
  enter.frameRefId = kFrameRef;
  enter.kind = PowerArmorTransitionMessage::kEnter;
  w.server->OnMessage(kAlice, MsgType::PowerArmorTransition, enter);
  auto r = w.host.Last(kAlice, MsgType::PowerArmorTransition);
  REQUIRE(r["ok"] == true);
  REQUIRE(r["phase"] == 1); // entering
  REQUIRE(w.host.SentToNeighbours(kAlice, MsgType::PowerArmorState));

  // Bob can't take the same frame
  w.server->OnMessage(kBob, MsgType::PowerArmorTransition, enter);
  REQUIRE(w.host.Last(kBob, MsgType::PowerArmorTransition)["error"] ==
          "Occupied");

  PowerArmorTransitionMessage ack = enter;
  ack.kind = PowerArmorTransitionMessage::kAck;
  w.server->OnMessage(kAlice, MsgType::PowerArmorTransition, ack);
  REQUIRE(w.host.Last(kAlice, MsgType::PowerArmorTransition)["phase"] == 2);
  auto state = w.host.Last(kAlice, MsgType::PowerArmorState);
  REQUIRE(state["pieces"].size() == 1);
  REQUIRE(state["coreCharge"] == 1.0);

  // Persisted with the actor, frame persisted with the world
  auto actorJson = w.server->ActorToJson(kAlice);
  REQUIRE(actorJson.contains("powerArmor"));
  auto world = w.server->WorldToJson();
  REQUIRE(world["powerArmorFrames"].size() == 1);

  // Disconnect: forced exit puts the frame back
  w.server->RemoveActor(kAlice);
  REQUIRE(w.server->PowerArmor().FindFrame(kFrameRef)->enabled);
}

TEST_CASE("Fo4Server: shots, hits, damage, kill XP and party sharing",
          "[fo4][Fo4Server]")
{
  ServerWorld w;
  auto& alice = w.server->Actor(kAlice);
  ItemKey gun{ k10mm };
  gun.ammoLoaded = 12;
  alice.inventory.Add(gun, 1);
  alice.equippedWeapon = gun;
  constexpr ActorId kRaider = 0xFF0000AA;
  w.host.pos[kRaider] = { 300, 0, 0 };
  w.server->Actor(kRaider).avs.SetCurrent(Av::Health, 10.f);
  w.server->Actor(kRaider).actorLevelForXp = 3;

  // Alice and Bob are partied; both get kill XP
  PartyActionMessage invite;
  invite.op = PartyActionMessage::kInvite;
  invite.targetProfileId = 2;
  w.server->OnMessage(kAlice, MsgType::PartyAction, invite);
  PartyActionMessage accept;
  accept.op = PartyActionMessage::kAccept;
  accept.partyId = *w.server->Parties().GetPartyOf(1);
  w.server->OnMessage(kBob, MsgType::PartyAction, accept);
  REQUIRE(w.host.Last(kBob, MsgType::PartyAction)["members"].size() == 2);

  WeaponFireMessage fire;
  fire.direction = { 1, 0, 0 };
  w.server->OnMessage(kAlice, MsgType::WeaponFire, fire);
  auto relay = w.host.Last(kAlice, MsgType::WeaponFire);
  REQUIRE(relay["seq"] == 1);
  REQUIRE(w.host.SentToNeighbours(kAlice, MsgType::WeaponFire));
  REQUIRE(alice.equippedWeapon->ammoLoaded == 11);

  HitReportMessage hit;
  hit.shotSeq = 1;
  hit.targetIdx = kRaider;
  w.server->OnMessage(kAlice, MsgType::HitReport, hit);
  REQUIRE(w.host.kills.size() == 1);
  REQUIRE(w.host.kills[0].first == kRaider);
  REQUIRE(w.host.SentToNeighbours(kRaider, MsgType::DamageApplied));
  // 20 * level 3 = 60 XP split between Alice and Bob (INT 1: +3%)
  auto& bob = w.server->Actor(kBob);
  REQUIRE(alice.progression.xp + bob.progression.xp >= 60);
  REQUIRE(bob.progression.xp > 0);

  // Replaying the same hit claim does nothing
  w.server->OnMessage(kAlice, MsgType::HitReport, hit);
  REQUIRE(w.host.kills.size() == 1);

  // PvP between party members is blocked (no friendly fire)
  w.host.now += 1000;
  w.server->OnMessage(kAlice, MsgType::WeaponFire, fire);
  hit.shotSeq = 2;
  hit.targetIdx = kBob;
  float bobHp = bob.avs.GetCurrent(Av::Health);
  w.server->OnMessage(kAlice, MsgType::HitReport, hit);
  REQUIRE(bob.avs.GetCurrent(Av::Health) == bobHp);
}

TEST_CASE("Fo4Server: workshop build flow over messages",
          "[fo4][Fo4Server]")
{
  ServerWorld w;
  WorkshopObjectData wall;
  wall.baseId = 0x1001;
  w.data->AddWorkshopObject(wall);
  RecipeData r;
  r.id = 0x2001;
  r.createdObjectId = 0x1001;
  r.components = { { kSteel, 2 } };
  w.data->AddRecipe(r);
  Workshop shop;
  shop.workbenchRefId = kShopRef;
  w.server->Workshops().AddWorkshop(shop);
  w.server->Actor(kAlice).inventory.AddSimple(kSteelScrap, 2);

  WorkshopManageMessage claim;
  claim.nonce = 1;
  claim.workshopRefId = kShopRef;
  w.server->OnMessage(kAlice, MsgType::WorkshopManage, claim);
  REQUIRE(w.host.Last(kAlice, MsgType::RequestResult)["ok"] == true);
  REQUIRE(w.host.Last(kAlice, MsgType::WorkshopState)["ownerId"] == 1);

  WorkshopModeMessage mode;
  mode.workshopRefId = kShopRef;
  w.server->OnMessage(kAlice, MsgType::WorkshopMode, mode);
  REQUIRE(w.host.Last(kAlice, MsgType::WorkshopMode)["allowed"] == true);
  // Bob has no permission
  w.server->OnMessage(kBob, MsgType::WorkshopMode, mode);
  REQUIRE(w.host.Last(kBob, MsgType::WorkshopMode)["allowed"] == false);

  WorkshopPlaceMessage place;
  place.nonce = 2;
  place.workshopRefId = kShopRef;
  place.recipeId = 0x2001;
  place.baseId = 0x1001;
  place.pos = { 10, 10, 0 };
  w.server->OnMessage(kAlice, MsgType::WorkshopPlace, place);
  auto res = w.host.Last(kAlice, MsgType::RequestResult);
  REQUIRE(res["ok"] == true);
  uint32_t refId = res["refId"];
  REQUIRE(refId != 0);
  REQUIRE(w.host.SentToNeighbours(kAlice, MsgType::WorkshopObjects));
  REQUIRE(w.server->Actor(kAlice).inventory.CountBase(kSteelScrap) == 0);

  // World persistence carries the placed object
  auto world = w.server->WorldToJson();
  Fo4Server restored(w.data, w.host);
  restored.LoadWorld(world);
  REQUIRE(restored.Workshops().Find(kShopRef)->objects.count(refId));
}

TEST_CASE("Fo4Server: progression, consumables, locks and persistence",
          "[fo4][Fo4Server]")
{
  ServerWorld w;
  ProgressionRequestMessage create;
  create.op = ProgressionRequestMessage::kCreateCharacter;
  create.special = { 4, 4, 4, 4, 4, 4, 4 };
  w.server->OnMessage(kAlice, MsgType::ProgressionRequest, create);
  REQUIRE(w.host.Last(kAlice, MsgType::RequestResult)["ok"] == true);
  REQUIRE(w.host.Last(kAlice, MsgType::ProgressionUpdate)["created"] ==
          true);

  // Stimpak heals over time on the server
  constexpr FormId kHeal = 0xE1;
  w.server->DefineEffect({ kHeal, EffectKind::RestoreOverTime, Av::Health });
  ConsumableData stim = *w.data->FindConsumable(kStimpak);
  stim.effects = { { kHeal, 10.f, 4 } };
  w.data->AddConsumable(stim);
  auto& alice = w.server->Actor(kAlice);
  alice.avs.Damage(Av::Health, -50.f);
  float hp = alice.avs.GetCurrent(Av::Health);
  alice.inventory.AddSimple(kStimpak, 1);
  UseItemMessage use;
  use.baseId = kStimpak;
  w.server->OnMessage(kAlice, MsgType::UseItem, use);
  REQUIRE(w.host.Last(kAlice, MsgType::RequestResult)["ok"] == true);
  w.server->Tick();
  w.host.now += 4000;
  w.server->Tick();
  REQUIRE(alice.avs.GetCurrent(Av::Health) == Catch::Approx(hp + 40.f));

  // Lockpicking over messages
  constexpr FormId kDoor = 0xFF00D001;
  w.host.refPos[kDoor] = { 10, 0, 0 };
  w.server->Locks().settings.baseChance[0] = 1.f; // deterministic
  w.server->Locks().SetLock(kDoor, { 25, 0, true, 0 });
  alice.inventory.AddSimple(0xA, 3);
  LockpickAttemptMessage begin;
  begin.op = 0;
  begin.refId = kDoor;
  w.server->OnMessage(kAlice, MsgType::LockpickAttempt, begin);
  auto b = w.host.Last(kAlice, MsgType::LockpickAttempt);
  REQUIRE(b["outcome"] == int(LockResultCode::BeginLockpick));
  LockpickAttemptMessage attempt;
  attempt.op = 1;
  attempt.refId = kDoor;
  attempt.sessionId = b["sessionId"];
  w.server->OnMessage(kAlice, MsgType::LockpickAttempt, attempt);
  auto o = w.host.Last(kAlice, MsgType::LockpickAttempt);
  REQUIRE(o["outcome"] == int(LockResultCode::Unlocked));
  REQUIRE(o["xp"] == 6);
  REQUIRE(alice.progression.xp > 0);

  // Actor persistence round trip
  auto j = w.server->ActorToJson(kAlice);
  Fo4Server other(w.data, w.host);
  other.LoadActor(kAlice, j);
  auto& again = other.Actor(kAlice);
  REQUIRE(again.progression.created);
  REQUIRE(again.inventory.CountBase(0xA) == 3);
  REQUIRE(again.avs.GetCurrent(Av::Health) ==
          Catch::Approx(alice.avs.GetCurrent(Av::Health)));
  // A broken part does not lose the rest
  j["avs"] = "garbage";
  Fo4Server third(w.data, w.host);
  third.LoadActor(kAlice, j);
  REQUIRE(third.Actor(kAlice).progression.created);
}

TEST_CASE("Fo4Server: container and corpse looting over messages",
          "[fo4][Fo4Server]")
{
  ServerWorld w;
  constexpr FormId kChestRef = 0xFF00C111, kChestBase = 0xC0;
  w.data->AddContainer({ kChestBase, { { kStimpak, 3 } }, false });
  w.host.refPos[kChestRef] = { 50, 0, 0 };
  w.host.refBase[kChestRef] = kChestBase;

  TakeItemFo4Message peek;
  peek.nonce = 1;
  peek.refId = kChestRef;
  peek.count = 0;
  w.server->OnMessage(kAlice, MsgType::TakeItemFo4, peek);
  auto contents = w.host.Last(kAlice, MsgType::SetInventoryFo4);
  REQUIRE(contents["refId"] == kChestRef);
  REQUIRE(contents["entries"][0]["count"] == 3);

  TakeItemFo4Message take = peek;
  take.nonce = 2;
  take.item.baseId = kStimpak;
  take.count = 2;
  w.server->OnMessage(kAlice, MsgType::TakeItemFo4, take);
  REQUIRE(w.host.Last(kAlice, MsgType::RequestResult)["ok"] == true);
  REQUIRE(w.server->Actor(kAlice).inventory.CountBase(kStimpak) == 2);

  // A locked chest refuses
  w.server->Locks().SetLock(kChestRef, { 50, 0, true, 0 });
  take.nonce = 3;
  take.count = 1;
  w.server->OnMessage(kAlice, MsgType::TakeItemFo4, take);
  REQUIRE(w.host.Last(kAlice, MsgType::RequestResult)["error"] == "Locked");

  // Corpse looting: a dead raider's inventory
  constexpr ActorId kRaider = 0xFF0000BB;
  w.server->Actor(kRaider).inventory.AddSimple(kAmmo10mm, 20);
  w.host.pos[kRaider] = { 100, 0, 0 };
  w.host.dead[kRaider] = true;
  TakeItemFo4Message loot;
  loot.nonce = 4;
  loot.refId = kRaider;
  loot.item.baseId = kAmmo10mm;
  loot.count = 20;
  w.server->OnMessage(kAlice, MsgType::TakeItemFo4, loot);
  REQUIRE(w.host.Last(kAlice, MsgType::RequestResult)["ok"] == true);
  REQUIRE(w.server->Actor(kAlice).inventory.CountBase(kAmmo10mm) == 20);
  REQUIRE(w.server->Actor(kRaider).inventory.IsEmpty());

  // Drop: ground stack shown to neighbours
  DropItemFo4Message drop;
  drop.nonce = 5;
  drop.item.baseId = kStimpak;
  drop.count = 1;
  w.server->OnMessage(kAlice, MsgType::DropItemFo4, drop);
  auto r = w.host.Last(kAlice, MsgType::RequestResult);
  REQUIRE(r["ok"] == true);
  REQUIRE(w.host.SentToNeighbours(kAlice, MsgType::SetInventoryFo4));
  REQUIRE(w.server->Containers().Find(r["refId"])->isGroundStack);
}
