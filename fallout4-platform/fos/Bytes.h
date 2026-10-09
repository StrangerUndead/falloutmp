#pragma once
// Internal: bounds-checked little-endian reading and writing.
#include "Fos.h"

#include <cstring>
#include <limits>
#include <string>

namespace fmp::fos::detail {

class Reader
{
public:
  Reader(std::span<const uint8_t> data, size_t base = 0)
    : data(data)
    , base(base)
  {
  }

  size_t Pos() const { return pos; }
  size_t AbsPos() const { return base + pos; }
  size_t Remaining() const { return data.size() - pos; }
  bool AtEnd() const { return pos == data.size(); }

  void Need(size_t n) const
  {
    if (n > Remaining()) {
      throw Error(ErrorCode::Truncated,
                  "needs " + std::to_string(n) + " bytes, " +
                    std::to_string(Remaining()) + " left",
                  AbsPos());
    }
  }

  void Seek(size_t p)
  {
    if (p > data.size()) {
      throw Error(ErrorCode::Truncated, "seek past the end", base + p);
    }
    pos = p;
  }

  uint8_t U8()
  {
    Need(1);
    return data[pos++];
  }

  uint16_t U16()
  {
    Need(2);
    uint16_t v = static_cast<uint16_t>(data[pos] | (data[pos + 1] << 8));
    pos += 2;
    return v;
  }

  uint32_t U32()
  {
    Need(4);
    uint32_t v = static_cast<uint32_t>(data[pos]) |
      (static_cast<uint32_t>(data[pos + 1]) << 8) |
      (static_cast<uint32_t>(data[pos + 2]) << 16) |
      (static_cast<uint32_t>(data[pos + 3]) << 24);
    pos += 4;
    return v;
  }

  uint64_t U64()
  {
    uint64_t lo = U32();
    uint64_t hi = U32();
    return lo | (hi << 32);
  }

  int32_t I32() { return static_cast<int32_t>(U32()); }

  float F32()
  {
    uint32_t v = U32();
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  }

  // 3 bytes, big-endian
  RefId Ref()
  {
    Need(3);
    RefId r{ (static_cast<uint32_t>(data[pos]) << 16) |
             (static_cast<uint32_t>(data[pos + 1]) << 8) | data[pos + 2] };
    pos += 3;
    return r;
  }

  std::string WStr()
  {
    uint16_t n = U16();
    Need(n);
    std::string s(reinterpret_cast<const char*>(data.data() + pos), n);
    pos += n;
    return s;
  }

  Bytes Take(size_t n)
  {
    Need(n);
    Bytes out(data.begin() + pos, data.begin() + pos + n);
    pos += n;
    return out;
  }

  std::span<const uint8_t> View(size_t n)
  {
    Need(n);
    auto s = data.subspan(pos, n);
    pos += n;
    return s;
  }

private:
  std::span<const uint8_t> data;
  size_t base = 0;
  size_t pos = 0;
};

class Writer
{
public:
  Bytes& Out() { return out; }
  size_t Size() const { return out.size(); }

  void U8(uint8_t v) { out.push_back(v); }
  void U16(uint16_t v)
  {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
  }
  void U32(uint32_t v)
  {
    for (int i = 0; i < 4; ++i) {
      out.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }
  }
  void U64(uint64_t v)
  {
    U32(static_cast<uint32_t>(v));
    U32(static_cast<uint32_t>(v >> 32));
  }
  void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
  void F32(float f)
  {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    U32(v);
  }
  void Ref(RefId r)
  {
    out.push_back(static_cast<uint8_t>(r.raw >> 16));
    out.push_back(static_cast<uint8_t>(r.raw >> 8));
    out.push_back(static_cast<uint8_t>(r.raw));
  }
  void WStr(const std::string& s)
  {
    if (s.size() > std::numeric_limits<uint16_t>::max()) {
      throw Error(ErrorCode::TooLarge, "string longer than 65535 bytes");
    }
    U16(static_cast<uint16_t>(s.size()));
    Raw(s.data(), s.size());
  }
  void Raw(const void* p, size_t n)
  {
    auto b = static_cast<const uint8_t*>(p);
    out.insert(out.end(), b, b + n);
  }
  void Raw(const Bytes& b) { out.insert(out.end(), b.begin(), b.end()); }
  void PatchU32(size_t at, uint32_t v)
  {
    for (int i = 0; i < 4; ++i) {
      out[at + i] = static_cast<uint8_t>(v >> (8 * i));
    }
  }

private:
  Bytes out;
};

// Limits (design §7.2)
inline constexpr size_t kMaxFileSize = 256u * 1024 * 1024;
inline constexpr uint32_t kMaxShotSide = 8192;
inline constexpr uint32_t kMaxHeaderSize = 4096;
inline constexpr size_t kMaxLightPlugins = 4096;
inline constexpr uint32_t kMaxBlocksPerTable = 4096;
inline constexpr uint32_t kMaxChangeForms = 4000000;
inline constexpr uint32_t kMaxInflated = 64u * 1024 * 1024;

inline size_t LengthFieldSize(uint8_t width)
{
  return width == 0 ? 1 : width == 1 ? 2 : 4;
}

inline uint8_t WidthFor(uint32_t n)
{
  return n <= 0xFF ? 0 : n <= 0xFFFF ? 1 : 2;
}

Bytes Inflate(std::span<const uint8_t> data, uint32_t size);
Bytes Deflate(std::span<const uint8_t> data);
// Writes the 27-byte initial data prefix of a reference body.
void WritePrefix(Bytes& body, RefId space, const std::array<float, 3>& pos,
                 const std::array<float, 3>& rot);

}
