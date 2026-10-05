import { RequestOutcome } from "../../core/requestTracker";
import { PowerArmorActorState } from "../clientEvents";
import { ClientContext, failed } from "../context";
import { PaPhase, PaTransitionKind } from "../messages/codes";
import {
  PowerArmorStateMessage,
  PowerArmorTransitionMessage,
} from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";

export type PaOutcome = RequestOutcome<PowerArmorTransitionMessage>;

// F17: power armor transitions are a server-driven two-step handshake:
//   Enter/Exit request -> server reply (phase Entering/Exiting)
//   -> the client plays the vanilla sequence -> Ack -> server reply (In/Out)
// The server moves the pieces and the core between the frame and the
// wearer only on Ack, and rolls back on its own if no Ack arrives
// (PowerArmorTransition with error "Timeout").
export class PowerArmorService {
  private phase = PaPhase.Out;
  private frameRefId = 0;
  private busy = false;
  private states = new Map<number, PowerArmorActorState>();

  constructor(private readonly ctx: ClientContext) {
    ctx.router.on(Fo4MsgType.PowerArmorTransition, (m) => this.onTransition(m));
    ctx.router.on(Fo4MsgType.PowerArmorState, (m) => this.onState(m));
  }

  getPhase(): PaPhase {
    return this.phase;
  }

  getFrame(): number {
    return this.frameRefId;
  }

  isInPowerArmor(): boolean {
    return this.phase === PaPhase.In;
  }

  isBusy(): boolean {
    return this.busy;
  }

  stateOf(actorIdx: number): PowerArmorActorState | undefined {
    return this.states.get(actorIdx);
  }

  async enter(frameRefId: number): Promise<PaOutcome> {
    if (this.busy || this.phase === PaPhase.Entering || this.phase === PaPhase.Exiting) {
      return this.reject("InTransition");
    }
    if (this.phase === PaPhase.In) {
      return this.reject("AlreadyInPowerArmor");
    }
    this.busy = true;
    try {
      const nonce = this.ctx.requests.nextNonce();
      const r1 = await this.step(nonce, PaTransitionKind.Enter, frameRefId);
      if (!r1.ok) {
        return r1;
      }
      this.setLocalPhase(PaPhase.Entering, frameRefId);
      const frame = this.ctx.platform.refs.toLocal(frameRefId);
      const played = frame ? await this.ctx.platform.playPowerArmorEnter(frame) : false;
      if (!played) {
        // No Ack: the server times out and sends the rollback.
        return failed(Fo4MsgType.PowerArmorTransition, "Interrupted");
      }
      const r2 = await this.step(nonce, PaTransitionKind.Ack, frameRefId);
      if (!r2.ok) {
        this.snapTo(r2.reply ? r2.reply.phase : PaPhase.Out, frameRefId);
        return r2;
      }
      this.setLocalPhase(PaPhase.In, frameRefId);
      return r2;
    } finally {
      this.busy = false;
    }
  }

  async exit(): Promise<PaOutcome> {
    if (this.busy || this.phase === PaPhase.Entering || this.phase === PaPhase.Exiting) {
      return this.reject("InTransition");
    }
    if (this.phase !== PaPhase.In) {
      return this.reject("NotInPowerArmor");
    }
    this.busy = true;
    const frameRefId = this.frameRefId;
    try {
      const nonce = this.ctx.requests.nextNonce();
      const r1 = await this.step(nonce, PaTransitionKind.Exit, frameRefId);
      if (!r1.ok) {
        // The vanilla exit can't be blocked in the plugin: the player may
        // already be out, so put them back in
        this.snapTo(PaPhase.In, frameRefId);
        return r1;
      }
      this.setLocalPhase(PaPhase.Exiting, frameRefId);
      const played = await this.ctx.platform.playPowerArmorExit();
      if (!played) {
        return failed(Fo4MsgType.PowerArmorTransition, "Interrupted");
      }
      const r2 = await this.step(nonce, PaTransitionKind.Ack, frameRefId);
      if (!r2.ok) {
        this.snapTo(r2.reply ? r2.reply.phase : PaPhase.In, frameRefId);
        return r2;
      }
      this.setLocalPhase(PaPhase.Out, frameRefId);
      return r2;
    } finally {
      this.busy = false;
    }
  }

  reset(): void {
    this.phase = PaPhase.Out;
    this.frameRefId = 0;
    this.busy = false;
    this.states.clear();
  }

  forgetActor(actorIdx: number): void {
    this.states.delete(actorIdx);
  }

