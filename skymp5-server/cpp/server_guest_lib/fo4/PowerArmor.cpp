#include "PowerArmor.h"
#include <algorithm>
#include <nlohmann/json.hpp>

// Design note: while power armor is worn, its pieces and core live in the
// WornPowerArmor record, not in the actor's normal inventory. That keeps
// worn pieces out of drop/sell/trade paths (no duplication vector) and makes
// the frame/worn split the single source of truth. Spare cores stay in the
// actor inventory. Recorded as a deviation in docs/falloutmp/STATUS.md.

namespace fo4 {

const char* PaErrorToString(PaError e) noexcept
{
  switch (e) {
    case PaError::None:
      return "None";
    case PaError::NoSuchFrame:
      return "NoSuchFrame";
    case PaError::Occupied:
      return "Occupied";
    case PaError::InTransition:
      return "InTransition";
    case PaError::OutOfReach:
      return "OutOfReach";
    case PaError::ActorDead:
      return "ActorDead";
    case PaError::AlreadyInPowerArmor:
      return "AlreadyInPowerArmor";
    case PaError::NotInPowerArmor:
      return "NotInPowerArmor";
    case PaError::InFurniture:
      return "InFurniture";
    case PaError::InCombat:
      return "InCombat";
    case PaError::OwnedByOther:
      return "OwnedByOther";
    case PaError::Vetoed:
      return "Vetoed";
    case PaError::Airborne:
      return "Airborne";
    case PaError::WrongNonce:
      return "WrongNonce";
    case PaError::NotAPiece:
      return "NotAPiece";
    case PaError::SlotTaken:
      return "SlotTaken";
    case PaError::PieceBroken:
      return "PieceBroken";
    case PaError::CoreAlreadyPresent:
      return "CoreAlreadyPresent";
    case PaError::FrameInUse:
      return "FrameInUse";
  }
  return "Unknown";
}

bool WornPowerArmor::Unpowered() const
{
  return !core || CoreCharge() <= 0.f;
}

float WornPowerArmor::CoreCharge() const
{
  return core ? ConditionToFraction(core->condition) : 0.f;
}

PowerArmorService::PowerArmorService(const IFo4DataSource& data_,
                                     PowerArmorSettings settings_)
  : settings(std::move(settings_))
  , data(data_)
  , resolver(data_)
{
}

PowerArmorFrame& PowerArmorService::AddFrame(PowerArmorFrame frame)
{
  FormId id = frame.refId;
  return frames[id] = std::move(frame);
}

PowerArmorFrame* PowerArmorService::FindFrame(FormId refId)
{
  auto it = frames.find(refId);
  return it == frames.end() ? nullptr : &it->second;
}

const PowerArmorFrame* PowerArmorService::FindFrame(FormId refId) const
{
  auto it = frames.find(refId);
  return it == frames.end() ? nullptr : &it->second;
}

PowerArmorSlot PowerArmorService::SlotOf(const ItemKey& key) const
{
  auto a = data.FindArmor(key.baseId);
  return a ? a->powerArmorSlot : PowerArmorSlot::None;
}

float PowerArmorService::MaxHealth(const ItemKey& key) const
{
  if (!data.FindArmor(key.baseId)) {
    return 0.f;
  }
  return resolver.ResolveArmor(key).health;
}

PaError PowerArmorService::CanPutIntoFrame(const PowerArmorFrame& frame,
                                           const ItemKey& item) const
{
  if (frame.wornBy) {
    return PaError::FrameInUse;
  }
  if (item.baseId == settings.fusionCoreId) {
    return frame.contents.CountBase(settings.fusionCoreId) > 0
      ? PaError::CoreAlreadyPresent
      : PaError::None;
  }
  auto slot = SlotOf(item);
  if (slot == PowerArmorSlot::None) {
    return PaError::NotAPiece;
  }
  if (item.condition == kConditionZero) {
    return PaError::PieceBroken;
  }
  for (auto& e : frame.contents.Entries()) {
    if (SlotOf(e.key) == slot) {
      return PaError::SlotTaken;
    }
  }
  return PaError::None;
}

PaError PowerArmorService::RequestEnter(const PaActorFacts& actor,
                                        FormId frameRefId, uint32_t nonce,
                                        int64_t nowMs)
{
  auto frame = FindFrame(frameRefId);
  if (!frame || !frame->enabled) {
    return PaError::NoSuchFrame;
  }
  if (auto it = worn.find(actor.actorId); it != worn.end()) {
    return it->second.phase == PaPhase::In ? PaError::AlreadyInPowerArmor
                                           : PaError::InTransition;
  }
  if (frame->wornBy) {
    return PaError::Occupied;
  }
  if (actor.distanceToFrame > actor.reach) {
    return PaError::OutOfReach;
  }
  if (!actor.alive) {
    return PaError::ActorDead;
  }
  if (actor.inFurniture) {
    return PaError::InFurniture;
  }
  if (actor.inCombat && !settings.allowInCombat) {
    return PaError::InCombat;
  }
  bool unowned = frame->ownerProfileId < 0;
  bool mine = !unowned && frame->ownerProfileId == actor.profileId;
  if (!unowned && !mine && !settings.allowStealing && !actor.isNpc) {
    return PaError::OwnedByOther;
  }
  if (veto && !veto(actor.actorId, frameRefId, true)) {
    return PaError::Vetoed;
  }
  if (unowned && actor.profileId >= 0) {
    frame->ownerProfileId = actor.profileId;
  }
  frame->wornBy = actor.actorId;
  WornPowerArmor w;
  w.frameRefId = frameRefId;
  w.phase = PaPhase::Entering;
  w.nonce = nonce;
  w.transitionStartedMs = nowMs;
  w.isNpc = actor.isNpc;
  worn[actor.actorId] = std::move(w);
  return PaError::None;
}

void PowerArmorService::MovePiecesToActor(PowerArmorFrame& frame,
                                          WornPowerArmor& w,
                                          Fo4Inventory& actorInventory)
{
  (void)actorInventory;
  for (auto& e : frame.contents.Entries()) {
    if (e.key.baseId == settings.fusionCoreId) {
      if (!w.core) {
        w.core = e.key;
      }
      continue;
    }
    auto slot = SlotOf(e.key);
    if (slot != PowerArmorSlot::None) {
      w.pieces[static_cast<size_t>(slot) - 1] = e.key;
    }
  }
  frame.contents.Clear();
}

void PowerArmorService::MovePiecesToFrame(PowerArmorFrame& frame,
                                          WornPowerArmor& w,
                                          Fo4Inventory& actorInventory)
{
  (void)actorInventory;
  for (auto& p : w.pieces) {
    if (p) {
      frame.contents.Add(*p, 1);
      p.reset();
    }
  }
  if (w.core) {
    // A depleted core is consumed, never left in the frame
    if (w.CoreCharge() > 0.f) {
      frame.contents.Add(*w.core, 1);
    }
    w.core.reset();
  }
}

PaError PowerArmorService::Ack(ActorId actor, uint32_t nonce,
                               Fo4Inventory& actorInventory, int64_t nowMs)
{
  (void)nowMs;
  auto it = worn.find(actor);
  if (it == worn.end()) {
    return PaError::NotInPowerArmor;
  }
  auto& w = it->second;
  if (w.nonce != nonce) {
    return PaError::WrongNonce;
  }
  auto frame = FindFrame(w.frameRefId);
  if (!frame) {
    worn.erase(it);
    return PaError::NoSuchFrame;
  }
  if (w.phase == PaPhase::Entering) {
    MovePiecesToActor(*frame, w, actorInventory);
    frame->enabled = false; // the frame is now on the wearer
    w.phase = PaPhase::In;
    return PaError::None;
  }
  if (w.phase == PaPhase::Exiting) {
    auto pos = pendingExitPos[actor];
    MovePiecesToFrame(*frame, w, actorInventory);
    std::copy(pos.begin(), pos.end(), frame->pos);
    frame->enabled = true;
    frame->wornBy.reset();
    pendingExitPos.erase(actor);
    worn.erase(it);
    return PaError::None;
  }
  return PaError::InTransition;
}

PaError PowerArmorService::RequestExit(const PaActorFacts& actor,
                                       const float exitPos[3], uint32_t nonce,
                                       int64_t nowMs)
{
  auto it = worn.find(actor.actorId);
  if (it == worn.end()) {
    return PaError::NotInPowerArmor;
  }
  auto& w = it->second;
  if (w.phase != PaPhase::In) {
    return PaError::InTransition;
  }
  if (actor.airborne && !settings.exitWhileFalling) {
    return PaError::Airborne;
  }
  if (veto && !veto(actor.actorId, w.frameRefId, false)) {
    return PaError::Vetoed;
  }
  w.phase = PaPhase::Exiting;
  w.nonce = nonce;
  w.transitionStartedMs = nowMs;
  pendingExitPos[actor.actorId] = { exitPos[0], exitPos[1], exitPos[2] };
  return PaError::None;
}

void PowerArmorService::ForceExit(ActorId actor, const float exitPos[3],
                                  Fo4Inventory& actorInventory)
{
  auto it = worn.find(actor);
  if (it == worn.end()) {
    return;
  }
  auto& w = it->second;
  if (auto frame = FindFrame(w.frameRefId)) {
    if (w.phase == PaPhase::Entering) {
      // Nothing moved yet: plain rollback, frame stays where it was
    } else {
      MovePiecesToFrame(*frame, w, actorInventory);
      std::copy(exitPos, exitPos + 3, frame->pos);
    }
    frame->enabled = true;
    frame->wornBy.reset();
  }
  pendingExitPos.erase(actor);
  worn.erase(it);
}

std::vector<ActorId> PowerArmorService::Tick(int64_t nowMs)
{
  std::vector<ActorId> rolledBack;
  for (auto it = worn.begin(); it != worn.end();) {
    auto& w = it->second;
    bool pending = w.phase == PaPhase::Entering || w.phase == PaPhase::Exiting;
    if (pending &&
        nowMs - w.transitionStartedMs > settings.transitionTimeoutMs) {
      rolledBack.push_back(it->first);
      if (w.phase == PaPhase::Entering) {
        if (auto frame = FindFrame(w.frameRefId)) {
          frame->wornBy.reset();
          frame->enabled = true;
        }
        it = worn.erase(it);
        continue;
      }
      // Exit not confirmed: the wearer stays in power armor
      w.phase = PaPhase::In;
      pendingExitPos.erase(it->first);
    }
    ++it;
  }
  return rolledBack;
}

const WornPowerArmor* PowerArmorService::GetWorn(ActorId actor) const
{
  auto it = worn.find(actor);
  return it == worn.end() ? nullptr : &it->second;
}

PaPhase PowerArmorService::GetPhase(ActorId actor) const
{
  auto w = GetWorn(actor);
  return w ? w->phase : PaPhase::Out;
}

bool PowerArmorService::TrySwapCore(WornPowerArmor& w,
                                    Fo4Inventory& actorInventory)
{
  // Pick the most charged spare core
  const InventoryEntry* best = nullptr;
  for (auto e : actorInventory.FindAllOf(settings.fusionCoreId)) {
    if (ConditionToFraction(e->key.condition) <= 0.f) {
      continue;
    }
    if (!best ||
        ConditionToFraction(e->key.condition) >
          ConditionToFraction(best->key.condition)) {
      best = e;
    }
  }
  if (!best) {
    return false;
  }
  ItemKey key = best->key;
  actorInventory.Remove(key, 1);
  w.core = key;
  return true;
}

PaEvents PowerArmorService::Drain(ActorId actor, float dtSec,
                                  PaMovement movement, float durationMult,
                                  Fo4Inventory& actorInventory)
{
  PaEvents ev;
  auto it = worn.find(actor);
  if (it == worn.end() || it->second.phase != PaPhase::In ||
      it->second.isNpc || dtSec <= 0.f) {
    return ev;
  }
  auto& w = it->second;
  bool wasUnpowered = w.Unpowered();
  if (wasUnpowered) {
    if (TrySwapCore(w, actorInventory)) {
      ev.coreSwapped = true;
    }
    return ev;
  }
  float rate = settings.drainPerSecond[static_cast<size_t>(movement)];
  float mult = durationMult > 0.f ? 1.f / durationMult : 1.f;
  // Movement samples arrive at 10 Hz and drain far less than one condition
  // step each, so the remainder is carried instead of rounded away.
  float charge = w.CoreCharge() - w.pendingDrain - dtSec * rate * mult;
  if (charge <= 0.f) {
    ev.coreDepleted = true;
    w.core.reset(); // depleted core is consumed
    w.pendingDrain = 0.f;
    if (TrySwapCore(w, actorInventory)) {
      ev.coreSwapped = true;
    } else {
      ev.becameUnpowered = true;
    }
    return ev;
  }
  w.core->condition = FractionToCondition(charge);
  w.pendingDrain = ConditionToFraction(w.core->condition) - charge;
  return ev;
}

PaEvents PowerArmorService::DamagePiece(ActorId actor, PowerArmorSlot slot,
                                        float damage)
{
  PaEvents ev;
  auto it = worn.find(actor);
  if (it == worn.end() || it->second.phase != PaPhase::In ||
      slot == PowerArmorSlot::None || damage <= 0.f) {
    return ev;
  }
  auto& piece = it->second.pieces[static_cast<size_t>(slot) - 1];
  if (!piece || piece->condition == kConditionZero) {
    return ev;
  }
  float maxHealth = MaxHealth(*piece);
  if (maxHealth <= 0.f) {
    return ev;
  }
  float mult = it->second.isNpc ? settings.pieceDamageMultNpc
                                : settings.pieceDamageMultPlayer;
  float health = ConditionToFraction(piece->condition) * maxHealth -
    damage * mult;
  piece->condition = FractionToCondition(health / maxHealth);
  if (piece->condition == kConditionZero) {
    ev.piecesBroken.push_back(slot);
  }
  return ev;
}

bool PowerArmorService::IsJetpackAllowed(ActorId actor,
                                         float actionPoints) const
{
  auto w = GetWorn(actor);
  if (!w || w->phase != PaPhase::In || w->Unpowered() || actionPoints <= 0.f) {
    return false;
  }
  auto& torso = w->pieces[static_cast<size_t>(PowerArmorSlot::Torso) - 1];
  if (!torso || torso->condition == kConditionZero ||
      settings.jetpackKeywordId == 0) {
    return false;
  }
  return resolver.ResolveArmor(*torso).keywords.count(
           settings.jetpackKeywordId) > 0;
}

ArmorStats PowerArmorService::GetWornProtection(ActorId actor) const
{
  ArmorStats total;
  auto w = GetWorn(actor);
  if (!w || w->phase != PaPhase::In) {
    return total;
  }
  std::map<FormId, float> res;
  for (auto& p : w->pieces) {
    if (!p || p->condition == kConditionZero) {
      continue; // broken pieces give no protection
    }
    auto s = resolver.ResolveArmor(*p);
    total.armorRating += s.armorRating;
    for (auto& r : s.resistances) {
      res[r.damageTypeId] += r.value;
    }
  }
  for (auto& [id, v] : res) {
    total.resistances.push_back({ id, v });
  }
  return total;
}

PaStateSnapshot PowerArmorService::Snapshot(ActorId actor) const
{
  PaStateSnapshot s;
  auto w = GetWorn(actor);
  if (!w) {
    return s;
  }
  s.phase = w->phase;
  s.frameRefId = w->frameRefId;
  if (auto f = FindFrame(w->frameRefId)) {
    s.frameBaseId = f->baseId;
  }
  // While entering, pieces are still on the frame
  if (w->phase == PaPhase::Entering) {
    auto fs = FrameSnapshot(w->frameRefId);
    s.pieces = fs.pieces;
    s.coreBaseId = fs.coreBaseId;
    s.coreCharge = fs.coreCharge;
  } else {
    for (size_t i = 0; i < kPowerArmorSlotCount; ++i) {
      if (auto& p = w->pieces[i]) {
        s.pieces.push_back(
          { static_cast<PowerArmorSlot>(i + 1), *p,
            static_cast<uint8_t>(
              std::lround(ConditionToFraction(p->condition) * 100.f)) });
      }
    }
    if (w->core) {
      s.coreBaseId = w->core->baseId;
      s.coreCharge = w->CoreCharge();
    }
  }
  s.unpowered = w->phase == PaPhase::In && w->Unpowered();
  s.jetpackCapable = IsJetpackAllowed(actor, 1.f);
  return s;
}

PaStateSnapshot PowerArmorService::FrameSnapshot(FormId frameRefId) const
{
  PaStateSnapshot s;
  auto f = FindFrame(frameRefId);
  if (!f) {
    return s;
  }
  s.frameRefId = f->refId;
  s.frameBaseId = f->baseId;
  for (auto& e : f->contents.Entries()) {
    if (e.key.baseId == settings.fusionCoreId) {
      s.coreBaseId = e.key.baseId;
      s.coreCharge = ConditionToFraction(e.key.condition);
      continue;
    }
    auto slot = SlotOf(e.key);
    if (slot != PowerArmorSlot::None) {
      s.pieces.push_back(
        { slot, e.key,
          static_cast<uint8_t>(
            std::lround(ConditionToFraction(e.key.condition) * 100.f)) });
    }
  }
  std::sort(s.pieces.begin(), s.pieces.end(),
            [](auto& a, auto& b) { return a.slot < b.slot; });
  return s;
}

nlohmann::json PowerArmorService::FrameToJson(const PowerArmorFrame& f) const
{
  nlohmann::json j = {
    { "refId", f.refId },
    { "baseId", f.baseId },
    { "owner", f.ownerProfileId },
    { "enabled", f.enabled },
    { "pos", { f.pos[0], f.pos[1], f.pos[2] } },
    { "worldOrCell", f.worldOrCell },
    { "contents", f.contents.ToJson() },
  };
  if (f.wornBy) {
    j["wornBy"] = *f.wornBy;
  }
  return j;
}

PowerArmorFrame PowerArmorService::FrameFromJson(const nlohmann::json& j)
{
  PowerArmorFrame f;
  f.refId = j.at("refId").get<FormId>();
  f.baseId = j.value("baseId", FormId(0));
  f.ownerProfileId = j.value("owner", ProfileId(-1));
  f.enabled = j.value("enabled", true);
  if (j.contains("pos") && j["pos"].is_array() && j["pos"].size() == 3) {
    for (int i = 0; i < 3; ++i) {
      f.pos[i] = j["pos"][i].get<float>();
    }
  }
  f.worldOrCell = j.value("worldOrCell", uint32_t(0));
  if (j.contains("contents")) {
    f.contents = Fo4Inventory::FromJson(j["contents"]);
  }
  if (j.contains("wornBy")) {
    f.wornBy = j["wornBy"].get<ActorId>();
  }
  return f;
}

nlohmann::json PowerArmorService::WornToJson(const WornPowerArmor& w) const
{
  auto pieces = nlohmann::json::array();
  for (auto& p : w.pieces) {
    pieces.push_back(p ? ItemKeyToJson(*p) : nlohmann::json());
  }
  nlohmann::json j = { { "frameRefId", w.frameRefId },
                       { "pieces", pieces },
                       { "isNpc", w.isNpc } };
  if (w.core) {
    j["core"] = ItemKeyToJson(*w.core);
  }
  return j;
}

WornPowerArmor PowerArmorService::WornFromJson(const nlohmann::json& j)
{
  WornPowerArmor w;
  w.frameRefId = j.at("frameRefId").get<FormId>();
  w.isNpc = j.value("isNpc", false);
  if (j.contains("pieces") && j["pieces"].is_array()) {
    for (size_t i = 0; i < kPowerArmorSlotCount && i < j["pieces"].size();
         ++i) {
      if (!j["pieces"][i].is_null()) {
        w.pieces[i] = ItemKeyFromJson(j["pieces"][i]);
      }
    }
  }
  if (j.contains("core")) {
    w.core = ItemKeyFromJson(j["core"]);
  }
  // Transitions are never persisted: a loaded record is always "in"
  w.phase = PaPhase::In;
  return w;
}

void PowerArmorService::RestoreWorn(ActorId actor, WornPowerArmor w)
{
  w.phase = PaPhase::In;
  if (auto f = FindFrame(w.frameRefId)) {
    f->wornBy = actor;
    f->enabled = false;
  }
  worn[actor] = std::move(w);
}

}
