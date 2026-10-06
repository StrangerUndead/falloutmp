#include "Fo4TestData.h"
#include "fo4/Workshop.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using namespace fo4;
using namespace fo4test;

namespace {
constexpr FormId kSanctuary = 0xFF0010AA;
constexpr FormId kWoodWall = 0x1001, kGenerator = 0x1002, kLight = 0x1003,
                 kCrop = 0x1004, kPump = 0x1005, kBed = 0x1006,
                 kTurret = 0x1007, kBeacon = 0x1008, kPylon = 0x1009,
                 kCarWreck = 0x100A;
constexpr FormId kRecWall = 0x2001, kRecGen = 0x2002, kRecLight = 0x2003,
                 kRecCrop = 0x2004, kRecPump = 0x2005, kRecBed = 0x2006,
                 kRecTurret = 0x2007, kRecBeacon = 0x2008, kRecPylon = 0x2009,
                 kRecCar = 0x200A;
constexpr FormId kWorkshopKw = 0x54BA6, kLocalLeader = 0x4D88D;
constexpr FormId kMutfruit = 0x3001, kWater = 0x3002;
constexpr ActorId kPlayer = 0xFF000001, kFriend = 0xFF000002,
                  kStranger = 0xFF000003;

struct ShopWorld
{
  InMemoryFo4DataSource data;
  std::unique_ptr<WorkshopService> ws;
  Fo4Inventory builder;
  FormId nextId = 0xFF100000;

  ShopWorld()
  {
    Build(data);
    auto obj = [&](FormId base, FormId recipe,
                   std::vector<ComponentCount> cost, WorkshopObjectData info) {
      info.baseId = base;
      data.AddWorkshopObject(info);
      RecipeData r;
      r.id = recipe;
      r.createdObjectId = base;
      r.workbenchKeywordId = kWorkshopKw;
      r.components = std::move(cost);
      data.AddRecipe(r);
    };
    obj(kWoodWall, kRecWall, { { kSteel, 2 } }, { .budgetCost = 1.f });
    WorkshopObjectData gen;
    gen.powerGenerated = 3;
    gen.budgetCost = 2;
    obj(kGenerator, kRecGen, { { kSteel, 4 } }, gen);
    WorkshopObjectData light;
    light.powerRequired = 1;
    light.happiness = 2;
    obj(kLight, kRecLight, { { kSteel, 1 } }, light);
    WorkshopObjectData crop;
    crop.food = 0.5f;
    crop.requiresWorker = true;
    obj(kCrop, kRecCrop, {}, crop);
    WorkshopObjectData pump;
    pump.water = 3;
    obj(kPump, kRecPump, { { kSteel, 1 } }, pump);
    WorkshopObjectData bed;
    bed.isBed = true;
    obj(kBed, kRecBed, {}, bed);
    WorkshopObjectData turret;
    turret.defense = 6;
    turret.powerRequired = 2;
    obj(kTurret, kRecTurret, { { kSteel, 2 } }, turret);
    WorkshopObjectData beacon;
    beacon.isRecruitmentBeacon = true;
    beacon.powerRequired = 1;
    obj(kBeacon, kRecBeacon, {}, beacon);
    WorkshopObjectData pylon;
    pylon.isPowerConnector = true;
    obj(kPylon, kRecPylon, {}, pylon);
    obj(kCarWreck, kRecCar, { { kSteel, 10 } }, {});

    WorkshopSettings s;
    s.foodItemId = kMutfruit;
    s.waterItemId = kWater;
    s.defaultBudget = 20;
    ws = std::make_unique<WorkshopService>(data, s);
    ws->allocateFormId = [this] { return nextId++; };

    Workshop w;
    w.workbenchRefId = kSanctuary;
    WorkshopArea area;
    area.shape = WorkshopArea::Shape::Box;
    area.center = { 0, 0, 0 };
    area.halfExtents = { 4000, 4000, 2000 };
    w.areas = { area };
    ws->AddWorkshop(w);
  }

