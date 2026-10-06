import { cloneKey } from "../../core/itemKeys";
import { RequestOutcome } from "../../core/requestTracker";
import { ClientContext, failed } from "../context";
import { ModOp } from "../messages/codes";
import { ItemKey } from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";

// F08/F23: crafting, weapon/armor modding and scrapping at workbenches.
// The server checks the bench, perks and components (auto-scrapping junk
// like vanilla) and answers with the created items; the inventory itself
// arrives as SetInventoryFo4.
export class CraftingService {
  constructor(private readonly ctx: ClientContext) {}

  async craft(workbenchRefId: number, recipeId: number, count = 1): Promise<RequestOutcome> {
    if (!Number.isInteger(count) || count < 1) {
      return this.reject(Fo4MsgType.CraftItemFo4, "InvalidCount");
    }
    const r = await this.ctx.request(Fo4MsgType.CraftItemFo4, { workbenchRefId, recipeId, count });
    if (r.ok) {
      this.ctx.events.emit("crafted", { recipeId, items: r.items });
    }
    return r;
  }

  // Resolves with the item's new key (its mod list changed) in items[0].
  attachMod(workbenchRefId: number, item: ItemKey, modId: number): Promise<RequestOutcome> {
    return this.ctx.request(Fo4MsgType.ModItem, { workbenchRefId, item: cloneKey(item), modId, op: ModOp.Attach });
  }

  detachMod(workbenchRefId: number, item: ItemKey, modId: number): Promise<RequestOutcome> {
    if (!item.mods.includes(modId)) {
      return this.reject(Fo4MsgType.ModItem, "IncompatibleMod");
    }
    return this.ctx.request(Fo4MsgType.ModItem, { workbenchRefId, item: cloneKey(item), modId, op: ModOp.Detach });
  }

  // Resolves with the produced components in items.
  async scrap(workbenchRefId: number, item: ItemKey, count = 1): Promise<RequestOutcome> {
    if (!Number.isInteger(count) || count < 1) {
      return this.reject(Fo4MsgType.ScrapItem, "InvalidCount");
    }
    const r = await this.ctx.request(Fo4MsgType.ScrapItem, { workbenchRefId, item: cloneKey(item), count });
    if (r.ok) {
      this.ctx.events.emit("scrapped", { items: r.items });
    }
    return r;
  }

  private reject(requestType: number, error: string): Promise<RequestOutcome> {
    this.ctx.reportFailure(requestType, error);
    return Promise.resolve(failed(requestType, error));
  }
}
