import { RequestOutcome } from "../../core/requestTracker";
import { ClientContext, failed } from "../context";
import {
  hasPerm,
  WireOp,
  WorkshopEditOp,
  WorkshopManageOp,
  WorkshopObjectsKind,
  WorkshopPerm,
} from "../messages/codes";
import {
  Vec3,
  WorkshopModeMessage,
  WorkshopObject,
  WorkshopObjectsMessage,
  WorkshopStateMessage,
  WorkshopWireEntry,
  WorkshopWireMessage,
} from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";

interface PlacedView {
  obj: WorkshopObject;
  local: number;
}

interface WireView {
  entry: WorkshopWireEntry;
  local: number;
}

interface SettlementView {
  version: number;
  hasSnapshot: boolean;
  objects: Map<number, PlacedView>;
  wires: Map<number, WireView>;
  scrappedPrePlaced: Set<number>;
  pendingChunks?: { version: number; chunkCount: number; parts: Map<number, WorkshopObjectsMessage> };
  state?: WorkshopStateMessage;
}

export interface PlaceArgs {
  recipeId: number;
  baseId: number;
  fromStored?: boolean;
  pos: Vec3;
  rot: Vec3;
  scale?: number;
  snapTargetRefId?: number;
}

export const kMaxEditItems = 64; // server limit per WorkshopEdit

// F22: settlements are server-owned. Build mode is granted by the server
// with a permission mask; every placement, edit and wire is a request; the
// resulting objects arrive for everyone (builder included) as
// WorkshopObjects deltas or chunked snapshots, which this service mirrors
// into spawned local references.
export class WorkshopService {
  private settlements = new Map<number, SettlementView>();
  private localToServer = new Map<number, number>();
  private activeWorkshop = 0;
  private activePerms = 0;
  private pendingEnter = new Map<number, (ok: RequestOutcome) => void>();

  constructor(private readonly ctx: ClientContext) {
    ctx.router.on(Fo4MsgType.WorkshopMode, (m) => this.onMode(m));
    ctx.router.on(Fo4MsgType.WorkshopObjects, (m) => this.onObjects(m));
    ctx.router.on(Fo4MsgType.WorkshopState, (m) => this.onState(m));
    ctx.router.on(Fo4MsgType.WorkshopWire, (m) => this.onWireRelay(m));
  }

  // Queries ------------------------------------------------------------------

  activeWorkshopId(): number {
    return this.activeWorkshop;
  }

  activePermissions(): number {
    return this.activePerms;
  }

  stateOf(workshopRefId: number): WorkshopStateMessage | undefined {
    return this.settlements.get(workshopRefId)?.state;
  }

  objectsOf(workshopRefId: number): WorkshopObject[] {
    const s = this.settlements.get(workshopRefId);
    return s ? Array.from(s.objects.values()).map((v) => v.obj) : [];
  }

  wiresOf(workshopRefId: number): WorkshopWireEntry[] {
    const s = this.settlements.get(workshopRefId);
    return s ? Array.from(s.wires.values()).map((v) => v.entry) : [];
  }

  versionOf(workshopRefId: number): number {
    return this.settlements.get(workshopRefId)?.version ?? 0;
  }

  // Server id of a local reference: placed objects first, then plugin refs.
  serverIdOf(local: number): number {
    return this.localToServer.get(local) ?? this.ctx.platform.refs.toServer(local);
  }

  // Server id of a wire from the local id spawnWire returned.
  wireServerIdOf(local: number): number {
    for (const s of this.settlements.values()) {
      for (const [serverId, w] of s.wires) {
        if (w.local === local) {
          return serverId;
        }
      }
    }
    return 0;
  }

  localIdOf(serverId: number): number {
    for (const s of this.settlements.values()) {
      const v = s.objects.get(serverId);
      if (v) {
        return v.local;
      }
    }
    return this.ctx.platform.refs.toLocal(serverId);
  }

  // Build mode ---------------------------------------------------------------

