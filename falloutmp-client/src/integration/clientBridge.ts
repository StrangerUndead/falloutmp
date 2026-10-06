// Glue between the session's message stream and FalloutMpClient: incoming
// Fallout 4 messages go to the client first (handleIncoming), and the
// upstream actor messages (CreateActor, DestroyActor, hosting) are mapped to
// form ids. Outgoing messages use a "sendMessage" event emitter.
// Only structural types are used here, to keep this package free of
// falloutPlatform runtime imports.
import { Fo4Transport } from "../core/transport";
import { FalloutMpClient } from "../falloutMpClient";

export interface SendMessageEmitter {
  emit(event: "sendMessage", payload: { message: { t: number }; reliability: "reliable" | "unreliable" }): unknown;
}

export class EmitterTransport implements Fo4Transport {
  constructor(private readonly emitter: SendMessageEmitter) {}
  send(message: { t: number }, reliable: boolean): void {
    this.emitter.emit("sendMessage", { message, reliability: reliable ? "reliable" : "unreliable" });
  }
}

export class ClientBridge {
  private idxToServerId = new Map<number, number>();
  constructor(private readonly client: FalloutMpClient) {}

  handleIncoming(msg: { t: number }): boolean {
    return this.client.onMessage(msg);
  }

  // Upstream CreateActor. Its idx is the server's slot index; Fallout 4
  // messages name actors by form id (refrId), so that is the id used here.
  onCreateActor(msg: { idx: number; isMe: boolean; refrId?: number; profileId?: number }): void {
    const serverId = msg.refrId !== undefined ? longToNormal(msg.refrId) : 0;
    if (!serverId) {
      return;
    }
    this.idxToServerId.set(msg.idx, serverId);
    if (msg.isMe) {
      this.client.setLocalActor(serverId, msg.profileId ?? -1);
    } else {
      this.client.onActorStreamedIn(serverId);
    }
  }

  // Upstream HostStart/HostStop carry "long" form ids: ids of plugin
  // references are offset by 0x100000000 (FormIdCasts::LongToNormal).
  onHostStart(msg: { target: number }): void {
    this.client.setHosted(longToNormal(msg.target), true);
  }

  onHostStop(msg: { target: number }): void {
    this.client.setHosted(longToNormal(msg.target), false);
  }

  onDestroyActor(msg: { idx: number }): void {
    const serverId = this.idxToServerId.get(msg.idx);
    this.idxToServerId.delete(msg.idx);
    if (serverId !== undefined) {
      this.client.onActorDestroyed(serverId);
    }
  }

  onConnectionLost(): void {
    this.idxToServerId.clear();
    this.client.onDisconnect();
  }
}

export function longToNormal(longFormId: number): number {
  return longFormId % 0x100000000; // same as the server's LongToNormal
}
