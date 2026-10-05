// Glue between the forked skymp5-client networking and FalloutMpClient.
//
// The upstream networkingService throws on unknown message types, so the
// fork routes the Fallout 4 range first:
//
//   if (bridge.handleIncoming(msgAny)) break;   // before the if/else chain
//
// Outgoing messages use the same "sendMessage" event as every upstream
// service, so reliability and the native plugin path stay unchanged.
// Only structural types are used here, to keep this package free of
// skyrimPlatform/falloutPlatform runtime imports.
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

export class SkympClientBridge {
  constructor(private readonly client: FalloutMpClient) {}

  handleIncoming(msg: { t: number }): boolean {
    return this.client.onMessage(msg);
  }

  // Upstream CreateActor message for the local player.
  onCreateActor(msg: { idx: number; isMe: boolean; profileId?: number }): void {
    if (msg.isMe) {
      this.client.setLocalActor(msg.idx, msg.profileId ?? -1);
    } else {
      this.client.onActorStreamedIn(msg.idx);
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
    this.client.onActorDestroyed(msg.idx);
  }

  onConnectionLost(): void {
    this.client.onDisconnect();
  }
}

export function longToNormal(longFormId: number): number {
  return longFormId % 0x100000000; // same as the server's LongToNormal
}
