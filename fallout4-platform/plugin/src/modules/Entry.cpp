// Entry: the entrance test build (F33-T04, T05, T06).
//
// Off unless FalloutMP.json has an "entry" block; then it never starts the
// multiplayer session, so the test runs in a plain game.
//
//   "entry": {
//     "test": {                      load a save from the main menu
//       "source": "Save5_",          a save in the save folder (exact name,
//                                    or a prefix: the newest match), or a
//                                    full path
//       "patch": true,               write position and time with the save
//                                    editor (fos) first
//       "pos": [-79800, 90500, 7800], "angleZ": 180, "worldspace": "0x3c",
//       "gameHour": 13.0,
//       "trigger": "gameDataReady",  or "mainMenu" (+ "settleMs")
//       "route": "A",                A: BGSSaveLoadManager::LoadGame,
//                                    B: the console's load command
//       "keepSave": false
//     },
//     "capture": false,              turn the loaded game into the entry
//                                    template and save FalloutMP_Template
//     "checkAllIds": false           also log the save/load functions later
//                                    tasks will call
//   }
//
// Every step goes to FalloutMP.log with timings and thread ids (F33 §9).
// Generated saves are named FalloutMP_Entry_<8 hex> and deleted after the
// load; leftovers are removed at startup.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>
#include <REX/W32/OLE32.h>
#include <REX/W32/SHELL32.h>

#include "Fos.h"
#include "GameUtil.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace fmp::modules {

namespace {
namespace fs = std::filesystem;

constexpr const char* kGeneratedPrefix = "FalloutMP_Entry_";
constexpr const char* kTemplateName = "FalloutMP_Template";
constexpr uint32_t kPipboy = 0x00021B3B;

// Engine functions the pinned CommonLibF4 doesn't wrap (ids from
// reference/fo4-save-entry.md §4.2)
constexpr uint64_t kIdLoadGame = 2228039;
constexpr uint64_t kIdDoLoadGame = 2228040;
constexpr uint64_t kIdSaveGame = 2228036;
constexpr uint64_t kIdDeleteSaveFile = 2228033;
constexpr uint64_t kIdDoBeforeNewOrLoad = 2228951;

struct IdCheck
{
  const char* name;
  uint64_t id;
  uint64_t expected; // inferred 1.11.240 offset, 0 = unknown
  bool used;         // called by this build (else logged on request only)
};

// Expected offsets: FO4_Wrld's 1.11.191 addresses + F4SE's +0x520 shift
// (an inference until this check runs; reference §4.2). REL::ID stops the
// game with a message naming the id when the Address Library lacks it, so
// ids this build doesn't call are only checked with "checkAllIds": true.
constexpr IdCheck kIds[] = {
  { "BGSSaveLoadManager::LoadGame", kIdLoadGame, 0xBF98D0, true },
  { "BGSSaveLoadManager::BuildSaveGameList", 2228053, 0, true },
  { "BGSSaveLoadManager::GetSaveDirectoryPath", 2228028, 0, true },
  { "BGSSaveLoadManager singleton", 2697802, 0, true },
  { "DoBeforeNewOrLoad", kIdDoBeforeNewOrLoad, 0, true },
  { "Main::QGameSystemsShouldUpdate", 2698031, 0, true },
  { "Console::ExecuteCommand", 2248537, 0, true },
  { "BGSSaveLoadManager::DoLoadGame", kIdDoLoadGame, 0xBF9980, false },
  { "BGSSaveLoadManager::SaveGame", kIdSaveGame, 0xBF9330, false },
  { "BGSSaveLoadManager::DeleteSaveFile", kIdDeleteSaveFile, 0, false },
  { "BGSSaveLoadManager::QueueSaveLoadTask", 2228080, 0, false },
};

struct TestConfig
{
  bool enabled = false;
  std::string source;
  bool patch = true;
  fos::EntryPatch entryPatch;
  bool onMainMenu = false;
  double settleMs = 1500;
  bool routeB = false;
  bool keepSave = false;
};

struct State
{
  TestConfig test;
  bool capture = false;
  double captureDelayMs = 20000;
  fs::path saveDir;
  uint32_t mainThread = 0;

  // The test
  bool testStarted = false;
  std::string generatedName; // without .fos
  double mainMenuOpenedMs = -1;
  double loadCalledMs = 0;
  std::atomic<bool> loadPending{ false };
  std::atomic<bool> loadInFlight{ false };
  std::atomic<bool> postLoadSeen{ false };
  std::atomic<bool> postLoadOk{ false };
  std::atomic<double> preLoadMs{ 0 };
  std::atomic<double> postLoadMs{ 0 };
  double reportAtMs[3] = { 0, 0, 0 };
  int reportsDone = 0;

