#include "MessageSerializerFactory.h"
#include "Messages.h"
#include "MinPacketId.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>
#include <slikenet/BitStream.h>

namespace {
std::shared_ptr<MessageSerializer> Serializer()
{
  static auto s = MessageSerializerFactory::CreateMessageSerializer();
  return s;
}

template <class M>
M RoundTripBinary(const M& msg, size_t* outSize = nullptr)
{
  SLNet::BitStream stream;
  Serializer()->Serialize(msg, stream);
  if (outSize) {
    *outSize = stream.GetNumberOfBytesUsed();
  }
  auto res = Serializer()->Deserialize(
    reinterpret_cast<const uint8_t*>(stream.GetData()),
    stream.GetNumberOfBytesUsed());
  REQUIRE(res);
  REQUIRE(res->format == DeserializeInputFormat::Binary);
  REQUIRE(static_cast<int>(res->msgType) == M::kMsgType.value);
  return *reinterpret_cast<M*>(res->message.get());
}

template <class M>
M RoundTripJson(const M& msg)
{
  nlohmann::json j;
  msg.WriteJson(j);
  std::string s = j.dump();
  std::vector<uint8_t> packet;
  packet.push_back(Networking::MinPacketId);
  packet.insert(packet.end(), s.begin(), s.end());
  auto res = Serializer()->Deserialize(packet.data(), packet.size());
  REQUIRE(res);
  REQUIRE(res->format == DeserializeInputFormat::Json);
  return *reinterpret_cast<M*>(res->message.get());
}

fo4msg::ItemKey Gun()
{
  fo4msg::ItemKey k;
  k.baseId = 0x4822;
  k.mods = { 0x7002, 0x7003 };
  k.ammoLoaded = 9;
  return k;
}
}

