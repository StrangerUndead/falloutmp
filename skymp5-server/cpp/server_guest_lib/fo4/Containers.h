#pragma once
// Containers, looting, pickup and drop with Fallout 4 item instances (F06).
#include "ItemInstance.h"
#include "LeveledLists.h"
#include <array>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <random>

namespace fo4 {

enum class ContainerError : uint8_t
{
  None = 0,
  NoSuchContainer,
  OutOfReach,
  Locked,
  ItemNotFound,
  InvalidCount,
  NotACorpse,
  Busy,
};
const char* ContainerErrorToString(ContainerError e) noexcept;

struct WorldContainer
{
  FormId refId = 0;
  FormId baseId = 0;
  FormId ownerFaction = 0; // taking from an owned container marks stolen
  std::array<float, 3> pos = { 0, 0, 0 };
  Fo4Inventory inventory;
  bool initialized = false; // base contents resolved
  bool isGroundStack = false; // created by a drop
  uint32_t version = 0;
};

struct ContainerSettings
{
  float reach = 364.f; // F07: 300 + 64 slack
  bool markStolen = true;
};

class ContainerService
{
public:
  ContainerService(const IFo4DataSource& data, ContainerSettings s = {});

  WorldContainer& Register(FormId refId, FormId baseId,
                           std::array<float, 3> pos, FormId ownerFaction = 0);
  WorldContainer* Find(FormId refId);
  const std::map<FormId, WorldContainer>& All() const { return containers; }

  // Resolves base contents (leveled lists at `level`) on first open.
  WorldContainer* Open(FormId refId, int32_t level, std::mt19937& rng);

  // Takes from a container into the actor. Owned containers mark items
  // stolen unless the actor is in the owner faction.
  ContainerError Take(FormId refId, const ItemKey& item, uint32_t count,
                      const std::array<float, 3>& actorPos,
                      Fo4Inventory& actorInv, bool actorIsOwner,
                      bool containerLocked);
  ContainerError Put(FormId refId, const ItemKey& item, uint32_t count,
                     const std::array<float, 3>& actorPos,
                     Fo4Inventory& actorInv, bool containerLocked);
  // Drops onto the ground: a ground-stack container at the position.
  ContainerError Drop(FormId newRefId, const ItemKey& item, uint32_t count,
                      const std::array<float, 3>& pos, Fo4Inventory& actorInv,
                      FormId* outRefId);
  // Removes empty ground stacks; returns their ids for clients.
  std::vector<FormId> CollectEmptyGroundStacks();

  nlohmann::json ToJson() const;
  void LoadJson(const nlohmann::json& j);

  ContainerSettings settings;

private:
  bool InReach(const WorldContainer& c, const std::array<float, 3>& p) const;

  const IFo4DataSource& data;
  LeveledListResolver leveled;
  std::map<FormId, WorldContainer> containers;
};

}
