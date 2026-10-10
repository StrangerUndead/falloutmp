#pragma once
// The per-frame tick that drives Platform::Tick on the main thread.
//
// Two sources: an F4SE permanent task, which runs every frame, also while a
// menu pauses the game (Pip-Boy, lockpicking, containers), and
// PlayerCharacter::Update (Actor vfunc 0xCF), which stops in pausing menus.
// F4SE runs its tasks on whichever thread pumps the game's message queue,
// which in game is often a worker thread (in-game log 2026-10-10: three
// threads within a second), so a tick only runs on the main thread, at
// most once per 4 ms. Ticks are skipped during loading screens. The
// threads each source ran on go to FalloutMP.log for the first minute.
namespace fmp::hooks {
void InstallFrameTick();
}
