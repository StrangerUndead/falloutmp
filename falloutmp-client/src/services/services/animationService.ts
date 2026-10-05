import { ClientContext } from "../context";
import { GraphVariable, UpdateActionsMessage, UpdateGraphVariablesMessage } from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";

// F02: animation sync (docs/falloutmp/features/F02-animation.md).
// Owner: graph variables of the player (and hosted NPCs) go out at 15 Hz
// when they change, a full set every second; whitelisted graph events go
// out reliable, batched per frame. Remote: events are replayed with
// notifyAnimationGraph, variables written to the puppet's graph (the
// plugin keeps them applied after every engine update until replaced).
export interface AnimationOptions {
  variableIntervalMs?: number;
  fullSnapshotMs?: number;
  // Variable names read on the owner (T0 movement + T1 state, reference
  // fo4-animation-sync.md §4.5). Names a graph lacks are skipped.
  variables?: string[];
}

export const kDefaultSyncedVariables = [
  // T0 movement
  "Speed", "Direction", "TurnDelta", "VelocityZ", "AimPitchCurrent", "AimHeadingCurrent",
  "iSyncIdleLocomotion", "iSyncTurnState", "iSyncWalkRun", "iSyncSneakWalkRun", "iSyncLocomotionSpeed",
  "iSyncDirection", "iSyncForwardBackward", "iSyncJumpState", "iSyncSprintState",
  "bInJumpState", "IsSneaking", "IsSprinting",
  // T1 state
  "iSyncSightedState", "iSyncGunDown", "iSyncReadyAlertRelaxed", "bAimActive", "bAimEnabled",
  "IsAttackReady", "iAttackState", "iMeleeState", "bIsThrowing", "IsBlocking", "iWantBlock",
  "IsStaggering", "staggerMagnitude", "staggerDirection", "iGetUpType", "weaponSpeedMult",
  "ReloadSpeedMult", "iWeaponChargeMode",
];

// Events that drive gameplay on the owner. Remote actors never replay
// them: shots, reloads and throws are shown by the combat service.
const kNeverReplay = new Set(
  ["weaponfire", "reloadcomplete", "throwend", "weaponswing", "event00"].map((n) => n.toLowerCase()),
);

interface Owned {
  seq: number;
  lastSendMs: number;
  lastFullMs: number;
  last: Map<string, number>;
  actionSeq: number;
  pendingEvents: string[];
}

interface Remote {
  lastVarSeq: number;
  lastActionSeq: number;
  values: Map<string, GraphVariable>;
}

export class AnimationService {
  private owned = new Map<number, Owned>(); // by server id (0 = me)
  private remotes = new Map<number, Remote>();
  private readonly o: Required<AnimationOptions>;
  replayedCount = 0;

  constructor(private readonly ctx: ClientContext, opts: AnimationOptions = {}) {
    this.o = {
      variableIntervalMs: opts.variableIntervalMs ?? 66,
      fullSnapshotMs: opts.fullSnapshotMs ?? 1000,
      variables: opts.variables ?? kDefaultSyncedVariables,
    };
    ctx.router.on(Fo4MsgType.UpdateActions, (m) => this.onActions(m));
    ctx.router.on(Fo4MsgType.UpdateGraphVariables, (m) => this.onVariables(m));
  }

  // Platform "animationEvent" for the player or a hosted NPC.
  onLocalEvent(actor: number, name: string): void {
    const p = this.ctx.platform;
    let serverId = 0;
    if (actor !== p.getPlayer()) {
      serverId = p.refs.toServer(actor);
      if (!serverId || !this.ctx.isHosted(serverId)) {
        return; // not an actor this client simulates
      }
    }
    this.ownedOf(serverId).pendingEvents.push(name);
  }

  tickOwner(): void {
    const p = this.ctx.platform;
    this.tickOwned(0, p.getPlayer());
    for (const npc of this.ctx.session.hosted) {
      const local = p.refs.toLocal(npc);
      if (local) {
        this.tickOwned(npc, local);
      }
    }
  }

