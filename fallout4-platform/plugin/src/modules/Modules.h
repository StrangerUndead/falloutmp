#pragma once
// Feature modules. Each Install registers its natives, event sinks and
// frame callbacks with the Platform. Main.cpp installs the ones whose
// feature is enabled in FalloutMP.json ("features": {"<name>": false}).
//
// | Feature      | Module             | FalloutPlatform natives / events |
// |--------------|--------------------|---------------------------------------------|
// | session      | Session.cpp        | getClientConfig, showNotification,
// sendToFront, menuOpenClose | | movement     | Movement.cpp       |
// getMovementFo4, setActorTransform, teleportActor, setMovementFlags,
// setAimAngles | | puppets      | PuppetsModule.cpp  | spawnPuppet,
// deletePuppet                    | | animation    | Animation.cpp      |
// get/setGraphVariables, notifyAnimationGraph, animationEvent | | appearance
// | Appearance.cpp     | get/applyAppearanceFo4, open/closeLooksMenu,
// looksMenuClosed | | inventory    | Inventory.cpp      | getInventoryEx,
// addItemEx, removeItemEx, setAmmoLoaded, use/drop/container events | |
// equipment    | Equipment.cpp      | equipItemEx, unequipItemEx,
// getEquippedItems, equipRequested | | actorValues  | ActorValues.cpp    |
// setActorValueCurrent/Max, setHealthFraction, killActor, hostedValuesChanged
// | | progression  | Progression.cpp    | setPlayerLevelAndXp, setPerkPoints,
// add/removePerk, openSpecialMenu | | effects      | Effects.cpp        |
// applyEffectVisuals                           | | combat       | Combat.cpp
// | playRemoteShot, playHitReaction, weaponFired, reloadRequested,
// projectileHit | | powerArmor   | PowerArmor.cpp     |
// playPowerArmorEnter/Exit, setInPowerArmor, applyPowerArmorVisual,
// setFusionCoreCharge, setJetpackEnabled, powerArmor* events | | workshop |
// Workshop.cpp       | enter/exitWorkshopMode,
// spawn/move/deleteWorkshopObject, setPrePlacedDisabled, spawn/deleteWire,
// setWorkshopRatings, workshop* events, craft/mod/scrap events | | locks |
// Locks.cpp          | lockpick/hacking menus, openTerminal, setLocked, lock
// events | | map          | Map.cpp            | setMapMarker,
// fastTravelRequested            | | world        | World.cpp          |
// setGameTime, setTimeScale, forceWeather      | | probe        | Probe.cpp |
// writes FalloutMP-probe.json (diagnostics)    |
namespace fmp {
class Platform;
}

namespace fmp::modules {

void InstallSession(Platform& p);
void InstallMovement(Platform& p);
void InstallPuppets(Platform& p);
void InstallAnimation(Platform& p);
void InstallAppearance(Platform& p);
void InstallInventory(Platform& p);
void InstallEquipment(Platform& p);
void InstallActorValues(Platform& p);
void InstallProgression(Platform& p);
void InstallEffects(Platform& p);
void InstallCombat(Platform& p);
void InstallPowerArmor(Platform& p);
void InstallWorkshop(Platform& p);
void InstallLocks(Platform& p);
void InstallMap(Platform& p);
void InstallWorld(Platform& p);
void InstallProbe(Platform& p);

// Installs every enabled module (Main.cpp, at kGameDataReady).
void InstallAll(Platform& p);

}
