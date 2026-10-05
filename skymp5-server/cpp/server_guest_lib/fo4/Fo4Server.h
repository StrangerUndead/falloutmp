#pragma once
// Fo4Server: the Fallout 4 game layer of the server.
//
// Owns every Fallout 4 system and the per-actor Fallout 4 state, turns
// client messages into validated actions, and sends the authoritative
// results back (class A in the sync standard). It talks to the engine-side
// server (PartOne) only through Fo4Host, so all of it is testable without
// the game or the network.
#include "ActorValues.h"
#include "Barter.h"
#include "Containers.h"
#include "Crafting.h"
#include "Effects.h"
#include "Locks.h"
#include "Movement.h"
#include "Party.h"
#include "PowerArmor.h"
#include "Progression.h"
#include "RangedCombat.h"
#include "Workshop.h"
#include "WorldServices.h"
#include <functional>
#include <map>
#include <optional>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <random>
#include <set>
#include <string>

class IMessageBase;
enum class MsgType : uint8_t;

namespace fo4 {

class Fo4Host
{
public:
  virtual ~Fo4Host() = default;
  virtual std::array<float, 3> GetActorPos(ActorId actor) = 0;
  virtual uint32_t GetActorWorldOrCell(ActorId actor) = 0;
  virtual bool IsActorAlive(ActorId actor) = 0;
  virtual ProfileId GetProfileId(ActorId actor) = 0;
  virtual bool IsNpc(ActorId actor) = 0;
  // World references (workbenches, frames, doors, terminals)
  virtual std::optional<std::array<float, 3>> GetRefPos(FormId refId) = 0;
  virtual FormId GetRefBaseId(FormId refId) = 0;
  virtual FormId AllocateFormId() = 0;
  virtual void SendTo(ActorId actor, const IMessageBase& msg,
                      bool reliable) = 0;
  virtual void SendToNeighbours(ActorId actor, const IMessageBase& msg,
                                bool reliable) = 0;
  virtual int64_t NowMs() = 0;
  // Fast travel and scripted moves. Returns false if not possible.
  virtual bool TeleportActor(ActorId actor, const std::array<float, 3>& pos,
                             uint32_t worldOrCell)
  {
    (void)actor;
    (void)pos;
    (void)worldOrCell;
    return false;
  }
  // Called when the server kills an actor (F12 takes it from here)
  virtual void OnActorKilled(ActorId victim, ActorId killer) = 0;
  // Applies an accepted movement sample to the actor (position, yaw).
  virtual void SetActorTransform(ActorId actor,
                                 const std::array<float, 3>& pos, float yawDeg)
  {
    (void)actor;
    (void)pos;
    (void)yawDeg;
  }
  // Gamemode event mp.<name>(...args). Returns false when a gamemode
  // handler returned false (blocks the action for blockable events).
  virtual bool FireGamemodeEvent(const std::string& name,
                                 const nlohmann::json& args)
  {
    (void)name;
    (void)args;
    return true;
  }
};

struct Fo4ServerSettings
{
  float activationReach = 300.f;
  float reachSlack = 64.f;
  uint32_t killXpBase = 20; // per target level, until R5 formula lands
  FormId capsId = 0xF;
  DamageSettings damage;
  PowerArmorSettings powerArmor;
  WorkshopSettings workshop;
  LockSettings locks;
  FireSettings fire;
  PartySettings party;
  ProgressionSettings progression;
  ContainerSettings containers;
  MapSettings map;
  MovementSettings movement;
  int64_t timeWeatherIntervalMs = 10000;
  // Objects per WorkshopObjects snapshot chunk (about 40 bytes each).
  uint32_t workshopSnapshotChunk = 200;
  // Biped slots outer apparel may not use over a power armor frame
  // (body armor slots 41-45 in FO4's first-person flags) [verify G-self]
  uint32_t powerArmorBlockedBipedSlots = 0x3E00;
};

struct Fo4ActorState
{
  Fo4Inventory inventory;
  uint32_t inventoryVersion = 0;
  ActorValueStore avs;
  Progression progression;
  std::unique_ptr<EffectSystem> effects;
  std::optional<ItemKey> equippedWeapon;
  std::vector<ItemKey> equippedArmor;
  int32_t level = 1; // NPCs; players use progression.level
  int32_t actorLevelForXp = 1;
  std::set<FormId> discoveredMarkers;
  int64_t lastCombatMs = -1000000000;
  int32_t lastAnnouncedLevel = 0; // for onFo4LevelUp
};

class Fo4Server
{
public:
  Fo4Server(std::shared_ptr<IFo4DataSource> data, Fo4Host& host,
            Fo4ServerSettings settings = {});
  ~Fo4Server();

