import { EventBus } from "../core/eventBus";
import { MessageRouter } from "../core/messageRouter";
import { RequestOutcome, RequestTracker } from "../core/requestTracker";
import { Fo4Transport } from "../core/transport";
import { FalloutPlatform } from "../platform/falloutPlatform";
import { ClientEvents } from "./clientEvents";
import { describeError } from "./messages/codes";
import {
  buildMessage,
  Fo4MessageMap,
  ImplementedFo4MsgType,
  MessageFields,
} from "./messages/fo4Messages";

export interface Session {
  // Server id (actor form id) of the local player; set from CreateActor.
  localActorId: number;
  profileId: number;
}

export interface RequestOptions {
  // Don't show a notification on failure (the caller handles it).
  quiet?: boolean;
}

// Everything a service needs; one instance per connection.
export class ClientContext {
  readonly router = new MessageRouter();
  readonly events = new EventBus<ClientEvents>();
  readonly requests: RequestTracker;
  readonly session: Session = { localActorId: 0, profileId: -1 };
  private quietNonces = new Set<number>();

  constructor(
    readonly platform: FalloutPlatform,
    readonly transport: Fo4Transport,
    requestTimeoutMs?: number,
  ) {
    this.requests = new RequestTracker(() => platform.nowMs(), {
      timeoutMs: requestTimeoutMs,
      firstNonce: platform.randomUint32(),
    });
    this.events.onListenerError = (event, e) =>
      platform.log("error", `listener of ${event} threw: ${String(e)}`);
    this.router.onError = (t, e) =>
      platform.log("error", `handler of message ${t} threw: ${String(e)}`);
  }

  send<K extends ImplementedFo4MsgType>(t: K, fields: MessageFields<K>, reliable = true): Fo4MessageMap[K] {
    const msg = buildMessage(t, fields);
    this.transport.send(msg, reliable);
    return msg;
  }

  // Sends a nonce'd request and resolves with the server's verdict.
  // Failures are reported through the "requestFailed" event as well.
  request<K extends ImplementedFo4MsgType, Reply = undefined>(
    t: K,
    fields: MessageFields<K>,
    opts: RequestOptions = {},
  ): Promise<RequestOutcome<Reply>> {
    const nonce = this.requests.nextNonce();
    const promise = this.requests.track<Reply>(nonce, t);
    if (opts.quiet) {
      this.quietNonces.add(nonce);
    }
    this.send(t, { ...fields, nonce } as MessageFields<K>);
    return promise.then((outcome) => {
      const quiet = this.quietNonces.delete(nonce);
      if (!outcome.ok) {
        this.reportFailure(outcome.requestType, outcome.error, quiet);
      }
      return outcome;
    });
  }

  reportFailure(requestType: number, error: string, quiet = false): void {
    const text = describeError(error);
    this.events.emit("requestFailed", { requestType, error, text });
    if (!quiet && text) {
      this.platform.showNotification(text);
    }
  }

  isLocalActor(serverId: number): boolean {
    return serverId !== 0 && serverId === this.session.localActorId;
  }

  // Local reference of a server actor; the player is always resolvable.
  localActor(serverId: number): number {
    return this.isLocalActor(serverId)
      ? this.platform.getPlayer()
      : this.platform.refs.toLocal(serverId);
  }
}

export function failed<Reply = undefined>(requestType: number, error: string): RequestOutcome<Reply> {
  return { ok: false, error, nonce: 0, requestType, refId: 0, items: [] };
}
