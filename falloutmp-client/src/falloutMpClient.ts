import { Fo4Transport } from "./core/transport";
import { FalloutPlatform } from "./platform/falloutPlatform";
import { ClientContext } from "./services/context";
import { isFallout4MsgType } from "./services/messages/msgType";
import { ActorValueService } from "./services/services/actorValueService";
import { AnimationOptions, AnimationService } from "./services/services/animationService";
import { AppearanceService } from "./services/services/appearanceService";
import { BarterService } from "./services/services/barterService";
import { CombatService } from "./services/services/combatService";
import { CraftingService } from "./services/services/craftingService";
import { EffectsService } from "./services/services/effectsService";
import { EquipmentService } from "./services/services/equipmentService";
import { InventoryService } from "./services/services/inventoryService";
import { LockService } from "./services/services/lockService";
import { MapService } from "./services/services/mapService";
import { MovementOptions, MovementService } from "./services/services/movementService";
import { PartyService } from "./services/services/partyService";
import { PowerArmorService } from "./services/services/powerArmorService";
import { ProgressionRules, ProgressionService } from "./services/services/progressionService";
import { RequestResultService } from "./services/services/requestResultService";
import { WorkshopService } from "./services/services/workshopService";
import { WorldTimeWeatherService } from "./services/services/worldTimeWeatherService";

export interface FalloutMpClientOptions {
  requestTimeoutMs?: number;
  inventoryReconcileIntervalMs?: number;
  progressionRules?: ProgressionRules;
  movement?: MovementOptions;
  animation?: AnimationOptions;
  // Wire platform capture events to the services (off in tests that drive
  // the services directly).
  bindPlatformEvents?: boolean;
}

// One instance per connection. The networking layer feeds incoming JSON
// messages to onMessage(), calls tick() every frame and onDisconnect() when
// the connection drops; the world view calls onActorStreamedIn/Out.
export class FalloutMpClient {
  readonly ctx: ClientContext;
  readonly results: RequestResultService;
  readonly inventory: InventoryService;
  readonly actorValues: ActorValueService;
  readonly equipment: EquipmentService;
  readonly progression: ProgressionService;
  readonly powerArmor: PowerArmorService;
  readonly workshop: WorkshopService;
  readonly crafting: CraftingService;
  readonly combat: CombatService;
  readonly barter: BarterService;
  readonly locks: LockService;
  readonly party: PartyService;
  readonly map: MapService;
  readonly timeWeather: WorldTimeWeatherService;
  readonly movement: MovementService;
  readonly effects: EffectsService;
  readonly animation: AnimationService;
  readonly appearance: AppearanceService;

  constructor(readonly platform: FalloutPlatform, transport: Fo4Transport, opts: FalloutMpClientOptions = {}) {
    this.ctx = new ClientContext(platform, transport, opts.requestTimeoutMs);
    this.results = new RequestResultService(this.ctx);
    this.inventory = new InventoryService(this.ctx, { reconcileIntervalMs: opts.inventoryReconcileIntervalMs });
    this.actorValues = new ActorValueService(this.ctx);
    this.equipment = new EquipmentService(this.ctx);
    this.progression = new ProgressionService(this.ctx, opts.progressionRules);
    this.powerArmor = new PowerArmorService(this.ctx);
    this.workshop = new WorkshopService(this.ctx);
    this.crafting = new CraftingService(this.ctx);
    this.combat = new CombatService(this.ctx);
    this.barter = new BarterService(this.ctx);
    this.locks = new LockService(this.ctx);
    this.party = new PartyService(this.ctx);
    this.map = new MapService(this.ctx);
    this.timeWeather = new WorldTimeWeatherService(this.ctx);
    this.movement = new MovementService(this.ctx, opts.movement);
    this.effects = new EffectsService(this.ctx);
    this.animation = new AnimationService(this.ctx, opts.animation);
    this.appearance = new AppearanceService(this.ctx);
    if (opts.bindPlatformEvents ?? true) {
      this.bindPlatformEvents();
    }
  }

  get events() {
    return this.ctx.events;
  }

  // From CreateActor(isMe) of the upstream protocol.
  setLocalActor(serverActorId: number, profileId: number): void {
    this.ctx.session.localActorId = serverActorId;
    this.ctx.session.profileId = profileId;
  }

  // F13: the server made this client the host of an NPC (HostStart), or
  // took it away (HostStop).
  setHosted(npcServerId: number, hosted: boolean): void {
    if (hosted) {
      this.ctx.session.hosted.add(npcServerId);
      this.movement.onHostStart(npcServerId);
    } else {
      this.ctx.session.hosted.delete(npcServerId);
    }
  }

  // Returns true when the message was a Fallout 4 message handled here.
  onMessage(msg: { t: number }): boolean {
    if (!isFallout4MsgType(msg.t)) {
      return false;
    }
    if (!this.ctx.router.dispatch(msg)) {
      this.platform.log("warn", `no handler for Fallout 4 message ${msg.t}`);
    }
    return true;
  }

  tick(): void {
    this.movement.tickOwner();
    this.movement.tickRemotes();
    this.animation.tickOwner();
    this.animation.tickRemotes();
    this.ctx.requests.tick();
    this.inventory.tick();
    this.combat.tick();
  }

