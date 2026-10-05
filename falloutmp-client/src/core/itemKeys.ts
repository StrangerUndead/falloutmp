import { ItemCount, ItemKey } from "../services/messages/fo4Messages";

// Stack identity, mirroring fo4::ItemKey::SameStack on the server: base,
// sorted mod list, condition and stolen owner. ammoLoaded is excluded, so
// a weapon stack doesn't split when its magazine changes.
export function stackKey(k: ItemKey): string {
  const mods = k.mods.slice().sort((a, b) => a - b).join(",");
  return `${k.baseId}|${mods}|${k.condition}|${k.stolenFrom}`;
}

export function sameStack(a: ItemKey, b: ItemKey): boolean {
  return stackKey(a) === stackKey(b);
}

export function cloneKey(k: ItemKey): ItemKey {
  return {
    baseId: k.baseId,
    mods: k.mods.slice(),
    condition: k.condition,
    stolenFrom: k.stolenFrom,
    ammoLoaded: k.ammoLoaded,
  };
}

export function isInstanced(k: ItemKey): boolean {
  return k.mods.length > 0 || k.condition !== 0 || k.stolenFrom !== 0;
}

export function countOf(entries: ItemCount[], baseId: number): number {
  let n = 0;
  for (const e of entries) {
    if (e.item.baseId === baseId) {
      n += e.count;
    }
  }
  return n;
}

export interface InventoryDiffOp {
  item: ItemKey;
  count: number; // > 0 add, < 0 remove
}

// Minimal add/remove ops that turn `from` into `to`, by stack identity.
// Removals come first so a stack that changes identity (e.g. a modded
// weapon) never doubles up in between.
export function diffInventories(from: ItemCount[], to: ItemCount[]): InventoryDiffOp[] {
  const have = new Map<string, ItemCount>();
  for (const e of from) {
    const k = stackKey(e.item);
    const prev = have.get(k);
    have.set(k, prev ? { item: prev.item, count: prev.count + e.count } : { item: e.item, count: e.count });
  }
  const want = new Map<string, ItemCount>();
  for (const e of to) {
    const k = stackKey(e.item);
    const prev = want.get(k);
    want.set(k, prev ? { item: prev.item, count: prev.count + e.count } : { item: e.item, count: e.count });
  }
  const removes: InventoryDiffOp[] = [];
  const adds: InventoryDiffOp[] = [];
  for (const [k, h] of have) {
    const w = want.get(k)?.count ?? 0;
    if (h.count > w) {
      removes.push({ item: cloneKey(h.item), count: w - h.count });
    }
  }
  for (const [k, w] of want) {
    const h = have.get(k)?.count ?? 0;
    if (w.count > h) {
      adds.push({ item: cloneKey(w.item), count: w.count - h });
    }
  }
  return removes.concat(adds);
}