  // Capture
  int captureStep = 0;
  double captureNextMs = 0;
  bool captureDone = false;
};

State g;

std::string Hex(uint64_t v)
{
  return std::format("{:X}", v);
}

uint32_t ThreadId()
{
  return REX::W32::GetCurrentThreadId();
}

void Hud(const std::string& text)
{
  RE::SendHUDMessage::ShowHUDMessage(text.c_str(), nullptr, false, false);
}

// --- Engine calls
// ------------------------------------------------------------

void DoBeforeNewOrLoad()
{
  using func_t = void (*)();
  static REL::Relocation<func_t> func{ REL::ID(kIdDoBeforeNewOrLoad) };
  func();
}

bool LoadGame(RE::BGSSaveLoadManager* mgr, const char* name)
{
  // bool LoadGame(mgr, name, deviceId, outputStats, checkForMods,
  //               ignoreMissingContent) (po3's CommonLibF4, StartOnSaveF4)
  using func_t = bool (*)(RE::BGSSaveLoadManager*, const char*, std::int32_t,
                          std::uint32_t, bool, bool);
  static REL::Relocation<func_t> func{ REL::ID(kIdLoadGame) };
  return func(mgr, name, -1, 0, true, true);
}

void LogIds(bool all)
{
  const auto base = REX::FModule::GetExecutingModule().GetBaseAddress();
  for (const auto& c : kIds) {
    if (!c.used && !all) {
      continue;
    }
    const auto offset = REL::ID(c.id).offset();
    std::string bytes;
    const auto* code = reinterpret_cast<const uint8_t*>(base + offset);
    for (int i = 0; i < 16; ++i) {
      bytes += std::format("{:02X} ", code[i]);
    }
    std::string note;
    if (c.expected) {
      note = offset == c.expected
        ? " (as expected)"
        : std::format(" (expected {:X}: the inference was wrong)", c.expected);
    }
    REX::INFO("entry: id {} {} at +{:X}{} : {}", c.id, c.name, offset, note,
              bytes);
  }
}

// --- Save folder
// -------------------------------------------------------------

fs::path DocumentsSaves()
{
  wchar_t* docs = nullptr;
  fs::path out;
  if (REX::W32::SHGetKnownFolderPath(REX::W32::FOLDERID_Documents, 0, nullptr,
                                     &docs) == 0 &&
      docs) {
    out = fs::path(docs) / "My Games" /
      std::string(F4SE::GetSaveFolderName()) / "Saves";
  }
  if (docs) {
    REX::W32::CoTaskMemFree(docs);
  }
  return out;
}

fs::path FindSaveDir()
{
  if (auto mgr = RE::BGSSaveLoadManager::GetSingleton()) {
    char buf[1024] = {};
    mgr->GetSaveDirectoryPath(buf);
    REX::INFO("entry: the game's save folder is '{}'", buf);
    fs::path p(buf);
    std::error_code ec;
    if (buf[0] && fs::is_directory(p, ec)) {
      return p;
    }
  }
  auto fallback = DocumentsSaves();
  REX::WARN("entry: using the default save folder {}", fallback.string());
  return fallback;
}

void RemoveLeftovers()
{
  std::error_code ec;
  if (g.saveDir.empty() || !fs::is_directory(g.saveDir, ec)) {
    return;
  }
  int removed = 0;
  for (auto& e : fs::directory_iterator(g.saveDir, ec)) {
    const auto name = e.path().filename().string();
    if (name.rfind(kGeneratedPrefix, 0) == 0) {
      fs::remove(e.path(), ec);
      ++removed;
    }
  }
  if (removed) {
    REX::INFO("entry: removed {} leftover generated save files", removed);
  }
}

void DeleteGenerated()
{
  if (g.generatedName.empty() || g.test.keepSave) {
    return;
  }
  std::error_code ec;
  for (const char* ext : { ".fos", ".f4se", ".fos.tmp" }) {
    fs::remove(g.saveDir / (g.generatedName + ext), ec);
  }
  REX::INFO("entry: deleted {} and its co-save", g.generatedName);
}

fs::path FindSource(const std::string& source)
{
  std::error_code ec;
  fs::path direct(source);
  if (direct.is_absolute() && fs::is_regular_file(direct, ec)) {
    return direct;
  }
  if (fs::is_regular_file(g.saveDir / source, ec)) {
    return g.saveDir / source;
  }
  // A prefix: the newest matching .fos
  fs::path best;
  fs::file_time_type bestTime{};
  for (auto& e : fs::directory_iterator(g.saveDir, ec)) {
    const auto name = e.path().filename().string();
    if (e.path().extension() == ".fos" && name.rfind(source, 0) == 0 &&
        name.rfind(kGeneratedPrefix, 0) != 0) {
      auto t = fs::last_write_time(e.path(), ec);
      if (best.empty() || t > bestTime) {
        best = e.path();
        bestTime = t;
      }
    }
  }
  return best;
}

// --- The load test
// -----------------------------------------------------------

void StartTest(Platform& p)
{
  if (g.testStarted) {
    return;
  }
  g.testStarted = true;
  const double t0 = p.NowMs();
  REX::INFO("entry: test starting (thread {}, main thread {})", ThreadId(),
            g.mainThread);

  const auto source = FindSource(g.test.source);
  if (source.empty()) {
    REX::ERROR("entry: no save matching '{}' in {}", g.test.source,
               g.saveDir.string());
    return;
  }
  std::ifstream in(source, std::ios::binary);
  fos::Bytes bytes((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  REX::INFO("entry: source {} ({} bytes, sha256 {})", source.string(),
            bytes.size(), fos::Sha256Hex(bytes));

  fos::Bytes out;
  try {
    if (g.test.patch) {
      out = fos::WriteEntrySave(bytes, g.test.entryPatch);
      REX::INFO("entry: patched and verified in {:.1f} ms ({} bytes)",
                p.NowMs() - t0, out.size());
    } else {
      fos::Parse(bytes); // still check that it reads
      out = std::move(bytes);
    }
  } catch (const fos::Error& e) {
    REX::ERROR("entry: the save editor refused the source: {}", e.what());
    return;
  }

  std::random_device rd;
  g.generatedName = std::format("{}{:08X}", kGeneratedPrefix, rd());
  const auto tmp = g.saveDir / (g.generatedName + ".fos.tmp");
  const auto dst = g.saveDir / (g.generatedName + ".fos");
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(out.data()),
            static_cast<std::streamsize>(out.size()));
    if (!f) {
      REX::ERROR("entry: can't write {}", tmp.string());
      return;
    }
  }
  std::error_code ec;
  fs::rename(tmp, dst, ec);
  if (ec) {
    REX::ERROR("entry: can't rename to {}: {}", dst.string(), ec.message());
    return;
  }
  REX::INFO("entry: wrote {} in {:.1f} ms", dst.string(), p.NowMs() - t0);

  auto mgr = RE::BGSSaveLoadManager::GetSingleton();
  if (!mgr) {
    REX::ERROR("entry: no BGSSaveLoadManager");
    return;
  }
  mgr->BuildSaveGameList(mgr->displayPlayerID);
  bool listed = false;
  for (auto entry : mgr->saveGameList) {
    if (entry && entry->fileName && g.generatedName == entry->fileName) {
      listed = true;
    }
  }
  REX::INFO("entry: save list has {} entries; ours is {}",
            mgr->saveGameList.size(), listed ? "listed" : "NOT listed");
  if (!mgr->saveGameList.empty() && mgr->saveGameList.back() &&
      mgr->saveGameList.back()->fileName) {
    REX::INFO("entry: newest list entry name '{}'",
              mgr->saveGameList.back()->fileName);
  }

  // As StartOnSaveF4 does: on the main thread (Tick runs the load)
  g.loadPending = true;
}

// Main thread only: F4SE tasks also run on worker threads.
void RunLoad(Platform& p)
{
  auto mgr = RE::BGSSaveLoadManager::GetSingleton();
  g.loadInFlight = true;
  g.loadCalledMs = p.NowMs();
  if (!g.test.routeB) {
    DoBeforeNewOrLoad();
    static REL::Relocation<bool*> shouldUpdate{
      RE::ID::Main::QGameSystemsShouldUpdate
    };
    *shouldUpdate = true;
    REX::INFO("entry: route A, LoadGame('{}') on thread {}", g.generatedName,
              ThreadId());
    const bool ok = LoadGame(mgr, g.generatedName.c_str());
    REX::INFO("entry: LoadGame returned {} after {:.0f} ms", ok,
              p.NowMs() - g.loadCalledMs);
    if (ok) {
      return;
    }
    REX::WARN("entry: route A failed; trying route B");
  }
  const std::string cmd = "load " + g.generatedName;
  REX::INFO("entry: route B, console '{}'", cmd);
  RE::Console::ExecuteCommand(cmd.c_str());
}

void ReportAfterLoad(Platform& p, int n)
{
  auto player = RE::PlayerCharacter::GetSingleton();
  std::string where = "no player";
  float dist = -1;
  if (player) {
    auto pos = player->GetPosition();
    where = std::format("({:.0f}, {:.0f}, {:.0f}) in {:X}", pos.x, pos.y,
                        pos.z, game::SpaceOf(player));
    if (g.test.entryPatch.placement) {
      const auto& t = g.test.entryPatch.placement->pos;
      dist = std::hypot(pos.x - t[0], pos.y - t[1]);
    }
  }
  float hour = -1;
  if (auto gh = game::Form<RE::TESGlobal>(0x38)) {
    hour = gh->GetValue();
  }
  REX::INFO("entry: report {} at +{:.1f} s after the load: player {}, {} "
            "from the target, GameHour {:.2f}, menus: Loading {} Fader {} "
            "Main {}, in game {}",
            n, (p.NowMs() - g.postLoadMs.load()) / 1000.0, where,
            dist < 0 ? std::string("n/a") : std::format("{:.0f} u", dist),
            hour, game::MenuOpen("LoadingMenu"), game::MenuOpen("FaderMenu"),
            game::MenuOpen("MainMenu"), p.InGame());
}

// --- The capture command (F33 §4.5.1 step 4)
// ----------------------------------

void CaptureTick(Platform& p)
{
  if (!g.capture || g.captureDone || !p.InGame() || game::Loading()) {
    return;
  }
  const double now = p.NowMs();
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!player) {
    return;
  }
  switch (g.captureStep) {
    case 0:
      if (now - p.InGameSinceMs() < g.captureDelayMs) {
        return;
      }
      Hud("FalloutMP capture: stripping the inventory");
      REX::INFO("entry: capture: removing all items (the Pip-Boy comes back)");
      papyrus::CallMethod(player, "ObjectReference", "RemoveAllItems", nullptr,
                          RE::BSTSmartPointer<RE::BSScript::Object>{}, false);
      g.captureStep = 1;
      g.captureNextMs = now + 3000;
      return;
    case 1:
      if (now < g.captureNextMs) {
        return;
      }
      CleanWorldSweepNow(p);
      g.captureStep = 2;
      g.captureNextMs = now + 3000;
      return;
    case 2: {
      if (now < g.captureNextMs) {
        return;
      }
      CleanWorldSweepNow(p);
      auto pipboy = game::Form<RE::TESBoundObject>(kPipboy);
      const int pipboys =
        pipboy ? player->GetInventoryObjectCount(pipboy) : -1;
      auto pos = player->GetPosition();
      REX::INFO("entry: capture: Pip-Boys {}, player at ({:.0f}, {:.0f}, "
                "{:.0f}) in {:X}",
                pipboys, pos.x, pos.y, pos.z, game::SpaceOf(player));
      if (pipboys < 1) {
        REX::WARN("entry: capture: no Pip-Boy yet, waiting");
        g.captureNextMs = now + 3000;
        return;
      }
      const std::string cmd = std::string("save ") + kTemplateName;
      REX::INFO("entry: capture: console '{}'", cmd);
      RE::Console::ExecuteCommand(cmd.c_str());
      Hud("FalloutMP capture: saved as FalloutMP_Template");
      g.captureDone = true;
      return;
    }
  }
}

