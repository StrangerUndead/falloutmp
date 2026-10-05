// F24 locks and terminals. The server rolls every lockpick and hack outcome
// (review finding C3); the vanilla minigames are only a presentation.
//
// Activation (F07/F24): TESForm::Activate (vfunc 0x40) of TESObjectDOOR,
// TESObjectCONT and BGSTerminal is hooked. The player's activation of a
// locked door or container is blocked and reported (lockedActivated); the
// server answers setLocked(ref, false) (key: the activation is then run
// again) or openLockpickMenu. Every terminal activation by the player is
// blocked and reported (terminalActivated); the server answers openTerminal
// or openHackingMenu. Activations the plugin runs itself pass (bypass).
// Fallback, if the engine checks the lock before the base Activate: a
// LockpickingMenu that opens without a server session is closed and the
// player's last activation (TESActivateEvent) is reported instead.
//
// Lockpicking: openLockpickMenu opens the vanilla LockpickingMenu. Its sweet
// spot is moved out of reach every frame (AdvanceMovie hook, IMenu vfunc
// 0x04), so the engine never unlocks on its own; each turn of the lock
// (rising edge of turningLock) is a lockpickAttempt. The server answers
// closeLockpickMenu(true) + setLocked(false) on success, and nothing when a
// pin broke. The player closing the menu is lockpickCancelled. Pins the
// engine breaks locally are corrected by the server's inventory updates.
//
// Hacking: openHackingMenu activates the terminal (locked locally) so the
// vanilla word game opens (TerminalMenu mode kHack). Each selection accepted
// in hack mode is a hackGuess (BSInputEventUser::OnButtonEvent hook, vfunc
// 0x08). A vanilla success (TerminalHacked, or the mode leaving kHack) is
// not trusted: it counts as a guess, the terminal menu closes and the
// terminal is locked again until the server decides: closeHackingMenu(true)
// + setLocked(false) + openTerminal, or setHackingAttemptsLeft (wrong
// guess), which reopens the word game.
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>

#include "GameUtil.h"
#include "Modules.h"
#include "Papyrus.h"
#include "Platform.h"

#include <atomic>
#include <cstdint>
#include <format>
#include <mutex>
#include <string_view>