  enterBuildMode(workshopRefId: number): Promise<RequestOutcome> {
    if (this.activeWorkshop === workshopRefId) {
      return Promise.resolve({ ok: true, error: "", nonce: 0, requestType: Fo4MsgType.WorkshopMode, refId: workshopRefId, items: [] });
    }
    const prev = this.pendingEnter.get(workshopRefId);
    if (prev) {
      prev(failed(Fo4MsgType.WorkshopMode, "Superseded"));
    }
    return new Promise((resolve) => {
      this.pendingEnter.set(workshopRefId, resolve);
      this.ctx.send(Fo4MsgType.WorkshopMode, { workshopRefId, enter: true });
    });
  }

  exitBuildMode(): void {
    if (!this.activeWorkshop) {
      return;
    }
    const id = this.activeWorkshop;
    this.ctx.send(Fo4MsgType.WorkshopMode, { workshopRefId: id, enter: false });
    this.leaveLocally(id, "");
  }

  // Building -----------------------------------------------------------------

  place(args: PlaceArgs): Promise<RequestOutcome> {
    const pre = this.checkBuild(WorkshopPerm.Build);
    if (pre) {
      return this.reject(Fo4MsgType.WorkshopPlace, pre);
    }
    return this.ctx.request(Fo4MsgType.WorkshopPlace, {
      workshopRefId: this.activeWorkshop,
      recipeId: args.recipeId,
      baseId: args.baseId,
      fromStored: args.fromStored ?? false,
      pos: args.pos,
      rot: args.rot,
      scale: args.scale ?? 1,
      snapTargetRefId: args.snapTargetRefId ?? 0,
    });
  }

  // Moves/scraps/stores/repairs placed objects, split into server-sized
  // batches. Resolves with the first failure or the merged refunds.
  async edit(op: WorkshopEditOp, items: { refId: number; pos?: Vec3; rot?: Vec3 }[]): Promise<RequestOutcome> {
    const perm = op === WorkshopEditOp.Move || op === WorkshopEditOp.Repair ? WorkshopPerm.Build : WorkshopPerm.Scrap;
    const pre = items.length === 0 ? "TooManyItems" : this.checkBuild(perm);
    if (pre) {
      return this.reject(Fo4MsgType.WorkshopEdit, pre);
    }
    const merged: RequestOutcome = { ok: true, error: "", nonce: 0, requestType: Fo4MsgType.WorkshopEdit, refId: 0, items: [] };
    for (let i = 0; i < items.length; i += kMaxEditItems) {
      const batch = items.slice(i, i + kMaxEditItems).map((it) => ({
        refId: it.refId,
        pos: it.pos ?? ([0, 0, 0] as Vec3),
        rot: it.rot ?? ([0, 0, 0] as Vec3),
      }));
      const r = await this.ctx.request(Fo4MsgType.WorkshopEdit, { workshopRefId: this.activeWorkshop, op, items: batch });
      if (!r.ok) {
        return r;
      }
      merged.nonce = r.nonce;
      merged.items = merged.items.concat(r.items);
    }
    return merged;
  }

  async connectWire(a: number, b: number, splineBaseId: number): Promise<RequestOutcome> {
    const pre = this.checkBuild(WorkshopPerm.Build);
    if (pre) {
      return this.reject(Fo4MsgType.WorkshopWire, pre);
    }
    const workshopRefId = this.activeWorkshop;
    const r = await this.ctx.request(Fo4MsgType.WorkshopWire, { workshopRefId, op: WireOp.Connect, a, b, splineBaseId });
    if (r.ok && r.refId) {
      // The server relays wires to neighbours only; the builder adds its own.
      this.addWire(this.view(workshopRefId), { wireRefId: r.refId, a, b, splineBaseId });
    }
    return r;
  }

  async disconnectWire(wireRefId: number): Promise<RequestOutcome> {
    const pre = this.checkBuild(WorkshopPerm.Build);
    if (pre) {
      return this.reject(Fo4MsgType.WorkshopWire, pre);
    }
    const workshopRefId = this.activeWorkshop;
    const r = await this.ctx.request(Fo4MsgType.WorkshopWire, { workshopRefId, op: WireOp.Disconnect, wireRefId });
    if (r.ok) {
      this.removeWire(this.view(workshopRefId), wireRefId);
    }
    return r;
  }

