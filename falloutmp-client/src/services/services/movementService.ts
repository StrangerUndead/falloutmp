import { ClientContext } from "../context";
import { UpdateMovementFo4Message, Vec3 } from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";
import { LocalMovement } from "../../platform/falloutPlatform";

export interface MovementOptions {
  sendIntervalMs?: number; // owner capture rate (F01: 100 ms)
  interpolationDelayMs?: number; // jitter buffer (F01: start at 120 ms)
  maxExtrapolationMs?: number; // then hold (F01: 200 ms)
  snapDistance?: number; // hard teleport above this error (F01: 512 u)
  bufferSize?: number;
}

interface RemoteSample {
  serverMs: number;
  pos: Vec3;
  yaw: number;
  worldOrCell: number;
  flags: number;
  aimPitch: number;
  aimHeading: number;
}

interface RemoteActor {
  samples: RemoteSample[];
  clockOffset: number; // local ms - server ms (running minimum)
  applied?: { pos: Vec3; worldOrCell: number };
  appliedFlags: number;
  lastSeq: number;
  lastRawTs: number;
  tsWraps: number; // ts is u32 server ms; unwrap across the 49-day wrap
}

// F01: owner capture and remote replay.
// Owner: the player's transform goes out unreliable every 100 ms, and at
// once when a movement flag changes (sneak, sprint, power armor, ...).
// Remote: samples are timestamped by the server; each actor is rendered
// interpolationDelayMs in the past on the server's timeline, extrapolated
// for at most maxExtrapolationMs, and snapped on cell changes or large
// errors.
interface OwnedActor {
  seq: number;
  lastSendMs: number;
  lastFlags: number;
  paused: boolean;
}

export class MovementService {
  private owned = new Map<number, OwnedActor>(); // by server id
  private remotes = new Map<number, RemoteActor>();
  private readonly o: Required<MovementOptions>;
  sentCount = 0;

  constructor(private readonly ctx: ClientContext, opts: MovementOptions = {}) {
    this.o = {
      sendIntervalMs: opts.sendIntervalMs ?? 100,
      interpolationDelayMs: opts.interpolationDelayMs ?? 120,
      maxExtrapolationMs: opts.maxExtrapolationMs ?? 200,
      snapDistance: opts.snapDistance ?? 512,
      bufferSize: opts.bufferSize ?? 20,
    };
    ctx.router.on(Fo4MsgType.UpdateMovementFo4, (m) => this.onRemote(m));
  }

  // Owner side, every frame: the player and every NPC this client hosts.
  tickOwner(): void {
    const p = this.ctx.platform;
    const me = this.ctx.session.localActorId;
    this.tickOwned(me, p.getPlayer());
    for (const npc of this.ctx.session.hosted) {
      const local = p.refs.toLocal(npc);
      if (local) {
        this.tickOwned(npc, local);
      }
    }
    for (const id of Array.from(this.owned.keys())) {
      if (id !== me && !this.ctx.isHosted(id)) {
        this.owned.delete(id); // hosting ended
      }
    }
  }

  private tickOwned(serverId: number, local: number): void {
    const p = this.ctx.platform;
    let o = this.owned.get(serverId);
    if (!o) {
      o = { seq: 0, lastSendMs: -1e12, lastFlags: -1, paused: true };
      this.owned.set(serverId, o);
    }
    const cur = p.getMovementFo4(local);
    if (!cur) {
      o.paused = true; // loading screen / unloaded: stop sending
      return;
    }
    const now = p.nowMs();
    const flagsChanged = cur.flags !== o.lastFlags;
    if (!o.paused && !flagsChanged && now - o.lastSendMs < this.o.sendIntervalMs) {
      return;
    }
    o.paused = false;
    this.send(serverId, o, cur, now);
  }

  // Remote side, every frame.
  tickRemotes(): void {
    const p = this.ctx.platform;
    const now = p.nowMs();
    for (const [idx, r] of this.remotes) {
      if (this.ctx.isHosted(idx)) {
        continue;
      }
      const local = p.refs.toLocal(idx);
      if (!local || !r.samples.length) {
        continue;
      }
      const renderServerMs = now - r.clockOffset - this.o.interpolationDelayMs;
      const target = this.sampleAt(r, renderServerMs);
      const last = r.samples[r.samples.length - 1];
      const cellChanged = !r.applied || r.applied.worldOrCell !== target.worldOrCell;
      if (cellChanged || dist(r.applied!.pos, target.pos) > this.o.snapDistance) {
        p.teleportActor(local, target.pos, target.yaw, target.worldOrCell);
      } else {
        p.setActorTransform(local, target.pos, target.yaw);
      }
      r.applied = { pos: target.pos, worldOrCell: target.worldOrCell };
      if (last.flags !== r.appliedFlags) {
        p.setMovementFlags(local, last.flags);
        r.appliedFlags = last.flags;
      }
      p.setAimAngles(local, target.aimPitch, target.aimHeading);
    }
  }

