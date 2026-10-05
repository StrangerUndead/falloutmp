#pragma once
// PlayerCharacter::Update (Actor vfunc 0xCF) drives Platform::Tick: once
// per frame on the main thread while a game is loaded.
namespace fmp::hooks {
void InstallPlayerUpdate();
}
