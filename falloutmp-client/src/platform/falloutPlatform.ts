// The falloutPlatform native API used by the FalloutMP client.
//
// This file is the contract between the TypeScript client and the
// fallout4-platform F4SE plugin (PLAT workstream). Every method is a native
// the plugin must implement; services only talk to the game through this
// interface, which is what lets the whole client run in Node tests against
// test/fakePlatform.ts.
//
// Conventions:
// - Form ids are numbers. "Local" ids are the game's runtime form ids;
//   server ids (refId/actorIdx in messages) are mapped by the RefResolver.
// - Methods that wait on the game (animations, menus) return Promises.
// - ItemKey/ItemCount are the protocol types: the plugin resolves mods and
//   condition to an ExtraDataList (BGSObjectInstanceExtra, ExtraHealth).
import {
  ItemCount,
  ItemKey,
  PowerArmorPiece,
  Vec3,
} from "../services/messages/fo4Messages";

export type FormId = number;

// Maps between server ids and local game references.
export interface RefResolver {
  // Local form id of a server object/actor, or 0 if it isn't streamed in.
  toLocal(serverId: number): FormId;
  // Server id of a local reference, or 0 if the server doesn't know it.
  toServer(localId: FormId): number;
}

export interface PlatformLog {
  log(level: "trace" | "info" | "warn" | "error", text: string): void;
}

export interface InventoryNatives {
  // Full inventory of a reference with instance data (mods, condition).
  getInventoryEx(ref: FormId): ItemCount[];
  addItemEx(ref: FormId, item: ItemKey, count: number, silent: boolean): void;
  removeItemEx(ref: FormId, item: ItemKey, count: number, silent: boolean): void;
  // Loaded rounds of the weapon the player has drawn.
  setAmmoLoaded(actor: FormId, rounds: number): void;
}

export interface ActorValueNatives {
  setActorValueCurrent(actor: FormId, avId: number, value: number): void;
  setActorValueMax(actor: FormId, avId: number, value: number): void;
  // Remote actors only get a health fraction (0..1) and limb conditions.
  setHealthFraction(actor: FormId, fraction: number): void;
  killActor(actor: FormId, killer: FormId): void;
}

export interface EquipmentNatives {
  equipItemEx(actor: FormId, item: ItemKey, preventRemoval: boolean, silent: boolean): void;
  unequipItemEx(actor: FormId, item: ItemKey, silent: boolean): void;
  getEquippedItems(actor: FormId): ItemKey[];
}

export interface ProgressionNatives {
  setPlayerLevelAndXp(level: number, xp: number, xpForNextLevel: number): void;
  setPerkPoints(points: number): void;
  addPerk(perkId: FormId): void;
  removePerk(perkId: FormId): void;
  // The vanilla SPECIAL menu during character creation.
  openSpecialMenu(): void;
}

export interface PowerArmorVisualState {
  frameBaseId: FormId;
  pieces: PowerArmorPiece[];
  unpowered: boolean;
}

export interface PowerArmorNatives {
  // Plays the vanilla enter/exit sequence for the player and resolves when
  // the animation completes (true) or is interrupted (false).
  playPowerArmorEnter(frame: FormId): Promise<boolean>;
  playPowerArmorExit(): Promise<boolean>;
  // Snaps an actor in or out of a frame without animation (corrections,
  // remote actors joining late, server timeouts).
  setInPowerArmor(actor: FormId, frame: FormId, inside: boolean): void;
  applyPowerArmorVisual(actor: FormId, state: PowerArmorVisualState): void;
  setFusionCoreCharge(charge: number): void;
  setJetpackEnabled(enabled: boolean): void;
}

export interface WorkshopObjectSpawn {
  baseId: FormId;
  pos: Vec3;
  rot: Vec3;
  scale: number;
}

export interface WorkshopRatings {
  food: number;
  water: number;
  safety: number;
  beds: number;
  power: number;
  powerLoad: number;
  population: number;
  happiness: number;
  budgetCurrent: number;
  budgetMax: number;
}

