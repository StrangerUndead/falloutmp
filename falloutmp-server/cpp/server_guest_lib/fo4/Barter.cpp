#include "Barter.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace fo4 {

const char* BarterErrorToString(BarterError e) noexcept
{
  switch (e) {
#define C(x)                                                                  \
  case BarterError::x:                                                        \
    return #x;
    C(None)
    C(UnknownVendor)
    C(VendorClosed)
    C(ItemNotAvailable)
    C(NotEnoughCaps)
    C(VendorNotEnoughCaps)
    C(VendorWontBuy)
    C(StolenFromVendor)
    C(PriceChanged)
    C(EmptyTrade)
    C(NotTrading)
    C(NotConfirmed)
    C(StaleOffer)
#undef C
  }
  return "Unknown";
}

namespace {
float BaseMod(float charisma)
{
  return std::max(1.f, 3.5f - 0.15f * charisma);
}
}

uint32_t BuyPrice(float value, const PriceModifiers& m)
{
  if (value <= 0.f) {
    return 0;
  }
  float p = std::max(1.2f * value, value * BaseMod(m.charisma) * m.buyMult);
  return static_cast<uint32_t>(std::ceil(p - 1e-4f));
}

uint32_t SellPrice(float value, const PriceModifiers& m)
{
  if (value <= 0.f) {
    return 0;
  }
  float p = std::min(0.8f * value, value / BaseMod(m.charisma) * m.sellMult);
  return static_cast<uint32_t>(std::floor(p + 1e-4f));
}

VendorService::VendorService(const IFo4DataSource& data_, FormId capsId_)
  : data(data_)
  , resolver(data_)
  , capsId(capsId_)
{
}

Vendor& VendorService::AddVendor(Vendor v)
{
  FormId id = v.vendorId;
  return vendors[id] = std::move(v);
}

Vendor* VendorService::Find(FormId id)
{
  auto it = vendors.find(id);
  return it == vendors.end() ? nullptr : &it->second;
}

float VendorService::ValueOf(const ItemKey& key) const
{
  if (data.FindWeapon(key.baseId)) {
    return resolver.ResolveWeapon(key).value;
  }
  if (data.FindArmor(key.baseId)) {
    float v = resolver.ResolveArmor(key).value;
    if (key.condition) {
      v *= ConditionScale(key.condition);
    }
    return v;
  }
  auto item = data.FindItem(key.baseId);
  return item ? static_cast<float>(item->value) : 0.f;
}

float VendorService::ConditionScale(uint16_t condition)
{
  if (condition == 0) {
    return 1.f;
  }
  if (condition == 0xFFFF) {
    return 0.f;
  }
  return std::min(1.f, condition / 1000.f);
}

bool VendorService::IsOpen(const Vendor& v, float hour) const
{
  if (v.openHour == v.closeHour) {
    return true;
  }
  if (v.openHour < v.closeHour) {
    return hour >= v.openHour && hour < v.closeHour;
  }
  return hour >= v.openHour || hour < v.closeHour; // overnight
}

int64_t VendorService::QuoteCapsDelta(const BarterRequest& req,
                                      const PriceModifiers& m,
                                      BarterError& error) const
{
  error = BarterError::None;
  auto it = vendors.find(req.vendorId);
  if (it == vendors.end()) {
    error = BarterError::UnknownVendor;
    return 0;
  }
  auto& v = it->second;
  int64_t delta = 0;
  for (auto& l : req.buy) {
    delta -= static_cast<int64_t>(BuyPrice(ValueOf(l.item), m)) * l.count;
  }
  for (auto& l : req.sell) {
    if (l.item.baseId == capsId) {
      continue;
    }
    if (l.item.stolenFrom && l.item.stolenFrom == v.factionId) {
      error = BarterError::StolenFromVendor;
      return 0;
    }
    if (l.item.stolenFrom && !v.acceptsStolen) {
      error = BarterError::VendorWontBuy;
      return 0;
    }
    if (!v.buysTypes.empty()) {
      auto item = data.FindItem(l.item.baseId);
      if (!item || !v.buysTypes.count(item->type)) {
        error = BarterError::VendorWontBuy;
        return 0;
      }
    }
    delta += static_cast<int64_t>(SellPrice(ValueOf(l.item), m)) * l.count;
  }
  return delta;
}