  WorkshopActor Actor(ActorId id = kPlayer, ProfileId p = 1)
  {
    WorkshopActor a;
    a.actorId = id;
    a.profileId = p;
    a.pos = { 100, 100, 0 };
    a.nowMs = now;
    return a;
  }

  int64_t now = 10000;
  uint32_t nonce = 1;

  WorkshopResult Place(FormId base, FormId recipe,
                       std::array<float, 3> pos = { 200, 200, 0 })
  {
    now += 200; // stay under the rate limit
    PlaceRequest r;
    r.nonce = nonce++;
    r.workshopId = kSanctuary;
    r.recipeId = recipe;
    r.baseId = base;
    r.pos = pos;
    return ws->Place(Actor(), r, builder);
  }

  void ClaimAndEnter()
  {
    REQUIRE(ws->Claim(Actor(), kSanctuary).Ok());
    REQUIRE(ws->EnterBuildMode(Actor(), kSanctuary).Ok());
  }

  const Workshop& W() { return *ws->Find(kSanctuary); }
};
}

TEST_CASE("Claiming a workshop and permissions", "[fo4][F22][Workshop]")
{
  ShopWorld w;
  REQUIRE(w.ws->EnterBuildMode(w.Actor(), kSanctuary).error ==
          WorkshopError::NotOwned);
  REQUIRE(w.ws->Claim(w.Actor(), kSanctuary).Ok());
  REQUIRE(w.ws->Claim(w.Actor(kStranger, 3), kSanctuary).error ==
          WorkshopError::AlreadyOwned);
  REQUIRE(w.ws->EnterBuildMode(w.Actor(kStranger, 3), kSanctuary).error ==
          WorkshopError::NoPermission);

  // Owner grants build+container to a friend
  REQUIRE(w.ws
            ->SetAcl(w.Actor(), kSanctuary, 2,
                     WorkshopPerm::Build | WorkshopPerm::Container)
            .Ok());
  REQUIRE(w.ws->EnterBuildMode(w.Actor(kFriend, 2), kSanctuary).Ok());
  // The friend cannot administer or scrap
  REQUIRE(w.ws->SetAcl(w.Actor(kFriend, 2), kSanctuary, 3, WorkshopPerm::All)
            .error == WorkshopError::NoPermission);
  // Revoking build kicks the friend out of build mode
  REQUIRE(w.ws->SetAcl(w.Actor(), kSanctuary, 2, 0).Ok());
  REQUIRE_FALSE(w.ws->GetBuildModeWorkshop(kFriend));

  // Claim limit
  w.ws->settings.maxPerPlayer = 1;
  Workshop other;
  other.workbenchRefId = 0xFF0010BB;
  w.ws->AddWorkshop(other);
  REQUIRE(w.ws->Claim(w.Actor(), 0xFF0010BB).error ==
          WorkshopError::ClaimLimit);

  // A living boss blocks the claim
  Workshop guarded;
  guarded.workbenchRefId = 0xFF0010CC;
  guarded.claimBlocked = true;
  w.ws->AddWorkshop(guarded);
  REQUIRE(w.ws->Claim(w.Actor(kFriend, 2), 0xFF0010CC).error ==
          WorkshopError::ClaimBlocked);

  REQUIRE(w.ws->Abandon(w.Actor(), kSanctuary).Ok());
  REQUIRE(w.W().owner.type == WorkshopOwner::Type::None);
}

