#pragma once
// Fallout 4 network messages (registry: docs/falloutmp/01-sync-standard.md
// section 6). Every message works in binary (BitStream) and JSON form, like
// the Skyrim messages. Enums travel as uint8_t.
#include "MessageBase.h"
#include "MsgType.h"
#include <array>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#define FO4_MSG_TYPE(Name)                                                    \
  static constexpr auto kMsgType =                                            \
    std::integral_constant<char, static_cast<char>(MsgType::Name)>{};

namespace fo4msg {

struct ItemKey
{
  uint32_t baseId = 0;
  std::vector<uint32_t> mods;
  uint16_t condition = 0;
  uint32_t stolenFrom = 0;
  uint16_t ammoLoaded = 0;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("baseId", baseId)
      .Serialize("mods", mods)
      .Serialize("condition", condition)
      .Serialize("stolenFrom", stolenFrom)
      .Serialize("ammoLoaded", ammoLoaded);
  }
};

struct ItemCount
{
  ItemKey item;
  uint32_t count = 0;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("item", item).Serialize("count", count);
  }
};

struct AvValue
{
  uint32_t avId = 0;
  float current = 0.f;
  float max = 0.f;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("avId", avId).Serialize("current", current).Serialize("max",
                                                                      max);
  }
};

}

// 68: inventory snapshot of the player (refId 0) or a container/workshop
struct SetInventoryFo4Message : public MessageBase<SetInventoryFo4Message>
{
  FO4_MSG_TYPE(SetInventoryFo4)
  uint32_t refId = 0;
  uint32_t version = 0;
  std::vector<fo4msg::ItemCount> entries;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("refId", refId)
      .Serialize("version", version)
      .Serialize("entries", entries);
  }
};

// 69/70: put into / take from a container (refId); 71: drop to ground
template <class Self, MsgType T>
struct ContainerOpFo4 : public MessageBase<Self>
{
  static constexpr auto kMsgType =
    std::integral_constant<char, static_cast<char>(T)>{};
  uint32_t nonce = 0;
  uint32_t refId = 0;
  fo4msg::ItemKey item;
  uint32_t count = 1;
  std::array<float, 3> pos = { 0, 0, 0 }; // drop position

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("nonce", nonce)
      .Serialize("refId", refId)
      .Serialize("item", item)
      .Serialize("count", count)
      .Serialize("pos", pos);
  }
};
struct PutItemFo4Message
  : public ContainerOpFo4<PutItemFo4Message, MsgType::PutItemFo4>
{
};
struct TakeItemFo4Message
  : public ContainerOpFo4<TakeItemFo4Message, MsgType::TakeItemFo4>
{
};
struct DropItemFo4Message
  : public ContainerOpFo4<DropItemFo4Message, MsgType::DropItemFo4>
{
};

// 72: actor values (owner: full set; neighbours: public subset)
struct ChangeValuesAvMessage : public MessageBase<ChangeValuesAvMessage>
{
  FO4_MSG_TYPE(ChangeValuesAv)
  uint32_t idx = 0;
  std::vector<fo4msg::AvValue> values;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType).Serialize("idx", idx).Serialize("values",
                                                               values);
  }
};

// 76: craft a recipe at a workbench
struct CraftItemFo4Message : public MessageBase<CraftItemFo4Message>
{
  FO4_MSG_TYPE(CraftItemFo4)
  uint32_t nonce = 0;
  uint32_t workbenchRefId = 0;
  uint32_t recipeId = 0;
  uint32_t count = 1;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("nonce", nonce)
      .Serialize("workbenchRefId", workbenchRefId)
      .Serialize("recipeId", recipeId)
      .Serialize("count", count);
  }
};

