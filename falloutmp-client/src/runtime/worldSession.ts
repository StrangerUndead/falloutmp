// One player's session on a FalloutMP server: connect, log in, stream
// other actors in and out, and feed everything else to FalloutMpClient.
//
// Server ids: the Fallout 4 messages (movement and the rest) name actors
// by form id, the upstream CreateActor/DestroyActor/Teleport by a slot
// index. The session keeps both: idx -> form id, and form id -> the local
// reference (a puppet, or the world reference itself).
import { FalloutMpClient, FalloutMpClientOptions } from "../falloutMpClient";
import { longToNormal } from "../integration/skympClientBridge";
import { ClientConfig, FalloutPlatform, FormId, PuppetSpawn, RefResolver } from "../platform/falloutPlatform";
import { isFallout4MsgType } from "../services/messages/msgType";
import {
  CreateActorMessage,
  CustomPacketMessage,
  DestroyActorMessage,
  HostMessage,
  loginPacket,
  TeleportMessage,
  UpstreamMsgType,
} from "./upstreamMessages";

export interface Connection {
  connect(host: string, port: number): void;
  disconnect(): void;
  send(message: object, reliable: boolean): void;
}

export interface MutableRefs extends RefResolver {
  map(serverId: number, localId: FormId): void;
  unmap(serverId: number): void;
}

export class RefMap implements MutableRefs {
  private s2l = new Map<number, FormId>();
  private l2s = new Map<FormId, number>();
  map(serverId: number, localId: FormId): void {
    this.unmap(serverId);
    this.s2l.set(serverId, localId);
    this.l2s.set(localId, serverId);
  }
  unmap(serverId: number): void {
    const local = this.s2l.get(serverId);
    this.s2l.delete(serverId);
    if (local !== undefined) {
      this.l2s.delete(local);
    }
  }
  toLocal(serverId: number): FormId {
    return this.s2l.get(serverId) ?? 0;
  }
  toServer(localId: FormId): number {
    return this.l2s.get(localId) ?? 0;
  }
}

export type SessionState = "idle" | "connecting" | "loggingIn" | "inWorld" | "waitingToRetry";

export interface WorldSessionOptions {
  client?: FalloutMpClientOptions;
  retryDelayMs?: number;
  // A denied connection (wrong version, full, banned) retries slower
  deniedRetryDelayMs?: number;
}

interface StreamedActor {
  serverId: number; // form id
  local: FormId;
  isPuppet: boolean;
  spawn?: PuppetSpawn;
}

// Player references and actors the server creates have ids from 0xff000000
const kFirstServerFormId = 0xff000000;

export class WorldSession {
  state: SessionState = "idle";
  client: FalloutMpClient | undefined;
  private config: ClientConfig | undefined;
  private disposeEvents: (() => void) | undefined;
  private retryAtMs = 0;
  private byIdx = new Map<number, StreamedActor>();
  private unknownTypes = new Set<number>();
  private readonly o: Required<Omit<WorldSessionOptions, "client">> & { client: FalloutMpClientOptions };

  constructor(
    private readonly platform: FalloutPlatform,
    private readonly refs: MutableRefs,
    private readonly conn: Connection,
    opts: WorldSessionOptions = {},
  ) {
    this.o = {
      client: opts.client ?? {},
      retryDelayMs: opts.retryDelayMs ?? 5000,
      deniedRetryDelayMs: opts.deniedRetryDelayMs ?? 30000,
    };
  }

  // Call once the game data is loaded.
  start(): void {
    this.config = this.platform.getClientConfig();
    if (!this.config) {
      this.platform.log("warn", "No server configured: set server-ip and server-port in FalloutMP.json");
      return;
    }
    this.connect();
  }

  get inWorld(): boolean {
    return this.state === "inWorld";
  }

  // Every frame, before the platform's "tick" event.
  tick(): void {
    if (this.state === "waitingToRetry" && this.platform.nowMs() >= this.retryAtMs) {
      this.connect();
    }
  }

