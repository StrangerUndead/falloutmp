#include "Codec.h"

#include "MessageSerializerFactory.h"
#include <nlohmann/json.hpp>
#include <slikenet/BitStream.h>

namespace fmp {

Codec::Codec()
  : serializer(MessageSerializerFactory::CreateMessageSerializer())
{
}

Codec::~Codec() = default;

std::vector<uint8_t> Codec::Encode(const std::string& json) const
{
  SLNet::BitStream stream;
  serializer->Serialize(json.data(), stream);
  auto data = stream.GetData();
  return { data, data + stream.GetNumberOfBytesUsed() };
}

std::string Codec::Decode(const uint8_t* data, size_t length) const
{
  if (length < 2) {
    return {};
  }
  if (auto res = serializer->Deserialize(data, length)) {
    nlohmann::json j;
    res->message->WriteJson(j);
    return j.dump();
  }
  // Not a registered message: JSON text after the packet id
  return std::string(reinterpret_cast<const char*>(data) + 1, length - 1);
}

}