export interface WorkshopNatives {
  // Opens the vanilla workshop menu on a workbench with the given
  // permissions (the plugin hides scrap/store when not allowed).
  enterWorkshopMode(workbench: FormId, perms: number): void;
  exitWorkshopMode(): void;
  // Spawns a placed object; returns the local reference.
  spawnWorkshopObject(spawn: WorkshopObjectSpawn): FormId;
  moveWorkshopObject(ref: FormId, pos: Vec3, rot: Vec3): void;
  deleteWorkshopObject(ref: FormId): void;
  // Pre-placed (plugin) references scrapped by players.
  setPrePlacedDisabled(ref: FormId, disabled: boolean): void;
  spawnWire(a: FormId, b: FormId, splineBaseId: FormId): FormId;
  deleteWire(ref: FormId): void;
  setWorkshopRatings(workbench: FormId, ratings: WorkshopRatings): void;
}

export interface LocalMovement {
  worldOrCell: FormId;
  pos: Vec3;
  yaw: number; // degrees
  aimPitch: number;
  aimHeading: number;
  speed: number;
  direction: number;
  velZ: number;
  flags: number; // MoveFlag bits
}

export interface MovementNatives {
  // Transform and movement state of the player or a hosted NPC this frame;
  // undefined while a loading screen is up or the actor isn't loaded.
  getMovementFo4(actor: FormId): LocalMovement | undefined;
  // Character-controller warp of a remote actor (no physics fighting).
  setActorTransform(actor: FormId, pos: Vec3, yaw: number): void;
  // Hard move (stream-in, cell change, large error).
  teleportActor(actor: FormId, pos: Vec3, yaw: number, worldOrCell: FormId): void;
  setMovementFlags(actor: FormId, flags: number): void;
  setAimAngles(actor: FormId, pitch: number, heading: number): void;
}

export interface EffectNatives {
  // Cosmetic visuals of active chems/effects on a puppet (shaders, sounds);
  // the plugin maps source items to their vanilla visuals.
  applyEffectVisuals(actor: FormId, sourceItems: FormId[]): void;
}

export interface CombatNatives {
  // Cosmetic replay of a remote actor's shot (muzzle flash, tracer, sound).
  playRemoteShot(shooter: FormId, weaponBaseId: FormId, origin: Vec3, direction: Vec3): void;
  // Hit feedback: blood, stagger, hit marker for the attacker.
  playHitReaction(target: FormId, limb: number, total: number, critical: boolean): void;
}

export interface MinigameNatives {
  // Lockpicking: the plugin shows the vanilla menu with server-side
  // success (the client never decides). Attempts and cancels come back
  // as platform events.
  openLockpickMenu(ref: FormId, sessionId: number): void;
  closeLockpickMenu(success: boolean): void;
  openHackingMenu(ref: FormId, sessionId: number, attemptsLeft: number): void;
  setHackingAttemptsLeft(attemptsLeft: number): void;
  closeHackingMenu(success: boolean): void;
  // Shows a terminal's pages (after the server allowed access).
  openTerminal(ref: FormId): void;
  setLocked(ref: FormId, locked: boolean): void;
}

export interface MapNatives {
  // Shows or hides a map marker; discovered markers allow fast travel.
  setMapMarker(ref: FormId, visible: boolean, canTravelTo: boolean): void;
}

export interface WorldNatives {
  setGameTime(gameDays: number, gameHour: number): void;
  setTimeScale(scale: number): void;
  forceWeather(weatherId: FormId, transitionSec: number): void;
}

export interface UiNatives {
  showNotification(text: string): void;
  // The CEF front (skymp5-front fork) receives client events as JSON.
  sendToFront(event: string, payload: unknown): void;
}

export interface PuppetSpawn {
  pos: Vec3;
  yaw: number; // degrees
  worldOrCell: FormId;
  // NPC base to copy; 0 for another player (a player-like actor)
  baseId: FormId;
  name: string;
  isFemale: boolean;
}

