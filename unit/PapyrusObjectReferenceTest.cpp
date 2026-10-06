#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>

#include "ActionListener.h"
#include "MpObjectReference.h"
#include "papyrus-vm/Structures.h"
#include "script_classes/PapyrusObjectReference.h"
#include "script_objects/EspmGameObject.h"

namespace {

class TestReference : public MpObjectReference
{
public:
  TestReference(const LocationalData& locationalData,
                const FormCallbacks& callbacks, uint32_t baseId,
                std::string baseType,
                std::optional<NiPoint3> primitiveBoundsDiv2 = std::nullopt)
    : MpObjectReference(locationalData, callbacks, baseId, baseType,
                        primitiveBoundsDiv2)
  {
  }

  void SendPapyrusEvent(const char* eventName,
                        const VarValue* arguments = nullptr,
                        size_t argumentsCount = 0) override
  {
    events.push_back(eventName);
    return MpObjectReference::SendPapyrusEvent(eventName, arguments,
                                               argumentsCount);
  }

  std::vector<std::string> events;
};

TestReference& CreateMpObjectReference(WorldState& worldState, uint32_t id)
{
  auto refr = std::make_unique<TestReference>(
    LocationalData(), FormCallbacks::DoNothing(), 0, "CONT");
  worldState.AddForm(std::move(refr), id);
  return worldState.GetFormAt<TestReference>(id);
}
TestReference& CreateMpObjectReference(PartOne& partOne, uint32_t id)
{
  return CreateMpObjectReference(partOne.worldState, id);
}
}

TEST_CASE("MoveTo", "[Papyrus][ObjectReference]")
{
  PapyrusObjectReference papyrusObjectReference;
  PartOne partOne;
  DoConnect(partOne, 0);
  uint32_t formId =
    partOne.CreateActor(0xff000000, { 666, 666, 666 }, 0, 0x3c);
  partOne.SetUserActor(0, 0xff000000);
  auto& messages = partOne.Messages();
  auto& actor = partOne.worldState.GetFormAt<MpActor>(formId);
  auto& refr = CreateMpObjectReference(partOne, 0xff000001);
  REQUIRE(actor.GetPos() != refr.GetPos());
  papyrusObjectReference.MoveTo(refr.ToVarValue(),
                                { actor.ToVarValue(), VarValue(0), VarValue(0),
                                  VarValue(0), VarValue(true) });
  REQUIRE(refr.GetPos() == actor.GetPos());
  REQUIRE(refr.GetCellOrWorld() == actor.GetCellOrWorld());
  refr.SetPos({ 0, 0, 0 });
  papyrusObjectReference.MoveTo(actor.ToVarValue(),
                                { refr.ToVarValue(), VarValue(0), VarValue(0),
                                  VarValue(0), VarValue(true) });
  REQUIRE(refr.GetPos() == actor.GetPos());
  REQUIRE(refr.GetCellOrWorld() == actor.GetCellOrWorld());
  {
    auto it = std::find_if(
      messages.begin(), messages.end(),
      [](PartOne::Message& msg) { return msg.j["t"] == MsgType::Teleport; });
    REQUIRE(it != messages.end());
  }
}
