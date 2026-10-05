import {
  Fo4MessageMap,
  ImplementedFo4MsgType,
} from "../services/messages/fo4Messages";

type Handler<K extends ImplementedFo4MsgType> = (msg: Fo4MessageMap[K]) => void;

// Dispatches incoming JSON messages to the services by type id.
export class MessageRouter {
  private handlers = new Map<number, ((msg: never) => void)[]>();
  unhandledCount = 0;
  errorCount = 0;
  onError: (t: number, error: unknown) => void = () => {};

  on<K extends ImplementedFo4MsgType>(t: K, handler: Handler<K>): void {
    const list = this.handlers.get(t) ?? [];
    list.push(handler as (msg: never) => void);
    this.handlers.set(t, list);
  }

  // Returns false when no service handles the type, so the caller can
  // pass the message on to the upstream SkyMP handlers.
  dispatch(msg: { t: number }): boolean {
    const list = this.handlers.get(msg.t);
    if (!list || !list.length) {
      this.unhandledCount++;
      return false;
    }
    for (const h of list) {
      try {
        h(msg as never);
      } catch (e) {
        this.errorCount++;
        this.onError(msg.t, e);
      }
    }
    return true;
  }

  handles(t: number): boolean {
    return (this.handlers.get(t)?.length ?? 0) > 0;
  }
}
