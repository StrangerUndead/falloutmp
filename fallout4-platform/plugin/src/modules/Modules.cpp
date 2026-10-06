#include "Modules.h"

#include <F4SE/F4SE.h>

#include "Platform.h"

namespace fmp::modules {

namespace {
struct Entry
{
  const char* feature;
  void (*install)(Platform&);
};

constexpr Entry kModules[] = {
  { "session", InstallSession },
  { "movement", InstallMovement },
  { "puppets", InstallPuppets },
  { "animation", InstallAnimation },
  { "appearance", InstallAppearance },
  { "inventory", InstallInventory },
  { "equipment", InstallEquipment },
  { "actorValues", InstallActorValues },
  { "progression", InstallProgression },
  { "effects", InstallEffects },
  { "combat", InstallCombat },
  { "powerArmor", InstallPowerArmor },
  { "workshop", InstallWorkshop },
  { "locks", InstallLocks },
  { "map", InstallMap },
  { "world", InstallWorld },
  { "probe", InstallProbe },
};
}

void InstallAll(Platform& p)
{
  for (auto& m : kModules) {
    if (!p.Feature(m.feature)) {
      REX::INFO("Feature '{}' is off", m.feature);
      continue;
    }
    try {
      m.install(p);
    } catch (std::exception& e) {
      REX::ERROR("Feature '{}' failed to install: {}", m.feature, e.what());
    }
  }
}

}