// --- Every frame
// ---------------------------------------------------------------

void Tick(Platform& p)
{
  // The permanent task runs on whichever thread pumps the message queue;
  // the load, the reports and the capture touch the game: main thread only
  if (!game::OnMainThread()) {
    return;
  }
  if (!g.mainThread) {
    g.mainThread = ThreadId();
    REX::INFO("entry: first frame task on the main thread {} (main menu "
              "open: {}, player: {})",
              g.mainThread, game::MenuOpen("MainMenu"),
              RE::PlayerCharacter::GetSingleton() != nullptr);
  }
  if (g.loadPending.exchange(false)) {
    RunLoad(p);
    return;
  }
  if (g.test.enabled && !g.testStarted && g.test.onMainMenu &&
      g.mainMenuOpenedMs >= 0 &&
      p.NowMs() - g.mainMenuOpenedMs >= g.test.settleMs) {
    StartTest(p);
  }
  if (g.postLoadSeen && g.reportsDone < 3 &&
      p.NowMs() >= g.reportAtMs[g.reportsDone]) {
    ReportAfterLoad(p, ++g.reportsDone);
  }
  CaptureTick(p);
}

class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::MenuOpenCloseEvent& e,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
  {
    const std::string_view name = e.menuName.c_str();
    if (name == "MainMenu" || name == "LoadingMenu" || name == "FaderMenu") {
      auto& p = Platform::Get();
      REX::INFO("entry: {} {} at {:.0f} ms", name,
                e.opening ? "opened" : "closed", p.NowMs());
      if (name == "MainMenu" && e.opening && g.mainMenuOpenedMs < 0) {
        g.mainMenuOpenedMs = p.NowMs();
      }
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};

TestConfig ReadTest(const nlohmann::json& j)
{
  TestConfig t;
  if (!j.is_object()) {
    return t;
  }
  t.enabled = true;
  t.source = j.value("source", "");
  t.patch = j.value("patch", true);
  t.onMainMenu = j.value("trigger", "gameDataReady") == "mainMenu";
  t.settleMs = j.value("settleMs", 1500.0);
  t.routeB = j.value("route", "A") == "B";
  t.keepSave = j.value("keepSave", false);
  if (t.patch) {
    if (j.contains("pos") && j["pos"].is_array() && j["pos"].size() == 3) {
      fos::Placement pl;
      pl.worldspace = static_cast<uint32_t>(
        std::stoul(j.value("worldspace", "0x3c"), nullptr, 0));
      for (int i = 0; i < 3; ++i) {
        pl.pos[i] = j["pos"][i].get<float>();
      }
      pl.yawRadians = game::ToRad(j.value("angleZ", 0.f));
      if (pl.yawRadians > 3.14159265f) {
        pl.yawRadians -= 6.2831853f; // within ±π
      }
      t.entryPatch.placement = pl;
    }
    if (j.contains("gameHour")) {
      t.entryPatch.gameHour = j["gameHour"].get<float>();
    }
    if (j.contains("gameDaysPassed")) {
      t.entryPatch.gameDaysPassed = j["gameDaysPassed"].get<float>();
    }
  }
  return t;
}
}