// 80: a shot (C->S request; S->C relay to neighbours with shooterIdx)
struct WeaponFireMessage : public MessageBase<WeaponFireMessage>
{
  FO4_MSG_TYPE(WeaponFire)
  uint32_t shooterIdx = 0;
  uint32_t seq = 0; // server-assigned on relay
  uint32_t weaponBaseId = 0;
  std::array<float, 3> origin = { 0, 0, 0 };
  std::array<float, 3> direction = { 0, 0, 0 };

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("shooterIdx", shooterIdx)
      .Serialize("seq", seq)
      .Serialize("weaponBaseId", weaponBaseId)
      .Serialize("origin", origin)
      .Serialize("direction", direction);
  }
};

// 81: reload request / result
struct WeaponReloadMessage : public MessageBase<WeaponReloadMessage>
{
  FO4_MSG_TYPE(WeaponReload)
  uint32_t nonce = 0;
  uint16_t loaded = 0;
  bool ok = false;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("nonce", nonce)
      .Serialize("loaded", loaded)
      .Serialize("ok", ok);
  }
};

// 82: hit claim for a recorded shot
struct HitReportMessage : public MessageBase<HitReportMessage>
{
  FO4_MSG_TYPE(HitReport)
  uint32_t shotSeq = 0;
  uint32_t projectileIndex = 0;
  uint32_t targetIdx = 0;
  uint8_t limb = 0;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("shotSeq", shotSeq)
      .Serialize("projectileIndex", projectileIndex)
      .Serialize("targetIdx", targetIdx)
      .Serialize("limb", limb);
  }
};

// 83: authoritative damage result (amounts capped at 3 types, M10)
struct DamageAppliedMessage : public MessageBase<DamageAppliedMessage>
{
  FO4_MSG_TYPE(DamageApplied)
  uint32_t targetIdx = 0;
  uint32_t aggressorIdx = 0;
  float total = 0.f;
  std::vector<float> amounts;
  uint8_t limb = 0;
  bool critical = false;
  bool killed = false;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("targetIdx", targetIdx)
      .Serialize("aggressorIdx", aggressorIdx)
      .Serialize("total", total)
      .Serialize("amounts", amounts)
      .Serialize("limb", limb)
      .Serialize("critical", critical)
      .Serialize("killed", killed);
  }
};

// 85: attach (op 0) or detach (op 1) a mod at a workbench
struct ModItemMessage : public MessageBase<ModItemMessage>
{
  FO4_MSG_TYPE(ModItem)
  uint32_t nonce = 0;
  uint32_t workbenchRefId = 0;
  fo4msg::ItemKey item;
  uint32_t modId = 0;
  uint8_t op = 0;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("nonce", nonce)
      .Serialize("workbenchRefId", workbenchRefId)
      .Serialize("item", item)
      .Serialize("modId", modId)
      .Serialize("op", op);
  }
};

// 86: scrap junk or equipment at a workbench
struct ScrapItemMessage : public MessageBase<ScrapItemMessage>
{
  FO4_MSG_TYPE(ScrapItem)
  uint32_t nonce = 0;
  uint32_t workbenchRefId = 0;
  fo4msg::ItemKey item;
  uint32_t count = 1;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("nonce", nonce)
      .Serialize("workbenchRefId", workbenchRefId)
      .Serialize("item", item)
      .Serialize("count", count);
  }
};

// 87: power armor enter/exit/ack (C->S) and phase commands/results (S->C)
struct PowerArmorTransitionMessage
  : public MessageBase<PowerArmorTransitionMessage>
{
  FO4_MSG_TYPE(PowerArmorTransition)
  enum Kind : uint8_t
  {
    kEnter = 0,
    kExit = 1,
    kAck = 2,
    kEjectCore = 3,
  };
  uint32_t nonce = 0;
  uint32_t actorIdx = 0;
  uint32_t frameRefId = 0;
  uint8_t kind = kEnter;
  uint8_t phase = 0; // fo4::PaPhase
  bool ok = true;
  std::string error;
  std::array<float, 3> exitPos = { 0, 0, 0 };

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("nonce", nonce)
      .Serialize("actorIdx", actorIdx)
      .Serialize("frameRefId", frameRefId)
      .Serialize("kind", kind)
      .Serialize("phase", phase)
      .Serialize("ok", ok)
      .Serialize("error", error)
      .Serialize("exitPos", exitPos);
  }
};

