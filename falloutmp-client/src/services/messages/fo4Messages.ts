// TypeScript mirror of skymp5-server/cpp/messages/Fo4Messages.h.
//
// The server's JSON reader rejects a message with a missing field, so
// requests must always be built with buildMessage(), which starts from the
// complete default object below. Fields that are std::optional in C++ are
// optional here and are listed in kOptionalFields; the server omits them from
// JSON when they have no value. test/protocolParity.test.ts parses the C++
// header and fails on any drift in field names, optionality or type ids.
import { Fo4MsgType } from "./msgType";

export type Vec3 = [number, number, number];

export interface ItemKey {
  baseId: number;
  mods: number[];
  condition: number; // 0 = full/untracked, 0xFFFF = broken (fo4/Condition.h)
  stolenFrom: number;
  ammoLoaded: number;
}

export interface ItemCount {
  item: ItemKey;
  count: number;
}

export interface AvValue {
  avId: number;
  current: number;
  max: number;
}

// F01: owner transform (C->S ~10 Hz) and relay (S->C, ts = server ms).
export interface UpdateMovementFo4Message {
  t: Fo4MsgType.UpdateMovementFo4;
  idx: number;
  seq: number; // u16
  ts: number; // u32
  worldOrCell: number;
  pos: Vec3;
  yaw: number; // degrees
  aimPitch: number;
  aimHeading: number;
  speed: number;
  direction: number;
  velZ: number;
  flags: number; // MoveFlag bits
  healthPercentage: number;
}

export interface SetInventoryFo4Message {
  t: Fo4MsgType.SetInventoryFo4;
  refId: number; // 0 = the receiving player
  version: number;
  entries: ItemCount[];
}

interface ContainerOpFields {
  nonce: number;
  refId: number;
  item: ItemKey;
  count: number;
  pos: Vec3;
}
export interface PutItemFo4Message extends ContainerOpFields {
  t: Fo4MsgType.PutItemFo4;
}
export interface TakeItemFo4Message extends ContainerOpFields {
  t: Fo4MsgType.TakeItemFo4;
}
export interface DropItemFo4Message extends ContainerOpFields {
  t: Fo4MsgType.DropItemFo4;
}

export interface ChangeValuesAvMessage {
  t: Fo4MsgType.ChangeValuesAv;
  idx: number;
  values: AvValue[];
}

export interface CraftItemFo4Message {
  t: Fo4MsgType.CraftItemFo4;
  nonce: number;
  workbenchRefId: number;
  recipeId: number;
  count: number;
}

export interface WeaponFireMessage {
  t: Fo4MsgType.WeaponFire;
  shooterIdx: number;
  seq: number;
  weaponBaseId: number;
  origin: Vec3;
  direction: Vec3;
  clientShotId: number; // shooter's own id, echoed back to the shooter only
}

export interface WeaponReloadMessage {
  t: Fo4MsgType.WeaponReload;
  nonce: number;
  loaded: number;
  ok: boolean;
}

export interface HitReportMessage {
  t: Fo4MsgType.HitReport;
  shotSeq: number;
  projectileIndex: number;
  targetIdx: number;
  limb: number;
}

export interface DamageAppliedMessage {
  t: Fo4MsgType.DamageApplied;
  targetIdx: number;
  aggressorIdx: number;
  total: number;
  amounts: number[];
  limb: number;
  critical: boolean;
  killed: boolean;
}

export interface ModItemMessage {
  t: Fo4MsgType.ModItem;
  nonce: number;
  workbenchRefId: number;
  item: ItemKey;
  modId: number;
  op: number; // ModOp
}

export interface ScrapItemMessage {
  t: Fo4MsgType.ScrapItem;
  nonce: number;
  workbenchRefId: number;
  item: ItemKey;
  count: number;
}

