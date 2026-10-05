#include "Fo4Server.h"
#include "Fo4Messages.h"
#include "MsgType.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace fo4 {

namespace {

fo4msg::ItemKey ToMsg(const ItemKey& k)
{
  fo4msg::ItemKey m;
  m.baseId = k.baseId;
  m.mods = k.mods;
  m.condition = k.condition;
  m.stolenFrom = k.stolenFrom;
  m.ammoLoaded = k.ammoLoaded;
  return m;
}

ItemKey FromMsg(const fo4msg::ItemKey& m)
{
  ItemKey k;
  k.baseId = m.baseId;
  k = k.WithMods(m.mods);
  k.condition = m.condition;
  k.stolenFrom = m.stolenFrom;
  k.ammoLoaded = m.ammoLoaded;
  return k;
}

std::vector<fo4msg::ItemCount> ToMsg(const std::vector<InventoryEntry>& v)
{
  std::vector<fo4msg::ItemCount> res;
  for (auto& e : v) {
    res.push_back({ ToMsg(e.key), e.count });
  }
  return res;
}

float Dist(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
  float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

template <class M>
const M& As(const IMessageBase& msg)
{
  return static_cast<const M&>(msg);
}

}

struct Fo4Server::Impl
{
  std::map<ActorId, int64_t> lastTickMs;
  int64_t lastTick = -1;
  int64_t lastTimeWeatherMs = -1000000;
  int64_t lastDiscoveryMs = -1000000;
};

Fo4Server::Fo4Server(std::shared_ptr<IFo4DataSource> data_, Fo4Host& host_,
                     Fo4ServerSettings s)
  : settings(s)
  , pImpl(std::make_unique<Impl>())
  , data(std::move(data_))
  , host(host_)
  , crafting(*data)
  , modding(*data)
  , scrap(*data)
  , powerArmor(*data, s.powerArmor)
  , workshops(*data, s.workshop)
  , vendors(*data, s.capsId)
  , locks(s.locks)
  , combat(*data, s.fire)
  , parties(s.party)
  , containers(*data, s.containers)
  , map(s.map)
  , movement(s.movement)
  , damageModel(s.damage)
  , rng(std::random_device{}())
{
  workshops.allocateFormId = [this] { return host.AllocateFormId(); };
  // Gamemode vetoes (blockable events, docs/falloutmp/guides/gamemode-api.md)
  powerArmor.veto = [this](ActorId a, FormId frame, bool entering) {
    return host.FireGamemodeEvent(
      entering ? "onFo4PowerArmorEnter" : "onFo4PowerArmorExit",
      nlohmann::json::array({ a, frame }));
  };
  workshops.veto = [this](ActorId a, FormId workshop, const char* action) {
    if (std::string(action) == "claim" &&
        settings.workshop.claimRule == WorkshopSettings::ClaimRule::Gamemode) {
      return false; // only mp.fo4.setWorkshopOwner assigns owners
    }
    return host.FireGamemodeEvent(
      "onFo4WorkshopAction", nlohmann::json::array({ a, workshop, action }));
  };
  clock.SetGameDays(8.0 / 24.0, host.NowMs());
}

Fo4Server::~Fo4Server() = default;

Fo4ActorState& Fo4Server::Actor(ActorId id)
{
  auto it = actors.find(id);
  if (it != actors.end()) {
    return it->second;
  }
  auto& st = actors[id];
  st.effects = std::make_unique<EffectSystem>(*data);
  for (auto& d : effectDefs) {
    st.effects->DefineEffect(d);
  }
  st.progression.settings = settings.progression;
  if (!st.avs.Find(Av::Strength)) {
    for (auto av : kSpecial) {
      st.avs.SetBase(av, 1.f);
    }
    st.avs.RecomputeDerived(1);
    st.avs.SetCurrent(Av::Health, st.avs.GetMax(Av::Health));
    st.avs.SetCurrent(Av::ActionPoints, st.avs.GetMax(Av::ActionPoints));
  }
  return st;
}

const Fo4ActorState* Fo4Server::FindActor(ActorId id) const
{
  auto it = actors.find(id);
  return it == actors.end() ? nullptr : &it->second;
}

void Fo4Server::DefineEffect(EffectDefinition def)
{
  effectDefs.push_back(def);
  for (auto& [id, st] : actors) {
    st.effects->DefineEffect(def);
  }
}

void Fo4Server::RemoveActor(ActorId id)
{
  auto pos = host.GetActorPos(id);
  float p[3] = { pos[0], pos[1], pos[2] };
  if (auto it = actors.find(id); it != actors.end()) {
    powerArmor.ForceExit(id, p, it->second.inventory);
  }
  workshops.ExitBuildMode(id);
  combat.Forget(id);
  movement.Forget(id);
}

void Fo4Server::SendInventory(ActorId actor)
{
  auto& st = Actor(actor);
  SetInventoryFo4Message m;
  m.refId = 0;
  m.version = ++st.inventoryVersion;
  m.entries = ToMsg(st.inventory.Entries());
  host.SendTo(actor, m, true);
}

void Fo4Server::SendActorValues(ActorId actor)
{
  auto& st = Actor(actor);
  ChangeValuesAvMessage m;
  m.idx = actor;
  for (auto& [id, e] : st.avs.Entries()) {
    m.values.push_back({ id, e.Current(), e.Max() });
  }
  host.SendTo(actor, m, true);
  // Neighbours get the public subset: health % and limb conditions
  ChangeValuesAvMessage pub;
  pub.idx = actor;
  float maxHp = st.avs.GetEffectiveMaxHealth();
  pub.values.push_back({ Av::Health,
                         maxHp > 0 ? st.avs.GetCurrent(Av::Health) / maxHp
                                   : 0.f,
                         1.f });
  for (auto limb : kLimbConditions) {
    if (st.avs.Find(limb)) {
      pub.values.push_back({ limb, st.avs.GetCurrent(limb), 100.f });
    }
  }
  host.SendToNeighbours(actor, pub, true);
}

void Fo4Server::SendProgression(ActorId actor)
{
  auto& st = Actor(actor);
  ProgressionUpdateMessage m;
  m.level = st.progression.level;
  m.xp = st.progression.xp;
  m.xpForNextLevel = XpForLevel(st.progression.level + 1);
  m.perkPoints = st.progression.perkPoints;
  for (size_t i = 0; i < 7; ++i) {
    m.special[i] = st.avs.GetCurrent(kSpecial[i]);
  }
  m.perks.assign(st.progression.ownedPerks.begin(),
                 st.progression.ownedPerks.end());
  m.created = st.progression.created;
  host.SendTo(actor, m, true);
  if (st.lastAnnouncedLevel != 0 &&
      st.progression.level > st.lastAnnouncedLevel) {
    host.FireGamemodeEvent(
      "onFo4LevelUp", nlohmann::json::array({ actor, st.progression.level }));
  }
  st.lastAnnouncedLevel = st.progression.level;
}

void Fo4Server::SendEffects(ActorId actor)
{
  auto& st = Actor(actor);
  EffectsUpdateMessage own;
  own.idx = actor;
  EffectsUpdateMessage pub;
  pub.idx = actor;
  for (auto& e : st.effects->Active()) {
    EffectsUpdateMessage::Effect m;
    m.effectId = e.effectId;
    m.sourceItem = e.sourceItem;
    m.kind = static_cast<uint8_t>(e.kind);
    m.avId = e.actorValue;
    m.magnitude = e.magnitude;
    m.remainingMs =
      static_cast<uint32_t>(std::max(0.f, e.remainingSec) * 1000.f);
    own.effects.push_back(m);
    pub.effects.push_back({ e.effectId, e.sourceItem, 0, 0, 0.f, 0 });
  }
  for (auto& [id, addicted] : st.effects->Addictions()) {
    if (addicted) {
      own.addictions.push_back(id);
    }
  }
  host.SendTo(actor, own, true);
  host.SendToNeighbours(actor, pub, true);
}

void Fo4Server::SendFullState(ActorId actor)
{
  SendInventory(actor);
  SendActorValues(actor);
  SendProgression(actor);
  SendEffects(actor);
  SendEquipment(actor);
  SendMapMarkers(actor, true);
  SendPartyState(actor, 0, "");
  uint32_t space = host.GetActorWorldOrCell(actor);
  for (auto& [id, w] : workshops.All()) {
    if (w.worldOrCell == space &&
        (!w.objects.empty() || !w.scrappedPrePlaced.empty())) {
      SendWorkshopSnapshot(actor, id);
    }
  }
  if (powerArmor.GetWorn(actor)) {
    auto snap = powerArmor.Snapshot(actor);
    PowerArmorStateMessage m;
    m.actorIdx = actor;
    m.frameRefId = snap.frameRefId;
    m.phase = static_cast<uint8_t>(snap.phase);
    m.frameBaseId = snap.frameBaseId;
    for (auto& p : snap.pieces) {
      m.pieces.push_back(
        { static_cast<uint8_t>(p.slot), ToMsg(p.key), p.healthPct });
    }
    m.coreBaseId = snap.coreBaseId;
    m.coreCharge = snap.coreCharge;
    m.unpowered = snap.unpowered;
    m.jetpackCapable = snap.jetpackCapable;
    host.SendTo(actor, m, true);
  }
}

namespace {
void Result(Fo4Host& host, ActorId actor, uint32_t nonce, MsgType type,
            bool ok, const std::string& error, uint32_t refId = 0,
            std::vector<fo4msg::ItemCount> items = {})
{
  RequestResultMessage r;
  r.nonce = nonce;
  r.requestType = static_cast<uint8_t>(type);
  r.ok = ok;
  r.error = error;
  r.refId = refId;
  r.items = std::move(items);
  host.SendTo(actor, r, true);
}
}

void Fo4Server::OnMessage(ActorId sender, MsgType type,
                          const IMessageBase& msg)
{
  auto& st = Actor(sender);
  auto perkRank = [&st](FormId p) { return st.progression.GetPerkRank(p); };

  // Workbench context: the sender must be near a workbench reference
  auto benchCtx = [&](uint32_t workbenchRefId, CrafterContext& ctx,
                      std::string& error) {
    auto refPos = host.GetRefPos(workbenchRefId);
    auto base = host.GetRefBaseId(workbenchRefId);
    auto furn = data->FindFurniture(base);
    if (!refPos || !furn) {
      error = "NoWorkbench";
      return false;
    }
    if (Dist(*refPos, host.GetActorPos(sender)) >
        settings.activationReach + settings.reachSlack) {
      error = "OutOfReach";
      return false;
    }
    ctx.workbenchKeywords = furn->keywords;
    ctx.perkRank = perkRank;
    return true;
  };

  switch (type) {
    case MsgType::UpdateMovementFo4: {
      auto& m = As<UpdateMovementFo4Message>(msg);
      if (m.idx != 0 && m.idx != sender) {
        return; // hosted NPC movement arrives with F13
      }
      MovementContext ctx;
      ctx.alive = host.IsActorAlive(sender);
      ctx.inPowerArmor = powerArmor.GetPhase(sender) == PaPhase::In;
      ctx.jetpackAllowed = ctx.inPowerArmor &&
        powerArmor.IsJetpackAllowed(sender,
                                    st.avs.GetCurrent(Av::ActionPoints));
      float carry = st.avs.GetCurrent(Av::CarryWeight);
      ctx.encumbered =
        carry > 0.f && st.inventory.TotalWeight(*data) > carry;
      if (settings.movement.speedMultAvId &&
          st.avs.Find(settings.movement.speedMultAvId)) {
        ctx.speedMult =
          st.avs.GetCurrent(settings.movement.speedMultAvId) / 100.f;
      }
      MovementSample sample{ m.seq, m.worldOrCell, m.pos, m.yaw, m.flags };
      int64_t now = host.NowMs();
      auto r = movement.Validate(sender, sample, ctx, host.GetActorPos(sender),
                                 host.GetActorWorldOrCell(sender), now);
      if (r.verdict == MovementVerdict::Correct) {
        bool correct = host.FireGamemodeEvent(
          "onFo4MovementViolation",
          nlohmann::json::array({ sender, r.reason, r.score }));
        if (correct) {
          spdlog::info("Movement correction for {:x}: {} (score {:.1f})",
                       sender, r.reason, r.score);
          host.TeleportActor(sender, r.correctionPos,
                             r.correctionWorldOrCell);
          return;
        }
        movement.ForceAccept(sender, sample, now); // the gamemode allowed it
        r.verdict = MovementVerdict::Accepted;
        r.dtSec = 0.f;
      }
      if (r.verdict != MovementVerdict::Accepted) {
        return;
      }
      host.SetActorTransform(sender, m.pos, m.yaw);
      if (ctx.inPowerArmor && r.dtSec > 0.f) {
        auto ev = powerArmor.Drain(sender, r.dtSec,
                                   static_cast<PaMovement>(r.movement), 1.f,
                                   st.inventory);
        if (ev.coreSwapped || ev.coreDepleted || ev.becameUnpowered ||
            !ev.piecesBroken.empty()) {
          SendPowerArmorStateOf(sender);
          if (ev.coreSwapped) {
            SendInventory(sender);
          }
        }
      }
      UpdateMovementFo4Message relay = m;
      relay.idx = sender;
      relay.ts = static_cast<uint32_t>(now);
      float maxHp = st.avs.GetEffectiveMaxHealth();
      relay.healthPercentage = static_cast<uint8_t>(std::clamp(
        maxHp > 0 ? st.avs.GetCurrent(Av::Health) / maxHp * 100.f : 0.f, 0.f,
        100.f));
      host.SendToNeighbours(sender, relay, false);
      return;
    }
    case MsgType::CraftItemFo4: {
      auto& m = As<CraftItemFo4Message>(msg);
      CrafterContext ctx;
      std::string err;
      if (!benchCtx(m.workbenchRefId, ctx, err)) {
        return Result(host, sender, m.nonce, type, false, err);
      }
      auto r = crafting.Craft(st.inventory, m.recipeId, m.count, ctx);
      Result(host, sender, m.nonce, type, r.Ok(), CraftErrorToString(r.error),
             0, r.Ok() ? std::vector<fo4msg::ItemCount>{ { ToMsg(r.created),
                                                           r.createdCount } }
                       : std::vector<fo4msg::ItemCount>{});
      if (r.Ok()) {
        SendInventory(sender);
        host.FireGamemodeEvent(
          "onFo4Craft",
          nlohmann::json::array({ sender, m.workbenchRefId, m.recipeId,
                                  r.created.baseId, r.createdCount }));
      }
      return;
    }
    case MsgType::ModItem: {
      auto& m = As<ModItemMessage>(msg);
      CrafterContext ctx;
      std::string err;
      if (!benchCtx(m.workbenchRefId, ctx, err)) {
        return Result(host, sender, m.nonce, type, false, err);
      }
      auto item = FromMsg(m.item);
      auto r = m.op == 0 ? modding.AttachMod(st.inventory, item, m.modId, ctx)
                         : modding.DetachMod(st.inventory, item, m.modId, ctx);
      if (r.Ok() && st.equippedWeapon && st.equippedWeapon->SameStack(item)) {
        st.equippedWeapon = r.newKey;
      }
      Result(host, sender, m.nonce, type, r.Ok(), CraftErrorToString(r.error),
             0, r.Ok() ? std::vector<fo4msg::ItemCount>{ { ToMsg(r.newKey), 1 } }
                       : std::vector<fo4msg::ItemCount>{});
      SendInventory(sender); // correction or new state either way
      if (r.Ok()) {
        host.FireGamemodeEvent(
          "onFo4ModItem",
          nlohmann::json::array({ sender, item.baseId, m.modId, m.op == 0 }));
      }
      return;
    }
    case MsgType::ScrapItem: {
      auto& m = As<ScrapItemMessage>(msg);
      CrafterContext ctx;
      std::string err;
      if (!benchCtx(m.workbenchRefId, ctx, err)) {
        return Result(host, sender, m.nonce, type, false, err);
      }
      ctx.scrapperRank = 0;
      auto item = FromMsg(m.item);
      auto r = data->FindMisc(item.baseId)
        ? scrap.ScrapJunk(st.inventory, item, m.count)
        : scrap.ScrapEquipment(st.inventory, item, ctx);
      Result(host, sender, m.nonce, type, r.Ok(), CraftErrorToString(r.error),
             0, ToMsg(r.produced));
      SendInventory(sender);
      if (r.Ok()) {
        host.FireGamemodeEvent(
          "onFo4Scrap", nlohmann::json::array({ sender, item.baseId, m.count }));
      }
      return;
    }
    case MsgType::TakeItemFo4:
    case MsgType::PutItemFo4: {
      bool take = type == MsgType::TakeItemFo4;
      struct
      {
        uint32_t nonce, refId;
        fo4msg::ItemKey item;
        uint32_t count;
      } m;
      if (take) {
        auto& t = As<TakeItemFo4Message>(msg);
        m = { t.nonce, t.refId, t.item, t.count };
      } else {
        auto& p = As<PutItemFo4Message>(msg);
        m = { p.nonce, p.refId, p.item, p.count };
      }
      auto actorPos = host.GetActorPos(sender);
      // Corpse looting: the container is a dead actor's inventory
      if (auto corpse = FindActor(m.refId);
          corpse && m.refId != sender && !host.IsActorAlive(m.refId)) {
        auto cpos = host.GetActorPos(m.refId);
        if (Dist(cpos, actorPos) > containers.settings.reach) {
          return Result(host, sender, m.nonce, type, false, "OutOfReach");
        }
        auto& from = take ? Actor(m.refId).inventory : st.inventory;
        auto& to = take ? st.inventory : Actor(m.refId).inventory;
        auto item = FromMsg(m.item);
        auto e = from.Find(item);
        if (m.count > 0 && (!e || e->count < m.count)) {
          return Result(host, sender, m.nonce, type, false, "ItemNotFound");
        }
        if (m.count > 0) {
          ItemKey key = e->key;
          from.Remove(key, m.count);
          to.Add(key, m.count);
          SendInventory(sender);
        }
        SetInventoryFo4Message inv;
        inv.refId = m.refId;
        inv.entries = ToMsg(Actor(m.refId).inventory.Entries());
        host.SendTo(sender, inv, true);
        return Result(host, sender, m.nonce, type, true, "");
      }
      if (!containers.Find(m.refId)) {
        auto base = host.GetRefBaseId(m.refId);
        auto pos = host.GetRefPos(m.refId);
        if (!pos || !data->FindContainer(base)) {
          return Result(host, sender, m.nonce, type, false,
                        "NoSuchContainer");
        }
        containers.Register(m.refId, base, *pos);
      }
      int32_t level = st.progression.level;
      containers.Open(m.refId, level, rng);
      bool locked = locks.GetLock(m.refId) && locks.GetLock(m.refId)->locked;
      ContainerError e = ContainerError::None;
      if (m.count > 0) {
        e = take ? containers.Take(m.refId, FromMsg(m.item), m.count,
                                   actorPos, st.inventory, false, locked)
                 : containers.Put(m.refId, FromMsg(m.item), m.count,
                                  actorPos, st.inventory, locked);
      } else if (locked) {
        e = ContainerError::Locked; // peek needs the lock open too
      }
      Result(host, sender, m.nonce, type, e == ContainerError::None,
             ContainerErrorToString(e));
      if (e == ContainerError::None) {
        if (m.count > 0) {
          SendInventory(sender);
        }
        auto c = containers.Find(m.refId);
        SendContainer(sender, m.refId, c && c->isGroundStack);
      }
      return;
    }
    case MsgType::DropItemFo4: {
      auto& m = As<DropItemFo4Message>(msg);
      FormId newRef = host.AllocateFormId();
      FormId out = 0;
      auto e = containers.Drop(newRef, FromMsg(m.item), m.count,
                               host.GetActorPos(sender), st.inventory, &out);
      if (e == ContainerError::None && st.equippedWeapon &&
          !st.inventory.Find(*st.equippedWeapon)) {
        st.equippedWeapon.reset(); // dropped the weapon in hand
      }
      Result(host, sender, m.nonce, type, e == ContainerError::None,
             ContainerErrorToString(e), out);
      if (e == ContainerError::None) {
        SendInventory(sender);
        SendContainer(sender, out, true);
      }
      return;
    }
    case MsgType::UpdateEquipmentFo4: {
      auto& m = As<UpdateEquipmentFo4Message>(msg);
      if (m.op == UpdateEquipmentFo4Message::kState) {
        SendEquipment(sender);
        return;
      }
      auto err = Equip(sender, FromMsg(m.item),
                       m.op == UpdateEquipmentFo4Message::kEquip);
      if (!err.empty()) {
        // Correction: the client re-applies the server equipment
        spdlog::debug("Equip rejected for {:x}: {}", sender, err);
        UpdateEquipmentFo4Message state;
        state.actorIdx = sender;
        if (st.equippedWeapon) {
          state.weapon = ToMsg(*st.equippedWeapon);
        }
        for (auto& a : st.equippedArmor) {
          state.armor.push_back(ToMsg(a));
        }
        host.SendTo(sender, state, true);
      }
      return;
    }
    case MsgType::FastTravelRequest: {
      auto& m = As<FastTravelRequestMessage>(msg);
      FastTravelFacts f;
      f.alive = host.IsActorAlive(sender);
      f.inBuildMode = workshops.GetBuildModeWorkshop(sender).has_value();
      f.lastCombatMs = st.lastCombatMs;
      f.carriedWeight = st.inventory.TotalWeight(*data);
      f.carryWeight = st.avs.GetCurrent(Av::CarryWeight);
      f.nowMs = host.NowMs();
      auto e = map.CanFastTravel(m.markerRefId, st.discoveredMarkers, f);
      bool ok = e == FastTravelError::None;
      if (ok &&
          !host.FireGamemodeEvent(
            "onFo4FastTravel", nlohmann::json::array({ sender, m.markerRefId }))) {
        return Result(host, sender, m.nonce, type, false, "Vetoed");
      }
      if (ok) {
        auto marker = map.Find(m.markerRefId);
        ok = host.TeleportActor(sender, marker->pos, marker->worldOrCell);
        if (!ok) {
          return Result(host, sender, m.nonce, type, false, "TeleportFailed");
        }
      }
      return Result(host, sender, m.nonce, type, ok,
                    FastTravelErrorToString(e));
    }
    case MsgType::UseItem: {
      auto& m = As<UseItemMessage>(msg);
      if (!host.FireGamemodeEvent("onFo4UseItem",
                                  nlohmann::json::array({ sender, m.baseId }))) {
        return Result(host, sender, m.nonce, type, false, "Vetoed");
      }
      auto r = st.effects->UseItem(m.baseId, st.inventory, st.avs, rng);
      Result(host, sender, m.nonce, type, r.Ok(),
             r.Ok() ? "" : (r.error == UseItemError::Dead ? "Dead"
                            : r.error == UseItemError::NotConsumable
                            ? "NotConsumable"
                            : "NotInInventory"));
      if (r.Ok()) {
        SendInventory(sender);
        SendActorValues(sender);
        SendEffects(sender);
      }
      return;
    }
    case MsgType::ProgressionRequest: {
      auto& m = As<ProgressionRequestMessage>(msg);
      ProgressionError e = ProgressionError::None;
      switch (m.op) {
        case ProgressionRequestMessage::kCreateCharacter:
          e = st.progression.CreateCharacter(m.special, st.avs);
          break;
        case ProgressionRequestMessage::kBuyPerk:
          e = st.progression.BuyPerk(m.chartKey, perkChart, st.avs);
          break;
        case ProgressionRequestMessage::kBuySpecial:
          e = st.progression.BuySpecial(m.specialAv, st.avs);
          break;
        default:
          e = ProgressionError::UnknownPerk;
      }
      Result(host, sender, m.nonce, type, e == ProgressionError::None,
             ProgressionErrorToString(e));
      if (e == ProgressionError::None &&
          m.op == ProgressionRequestMessage::kBuyPerk) {
        host.FireGamemodeEvent("onFo4PerkBought",
                               nlohmann::json::array({ sender, m.chartKey }));
      }
      SendProgression(sender);
      SendActorValues(sender);
      return;
    }
    case MsgType::PowerArmorTransition: {
      auto& m = As<PowerArmorTransitionMessage>(msg);
      PaActorFacts f;
      f.actorId = sender;
      f.profileId = host.GetProfileId(sender);
      f.alive = host.IsActorAlive(sender);
      f.isNpc = false;
      f.reach = settings.activationReach + settings.reachSlack;
      auto pos = host.GetActorPos(sender);
      if (auto frame = powerArmor.FindFrame(m.frameRefId)) {
        f.distanceToFrame =
          Dist(pos, { frame->pos[0], frame->pos[1], frame->pos[2] });
      }
      PaError e = PaError::None;
      int64_t now = host.NowMs();
      switch (m.kind) {
        case PowerArmorTransitionMessage::kEnter:
          e = powerArmor.RequestEnter(f, m.frameRefId, m.nonce, now);
          break;
        case PowerArmorTransitionMessage::kExit: {
          float p[3] = { pos[0], pos[1], pos[2] };
          e = powerArmor.RequestExit(f, p, m.nonce, now);
          break;
        }
        case PowerArmorTransitionMessage::kAck:
          e = powerArmor.Ack(sender, m.nonce, st.inventory, now);
          break;
        default:
          e = PaError::InTransition;
      }
      PowerArmorTransitionMessage reply;
      reply.nonce = m.nonce;
      reply.actorIdx = sender;
      reply.frameRefId = m.frameRefId;
      reply.kind = m.kind;
      reply.ok = e == PaError::None;
      reply.error = reply.ok ? "" : PaErrorToString(e);
      reply.phase = static_cast<uint8_t>(powerArmor.GetPhase(sender));
      reply.exitPos = pos;
      host.SendTo(sender, reply, true);
      if (reply.ok) {
        // Everyone nearby renders the new phase (F17 §4.5)
        auto snap = powerArmor.GetWorn(sender) ? powerArmor.Snapshot(sender)
                                               : PaStateSnapshot{};
        PowerArmorStateMessage s;
        s.actorIdx = sender;
        s.frameRefId = m.frameRefId;
        s.phase = static_cast<uint8_t>(snap.phase);
        s.frameBaseId = snap.frameBaseId;
        for (auto& p : snap.pieces) {
          s.pieces.push_back(
            { static_cast<uint8_t>(p.slot), ToMsg(p.key), p.healthPct });
        }
        s.coreBaseId = snap.coreBaseId;
        s.unpowered = snap.unpowered;
        s.jetpackCapable = snap.jetpackCapable;
        host.SendToNeighbours(sender, s, true);
        s.coreCharge = snap.coreCharge; // exact charge: owner only
        host.SendTo(sender, s, true);
      }
      return;
    }
    case MsgType::WeaponReload: {
      auto& m = As<WeaponReloadMessage>(msg);
      WeaponReloadMessage reply;
      reply.nonce = m.nonce;
      if (st.equippedWeapon) {
        auto r = combat.Reload(sender, *st.equippedWeapon, st.inventory);
        reply.ok = r.Ok();
        reply.loaded = r.loaded;
        if (r.Ok()) {
          st.equippedWeapon = r.weaponAfter;
          SendInventory(sender);
        }
      }
      host.SendTo(sender, reply, true);
      return;
    }
    case MsgType::WeaponFire: {
      auto& m = As<WeaponFireMessage>(msg);
      if (!st.equippedWeapon) {
        return;
      }
      auto r = combat.Fire(sender, *st.equippedWeapon, st.inventory,
                           host.GetActorPos(sender), host.NowMs());
      if (!r.Ok()) {
        spdlog::debug("WeaponFire rejected for {:x}: {}", sender,
                      FireErrorToString(r.error));
        SendInventory(sender); // restores the client's ammo count
        return;
      }
      st.equippedWeapon = r.weaponAfter;
      WeaponFireMessage relay = m;
      relay.shooterIdx = sender;
      relay.seq = r.seq;
      relay.weaponBaseId = st.equippedWeapon->baseId;
      relay.origin = host.GetActorPos(sender);
      relay.clientShotId = 0; // the shooter's own id is private
      host.SendToNeighbours(sender, relay, false);
      relay.clientShotId = m.clientShotId;
      host.SendTo(sender, relay, false); // seq for later hit claims
      return;
    }
    case MsgType::HitReport: {
      auto& m = As<HitReportMessage>(msg);
      ActorId target = m.targetIdx;
      HitClaim c;
      c.shotSeq = m.shotSeq;
      c.projectileIndex = m.projectileIndex;
      c.targetActorId = target;
      c.targetAlive = host.IsActorAlive(target);
      c.targetPos = host.GetActorPos(target);
      c.claimTimeMs = host.NowMs();
      auto shot = combat.FindShot(sender, m.shotSeq);
      if (shot) {
        // Lag compensation: where the target was when the shot arrived
        if (auto rewound = movement.PositionAt(target, shot->timeMs)) {
          c.targetPos = *rewound;
        }
      }
      auto e = combat.ValidateHit(sender, c);
      if (e != FireError::None || !shot) {
        return;
      }
      bool pvp = !host.IsNpc(sender) && !host.IsNpc(target);
      if (pvp) {
        auto verdict = parties.CanDamage(host.GetProfileId(sender),
                                         host.GetProfileId(target),
                                         c.targetPos,
                                         host.GetActorWorldOrCell(target));
        if (verdict != PvpVerdict::Allowed) {
          return;
        }
        parties.NotePvpDamage(host.GetProfileId(sender), host.NowMs());
        parties.NotePvpDamage(host.GetProfileId(target), host.NowMs());
      }
      OmodStatResolver res(*data);
      auto stats = res.ResolveWeapon(shot->weapon);
      HitInput hit;
      hit.paperDamage.push_back({ 0, stats.damage });
      for (auto& d : stats.damageTypes) {
        hit.paperDamage.push_back(d);
      }
      hit.projectiles = static_cast<uint32_t>(stats.numProjectiles);
      hit.attackerIsPlayer = !host.IsNpc(sender);
      hit.targetIsPlayer = !host.IsNpc(target);
      hit.isPvp = pvp;
      hit.headshot = m.limb == 1;
      auto& tst = Actor(target);
      TargetResistances tr;
      tr.damageResist = tst.avs.GetCurrent(Av::DamageResist);
      for (auto& a : tst.equippedArmor) {
        if (data->FindArmor(a.baseId)) {
          auto as = res.ResolveArmor(a);
          tr.damageResist += as.armorRating;
          for (auto& r : as.resistances) {
            tr.byType[r.damageTypeId] += r.value;
          }
        }
      }
      auto pa = powerArmor.GetWornProtection(target);
      tr.damageResist += pa.armorRating;
      for (auto& r : pa.resistances) {
        tr.byType[r.damageTypeId] += r.value;
      }
      auto out = damageModel.Resolve(hit, tr);
      st.lastCombatMs = host.NowMs();
      tst.lastCombatMs = host.NowMs();
      bool wasAlive = !tst.avs.IsDead();
      tst.avs.Damage(Av::Health, -out.total);
      bool killed = wasAlive && tst.avs.IsDead();
      DamageAppliedMessage d;
      d.targetIdx = target;
      d.aggressorIdx = sender;
      d.total = out.total;
      for (size_t i = 0; i < out.perType.size() && i < 3; ++i) {
        d.amounts.push_back(out.perType[i].value);
      }
      d.limb = m.limb;
      d.killed = killed;
      host.SendToNeighbours(target, d, true);
      host.SendTo(target, d, true);
      SendActorValues(target);
      if (killed) {
        AwardKillXp(sender, target);
        host.OnActorKilled(target, sender);
      }
      return;
    }
    case MsgType::WorkshopMode: {
      auto& m = As<WorkshopModeMessage>(msg);
      WorkshopModeMessage reply;
      reply.workshopRefId = m.workshopRefId;
      reply.enter = m.enter;
      if (m.enter) {
        WorkshopActor a{ sender, host.GetProfileId(sender), -1,
                         host.GetActorPos(sender), perkRank, host.NowMs() };
        auto r = workshops.EnterBuildMode(a, m.workshopRefId);
        reply.allowed = r.Ok();
        reply.reason = r.Ok() ? "" : WorkshopErrorToString(r.error);
        if (auto w = workshops.Find(m.workshopRefId)) {
          reply.perms = workshops.GetPerms(*w, a);
        }
      } else {
        workshops.ExitBuildMode(sender);
      }
      host.SendTo(sender, reply, true);
      if (reply.allowed) {
        SendWorkshopSnapshot(sender, m.workshopRefId);
        SendWorkshopState(sender, m.workshopRefId);
      }
      return;
    }
    case MsgType::WorkshopPlace: {
      auto& m = As<WorkshopPlaceMessage>(msg);
      WorkshopActor a{ sender, host.GetProfileId(sender), -1,
                       host.GetActorPos(sender), perkRank, host.NowMs() };
      PlaceRequest req;
      req.nonce = m.nonce;
      req.workshopId = m.workshopRefId;
      req.recipeId = m.recipeId;
      req.baseId = m.baseId;
      req.fromStored = m.fromStored;
      req.pos = m.pos;
      req.rot = m.rot;
      req.scale = m.scale;
      req.snapTargetRefId = m.snapTargetRefId;
      auto r = workshops.Place(a, req, st.inventory);
      Result(host, sender, m.nonce, type, r.Ok(),
             WorkshopErrorToString(r.error), r.refId);
      if (r.Ok()) {
        WorkshopObjectsMessage delta;
        delta.workshopRefId = m.workshopRefId;
        delta.version = workshops.Find(m.workshopRefId)->version;
        delta.kind = 1;
        delta.added.push_back(
          { r.refId, m.baseId, m.pos, m.rot, m.scale, 0 });
        host.SendToNeighbours(sender, delta, true);
        host.SendTo(sender, delta, true);
        SendInventory(sender);
        host.FireGamemodeEvent(
          "onFo4WorkshopPlace",
          nlohmann::json::array({ sender, m.workshopRefId, r.refId, m.baseId }));
      }
      return;
    }
    case MsgType::WorkshopEdit: {
      auto& m = As<WorkshopEditMessage>(msg);
      WorkshopActor a{ sender, host.GetProfileId(sender), -1,
                       host.GetActorPos(sender), perkRank, host.NowMs() };
      std::vector<WorkshopEditItem> items;
      for (auto& i : m.items) {
        items.push_back({ i.refId, i.pos, i.rot });
      }
      auto op = static_cast<WorkshopEditOp>(m.op);
      auto r =
        workshops.Edit(a, m.workshopRefId, op, items, st.inventory, m.nonce);
      Result(host, sender, m.nonce, type, r.Ok(),
             WorkshopErrorToString(r.error), 0, ToMsg(r.refunds));
      if (r.Ok()) {
        WorkshopObjectsMessage delta;
        delta.workshopRefId = m.workshopRefId;
        delta.version = workshops.Find(m.workshopRefId)->version;
        delta.kind = 1;
        for (auto& i : m.items) {
          if (op == WorkshopEditOp::Move) {
            auto& obj = workshops.Find(m.workshopRefId)->objects.at(i.refId);
            delta.added.push_back(
              { obj.refId, obj.baseId, obj.pos, obj.rot, obj.scale, 0 });
          } else if (op != WorkshopEditOp::Repair) {
            delta.removed.push_back(i.refId);
          }
        }
        host.SendToNeighbours(sender, delta, true);
        host.SendTo(sender, delta, true);
        SendInventory(sender);
      }
      return;
    }
    case MsgType::WorkshopWire: {
      auto& m = As<WorkshopWireMessage>(msg);
      WorkshopActor a{ sender, host.GetProfileId(sender), -1,
                       host.GetActorPos(sender), perkRank, host.NowMs() };
      auto r = m.op == 0
        ? workshops.ConnectWire(a, m.workshopRefId, m.a, m.b, m.splineBaseId,
                                m.nonce)
        : workshops.DisconnectWire(a, m.workshopRefId, m.wireRefId, m.nonce);
      Result(host, sender, m.nonce, type, r.Ok(),
             WorkshopErrorToString(r.error), r.refId);
      if (r.Ok()) {
        WorkshopWireMessage relay = m;
        relay.wireRefId = m.op == 0 ? r.refId : m.wireRefId;
        host.SendToNeighbours(sender, relay, true);
      }
      return;
    }
    case MsgType::WorkshopManage: {
      auto& m = As<WorkshopManageMessage>(msg);
      WorkshopActor a{ sender, host.GetProfileId(sender), -1,
                       host.GetActorPos(sender), perkRank, host.NowMs() };
      WorkshopResult r;
      switch (m.op) {
        case WorkshopManageMessage::kClaim:
          r = workshops.Claim(a, m.workshopRefId);
          break;
        case WorkshopManageMessage::kAbandon:
          r = workshops.Abandon(a, m.workshopRefId);
          break;
        case WorkshopManageMessage::kSetAcl:
          r = workshops.SetAcl(a, m.workshopRefId, m.profileId, m.perms);
          break;
        case WorkshopManageMessage::kAssign:
          r = workshops.Assign(a, m.workshopRefId, m.actorId, m.objectRefId);
          break;
        default:
          r.error = WorkshopError::NoPermission;
      }
      Result(host, sender, m.nonce, type, r.Ok(),
             WorkshopErrorToString(r.error));
      SendWorkshopState(sender, m.workshopRefId);
      return;
    }
    case MsgType::Barter: {
      auto& m = As<BarterMessage>(msg);
      BarterRequest req;
      req.vendorId = m.vendorId;
      for (auto& l : m.buy) {
        req.buy.push_back({ FromMsg(l.item), l.count });
      }
      for (auto& l : m.sell) {
        req.sell.push_back({ FromMsg(l.item), l.count });
      }
      req.expectedCapsDelta = m.capsDelta;
      PriceModifiers pm;
      pm.charisma = st.avs.GetCurrent(Av::Charisma);
      BarterMessage reply;
      reply.nonce = m.nonce;
      reply.op = m.op;
      reply.vendorId = m.vendorId;
      if (m.op == 0) {
        BarterError e;
        reply.capsDelta = vendors.QuoteCapsDelta(req, pm, e);
        reply.error = e == BarterError::None ? "" : BarterErrorToString(e);
      } else {
        auto r = vendors.Trade(req, st.inventory, pm, clock.GameHour(host.NowMs()));
        reply.capsDelta = r.capsDelta;
        reply.error = r.Ok() ? "" : BarterErrorToString(r.error);
        SendInventory(sender);
        if (r.Ok()) {
          host.FireGamemodeEvent(
            "onFo4Trade",
            nlohmann::json::array({ sender, m.vendorId, r.capsDelta }));
        }
      }
      host.SendTo(sender, reply, true);
      return;
    }
    case MsgType::LockpickAttempt: {
      auto& m = As<LockpickAttemptMessage>(msg);
      LockpickerFacts f{ sender, 0, st.avs.GetCurrent(Av::Perception),
                         host.NowMs() };
      // Locksmith rank = owned rank forms of the "Locksmith" chart entry
      f.locksmithRank = st.progression.GetChartRank("Locksmith");
      LockpickAttemptMessage reply;
      reply.op = m.op;
      reply.refId = m.refId;
      if (m.op == 0) {
        auto refPos = host.GetRefPos(m.refId);
        if (!refPos ||
            Dist(*refPos, host.GetActorPos(sender)) >
              settings.activationReach + settings.reachSlack) {
          reply.outcome = static_cast<uint8_t>(LockResultCode::Inaccessible);
        } else {
          auto r = locks.Activate(m.refId, f, st.inventory);
          reply.outcome = static_cast<uint8_t>(r.code);
          reply.sessionId = r.sessionId;
        }
      } else if (m.op == 1) {
        auto r = locks.Attempt(m.sessionId, f, st.inventory, rng);
        reply.outcome = static_cast<uint8_t>(r.code);
        reply.sessionId = m.sessionId;
        reply.xp = r.xp;
        if (r.xp) {
          st.progression.AwardXp(r.xp, false, st.avs);
          SendProgression(sender);
        }
        SendInventory(sender); // pin count
        if (r.code == LockResultCode::Unlocked) {
          host.FireGamemodeEvent(
            "onFo4Unlock", nlohmann::json::array({ sender, m.refId, "lockpick" }));
        }
      } else {
        locks.CancelLockpick(m.sessionId);
        return;
      }
      host.SendTo(sender, reply, true);
      return;
    }
    case MsgType::TerminalAction: {
      auto& m = As<TerminalActionMessage>(msg);
      HackerFacts f{ sender, st.progression.GetChartRank("Hacker"),
                     st.avs.GetCurrent(Av::Intelligence), host.NowMs() };
      TerminalActionMessage reply;
      reply.op = m.op;
      reply.refId = m.refId;
      auto r = m.op == 0 ? locks.BeginHack(m.refId, f)
                         : locks.HackAttempt(m.sessionId, f, rng);
      reply.outcome = static_cast<uint8_t>(r.code);
      reply.sessionId = r.sessionId;
      reply.attemptsLeft = r.attemptsLeft;
      reply.xp = r.xp;
      if (r.xp) {
        st.progression.AwardXp(r.xp, false, st.avs);
        SendProgression(sender);
      }
      host.SendTo(sender, reply, true);
      if (r.code == HackResultCode::Hacked) {
        host.FireGamemodeEvent(
          "onFo4Unlock", nlohmann::json::array({ sender, m.refId, "hack" }));
      }
      return;
    }
    case MsgType::PartyAction: {
      auto& m = As<PartyActionMessage>(msg);
      ProfileId me = host.GetProfileId(sender);
      int64_t now = host.NowMs();
      // Everyone whose party view can change: the old and new members of
      // the parties involved, plus the target of a kick.
      std::set<ProfileId> affected;
      auto addMembersOf = [&](std::optional<PartyId> pid) {
        if (auto p = pid ? parties.Find(*pid) : nullptr) {
          affected.insert(p->members.begin(), p->members.end());
        }
      };
      addMembersOf(parties.GetPartyOf(me));
      if (m.op == PartyActionMessage::kAccept) {
        addMembersOf(m.partyId);
      }
      PartyError e = PartyError::None;
      switch (m.op) {
        case PartyActionMessage::kInvite:
          e = parties.Invite(me, m.targetProfileId, now);
          break;
        case PartyActionMessage::kAccept:
          e = parties.Accept(me, m.partyId, now);
          break;
        case PartyActionMessage::kDecline:
          e = parties.Decline(me, m.partyId);
          break;
        case PartyActionMessage::kLeave:
          e = parties.Leave(me);
          break;
        case PartyActionMessage::kKick:
          e = parties.Kick(me, m.targetProfileId);
          affected.insert(m.targetProfileId);
          break;
        case PartyActionMessage::kPromote:
          e = parties.Promote(me, m.targetProfileId);
          break;
        case PartyActionMessage::kSetPvpFlag:
          if (!host.FireGamemodeEvent(
                "onFo4PvpFlagChange",
                nlohmann::json::array({ sender, m.value }))) {
            SendPartyState(sender, m.nonce, "Vetoed");
            return;
          }
          e = parties.SetPvpFlag(me, m.value, now);
          break;
        default:
          e = PartyError::UnknownParty;
      }
      addMembersOf(parties.GetPartyOf(me));
      SendPartyState(sender, m.nonce,
                     e == PartyError::None ? "" : PartyErrorToString(e));
      if (e != PartyError::None) {
        return;
      }
      if (m.op != PartyActionMessage::kSetPvpFlag) {
        static const char* kOpNames[] = { "invite", "accept",  "decline",
                                          "leave",  "kick",    "promote" };
        auto pid = parties.GetPartyOf(me);
        host.FireGamemodeEvent(
          "onFo4PartyChange",
          nlohmann::json::array({ pid ? *pid : m.partyId,
                                  m.op < 6 ? kOpNames[m.op] : "unknown", me,
                                  m.targetProfileId }));
      }
      if (m.op == PartyActionMessage::kInvite) {
        if (auto invitee = FindPlayerByProfile(m.targetProfileId)) {
          PartyActionMessage invite;
          invite.op = PartyActionMessage::kInvite;
          invite.partyId = parties.GetPartyOf(me).value_or(0);
          invite.targetProfileId = me; // who invited
          host.SendTo(*invitee, invite, true);
        }
      }
      if (m.op != PartyActionMessage::kSetPvpFlag &&
          m.op != PartyActionMessage::kDecline) {
        for (ProfileId p : affected) {
          auto actor = p == me ? std::nullopt : FindPlayerByProfile(p);
          if (actor) {
            SendPartyState(*actor, 0, "");
          }
        }
      }
      return;
    }
    default:
      spdlog::warn("Fo4Server: message type {} is not handled",
                   static_cast<int>(type));
      return;
  }
}

std::string Fo4Server::Equip(ActorId actor, const ItemKey& item, bool equip)
{
  auto& st = Actor(actor);
  auto entry = st.inventory.Find(item);
  if (equip && !entry) {
    return "NotInInventory";
  }
  ItemKey key = entry ? entry->key : item;
  if (data->FindWeapon(key.baseId)) {
    if (equip) {
      st.equippedWeapon = key;
    } else if (st.equippedWeapon && st.equippedWeapon->SameStack(key)) {
      st.equippedWeapon.reset();
    }
  } else if (auto armor = data->FindArmor(key.baseId)) {
    if (equip) {
      if (key.condition == kConditionZero) {
        return "Broken";
      }
      if (powerArmor.GetWorn(actor) &&
          armor->powerArmorSlot == PowerArmorSlot::None &&
          (armor->bipedSlots & settings.powerArmorBlockedBipedSlots)) {
        return "InPowerArmor"; // outer apparel can't go over the frame
      }
      if (armor->powerArmorSlot != PowerArmorSlot::None) {
        return "PowerArmorPiece"; // pieces go onto a frame (F17)
      }
      // Replace whatever shares a biped slot
      st.equippedArmor.erase(
        std::remove_if(st.equippedArmor.begin(), st.equippedArmor.end(),
                       [&](const ItemKey& other) {
                         auto o = data->FindArmor(other.baseId);
                         return o && (o->bipedSlots & armor->bipedSlots);
                       }),
        st.equippedArmor.end());
      st.equippedArmor.push_back(key);
    } else {
      st.equippedArmor.erase(
        std::remove_if(st.equippedArmor.begin(), st.equippedArmor.end(),
                       [&](const ItemKey& other) {
                         return other.SameStack(key);
                       }),
        st.equippedArmor.end());
    }
  } else {
    return "NotEquippable";
  }
  SendEquipment(actor);
  return "";
}

void Fo4Server::SendEquipment(ActorId actor)
{
  auto& st = Actor(actor);
  UpdateEquipmentFo4Message m;
  m.actorIdx = actor;
  m.op = UpdateEquipmentFo4Message::kState;
  if (st.equippedWeapon) {
    m.weapon = ToMsg(*st.equippedWeapon);
  }
  for (auto& a : st.equippedArmor) {
    m.armor.push_back(ToMsg(a));
  }
  host.SendTo(actor, m, true);
  host.SendToNeighbours(actor, m, true);
}

void Fo4Server::BroadcastTimeWeather()
{
  int64_t now = host.NowMs();
  WorldTimeWeatherMessage m;
  m.gameDays = clock.GameDays(now);
  m.gameHour = clock.GameHour(now);
  m.timeScale = clock.TimeScale();
  m.weatherId = weather.weatherId;
  m.transitionSec = weather.transitionSec;
  m.radstorm = weather.radstorm;
  m.serverNowMs = now;
  for (auto& [id, st] : actors) {
    if (!host.IsNpc(id)) {
      host.SendTo(id, m, true);
    }
  }
}

void Fo4Server::SendMapMarkers(ActorId actor, bool full)
{
  auto& st = Actor(actor);
  MapDiscoveryMessage m;
  m.full = full;
  for (auto& [id, marker] : map.All()) {
    if (st.discoveredMarkers.count(id) || marker.visibleByDefault) {
      m.markers.push_back({ id, marker.name, marker.type, marker.pos });
    }
  }
  host.SendTo(actor, m, true);
}

std::optional<ActorId> Fo4Server::FindPlayerByProfile(ProfileId profile) const
{
  if (profile < 0) {
    return std::nullopt;
  }
  for (auto& [id, st] : actors) {
    if (!host.IsNpc(id) && host.GetProfileId(id) == profile) {
      return id;
    }
  }
  return std::nullopt;
}

void Fo4Server::SendPartyState(ActorId to, uint32_t nonce,
                               const std::string& error)
{
  ProfileId profile = host.GetProfileId(to);
  PartyActionMessage m;
  m.nonce = nonce;
  m.op = PartyActionMessage::kState;
  m.error = error;
  if (auto pid = parties.GetPartyOf(profile)) {
    auto p = parties.Find(*pid);
    m.partyId = p->id;
    m.leader = p->leader;
    m.members = p->members;
  }
  m.value = parties.IsFlagged(profile);
  host.SendTo(to, m, true);
}

void Fo4Server::SendWorkshopState(ActorId to, FormId workshopRefId)
{
  auto w = workshops.Find(workshopRefId);
  if (!w) {
    return;
  }
  WorkshopActor a{ to, host.GetProfileId(to), -1, host.GetActorPos(to),
                   nullptr, host.NowMs() };
  WorkshopStateMessage s;
  s.workshopRefId = w->workbenchRefId;
  s.version = w->version;
  s.ownerType = static_cast<uint8_t>(w->owner.type);
  s.ownerId = w->owner.id;
  s.yourPerms = workshops.GetPerms(*w, a);
  s.budgetCurrent = w->budget.current;
  s.budgetMax = w->budget.max;
  s.objects = w->budget.objects;
  s.maxObjects = w->budget.maxObjects;
  s.food = w->ratings.food;
  s.water = w->ratings.water;
  s.safety = w->ratings.safety;
  s.beds = w->ratings.beds;
  s.power = w->ratings.power;
  s.powerLoad = w->ratings.powerLoad;
  s.population = w->ratings.population;
  s.happiness = w->ratings.happiness;
  host.SendTo(to, s, true);
}

void Fo4Server::SendWorkshopSnapshot(ActorId to, FormId workshopRefId)
{
  auto w = workshops.Find(workshopRefId);
  if (!w) {
    return;
  }
  using Obj = WorkshopObjectsMessage::Object;
  std::vector<Obj> all;
  all.reserve(w->objects.size());
  for (auto& [id, o] : w->objects) {
    uint16_t flags = 0;
    if (o.destroyed) {
      flags |= Obj::kDestroyed;
    }
    if (o.powered) {
      flags |= Obj::kPowered;
    }
    all.push_back({ o.refId, o.baseId, o.pos, o.rot, o.scale, flags });
  }
  const size_t perChunk = std::max<size_t>(1, settings.workshopSnapshotChunk);
  const size_t chunks = std::max<size_t>(1, (all.size() + perChunk - 1) / perChunk);
  for (size_t c = 0; c < chunks; ++c) {
    WorkshopObjectsMessage m;
    m.workshopRefId = workshopRefId;
    m.version = w->version;
    m.kind = 0;
    m.chunk = static_cast<uint16_t>(c);
    m.chunkCount = static_cast<uint16_t>(chunks);
    auto begin = all.begin() + std::min(all.size(), c * perChunk);
    auto end = all.begin() + std::min(all.size(), (c + 1) * perChunk);
    m.added.assign(begin, end);
    if (c == 0) {
      m.scrappedPrePlaced.assign(w->scrappedPrePlaced.begin(),
                                 w->scrappedPrePlaced.end());
      for (auto& wire : w->wires) {
        m.wires.push_back(
          { wire.wireRefId, wire.a, wire.b, wire.splineBaseId });
      }
    }
    host.SendTo(to, m, true);
  }
}

void Fo4Server::SendPowerArmorStateOf(ActorId actor)
{
  auto snap = powerArmor.Snapshot(actor);
  PowerArmorStateMessage m;
  m.actorIdx = actor;
  m.frameRefId = snap.frameRefId;
  m.phase = static_cast<uint8_t>(snap.phase);
  m.frameBaseId = snap.frameBaseId;
  for (auto& p : snap.pieces) {
    m.pieces.push_back(
      { static_cast<uint8_t>(p.slot), ToMsg(p.key), p.healthPct });
  }
  m.coreBaseId = snap.coreBaseId;
  m.unpowered = snap.unpowered;
  m.jetpackCapable = snap.jetpackCapable;
  host.SendToNeighbours(actor, m, true);
  m.coreCharge = snap.coreCharge; // exact charge: owner only
  host.SendTo(actor, m, true);
}

void Fo4Server::SendContainer(ActorId to, FormId refId, bool alsoNeighbours)
{
  auto c = containers.Find(refId);
  if (!c) {
    return;
  }
  SetInventoryFo4Message m;
  m.refId = refId;
  m.version = c->version;
  m.entries = ToMsg(c->inventory.Entries());
  host.SendTo(to, m, true);
  if (alsoNeighbours) {
    host.SendToNeighbours(to, m, true);
  }
}

void Fo4Server::AwardKillXp(ActorId killer, ActorId victim)
{
  if (host.IsNpc(killer)) {
    return;
  }
  uint32_t xp = settings.killXpBase *
    static_cast<uint32_t>(std::max(1, Actor(victim).actorLevelForXp));
  std::map<ProfileId, std::array<float, 3>> positions;
  std::map<ProfileId, ActorId> actorByProfile;
  for (auto& [id, st] : actors) {
    if (!host.IsNpc(id)) {
      ProfileId p = host.GetProfileId(id);
      positions[p] = host.GetActorPos(id);
      actorByProfile[p] = id;
    }
  }
  for (auto& [profile, share] :
       parties.ShareXp(host.GetProfileId(killer), xp, positions)) {
    auto it = actorByProfile.find(profile);
    if (it == actorByProfile.end()) {
      continue;
    }
    auto& st = Actor(it->second);
    st.progression.AwardXp(share, false, st.avs);
    SendProgression(it->second);
  }
}

void Fo4Server::Tick()
{
  int64_t now = host.NowMs();
  float dt = pImpl->lastTick < 0
    ? 0.f
    : static_cast<float>(now - pImpl->lastTick) / 1000.f;
  pImpl->lastTick = now;

  for (auto actor : powerArmor.Tick(now)) {
    PowerArmorTransitionMessage m;
    m.actorIdx = actor;
    m.ok = false;
    m.error = "Timeout";
    m.phase = static_cast<uint8_t>(powerArmor.GetPhase(actor));
    host.SendTo(actor, m, true);
  }
  if (dt > 0.f) {
    std::vector<ActorId> effectsChanged;
    for (auto& [id, st] : actors) {
      size_t before = st.effects->Active().size();
      size_t addictionsBefore = st.effects->Addictions().size();
      st.effects->Tick(dt, st.avs);
      st.avs.Tick(dt, false);
      if (st.effects->Active().size() != before ||
          st.effects->Addictions().size() != addictionsBefore) {
        effectsChanged.push_back(id);
      }
    }
    for (auto id : effectsChanged) {
      SendEffects(id); // expired effects, coalesced to one per tick
    }
  }
  if (now - pImpl->lastTimeWeatherMs >= settings.timeWeatherIntervalMs) {
    pImpl->lastTimeWeatherMs = now;
    BroadcastTimeWeather();
  }
  if (now - pImpl->lastDiscoveryMs >= 1000) {
    pImpl->lastDiscoveryMs = now;
    for (auto& [id, st] : actors) {
      if (host.IsNpc(id) || !host.IsActorAlive(id)) {
        continue;
      }
      auto found = map.Discover(host.GetActorPos(id),
                                host.GetActorWorldOrCell(id),
                                st.discoveredMarkers);
      if (!found.empty()) {
        MapDiscoveryMessage m;
        for (auto mid : found) {
          auto mk = map.Find(mid);
          m.markers.push_back({ mid, mk->name, mk->type, mk->pos });
        }
        host.SendTo(id, m, true);
        for (auto mid : found) {
          host.FireGamemodeEvent("onFo4LocationDiscovered",
                                 nlohmann::json::array({ id, mid }));
        }
        // Discovering a location awards XP like vanilla
        st.progression.AwardXp(static_cast<uint32_t>(found.size()) * 20u,
                               false, st.avs);
        SendProgression(id);
      }
    }
  }

  std::map<ActorId, std::array<float, 3>> positions;
  for (auto& [id, st] : actors) {
    if (workshops.GetBuildModeWorkshop(id)) {
      positions[id] = host.GetActorPos(id);
    }
  }
  for (auto actor : workshops.TickBuildMode(positions, now)) {
    WorkshopModeMessage m;
    m.enter = false;
    m.allowed = false;
    m.reason = "LeftBuildArea";
    host.SendTo(actor, m, true);
  }
}

nlohmann::json Fo4Server::ActorToJson(ActorId id) const
{
  auto st = FindActor(id);
  if (!st) {
    return nlohmann::json::object();
  }
  nlohmann::json j = { { "schemaVersion", 1 },
                       { "inventory", st->inventory.ToJson() },
                       { "avs", st->avs.ToJson() },
                       { "progression", st->progression.ToJson() },
                       { "effects", st->effects->ToJson() } };
  if (st->equippedWeapon) {
    j["equippedWeapon"] = ItemKeyToJson(*st->equippedWeapon);
  }
  auto armor = nlohmann::json::array();
  for (auto& a : st->equippedArmor) {
    armor.push_back(ItemKeyToJson(a));
  }
  j["equippedArmor"] = armor;
  j["discoveredMarkers"] = st->discoveredMarkers;
  if (auto w = powerArmor.GetWorn(id)) {
    j["powerArmor"] = powerArmor.WornToJson(*w);
  }
  return j;
}

void Fo4Server::LoadActor(ActorId id, const nlohmann::json& j)
{
  auto& st = Actor(id);
  if (!j.is_object()) {
    return;
  }
  // Each part loads independently: a broken part keeps its defaults
  try {
    if (j.contains("inventory"))
      st.inventory = Fo4Inventory::FromJson(j["inventory"]);
  } catch (const std::exception& e) {
    spdlog::error("Fo4Server: bad inventory for {:x}: {}", id, e.what());
  }
  try {
    if (j.contains("avs"))
      st.avs = ActorValueStore::FromJson(j["avs"]);
  } catch (const std::exception& e) {
    spdlog::error("Fo4Server: bad avs for {:x}: {}", id, e.what());
  }
  try {
    if (j.contains("progression"))
      st.progression =
        Progression::FromJson(j["progression"], settings.progression);
  } catch (const std::exception& e) {
    spdlog::error("Fo4Server: bad progression for {:x}: {}", id, e.what());
  }
  try {
    if (j.contains("effects"))
      st.effects->LoadJson(j["effects"], st.avs);
    if (j.contains("discoveredMarkers"))
      st.discoveredMarkers = j["discoveredMarkers"].get<std::set<FormId>>();
    if (j.contains("equippedWeapon"))
      st.equippedWeapon = ItemKeyFromJson(j["equippedWeapon"]);
    st.equippedArmor.clear();
    for (auto& a : j.value("equippedArmor", nlohmann::json::array())) {
      st.equippedArmor.push_back(ItemKeyFromJson(a));
    }
    if (j.contains("powerArmor")) {
      powerArmor.RestoreWorn(id, PowerArmorService::WornFromJson(j["powerArmor"]));
    }
  } catch (const std::exception& e) {
    spdlog::error("Fo4Server: bad equipment/effects for {:x}: {}", id,
                  e.what());
  }
}

nlohmann::json Fo4Server::WorldToJson() const
{
  auto ws = nlohmann::json::array();
  for (auto& [id, w] : workshops.All()) {
    ws.push_back(workshops.ToJson(w));
  }
  auto frames = nlohmann::json::array();
  for (auto& [id, f] : powerArmor.Frames()) {
    frames.push_back(powerArmor.FrameToJson(f));
  }
  return { { "schemaVersion", 1 },
           { "workshops", ws },
           { "powerArmorFrames", frames },
           { "locks", locks.ToJson() },
           { "containers", containers.ToJson() },
           { "clock", clock.ToJson(host.NowMs()) },
           { "weather",
             { { "weatherId", weather.weatherId },
               { "radstorm", weather.radstorm } } },
           { "parties", parties.ToJson() } };
}

void Fo4Server::LoadWorld(const nlohmann::json& j)
{
  if (!j.is_object()) {
    return;
  }
  for (auto& w : j.value("workshops", nlohmann::json::array())) {
    try {
      workshops.AddWorkshop(WorkshopService::FromJson(w));
    } catch (const std::exception& e) {
      spdlog::error("Fo4Server: skipping bad workshop record: {}", e.what());
    }
  }
  for (auto& f : j.value("powerArmorFrames", nlohmann::json::array())) {
    try {
      powerArmor.AddFrame(PowerArmorService::FrameFromJson(f));
    } catch (const std::exception& e) {
      spdlog::error("Fo4Server: skipping bad frame record: {}", e.what());
    }
  }
  if (j.contains("locks")) {
    locks.LoadJson(j["locks"]);
  }
  if (j.contains("parties")) {
    parties.LoadJson(j["parties"]);
  }
  if (j.contains("containers")) {
    containers.LoadJson(j["containers"]);
  }
  if (j.contains("clock")) {
    clock.LoadJson(j["clock"], host.NowMs());
  }
  if (j.contains("weather")) {
    weather.weatherId = j["weather"].value("weatherId", 0u);
    weather.radstorm = j["weather"].value("radstorm", false);
  }
}

}
