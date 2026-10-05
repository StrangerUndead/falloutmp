import { cloneKey, sameStack } from "../../core/itemKeys";
import { ClientContext } from "../context";
import { EquipOp } from "../messages/codes";
import { ItemKey, UpdateEquipmentFo4Message } from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";

interface EquipmentState {
  weapon?: ItemKey;
  armor: ItemKey[];
}

// F05: equip requests go to the server; the game only changes when the
// server's equipment state arrives (for the owner and for puppets).
export class EquipmentService {
  private states = new Map<number, EquipmentState>();

  constructor(private readonly ctx: ClientContext) {
    ctx.router.on(Fo4MsgType.UpdateEquipmentFo4, (m) => this.onEquipment(m));
  }

  equip(item: ItemKey): void {
    this.ctx.send(Fo4MsgType.UpdateEquipmentFo4, { op: EquipOp.Equip, item: cloneKey(item) });
  }

  unequip(item: ItemKey): void {
    this.ctx.send(Fo4MsgType.UpdateEquipmentFo4, { op: EquipOp.Unequip, item: cloneKey(item) });
  }

  requestState(): void {
    this.ctx.send(Fo4MsgType.UpdateEquipmentFo4, { op: EquipOp.State });
  }

  get(actorIdx: number): EquipmentState | undefined {
    return this.states.get(actorIdx);
  }

  localWeapon(): ItemKey | undefined {
    return this.states.get(this.ctx.session.localActorId)?.weapon;
  }

  forgetActor(actorIdx: number): void {
    this.states.delete(actorIdx);
  }

  reset(): void {
    this.states.clear();
  }

  // Re-applies cached equipment to a puppet that just streamed in.
  applyTo(actorIdx: number): void {
    const s = this.states.get(actorIdx);
    const local = this.ctx.localActor(actorIdx);
    if (s && local) {
      this.applyToGame(local, s, this.ctx.isLocalActor(actorIdx));
    }
  }

  private onEquipment(m: UpdateEquipmentFo4Message): void {
    if (m.op !== EquipOp.State) {
      return; // requests are client->server only
    }
    const prev = this.states.get(m.actorIdx);
    const state: EquipmentState = { weapon: m.weapon, armor: m.armor };
    this.states.set(m.actorIdx, state);
    const local = this.ctx.localActor(m.actorIdx);
    const isOwner = this.ctx.isLocalActor(m.actorIdx);
    if (local) {
      this.applyToGame(local, state, isOwner);
    }
    if (isOwner && m.weapon && (!prev?.weapon || prev.weapon.ammoLoaded !== m.weapon.ammoLoaded)) {
      this.ctx.platform.setAmmoLoaded(local, m.weapon.ammoLoaded);
    }
    this.ctx.events.emit("equipmentChanged", { actorIdx: m.actorIdx, weapon: m.weapon, armor: m.armor });
  }

  private applyToGame(actor: number, s: EquipmentState, isOwner: boolean): void {
    const p = this.ctx.platform;
    const want: ItemKey[] = s.armor.slice();
    if (s.weapon) {
      want.push(s.weapon);
    }
    const have = p.getEquippedItems(actor);
    for (const h of have) {
      if (!want.some((w) => sameStack(w, h))) {
        p.unequipItemEx(actor, h, true);
      }
    }
    for (const w of want) {
      if (!have.some((h) => sameStack(w, h))) {
        // Puppets can't drop or swap gear on their own.
        p.equipItemEx(actor, w, !isOwner, true);
      }
    }
  }
}
