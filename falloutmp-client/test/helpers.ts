import { Harness } from "./fakePlatform";
import { ItemCount } from "../src/services/messages/fo4Messages";
import { Fo4MsgType } from "../src/services/messages/msgType";

// Answers the last nonce'd request of `type` with a RequestResult.
export function reply(
  h: Harness,
  type: Fo4MsgType,
  ok: boolean,
  error = "",
  extra: { refId?: number; items?: ItemCount[] } = {},
): number {
  const sent = h.transport.ofType<{ nonce: number }>(type);
  const last = sent[sent.length - 1];
  if (!last) {
    throw new Error(`no ${Fo4MsgType[type]} was sent`);
  }
  h.receive({
    t: Fo4MsgType.RequestResult,
    nonce: last.nonce,
    requestType: type,
    ok,
    error,
    refId: extra.refId ?? 0,
    items: extra.items ?? [],
  } as { t: number });
  return last.nonce;
}

export function lastSent<T>(h: Harness, type: Fo4MsgType): T {
  const sent = h.transport.ofType<T>(type);
  if (!sent.length) {
    throw new Error(`no ${Fo4MsgType[type]} was sent`);
  }
  return sent[sent.length - 1];
}