  // Ownership ----------------------------------------------------------------

  claim(workshopRefId: number): Promise<RequestOutcome> {
    return this.ctx.request(Fo4MsgType.WorkshopManage, { workshopRefId, op: WorkshopManageOp.Claim });
  }

  abandon(workshopRefId: number): Promise<RequestOutcome> {
    return this.ctx.request(Fo4MsgType.WorkshopManage, { workshopRefId, op: WorkshopManageOp.Abandon });
  }

  // perms = 0 removes the profile from the ACL.
  setAccess(workshopRefId: number, profileId: number, perms: number): Promise<RequestOutcome> {
    return this.ctx.request(Fo4MsgType.WorkshopManage, { workshopRefId, op: WorkshopManageOp.SetAcl, profileId, perms });
  }

  assignSettler(workshopRefId: number, actorId: number, objectRefId: number): Promise<RequestOutcome> {
    return this.ctx.request(Fo4MsgType.WorkshopManage, { workshopRefId, op: WorkshopManageOp.Assign, actorId, objectRefId });
  }

  reset(): void {
    for (const s of this.settlements.values()) {
      this.clearSettlement(s);
    }
    this.settlements.clear();
    this.localToServer.clear();
    if (this.activeWorkshop) {
      this.ctx.platform.exitWorkshopMode();
    }
    this.activeWorkshop = 0;
    this.activePerms = 0;
    for (const resolve of this.pendingEnter.values()) {
      resolve(failed(Fo4MsgType.WorkshopMode, "Disconnected"));
    }
    this.pendingEnter.clear();
  }

  // Apply ----------------------------------------------------------------

  private onMode(m: WorkshopModeMessage): void {
    const pending = this.pendingEnter.get(m.workshopRefId);
    if (m.enter && pending) {
      this.pendingEnter.delete(m.workshopRefId);
      if (m.allowed) {
        if (this.activeWorkshop && this.activeWorkshop !== m.workshopRefId) {
          this.leaveLocally(this.activeWorkshop, "");
        }
        this.activeWorkshop = m.workshopRefId;
        this.activePerms = m.perms;
        this.ctx.platform.enterWorkshopMode(this.ctx.platform.refs.toLocal(m.workshopRefId), m.perms);
        this.ctx.events.emit("workshopModeChanged", { workshopRefId: m.workshopRefId, active: true, perms: m.perms, reason: "" });
        pending({ ok: true, error: "", nonce: 0, requestType: Fo4MsgType.WorkshopMode, refId: m.workshopRefId, items: [] });
      } else {
        this.ctx.reportFailure(Fo4MsgType.WorkshopMode, m.reason);
        pending(failed(Fo4MsgType.WorkshopMode, m.reason));
      }
      return;
    }
    if (!m.enter && m.reason && this.activeWorkshop) {
      // Server-side exit, e.g. "LeftBuildArea" (refId may be 0)
      this.leaveLocally(this.activeWorkshop, m.reason);
      this.ctx.reportFailure(Fo4MsgType.WorkshopMode, m.reason);
    }
  }

  private leaveLocally(workshopRefId: number, reason: string): void {
    this.activeWorkshop = 0;
    this.activePerms = 0;
    this.ctx.platform.exitWorkshopMode();
    this.ctx.events.emit("workshopModeChanged", { workshopRefId, active: false, perms: 0, reason });
  }

  private onState(m: WorkshopStateMessage): void {
    const s = this.view(m.workshopRefId);
    s.state = m;
    if (this.activeWorkshop === m.workshopRefId) {
      this.activePerms = m.yourPerms;
    }
    const local = this.ctx.platform.refs.toLocal(m.workshopRefId);
    if (local) {
      this.ctx.platform.setWorkshopRatings(local, {
        food: m.food,
        water: m.water,
        safety: m.safety,
        beds: m.beds,
        power: m.power,
        powerLoad: m.powerLoad,
        population: m.population,
        happiness: m.happiness,
        budgetCurrent: m.budgetCurrent,
        budgetMax: m.budgetMax,
      });
    }
    this.ctx.events.emit("workshopStateChanged", m);
  }

