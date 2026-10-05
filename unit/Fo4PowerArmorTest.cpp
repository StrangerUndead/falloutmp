#include "Fo4TestData.h"
#include "fo4/PowerArmor.h"
#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using namespace fo4;
using namespace fo4test;

namespace {
constexpr FormId kFrameRef = 0xFF000100, kFrameBase = 0x2079E;
constexpr FormId kHelmet = 0x30A1, kLArm = 0x30A2, kRArm = 0x30A3,
                 kLLeg = 0x30A4, kRLeg = 0x30A5;
constexpr FormId kJetpackMod = 0x183575, kJetpackKw = 0x7777;
constexpr ActorId kPlayer = 0xFF000001, kOther = 0xFF000002;

struct PaWorld
{
  InMemoryFo4DataSource data;
  PowerArmorSettings settings;
  std::unique_ptr<PowerArmorService> pa;
  Fo4Inventory playerInv;
  int64_t now = 1000;

  PaWorld()
  {
    Build(data);
    auto piece = [&](FormId id, PowerArmorSlot slot) {
      ArmorData a;
      a.id = id;
      a.armorRating = 50;
      a.health = 200;
      a.resistances = { { 0x60A81, 30.f } };
      a.powerArmorSlot = slot;
      data.AddArmor(a);
    };
    piece(kHelmet, PowerArmorSlot::Helmet);
    piece(kLArm, PowerArmorSlot::LeftArm);
    piece(kRArm, PowerArmorSlot::RightArm);
    piece(kLLeg, PowerArmorSlot::LeftLeg);
    piece(kRLeg, PowerArmorSlot::RightLeg);
    ObjectModData jet;
    jet.id = kJetpackMod;
    jet.target = OmodTarget::Armor;
    OmodPropertyData kw;
    kw.valueType = OmodValueType::FormIdInt;
    kw.function = OmodFunction::Add;
    kw.property = ArmorProperty::Keywords;
    kw.formValue = kJetpackKw;
    jet.properties = { kw };
    data.AddObjectMod(jet);

    settings.jetpackKeywordId = kJetpackKw;
    pa = std::make_unique<PowerArmorService>(data, settings);

    PowerArmorFrame f;
    f.refId = kFrameRef;
    f.baseId = kFrameBase;
    f.contents.AddSimple(kHelmet, 1);
    f.contents.AddSimple(kT45Torso, 1);
    f.contents.AddSimple(kLArm, 1);
    f.contents.AddSimple(kRArm, 1);
    f.contents.AddSimple(kLLeg, 1);
    f.contents.AddSimple(kRLeg, 1);
    f.contents.AddSimple(kFusionCore, 1);
    pa->AddFrame(f);
  }

  PaActorFacts Facts(ActorId id = kPlayer, ProfileId profile = 1)
  {
    PaActorFacts a;
    a.actorId = id;
    a.profileId = profile;
    a.distanceToFrame = 100.f;
    return a;
  }

  void Enter(ActorId id = kPlayer, ProfileId profile = 1)
  {
    REQUIRE(pa->RequestEnter(Facts(id, profile), kFrameRef, 7, now) ==
            PaError::None);
    REQUIRE(pa->Ack(id, 7, playerInv, now + 500) == PaError::None);
  }
};
}

TEST_CASE("Entering power armor moves pieces and core onto the wearer",
          "[fo4][F17][PowerArmor]")
{
  PaWorld w;
  REQUIRE(w.pa->RequestEnter(w.Facts(), kFrameRef, 7, w.now) == PaError::None);
  REQUIRE(w.pa->GetPhase(kPlayer) == PaPhase::Entering);
  auto entering = w.pa->Snapshot(kPlayer);
  REQUIRE(entering.pieces.size() == 6); // still read from the frame
  REQUIRE(w.pa->FindFrame(kFrameRef)->enabled);

  REQUIRE(w.pa->Ack(kPlayer, 99, w.playerInv, w.now) == PaError::WrongNonce);
  REQUIRE(w.pa->Ack(kPlayer, 7, w.playerInv, w.now + 500) == PaError::None);
  REQUIRE(w.pa->GetPhase(kPlayer) == PaPhase::In);
  auto frame = w.pa->FindFrame(kFrameRef);
  REQUIRE_FALSE(frame->enabled);
  REQUIRE(frame->contents.IsEmpty());
  REQUIRE(frame->ownerProfileId == 1); // claimed on first entry
  auto s = w.pa->Snapshot(kPlayer);
  REQUIRE(s.pieces.size() == 6);
  REQUIRE(s.coreBaseId == kFusionCore);
  REQUIRE(s.coreCharge == 1.f);
  REQUIRE_FALSE(s.unpowered);
  // 6 intact pieces: 50*5 + 110 torso DR
  REQUIRE(w.pa->GetWornProtection(kPlayer).armorRating == 360.f);
}

