import { cloneKey } from "../../core/itemKeys";
import { RequestOutcome } from "../../core/requestTracker";
import { ClientContext, failed } from "../context";
import { BarterOp } from "../messages/codes";
import { BarterMessage, ItemCount } from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";

export type BarterOutcome = RequestOutcome<BarterMessage>;

// F24: the barter menu asks the server for a quote, shows it, and on
// confirm trades with the quoted caps delta. The server re-prices the trade
// and refuses it ("PriceChanged") if the delta no longer matches, so a stale
// menu can never buy at an old price. capsDelta > 0 means the player gains.
export class BarterService {
  constructor(private readonly ctx: ClientContext) {
    ctx.router.on(Fo4MsgType.Barter, (m) => this.onBarter(m));
  }

  quote(vendorId: number, buy: ItemCount[], sell: ItemCount[]): Promise<BarterOutcome> {
    return this.send(BarterOp.Quote, vendorId, buy, sell, 0, true);
  }

  trade(vendorId: number, buy: ItemCount[], sell: ItemCount[], quotedCapsDelta: number): Promise<BarterOutcome> {
    return this.send(BarterOp.Trade, vendorId, buy, sell, quotedCapsDelta, false);
  }

  private send(
    op: BarterOp,
    vendorId: number,
    buy: ItemCount[],
    sell: ItemCount[],
    capsDelta: number,
    quiet: boolean,
  ): Promise<BarterOutcome> {
    if (!buy.length && !sell.length) {
      this.ctx.reportFailure(Fo4MsgType.Barter, "EmptyTrade", quiet);
      return Promise.resolve(failed(Fo4MsgType.Barter, "EmptyTrade"));
    }
    const nonce = this.ctx.requests.nextNonce();
    const promise = this.ctx.requests.track<BarterMessage>(nonce, Fo4MsgType.Barter);
    const copy = (l: ItemCount[]) => l.map((e) => ({ item: cloneKey(e.item), count: e.count }));
    this.ctx.send(Fo4MsgType.Barter, { nonce, op, vendorId, buy: copy(buy), sell: copy(sell), capsDelta });
    return promise.then((r) => {
      if (!r.ok) {
        this.ctx.reportFailure(Fo4MsgType.Barter, r.error, quiet);
      }
      return r;
    });
  }

  private onBarter(m: BarterMessage): void {
    this.ctx.requests.complete<BarterMessage>(m.nonce, {
      ok: !m.error,
      error: m.error,
      refId: m.vendorId,
      items: [],
      reply: m,
    });
  }
}
