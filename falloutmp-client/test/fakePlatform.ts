import { EventBus } from "../src/core/eventBus";
import { diffInventories, sameStack } from "../src/core/itemKeys";
import { RecordingTransport } from "../src/core/transport";
import { FalloutMpClient, FalloutMpClientOptions } from "../src/falloutMpClient";
import {
  ClientConfig,
  FalloutPlatform,
  LooksMenuMode,
  PuppetSpawn,
  FormId,
  PlatformEvents,
  PowerArmorVisualState,
  RefResolver,
  WorkshopObjectSpawn,
  WorkshopRatings,
} from "../src/platform/falloutPlatform";
import { AppearanceFo4, GraphVariable, ItemCount, ItemKey, Vec3 } from "../src/services/messages/fo4Messages";

export const kPlayer = 0x14;
export const kPlayerServerId = 0xff000001;
export const kPlayerProfile = 7;

export interface Call {
  fn: string;
  args: unknown[];
}

class FakeRefs implements RefResolver {
  private s2l = new Map<number, number>();
  private l2s = new Map<number, number>();
  map(serverId: number, localId: number): void {
    this.s2l.set(serverId, localId);
    this.l2s.set(localId, serverId);
  }
  toLocal(serverId: number): number {
    return this.s2l.get(serverId) ?? 0;
  }
  toServer(localId: number): number {
    return this.l2s.get(localId) ?? 0;
  }
  unmap(serverId: number): void {
    const local = this.s2l.get(serverId);
    this.s2l.delete(serverId);
    if (local !== undefined) {
      this.l2s.delete(local);
    }
  }
}

// A scriptable stand-in for the fallout4-platform natives.
export class FakePlatform implements FalloutPlatform {
  readonly refs = new FakeRefs();
  readonly calls: Call[] = [];
  readonly inventories = new Map<number, ItemCount[]>();
  readonly equipped = new Map<number, ItemKey[]>();
  readonly notifications: string[] = [];
  readonly logs: string[] = [];
  readonly bus = new EventBus<PlatformEvents>();
  readonly spawned = new Map<number, WorkshopObjectSpawn>();
  readonly wires = new Map<number, { a: number; b: number }>();
  nextSpawnId = 0xff800000;
  now = 1000;
  // Power armor animations resolve with these results (true by default).
  enterAnimationResult = true;
  exitAnimationResult = true;
  private animationGate: (() => void) | undefined;
  holdAnimations = false;

  private record(fn: string, ...args: unknown[]): void {
    this.calls.push({ fn, args });
  }

  callsOf(fn: string): unknown[][] {
    return this.calls.filter((c) => c.fn === fn).map((c) => c.args);
  }

  clearCalls(): void {
    this.calls.length = 0;
  }

  releaseAnimation(): void {
    const g = this.animationGate;
    this.animationGate = undefined;
    g?.();
  }

  // PlatformLog
  log(level: string, text: string): void {
    this.logs.push(`${level}: ${text}`);
  }

  getPlayer(): FormId {
    return kPlayer;
  }
  nowMs(): number {
    return this.now;
  }
  randomUint32(): number {
    return 1000;
  }

  // Inventory
  getInventoryEx(ref: FormId): ItemCount[] {
    return (this.inventories.get(ref) ?? []).map((e) => ({ item: { ...e.item, mods: e.item.mods.slice() }, count: e.count }));
  }
  addItemEx(ref: FormId, item: ItemKey, count: number, silent: boolean): void {
    this.record("addItemEx", ref, item, count, silent);
    const inv = this.inventories.get(ref) ?? [];
    const e = inv.find((x) => sameStack(x.item, item));
    if (e) e.count += count;
    else inv.push({ item: { ...item, mods: item.mods.slice() }, count });
    this.inventories.set(ref, inv);
  }
  removeItemEx(ref: FormId, item: ItemKey, count: number, silent: boolean): void {
    this.record("removeItemEx", ref, item, count, silent);
    const inv = this.inventories.get(ref) ?? [];
    const e = inv.find((x) => sameStack(x.item, item));
    if (e) {
      e.count -= count;
      if (e.count <= 0) inv.splice(inv.indexOf(e), 1);
    }
  }
  setAmmoLoaded(actor: FormId, rounds: number): void {
    this.record("setAmmoLoaded", actor, rounds);
  }

