#pragma once
// Workshops and settlements (F22).
//
// The server owns every settlement: ownership and permissions, the placed
// object set, budget, shared workshop inventory, wires and power, settler
// assignments, ratings and the daily economy. Clients only request changes
// (class A); the exact placement transform is client-proposed and validated
// (class B). Engine-free and deterministic: positions, inventories, random
// numbers and the game clock are passed in.
#include "ComponentPlanner.h"
#include "Fo4Data.h"
#include "ItemInstance.h"
#include <array>
#include <functional>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <random>
#include <set>

namespace fo4 {

using ActorId = uint32_t;
using ProfileId = int32_t;

enum class WorkshopError : uint8_t
{
  None = 0,
  NoSuchWorkshop,
  NotOwned,
  AlreadyOwned,
  NoPermission,
  NotInBuildMode,
  OutsideBuildArea,
  TooFar,
  InvalidTransform,
  InvalidScale,
  RateLimited,
  DuplicateNonce,
  UnknownRecipe,
  RecipeMismatch,
  MissingPerk,
  MissingComponents,
  NotStored,
  OverBudget,
  ItemNotFound,
  NotScrappable,
  WireInvalid,
  WireTooLong,
  WireDuplicate,
  TooManyWires,
  NotAssignable,
  AssignmentTaken,
  ClaimLimit,
  ClaimBlocked,
  Vetoed,
  TooManyItems,
  SnapTargetInvalid,
};
const char* WorkshopErrorToString(WorkshopError e) noexcept;

namespace WorkshopPerm {
enum : uint16_t
{
  Build = 1 << 0,
  Scrap = 1 << 1,
  Container = 1 << 2,
  Craft = 1 << 3,
  Assign = 1 << 4,
  Admin = 1 << 5,
  All = 0x3F,
};
}

struct WorkshopArea
{
  enum class Shape : uint8_t
  {
    Box,
    Sphere,
  };
  Shape shape = Shape::Box;
  std::array<float, 3> center = { 0, 0, 0 };
  std::array<float, 3> halfExtents = { 0, 0, 0 }; // box
  float rotZ = 0.f;                               // box yaw, radians
  float radius = 0.f;                             // sphere
  bool Contains(const std::array<float, 3>& p, float tolerance) const;
};

struct PlacedObject
{
  FormId refId = 0;
  FormId baseId = 0;
  FormId recipeId = 0;
  std::array<float, 3> pos = { 0, 0, 0 };
  std::array<float, 3> rot = { 0, 0, 0 };
  float scale = 1.f;
  bool destroyed = false; // damaged by an attack; needs repair
  bool powered = false;   // derived by the power graph
  ActorId assignedActor = 0;
  ProfileId builtBy = -1;
};

struct Wire
{
  FormId wireRefId = 0;
  FormId a = 0;
  FormId b = 0;
  FormId splineBaseId = 0;
};

struct SettlerState
{
  ActorId actorId = 0;
  FormId jobRefId = 0;
  FormId bedRefId = 0;
};

// Rating indices follow WorkshopParentScript's AV order where it matters.
struct WorkshopRatings
{
  float food = 0, water = 0, safety = 0, beds = 0, power = 0;
  float powerCapacity = 0, powerLoad = 0;
  float population = 0, happiness = 50, happinessTarget = 50;
  float unassignedPopulation = 0;
};

struct WorkshopBudget
{
  float current = 0;
  float max = 100;
  uint32_t objects = 0;
  uint32_t maxObjects = 1000;
};

struct WorkshopOwner
{
  enum class Type : uint8_t
  {
    None,
    Profile,
    Group, // gamemode-defined group / party (F32)
  };
  Type type = Type::None;
  int32_t id = -1;
};

struct Workshop
{
  FormId workbenchRefId = 0;
  FormId locationId = 0;
  uint32_t worldOrCell = 0;
  std::vector<WorkshopArea> areas;
  WorkshopOwner owner;
  std::map<ProfileId, uint16_t> acl;
  std::map<FormId, PlacedObject> objects;
  std::set<FormId> scrappedPrePlaced;
  std::map<FormId, uint32_t> stored;
  std::vector<Wire> wires;
  std::map<ActorId, SettlerState> settlers;
  Fo4Inventory container; // shared workshop inventory
  WorkshopRatings ratings;
  WorkshopBudget budget;
  double lastDailyUpdateDay = 0.0;
  uint32_t version = 0;
  bool claimBlocked = false; // e.g. a boss is still alive at the location
};

// Who is acting and what the server knows about them.
struct WorkshopActor
{
  ActorId actorId = 0;
  ProfileId profileId = -1;
  int32_t groupId = -1; // party/faction group for Group-owned workshops
  std::array<float, 3> pos = { 0, 0, 0 };
  std::function<int32_t(FormId perkId)> perkRank;
  int64_t nowMs = 0;
};

struct PlaceRequest
{
  uint32_t nonce = 0;
  FormId workshopId = 0;
  FormId recipeId = 0;
  FormId baseId = 0;
  bool fromStored = false;
  std::array<float, 3> pos = { 0, 0, 0 };
  std::array<float, 3> rot = { 0, 0, 0 };
  float scale = 1.f;
  FormId snapTargetRefId = 0;
};

struct WorkshopResult
{
  WorkshopError error = WorkshopError::None;
  FormId refId = 0;
  std::vector<InventoryEntry> refunds;
  std::vector<ComponentShortfall> shortfalls;
  bool Ok() const { return error == WorkshopError::None; }
};

enum class WorkshopEditOp : uint8_t
{
  Move,
  Scrap,
  Store,
  Repair,
};

struct WorkshopEditItem
{
  FormId refId = 0;
  std::array<float, 3> pos = { 0, 0, 0 };
  std::array<float, 3> rot = { 0, 0, 0 };
};

struct PreplacedRef
{
  FormId refId = 0;
  FormId baseId = 0;
  std::array<float, 3> pos = { 0, 0, 0 };
};

struct DailyReport
{
  float foodProduced = 0, waterProduced = 0;
  bool recruited = false;
  float attackChance = 0;
  bool attackTriggered = false;
};

struct WorkshopSettings
{
  enum class ClaimRule : uint8_t
  {
    FirstClaim,
    AdminOnly,
    Gamemode, // only through the veto hook
  };
  ClaimRule claimRule = ClaimRule::FirstClaim;
  uint32_t maxPerPlayer = 5;
  uint32_t maxObjects = 1000;
  float defaultBudget = 100.f;
  float maxBuildDistance = 4096.f;
  float maxCoord = 2.0e6f;
  bool allowScale = false;
  float minScale = 0.1f, maxScale = 10.f;
  uint32_t maxRequestsPerSecond = 10;
  uint32_t maxItemsPerEdit = 64;
  float maxWireLength = 1500.f; // [verify GameProfile constant]
  uint32_t maxWiresPerConnector = 6;
  float snapRadius = 256.f;
  int64_t buildModeLeaveAreaMs = 5000;
  float popBase = 10.f;
  bool attacks = false; // T2
  // Daily production
  FormId foodItemId = 0;  // produce item for food rating
  FormId waterItemId = 0; // purified water for water rating
  float foodStorageBase = 10.f, foodStoragePerPop = 1.f;
  float waterStorageBase = 5.f, waterStoragePerPop = 0.25f;
};

class WorkshopService
{
public:
  WorkshopService(const IFo4DataSource& data, WorkshopSettings settings);

