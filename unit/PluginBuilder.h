#pragma once
// In-memory synthetic plugin builder for tests (ESPM-002).
//
// Produces a byte-exact .esm/.esp image: a TES4 header record followed by
// top-level groups, one per record type. Tests feed the bytes to
// espm::Browser, so readers are verified without any Bethesda data.
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace test_espm {

using Bytes = std::vector<uint8_t>;

class FieldWriter
{
public:
  template <class T>
  FieldWriter& Add(const T& v)
  {
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    bytes.insert(bytes.end(), p, p + sizeof(T));
    return *this;
  }
  FieldWriter& AddString(const std::string& s)
  {
    bytes.insert(bytes.end(), s.begin(), s.end());
    bytes.push_back(0);
    return *this;
  }
  FieldWriter& Zeros(size_t n)
  {
    bytes.insert(bytes.end(), n, 0);
    return *this;
  }
  Bytes bytes;
};

struct Field
{
  std::string type; // 4 chars
  Bytes data;
};

struct Record
{
  std::string type;
  uint32_t formId = 0;
  uint32_t flags = 0;
  uint16_t formVersion = 131;
  std::vector<Field> fields;

  Record& Add(const std::string& t, Bytes d)
  {
    fields.push_back({ t, std::move(d) });
    return *this;
  }
  Record& Add(const std::string& t, const FieldWriter& w)
  {
    return Add(t, w.bytes);
  }
  Record& AddString(const std::string& t, const std::string& s)
  {
    FieldWriter w;
    w.AddString(s);
    return Add(t, w);
  }
  template <class T>
  Record& AddValue(const std::string& t, const T& v)
  {
    FieldWriter w;
    w.Add(v);
    return Add(t, w);
  }
  Record& EditorId(const std::string& s) { return AddString("EDID", s); }
  Record& Keywords(const std::vector<uint32_t>& ids)
  {
    AddValue<uint32_t>("KSIZ", static_cast<uint32_t>(ids.size()));
    FieldWriter w;
    for (auto id : ids) {
      w.Add(id);
    }
    return Add("KWDA", w);
  }
};

class PluginBuilder
{
public:
  // formVersion 131 = Fallout 4, 44 = Skyrim SE
  explicit PluginBuilder(uint16_t formVersion_ = 131, float hedr_ = 1.0f)
    : formVersion(formVersion_)
    , hedr(hedr_)
  {
  }

  PluginBuilder& Master(const std::string& name)
  {
    masters.push_back(name);
    return *this;
  }
  PluginBuilder& SetFlags(uint32_t f)
  {
    tes4Flags = f;
    return *this;
  }

  Record& AddRecord(const std::string& type, uint32_t formId)
  {
    Record r;
    r.type = type;
    r.formId = formId;
    r.formVersion = formVersion;
    records.push_back(r);
    return records.back();
  }

  Bytes Build() const
  {
    Bytes out;
    Record tes4;
    tes4.type = "TES4";
    tes4.flags = tes4Flags;
    tes4.formVersion = formVersion;
    FieldWriter hedrW;
    hedrW.Add(hedr).Add(static_cast<int32_t>(records.size())).Add(
      static_cast<uint32_t>(0x800));
    tes4.Add("HEDR", hedrW);
    for (auto& m : masters) {
      tes4.AddString("MAST", m);
      tes4.AddValue<uint64_t>("DATA", 0);
    }
    WriteRecord(out, tes4);

    std::map<std::string, std::vector<const Record*>> byType;
    std::vector<std::string> order;
    for (auto& r : records) {
      if (!byType.count(r.type)) {
        order.push_back(r.type);
      }
      byType[r.type].push_back(&r);
    }
    for (auto& type : order) {
      Bytes body;
      for (auto* r : byType[type]) {
        WriteRecord(body, *r);
      }
      WriteGroupHeader(out, type, static_cast<uint32_t>(body.size() + 24));
      out.insert(out.end(), body.begin(), body.end());
    }
    return out;
  }

private:
  static void Put(Bytes& out, const void* p, size_t n)
  {
    auto b = reinterpret_cast<const uint8_t*>(p);
    out.insert(out.end(), b, b + n);
  }
  template <class T>
  static void PutV(Bytes& out, T v)
  {
    Put(out, &v, sizeof(T));
  }
  static void PutType(Bytes& out, const std::string& t)
  {
    char buf[4] = { 0, 0, 0, 0 };
    std::memcpy(buf, t.data(), std::min<size_t>(4, t.size()));
    Put(out, buf, 4);
  }

  static void WriteRecord(Bytes& out, const Record& r)
  {
    Bytes data;
    for (auto& f : r.fields) {
      PutType(data, f.type);
      PutV<uint16_t>(data, static_cast<uint16_t>(f.data.size()));
      data.insert(data.end(), f.data.begin(), f.data.end());
    }
    PutType(out, r.type);
    PutV<uint32_t>(out, static_cast<uint32_t>(data.size()));
    PutV<uint32_t>(out, r.flags);
    PutV<uint32_t>(out, r.formId);
    PutV<uint32_t>(out, 0); // revision / version control
    PutV<uint16_t>(out, r.formVersion);
    PutV<uint16_t>(out, 0);
    out.insert(out.end(), data.begin(), data.end());
  }

  static void WriteGroupHeader(Bytes& out, const std::string& label,
                               uint32_t size)
  {
    PutType(out, "GRUP");
    PutV<uint32_t>(out, size);
    PutType(out, label);
    PutV<int32_t>(out, 0); // top group
    PutV<uint16_t>(out, 0);
    PutV<uint16_t>(out, 0);
    PutV<uint16_t>(out, 0);
    PutV<uint16_t>(out, 0);
  }

  uint16_t formVersion;
  float hedr;
  uint32_t tes4Flags = 0x1; // ESM
  std::vector<std::string> masters;
  std::vector<Record> records;
};

}