// 88: power armor state of an actor or a parked frame
struct PowerArmorStateMessage : public MessageBase<PowerArmorStateMessage>
{
  FO4_MSG_TYPE(PowerArmorState)
  struct Piece
  {
    uint8_t slot = 0;
    fo4msg::ItemKey item;
    uint8_t healthPct = 100;
    template <class A>
    void Serialize(A& a)
    {
      a.Serialize("slot", slot).Serialize("item", item).Serialize(
        "healthPct", healthPct);
    }
  };
  uint32_t actorIdx = 0;
  uint32_t frameRefId = 0;
  uint8_t phase = 0;
  uint32_t frameBaseId = 0;
  std::vector<Piece> pieces;
  uint32_t coreBaseId = 0;
  std::optional<float> coreCharge; // owner only
  bool unpowered = false;
  bool jetpackCapable = false;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("actorIdx", actorIdx)
      .Serialize("frameRefId", frameRefId)
      .Serialize("phase", phase)
      .Serialize("frameBaseId", frameBaseId)
      .Serialize("pieces", pieces)
      .Serialize("coreBaseId", coreBaseId)
      .Serialize("coreCharge", coreCharge)
      .Serialize("unpowered", unpowered)
      .Serialize("jetpackCapable", jetpackCapable);
  }
};

// 89: progression snapshot to the owner
struct ProgressionUpdateMessage
  : public MessageBase<ProgressionUpdateMessage>
{
  FO4_MSG_TYPE(ProgressionUpdate)
  int32_t level = 1;
  uint64_t xp = 0;
  uint32_t xpForNextLevel = 0;
  int32_t perkPoints = 0;
  std::array<float, 7> special = { 1, 1, 1, 1, 1, 1, 1 };
  std::vector<uint32_t> perks;
  bool created = false;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("level", level)
      .Serialize("xp", xp)
      .Serialize("xpForNextLevel", xpForNextLevel)
      .Serialize("perkPoints", perkPoints)
      .Serialize("special", special)
      .Serialize("perks", perks)
      .Serialize("created", created);
  }
};

// 90: progression requests
struct ProgressionRequestMessage
  : public MessageBase<ProgressionRequestMessage>
{
  FO4_MSG_TYPE(ProgressionRequest)
  enum Op : uint8_t
  {
    kCreateCharacter = 0,
    kBuyPerk = 1,
    kBuySpecial = 2,
  };
  uint32_t nonce = 0;
  uint8_t op = kBuyPerk;
  std::array<int32_t, 7> special = { 1, 1, 1, 1, 1, 1, 1 };
  std::string chartKey;
  uint32_t specialAv = 0;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("nonce", nonce)
      .Serialize("op", op)
      .Serialize("special", special)
      .Serialize("chartKey", chartKey)
      .Serialize("specialAv", specialAv);
  }
};

// 91: consume an item (stimpak, chem, food)
struct UseItemMessage : public MessageBase<UseItemMessage>
{
  FO4_MSG_TYPE(UseItem)
  uint32_t nonce = 0;
  uint32_t baseId = 0;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType).Serialize("nonce", nonce).Serialize("baseId",
                                                                   baseId);
  }
};

// 93: workshop build mode enter/exit (C->S) and permission reply (S->C)
struct WorkshopModeMessage : public MessageBase<WorkshopModeMessage>
{
  FO4_MSG_TYPE(WorkshopMode)
  uint32_t workshopRefId = 0;
  bool enter = true;
  bool allowed = false;
  std::string reason;
  uint16_t perms = 0;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("workshopRefId", workshopRefId)
      .Serialize("enter", enter)
      .Serialize("allowed", allowed)
      .Serialize("reason", reason)
      .Serialize("perms", perms);
  }
};