TEST_CASE("Placing objects validates mode, area, transform, recipe and "
          "budget",
          "[fo4][F22][Workshop]")
{
  ShopWorld w;
  REQUIRE(w.ws->Claim(w.Actor(), kSanctuary).Ok());
  REQUIRE(w.Place(kWoodWall, kRecWall).error == WorkshopError::NotInBuildMode);
  REQUIRE(w.ws->EnterBuildMode(w.Actor(), kSanctuary).Ok());

  // Components: workshop container first, then the builder
  w.ws->Find(kSanctuary)->container.AddSimple(kSteelScrap, 1);
  w.builder.AddSimple(kWrench, 1); // 2 steel when scrapped
  auto ok = w.Place(kWoodWall, kRecWall);
  REQUIRE(ok.Ok());
  REQUIRE(ok.refId != 0);
  REQUIRE(w.W().container.CountBase(kSteelScrap) == 0);
  REQUIRE(w.builder.CountBase(kWrench) == 0);
  REQUIRE(w.builder.CountBase(kSteelScrap) == 1); // leftover from wrench
  REQUIRE(w.W().objects.size() == 1);
  REQUIRE(w.W().budget.current == 1.f);

  // Not enough components: nothing changes
  auto version = w.W().version;
  auto fail = w.Place(kGenerator, kRecGen);
  REQUIRE(fail.error == WorkshopError::MissingComponents);
  REQUIRE(fail.shortfalls.size() == 1);
  REQUIRE(w.W().version == version);
  REQUIRE(w.builder.CountBase(kSteelScrap) == 1);

  REQUIRE(w.Place(kWoodWall, kRecGen).error == WorkshopError::RecipeMismatch);
  REQUIRE(w.Place(kWoodWall, 0xBAD).error == WorkshopError::UnknownRecipe);
  REQUIRE(w.Place(kBed, kRecBed, { 9000, 0, 0 }).error ==
          WorkshopError::TooFar);
  REQUIRE(w.Place(kBed, kRecBed, { 2800, 2800, 0 }).Ok());
  REQUIRE(w.Place(kBed, kRecBed, { NAN, 0, 0 }).error ==
          WorkshopError::InvalidTransform);

  // Outside the build area (area ends at 4000 + tolerance)
  w.ws->settings.maxBuildDistance = 100000;
  REQUIRE(w.Place(kBed, kRecBed, { 5000, 0, 0 }).error ==
          WorkshopError::OutsideBuildArea);

  // Budget: 20 units, walls cost 1 each, beds cost 1
  w.builder.AddSimple(kSteelScrap, 100);
  int placed = 0;
  while (w.Place(kWoodWall, kRecWall).Ok()) {
    ++placed;
  }
  REQUIRE(w.W().budget.current == Catch::Approx(20.f));
  REQUIRE(w.Place(kWoodWall, kRecWall).error == WorkshopError::OverBudget);
}

TEST_CASE("Workshop requests are rate limited and deduplicated",
          "[fo4][F22][Workshop]")
{
  ShopWorld w;
  w.ClaimAndEnter();
  PlaceRequest r;
  r.workshopId = kSanctuary;
  r.recipeId = kRecBed;
  r.baseId = kBed;
  r.pos = { 200, 200, 0 };
  r.nonce = 500;
  REQUIRE(w.ws->Place(w.Actor(), r, w.builder).Ok());
  REQUIRE(w.ws->Place(w.Actor(), r, w.builder).error ==
          WorkshopError::DuplicateNonce);
  int ok = 0;
  for (uint32_t i = 0; i < 20; ++i) {
    r.nonce = 1000 + i;
    if (w.ws->Place(w.Actor(), r, w.builder).Ok()) {
      ++ok;
    }
  }
  REQUIRE(ok == 8); // 10 per second, two already used in this second
}

