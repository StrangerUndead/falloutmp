// Typed gamemode API for the Fallout 4 game layer.
//
// Gamemodes use it as `mp.fo4.<function>(...)`. Every call goes through the
// native `fo4Call(command, argsJson)` bridge; the command table lives in
// falloutmp-server/cpp/server_guest_lib/fo4/Fo4GamemodeApi.cpp. Failed calls
// throw an Error with the server's error code.

export interface Fo4ItemKey {
  baseId: number;
  mods?: number[];
  condition?: number;
  stolenFrom?: number;
  ammoLoaded?: number;
}

export interface Fo4InventoryEntry extends Fo4ItemKey {
  count: number;
}

export type Fo4Vec3 = [number, number, number];

export interface Fo4WorkshopArea {
  shape?: "box" | "sphere";
  center: Fo4Vec3;
  halfExtents?: Fo4Vec3;
  radius?: number;
  rotZ?: number;
}

export type Fo4EffectKind =
  | "restoreOverTime"
  | "restoreInstant"
  | "damageOverTime"
  | "valueModifier"
  | "removeRads"
  | "addRads";

export type Fo4PvpZoneMode = "safe" | "open" | "flagged";

export interface Fo4CallBridge {
  fo4Call(command: string, argsJson: string): string;
}

export class Fo4Error extends Error {
  constructor(public readonly command: string, public readonly code: string) {
    super(`fo4.${command} failed: ${code}`);
  }
}