  onActorStreamedIn(serverActorId: number): void {
    this.appearance.applyTo(serverActorId);
    this.animation.applyTo(serverActorId);
    this.actorValues.applyRemoteTo(serverActorId);
    this.equipment.applyTo(serverActorId);
    this.powerArmor.applyTo(serverActorId);
  }

  onActorStreamedOut(serverActorId: number): void {
    // Keep the cached state; the server resends on the next stream-in.
    void serverActorId;
  }

  onActorDestroyed(serverActorId: number): void {
    this.actorValues.forgetActor(serverActorId);
    this.equipment.forgetActor(serverActorId);
    this.powerArmor.forgetActor(serverActorId);
    this.movement.forgetActor(serverActorId);
    this.effects.forgetActor(serverActorId);
    this.animation.forgetActor(serverActorId);
    this.appearance.forgetActor(serverActorId);
  }

  onDisconnect(): void {
    this.ctx.requests.failAll("Disconnected");
    this.ctx.session.hosted.clear();
    this.inventory.reset();
    this.actorValues.reset();
    this.equipment.reset();
    this.progression.reset();
    this.powerArmor.reset();
    this.workshop.reset();
    this.combat.reset();
    this.locks.reset();
    this.party.reset();
    this.map.reset();
    this.timeWeather.reset();
    this.movement.reset();
    this.effects.reset();
    this.animation.reset();
    this.appearance.reset();
  }

  private bindPlatformEvents(): void {
    const p = this.platform;
    const server = (local: number) => this.workshop.serverIdOf(local);
    p.on("tick", () => this.tick());
    p.on("powerArmorEnterRequested", (e) => void this.powerArmor.enter(server(e.frame)));
    p.on("powerArmorExitRequested", () => void this.powerArmor.exit());
    p.on("workshopActivated", (e) => void this.workshop.enterBuildMode(server(e.workbench)));
    p.on("workshopMenuClosed", () => this.workshop.exitBuildMode());
    p.on("workshopPlaceRequested", (e) =>
      void this.workshop.place({
        recipeId: e.recipeId,
        baseId: e.baseId,
        fromStored: e.fromStored,
        pos: e.pos,
        rot: e.rot,
        scale: e.scale,
        snapTargetRefId: e.snapTarget ? server(e.snapTarget) : 0,
      }),
    );
    p.on("workshopEditRequested", (e) =>
      void this.workshop.edit(
        e.op,
        e.items.map((i) => ({ refId: server(i.ref), pos: i.pos, rot: i.rot })),
      ),
    );
    p.on("workshopWireRequested", (e) => void this.workshop.connectWire(server(e.a), server(e.b), e.splineBaseId));
    p.on("craftRequested", (e) => void this.crafting.craft(server(e.workbench), e.recipeId, e.count));
    p.on("modRequested", (e) =>
      void (e.attach
        ? this.crafting.attachMod(server(e.workbench), e.item, e.modId)
        : this.crafting.detachMod(server(e.workbench), e.item, e.modId)),
    );
    p.on("scrapRequested", (e) => void this.crafting.scrap(server(e.workbench), e.item, e.count));
    p.on("useItemRequested", (e) => void this.inventory.useItem(e.baseId));
    p.on("equipRequested", (e) => (e.equip ? this.equipment.equip(e.item) : this.equipment.unequip(e.item)));
    p.on("dropRequested", (e) => void this.inventory.drop(e.item, e.count));
    p.on("containerTransferRequested", (e) =>
      void (e.take
        ? this.inventory.take(server(e.container), e.item, e.count)
        : this.inventory.put(server(e.container), e.item, e.count)),
    );
    p.on("lockedActivated", (e) => this.locks.activateLock(server(e.ref)));
    p.on("lockpickAttempt", () => this.locks.attemptLockpick());
    p.on("lockpickCancelled", () => this.locks.cancelLockpick());
    p.on("terminalActivated", (e) => this.locks.activateTerminal(server(e.ref)));
    p.on("hackGuess", () => this.locks.guessPassword());
    p.on("weaponFired", (e) => {
      const shooter = e.shooter && e.shooter !== p.getPlayer() ? p.refs.toServer(e.shooter) : 0;
      if (shooter && !this.ctx.isHosted(shooter)) {
        return; // not ours to report
      }
      this.lastLocalShotId = this.combat.fire(e.weaponBaseId, e.origin, e.direction, shooter);
    });
    p.on("hostedValuesChanged", (e) => this.actorValues.reportHosted(p.refs.toServer(e.actor), e.values));
    p.on("reloadRequested", () => void this.combat.reload());
    p.on("projectileHit", (e) =>
      this.combat.reportHit(e.localShotId || this.lastLocalShotId, e.projectileIndex, this.toServerActor(e.target), e.limb),
    );
    p.on("animationEvent", (e) => this.animation.onLocalEvent(e.actor, e.name));
    p.on("looksMenuClosed", () => this.appearance.onLooksMenuClosed());
    p.on("fastTravelRequested", (e) => void this.map.fastTravel(server(e.marker)));
  }

  private lastLocalShotId = 0;

  private toServerActor(local: number): number {
    return local === this.platform.getPlayer() ? this.ctx.session.localActorId : this.platform.refs.toServer(local);
  }
}