TEST_CASE("Power flows through wires; consumers fill capacity in order",
          "[fo4][F22][Workshop]")
{
  ShopWorld w;
  w.ClaimAndEnter();
  w.builder.AddSimple(kSteelScrap, 50);
  auto gen = w.Place(kGenerator, kRecGen).refId;
  auto pylon = w.Place(kPylon, kRecPylon).refId;
  auto l1 = w.Place(kLight, kRecLight).refId;
  auto l2 = w.Place(kLight, kRecLight).refId;
  auto turret = w.Place(kTurret, kRecTurret).refId;
  auto wall = w.Place(kWoodWall, kRecWall).refId;

  REQUIRE_FALSE(w.W().objects.at(l1).powered);
  REQUIRE(w.ws->ConnectWire(w.Actor(), kSanctuary, gen, pylon, 0, 9001).Ok());
  REQUIRE(w.ws->ConnectWire(w.Actor(), kSanctuary, pylon, l1, 0, 9002).Ok());
  REQUIRE(w.ws->ConnectWire(w.Actor(), kSanctuary, pylon, l2, 0, 9003).Ok());
  REQUIRE(
    w.ws->ConnectWire(w.Actor(), kSanctuary, pylon, turret, 0, 9004).Ok());
  // Capacity 3: light 1 + light 1 fit, the turret (2) does not
  REQUIRE(w.W().objects.at(l1).powered);
  REQUIRE(w.W().objects.at(l2).powered);
  REQUIRE_FALSE(w.W().objects.at(turret).powered);
  REQUIRE(w.W().ratings.powerCapacity == 3.f);
  REQUIRE(w.W().ratings.powerLoad == 4.f);
  REQUIRE(w.W().ratings.safety == 0.f); // unpowered turret gives nothing

  // Invalid wires
  REQUIRE(
    w.ws->ConnectWire(w.Actor(), kSanctuary, gen, pylon, 0, 9005).error ==
    WorkshopError::WireDuplicate);
  REQUIRE(w.ws->ConnectWire(w.Actor(), kSanctuary, gen, wall, 0, 9006).error ==
          WorkshopError::WireInvalid);
  auto far = w.Place(kLight, kRecLight, { 2000, 2000, 0 }).refId;
  REQUIRE(far != 0);
  REQUIRE(
    w.ws->ConnectWire(w.Actor(), kSanctuary, pylon, far, 0, 9007).error ==
    WorkshopError::WireTooLong);

  // Scrapping a light frees capacity for the turret and removes its wire
  REQUIRE(w.ws
            ->Edit(w.Actor(), kSanctuary, WorkshopEditOp::Scrap, { { l1 } },
                   w.builder, 9100)
            .Ok());
  REQUIRE(w.W().wires.size() == 3);
  REQUIRE(w.W().objects.at(turret).powered);
  REQUIRE(w.W().ratings.safety == 6.f);
}

TEST_CASE("Scrap refunds into the workshop, store makes placement free",
          "[fo4][F22][Workshop]")
{
  ShopWorld w;
  w.ClaimAndEnter();
  w.builder.AddSimple(kSteelScrap, 2);
  auto wall = w.Place(kWoodWall, kRecWall).refId;
  REQUIRE(w.builder.CountBase(kSteelScrap) == 0);

  auto scrap = w.ws->Edit(w.Actor(), kSanctuary, WorkshopEditOp::Scrap,
                          { { wall } }, w.builder, 77);
  REQUIRE(scrap.Ok());
  REQUIRE(w.W().container.CountBase(kSteelScrap) == 2);
  REQUIRE(w.W().objects.empty());
  REQUIRE(w.W().budget.current == 0.f);

  auto wall2 = w.Place(kWoodWall, kRecWall).refId;
  REQUIRE(w.ws
            ->Edit(w.Actor(), kSanctuary, WorkshopEditOp::Store, { { wall2 } },
                   w.builder, 78)
            .Ok());
  REQUIRE(w.W().stored.at(kWoodWall) == 1);
  PlaceRequest fromStore;
  fromStore.nonce = 79;
  fromStore.workshopId = kSanctuary;
  fromStore.baseId = kWoodWall;
  fromStore.fromStored = true;
  fromStore.pos = { 10, 10, 0 };
  w.now += 1000;
  REQUIRE(w.ws->Place(w.Actor(), fromStore, w.builder).Ok());
  REQUIRE(w.W().stored.empty());
  fromStore.nonce = 80;
  REQUIRE(w.ws->Place(w.Actor(), fromStore, w.builder).error ==
          WorkshopError::NotStored);

  // Moving checks the transform; scrapping unknown refs fails cleanly
  auto id = w.W().objects.begin()->first;
  REQUIRE(w.ws
            ->Edit(w.Actor(), kSanctuary, WorkshopEditOp::Move,
                   { { id, { 300, 300, 10 }, { 0, 0, 1 } } }, w.builder, 81)
            .Ok());
  REQUIRE(w.W().objects.at(id).pos[2] == 10.f);
  REQUIRE(w.ws
            ->Edit(w.Actor(), kSanctuary, WorkshopEditOp::Move, { { 0xDEAD } },
                   w.builder, 82)
            .error == WorkshopError::ItemNotFound);

  // Pre-placed junk (a car) scraps once, for half its cost
  PreplacedRef car{ 0x000ABCDE, kCarWreck, { 500, 500, 0 } };
  auto carScrap = w.ws->ScrapPreplaced(w.Actor(), kSanctuary, car, 83);
  REQUIRE(carScrap.Ok());
  // The 2 refunded steel were spent on wall2 (container is used first)
  REQUIRE(w.W().container.CountBase(kSteelScrap) == 5);
  REQUIRE(w.ws->ScrapPreplaced(w.Actor(), kSanctuary, car, 84).error ==
          WorkshopError::NotScrappable);
}

