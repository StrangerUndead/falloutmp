import { RequestOutcome } from "../../core/requestTracker";
import { ProgressionState } from "../clientEvents";
import { ClientContext, failed } from "../context";
import { ProgressionOp } from "../messages/codes";
import { ProgressionUpdateMessage, SpecialArray } from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";

export interface ProgressionRules {
  creationPoints: number; // server setting progression.creationPoints
  specialMax: number;
}

// F19: level, XP, perks and SPECIAL are server-owned. The client mirrors
// them into the Pip-Boy and pre-checks requests to avoid pointless trips;
// the server re-validates everything.
export class ProgressionService {
  private state: ProgressionState | undefined;
  private appliedPerks = new Set<number>();
  private creationPrompted = false;

  constructor(
    private readonly ctx: ClientContext,
    readonly rules: ProgressionRules = { creationPoints: 21, specialMax: 10 },
  ) {
    ctx.router.on(Fo4MsgType.ProgressionUpdate, (m) => this.onUpdate(m));
  }

  get(): ProgressionState | undefined {
    return this.state;
  }

  hasPerk(perkId: number): boolean {
    return this.appliedPerks.has(perkId);
  }

  validateSpecial(special: SpecialArray): string {
    let sum = 0;
    for (const v of special) {
      if (!Number.isInteger(v) || v < 1 || v > this.rules.specialMax) {
        return "BadSpecialAllocation";
      }
      sum += v;
    }
    return sum === 7 + this.rules.creationPoints ? "" : "BadSpecialAllocation";
  }

  createCharacter(special: SpecialArray): Promise<RequestOutcome> {
    const err = this.state?.created ? "AlreadyCreated" : this.validateSpecial(special);
    if (err) {
      return this.reject(err);
    }
    return this.ctx.request(Fo4MsgType.ProgressionRequest, {
      op: ProgressionOp.CreateCharacter,
      special: special.slice() as SpecialArray,
    });
  }

  // chartKey: the perk chart entry, e.g. "GunNut" (server perk chart).
  buyPerk(chartKey: string): Promise<RequestOutcome> {
    if (this.state && this.state.perkPoints <= 0) {
      return this.reject("NoPerkPoints");
    }
    return this.ctx.request(Fo4MsgType.ProgressionRequest, { op: ProgressionOp.BuyPerk, chartKey });
  }

  buySpecial(specialAv: number): Promise<RequestOutcome> {
    if (this.state && this.state.perkPoints <= 0) {
      return this.reject("NoPerkPoints");
    }
    return this.ctx.request(Fo4MsgType.ProgressionRequest, { op: ProgressionOp.BuySpecial, specialAv });
  }

  reset(): void {
    this.state = undefined;
    this.appliedPerks.clear();
    this.creationPrompted = false;
  }

  private reject(error: string): Promise<RequestOutcome> {
    this.ctx.reportFailure(Fo4MsgType.ProgressionRequest, error);
    return Promise.resolve(failed(Fo4MsgType.ProgressionRequest, error));
  }

  private onUpdate(m: ProgressionUpdateMessage): void {
    const p = this.ctx.platform;
    const prev = this.state;
    const next: ProgressionState = {
      level: m.level,
      xp: m.xp,
      xpForNextLevel: m.xpForNextLevel,
      perkPoints: m.perkPoints,
      special: m.special,
      perks: m.perks,
      created: m.created,
    };
    this.state = next;

    p.setPlayerLevelAndXp(m.level, m.xp, m.xpForNextLevel);
    if (!prev || prev.perkPoints !== m.perkPoints) {
      p.setPerkPoints(m.perkPoints);
    }
    const want = new Set(m.perks);
    for (const id of Array.from(this.appliedPerks)) {
      if (!want.has(id)) {
        p.removePerk(id);
        this.appliedPerks.delete(id);
      }
    }
    for (const id of m.perks) {
      if (!this.appliedPerks.has(id)) {
        p.addPerk(id);
        this.appliedPerks.add(id);
      }
    }

    this.ctx.events.emit("progressionChanged", next);
    if (prev && m.level > prev.level) {
      p.showNotification(`Level up! You are now level ${m.level}.`);
      this.ctx.events.emit("levelUp", { level: m.level, perkPoints: m.perkPoints });
    }
    if (!m.created && !this.creationPrompted) {
      this.creationPrompted = true;
      p.openSpecialMenu();
      this.ctx.events.emit("characterCreationRequired", {});
    }
  }
}