  tickRemotes(): void {
    // Variables are sticky in the plugin; nothing per frame here.
  }

  // A puppet (re)appeared: give its graph the last known state.
  applyTo(serverId: number): void {
    const r = this.remotes.get(serverId);
    const local = this.ctx.platform.refs.toLocal(serverId);
    if (r && local && r.values.size) {
      this.ctx.platform.setGraphVariables(local, Array.from(r.values.values()));
    }
  }

  forgetActor(serverId: number): void {
    this.remotes.delete(serverId);
  }

  reset(): void {
    this.owned.clear();
    this.remotes.clear();
  }

  private ownedOf(serverId: number): Owned {
    let o = this.owned.get(serverId);
    if (!o) {
      o = { seq: 0, lastSendMs: -1e12, lastFullMs: -1e12, last: new Map(), actionSeq: 0, pendingEvents: [] };
      this.owned.set(serverId, o);
    }
    return o;
  }

  private tickOwned(serverId: number, local: number): void {
    const p = this.ctx.platform;
    const o = this.ownedOf(serverId);
    if (o.pendingEvents.length) {
      o.actionSeq = (o.actionSeq + 1) >>> 0;
      // At most 16 per message (server limit)
      while (o.pendingEvents.length) {
        const events = o.pendingEvents.splice(0, 16);
        this.ctx.send(Fo4MsgType.UpdateActions, { idx: serverId, seq: o.actionSeq, ts: 0, events }, true);
      }
    }
    const now = p.nowMs();
    if (now - o.lastSendMs < this.o.variableIntervalMs) {
      return;
    }
    const values = p.getGraphVariables(local, this.o.variables);
    if (!values.length) {
      return;
    }
    const full = now - o.lastFullMs >= this.o.fullSnapshotMs;
    const changed = full ? values : values.filter((v) => o.last.get(v.name) !== v.value);
    if (!changed.length) {
      return;
    }
    for (const v of values) {
      o.last.set(v.name, v.value);
    }
    o.lastSendMs = now;
    if (full) {
      o.lastFullMs = now;
    }
    o.seq = (o.seq + 1) & 0xffff;
    this.ctx.send(Fo4MsgType.UpdateGraphVariables, { idx: serverId, seq: o.seq, ts: 0, values: changed }, false);
  }

  private remoteOf(idx: number): Remote | undefined {
    if (this.ctx.isLocalActor(idx) || this.ctx.isHosted(idx)) {
      return undefined; // we drive it ourselves
    }
    let r = this.remotes.get(idx);
    if (!r) {
      r = { lastVarSeq: -1, lastActionSeq: -1, values: new Map() };
      this.remotes.set(idx, r);
    }
    return r;
  }

  private onActions(m: UpdateActionsMessage): void {
    const r = this.remoteOf(m.idx);
    if (!r) {
      return;
    }
    r.lastActionSeq = m.seq;
    const local = this.ctx.platform.refs.toLocal(m.idx);
    if (!local) {
      return;
    }
    for (const name of m.events) {
      if (kNeverReplay.has(name.toLowerCase())) {
        continue;
      }
      this.ctx.platform.notifyAnimationGraph(local, name);
      this.replayedCount++;
    }
  }

  private onVariables(m: UpdateGraphVariablesMessage): void {
    const r = this.remoteOf(m.idx);
    if (!r) {
      return;
    }
    // Unreliable: drop reordered packets (16-bit sequence)
    if (r.lastVarSeq >= 0 && ((m.seq - r.lastVarSeq) << 16) >> 16 <= 0) {
      return;
    }
    r.lastVarSeq = m.seq;
    for (const v of m.values) {
      r.values.set(v.name, v);
    }
    const local = this.ctx.platform.refs.toLocal(m.idx);
    if (local) {
      this.ctx.platform.setGraphVariables(local, m.values);
    }
  }
}