TEST_CASE("Power armor enter is rejected for every invalid case",
          "[fo4][F17][PowerArmor]")
{
  PaWorld w;
  auto f = w.Facts();
  f.distanceToFrame = 1000.f;
  REQUIRE(w.pa->RequestEnter(f, kFrameRef, 1, w.now) == PaError::OutOfReach);
  f = w.Facts();
  f.alive = false;
  REQUIRE(w.pa->RequestEnter(f, kFrameRef, 1, w.now) == PaError::ActorDead);
  f = w.Facts();
  f.inFurniture = true;
  REQUIRE(w.pa->RequestEnter(f, kFrameRef, 1, w.now) == PaError::InFurniture);
  REQUIRE(w.pa->RequestEnter(w.Facts(), 0x1234, 1, w.now) ==
          PaError::NoSuchFrame);

  w.pa->veto = [](ActorId, FormId, bool) { return false; };
  REQUIRE(w.pa->RequestEnter(w.Facts(), kFrameRef, 1, w.now) ==
          PaError::Vetoed);
  w.pa->veto = nullptr;

  // Owned by another profile and stealing is off
  w.pa->FindFrame(kFrameRef)->ownerProfileId = 42;
  REQUIRE(w.pa->RequestEnter(w.Facts(), kFrameRef, 1, w.now) ==
          PaError::OwnedByOther);
  w.pa->settings.allowStealing = true;
  REQUIRE(w.pa->RequestEnter(w.Facts(), kFrameRef, 1, w.now) == PaError::None);
  REQUIRE(w.pa->FindFrame(kFrameRef)->ownerProfileId == 42); // not claimed

  // A second actor cannot take the same frame
  REQUIRE(w.pa->RequestEnter(w.Facts(kOther, 2), kFrameRef, 2, w.now) ==
          PaError::Occupied);
  // ... and the first cannot start a second transition
  REQUIRE(w.pa->RequestEnter(w.Facts(), kFrameRef, 3, w.now) ==
          PaError::InTransition);
}

TEST_CASE("Unacknowledged transitions roll back", "[fo4][F17][PowerArmor]")
{
  PaWorld w;
  REQUIRE(w.pa->RequestEnter(w.Facts(), kFrameRef, 7, w.now) == PaError::None);
  REQUIRE(w.pa->Tick(w.now + 5000).empty());
  auto rolled = w.pa->Tick(w.now + 10001);
  REQUIRE(rolled == std::vector<ActorId>{ kPlayer });
  REQUIRE(w.pa->GetPhase(kPlayer) == PaPhase::Out);
  REQUIRE_FALSE(w.pa->FindFrame(kFrameRef)->wornBy);
  REQUIRE(w.pa->FindFrame(kFrameRef)->contents.Entries().size() == 7);

  // Exit not acknowledged: the wearer stays in power armor
  w.Enter();
  float pos[3] = { 10, 20, 30 };
  REQUIRE(w.pa->RequestExit(w.Facts(), pos, 8, w.now) == PaError::None);
  w.pa->Tick(w.now + 20000);
  REQUIRE(w.pa->GetPhase(kPlayer) == PaPhase::In);
}

TEST_CASE("Exiting restores the frame at the exit position",
          "[fo4][F17][PowerArmor]")
{
  PaWorld w;
  w.Enter();
  auto airborne = w.Facts();
  airborne.airborne = true;
  float pos[3] = { 100, 200, 300 };
  REQUIRE(w.pa->RequestExit(airborne, pos, 8, w.now) == PaError::Airborne);
  REQUIRE(w.pa->RequestExit(w.Facts(), pos, 8, w.now) == PaError::None);
  REQUIRE(w.pa->Ack(kPlayer, 8, w.playerInv, w.now) == PaError::None);
  auto frame = w.pa->FindFrame(kFrameRef);
  REQUIRE(frame->enabled);
  REQUIRE_FALSE(frame->wornBy);
  REQUIRE(frame->pos[0] == 100.f);
  REQUIRE(frame->contents.Entries().size() == 7);
  REQUIRE(w.pa->GetPhase(kPlayer) == PaPhase::Out);
  REQUIRE(w.pa->RequestExit(w.Facts(), pos, 9, w.now) ==
          PaError::NotInPowerArmor);
}

