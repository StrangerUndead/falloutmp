#include "Runtime.h"

#include <utility>

namespace fmp {

Runtime::Runtime(Game& game_, Options options_)
  : game(game_)
  , options(std::move(options_))
{
  JsHost::Callbacks cb;
  cb.native = [this](const std::string& name, const std::string& argsJson) {
    auto args = nlohmann::json::parse(argsJson);
    nlohmann::json result;
    if (!game.CallNative(name, args, result)) {
      return std::string(R"({"missing":true})");
    }
    return nlohmann::json{ { "r", std::move(result) } }.dump();
  };
  cb.send = [this](const std::string& json, bool reliable) {
    if (!net || !net->IsConnected()) {
      return;
    }
    try {
      auto bytes = codec.Encode(json);
      net->Send(bytes.data(), bytes.size(), reliable);
    } catch (std::exception& e) {
      game.Log("error", std::string("Unable to send message: ") + e.what() +
                 " " + json.substr(0, 200));
    }
  };
  cb.connect = [this](const std::string& host, int port) {
    Connect(host, port);
  };
  cb.disconnect = [this] { pendingDisconnect = true; };
  cb.log = [this](const std::string& level, const std::string& text) {
    game.Log(level, text);
  };
  js = std::make_unique<JsHost>(std::move(cb));
}

Runtime::~Runtime()
{
  js.reset(); // the script may still reference the connection
  net.reset();
}

bool Runtime::LoadScript(const std::string& source,
                         const std::string& fileName)
{
  return js->Eval(source, fileName);
}

void Runtime::Connect(const std::string& host, int port)
{
  if (port <= 0 || port > 65535) {
    throw std::runtime_error("bad port " + std::to_string(port));
  }
  net.reset();
  pendingDisconnect = false;
  net = std::make_unique<NetClient>(host, static_cast<uint16_t>(port),
                                    options.protocolPrefix +
                                      options.serverPassword,
                                    options.timeoutMs);
  game.Log("info", "Connecting to " + host + ":" + std::to_string(port));
}

void Runtime::Tick(double nowMs)
{
  if (net) {
    struct Event
    {
      NetEvent type;
      std::string payload;
    };
    std::vector<Event> events;
    net->Tick([&](NetEvent type, const uint8_t* data, size_t length,
                  const char* error) {
      if (type == NetEvent::Message) {
        try {
          events.push_back({ type, codec.Decode(data, length) });
        } catch (std::exception& e) {
          game.Log("error",
                   std::string("Unable to decode a message: ") + e.what());
        }
      } else {
        events.push_back({ type, nlohmann::json{ { "error", error } }.dump() });
      }
    });
    for (auto& e : events) {
      switch (e.type) {
        case NetEvent::Connected:
          js->Emit("connected", e.payload);
          break;
        case NetEvent::ConnectionFailed:
          js->Emit("connectionFailed", e.payload);
          break;
        case NetEvent::ConnectionDenied:
          js->Emit("connectionDenied", e.payload);
          break;
        case NetEvent::Disconnected:
          js->Emit("disconnected", e.payload);
          break;
        case NetEvent::Message:
          js->Emit("message", e.payload);
          break;
      }
      if (pendingDisconnect) {
        break; // the script dropped the connection: ignore the rest
      }
    }
    if (pendingDisconnect) {
      net.reset();
      pendingDisconnect = false;
    }
  }
  js->Emit("tick", nlohmann::json{ { "nowMs", nowMs } }.dump());
}

void Runtime::Emit(const std::string& kind, const nlohmann::json& payload)
{
  js->Emit(kind, payload.dump());
}

bool Runtime::IsConnected() const
{
  return net && net->IsConnected();
}

}