TEST_CASE("Fallout 4 messages round trip in binary and JSON",
          "[fo4][NET][Fo4Messages]")
{
  {
    SetInventoryFo4Message m;
    m.refId = 7;
    m.version = 3;
    m.entries = { { Gun(), 1 }, { { 0xF }, 250 } };
    for (auto r : { RoundTripBinary(m), RoundTripJson(m) }) {
      REQUIRE(r.refId == 7);
      REQUIRE(r.entries.size() == 2);
      REQUIRE(r.entries[0].item.mods ==
              std::vector<uint32_t>{ 0x7002, 0x7003 });
      REQUIRE(r.entries[0].item.ammoLoaded == 9);
      REQUIRE(r.entries[1].count == 250);
    }
  }
  {
    ChangeValuesAvMessage m;
    m.idx = 1;
    m.values = { { 0x2D4, 75.5f, 140.f }, { 0x2E1, 30.f, 1000.f } };
    auto r = RoundTripBinary(m);
    REQUIRE(r.values.size() == 2);
    REQUIRE(r.values[0].current == 75.5f);
    REQUIRE(RoundTripJson(m).values[1].avId == 0x2E1);
  }
  {
    ModItemMessage m;
    m.nonce = 5;
    m.workbenchRefId = 0x100;
    m.item = Gun();
    m.modId = 0x7005;
    m.op = 1;
    auto r = RoundTripBinary(m);
    REQUIRE(r.item.baseId == 0x4822);
    REQUIRE(r.op == 1);
    REQUIRE(RoundTripJson(m).modId == 0x7005);
  }
  {
    PowerArmorStateMessage m;
    m.actorIdx = 2;
    m.phase = 2;
    m.pieces = { { 1, { 0x30A1 }, 80 }, { 2, { 0x30A0 }, 100 } };
    m.coreCharge = 0.42f;
    m.jetpackCapable = true;
    size_t bytes = 0;
    auto r = RoundTripBinary(m, &bytes);
    REQUIRE(r.pieces.size() == 2);
    REQUIRE(r.pieces[0].healthPct == 80);
    REQUIRE(r.coreCharge == 0.42f);
    REQUIRE(r.jetpackCapable);
    REQUIRE(bytes <= 200); // F17 budget
    m.coreCharge.reset();  // neighbours do not get the exact charge
    REQUIRE_FALSE(RoundTripJson(m).coreCharge);
  }
  {
    PowerArmorTransitionMessage m;
    m.kind = PowerArmorTransitionMessage::kExit;
    m.error = "Airborne";
    m.ok = false;
    m.exitPos = { 1, 2, 3 };
    auto r = RoundTripJson(m);
    REQUIRE(r.error == "Airborne");
    REQUIRE(r.exitPos[2] == 3.f);
    REQUIRE(RoundTripBinary(m).kind == PowerArmorTransitionMessage::kExit);
  }
  {
    WorkshopPlaceMessage m;
    m.nonce = 9;
    m.workshopRefId = 0xFF0010AA;
    m.recipeId = 0x2001;
    m.baseId = 0x1001;
    m.pos = { 10, 20, 30 };
    m.rot = { 0, 0, 1.5f };
    size_t bytes = 0;
    auto r = RoundTripBinary(m, &bytes);
    REQUIRE(r.pos[1] == 20.f);
    REQUIRE(r.rot[2] == 1.5f);
    REQUIRE(bytes < 64);
  }
  {
    WorkshopObjectsMessage m;
    m.workshopRefId = 1;
    m.version = 44;
    m.chunk = 2;
    m.chunkCount = 5;
    for (uint32_t i = 0; i < 96; ++i) {
      m.added.push_back(
        { 0xFF100000 + i, 0x1001, { 1, 2, 3 }, { 0, 0, 0 }, 1.f, 0 });
    }
    m.removed = { 5, 6 };
    m.wires = { { 0xAA, 1, 2, 0 } };
    size_t bytes = 0;
    auto r = RoundTripBinary(m, &bytes);
    REQUIRE(r.added.size() == 96);
    REQUIRE(r.added[95].refId == 0xFF100000 + 95);
    REQUIRE(r.wires[0].wireRefId == 0xAA);
    // ~38 B per object on the wire (F22 budget ~34 B + scale)
    REQUIRE(bytes / 96 <= 40);
  }
  {
    BarterMessage m;
    m.op = 1;
    m.vendorId = 0x1000;
    m.buy = { { { 0x23736 }, 2 } };
    m.capsDelta = -276;
    auto r = RoundTripBinary(m);
    REQUIRE(r.capsDelta == -276);
    REQUIRE(RoundTripJson(m).buy[0].count == 2);
  }
  {
    RequestResultMessage m;
    m.nonce = 77;
    m.requestType = static_cast<uint8_t>(MsgType::WorkshopPlace);
    m.ok = false;
    m.error = "OverBudget";
    auto r = RoundTripJson(m);
    REQUIRE(r.error == "OverBudget");
    REQUIRE(RoundTripBinary(m).nonce == 77);
  }
  {
    PartyActionMessage m;
    m.op = PartyActionMessage::kState;
    m.partyId = 3;
    m.leader = 1;
    m.members = { 1, 2, 5 };
    auto r = RoundTripBinary(m);
    REQUIRE(r.members == std::vector<int32_t>{ 1, 2, 5 });
  }
  {
    DamageAppliedMessage m;
    m.targetIdx = 4;
    m.total = 33.f;
    m.amounts = { 20.f, 13.f };
    m.killed = true;
    size_t bytes = 0;
    auto r = RoundTripBinary(m, &bytes);
    REQUIRE(r.killed);
    REQUIRE(bytes <= 96); // M10 budget
  }
  {
    ProgressionRequestMessage m;
    m.op = ProgressionRequestMessage::kCreateCharacter;
    m.special = { 4, 4, 4, 4, 4, 4, 4 };
    REQUIRE(RoundTripJson(m).special[6] == 4);
    ProgressionUpdateMessage u;
    u.level = 12;
    u.xp = 9000;
    u.perks = { 0x4D9B1 };
    REQUIRE(RoundTripBinary(u).xp == 9000);
  }
  {
    LockpickAttemptMessage l;
    l.op = 1;
    l.sessionId = 3;
    l.outcome = 12;
    REQUIRE(RoundTripBinary(l).outcome == 12);
    TerminalActionMessage t;
    t.attemptsLeft = 2;
    REQUIRE(RoundTripJson(t).attemptsLeft == 2);
  }
  {
    WeaponFireMessage f;
    f.shooterIdx = 1;
    f.origin = { 1, 2, 3 };
    f.direction = { 0, 1, 0 };
    size_t bytes = 0;
    REQUIRE(RoundTripBinary(f, &bytes).direction[1] == 1.f);
    REQUIRE(bytes <= 64); // F09 budget per shot
    HitReportMessage h;
    h.shotSeq = 5;
    h.targetIdx = 9;
    REQUIRE(RoundTripBinary(h).targetIdx == 9);
  }
  {
    EffectsUpdateMessage e;
    e.idx = 7;
    e.effects.push_back({ 0xE1, 0x23736, 0, 0x2D4, 10.f, 4000 });
    e.addictions = { 0x1234 };
    auto b = RoundTripBinary(e);
    REQUIRE(b.effects.size() == 1);
    REQUIRE(b.effects[0].remainingMs == 4000);
    REQUIRE(RoundTripJson(e).addictions[0] == 0x1234);
    UpdateMovementFo4Message mv;
    mv.seq = 65535;
    mv.flags = 0x1040;
    mv.pos = { 1, 2, 3 };
    size_t bytes = 0;
    auto mb = RoundTripBinary(mv, &bytes);
    REQUIRE(mb.seq == 65535);
    REQUIRE(mb.flags == 0x1040);
    REQUIRE(bytes <= 64); // F01 budget per sample
  }
}