  // Actor values
  setActorValueCurrent(actor: FormId, avId: number, value: number): void {
    this.record("setActorValueCurrent", actor, avId, value);
  }
  setActorValueMax(actor: FormId, avId: number, value: number): void {
    this.record("setActorValueMax", actor, avId, value);
  }
  setHealthFraction(actor: FormId, fraction: number): void {
    this.record("setHealthFraction", actor, fraction);
  }
  killActor(actor: FormId, killer: FormId): void {
    this.record("killActor", actor, killer);
  }

  // Equipment
  equipItemEx(actor: FormId, item: ItemKey, preventRemoval: boolean, silent: boolean): void {
    this.record("equipItemEx", actor, item, preventRemoval, silent);
    const list = this.equipped.get(actor) ?? [];
    list.push(item);
    this.equipped.set(actor, list);
  }
  unequipItemEx(actor: FormId, item: ItemKey, silent: boolean): void {
    this.record("unequipItemEx", actor, item, silent);
    this.equipped.set(actor, (this.equipped.get(actor) ?? []).filter((k) => !sameStack(k, item)));
  }
  getEquippedItems(actor: FormId): ItemKey[] {
    return (this.equipped.get(actor) ?? []).slice();
  }

  // Progression
  setPlayerLevelAndXp(level: number, xp: number, xpForNextLevel: number): void {
    this.record("setPlayerLevelAndXp", level, xp, xpForNextLevel);
  }
  setPerkPoints(points: number): void {
    this.record("setPerkPoints", points);
  }
  addPerk(perkId: FormId): void {
    this.record("addPerk", perkId);
  }
  removePerk(perkId: FormId): void {
    this.record("removePerk", perkId);
  }
  openSpecialMenu(): void {
    this.record("openSpecialMenu");
  }

  // Power armor
  playPowerArmorEnter(frame: FormId): Promise<boolean> {
    this.record("playPowerArmorEnter", frame);
    return this.animation(this.enterAnimationResult);
  }
  playPowerArmorExit(): Promise<boolean> {
    this.record("playPowerArmorExit");
    return this.animation(this.exitAnimationResult);
  }
  private animation(result: boolean): Promise<boolean> {
    if (!this.holdAnimations) {
      return Promise.resolve(result);
    }
    return new Promise((resolve) => {
      this.animationGate = () => resolve(result);
    });
  }
  setInPowerArmor(actor: FormId, frame: FormId, inside: boolean): void {
    this.record("setInPowerArmor", actor, frame, inside);
  }
  applyPowerArmorVisual(actor: FormId, state: PowerArmorVisualState): void {
    this.record("applyPowerArmorVisual", actor, state);
  }
  setFusionCoreCharge(charge: number): void {
    this.record("setFusionCoreCharge", charge);
  }
  setJetpackEnabled(enabled: boolean): void {
    this.record("setJetpackEnabled", enabled);
  }