export interface PowerArmorTransitionMessage {
  t: Fo4MsgType.PowerArmorTransition;
  nonce: number;
  actorIdx: number;
  frameRefId: number;
  kind: number; // PaTransitionKind
  phase: number; // PaPhase
  ok: boolean;
  error: string;
  exitPos: Vec3;
}

export interface PowerArmorPiece {
  slot: number; // PowerArmorSlot
  item: ItemKey;
  healthPct: number;
}

export interface PowerArmorStateMessage {
  t: Fo4MsgType.PowerArmorState;
  actorIdx: number;
  frameRefId: number;
  phase: number;
  frameBaseId: number;
  pieces: PowerArmorPiece[];
  coreBaseId: number;
  coreCharge?: number; // owner only
  unpowered: boolean;
  jetpackCapable: boolean;
}

export type SpecialArray = [number, number, number, number, number, number, number];

export interface ProgressionUpdateMessage {
  t: Fo4MsgType.ProgressionUpdate;
  level: number;
  xp: number;
  xpForNextLevel: number;
  perkPoints: number;
  special: SpecialArray;
  perks: number[];
  created: boolean;
}

export interface ProgressionRequestMessage {
  t: Fo4MsgType.ProgressionRequest;
  nonce: number;
  op: number; // ProgressionOp
  special: SpecialArray;
  chartKey: string;
  specialAv: number;
}

export interface UseItemMessage {
  t: Fo4MsgType.UseItem;
  nonce: number;
  baseId: number;
}

export interface ActiveEffectEntry {
  effectId: number;
  sourceItem: number;
  kind: number; // EffectKind
  avId: number;
  magnitude: number; // 0 for neighbours
  remainingMs: number; // 0 for neighbours
}

// F20: owner full list + addictions; neighbours ids only (visuals)
export interface EffectsUpdateMessage {
  t: Fo4MsgType.EffectsUpdate;
  idx: number;
  full: boolean;
  effects: ActiveEffectEntry[];
  addictions: number[];
}

export interface WorkshopModeMessage {
  t: Fo4MsgType.WorkshopMode;
  workshopRefId: number;
  enter: boolean;
  allowed: boolean;
  reason: string;
  perms: number; // WorkshopPerm bits
}

export interface WorkshopPlaceMessage {
  t: Fo4MsgType.WorkshopPlace;
  nonce: number;
  workshopRefId: number;
  recipeId: number;
  baseId: number;
  fromStored: boolean;
  pos: Vec3;
  rot: Vec3;
  scale: number;
  snapTargetRefId: number;
}

export interface WorkshopEditItem {
  refId: number;
  pos: Vec3;
  rot: Vec3;
}

export interface WorkshopEditMessage {
  t: Fo4MsgType.WorkshopEdit;
  nonce: number;
  workshopRefId: number;
  op: number; // WorkshopEditOp
  items: WorkshopEditItem[];
}

export interface WorkshopStateMessage {
  t: Fo4MsgType.WorkshopState;
  workshopRefId: number;
  version: number;
  ownerType: number; // WorkshopOwnerType
  ownerId: number;
  yourPerms: number;
  budgetCurrent: number;
  budgetMax: number;
  objects: number;
  maxObjects: number;
  food: number;
  water: number;
  safety: number;
  beds: number;
  power: number;
  powerLoad: number;
  population: number;
  happiness: number;
}

export interface WorkshopWireMessage {
  t: Fo4MsgType.WorkshopWire;
  nonce: number;
  workshopRefId: number;
  op: number; // WireOp
  a: number;
  b: number;
  splineBaseId: number;
  wireRefId: number;
}

export interface BarterMessage {
  t: Fo4MsgType.Barter;
  nonce: number;
  op: number; // BarterOp
  vendorId: number;
  buy: ItemCount[];
  sell: ItemCount[];
  capsDelta: number;
  error: string;
}

export interface LockpickAttemptMessage {
  t: Fo4MsgType.LockpickAttempt;
  op: number; // LockpickOp
  refId: number;
  sessionId: number;
  outcome: number; // LockResultCode
  xp: number;
}

