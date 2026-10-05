#pragma once
// JSON <-> wire format of SkyMP messages (skymp5-server/cpp/messages).
// Messages the serializer knows travel in the binary format; anything
// else (custom packets) as the packet id byte followed by JSON text.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class MessageSerializer;

namespace fmp {

class Codec
{
public:
  Codec();
  ~Codec();

  // Throws on JSON without a numeric "t".
  std::vector<uint8_t> Encode(const std::string& json) const;

  // data[0] is the packet id. Returns the message as JSON text.
  std::string Decode(const uint8_t* data, size_t length) const;

private:
  std::shared_ptr<MessageSerializer> serializer;
};

}
