#include "NetClient.h"

#include <slikenet/MessageIdentifiers.h>
#include <slikenet/peer.h>
#include <stdexcept>

namespace fmp {

namespace {
constexpr unsigned char kMinPacketId = 134; // messages/MinPacketId.h

const char* DenyReason(unsigned char id)
{
  switch (id) {
    case ID_ALREADY_CONNECTED:
      return "Already connected";
    case ID_CONNECTION_BANNED:
      return "Banned";
    case ID_INVALID_PASSWORD:
      return "Invalid password (client and server versions or games differ)";
    case ID_INCOMPATIBLE_PROTOCOL_VERSION:
      return "Incompatible protocol version";
    case ID_IP_RECENTLY_CONNECTED:
      return "IP recently connected";
    case ID_NO_FREE_INCOMING_CONNECTIONS:
      return "Server is full";
    default:
      return nullptr;
  }
}
}

struct NetClient::Impl
{
  SLNet::RakPeer peer;
  SLNet::SocketDescriptor socket{ 0, nullptr };
  SLNet::RakNetGUID serverGuid = SLNet::UNASSIGNED_RAKNET_GUID;
  bool connected = false;
};

NetClient::NetClient(const std::string& host, uint16_t port,
                     const std::string& password, int timeoutMs)
  : pImpl(std::make_unique<Impl>())
{
  auto res = pImpl->peer.Startup(1, &pImpl->socket, 1);
  if (res != SLNet::RAKNET_STARTED) {
    throw std::runtime_error("network startup failed with code " +
                             std::to_string(static_cast<int>(res)));
  }
  auto con = pImpl->peer.Connect(host.data(), port, password.data(),
                                 static_cast<int>(password.size()));
  if (con != SLNet::CONNECTION_ATTEMPT_STARTED) {
    pImpl->peer.Shutdown(0);
    throw std::runtime_error("connect to " + host + ":" +
                             std::to_string(port) + " failed with code " +
                             std::to_string(static_cast<int>(con)));
  }
  pImpl->peer.SetTimeoutTime(timeoutMs, SLNet::UNASSIGNED_SYSTEM_ADDRESS);
}

NetClient::~NetClient()
{
  pImpl->peer.Shutdown(pImpl->connected ? 100 : 0);
}

void NetClient::Send(const uint8_t* data, size_t length, bool reliable)
{
  if (!pImpl->connected || length == 0) {
    return;
  }
  pImpl->peer.Send(reinterpret_cast<const char*>(data),
                   static_cast<int>(length), MEDIUM_PRIORITY,
                   reliable ? RELIABLE_ORDERED : UNRELIABLE, 0,
                   pImpl->serverGuid, false);
}

void NetClient::Tick(const OnEvent& onEvent)
{
  while (SLNet::Packet* packet = pImpl->peer.Receive()) {
    struct Guard
    {
      SLNet::RakPeer& peer;
      SLNet::Packet* p;
      ~Guard() { peer.DeallocatePacket(p); }
    } guard{ pImpl->peer, packet };

    if (packet->length == 0) {
      continue;
    }
    unsigned char id = packet->data[0];
    if (id >= kMinPacketId) {
      onEvent(NetEvent::Message, packet->data, packet->length, "");
    } else if (id == ID_CONNECTION_REQUEST_ACCEPTED) {
      pImpl->serverGuid = packet->guid;
      pImpl->connected = true;
      onEvent(NetEvent::Connected, nullptr, 0, "");
    } else if (id == ID_CONNECTION_LOST ||
               id == ID_DISCONNECTION_NOTIFICATION) {
      pImpl->connected = false;
      onEvent(NetEvent::Disconnected, nullptr, 0, "");
    } else if (id == ID_CONNECTION_ATTEMPT_FAILED) {
      onEvent(NetEvent::ConnectionFailed, nullptr, 0,
              "No answer from the server");
    } else if (const char* reason = DenyReason(id)) {
      onEvent(NetEvent::ConnectionDenied, nullptr, 0, reason);
    }
  }
}

bool NetClient::IsConnected() const
{
  return pImpl->connected;
}

}