export interface TerminalActionMessage {
  t: Fo4MsgType.TerminalAction;
  op: number; // TerminalOp
  refId: number;
  sessionId: number;
  outcome: number; // HackResultCode
  attemptsLeft: number;
  xp: number;
}

export interface RequestResultMessage {
  t: Fo4MsgType.RequestResult;
  nonce: number;
  requestType: number; // Fo4MsgType of the request
  ok: boolean;
  error: string;
  refId: number;
  items: ItemCount[];
}

export interface WorkshopObject {
  refId: number;
  baseId: number;
  pos: Vec3;
  rot: Vec3;
  scale: number;
  flags: number;
}

export interface WorkshopWireEntry {
  wireRefId: number;
  a: number;
  b: number;
  splineBaseId: number;
}

export interface WorkshopObjectsMessage {
  t: Fo4MsgType.WorkshopObjects;
  workshopRefId: number;
  version: number;
  kind: number; // WorkshopObjectsKind
  chunk: number;
  chunkCount: number;
  added: WorkshopObject[];
  removed: number[];
  scrappedPrePlaced: number[];
  wires: WorkshopWireEntry[];
}

export interface WorkshopManageMessage {
  t: Fo4MsgType.WorkshopManage;
  nonce: number;
  workshopRefId: number;
  op: number; // WorkshopManageOp
  actorId: number;
  objectRefId: number;
  profileId: number;
  perms: number;
}

export interface PartyActionMessage {
  t: Fo4MsgType.PartyAction;
  nonce: number;
  op: number; // PartyOp
  targetProfileId: number;
  partyId: number;
  value: boolean;
  leader: number;
  members: number[];
  error: string;
}

export interface UpdateEquipmentFo4Message {
  t: Fo4MsgType.UpdateEquipmentFo4;
  actorIdx: number;
  op: number; // EquipOp
  item: ItemKey;
  weapon?: ItemKey;
  armor: ItemKey[];
}

export interface MapMarkerEntry {
  refId: number;
  name: string;
  type: number;
  pos: Vec3;
}

export interface MapDiscoveryMessage {
  t: Fo4MsgType.MapDiscovery;
  full: boolean;
  markers: MapMarkerEntry[];
}

export interface FastTravelRequestMessage {
  t: Fo4MsgType.FastTravelRequest;
  nonce: number;
  markerRefId: number;
}

export interface WorldTimeWeatherMessage {
  t: Fo4MsgType.WorldTimeWeather;
  gameDays: number;
  gameHour: number;
  timeScale: number;
  weatherId: number;
  transitionSec: number;
  radstorm: boolean;
  serverNowMs: number;
}

