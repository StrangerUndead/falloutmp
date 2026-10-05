#include "Workshop.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace fo4 {

const char* WorkshopErrorToString(WorkshopError e) noexcept
{
  switch (e) {
#define C(x)                                                                  \
  case WorkshopError::x:                                                      \
    return #x;
    C(None)
    C(NoSuchWorkshop)
    C(NotOwned)
    C(AlreadyOwned)
    C(NoPermission)
    C(NotInBuildMode)
    C(OutsideBuildArea)
    C(TooFar)
    C(InvalidTransform)
    C(InvalidScale)
    C(RateLimited)
    C(DuplicateNonce)
    C(UnknownRecipe)
    C(RecipeMismatch)
    C(MissingPerk)
    C(MissingComponents)
    C(NotStored)
    C(OverBudget)
    C(ItemNotFound)
    C(NotScrappable)
    C(WireInvalid)
    C(WireTooLong)
    C(WireDuplicate)
    C(TooManyWires)
    C(NotAssignable)
    C(AssignmentTaken)
    C(ClaimLimit)
    C(ClaimBlocked)
    C(Vetoed)
    C(TooManyItems)
    C(SnapTargetInvalid)
#undef C
  }
  return "Unknown";
}

namespace {
float Dist(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
  float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}
bool Finite(const std::array<float, 3>& v)
{
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
WorkshopResult Err(WorkshopError e)
{
  WorkshopResult r;
  r.error = e;
  return r;
}
}

bool WorkshopArea::Contains(const std::array<float, 3>& p,
                            float tolerance) const
{
  if (shape == Shape::Sphere) {
    return Dist(p, center) <= radius + tolerance;
  }
  float dx = p[0] - center[0], dy = p[1] - center[1], dz = p[2] - center[2];
  float c = std::cos(-rotZ), s = std::sin(-rotZ);
  float lx = dx * c - dy * s, ly = dx * s + dy * c;
  return std::fabs(lx) <= halfExtents[0] + tolerance &&
    std::fabs(ly) <= halfExtents[1] + tolerance &&
    std::fabs(dz) <= halfExtents[2] + tolerance;
}

WorkshopService::WorkshopService(const IFo4DataSource& data_,
                                 WorkshopSettings settings_)
  : settings(std::move(settings_))
  , data(data_)
  , planner(data_)
{
}

Workshop& WorkshopService::AddWorkshop(Workshop w)
{
  FormId id = w.workbenchRefId;
  if (w.budget.maxObjects == 1000) {
    w.budget.maxObjects = settings.maxObjects;
  }
  if (w.budget.max == 100.f) {
    w.budget.max = settings.defaultBudget;
  }
  auto& ref = workshops[id] = std::move(w);
  Recompute(ref);
  return ref;
}

Workshop* WorkshopService::Find(FormId id)
{
  auto it = workshops.find(id);
  return it == workshops.end() ? nullptr : &it->second;
}

const Workshop* WorkshopService::Find(FormId id) const
{
  auto it = workshops.find(id);
  return it == workshops.end() ? nullptr : &it->second;
}

uint16_t WorkshopService::GetPerms(const Workshop& w,
                                   const WorkshopActor& a) const
{
  switch (w.owner.type) {
    case WorkshopOwner::Type::None:
      return 0;
    case WorkshopOwner::Type::Profile:
      if (a.profileId >= 0 && w.owner.id == a.profileId) {
        return WorkshopPerm::All;
      }
      break;
    case WorkshopOwner::Type::Group:
      if (a.groupId >= 0 && w.owner.id == a.groupId) {
        return WorkshopPerm::All & ~WorkshopPerm::Admin;
      }
      break;
  }
  auto it = w.acl.find(a.profileId);
  return it == w.acl.end() ? 0 : it->second;
}

uint32_t WorkshopService::CountOwnedBy(ProfileId p) const
{
  uint32_t n = 0;
  for (auto& [id, w] : workshops) {
    if (w.owner.type == WorkshopOwner::Type::Profile && w.owner.id == p) {
      ++n;
    }
  }
  return n;
}

bool WorkshopService::CheckRate(ActorId actor, int64_t nowMs)
{
  auto& times = requestTimes[actor];
  times.erase(std::remove_if(times.begin(), times.end(),
                             [&](int64_t t) { return nowMs - t >= 1000; }),
              times.end());
  if (times.size() >= settings.maxRequestsPerSecond) {
    return false;
  }
  times.push_back(nowMs);
  return true;
}

bool WorkshopService::CheckNonce(ActorId actor, uint32_t nonce)
{
  if (nonce == 0) {
    return true;
  }
  auto& seen = seenNonces[actor];
  if (!seen.insert(nonce).second) {
    return false;
  }
  if (seen.size() > 256) {
    seen.erase(seen.begin()); // bounded memory; nonces are increasing
  }
  return true;
}

WorkshopResult WorkshopService::Check(const WorkshopActor& a,
                                      FormId workshopId, uint16_t perm,
                                      bool needBuildMode, Workshop*& out)
{
  out = Find(workshopId);
  if (!out) {
    return Err(WorkshopError::NoSuchWorkshop);
  }
  if (out->owner.type == WorkshopOwner::Type::None) {
    return Err(WorkshopError::NotOwned);
  }
  if ((GetPerms(*out, a) & perm) != perm) {
    return Err(WorkshopError::NoPermission);
  }
  if (needBuildMode) {
    auto it = buildMode.find(a.actorId);
    if (it == buildMode.end() || it->second.workshopId != workshopId) {
      return Err(WorkshopError::NotInBuildMode);
    }
  }
  return {};
}

WorkshopResult WorkshopService::Claim(const WorkshopActor& a,
                                      FormId workshopId)
{
  auto w = Find(workshopId);
  if (!w) {
    return Err(WorkshopError::NoSuchWorkshop);
  }
  if (w->owner.type != WorkshopOwner::Type::None) {
    return Err(WorkshopError::AlreadyOwned);
  }
  if (w->claimBlocked) {
    return Err(WorkshopError::ClaimBlocked);
  }
  if (settings.claimRule == WorkshopSettings::ClaimRule::AdminOnly) {
    return Err(WorkshopError::NoPermission);
  }
  if (settings.claimRule == WorkshopSettings::ClaimRule::Gamemode && !veto) {
    return Err(WorkshopError::Vetoed);
  }
  if (CountOwnedBy(a.profileId) >= settings.maxPerPlayer) {
    return Err(WorkshopError::ClaimLimit);
  }
  if (veto && !veto(a.actorId, workshopId, "claim")) {
    return Err(WorkshopError::Vetoed);
  }
  w->owner = { WorkshopOwner::Type::Profile, a.profileId };
  ++w->version;
  return {};
}

WorkshopResult WorkshopService::Abandon(const WorkshopActor& a,
                                        FormId workshopId)
{
  Workshop* w = nullptr;
  auto r = Check(a, workshopId, WorkshopPerm::Admin, false, w);
  if (!r.Ok()) {
    return r;
  }
  w->owner = {};
  w->acl.clear();
  for (auto it = buildMode.begin(); it != buildMode.end();) {
    it = it->second.workshopId == workshopId ? buildMode.erase(it) : ++it;
  }
  ++w->version;
  return {};
}

WorkshopResult WorkshopService::SetAcl(const WorkshopActor& a,
                                       FormId workshopId, ProfileId target,
                                       uint16_t perms)
{
  Workshop* w = nullptr;
  auto r = Check(a, workshopId, WorkshopPerm::Admin, false, w);
  if (!r.Ok()) {
    return r;
  }
  perms &= WorkshopPerm::All;
  if (perms == 0) {
    w->acl.erase(target);
  } else {
    w->acl[target] = perms;
  }
  // Revoked builders are thrown out of build mode
  if (!(perms & WorkshopPerm::Build)) {
    for (auto it = buildMode.begin(); it != buildMode.end();) {
      bool revoked = it->second.workshopId == workshopId &&
        it->second.profileId == target;
      it = revoked ? buildMode.erase(it) : std::next(it);
    }
  }
  ++w->version;
  return {};
}

bool WorkshopService::InArea(const Workshop& w,
                             const std::array<float, 3>& p,
                             float tolerance) const
{
  if (w.areas.empty()) {
    return true; // area unknown (data not loaded): only distance checks
  }
  for (auto& area : w.areas) {
    if (area.Contains(p, tolerance)) {
      return true;
    }
  }
  return false;
}

WorkshopResult WorkshopService::EnterBuildMode(const WorkshopActor& a,
                                               FormId workshopId)
{
  Workshop* w = nullptr;
  auto r = Check(a, workshopId, WorkshopPerm::Build, false, w);
  if (!r.Ok()) {
    return r;
  }
  if (!InArea(*w, a.pos, 0.f)) {
    return Err(WorkshopError::OutsideBuildArea);
  }
  if (veto && !veto(a.actorId, workshopId, "enter")) {
    return Err(WorkshopError::Vetoed);
  }
  buildMode[a.actorId] = { workshopId, -1, a.profileId };
  return {};
}

void WorkshopService::ExitBuildMode(ActorId actor)
{
  buildMode.erase(actor);
}

std::optional<FormId> WorkshopService::GetBuildModeWorkshop(
  ActorId actor) const
{
  auto it = buildMode.find(actor);
  if (it == buildMode.end()) {
    return std::nullopt;
  }
  return it->second.workshopId;
}

std::vector<ActorId> WorkshopService::TickBuildMode(
  const std::map<ActorId, std::array<float, 3>>& positions, int64_t nowMs)
{
  std::vector<ActorId> forced;
  for (auto it = buildMode.begin(); it != buildMode.end();) {
    auto w = Find(it->second.workshopId);
    auto pos = positions.find(it->first);
    bool inside = w && pos != positions.end() && InArea(*w, pos->second, 0.f);
    if (inside) {
      it->second.outsideSinceMs = -1;
    } else if (it->second.outsideSinceMs < 0) {
      it->second.outsideSinceMs = nowMs;
    }
    if (!w ||
        (it->second.outsideSinceMs >= 0 &&
         nowMs - it->second.outsideSinceMs > settings.buildModeLeaveAreaMs)) {
      forced.push_back(it->first);
      it = buildMode.erase(it);
      continue;
    }
    ++it;
  }
  return forced;
}

WorkshopError WorkshopService::CheckTransform(
  const Workshop& w, const WorkshopActor& a, const std::array<float, 3>& pos,
  const std::array<float, 3>& rot, float scale) const
{
  if (!Finite(pos) || !Finite(rot) || !std::isfinite(scale)) {
    return WorkshopError::InvalidTransform;
  }
  for (float v : pos) {
    if (std::fabs(v) > settings.maxCoord) {
      return WorkshopError::InvalidTransform;
    }
  }
  if (scale != 1.f &&
      (!settings.allowScale || scale < settings.minScale ||
       scale > settings.maxScale)) {
    return WorkshopError::InvalidScale;
  }
  if (Dist(pos, a.pos) > settings.maxBuildDistance) {
    return WorkshopError::TooFar;
  }
  if (!InArea(w, pos, 64.f)) {
    return WorkshopError::OutsideBuildArea;
  }
  return WorkshopError::None;
}

const RecipeData* WorkshopService::FindBuildRecipe(FormId baseId) const
{
  auto recipes = data.GetRecipesCreating(baseId);
  return recipes.empty() ? nullptr : recipes.front();
}

float WorkshopService::CostOf(FormId baseId) const
{
  auto o = data.FindWorkshopObject(baseId);
  return o ? o->budgetCost : 1.f;
}

bool WorkshopService::IsPowerNode(FormId baseId) const
{
  auto o = data.FindWorkshopObject(baseId);
  return o &&
    (o->powerGenerated > 0.f || o->powerRequired > 0.f || o->isPowerConnector);
}

bool WorkshopService::PayCost(Workshop& w, Fo4Inventory& builder,
                              const std::vector<ComponentCount>& cost,
                              std::vector<ComponentShortfall>& shortfalls) const
{
  // Sources in vanilla order: workshop container first, then the builder.
  std::vector<ComponentCount> fromContainer, fromBuilder;
  for (auto& c : cost) {
    uint32_t inContainer = data.FindComponent(c.componentId)
      ? planner.CountAvailable(w.container, c.componentId)
      : w.container.CountBase(c.componentId);
    uint32_t a = std::min(inContainer, c.count);
    if (a) {
      fromContainer.push_back({ c.componentId, a });
    }
    if (c.count > a) {
      fromBuilder.push_back({ c.componentId, c.count - a });
    }
  }
  auto p1 = planner.Plan(w.container, fromContainer);
  auto p2 = planner.Plan(builder, fromBuilder);
  if (!p1.ok || !p2.ok) {
    shortfalls = p2.ok ? p1.shortfalls : p2.shortfalls;
    return false;
  }
  w.container = std::move(p1.resultInventory);
  builder = std::move(p2.resultInventory);
  return true;
}

WorkshopResult WorkshopService::Place(const WorkshopActor& a,
                                      const PlaceRequest& req,
                                      Fo4Inventory& builderInventory)
{
  Workshop* w = nullptr;
  auto r = Check(a, req.workshopId, WorkshopPerm::Build, true, w);
  if (!r.Ok()) {
    return r;
  }
  if (!CheckRate(a.actorId, a.nowMs)) {
    return Err(WorkshopError::RateLimited);
  }
  if (!CheckNonce(a.actorId, req.nonce)) {
    return Err(WorkshopError::DuplicateNonce);
  }
  if (auto e = CheckTransform(*w, a, req.pos, req.rot, req.scale);
      e != WorkshopError::None) {
    return Err(e);
  }
  if (req.snapTargetRefId) {
    auto it = w->objects.find(req.snapTargetRefId);
    if (it == w->objects.end() ||
        Dist(it->second.pos, req.pos) > settings.snapRadius) {
      return Err(WorkshopError::SnapTargetInvalid);
    }
  }
  float cost = CostOf(req.baseId);
  if (w->budget.current + cost > w->budget.max + 1e-4f ||
      w->budget.objects + 1 > w->budget.maxObjects) {
    return Err(WorkshopError::OverBudget);
  }

  Workshop candidate = *w;
  Fo4Inventory builderCandidate = builderInventory;
  if (req.fromStored) {
    auto it = candidate.stored.find(req.baseId);
    if (it == candidate.stored.end() || it->second == 0) {
      return Err(WorkshopError::NotStored);
    }
    if (--it->second == 0) {
      candidate.stored.erase(it);
    }
  } else {
    auto recipe = data.FindRecipe(req.recipeId);
    if (!recipe) {
      return Err(WorkshopError::UnknownRecipe);
    }
    if (recipe->createdObjectId != req.baseId) {
      return Err(WorkshopError::RecipeMismatch);
    }
    for (auto& p : recipe->perks) {
      if ((a.perkRank ? a.perkRank(p.perkId) : 0) < p.minRank) {
        return Err(WorkshopError::MissingPerk);
      }
    }
    WorkshopResult fail;
    if (!PayCost(candidate, builderCandidate, recipe->components,
                 fail.shortfalls)) {
      fail.error = WorkshopError::MissingComponents;
      return fail;
    }
  }
  if (veto && !veto(a.actorId, req.workshopId, "place")) {
    return Err(WorkshopError::Vetoed);
  }

  PlacedObject obj;
  obj.refId = allocateFormId ? allocateFormId() : 0;
  if (!obj.refId) {
    return Err(WorkshopError::InvalidTransform);
  }
  obj.baseId = req.baseId;
  obj.recipeId = req.recipeId;
  obj.pos = req.pos;
  obj.rot = req.rot;
  obj.scale = req.scale;
  obj.builtBy = a.profileId;
  candidate.objects[obj.refId] = obj;
  ++candidate.version;
  Recompute(candidate);

  *w = std::move(candidate);
  builderInventory = std::move(builderCandidate);
  WorkshopResult ok;
  ok.refId = obj.refId;
  return ok;
}

WorkshopResult WorkshopService::Edit(
  const WorkshopActor& a, FormId workshopId, WorkshopEditOp op,
  const std::vector<WorkshopEditItem>& items, Fo4Inventory& builderInventory,
  uint32_t nonce)
{
  uint16_t perm = (op == WorkshopEditOp::Scrap || op == WorkshopEditOp::Store)
    ? WorkshopPerm::Scrap
    : WorkshopPerm::Build;
  Workshop* w = nullptr;
  auto r = Check(a, workshopId, perm, true, w);
  if (!r.Ok()) {
    return r;
  }
  if (items.empty() || items.size() > settings.maxItemsPerEdit) {
    return Err(WorkshopError::TooManyItems);
  }
  if (!CheckRate(a.actorId, a.nowMs)) {
    return Err(WorkshopError::RateLimited);
  }
  if (!CheckNonce(a.actorId, nonce)) {
    return Err(WorkshopError::DuplicateNonce);
  }
  const char* action = op == WorkshopEditOp::Move ? "move"
    : op == WorkshopEditOp::Scrap                 ? "scrap"
    : op == WorkshopEditOp::Store                 ? "store"
                                                  : "repair";
  if (veto && !veto(a.actorId, workshopId, action)) {
    return Err(WorkshopError::Vetoed);
  }

  Workshop candidate = *w;
  Fo4Inventory builderCandidate = builderInventory;
  WorkshopResult res;
  for (auto& item : items) {
    auto it = candidate.objects.find(item.refId);
    if (it == candidate.objects.end()) {
      return Err(WorkshopError::ItemNotFound);
    }
    auto& obj = it->second;
    switch (op) {
      case WorkshopEditOp::Move: {
        if (auto e = CheckTransform(candidate, a, item.pos, item.rot,
                                    obj.scale);
            e != WorkshopError::None) {
          return Err(e);
        }
        obj.pos = item.pos;
        obj.rot = item.rot;
        break;
      }
      case WorkshopEditOp::Scrap:
      case WorkshopEditOp::Store: {
        if (op == WorkshopEditOp::Store) {
          candidate.stored[obj.baseId] += 1;
        } else if (auto recipe = FindBuildRecipe(obj.baseId)) {
          // Player-built objects refund their full build cost
          for (auto& c : recipe->components) {
            FormId scrap = planner.GetScrapItem(c.componentId);
            FormId add = scrap ? scrap : c.componentId;
            candidate.container.AddSimple(add, c.count);
            res.refunds.push_back({ ItemKey{ add }, c.count });
          }
        }
        // Clear assignment and wires
        if (obj.assignedActor) {
          if (auto s = candidate.settlers.find(obj.assignedActor);
              s != candidate.settlers.end()) {
            if (s->second.jobRefId == obj.refId) {
              s->second.jobRefId = 0;
            }
            if (s->second.bedRefId == obj.refId) {
              s->second.bedRefId = 0;
            }
          }
        }
        FormId gone = obj.refId;
        candidate.wires.erase(
          std::remove_if(candidate.wires.begin(), candidate.wires.end(),
                         [&](const Wire& wr) {
                           return wr.a == gone || wr.b == gone;
                         }),
          candidate.wires.end());
        candidate.objects.erase(it);
        break;
      }
      case WorkshopEditOp::Repair: {
        if (!obj.destroyed) {
          return Err(WorkshopError::NotScrappable);
        }
        if (auto recipe = FindBuildRecipe(obj.baseId)) {
          std::vector<ComponentCount> half;
          for (auto& c : recipe->components) {
            half.push_back({ c.componentId, (c.count + 1) / 2 });
          }
          if (!PayCost(candidate, builderCandidate, half, res.shortfalls)) {
            res.error = WorkshopError::MissingComponents;
            return res;
          }
        }
        obj.destroyed = false;
        break;
      }
    }
  }
  ++candidate.version;
  Recompute(candidate);
  *w = std::move(candidate);
  builderInventory = std::move(builderCandidate);
  return res;
}

WorkshopResult WorkshopService::ScrapPreplaced(const WorkshopActor& a,
                                               FormId workshopId,
                                               const PreplacedRef& ref,
                                               uint32_t nonce)
{
  Workshop* w = nullptr;
  auto r = Check(a, workshopId, WorkshopPerm::Scrap, true, w);
  if (!r.Ok()) {
    return r;
  }
  if (!CheckNonce(a.actorId, nonce)) {
    return Err(WorkshopError::DuplicateNonce);
  }
  if (w->scrappedPrePlaced.count(ref.refId) || !InArea(*w, ref.pos, 64.f)) {
    return Err(WorkshopError::NotScrappable);
  }
  auto recipe = FindBuildRecipe(ref.baseId);
  if (!recipe) {
    return Err(WorkshopError::NotScrappable);
  }
  WorkshopResult res;
  for (auto& c : recipe->components) {
    // Pre-placed junk yields half its build cost, at least one unit
    uint32_t n = std::max<uint32_t>(1, c.count / 2);
    FormId scrap = planner.GetScrapItem(c.componentId);
    FormId add = scrap ? scrap : c.componentId;
    w->container.AddSimple(add, n);
    res.refunds.push_back({ ItemKey{ add }, n });
  }
  w->scrappedPrePlaced.insert(ref.refId);
  ++w->version;
  return res;
}

WorkshopResult WorkshopService::ConnectWire(const WorkshopActor& a,
                                            FormId workshopId, FormId from,
                                            FormId to, FormId splineBaseId,
                                            uint32_t nonce)
{
  Workshop* w = nullptr;
  auto r = Check(a, workshopId, WorkshopPerm::Build, true, w);
  if (!r.Ok()) {
    return r;
  }
  if (!CheckNonce(a.actorId, nonce)) {
    return Err(WorkshopError::DuplicateNonce);
  }
  auto ia = w->objects.find(from), ib = w->objects.find(to);
  if (from == to || ia == w->objects.end() || ib == w->objects.end() ||
      !IsPowerNode(ia->second.baseId) || !IsPowerNode(ib->second.baseId)) {
    return Err(WorkshopError::WireInvalid);
  }
  if (Dist(ia->second.pos, ib->second.pos) > settings.maxWireLength) {
    return Err(WorkshopError::WireTooLong);
  }
  size_t countA = 0, countB = 0;
  for (auto& wr : w->wires) {
    if ((wr.a == from && wr.b == to) || (wr.a == to && wr.b == from)) {
      return Err(WorkshopError::WireDuplicate);
    }
    countA += (wr.a == from || wr.b == from);
    countB += (wr.a == to || wr.b == to);
  }
  if (countA >= settings.maxWiresPerConnector ||
      countB >= settings.maxWiresPerConnector) {
    return Err(WorkshopError::TooManyWires);
  }
  Wire wire{ allocateFormId ? allocateFormId() : 0, from, to, splineBaseId };
  w->wires.push_back(wire);
  ++w->version;
  Recompute(*w);
  WorkshopResult ok;
  ok.refId = wire.wireRefId;
  return ok;
}

WorkshopResult WorkshopService::DisconnectWire(const WorkshopActor& a,
                                               FormId workshopId,
                                               FormId wireRefId,
                                               uint32_t nonce)
{
  Workshop* w = nullptr;
  auto r = Check(a, workshopId, WorkshopPerm::Build, true, w);
  if (!r.Ok()) {
    return r;
  }
  if (!CheckNonce(a.actorId, nonce)) {
    return Err(WorkshopError::DuplicateNonce);
  }
  auto it = std::find_if(w->wires.begin(), w->wires.end(),
                         [&](const Wire& wr) {
                           return wr.wireRefId == wireRefId;
                         });
  if (it == w->wires.end()) {
    return Err(WorkshopError::ItemNotFound);
  }
  w->wires.erase(it);
  ++w->version;
  Recompute(*w);
  return {};
}

WorkshopResult WorkshopService::AddSettler(FormId workshopId,
                                           ActorId settler)
{
  auto w = Find(workshopId);
  if (!w) {
    return Err(WorkshopError::NoSuchWorkshop);
  }
  w->settlers[settler] = { settler, 0, 0 };
  ++w->version;
  Recompute(*w);
  return {};
}

WorkshopResult WorkshopService::Assign(const WorkshopActor& a,
                                       FormId workshopId, ActorId settler,
                                       FormId objectRefId)
{
  Workshop* w = nullptr;
  auto r = Check(a, workshopId, WorkshopPerm::Assign, false, w);
  if (!r.Ok()) {
    return r;
  }
  auto s = w->settlers.find(settler);
  auto o = w->objects.find(objectRefId);
  if (s == w->settlers.end() || o == w->objects.end()) {
    return Err(WorkshopError::NotAssignable);
  }
  auto info = data.FindWorkshopObject(o->second.baseId);
  if (!info || (!info->requiresWorker && !info->isBed)) {
    return Err(WorkshopError::NotAssignable);
  }
  if (o->second.assignedActor && o->second.assignedActor != settler) {
    return Err(WorkshopError::AssignmentTaken);
  }
  if (veto && !veto(a.actorId, workshopId, "assign")) {
    return Err(WorkshopError::Vetoed);
  }
  FormId& slot = info->isBed ? s->second.bedRefId : s->second.jobRefId;
  if (slot && slot != objectRefId) {
    if (auto prev = w->objects.find(slot); prev != w->objects.end()) {
      prev->second.assignedActor = 0;
    }
  }
  slot = objectRefId;
  o->second.assignedActor = settler;
  ++w->version;
  Recompute(*w);
  return {};
}

void WorkshopService::Recompute(Workshop& w) const
{
  // --- power: union-find over wires, then fill consumers per component
  std::map<FormId, FormId> parent;
  std::function<FormId(FormId)> root = [&](FormId x) {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  };
  for (auto& [id, o] : w.objects) {
    parent[id] = id;
  }
  for (auto& wr : w.wires) {
    if (parent.count(wr.a) && parent.count(wr.b)) {
      parent[root(wr.a)] = root(wr.b);
    }
  }
  std::map<FormId, float> capacity, used;
  float totalCapacity = 0, totalLoad = 0;
  for (auto& [id, o] : w.objects) {
    o.powered = false;
    auto info = data.FindWorkshopObject(o.baseId);
    if (info && !o.destroyed && info->powerGenerated > 0.f) {
      capacity[root(id)] += info->powerGenerated;
      totalCapacity += info->powerGenerated;
      o.powered = true;
    }
  }
  for (auto& [id, o] : w.objects) { // map order = deterministic by refId
    auto info = data.FindWorkshopObject(o.baseId);
    if (!info || o.destroyed || info->powerRequired <= 0.f) {
      if (info && info->isPowerConnector) {
        o.powered = capacity[root(id)] > 0.f;
      }
      continue;
    }
    FormId c = root(id);
    totalLoad += info->powerRequired;
    if (used[c] + info->powerRequired <= capacity[c] + 1e-4f) {
      used[c] += info->powerRequired;
      o.powered = true;
    }
  }

  // --- ratings
  WorkshopRatings r;
  r.happiness = w.ratings.happiness;
  float happinessBonus = 0;
  w.budget.current = 0;
  w.budget.objects = static_cast<uint32_t>(w.objects.size());
  for (auto& [id, o] : w.objects) {
    w.budget.current += CostOf(o.baseId);
    auto info = data.FindWorkshopObject(o.baseId);
    if (!info || o.destroyed) {
      continue;
    }
    bool needsPower = info->powerRequired > 0.f;
    if (needsPower && !o.powered) {
      continue;
    }
    if (info->requiresWorker && !o.assignedActor) {
      continue;
    }
    r.food += info->food;
    r.water += info->water;
    r.safety += info->defense;
    r.beds += info->beds + (info->isBed ? 1.f : 0.f);
    happinessBonus += info->happiness;
  }
  r.powerCapacity = totalCapacity;
  r.powerLoad = totalLoad;
  r.power = totalCapacity;
  r.population = static_cast<float>(w.settlers.size());
  for (auto& [id, s] : w.settlers) {
    if (!s.jobRefId) {
      r.unassignedPopulation += 1.f;
    }
  }
  // Happiness target (approximation of WorkshopScript: each need is worth
  // up to 20 when it covers the population; objects add bonus happiness).
  if (r.population <= 0.f) {
    r.happinessTarget = 50.f;
  } else {
    auto need = [&](float have) {
      return 20.f * std::min(1.f, have / r.population);
    };
    r.happinessTarget = std::clamp(need(r.food) + need(r.water) +
                                     need(r.beds) + need(r.safety) +
                                     happinessBonus,
                                   0.f, 100.f);
  }
  w.ratings = r;
}

DailyReport WorkshopService::DailyUpdate(Workshop& w, double gameDay,
                                         std::mt19937& rng)
{
  DailyReport rep;
  Recompute(w);
  auto& r = w.ratings;
  std::uniform_real_distribution<float> roll(0.f, 1.f);

  // Production: surplus over consumption, capped by storage
  float pop = r.population;
  auto produce = [&](FormId item, float rating, float base, float perPop) {
    if (!item || w.owner.type == WorkshopOwner::Type::None) {
      return 0.f;
    }
    float surplus = std::floor(std::max(0.f, rating - pop));
    float cap = std::floor(base + perPop * pop);
    float have = static_cast<float>(w.container.CountBase(item));
    float add = std::max(0.f, std::min(surplus, cap - have));
    if (add > 0.f) {
      w.container.AddSimple(item, static_cast<uint32_t>(add));
    }
    return add;
  };
  rep.foodProduced = produce(settings.foodItemId, r.food,
                             settings.foodStorageBase,
                             settings.foodStoragePerPop);
  rep.waterProduced = produce(settings.waterItemId, r.water,
                              settings.waterStorageBase,
                              settings.waterStoragePerPop);

  // Happiness drifts 20% of the way to the target each day
  r.happiness += (r.happinessTarget - r.happiness) * 0.2f;

  // Recruitment needs a powered beacon, room and few idle settlers
  bool beacon = false;
  for (auto& [id, o] : w.objects) {
    auto info = data.FindWorkshopObject(o.baseId);
    if (info && info->isRecruitmentBeacon && !o.destroyed &&
        (info->powerRequired <= 0.f || o.powered)) {
      beacon = true;
    }
  }
  if (beacon && pop < settings.popBase && r.unassignedPopulation < 5.f &&
      w.owner.type != WorkshopOwner::Type::None) {
    float chance = 0.1f * std::max(0.1f, r.happiness / 50.f);
    rep.recruited = roll(rng) < chance;
  }

  // Attacks (T2): vanilla chance formula, only if enabled
  if (pop > 0.f) {
    rep.attackChance = std::max(0.02f, 0.02f + 0.001f * (r.food + r.water) -
                                         0.01f * r.safety - 0.005f * pop);
    rep.attackTriggered = settings.attacks && roll(rng) < rep.attackChance;
  }
  w.lastDailyUpdateDay = gameDay;
  ++w.version;
  return rep;
}

nlohmann::json WorkshopService::ToJson(const Workshop& w) const
{
  using nlohmann::json;
  json objects = json::array();
  for (auto& [id, o] : w.objects) {
    json jo = { { "refId", o.refId },     { "baseId", o.baseId },
                { "recipeId", o.recipeId }, { "pos", o.pos },
                { "rot", o.rot },           { "builtBy", o.builtBy } };
    if (o.scale != 1.f)
      jo["scale"] = o.scale;
    if (o.destroyed)
      jo["destroyed"] = true;
    if (o.assignedActor)
      jo["assignedActor"] = o.assignedActor;
    objects.push_back(std::move(jo));
  }
  json wires = json::array();
  for (auto& wr : w.wires) {
    wires.push_back({ wr.wireRefId, wr.a, wr.b, wr.splineBaseId });
  }
  json settlers = json::array();
  for (auto& [id, s] : w.settlers) {
    settlers.push_back({ s.actorId, s.jobRefId, s.bedRefId });
  }
  json acl = json::object();
  for (auto& [p, perms] : w.acl) {
    acl[std::to_string(p)] = perms;
  }
  json stored = json::object();
  for (auto& [b, n] : w.stored) {
    stored[std::to_string(b)] = n;
  }
  return json{ { "schemaVersion", 1 },
               { "workbench", w.workbenchRefId },
               { "location", w.locationId },
               { "worldOrCell", w.worldOrCell },
               { "owner",
                 { static_cast<int>(w.owner.type), w.owner.id } },
               { "acl", acl },
               { "objects", objects },
               { "scrappedPrePlaced", w.scrappedPrePlaced },
               { "stored", stored },
               { "wires", wires },
               { "settlers", settlers },
               { "container", w.container.ToJson() },
               { "happiness", w.ratings.happiness },
               { "budgetMax", w.budget.max },
               { "maxObjects", w.budget.maxObjects },
               { "lastDailyUpdateDay", w.lastDailyUpdateDay },
               { "version", w.version } };
}

Workshop WorkshopService::FromJson(const nlohmann::json& j)
{
  Workshop w;
  w.workbenchRefId = j.at("workbench").get<FormId>();
  w.locationId = j.value("location", FormId(0));
  w.worldOrCell = j.value("worldOrCell", uint32_t(0));
  if (j.contains("owner") && j["owner"].is_array() && j["owner"].size() == 2) {
    w.owner.type = static_cast<WorkshopOwner::Type>(j["owner"][0].get<int>());
    w.owner.id = j["owner"][1].get<int32_t>();
  }
  if (j.contains("acl")) {
    for (auto& [k, v] : j["acl"].items()) {
      w.acl[std::stoi(k)] = v.get<uint16_t>();
    }
  }
  for (auto& jo : j.value("objects", nlohmann::json::array())) {
    try {
      PlacedObject o;
      o.refId = jo.at("refId").get<FormId>();
      o.baseId = jo.at("baseId").get<FormId>();
      o.recipeId = jo.value("recipeId", FormId(0));
      o.pos = jo.at("pos").get<std::array<float, 3>>();
      o.rot = jo.value("rot", std::array<float, 3>{ 0, 0, 0 });
      o.scale = jo.value("scale", 1.f);
      o.destroyed = jo.value("destroyed", false);
      o.assignedActor = jo.value("assignedActor", ActorId(0));
      o.builtBy = jo.value("builtBy", ProfileId(-1));
      w.objects[o.refId] = o;
    } catch (const std::exception&) {
      // one bad object must not drop the settlement (REF-020 rule)
    }
  }
  if (j.contains("scrappedPrePlaced")) {
    for (auto& id : j["scrappedPrePlaced"]) {
      w.scrappedPrePlaced.insert(id.get<FormId>());
    }
  }
  if (j.contains("stored")) {
    for (auto& [k, v] : j["stored"].items()) {
      w.stored[static_cast<FormId>(std::stoul(k))] = v.get<uint32_t>();
    }
  }
  for (auto& jw : j.value("wires", nlohmann::json::array())) {
    w.wires.push_back({ jw[0].get<FormId>(), jw[1].get<FormId>(),
                        jw[2].get<FormId>(), jw[3].get<FormId>() });
  }
  for (auto& js : j.value("settlers", nlohmann::json::array())) {
    SettlerState s{ js[0].get<ActorId>(), js[1].get<FormId>(),
                    js[2].get<FormId>() };
    w.settlers[s.actorId] = s;
  }
  if (j.contains("container")) {
    w.container = Fo4Inventory::FromJson(j["container"]);
  }
  w.ratings.happiness = j.value("happiness", 50.f);
  w.budget.max = j.value("budgetMax", 100.f);
  w.budget.maxObjects = j.value("maxObjects", uint32_t(1000));
  w.lastDailyUpdateDay = j.value("lastDailyUpdateDay", 0.0);
  w.version = j.value("version", uint32_t(0));
  return w;
}

}