export const createFo4Api = (bridge: Fo4CallBridge) => {
  const call = <T = Record<string, unknown>>(
    command: string,
    args: Record<string, unknown> = {}
  ): T => {
    const res = JSON.parse(bridge.fo4Call(command, JSON.stringify(args)));
    if (!res.ok) {
      throw new Fo4Error(command, String(res.error));
    }
    return res as T;
  };

  return {
    // Items and equipment
    getInventory: (actorId: number): Fo4InventoryEntry[] =>
      call<{ entries: Fo4InventoryEntry[] }>("getInventory", { actorId })
        .entries,
    addItem: (actorId: number, item: Fo4ItemKey, count = 1) => {
      call("addItem", { actorId, item, count });
    },
    removeItem: (actorId: number, item: Fo4ItemKey, count = 1) => {
      call("removeItem", { actorId, item, count });
    },
    equipWeapon: (actorId: number, item: Fo4ItemKey | null) => {
      call("equipWeapon", { actorId, item });
    },
    setEquippedArmor: (actorId: number, items: Fo4ItemKey[]) => {
      call("setEquippedArmor", { actorId, items });
    },
    getWeaponStats: (item: Fo4ItemKey) =>
      call<{
        damage: number;
        capacity: number;
        ammoId: number;
        automatic: boolean;
        weight: number;
        value: number;
        maxShotsPerSecond: number;
      }>("getWeaponStats", { item }),

    // Actor values (SPECIAL, Health, AP, Rads, limbs, ...)
    getActorValue: (actorId: number, avId: number) =>
      call<{ current: number; max: number; base: number }>("getActorValue", {
        actorId,
        avId,
      }),
    setActorValue: (
      actorId: number,
      avId: number,
      value: { base?: number; current?: number }
    ) => {
      call("setActorValue", { actorId, avId, ...value });
    },

    // Progression
    getProgression: (actorId: number) =>
      call<{ progression: Record<string, unknown> }>("getProgression", {
        actorId,
      }).progression,
    awardXp: (actorId: number, amount: number, direct = false) =>
      call<{ level: number; perkPointsGained: number }>("awardXp", {
        actorId,
        amount,
        direct,
      }),
    definePerk: (perk: {
      key: string;
      name?: string;
      special: number;
      specialRequired?: number;
      ranks: { perkId: number; levelRequired?: number }[];
    }) => {
      call("definePerk", perk);
    },
    defineEffect: (effect: {
      effectId: number;
      kind: Fo4EffectKind;
      avId?: number;
    }) => {
      call("defineEffect", effect);
    },

    // Effects (chems, addictions, radiation)
    getEffects: (actorId: number) =>
      call<{ effects: Record<string, unknown> }>("getEffects", { actorId })
        .effects,
    cureAddictions: (actorId: number) => {
      call("cureAddictions", { actorId });
    },
    addRads: (actorId: number, amount: number) => {
      call("addRads", { actorId, amount });
    },

    // Power armor
    addPowerArmorFrame: (frame: {
      refId: number;
      baseId?: number;
      pos: Fo4Vec3;
      worldOrCell?: number;
      ownerProfileId?: number;
      contents?: Fo4ItemKey[];
    }) => {
      call("addPowerArmorFrame", frame);
    },
    getPowerArmorFrame: (frameRefId: number) =>
      call<{ frame: Record<string, unknown> }>("getPowerArmor", { frameRefId })
        .frame,
    getWornPowerArmor: (actorId: number) =>
      call<{ worn: Record<string, unknown> | null }>("getPowerArmor", {
        actorId,
      }).worn,

    // Settlements
    addWorkshop: (workshop: {
      refId: number;
      locationId?: number;
      areas?: Fo4WorkshopArea[];
      budgetMax?: number;
    }) => {
      call("addWorkshop", workshop);
    },
    getWorkshop: (refId: number) =>
      call<{ workshop: Record<string, unknown> }>("getWorkshop", { refId })
        .workshop,
    setWorkshopOwner: (
      refId: number,
      owner: { type: "none" | "profile" | "group"; id?: number }
    ) => {
      call("setWorkshopOwner", { refId, ...owner });
    },
    addSettler: (refId: number, actorId: number) => {
      call("addSettler", { refId, actorId });
    },

    // Vendors
    addVendor: (vendor: {
      vendorId: number;
      factionId?: number;
      caps?: number;
      inventory?: { entries: Fo4InventoryEntry[] };
      buysTypes?: number[];
      acceptsStolen?: boolean;
      hours?: [number, number];
    }) => {
      call("addVendor", vendor);
    },

    // Locks and terminals
    setLock: (
      refId: number,
      lock: { level: number; keyId?: number; locked?: boolean;
              ownerFaction?: number }
    ) => {
      call("setLock", { refId, ...lock });
    },
    getLock: (refId: number) =>
      call<{ lock: { level: number; keyId: number; locked: boolean } | null }>(
        "getLock",
        { refId }
      ).lock,
    setTerminalLock: (refId: number, level: number, locked = true) => {
      call("setTerminalLock", { refId, level, locked });
    },

    // Parties and PvP
    getParty: (profileId: number) =>
      call<{ party: { id: number; leader: number; members: number[] } | null }>(
        "getParty",
        { profileId }
      ).party,
    addPvpZone: (zone: {
      center: Fo4Vec3;
      radius: number;
      worldOrCell?: number;
      mode: Fo4PvpZoneMode;
    }) => {
      call("addPvpZone", zone);
    },

    // Equipment (validated like a client request)
    equip: (actorId: number, item: Fo4ItemKey, equip = true) => {
      call("equip", { actorId, item, equip });
    },

    // Time and weather
    getTime: () =>
      call<{
        gameDays: number;
        gameHour: number;
        timeScale: number;
        weatherId: number;
        radstorm: boolean;
      }>("getTime"),
    setTime: (time: { gameDays?: number; gameHour?: number;
                      timeScale?: number }) => {
      call("setTime", time);
    },
    setWeather: (weather: { weatherId: number; transitionSec?: number;
                            radstorm?: boolean }) => {
      call("setWeather", weather);
    },

    // Map markers and fast travel
    addMapMarker: (marker: {
      refId: number;
      pos: Fo4Vec3;
      worldOrCell?: number;
      name?: string;
      type?: number;
      canTravel?: boolean;
      visibleByDefault?: boolean;
    }) => {
      call("addMapMarker", marker);
    },
    discoverMarker: (actorId: number, refId: number) => {
      call("discoverMarker", { actorId, refId });
    },
    getDiscoveredMarkers: (actorId: number): number[] =>
      call<{ markers: number[] }>("getDiscoveredMarkers", { actorId })
        .markers,

    // State
    sendFullState: (actorId: number) => {
      call("sendFullState", { actorId });
    },
    getActorState: (actorId: number) =>
      call<{ state: Record<string, unknown> }>("getActorState", { actorId })
        .state,
    getWorldState: () =>
      call<{ world: Record<string, unknown> }>("getWorldState").world,
  };
};

export type Fo4Api = ReturnType<typeof createFo4Api>;
