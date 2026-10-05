import { ClientContext } from "../context";
import { Av } from "../messages/codes";
import { AvValue, ChangeValuesAvMessage } from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";

// F11/F19: actor values are server-owned. The owner receives every value
// (current and max); neighbours receive health as a 0..1 fraction plus limb
// conditions, which is all a puppet needs for health bars and crippling.
export class ActorValueService {
  private own = new Map<number, AvValue>();
  private remote = new Map<number, Map<number, AvValue>>();

  constructor(private readonly ctx: ClientContext) {
    ctx.router.on(Fo4MsgType.ChangeValuesAv, (m) => this.onChangeValues(m));
  }

  get(avId: number): number {
    return this.own.get(avId)?.current ?? 0;
  }

  getMax(avId: number): number {
    return this.own.get(avId)?.max ?? 0;
  }

  healthFraction(actorIdx: number): number | undefined {
    if (this.ctx.isLocalActor(actorIdx)) {
      const hp = this.own.get(Av.Health);
      return hp && hp.max > 0 ? hp.current / hp.max : undefined;
    }
    return this.remote.get(actorIdx)?.get(Av.Health)?.current;
  }

  remoteValue(actorIdx: number, avId: number): number | undefined {
    return this.remote.get(actorIdx)?.get(avId)?.current;
  }

  forgetActor(actorIdx: number): void {
    this.remote.delete(actorIdx);
  }

  reset(): void {
    this.own.clear();
    this.remote.clear();
  }

  private onChangeValues(m: ChangeValuesAvMessage): void {
    const p = this.ctx.platform;
    if (this.ctx.isLocalActor(m.idx)) {
      const player = p.getPlayer();
      for (const v of m.values) {
        const prev = this.own.get(v.avId);
        // Max first: setting current above the old max would clamp.
        if (!prev || prev.max !== v.max) {
          p.setActorValueMax(player, v.avId, v.max);
        }
        if (!prev || prev.current !== v.current) {
          p.setActorValueCurrent(player, v.avId, v.current);
        }
        this.own.set(v.avId, { ...v });
      }
    } else {
      const map = this.remote.get(m.idx) ?? new Map<number, AvValue>();
      this.remote.set(m.idx, map);
      const local = p.refs.toLocal(m.idx);
      for (const v of m.values) {
        map.set(v.avId, { ...v });
        if (!local) {
          continue; // applied when the actor streams in (applyRemoteTo)
        }
        if (v.avId === Av.Health) {
          p.setHealthFraction(local, v.current);
        } else {
          p.setActorValueCurrent(local, v.avId, v.current);
        }
      }
    }
    this.ctx.events.emit("actorValuesChanged", { actorIdx: m.idx, values: m.values });
  }

  // Re-applies cached values to a puppet that just streamed in.
  applyRemoteTo(actorIdx: number): void {
    const map = this.remote.get(actorIdx);
    const local = this.ctx.platform.refs.toLocal(actorIdx);
    if (!map || !local) {
      return;
    }
    for (const v of map.values()) {
      if (v.avId === Av.Health) {
        this.ctx.platform.setHealthFraction(local, v.current);
      } else {
        this.ctx.platform.setActorValueCurrent(local, v.avId, v.current);
      }
    }
  }
}
