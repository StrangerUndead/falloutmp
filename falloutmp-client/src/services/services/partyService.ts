import { RequestOutcome } from "../../core/requestTracker";
import { PartyState } from "../clientEvents";
import { ClientContext, failed } from "../context";
import { PartyOp } from "../messages/codes";
import { PartyActionMessage } from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";

export type PartyOutcome = RequestOutcome<PartyActionMessage>;

interface Invite {
  partyId: number;
  fromProfileId: number;
  expiresAtMs: number;
}

export const kInviteTtlMs = 60000; // server pvp.inviteTtl default

// F32: parties and the PvP flag. Every request is answered with the
// sender's party state (op State, same nonce); the server pushes state to
// all members on any change and an Invite (op Invite) to the invitee.
export class PartyService {
  private state: PartyState = { partyId: 0, leader: -1, members: [], pvpFlag: false };
  private invites: Invite[] = [];

  constructor(private readonly ctx: ClientContext) {
    ctx.router.on(Fo4MsgType.PartyAction, (m) => this.onParty(m));
  }

  get(): PartyState {
    return this.state;
  }

  inParty(): boolean {
    return this.state.partyId !== 0;
  }

  isLeader(): boolean {
    return this.inParty() && this.state.leader === this.ctx.session.profileId;
  }

  isMember(profileId: number): boolean {
    return this.state.members.includes(profileId);
  }

  pendingInvites(): Invite[] {
    const now = this.ctx.platform.nowMs();
    this.invites = this.invites.filter((i) => i.expiresAtMs > now);
    return this.invites.slice();
  }

  invite(profileId: number): Promise<PartyOutcome> {
    if (profileId === this.ctx.session.profileId) {
      return this.reject("CannotTargetSelf");
    }
    if (this.inParty() && !this.isLeader()) {
      return this.reject("NotLeader");
    }
    return this.request(PartyOp.Invite, { targetProfileId: profileId });
  }

  accept(partyId: number): Promise<PartyOutcome> {
    this.invites = this.invites.filter((i) => i.partyId !== partyId);
    return this.request(PartyOp.Accept, { partyId });
  }

  decline(partyId: number): Promise<PartyOutcome> {
    this.invites = this.invites.filter((i) => i.partyId !== partyId);
    return this.request(PartyOp.Decline, { partyId });
  }

  leave(): Promise<PartyOutcome> {
    if (!this.inParty()) {
      return this.reject("NotInParty");
    }
    return this.request(PartyOp.Leave, {});
  }

  kick(profileId: number): Promise<PartyOutcome> {
    if (!this.isLeader()) {
      return this.reject("NotLeader");
    }
    return this.request(PartyOp.Kick, { targetProfileId: profileId });
  }

  promote(profileId: number): Promise<PartyOutcome> {
    if (!this.isLeader()) {
      return this.reject("NotLeader");
    }
    return this.request(PartyOp.Promote, { targetProfileId: profileId });
  }

  setPvpFlag(flag: boolean): Promise<PartyOutcome> {
    return this.request(PartyOp.SetPvpFlag, { value: flag });
  }

  reset(): void {
    this.state = { partyId: 0, leader: -1, members: [], pvpFlag: false };
    this.invites = [];
  }

  private request(op: PartyOp, fields: Partial<PartyActionMessage>): Promise<PartyOutcome> {
    const nonce = this.ctx.requests.nextNonce();
    const promise = this.ctx.requests.track<PartyActionMessage>(nonce, Fo4MsgType.PartyAction);
    this.ctx.send(Fo4MsgType.PartyAction, { ...fields, nonce, op });
    return promise.then((r) => {
      if (!r.ok) {
        this.ctx.reportFailure(Fo4MsgType.PartyAction, r.error);
      }
      return r;
    });
  }

  private reject(error: string): Promise<PartyOutcome> {
    this.ctx.reportFailure(Fo4MsgType.PartyAction, error);
    return Promise.resolve(failed(Fo4MsgType.PartyAction, error));
  }

  private onParty(m: PartyActionMessage): void {
    if (m.op === PartyOp.Invite) {
      const invite: Invite = {
        partyId: m.partyId,
        fromProfileId: m.targetProfileId,
        expiresAtMs: this.ctx.platform.nowMs() + kInviteTtlMs,
      };
      this.invites = this.invites.filter((i) => i.partyId !== m.partyId).concat([invite]);
      this.ctx.events.emit("partyInvite", invite);
      return;
    }
    if (m.op !== PartyOp.State) {
      return;
    }
    const next: PartyState = {
      partyId: m.partyId,
      leader: m.partyId ? m.leader : -1,
      members: m.partyId ? m.members : [],
      pvpFlag: m.value,
    };
    const changed =
      next.partyId !== this.state.partyId ||
      next.leader !== this.state.leader ||
      next.pvpFlag !== this.state.pvpFlag ||
      next.members.join(",") !== this.state.members.join(",");
    this.state = next;
    if (m.nonce) {
      this.ctx.requests.complete<PartyActionMessage>(m.nonce, {
        ok: !m.error,
        error: m.error,
        refId: m.partyId,
        items: [],
        reply: m,
      });
    }
    if (changed) {
      this.ctx.events.emit("partyChanged", next);
    }
  }
}