TEST_CASE("Skyrim messages are unchanged by the Fallout 4 registry",
          "[fo4][NET][Fo4Messages]")
{
  ActivateMessage a;
  a.data.caster = 0x14;
  a.data.target = 0x1234;
  SLNet::BitStream s;
  Serializer()->Serialize(a, s);
  auto data = reinterpret_cast<const uint8_t*>(s.GetData());
  // Golden upstream layout: [MinPacketId][t=6][data: t=6][caster u64]
  // [target u64][bool]. ActivateMessage::Data writes "t" again.
  REQUIRE(data[0] == Networking::MinPacketId);
  REQUIRE(data[1] == 6);
  REQUIRE(data[2] == 6);
  REQUIRE(s.GetNumberOfBytesUsed() == 3 + 8 + 8 + 1);
}

TEST_CASE("Fo4 messages: appearance and animation round trips",
          "[fo4][Fo4Messages][F02][F03]")
{
  UpdateAppearanceFo4Message a;
  a.idx = 0xff000001;
  a.rev = 3;
  a.data.isFemale = true;
  a.data.raceId = 0x13746;
  a.data.headPartIds = { 1, 2, 3 };
  a.data.bodyMorph = { 0.2f, 0.5f, 0.3f };
  a.data.morphRegions = { 0.1f, -0.2f };
  a.data.morphSliders = { { 0xABCD, 0.75f } };
  a.data.faceRegions = { { 4, { 0.1f, 0, 0 }, { 0, 0.2f, 0 }, 1.1f } };
  a.data.tints = { { 12, 2, 80, 0x112233FF, -1 } };
  a.data.skinTone = 0xAABBCCDD;
  size_t size = 0;
  for (auto r : { RoundTripBinary(a, &size), RoundTripJson(a) }) {
    REQUIRE(r.rev == 3);
    REQUIRE(r.data.isFemale);
    REQUIRE(r.data.headPartIds == std::vector<uint32_t>{ 1, 2, 3 });
    REQUIRE(r.data.morphSliders[0].value == 0.75f);
    REQUIRE(r.data.faceRegions[0].scale == 1.1f);
    REQUIRE(r.data.tints[0].templateColorIndex == -1);
    REQUIRE(r.data.skinTone == 0xAABBCCDD);
  }
  REQUIRE(size < 2048); // F03 budget

  UpdateActionsMessage ac;
  ac.idx = 0;
  ac.seq = 9;
  ac.events = { "JumpUp", "reloadStart" };
  REQUIRE(RoundTripBinary(ac).events[1] == "reloadStart");
  REQUIRE(RoundTripJson(ac).seq == 9);

  UpdateGraphVariablesMessage gv;
  gv.seq = 65535;
  gv.values = { { "Speed", 0, 312.5f }, { "bIsSneaking", 2, 1.f } };
  auto g = RoundTripBinary(gv);
  REQUIRE(g.seq == 65535);
  REQUIRE(g.values[1].type == 2);
  REQUIRE(RoundTripJson(gv).values[0].value == 312.5f);
}