export interface PuppetNatives {
  // Creates the stand-in for another player or a streamed-in NPC and
  // returns its local reference (0 when it can't be created yet). The
  // plugin keeps puppets out of saves and switches their AI off.
  spawnPuppet(spawn: PuppetSpawn): FormId;
  deletePuppet(ref: FormId): void;
}

export interface ClientConfig {
  serverIp: string;
  serverPort: number;
  // Offline-mode servers identify players by this number
  profileId: number;
}

export interface SessionNatives {
  // From the plugin's settings file; undefined when no server is set.
  getClientConfig(): ClientConfig | undefined;
}

// Player-side capture events the plugin emits.
export interface PlatformEvents {
  tick: Record<string, never>;
  // Player activated a power armor frame / pressed the exit key.
  powerArmorEnterRequested: { frame: FormId };
  powerArmorExitRequested: Record<string, never>;
  // Workshop menu actions (the plugin cancels the vanilla effect and
  // reports the intent; the server decides).
  workshopActivated: { workbench: FormId };
  workshopMenuClosed: { workbench: FormId };
  workshopPlaceRequested: {
    workbench: FormId;
    recipeId: FormId;
    baseId: FormId;
    fromStored: boolean;
    pos: Vec3;
    rot: Vec3;
    scale: number;
    snapTarget: FormId;
  };
  workshopEditRequested: {
    workbench: FormId;
    op: number;
    items: { ref: FormId; pos: Vec3; rot: Vec3 }[];
  };
  workshopWireRequested: { workbench: FormId; a: FormId; b: FormId; splineBaseId: FormId };
  // Crafting menus (the plugin cancels the vanilla craft and reports it).
  craftRequested: { workbench: FormId; recipeId: FormId; count: number };
  modRequested: { workbench: FormId; item: ItemKey; modId: FormId; attach: boolean };
  scrapRequested: { workbench: FormId; item: ItemKey; count: number };
  // Pip-Boy / quick actions.
  useItemRequested: { baseId: FormId };
  equipRequested: { item: ItemKey; equip: boolean };
  dropRequested: { item: ItemKey; count: number };
  containerTransferRequested: { container: FormId; item: ItemKey; count: number; take: boolean };
  // Activation of a locked object or terminal.
  lockedActivated: { ref: FormId };
  lockpickAttempt: { ref: FormId; sessionId: number };
  lockpickCancelled: { ref: FormId; sessionId: number };
  terminalActivated: { ref: FormId };
  hackGuess: { ref: FormId; sessionId: number };
  // Combat capture.
  // shooter: the player (omitted) or a hosted NPC's local ref
  weaponFired: { weaponBaseId: FormId; origin: Vec3; direction: Vec3; shooter?: FormId };
  reloadRequested: Record<string, never>;
  projectileHit: { localShotId: number; projectileIndex: number; target: FormId; limb: number };
  fastTravelRequested: { marker: FormId };
  // F13: values of a hosted NPC changed in the host's simulation
  hostedValuesChanged: { actor: FormId; values: { avId: number; current: number; max: number }[] };
}

export interface PlatformEventSource {
  on<K extends keyof PlatformEvents>(event: K, handler: (e: PlatformEvents[K]) => void): void;
}

export interface FalloutPlatform
  extends PlatformLog,
    InventoryNatives,
    ActorValueNatives,
    EquipmentNatives,
    ProgressionNatives,
    PowerArmorNatives,
    MovementNatives,
    EffectNatives,
    WorkshopNatives,
    CombatNatives,
    MinigameNatives,
    MapNatives,
    WorldNatives,
    UiNatives,
    PuppetNatives,
    SessionNatives,
    PlatformEventSource {
  readonly refs: RefResolver;
  getPlayer(): FormId;
  nowMs(): number;
  randomUint32(): number;
}
