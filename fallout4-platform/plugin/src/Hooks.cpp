#include "Hooks.h"

#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "Platform.h"

namespace fmp::hooks {

namespace {
struct PlayerUpdate
{
  static void Thunk(RE::PlayerCharacter* a_this, float a_delta)
  {
    original(a_this, a_delta);
    try {
      Platform::Get().Tick(a_delta);
    } catch (std::exception& e) {
      REX::ERROR("Tick failed: {}", e.what());
    }
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};
}

void InstallPlayerUpdate()
{
  static bool installed = false;
  if (installed) {
    return;
  }
  installed = true;
  REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::PlayerCharacter[0] };
  PlayerUpdate::original = vtbl.write_vfunc(0xCF, PlayerUpdate::Thunk);
  REX::INFO("Installed the player update hook");
}

}