// 94: place a workshop object
struct WorkshopPlaceMessage : public MessageBase<WorkshopPlaceMessage>
{
  FO4_MSG_TYPE(WorkshopPlace)
  uint32_t nonce = 0;
  uint32_t workshopRefId = 0;
  uint32_t recipeId = 0;
  uint32_t baseId = 0;
  bool fromStored = false;
  std::array<float, 3> pos = { 0, 0, 0 };
  std::array<float, 3> rot = { 0, 0, 0 };
  float scale = 1.f;
  uint32_t snapTargetRefId = 0;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("nonce", nonce)
      .Serialize("workshopRefId", workshopRefId)
      .Serialize("recipeId", recipeId)
      .Serialize("baseId", baseId)
      .Serialize("fromStored", fromStored)
      .Serialize("pos", pos)
      .Serialize("rot", rot)
      .Serialize("scale", scale)
      .Serialize("snapTargetRefId", snapTargetRefId);
  }
};

// 95: move/scrap/store/repair placed objects (≤ 64 per message)
struct WorkshopEditMessage : public MessageBase<WorkshopEditMessage>
{
  FO4_MSG_TYPE(WorkshopEdit)
  struct Item
  {
    uint32_t refId = 0;
    std::array<float, 3> pos = { 0, 0, 0 };
    std::array<float, 3> rot = { 0, 0, 0 };
    template <class A>
    void Serialize(A& a)
    {
      a.Serialize("refId", refId).Serialize("pos", pos).Serialize("rot", rot);
    }
  };
  uint32_t nonce = 0;
  uint32_t workshopRefId = 0;
  uint8_t op = 0; // fo4::WorkshopEditOp
  std::vector<Item> items;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("nonce", nonce)
      .Serialize("workshopRefId", workshopRefId)
      .Serialize("op", op)
      .Serialize("items", items);
  }
};

// 96: settlement state to the owner, ACL members and builders
struct WorkshopStateMessage : public MessageBase<WorkshopStateMessage>
{
  FO4_MSG_TYPE(WorkshopState)
  uint32_t workshopRefId = 0;
  uint32_t version = 0;
  uint8_t ownerType = 0;
  int32_t ownerId = -1;
  uint16_t yourPerms = 0;
  float budgetCurrent = 0.f, budgetMax = 0.f;
  uint32_t objects = 0, maxObjects = 0;
  float food = 0, water = 0, safety = 0, beds = 0, power = 0;
  float powerLoad = 0, population = 0, happiness = 0;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("workshopRefId", workshopRefId)
      .Serialize("version", version)
      .Serialize("ownerType", ownerType)
      .Serialize("ownerId", ownerId)
      .Serialize("yourPerms", yourPerms)
      .Serialize("budgetCurrent", budgetCurrent)
      .Serialize("budgetMax", budgetMax)
      .Serialize("objects", objects)
      .Serialize("maxObjects", maxObjects)
      .Serialize("food", food)
      .Serialize("water", water)
      .Serialize("safety", safety)
      .Serialize("beds", beds)
      .Serialize("power", power)
      .Serialize("powerLoad", powerLoad)
      .Serialize("population", population)
      .Serialize("happiness", happiness);
  }
};

// 97: wire connect (op 0) / disconnect (op 1); S->C carries added/removed
struct WorkshopWireMessage : public MessageBase<WorkshopWireMessage>
{
  FO4_MSG_TYPE(WorkshopWire)
  uint32_t nonce = 0;
  uint32_t workshopRefId = 0;
  uint8_t op = 0;
  uint32_t a = 0, b = 0;
  uint32_t splineBaseId = 0;
  uint32_t wireRefId = 0;

  template <class Ar>
  void Serialize(Ar& ar)
  {
    ar.Serialize("t", kMsgType)
      .Serialize("nonce", nonce)
      .Serialize("workshopRefId", workshopRefId)
      .Serialize("op", op)
      .Serialize("a", a)
      .Serialize("b", b)
      .Serialize("splineBaseId", splineBaseId)
      .Serialize("wireRefId", wireRefId);
  }
};

