import { RequestOutcome } from "../../core/requestTracker";
import { ClientContext } from "../context";
import {
  DamageAppliedMessage,
  Vec3,
  WeaponFireMessage,
  WeaponReloadMessage,
} from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";

interface PendingHit {
  projectileIndex: number;
  targetIdx: number;
  limb: number;
  atMs: number;
}

export interface CombatOptions {
  // How long a shot id stays claimable (server shot log is a bit longer).
  shotTtlMs?: number;
}

// F09/F11: shots go to the server, which spends ammo, assigns a sequence
// number and relays the shot to neighbours. The shooter gets the relay too,
// carrying its own clientShotId, so hit claims can name the server shot.
// Damage is computed on the server and returned as DamageApplied.
export class CombatService {
  private nextShotId = 1;
  private shotSeq = new Map<number, { seq: number; atMs: number }>();
  private pendingHits = new Map<number, PendingHit[]>();
  private readonly shotTtlMs: number;

  constructor(private readonly ctx: ClientContext, opts: CombatOptions = {}) {
    this.shotTtlMs = opts.shotTtlMs ?? 3000;
    ctx.router.on(Fo4MsgType.WeaponFire, (m) => this.onFire(m));
    ctx.router.on(Fo4MsgType.WeaponReload, (m) => this.onReload(m));
    ctx.router.on(Fo4MsgType.DamageApplied, (m) => this.onDamage(m));
  }

  // Returns the local shot id the platform uses for later hit reports.
  fire(weaponBaseId: number, origin: Vec3, direction: Vec3): number {
    const id = this.nextShotId;
    this.nextShotId = (this.nextShotId + 1) >>> 0 || 1;
    this.ctx.send(
      Fo4MsgType.WeaponFire,
      { weaponBaseId, origin, direction, clientShotId: id },
      false,
    );
    return id;
  }

  // A projectile of a local shot hit something. Held until the server's
  // sequence number for the shot is known.
  reportHit(localShotId: number, projectileIndex: number, targetIdx: number, limb: number): void {
    const known = this.shotSeq.get(localShotId);
    if (known) {
      this.sendHit(known.seq, projectileIndex, targetIdx, limb);
      return;
    }
    const list = this.pendingHits.get(localShotId) ?? [];
    list.push({ projectileIndex, targetIdx, limb, atMs: this.ctx.platform.nowMs() });
    this.pendingHits.set(localShotId, list);
  }

  reload(): Promise<RequestOutcome<WeaponReloadMessage>> {
    const nonce = this.ctx.requests.nextNonce();
    const promise = this.ctx.requests.track<WeaponReloadMessage>(nonce, Fo4MsgType.WeaponReload);
    this.ctx.send(Fo4MsgType.WeaponReload, { nonce });
    return promise.then((r) => {
      if (r.ok && r.reply) {
        this.ctx.platform.setAmmoLoaded(this.ctx.platform.getPlayer(), r.reply.loaded);
      } else if (!r.ok) {
        this.ctx.reportFailure(Fo4MsgType.WeaponReload, r.error, true);
      }
      return r;
    });
  }

  tick(): void {
    const now = this.ctx.platform.nowMs();
    for (const [id, s] of Array.from(this.shotSeq.entries())) {
      if (now - s.atMs > this.shotTtlMs) {
        this.shotSeq.delete(id);
      }
    }
    for (const [id, hits] of Array.from(this.pendingHits.entries())) {
      if (hits.length && now - hits[0].atMs > this.shotTtlMs) {
        this.pendingHits.delete(id); // the shot was rejected or lost
      }
    }
  }

  reset(): void {
    this.shotSeq.clear();
    this.pendingHits.clear();
  }

  pendingHitCount(): number {
    let n = 0;
    for (const h of this.pendingHits.values()) n += h.length;
    return n;
  }

  private sendHit(shotSeq: number, projectileIndex: number, targetIdx: number, limb: number): void {
    this.ctx.send(Fo4MsgType.HitReport, { shotSeq, projectileIndex, targetIdx, limb });
  }

  private onFire(m: WeaponFireMessage): void {
    if (this.ctx.isLocalActor(m.shooterIdx)) {
      if (!m.clientShotId) {
        return;
      }
      this.shotSeq.set(m.clientShotId, { seq: m.seq, atMs: this.ctx.platform.nowMs() });
      const hits = this.pendingHits.get(m.clientShotId);
      if (hits) {
        this.pendingHits.delete(m.clientShotId);
        for (const h of hits) {
          this.sendHit(m.seq, h.projectileIndex, h.targetIdx, h.limb);
        }
      }
      return;
    }
    const shooter = this.ctx.platform.refs.toLocal(m.shooterIdx);
    if (shooter) {
      this.ctx.platform.playRemoteShot(shooter, m.weaponBaseId, m.origin, m.direction);
    }
  }

  private onReload(m: WeaponReloadMessage): void {
    this.ctx.requests.complete<WeaponReloadMessage>(m.nonce, {
      ok: m.ok,
      error: m.ok ? "" : "ReloadRejected",
      refId: 0,
      items: [],
      reply: m,
    });
  }

  private onDamage(m: DamageAppliedMessage): void {
    const p = this.ctx.platform;
    const target = this.ctx.localActor(m.targetIdx);
    if (target) {
      p.playHitReaction(target, m.limb, m.total, m.critical);
    }
    this.ctx.events.emit("damageApplied", {
      targetIdx: m.targetIdx,
      aggressorIdx: m.aggressorIdx,
      total: m.total,
      limb: m.limb,
      critical: m.critical,
      killed: m.killed,
    });
    if (m.killed) {
      if (this.ctx.isLocalActor(m.targetIdx)) {
        // The player's death and respawn are driven by the upstream
        // death-state sync; the UI just gets told who did it.
        this.ctx.events.emit("localPlayerKilled", { aggressorIdx: m.aggressorIdx });
      } else if (target) {
        p.killActor(target, this.ctx.localActor(m.aggressorIdx));
      }
    }
  }
}
