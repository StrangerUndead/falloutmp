import { ClientContext } from "../context";
import { Fo4MsgType } from "../messages/msgType";

// Completes nonce'd requests from the generic RequestResult (107) reply.
export class RequestResultService {
  lateReplies = 0; // replies that arrived after their request timed out

  constructor(ctx: ClientContext) {
    ctx.router.on(Fo4MsgType.RequestResult, (m) => {
      const known = ctx.requests.complete(m.nonce, {
        ok: m.ok,
        error: m.ok ? "" : m.error,
        refId: m.refId,
        items: m.items,
        requestType: m.requestType,
      });
      if (!known) {
        this.lateReplies++;
      }
    });
  }
}
