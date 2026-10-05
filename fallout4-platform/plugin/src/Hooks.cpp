#include "Hooks.h"

#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "Platform.h"

namespace fmp::hooks {

namespace {
double g_lastTickMs = 0;

void TickNow()
{
  auto& p = Platform::Get();
  double now = p.NowMs();
  float dt =
    g_lastTickMs > 0 ? static_cast<float>(now - g_lastTickMs) / 1000.f : 0.f;
  g_lastTickMs = now;
  // Natives spawn and move references: not while the world is loading
  // [verify] the permanent task also runs during loading screens
  if (game::Loading() || !RE::PlayerCharacter::GetSingleton()) {
    return;
  }
  try {
    p.Tick(dt);
  } catch (std::exception& e) {
    REX::ERROR("Tick failed: {}", e.what());
  }
}

struct PlayerUpdate
{
  static void Thunk(RE::PlayerCharacter* a_this, float a_delta)
  {
    original(a_this, a_delta);
    TickNow();
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};
}

void InstallFrameTick()
{
  static bool installed = false;
  if (installed) {
    return;
  }
  installed = true;
  if (auto tasks = F4SE::GetTaskInterface()) {
    // [verify] permanent tasks run once per frame on the main thread,
    // including while the game is paused by a menu
    tasks->AddTaskPermanent([] { TickNow(); });
    REX::INFO("Frame tick: F4SE permanent task");
    return;
  }
  REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::PlayerCharacter[0] };
  PlayerUpdate::original = vtbl.write_vfunc(0xCF, PlayerUpdate::Thunk);
  REX::INFO("Frame tick: player update hook (no task interface)");
}

}