BarterResult VendorService::Trade(const BarterRequest& req,
                                  Fo4Inventory& playerInv,
                                  const PriceModifiers& m, float gameHour)
{
  BarterResult r;
  auto v = Find(req.vendorId);
  if (!v) {
    r.error = BarterError::UnknownVendor;
    return r;
  }
  if (req.buy.empty() && req.sell.empty()) {
    r.error = BarterError::EmptyTrade;
    return r;
  }
  if (!IsOpen(*v, gameHour)) {
    r.error = BarterError::VendorClosed;
    return r;
  }
  int64_t delta = QuoteCapsDelta(req, m, r.error);
  if (r.error != BarterError::None) {
    return r;
  }
  if (delta != req.expectedCapsDelta) {
    r.error = BarterError::PriceChanged; // client must re-quote
    return r;
  }
  Fo4Inventory player = playerInv;
  Vendor vendor = *v;
  for (auto& l : req.buy) {
    if (l.count == 0 || !vendor.inventory.Remove(l.item, l.count)) {
      r.error = BarterError::ItemNotAvailable;
      return r;
    }
    player.Add(l.item, l.count);
  }
  for (auto& l : req.sell) {
    if (l.count == 0 || !player.Remove(l.item, l.count)) {
      r.error = BarterError::ItemNotAvailable;
      return r;
    }
    vendor.inventory.Add(l.item, l.count);
  }
  if (delta < 0) {
    uint32_t pay = static_cast<uint32_t>(-delta);
    if (!player.RemoveAnyOf(capsId, pay)) {
      r.error = BarterError::NotEnoughCaps;
      return r;
    }
    vendor.caps += pay;
  } else if (delta > 0) {
    uint32_t get = static_cast<uint32_t>(delta);
    if (vendor.caps < get) {
      r.error = BarterError::VendorNotEnoughCaps;
      return r;
    }
    vendor.caps -= get;
    player.AddSimple(capsId, get);
  }
  playerInv = std::move(player);
  *v = std::move(vendor);
  r.capsDelta = delta;
  return r;
}

void VendorService::Restock(double gameDay, double periodDays,
                            const std::function<uint32_t(Vendor&)>& refill)
{
  for (auto& [id, v] : vendors) {
    if (gameDay - v.lastRestockDay >= periodDays) {
      v.inventory.Clear();
      v.caps = refill ? refill(v) : v.caps;
      v.lastRestockDay = gameDay;
    }
  }
}

nlohmann::json VendorService::VendorToJson(const Vendor& v) const
{
  std::vector<int> types;
  for (auto t : v.buysTypes) {
    types.push_back(static_cast<int>(t));
  }
  return { { "vendorId", v.vendorId },
           { "factionId", v.factionId },
           { "inventory", v.inventory.ToJson() },
           { "caps", v.caps },
           { "buysTypes", types },
           { "acceptsStolen", v.acceptsStolen },
           { "hours", { v.openHour, v.closeHour } },
           { "lastRestockDay", v.lastRestockDay } };
}

Vendor VendorService::VendorFromJson(const nlohmann::json& j)
{
  Vendor v;
  v.vendorId = j.at("vendorId").get<FormId>();
  v.factionId = j.value("factionId", FormId(0));
  if (j.contains("inventory")) {
    v.inventory = Fo4Inventory::FromJson(j["inventory"]);
  }
  v.caps = j.value("caps", uint32_t(0));
  for (int t : j.value("buysTypes", std::vector<int>{})) {
    v.buysTypes.insert(static_cast<ItemType>(t));
  }
  v.acceptsStolen = j.value("acceptsStolen", false);
  if (j.contains("hours") && j["hours"].size() == 2) {
    v.openHour = j["hours"][0].get<float>();
    v.closeHour = j["hours"][1].get<float>();
  }
  v.lastRestockDay = j.value("lastRestockDay", 0.0);
  return v;
}

// ------------------------------------------------------------ player trade

PlayerTrade::PlayerTrade(uint32_t a_, uint32_t b_, FormId capsId_)
  : a(a_)
  , b(b_)
  , capsId(capsId_)
{
}

BarterError PlayerTrade::SetOffer(uint32_t actor, std::vector<TradeLine> items,
                                  uint32_t caps)
{
  if (actor != a && actor != b) {
    return BarterError::NotTrading;
  }
  (actor == a ? offerA : offerB) = std::move(items);
  (actor == a ? capsA : capsB) = caps;
  confirmedA = confirmedB = false; // any change resets both confirmations
  ++version;
  return BarterError::None;
}

BarterError PlayerTrade::Confirm(uint32_t actor, uint32_t offerVersion)
{
  if (actor != a && actor != b) {
    return BarterError::NotTrading;
  }
  if (offerVersion != version) {
    return BarterError::StaleOffer;
  }
  (actor == a ? confirmedA : confirmedB) = true;
  return BarterError::None;
}

BarterError PlayerTrade::Commit(Fo4Inventory& invA, Fo4Inventory& invB)
{
  if (!BothConfirmed()) {
    return BarterError::NotConfirmed;
  }
  Fo4Inventory na = invA, nb = invB;
  auto move = [&](Fo4Inventory& from, Fo4Inventory& to,
                  const std::vector<TradeLine>& lines, uint32_t caps) {
    for (auto& l : lines) {
      if (l.count == 0 || !from.Remove(l.item, l.count)) {
        return false;
      }
      to.Add(l.item, l.count);
    }
    if (caps) {
      if (!from.RemoveAnyOf(capsId, caps)) {
        return false;
      }
      to.AddSimple(capsId, caps);
    }
    return true;
  };
  if (!move(na, nb, offerA, capsA) || !move(nb, na, offerB, capsB)) {
    confirmedA = confirmedB = false;
    return BarterError::ItemNotAvailable;
  }
  invA = std::move(na);
  invB = std::move(nb);
  confirmedA = confirmedB = false;
  offerA.clear();
  offerB.clear();
  capsA = capsB = 0;
  ++version;
  return BarterError::None;
}

}