  // Re-applies the cached look to a puppet that just streamed in.
  applyTo(actorIdx: number): void {
    const s = this.states.get(actorIdx);
    if (s && !this.ctx.isLocalActor(actorIdx)) {
      this.applyRemote(s, undefined);
    }
  }

  private step(nonce: number, kind: PaTransitionKind, frameRefId: number): Promise<PaOutcome> {
    const promise = this.ctx.requests.track<PowerArmorTransitionMessage>(nonce, Fo4MsgType.PowerArmorTransition);
    this.ctx.send(Fo4MsgType.PowerArmorTransition, { nonce, kind, frameRefId });
    return promise.then((o) => {
      if (!o.ok) {
        this.ctx.reportFailure(Fo4MsgType.PowerArmorTransition, o.error);
      }
      return o;
    });
  }

  private reject(error: string): Promise<PaOutcome> {
    this.ctx.reportFailure(Fo4MsgType.PowerArmorTransition, error);
    return Promise.resolve(failed(Fo4MsgType.PowerArmorTransition, error));
  }

  private onTransition(m: PowerArmorTransitionMessage): void {
    if (m.nonce !== 0 && this.ctx.requests.isPending(m.nonce)) {
      this.ctx.requests.complete<PowerArmorTransitionMessage>(m.nonce, {
        ok: m.ok,
        error: m.ok ? "" : m.error,
        refId: m.frameRefId,
        items: [],
        reply: m,
      });
      return;
    }
    if (!m.ok && m.error === "Timeout") {
      // Server rollback after a missing Ack
      this.snapTo(m.phase, this.frameRefId);
      this.ctx.reportFailure(Fo4MsgType.PowerArmorTransition, "Timeout", true);
    }
  }

  private onState(m: PowerArmorStateMessage): void {
    const prev = this.states.get(m.actorIdx);
    const s: PowerArmorActorState = {
      actorIdx: m.actorIdx,
      frameRefId: m.frameRefId,
      phase: m.phase,
      frameBaseId: m.frameBaseId,
      pieces: m.pieces,
      coreBaseId: m.coreBaseId,
      coreCharge: m.coreCharge,
      unpowered: m.unpowered,
      jetpackCapable: m.jetpackCapable,
    };
    this.states.set(m.actorIdx, s);
    const p = this.ctx.platform;
    if (this.ctx.isLocalActor(m.actorIdx)) {
      // Server-forced changes (death, ejected core, gamemode) snap the
      // player; during our own handshake the handshake owns the phase.
      if (!this.busy && m.phase !== this.phase) {
        this.snapTo(m.phase, m.frameRefId);
      }
      const inside = m.phase === PaPhase.In;
      if (inside) {
        p.applyPowerArmorVisual(p.getPlayer(), {
          frameBaseId: m.frameBaseId,
          pieces: m.pieces,
          unpowered: m.unpowered,
        });
      }
      if (m.coreCharge !== undefined) {
        p.setFusionCoreCharge(m.coreCharge);
      }
      p.setJetpackEnabled(inside && m.jetpackCapable && !m.unpowered);
    } else {
      this.applyRemote(s, prev);
    }
    this.ctx.events.emit("powerArmorChanged", s);
  }

  private applyRemote(s: PowerArmorActorState, prev: PowerArmorActorState | undefined): void {
    const p = this.ctx.platform;
    const actor = p.refs.toLocal(s.actorIdx);
    if (!actor) {
      return;
    }
    const inside = s.phase === PaPhase.In || s.phase === PaPhase.Entering;
    const wasInside = prev ? prev.phase === PaPhase.In || prev.phase === PaPhase.Entering : false;
    if (!prev || inside !== wasInside) {
      p.setInPowerArmor(actor, p.refs.toLocal(s.frameRefId), inside);
    }
    if (inside) {
      p.applyPowerArmorVisual(actor, { frameBaseId: s.frameBaseId, pieces: s.pieces, unpowered: s.unpowered });
    }
  }

  private setLocalPhase(phase: PaPhase, frameRefId: number): void {
    this.phase = phase;
    this.frameRefId = phase === PaPhase.Out ? 0 : frameRefId;
  }

  private snapTo(phase: number, frameRefId: number): void {
    const p = this.ctx.platform;
    const inside = phase === PaPhase.In;
    p.setInPowerArmor(p.getPlayer(), p.refs.toLocal(frameRefId), inside);
    if (!inside) {
      p.setJetpackEnabled(false);
    }
    this.setLocalPhase(inside ? PaPhase.In : PaPhase.Out, frameRefId);
  }
}