  onConnected(): void {
    if (!this.config) {
      return;
    }
    this.state = "loggingIn";
    const scoped = scopeEvents(this.platform);
    this.disposeEvents = scoped.dispose;
    this.client = new FalloutMpClient(scoped.platform, { send: (m, r) => this.conn.send(m, r) }, this.o.client);
    this.conn.send(loginPacket(this.config.profileId), true);
    this.platform.log("info", `Connected, logging in as profile ${this.config.profileId}`);
  }

  onConnectionFailed(error: string): void {
    this.platform.showNotification(`Can't reach the server: ${error}`);
    this.retryLater(this.o.retryDelayMs);
  }

  onConnectionDenied(error: string): void {
    this.platform.showNotification(`The server refused the connection: ${error}`);
    this.retryLater(this.o.deniedRetryDelayMs);
  }

  onDisconnected(): void {
    const wasInWorld = this.inWorld;
    this.leaveWorld();
    this.platform.showNotification(wasInWorld ? "Lost connection to the server" : "Disconnected");
    this.retryLater(this.o.retryDelayMs);
  }

  onMessage(json: string): void {
    let msg: { t?: unknown };
    try {
      msg = JSON.parse(json);
    } catch (e) {
      this.platform.log("error", `Bad message from the server: ${String(e)}`);
      return;
    }
    if (typeof msg.t !== "number") {
      return; // legacy messages with a string "type"
    }
    switch (msg.t) {
      case UpstreamMsgType.CreateActor:
        return this.onCreateActor(msg as CreateActorMessage);
      case UpstreamMsgType.DestroyActor:
        return this.onDestroyActor(msg as DestroyActorMessage);
      case UpstreamMsgType.Teleport:
        return this.onTeleport(msg as TeleportMessage);
      case UpstreamMsgType.HostStart:
      case UpstreamMsgType.HostStop:
        return this.onHost(msg as HostMessage);
      case UpstreamMsgType.CustomPacket:
        return this.onCustomPacket(msg as CustomPacketMessage);
      case UpstreamMsgType.SetRaceMenuOpen:
        return; // character creation is not synced yet (F03)
    }
    if (isFallout4MsgType(msg.t)) {
      this.client?.onMessage(msg as { t: number });
      return;
    }
    if (!this.unknownTypes.has(msg.t)) {
      this.unknownTypes.add(msg.t);
      this.platform.log("trace", `Ignoring server message type ${msg.t}`);
    }
  }

  // The plugin removes puppets before the game saves (they must not end
  // up in the player's save); afterwards they are created again where the
  // movement buffer last had them.
  recreatePuppets(): void {
    const client = this.client;
    if (!client) {
      return;
    }
    for (const a of this.byIdx.values()) {
      if (!a.isPuppet || !a.spawn) {
        continue;
      }
      const at = client.movement.lastPosition(a.serverId);
      const spawn = at ? { ...a.spawn, pos: at.pos, yaw: at.yaw, worldOrCell: at.worldOrCell } : a.spawn;
      this.refs.unmap(a.serverId);
      a.local = this.platform.spawnPuppet(spawn);
      if (a.local) {
        this.refs.map(a.serverId, a.local);
        client.movement.forgetApplied(a.serverId);
        client.onActorStreamedIn(a.serverId);
      }
    }
  }

  // Puppets and mappings of the actors currently streamed in (tests, UI).
  streamedActors(): StreamedActor[] {
    return Array.from(this.byIdx.values());
  }

  private connect(): void {
    if (!this.config) {
      return;
    }
    this.state = "connecting";
    try {
      this.conn.connect(this.config.serverIp, this.config.serverPort);
    } catch (e) {
      this.platform.log("error", `Unable to connect: ${String(e)}`);
      this.retryLater(this.o.retryDelayMs);
    }
  }

  private retryLater(delayMs: number): void {
    this.state = "waitingToRetry";
    this.retryAtMs = this.platform.nowMs() + delayMs;
  }

