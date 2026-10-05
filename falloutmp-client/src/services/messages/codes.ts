// Enum mirrors of the server's fo4 namespace (server_guest_lib/fo4/*.h).
// Values travel as uint8_t; the parity test checks the ones that are C++
// enums with explicit names.

export enum PaPhase {
  Out = 0,
  Entering = 1,
  In = 2,
  Exiting = 3,
}

export enum PaTransitionKind {
  Enter = 0,
  Exit = 1,
  Ack = 2,
  EjectCore = 3,
}

export enum PowerArmorSlot {
  None = 0,
  Helmet = 1,
  Torso = 2,
  LeftArm = 3,
  RightArm = 4,
  LeftLeg = 5,
  RightLeg = 6,
}

export enum ProgressionOp {
  CreateCharacter = 0,
  BuyPerk = 1,
  BuySpecial = 2,
}

export enum ModOp {
  Attach = 0,
  Detach = 1,
}

export enum EquipOp {
  Equip = 0,
  Unequip = 1,
  State = 2,
}

export enum WorkshopEditOp {
  Move = 0,
  Scrap = 1,
  Store = 2,
  Repair = 3,
}

export enum WorkshopManageOp {
  Claim = 0,
  Abandon = 1,
  SetAcl = 2,
  Assign = 3,
}

export enum WorkshopOwnerType {
  None = 0,
  Profile = 1,
  Group = 2,
}

export enum WorkshopObjectsKind {
  Snapshot = 0,
  Delta = 1,
}

export enum WireOp {
  Connect = 0,
  Disconnect = 1,
}

export const WorkshopPerm = {
  Build: 1 << 0,
  Scrap: 1 << 1,
  Container: 1 << 2,
  Craft: 1 << 3,
  Assign: 1 << 4,
  Admin: 1 << 5,
  All: 0x3f,
} as const;

export function hasPerm(perms: number, perm: number): boolean {
  return (perms & perm) === perm;
}

export enum BarterOp {
  Quote = 0,
  Trade = 1,
}

export enum LockpickOp {
  Activate = 0,
  Attempt = 1,
  Cancel = 2,
}

export enum TerminalOp {
  Begin = 0,
  Attempt = 1,
}

export enum LockResultCode {
  Opened = 0,
  OpenedWithKey,
  BeginLockpick,
  NeedsKey,
  Barred,
  Chained,
  NeedsTerminal,
  Inaccessible,
  PerkTooLow,
  NoPins,
  Busy,
  NoSession,
  Unlocked,
  Failed,
  SessionExpired,
}

export enum HackResultCode {
  Open = 0,
  BeginHack,
  PerkTooLow,
  LockedOut,
  Busy,
  NoSession,
  Hacked,
  WrongGuess,
  LockoutStarted,
}

export enum PartyOp {
  Invite = 0,
  Accept = 1,
  Decline = 2,
  Leave = 3,
  Kick = 4,
  Promote = 5,
  SetPvpFlag = 6,
  State = 100,
}

// Map marker types are the FO4 TNAM values; only the ones the HUD treats
// specially are named.
export enum MapMarkerType {
  None = 0,
}

// Limb indices used by HitReport/DamageApplied (F11 §4.3).
export enum Limb {
  Torso = 0,
  Head = 1,
  LeftArm = 2,
  RightArm = 3,
  LeftLeg = 4,
  RightLeg = 5,
}

// Actor value ids the client reads by name (fo4/ActorValues.h, Av::).
// These are the vanilla AVIF form ids.
export const Av = {
  Strength: 0x2c2,
  Perception: 0x2c3,
  Endurance: 0x2c4,
  Charisma: 0x2c5,
  Intelligence: 0x2c6,
  Agility: 0x2c7,
  Luck: 0x2c8,
  Experience: 0x2c9,
  Health: 0x2d4,
  ActionPoints: 0x2d5,
  CarryWeight: 0x2dc,
  Rads: 0x2e1,
  DamageResist: 0x2e3,
  EnergyResist: 0x2eb,
  PowerArmorBattery: 0x35c,
  PerceptionCondition: 0x36c, // head
  EnduranceCondition: 0x36d, // torso
  LeftAttackCondition: 0x36e,
  RightAttackCondition: 0x36f,
  LeftMobilityCondition: 0x370,
  RightMobilityCondition: 0x371,
} as const;

