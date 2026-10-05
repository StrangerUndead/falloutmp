#pragma once
// Client side of the SkyMP transport (SLikeNet), as in
// falloutmp-server/cpp/mp_common/Networking.cpp, without the server-only
// dependencies (prometheus, IdManager) so the F4SE plugin stays small.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace fmp {

enum class NetEvent
{
  Connected,
  ConnectionFailed, // no answer
  ConnectionDenied, // answered with a reason (wrong version, full, banned)
  Disconnected,
  Message
};

class NetClient
{
public:
  // password is the SLikeNet connection password: the game's protocol
  // prefix plus the server password (empty by default).
  NetClient(const std::string& host, uint16_t port,
            const std::string& password, int timeoutMs = 60000);
  ~NetClient();
  NetClient(const NetClient&) = delete;
  NetClient& operator=(const NetClient&) = delete;

  // data[0] is the packet id (>= MinPacketId) followed by the payload.
  void Send(const uint8_t* data, size_t length, bool reliable);

  using OnEvent = std::function<void(NetEvent, const uint8_t* data,
                                     size_t length, const char* error)>;
  void Tick(const OnEvent& onEvent);

  bool IsConnected() const;

private:
  struct Impl;
  std::unique_ptr<Impl> pImpl;
};

}