// 98: barter quote (op 0) / trade (op 1) and reply
struct BarterMessage : public MessageBase<BarterMessage>
{
  FO4_MSG_TYPE(Barter)
  uint32_t nonce = 0;
  uint8_t op = 0;
  uint32_t vendorId = 0;
  std::vector<fo4msg::ItemCount> buy;
  std::vector<fo4msg::ItemCount> sell;
  int64_t capsDelta = 0;
  std::string error;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("nonce", nonce)
      .Serialize("op", op)
      .Serialize("vendorId", vendorId)
      .Serialize("buy", buy)
      .Serialize("sell", sell)
      .Serialize("capsDelta", capsDelta)
      .Serialize("error", error);
  }
};

// 99: lockpicking (op 0 begin/activate, 1 attempt, 2 cancel; S->C outcome)
struct LockpickAttemptMessage : public MessageBase<LockpickAttemptMessage>
{
  FO4_MSG_TYPE(LockpickAttempt)
  uint8_t op = 0;
  uint32_t refId = 0;
  uint32_t sessionId = 0;
  uint8_t outcome = 0; // fo4::LockResultCode
  uint32_t xp = 0;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("op", op)
      .Serialize("refId", refId)
      .Serialize("sessionId", sessionId)
      .Serialize("outcome", outcome)
      .Serialize("xp", xp);
  }
};

// 100: terminal hacking (op 0 begin, 1 attempt; S->C outcome)
struct TerminalActionMessage : public MessageBase<TerminalActionMessage>
{
  FO4_MSG_TYPE(TerminalAction)
  uint8_t op = 0;
  uint32_t refId = 0;
  uint32_t sessionId = 0;
  uint8_t outcome = 0; // fo4::HackResultCode
  uint8_t attemptsLeft = 0;
  uint32_t xp = 0;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("op", op)
      .Serialize("refId", refId)
      .Serialize("sessionId", sessionId)
      .Serialize("outcome", outcome)
      .Serialize("attemptsLeft", attemptsLeft)
      .Serialize("xp", xp);
  }
};

// 107: generic result of a nonce'd request
struct RequestResultMessage : public MessageBase<RequestResultMessage>
{
  FO4_MSG_TYPE(RequestResult)
  uint32_t nonce = 0;
  uint8_t requestType = 0; // MsgType of the request
  bool ok = false;
  std::string error;
  uint32_t refId = 0;
  std::vector<fo4msg::ItemCount> items;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("nonce", nonce)
      .Serialize("requestType", requestType)
      .Serialize("ok", ok)
      .Serialize("error", error)
      .Serialize("refId", refId)
      .Serialize("items", items);
  }
};

// 108: settlement objects snapshot (chunked) or delta
struct WorkshopObjectsMessage : public MessageBase<WorkshopObjectsMessage>
{
  FO4_MSG_TYPE(WorkshopObjects)
  struct Object
  {
    uint32_t refId = 0;
    uint32_t baseId = 0;
    std::array<float, 3> pos = { 0, 0, 0 };
    std::array<float, 3> rot = { 0, 0, 0 };
    float scale = 1.f;
    uint16_t flags = 0;
    template <class A>
    void Serialize(A& a)
    {
      a.Serialize("refId", refId)
        .Serialize("baseId", baseId)
        .Serialize("pos", pos)
        .Serialize("rot", rot)
        .Serialize("scale", scale)
        .Serialize("flags", flags);
    }
  };
  struct WireEntry
  {
    uint32_t wireRefId = 0, a = 0, b = 0, splineBaseId = 0;
    template <class Ar>
    void Serialize(Ar& ar)
    {
      ar.Serialize("wireRefId", wireRefId)
        .Serialize("a", a)
        .Serialize("b", b)
        .Serialize("splineBaseId", splineBaseId);
    }
  };
  uint32_t workshopRefId = 0;
  uint32_t version = 0;
  uint8_t kind = 0; // 0 snapshot, 1 delta
  uint16_t chunk = 0, chunkCount = 1;
  std::vector<Object> added;
  std::vector<uint32_t> removed;
  std::vector<uint32_t> scrappedPrePlaced;
  std::vector<WireEntry> wires;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("workshopRefId", workshopRefId)
      .Serialize("version", version)
      .Serialize("kind", kind)
      .Serialize("chunk", chunk)
      .Serialize("chunkCount", chunkCount)
      .Serialize("added", added)
      .Serialize("removed", removed)
      .Serialize("scrappedPrePlaced", scrappedPrePlaced)
      .Serialize("wires", wires);
  }
};