  Workshop& AddWorkshop(Workshop w);
  Workshop* Find(FormId workbenchRefId);
  const Workshop* Find(FormId workbenchRefId) const;
  const std::map<FormId, Workshop>& All() const { return workshops; }

  // Gamemode veto: (actorId, workshopId, action name) -> allow?
  std::function<bool(ActorId, FormId, const char*)> veto;
  // Allocates form ids for placed objects and wires.
  std::function<FormId()> allocateFormId;

  uint16_t GetPerms(const Workshop& w, const WorkshopActor& a) const;

  WorkshopResult Claim(const WorkshopActor& a, FormId workshopId);
  WorkshopResult Abandon(const WorkshopActor& a, FormId workshopId);
  WorkshopResult SetAcl(const WorkshopActor& a, FormId workshopId,
                        ProfileId target, uint16_t perms);

  WorkshopResult EnterBuildMode(const WorkshopActor& a, FormId workshopId);
  void ExitBuildMode(ActorId actor);
  std::optional<FormId> GetBuildModeWorkshop(ActorId actor) const;
  // Updates positions for the area-leave rule; returns actors forced out.
  std::vector<ActorId> TickBuildMode(
    const std::map<ActorId, std::array<float, 3>>& positions, int64_t nowMs);

  WorkshopResult Place(const WorkshopActor& a, const PlaceRequest& req,
                       Fo4Inventory& builderInventory);
  WorkshopResult Edit(const WorkshopActor& a, FormId workshopId,
                      WorkshopEditOp op,
                      const std::vector<WorkshopEditItem>& items,
                      Fo4Inventory& builderInventory, uint32_t nonce);
  // Scrap a pre-placed ESM object (cars, trees, debris) inside the area.
  WorkshopResult ScrapPreplaced(const WorkshopActor& a, FormId workshopId,
                                const PreplacedRef& ref, uint32_t nonce);

