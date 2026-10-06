#pragma once
// One client session: the JavaScript client, its connection and the game.
//
// Every frame the platform calls Tick(): network packets become
// "connected"/"connectionFailed"/"connectionDenied"/"disconnected"/"message"
// events for the script, followed by "tick". The script answers with
// __fmp.send/connect/native calls (JsHost.h).
#include "Codec.h"
#include "Game.h"
#include "JsHost.h"
#include "NetClient.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fmp {

class Runtime
{
public:
  struct Options
  {
    // SLikeNet password prefix of the game ("fo4-1_", GameId.h) and the
    // server password appended to it.
    std::string protocolPrefix = "fo4-1_";
    std::string serverPassword;
    int timeoutMs = 60000;
  };

  Runtime(Game& game, Options options);
  ~Runtime();

  bool LoadScript(const std::string& source, const std::string& fileName);

  // nowMs: a monotonic clock, passed to the script as event payload.
  void Tick(double nowMs);

  // Lifecycle events from the platform (gameDataLoaded, menus, ...).
  void Emit(const std::string& kind, const nlohmann::json& payload);

  bool IsConnected() const;
  JsHost& Js() { return *js; }

private:
  void Connect(const std::string& host, int port);

  Game& game;
  Options options;
  Codec codec;
  std::unique_ptr<NetClient> net;
  std::unique_ptr<JsHost> js;
  bool pendingDisconnect = false;
};

}
