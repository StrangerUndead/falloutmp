#include "TestUtils.hpp"
#include <catch2/catch_all.hpp>

#include "ConsoleCommandMessage.h"
#include "PacketParser.h"

using Catch::Matchers::ContainsSubstring;

TEST_CASE("ConsoleCommand packet is parsed", "[ConsoleCommand]")
{
  class MyActionListener : public ActionListener
  {
  public:
    explicit MyActionListener(PartOne& partOne)
      : ActionListener(partOne)
    {
    }

    void OnConsoleCommand(const RawMessageData& rawMsgData_,
                          const ConsoleCommandMessage& msg_) override
    {
      rawMsgData = rawMsgData_;
      commandName = msg_.data.commandName;
      args = msg_.data.args;
    }

    RawMessageData rawMsgData;
    std::string commandName;
    std::vector<std::variant<int64_t, std::string>> args;
  };

  nlohmann::json j{ { "t", MsgType::ConsoleCommand },
                    { "data",
                      { { "commandName", "additem" },
                        { "args", { 0x14, 0x12eb7, 0x1 } } } } };

  auto msg = MakeMessage(j);

  PartOne partOne;
  MyActionListener listener(partOne);

  PacketParser p;
  p.TransformPacketIntoAction(
    122, reinterpret_cast<Networking::PacketData>(msg.data()), msg.size(),
    listener);

  REQUIRE(listener.args ==
          std::vector<std::variant<int64_t, std::string>>{
            int64_t(0x14), int64_t(0x12eb7), int64_t(0x1) });
  REQUIRE(listener.commandName == "additem");
  REQUIRE(listener.rawMsgData.userId == 122);
}