  private onObjects(m: WorkshopObjectsMessage): void {
    const s = this.view(m.workshopRefId);
    if (m.kind === WorkshopObjectsKind.Snapshot) {
      this.collectSnapshotChunk(s, m);
    } else {
      // A delta older than the current snapshot is already included in it.
      if (s.hasSnapshot && m.version <= s.version) {
        return;
      }
      this.applyDelta(s, m);
      s.version = Math.max(s.version, m.version);
    }
    this.ctx.events.emit("workshopObjectsChanged", {
      workshopRefId: m.workshopRefId,
      version: s.version,
      objectCount: s.objects.size,
    });
  }

  private collectSnapshotChunk(s: SettlementView, m: WorkshopObjectsMessage): void {
    if (s.hasSnapshot && m.version < s.version) {
      return;
    }
    if (!s.pendingChunks || s.pendingChunks.version !== m.version) {
      s.pendingChunks = { version: m.version, chunkCount: Math.max(1, m.chunkCount), parts: new Map() };
    }
    s.pendingChunks.parts.set(m.chunk, m);
    if (s.pendingChunks.parts.size < s.pendingChunks.chunkCount) {
      return;
    }
    const parts = Array.from(s.pendingChunks.parts.values());
    s.pendingChunks = undefined;
    const objects = new Map<number, WorkshopObject>();
    const wires = new Map<number, WorkshopWireEntry>();
    const scrapped = new Set<number>();
    for (const part of parts) {
      for (const o of part.added) objects.set(o.refId, o);
      for (const w of part.wires) wires.set(w.wireRefId, w);
      for (const r of part.scrappedPrePlaced) scrapped.add(r);
    }
    const p = this.ctx.platform;
    // Objects
    for (const [refId, v] of Array.from(s.objects.entries())) {
      if (!objects.has(refId)) {
        this.deleteObject(s, refId, v);
      }
    }
    for (const o of objects.values()) {
      this.upsertObject(s, o);
    }
    // Wires (after objects, so endpoints exist)
    for (const [id] of Array.from(s.wires.entries())) {
      if (!wires.has(id)) {
        this.removeWire(s, id);
      }
    }
    for (const w of wires.values()) {
      if (!s.wires.has(w.wireRefId)) {
        this.addWire(s, w);
      }
    }
    // Pre-placed references scrapped by players
    for (const r of Array.from(s.scrappedPrePlaced)) {
      if (!scrapped.has(r)) {
        const local = p.refs.toLocal(r);
        if (local) p.setPrePlacedDisabled(local, false);
        s.scrappedPrePlaced.delete(r);
      }
    }
    for (const r of scrapped) {
      if (!s.scrappedPrePlaced.has(r)) {
        const local = p.refs.toLocal(r);
        if (local) p.setPrePlacedDisabled(local, true);
        s.scrappedPrePlaced.add(r);
      }
    }
    s.version = m.version;
    s.hasSnapshot = true;
  }

  private applyDelta(s: SettlementView, m: WorkshopObjectsMessage): void {
    for (const o of m.added) {
      this.upsertObject(s, o);
    }
    for (const refId of m.removed) {
      const v = s.objects.get(refId);
      if (v) {
        this.deleteObject(s, refId, v);
      } else {
        // Scrapping a pre-placed reference
        const local = this.ctx.platform.refs.toLocal(refId);
        if (local) {
          this.ctx.platform.setPrePlacedDisabled(local, true);
          s.scrappedPrePlaced.add(refId);
        }
      }
    }
    for (const r of m.scrappedPrePlaced) {
      const local = this.ctx.platform.refs.toLocal(r);
      if (local) this.ctx.platform.setPrePlacedDisabled(local, true);
      s.scrappedPrePlaced.add(r);
    }
    for (const w of m.wires) {
      if (!s.wires.has(w.wireRefId)) {
        this.addWire(s, w);
      }
    }
  }

