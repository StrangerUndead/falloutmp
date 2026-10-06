import { ClientContext } from "../context";
import {
  HackResultCode,
  LockpickOp,
  LockResultCode,
  TerminalOp,
} from "../messages/codes";
import { LockpickAttemptMessage, TerminalActionMessage } from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";

interface Session {
  refId: number;
  sessionId: number;
}

// F07/F29 locks and terminals (review finding C3): the vanilla minigames
// are only a presentation. Every attempt goes to the server, which rolls
// the outcome from Locksmith/Hacker ranks and SPECIAL, spends bobby pins
// and awards XP.
export class LockService {
  private lockpick: Session | undefined;
  private hack: Session | undefined;

  constructor(private readonly ctx: ClientContext) {
    ctx.router.on(Fo4MsgType.LockpickAttempt, (m) => this.onLockpick(m));
    ctx.router.on(Fo4MsgType.TerminalAction, (m) => this.onTerminal(m));
  }

  activeLockpickSession(): Session | undefined {
    return this.lockpick;
  }

  activeHackSession(): Session | undefined {
    return this.hack;
  }

  // Player activated a locked door/container.
  activateLock(refId: number): void {
    this.ctx.send(Fo4MsgType.LockpickAttempt, { op: LockpickOp.Activate, refId });
  }

  attemptLockpick(): void {
    if (!this.lockpick) {
      return;
    }
    this.ctx.send(Fo4MsgType.LockpickAttempt, {
      op: LockpickOp.Attempt,
      refId: this.lockpick.refId,
      sessionId: this.lockpick.sessionId,
    });
  }

  cancelLockpick(): void {
    if (!this.lockpick) {
      return;
    }
    this.ctx.send(Fo4MsgType.LockpickAttempt, {
      op: LockpickOp.Cancel,
      refId: this.lockpick.refId,
      sessionId: this.lockpick.sessionId,
    });
    this.lockpick = undefined;
    this.ctx.platform.closeLockpickMenu(false);
  }

  activateTerminal(refId: number): void {
    this.ctx.send(Fo4MsgType.TerminalAction, { op: TerminalOp.Begin, refId });
  }

  guessPassword(): void {
    if (!this.hack) {
      return;
    }
    this.ctx.send(Fo4MsgType.TerminalAction, {
      op: TerminalOp.Attempt,
      refId: this.hack.refId,
      sessionId: this.hack.sessionId,
    });
  }

  // The plugin's word game ended without a result; the server's session
  // times out on its own.
  cancelHack(): void {
    this.hack = undefined;
  }

  reset(): void {
    if (this.lockpick) {
      this.ctx.platform.closeLockpickMenu(false);
    }
    if (this.hack) {
      this.ctx.platform.closeHackingMenu(false);
    }
    this.lockpick = undefined;
    this.hack = undefined;
  }

  private onLockpick(m: LockpickAttemptMessage): void {
    const p = this.ctx.platform;
    const local = p.refs.toLocal(m.refId);
    const code = m.outcome as LockResultCode;
    switch (code) {
      case LockResultCode.Opened:
      case LockResultCode.OpenedWithKey:
        if (local) p.setLocked(local, false);
        if (code === LockResultCode.OpenedWithKey) p.showNotification("Unlocked with the key.");
        break;
      case LockResultCode.BeginLockpick:
        this.lockpick = { refId: m.refId, sessionId: m.sessionId };
        p.openLockpickMenu(local, m.sessionId);
        break;
      case LockResultCode.Unlocked:
        this.lockpick = undefined;
        p.closeLockpickMenu(true);
        if (local) p.setLocked(local, false);
        break;
      case LockResultCode.Failed:
        // A pin broke; the menu stays open for the next attempt.
        break;
      case LockResultCode.NoPins:
      case LockResultCode.SessionExpired:
      case LockResultCode.NoSession:
        if (this.lockpick) {
          this.lockpick = undefined;
          p.closeLockpickMenu(false);
        }
        this.ctx.reportFailure(Fo4MsgType.LockpickAttempt, LockResultCode[code]);
        break;
      default:
        this.ctx.reportFailure(Fo4MsgType.LockpickAttempt, LockResultCode[code] ?? "Failed");
    }
    this.ctx.events.emit("lockOutcome", { refId: m.refId, outcome: m.outcome, xp: m.xp });
  }

  private onTerminal(m: TerminalActionMessage): void {
    const p = this.ctx.platform;
    const local = p.refs.toLocal(m.refId);
    const code = m.outcome as HackResultCode;
    switch (code) {
      case HackResultCode.Open:
        if (local) p.openTerminal(local);
        break;
      case HackResultCode.BeginHack:
        this.hack = { refId: m.refId, sessionId: m.sessionId };
        p.openHackingMenu(local, m.sessionId, m.attemptsLeft);
        break;
      case HackResultCode.Hacked:
        this.hack = undefined;
        p.closeHackingMenu(true);
        if (local) {
          p.setLocked(local, false);
          p.openTerminal(local);
        }
        break;
      case HackResultCode.WrongGuess:
        p.setHackingAttemptsLeft(m.attemptsLeft);
        break;
      case HackResultCode.LockoutStarted:
      case HackResultCode.LockedOut:
      case HackResultCode.NoSession:
        if (this.hack) {
          this.hack = undefined;
          p.closeHackingMenu(false);
        }
        this.ctx.reportFailure(Fo4MsgType.TerminalAction, code === HackResultCode.NoSession ? "NoSession" : "LockedOut");
        break;
      default:
        this.ctx.reportFailure(Fo4MsgType.TerminalAction, HackResultCode[code] ?? "Failed");
    }
    this.ctx.events.emit("hackOutcome", { refId: m.refId, outcome: m.outcome, attemptsLeft: m.attemptsLeft, xp: m.xp });
  }
}
