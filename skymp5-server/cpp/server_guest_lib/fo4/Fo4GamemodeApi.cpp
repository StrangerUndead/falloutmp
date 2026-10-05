#include "Fo4GamemodeApi.h"
#include "Fo4Server.h"
#include <cmath>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>

namespace fo4 {

namespace {
using json = nlohmann::json;
using Handler = std::function<json(Fo4Server&, const json&)>;

json Ok(json extra = json::object())
{
  extra["ok"] = true;
  return extra;
}

std::array<float, 3> Vec(const json& j, const char* key)
{
  if (!j.contains(key)) {
    return { 0, 0, 0 };
  }
  return j.at(key).get<std::array<float, 3>>();
}

json InventoryJson(const Fo4Inventory& inv)
{
  return inv.ToJson()["entries"];
}

const std::map<std::string, Handler>& Commands()
{
  static const std::map<std::string, Handler> kCommands = {
    // ------------------------------------------------------------ items
    { "getInventory",
      [](Fo4Server& s, const json& a) {
        return Ok({ { "entries",
                      InventoryJson(s.Actor(a.at("actorId")).inventory) } });
      } },
    { "addItem",
      [](Fo4Server& s, const json& a) {
        auto id = a.at("actorId").get<ActorId>();
        s.Actor(id).inventory.Add(ItemKeyFromJson(a.at("item")),
                                  a.value("count", 1u));
        s.SendInventory(id);
        return Ok();
      } },
    { "removeItem",
      [](Fo4Server& s, const json& a) {
        auto id = a.at("actorId").get<ActorId>();
        bool ok = s.Actor(id).inventory.Remove(ItemKeyFromJson(a.at("item")),
                                               a.value("count", 1u));
        if (ok) {
          s.SendInventory(id);
        }
        return ok ? Ok() : json{ { "ok", false }, { "error", "NotEnough" } };
      } },
    { "equipWeapon",
      [](Fo4Server& s, const json& a) {
        auto& st = s.Actor(a.at("actorId"));
        if (a.at("item").is_null()) {
          st.equippedWeapon.reset();
          return Ok();
        }
        auto key = ItemKeyFromJson(a.at("item"));
        if (!st.inventory.Find(key)) {
          return json{ { "ok", false }, { "error", "NotInInventory" } };
        }
        st.equippedWeapon = st.inventory.Find(key)->key;
        return Ok();
      } },
    { "setEquippedArmor",
      [](Fo4Server& s, const json& a) {
        auto& st = s.Actor(a.at("actorId"));
        st.equippedArmor.clear();
        for (auto& i : a.at("items")) {
          st.equippedArmor.push_back(ItemKeyFromJson(i));
        }
        return Ok();
      } },
    { "getWeaponStats",
      [](Fo4Server& s, const json& a) {
        OmodStatResolver r(s.Data());
        auto st = r.ResolveWeapon(ItemKeyFromJson(a.at("item")));
        return Ok({ { "damage", st.damage },
                    { "capacity", st.capacity },
                    { "ammoId", st.ammoId },
                    { "automatic", st.automatic },
                    { "weight", st.weight },
                    { "value", st.value },
                    { "maxShotsPerSecond", st.MaxShotsPerSecond() } });
      } },
    // ----------------------------------------------------- actor values
    { "getActorValue",
      [](Fo4Server& s, const json& a) {
        auto& avs = s.Actor(a.at("actorId")).avs;
        FormId av = a.at("avId");
        return Ok({ { "current", avs.GetCurrent(av) },
                    { "max", avs.GetMax(av) },
                    { "base", avs.GetBase(av) } });
      } },
    { "setActorValue",
      [](Fo4Server& s, const json& a) {
        auto id = a.at("actorId").get<ActorId>();
        auto& avs = s.Actor(id).avs;
        FormId av = a.at("avId");
        if (a.contains("base")) {
          avs.SetBase(av, a["base"]);
          avs.RecomputeDerived(s.Actor(id).progression.level);
        }
        if (a.contains("current")) {
          avs.SetCurrent(av, a["current"]);
        }
        s.SendActorValues(id);
        return Ok();
      } },
    // ------------------------------------------------------ progression
    { "getProgression",
      [](Fo4Server& s, const json& a) {
        return Ok({ { "progression",
                      s.Actor(a.at("actorId")).progression.ToJson() } });
      } },
    { "awardXp",
      [](Fo4Server& s, const json& a) {
        auto id = a.at("actorId").get<ActorId>();
        auto& st = s.Actor(id);
        auto ev = st.progression.AwardXp(a.at("amount"), a.value("direct", false),
                                         st.avs);
        s.SendProgression(id);
        return Ok({ { "level", ev.newLevel },
                    { "perkPointsGained", ev.perkPointsGained } });
      } },
    { "definePerk",
      [](Fo4Server& s, const json& a) {
        PerkChartEntry e;
        e.name = a.value("name", std::string());
        e.special = a.at("special");
        e.specialRequired = a.value("specialRequired", 1);
        for (auto& r : a.at("ranks")) {
          e.ranks.push_back({ r.at("perkId"), r.value("levelRequired", 1) });
        }
        s.PerkChart()[a.at("key").get<std::string>()] = std::move(e);
        return Ok();
      } },
    { "defineEffect",
      [](Fo4Server& s, const json& a) {
        static const std::map<std::string, EffectKind> kKinds = {
          { "restoreOverTime", EffectKind::RestoreOverTime },
          { "restoreInstant", EffectKind::RestoreInstant },
          { "damageOverTime", EffectKind::DamageOverTime },
          { "valueModifier", EffectKind::ValueModifier },
          { "removeRads", EffectKind::RemoveRads },
          { "addRads", EffectKind::AddRads },
        };
        auto it = kKinds.find(a.at("kind").get<std::string>());
        if (it == kKinds.end()) {
          return json{ { "ok", false }, { "error", "UnknownKind" } };
        }
        s.DefineEffect({ a.at("effectId"), it->second, a.value("avId", 0u) });
        return Ok();
      } },
    { "getEffects",
      [](Fo4Server& s, const json& a) {
        return Ok({ { "effects",
                      s.Actor(a.at("actorId")).effects->ToJson() } });
      } },
    { "cureAddictions",
      [](Fo4Server& s, const json& a) {
        auto id = a.at("actorId").get<ActorId>();
        s.Actor(id).effects->CureAddictions();
        s.SendEffects(id);
        return Ok();
      } },
    { "addRads",
      [](Fo4Server& s, const json& a) {
        auto id = a.at("actorId").get<ActorId>();
        auto& st = s.Actor(id);
        st.effects->ApplyRadiationExposure(a.at("amount").get<float>(), 1.f,
                                           st.avs);
        s.SendActorValues(id);
        return Ok();
      } },
    // ----------------------------------------------------- power armor
    { "addPowerArmorFrame",
      [](Fo4Server& s, const json& a) {
        PowerArmorFrame f;
        f.refId = a.at("refId");
        f.baseId = a.value("baseId", 0x2079Eu);
        auto p = Vec(a, "pos");
        f.pos[0] = p[0];
        f.pos[1] = p[1];
        f.pos[2] = p[2];
        f.worldOrCell = a.value("worldOrCell", 0x3Cu);
        f.ownerProfileId = a.value("ownerProfileId", -1);
        for (auto& i : a.value("contents", json::array())) {
          f.contents.Add(ItemKeyFromJson(i), 1);
        }
        s.PowerArmor().AddFrame(std::move(f));
        return Ok();
      } },
    { "getPowerArmor",
      [](Fo4Server& s, const json& a) {
        if (a.contains("frameRefId")) {
          auto f = s.PowerArmor().FindFrame(a["frameRefId"]);
          if (!f) {
            return json{ { "ok", false }, { "error", "NoSuchFrame" } };
          }
          return Ok({ { "frame", s.PowerArmor().FrameToJson(*f) } });
        }
        auto w = s.PowerArmor().GetWorn(a.at("actorId"));
        return Ok({ { "worn",
                      w ? s.PowerArmor().WornToJson(*w) : json(nullptr) } });
      } },
    // -------------------------------------------------------- workshops
    { "addWorkshop",
      [](Fo4Server& s, const json& a) {
        Workshop w;
        w.workbenchRefId = a.at("refId");
        w.locationId = a.value("locationId", 0u);
        for (auto& ar : a.value("areas", json::array())) {
          WorkshopArea area;
          area.shape = ar.value("shape", std::string("box")) == "sphere"
            ? WorkshopArea::Shape::Sphere
            : WorkshopArea::Shape::Box;
          area.center = Vec(ar, "center");
          area.halfExtents = Vec(ar, "halfExtents");
          area.radius = ar.value("radius", 0.f);
          area.rotZ = ar.value("rotZ", 0.f);
          w.areas.push_back(area);
        }
        w.budget.max = a.value("budgetMax", s.Workshops().settings.defaultBudget);
        s.Workshops().AddWorkshop(std::move(w));
        return Ok();
      } },
    { "getWorkshop",
      [](Fo4Server& s, const json& a) {
        auto w = s.Workshops().Find(a.at("refId"));
        if (!w) {
          return json{ { "ok", false }, { "error", "NoSuchWorkshop" } };
        }
        auto j = s.Workshops().ToJson(*w);
        j["ratings"] = { { "food", w->ratings.food },
                         { "water", w->ratings.water },
                         { "safety", w->ratings.safety },
                         { "beds", w->ratings.beds },
                         { "power", w->ratings.power },
                         { "population", w->ratings.population },
                         { "happiness", w->ratings.happiness } };
        return Ok({ { "workshop", j } });
      } },
    { "setWorkshopOwner",
      [](Fo4Server& s, const json& a) {
        auto w = s.Workshops().Find(a.at("refId"));
        if (!w) {
          return json{ { "ok", false }, { "error", "NoSuchWorkshop" } };
        }
        auto type = a.value("type", std::string("profile"));
        w->owner.type = type == "none" ? WorkshopOwner::Type::None
          : type == "group"            ? WorkshopOwner::Type::Group
                                       : WorkshopOwner::Type::Profile;
        w->owner.id = a.value("id", -1);
        ++w->version;
        return Ok();
      } },
    { "addSettler",
      [](Fo4Server& s, const json& a) {
        auto r = s.Workshops().AddSettler(a.at("refId"), a.at("actorId"));
        return r.Ok() ? Ok()
                      : json{ { "ok", false },
                              { "error", WorkshopErrorToString(r.error) } };
      } },
    // ---------------------------------------------------------- vendors
    { "addVendor",
      [](Fo4Server& s, const json& a) {
        s.Vendors().AddVendor(VendorService::VendorFromJson(a));
        return Ok();
      } },
    // ------------------------------------------------------------ locks
    { "setLock",
      [](Fo4Server& s, const json& a) {
        LockState l;
        l.level = a.at("level");
        l.keyId = a.value("keyId", 0u);
        l.locked = a.value("locked", true);
        l.ownerFaction = a.value("ownerFaction", 0u);
        s.Locks().SetLock(a.at("refId"), l);
        return Ok();
      } },
    { "getLock",
      [](Fo4Server& s, const json& a) {
        auto l = s.Locks().GetLock(a.at("refId"));
        if (!l) {
          return Ok({ { "lock", nullptr } });
        }
        return Ok({ { "lock",
                      { { "level", l->level },
                        { "keyId", l->keyId },
                        { "locked", l->locked } } } });
      } },
    { "setTerminalLock",
      [](Fo4Server& s, const json& a) {
        TerminalState t;
        t.level = a.at("level");
        t.locked = a.value("locked", true);
        s.Locks().SetTerminal(a.at("refId"), t);
        return Ok();
      } },
    // ---------------------------------------------------------- parties
    { "getParty",
      [](Fo4Server& s, const json& a) {
        auto pid = s.Parties().GetPartyOf(a.at("profileId"));
        if (!pid) {
          return Ok({ { "party", nullptr } });
        }
        auto p = s.Parties().Find(*pid);
        return Ok({ { "party",
                      { { "id", p->id },
                        { "leader", p->leader },
                        { "members", p->members } } } });
      } },
    { "addPvpZone",
      [](Fo4Server& s, const json& a) {
        PvpZone z;
        z.center = Vec(a, "center");
        z.radius = a.at("radius");
        z.worldOrCell = a.value("worldOrCell", 0u);
        auto mode = a.value("mode", std::string("safe"));
        z.mode = mode == "open" ? PvpZoneMode::Open
          : mode == "flagged"   ? PvpZoneMode::Flagged
                                : PvpZoneMode::Safe;
        s.Parties().AddZone(z);
        return Ok();
      } },
    // ------------------------------------------------- time and weather
    { "getTime",
      [](Fo4Server& s, const json&) {
        auto now = s.NowMs();
        return Ok({ { "gameDays", s.Clock().GameDays(now) },
                    { "gameHour", s.Clock().GameHour(now) },
                    { "timeScale", s.Clock().TimeScale() },
                    { "weatherId", s.Weather().weatherId },
                    { "radstorm", s.Weather().radstorm } });
      } },
    { "setTime",
      [](Fo4Server& s, const json& a) {
        auto now = s.NowMs();
        if (a.contains("gameDays")) {
          s.Clock().SetGameDays(a["gameDays"].get<double>(), now);
        } else if (a.contains("gameHour")) {
          double day = std::floor(s.Clock().GameDays(now));
          s.Clock().SetGameDays(day + a["gameHour"].get<double>() / 24.0,
                                now);
        }
        if (a.contains("timeScale")) {
          s.Clock().SetTimeScale(a["timeScale"].get<float>(), now);
        }
        s.BroadcastTimeWeather();
        return Ok();
      } },
    { "setWeather",
      [](Fo4Server& s, const json& a) {
        s.Weather().weatherId = a.value("weatherId", 0u);
        s.Weather().transitionSec = a.value("transitionSec", 10.f);
        s.Weather().radstorm = a.value("radstorm", false);
        s.BroadcastTimeWeather();
        return Ok();
      } },
    // ------------------------------------------------- map and travel
    { "addMapMarker",
      [](Fo4Server& s, const json& a) {
        MapMarker m;
        m.refId = a.at("refId");
        m.pos = Vec(a, "pos");
        m.worldOrCell = a.value("worldOrCell", 0x3Cu);
        m.name = a.value("name", std::string());
        m.type = a.value("type", 0);
        m.canTravel = a.value("canTravel", true);
        m.visibleByDefault = a.value("visibleByDefault", false);
        s.Map().AddMarker(std::move(m));
        return Ok();
      } },
    { "discoverMarker",
      [](Fo4Server& s, const json& a) {
        auto id = a.at("actorId").get<ActorId>();
        s.Actor(id).discoveredMarkers.insert(a.at("refId").get<FormId>());
        s.SendMapMarkers(id, true);
        return Ok();
      } },
    { "getDiscoveredMarkers",
      [](Fo4Server& s, const json& a) {
        return Ok({ { "markers",
                      s.Actor(a.at("actorId")).discoveredMarkers } });
      } },
    { "equip",
      [](Fo4Server& s, const json& a) {
        auto err = s.Equip(a.at("actorId"), ItemKeyFromJson(a.at("item")),
                           a.value("equip", true));
        return err.empty() ? Ok() : json{ { "ok", false }, { "error", err } };
      } },
    // ----------------------------------------------------------- misc
    { "sendFullState",
      [](Fo4Server& s, const json& a) {
        s.SendFullState(a.at("actorId"));
        return Ok();
      } },
    { "getActorState",
      [](Fo4Server& s, const json& a) {
        return Ok({ { "state", s.ActorToJson(a.at("actorId")) } });
      } },
    { "getWorldState",
      [](Fo4Server& s, const json&) {
        return Ok({ { "world", s.WorldToJson() } });
      } },
  };
  return kCommands;
}
}

nlohmann::json Fo4Call(Fo4Server& server, const std::string& command,
                       const nlohmann::json& args)
{
  auto& cmds = Commands();
  auto it = cmds.find(command);
  if (it == cmds.end()) {
    return { { "ok", false }, { "error", "UnknownCommand: " + command } };
  }
  try {
    return it->second(server, args);
  } catch (const std::exception& e) {
    return { { "ok", false }, { "error", std::string("BadArguments: ") + e.what() } };
  }
}

std::vector<std::string> Fo4ListCommands()
{
  std::vector<std::string> res;
  for (auto& [name, h] : Commands()) {
    res.push_back(name);
  }
  return res;
}

}