bool EntrySuppressesSession()
{
  return g.test.enabled || g.capture;
}

void EntryOnPreLoadGame(const char* name)
{
  // Loader thread: atomics and the log only
  g.preLoadMs = Platform::Get().NowMs();
  REX::INFO("entry: kPreLoadGame '{}' on thread {}", name ? name : "",
            ThreadId());
}

void EntryOnPostLoadGame(bool ok)
{
  auto& p = Platform::Get();
  const double now = p.NowMs();
  REX::INFO("entry: kPostLoadGame ok={} on thread {}, {:.0f} ms after "
            "kPreLoadGame",
            ok, ThreadId(), now - g.preLoadMs.load());
  if (!g.loadInFlight.exchange(false)) {
    return; // not our load
  }
  g.postLoadOk = ok;
  g.postLoadMs = now;
  p.QueueTask([&p, ok] {
    REX::INFO("entry: our load finished ({}); {:.0f} ms since the call",
              ok ? "ok" : "FAILED", p.NowMs() - g.loadCalledMs);
    DeleteGenerated();
    const double t = p.NowMs();
    g.reportAtMs[0] = t + 2000;
    g.reportAtMs[1] = t + 5000;
    g.reportAtMs[2] = t + 10000;
    g.postLoadSeen = true;
  });
}