  // Newest known transform of a remote actor.
  lastPosition(idx: number): { pos: Vec3; yaw: number; worldOrCell: number } | undefined {
    const s = this.remotes.get(idx)?.samples;
    const last = s && s[s.length - 1];
    return last ? { pos: last.pos, yaw: last.yaw, worldOrCell: last.worldOrCell } : undefined;
  }

  // A new puppet for the same actor: the next frame teleports it into place.
  forgetApplied(idx: number): void {
    const r = this.remotes.get(idx);
    if (r) {
      r.applied = undefined;
      r.appliedFlags = -1;
    }
  }

  // The world view calls this when a puppet is destroyed or respawned, so
  // the next sample snaps.
  forgetActor(idx: number): void {
    this.remotes.delete(idx);
  }

  reset(): void {
    this.remotes.clear();
    this.owned.clear();
  }

  // A hosted NPC stops being a puppet: its remote buffer is dropped.
  onHostStart(serverId: number): void {
    this.remotes.delete(serverId);
  }

  remoteCount(): number {
    return this.remotes.size;
  }

  private send(serverId: number, o: OwnedActor, cur: LocalMovement, now: number): void {
    o.seq = (o.seq + 1) & 0xffff;
    o.lastSendMs = now;
    o.lastFlags = cur.flags;
    this.sentCount++;
    this.ctx.send(
      Fo4MsgType.UpdateMovementFo4,
      {
        idx: serverId,
        seq: o.seq,
        ts: now >>> 0,
        worldOrCell: cur.worldOrCell,
        pos: cur.pos,
        yaw: cur.yaw,
        aimPitch: cur.aimPitch,
        aimHeading: cur.aimHeading,
        speed: cur.speed,
        direction: cur.direction,
        velZ: cur.velZ,
        flags: cur.flags,
      },
      false,
    );
  }

  private onRemote(m: UpdateMovementFo4Message): void {
    if (this.ctx.isLocalActor(m.idx) || this.ctx.isHosted(m.idx)) {
      return; // we simulate it ourselves
    }
    const now = this.ctx.platform.nowMs();
    let r = this.remotes.get(m.idx);
    if (!r) {
      r = { samples: [], clockOffset: now - m.ts, appliedFlags: -1, lastSeq: (m.seq - 1) & 0xffff, lastRawTs: m.ts, tsWraps: 0 };
      this.remotes.set(m.idx, r);
    }
    // Unreliable channel: drop reordered and duplicate samples
    const delta = ((m.seq - r.lastSeq) << 16) >> 16;
    if (delta <= 0) {
      return;
    }
    r.lastSeq = m.seq;
    if (m.ts < r.lastRawTs - 0x80000000) {
      r.tsWraps++;
    }
    r.lastRawTs = m.ts;
    const serverMs = m.ts + r.tsWraps * 0x100000000;
    // Minimum of (arrival - server time) tracks clock offset + best latency
    r.clockOffset = Math.min(r.clockOffset, now - serverMs);
    r.samples.push({
      serverMs,
      pos: m.pos,
      yaw: m.yaw,
      worldOrCell: m.worldOrCell,
      flags: m.flags,
      aimPitch: m.aimPitch,
      aimHeading: m.aimHeading,
    });
    if (r.samples.length > this.o.bufferSize) {
      r.samples.splice(0, r.samples.length - this.o.bufferSize);
    }
  }

  private sampleAt(r: RemoteActor, t: number): RemoteSample {
    const s = r.samples;
    if (t <= s[0].serverMs) {
      return s[0];
    }
    for (let i = 1; i < s.length; i++) {
      if (s[i].serverMs >= t) {
        const a = s[i - 1];
        const b = s[i];
        if (a.worldOrCell !== b.worldOrCell) {
          return b;
        }
        const k = (t - a.serverMs) / Math.max(1, b.serverMs - a.serverMs);
        return { ...b, pos: lerp3(a.pos, b.pos, k), yaw: lerpAngle(a.yaw, b.yaw, k) };
      }
    }
    // Past the newest sample: extrapolate from the last two, then hold
    const b = s[s.length - 1];
    if (s.length < 2) {
      return b;
    }
    const a = s[s.length - 2];
    const span = b.serverMs - a.serverMs;
    if (span <= 0 || a.worldOrCell !== b.worldOrCell) {
      return b;
    }
    const ahead = Math.min(t - b.serverMs, this.o.maxExtrapolationMs);
    const k = 1 + ahead / span;
    return { ...b, pos: lerp3(a.pos, b.pos, k) };
  }
}

function lerp3(a: Vec3, b: Vec3, k: number): Vec3 {
  return [a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k, a[2] + (b[2] - a[2]) * k];
}

// Shortest way around the circle, in degrees.
export function lerpAngle(a: number, b: number, k: number): number {
  let d = ((b - a) % 360 + 540) % 360 - 180;
  if (d === -180) d = 180;
  return (((a + d * k) % 360) + 360) % 360;
}

function dist(a: Vec3, b: Vec3): number {
  const dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return Math.sqrt(dx * dx + dy * dy + dz * dz);
}