  private upsertObject(s: SettlementView, o: WorkshopObject): void {
    const p = this.ctx.platform;
    const existing = s.objects.get(o.refId);
    if (existing && existing.obj.baseId === o.baseId && existing.obj.scale === o.scale) {
      if (!sameVec(existing.obj.pos, o.pos) || !sameVec(existing.obj.rot, o.rot)) {
        p.moveWorkshopObject(existing.local, o.pos, o.rot);
      }
      existing.obj = o;
      return;
    }
    if (existing) {
      this.deleteObject(s, o.refId, existing);
    }
    const local = p.spawnWorkshopObject({ baseId: o.baseId, pos: o.pos, rot: o.rot, scale: o.scale });
    s.objects.set(o.refId, { obj: o, local });
    if (local) {
      this.localToServer.set(local, o.refId);
    }
  }

  private deleteObject(s: SettlementView, refId: number, v: PlacedView): void {
    // Wires attached to a removed object go with it (the server removes
    // them too; this keeps the scene consistent until the next snapshot).
    for (const [id, w] of Array.from(s.wires.entries())) {
      if (w.entry.a === refId || w.entry.b === refId) {
        this.removeWire(s, id);
      }
    }
    if (v.local) {
      this.ctx.platform.deleteWorkshopObject(v.local);
      this.localToServer.delete(v.local);
    }
    s.objects.delete(refId);
  }

  private addWire(s: SettlementView, entry: WorkshopWireEntry): void {
    if (s.wires.has(entry.wireRefId)) {
      return;
    }
    const a = this.localIdOf(entry.a);
    const b = this.localIdOf(entry.b);
    const local = a && b ? this.ctx.platform.spawnWire(a, b, entry.splineBaseId) : 0;
    s.wires.set(entry.wireRefId, { entry, local });
  }

  private removeWire(s: SettlementView, wireRefId: number): void {
    const w = s.wires.get(wireRefId);
    if (!w) {
      return;
    }
    if (w.local) {
      this.ctx.platform.deleteWire(w.local);
    }
    s.wires.delete(wireRefId);
  }

  private onWireRelay(m: WorkshopWireMessage): void {
    const s = this.view(m.workshopRefId);
    if (m.op === WireOp.Connect) {
      this.addWire(s, { wireRefId: m.wireRefId, a: m.a, b: m.b, splineBaseId: m.splineBaseId });
    } else {
      this.removeWire(s, m.wireRefId);
    }
  }

  private clearSettlement(s: SettlementView): void {
    const p = this.ctx.platform;
    for (const id of Array.from(s.wires.keys())) {
      this.removeWire(s, id);
    }
    for (const [refId, v] of Array.from(s.objects.entries())) {
      this.deleteObject(s, refId, v);
    }
    for (const r of s.scrappedPrePlaced) {
      const local = p.refs.toLocal(r);
      if (local) p.setPrePlacedDisabled(local, false);
    }
    s.scrappedPrePlaced.clear();
  }

  private view(workshopRefId: number): SettlementView {
    let s = this.settlements.get(workshopRefId);
    if (!s) {
      s = { version: 0, hasSnapshot: false, objects: new Map(), wires: new Map(), scrappedPrePlaced: new Set() };
      this.settlements.set(workshopRefId, s);
    }
    return s;
  }

  private checkBuild(perm: number): string {
    if (!this.activeWorkshop) {
      return "NotInBuildMode";
    }
    return hasPerm(this.activePerms, perm) ? "" : "NoPermission";
  }

  private reject(requestType: number, error: string): Promise<RequestOutcome> {
    this.ctx.reportFailure(requestType, error);
    return Promise.resolve(failed(requestType, error));
  }
}

function sameVec(a: Vec3, b: Vec3): boolean {
  return a[0] === b[0] && a[1] === b[1] && a[2] === b[2];
}
