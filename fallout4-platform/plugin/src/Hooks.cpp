#include "Hooks.h"

#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "Platform.h"

#include <atomic>
#include <format>
#include <map>
#include <mutex>
#include <string>

namespace fmp::hooks {

namespace {
// Both sources can run on the main thread in the same frame: one tick per
// 4 ms at most.
constexpr double kMinTickGapMs = 4.0;
constexpr double kStatsEveryMs = 10000.0;
constexpr int kStatsReports = 6;

double g_lastTickMs = 0;

// Which threads each source ran on, for FalloutMP.log (the first minute of
// ticking). Sources run on any thread, hence the lock.
struct Stats
{
  std::mutex lock;
  std::map<uint32_t, uint64_t> task, player;
  uint64_t ticks = 0;
  double firstMs = -1;
  double lastReportMs = 0;
  int reports = 0;
} g_stats;

std::string Describe(const std::map<uint32_t, uint64_t>& byThread)
{
  std::string s;
  for (auto& [thread, n] : byThread) {
    s += std::format("{}{}{} x{}", s.empty() ? "" : ", ", thread,
                     thread == game::MainThreadId() ? " (main)" : "", n);
  }
  return s.empty() ? "none" : s;
}

void Count(bool fromTask)
{
  auto& p = Platform::Get();
  const double now = p.NowMs();
  std::lock_guard l(g_stats.lock);
  if (g_stats.reports >= kStatsReports) {
    return;
  }
  ++(fromTask ? g_stats.task : g_stats.player)[game::CurrentThreadId()];
  if (g_stats.firstMs < 0) {
    g_stats.firstMs = g_stats.lastReportMs = now;
  }
  if (now - g_stats.lastReportMs >= kStatsEveryMs) {
    g_stats.lastReportMs = now;
    ++g_stats.reports;
    REX::INFO("Frame tick threads (last {:.0f} s): F4SE task: {}; player "
              "update: {}; ticks run {}",
              kStatsEveryMs / 1000, Describe(g_stats.task),
              Describe(g_stats.player), g_stats.ticks);
    g_stats.task.clear();
    g_stats.player.clear();
    g_stats.ticks = 0;
  }
}

void TickNow(bool fromTask)
{
  Count(fromTask);
  // Game objects are only safe to change on the main thread. F4SE runs its
  // tasks on whichever thread pumps the message queue, so the other
  // threads skip (in game the player update hook covers the main thread).
  if (!game::OnMainThread()) {
    return;
  }
  auto& p = Platform::Get();
  double now = p.NowMs();
  if (g_lastTickMs > 0 && now - g_lastTickMs < kMinTickGapMs) {
    return;
  }
  float dt =
    g_lastTickMs > 0 ? static_cast<float>(now - g_lastTickMs) / 1000.f : 0.f;
  g_lastTickMs = now;
  // Natives spawn and move references: not while the world is loading
  if (game::Loading() || !RE::PlayerCharacter::GetSingleton()) {
    return;
  }
  {
    std::lock_guard l(g_stats.lock);
    ++g_stats.ticks;
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
    TickNow(false);
  }
  static inline REL::Relocation<decltype(&PlayerUpdate::Thunk)> original;
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
    // Runs every frame, also while a menu pauses the game, but not always
    // on the main thread (TickNow skips the other threads)
    tasks->AddTaskPermanent([] { TickNow(true); });
  }
  // PlayerCharacter::Update (Actor vfunc 0xCF): in game, every frame the
  // game isn't paused
  REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::PlayerCharacter[0] };
  PlayerUpdate::original = vtbl.write_vfunc(0xCF, PlayerUpdate::Thunk);
  REX::INFO("Frame tick: F4SE permanent task and player update, main thread "
            "{} only",
            game::MainThreadId());
}

}