// 109: claim/abandon/ACL/assign
struct WorkshopManageMessage : public MessageBase<WorkshopManageMessage>
{
  FO4_MSG_TYPE(WorkshopManage)
  enum Op : uint8_t
  {
    kClaim = 0,
    kAbandon = 1,
    kSetAcl = 2,
    kAssign = 3,
  };
  uint32_t nonce = 0;
  uint32_t workshopRefId = 0;
  uint8_t op = kClaim;
  uint32_t actorId = 0;
  uint32_t objectRefId = 0;
  int32_t profileId = -1;
  uint16_t perms = 0;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("nonce", nonce)
      .Serialize("workshopRefId", workshopRefId)
      .Serialize("op", op)
      .Serialize("actorId", actorId)
      .Serialize("objectRefId", objectRefId)
      .Serialize("profileId", profileId)
      .Serialize("perms", perms);
  }
};

// 120: party lifecycle and state
struct PartyActionMessage : public MessageBase<PartyActionMessage>
{
  FO4_MSG_TYPE(PartyAction)
  enum Op : uint8_t
  {
    kInvite = 0,
    kAccept = 1,
    kDecline = 2,
    kLeave = 3,
    kKick = 4,
    kPromote = 5,
    kSetPvpFlag = 6,
    kState = 100, // S->C
  };
  uint32_t nonce = 0;
  uint8_t op = kInvite;
  int32_t targetProfileId = -1;
  uint32_t partyId = 0;
  bool value = false;
  int32_t leader = -1;
  std::vector<int32_t> members;
  std::string error;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("nonce", nonce)
      .Serialize("op", op)
      .Serialize("targetProfileId", targetProfileId)
      .Serialize("partyId", partyId)
      .Serialize("value", value)
      .Serialize("leader", leader)
      .Serialize("members", members)
      .Serialize("error", error);
  }
};

// 67: equipment change request (C->S) and equipment state (S->C)
struct UpdateEquipmentFo4Message
  : public MessageBase<UpdateEquipmentFo4Message>
{
  FO4_MSG_TYPE(UpdateEquipmentFo4)
  enum Op : uint8_t
  {
    kEquip = 0,
    kUnequip = 1,
    kState = 2,
  };
  uint32_t actorIdx = 0;
  uint8_t op = kState;
  fo4msg::ItemKey item;
  std::optional<fo4msg::ItemKey> weapon;
  std::vector<fo4msg::ItemKey> armor;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("actorIdx", actorIdx)
      .Serialize("op", op)
      .Serialize("item", item)
      .Serialize("weapon", weapon)
      .Serialize("armor", armor);
  }
};

// 103: map markers known to the player (S->C)
struct MapDiscoveryMessage : public MessageBase<MapDiscoveryMessage>
{
  FO4_MSG_TYPE(MapDiscovery)
  struct Marker
  {
    uint32_t refId = 0;
    std::string name;
    uint8_t type = 0;
    std::array<float, 3> pos = { 0, 0, 0 };
    template <class A>
    void Serialize(A& a)
    {
      a.Serialize("refId", refId)
        .Serialize("name", name)
        .Serialize("type", type)
        .Serialize("pos", pos);
    }
  };
  bool full = false; // full list (on join) or newly discovered
  std::vector<Marker> markers;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType).Serialize("full", full).Serialize("markers",
                                                                 markers);
  }
};

