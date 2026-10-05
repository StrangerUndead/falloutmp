// FalloutPlatform on top of the fallout4-platform plugin's native bridge
// (fallout4-platform/core/JsHost.h). Every method forwards to
// __fmp.native(name, argsJson). A native the plugin doesn't implement yet
// is logged once and answers with a neutral default, so the client keeps
// running while the plugin grows.
import { EventBus } from "../core/eventBus";
import { FalloutPlatform, PlatformEvents, RefResolver } from "../platform/falloutPlatform";

export interface NativeBridge {
  native(name: string, argsJson: string): string;
  send(json: string, reliable: boolean): void;
  connect(host: string, port: number): void;
  disconnect(): void;
  log(level: string, text: string): void;
}

// The player reference (PlayerRef) is 0x14 in every Fallout 4 game.
export const kPlayerRef = 0x14;

type Defaults = { [K in keyof FalloutPlatform]?: () => unknown };

// What a missing native returns.
const kDefaults: Defaults = {
  getInventoryEx: () => [],
  getEquippedItems: () => [],
  getMovementFo4: () => undefined,
  getClientConfig: () => undefined,
  spawnPuppet: () => 0,
  spawnWorkshopObject: () => 0,
  spawnWire: () => 0,
  playPowerArmorEnter: () => Promise.resolve(false),
  playPowerArmorExit: () => Promise.resolve(false),
};

const kAsync = new Set<string>(["playPowerArmorEnter", "playPowerArmorExit"]);

const kNatives: (keyof FalloutPlatform)[] = [
  "getInventoryEx", "addItemEx", "removeItemEx", "setAmmoLoaded",
  "setActorValueCurrent", "setActorValueMax", "setHealthFraction", "killActor",
  "equipItemEx", "unequipItemEx", "getEquippedItems",
  "setPlayerLevelAndXp", "setPerkPoints", "addPerk", "removePerk", "openSpecialMenu",
  "playPowerArmorEnter", "playPowerArmorExit", "setInPowerArmor", "applyPowerArmorVisual",
  "setFusionCoreCharge", "setJetpackEnabled",
  "enterWorkshopMode", "exitWorkshopMode", "spawnWorkshopObject", "moveWorkshopObject",
  "deleteWorkshopObject", "setPrePlacedDisabled", "spawnWire", "deleteWire", "setWorkshopRatings",
  "getMovementFo4", "setActorTransform", "teleportActor", "setMovementFlags", "setAimAngles",
  "applyEffectVisuals", "playRemoteShot", "playHitReaction",
  "openLockpickMenu", "closeLockpickMenu", "openHackingMenu", "setHackingAttemptsLeft",
  "closeHackingMenu", "openTerminal", "setLocked",
  "setMapMarker", "setGameTime", "setTimeScale", "forceWeather",
  "showNotification", "sendToFront",
  "spawnPuppet", "deletePuppet", "getClientConfig",
];

export interface NativePlatformHandle {
  platform: FalloutPlatform;
  // Platform events from the plugin (and the per-frame "tick").
  emit<K extends keyof PlatformEvents>(event: K, e: PlatformEvents[K]): void;
  setNow(nowMs: number): void;
  missingNatives(): string[];
}

export function createNativePlatform(bridge: NativeBridge, refs: RefResolver): NativePlatformHandle {
  const bus = new EventBus<PlatformEvents>();
  const missing = new Set<string>();
  let now = 0;

  const call = (name: string, args: unknown[]): unknown => {
    if (missing.has(name)) {
      return kDefaults[name as keyof FalloutPlatform]?.();
    }
    const res = JSON.parse(bridge.native(name, JSON.stringify(args))) as { r?: unknown; missing?: boolean };
    if (res.missing) {
      missing.add(name);
      bridge.log("warn", `native '${name}' is not implemented by the plugin yet`);
      return kDefaults[name as keyof FalloutPlatform]?.();
    }
    // JSON has no undefined: null means "nothing" (getMovementFo4 during loading)
    return res.r === null ? undefined : res.r;
  };

  const platform: Record<string, unknown> = {
    refs,
    log: (level: string, text: string) => bridge.log(level, text),
    getPlayer: () => kPlayerRef,
    nowMs: () => now,
    randomUint32: () => Math.floor(Math.random() * 0x100000000) >>> 0,
    on: (event: keyof PlatformEvents, handler: (e: never) => void) => bus.on(event, handler as never),
  };
  for (const name of kNatives) {
    platform[name] = kAsync.has(name)
      ? (...args: unknown[]) => {
          const r = call(name, args);
          return r instanceof Promise ? r : Promise.resolve(r ?? false);
        }
      : (...args: unknown[]) => call(name, args);
  }

  return {
    platform: platform as unknown as FalloutPlatform,
    emit: (event, e) => bus.emit(event, e),
    setNow: (nowMs) => {
      now = nowMs;
    },
    missingNatives: () => Array.from(missing),
  };
}
