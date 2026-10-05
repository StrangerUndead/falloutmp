import { cloneKey, countOf, diffInventories, sameStack } from "../../core/itemKeys";
import { RequestOutcome } from "../../core/requestTracker";
import { ClientContext, failed } from "../context";
import { Fo4MsgType } from "../messages/msgType";
import { ItemCount, ItemKey, SetInventoryFo4Message } from "../messages/fo4Messages";

interface ContainerView {
  version: number;
  entries: ItemCount[];
}

export interface InventoryServiceOptions {
  reconcileIntervalMs?: number;
}

// F04/F06: the server inventory is the truth. Snapshots are diffed into the
// game with silent add/remove, and a periodic reconcile repairs anything the
// game changed on its own (scripts, vanilla pickups the plugin missed).
export class InventoryService {
  private player: ContainerView = { version: 0, entries: [] };
  private containers = new Map<number, ContainerView>();
  private lastReconcileMs = 0;
  private readonly reconcileIntervalMs: number;
  divergenceCount = 0; // metric: reconciles that had to fix something

  constructor(private readonly ctx: ClientContext, opts: InventoryServiceOptions = {}) {
    this.reconcileIntervalMs = opts.reconcileIntervalMs ?? 5000;
    ctx.router.on(Fo4MsgType.SetInventoryFo4, (m) => this.onSetInventory(m));
  }

  entries(): ItemCount[] {
    return this.player.entries;
  }

  version(): number {
    return this.player.version;
  }

  count(baseId: number): number {
    return countOf(this.player.entries, baseId);
  }

  find(key: ItemKey): ItemCount | undefined {
    return this.player.entries.find((e) => sameStack(e.item, key));
  }

  containerContents(refId: number): ItemCount[] | undefined {
    return this.containers.get(refId)?.entries;
  }

  // Requests -------------------------------------------------------------

  take(containerRefId: number, item: ItemKey, count: number): Promise<RequestOutcome> {
    if (count <= 0) {
      return Promise.resolve(failed(Fo4MsgType.TakeItemFo4, "InvalidCount"));
    }
    return this.ctx.request(Fo4MsgType.TakeItemFo4, { refId: containerRefId, item: cloneKey(item), count });
  }

  put(containerRefId: number, item: ItemKey, count: number): Promise<RequestOutcome> {
    if (count <= 0 || !this.hasAtLeast(item, count)) {
      return Promise.resolve(this.localFailure(Fo4MsgType.PutItemFo4, count <= 0 ? "InvalidCount" : "ItemNotFound"));
    }
    return this.ctx.request(Fo4MsgType.PutItemFo4, { refId: containerRefId, item: cloneKey(item), count });
  }

  // Opening a container: count 0 asks for its contents without moving
  // anything (the server rolls leveled contents on first open).
  open(containerRefId: number): Promise<RequestOutcome> {
    return this.ctx.request(
      Fo4MsgType.TakeItemFo4,
      { refId: containerRefId, item: { baseId: 0, mods: [], condition: 0, stolenFrom: 0, ammoLoaded: 0 }, count: 0 },
      { quiet: true },
    );
  }

  drop(item: ItemKey, count: number): Promise<RequestOutcome> {
    if (count <= 0 || !this.hasAtLeast(item, count)) {
      return Promise.resolve(this.localFailure(Fo4MsgType.DropItemFo4, count <= 0 ? "InvalidCount" : "ItemNotFound"));
    }
    return this.ctx.request(Fo4MsgType.DropItemFo4, { item: cloneKey(item), count });
  }

  useItem(baseId: number): Promise<RequestOutcome> {
    if (this.count(baseId) <= 0) {
      return Promise.resolve(this.localFailure(Fo4MsgType.UseItem, "NotInInventory"));
    }
    return this.ctx.request(Fo4MsgType.UseItem, { baseId });
  }

  // Apply ------------------------------------------------------------------

  private onSetInventory(m: SetInventoryFo4Message): void {
    if (m.refId === 0) {
      // Reliable messages may arrive out of order; versions only grow.
      if (m.version !== 0 && m.version < this.player.version) {
        return;
      }
      this.player = { version: m.version, entries: m.entries };
      this.applyToGame(this.ctx.platform.getPlayer(), m.entries);
      this.lastReconcileMs = this.ctx.platform.nowMs();
    } else {
      const prev = this.containers.get(m.refId);
      // Corpse inventories carry version 0 and are always applied.
      if (prev && m.version !== 0 && m.version < prev.version) {
        return;
      }
      this.containers.set(m.refId, { version: m.version, entries: m.entries });
      const local = this.ctx.platform.refs.toLocal(m.refId);
      if (local) {
        this.applyToGame(local, m.entries);
      }
    }
    this.ctx.events.emit("inventoryChanged", { refId: m.refId, version: m.version, entries: m.entries });
  }

  // Called by the world view when a container streams in after its
  // contents arrived.
  applyContainerToRef(refId: number): void {
    const view = this.containers.get(refId);
    const local = this.ctx.platform.refs.toLocal(refId);
    if (view && local) {
      this.applyToGame(local, view.entries);
    }
  }

  tick(): void {
    const now = this.ctx.platform.nowMs();
    if (now - this.lastReconcileMs < this.reconcileIntervalMs) {
      return;
    }
    this.lastReconcileMs = now;
    if (this.player.version === 0) {
      return; // nothing from the server yet
    }
    if (this.applyToGame(this.ctx.platform.getPlayer(), this.player.entries) > 0) {
      this.divergenceCount++;
      this.ctx.platform.log("warn", "inventory reconcile corrected the game inventory");
    }
  }

  reset(): void {
    this.player = { version: 0, entries: [] };
    this.containers.clear();
  }

  // Returns the number of ops applied.
  private applyToGame(ref: number, target: ItemCount[]): number {
    const p = this.ctx.platform;
    const ops = diffInventories(p.getInventoryEx(ref), target);
    for (const op of ops) {
      if (op.count > 0) {
        p.addItemEx(ref, op.item, op.count, true);
      } else {
        p.removeItemEx(ref, op.item, -op.count, true);
      }
    }
    return ops.length;
  }

  private hasAtLeast(item: ItemKey, count: number): boolean {
    return (this.find(item)?.count ?? 0) >= count;
  }

  private localFailure(requestType: number, error: string): RequestOutcome {
    this.ctx.reportFailure(requestType, error);
    return failed(requestType, error);
  }
}
