// Session natives: server config, notifications, the (future) web front,
// and menu open/close events for every module.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "Modules.h"
#include "Platform.h"

namespace fmp::modules {

namespace {
class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::MenuOpenCloseEvent& e,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
  {
    Platform::Get().Emit(
      "menuOpenClose",
      Json{ { "menu", e.menuName.c_str() }, { "open", e.opening } });
    return RE::BSEventNotifyControl::kContinue;
  }
};
}

void InstallSession(Platform& p)
{
  p.RegisterNative("getClientConfig", [&p](const Json&) -> Json {
    auto& c = p.Config();
    if (c.serverIp.empty()) {
      return nullptr;
    }
    return { { "serverIp", c.serverIp },
             { "serverPort", c.serverPort },
             { "profileId", c.profileId } };
  });
  p.RegisterNative("showNotification", [](const Json& a) -> Json {
    auto text = a.at(0).get<std::string>();
    RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, false, false);
    return nullptr;
  });
  // No browser overlay yet (ADR-005): front events go to the log
  p.RegisterNative("sendToFront", [](const Json& a) -> Json {
    REX::DEBUG("front event {} {}", a.at(0).get<std::string>(),
               a.size() > 1 ? a[1].dump() : "");
    return nullptr;
  });

  static MenuSink menuSink;
  if (auto ui = RE::UI::GetSingleton()) {
    ui->RegisterSink<RE::MenuOpenCloseEvent>(&menuSink);
  }
}

}
