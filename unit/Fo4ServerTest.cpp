#include "fo4/Fo4Server.h"
#include "Fo4Messages.h"
#include "Fo4TestData.h"
#include <catch2/catch_all.hpp>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>

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
  ActorId except = 0;
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
  void OnActorKilled(ActorId v, ActorId k) override
  {
    kills.push_back({ v, k });
  }
  std::vector<std::pair<ActorId, std::array<float, 3>>> teleports;
  bool TeleportActor(ActorId a, const std::array<float, 3>& p,
                     uint32_t) override
  {
    teleports.push_back({ a, p });
    pos[a] = p;
    return true;
  }

  std::map<ActorId, ActorId> hostOf;
  ActorId GetHostOf(ActorId a) override
  {
    auto it = hostOf.find(a);
    return it == hostOf.end() ? 0 : it->second;
  }
  void SendToNeighboursExcept(ActorId a, ActorId except, const IMessageBase& m,
                              bool) override
  {
    nlohmann::json j;
    m.WriteJson(j);
    sent.push_back({ a, true, j.value("t", -1), j, except });
  }

  void SetActorTransform(ActorId a, const std::array<float, 3>& p,
                         float) override
  {
    pos[a] = p;
  }

  std::vector<std::pair<std::string, nlohmann::json>> events;
  std::set<std::string> blocked;
  bool FireGamemodeEvent(const std::string& name,
                         const nlohmann::json& args) override
  {
    events.push_back({ name, args });
    return !blocked.count(name);
  }
  int CountEvents(const std::string& name) const
  {
    int n = 0;
    for (auto& e : events) {
      n += e.first == name;
    }
    return n;
  }

  // Last message of a type sent to an actor's neighbours
  nlohmann::json LastToNeighbours(ActorId a, MsgType t)
  {
    for (auto it = sent.rbegin(); it != sent.rend(); ++it) {
      if (it->to == a && it->neighbours && it->type == int(t)) {
        return it->json;
      }
    }
    return nullptr;
  }

  std::vector<nlohmann::json> AllTo(ActorId a, MsgType t)
  {
    std::vector<nlohmann::json> out;
    for (auto& s : sent) {
      if (s.to == a && !s.neighbours && s.type == int(t)) {
        out.push_back(s.json);
      }
    }
    return out;
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
  fire.clientShotId = 77;
  w.server->OnMessage(kAlice, MsgType::WeaponFire, fire);
  auto relay = w.host.Last(kAlice, MsgType::WeaponFire);
  REQUIRE(relay["seq"] == 1);
  // The shooter's own shot id comes back to the shooter only
  REQUIRE(relay["clientShotId"] == 77);
  REQUIRE(
    w.host.LastToNeighbours(kAlice, MsgType::WeaponFire)["clientShotId"] == 0);
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

TEST_CASE("Fo4Server: workshop build flow over messages", "[fo4][Fo4Server]")
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
  REQUIRE(w.host.Last(kAlice, MsgType::ProgressionUpdate)["created"] == true);

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
  w.server->Locks().settings.maxChance = 1.f;     // no 95% cap
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

TEST_CASE("Fo4Server: equipment rules and broadcast", "[fo4][Fo4Server][F05]")
{
  ServerWorld w;
  ArmorData leather;
  leather.id = 0x5001;
  leather.bipedSlots = 0x800; // torso
  w.data->AddArmor(leather);
  ArmorData coat = leather;
  coat.id = 0x5002;
  coat.bipedSlots = 0x800 | 0x1000;
  w.data->AddArmor(coat);
  auto& a = w.server->Actor(kAlice);
  a.inventory.AddSimple(0x5001, 1);
  a.inventory.AddSimple(0x5002, 1);
  a.inventory.Add(ItemKey{ k10mm }, 1);

  UpdateEquipmentFo4Message eq;
  eq.op = UpdateEquipmentFo4Message::kEquip;
  eq.item.baseId = 0x5001;
  w.server->OnMessage(kAlice, MsgType::UpdateEquipmentFo4, eq);
  REQUIRE(a.equippedArmor.size() == 1);
  REQUIRE(w.host.SentToNeighbours(kAlice, MsgType::UpdateEquipmentFo4));
  // The coat shares the torso slot: it replaces the leather
  eq.item.baseId = 0x5002;
  w.server->OnMessage(kAlice, MsgType::UpdateEquipmentFo4, eq);
  REQUIRE(a.equippedArmor.size() == 1);
  REQUIRE(a.equippedArmor[0].baseId == 0x5002);
  // Weapon
  eq.item.baseId = k10mm;
  w.server->OnMessage(kAlice, MsgType::UpdateEquipmentFo4, eq);
  REQUIRE(a.equippedWeapon);
  // Not owned: rejected with a state correction
  eq.item.baseId = 0x9999;
  w.server->OnMessage(kAlice, MsgType::UpdateEquipmentFo4, eq);
  auto state = w.host.Last(kAlice, MsgType::UpdateEquipmentFo4);
  REQUIRE(state["armor"].size() == 1);
  REQUIRE(state["weapon"]["baseId"] == k10mm);
  // Power armor pieces can't be worn as apparel
  REQUIRE(w.server->Equip(kAlice, ItemKey{ kT45Torso }, true) ==
          "NotInInventory");
  a.inventory.AddSimple(kT45Torso, 1);
  REQUIRE(w.server->Equip(kAlice, ItemKey{ kT45Torso }, true) ==
          "PowerArmorPiece");
  // Unequip
  eq.op = UpdateEquipmentFo4Message::kUnequip;
  eq.item.baseId = 0x5002;
  w.server->OnMessage(kAlice, MsgType::UpdateEquipmentFo4, eq);
  REQUIRE(a.equippedArmor.empty());
}

TEST_CASE("Fo4Server: map discovery, fast travel and the clock",
          "[fo4][Fo4Server][F25][F26]")
{
  ServerWorld w;
  w.server->Map().AddMarker(
    { 0xAA01, { 5000, 0, 0 }, 0x3c, "Red Rocket", 0, true, false });
  w.server->Map().AddMarker(
    { 0xAA02, { 900, 0, 0 }, 0x3c, "Sanctuary", 12, true, false });
  auto& a = w.server->Actor(kAlice);

  FastTravelRequestMessage ft;
  ft.nonce = 1;
  ft.markerRefId = 0xAA02;
  w.server->OnMessage(kAlice, MsgType::FastTravelRequest, ft);
  REQUIRE(w.host.Last(kAlice, MsgType::RequestResult)["error"] ==
          "NotDiscovered");

  // Standing near Sanctuary discovers it on the next tick (+20 XP)
  w.server->Tick();
  REQUIRE(a.discoveredMarkers.count(0xAA02));
  REQUIRE_FALSE(a.discoveredMarkers.count(0xAA01));
  REQUIRE(w.host.Last(kAlice, MsgType::MapDiscovery)["markers"].size() == 1);
  REQUIRE(a.progression.xp > 0);

  // In combat: refused
  a.lastCombatMs = w.host.now;
  ft.nonce = 2;
  w.server->OnMessage(kAlice, MsgType::FastTravelRequest, ft);
  REQUIRE(w.host.Last(kAlice, MsgType::RequestResult)["error"] == "InCombat");
  a.lastCombatMs = -1000000000;
  // Over-encumbered: refused
  ConsumableData rock;
  rock.id = 0x7777;
  rock.weight = 1000;
  w.data->AddConsumable(rock);
  a.inventory.AddSimple(0x7777, 1);
  ft.nonce = 3;
  w.server->OnMessage(kAlice, MsgType::FastTravelRequest, ft);
  REQUIRE(w.host.Last(kAlice, MsgType::RequestResult)["error"] ==
          "OverEncumbered");
  a.inventory.RemoveAnyOf(0x7777, 1);
  ft.nonce = 4;
  w.server->OnMessage(kAlice, MsgType::FastTravelRequest, ft);
  REQUIRE(w.host.Last(kAlice, MsgType::RequestResult)["ok"] == true);
  REQUIRE(w.host.teleports.size() == 1);
  REQUIRE(w.host.teleports[0].second[0] == 900.f);

  // Clock: 20x, so one real minute is 20 game minutes
  auto now = w.host.now;
  w.server->Clock().SetGameDays(1.5, now); // day 1, 12:00
  REQUIRE(w.server->Clock().GameHour(now) == Catch::Approx(12.f));
  REQUIRE(w.server->Clock().GameHour(now + 60000) ==
          Catch::Approx(12.f + 20.f / 60.f));
  w.server->Clock().SetTimeScale(0.f, now + 60000);
  REQUIRE(w.server->Clock().GameHour(now + 600000) ==
          Catch::Approx(12.f + 20.f / 60.f));
  w.host.now += 20000;
  w.server->Tick();
  auto tw = w.host.Last(kAlice, MsgType::WorldTimeWeather);
  REQUIRE(tw["timeScale"] == 0.0);

  // Persistence of discoveries and clock
  auto actorJson = w.server->ActorToJson(kAlice);
  REQUIRE(actorJson["discoveredMarkers"].size() == 1);
  auto world = w.server->WorldToJson();
  Fo4Server other(w.data, w.host);
  other.LoadWorld(world);
  REQUIRE(other.Clock().TimeScale() == 0.f);
}

TEST_CASE("Fo4Server: party changes reach invitees and all members",
          "[fo4][Fo4Server]")
{
  ServerWorld w;
  w.server->Actor(kBob); // Bob is online

  PartyActionMessage invite;
  invite.nonce = 5;
  invite.op = PartyActionMessage::kInvite;
  invite.targetProfileId = 2;
  w.server->OnMessage(kAlice, MsgType::PartyAction, invite);
  auto mine = w.host.Last(kAlice, MsgType::PartyAction);
  REQUIRE(mine["nonce"] == 5);
  REQUIRE(mine["op"] == PartyActionMessage::kState);
  REQUIRE(mine["error"] == "");
  uint32_t partyId = mine["partyId"];
  REQUIRE(partyId != 0);
  // Bob is told who invited him to which party
  auto inv = w.host.Last(kBob, MsgType::PartyAction);
  REQUIRE(inv["op"] == PartyActionMessage::kInvite);
  REQUIRE(inv["partyId"] == partyId);
  REQUIRE(inv["targetProfileId"] == 1);

  // Bob accepts: Alice gets a pushed state with both members
  PartyActionMessage accept;
  accept.nonce = 9;
  accept.op = PartyActionMessage::kAccept;
  accept.partyId = partyId;
  w.server->OnMessage(kBob, MsgType::PartyAction, accept);
  auto push = w.host.Last(kAlice, MsgType::PartyAction);
  REQUIRE(push["nonce"] == 0);
  REQUIRE(push["members"].size() == 2);
  REQUIRE(w.host.Last(kBob, MsgType::PartyAction)["nonce"] == 9);

  // Alice kicks Bob: Bob is told he is out
  PartyActionMessage kick;
  kick.nonce = 6;
  kick.op = PartyActionMessage::kKick;
  kick.targetProfileId = 2;
  w.server->OnMessage(kAlice, MsgType::PartyAction, kick);
  auto out = w.host.Last(kBob, MsgType::PartyAction);
  REQUIRE(out["nonce"] == 0);
  REQUIRE(out["partyId"] == 0);

  // A failed request is not pushed to anyone else
  size_t bobMessages = w.host.AllTo(kBob, MsgType::PartyAction).size();
  PartyActionMessage badKick = kick;
  badKick.nonce = 7;
  w.server->OnMessage(kAlice, MsgType::PartyAction, badKick);
  REQUIRE(w.host.Last(kAlice, MsgType::PartyAction)["error"] ==
          "TargetNotInParty");
  REQUIRE(w.host.AllTo(kBob, MsgType::PartyAction).size() == bobMessages);
}

TEST_CASE("Fo4Server: settlement snapshots on build mode and on join",
          "[fo4][Fo4Server]")
{
  ServerWorld w;
  w.server->settings.workshopSnapshotChunk = 2;
  Workshop shop;
  shop.workbenchRefId = kShopRef;
  shop.worldOrCell = 0x3c;
  shop.owner = { WorkshopOwner::Type::Profile, 1 };
  for (FormId i = 0; i < 5; ++i) {
    PlacedObject o;
    o.refId = 0xFF300000 + i;
    o.baseId = 0x1001;
    o.pos = { float(i), 0, 0 };
    o.destroyed = i == 0;
    shop.objects[o.refId] = o;
  }
  shop.scrappedPrePlaced = { 0x0001A000 };
  shop.wires.push_back({ 0xFF3000FF, 0xFF300000, 0xFF300001, 0x1D971 });
  shop.version = 12;
  w.server->Workshops().AddWorkshop(shop);

  WorkshopModeMessage mode;
  mode.workshopRefId = kShopRef;
  w.server->OnMessage(kAlice, MsgType::WorkshopMode, mode);
  REQUIRE(w.host.Last(kAlice, MsgType::WorkshopMode)["allowed"] == true);
  auto chunks = w.host.AllTo(kAlice, MsgType::WorkshopObjects);
  REQUIRE(chunks.size() == 3); // 5 objects, 2 per chunk
  size_t objects = 0;
  for (auto& c : chunks) {
    REQUIRE(c["kind"] == 0);
    REQUIRE(c["chunkCount"] == 3);
    REQUIRE(c["version"] == 12);
    objects += c["added"].size();
  }
  REQUIRE(objects == 5);
  REQUIRE(chunks[0]["wires"].size() == 1);
  REQUIRE(chunks[0]["scrappedPrePlaced"][0] == 0x0001A000);
  REQUIRE(chunks[0]["added"][0]["flags"] ==
          WorkshopObjectsMessage::Object::kDestroyed);
  REQUIRE(w.host.Last(kAlice, MsgType::WorkshopState)["yourPerms"] ==
          WorkshopPerm::All);

  // Joining in the same worldspace sends the snapshot too
  w.host.sent.clear();
  w.server->SendFullState(kBob);
  REQUIRE(w.host.AllTo(kBob, MsgType::WorkshopObjects).size() == 3);
  REQUIRE(w.host.Last(kBob, MsgType::PartyAction)["partyId"] == 0);
}

TEST_CASE("Fo4Server: gamemode events observe and can block actions",
          "[fo4][Fo4Server]")
{
  ServerWorld w;
  auto& alice = w.server->Actor(kAlice);

  // Power armor entry blocked by the gamemode
  PowerArmorFrame f;
  f.refId = kFrameRef;
  f.pos[0] = 50;
  w.server->PowerArmor().AddFrame(f);
  w.host.blocked.insert("onFo4PowerArmorEnter");
  PowerArmorTransitionMessage enter;
  enter.nonce = 1;
  enter.frameRefId = kFrameRef;
  w.server->OnMessage(kAlice, MsgType::PowerArmorTransition, enter);
  REQUIRE(w.host.Last(kAlice, MsgType::PowerArmorTransition)["error"] ==
          "Vetoed");
  REQUIRE(w.host.events.back().second ==
          nlohmann::json::array({ kAlice, kFrameRef }));

  // Consumables blocked
  alice.inventory.AddSimple(kStimpak, 1);
  w.host.blocked.insert("onFo4UseItem");
  UseItemMessage use;
  use.nonce = 2;
  use.baseId = kStimpak;
  w.server->OnMessage(kAlice, MsgType::UseItem, use);
  REQUIRE(w.host.Last(kAlice, MsgType::RequestResult)["error"] == "Vetoed");
  REQUIRE(alice.inventory.CountBase(kStimpak) == 1);

  // PvP flag blocked, party changes observed
  w.host.blocked.insert("onFo4PvpFlagChange");
  PartyActionMessage flag;
  flag.nonce = 3;
  flag.op = PartyActionMessage::kSetPvpFlag;
  flag.value = true;
  w.server->OnMessage(kAlice, MsgType::PartyAction, flag);
  REQUIRE(w.host.Last(kAlice, MsgType::PartyAction)["error"] == "Vetoed");
  REQUIRE(!w.server->Parties().IsFlagged(1));
  PartyActionMessage invite;
  invite.nonce = 4;
  invite.op = PartyActionMessage::kInvite;
  invite.targetProfileId = 2;
  w.server->OnMessage(kAlice, MsgType::PartyAction, invite);
  REQUIRE(w.host.CountEvents("onFo4PartyChange") == 1);
  REQUIRE(w.host.events.back().second[1] == "invite");

  // Crafting is observed
  alice.inventory.AddSimple(kSteelScrap, 10);
  alice.inventory.AddSimple(kScrewScrap, 5);
  CraftItemFo4Message craft;
  craft.nonce = 5;
  craft.workbenchRefId = kBenchRef;
  craft.recipeId = kRecipeMount;
  w.server->OnMessage(kAlice, MsgType::CraftItemFo4, craft);
  REQUIRE(w.host.Last(kAlice, MsgType::RequestResult)["ok"] == true);
  REQUIRE(w.host.CountEvents("onFo4Craft") == 1);

  // Level ups are announced once per new level, not on the first sync
  w.server->SendProgression(kAlice);
  REQUIRE(w.host.CountEvents("onFo4LevelUp") == 0);
  alice.progression.AwardXp(1000, true, alice.avs);
  w.server->SendProgression(kAlice);
  REQUIRE(w.host.CountEvents("onFo4LevelUp") == 1);
  w.server->SendProgression(kAlice);
  REQUIRE(w.host.CountEvents("onFo4LevelUp") == 1);

  // Gamemode-only claims: players can't claim even without a handler
  w.server->settings.workshop.claimRule =
    WorkshopSettings::ClaimRule::Gamemode;
  Fo4Server gm(w.data, w.host, w.server->settings);
  Workshop shop;
  shop.workbenchRefId = kShopRef;
  gm.Workshops().AddWorkshop(shop);
  WorkshopManageMessage claim;
  claim.nonce = 6;
  claim.workshopRefId = kShopRef;
  gm.OnMessage(kAlice, MsgType::WorkshopManage, claim);
  REQUIRE(w.host.Last(kAlice, MsgType::RequestResult)["ok"] == false);
}

TEST_CASE("Fo4Server: movement is validated, relayed and corrected",
          "[fo4][Fo4Server][F01]")
{
  ServerWorld w;
  w.server->Movement().settings.enforceSpeed = true;
  w.server->Actor(kAlice);
  uint16_t seq = 0;
  auto move = [&](std::array<float, 3> p, uint16_t flags = 0) {
    w.host.now += 100;
    UpdateMovementFo4Message m;
    m.seq = ++seq;
    m.worldOrCell = 0x3c;
    m.pos = p;
    m.yaw = 90.f;
    m.flags = flags;
    m.ts = 12345; // client time, rewritten on relay
    w.server->OnMessage(kAlice, MsgType::UpdateMovementFo4, m);
  };

  move({ 0, 0, 0 });
  move({ 50, 0, 0 });
  REQUIRE(w.host.pos[kAlice][0] == 50.f);
  auto relay = w.host.LastToNeighbours(kAlice, MsgType::UpdateMovementFo4);
  REQUIRE(relay["idx"] == kAlice);
  REQUIRE(relay["ts"] == static_cast<uint32_t>(w.host.now));
  REQUIRE(relay["healthPercentage"] == 100);

  // A speed hack never moves the server position and ends in a teleport
  size_t relays = 0;
  for (auto& s : w.host.sent) {
    relays += s.neighbours && s.type == int(MsgType::UpdateMovementFo4);
  }
  for (int i = 1; i <= 5; ++i) {
    move({ 50.f + 400.f * i, 0, 0 });
  }
  REQUIRE(w.host.pos[kAlice][0] == 50.f);
  REQUIRE(w.host.teleports.size() == 1);
  REQUIRE(w.host.teleports[0].second[0] == 50.f);
  REQUIRE(w.host.CountEvents("onFo4MovementViolation") == 1);
  size_t relaysAfter = 0;
  for (auto& s : w.host.sent) {
    relaysAfter += s.neighbours && s.type == int(MsgType::UpdateMovementFo4);
  }
  REQUIRE(relaysAfter == relays);

  // A gamemode can waive the correction (e.g. its own scripted launch)
  w.host.blocked.insert("onFo4MovementViolation");
  w.host.now += 3000;
  for (int i = 1; i <= 5; ++i) {
    move({ 50.f + 400.f * i, 0, 0 });
  }
  REQUIRE(w.host.teleports.size() == 1);
  REQUIRE(w.host.pos[kAlice][0] > 50.f);

  // Movement for someone else's actor is ignored
  UpdateMovementFo4Message other;
  other.idx = kBob;
  other.seq = 1;
  other.pos = { 1, 2, 3 };
  w.server->OnMessage(kAlice, MsgType::UpdateMovementFo4, other);
  REQUIRE(w.host.pos[kBob][0] == 500.f);
}

TEST_CASE("Fo4Server: running in power armor drains the core",
          "[fo4][Fo4Server][F01][F17]")
{
  ServerWorld w;
  PowerArmorFrame f;
  f.refId = kFrameRef;
  f.pos[0] = 50;
  f.contents.AddSimple(kT45Torso, 1);
  f.contents.AddSimple(kFusionCore, 1);
  w.server->PowerArmor().AddFrame(f);
  PowerArmorTransitionMessage enter;
  enter.nonce = 1;
  enter.frameRefId = kFrameRef;
  w.server->OnMessage(kAlice, MsgType::PowerArmorTransition, enter);
  enter.kind = PowerArmorTransitionMessage::kAck;
  w.server->OnMessage(kAlice, MsgType::PowerArmorTransition, enter);
  REQUIRE(w.server->PowerArmor().GetPhase(kAlice) == PaPhase::In);

  float before = w.server->PowerArmor().Snapshot(kAlice).coreCharge;
  uint16_t seq = 0;
  float x = 0;
  for (int i = 0; i < 20; ++i) {
    w.host.now += 100;
    x += 45.f; // 450 u/s: running
    UpdateMovementFo4Message m;
    m.seq = ++seq;
    m.worldOrCell = 0x3c;
    m.pos = { x, 0, 0 };
    w.server->OnMessage(kAlice, MsgType::UpdateMovementFo4, m);
  }
  float after = w.server->PowerArmor().Snapshot(kAlice).coreCharge;
  REQUIRE(after < before);
  // About 2 s of running at 1/1200 per second
  REQUIRE(before - after == Catch::Approx(2.f / 1200.f).margin(0.0005));
}

TEST_CASE("Fo4Server: effects are pushed on use and on expiry",
          "[fo4][Fo4Server][F20]")
{
  ServerWorld w;
  constexpr FormId kHeal = 0xE1;
  w.server->DefineEffect({ kHeal, EffectKind::RestoreOverTime, Av::Health });
  ConsumableData stim = *w.data->FindConsumable(kStimpak);
  stim.effects = { { kHeal, 10.f, 2 } };
  w.data->AddConsumable(stim);
  auto& alice = w.server->Actor(kAlice);
  alice.inventory.AddSimple(kStimpak, 1);
  w.server->Tick();

  UseItemMessage use;
  use.nonce = 1;
  use.baseId = kStimpak;
  w.server->OnMessage(kAlice, MsgType::UseItem, use);
  auto own = w.host.Last(kAlice, MsgType::EffectsUpdate);
  REQUIRE(own["effects"].size() == 1);
  REQUIRE(own["effects"][0]["sourceItem"] == kStimpak);
  REQUIRE(own["effects"][0]["remainingMs"] == 2000);
  REQUIRE(own["effects"][0]["magnitude"] == 10.0);
  auto pub = w.host.LastToNeighbours(kAlice, MsgType::EffectsUpdate);
  REQUIRE(pub["effects"][0]["effectId"] == kHeal);
  REQUIRE(pub["effects"][0]["magnitude"] == 0.0); // visuals only
  REQUIRE(pub["addictions"].empty());

  // Expiry pushes an empty list once
  size_t before = w.host.AllTo(kAlice, MsgType::EffectsUpdate).size();
  w.host.now += 3000;
  w.server->Tick();
  auto all = w.host.AllTo(kAlice, MsgType::EffectsUpdate);
  REQUIRE(all.size() == before + 1);
  REQUIRE(all.back()["effects"].empty());
  w.host.now += 1000;
  w.server->Tick();
  REQUIRE(w.host.AllTo(kAlice, MsgType::EffectsUpdate).size() == before + 1);

  // Full state includes effects
  w.server->SendFullState(kAlice);
  REQUIRE(w.host.AllTo(kAlice, MsgType::EffectsUpdate).size() == before + 2);
}

TEST_CASE("Fo4Server: a host drives its NPC within the same rules",
          "[fo4][Fo4Server][F13]")
{
  ServerWorld w;
  constexpr ActorId kRaider = 0xFF0000AA;
  w.host.pos[kRaider] = { 1000, 0, 0 };
  w.host.hostOf[kRaider] = kAlice;
  auto& raider = w.server->Actor(kRaider);
  raider.avs.SetBase(Av::Health, 100.f);
  raider.avs.SetCurrent(Av::Health, 100.f);
  w.server->Actor(kBob);

  // Movement: the host may move it, nobody else
  UpdateMovementFo4Message mv;
  mv.idx = kRaider;
  mv.seq = 1;
  mv.worldOrCell = 0x3c;
  mv.pos = { 1010, 0, 0 };
  w.host.now += 100;
  w.server->OnMessage(kAlice, MsgType::UpdateMovementFo4, mv);
  REQUIRE(w.host.pos[kRaider][0] == 1010.f);
  auto relay = w.host.LastToNeighbours(kRaider, MsgType::UpdateMovementFo4);
  REQUIRE(relay["idx"] == kRaider);
  REQUIRE(w.host.sent.back().except == kAlice); // not echoed to the host
  mv.seq = 2;
  mv.pos = { 1020, 0, 0 };
  w.host.now += 100;
  w.server->OnMessage(kBob, MsgType::UpdateMovementFo4, mv);
  REQUIRE(w.host.pos[kRaider][0] == 1010.f);

  // Fire: the host names the gun; the server enforces the fire rate
  WeaponFireMessage fire;
  fire.shooterIdx = kRaider;
  fire.weaponBaseId = k10mm;
  fire.clientShotId = 9;
  w.server->OnMessage(kAlice, MsgType::WeaponFire, fire);
  auto echo = w.host.Last(kAlice, MsgType::WeaponFire);
  REQUIRE(echo["shooterIdx"] == kRaider);
  REQUIRE(echo["clientShotId"] == 9);
  uint32_t seq = echo["seq"];
  REQUIRE(raider.equippedWeapon->baseId == k10mm);
  size_t fires = w.host.AllTo(kAlice, MsgType::WeaponFire).size();
  w.server->OnMessage(kAlice, MsgType::WeaponFire, fire); // same instant
  REQUIRE(w.host.AllTo(kAlice, MsgType::WeaponFire).size() == fires);
  // Bob can't fire for Alice's raider; a non-gun is refused
  w.server->OnMessage(kBob, MsgType::WeaponFire, fire);
  REQUIRE(w.host.AllTo(kBob, MsgType::WeaponFire).empty());

  // The raider's hit on Bob is claimed by the host
  w.host.pos[kBob] = { 1100, 0, 0 };
  auto& bob = w.server->Actor(kBob);
  float bobHp = bob.avs.GetCurrent(Av::Health);
  HitReportMessage hit;
  hit.shooterIdx = kRaider;
  hit.shotSeq = seq;
  hit.targetIdx = kBob;
  w.server->OnMessage(kAlice, MsgType::HitReport, hit);
  REQUIRE(bob.avs.GetCurrent(Av::Health) < bobHp);
  REQUIRE(w.host.Last(kBob, MsgType::DamageApplied)["aggressorIdx"] ==
          kRaider);
  // Bob can't claim the raider's shots
  hit.projectileIndex = 0;
  float after = bob.avs.GetCurrent(Av::Health);
  w.server->OnMessage(kBob, MsgType::HitReport, hit);
  REQUIRE(bob.avs.GetCurrent(Av::Health) == after);

  // AV reports from the host are bounded and can never kill (C1)
  ChangeValuesAvMessage av;
  av.idx = kRaider;
  av.values = { { Av::Health, 0.f, 100.f } };
  w.host.now += 100;
  w.server->OnMessage(kAlice, MsgType::ChangeValuesAv, av);
  REQUIRE(raider.avs.GetCurrent(Av::Health) > 0.f);
  REQUIRE(!raider.avs.IsDead());
  // The host gets the truth back
  REQUIRE(!w.host.Last(kRaider, MsgType::ChangeValuesAv).is_null());
  // Reports from anyone else are ignored
  float hp = raider.avs.GetCurrent(Av::Health);
  w.host.now += 100;
  w.server->OnMessage(kBob, MsgType::ChangeValuesAv, av);
  REQUIRE(raider.avs.GetCurrent(Av::Health) == hp);
}

TEST_CASE("Fo4Server: appearance is validated, relayed, streamed and saved",
          "[fo4][Fo4Server][F03]")
{
  ServerWorld w;
  UpdateAppearanceFo4Message m;
  m.rev = 1;
  m.data.raceId = 0x13746;
  m.data.isFemale = true;
  m.data.headPartIds = { 0x1000, 0x1001 };
  m.data.bodyMorph = { 0.3f, 0.3f, 0.4f };
  m.data.morphSliders = { { 7, 0.5f } };
  m.data.tints = { { 3, 1, 50, 0xFF00FFFF, -1 } };
  w.server->OnMessage(kAlice, MsgType::UpdateAppearanceFo4, m);
  auto relay = w.host.LastToNeighbours(kAlice, MsgType::UpdateAppearanceFo4);
  REQUIRE(relay != nullptr);
  REQUIRE(relay["idx"] == kAlice);
  REQUIRE(relay["data"]["headPartIds"].size() == 2);
  REQUIRE(w.host.CountEvents("onFo4AppearanceChange") == 1);

  // Stale revision: rejected, the owner gets the stored appearance back
  w.host.sent.clear();
  m.data.isFemale = false;
  w.server->OnMessage(kAlice, MsgType::UpdateAppearanceFo4, m);
  REQUIRE_FALSE(w.host.SentToNeighbours(kAlice, MsgType::UpdateAppearanceFo4));
  REQUIRE(w.host.Last(
            kAlice, MsgType::UpdateAppearanceFo4)["data"]["isFemale"] == true);

  // Bad numbers are rejected
  m.rev = 2;
  m.data.bodyMorph[0] = std::numeric_limits<float>::quiet_NaN();
  w.host.sent.clear();
  w.server->OnMessage(kAlice, MsgType::UpdateAppearanceFo4, m);
  REQUIRE_FALSE(w.host.SentToNeighbours(kAlice, MsgType::UpdateAppearanceFo4));

  // Another player streaming in gets appearance and equipment of Alice
  w.host.sent.clear();
  w.server->OnStreamIn(kBob, kAlice);
  auto toBob = w.host.Last(kBob, MsgType::UpdateAppearanceFo4);
  REQUIRE(toBob["idx"] == kAlice);
  REQUIRE(toBob["data"]["isFemale"] == true);
  REQUIRE(w.host.Last(kBob, MsgType::UpdateEquipmentFo4)["actorIdx"] ==
          kAlice);

  // Her own stream-in is the full state, appearance first
  w.host.sent.clear();
  w.server->OnStreamIn(kAlice, kAlice);
  REQUIRE(w.host.sent.front().type == int(MsgType::UpdateAppearanceFo4));
  REQUIRE(w.host.Last(kAlice, MsgType::SetInventoryFo4) != nullptr);

  // Saved and loaded with the actor
  auto saved = w.server->ActorToJson(kAlice);
  REQUIRE(saved["appearanceRev"] == 1);
  ServerWorld w2;
  w2.server->LoadActor(kAlice, saved);
  w2.server->OnStreamIn(kBob, kAlice);
  REQUIRE(w2.host.Last(
            kBob, MsgType::UpdateAppearanceFo4)["data"]["tints"][0]["value"] ==
          50);
}

TEST_CASE("Fo4Server: animation events and variables are relayed with limits",
          "[fo4][Fo4Server][F02]")
{
  ServerWorld w;
  UpdateActionsMessage a;
  a.seq = 1;
  a.events = { "JumpUp", "weaponFire" };
  w.server->OnMessage(kAlice, MsgType::UpdateActions, a);
  auto relay = w.host.LastToNeighbours(kAlice, MsgType::UpdateActions);
  REQUIRE(relay["idx"] == kAlice);
  REQUIRE(relay["events"].size() == 2);
  REQUIRE(relay["ts"] == w.host.now);

  UpdateGraphVariablesMessage v;
  v.values = { { "Speed", 0, 250.f }, { "Direction", 0, 0.25f } };
  w.server->OnMessage(kAlice, MsgType::UpdateGraphVariables, v);
  REQUIRE(w.host.LastToNeighbours(
            kAlice, MsgType::UpdateGraphVariables)["values"][0]["name"] ==
          "Speed");

  // Not someone else's actor
  w.host.sent.clear();
  a.idx = kBob;
  w.server->OnMessage(kAlice, MsgType::UpdateActions, a);
  REQUIRE(w.host.sent.empty());

  // Floods are cut: 25 variable updates per second
  a.idx = 0;
  int relayed = 0;
  for (int i = 0; i < 100; ++i) {
    w.host.sent.clear();
    w.server->OnMessage(kAlice, MsgType::UpdateGraphVariables, v);
    relayed += w.host.SentToNeighbours(kAlice, MsgType::UpdateGraphVariables);
  }
  REQUIRE(relayed < 30);
  w.host.now += 1000;
  w.host.sent.clear();
  w.server->OnMessage(kAlice, MsgType::UpdateGraphVariables, v);
  REQUIRE(w.host.SentToNeighbours(kAlice, MsgType::UpdateGraphVariables));

  // Non-finite values are dropped
  w.host.sent.clear();
  v.values[0].value = std::numeric_limits<float>::infinity();
  w.server->OnMessage(kAlice, MsgType::UpdateGraphVariables, v);
  REQUIRE_FALSE(
    w.host.SentToNeighbours(kAlice, MsgType::UpdateGraphVariables));
}