// Every Fallout 4 message implemented on both sides, keyed by type id.
export interface Fo4MessageMap {
  [Fo4MsgType.UpdateMovementFo4]: UpdateMovementFo4Message;
  [Fo4MsgType.SetInventoryFo4]: SetInventoryFo4Message;
  [Fo4MsgType.PutItemFo4]: PutItemFo4Message;
  [Fo4MsgType.TakeItemFo4]: TakeItemFo4Message;
  [Fo4MsgType.DropItemFo4]: DropItemFo4Message;
  [Fo4MsgType.ChangeValuesAv]: ChangeValuesAvMessage;
  [Fo4MsgType.CraftItemFo4]: CraftItemFo4Message;
  [Fo4MsgType.WeaponFire]: WeaponFireMessage;
  [Fo4MsgType.WeaponReload]: WeaponReloadMessage;
  [Fo4MsgType.HitReport]: HitReportMessage;
  [Fo4MsgType.DamageApplied]: DamageAppliedMessage;
  [Fo4MsgType.ModItem]: ModItemMessage;
  [Fo4MsgType.ScrapItem]: ScrapItemMessage;
  [Fo4MsgType.PowerArmorTransition]: PowerArmorTransitionMessage;
  [Fo4MsgType.PowerArmorState]: PowerArmorStateMessage;
  [Fo4MsgType.ProgressionUpdate]: ProgressionUpdateMessage;
  [Fo4MsgType.ProgressionRequest]: ProgressionRequestMessage;
  [Fo4MsgType.UseItem]: UseItemMessage;
  [Fo4MsgType.EffectsUpdate]: EffectsUpdateMessage;
  [Fo4MsgType.WorkshopMode]: WorkshopModeMessage;
  [Fo4MsgType.WorkshopPlace]: WorkshopPlaceMessage;
  [Fo4MsgType.WorkshopEdit]: WorkshopEditMessage;
  [Fo4MsgType.WorkshopState]: WorkshopStateMessage;
  [Fo4MsgType.WorkshopWire]: WorkshopWireMessage;
  [Fo4MsgType.Barter]: BarterMessage;
  [Fo4MsgType.LockpickAttempt]: LockpickAttemptMessage;
  [Fo4MsgType.TerminalAction]: TerminalActionMessage;
  [Fo4MsgType.RequestResult]: RequestResultMessage;
  [Fo4MsgType.WorkshopObjects]: WorkshopObjectsMessage;
  [Fo4MsgType.WorkshopManage]: WorkshopManageMessage;
  [Fo4MsgType.PartyAction]: PartyActionMessage;
  [Fo4MsgType.UpdateEquipmentFo4]: UpdateEquipmentFo4Message;
  [Fo4MsgType.MapDiscovery]: MapDiscoveryMessage;
  [Fo4MsgType.FastTravelRequest]: FastTravelRequestMessage;
  [Fo4MsgType.WorldTimeWeather]: WorldTimeWeatherMessage;
}

export type ImplementedFo4MsgType = keyof Fo4MessageMap;
export type AnyFo4Message = Fo4MessageMap[ImplementedFo4MsgType];

export function emptyItemKey(baseId = 0): ItemKey {
  return { baseId, mods: [], condition: 0, stolenFrom: 0, ammoLoaded: 0 };
}

const v0 = (): Vec3 => [0, 0, 0];
const special1 = (): SpecialArray => [1, 1, 1, 1, 1, 1, 1];

// Complete default bodies (without "t"). Factories, so nested arrays are
// never shared between messages.
type DefaultsTable = {
  [K in ImplementedFo4MsgType]: () => Omit<Fo4MessageMap[K], "t">;
};

const containerOp = (): ContainerOpFields => ({
  nonce: 0,
  refId: 0,
  item: emptyItemKey(),
  count: 1,
  pos: v0(),
});

