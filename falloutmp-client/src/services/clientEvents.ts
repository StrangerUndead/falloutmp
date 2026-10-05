// Events the client raises for the UI (CEF front, HUD) and for gamemode
// client scripts. All payloads are plain data.
import {
  ActiveEffectEntry,
  AvValue,
  ItemCount,
  ItemKey,
  MapMarkerEntry,
  PowerArmorPiece,
  SpecialArray,
  WorkshopStateMessage,
} from "./messages/fo4Messages";

export interface ProgressionState {
  level: number;
  xp: number;
  xpForNextLevel: number;
  perkPoints: number;
  special: SpecialArray;
  perks: number[];
  created: boolean;
}

export interface PowerArmorActorState {
  actorIdx: number;
  frameRefId: number;
  phase: number;
  frameBaseId: number;
  pieces: PowerArmorPiece[];
  coreBaseId: number;
  coreCharge?: number;
  unpowered: boolean;
  jetpackCapable: boolean;
}

export interface PartyState {
  partyId: number; // 0 = not in a party
  leader: number;
  members: number[];
  pvpFlag: boolean;
}

export interface ClientEvents {
  appearanceApplied: { actor: number; ok: boolean };
  inventoryChanged: { refId: number; version: number; entries: ItemCount[] };
  actorValuesChanged: { actorIdx: number; values: AvValue[] };
  equipmentChanged: { actorIdx: number; weapon?: ItemKey; armor: ItemKey[] };
  progressionChanged: ProgressionState;
  levelUp: { level: number; perkPoints: number };
  characterCreationRequired: Record<string, never>;
  powerArmorChanged: PowerArmorActorState;
  workshopModeChanged: { workshopRefId: number; active: boolean; perms: number; reason: string };
  workshopStateChanged: WorkshopStateMessage;
  workshopObjectsChanged: { workshopRefId: number; version: number; objectCount: number };
  crafted: { recipeId: number; items: ItemCount[] };
  scrapped: { items: ItemCount[] };
  lockOutcome: { refId: number; outcome: number; xp: number };
  hackOutcome: { refId: number; outcome: number; attemptsLeft: number; xp: number };
  partyChanged: PartyState;
  partyInvite: { partyId: number; fromProfileId: number; expiresAtMs: number };
  mapMarkersChanged: { markers: MapMarkerEntry[]; discovered: MapMarkerEntry[] };
  timeWeatherChanged: { gameDays: number; gameHour: number; timeScale: number; weatherId: number; radstorm: boolean };
  damageApplied: { targetIdx: number; aggressorIdx: number; total: number; limb: number; critical: boolean; killed: boolean };
  localPlayerKilled: { aggressorIdx: number };
  effectsChanged: { actorIdx: number; effects: ActiveEffectEntry[]; addictions: number[] };
  becameAddicted: { addictionId: number };
  requestFailed: { requestType: number; error: string; text: string };
}