  // Workshop
  enterWorkshopMode(workbench: FormId, perms: number): void {
    this.record("enterWorkshopMode", workbench, perms);
  }
  exitWorkshopMode(): void {
    this.record("exitWorkshopMode");
  }
  spawnWorkshopObject(spawn: WorkshopObjectSpawn): FormId {
    const id = this.nextSpawnId++;
    this.spawned.set(id, spawn);
    this.record("spawnWorkshopObject", spawn, id);
    return id;
  }
  moveWorkshopObject(ref: FormId, pos: Vec3, rot: Vec3): void {
    this.record("moveWorkshopObject", ref, pos, rot);
    const s = this.spawned.get(ref);
    if (s) {
      s.pos = pos;
      s.rot = rot;
    }
  }
  deleteWorkshopObject(ref: FormId): void {
    this.record("deleteWorkshopObject", ref);
    this.spawned.delete(ref);
  }
  setPrePlacedDisabled(ref: FormId, disabled: boolean): void {
    this.record("setPrePlacedDisabled", ref, disabled);
  }
  spawnWire(a: FormId, b: FormId, splineBaseId: FormId): FormId {
    const id = this.nextSpawnId++;
    this.wires.set(id, { a, b });
    this.record("spawnWire", a, b, splineBaseId, id);
    return id;
  }
  deleteWire(ref: FormId): void {
    this.record("deleteWire", ref);
    this.wires.delete(ref);
  }
  setWorkshopRatings(workbench: FormId, ratings: WorkshopRatings): void {
    this.record("setWorkshopRatings", workbench, ratings);
  }

  // Movement
  movement: import("../src/platform/falloutPlatform").LocalMovement | undefined = undefined;
  hostedMovement = new Map<number, import("../src/platform/falloutPlatform").LocalMovement>();
  getMovementFo4(actor: FormId) {
    const m = actor === kPlayer ? this.movement : this.hostedMovement.get(actor);
    return m ? { ...m, pos: [...m.pos] as Vec3 } : undefined;
  }
  setActorTransform(actor: FormId, pos: Vec3, yaw: number): void {
    this.record("setActorTransform", actor, pos, yaw);
  }
  teleportActor(actor: FormId, pos: Vec3, yaw: number, worldOrCell: FormId): void {
    this.record("teleportActor", actor, pos, yaw, worldOrCell);
  }
  setMovementFlags(actor: FormId, flags: number): void {
    this.record("setMovementFlags", actor, flags);
  }
  setAimAngles(actor: FormId, pitch: number, heading: number): void {
    this.record("setAimAngles", actor, pitch, heading);
  }

  applyEffectVisuals(actor: FormId, sourceItems: FormId[]): void {
    this.record("applyEffectVisuals", actor, sourceItems);
  }

  // Combat
  playRemoteShot(shooter: FormId, weaponBaseId: FormId, origin: Vec3, direction: Vec3): void {
    this.record("playRemoteShot", shooter, weaponBaseId, origin, direction);
  }
  playHitReaction(target: FormId, limb: number, total: number, critical: boolean): void {
    this.record("playHitReaction", target, limb, total, critical);
  }

  // Minigames
  openLockpickMenu(ref: FormId, sessionId: number): void {
    this.record("openLockpickMenu", ref, sessionId);
  }
  closeLockpickMenu(success: boolean): void {
    this.record("closeLockpickMenu", success);
  }
  openHackingMenu(ref: FormId, sessionId: number, attemptsLeft: number): void {
    this.record("openHackingMenu", ref, sessionId, attemptsLeft);
  }
  setHackingAttemptsLeft(attemptsLeft: number): void {
    this.record("setHackingAttemptsLeft", attemptsLeft);
  }
  closeHackingMenu(success: boolean): void {
    this.record("closeHackingMenu", success);
  }
  openTerminal(ref: FormId): void {
    this.record("openTerminal", ref);
  }
  setLocked(ref: FormId, locked: boolean): void {
    this.record("setLocked", ref, locked);
  }