export const kMessageDefaults: DefaultsTable = {
  [Fo4MsgType.UpdateMovementFo4]: () => ({
    idx: 0,
    seq: 0,
    ts: 0,
    worldOrCell: 0,
    pos: v0(),
    yaw: 0,
    aimPitch: 0,
    aimHeading: 0,
    speed: 0,
    direction: 0,
    velZ: 0,
    flags: 0,
    healthPercentage: 100,
  }),
  [Fo4MsgType.SetInventoryFo4]: () => ({ refId: 0, version: 0, entries: [] }),
  [Fo4MsgType.PutItemFo4]: containerOp,
  [Fo4MsgType.TakeItemFo4]: containerOp,
  [Fo4MsgType.DropItemFo4]: containerOp,
  [Fo4MsgType.ChangeValuesAv]: () => ({ idx: 0, values: [] }),
  [Fo4MsgType.CraftItemFo4]: () => ({
    nonce: 0,
    workbenchRefId: 0,
    recipeId: 0,
    count: 1,
  }),
  [Fo4MsgType.WeaponFire]: () => ({
    shooterIdx: 0,
    seq: 0,
    weaponBaseId: 0,
    origin: v0(),
    direction: v0(),
    clientShotId: 0,
  }),
  [Fo4MsgType.WeaponReload]: () => ({ nonce: 0, loaded: 0, ok: false }),
  [Fo4MsgType.HitReport]: () => ({
    shotSeq: 0,
    projectileIndex: 0,
    targetIdx: 0,
    limb: 0,
  }),
  [Fo4MsgType.DamageApplied]: () => ({
    targetIdx: 0,
    aggressorIdx: 0,
    total: 0,
    amounts: [],
    limb: 0,
    critical: false,
    killed: false,
  }),
  [Fo4MsgType.ModItem]: () => ({
    nonce: 0,
    workbenchRefId: 0,
    item: emptyItemKey(),
    modId: 0,
    op: 0,
  }),
  [Fo4MsgType.ScrapItem]: () => ({
    nonce: 0,
    workbenchRefId: 0,
    item: emptyItemKey(),
    count: 1,
  }),
  [Fo4MsgType.PowerArmorTransition]: () => ({
    nonce: 0,
    actorIdx: 0,
    frameRefId: 0,
    kind: 0,
    phase: 0,
    ok: true,
    error: "",
    exitPos: v0(),
  }),
  [Fo4MsgType.PowerArmorState]: () => ({
    actorIdx: 0,
    frameRefId: 0,
    phase: 0,
    frameBaseId: 0,
    pieces: [],
    coreBaseId: 0,
    unpowered: false,
    jetpackCapable: false,
  }),
  [Fo4MsgType.ProgressionUpdate]: () => ({
    level: 1,
    xp: 0,
    xpForNextLevel: 0,
    perkPoints: 0,
    special: special1(),
    perks: [],
    created: false,
  }),
  [Fo4MsgType.ProgressionRequest]: () => ({
    nonce: 0,
    op: 1,
    special: special1(),
    chartKey: "",
    specialAv: 0,
  }),
  [Fo4MsgType.UseItem]: () => ({ nonce: 0, baseId: 0 }),
  [Fo4MsgType.EffectsUpdate]: () => ({ idx: 0, full: true, effects: [], addictions: [] }),
  [Fo4MsgType.WorkshopMode]: () => ({
    workshopRefId: 0,
    enter: true,
    allowed: false,
    reason: "",
    perms: 0,
  }),
  [Fo4MsgType.WorkshopPlace]: () => ({
    nonce: 0,
    workshopRefId: 0,
    recipeId: 0,
    baseId: 0,
    fromStored: false,
    pos: v0(),
    rot: v0(),
    scale: 1,
    snapTargetRefId: 0,
  }),
  [Fo4MsgType.WorkshopEdit]: () => ({
    nonce: 0,
    workshopRefId: 0,
    op: 0,
    items: [],
  }),
  [Fo4MsgType.WorkshopState]: () => ({
    workshopRefId: 0,
    version: 0,
    ownerType: 0,
    ownerId: -1,
    yourPerms: 0,
    budgetCurrent: 0,
    budgetMax: 0,
    objects: 0,
    maxObjects: 0,
    food: 0,
    water: 0,
    safety: 0,
    beds: 0,
    power: 0,
    powerLoad: 0,
    population: 0,
    happiness: 0,
  }),
  [Fo4MsgType.WorkshopWire]: () => ({
    nonce: 0,
    workshopRefId: 0,
    op: 0,
    a: 0,
    b: 0,
    splineBaseId: 0,
    wireRefId: 0,
  }),
  [Fo4MsgType.Barter]: () => ({
    nonce: 0,
    op: 0,
    vendorId: 0,
    buy: [],
    sell: [],
    capsDelta: 0,
    error: "",
  }),
  [Fo4MsgType.LockpickAttempt]: () => ({
    op: 0,
    refId: 0,
    sessionId: 0,
    outcome: 0,
    xp: 0,
  }),
  [Fo4MsgType.TerminalAction]: () => ({
    op: 0,
    refId: 0,
    sessionId: 0,
    outcome: 0,
    attemptsLeft: 0,
    xp: 0,
  }),
  [Fo4MsgType.RequestResult]: () => ({
    nonce: 0,
    requestType: 0,
    ok: false,
    error: "",
    refId: 0,
    items: [],
  }),
  [Fo4MsgType.WorkshopObjects]: () => ({
    workshopRefId: 0,
    version: 0,
    kind: 0,
    chunk: 0,
    chunkCount: 1,
    added: [],
    removed: [],
    scrappedPrePlaced: [],
    wires: [],
  }),
  [Fo4MsgType.WorkshopManage]: () => ({
    nonce: 0,
    workshopRefId: 0,
    op: 0,
    actorId: 0,
    objectRefId: 0,
    profileId: -1,
    perms: 0,
  }),
  [Fo4MsgType.PartyAction]: () => ({
    nonce: 0,
    op: 0,
    targetProfileId: -1,
    partyId: 0,
    value: false,
    leader: -1,
    members: [],
    error: "",
  }),
  [Fo4MsgType.UpdateEquipmentFo4]: () => ({
    actorIdx: 0,
    op: 2,
    item: emptyItemKey(),
    armor: [],
  }),
  [Fo4MsgType.MapDiscovery]: () => ({ full: false, markers: [] }),
  [Fo4MsgType.FastTravelRequest]: () => ({ nonce: 0, markerRefId: 0 }),
  [Fo4MsgType.WorldTimeWeather]: () => ({
    gameDays: 0,
    gameHour: 0,
    timeScale: 20,
    weatherId: 0,
    transitionSec: 10,
    radstorm: false,
    serverNowMs: 0,
  }),
};

