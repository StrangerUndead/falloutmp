#pragma once
// The per-frame tick that drives Platform::Tick on the main thread.
//
// Primary: an F4SE permanent task, which runs every frame, also while a
// menu pauses the game (Pip-Boy, lockpicking, containers), so the network
// and the menus' server round trips keep working. Fallback (no task
// interface): PlayerCharacter::Update (Actor vfunc 0xCF), which stops in
// pausing menus. Ticks are skipped during loading screens.
namespace fmp::hooks {
void InstallFrameTick();
}