export const kLimbConditionAvs = [
  Av.PerceptionCondition,
  Av.EnduranceCondition,
  Av.LeftAttackCondition,
  Av.RightAttackCondition,
  Av.LeftMobilityCondition,
  Av.RightMobilityCondition,
] as const;

export const kSpecialAvs = [
  Av.Strength,
  Av.Perception,
  Av.Endurance,
  Av.Charisma,
  Av.Intelligence,
  Av.Agility,
  Av.Luck,
] as const;

// Readable text for the server's error codes. The server sends the C++
// enum names (e.g. "MissingComponents"); unknown codes fall back to a
// spaced version of the identifier.
const kErrorText: Record<string, string> = {
  Timeout: "The server did not answer in time.",
  Disconnected: "Connection to the server was lost.",
  OutOfReach: "You are too far away.",
  NoWorkbench: "That is not a workbench.",
  UnknownRecipe: "Unknown recipe.",
  WrongWorkbench: "This recipe needs a different workbench.",
  MissingPerk: "You lack the perk required.",
  MissingComponents: "You don't have the components.",
  NotScrappable: "That item can't be scrapped.",
  ItemNotFound: "You don't have that item.",
  IncompatibleMod: "That mod doesn't fit this item.",
  NotModifiable: "That item can't be modified.",
  Occupied: "Someone else is using that power armor.",
  InTransition: "Wait for the current action to finish.",
  AlreadyInPowerArmor: "You are already in power armor.",
  NotInPowerArmor: "You are not in power armor.",
  InCombat: "You can't do that in combat.",
  OwnedByOther: "That belongs to someone else.",
  Airborne: "You can't do that while falling.",
  NotOwned: "Nobody owns this settlement.",
  AlreadyOwned: "This settlement is already owned.",
  NoPermission: "You don't have permission here.",
  NotInBuildMode: "Enter workshop mode first.",
  OutsideBuildArea: "That is outside the build area.",
  TooFar: "That is too far away.",
  OverBudget: "The settlement build limit is reached.",
  RateLimited: "Slow down.",
  ClaimLimit: "You own too many settlements.",
  LeftBuildArea: "You left the settlement.",
  NotDiscovered: "You haven't discovered that location.",
  OverEncumbered: "You are carrying too much to fast travel.",
  InBuildMode: "Leave workshop mode first.",
  Dead: "You are dead.",
  NotConsumable: "You can't use that.",
  NotInInventory: "You don't have that item.",
  Broken: "That item is broken.",
  InPowerArmor: "You can't wear that in power armor.",
  PowerArmorPiece: "Power armor pieces go on a frame.",
  NotEquippable: "That can't be equipped.",
  Locked: "It's locked.",
  NotLeader: "Only the party leader can do that.",
  PartyFull: "The party is full.",
  AlreadyInParty: "That player is already in a party.",
  NoInvite: "There is no invite to accept.",
  InviteExpired: "The invite expired.",
  FlagCooldown: "You changed your PvP flag too recently.",
  NotEnoughCaps: "You don't have enough caps.",
  VendorNotEnoughCaps: "The vendor doesn't have enough caps.",
  PriceChanged: "Prices changed; check the offer again.",
  ItemNotAvailable: "The vendor no longer has that item.",
  VendorWontBuy: "The vendor won't buy that.",
  StolenFromVendor: "The vendor won't buy back stolen goods.",
  NoPerkPoints: "You have no perk points.",
  LevelTooLow: "Your level is too low.",
  SpecialTooLow: "Your S.P.E.C.I.A.L. is too low for that perk.",
  MaxRank: "That perk is already at its highest rank.",
  NeedsKey: "You need the key.",
  NeedsTerminal: "This is opened from a terminal.",
  PerkTooLow: "Your skill is too low for this lock.",
  NoPins: "You have no bobby pins.",
  LockedOut: "The terminal is locked.",
  TeleportFailed: "Fast travel failed.",
  NoSuchContainer: "That container no longer exists.",
};

export function describeError(code: string): string {
  if (!code) {
    return "";
  }
  const known = kErrorText[code];
  if (known) {
    return known;
  }
  const spaced = code.replace(/([a-z])([A-Z])/g, "$1 $2").toLowerCase();
  return spaced.charAt(0).toUpperCase() + spaced.slice(1) + ".";
}