TEST_CASE("Settlers, assignment, ratings and the daily update",
          "[fo4][F22][Workshop]")
{
  ShopWorld w;
  w.ClaimAndEnter();
  w.builder.AddSimple(kSteelScrap, 50);
  auto crop1 = w.Place(kCrop, kRecCrop).refId;
  auto crop2 = w.Place(kCrop, kRecCrop).refId;
  w.Place(kPump, kRecPump);
  auto bed = w.Place(kBed, kRecBed).refId;

  REQUIRE(w.ws->AddSettler(kSanctuary, 0xFF00A001).Ok());
  REQUIRE(w.W().ratings.population == 1.f);
  REQUIRE(w.W().ratings.food == 0.f); // crops need a worker
  REQUIRE(w.ws->Assign(w.Actor(), kSanctuary, 0xFF00A001, crop1).Ok());
  REQUIRE(w.ws->Assign(w.Actor(), kSanctuary, 0xFF00A001, bed).Ok());
  REQUIRE(w.W().ratings.food == 0.5f);
  REQUIRE(w.W().ratings.water == 3.f);
  REQUIRE(w.W().ratings.beds == 1.f);
  REQUIRE(w.W().ratings.unassignedPopulation == 0.f);

  // Reassigning the job moves the settler off crop1
  REQUIRE(w.ws->Assign(w.Actor(), kSanctuary, 0xFF00A001, crop2).Ok());
  REQUIRE(w.W().objects.at(crop1).assignedActor == 0);
  REQUIRE(w.ws->AddSettler(kSanctuary, 0xFF00A002).Ok());
  REQUIRE(w.ws->Assign(w.Actor(), kSanctuary, 0xFF00A002, crop2).error ==
          WorkshopError::AssignmentTaken);
  auto wall = w.Place(kWoodWall, kRecWall).refId;
  REQUIRE(w.ws->Assign(w.Actor(), kSanctuary, 0xFF00A002, wall).error ==
          WorkshopError::NotAssignable);

  // Daily update: water surplus 3 - 2 = 1 bottle, food 0.5 - 2 = none
  std::mt19937 rng(42);
  auto& shop = *w.ws->Find(kSanctuary);
  auto report = w.ws->DailyUpdate(shop, 1.0, rng);
  REQUIRE(report.waterProduced == 1.f);
  REQUIRE(report.foodProduced == 0.f);
  REQUIRE(shop.container.CountBase(kWater) == 1);
  REQUIRE(report.attackChance >= 0.02f);
  REQUIRE_FALSE(report.attackTriggered); // attacks are off by default
  float before = shop.ratings.happiness;
  REQUIRE(shop.ratings.happinessTarget < 100.f);
  REQUIRE(shop.ratings.happiness ==
          Catch::Approx(before)); // already updated in this call
}