  // Map / world / UI
  setMapMarker(ref: FormId, visible: boolean, canTravelTo: boolean): void {
    this.record("setMapMarker", ref, visible, canTravelTo);
  }
  setGameTime(gameDays: number, gameHour: number): void {
    this.record("setGameTime", gameDays, gameHour);
  }
  setTimeScale(scale: number): void {
    this.record("setTimeScale", scale);
  }
  forceWeather(weatherId: FormId, transitionSec: number): void {
    this.record("forceWeather", weatherId, transitionSec);
  }
  showNotification(text: string): void {
    this.notifications.push(text);
  }
  sendToFront(event: string, payload: unknown): void {
    this.record("sendToFront", event, payload);
  }
  readonly puppets = new Map<number, PuppetSpawn>();
  nextPuppetId = 0xff900000;
  clientConfig: ClientConfig | undefined = { serverIp: "127.0.0.1", serverPort: 7777, profileId: kPlayerProfile };
  spawnPuppet(spawn: PuppetSpawn): FormId {
    const ref = this.nextPuppetId++;
    this.puppets.set(ref, spawn);
    this.record("spawnPuppet", spawn);
    return ref;
  }
  deletePuppet(ref: FormId): void {
    this.puppets.delete(ref);
    this.record("deletePuppet", ref);
  }
  getClientConfig(): ClientConfig | undefined {
    return this.clientConfig;
  }
  // F02: graph variables per actor; notified events are recorded
  readonly graphVariables = new Map<number, Map<string, GraphVariable>>();
  readonly animEvents: { actor: number; name: string }[] = [];
  getGraphVariables(actor: FormId, names: string[]): GraphVariable[] {
    const vars = this.graphVariables.get(actor);
    return names.map((n) => vars?.get(n)).filter((v): v is GraphVariable => !!v);
  }
  setGraphVariables(actor: FormId, values: GraphVariable[]): void {
    let vars = this.graphVariables.get(actor);
    if (!vars) {
      vars = new Map();
      this.graphVariables.set(actor, vars);
    }
    for (const v of values) {
      vars.set(v.name, { ...v });
    }
    this.record("setGraphVariables", actor, values);
  }
  notifyAnimationGraph(actor: FormId, eventName: string): boolean {
    this.animEvents.push({ actor, name: eventName });
    return true;
  }
  // F03
  readonly appearances = new Map<number, AppearanceFo4>();
  looksMenuOpen: LooksMenuMode | undefined = undefined;
  getAppearanceFo4(actor: FormId): AppearanceFo4 | undefined {
    return this.appearances.get(actor);
  }
  applyAppearanceFo4(actor: FormId, data: AppearanceFo4): Promise<boolean> {
    this.appearances.set(actor, data);
    this.record("applyAppearanceFo4", actor, data);
    return Promise.resolve(true);
  }
  openLooksMenu(mode: LooksMenuMode): void {
    this.looksMenuOpen = mode;
  }
  closeLooksMenu(): void {
    this.looksMenuOpen = undefined;
  }

  on<K extends keyof PlatformEvents>(event: K, handler: (e: PlatformEvents[K]) => void): void {
    this.bus.on(event, handler);
  }

  fire<K extends keyof PlatformEvents>(event: K, e: PlatformEvents[K]): void {
    this.bus.emit(event, e);
  }

  // Test helper: is the game inventory equal to `want` by stack identity?
  inventoryMatches(ref: number, want: ItemCount[]): boolean {
    return diffInventories(this.getInventoryEx(ref), want).length === 0;
  }
}

export interface Harness {
  platform: FakePlatform;
  transport: RecordingTransport;
  client: FalloutMpClient;
  // Delivers a server message the way the native plugin would (JSON).
  receive(msg: { t: number }): void;
  flush(): Promise<void>;
}

export function makeClient(opts: FalloutMpClientOptions = {}): Harness {
  const platform = new FakePlatform();
  const transport = new RecordingTransport();
  const client = new FalloutMpClient(platform, transport, opts);
  client.setLocalActor(kPlayerServerId, kPlayerProfile);
  return {
    platform,
    transport,
    client,
    receive(msg) {
      client.onMessage(JSON.parse(JSON.stringify(msg)));
    },
    async flush() {
      for (let i = 0; i < 10; i++) {
        await Promise.resolve();
      }
    },
  };
}

export function key(baseId: number, extra: Partial<ItemKey> = {}): ItemKey {
  return { baseId, mods: [], condition: 0, stolenFrom: 0, ammoLoaded: 0, ...extra };
}