namespace fmp::modules {

namespace {
constexpr auto kLockpickingMenu = "LockpickingMenu";
constexpr auto kTerminalMenu = "TerminalMenu";

// The same reference is not reported twice within this time (looping or
// repeated activation input).
constexpr double kReportCooldownMs = 500.0;
// Minimum time between two lockpick attempts / two hack guesses.
constexpr double kAttemptCooldownMs = 300.0;
// A blocked activation runs again when the server unlocks the reference
// within this time (the player holds the key).
constexpr double kPendingActivationMs = 10000.0;
// A menu the plugin opened must appear within this time.
constexpr double kMenuTimeoutMs = 3000.0;
// The fallback reports the player's activation of this age at most.
constexpr double kLastActivationMs = 3000.0;
// Lockpicking sweet spot center out of the pick's range (degrees).
// [verify] the lock never turns locally with it and the pick still bends
constexpr float kUnreachableSweetSpot = 1000.f;

enum class Kind
{
  Door,
  Container,
  Terminal
};

struct LockpickSession
{
  bool active = false;
  uint32_t ref = 0;
  uint32_t sessionId = 0;
  bool menuSeen = false;
  double openedMs = 0;
  bool turning = false;
  double lastAttemptMs = 0;
};

struct HackSession
{
  bool active = false;
  uint32_t ref = 0;
  uint32_t sessionId = 0;
  int attemptsLeft = 0;
  // A vanilla success was reverted; the server's verdict is pending.
  bool awaitingVerdict = false;
  // Reopen the word game once the terminal menu has closed.
  bool reopen = false;
  bool wasHackMode = false;
  double lastGuessMs = 0;
};

// Shared by the main thread, event sinks and menu hooks (UI thread):
// guarded by `m`. Never call into the engine while holding it (our own
// hooks may run inside those calls).
struct State
{
  std::mutex m;
  LockpickSession pick;
  HackSession hack;
  // Last blocked activation of a locked door/container
  uint32_t pendingRef = 0;
  double pendingMs = 0;
  // Last activation by the player seen by TESActivateEvent (fallback)
  uint32_t lastActivatedRef = 0;
  double lastActivatedMs = 0;
  // Report cooldown
  uint32_t lastReportRef = 0;
  double lastReportMs = 0;
  // openTerminal waiting for the current terminal menu to close
  uint32_t deferredTerminal = 0;
  double deferredUntilMs = 0;
};

State& S()
{
  static State s;
  return s;
}

// Reference the plugin activates itself (main thread).
std::atomic<uint32_t> g_bypassRef{ 0 };

double Now()
{
  return Platform::Get().NowMs();
}

// Activation goes to the server only when a server is configured; without
// one the game plays as vanilla.
bool Networked()
{
  auto& p = Platform::Get();
  return p.Started() && !p.Config().serverIp.empty();
}

// A reference argument: 0 when the script passed no (local) reference.
uint32_t RefArg(const Json& a, size_t i)
{
  const auto& v = a.at(i);
  return v.is_number() ? v.get<uint32_t>() : 0;
}

bool IsLocked(RE::TESObjectREFR* ref)
{
  auto lock = ref ? ref->GetLock() : nullptr;
  if (!lock ||
      !(lock->flags &
        static_cast<std::uint8_t>(RE::REFR_LOCK::Flags::kLocked))) {
    return false;
  }
  return lock->GetLockLevel(ref) != RE::LOCK_LEVEL::kUnlocked;
}

// Locks or unlocks the reference's REFR_LOCK; false if it has none.
bool WriteLock(RE::TESObjectREFR* ref, bool locked)
{
  auto lock = ref ? ref->GetLock() : nullptr;
  if (!lock) {
    return false;
  }
  lock->SetLocked(locked);
  // [verify] marks the change for the save and refreshes the activate prompt
  ref->AddLockChange();
  return true;
}

void HideMenu(const char* name)
{
  if (auto queue = RE::UIMessageQueue::GetSingleton()) {
    queue->AddMessage(RE::BSFixedString(name), RE::UI_MESSAGE_TYPE::kHide);
  }
}

// Runs the vanilla activation for the player past our block (main thread).
bool ActivateAsPlayer(RE::TESObjectREFR* ref)
{
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!ref || !player) {
    return false;
  }
  g_bypassRef = ref->GetFormID();
  const bool ok = ref->ActivateRef(player, nullptr, 1, false, false, false);
  g_bypassRef = 0;
  return ok;
}

// --- Activation -----------------------------------------------------------

// Main thread [verify]: whether the player's activation goes to the server
// instead of the engine.
bool InterceptActivation(Kind kind, RE::TESObjectREFR* target,
                         RE::TESObjectREFR* actionRef)
{
  auto player = RE::PlayerCharacter::GetSingleton();
  if (!target || !actionRef || !player || actionRef != player ||
      !Networked()) {
    return false;
  }
  const auto id = target->GetFormID();
  if (id == g_bypassRef.load()) {
    return false;
  }
  if (kind != Kind::Terminal && !IsLocked(target)) {
    return false;
  }
  const auto now = Now();
  bool report;
  {
    auto& s = S();
    std::scoped_lock lock(s.m);
    report = s.lastReportRef != id || now - s.lastReportMs > kReportCooldownMs;
    if (report) {
      s.lastReportRef = id;
      s.lastReportMs = now;
    }
    if (kind != Kind::Terminal) {
      s.pendingRef = id;
      s.pendingMs = now;
    }
  }
  if (report) {
    Platform::Get().Emit(
      kind == Kind::Terminal ? "terminalActivated" : "lockedActivated",
      Json{ { "ref", id } });
  }
  return true;
}

// TESForm::Activate(itemActivated, actionRef, objectToGet, count), vfunc
// 0x40, on the vtable of one base object type.
template <Kind K>
struct ActivateHook
{
  static bool Thunk(RE::TESForm* a_this, RE::TESObjectREFR* a_target,
                    RE::TESObjectREFR* a_actionRef,
                    RE::TESBoundObject* a_objectToGet, std::int32_t a_count)
  {
    try {
      if (InterceptActivation(K, a_target, a_actionRef)) {
        return false;
      }
    } catch (std::exception& e) {
      REX::ERROR("Activation hook failed: {}", e.what());
    }
    return original(a_this, a_target, a_actionRef, a_objectToGet, a_count);
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

// --- Lockpicking ----------------------------------------------------------

// LockpickingMenu::AdvanceMovie (IMenu vfunc 0x04). Runs where the menu
// advances (UI); only the menu itself and plugin state are touched.
struct LockpickAdvanceHook
{
  static void Thunk(RE::LockpickingMenu* a_this, float a_timeDelta,
                    std::uint64_t a_time)
  {
    bool active = false;
    {
      auto& s = S();
      std::scoped_lock lock(s.m);
      active = s.pick.active;
    }
    // The engine must never succeed on its own: the server decides.
    if (a_this && active) {
      a_this->sweetSpotCenter = kUnreachableSweetSpot;
    }
    original(a_this, a_timeDelta, a_time);
    if (!a_this || !active) {
      return;
    }
    a_this->sweetSpotCenter = kUnreachableSweetSpot;
    try {
      AfterAdvance(*a_this);
    } catch (std::exception& e) {
      REX::ERROR("Lockpick hook failed: {}", e.what());
    }
  }

  // Each new turn of the lock is one attempt the server rolls.
  // [verify] turningLock is set while the player holds "turn", even when
  // the lock cannot turn
  static void AfterAdvance(const RE::LockpickingMenu& menu)
  {
    uint32_t ref = 0, sessionId = 0;
    bool attempt = false;
    {
      auto& s = S();
      std::scoped_lock lock(s.m);
      auto& pick = s.pick;
      if (!pick.active) {
        return;
      }
      const bool turning = menu.turningLock;
      const auto now = Now();
      if (turning && !pick.turning &&
          now - pick.lastAttemptMs > kAttemptCooldownMs) {
        pick.lastAttemptMs = now;
        attempt = true;
        ref = pick.ref;
        sessionId = pick.sessionId;
      }
      pick.turning = turning;
    }
    if (attempt) {
      Platform::Get().Emit("lockpickAttempt",
                           Json{ { "ref", ref }, { "sessionId", sessionId } });
    }
  }

  static inline REL::Relocation<decltype(&Thunk)> original;
};

void OpenLockpickMenu(Platform& p, uint32_t id, uint32_t sessionId)
{
  auto ref = game::Ref(id);
  // The server holds the lock; the local one must be locked for the menu.
  if (!ref || (!IsLocked(ref) && !WriteLock(ref, true))) {
    p.Log("warn",
          std::format("openLockpickMenu: {:X} is not a lockable reference", id));
    p.Emit("lockpickCancelled", Json{ { "ref", id }, { "sessionId", sessionId } });
    return;
  }
  {
    auto& s = S();
    std::scoped_lock lock(s.m);
    s.pick = {};
    s.pick.active = true;
    s.pick.ref = id;
    s.pick.sessionId = sessionId;
    s.pick.openedMs = Now();
    s.pendingRef = 0; // after a pick the player activates again (vanilla)
  }
  // [verify] the vanilla menu opens for a ref the player isn't facing, and
  // refuses (no menu) without local bobby pins: the timeout below cancels
  RE::LockpickingMenu::OpenLockpickingMenu(ref);
}

void CloseLockpickMenu()
{
  bool wasActive;
  {
    auto& s = S();
    std::scoped_lock lock(s.m);
    wasActive = s.pick.active;
    s.pick = {}; // the close event that follows reports nothing
  }
  if (wasActive && game::MenuOpen(kLockpickingMenu)) {
    HideMenu(kLockpickingMenu);
  }
}

// --- Hacking --------------------------------------------------------------

// Opens the vanilla word game on a terminal (the terminal is locked first).
void OpenWordGame(Platform& p, RE::TESObjectREFR* ref)
{
  if (!IsLocked(ref) && !WriteLock(ref, true)) {
    p.Log("warn", std::format("openHackingMenu: terminal {:X} has no lock",
                              ref->GetFormID()));
    return;
  }
  // [verify] activating a locked terminal starts the word game (needs the
  // local Hacker rank, synced by progression)
  ActivateAsPlayer(ref);
}

// The vanilla word game succeeded locally: one guess for the server, and the
// terminal stays closed until its verdict. Main thread.
void OnLocalHackSuccess(uint32_t terminalId)
{
  uint32_t ref = 0, sessionId = 0;
  bool guess = false;
  {
    auto& s = S();
    std::scoped_lock lock(s.m);
    auto& hack = s.hack;
    if (!hack.active || hack.awaitingVerdict ||
        (terminalId && terminalId != hack.ref)) {
      return;
    }
    hack.awaitingVerdict = true;
    const auto now = Now();
    // The accepted selection that won was already reported as a guess.
    guess = now - hack.lastGuessMs > 1000.0;
    if (guess) {
      hack.lastGuessMs = now;
    }
    ref = hack.ref;
    sessionId = hack.sessionId;
  }
  if (guess) {
    Platform::Get().Emit("hackGuess",
                         Json{ { "ref", ref }, { "sessionId", sessionId } });
  }
  HideMenu(kTerminalMenu);
  if (auto r = game::Ref(ref)) {
    WriteLock(r, true);
  }
  Platform::Get().Log(
    "info", std::format("Terminal {:X} hacked locally; waiting for the server",
                        ref));
}

// TerminalMenu::AdvanceMovie (IMenu vfunc 0x04): watches the mode for a
// local hack success (kHack -> login/list).
struct TerminalAdvanceHook
{
  static void Thunk(RE::TerminalMenu* a_this, float a_timeDelta,
                    std::uint64_t a_time)
  {
    original(a_this, a_timeDelta, a_time);
    if (!a_this) {
      return;
    }
    const auto mode = a_this->mode.get();
    bool success = false;
    uint32_t ref = 0;
    {
      auto& s = S();
      std::scoped_lock lock(s.m);
      auto& hack = s.hack;
      if (!hack.active) {
        return;
      }
      const bool hackMode = mode == RE::TerminalMenu::Mode::kHack;
      // [verify] a solved word game moves to kLogin/kList; a lockout closes
      // the menu instead
      success = hack.wasHackMode && !hackMode &&
                mode != RE::TerminalMenu::Mode::kInit &&
                !hack.awaitingVerdict;
      hack.wasHackMode = hackMode;
      ref = hack.ref;
    }
    if (success) {
      Platform::Get().QueueTask([ref] { OnLocalHackSuccess(ref); });
    }
  }
  static inline REL::Relocation<decltype(&Thunk)> original;
};

// TerminalMenu's BSInputEventUser::OnButtonEvent (vfunc 0x08 of its second
// vtable): an accepted selection in hack mode is a guess.
struct TerminalButtonHook
{
  static void Thunk(RE::BSInputEventUser* a_this,
                    const RE::ButtonEvent* a_event)
  {
    try {
      if (a_this && a_event) {
        Check(*static_cast<RE::TerminalMenu*>(a_this), *a_event);
      }
    } catch (std::exception& e) {
      REX::ERROR("Terminal input hook failed: {}", e.what());
    }
    original(a_this, a_event);
  }

  static void Check(const RE::TerminalMenu& menu, const RE::ButtonEvent& e)
  {
    if (menu.mode.get() != RE::TerminalMenu::Mode::kHack || !e.QJustPressed()) {
      return;
    }
    const std::string_view name{ e.QUserEvent().c_str() };
    REX::DEBUG("Terminal hack input '{}'", name);
    // [verify] the user events of a word/bracket selection (keyboard,
    // gamepad, mouse); bracket selections count as guesses too
    if (name != "Accept" && name != "Click") {
      return;
    }
    uint32_t ref = 0, sessionId = 0;
    {
      auto& s = S();
      std::scoped_lock lock(s.m);
      auto& hack = s.hack;
      const auto now = Now();
      if (!hack.active || hack.awaitingVerdict ||
          now - hack.lastGuessMs < kAttemptCooldownMs) {
        return;
      }
      hack.lastGuessMs = now;
      ref = hack.ref;
      sessionId = hack.sessionId;
    }
    Platform::Get().Emit("hackGuess",
                         Json{ { "ref", ref }, { "sessionId", sessionId } });
  }

  static inline REL::Relocation<decltype(&Thunk)> original;
};

void OpenHackingMenu(Platform& p, uint32_t id, uint32_t sessionId,
                     int attemptsLeft)
{
  auto ref = game::Ref(id);
  if (!ref) {
    p.Log("warn", std::format("openHackingMenu: unknown terminal {:X}", id));
    return;
  }
  // A terminal menu still open (closing) is replaced once it has closed.
  const bool menuOpen = game::MenuOpen(kTerminalMenu);
  {
    auto& s = S();
    std::scoped_lock lock(s.m);
    s.hack = {};
    s.hack.active = true;
    s.hack.ref = id;
    s.hack.sessionId = sessionId;
    s.hack.attemptsLeft = attemptsLeft;
    s.hack.reopen = menuOpen;
  }
  if (!menuOpen) {
    OpenWordGame(p, ref);
  }
}

void SetHackingAttemptsLeft(int attemptsLeft)
{
  auto& s = S();
  std::scoped_lock lock(s.m);
  if (!s.hack.active) {
    return;
  }
  // [verify] the word game's own attempt counter lives in the menu's SWF;
  // CommonLibF4 has no way to write it, so it can differ from the server's
  s.hack.attemptsLeft = attemptsLeft;
  if (s.hack.awaitingVerdict) {
    // The local success was a wrong guess on the server: new word game.
    s.hack.awaitingVerdict = false;
    s.hack.reopen = true;
  }
}

void CloseHackingMenu()
{
  bool wasActive;
  {
    auto& s = S();
    std::scoped_lock lock(s.m);
    wasActive = s.hack.active;
    s.hack = {};
  }
  if (wasActive && game::MenuOpen(kTerminalMenu)) {
    HideMenu(kTerminalMenu);
  }
}

// --- Terminals and locks --------------------------------------------------

void OpenTerminal(Platform& p, uint32_t id)
{
  auto ref = game::Ref(id);
  if (!ref) {
    p.Log("warn", std::format("openTerminal: unknown terminal {:X}", id));
    return;
  }
  // The server granted access: no local lock may start the word game.
  if (IsLocked(ref)) {
    WriteLock(ref, false);
  }
  if (game::MenuOpen(kTerminalMenu)) {
    // Still closing (closeHackingMenu just before): open it afterwards.
    auto& s = S();
    std::scoped_lock lock(s.m);
    s.deferredTerminal = id;
    s.deferredUntilMs = Now() + kMenuTimeoutMs;
    return;
  }
  // [verify] the vanilla activation seats the player and shows the pages
  // (BGSTerminal::Show(ref) would show them without the furniture)
  ActivateAsPlayer(ref);
}

void SetLocked(Platform& p, uint32_t id, bool locked)
{
  auto ref = game::Ref(id);
  if (!ref) {
    p.Log("warn", std::format("setLocked: unknown reference {:X}", id));
    return;
  }
  if (!WriteLock(ref, locked)) {
    // No lock data yet: Papyrus creates it.
    papyrus::CallMethod(ref, "ObjectReference", "Lock", nullptr, locked,
                        false);
  }
  if (locked) {
    return;
  }
  // Unlocked with the key (or already open on the server): the player's
  // blocked activation goes on.
  bool rerun = false;
  {
    auto& s = S();
    std::scoped_lock lock(s.m);
    if (s.pendingRef == id && Now() - s.pendingMs < kPendingActivationMs &&
        !s.pick.active) {
      s.pendingRef = 0;
      rerun = true;
    }
  }
  if (rerun) {
    ActivateAsPlayer(ref);
  }
}

// --- Sinks and frame work -------------------------------------------------

class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::MenuOpenCloseEvent& e,
    RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
  {
    try {
      const std::string_view name{ e.menuName.c_str() };
      if (name == kLockpickingMenu) {
        OnLockpickMenu(e.opening);
      } else if (name == kTerminalMenu) {
        OnTerminalMenu(e.opening);
      }
    } catch (std::exception& ex) {
      REX::ERROR("Locks menu sink failed: {}", ex.what());
    }
    return RE::BSEventNotifyControl::kContinue;
  }

private:
  // Plugin state, Emit and the UI message queue only (any thread).
  static void OnLockpickMenu(bool opening)
  {
    auto& s = S();
    uint32_t ref = 0, sessionId = 0;
    bool cancelled = false, unexpected = false;
    {
      std::scoped_lock lock(s.m);
      if (opening) {
        if (s.pick.active) {
          s.pick.menuSeen = true;
          return;
        }
        // The engine opened it itself: the activation hook did not see
        // the locked reference.
        unexpected = true;
        if (Now() - s.lastActivatedMs < kLastActivationMs) {
          ref = s.lastActivatedRef;
          s.pendingRef = ref;
          s.pendingMs = Now();
        }
      } else if (s.pick.active) {
        cancelled = true;
        ref = s.pick.ref;
        sessionId = s.pick.sessionId;
        s.pick = {};
      }
    }
    auto& p = Platform::Get();
    if (cancelled) {
      p.Emit("lockpickCancelled",
             Json{ { "ref", ref }, { "sessionId", sessionId } });
      return;
    }
    if (!unexpected || !Networked()) {
      return;
    }
    HideMenu(kLockpickingMenu);
    if (ref) {
      p.Emit("lockedActivated", Json{ { "ref", ref } });
    } else {
      p.Log("warn", "Lockpicking menu opened without a known reference");
    }
  }

  static void OnTerminalMenu(bool opening)
  {
    if (opening) {
      return;
    }
    auto& s = S();
    std::scoped_lock lock(s.m);
    // The player left the terminal during the word game. The contract has
    // no cancel event; the server drops the session on its own.
    if (s.hack.active && !s.hack.awaitingVerdict && !s.hack.reopen) {
      s.hack = {};
    }
  }
};

// The player's last activation, for the lockpicking fallback.
class ActivateSink final : public RE::BSTEventSink<RE::TESActivateEvent>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::TESActivateEvent& e,
    RE::BSTEventSource<RE::TESActivateEvent>*) override
  {
    // Pointer comparison and the form id only
    auto target = e.objectActivated.get();
    auto player = RE::PlayerCharacter::GetSingleton();
    if (target && player && e.actionRef.get() == player) {
      auto& s = S();
      std::scoped_lock lock(s.m);
      s.lastActivatedRef = target->GetFormID();
      s.lastActivatedMs = Now();
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};

class HackedSink final : public RE::BSTEventSink<RE::TerminalHacked::Event>
{
public:
  RE::BSEventNotifyControl ProcessEvent(
    const RE::TerminalHacked::Event& e,
    RE::BSTEventSource<RE::TerminalHacked::Event>*) override
  {
    RE::ObjectRefHandle handle = e.terminal;
    Platform::Get().QueueTask([handle] {
      auto ref = handle.get();
      OnLocalHackSuccess(ref ? ref->GetFormID() : 0);
    });
    return RE::BSEventNotifyControl::kContinue;
  }
};

void OnFrame(Platform& p)
{
  auto& s = S();
  const auto now = Now();
  const bool terminalOpen = game::MenuOpen(kTerminalMenu);
  uint32_t deferred = 0, reopen = 0, timedOutRef = 0, timedOutSession = 0;
  {
    std::scoped_lock lock(s.m);
    if (s.deferredTerminal && !terminalOpen) {
      deferred = s.deferredTerminal;
      s.deferredTerminal = 0;
    } else if (s.deferredTerminal && now > s.deferredUntilMs) {
      s.deferredTerminal = 0;
    }
    if (s.hack.active && s.hack.reopen && !terminalOpen) {
      s.hack.reopen = false;
      s.hack.wasHackMode = false;
      reopen = s.hack.ref;
    }
    if (s.pick.active && !s.pick.menuSeen &&
        now - s.pick.openedMs > kMenuTimeoutMs) {
      timedOutRef = s.pick.ref;
      timedOutSession = s.pick.sessionId;
      s.pick = {};
    }
  }
  if (deferred) {
    OpenTerminal(p, deferred);
  }
  if (reopen) {
    if (auto ref = game::Ref(reopen)) {
      OpenWordGame(p, ref);
    }
  }
  if (timedOutRef) {
    p.Log("warn", std::format("Lockpicking menu for {:X} did not open",
                              timedOutRef));
    p.Emit("lockpickCancelled",
           Json{ { "ref", timedOutRef }, { "sessionId", timedOutSession } });
  }
}

void InstallHooks()
{
  static bool installed = false;
  if (installed) {
    return;
  }
  installed = true;

  REL::Relocation<std::uintptr_t> door{ RE::VTABLE::TESObjectDOOR[0] };
  ActivateHook<Kind::Door>::original =
    door.write_vfunc(0x40, ActivateHook<Kind::Door>::Thunk);
  REL::Relocation<std::uintptr_t> cont{ RE::VTABLE::TESObjectCONT[0] };
  ActivateHook<Kind::Container>::original =
    cont.write_vfunc(0x40, ActivateHook<Kind::Container>::Thunk);
  REL::Relocation<std::uintptr_t> term{ RE::VTABLE::BGSTerminal[0] };
  ActivateHook<Kind::Terminal>::original =
    term.write_vfunc(0x40, ActivateHook<Kind::Terminal>::Thunk);

  REL::Relocation<std::uintptr_t> pick{ RE::VTABLE::LockpickingMenu[0] };
  LockpickAdvanceHook::original =
    pick.write_vfunc(0x04, LockpickAdvanceHook::Thunk);
  REL::Relocation<std::uintptr_t> termMenu{ RE::VTABLE::TerminalMenu[0] };
  TerminalAdvanceHook::original =
    termMenu.write_vfunc(0x04, TerminalAdvanceHook::Thunk);
  // [verify] VTABLE::TerminalMenu[1] is the BSInputEventUser vtable (+0x10)
  REL::Relocation<std::uintptr_t> termInput{ RE::VTABLE::TerminalMenu[1] };
  TerminalButtonHook::original =
    termInput.write_vfunc(0x08, TerminalButtonHook::Thunk);
  REX::INFO("Installed the lock and terminal hooks");
}
}

void InstallLocks(Platform& p)
{
  InstallHooks();

  static MenuSink menuSink;
  if (auto ui = RE::UI::GetSingleton()) {
    ui->RegisterSink<RE::MenuOpenCloseEvent>(&menuSink);
  }
  static ActivateSink activateSink;
  if (auto source = RE::TESActivateEvent::GetEventSource()) {
    source->RegisterSink(&activateSink);
  }
  static HackedSink hackedSink;
  if (auto source = RE::TerminalHacked::GetEventSource()) {
    source->RegisterSink(&hackedSink);
  }
  p.OnFrame([&p](float) { OnFrame(p); });

  p.RegisterNative("setLocked", [&p](const Json& a) -> Json {
    SetLocked(p, RefArg(a, 0), a.at(1).get<bool>());
    return nullptr;
  });
  p.RegisterNative("openLockpickMenu", [&p](const Json& a) -> Json {
    OpenLockpickMenu(p, RefArg(a, 0), a.at(1).get<uint32_t>());
    return nullptr;
  });
  p.RegisterNative("closeLockpickMenu", [](const Json& a) -> Json {
    (void)a.at(0).get<bool>(); // success: the server's setLocked follows
    CloseLockpickMenu();
    return nullptr;
  });
  p.RegisterNative("openHackingMenu", [&p](const Json& a) -> Json {
    OpenHackingMenu(p, RefArg(a, 0), a.at(1).get<uint32_t>(),
                    a.at(2).get<int>());
    return nullptr;
  });
  p.RegisterNative("setHackingAttemptsLeft", [](const Json& a) -> Json {
    SetHackingAttemptsLeft(a.at(0).get<int>());
    return nullptr;
  });
  p.RegisterNative("closeHackingMenu", [](const Json& a) -> Json {
    (void)a.at(0).get<bool>(); // success: setLocked + openTerminal follow
    CloseHackingMenu();
    return nullptr;
  });
  p.RegisterNative("openTerminal", [&p](const Json& a) -> Json {
    OpenTerminal(p, RefArg(a, 0));
    return nullptr;
  });
}

}
