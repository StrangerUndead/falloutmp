#pragma once
// Vendors, barter and player-to-player trade (F23).
// Prices follow the vanilla Charisma formula; every trade is atomic across
// both inventories and both caps balances.
#include "ItemInstance.h"
#include "OmodStatResolver.h"
#include <functional>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <set>

namespace fo4 {

enum class BarterError : uint8_t
{
  None = 0,
  UnknownVendor,
  VendorClosed,
  ItemNotAvailable,
  NotEnoughCaps,
  VendorNotEnoughCaps,
  VendorWontBuy,
  StolenFromVendor,
  PriceChanged,
  EmptyTrade,
  NotTrading,
  NotConfirmed,
  StaleOffer,
};
const char* BarterErrorToString(BarterError e) noexcept;

struct PriceModifiers
{
  float charisma = 1.f;
  float buyMult = 1.f;  // perks (Cap Collector), bobblehead, discounts
  float sellMult = 1.f; // perks
};

// buy = max(1.2 * value, value * (3.5 - 0.15 * CHA) * buyMult)  (ceil)
// sell = min(0.8 * value, value / (3.5 - 0.15 * CHA) * sellMult) (floor)
uint32_t BuyPrice(float value, const PriceModifiers& m);
uint32_t SellPrice(float value, const PriceModifiers& m);

struct Vendor
{
  FormId vendorId = 0;
  FormId factionId = 0;
  Fo4Inventory inventory;
  uint32_t caps = 0;
  // Item types the vendor buys; empty = everything
  std::set<ItemType> buysTypes;
  bool acceptsStolen = false;
  // Opening hours in game hours [start, end); start == end = always open
  float openHour = 0.f, closeHour = 0.f;
  double lastRestockDay = 0.0;
};

struct TradeLine
{
  ItemKey item;
  uint32_t count = 0;
};

struct BarterRequest
{
  FormId vendorId = 0;
  std::vector<TradeLine> buy;  // from the vendor
  std::vector<TradeLine> sell; // to the vendor
  int64_t expectedCapsDelta = 0; // player view: + gains, - pays
};

struct BarterResult
{
  BarterError error = BarterError::None;
  int64_t capsDelta = 0;
  bool Ok() const { return error == BarterError::None; }
};

class VendorService
{
public:
  VendorService(const IFo4DataSource& data, FormId capsId);

  Vendor& AddVendor(Vendor v);
  Vendor* Find(FormId vendorId);

  int64_t QuoteCapsDelta(const BarterRequest& req, const PriceModifiers& m,
                         BarterError& error) const;
  BarterResult Trade(const BarterRequest& req, Fo4Inventory& playerInv,
                     const PriceModifiers& m, float gameHour);

  // Restocks vendors whose restock period elapsed. `refill` regenerates the
  // vendor inventory (leveled lists, F14) and returns base caps.
  void Restock(double gameDay, double periodDays,
               const std::function<uint32_t(Vendor&)>& refill);

  nlohmann::json VendorToJson(const Vendor& v) const;
  static Vendor VendorFromJson(const nlohmann::json& j);

private:
  float ValueOf(const ItemKey& key) const;
  static float ConditionScale(uint16_t condition);
  bool IsOpen(const Vendor& v, float hour) const;

  const IFo4DataSource& data;
  OmodStatResolver resolver;
  FormId capsId;
  std::map<FormId, Vendor> vendors;
};

// Player-to-player trade: both sides put offers, both confirm, any change
// resets confirmations; commit moves everything atomically.
class PlayerTrade
{
public:
  PlayerTrade(uint32_t actorA, uint32_t actorB, FormId capsId);

  BarterError SetOffer(uint32_t actor, std::vector<TradeLine> items,
                       uint32_t caps);
  BarterError Confirm(uint32_t actor, uint32_t offerVersion);
  // Commits when both confirmed the current version.
  BarterError Commit(Fo4Inventory& invA, Fo4Inventory& invB);

  uint32_t Version() const { return version; }
  bool BothConfirmed() const { return confirmedA && confirmedB; }

private:
  uint32_t a, b;
  FormId capsId;
  std::vector<TradeLine> offerA, offerB;
  uint32_t capsA = 0, capsB = 0;
  bool confirmedA = false, confirmedB = false;
  uint32_t version = 0;
};

}
