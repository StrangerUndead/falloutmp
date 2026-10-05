import { ClientContext } from "../context";
import { ActiveEffectEntry, EffectsUpdateMessage } from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";

interface OwnEffect extends ActiveEffectEntry {
  receivedAtMs: number;
}

// F20: active effects and addictions. The owner gets magnitudes and
// remaining time for the Pip-Boy/HUD list; puppets get the effect ids so the
// plugin can play the matching visuals.
export class EffectsService {
  private own: OwnEffect[] = [];
  private addictions: number[] = [];
  private remoteVisuals = new Map<number, string>();

  constructor(private readonly ctx: ClientContext) {
    ctx.router.on(Fo4MsgType.EffectsUpdate, (m) => this.onUpdate(m));
  }

  // Active effects with remaining time counted down locally.
  active(): ActiveEffectEntry[] {
    const now = this.ctx.platform.nowMs();
    return this.own.map((e) => ({
      effectId: e.effectId,
      sourceItem: e.sourceItem,
      kind: e.kind,
      avId: e.avId,
      magnitude: e.magnitude,
      remainingMs: Math.max(0, e.remainingMs - (now - e.receivedAtMs)),
    }));
  }

  activeAddictions(): number[] {
    return this.addictions.slice();
  }

  isAddicted(id: number): boolean {
    return this.addictions.includes(id);
  }

  forgetActor(actorIdx: number): void {
    this.remoteVisuals.delete(actorIdx);
  }

  reset(): void {
    this.own = [];
    this.addictions = [];
    this.remoteVisuals.clear();
  }

  private onUpdate(m: EffectsUpdateMessage): void {
    if (this.ctx.isLocalActor(m.idx)) {
      const now = this.ctx.platform.nowMs();
      const previous = new Set(this.addictions);
      this.own = m.effects.map((e) => ({ ...e, receivedAtMs: now }));
      this.addictions = m.addictions.slice();
      for (const a of this.addictions) {
        if (!previous.has(a)) {
          this.ctx.platform.showNotification("You have become addicted.");
          this.ctx.events.emit("becameAddicted", { addictionId: a });
        }
      }
    } else {
      // Only re-apply visuals when the set of sources changed
      const sources = Array.from(new Set(m.effects.map((e) => e.sourceItem))).sort((a, b) => a - b);
      const key = sources.join(",");
      const local = this.ctx.platform.refs.toLocal(m.idx);
      if (local && this.remoteVisuals.get(m.idx) !== key) {
        this.ctx.platform.applyEffectVisuals(local, sources);
        this.remoteVisuals.set(m.idx, key);
      }
    }
    this.ctx.events.emit("effectsChanged", { actorIdx: m.idx, effects: m.effects, addictions: m.addictions });
  }
}