  private leaveWorld(): void {
    for (const a of this.byIdx.values()) {
      if (a.isPuppet) {
        this.platform.deletePuppet(a.local);
      }
      this.refs.unmap(a.serverId);
    }
    this.byIdx.clear();
    this.client?.onDisconnect();
    this.client = undefined;
    this.disposeEvents?.();
    this.disposeEvents = undefined;
  }

  private onCreateActor(m: CreateActorMessage): void {
    const client = this.client;
    if (!client) {
      return;
    }
    const serverId = m.refrId !== undefined ? longToNormal(m.refrId) : 0;
    if (!serverId) {
      this.platform.log("warn", `CreateActor ${m.idx} without a form id, ignored`);
      return;
    }
    const { pos, rot, worldOrCell } = m.transform;
    if (m.isMe) {
      const player = this.platform.getPlayer();
      this.byIdx.set(m.idx, { serverId, local: player, isPuppet: false });
      client.setLocalActor(serverId, this.config?.profileId ?? -1);
      this.platform.teleportActor(player, pos, rot[2], worldOrCell);
      if (this.state !== "inWorld") {
        this.state = "inWorld";
        this.platform.showNotification("Connected to the server");
      }
      return;
    }
    this.destroy(m.idx); // re-sent after a respawn
    // References from the load order exist in every client's game already;
    // players and server-created actors need a puppet.
    const isPuppet = serverId >= kFirstServerFormId;
    let local: FormId = serverId;
    let spawn: PuppetSpawn | undefined;
    if (isPuppet) {
      spawn = {
        pos,
        yaw: rot[2],
        worldOrCell,
        baseId: m.baseId ?? 0,
        name: m.appearance?.name ?? "",
        isFemale: m.appearance?.isFemale ?? false,
      };
      local = this.platform.spawnPuppet(spawn);
      if (!local) {
        this.platform.log("warn", `Unable to spawn actor ${serverId.toString(16)}`);
        return;
      }
    }
    this.byIdx.set(m.idx, { serverId, local, isPuppet, spawn });
    this.refs.map(serverId, local);
    client.onActorStreamedIn(serverId);
  }

  private onDestroyActor(m: DestroyActorMessage): void {
    this.destroy(m.idx);
  }

  private destroy(idx: number): void {
    const a = this.byIdx.get(idx);
    if (!a || a.local === this.platform.getPlayer()) {
      return;
    }
    this.byIdx.delete(idx);
    if (a.isPuppet) {
      this.platform.deletePuppet(a.local);
    }
    this.refs.unmap(a.serverId);
    this.client?.onActorDestroyed(a.serverId);
  }

  private onTeleport(m: TeleportMessage): void {
    const a = this.byIdx.get(m.idx);
    if (a) {
      this.platform.teleportActor(a.local, m.pos, m.rot[2], m.worldOrCell);
    }
  }

  private onHost(m: HostMessage): void {
    this.client?.setHosted(longToNormal(m.target), m.t === UpstreamMsgType.HostStart);
  }

  private onCustomPacket(m: CustomPacketMessage): void {
    let content: { customPacketType?: string };
    try {
      content = JSON.parse(m.contentJsonDump);
    } catch {
      return;
    }
    const type = content.customPacketType ?? "";
    if (type.startsWith("loginFailed")) {
      this.platform.showNotification(`Login failed (${type})`);
      this.platform.log("error", `Login failed: ${m.contentJsonDump}`);
    }
  }
}

// FalloutMpClient subscribes to platform events for its lifetime; a new
// client per connection must not leave the old one listening.
function scopeEvents(p: FalloutPlatform): { platform: FalloutPlatform; dispose(): void } {
  let active = true;
  const on: FalloutPlatform["on"] = (event, handler) =>
    p.on(event, (e) => {
      if (active) {
        handler(e);
      }
    });
  const platform = new Proxy(p, {
    get(target, prop) {
      if (prop === "on") {
        return on;
      }
      const v = (target as unknown as Record<PropertyKey, unknown>)[prop];
      return typeof v === "function" ? (v as (...a: unknown[]) => unknown).bind(target) : v;
    },
  });
  return { platform, dispose: () => (active = false) };
}
