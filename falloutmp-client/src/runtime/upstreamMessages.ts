// The core server messages (shared with the server engine) the client handles
// (falloutmp-server/cpp/messages/MsgType.h). Field names are the JSON the
// native codec produces from the binary messages.
import { Vec3 } from "../services/messages/fo4Messages";

export enum UpstreamMsgType {
  CustomPacket = 1,
  Teleport = 20,
  DestroyActor = 25,
  HostStart = 26,
  HostStop = 27,
  SetRaceMenuOpen = 29,
  CreateActor = 33,
}

export interface CreateActorMessage {
  t: UpstreamMsgType.CreateActor;
  idx: number; // the server's slot index, not a form id
  isMe: boolean;
  transform: { worldOrCell: number; pos: Vec3; rot: Vec3 };
  // Form id, offset by 0x100000000 for references from plugins
  refrId?: number;
  baseId?: number;
  baseRecordType?: string;
  appearance?: { name?: string; isFemale?: boolean };
  isDead?: boolean;
  props?: { isDisabled?: boolean; isHostedByOther?: boolean; isDead?: boolean; isRaceMenuOpen?: boolean };
}

export interface DestroyActorMessage {
  t: UpstreamMsgType.DestroyActor;
  idx: number;
}

export interface TeleportMessage {
  t: UpstreamMsgType.Teleport;
  idx: number;
  pos: Vec3;
  rot: Vec3;
  worldOrCell: number;
}

export interface HostMessage {
  t: UpstreamMsgType.HostStart | UpstreamMsgType.HostStop;
  target: number; // long form id
}

export interface CustomPacketMessage {
  t: UpstreamMsgType.CustomPacket;
  contentJsonDump: string;
}

export function loginPacket(profileId: number): CustomPacketMessage {
  return {
    t: UpstreamMsgType.CustomPacket,
    contentJsonDump: JSON.stringify({ customPacketType: "loginWithProfileId", gameData: { profileId } }),
  };
}
