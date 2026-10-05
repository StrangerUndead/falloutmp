// F19 progression of the local player: setPlayerLevelAndXp, setPerkPoints,
// addPerk, removePerk, openSpecialMenu (falloutPlatform.ts
// ProgressionNatives).
//
// Level, XP, perk points and perks are server-owned; the engine only
// mirrors them for the HUD, the Pip-Boy and engine-side perk effects
// (lockpick tiers, crafting gates, carry weight). The mirror must never
// start a vanilla level-up: the level is written before the XP, and the XP
// stays below the next level's threshold.
//
// Not here (follow-up, F19-T07/CLI-021): suppressing vanilla XP awards
// (Actor::RewardExperience on the player) and the perk chart's own commit
// (PlayerCharacter::SelectPerk); both need detours of non-virtual functions.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>

namespace fmp::modules {

namespace {
constexpr uint32_t kExperience = 0x2C9; // Av.Experience: the XP total

void SetLevelAndXp(Platform& p, int64_t level, double xp, double xpForNext)
{
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!player || !std::isfinite(xp) || !std::isfinite(xpForNext)) {
    return;
  }
  // The player's level lives in its base NPC (0x7), like `player.setlevel`.
  // Written directly, no LevelIncrease event fires: no level-up message,
  // no vanilla perk point.
  // [verify] the Pip-Boy and HUD show the new level at once (else the
  // menus refresh on reopen), and the derived Health max follows (the
  // server also sends it through setActorValueMax).
  auto lvl = static_cast<uint16_t>(std::clamp<int64_t>(level, 1, 65534));
  if (auto npc = player->GetNPC(); npc && npc->actorData.level != lvl) {
    npc->actorData.level = lvl;
    p.Log("info", std::format("Player level {}", lvl));
  }

  auto info = game::Form<RE::ActorValueInfo>(kExperience);
  if (!info) {
    return;
  }
  // xpForNextLevel is the server's total for the next level (vanilla curve,
  // fo4/Progression.cpp XpForLevel); staying below it means the engine has
  // no level-up to grant.
  // [verify] the Experience AV holds the XP total (not the XP into the
  // level) and setting it shows the right fill of the HUD XP bar without a
  // level-up.
  double target = std::max(0.0, xp);
  if (xpForNext >= 1.0) {
    target = std::min(target, xpForNext - 1.0);
  }
  RE::ActorValueOwner& values = *player;
  float delta = static_cast<float>(target) - values.GetActorValue(*info);
  if (std::fabs(delta) >= 0.5f) {
    values.SetBaseActorValue(*info, values.GetBaseActorValue(*info) + delta);
  }
}

void SetPerkPoints(int64_t points)
{
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!player) {
    return;
  }
  // [verify] SetPerkCount updates the "perk point available" HUD hint and
  // the perk chart's point counter without a sound loop.
  player->SetPerkCount(
    static_cast<std::uint8_t>(std::clamp<int64_t>(points, 0, 255)));
}

// Papyrus Actor.AddPerk/RemovePerk: the player's perk list, rank chain and
// entry points are kept by the native behind them.
void ChangePerk(Platform& p, uint32_t perkId, bool add)
{
  auto player = RE::PlayerCharacter::GetSingleton();
  auto perk = game::Form<RE::BGSPerk>(perkId);
  if (!player || !perk) {
    p.Log("warn", std::format("{} of unknown perk {:X}",
                              add ? "addPerk" : "removePerk", perkId));
    return;
  }
  if (add) {
    // AddPerk(Perk akPerk, bool abNotify = false)
    papyrus::CallMethod(player, "Actor", "AddPerk", nullptr, perk, false);
  } else {
    // RemovePerk(Perk akPerk)
    papyrus::CallMethod(player, "Actor", "RemovePerk", nullptr, perk);
  }
}
}

void InstallProgression(Platform& p)
{
  p.RegisterNative("setPlayerLevelAndXp", [&p](const Json& a) -> Json {
    SetLevelAndXp(p, a.at(0).get<int64_t>(), a.at(1).get<double>(),
                  a.at(2).get<double>());
    return nullptr;
  });
  p.RegisterNative("setPerkPoints", [](const Json& a) -> Json {
    SetPerkPoints(a.at(0).get<int64_t>());
    return nullptr;
  });
  p.RegisterNative("addPerk", [&p](const Json& a) -> Json {
    ChangePerk(p, a.at(0).get<uint32_t>(), true);
    return nullptr;
  });
  p.RegisterNative("removePerk", [&p](const Json& a) -> Json {
    ChangePerk(p, a.at(0).get<uint32_t>(), false);
    return nullptr;
  });
  // The vanilla SPECIAL menu for character creation (Game.ShowSPECIALMenu,
  // no parameters). The menu's own commit is not intercepted yet: the
  // server's ProgressionUpdate overwrites the values (F19 §4.4).
  p.RegisterNative("openSpecialMenu", [](const Json&) -> Json {
    papyrus::CallStatic("Game", "ShowSPECIALMenu", nullptr);
    return nullptr;
  });
}

}