  // Entry point for every Fallout 4 message from a client.
  void OnMessage(ActorId sender, MsgType type, const IMessageBase& msg);
  // Timers: transitions, effects, regen, build mode; call every tick.
  void Tick();

  Fo4ActorState& Actor(ActorId id);
  const Fo4ActorState* FindActor(ActorId id) const;
  void RemoveActor(ActorId id); // disconnect: forced PA exit etc.

  // Full state to the owner (on spawn and reconnect)
  void SendFullState(ActorId actor);
  void SendInventory(ActorId actor);
  void SendActorValues(ActorId actor);
  void SendProgression(ActorId actor);

  // Server-side kill credit and XP (also used by gamemode kills)
  void AwardKillXp(ActorId killer, ActorId victim);

  // Services, for gamemode bindings and world setup
  const IFo4DataSource& Data() const { return *data; }
  CraftingService& Crafting() { return crafting; }
  ModdingService& Modding() { return modding; }
  ScrapService& Scrap() { return scrap; }
  PowerArmorService& PowerArmor() { return powerArmor; }
  WorkshopService& Workshops() { return workshops; }
  VendorService& Vendors() { return vendors; }
  LockService& Locks() { return locks; }
  RangedCombat& Combat() { return combat; }
  PartyService& Parties() { return parties; }
  ContainerService& Containers() { return containers; }
  WorldClock& Clock() { return clock; }
  int64_t NowMs() { return host.NowMs(); }
  WeatherState& Weather() { return weather; }
  MapService& Map() { return map; }
  MovementValidator& Movement() { return movement; }
  void BroadcastTimeWeather();
  void SendMapMarkers(ActorId actor, bool full);
  // Equip/unequip validation and broadcast (F05)
  std::string Equip(ActorId actor, const ItemKey& item, bool equip);
  void SendEquipment(ActorId actor);
  // Settlement objects as a chunked snapshot plus the settlement state
  // (on build-mode entry and on join for settlements in the same space).
  void SendWorkshopSnapshot(ActorId to, FormId workshopRefId);
  void SendWorkshopState(ActorId to, FormId workshopRefId);
  // Party state of `to`'s profile (nonce 0 for pushes).
  void SendPartyState(ActorId to, uint32_t nonce, const std::string& error);
  std::optional<ActorId> FindPlayerByProfile(ProfileId profile) const;
  DamageModel& Damage() { return damageModel; }
  std::map<std::string, PerkChartEntry>& PerkChart() { return perkChart; }
  // Effect definitions applied to every actor's effect system
  void DefineEffect(EffectDefinition def);

  nlohmann::json ActorToJson(ActorId id) const;
  void LoadActor(ActorId id, const nlohmann::json& j);
  nlohmann::json WorldToJson() const;
  void LoadWorld(const nlohmann::json& j);

  Fo4ServerSettings settings;

private:
  void SendContainer(ActorId to, FormId refId, bool alsoNeighbours);
  void SendPowerArmorStateOf(ActorId actor);
  struct Impl;
  std::unique_ptr<Impl> pImpl;

  std::shared_ptr<IFo4DataSource> data;
  Fo4Host& host;
  CraftingService crafting;
  ModdingService modding;
  ScrapService scrap;
  PowerArmorService powerArmor;
  WorkshopService workshops;
  VendorService vendors;
  LockService locks;
  RangedCombat combat;
  PartyService parties;
  ContainerService containers;
  WorldClock clock;
  WeatherState weather;
  MapService map;
  MovementValidator movement;
  DamageModel damageModel;
  std::map<std::string, PerkChartEntry> perkChart;
  std::vector<EffectDefinition> effectDefs;
  std::map<ActorId, Fo4ActorState> actors;
  std::mt19937 rng;
};

}