  WorkshopResult ConnectWire(const WorkshopActor& a, FormId workshopId,
                             FormId from, FormId to, FormId splineBaseId,
                             uint32_t nonce);
  WorkshopResult DisconnectWire(const WorkshopActor& a, FormId workshopId,
                                FormId wireRefId, uint32_t nonce);

  WorkshopResult AddSettler(FormId workshopId, ActorId settler);
  WorkshopResult Assign(const WorkshopActor& a, FormId workshopId,
                        ActorId settler, FormId objectRefId);

  // Recomputes power and ratings (cheap; called after every change).
  void Recompute(Workshop& w) const;
  // Runs once per game day per workshop. `rng` decides recruitment/attacks.
  DailyReport DailyUpdate(Workshop& w, double gameDay, std::mt19937& rng);

  // Owned workshops count for the claim limit
  uint32_t CountOwnedBy(ProfileId p) const;

  nlohmann::json ToJson(const Workshop& w) const;
  static Workshop FromJson(const nlohmann::json& j);

  WorkshopSettings settings;

private:
  WorkshopResult Check(const WorkshopActor& a, FormId workshopId,
                       uint16_t perm, bool needBuildMode, Workshop*& out);
  bool CheckRate(ActorId actor, int64_t nowMs);
  bool CheckNonce(ActorId actor, uint32_t nonce);
  bool InArea(const Workshop& w, const std::array<float, 3>& p,
              float tolerance) const;
  WorkshopError CheckTransform(const Workshop& w, const WorkshopActor& a,
                               const std::array<float, 3>& pos,
                               const std::array<float, 3>& rot,
                               float scale) const;
  bool PayCost(Workshop& w, Fo4Inventory& builder,
               const std::vector<ComponentCount>& cost,
               std::vector<ComponentShortfall>& shortfalls) const;
  const RecipeData* FindBuildRecipe(FormId baseId) const;
  float CostOf(FormId baseId) const;
  bool IsPowerNode(FormId baseId) const;

  const IFo4DataSource& data;
  ComponentPlanner planner;
  std::map<FormId, Workshop> workshops;
  struct BuildModeState
  {
    FormId workshopId = 0;
    int64_t outsideSinceMs = -1;
    ProfileId profileId = -1;
  };
  std::map<ActorId, BuildModeState> buildMode;
  std::map<ActorId, std::vector<int64_t>> requestTimes;
  std::map<ActorId, std::set<uint32_t>> seenNonces;
};

}