void InstallEntry(Platform& p)
{
  const auto& raw = p.Config().raw;
  const bool configured = raw.contains("entry") && raw["entry"].is_object();
  g.saveDir = FindSaveDir();
  RemoveLeftovers();
  if (!configured) {
    return;
  }
  const auto& e = raw["entry"];
  g.test = ReadTest(e.value("test", nlohmann::json()));
  g.capture = e.value("capture", false);
  g.captureDelayMs = e.value("captureDelayMs", 20000.0);
  REX::INFO("entry: test {}, capture {}; the multiplayer session stays off",
            g.test.enabled, g.capture);
  LogIds(e.value("checkAllIds", false));

  static MenuSink menuSink;
  if (auto ui = RE::UI::GetSingleton()) {
    ui->RegisterSink<RE::MenuOpenCloseEvent>(&menuSink);
  }
  // The permanent task covers the main menu (Platform's tick needs a
  // loaded game); the frame callback covers the game, where the task often
  // runs on a worker thread. Tick only acts on the main thread.
  if (auto tasks = F4SE::GetTaskInterface()) {
    tasks->AddTaskPermanent([&p] { Tick(p); });
  }
  p.OnFrame([&p](float) { Tick(p); });
  if (g.test.enabled && !g.test.onMainMenu) {
    // At kGameDataReady, as StartOnSaveF4 does
    StartTest(p);
  }
}

}