// Fields that are std::optional on the C++ side.
export const kOptionalFields: { [K in ImplementedFo4MsgType]?: string[] } = {
  [Fo4MsgType.PowerArmorState]: ["coreCharge"],
  [Fo4MsgType.UpdateEquipmentFo4]: ["weapon"],
};

// Default bodies of the nested structs, for the parity test and builders.
export const kNestedDefaults = {
  ItemKey: () => emptyItemKey(),
  ItemCount: (): ItemCount => ({ item: emptyItemKey(), count: 0 }),
  AvValue: (): AvValue => ({ avId: 0, current: 0, max: 0 }),
  Piece: (): PowerArmorPiece => ({
    slot: 0,
    item: emptyItemKey(),
    healthPct: 100,
  }),
  Item: (): WorkshopEditItem => ({ refId: 0, pos: v0(), rot: v0() }),
  Object: (): WorkshopObject => ({
    refId: 0,
    baseId: 0,
    pos: v0(),
    rot: v0(),
    scale: 1,
    flags: 0,
  }),
  WireEntry: (): WorkshopWireEntry => ({
    wireRefId: 0,
    a: 0,
    b: 0,
    splineBaseId: 0,
  }),
  Marker: (): MapMarkerEntry => ({ refId: 0, name: "", type: 0, pos: v0() }),
  Effect: (): ActiveEffectEntry => ({ effectId: 0, sourceItem: 0, kind: 0, avId: 0, magnitude: 0, remainingMs: 0 }),
};

export type MessageFields<K extends ImplementedFo4MsgType> = Partial<
  Omit<Fo4MessageMap[K], "t">
>;

// Builds a complete message: every non-optional field is present.
export function buildMessage<K extends ImplementedFo4MsgType>(
  t: K,
  fields: MessageFields<K> = {},
): Fo4MessageMap[K] {
  const base = kMessageDefaults[t]() as Omit<Fo4MessageMap[K], "t">;
  const out: Record<string, unknown> = { t, ...base };
  for (const [k, v] of Object.entries(fields)) {
    if (v !== undefined) {
      out[k] = v;
    }
  }
  return out as unknown as Fo4MessageMap[K];
}

// Narrowing helper for incoming JSON.
export function isMessageOfType<K extends ImplementedFo4MsgType>(
  msg: { t: number },
  t: K,
): msg is Fo4MessageMap[K] {
  return msg.t === t;
}