TEST_CASE("Recruitment needs a powered beacon", "[fo4][F22][Workshop]")
{
  ShopWorld w;
  w.ClaimAndEnter();
  w.builder.AddSimple(kSteelScrap, 50);
  auto beacon = w.Place(kBeacon, kRecBeacon).refId;
  std::mt19937 rng(1);
  auto& shop = *w.ws->Find(kSanctuary);
  bool recruited = false;
  for (int day = 1; day < 50; ++day) {
    recruited |= w.ws->DailyUpdate(shop, day, rng).recruited;
  }
  REQUIRE_FALSE(recruited); // unpowered beacon
  auto gen = w.Place(kGenerator, kRecGen).refId;
  REQUIRE(w.ws->ConnectWire(w.Actor(), kSanctuary, gen, beacon, 0, 5555).Ok());
  for (int day = 50; day < 200 && !recruited; ++day) {
    recruited |= w.ws->DailyUpdate(shop, day, rng).recruited;
  }
  REQUIRE(recruited);
}

TEST_CASE("Leaving the build area for more than 5 s exits build mode",
          "[fo4][F22][Workshop]")
{
  ShopWorld w;
  w.ClaimAndEnter();
  std::map<ActorId, std::array<float, 3>> pos{ { kPlayer, { 9000, 0, 0 } } };
  REQUIRE(w.ws->TickBuildMode(pos, 1000).empty());
  REQUIRE(w.ws->TickBuildMode(pos, 5000).empty());
  pos[kPlayer] = { 0, 0, 0 }; // came back in time
  REQUIRE(w.ws->TickBuildMode(pos, 5500).empty());
  pos[kPlayer] = { 9000, 0, 0 };
  w.ws->TickBuildMode(pos, 6000);
  REQUIRE(w.ws->TickBuildMode(pos, 11001) == std::vector<ActorId>{ kPlayer });
  REQUIRE_FALSE(w.ws->GetBuildModeWorkshop(kPlayer));
}

TEST_CASE("Workshop persistence round trip", "[fo4][F22][Workshop]")
{
  ShopWorld w;
  w.ClaimAndEnter();
  w.builder.AddSimple(kSteelScrap, 50);
  auto gen = w.Place(kGenerator, kRecGen).refId;
  auto light = w.Place(kLight, kRecLight).refId;
  REQUIRE(w.ws->ConnectWire(w.Actor(), kSanctuary, gen, light, 0, 4242).Ok());
  w.ws->AddSettler(kSanctuary, 0xFF00A001);
  REQUIRE(w.ws->SetAcl(w.Actor(), kSanctuary, 7, WorkshopPerm::Build).Ok());
  w.ws->Find(kSanctuary)->stored[kBed] = 2;
  w.ws->Find(kSanctuary)->container.AddSimple(kWrench, 3);

  auto j = w.ws->ToJson(w.W());
  auto restored = WorkshopService::FromJson(j);
  WorkshopService fresh(w.data, w.ws->settings);
  auto& r = fresh.AddWorkshop(restored); // recomputes power and ratings
  REQUIRE(r.owner.id == 1);
  REQUIRE(r.acl.at(7) == WorkshopPerm::Build);
  REQUIRE(r.objects.size() == 2);
  REQUIRE(r.wires.size() == 1);
  REQUIRE(r.objects.at(light).powered);
  REQUIRE(r.settlers.size() == 1);
  REQUIRE(r.stored.at(kBed) == 2);
  REQUIRE(r.container.CountBase(kWrench) == 3);
  REQUIRE(r.version == w.W().version);

  // A corrupt object entry is skipped, not fatal
  j["objects"].push_back({ { "refId", 5 } });
  REQUIRE(WorkshopService::FromJson(j).objects.size() == 2);
}

TEST_CASE("WorkshopArea box rotation and sphere", "[fo4][F22]")
{
  WorkshopArea box;
  box.center = { 0, 0, 0 };
  box.halfExtents = { 100, 10, 50 };
  box.rotZ = 3.14159265f / 2.f; // long side now along Y
  REQUIRE(box.Contains({ 0, 90, 0 }, 0));
  REQUIRE_FALSE(box.Contains({ 90, 0, 0 }, 0));
  WorkshopArea s;
  s.shape = WorkshopArea::Shape::Sphere;
  s.radius = 10;
  REQUIRE(s.Contains({ 0, 0, 9 }, 0));
  REQUIRE_FALSE(s.Contains({ 0, 0, 11 }, 0));
  REQUIRE(s.Contains({ 0, 0, 11 }, 2));
}