// 104: fast travel request (reply: RequestResult, then a teleport)
struct FastTravelRequestMessage
  : public MessageBase<FastTravelRequestMessage>
{
  FO4_MSG_TYPE(FastTravelRequest)
  uint32_t nonce = 0;
  uint32_t markerRefId = 0;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType).Serialize("nonce", nonce).Serialize(
      "markerRefId", markerRefId);
  }
};

// 105: server clock and weather, sent every 10 s and on change
struct WorldTimeWeatherMessage : public MessageBase<WorldTimeWeatherMessage>
{
  FO4_MSG_TYPE(WorldTimeWeather)
  double gameDays = 0;
  float gameHour = 0;
  float timeScale = 20;
  uint32_t weatherId = 0;
  float transitionSec = 10;
  bool radstorm = false;
  int64_t serverNowMs = 0;

  template <class A>
  void Serialize(A& a)
  {
    a.Serialize("t", kMsgType)
      .Serialize("gameDays", gameDays)
      .Serialize("gameHour", gameHour)
      .Serialize("timeScale", timeScale)
      .Serialize("weatherId", weatherId)
      .Serialize("transitionSec", transitionSec)
      .Serialize("radstorm", radstorm)
      .Serialize("serverNowMs", serverNowMs);
  }
};

#undef FO4_MSG_TYPE

#define REGISTER_FO4_MESSAGES                                                 \
  REGISTER_MESSAGE(SetInventoryFo4Message)                                    \
  REGISTER_MESSAGE(PutItemFo4Message)                                         \
  REGISTER_MESSAGE(TakeItemFo4Message)                                        \
  REGISTER_MESSAGE(DropItemFo4Message)                                        \
  REGISTER_MESSAGE(ChangeValuesAvMessage)                                     \
  REGISTER_MESSAGE(CraftItemFo4Message)                                       \
  REGISTER_MESSAGE(WeaponFireMessage)                                         \
  REGISTER_MESSAGE(WeaponReloadMessage)                                       \
  REGISTER_MESSAGE(HitReportMessage)                                          \
  REGISTER_MESSAGE(DamageAppliedMessage)                                      \
  REGISTER_MESSAGE(ModItemMessage)                                            \
  REGISTER_MESSAGE(ScrapItemMessage)                                          \
  REGISTER_MESSAGE(PowerArmorTransitionMessage)                               \
  REGISTER_MESSAGE(PowerArmorStateMessage)                                    \
  REGISTER_MESSAGE(ProgressionUpdateMessage)                                  \
  REGISTER_MESSAGE(ProgressionRequestMessage)                                 \
  REGISTER_MESSAGE(UseItemMessage)                                            \
  REGISTER_MESSAGE(WorkshopModeMessage)                                       \
  REGISTER_MESSAGE(WorkshopPlaceMessage)                                      \
  REGISTER_MESSAGE(WorkshopEditMessage)                                       \
  REGISTER_MESSAGE(WorkshopStateMessage)                                      \
  REGISTER_MESSAGE(WorkshopWireMessage)                                       \
  REGISTER_MESSAGE(BarterMessage)                                             \
  REGISTER_MESSAGE(LockpickAttemptMessage)                                    \
  REGISTER_MESSAGE(TerminalActionMessage)                                     \
  REGISTER_MESSAGE(RequestResultMessage)                                      \
  REGISTER_MESSAGE(WorkshopObjectsMessage)                                    \
  REGISTER_MESSAGE(WorkshopManageMessage)                                     \
  REGISTER_MESSAGE(PartyActionMessage)                                        \
  REGISTER_MESSAGE(UpdateEquipmentFo4Message)                                 \
  REGISTER_MESSAGE(MapDiscoveryMessage)                                       \
  REGISTER_MESSAGE(FastTravelRequestMessage)                                  \
  REGISTER_MESSAGE(WorldTimeWeatherMessage)