TEST_CASE("Core drains by movement, swaps automatically, then goes "
          "unpowered",
          "[fo4][F17][PowerArmor]")
{
  PaWorld w;
  w.Enter();
  // Idle: no drain
  w.pa->Drain(kPlayer, 600.f, PaMovement::Idle, 1.f, w.playerInv);
  REQUIRE(w.pa->GetWorn(kPlayer)->CoreCharge() == 1.f);
  // 10 minutes of running = half a core
  w.pa->Drain(kPlayer, 600.f, PaMovement::Run, 1.f, w.playerInv);
  REQUIRE(w.pa->GetWorn(kPlayer)->CoreCharge() ==
          Catch::Approx(0.5f).margin(0.002));
  // Nuclear Physicist style duration x2 halves the drain
  w.pa->Drain(kPlayer, 120.f, PaMovement::Run, 2.f, w.playerInv);
  REQUIRE(w.pa->GetWorn(kPlayer)->CoreCharge() ==
          Catch::Approx(0.45f).margin(0.002));

  ItemKey spare{ kFusionCore };
  spare.condition = FractionToCondition(0.3f);
  w.playerInv.Add(spare, 1);
  auto ev =
    w.pa->Drain(kPlayer, 10000.f, PaMovement::Sprint, 1.f, w.playerInv);
  REQUIRE(ev.coreDepleted);
  REQUIRE(ev.coreSwapped);
  REQUIRE(w.playerInv.CountBase(kFusionCore) == 0);
  REQUIRE(w.pa->GetWorn(kPlayer)->CoreCharge() == Catch::Approx(0.3f));

  ev = w.pa->Drain(kPlayer, 10000.f, PaMovement::Sprint, 1.f, w.playerInv);
  REQUIRE(ev.becameUnpowered);
  REQUIRE(w.pa->Snapshot(kPlayer).unpowered);
  REQUIRE_FALSE(w.pa->IsJetpackAllowed(kPlayer, 100.f));

  // Picking up a new core powers the suit back up on the next sample
  w.playerInv.AddSimple(kFusionCore, 1);
  ev = w.pa->Drain(kPlayer, 1.f, PaMovement::Walk, 1.f, w.playerInv);
  REQUIRE(ev.coreSwapped);
  REQUIRE_FALSE(w.pa->Snapshot(kPlayer).unpowered);

  // A depleted core is never put back into the frame
  float pos[3] = { 0, 0, 0 };
  w.pa->GetWorn(kPlayer); // still in
}

TEST_CASE("NPC wearers do not drain cores and take triple piece damage",
          "[fo4][F17][PowerArmor]")
{
  PaWorld w;
  auto npc = w.Facts(0xFF0000AA, -1);
  npc.isNpc = true;
  REQUIRE(w.pa->RequestEnter(npc, kFrameRef, 1, w.now) == PaError::None);
  REQUIRE(w.pa->Ack(0xFF0000AA, 1, w.playerInv, w.now) == PaError::None);
  w.pa->Drain(0xFF0000AA, 10000.f, PaMovement::Sprint, 1.f, w.playerInv);
  REQUIRE(w.pa->GetWorn(0xFF0000AA)->CoreCharge() == 1.f);

  w.pa->DamagePiece(0xFF0000AA, PowerArmorSlot::Helmet, 50.f);
  auto s = w.pa->Snapshot(0xFF0000AA);
  REQUIRE(s.pieces[0].slot == PowerArmorSlot::Helmet);
  REQUIRE(s.pieces[0].healthPct == 25); // 200 - 150
}

TEST_CASE("Broken pieces lose protection and cannot be attached",
          "[fo4][F17][PowerArmor]")
{
  PaWorld w;
  w.Enter();
  auto ev = w.pa->DamagePiece(kPlayer, PowerArmorSlot::Helmet, 100.f);
  REQUIRE(ev.piecesBroken.empty());
  ev = w.pa->DamagePiece(kPlayer, PowerArmorSlot::Helmet, 150.f);
  REQUIRE(ev.piecesBroken ==
          std::vector<PowerArmorSlot>{ PowerArmorSlot::Helmet });
  REQUIRE(w.pa->GetWornProtection(kPlayer).armorRating == 310.f);
  // Damage to a missing slot is ignored
  REQUIRE(w.pa->DamagePiece(kPlayer, PowerArmorSlot::None, 10.f)
            .piecesBroken.empty());

  PowerArmorFrame empty;
  empty.refId = 0xFF000200;
  w.pa->AddFrame(empty);
  auto& f = *w.pa->FindFrame(0xFF000200);
  ItemKey broken{ kHelmet };
  broken.condition = kConditionZero;
  REQUIRE(w.pa->CanPutIntoFrame(f, broken) == PaError::PieceBroken);
  REQUIRE(w.pa->CanPutIntoFrame(f, ItemKey{ kHelmet }) == PaError::None);
  f.contents.AddSimple(kHelmet, 1);
  REQUIRE(w.pa->CanPutIntoFrame(f, ItemKey{ kHelmet }) == PaError::SlotTaken);
  REQUIRE(w.pa->CanPutIntoFrame(f, ItemKey{ kWrench }) == PaError::NotAPiece);
  REQUIRE(w.pa->CanPutIntoFrame(f, ItemKey{ kFusionCore }) == PaError::None);
  f.contents.AddSimple(kFusionCore, 1);
  REQUIRE(w.pa->CanPutIntoFrame(f, ItemKey{ kFusionCore }) ==
          PaError::CoreAlreadyPresent);
  REQUIRE(w.pa->CanPutIntoFrame(*w.pa->FindFrame(kFrameRef),
                                ItemKey{ kHelmet }) == PaError::FrameInUse);
}

TEST_CASE("Jetpack needs a jetpack torso, power and AP",
          "[fo4][F17][PowerArmor]")
{
  PaWorld w;
  w.Enter();
  REQUIRE_FALSE(w.pa->IsJetpackAllowed(kPlayer, 50.f));
  float pos[3] = { 0, 0, 0 };
  w.pa->ForceExit(kPlayer, pos, w.playerInv);
  auto frame = w.pa->FindFrame(kFrameRef);
  REQUIRE(frame->contents.Remove(ItemKey{ kT45Torso }, 1));
  frame->contents.Add(ItemKey{ kT45Torso }.WithMods({ kJetpackMod }), 1);
  w.Enter();
  REQUIRE(w.pa->IsJetpackAllowed(kPlayer, 50.f));
  REQUIRE_FALSE(w.pa->IsJetpackAllowed(kPlayer, 0.f));
  REQUIRE(w.pa->Snapshot(kPlayer).jetpackCapable);
}

TEST_CASE("Forced exit and persistence round trip", "[fo4][F17][PowerArmor]")
{
  PaWorld w;
  w.Enter();
  w.pa->DamagePiece(kPlayer, PowerArmorSlot::Torso, 225.f);
  auto wornJson = w.pa->WornToJson(*w.pa->GetWorn(kPlayer));
  auto frameJson = w.pa->FrameToJson(*w.pa->FindFrame(kFrameRef));

  // "Server restart": a fresh service restores both records
  PowerArmorService restored(w.data, w.settings);
  restored.AddFrame(PowerArmorService::FrameFromJson(frameJson));
  restored.RestoreWorn(kPlayer, PowerArmorService::WornFromJson(wornJson));
  REQUIRE(restored.GetPhase(kPlayer) == PaPhase::In);
  REQUIRE(restored.Snapshot(kPlayer).pieces.size() == 6);
  REQUIRE(restored.Snapshot(kPlayer).pieces[1].healthPct == 50);
  REQUIRE_FALSE(restored.FindFrame(kFrameRef)->enabled);

  // Disconnect: forced exit, frame keeps pieces and owner
  float pos[3] = { 5, 6, 7 };
  Fo4Inventory inv;
  restored.ForceExit(kPlayer, pos, inv);
  auto f = restored.FindFrame(kFrameRef);
  REQUIRE(f->enabled);
  REQUIRE(f->pos[2] == 7.f);
  REQUIRE(f->ownerProfileId == 1);
  REQUIRE(f->contents.Entries().size() == 7);
  REQUIRE(restored.GetPhase(kPlayer) == PaPhase::Out);
}

TEST_CASE("Condition encoding", "[fo4][F17]")
{
  REQUIRE(ConditionToFraction(kConditionFull) == 1.f);
  REQUIRE(ConditionToFraction(kConditionZero) == 0.f);
  REQUIRE(FractionToCondition(1.f) == kConditionFull);
  REQUIRE(FractionToCondition(0.f) == kConditionZero);
  REQUIRE(FractionToCondition(-3.f) == kConditionZero);
  REQUIRE(FractionToCondition(0.0001f) == 1);
  REQUIRE(FractionToCondition(0.5f) == 500);
}
