# Papyrus API Map: SkyMP (Skyrim) → FalloutMP (Fallout 4)

> **Purpose.** This is the reference a FalloutMP implementer uses to port every Papyrus / SkyrimPlatform (SP) call in SkyMP to Fallout 4, and to extend the server-side Papyrus VM with the natives FO4 scripts need.
> **Audience.** Future Claude Code sessions and human contributors (see [../README.md](../README.md)). It assumes you have read the broad survey [../../FALLOUT4_PORT_RESEARCH.md](../../FALLOUT4_PORT_RESEARCH.md).
> **Status.** Research snapshot, October 2026. The repo is at upstream SkyMP `f926944` + `c16c7b9`.

**Provenance marks**
- `[src: path:line]`: verified in source. Paths without a prefix are relative to the repo root.
  - `f4se/scripts/{vanilla,modified}/X.psc` means the F4SE repository (`github.com/ianpatt/f4se`, 0.7.9 tree, whose `vanilla/` folder holds Bethesda's FO4 base `.psc` files). It was checked out in the session scratchpad, **not** in this repo.
  - `libxse:` means `github.com/libxse/commonlibf4` at commit `7c8c6f8` (2026-09-22).
- `[web: URL]`: taken from an external page; re-check before relying on it. `falloutck.uesp.net` returns 403 to plain fetches, but its MediaWiki API (`/w/api.php?action=parse&page=…&prop=wikitext&format=json`) works.
- `[inference]`: reasoning, not verified.

**Markers used in signature lists**
- `[G]` global (static) function.
- `[S]` script-defined: the body is Papyrus inside the native script, not engine code. The server VM runs it if the vanilla `.pex` is loaded.
- `[L]` latent: does not return in the same frame. SP exposes it as a `Promise`, and the server VM must return a promise. Source: the 61 members of `Category:Latent_Functions` [web: https://falloutck.uesp.net/wiki/Category:Latent_Functions] plus F4SE `LatentNativeFunction*` registrations [src: f4se/f4se/PapyrusObjectReference.cpp, PapyrusUI.cpp, PapyrusFavoritesManager.cpp].
- `[L*]` script-defined wrapper that calls a latent native.
- `[F4SE]` added by F4SE (only present when F4SE is installed). Everything in `f4se/scripts/modified/*.psc` is an F4SE addition: those files are fragments merged into the vanilla scripts at build time [src: f4se/scripts/update_scripts.py].
- `[dbg]` `debugOnly`: calls are stripped from scripts compiled in release mode. The native still exists at runtime.

---

## 0. TL;DR

1. **Scale.**
   - FO4 vanilla Papyrus has **891 native functions** in **91 native scripts**, plus 118 script-defined helper functions inside those scripts and 160 distinct event names.
     - The local F4SE `vanilla/` folder covers 86 of the scripts (881 natives). The other 5 (`IdleMarker`, `ShaderParticleGeometry`, `SoundCategory`, `SoundCategorySnapshot`, `TalkingActivator`) were taken from the CK wiki.
   - F4SE 0.7.9 adds **232 functions** (227 native), **11 structs** and **8 events**. 10 of its scripts are F4SE-only: `F4SE`, `UI`, `Input`, `InstanceData`, `FavoritesManager`, `DefaultObject`, `MatSwap`, `WaterType`, `ArmorAddon` and `EquipSlot`.
   - The full catalogue is in §1.
2. **The client uses about 150 distinct SP/Papyrus calls** (§2). About 60% port as **Same** or with a trivial rename.
   The biggest structural changes:
   - **Actor values are forms.** `actor.getActorValue("health")` becomes `ref.GetValue(ActorValue akAV)` on **ObjectReference**.
   - **Player controls go through `InputEnableLayer`.** `Game.Disable/EnablePlayerControls` and `Game.EnableFastTravel` do not exist.
   - **Different constant tables.** Form types, menu names, MotionType (FO4 `Motion_Keyframed = 2`, not 4) and camera states all differ.
   - **The SP3 marshaller must handle FO4 types.** It needs structs, `Var`, and non-form ScriptObjects such as `InputEnableLayer` handles.
3. **Client calls with no FO4 Papyrus equivalent** (vanilla or F4SE). Each needs a new native in the FO4 `TESModPlatform`, a C++ fast path in the platform's `CallNative`, a different design, or removal:
   - `Debug.sendAnimationEvent`. SP already bypasses Papyrus for this call [src: skyrim-platform/src/platform_se/skyrim_platform/CallNative.cpp:299]; port it with `IAnimationGraphManagerHolder::NotifyAnimationGraphImpl`.
   - `Actor.keepOffsetFromActor` / `clearKeepOffsetFromActor`. This is the core of remote-actor locomotion.
   - `Actor.setDontMove`, `Actor.isSwimming`.
   - `Actor.queueNiNodeUpdate`. F4SE `Actor.QueueUpdate` covers it.
   - `Actor.getSpellCount` / `getNthSpell`.
   - `Game.getFormEx`, `Game.getCurrentCrosshairRef`, the four `Game` tint-mask getters, `Game.getModCount` / `getModName` (F4SE `GetInstalledPlugins` covers these two).
   - `Utility.getINIInt`, `Input.isKeyPressed`, `Input.getNumKeysPressed`.
   - `ObjectReference.setDisplayName`, `ObjectReference.isHarvested`.
   - `Form.getType`, `Flora.getIngredient`, `Weapon.getWeaponType`, `Keyword.getKeyword`, `ColorForm.getColor`.
   - All `NetImmerse.*` (node world position, node scale, node texture set).
   - All face/headpart/tint setters on `ActorBase`: `get/setFaceMorph`, `get/setFacePreset`, `get/setFaceTextureSet`, `getHairColor`, `get/setNthHeadPart`, `setVoiceType`. F4SE partly covers these with `GetHeadParts` and `Get/SetBodyWeight`.
   - `ActorValueInfo.*`.
   - Every SkyMP `TESModPlatform.*` native.
   - The Skyrim-specific SP helpers: `setInventory`, `castSpellImmediate`, `interruptCast`, `get/applyAnimationVariables*`, `setCollision`, `loadGame`, `getExtraContainerChanges`, `getContainer`.
   - Full list with proposed replacements: §2.4.
4. **Server (§3).**
   - SkyMP registers **127 natives in 22 classes**. About 70 have a same-named FO4 counterpart. About 25 are Skyrim/SKSE-only and must either become FalloutMP extensions or be dropped.
   - To run FO4 scripts, the server must add the **ScriptObject event plumbing** as P0: timers, remote events, custom events, hit/inventory registration, `CallFunction`/`GetPropertyValue`, and `Var`/struct support.
   - It must also add the **ActorValue-form AV API**, `GlobalVariable`, `Math`, `Quest` stage natives and `InputEnableLayer`.
5. **Events (§4).**
   - FO4 removes `OnUpdate` (timers replace it).
   - `OnHit` and `OnMagicEffectApply` require registration and are **single-shot**.
   - `OnItemAdded` and `OnItemRemoved` are sent only to scripts that have an inventory filter.
   - Any game event can be received remotely (`Event Actor.OnDeath(Actor akSender, Actor akKiller)`).
   - Scripts can define and send custom events.
   - Non-native scripts can no longer declare new events. SkyMP's `SkympOnActivateClose` must move to a native stub.
   - SkyMP's server fires `OnHit` and `OnItemAdded` **unconditionally**. FO4 semantics require filtering and registration bookkeeping.

---

## 1. Fallout 4 native API catalogue

### 1.0 How to read this section

**Coverage**
- Every native script in `f4se/scripts/vanilla` (86 files) is listed, plus the F4SE additions from `f4se/scripts/modified` (29 files).
- Five native scripts are missing from that folder and were taken from the CK wiki: `IdleMarker`, `ShaderParticleGeometry`, `SoundCategory`, `SoundCategorySnapshot`, `TalkingActivator`.
- The 17 non-native vanilla gameplay scripts in the folder (workshop, followers, Dogmeat…) are summarised in §1.9.

**Format**
- Each class gets a header line: `extends`, script flags, and counts.
- The code block lists one signature per line, copied verbatim from the `.psc` files (default values included). Markers are explained at the top of this document.
- Events come after the functions.
  - Events on `ScriptObject` must be registered. The trailing `;` comment names the registration call.
  - Events on Form-derived classes (`ObjectReference`, `Actor`, `Quest`, `Location`, `Package`, `Perk`, `Scene`, `Terminal`, `TopicInfo`, `Alias`) are **delivered automatically** to scripts attached to the form. They also reach aliases and active magic effects that target the form. Any other script can receive them with `RegisterForRemoteEvent(src, "OnX")` and a handler named `Event <RootType>.OnX(<RootType> akSender, …)` [web: https://falloutck.uesp.net/wiki/Remote_Papyrus_Event_Registration].
  - Exceptions to automatic delivery are annotated on the event line (`OnItemAdded`/`OnItemRemoved` need an inventory filter; `OnPlayerLoadGame` reaches the player only).
- Structs and `AutoReadOnly` constants follow the events.

**Global FO4 rules that apply to every class** (details in §4 and §5)
- `ScriptObject` is the root type, and **all event registrations are per script instance** [web: https://falloutck.uesp.net/wiki/Differences_from_Skyrim_to_Fallout_4].
- `OnUpdate` / `RegisterForUpdate*` are gone. Use `StartTimer` / `OnTimer`.
- Non-native scripts cannot declare new events. They use `CustomEvent` + `SendCustomEvent`.
- `OnHit` / `OnMagicEffectApply` need a single-shot registration. `OnItemAdded` / `OnItemRemoved` need an inventory filter.

### 1.1 Class index

The SP3/client and server sections below refer to these classes. Counts come from the `.psc` files. "Events" counts events declared in the script itself; `ActiveMagicEffect`, `ReferenceAlias` and `RefCollectionAlias` re-declare the 81 `ObjectReference` + `Actor` events.

| Class | extends | Flags | Natives | Script-defined | F4SE adds | Events |
|---|---|---|---:|---:|---:|---:|
| ScriptObject | — | Native Hidden | 57 | 2 | 12 | 31 |
| Form | ScriptObject | Native Hidden | 7 | 0 | 28 | 0 |
| ObjectReference | Form | Native Hidden | 216 | 17 | 13 | 42 |
| Actor | ObjectReference | Native Hidden | 191 | 29 | 6 | 39 |
| ActorBase | Form | Native Hidden | 16 | 0 | 6 | 0 |
| Game | — | Native Hidden | 104 | 11 | 10 | 0 |
| Debug | — | Native DebugOnly Hidden | 31 | 4 | 0 | 0 |
| Utility | — | Native Hidden | 30 | 0 | 2 | 0 |
| Math | — | Native Hidden | 13 | 2 | 8 | 0 |
| InputEnableLayer | ScriptObject | Native Hidden | 35 | 0 | 0 | 0 |
| GlobalVariable | Form | Native Hidden | 2 | 3 | 0 | 0 |
| F4SE | — | Native Hidden | 0 | 0 | 6 | 0 |
| UI | — | Native Hidden | 0 | 0 | 10 | 0 |
| Input | — | Native Hidden | 0 | 0 | 2 | 0 |
| FavoritesManager | — | Native Hidden | 0 | 0 | 6 | 0 |
| DefaultObject | Form | native Hidden | 0 | 0 | 3 | 0 |
| CommonArrayFunctions | — |  | 0 | 12 | 0 | 0 |
| Weapon | Form | Native Hidden | 2 | 0 | 3 | 0 |
| Armor | Form | Native Hidden | 0 | 0 | 1 | 0 |
| ArmorAddon | Form | Native Hidden | 0 | 0 | 1 | 0 |
| Ammo | Form | Native Hidden | 0 | 0 | 0 | 0 |
| ObjectMod | Form | Native Hidden | 0 | 0 | 6 | 0 |
| InstanceData | — | Native Hidden | 0 | 0 | 56 | 0 |
| InstanceNamingRules | Form | Native Hidden | 1 | 0 | 0 | 0 |
| Component | Form | Native Hidden | 0 | 0 | 4 | 0 |
| MiscObject | Form | Native Hidden | 1 | 0 | 2 | 0 |
| ConstructibleObject | MiscObject | Native Hidden | 0 | 0 | 10 | 0 |
| Potion | Form | Native Hidden | 1 | 0 | 0 | 0 |
| Ingredient | Form | Native Hidden | 4 | 0 | 0 | 0 |
| Book | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Holotape | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Key | MiscObject | Native Hidden | 0 | 0 | 0 | 0 |
| SoulGem | MiscObject | Native Hidden | 0 | 0 | 0 | 0 |
| MatSwap | Form | native Hidden | 0 | 0 | 2 | 0 |
| EquipSlot | Form | native Hidden | 0 | 0 | 1 | 0 |
| Keyword | Form | Native Hidden | 2 | 0 | 0 | 0 |
| LocationRefType | Keyword | Native Hidden | 0 | 0 | 0 | 0 |
| FormList | Form | Native Hidden | 7 | 0 | 0 | 0 |
| LeveledItem | Form | Native Hidden | 2 | 0 | 0 | 0 |
| LeveledActor | Form | Native Hidden | 2 | 0 | 0 | 0 |
| LeveledSpell | Form | Native Hidden | 2 | 0 | 0 | 0 |
| Outfit | Form | Native Hidden | 0 | 0 | 0 | 0 |
| ActorValue | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Race | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Perk | Form | Native Hidden | 0 | 0 | 7 | 1 |
| HeadPart | Form | Native Hidden | 0 | 0 | 6 | 0 |
| Class | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Faction | Form | Native Hidden | 23 | 0 | 0 | 0 |
| AssociationType | Form | Native Hidden | 0 | 0 | 0 | 0 |
| VoiceType | Form | Native Hidden | 0 | 0 | 0 | 0 |
| CombatStyle | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Idle | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Action | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Package | Form | Native Hidden | 2 | 0 | 0 | 3 |
| Cell | Form | Native Hidden | 13 | 0 | 1 | 0 |
| WorldSpace | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Location | Form | Native Hidden | 16 | 2 | 4 | 2 |
| EncounterZone | Form | Native Hidden | 3 | 0 | 12 | 0 |
| Weather | Form | Native Hidden | 10 | 0 | 0 | 0 |
| Activator | Form | Native Hidden | 1 | 0 | 0 | 0 |
| Furniture | Activator | Native Hidden | 1 | 0 | 0 | 0 |
| Flora | Activator | Native Hidden | 0 | 0 | 0 | 0 |
| TalkingActivator | Activator | Native Hidden | 0 | 0 | 0 | 0 |
| Container | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Door | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Terminal | Form | Native Hidden | 1 | 0 | 0 | 1 |
| Light | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Static | Form | Native Hidden | 0 | 0 | 0 | 0 |
| MovableStatic | Static | Native Hidden | 0 | 0 | 0 | 0 |
| IdleMarker | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Explosion | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Hazard | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Projectile | Form | Native Hidden | 0 | 0 | 0 | 0 |
| WaterType | Form | native Hidden | 0 | 0 | 4 | 0 |
| TextureSet | Form | Hidden Native | 0 | 0 | 0 | 0 |
| Quest | Form | Native Hidden | 26 | 8 | 0 | 41 |
| Alias | ScriptObject | Native Hidden | 3 | 0 | 0 | 3 |
| ReferenceAlias | Alias | Native Hidden | 5 | 20 | 0 | 81 |
| RefCollectionAlias | Alias | Native Hidden | 6 | 20 | 0 | 81 |
| LocationAlias | Alias | Native Hidden | 3 | 0 | 0 | 0 |
| Scene | Form | Native Hidden | 7 | 0 | 0 | 5 |
| Topic | Form | Native Hidden | 1 | 0 | 0 | 0 |
| TopicInfo | Form | Native Hidden | 2 | 0 | 0 | 2 |
| Message | Form | Native Hidden | 5 | 0 | 0 | 0 |
| CameraShot | Form | Native Hidden | 0 | 0 | 0 | 0 |
| Spell | Form | Native Hidden | 3 | 0 | 0 | 0 |
| MagicEffect | Form | Native Hidden | 1 | 0 | 0 | 0 |
| ActiveMagicEffect | ScriptObject | Native Hidden | 6 | 0 | 0 | 83 |
| Enchantment | Form | Native Hidden | 1 | 0 | 0 | 0 |
| Scroll | Form | Native Hidden | 1 | 0 | 0 | 0 |
| Shout | Form | Native Hidden | 0 | 0 | 0 | 0 |
| WordOfPower | Form | Native Hidden | 0 | 0 | 0 | 0 |
| EffectShader | Form | Native Hidden | 2 | 0 | 0 | 0 |
| VisualEffect | Form | Native Hidden | 2 | 0 | 0 | 0 |
| ImageSpaceModifier | Form | Native Hidden | 5 | 0 | 0 | 0 |
| ImpactDataSet | Form | Native Hidden | 0 | 0 | 0 | 0 |
| ShaderParticleGeometry | Form | Native Hidden | 2 | 0 | 0 | 0 |
| Sound | Form | Native Hidden | 4 | 0 | 0 | 0 |
| SoundCategory | Form | Native Hidden | 6 | 0 | 0 | 0 |
| SoundCategorySnapshot | Form | Native Hidden | 2 | 0 | 0 | 0 |
| MusicType | Form | Native Hidden | 2 | 0 | 0 | 0 |
| OutputModel | Form | Native Hidden | 0 | 0 | 0 | 0 |

### 1.2 Core runtime

#### ScriptObject
`extends (none)` · `Native Hidden` · 57 natives + 2 script-defined + 12 F4SE [src: f4se/scripts/vanilla/ScriptObject.psc] [src: f4se/scripts/modified/ScriptObject.psc]

> Root of every script (forms, aliases, active effects, InputEnableLayer). All registrations are per script instance. GetState/GotoState are script-defined here (use the `__state` intrinsic).

```papyrus
AddInventoryEventFilter(Form akFilter)
Var CallFunction(string asFuncName, Var[] aParams)  [L]
CallFunctionNoWait(string asFuncName, Var[] aParams)
CancelTimer(int aiTimerID = 0)
CancelTimerGameTime(int aiTimerID = 0)
Var GetPropertyValue(string asPropertyName)  [L]
string GetState()  [S]
GotoState(string asNewState)  [S]
ScriptObject CastAs(string asScriptName)
bool IsBoundGameObjectAvailable()
bool RegisterForAnimationEvent(ObjectReference akSender, string asEventName)
RegisterForCustomEvent(ScriptObject akSender, CustomEventName asEventName)
RegisterForDetectionLOSGain(Actor akViewer, ObjectReference akTarget)
RegisterForDetectionLOSLost(Actor akViewer, ObjectReference akTarget)
RegisterForDirectLOSGain(ObjectReference akViewer, ObjectReference akTarget, string asViewerNode = "", string asTargetNode = "")
RegisterForDirectLOSLost(ObjectReference akViewer, ObjectReference akTarget, string asViewerNode = "", string asTargetNode = "")
RegisterForDistanceLessThanEvent(ScriptObject akObj1, ScriptObject akObj2, float afDistance)
RegisterForDistanceGreaterThanEvent(ScriptObject akObj1, ScriptObject akObj2, float afDistance)
RegisterForHitEvent(ScriptObject akTarget, ScriptObject akAggressorFilter = None, Form akSourceFilter = None, Form akProjectileFilter = None, int aiPowerFilter = -1, int aiSneakFilter = -1, int aiBashFilter = -1, int aiBlockFilter = -1, bool abMatch = true)
RegisterForMagicEffectApplyEvent(ScriptObject akTarget, ScriptObject akCasterFilter = None, Form akEffectFilter = None, bool abMatch = true)
RegisterForMenuOpenCloseEvent(string asMenuName)
RegisterForPlayerSleep()
RegisterForPlayerTeleport()
RegisterForPlayerWait()
RegisterForRadiationDamageEvent(ScriptObject akTarget)
bool RegisterForRemoteEvent(ScriptObject akEventSource, ScriptEventName asEventName)
RegisterForTrackedStatsEvent(string asStat, int aiThreshold)
RegisterForLooksMenuEvent()
RegisterForTutorialEvent(String asEventName)
RemoveAllInventoryEventFilters()
RemoveInventoryEventFilter(Form akFilter)
SendCustomEvent(CustomEventName asEvent, Var[] akArgs = None)
SetPropertyValue(string asPropertyName, Var aValue)  [L]
SetPropertyValueNoWait(string asPropertyName, Var aValue)
StartTimer(float afInterval, int aiTimerID = 0)
StartTimerGameTime(float afInterval, int aiTimerID = 0)
UnregisterForAllEvents()
UnregisterForAllCustomEvents()
UnregisterForAllHitEvents(ScriptObject akTarget = None)
UnregisterForAllMagicEffectApplyEvents(ScriptObject akTarget = None)
UnregisterForAllMenuOpenCloseEvents()
UnregisterForAllRadiationDamageEvents()
UnregisterForAllRemoteEvents()
UnregisterForAllTrackedStatsEvents()
UnregisterForAnimationEvent(ObjectReference akSender, string asEventName)
UnregisterForCustomEvent(ScriptObject akSender, CustomEventName asEventName)
UnregisterForDistanceEvents(ScriptObject akObj1, ScriptObject akObj2)
UnregisterForHitEvent(ScriptObject akTarget, ScriptObject akAggressorFilter = None, Form akSourceFilter = None, Form akProjectileFilter = None, int aiPowerFilter = -1, int aiSneakFilter = -1, int aiBashFilter = -1, int aiBlockFilter = -1, bool abMatch = true)
UnregisterForLOS(ObjectReference akViewer, ObjectReference akTarget)
UnregisterForMagicEffectApplyEvent(ScriptObject akTarget, ScriptObject akCasterFilter = None, Form akEffectFilter = None, bool abMatch = true)
UnregisterForMenuOpenCloseEvent(string asMenuName)
UnregisterForPlayerSleep()
UnregisterForPlayerTeleport()
UnregisterForPlayerWait()
UnregisterForRadiationDamageEvent(ScriptObject akTarget)
UnregisterForRemoteEvent(ScriptObject akEventSource, ScriptEventName asEventName)
UnregisterForTrackedStatsEvent(string asStat)
UnregisterForLooksMenuEvent()
UnregisterForTutorialEvent(String asEventName)
RegisterForKey(int key)  [F4SE]
UnregisterForKey(int key)  [F4SE]
RegisterForControl(string control)  [F4SE]
UnregisterForControl(string control)  [F4SE]
RegisterForExternalEvent(string eventName, string callback)  [F4SE]
UnregisterForExternalEvent(string eventName)  [F4SE]
RegisterForCameraState()  [F4SE]
UnregisterForCameraState()  [F4SE]
RegisterForFurnitureEvent(Var filter = None)  [F4SE]
UnregisterForFurnitureEvent(Var filter = None)  [F4SE]
RegisterForGamepadButton(int key)  [F4SE]
UnregisterForGamepadButton(int key)  [F4SE]

Event OnAnimationEvent(ObjectReference akSource, string asEventName)   ; RegisterForAnimationEvent(akSender, name) - persists until Unregister/graph unload; target 3D must be loaded
Event OnAnimationEventUnregistered(ObjectReference akSource, string asEventName)   ; auto, when a registered graph unloads
Event OnBeginState(string asOldState)   ; GotoState()
Event OnDistanceLessThan(ObjectReference akObj1, ObjectReference akObj2, float afDistance)   ; RegisterForDistanceLessThanEvent(o1,o2,d) - single-shot, one per pair
Event OnDistanceGreaterThan(ObjectReference akObj1, ObjectReference akObj2, float afDistance)   ; RegisterForDistanceGreaterThanEvent(o1,o2,d) - single-shot, one per pair
Event OnEndState(string asNewState)   ; GotoState()
Event OnGainLOS(ObjectReference akViewer, ObjectReference akTarget)   ; RegisterForDetectionLOSGain / RegisterForDirectLOSGain - single-shot, throttled
Event OnHit(ObjectReference akTarget, ObjectReference akAggressor, Form akSource, Projectile akProjectile, bool abPowerAttack, bool abSneakAttack, bool abBashAttack, bool abHitBlocked, string asMaterialName)   ; RegisterForHitEvent(target, filters...) - SINGLE-SHOT, per script; re-register in handler
Event OnInit()   ; auto (after properties are filled)
Event OnLostLOS(ObjectReference akViewer, ObjectReference akTarget)   ; RegisterForDetectionLOSLost / RegisterForDirectLOSLost - single-shot, throttled
Event OnMagicEffectApply(ObjectReference akTarget, ObjectReference akCaster, MagicEffect akEffect)   ; RegisterForMagicEffectApplyEvent(target, filters) - single-shot
Event OnMenuOpenCloseEvent(string asMenuName, bool abOpening)   ; RegisterForMenuOpenCloseEvent(menuName) - persistent
Event OnPlayerSleepStart(float afSleepStartTime, float afDesiredSleepEndTime, ObjectReference akBed)   ; RegisterForPlayerSleep
Event OnPlayerSleepStop(bool abInterrupted, ObjectReference akBed)   ; RegisterForPlayerSleep
Event OnPlayerTeleport()   ; RegisterForPlayerTeleport
Event OnPlayerWaitStart(float afWaitStartTime, float afDesiredWaitEndTime)   ; RegisterForPlayerWait
Event OnPlayerWaitStop(bool abInterrupted)   ; RegisterForPlayerWait
Event OnRadiationDamage(ObjectReference akTarget, bool abIngested)   ; RegisterForRadiationDamageEvent(target) - single-shot
Event OnTimer(int aiTimerID)   ; StartTimer(sec, id) - fires once per call; real time, paused in menus, scaled by VATS; ObjectReference scripts must pass explicit id
Event OnTimerGameTime(int aiTimerID)   ; StartTimerGameTime(hours, id) - fires once; not sent in menu mode
Event OnTrackedStatsEvent(string arStatName, int aiStatValue)   ; RegisterForTrackedStatsEvent(stat, threshold) - single-shot
Event OnLooksMenuEvent(int aiFlavor)   ; RegisterForLooksMenuEvent
Event OnTutorialEvent(String asEventName, Message aMessage)   ; RegisterForTutorialEvent(name)
Event OnKeyDown(int keyCode)   ; F4SE RegisterForKey(dxScanCode)
Event OnKeyUp(int keyCode, float time)   ; F4SE RegisterForKey(dxScanCode)
Event OnControlDown(string control)   ; F4SE RegisterForControl(name)
Event OnControlUp(string control, float time)   ; F4SE RegisterForControl(name)
Event OnPlayerCameraState(int oldState, int newState)   ; F4SE RegisterForCameraState()
Event OnFurnitureEvent(Actor akActor, ObjectReference akFurniture, bool isGettingUp)   ; F4SE RegisterForFurnitureEvent(Var filter)
Event OnGamepadButtonDown(int keyCode)   ; F4SE RegisterForGamepadButton(xinput)
Event OnGamepadButtonUp(int keyCode, float time)   ; F4SE RegisterForGamepadButton(xinput)
```

#### Form
`extends ScriptObject` · `Native Hidden` · 7 natives + 28 F4SE [src: f4se/scripts/vanilla/Form.psc] [src: f4se/scripts/modified/Form.psc]

> Vanilla FO4 Form has no GetName/GetType/GetWeight: GetName/GetWeight are F4SE; GetType does not exist at all. F4SE GetWeight/SetWeight is item weight (not NPC body weight).

```papyrus
Int GetFormID()
int GetGoldValue()
bool HasKeyword(Keyword akKeyword)
bool HasKeywordInFormList(FormList akKeywordList)
bool PlayerKnows()
StartObjectProfiling()  [dbg]
StopObjectProfiling()  [dbg]
string GetName()  [F4SE]
SetName(string name)  [F4SE]
string GetEditorID()  [F4SE]
string GetDescription()  [F4SE]
float GetWeight()  [F4SE]
SetWeight(float weight)  [F4SE]
SetGoldValue(int value)  [F4SE]
Keyword[] GetKeywords()  [F4SE]
bool HasWorldModel()  [F4SE]
string GetWorldModelPath()  [F4SE]
SetWorldModelPath(string path)  [F4SE]
string GetIconPath()  [F4SE]
SetIconPath(string path)  [F4SE]
string GetMessageIconPath()  [F4SE]
SetMessageIconPath(string path)  [F4SE]
Enchantment GetEnchantment()  [F4SE]
SetEnchantment(Enchantment e)  [F4SE]
int GetEnchantmentValue()  [F4SE]
SetEnchantmentValue(int value)  [F4SE]
EquipSlot GetEquipType()  [F4SE]
SetEquipType(EquipSlot type)  [F4SE]
Race GetRaceForm()  [F4SE]
SetRaceForm(Race newRace)  [F4SE]
int GetSlotMask()  [F4SE]
SetSlotMask(int slotMask)  [F4SE]
int AddSlotToMask(int slotMask)  [F4SE]
int RemoveSlotFromMask(int slotMask)  [F4SE]
int GetMaskForSlot(int slot)  [G,F4SE]

F4SE consts: kSlotMask30..kSlotMask61 = 1 << (slot - 30)  (biped slots 30..61)
```

#### ObjectReference
`extends Form` · `Native Hidden` · 216 natives + 17 script-defined + 13 F4SE [src: f4se/scripts/vanilla/ObjectReference.psc] [src: f4se/scripts/modified/ObjectReference.psc]

> Actor-value API lives here in FO4 (GetValue/SetValue/ModValue/DamageValue/RestoreValue/GetBaseValue/GetValuePercentage take `ActorValue` forms). OMOD API: AttachMod/RemoveMod/AttachModToInventoryItem/RemoveModFromInventoryItem. No SetDisplayName/IsHarvested/GetTotalItemWeight (Skyrim/SKSE).

```papyrus
Unlock(bool abAsOwner = false)  [S]
bool rampRumble(float power = 0.5, float duration = 0.25, float falloff = 1600.0)  [S]
bool IsNearPlayer()  [S]
bool IsInInterior()  [S]
bool MoveToIfUnloaded(ObjectReference akTarget, float afXOffset = 0.0, float afYOffset = 0.0, float afZOffset = 0.0)  [S,L*]
bool HasRefType(LocationRefType akRefType)  [S]
ObjectReference[] getLinkedRefArray(keyword keywordLink)  [S]
MoveToWhenUnloaded(ObjectReference akTarget, float afXOffset = 0.0, float afYOffset = 0.0, float afZOffset = 0.0)  [S,L*]
DeleteWhenAble()  [S,L*]
AddKeyIfNeeded(ObjectReference ObjectWithNeededKey)  [S]
bool Activate(ObjectReference akActivator, bool abDefaultProcessingOnly = false)
bool AddDependentAnimatedObjectReference(ObjectReference akDependent)
AddItem(Form akItemToAdd, int aiCount = 1, bool abSilent = false)  [L]
AddKeyword(Keyword apKeyword)
AddTextReplacementData(string asTokenLabel, Form akForm)
AddToMap(bool abAllowFastTravel = false)
ApplyConveyorBelt(string aTarget, float aLinVelX, float aLinVelY, float aLinVelZ, bool abOn = true, bool abReverse = false)
ApplyHavokImpulse(float afX, float afY, float afZ, float afMagnitude)  [L]
ApplyFanMotor(string aTarget, float aAxisX, float aAxisY, float aAxisZ, float aForce, bool abOn = true)
FanMotorOn(bool abOn = true)
bool IsFanMotorOn()
bool AttachMod(ObjectMod akMod, int aiAttachIndex = 0)
bool AttachModToInventoryItem(Form akItem, ObjectMod akMod)
AttachTo(ObjectReference akParent)
BlockActivation(bool abBlocked = true, bool abHideActivateText = false)
int CalculateEncounterLevel(int aiDifficulty = 4)
bool CanFastTravelToMarker()
bool CanProduceForWorkshop()
ClearDestruction()  [L]
ClearFromOldLocations()
ConveyorBeltOn(bool abOn = true)
int CountActorsLinkedToMe(Keyword apLinkKeyword = None, Keyword apExcludeKeyword = None)
int CountLinkedRefChain(keyword apKeyword = None, int maxExpectedLinkedRefs = 100)
int CountRefsLinkedToMe(Keyword apLinkKeyword = None, Keyword apExcludeKeyword = None)
CreateDetectionEvent(Actor akOwner, int aiSoundLevel = 0)
DamageObject(float afDamage)  [L]
DamageValue(ActorValue akAV, float afDamage)
Delete()  [L]
Disable(bool abFadeOut = false)  [L]
DisableLinkChain(Keyword apKeyword = None, bool abFadeOut = false)
DisableNoWait(bool abFadeOut = false)
Drop(bool abSilent = false)  [L]
ObjectReference DropFirstObject(bool abInitiallyDisabled = false)  [L]
ObjectReference DropObject(Form akObject, int aiCount = 1)  [L]
Enable(bool abFadeIn = false)  [L]
EnableFastTravel(bool abEnable = true)
EnableLinkChain(Keyword apKeyword = None, bool abFadeIn = false)
EnableNoWait(bool abFadeIn = false)
ObjectReference[] FindAllReferencesOfType(Form akObjectOrList, float afRadius)
ObjectReference[] FindAllReferencesWithKeyword(Form akKeywordOrList, float afRadius)
ForceAddRagdollToWorld()  [L]
ForceRemoveRagdollFromWorld()  [L]
ActorBase GetActorOwner()
Actor GetActorRefOwner()
Actor[] GetActorsLinkedToMe(Keyword apLinkKeyword = None, Keyword apExcludeKeyword = None)
float GetAngleX()
float GetAngleY()
float GetAngleZ()
bool GetAnimationVariableBool(string arVariableName)
int GetAnimationVariableInt(string arVariableName)
float GetAnimationVariableFloat(string arVariableName)
Form GetBaseObject()
float GetBaseValue(ActorValue akAV)
int GetCurrentDestructionStage()
Location GetCurrentLocation()
Scene GetCurrentScene()
float GetDistance(ObjectReference akOther)
Location GetEditorLocation()
EncounterZone GetEncounterZone()
Faction GetFactionOwner()
float GetHeadingAngle(ObjectReference akOther)
float GetHeight()
int GetInventoryValue()
int GetItemCount(Form akItem = None)
int GetComponentCount(Form akItem = None)
ObjectReference GetContainer()
float GetItemHealthPercent()
Key GetKey()
float GetLength()
ObjectReference GetLinkedRef(Keyword apKeyword = NONE)
ObjectReference[] GetLinkedRefChain(keyword apKeyword = None, int iMaxExpectedLinkedRefs = 100)
ObjectReference[] GetLinkedRefChildren(keyword apKeyword)
int GetLockLevel()
LocationRefType[] GetLocRefTypes()
ObjectReference GetNthLinkedRef(int aiLinkedRef, Keyword apKeyword = None)
float GetMass()
int GetOpenState()
Cell GetParentCell()
float GetPositionX()
float GetPositionY()
float GetPositionZ()
float GetRadioFrequency()
float GetRadioVolume()
float GetResourceDamage(ActorValue akValue=None)
float[] GetSafePosition(float aSearchRadius = -1.0, float aSafeRadius = -1.0)
ObjectReference[] GetRefsLinkedToMe(Keyword apLinkKeyword = None, Keyword apExcludeKeyword = None)
ObjectReference[] GetWorkshopOwnedObjects(Actor akActor)
float GetWorkshopResourceDamage(ActorValue akValue)
ObjectReference[] GetWorkshopResourceObjects(ActorValue akAV = None, int aiOption = 0)
float GetScale()
Cell GetTeleportCell()
Cell GetTransitionCell()
float GetTransmitterDistance()
int GetTriggerObjectCount()
float GetValue(ActorValue akAV)
float GetValuePercentage(ActorValue akAV)
VoiceType GetVoiceType()
float GetWidth()
WorldSpace GetWorldSpace()
actor GetSelfAsActor()  [S]
bool HasActorRefOwner()
bool HasDirectLOS(ObjectReference akTarget, string asSourceNode = "", string asTargetNode = "")
bool HasEffectKeyword(Keyword akKeyword)
bool HasKeyword(Keyword apKeyword)
bool HasKeywordInFormList(FormList akKeywordList)
bool HasLocRefType(LocationRefType akRefType)
bool HasNode(string asNodeName)
bool HasOwner()  [S]
bool HasSharedPowerGrid(ObjectReference akCompare)
IgnoreFriendlyHits(bool abIgnore = true)
InterruptCast()
bool IsActivateChild(ObjectReference akChild)
bool IsActivationBlocked()
bool Is3DLoaded()
bool IsConveyorBeltOn()
bool IsCreated()
bool IsDeleted()
bool IsDestroyed()
bool IsDisabled()
bool IsEnabled()  [S]
bool IsFurnitureInUse(bool abIgnoreReserved = false)
bool IsFurnitureMarkerInUse(int aiMarker, bool abIgnoreReserved = false)
bool IsIgnoringFriendlyHits()
bool IsInDialogueWithPlayer()
bool IsLockBroken()
bool IsLocked()
bool IsMapMarkerVisible()
bool IsOwnedBy(Actor akOwner)
bool IsPowered()
bool IsQuestItem()
bool IsRadioOn()
bool IsRefInTransitionCell(ObjectReference akRef)
bool IsTeleportAreaLoaded()
bool IsWithinBuildableArea(ObjectReference akRef)
KnockAreaEffect(float afMagnitude, float afRadius)
Lock(bool abLock = true, bool abAsOwner = false)
MakeRadioReceiver(float afFrequency, float afVolume = 1.0, OutputModel aOverrideModel = None, bool abActive = true, bool abNoStatic = false)
MakeTransmitterRepeater(ObjectReference akTransmitterToRepeat, float afInnerRadius, float afOuterRadius, bool abUnlimitedRange = false)
ModValue(ActorValue akAV, float afAmount)
MoveTo(ObjectReference akTarget, float afXOffset = 0.0, float afYOffset = 0.0, float afZOffset = 0.0, bool abMatchRotation = true)  [L]
MoveToMyEditorLocation()  [L]
MoveToNearestNavmeshLocation()  [L]
MoveToNode(ObjectReference akTarget, string asNodeName, string asMatchNodeName = "")  [L]
Location OpenWorkshopSettlementMenu(Keyword akActionKW, Message astrConfirm = None, Location aLocToHighlight = None)  [L]
Location OpenWorkshopSettlementMenuEx(Keyword akActionKW, Message astrConfirm = None, Location aLocToHighlight = None, FormList akIncludeKeywordList = None, FormList akExcludeKeywordList = None, bool abExcludeZeroPopulation = false, bool abOnlyOwnedWorkshops = true, bool abTurnOffHeader = false, bool abOnlyPotentialVassalSettlements = false, bool abDisableReservedByQuests = false)  [L]
int RaidTargetsAvailable(Keyword akActionKW, Message astrConfirm = None, Location aLocToHighlight = None, FormList akIncludeKeywordList = None, FormList akExcludeKeywordList = None, bool abExcludeZeroPopulation = false, bool abOnlyOwnedWorkshops = true, bool abTurnOffHeader = false, bool abOnlyPotentialVassalSettlements = false, bool abDisableReservedByQuests = false)  [L]
ObjectReference PlaceAtMe(Form akFormToPlace, int aiCount = 1, bool abForcePersist = false, bool abInitiallyDisabled = false, bool abDeleteWhenAble = true)
Actor PlaceActorAtMe(ActorBase akActorToPlace, int aiLevelMod = 4, EncounterZone akZone = None)
ObjectReference PlaceAtNode(string asNodeName, Form akFormToPlace, int aiCount = 1, bool abForcePersist = false, bool abInitiallyDisabled = false, bool abDeleteWhenAble = true, bool abAttach = false)
bool PlayAnimation(string asAnimation)
bool PlayAnimationAndWait(string asAnimation, string asEventName)  [L]
bool PlayGamebryoAnimation(string asAnimation, bool abStartOver = false, float afEaseInTime = 0.0)
bool PlayImpactEffect(ImpactDataSet akImpactEffect, string asNodeName = "", float afPickDirX = 0.0, float afPickDirY = 0.0, float afPickDirZ = -1.0, float afPickLength = 512.0, bool abApplyNodeRotation = false, bool abUseNodeLocalRotation = false)
bool PlaySyncedAnimationSS(string asAnimation1, ObjectReference akObj2, string asAnimation2)
bool PlaySyncedAnimationAndWaitSS(string asAnimation1, string asEvent1, ObjectReference akObj2, string asAnimation2, string asEvent2)  [L]
PlayTerrainEffect(string asEffectModelName, string asAttachBoneName)
PauseAudio()
PreloadExteriorCell()
PreloadTargetArea()
ProcessTrapHit(ObjectReference akTrap, float afDamage, float afPushback, float afXVel, float afYVel, float afZVel, float afXPos, float afYPos, float afZPos, int aeMaterial, float afStagger)
PushActorAway(Actor akActorToPush, float aiKnockbackForce)
RecalculateResources()
RemoveAllItems(ObjectReference akTransferTo = None, bool abKeepOwnership = false)
RemoveComponents(Component akComponent, int aiCount, bool abSilent = false)  [L]
RemoveItem(Form akItemToRemove, int aiCount = 1, bool abSilent = false, ObjectReference akOtherContainer = None)  [L]
RemoveItemByComponent(Form akComponentToRemove, int aiCount = 1, bool abSilent = false, ObjectReference akOtherContainer = None)  [L]
RemoveKeyword(Keyword apKeyword)
RemoveAllMods()
RemoveAllModsFromInventoryItem(Form akItem)
RemoveMod(ObjectMod akMod)
RemoveModFromInventoryItem(Form akItem, ObjectMod akMod)
bool RemoveDependentAnimatedObjectReference(ObjectReference akDependent)
Repair()
Reset(ObjectReference akTarget = None)  [L]
ResetKeyword(Keyword apKeyword)
RestoreValue(ActorValue akAV, float afAmount)
ResumeAudio()
ReverseConveyorBelt(bool abReverse = true)
Say(Topic akTopicToSay, Actor akActorToSpeakAs = None, bool abSpeakInPlayersHead = false, ObjectReference akTarget = None)
SayCustom(Keyword akKeywordToSay, Actor akActorToSpeakAs = None, bool abSpeakInPlayersHead = false, ObjectReference akTarget = None)
int SellItem(Form Item, int Value, int amountToSell = -1, bool silent = false, form paymentItem = none, objectReference PaymentContainer = None)  [S]
SendStealAlarm(Actor akThief)
SetActivateTextOverride(Message akText)
SetActorCause(Actor akActor)
SetActorOwner(ActorBase akActorBase, bool abNoCrime = false)
SetActorRefOwner(Actor akActor, bool abNoCrime = false)
SetAngle(float afXAngle, float afYAngle, float afZAngle)  [L]
SetAnimationVariableBool(string arVariableName, bool abNewValue)
SetAnimationVariableInt(string arVariableName, int aiNewValue)
SetAnimationVariableFloat(string arVariableName, float afNewValue)
SetAttractionActive(Keyword apKeyword, bool abActive = true)
SetConveyorBeltVelocity(float afLinVelX, float afLinVelY, float afLinVelZ)
SetDestroyed(bool abDestroyed = true)
SetDirectAtTarget(ObjectReference akTarget)
SetFactionOwner(Faction akFaction, bool abNoCrime = false)
SetHarvested(bool abHarvested)
SetLinkedRef(ObjectReference akLinkedRef, Keyword apKeyword = NONE)
SetLockLevel(int aiLockLevel)
SetLocRefType(Location akLoc, LocationRefType akRefType)
SetMotionType(int aeMotionType, bool abAllowActivate = true)  [L]
SetNoFavorAllowed(bool abNoFavor = true)
SetOpen(bool abOpen = true)
SetPersistLoc(Location akLoc)
SetPlayerHasTaken(bool abTaken = true)
SetPosition(float afX, float afY, float afZ)  [L]
SetRadioOn(bool abOn = true)
SetRadioFrequency(float afFrequency)
SetRadioVolume(float afVolume)
SetScale(float afScale)  [L]
SetValue(ActorValue akAV, float afValue)
TranslateTo(float afX, float afY, float afZ, float afXAngle, float afYAngle, float afZAngle, float afSpeed, float afMaxRotationSpeed = 0.0)
SplineTranslateTo(float afX, float afY, float afZ, float afXAngle, float afYAngle, float afZAngle, float afTangentMagnitude, float afSpeed, float afMaxRotationSpeed = 0.0)
SplineTranslateToRefNode(ObjectReference arTarget, string arNodeName, float afTangentMagnitude, float afSpeed, float afMaxRotationSpeed = 0.0)
StartWorkshop(bool abStart = true)
StopTranslation()
StoreInWorkshop(Form akBaseItem, int aiCount=1)
TranslateToRef(ObjectReference arTarget, float afSpeed, float afMaxRotationSpeed = 0.0)  [S]
SplineTranslateToRef(ObjectReference arTarget, float afTangentMagnitude, float afSpeed, float afMaxRotationSpeed = 0.0)  [S]
TetherToHorse(ObjectReference akHorse)
bool WaitForAnimationEvent(string asEventName)  [L]
bool WaitFor3DLoad()  [L]
WaitForWorkshopResourceRecalc()  [L]
bool IsInLocation(Location akLocation)  [S]
ObjectMod[] GetAllMods()  [F4SE]
ObjectReference[] GetConnectedObjects()  [F4SE]
ObjectReference AttachWire(ObjectReference akRef, Form spline = None)  [L,F4SE]
ObjectReference CreateWire(ObjectReference akRef, Form spline = None)  [S,F4SE]
bool Scrap(ObjectReference akWorkshop)  [L,F4SE]
string GetDisplayName()  [F4SE]
Form[] GetInventoryItems()  [L,F4SE]
float GetInventoryWeight()  [F4SE]
ConnectPoint[] GetConnectPoints()  [L,F4SE]
bool TransmitConnectedPower()  [L,F4SE]
MatSwap:RemapData[] ApplyMaterialSwap(MatSwap mSwap, bool renameMaterial = false)  [L,F4SE]
SetMaterialSwap(MatSwap mSwap)  [F4SE]
MatSwap GetMaterialSwap()  [F4SE]

Event OnActivate(ObjectReference akActionRef)
Event OnCellAttach()
Event OnCellDetach()
Event OnCellLoad()
Event OnClose(ObjectReference akActionRef)
Event OnContainerChanged(ObjectReference akNewContainer, ObjectReference akOldContainer)
Event OnDestructionStageChanged(int aiOldStage, int aiCurrentStage)
Event OnEquipped(Actor akActor)
Event OnExitFurniture(ObjectReference akActionRef)
Event OnGrab()
Event OnHolotapeChatter(string astrChatter, float afNumericData)
Event OnHolotapePlay(ObjectReference aTerminalRef)
Event OnItemAdded(Form akBaseItem, int aiItemCount, ObjectReference akItemReference, ObjectReference akSourceContainer)   ; needs AddInventoryEventFilter(filter|None) on the receiving script
Event OnItemRemoved(Form akBaseItem, int aiItemCount, ObjectReference akItemReference, ObjectReference akDestContainer)   ; needs AddInventoryEventFilter(filter|None) on the receiving script
Event OnLoad()
Event OnLockStateChanged()
Event OnOpen(ObjectReference akActionRef)
Event OnPipboyRadioDetection(bool abDetected)
Event OnPlayerDialogueTarget()
Event OnPowerOn(ObjectReference akPowerGenerator)
Event OnPowerOff()
Event OnRead()
Event OnRelease()
Event OnReset()
Event OnSell(Actor akSeller)
Event OnSpellCast(Form akSpell)
Event OnTranslationAlmostComplete()   ; after TranslateTo/SplineTranslateTo
Event OnTranslationComplete()   ; after TranslateTo/SplineTranslateTo
Event OnTranslationFailed()   ; after StopTranslation
Event OnTrapHitStart(ObjectReference akTarget, float afXVel, float afYVel, float afZVel, float afXPos, float afYPos, float afZPos, int aeMaterial, bool abInitialHit, int aeMotionType)
Event OnTrapHitStop(ObjectReference akTarget)
Event OnTriggerEnter(ObjectReference akActionRef)
Event OnTriggerLeave(ObjectReference akActionRef)
Event OnUnequipped(Actor akActor)
Event OnUnload()
Event OnWorkshopMode(bool aStart)   ; sent to workshop ref
Event OnWorkshopObjectDestroyed(ObjectReference akReference)
Event OnWorkshopObjectGrabbed(ObjectReference akReference)
Event OnWorkshopObjectMoved(ObjectReference akReference)
Event OnWorkshopObjectPlaced(ObjectReference akReference)   ; sent to workshop ref AND placed ref
Event OnWorkshopObjectRepaired(ObjectReference akReference)
Event OnWorkshopNPCTransfer(Location akNewWorkshop, Keyword akActionKW)

F4SE Struct ObjectReference:ConnectPoint { string parent; string name; float roll; float pitch; float yaw; float x; float y; float z; float scale; ObjectReference object }
consts: Motion_Fixed=0, Motion_Dynamic=1, Motion_Keyframed=2
```

#### Actor
`extends ObjectReference` · `Native Hidden` · 191 natives + 29 script-defined + 6 F4SE [src: f4se/scripts/vanilla/Actor.psc] [src: f4se/scripts/modified/Actor.psc]

> No KeepOffsetFromActor, SetDontMove, QueueNiNodeUpdate, IsSwimming, Get/Set/Damage/RestoreActorValue (moved to ObjectReference with ActorValue forms). ForceMovement* debug natives are "not in release builds" per the psc comment.

```papyrus
ModFavorPoints(int iFavorPoints = 1)  [S]
ModFavorPointsWithGlobal(GlobalVariable FavorPointsGlobal)  [S]
MakePlayerFriend()  [S]
AddPerk(Perk akPerk, bool abNotify=false)
bool AddSpell(Spell akSpell, bool abVerbose=true)
AllowBleedoutDialogue(bool abCanTalk)
AllowPCDialogue(bool abTalk)
AttachAshPile(Form akAshPileBase = None)
AttemptAnimationSetSwitch()
bool CanFlyHere()
bool ChangeAnimArchetype(keyword apKeyword = none)
bool ChangeAnimFlavor(keyword apKeyword = none)
ChangeHeadPart(headpart apHeadPart, bool abRemovePart = false, bool abRemoveExtraParts = false)
ClearArrested()
ClearExpressionOverride()
ClearExtraArrows()
ClearForcedLandingMarker()  [S]
ClearLookAt()
bool Dismount()
DispelAllSpells()
bool DispelSpell(Spell akSpell)
DoCombatSpellApply(Spell akSpell, ObjectReference akTarget)
EnableAI(bool abEnable = true, bool abPauseVoice = false)
EndDeferredKill()
EquipItem(Form akItem, bool abPreventRemoval = false, bool abSilent = false)
EquipSpell(Spell akSpell, int aiSource)
EvaluatePackage(bool abResetAI = false)
FollowerWait()  [S]
FollowerFollow()  [S]
FollowerSetDistanceNear()  [S]
FollowerSetDistanceMedium()  [S]
FollowerSetDistanceFar()  [S]
ActorBase GetActorBase()  [S]
int GetBribeAmount()
Actor[] GetAllCombatTargets()
Faction GetCrimeFaction()
int GetCombatState()
Actor GetCombatTarget()
Package GetCurrentPackage()
Actor GetDialogueTarget()
int GetEquippedItemType(int aiEquipIndex)
Weapon GetEquippedWeapon(int aiEquipIndex = 0)
Armor GetEquippedShield()
Spell GetEquippedSpell(int aiSource)
int GetFactionRank(Faction akFaction)
int GetFactionReaction(Actor akOther)
int GetFlyingState()
ObjectReference GetForcedLandingMarker()
int GetGoldAmount()
int GetHighestRelationshipRank()
Actor GetKiller()
int GetLevel()
float GetLightLevel()
int GetLowestRelationshipRank()
ActorBase GetLeveledActorBase()
bool GetNoBleedoutRecovery()
bool GetPlayerControls()
Race GetRace()
int GetRelationshipRank(Actor akOther)
int GetSitState()
int GetSleepState()
bool HasAssociation(AssociationType akAssociation, Actor akOther = None)
bool HasFamilyRelationship(Actor akOther = None)
bool HasDetectionLOS(ObjectReference akOther)
bool HasMagicEffect(MagicEffect akEffect)
bool HasMagicEffectWithKeyword(Keyword akKeyword)
bool HasParentRelationship(Actor akOther)
bool HasPerk(Perk akPerk)
bool HasSpell(Form akForm)
bool IsAIEnabled()
bool IsAlarmed()
bool IsAlerted()
bool IsAllowedToFly()
bool IsArrested()
bool IsArrestingTarget()
bool IsBeingRidden()
bool IsBeingRiddenBy(Actor akActor)
bool IsBleedingOut()
bool IsBribed()
bool IsChild()
bool IsCommandedActor()
bool IsDead()
bool IsDetectedBy(Actor akOther)
bool IsDismembered(string asBodyPart = "")
bool IsDoingFavor()
bool IsEquipped(Form akItem)
bool IsEssential()
bool IsFlying()
bool IsGuard()
bool IsGhost()
bool IsHostileToActor(Actor akActor)
bool IsInCombat()
bool IsInFaction(Faction akFaction)
bool IsInIronSights()
bool IsInKillMove()
bool IsInPowerArmor()  [S]
bool IsInScene()
bool IsIntimidated()
bool IsOnMount()
bool IsOverEncumbered()
bool IsOwner(ObjectReference akObject)  [S]
bool IsPlayersLastRiddenHorse()
bool IsPlayerTeammate()
bool IsRunning()
bool IsSeatOccupied(keyword apKeyword)
bool IsSneaking()
bool IsSprinting()
bool IsTalking()
bool IsTrespassing()
bool IsUnconscious()
bool IsWeaponDrawn()
Kill(Actor akKiller = None)
KillEssential(Actor akKiller = None)  [S]
KillSilent(Actor akKiller = None)
Dismember(string asBodyPart, bool abForceExplode = false, bool abForceDismember = false, bool abForceBloodyMess = false)
MarkItemAsFavorite(Form akItem, int aiSlot=-1)
ModFactionRank(Faction akFaction, int aiMod)
MoveToPackageLocation()  [L]
OpenInventory(bool abForceOpen = false)
bool PathToReference(ObjectReference aTarget, float afWalkRunPercent)  [L]
bool PlayIdle(Idle akIdle)
bool PlayIdleAction(Action aAction, ObjectReference aTarget = None)
bool PlayIdleWithTarget(Idle akIdle, ObjectReference akTarget)
PlaySubGraphAnimation(string asEventName)
RemoveFromFaction(Faction akFaction)
RemoveFromAllFactions()
RemovePerk(Perk akPerk)
bool RemoveSpell(Spell akSpell)
ResetHealthAndLimbs()
Resurrect()  [L]
SendAssaultAlarm()
SendTrespassAlarm(Actor akCriminal)
SetAlert(bool abAlerted = true)
SetAllowFlying(bool abAllowed = true, bool abAllowCrash = true, bool abAllowSearch = false)
SetAlpha(float afTargetAlpha, bool abFade = false)
SetAttackActorOnSight(bool abAttackOnSight = true)
SetAnimArchetypeConfident()  [S]
SetAnimArchetypeDepressed()  [S]
SetAnimArchetypeElderly()  [S]
SetAnimArchetypeFriendly()  [S]
SetAnimArchetypeIrritated()  [S]
SetAnimArchetypeNeutral()  [S]
SetAnimArchetypeNervous()  [S]
SetDogAnimArchetypeAgitated()  [S]
SetDogAnimArchetypeAlert()  [S]
SetDogAnimArchetypeNeutral()  [S]
SetDogAnimArchetypePlayful()  [S]
SetAvoidPlayer(bool abAvoid = true)
SetCommandState(bool abStartCommandMode)
SetBribed(bool abBribe = true)
SetCanDoCommand(bool abCanCommand= true)
SetCombatStyle(CombatStyle akCombatStyle)
SetCompanion(bool SetCompanion = true, bool FillCompanionAlias = true)  [S]
SetAvailableToBeCompanion()  [S]
DisallowCompanion(bool SuppressDismissMessage = false)  [S]
AllowCompanion(bool MakeCompanionIfNoneCurrently = true, bool ForceCompanion = false)  [S]
SetCrimeFaction(Faction akFaction)
SetCriticalStage(int aiStage)
SetDoingFavor(bool abDoingFavor = true, bool abWorkShopMode=false)
ChangeAnimFaceArchetype(keyword apKeyword = none)
SetEyeTexture(TextureSet akNewTexture)
SetEssential(bool abEssential)
SetFactionRank(Faction akFaction, int aiRank)
SetForcedLandingMarker(ObjectReference aMarker)
SetGhost(bool abIsGhost = true)
SetHasCharGenSkeleton(bool abCharGen = true)
AddToFaction(Faction akFaction)  [S]
SetHeadTracking(bool abEnable = true)
SetIntimidated(bool abIntimidate = true)
SetLookAt(ObjectReference akTarget, bool abPathingLookAt = false)
SetNoBleedoutRecovery(bool abAllowed)
SetNotShowOnStealthMeter(bool abNotShow)
SetOutfit(Outfit akOutfit, bool abSleepOutfit = false)
SetOverrideVoiceType(VoiceType akVoiceType)
SetPlayerControls(bool abControls)
SetPlayerResistingArrest()
SetPlayerTeammate(bool abTeammate = true, bool abCanDoFavor=true, bool abGivePlayerXP=false)
SetProtected(bool abProtected)
SetRace(Race akRace = None)
SetRelationshipRank(Actor akOther, int aiRank)
bool SetRestrained(bool abRestrained = true)
SetSubGraphFloatVariable(string asVariableName, float afValue)
bool SetUnconscious(bool abUnconscious = true)
SetVehicle(Actor akVehicle)
ShowBarterMenu()
StartCannibal(Actor akTarget)
StartCombat(Actor akTarget, bool abPreferredTarget = false)
StartDeferredKill()
StartVampireFeed(Actor akTarget)
StartFrenzyAttack(float aChance = 0.1, float aInterval = 0.5)
StopCombat()
StopCombatAlarm()
SwitchToPowerArmor(ObjectReference aArmorFurniture)
bool SnapIntoInteraction(ObjectReference akTarget)
bool TrapSoul(Actor akTarget)
UnequipAll()
UnequipItem(Form akItem, bool abPreventEquip = false, bool abSilent = false)
UnequipItemSlot(int aiSlot)
UnequipSpell(Spell akSpell, int aiSource)
UnLockOwnedDoorsInCell()
bool WillIntimidateSucceed()
bool WornHasKeyword(Keyword akKeyword)
bool WouldBeStealing(ObjectReference akObject)
int WouldRefuseCommand(ObjectReference akObject)
StartSneaking()
DrawWeapon()
DogPlaceInMouth(Form akItem)
DogDropItems()
ForceMovementDirection(float afXAngle = 0.0, float afYAngle = 0.0, float afZAngle = 0.0)  [dbg]
ForceMovementSpeed(float afSpeedMult)  [dbg]
ForceMovementRotationSpeed(float afXMult = 0.0, float afYMult = 0.0, float afZMult = 0.0)  [dbg]
ForceMovementDirectionRamp(float afXAngle = 0.0, float afYAngle = 0.0, float afZAngle = 0.0, float afRampTime = 0.1)  [dbg]
ForceMovementSpeedRamp(float afSpeedMult, float afRampTime = 0.1)  [dbg]
ForceMovementRotationSpeedRamp(float afXMult = 0.0, float afYMult = 0.0, float afZMult = 0.0, float afRampTime = 0.1)  [dbg]
ForceTargetDirection(float afXAngle = 0.0, float afYAngle = 0.0, float afZAngle = 0.0)  [dbg]
ForceTargetSpeed(float afSpeed)  [dbg]
ForceTargetAngle(float afXAngle = 0.0, float afYAngle = 0.0, float afZAngle = 0.0)  [dbg]
ClearForcedMovement()  [dbg]
bool CanMoveVertical()  [dbg]
bool CanStrafe()  [dbg]
WornItem GetWornItem(int slotIndex, bool firstPerson = false)  [F4SE]
ObjectMod[] GetWornItemMods(int slotIndex)  [F4SE]
InstanceData:Owner GetInstanceOwner(int slotIndex)  [S,F4SE]
QueueUpdate(bool bDoEquipment = false, int flags = 0)  [F4SE]
ObjectReference GetFurnitureReference()  [F4SE]
bool IsProtected()  [F4SE]

Event OnCombatStateChanged(Actor akTarget, int aeCombatState)
Event OnCommandModeCompleteCommand(int aeCommandType, ObjectReference akTarget)
Event OnCommandModeEnter()
Event OnCommandModeExit()
Event OnCommandModeGiveCommand(int aeCommandType, ObjectReference akTarget)
Event OnCompanionDismiss()
Event OnConsciousnessStateChanged(bool abUnconscious)
Event OnCripple(ActorValue akActorValue, bool abCrippled)
Event OnDeferredKill(Actor akKiller)
Event OnDeath(Actor akKiller)
Event OnDifficultyChanged(int aOldDifficulty, int aNewDifficulty)
Event OnDying(Actor akKiller)
Event OnEnterBleedout()
Event OnEnterSneaking()
Event OnEscortWaitStart()
Event OnEscortWaitStop()
Event OnGetUp(ObjectReference akFurniture)
Event OnItemEquipped(Form akBaseObject, ObjectReference akReference)
Event OnItemUnequipped(Form akBaseObject, ObjectReference akReference)
Event OnKill(Actor akVictim)
Event OnLocationChange(Location akOldLoc, Location akNewLoc)
Event OnPackageChange(Package akOldPackage)
Event OnPackageEnd(Package akOldPackage)
Event OnPackageStart(Package akNewPackage)
Event OnPartialCripple(ActorValue akActorValue, bool abCrippled)
Event OnPickpocketFailed()
Event OnPlayerCreateRobot(Actor akNewRobot)
Event OnPlayerEnterVertibird(ObjectReference akVertibird)
Event OnPlayerFallLongDistance(float afDamage)
Event OnPlayerFireWeapon(Form akBaseObject)
Event OnPlayerHealTeammate(Actor akTeammate)
Event OnPlayerLoadGame()   ; player actor only (or alias/effect on player)
Event OnPlayerModArmorWeapon(Form akBaseObject, ObjectMod akModBaseObject)
Event OnPlayerModRobot(Actor akRobot, ObjectMod akModBaseObject)
Event OnPlayerSwimming()
Event OnPlayerUseWorkBench(ObjectReference akWorkBench)
Event OnRaceSwitchComplete()
Event OnSit(ObjectReference akFurniture)
Event OnSpeechChallengeAvailable(ObjectReference akSpeaker)

F4SE Struct Actor:WornItem { Form item; Form model; string modelName; Form materialSwap; TextureSet texture }
consts: CritStage_None=0, CritStage_GooStart=1, CritStage_GooEnd=2, CritStage_DisintegrateStart=3, CritStage_DisintegrateEnd=4, CritStage_FreezeStart=5, CritStage_FreezeEnd=6
```

#### ActorBase
`extends Form` · `Native Hidden` · 16 natives + 6 F4SE [src: f4se/scripts/vanilla/ActorBase.psc] [src: f4se/scripts/modified/ActorBase.psc]

> No face/headpart/tint/weight setters in vanilla. F4SE adds GetHeadParts, Get/SetBodyWeight (3-axis struct), GetTemplate, GetOutfit.

```papyrus
Class GetClass()
int GetDeadCount()
FormList GetGiftFilter()
Race GetRace()
int GetLevel()
int GetLevelExact()
int GetSex()
Actor GetUniqueActor()
bool IsEssential()
bool IsInvulnerable()
bool IsProtected()
bool IsUnique()
SetEssential(bool abEssential = true)
SetInvulnerable(bool abInvulnerable = true)
SetProtected(bool abProtected = true)
SetOutfit(Outfit akOutfit, bool abSleepOutfit = false)
ActorBase GetTemplate(bool bTopMost = true)  [F4SE]
bool HasHeadPartOverlays()  [F4SE]
HeadPart[] GetHeadParts(bool bOverlays = false)  [F4SE]
Outfit GetOutfit(bool bSleepOutfit = false)  [F4SE]
SetBodyWeight(BodyWeight weight)  [F4SE]
BodyWeight GetBodyWeight()  [F4SE]

F4SE Struct ActorBase:BodyWeight { float thin; float muscular; float large }
```

#### Game
`extends (none)` · `Native Hidden` · 104 natives + 11 script-defined + 10 F4SE [src: f4se/scripts/vanilla/Game.psc] [src: f4se/scripts/modified/Game.psc]

> No GetFormEx/GetModCount/GetModName/GetCurrentCrosshairRef/tint-mask functions/Disable/EnablePlayerControls/EnableFastTravel (use InputEnableLayer). F4SE adds GetInstalledPlugins (struct array), SetGameSetting*, GetCameraState, GetCurrentConsoleRef.

```papyrus
AddAchievement(int aiAchievementID)  [G]
AddPerkPoints(int aiPerkPoints)  [G]
AdvanceSkill(string asSkillName, float afMagnitude)  [G]
ClearPrison()  [G]
ClearTempEffects()  [G]
EnablePipboyHDRMask(bool abEnable = true)  [G]
Error(string asMessage)  [G]
FadeOutGame(bool abFadingOut, bool abBlackFade, float afSecsBeforeFade, float afFadeDuration, bool abStayFaded = false)  [G]
FastTravel(ObjectReference akDestination)  [G]
ObjectReference FindClosestReferenceOfType(Form arBaseObject, float afX, float afY, float afZ, float afRadius)  [G]
ObjectReference FindRandomReferenceOfType(Form arBaseObject, float afX, float afY, float afZ, float afRadius)  [G]
ObjectReference FindClosestReferenceOfAnyTypeInList(FormList arBaseObjects, float afX, float afY, float afZ, float afRadius)  [G]
ObjectReference FindRandomReferenceOfAnyTypeInList(FormList arBaseObjects, float afX, float afY, float afZ, float afRadius)  [G]
ObjectReference FindClosestReferenceOfTypeFromRef(Form arBaseObject, ObjectReference arCenter, float afRadius)  [G,S]
ObjectReference FindRandomReferenceOfTypeFromRef(Form arBaseObject, ObjectReference arCenter, float afRadius)  [G,S]
ObjectReference FindClosestReferenceOfAnyTypeInListFromRef(FormList arBaseObjects, ObjectReference arCenter, float afRadius)  [G,S]
ObjectReference FindRandomReferenceOfAnyTypeInListFromRef(FormList arBaseObjects, ObjectReference arCenter, float afRadius)  [G,S]
Actor FindClosestActor(float afX, float afY, float afZ, float afRadius)  [G]
Actor FindRandomActor(float afX, float afY, float afZ, float afRadius)  [G]
Actor FindClosestActorFromRef(ObjectReference arCenter, float afRadius)  [G,S]
Actor FindRandomActorFromRef(ObjectReference arCenter, float afRadius)  [G,S]
ForceDisableSSRGodraysDirLight(bool abDisableSSR, bool abDisableGodrays, bool abDisableDirLight)  [G]
ForceThirdPerson()  [G]
ForceFirstPerson()  [G]
int GetXPForLevel(int auiLevel)  [G]
ShowFirstPersonGeometry(bool abShow = true)  [G]
ShowAllMapMarkers()  [G]
ActorValue GetAggressionAV()  [G]
ActorValue GetAgilityAV()  [G]
MiscObject GetCaps()  [G,S]
ActorValue GetCharismaAV()  [G]
CommonPropertiesScript GetCommonProperties()  [G,S]
ActorValue GetConfidenceAV()  [G]
int GetDifficulty()  [G]
ActorValue GetEnduranceAV()  [G]
Form GetForm(int aiFormID)  [G]
Form GetFormFromFile(int aiFormID, string asFilename)  [G]
float GetGameSettingFloat(string asGameSetting)  [G]
int GetGameSettingInt(string asGameSetting)  [G]
string GetGameSettingString(string asGameSetting)  [G]
ActorValue GetHealthAV()  [G]
ActorValue GetIntelligenceAV()  [G]
ActorValue GetLuckAV()  [G]
ActorValue GetPerceptionAV()  [G]
Actor GetPlayer()  [G]
Actor[] GetPlayerFollowers()  [G]
int GetPlayerLevel()  [G,S]
ObjectReference GetPlayerGrabbedRef()  [G]
float GetPlayerRadioFrequency()  [G]
Actor GetPlayersLastRiddenHorse()  [G]
float GetRealHoursPassed()  [G]
ActorValue GetSuspiciousAV()  [G]
ActorValue GetStrengthAV()  [G]
GivePlayerCaps(int nCaps)  [G,S]
RemovePlayerCaps(int nCaps)  [G,S]
IncrementSkill(ActorValue akActorValue, int aiCount = 1)  [G]
IncrementStat(string asStatName, int aiModAmount = 1)  [G]
InitializeMarkerDistances()  [G]
bool IsActivateControlsEnabled()  [G]
bool IsVATSControlsEnabled()  [G]
bool IsVATSPlaybackActive()  [G]
bool IsCamSwitchControlsEnabled()  [G]
bool IsFastTravelControlsEnabled()  [G]
bool IsFastTravelEnabled()  [G]
bool IsFavoritesControlsEnabled()  [G]
bool IsFightingControlsEnabled()  [G]
bool IsJournalControlsEnabled()  [G]
bool IsJumpingControlsEnabled()  [G]
bool IsLookingControlsEnabled()  [G]
bool IsMenuControlsEnabled()  [G]
bool IsMovementControlsEnabled()  [G]
bool IsPluginInstalled(string asName)  [G]
bool IsPlayerInRadioRange(float afFrequency)  [G]
bool IsPlayerListening(float afFrequency)  [G]
bool IsPlayerRadioOn()  [G]
bool IsSneakingControlsEnabled()  [G]
PassTime(int aiHours)  [G]
PlayBink(string asFileName, bool abInterruptible = false, bool abMuteAudio = true, bool abMuteMusic = true, bool abLetterbox = true, bool abIsNewGameBink = false)  [G,L]
PrecacheCharGen()  [G]
PrecacheCharGenClear()  [G]
int QueryStat(string asStat)  [G]
QuitToMainMenu()  [G]
RequestAutoSave()  [G]
RequestModel(string asModelName)  [G]
RequestSave()  [G]
RewardPlayerXP(int auiXPAmount, bool abDirect = false)  [G]
ServeTime()  [G]
SetCameraTarget(Actor arTarget)  [G]
SetCharGenHUDMode(int aiCGHUDMode)  [G]
SetInsideMemoryHUDMode(bool aInsideMemory)  [G]
ShowPerkVaultBoyOnHUD(string aVaultBoySwf, Sound aSoundDescriptor = None)  [G]
SetInChargen(bool abDisableSaving, bool abDisableWaiting, bool abShowControlsDisabledMessage)  [G]
SetPlayerAIDriven(bool abAIDriven = true)  [G]
SetPlayerOnElevator(bool abOnElevator= true)  [G]
SetPlayerRadioFrequency(float afFrequency)  [G]
SetPlayerReportCrime(bool abReportCrime = true)  [G]
SetSittingRotation(float afValue)  [G]
ShakeCamera(ObjectReference akSource = None, float afStrength = 0.5, float afDuration = 0.0)  [G]
ShakeController(float afSmallMotorStrength, float afBigMotorStreangth, float afDuration)  [G]
ShowFatigueWarningOnHUD()  [G]
ShowRaceMenu(ObjectReference akMenuTarget = None, int uiMode = 0, ObjectReference akMenuSpouseFemale = None, ObjectReference akMenuSpouseMale = None, ObjectReference akVendor = None)  [G]
ShowSPECIALMenu()  [G]
ShowTitleSequenceMenu()  [G]
HideTitleSequenceMenu()  [G]
StartTitleSequence(string asSequenceName)  [G]
ShowPipboyBootSequence(string asAnimationName)  [G]
ShowPipboyPlugin()  [G]
ShowTrainingMenu(Actor aTrainer)  [G]
TriggerScreenBlood(int aiValue)  [G]
PlayEventCamera(CameraShot akCamera, ObjectReference akRef)  [G]
StartDialogueCameraOrCenterOnTarget(ObjectReference akCameraTarget = None)  [G]
StopDialogueCamera(bool abConsiderResume = false, bool abSwitchingTo1stP = false)  [G]
TurnPlayerRadioOn(bool abRadioOn = true)  [G]
bool UsingGamepad()  [G]
Warning(string asMessage)  [G]
ObjectReference GetCurrentConsoleRef()  [G,F4SE]
PluginInfo[] GetInstalledPlugins()  [G,F4SE]
PluginInfo[] GetInstalledLightPlugins()  [G,F4SE]
string[] GetPluginDependencies(string plugin)  [G,F4SE]
SetGameSettingFloat(string setting, float value)  [G,F4SE]
SetGameSettingInt(string setting, int value)  [G,F4SE]
SetGameSettingBool(string setting, bool value)  [G,F4SE]
SetGameSettingString(string setting, string value)  [G,F4SE]
UpdateThirdPerson()  [G,F4SE]
int GetCameraState()  [G,F4SE]

F4SE Struct Game:PluginInfo { int index; string name; string author; string description }
```

#### Debug
`extends (none)` · `Native DebugOnly Hidden` · 31 natives + 4 script-defined [src: f4se/scripts/vanilla/Debug.psc]

> Whole script is flagged `DebugOnly`: calls are stripped from scripts compiled in release mode; the natives still exist at runtime. No SendAnimationEvent in FO4.

```papyrus
CenterOnCell(string asCellname)  [G]
float CenterOnCellAndWait(string asCellname)  [G,L]
float PlayerMoveToAndWait(string asDestRef)  [G,L]
CloseUserLog(string asLogName)  [G]
DumpAliasData(Quest akQuest)  [G]
DumpEventRegistrations(ScriptObject akScript)  [G]
EnableAI(bool abEnable = true)  [G]
EnableCollisions(bool abEnable = true)  [G]
EnableDetection(bool abEnable = true)  [G]
EnableMenus(bool abEnable = true)  [G]
string GetConfigName()  [G]
string GetPlatformName()  [G]
string GetVersionNumber()  [G]
MessageBox(string asMessageBoxText)  [G]
Notification(string asNotificationText)  [G]
bool OpenUserLog(string asLogName)  [G]
QuitGame()  [G]
SetFootIK(bool abFootIK)  [G]
SetGodMode(bool abGodMode)  [G]
StartScriptProfiling(string asScriptName)  [G]
StartStackProfiling()  [G]
StartStackRootProfiling(string asScriptName, ScriptObject akObj = None)  [G]
StopScriptProfiling(string asScriptName)  [G]
StopStackProfiling()  [G]
StopStackRootProfiling(string asScriptName, ScriptObject akObj = None)  [G]
Trace(string asTextToPrint, int aiSeverity = 0)  [G]
TraceFunction(string asTextToPrint = "Tracing function on request", int aiSeverity = 0)  [G]
TraceStack(string asTextToPrint = "Tracing stack on request", int aiSeverity = 0)  [G]
bool TraceUser(string asUserLog, string asTextToPrint, int aiSeverity = 0)  [G]
TraceConditionalGlobal(string TextToPrint, GlobalVariable ShowTrace)  [G,S]
TraceConditional(string TextToPrint, bool ShowTrace, int Severity = 0)  [G,S]
TraceAndBox(string asTextToPrint, int aiSeverity = 0)  [G,S]
TraceSelf(ScriptObject CallingScript, string FunctionName, string StringToTrace)  [G,S]
ShowRefPosition(ObjectReference arRef)  [G]
DBSendPlayerPosition()  [G]
```

#### Utility
`extends (none)` · `Native Hidden` · 30 natives + 2 F4SE [src: f4se/scripts/vanilla/Utility.psc] [src: f4se/scripts/modified/Utility.psc]

> No GetINI* in FO4 (SetINI* are debugOnly). No Create*/Resize*Array (FO4 arrays are dynamic). CallGlobalFunction(NoWait) replaces SKSE-style dynamic calls.

```papyrus
Var CallGlobalFunction(string asScriptName, string asFuncName, Var[] aParams)  [G,L]
CallGlobalFunctionNoWait(string asScriptName, string asFuncName, Var[] aParams)  [G]
string GameTimeToString(float afGameTime)  [G]
float GetCurrentGameTime()  [G]
float GetCurrentRealTime()  [G]
int GetCurrentStackID()  [G,dbg]
bool IsInMenuMode()  [G]
int RandomInt(int aiMin = 0, int aiMax = 100)  [G]
float RandomFloat(float afMin = 0.0, float afMax = 1.0)  [G]
SetINIFloat(string ini, float value)  [G,dbg]
SetINIInt(string ini, int value)  [G,dbg]
SetINIBool(string ini, bool value)  [G,dbg]
SetINIString(string ini, string value)  [G,dbg]
Wait(float afSeconds)  [G,L]
WaitGameTime(float afHours)  [G,L]
WaitMenuMode(float afSeconds)  [G,L]
string CaptureFrameRate(int numFrames)  [G,dbg]
EnterTestData(string astestType, string astestMatter, string astestDetails, string astestResultContext, string astestResult)  [G,dbg]
PostStartUpTimes()  [G,dbg]
StartFrameRateCapture()  [G,dbg]
EndFrameRateCapture()  [G,dbg]
float GetAverageFrameRate()  [G,dbg]
float GetMinFrameRate()  [G,dbg]
float GetMaxFrameRate()  [G,dbg]
string GetCurrentMemory()  [G,dbg]
int GetBudgetCount()  [G,dbg]
string GetCurrentBudget(int aiBudgetNumber)  [G,dbg]
string GetBudgetLimit(int aiBudgetNumber)  [G,dbg]
bool OverBudget(int aiBudgetNumber)  [G,dbg]
string GetBudgetName(int aiBudgetNumber)  [G,dbg]
Var[] VarToVarArray(Var v)  [G,F4SE]
Var VarArrayToVar(Var[] v)  [G,F4SE]
```

#### Math
`extends (none)` · `Native Hidden` · 13 natives + 2 script-defined + 8 F4SE [src: f4se/scripts/vanilla/Math.psc] [src: f4se/scripts/modified/Math.psc]

```papyrus
float abs(float afValue)  [G]
float acos(float afValue)  [G]
float asin(float afValue)  [G]
float atan(float afValue)  [G]
int Ceiling(float afValue)  [G]
float cos(float afValue)  [G]
float DegreesToRadians(float afDegrees)  [G]
int Floor(float afValue)  [G]
float pow(float x, float y)  [G]
float RadiansToDegrees(float afRadians)  [G]
float sin(float afValue)  [G]
float sqrt(float afValue)  [G]
float tan(float afValue)  [G]
float Max(float afValue1, float afValue2)  [G,S]
float Min(float afValue1, float afValue2)  [G,S]
int LeftShift(int value, int shiftBy)  [G,F4SE]
int RightShift(int value, int shiftBy)  [G,F4SE]
int LogicalAnd(int arg1, int arg2)  [G,F4SE]
int LogicalOr(int arg1, int arg2)  [G,F4SE]
int LogicalXor(int arg1, int arg2)  [G,F4SE]
int LogicalNot(int arg1)  [G,F4SE]
float Log(float arg1)  [G,F4SE]
float Exp(float arg1)  [G,F4SE]
```

#### InputEnableLayer
`extends ScriptObject` · `Native Hidden` · 35 natives [src: f4se/scripts/vanilla/InputEnableLayer.psc]

> Replaces Game.DisablePlayerControls/EnablePlayerControls/EnableFastTravel. A layer is a ScriptObject (not a Form): create with InputEnableLayer.Create(), keep a reference, Delete() when done; any layer disabling a control disables it.

```papyrus
InputEnableLayer Create()  [G]
Delete()
DisablePlayerControls(bool abMovement = true, bool abFighting = true, bool abCamSwitch = false, bool abLooking = false, bool abSneaking = false, bool abMenu = true, bool abActivate = true, bool abJournalTabs = false, bool abVATS = true, bool abFavorites = true, bool abRunning = false)
EnableActivate(bool abEnable = true)
EnableVATS(bool abEnable = true)
EnableCamSwitch(bool abEnable = true)
EnableFastTravel(bool abEnable = true)
EnableFavorites(bool abEnable = true)
EnableFighting(bool abEnable = true)
EnableJournal(bool abEnable = true)
EnableJumping(bool abEnable = true)
EnableLooking(bool abEnable = true)
EnableMenu(bool abEnable = true)
EnableMovement(bool abEnable = true)
EnableRunning(bool abEnable = true)
EnableSprinting(bool abEnable = true)
EnableZKey(bool abEnable = true)
EnablePlayerControls(bool abMovement = true, bool abFighting = true, bool abCamSwitch = true, bool abLooking = true, bool abSneaking = true, bool abMenu = true, bool abActivate = true, bool abJournalTabs = true, bool abVATS = true, bool abFavorites = true, bool abRunning = true)
EnableSneaking(bool abEnable = true)
bool IsActivateEnabled()
bool IsVATSEnabled()
bool IsCamSwitchEnabled()
bool IsFastTravelEnabled()
bool IsFavoritesEnabled()
bool IsFightingEnabled()
bool IsJournalEnabled()
bool IsJumpingEnabled()
bool IsLookingEnabled()
bool IsMenuEnabled()
bool IsMovementEnabled()
bool IsRunningEnabled()
bool IsSprintingEnabled()
bool IsZKeyEnabled()
bool IsSneakingEnabled()
Reset()
```

#### GlobalVariable
`extends Form` · `Native Hidden` · 2 natives + 3 script-defined [src: f4se/scripts/vanilla/GlobalVariable.psc]

> GetValueInt/SetValueInt/Mod and the `Value` property are script-defined wrappers over GetValue/SetValue.

```papyrus
float GetValue()
SetValue(float afNewValue)
int GetValueInt()  [S]
SetValueInt(int aiNewValue)  [S]
float Mod(float afHowMuch)  [S]
```

#### F4SE
`extends (none)` · `Native Hidden` · 0 natives + 6 F4SE · **F4SE-only class** [src: f4se/scripts/modified/F4SE.psc]

```papyrus
int GetVersion()  [G,F4SE]
int GetVersionMinor()  [G,F4SE]
int GetVersionBeta()  [G,F4SE]
int GetVersionRelease()  [G,F4SE]
int GetScriptVersionRelease()  [G,S,F4SE]
int GetPluginVersion(string name)  [G,F4SE]
```

#### UI
`extends (none)` · `Native Hidden` · 0 natives + 10 F4SE · **F4SE-only class** [src: f4se/scripts/modified/UI.psc]

> F4SE-only class. IsMenuOpen uses FO4 menu names (e.g. "PipboyMenu", "LooksMenu", "ContainerMenu").

```papyrus
bool IsMenuOpen(string menu)  [G,F4SE]
bool IsMenuRegistered(string menu)  [G,F4SE]
bool RegisterBasicCustomMenu(string menuName, string menuPath, string rootPath)  [G,S,F4SE]
bool RegisterCustomMenu(string menuName, string menuPath, string rootPath, MenuData mData)  [G,F4SE]
bool OpenMenu(string menuName)  [G,F4SE]
bool CloseMenu(string menuName)  [G,F4SE]
bool Set(string menu, string path, Var arg)  [G,L,F4SE]
Var Get(string menu, string path)  [G,L,F4SE]
Var Invoke(string menu, string path, Var[] args = None)  [G,L,F4SE]
bool Load(string menu, string sourceVar, string assetPath, ScriptObject receiver = None, string callback = "")  [G,L,F4SE]

F4SE Struct UI:MenuData { int menuFlags = 0x801849D; int movieFlags = 3; int extendedFlags = 3; int depth = 6 }
```

#### Input
`extends (none)` · `Native Hidden` · 0 natives + 2 F4SE · **F4SE-only class** [src: f4se/scripts/modified/Input.psc]

> F4SE-only class. There is no IsKeyPressed/GetNumKeysPressed/TapKey in FO4 F4SE; use RegisterForKey + OnKeyDown/OnKeyUp or the platform input hook.

```papyrus
int GetMappedKey(string control, int deviceType = 0xFF)  [G,F4SE]
string GetMappedControl(int keycode)  [G,F4SE]
```

#### FavoritesManager
`extends (none)` · `Native Hidden` · 0 natives + 6 F4SE · **F4SE-only class** [src: f4se/scripts/modified/FavoritesManager.psc]

```papyrus
Form[] GetTaggedForms()  [G,L,F4SE]
AddTaggedForms(Form[] forms)  [G,L,F4SE]
RemoveTaggedForms(Form[] forms)  [G,L,F4SE]
bool IsTaggedForm(Form akForm)  [G,L,F4SE]
Form[] GetFavorites()  [G,L,F4SE]
SetFavorites(Form[] favorites)  [G,L,F4SE]
```

#### DefaultObject
`extends Form` · `native Hidden` · 0 natives + 3 F4SE · **F4SE-only class** [src: f4se/scripts/modified/DefaultObject.psc]

```papyrus
Form GetDefaultObject(string editorId)  [G,F4SE]
Form Get()  [F4SE]
Set(Form newForm)  [F4SE]
```

#### CommonArrayFunctions
`extends (none)` · `-` · 0 natives + 12 script-defined [src: f4se/scripts/vanilla/CommonArrayFunctions.psc]

```papyrus
bool CheckObjectReferenceAgainstArray(ObjectReference ObjectToCheck, ObjectReference[] ArrayToCheck, bool returnValueIfArrayIsEmpty = false)  [G,S]
int FindInReferenceAliasArray(ObjectReference ObjectToCheck, ReferenceAlias[] ArrayToCheck)  [G,S]
bool CheckObjectReferenceAgainstReferenceAliasArray(ObjectReference ObjectToCheck, ReferenceAlias[] ArrayToCheck, bool returnValueIfArrayIsEmpty = false)  [G,S]
bool CheckActorAgainstFactionArray(Actor ObjectToCheck, Faction[] ArrayToCheck, bool returnValueIfArrayIsEmpty = false)  [G,S]
bool CheckObjectAgainstKeywordArray(ObjectReference ObjectToCheck, Keyword[] ArrayToCheck, bool returnValueIfArrayIsEmpty = false)  [G,S]
bool CheckFormAgainstKeywordArray(Form ObjectToCheck, Keyword[] ArrayToCheck, bool returnValueIfArrayIsEmpty = false)  [G,S]
bool CheckFormAgainstArray(Form FormToCheck, Form[] ArrayToCheck, bool returnValueIfArrayIsEmpty = false)  [G,S]
bool CheckLocationAgainstArray(Location ObjectToCheck, Location[] ArrayToCheck, bool returnValueIfArrayIsEmpty = false, bool matchIfChildLocation = false)  [G,S]
bool CheckLocationAgainstLocationAliasArray(Location ObjectToCheck, LocationAlias[] ArrayToCheck, bool returnValueIfArrayIsEmpty = false, bool matchIfChildLocation = false)  [G,S]
keyword GetFirstFoundKeywordInArrayForLocation(Location LocationToCheck, Keyword[] ArrayToCheck)  [G,S]
Faction GetFirstFoundFactionInArrayForActor(Actor ActorToCheck, Faction[] ArrayToCheck)  [G,S]
bool IsActorInArrayHostileToActor(Actor ActorToCheck, ObjectReference[] ArrayToCheck)  [G,S]
```

### 1.3 Items, crafting and instances

#### Weapon
`extends Form` · `Native Hidden` · 2 natives + 3 F4SE [src: f4se/scripts/vanilla/Weapon.psc] [src: f4se/scripts/modified/Weapon.psc]

> No GetWeaponType/GetBaseDamage etc. in vanilla; weapon stats live in OMOD-modified instances (F4SE InstanceData).

```papyrus
Fire(ObjectReference akSource, Ammo akAmmo = None)
Ammo GetAmmo()
ObjectMod GetEmbeddedMod()  [F4SE]
SetEmbeddedMod(ObjectMod mod)  [F4SE]
InstanceData:Owner GetInstanceOwner()  [S,F4SE]
```

#### Armor
`extends Form` · `Native Hidden` · 0 natives + 1 F4SE [src: f4se/scripts/vanilla/Armor.psc] [src: f4se/scripts/modified/Armor.psc]

> No vanilla natives at all; F4SE adds GetArmorAddons. Ratings/health via F4SE InstanceData.

```papyrus
ArmorAddon[] GetArmorAddons()  [F4SE]
```

#### ArmorAddon
`extends Form` · `Native Hidden` · 0 natives + 1 F4SE · **F4SE-only class** [src: f4se/scripts/modified/ArmorAddon.psc]

```papyrus
Race[] GetAdditionalRaces()  [F4SE]
```

#### Ammo
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Ammo.psc]

(no functions, events or structs)

#### ObjectMod
`extends Form` · `Native Hidden` · 0 natives + 6 F4SE [src: f4se/scripts/vanilla/ObjectMod.psc] [src: f4se/scripts/modified/ObjectMod.psc]

> No vanilla natives. F4SE adds PropertyModifier[] access plus ~110 target-ID constants.

```papyrus
PropertyModifier[] GetPropertyModifiers()  [F4SE]
int GetMaxRank()  [F4SE]
SetMaxRank(int rank)  [F4SE]
int GetPriority()  [F4SE]
SetPriority(int priority)  [F4SE]
MiscObject GetLooseMod()  [F4SE]

F4SE Struct ObjectMod:PropertyModifier { int target; int operator; Form object; float value1; float value2 }
F4SE consts: 108 property-modifier target IDs (Weapon_Target_* 0..94, Armor_Target_* 256..269, Actor_Target_* 512..517); Modifier_Operator_Set=0, Modifier_Operator_Add=1, Modifier_Operator_Mult_Add=2, Modifier_Operator_And=3, Modifier_Operator_Or=4, Modifier_Operator_Rem=5
```

#### InstanceData
`extends (none)` · `Native Hidden` · 0 natives + 56 F4SE · **F4SE-only class** [src: f4se/scripts/modified/InstanceData.psc]

> F4SE-only. Addresses a weapon/armor *instance* via struct InstanceData:Owner {owner, slotIndex} (slotIndex -1 = the reference itself; equipped items via Actor.GetInstanceOwner(slot)).

```papyrus
int GetAttackDamage(Owner akOwner)  [G,F4SE]
SetAttackDamage(Owner akOwner, int damage)  [G,F4SE]
DamageTypeInfo[] GetDamageTypes(Owner akOwner)  [G,F4SE]
SetDamageTypes(Owner akOwner, DamageTypeInfo[] dts)  [G,F4SE]
int GetAmmoCapacity(Owner akOwner)  [G,F4SE]
SetAmmoCapacity(Owner akOwner, int capacity)  [G,F4SE]
Ammo GetAmmo(Owner akOwner)  [G,F4SE]
SetAmmo(Owner akOwner, Ammo akAmmo)  [G,F4SE]
LeveledItem GetAddAmmoList(Owner akOwner)  [G,F4SE]
SetAddAmmoList(Owner akOwner, LeveledItem akAmmo)  [G,F4SE]
int GetAccuracyBonus(Owner akOwner)  [G,F4SE]
SetAccuracyBonus(Owner akOwner, int bonus)  [G,F4SE]
float GetActionPointCost(Owner akOwner)  [G,F4SE]
SetActionPointCost(Owner akOwner, float cost)  [G,F4SE]
float GetAttackDelay(Owner akOwner)  [G,F4SE]
SetAttackDelay(Owner akOwner, float delay)  [G,F4SE]
float GetOutOfRangeMultiplier(Owner akOwner)  [G,F4SE]
SetOutOfRangeMultiplier(Owner akOwner, float mult)  [G,F4SE]
float GetReloadSpeed(Owner akOwner)  [G,F4SE]
SetReloadSpeed(Owner akOwner, float speed)  [G,F4SE]
float GetReach(Owner akOwner)  [G,F4SE]
SetReach(Owner akOwner, float reach)  [G,F4SE]
float GetMinRange(Owner akOwner)  [G,F4SE]
SetMinRange(Owner akOwner, float minRange)  [G,F4SE]
float GetMaxRange(Owner akOwner)  [G,F4SE]
SetMaxRange(Owner akOwner, float maxRange)  [G,F4SE]
float GetSpeed(Owner akOwner)  [G,F4SE]
SetSpeed(Owner akOwner, float speed)  [G,F4SE]
int GetStagger(Owner akOwner)  [G,F4SE]
SetStagger(Owner akOwner, int stagger)  [G,F4SE]
ActorValue GetSkill(Owner akOwner)  [G,F4SE]
SetSkill(Owner akOwner, ActorValue skill)  [G,F4SE]
ActorValue GetResist(Owner akOwner)  [G,F4SE]
SetResist(Owner akOwner, ActorValue resist)  [G,F4SE]
float GetCritMultiplier(Owner akOwner)  [G,F4SE]
SetCritMultiplier(Owner akOwner, float crit)  [G,F4SE]
float GetCritChargeBonus(Owner akOwner)  [G,F4SE]
SetCritChargeBonus(Owner akOwner, float bonus)  [G,F4SE]
Projectile GetProjectileOverride(Owner akOwner)  [G,F4SE]
SetProjectileOverride(Owner akOwner, Projectile proj)  [G,F4SE]
int GetNumProjectiles(Owner akOwner)  [G,F4SE]
SetNumProjectiles(Owner akOwner, int numProj)  [G,F4SE]
float GetSightedTransition(Owner akOwner)  [G,F4SE]
SetSightedTransition(Owner akOwner, float seconds)  [G,F4SE]
bool GetFlag(Owner akOwner, int flag)  [G,F4SE]
SetFlag(Owner akOwner, int flag, bool set)  [G,F4SE]
int GetArmorHealth(Owner akOwner)  [G,F4SE]
SetArmorHealth(Owner akOwner, int health)  [G,F4SE]
int GetArmorRating(Owner akOwner)  [G,F4SE]
SetArmorRating(Owner akOwner, int health)  [G,F4SE]
float GetWeight(Owner akOwner)  [G,F4SE]
SetWeight(Owner akOwner, float weight)  [G,F4SE]
int GetGoldValue(Owner akOwner)  [G,F4SE]
SetGoldValue(Owner akOwner, int value)  [G,F4SE]
Keyword[] GetKeywords(Owner akOwner)  [G,F4SE]
SetKeywords(Owner akOwner, Keyword[] kwds)  [G,F4SE]

F4SE Struct InstanceData:Owner { Form owner; int slotIndex }
F4SE Struct InstanceData:DamageTypeInfo { Form type; int damage }
F4SE consts: Flag_IgnoresNormalResist=0x0000002, Flag_MinorCrime=0x0000004, Flag_ChargingReload=0x0000008, Flag_HideBackpack=0x0000010, Flag_NonHostile=0x0000040, Flag_NPCsUseAmmo=0x0000200, Flag_RepeatableSingleFire=0x0000800, Flag_HasScope=0x0001000, Flag_HoldInputToPower=0x0002000, Flag_Automatic=0x0004000, Flag_CantDrop=0x0008000, Flag_ChargingAttack=0x0010000, Flag_NotUsedInNormalCombat=0x0020000, Flag_BoundWeapon=0x0040000, Flag_SecondaryWeapon=0x0200000, Flag_BoltAction=0x0400000, Flag_NoJamAfterReload=0x0800000, Flag_DisableShells=0x1000000
```

#### InstanceNamingRules
`extends Form` · `Native Hidden` · 1 natives [src: f4se/scripts/vanilla/InstanceNamingRules.psc]

```papyrus
MergeWith(InstanceNamingRules aSource)
```

#### Component
`extends Form` · `Native Hidden` · 0 natives + 4 F4SE [src: f4se/scripts/vanilla/Component.psc] [src: f4se/scripts/modified/Component.psc]

```papyrus
MiscObject GetScrapItem()  [F4SE]
SetScrapItem(MiscObject akMisc)  [F4SE]
GlobalVariable GetScrapScalar()  [F4SE]
SetScrapScalar(GlobalVariable akGlobal)  [F4SE]
```

#### MiscObject
`extends Form` · `Native Hidden` · 1 natives + 2 F4SE [src: f4se/scripts/vanilla/MiscObject.psc] [src: f4se/scripts/modified/MiscObject.psc]

```papyrus
int GetObjectComponentCount(Component akComponent)
MiscComponent[] GetMiscComponents()  [F4SE]
SetMiscComponents(MiscComponent[] components)  [F4SE]

F4SE Struct MiscObject:MiscComponent { Component object; int count }
```

#### ConstructibleObject
`extends MiscObject` · `Native Hidden` · 0 natives + 10 F4SE [src: f4se/scripts/vanilla/ConstructibleObject.psc] [src: f4se/scripts/modified/ConstructibleObject.psc]

```papyrus
ConstructibleComponent[] GetConstructibleComponents()  [F4SE]
SetConstructibleComponents(ConstructibleComponent[] components)  [F4SE]
Form GetCreatedObject()  [F4SE]
SetCreatedObject(Form akForm)  [F4SE]
int GetCreatedCount()  [F4SE]
SetCreatedCount(int count)  [F4SE]
int GetPriority()  [F4SE]
SetPriority(int priority)  [F4SE]
Keyword GetWorkbenchKeyword()  [F4SE]
SetWorkbenchKeyword(Keyword akKeyword)  [F4SE]

F4SE Struct ConstructibleObject:ConstructibleComponent { Form object; int count }
```

#### Potion
`extends Form` · `Native Hidden` · 1 natives [src: f4se/scripts/vanilla/Potion.psc]

```papyrus
bool IsHostile()
```

#### Ingredient
`extends Form` · `Native Hidden` · 4 natives [src: f4se/scripts/vanilla/Ingredient.psc]

```papyrus
bool IsHostile()
LearnEffect(int aiIndex)
int LearnNextEffect()
LearnAllEffects()
```

#### Book
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Book.psc]

(no functions, events or structs)

#### Holotape
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Holotape.psc]

(no functions, events or structs)

#### Key
`extends MiscObject` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Key.psc]

(no functions, events or structs)

#### SoulGem
`extends MiscObject` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/SoulGem.psc]

(no functions, events or structs)

#### MatSwap
`extends Form` · `native Hidden` · 0 natives + 2 F4SE · **F4SE-only class** [src: f4se/scripts/modified/MatSwap.psc]

```papyrus
RemapData[] GetRemapData()  [F4SE]
SetRemapData(RemapData[] data)  [F4SE]

F4SE Struct MatSwap:RemapData { string source; string target; float colorIndex }
```

#### EquipSlot
`extends Form` · `native Hidden` · 0 natives + 1 F4SE · **F4SE-only class** [src: f4se/scripts/modified/EquipSlot.psc]

```papyrus
EquipSlot[] GetParents()  [F4SE]
```

#### Keyword
`extends Form` · `Native Hidden` · 2 natives [src: f4se/scripts/vanilla/Keyword.psc]

```papyrus
SendStoryEvent(Location akLoc = None, ObjectReference akRef1 = None, ObjectReference akRef2 = None, int aiValue1 = 0, int aiValue2 = 0)
bool SendStoryEventAndWait(Location akLoc = None, ObjectReference akRef1 = None, ObjectReference akRef2 = None, int aiValue1 = 0, int aiValue2 = 0)  [L]
```

#### LocationRefType
`extends Keyword` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/LocationRefType.psc]

(no functions, events or structs)

#### FormList
`extends Form` · `Native Hidden` · 7 natives [src: f4se/scripts/vanilla/FormList.psc]

```papyrus
AddForm(Form apForm)
int Find(Form apForm)
int GetSize()
Form GetAt(int aiIndex)
bool HasForm(Form akForm)
RemoveAddedForm(Form apForm)
Revert()
```

#### LeveledItem
`extends Form` · `Native Hidden` · 2 natives [src: f4se/scripts/vanilla/LeveledItem.psc]

```papyrus
AddForm(Form apForm, int aiLevel, int aiCount)
Revert()
```

#### LeveledActor
`extends Form` · `Native Hidden` · 2 natives [src: f4se/scripts/vanilla/LeveledActor.psc]

```papyrus
AddForm(Form apForm, int aiLevel)
Revert()
```

#### LeveledSpell
`extends Form` · `Native Hidden` · 2 natives [src: f4se/scripts/vanilla/LeveledSpell.psc]

```papyrus
AddForm(Form apForm, int aiLevel)
Revert()
```

#### Outfit
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Outfit.psc]

(no functions, events or structs)

### 1.4 Characters and actor data

#### ActorValue
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/ActorValue.psc]

> AVIF record type. No natives; obtain via properties, Game.Get*AV(), GetFormFromFile, or F4SE-free platform helpers.

(no functions, events or structs)

#### Race
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Race.psc]

(no functions, events or structs)

#### Perk
`extends Form` · `Native Hidden` · 0 natives + 7 F4SE [src: f4se/scripts/vanilla/Perk.psc] [src: f4se/scripts/modified/Perk.psc]

```papyrus
bool IsPlayable()  [F4SE]
bool IsHidden()  [F4SE]
int GetLevel()  [F4SE]
int GetNumRanks()  [F4SE]
Perk GetNextPerk()  [F4SE]
string GetSWFPath()  [F4SE]
bool IsEligible(Actor akActor)  [F4SE]

Event OnEntryRun(int auiEntryID, ObjectReference akTarget, Actor akOwner)
```

#### HeadPart
`extends Form` · `Native Hidden` · 0 natives + 6 F4SE [src: f4se/scripts/vanilla/HeadPart.psc] [src: f4se/scripts/modified/HeadPart.psc]

```papyrus
int GetType()  [F4SE]
HeadPart[] GetExtraParts()  [F4SE]
bool HasExtraPart(HeadPart p)  [F4SE]
bool IsExtraPart()  [F4SE]
FormList GetValidRaces()  [F4SE]
SetValidRaces(FormList vRaces)  [F4SE]

F4SE consts: Type_Misc=0, Type_Face=1, Type_Eyes=2, Type_Hair=3, Type_FacialHair=4, Type_Scar=5, Type_Brows=6, Type_HeadRear=9
```

#### Class
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Class.psc]

(no functions, events or structs)

#### Faction
`extends Form` · `Native Hidden` · 23 natives [src: f4se/scripts/vanilla/Faction.psc]

```papyrus
bool CanPayCrimeGold()
int GetCrimeGold()
int GetCrimeGoldNonViolent()
int GetCrimeGoldViolent()
int GetInfamy()
int GetInfamyNonViolent()
int GetInfamyViolent()
int GetFactionReaction(Actor akOther)
int GetStolenItemValueCrime()
int GetStolenItemValueNoCrime()
bool IsFactionInCrimeGroup(Faction akOther)
bool IsPlayerEnemy()
bool IsPlayerExpelled()
ModCrimeGold(int aiAmount, bool abViolent = false)
PlayerPayCrimeGold(bool abRemoveStolenItems = true, bool abGoToJail = true)
SendAssaultAlarm()
SendPlayerToJail(bool abRemoveInventory = true, bool abRealJail = true)  [L]
SetAlly(Faction akOther, bool abSelfIsFriendToOther = false, bool abOtherIsFriendToSelf = false)
SetCrimeGold(int aiGold)
SetCrimeGoldViolent(int aiGold)
SetEnemy(Faction akOther, bool abSelfIsNeutralToOther = false, bool abOtherIsNeutralToSelf = false)
SetPlayerEnemy(bool abIsEnemy = true)
SetPlayerExpelled(bool abIsExpelled = true)
```

#### AssociationType
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/AssociationType.psc]

(no functions, events or structs)

#### VoiceType
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/VoiceType.psc]

(no functions, events or structs)

#### CombatStyle
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/CombatStyle.psc]

(no functions, events or structs)

#### Idle
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Idle.psc]

(no functions, events or structs)

#### Action
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Action.psc]

(no functions, events or structs)

#### Package
`extends Form` · `Native Hidden` · 2 natives [src: f4se/scripts/vanilla/Package.psc]

```papyrus
Quest GetOwningQuest()
Package GetTemplate()

Event OnStart(Actor akActor)
Event OnEnd(Actor akActor)
Event OnChange(Actor akActor)
```

### 1.5 World objects and places

#### Cell
`extends Form` · `Native Hidden` · 13 natives + 1 F4SE [src: f4se/scripts/vanilla/Cell.psc] [src: f4se/scripts/modified/Cell.psc]

```papyrus
EnableFastTravel(bool abEnable = true)
ActorBase GetActorOwner()
Faction GetFactionOwner()
bool IsAttached()
bool IsInterior()
bool IsLoaded()
Reset()
SetActorOwner(ActorBase akActor)
SetFactionOwner(Faction akFaction)
SetFogColor(int aiNearRed, int aiNearGreen, int aiNearBlue, int aiFarRed, int aiFarGreen, int aiFarBlue)
SetFogPlanes(float afNear, float afFar)
SetFogPower(float afPower)
SetPublic(bool abPublic = true)
WaterType GetWaterType()  [F4SE]
```

#### WorldSpace
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/WorldSpace.psc]

(no functions, events or structs)

#### Location
`extends Form` · `Native Hidden` · 16 natives + 2 script-defined + 4 F4SE [src: f4se/scripts/vanilla/Location.psc] [src: f4se/scripts/modified/Location.psc]

```papyrus
AddLinkedLocation(Location akLoc, Keyword akKeyword)
Location[] GetAllLinkedLocations(Keyword akKeyword)
float GetKeywordData(Keyword akKeyword)
int GetRefTypeAliveCount(LocationRefType akRefType)
int GetRefTypeDeadCount(LocationRefType akRefType)
bool HasCommonParent(Location akOther, Keyword akFilter = None)
bool HasEverBeenCleared()
bool HasRefType(LocationRefType akRefType)
bool IsCleared()
bool IsChild(Location akOther)
bool IsLinkedLocation(Location akLocation, Keyword akKeyword)
bool IsLoaded()
bool IsSameLocation(Location akOtherLocation, Keyword akKeyword = None)  [S]
ModifyKeywordData(Keyword akKeyword, float afData)  [S]
RemoveLinkedLocation(Location akLoc, Keyword akKeyword)
Reset()
SetKeywordData(Keyword akKeyword, float afData)
SetCleared(bool abCleared = true)
Location GetParent()  [F4SE]
SetParent(Location akLocation)  [F4SE]
EncounterZone GetEncounterZone(bool recursive = false)  [F4SE]
SetEncounterZone(EncounterZone ez)  [F4SE]

Event OnLocationCleared()
Event OnLocationLoaded()
```

#### EncounterZone
`extends Form` · `Native Hidden` · 3 natives + 12 F4SE [src: f4se/scripts/vanilla/EncounterZone.psc] [src: f4se/scripts/modified/EncounterZone.psc]

```papyrus
int CountActors(Keyword apRequiredLinkedRefKeyword = None, Keyword apExcludeLinkedRefKeyword = None)
Actor[] GetActors(Keyword apRequiredLinkedRefKeyword = None, Keyword apExcludeLinkedRefKeyword = None)
Reset()
Location GetLocation()  [F4SE]
SetLocation(Location akLoc)  [F4SE]
int GetRank()  [F4SE]
SetRank(int rank)  [F4SE]
int GetMinLevel()  [F4SE]
SetMinLevel(int level)  [F4SE]
int GetMaxLevel()  [F4SE]
SetMaxLevel(int level)  [F4SE]
bool IsNeverResetable()  [F4SE]
SetNeverResetable(bool resetable)  [F4SE]
bool IsWorkshop()  [F4SE]
SetWorkshop(bool ws)  [F4SE]
```

#### Weather
`extends Form` · `Native Hidden` · 10 natives [src: f4se/scripts/vanilla/Weather.psc]

```papyrus
EnableAmbientParticles(bool abEnable = true)  [G]
Weather FindWeather(int auiType)  [G]
ForceActive(bool abOverride=false)
int GetClassification()
Weather GetCurrentWeather()  [G]
float GetCurrentWeatherTransition()  [G]
Weather GetOutgoingWeather()  [G]
int GetSkyMode()  [G]
ReleaseOverride()  [G]
SetActive(bool abOverride=false, bool abAccelerate=false)
```

#### Activator
`extends Form` · `Native Hidden` · 1 natives [src: f4se/scripts/vanilla/Activator.psc]

```papyrus
bool IsRadio()
```

#### Furniture
`extends Activator` · `Native Hidden` · 1 natives [src: f4se/scripts/vanilla/Furniture.psc]

```papyrus
Form GetAssociatedForm()
```

#### Flora
`extends Activator` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Flora.psc]

(no functions, events or structs)

#### TalkingActivator
`extends Activator` · `Native Hidden` · 0 natives · not in the local vanilla set [web: https://falloutck.uesp.net/wiki/TalkingActivator_Script]

#### Container
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Container.psc]

(no functions, events or structs)

#### Door
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Door.psc]

(no functions, events or structs)

#### Terminal
`extends Form` · `Native Hidden` · 1 natives [src: f4se/scripts/vanilla/Terminal.psc]

```papyrus
ShowOnPipboy()

Event OnMenuItemRun(int auiMenuItemID, ObjectReference akTerminalRef)
```

#### Light
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Light.psc]

(no functions, events or structs)

#### Static
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Static.psc]

(no functions, events or structs)

#### MovableStatic
`extends Static` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/MovableStatic.psc]

(no functions, events or structs)

#### IdleMarker
`extends Form` · `Native Hidden` · 0 natives · not in the local vanilla set [web: https://falloutck.uesp.net/wiki/IdleMarker_Script]

#### Explosion
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Explosion.psc]

(no functions, events or structs)

#### Hazard
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Hazard.psc]

(no functions, events or structs)

#### Projectile
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Projectile.psc]

(no functions, events or structs)

#### WaterType
`extends Form` · `native Hidden` · 0 natives + 4 F4SE · **F4SE-only class** [src: f4se/scripts/modified/WaterType.psc]

```papyrus
Spell GetConsumeSpell()  [F4SE]
SetConsumeSpell(Spell sp)  [F4SE]
Spell GetContactSpell()  [F4SE]
SetContactSpell(Spell sp)  [F4SE]
```

#### TextureSet
`extends Form` · `Hidden Native` · 0 natives [src: f4se/scripts/vanilla/TextureSet.psc]

(no functions, events or structs)

### 1.6 Quests, aliases, dialogue, messages

#### Quest
`extends Form` · `Native Hidden` · 26 natives + 8 script-defined [src: f4se/scripts/vanilla/Quest.psc]

> GetStage/SetStage/GetStageDone are script-defined wrappers over GetCurrentStageID/SetCurrentStageID/IsStageDone.

```papyrus
bool ModObjectiveGlobal(float afModValue, GlobalVariable aModGlobal, int aiObjectiveID = -1, float afTargetValue = -1.0, bool abCountingUp = true, bool abCompleteObjective = true, bool abRedisplayObjective = true, bool abAllowRollbackObjective = false)  [S]
SetAllStages(int lastStage)  [S]
SetQuestStage(QuestStage questStageToSet)  [G,S]
bool GetQuestStageDone(QuestStage questStageToCheck)  [G,S]
SetObjectiveSkipped(int aiObjectiveID)  [S]
CompleteAllObjectives()
CompleteQuest()
FailAllObjectives()
Alias GetAlias(int aiAliasID)
int GetCurrentStageID()
int GetStage()  [S]
bool GetStageDone(int aiStage)  [S]
bool HasObjective(int aiObjective)
bool IsActive()
bool IsCompleted()
bool IsObjectiveCompleted(int aiObjective)
bool IsObjectiveDisplayed(int aiObjective)
bool IsObjectiveFailed(int aiObjective)
bool IsRunning()
bool IsStageDone(int aiStage)
bool IsStarting()
bool IsStopping()
bool IsStopped()
Reset()
ResetSpeechChallenges()
SetActive(bool abActive = true)
bool SetCurrentStageID(int aiStageID)  [L]
SetObjectiveCompleted(int aiObjective, bool abCompleted = true)
SetObjectiveDisplayed(int aiObjective, bool abDisplayed = true, bool abForce = false)
SetObjectiveFailed(int aiObjective, bool abFailed = true)
bool SetStage(int aiStage)  [S,L*]
bool Start()  [L]
Stop()
bool UpdateCurrentInstanceGlobal(GlobalVariable aUpdateGlobal)

Event OnQuestInit()
Event OnQuestShutdown()
Event OnReset()
Event OnStageSet(int auiStageID, int auiItemID)
Event OnStoryActivateActor(Location akLocation, ObjectReference akActor)
Event OnStoryActorAttach(ObjectReference akActor, Location akLocation)
Event OnStoryAddToPlayer(ObjectReference akOwner, ObjectReference akContainer, Location akLocation, Form akItemBase, int aiAcquireType)
Event OnStoryArrest(ObjectReference akArrestingGuard, ObjectReference akCriminal, Location akLocation, int aiCrime)
Event OnStoryAssaultActor(ObjectReference akVictim, ObjectReference akAttacker, Location akLocation, int aiCrime)
Event OnStoryAttractionObject(ObjectReference akActor, ObjectReference akObject, Location akLocation, bool abCommanded)
Event OnStoryBribeNPC(ObjectReference akActor)
Event OnStoryCastMagic(ObjectReference akCastingActor, ObjectReference akSpellTarget, Location akLocation, Form akSpell)
Event OnStoryChangeLocation(ObjectReference akActor, Location akOldLocation, Location akNewLocation)
Event OnStoryClearLocation(Location akOldLocation)
Event OnStoryCraftItem(ObjectReference akBench, Location akLocation, Form akCreatedItem)
Event OnStoryCrimeGold(ObjectReference akVictim, ObjectReference akCriminal, Form akFaction, int aiGoldAmount, int aiCrime)
Event OnStoryCure(Form akInfection)
Event OnStoryDialogue(Location akLocation, ObjectReference akActor1, ObjectReference akActor2)
Event OnStoryDiscoverDeadBody(ObjectReference akActor, ObjectReference akDeadActor, Location akLocation)
Event OnStoryEscapeJail(Location akLocation, Form akCrimeGroup)
Event OnStoryFlatterNPC(ObjectReference akActor)
Event OnStoryHackTerminal(ObjectReference akComputer, bool abSucceeded)
Event OnStoryHello(Location akLocation, ObjectReference akActor1, ObjectReference akActor2)
Event OnStoryIncreaseLevel(int aiNewLevel)
Event OnStoryInfection(ObjectReference akTransmittingActor, Form akInfection)
Event OnStoryIntimidateNPC(ObjectReference akActor)
Event OnStoryIronSights(ObjectReference akActor, Form akWeapon)
Event OnStoryJail(ObjectReference akGuard, Form akCrimeGroup, Location akLocation, int aiCrimeGold)
Event OnStoryKillActor(ObjectReference akVictim, ObjectReference akKiller, Location akLocation, int aiCrimeStatus, int aiRelationshipRank)
Event OnStoryLocationLoaded(Location akLocation)
Event OnStoryMineExplosion(ObjectReference akVictim, ObjectReference akAttacker)
Event OnStoryNewVoicePower(ObjectReference akActor, Form akVoicePower)
Event OnStoryPickLock(ObjectReference akActor, ObjectReference akLock)
Event OnStoryPickPocket(ObjectReference akVictim, bool abSuccess)
Event OnStoryPayFine(ObjectReference akCriminal, ObjectReference akGuard, Form akCrimeGroup, int aiCrimeGold)
Event OnStoryPlayerGetsFavor(ObjectReference akActor)
Event OnStoryRelationshipChange(ObjectReference akActor1, ObjectReference akActor2, int aiOldRelationship, int aiNewRelationship)
Event OnStoryRemoveFromPlayer(ObjectReference akOwner, ObjectReference akItem, Location akLocation, Form akItemBase, int aiRemoveType)
Event OnStoryScript(Keyword akKeyword, Location akLocation, ObjectReference akRef1, ObjectReference akRef2, int aiValue1, int aiValue2)
Event OnStoryServedTime(Location akLocation, Form akCrimeGroup, int aiCrimeGold, int aiDaysJail)
Event OnStoryTrespass(ObjectReference akVictim, ObjectReference akTrespasser, Location akLocation, int aiCrime)

Struct Quest:QuestStage { Quest QuestToSet; int StageToSet }
```

#### Alias
`extends ScriptObject` · `Native Hidden` · 3 natives [src: f4se/scripts/vanilla/Alias.psc]

```papyrus
Quest GetOwningQuest()
StartObjectProfiling()  [dbg]
StopObjectProfiling()  [dbg]

Event OnAliasInit()
Event OnAliasReset()
Event OnAliasShutdown()
```

#### ReferenceAlias
`extends Alias` · `Native Hidden` · 5 natives + 20 script-defined [src: f4se/scripts/vanilla/ReferenceAlias.psc]

> Also receives every ObjectReference and Actor event of the filled reference (identical signatures).

```papyrus
ApplyToRef(ObjectReference akRef)
Clear()
ObjectReference GetReference()
ForceRefTo(ObjectReference akNewRef)
bool ForceRefIfEmpty(ObjectReference akNewRef)  [S]
Actor GetActorReference()  [S]
ObjectReference GetRef()  [S]
Actor GetActorRef()  [S]
RemoveFromRef(ObjectReference akRef)
bool TryToAddToFaction(Faction FactionToAddTo)  [S]
bool TryToRemoveFromFaction(Faction FactionToRemoveFrom)  [S]
bool TryToStopCombat()  [S]
bool TryToDisable()  [S,L*]
bool TryToDisableNoWait()  [S]
bool TryToEnable()  [S,L*]
bool TryToEnableNoWait()  [S]
bool TryToEvaluatePackage()  [S]
bool TryToKill()  [S]
bool TryToMoveTo(ObjectReference RefToMoveTo)  [S,L*]
bool TryToReset()  [S]
bool TryToClear()  [S]
float TryToGetActorValue(ActorValue ActorValueToGet)  [S]
float TryToGetValue(ActorValue akAV)  [S]
bool TryToSetActorValue(ActorValue ValueToSet, float afValue)  [S]
bool TryToSetValue(ActorValue akAV, float afValue)  [S]
```

#### RefCollectionAlias
`extends Alias` · `Native Hidden` · 6 natives + 20 script-defined [src: f4se/scripts/vanilla/RefCollectionAlias.psc]

> Also receives every ObjectReference and Actor event of each member, with `ObjectReference akSenderRef` prepended to the parameter list.

```papyrus
AddToFaction(faction akFaction)  [S]
BlockActivation(bool abBlocked = True, bool abHideActivateText = false)  [S]
Actor GetActorAt(int aiIndex)  [S]
ObjectReference GetFirstOwnedObject(Actor actorOwner)  [S]
EnableAll(bool bFadeIn = false)  [S]
DisableAll(bool bFadeOut = false)  [S]
EvaluateAll()  [S]
MoveAllTo(ObjectReference akTarget)  [S]
bool IsOwnedObjectInList(Actor actorOwner)  [S]
KillAll(actor akKiller = NONE)  [S]
StartCombatAll(actor akCombatTarget)  [S]
RemoveFromFaction(faction akFaction)  [S]
RemoveFromAllFactions()  [S]
ResetAll()  [S]
SetProtected(bool bSetProtected = true)  [S]
SetEssential(bool bSetEssential = true)  [S]
AddRefCollection(RefCollectionAlias refCollectionAliasToAdd)  [S]
AddArray(ObjectReference[] refArrayToAdd)  [S]
SetValue(ActorValue akActorValue, float fValue)  [S]
bool LinkCollectionTo(RefCollectionAlias LinkedRefCollectionAlias, keyword LinkKeyword = none, bool WrapLinks = false)  [S]
AddRef(ObjectReference akNewRef)
int Find(ObjectReference akFindRef)
ObjectReference GetAt(int aiIndex)
int GetCount()
RemoveAll()
RemoveRef(ObjectReference akRemoveRef)
```

#### LocationAlias
`extends Alias` · `Native Hidden` · 3 natives [src: f4se/scripts/vanilla/LocationAlias.psc]

```papyrus
Clear()
Location GetLocation()
ForceLocationTo(Location akNewLocation)
```

#### Scene
`extends Form` · `Native Hidden` · 7 natives [src: f4se/scripts/vanilla/Scene.psc]

```papyrus
ForceStart()
Start()
Stop()
bool IsPlaying()
Quest GetOwningQuest()
bool IsActionComplete(int aiActionID)
Pause(bool abPause)

Event OnAction(int auiActionID, ReferenceAlias akAlias)
Event OnBegin()
Event OnEnd()
Event OnPhaseBegin(int auiPhaseIndex)
Event OnPhaseEnd(int auiPhaseIndex)
```

#### Topic
`extends Form` · `Native Hidden` · 1 natives [src: f4se/scripts/vanilla/Topic.psc]

```papyrus
Add()
```

#### TopicInfo
`extends Form` · `Native Hidden` · 2 natives [src: f4se/scripts/vanilla/TopicInfo.psc]

```papyrus
Quest GetOwningQuest()
bool HasBeenSaid()

Event OnBegin(ObjectReference akSpeakerRef, bool abHasBeenSaid)
Event OnEnd(ObjectReference akSpeakerRef, bool abHasBeenSaid)
```

#### Message
`extends Form` · `Native Hidden` · 5 natives [src: f4se/scripts/vanilla/Message.psc]

```papyrus
int Show(float afArg1 = 0.0, float afArg2 = 0.0, float afArg3 = 0.0, float afArg4 = 0.0, float afArg5 = 0.0, float afArg6 = 0.0, float afArg7 = 0.0, float afArg8 = 0.0, float afArg9 = 0.0)  [L]
ShowAsHelpMessage(string asEvent, float afDuration, float afInterval, int aiMaxTimes, string asContext="", int aiPriority=0)
UnshowAsHelpMessage()
ClearHelpMessages()  [G]
ResetHelpMessage(string asEvent)  [G]
```

#### CameraShot
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/CameraShot.psc]

(no functions, events or structs)

### 1.7 Magic and effects

#### Spell
`extends Form` · `Native Hidden` · 3 natives [src: f4se/scripts/vanilla/Spell.psc]

```papyrus
Cast(ObjectReference akSource, ObjectReference akTarget=NONE)  [L]
RemoteCast(ObjectReference akSource, Actor akBlameActor, ObjectReference akTarget=NONE)  [L]
bool IsHostile()
```

#### MagicEffect
`extends Form` · `Native Hidden` · 1 natives [src: f4se/scripts/vanilla/MagicEffect.psc]

```papyrus
string GetAssociatedSkill()  [L]
```

#### ActiveMagicEffect
`extends ScriptObject` · `Native Hidden` · 6 natives [src: f4se/scripts/vanilla/ActiveMagicEffect.psc]

> Also receives every ObjectReference and Actor event of its target (identical signatures).

```papyrus
Dispel()
MagicEffect GetBaseObject()
Actor GetCasterActor()
Actor GetTargetActor()
StartObjectProfiling()  [dbg]
StopObjectProfiling()  [dbg]

Event OnEffectStart(Actor akTarget, Actor akCaster)
Event OnEffectFinish(Actor akTarget, Actor akCaster)
```

#### Enchantment
`extends Form` · `Native Hidden` · 1 natives [src: f4se/scripts/vanilla/Enchantment.psc]

```papyrus
bool IsHostile()
```

#### Scroll
`extends Form` · `Native Hidden` · 1 natives [src: f4se/scripts/vanilla/Scroll.psc]

```papyrus
Cast(ObjectReference akSource, ObjectReference akTarget=NONE)  [L]
```

#### Shout
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/Shout.psc]

(no functions, events or structs)

#### WordOfPower
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/WordOfPower.psc]

(no functions, events or structs)

#### EffectShader
`extends Form` · `Native Hidden` · 2 natives [src: f4se/scripts/vanilla/EffectShader.psc]

```papyrus
Play(ObjectReference akObject, float afDuration = -1.0)
Stop(ObjectReference akObject)
```

#### VisualEffect
`extends Form` · `Native Hidden` · 2 natives [src: f4se/scripts/vanilla/VisualEffect.psc]

```papyrus
Play(ObjectReference akObject, float afTime = -1.0, ObjectReference akFacingObject = None)
Stop(ObjectReference akObject)
```

#### ImageSpaceModifier
`extends Form` · `Native Hidden` · 5 natives [src: f4se/scripts/vanilla/ImageSpaceModifier.psc]

```papyrus
Apply(float afStrength = 1.0)
ApplyCrossFade(float afFadeDuration = 1.0)
PopTo(ImageSpaceModifier akNewModifier, float afStrength = 1.0)
Remove()
RemoveCrossFade(float afFadeDuration = 1.0)  [G]
```

#### ImpactDataSet
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/ImpactDataSet.psc]

(no functions, events or structs)

#### ShaderParticleGeometry
`extends Form` · `Native Hidden` · 2 natives · not in the local vanilla set [web: https://falloutck.uesp.net/wiki/ShaderParticleGeometry_Script]

```papyrus
Apply(float afFadeInTime)
Remove(float afFadeOutTime)
```

### 1.8 Audio

#### Sound
`extends Form` · `Native Hidden` · 4 natives [src: f4se/scripts/vanilla/Sound.psc]

```papyrus
int Play(ObjectReference akSource)
bool PlayAndWait(ObjectReference akSource)  [L]
StopInstance(int aiPlaybackInstance)  [G]
SetInstanceVolume(int aiPlaybackInstance, float afVolume)  [G]
```

#### SoundCategory
`extends Form` · `Native Hidden` · 6 natives · not in the local vanilla set [web: https://falloutck.uesp.net/wiki/SoundCategory_Script]

```papyrus
Mute()
Pause()
SetFrequency(float afFrequencyCoeffecient)
SetVolume(float afVolume)
UnMute()
UnPause()
```

#### SoundCategorySnapshot
`extends Form` · `Native Hidden` · 2 natives · not in the local vanilla set [web: https://falloutck.uesp.net/wiki/SoundCategorySnapshot_Script]

```papyrus
Push(float afTransitionSecs)
Remove()
```

#### MusicType
`extends Form` · `Native Hidden` · 2 natives [src: f4se/scripts/vanilla/MusicType.psc]

```papyrus
Add()
Remove()
```

#### OutputModel
`extends Form` · `Native Hidden` · 0 natives [src: f4se/scripts/vanilla/OutputModel.psc]

(no functions, events or structs)

### 1.9 Non-native vanilla gameplay scripts in the local set

These are not engine APIs. They show how FO4's own content uses the API (remote events, custom events, timers, structs), and they matter if the server ever runs workshop or follower content. Do not paste their bodies into this repo (Bethesda copyright). Signatures and names are fine.

| Script | extends / flags | What it is | Custom events it defines | Remote / custom events it handles |
|---|---|---|---|---|
| `WorkshopParentScript` | Quest · Hidden Conditional | The settlement controller (102 functions). | `WorkshopInitializeLocation`, `WorkshopDailyUpdate`, `WorkshopAddActor`, `WorkshopActorAssignedToWork`, `WorkshopActorUnassigned`, `WorkshopObjectBuilt`, `WorkshopObjectDestroyed`, `WorkshopObjectMoved`, `WorkshopObjectDestructionStageChanged`, `WorkshopObjectPowerStageChanged`, `WorkshopPlayerOwnershipChanged`, `WorkshopEnterMenu`, `WorkshopObjectRepaired`, `WorkshopActorCaravanAssign`, `WorkshopActorCaravanUnassign` | `Location.OnLocationCleared`, `Actor.OnLocationChange`, `OnStageSet`, `OnTimerGameTime` |
| `WorkshopScript` | ObjectReference · Conditional | Script on each workbench (workshop) ref. | — | `OnWorkshopMode`, `OnWorkshopObjectPlaced/Moved/Destroyed/Repaired`, `WorkshopParentScript.WorkshopDailyUpdate`, timers |
| `WorkshopObjectScript` | ObjectReference | Script on placed workshop objects. | — | `OnPowerOn/Off`, `OnWorkshopObjectGrabbed/Moved`, `OnDestructionStageChanged`, `OnActivate` |
| `WorkshopNPCScript` | Actor · Conditional | Settler behaviour. | — | `OnWorkshopNPCTransfer`, `FollowersScript.CompanionChange`, `OnCommandModeGiveCommand`, `OnEnterBleedout`, `OnDeath` |
| `workshopObjectActorScript` | Actor · Const | Workshop-built actors such as turrets. | — | `OnWorkshopObjectPlaced`, `OnDeath`, `OnEnterBleedout` |
| `WorkshopDataScript` | (none) · Hidden Const | Struct library only. | — | — (structs `WorkshopRatingKeyword`, `WorkshopActorValue`, …) |
| `FollowersScript` | Quest · Conditional | Companion framework (89 functions). | `CompanionChange`, `Loitering`, `AffinityEvent`, `PossibleMurderEvent`, `AutonomyDisallowed` | About 15 remote `Actor.*` / `ReferenceAlias.*` events, `OnHit`, `OnMagicEffectApply`, `OnRadiationDamage`, `OnPlayerSleepStart/Stop`, `OnPlayerTeleport` |
| `CompanionActorScript` | Actor · conditional | Per-companion affinity logic. | — | `FollowersScript.CompanionChange/AffinityEvent/PossibleMurderEvent`, timers |
| `AOScript` | Quest | Ambient Dogmeat "find" logic. | — | `FollowersScript.Loitering`, `ReferenceAlias.OnLoad/OnUnload` |
| `DogmeatActorScript`, `DogmeatIdles` | Actor / Quest Const | Dogmeat. | — | `OnItemAdded`, `OnCombatStateChanged`, timers |
| `TeleportActorScript` | Actor | Teleport effect helper. | `TeleportDone` | — |
| `Trigger`, `SAEClutterTriggerScript`, `SASTriggerScript` | ObjectReference | Simple trigger volumes. | — | `OnTriggerEnter/Leave` |
| `CommonArrayFunctions` | (none) | Global array helpers (12 global functions). | — | — |
| `CommonPropertiesScript` | Quest · const | Bag of 57 shared properties, returned by `Game.GetCommonProperties()` (script-defined: `GetFormFromFile(0x000A7D73, "Fallout4.esm")`). | — | — |

**Native calls observed in these scripts** (call-site counts, top 40) [src: f4se/scripts/vanilla/{Workshop*,Followers*,Companion*,AO*,Dogmeat*,Teleport*,Trigger,SA*}.psc]. This list is the empirical basis for the server priorities in §3.2:
- **50+ call sites:** `Debug.Trace` 248, `GetValue` 171 (AV and GlobalVariable), `SetValue` 92, `ReferenceAlias.GetActorReference` 75, `Game.GetCommonProperties` 62.
- **20–49:** `GetBaseValue` 40, `Debug.TraceConditional` 34, `Game.GetPlayer` 30, `HasKeyword` 28, `FormList.Find`/`GetAt` 27, `Actor.PlayIdle` 25, `SendCustomEvent` 24, `RegisterForRemoteEvent` 22, `GetItemCount` 20.
- **10–19:** `RegisterForCustomEvent` 19, `Math.Ceiling` 19, `SetLinkedRef` 18, `Message.Show` 17, `ForceRefTo` 17, `GetCurrentLocation` 16, `StartTimer` 15, `Utility.GetCurrentGameTime` 15, `EvaluatePackage` 15, `Math.Min` 15, `UnregisterForCustomEvent` 14, `StartTimerGameTime` 13, `GetReference` 13, `GetBaseObject` 13, `IsInFaction` 12, `Utility.Wait` 12, `GetLinkedRef` 12, `Quest.SetStage` 11, `RandomInt` 11, `IsDead` 11, `RemoveItem` 10.
- **Under 10:** `CancelTimer` 8, `Keyword.SendStoryEventAndWait` 8, `PlaceAtMe` 8, `AddItem` 8.

---

## 2. Client mapping (skymp5-client → FalloutMP client)

### 2.0 Method and scope

**skymp5-client/src** (139 `.ts` files, about 10.9k LoC) was scanned with a script that matches:
- static `Class.method(` calls against the 93 SP classes and 1,496 methods parsed from `skyrim-platform/src/platform_se/codegen/convert-files/skyrimPlatform.ts`;
- instance `.method(` calls against SP instance-method names;
- top-level SP functions and `sp.<let>` APIs;
- `sp.on/once("…")` event names;
- string-based `callNative("Class","fn")` calls.

False positives (`Map.get`, `Set.add`, `Array.find`, `ObjectReferenceEx.getDistance`, `PlayerCharacterSpeedCalculator.getSpeed`, …) were removed by hand. Every FO4 equivalent below was checked against the `.psc` files.

**Other places that touch SP**
- **skymp5-front** makes no Papyrus calls. It only uses the CEF bridge `window.skyrimPlatform.sendMessage(...)` and `window.skyrimPlatform.widgets` [src: skymp5-front/src/index.js:13-42, App.js:46, constructorComponents/chat/index.js:254]. That bridge is a platform (CEF) API and ports unchanged.
- **skymp5-functions-lib** contains only `index.ts` in this repo; its `./src/...` imports point at the private gamemode. It uses the server `mp.*` API and makes no SP calls [src: skymp5-functions-lib/index.ts:1-30].
- **Gamemode JS** arrives at runtime as signed strings (`updateOwner` / `updateNeighbor` / `eventSources`) and may call any SP API. It cannot be audited here. Treat this whole table as the *minimum* surface.
- **Server-triggered calls.** The server can run arbitrary client calls through **SpSnippet**. The calls the current server natives trigger are listed in §2.3.10.

### 2.1 Three ways an SP call is served (and what changes for FO4)

1. **Real Papyrus native through SP3 reflection** (`CallNativeSafe`).
   - The native must be bound in the VM: `provider.GetFunctionInfo` throws "Native function not found" otherwise.
   - The argument count must match **exactly**: `GetParamCount() != numArgs` throws [src: skyrim-platform/src/platform_se/skyrim_platform/CallNative.cpp:274]. Wherever FO4 adds a defaulted parameter (`PlaceAtMe`, `BlockActivation`, `AddPerk`, `ShowRaceMenu`…), the JS must pass it explicitly. The generated typings will include it.
2. **C++ fast paths inside SP that bypass Papyrus.** Each must be re-implemented with CommonLibF4:
   - `Debug.sendAnimationEvent` [CallNative.cpp:299]
   - `ObjectReference.ClearDestruction`, reused as a rename hack [:309]
   - `queueNiNodeUpdate` → `DoReset3D` [:329]
   - `pushActorAway` [:344]
   - `getFormID` [:376]
   - `Game.getFormEx` [:380]
   - Actor/ObjectReference `addItem` [:386] and `removeItem` [:403]
   - `Game.GetModCount/GetModName` [src: CallNativeApi.cpp:28-40]
   - `Game.getPlayer` [src: Sp3Api.cpp:274]

   **Caveat:** the fast paths inside `CallNativeSafe` run *after* the bound-native lookup. For functions FO4 lacks (`Debug.SendAnimationEvent`, `Game.GetFormEx`), the FO4 platform must bind stub natives with those names or check fast paths before the lookup.
3. **SP-specific natives.**
   - `TESModPlatform.psc` natives [src: skyrim-platform/src/platform_se/psc/TESModPlatform.psc].
   - Top-level C++ functions: `setInventory`, `loadGame`, `worldPointToScreenPoint`, …

**Status legend**

| Status | Meaning |
|---|---|
| **Same** | Same name (SP camelCase aside) and compatible signature. |
| **Same (F4SE)** | Same, but the function comes from F4SE. |
| **Renamed** | Different class or name, same semantics. |
| **Sig-diff** | Same purpose, but different parameters, value domain or owning class. |
| **Missing → X** | No FO4 Papyrus equivalent. X is one of: Papyrus alternative, platform fast path, new FO4 `TESModPlatform` native, CommonLibF4 engine call, server-side, drop. |

File paths in "Used in" are relative to `skymp5-client/src/`. `services/services/` is abbreviated `svc/`.

### 2.3 Mapping tables

#### 2.3.1 Game
| Skyrim call | Used in | Purpose | FO4 equivalent | Status | Notes |
|---|---|---|---|---|---|
| `Game.getPlayer()` | 57 sites, e.g. svc/deathService.ts:23, svc/craftService.ts:26 | local player | `Actor GetPlayer() [G]` | Same | SP3 fast path returns `RE::PlayerCharacter::GetSingleton()` [src: Sp3Api.cpp:274]; ports 1:1 |
| `Game.getFormEx(id)` | 65 sites, e.g. svc/timeService.ts:31, view/formView.ts:96 | lookup, including 0xFF… runtime ids | none. `Form GetForm(int aiFormID) [G]` takes a signed int | Missing → platform fast path | SP uses `TESForm::LookupByID` [src: CallNative.cpp:379-384]. Port with `RE::TESForm::GetFormByID` [src: libxse include/RE/T/TESForm.h:154] and bind a stub `Game.GetFormEx` (see §2.1) |
| `Game.getForm(id)` | view/modelApplyUtils.ts:26,31 | lookup | `Form GetForm(int aiFormID) [G]` | Same | |
| `Game.findClosestActor(x,y,z,r)` | sync/movementApply.ts:39 | resolve `lookAt` target | `Actor FindClosestActor(float afX, float afY, float afZ, float afRadius) [G]` | Same | |
| `Game.findRandomActor(...)` | svc/worldCleanerService.ts:43; view/modelApplyUtils.ts:60 | sweep vanilla actors; pick harvester | `Actor FindRandomActor(float, float, float, float) [G]` | Same | |
| `Game.findRandomReferenceOfType(...)` | svc/dropItemService.ts:41 | find dropped item | `ObjectReference FindRandomReferenceOfType(Form, float, float, float, float) [G]` | Same | |
| `Game.forceThirdPerson()` | svc/sweetCameraEnforcementService.ts:173,450,286 | camera enforcement | `ForceThirdPerson() [G]` | Same | |
| `callNative("Game","getCameraState")` | svc/sweetCameraEnforcementService.ts:277 | detect first person | F4SE `int GetCameraState() [G,F4SE]` | Same (F4SE) | Values after 4 differ from Skyrim: 5 transition, 6 tween, 7/8 third person, 9 furniture, 10 horse, 11 bleedout, 12 dialogue [src: f4se/scripts/modified/Game.psc:28-42]. The client only tests `=== 0`, so it works |
| `Game.disablePlayerControls(9 args)` | svc/sweetCameraEnforcementService.ts:174,451; svc/authService.ts:601 | freeze player | `InputEnableLayer.Create()` then `layer.DisablePlayerControls(abMovement, abFighting, abCamSwitch, abLooking, abSneaking, abMenu, abActivate, abJournalTabs, abVATS, abFavorites, abRunning)` | Renamed + Sig-diff | FO4 has no `Game.DisablePlayerControls` and no `aiDisablePOVType`. Keep the layer handle alive (e.g. in `sp.storage`) and `Delete()` it later. SP3 must marshal a non-Form ScriptObject handle |
| `Game.enablePlayerControls(9 args)` | svc/sweetCameraEnforcementService.ts:215,245 | unfreeze | `layer.EnablePlayerControls(11 bools)`, or `layer.Reset()` / `layer.Delete()` | Renamed | |
| `Game.enableFastTravel(false)` | svc/disableFastTravelService.ts:10 | disable fast travel | `layer.EnableFastTravel(bool abEnable = true)` | Renamed | `ObjectReference/Cell.EnableFastTravel` work per map marker / cell, not globally |
| `Game.getCurrentCrosshairRef()` | view/playerCharacterDataHolder.ts:13 | crosshair target in movement data | none | Missing → new native | Sink FO4 pick-ref events (`PickRefUpdateEvent` in libxse `include/RE/P/PlayerCharacter.h`; `ViewCasterUpdateEvent` sink vtable at `IDs_VTABLE.h:4820`). F4SE `GetCurrentConsoleRef` is a different thing |
| `Game.getModCount()` / `Game.getModName(i)` | svc/remoteServer.ts:576-577 (load order); sweetTaffy*Service, svc/sweetCameraEnforcementService.ts:491-493 ("sweetpie" detection) | load order | F4SE `PluginInfo[] GetInstalledPlugins() [G,F4SE]` and `GetInstalledLightPlugins()`; `struct PluginInfo {int index; string name; string author; string description}` | Renamed (F4SE) or platform | SP already implements both in C++ over `TESDataHandler` [src: CallNativeApi.cpp:27-40]. Port with `RE::TESDataHandler`; no F4SE needed |
| `Game.getNumTintMasks/getNthTintMaskTexturePath/getNthTintMaskType/getNthTintMaskColor` | sync/appearance.ts:75-81 | read player face tints | none | Missing → new native | FO4 tints are `BGSCharacterTint` entries (template + colour + value) [src: libxse IDs_VTABLE.h:1874-1881], not Skyrim tint masks. Redesign appearance (research doc §4.6) |
| `Game.quitToMainMenu()` | version.ts:18; svc/remoteServer.ts:645 | kick to menu | `QuitToMainMenu() [G]` | Same | |
| `Game.setGameSettingFloat(gmst, v)` | svc/ragdollService.ts:29-34 (`fDiffMultHPToPC{E,H,L,N,VE,VH}`) | neutralise difficulty damage multipliers | F4SE `SetGameSettingFloat(string, float) [G,F4SE]` | Same (F4SE) | Check the GMST names in Fallout4.esm; FO4 adds Survival multipliers [inference] |
| `Game.setGameSettingInt(gmst, v)` | index.ts:62 (`iDeathDropWeaponChance`) | no weapon drop on death | F4SE `SetGameSettingInt(string, int) [G,F4SE]` | Same (F4SE) | Check the GMST exists [inference] |
| `Game.setInChargen(a,b,c)` | svc/singlePlayerService.ts:22; svc/enforceLimitationsService.ts:12,16 | disable save/wait | `SetInChargen(bool abDisableSaving, bool abDisableWaiting, bool abShowControlsDisabledMessage) [G]` | Same | |
| `Game.showRaceMenu()` | svc/remoteServer.ts:854, plus server SpSnippet `ShowRaceMenu` / `ShowLimitedRaceMenu` [src: skymp5-server/cpp/server_guest_lib/script_classes/PapyrusGame.cpp:243-244] | character editor | `ShowRaceMenu(ObjectReference akMenuTarget = None, int uiMode = 0, ObjectReference akMenuSpouseFemale = None, ObjectReference akMenuSpouseMale = None, ObjectReference akVendor = None) [G]`; the menu is `LooksMenu` | Sig-diff | uiMode values [web: https://falloutck.uesp.net/wiki/ShowRaceMenu_-_Game]: 0 = start-of-game chargen (has spouse logic), 1 = Remake (player only, no sex change), 2 = haircut, 3 = surgery, 4 = face paint. Use 1; map `ShowLimitedRaceMenu` to 2/3/4. Pass all 5 args |

#### 2.3.2 Debug / Utility / UI / Input
| Skyrim call | Used in | Purpose | FO4 equivalent | Status | Notes |
|---|---|---|---|---|---|
| `Debug.messageBox(s)` | version.ts:10; svc/singlePlayerService.ts:17; svc/spVersionCheckService.ts:14 | modal message | `MessageBox(string asMessageBoxText) [G]` | Same | The whole FO4 Debug script is `DebugOnly`: calls are stripped from release-compiled scripts, but the native exists [src: f4se/scripts/vanilla/Debug.psc:1] |
| `Debug.notification(s)` | svc/sweetCameraEnforcementService.ts:333; svc/spSnippetService.ts:139 | HUD text | `Notification(string asNotificationText) [G]` | Same | |
| `Debug.sendAnimationEvent(ref, ev)` | sync/movementApply.ts:111,117,118,126; svc/deathService.ts:131; svc/sweetCameraEnforcementService.ts:199,452 | replay animation events on remote actors and the player | none in FO4 Papyrus | **Missing → platform fast path** | SP never calls Papyrus here; it runs `SendAnimationEvent::Run` [src: CallNative.cpp:299-307]. Port with `IAnimationGraphManagerHolder::NotifyAnimationGraphImpl` [src: libxse include/RE/I/IAnimationGraphManagerHolder.h:23]. Papyrus fallbacks: `ObjectReference.PlayAnimation(string)` (behaviour on actors untested [inference]), `Actor.PlaySubGraphAnimation(string)`, `PlayIdle(Idle)`, `PlayIdleAction(Action, ObjectReference = None)`. FO4 event names differ |
| `Utility.wait(s)` | 26 sites, e.g. svc/deathService.ts:92 | delay | `Wait(float afSeconds) [G,L]` | Same | Promise in SP |
| `Utility.waitMenuMode(s)` | version.ts:15; svc/remoteServer.ts:233 | delay in menus | `WaitMenuMode(float afSeconds) [G,L]` | Same | |
| `Utility.getINIInt("iSize W/H:Display")` | view/formView.ts:28-29 | screen size for nickname projection | none (FO4 `Utility` has only the debugOnly `SetINI*`) | Missing → platform | Take the size from the overlay's swap chain, or add a native over `RE::INISettingCollection` [src: libxse include/RE/S/Setting.h:310-333] |
| `Utility.setINIBool/Float/Int(key, v)` | index.ts:61,63; svc/disableDifficultySelectionService.ts:13 | INI tweaks (`bAlwaysActive:General`, `fAutoVanityModeDelay:Camera`, `iDifficulty:GamePlay`) | `SetINIBool/SetINIFloat/SetINIInt(string ini, value) [G,dbg]` | Same | `debugOnly` only affects compilation [inference]. Check the keys exist in the FO4 INIs |
| `Ui.isMenuOpen(name)` | 13 sites, e.g. version.ts:17; svc/dropItemService.ts:26; svc/sendInputsService.ts:240; svc/remoteServer.ts:198 | gate logic on menus | F4SE `bool UI.IsMenuOpen(string menu) [G,F4SE]` | Renamed (class `UI`, F4SE) | F4SE-free option: platform fast path `RE::UI::GetMenuOpen` [src: libxse include/RE/U/UI.h:77]. Menu names differ (§5.6) |
| `Input.isKeyPressed(dx)`, `Input.getNumKeysPressed()` | svc/keyboardEventsService.ts:13,20 | key-combo bindings | none. F4SE `Input` has only `GetMappedKey`/`GetMappedControl` [src: f4se/scripts/modified/Input.psc] | Missing → platform | Keep key state in the platform's DirectInput hook (it already feeds `buttonEvent`), or use F4SE `RegisterForKey` + `OnKeyDown/Up` |

#### 2.3.3 ObjectReference
| Skyrim call | Used in | Purpose | FO4 equivalent | Status | Notes |
|---|---|---|---|---|---|
| `getPositionX/Y/Z()` | extensions/objectReferenceEx.ts:21; svc/dropItemService.ts:43-45 | position | `float GetPositionX()` / `Y` / `Z` | Same | |
| `getAngleX/Y/Z()` | sync/movementGet.ts:81; sync/movementApply.ts:72,180 | rotation in degrees | `float GetAngleX()` / `Y` / `Z` | Same | |
| `setPosition(x,y,z)` | view/spawnProcess.ts:18; svc/worldCleanerService.ts:67 | spawn placement | `SetPosition(float, float, float) [L]` | Same | |
| `setAngle(x,y,z)` | view/formView.ts:216 | spawn rotation | `SetAngle(float, float, float) [L]` | Same | |
| `translateTo(8 args)` | sync/movementApply.ts:194 | smooth remote movement | `TranslateTo(float afX, float afY, float afZ, float afXAngle, float afYAngle, float afZAngle, float afSpeed, float afMaxRotationSpeed = 0.0)` | Same | Speed is clamped to ≥ 1.0. Fires `OnTranslation*` events. Behaviour on FO4 actors is untested [inference] |
| `getDistance(ref)` | view/formView.ts:552 | nickname range | `float GetDistance(ObjectReference akOther)` | Same | |
| `getParentCell()` / `getWorldSpace()` | extensions/objectReferenceEx.ts:7,12; sync/movementGet.ts:76 | worldOrCell | `Cell GetParentCell()`, `WorldSpace GetWorldSpace()` | Same | |
| `is3DLoaded()` | sync/movementApply.ts:211; view/formView.ts:412,449,475 | readiness | `bool Is3DLoaded()` | Same | `WaitFor3DLoad() [L]` also exists |
| `isDisabled()` / `isDeleted()` | svc/worldCleanerService.ts:60; view/modelApplyUtils.ts:38,85 | state | `bool IsDisabled()`, `bool IsDeleted()` | Same | |
| `disable(f)` / `enable(f)` / `delete()` | svc/worldCleanerService.ts:101,106; view/modelApplyUtils.ts:44-97; view/spawnProcess.ts:31; view/formView.ts:321 | world state | `Disable(bool abFadeOut = false) [L]`, `Enable(bool abFadeIn = false) [L]`, `Delete() [L]` | Same | |
| `disableNoWait(f)` | svc/worldCleanerService.ts:68,94; svc/remoteServer.ts:803 | fast disable | `DisableNoWait(bool abFadeOut = false)` | Same | |
| `placeAtMe(form,n,persist,disabled)` | view/formView.ts:170 | spawn remote actors/objects | `ObjectReference PlaceAtMe(Form akFormToPlace, int aiCount = 1, bool abForcePersist = false, bool abInitiallyDisabled = false, bool abDeleteWhenAble = true)` | Sig-diff | Pass 5 args. `PlaceActorAtMe(ActorBase, int aiLevelMod = 4, EncounterZone = None)` also exists |
| `activate(ref, default)` | svc/remoteServer.ts:189; view/modelApplyUtils.ts:26,31,72 | open container, harvest | `bool Activate(ObjectReference akActivator, bool abDefaultProcessingOnly = false)` | Same | |
| `blockActivation(bool)` | extensions/objectReferenceEx.ts:53,55; sync/movementApply.ts:58 | server-authoritative activation | `BlockActivation(bool abBlocked = true, bool abHideActivateText = false)` | Sig-diff | Pass 2 args. `true` hides the prompt, which suits remote players |
| `getBaseObject()` | 21 sites | type checks | `Form GetBaseObject()` | Same | |
| `getAnimationVariableBool/Float(name)` | svc/sendInputsService.ts:44-46; sync/movementGet.ts:62,84,86,105,123 | read graph vars (`bInJumpState`, `SpeedSampled`, `Direction`, `IsBlocking`, `IsSneaking`, `IsCasting*`) | `GetAnimationVariableBool/Int/Float(string)` | Same API, other names | FO4 behaviour-graph variable names must be dumped first |
| `setAnimationVariableInt("iGetUpType",1)` | svc/deathService.ts:130 | get-up style | `SetAnimationVariableInt(string, int)` (+ `Bool`/`Float`) | Same API | Variable name unverified for FO4 [inference] |
| `getOpenState()` / `setOpen(b)` | svc/activationService.ts:46; view/modelApplyUtils.ts:14,23 | doors | `int GetOpenState()` (0 none, 1 open, 2 opening, 3 closed, 4 closing), `SetOpen(bool abOpen = true)` | Same | [src: f4se/scripts/vanilla/ObjectReference.psc:446-453] |
| `lock(b,b)` / `isLocked()` | extensions/objectReferenceEx.ts:58-59 | unlock locally | `Lock(bool abLock = true, bool abAsOwner = false)`, `bool IsLocked()` | Same | |
| `setMotionType(t, allow)` | extensions/objectReferenceEx.ts:63,68; view/spawnProcess.ts:49 | keyframe items | `SetMotionType(int aeMotionType, bool abAllowActivate = true) [L]` | **Sig-diff (constants)** | FO4: `Motion_Fixed=0, Motion_Dynamic=1, Motion_Keyframed=2` [src: vanilla/ObjectReference.psc:1164-1166]. SP: `MotionType.Keyframed = 4` [src: skyrimPlatform.ts:1104-1112] |
| `setHarvested(b)` | view/modelApplyUtils.ts:74 | flora | `SetHarvested(bool abHarvested)` | Same | |
| `isHarvested()` | view/modelApplyUtils.ts:55 | flora | none | Missing → server / new native | The server already tracks an `isHarvested` property |
| `setDisplayName(name, force)` | svc/remoteServer.ts:336; view/formView.ts:98,286 | player names, `%original_name%` | none | Missing → F4SE alternative / new native | Remote players have unique cloned NPC bases, so F4SE `Form.SetName(string)` on the base works. For shared bases, write `ExtraTextDisplayData` from a native [src: libxse IDs_VTABLE.h:6699; include/RE/E/ExtraDataList.h:125] |
| `getDisplayName()` | view/formView.ts:566 | nickname text | F4SE `string GetDisplayName() [F4SE]` | Same (F4SE) | Engine: `TESObjectREFR::GetDisplayFullName` [src: libxse include/RE/T/TESObjectREFR.h:328] |
| `getTotalItemWeight()` | sync/movementGet.ts:128 | encumbrance | F4SE `float GetInventoryWeight() [F4SE]` | Renamed (F4SE) | |
| `removeAllItems(to, keep, quest)` | sync/equipment.ts:110,114; sync/inventory.ts:330 | reset inventory | `RemoveAllItems(ObjectReference akTransferTo = None, bool abKeepOwnership = false)` | Sig-diff | Drop the 3rd arg |
| `pushActorAway(a, f)` | svc/deathService.ts:117; sync/animation.ts:167 | ragdoll | `PushActorAway(Actor akActorToPush, float aiKnockbackForce)` | Same | SP fast path calls the engine directly [src: CallNative.cpp:344-374] |
| `forceRemoveRagdollFromWorld()` | svc/ragdollService.ts:16 | get up | `ForceRemoveRagdollFromWorld() [L]` | Same | |
| `isInDialogueWithPlayer()` | svc/worldCleanerService.ts:111 | protect talking NPCs | `bool IsInDialogueWithPlayer()` | Same | |
| `playAnimation(name)` | svc/remoteServer.ts:313 | restore activator animation | `bool PlayAnimation(string asAnimation)` | Same | |
| `addItem/removeItem/getItemCount` | not called directly (the client uses `setInventory` / `addItemEx`); SP fast paths exist [src: CallNative.cpp:386-424] | — | `AddItem(Form akItemToAdd, int aiCount = 1, bool abSilent = false) [L]`, `RemoveItem(Form, int = 1, bool = false, ObjectReference akOtherContainer = None) [L]`, `int GetItemCount(Form akItem = None)` | Same | These cannot address OMOD instances (§5.9) |

#### 2.3.4 Actor
| Skyrim call | Used in | Purpose | FO4 equivalent | Status | Notes |
|---|---|---|---|---|---|
| `getActorValue(name)` | svc/sweetTaffyPlayerCombatService.ts:65 ("Stamina"); sync/actorvalues.ts:29; sync/movementGet.ts:127 ("CarryWeight"); sync/movementApply.ts:189 ("Variable10") | read AV | `float GetValue(ActorValue akAV)` on **ObjectReference** | Sig-diff | String becomes an `ActorValue` form (§5.2) |
| `setActorValue(name, v)` | svc/remoteServer.ts:563; sync/animation.ts:168,187; view/formView.ts:253 ("Aggression"), 288 ("attackDamageMult"), 302-303 ("health"/"magicka" = 1e6) | base AVs; harmless clones | `SetValue(ActorValue akAV, float afValue)` | Sig-diff | |
| `damageActorValue` / `restoreActorValue` | sync/actorvalues.ts:42,44; sync/movementApply.ts:146,148; svc/sweetCameraEnforcementService.ts:289 | health % sync | `DamageValue(ActorValue, float)`, `RestoreValue(ActorValue, float)` | Sig-diff | `ModValue(ActorValue, float)` also exists |
| `getActorValuePercentage(name)` | sync/actorvalues.ts:13-15,26,34; sync/movementGet.ts:41; sync/movementApply.ts:137 | health/stamina/magicka % | `float GetValuePercentage(ActorValue akAV)` (0..1) | Sig-diff | FO4 has no magicka. Stamina becomes Action Points |
| `getBaseActorValue(name)` | sync/actorvalues.ts:28; sync/movementApply.ts:142 | max value | `float GetBaseValue(ActorValue akAV)` | Sig-diff | |
| `keepOffsetFromActor(self, …9)` / `clearKeepOffsetFromActor()` | sync/movementApply.ts:78,86; view/formView.ts:400,450 | drive remote locomotion animations | none | **Missing → redesign / new native** | Options, all to be tested in game: (a) `TranslateTo` plus graph variables set with `SetAnimationVariableFloat`; (b) the debug natives `ForceMovementDirection/Speed/RotationSpeed(+Ramp)`, `ForceTargetDirection/Speed/Angle`, `ClearForcedMovement`, which the psc labels "not in release builds" [src: vanilla/Actor.psc:1040-1096]; (c) wrap the engine keep-offset movement agent (`MovementPlannerAgentKeepOffset` exists in FO4 [src: libxse IDs_VTABLE.h:1060,8521]). This is the highest-risk call in the table |
| `setHeadTracking(b)` / `setLookAt(ref, p)` | sync/movementApply.ts:51-54 | head tracking | `SetHeadTracking(bool abEnable = true)`, `SetLookAt(ObjectReference akTarget, bool abPathingLookAt = false)` | Same | `ClearLookAt()` exists |
| `startDeferredKill()` / `endDeferredKill()` | svc/deathService.ts:24,97; sync/appearance.ts:171; view/formView.ts:301 | local immortality | `StartDeferredKill()`, `EndDeferredKill()` | Same | |
| `kill(k)` / `killSilent(k)` | svc/deathService.ts:98; svc/worldCleanerService.ts:92 | death | `Kill(Actor akKiller = None)`, `KillSilent(Actor akKiller = None)` | Same | |
| `resurrect()` | view/spawnProcess.ts:42 | spawn | `Resurrect() [L]` | Same | |
| `setDontMove(b)` | svc/deathService.ts:94,108; svc/sweetTaffyPlayerCombatService.ts:174,197 | freeze | none | Missing → Papyrus alternative | Player: `InputEnableLayer.EnableMovement(false)`. NPC: `SetRestrained(bool abRestrained = true)` or `EnableAI(false)` |
| `isDead()` | 6 sites | | `bool IsDead()` | Same | |
| `isEquipped(f)` | sync/inventory.ts:144 | | `bool IsEquipped(Form akItem)` | Same | Does not distinguish instances |
| `isSneaking/isRunning/isSprinting/isWeaponDrawn()` | sync/movementGet.ts:89-118; sync/movementApply.ts:110-131 | movement state | same names | Same | |
| `isSwimming()` | svc/sweetCameraEnforcementService.ts:440 | camera rule | none | Missing → new native | `TESObjectREFR::IsInWater()` [src: libxse include/RE/T/TESObjectREFR.h:459], or the player-only `OnPlayerSwimming` event |
| `getSitState()` | sync/movementApply.ts:186 | `=== 3` sitting | `int GetSitState()` | Same | Values 0, 2, 3, 4 [src: vanilla/Actor.psc:275-279] |
| `getFurnitureReference()` | svc/craftService.ts:26; svc/remoteServer.ts:202; sync/movementGet.ts:110 | furniture | F4SE `ObjectReference GetFurnitureReference() [F4SE]` | Same (F4SE) | |
| `getCombatTarget()` / `getDialogueTarget()` | sync/movementGet.ts:48; svc/worldCleanerService.ts:111 | | same names | Same | |
| `getEquippedWeapon(bLeft)` | svc/playerBowShotService.ts:71; svc/sweetTaffyPlayerCombatService.ts:186 | weapon checks | `Weapon GetEquippedWeapon(int aiEquipIndex = 0)` | Sig-diff | Bool becomes an equip index |
| `getEquippedItemType(h)` | svc/magicSyncService.ts:166-167 | hand content | `int GetEquippedItemType(int aiEquipIndex)` | Sig-diff | Adds 9 = Gun, 10 = Grenade, 11 = Mine [src: vanilla/Actor.psc:183-202] |
| `getEquippedSpell` / `equipSpell` / `unequipSpell` / `addSpell` / `removeSpell` | svc/magicSyncService.ts:158,162; sync/equipment.ts:32,99,104; sync/spell.ts:15,29 | magic sync | same names exist (`AddSpell(Spell, bool abVerbose=true)`) | Same, but irrelevant | Drop magic sync for FO4 |
| `getSpellCount()` / `getNthSpell(i)` | sync/spell.ts:6-7 | spell list | none | Missing → drop | |
| `addPerk(p)` | svc/sweetTaffyDynamicPerksService.ts:106; svc/sweetTaffyStaticPerksService.ts:31 | gamemode perks | `AddPerk(Perk akPerk, bool abNotify=false)` | Sig-diff | Pass 2 args |
| `unequipItem(f,b,b)` / `unequipAll()` | svc/remoteServer.ts:97; sync/equipment.ts:112 | equipment | `UnequipItem(Form akItem, bool abPreventEquip = false, bool abSilent = false)`, `UnequipAll()` | Same | `UnequipItemSlot(int aiSlot)` also exists |
| `queueNiNodeUpdate()` | sync/inventory.ts:453; sync/appearance.ts:168; view/formView.ts:514-515 | refresh 3D | F4SE `QueueUpdate(bool bDoEquipment = false, int flags = 0) [F4SE]` (`0xC` = body only) | Renamed (F4SE) / platform | SP's fast path calls `DoReset3D(false)` [src: CallNative.cpp:329-342]. FO4 engine equivalent: `RE::Actor::Reset3D` [src: libxse include/RE/A/Actor.h:500] |
| `setAlpha(a, fade)` | svc/worldCleanerService.ts:95 | hide | `SetAlpha(float afTargetAlpha, bool abFade = false)` | Same | |
| `getRace()` | svc/worldCleanerService.ts:85; view/formView.ts:222 | race filters | `Race GetRace()` | Same | The race ids are Skyrim's and must be rewritten |
| `hasLOS(ref)` | view/formView.ts:553 | nickname visibility | `bool HasDetectionLOS(ObjectReference akOther)` (Actor) or `bool HasDirectLOS(ObjectReference akTarget, string asSourceNode = "", string asTargetNode = "")` | Renamed | |
| `wornHasKeyword(kw)` | view/formView.ts:595 | `SweetHidePerson` | `bool WornHasKeyword(Keyword akKeyword)` | Same | |

#### 2.3.5 ActorBase (appearance)
| Skyrim call | Used in | Purpose | FO4 equivalent | Status | Notes |
|---|---|---|---|---|---|
| `getSex()` / `getRace()` | sync/appearance.ts:42-43 | | `int GetSex()`, `Race GetRace()` | Same | |
| `getWeight()` / `setWeight(w)` | sync/appearance.ts:44,136 | body weight 0..100 | F4SE `BodyWeight GetBodyWeight()` / `SetBodyWeight(BodyWeight)`, `struct BodyWeight {float thin; float muscular; float large}` | Sig-diff (F4SE) | The three values must sum to 1.0; call `Actor.QueueUpdate` afterwards. F4SE `Form.Get/SetWeight` is **item** weight |
| `getHairColor()` + `ColorForm.getColor()` | sync/appearance.ts:38,45,53 | hair/skin colour | none (FO4 has no `ColorForm` script) | Missing → new native | |
| `getNumHeadParts()` / `getNthHeadPart(i)` | sync/appearance.ts:57,59 | read headparts | F4SE `HeadPart[] GetHeadParts(bool bOverlays = false)` | Renamed (F4SE) | |
| `setNthHeadPart(hp,i)` | sync/appearance.ts:140 | write headparts | per actor: `Actor.ChangeHeadPart(HeadPart apHeadPart, bool abRemovePart = false, bool abRemoveExtraParts = false)` | Missing → new native for the base | Add `SetNpcHeadParts(ActorBase, HeadPart[])` |
| `get/setFaceMorph`, `get/setFacePreset` | sync/appearance.ts:66,70,143-144 | 19 morphs + 4 presets | none | Missing → new natives | FO4 morph model: sliders (MSDK/MSDV) + face regions (FMRI/FMRS) |
| `get/setFaceTextureSet` | sync/appearance.ts:47-48,141 | face texture | none (only `Actor.SetEyeTexture(TextureSet)`) | Missing → new native | Skin tone is a tint in FO4 [inference] |
| `setVoiceType(vt)` | sync/appearance.ts:142 | silent clone | `Actor.SetOverrideVoiceType(VoiceType akVoiceType)` (on the actor) | Renamed | |

#### 2.3.6 Form and other classes
| Skyrim call | Used in | Purpose | FO4 equivalent | Status | Notes |
|---|---|---|---|---|---|
| `getFormID()` | 113 sites | | `Int GetFormID()` | Same | SP fast path. Ids ≥ 0x80000000 are negative as a Papyrus int (§5.11) |
| `getName()` / `setName(s)` | svc/containersService.ts:64; svc/remoteServer.ts:328; sync/appearance.ts:146,149 | names | F4SE `string GetName()`, `SetName(string name)` | Same (F4SE) | Not in vanilla FO4 |
| `getType()` | 9 sites, e.g. extensions/objectReferenceEx.ts:38; svc/hitService.ts:38 | branch on form type | none | **Missing → platform fast path** | `RE::TESForm::GetFormType()` [src: libxse include/RE/T/TESForm.h:229] plus a new FO4 `FormType` enum (values differ, §5.5) |
| `hasKeyword(kw)` | svc/sweetTaffyDynamicPerksService.ts:114 | | `bool HasKeyword(Keyword)` | Same | |
| `Keyword.getKeyword(edid)` | svc/sweetTaffyDynamicPerksService.ts:113; view/formView.ts:594 | keyword by editor id | none | Missing → new native | `RE::TESForm::GetFormByEditorID` [src: libxse TESForm.h:173], or `Game.GetFormFromFile` |
| `Weapon.getWeaponType()` | svc/playerBowShotService.ts:78; svc/sweetTaffyPlayerCombatService.ts:186 | bow detection, timings | none | Missing → Papyrus alternative / native | Use WEAP keywords or `GetEquippedItemType`. The Skyrim bow logic does not apply to guns anyway |
| `Flora.getIngredient()` | extensions/objectReferenceEx.ts:67 | flora produce | none (FO4 `Flora` has no natives) | Missing → server / native | |
| `GlobalVariable.getValue/setValue` | svc/timeService.ts:43-52 | GameHour/Day/Month/Year/TimeScale (0x38, 0x37, 0x36, 0x35, 0x3A) | `float GetValue()`, `SetValue(float)` | Same | Verify the ids in Fallout4.esm [inference] |
| `Sound.play(ref)` | svc/spSnippetService.ts:126 | pickup sound (Skyrim ids 0x334AB / 0x14115) | `int Play(ObjectReference akSource)` | Same | Replace the sound ids |
| `ActorValueInfo.getActorValueInfoByID/setSkillUseMult/setSkillOffsetMult` | svc/disableSkillAdvanceService.ts:37-43 | disable skill XP | none (the `ActorValue` script has no natives) | Missing → drop | FO4 has no skill-use XP |
| `X.from(obj)` casts | Actor ×31, ObjectReference ×21, Ammo ×5, GlobalVariable ×5, ActorBase ×3, Spell ×3, Cell/WorldSpace/Perk/TextureSet ×2, Armor/Enchantment/Flora/Form/HeadPart/Potion/Race/Scroll/Sound/VoiceType/Weapon ×1 | SP3 casts | All of these script types exist in FO4 | Same | `ColorForm`, `ActorValueInfo`, `NetImmerse`, `DefaultObjectManager`, `TreeObject`, `Apparatus`, `Art`, `SoundDescriptor` do not |

#### 2.3.7 NetImmerse (SKSE in Skyrim; nothing in FO4)
| Skyrim call | Used in | Purpose | FO4 equivalent | Status | Notes |
|---|---|---|---|---|---|
| `getNodeWorldPositionX/Y/Z(ref, node, fp)` | view/formView.ts:499-501,557-559 | head position for nicknames (`"NPC Head [Head]"`) | none | Missing → new native | `NiAVObject::GetObjectByName` [src: libxse include/RE/N/NiAVObject.h:38] + `world.translate`. The FO4 node is `"Head"`. Papyrus-only hack: `MoveToNode` a marker and read its position. `ObjectReference.HasNode(string)` exists |
| `setNodeScale(ref, node, s, fp)` | view/modelApplyUtils.ts:124; server SpSnippet [src: script_classes/PapyrusNetImmerse.cpp:83] | model tweaks | none | Missing → new native | Whole-ref alternative: `SetScale(float) [L]` |
| `setNodeTextureSet(ref, node, ts, fp)` | view/modelApplyUtils.ts:111; server SpSnippet [src: PapyrusNetImmerse.cpp:82] | retexture | none | Missing → F4SE alternative / native | FO4 uses material swaps: F4SE `ApplyMaterialSwap(MatSwap, bool renameMaterial = false) [L,F4SE]`, `SetMaterialSwap(MatSwap)` |

#### 2.3.8 TESModPlatform (SkyMP natives; all must be rewritten for FO4)
| Skyrim call | Used in | Purpose | FO4 replacement | Priority / notes |
|---|---|---|---|---|
| `createNpc()` | sync/appearance.ts:154 | clone template NPC | new `ActorBase CreateNpc()` | P0. Needs an FO4 template TESNPC and a DoNothing package; Skyrim ids 0x10D13E / 0x654E2 do not apply |
| `setNpcSex`, `setNpcRace`, `setNpcSkinColor`, `setNpcHairColor`, `resizeHeadpartsArray`, `setFormIdUnsafe`, `getSkinColor`, `clearTintMasks`, `pushTintMask`, `resizeTintsArray` | sync/appearance.ts:39,113-139 | appearance on the clone | new `SetNpcSex`, `SetNpcRace` (actor-level `Actor.SetRace(Race akRace = None)` exists), `SetNpcHeadParts`, `SetNpcMorphs(int[] keys, float[] values)`, `SetNpcFaceRegions`, `SetNpcTints`, `SetNpcHairColor`; body weight via F4SE `SetBodyWeight` | P1. Layout reference: `f4se/f4se/GameCustomization.h` |
| `addItemEx(…12 args)` + `pushWornState(w, wl)` | sync/inventory.ts:397,444,447 | inventory with extra data | new `AddItemEx(ObjectReference, Form, int count, ObjectMod[] mods, float health, string name, bool worn)` building `BGSObjectInstanceExtra` | P1. Vanilla `AttachModToInventoryItem(Form, ObjectMod)` fails when the container holds more than one of the item [web: https://falloutck.uesp.net/wiki/AttachModToInventoryItem_-_ObjectReference] |
| `evaluateLeveledNpc(ids)` | view/formView.ts:626 (dead code, disabled at :140-141) | LVLN | server-side | P2 |
| `isPlayerRunningEnabled()` | sync/movementGet.ts:115 | run toggle | new native over PlayerControls [inference] | P1 |
| `moveRefrToPosition(refr, cell, world, pos, rot)` | svc/remoteServer.ts:261,496 | teleport across cells | new native (engine MoveTo with cell/worldspace) | P0. Papyrus fallback: `PlaceAtMe` a marker, then `MoveTo` it |
| `resetContainer(base)` | sync/inventory.ts:328 | clear base container | new native | P1 |
| `setWeaponDrawnMode(actor, mode)` | sync/movementApply.ts:132; view/formView.ts:326,467 | force drawn/sheathed | `Actor.DrawWeapon()` draws only; there is no sheathe native. New native + hook | P0 |
| `blockPapyrusEvents(true)` | svc/blockPapyrusEventsService.ts:12,16 | silence vanilla scripts | platform hook on the VM event dispatch | P0 |

#### 2.3.9 SP platform helpers and events (non-Papyrus)
| Skyrim API | Used in | FO4 plan | Status |
|---|---|---|---|
| `setInventory(formId, inv)` | sync/equipment.ts:118 | rewrite around FO4 `BGSInventoryList` + instance data | Missing → platform |
| `castSpellImmediate`, `interruptCast` | svc/remoteServer.ts:957,963 | replace with weapon-fire sync | Drop |
| `getAnimationVariablesFromActor`, `applyAnimationVariablesToActor` | svc/magicSyncService.ts:115; svc/remoteServer.ts:984 | rewrite with FO4 graph-variable tables: `GetGraphVariableImpl*` / `SetGraphVariable*` [src: libxse include/RE/I/IAnimationGraphManagerHolder.h:37-66] | Missing → platform |
| `setCollision(refrId, b)` | sync/animation.ts:193,197 | new native over hknp collision [inference] | Missing → platform |
| `getExtraContainerChanges(id)`, `getContainer(base)` | sync/inventory.ts:208,239 | rewrite | Missing → platform |
| `loadGame(pos, angle, cell, npc, order, time)` | svc/loadGameService.ts:10-17 | template `.fos` save + MoveTo (research doc §4.1) | Missing → platform |
| `worldPointToScreenPoint` | view/formView.ts:503,556 | `NiCamera::WorldPtToScreenPt3` [src: libxse include/RE/N/NiCamera.h:52] | Port |
| text API (`createText`, `destroyText`, `setText*`, `getText*`, `setTextsVisibility`) | svc/netInfoService.ts:64-156; view/formView.ts:566-578; svc/clientListener.ts:12-21 | same API (D3D11 overlay) | Port |
| `printConsole`, `writeLogs`, `callNative`, `on/once/unsubscribe`, `storage`, `settings`, `browser.*`, `win32.loadUrl`, `HttpClient`, `mpClientPlugin.*`, `getFileInfo`, `findConsoleCommand`, `getPluginSourceCode`, `writePlugin`, `getPlatformVersion`, `decodeUtf8` | svc/authService.ts, svc/browserService.ts, svc/networkingService.ts:22-97, svc/consoleCommandsService.ts:37-51, svc/loadOrderVerificationService.ts:135 | same APIs. `getFileInfo` must handle `.ba2`. `findConsoleCommand` needs the FO4 command table [src: libxse include/RE/S/SCRIPT_OUTPUT.h] | Port |
| `hooks.sendAnimationEvent.add({enter, leave}, minId, maxId, pattern)` | 19 sites, e.g. svc/deathService.ts:32-60; sync/animation.ts:215,307 | hook FO4 `NotifyAnimationGraph`. First-person graph caveat: research doc §7.1 | Port |
| `on("update")` / `on("tick")` | 43 / 10 sites | platform loop | Port |
| `on("activate")` | svc/activationService.ts:15 | `RE::TESActivateEvent` [src: libxse include/RE/G/GameScript.h] | Port |
| `on("containerChanged")` | svc/containersService.ts:16 and 3 more | `RE::TESContainerChangedEvent` | Port |
| `on("equip")` / `on("unequip")` | svc/sendInputsService.ts:34-35 | `RE::TESEquipEvent` | Port |
| `on("hit")` | svc/hitService.ts:12 | `RE::TESHitEvent` | Port |
| `on("menuOpen")` / `on("menuClose")` | svc/browserService.ts:21-22 | `RE::MenuOpenCloseEvent`; FO4 names (§5.6) | Port |
| `on("buttonEvent")` | svc/sweetCameraEnforcementService.ts:79 | `RE::ButtonEvent` | Port |
| `on("loadGame")` / `on("preLoadGame")` | svc/loadGameService.ts:7; svc/timersService.ts:28 | `RE::TESLoadGameEvent`; F4SE `kMessage_PreLoadGame` | Port |
| `on("spellCast")` | svc/magicSyncService.ts:17 | `RE::TESSpellCastEvent` | Drop |
| `on("playerBowShot")` | svc/playerBowShotService.ts:13 | weapon fire via animation event + `PlayerAmmoCountEvent` [src: libxse include/RE/P/PlayerAmmoCountEvent.h] | Replace |

#### 2.3.10 Calls the server sends to clients via SpSnippet
The client must keep supporting these on FO4. Sites are in `skymp5-server/cpp/server_guest_lib/script_classes/`.

| Server native → client call | Server site | FO4 client call | Status |
|---|---|---|---|
| `Actor.DrawWeapon`, `UnequipAll`, `PlayIdle`, `GetSitState` | PapyrusActor.cpp:32,41,50,59 | same names | Same |
| `Actor.SetActorValue` (sent to the host) | PapyrusActor.cpp:103 | `SetValue(ActorValue, float)` | Sig-diff: serialize the AV as a form |
| `Actor.SetAlpha` | PapyrusActor.cpp:208 | same | Same |
| `Actor.EquipItem` / `EquipItemEx` / `EquipSpell` / `UnequipItem` | PapyrusActor.cpp:278,317,351,370 | `EquipItem(Form, bool, bool)`; `EquipItemEx` has no counterpart | Same / Missing (`EquipItemEx`) |
| `Actor.SetDontMove` | PapyrusActor.cpp:386 | IEL or `SetRestrained` | Missing → alternative |
| `Actor.AddSpell` / `RemoveSpell` | PapyrusActor.cpp:588,631 | same | Same |
| `Debug.Notification` / `MessageBox` | PapyrusDebug.cpp:10,17 | same | Same |
| `Debug.SendAnimationEvent` | PapyrusDebug.cpp:29 | platform fast path | Missing → platform |
| `Game.ForceThirdPerson` / `DisablePlayerControls` / `EnablePlayerControls` / `GetCameraState` / `ShakeController` | PapyrusGame.cpp:13,20,27,165,225 | same / IEL / IEL / F4SE / `ShakeController(float, float, float)` | Same / Renamed |
| `Game.ShowRaceMenu` / `ShowLimitedRaceMenu` | PapyrusGame.cpp:177 | `ShowRaceMenu(None, uiMode, …)` | Sig-diff |
| `Message.Show` | PapyrusMessage.cpp:6 | `int Show(float ×9) [L]` | Same |
| `NetImmerse.SetNodeTextureSet` / `SetNodeScale` | PapyrusNetImmerse.cpp:37,70 | new natives / MatSwap | Missing |
| `ObjectReference.Enable` / `Disable` / `SetPosition` / `PlayAnimation` / `PlayAnimationAndWait` / `PlayGamebryoAnimation` / `SetDisplayName` | PapyrusObjectReference.cpp:455,476,586,625,651,690,892 | same names; `SetDisplayName` → F4SE `SetName` or a native | Same / Missing |
| `Sound.Play` | PapyrusSound.cpp:26 | same | Same |
| `EffectShader` / `VisualEffect` `Play` / `Stop` | PapyrusEffectBase.cpp:45,53 | `EffectShader.Play(ObjectReference, float afDuration = -1.0)`, `VisualEffect.Play(ObjectReference, float afTime = -1.0, ObjectReference akFacingObject = None)`, `Stop(ObjectReference)` | Same |
| `SkympHacks.AddItem` / `RemoveItem` (client pseudo-class: plays a sound and shows a notification) | PapyrusObjectReference.cpp:171,267 | client-only | Port with FO4 sound ids |

### 2.4 Summary: client calls that need new code on FO4

The table gives one line per missing call. **N** = new native in the FO4 `TESModPlatform` (Papyrus-registered by the platform). **FP** = C++ fast path in `CallNative`.

| Missing call | Recommended replacement | Kind | Priority |
|---|---|---|---|
| `Debug.sendAnimationEvent` | `NotifyAnimationGraphImpl` (keep the JS API) | FP + stub native | P0 |
| `Actor.keepOffsetFromActor` / `clearKeepOffsetFromActor` | prototype (a)/(b)/(c) from §2.3.4 | N or design change | P0 (risk) |
| `Game.getFormEx` | `TESForm::GetFormByID` | FP + stub native | P0 |
| `Form.getType` | `TESForm::GetFormType` + FO4 FormType enum | FP | P0 |
| `TESModPlatform.createNpc`, `moveRefrToPosition`, `setWeaponDrawnMode`, `blockPapyrusEvents` | FO4 rewrites | N | P0 |
| `Actor.setDontMove` | `InputEnableLayer` / `SetRestrained` | Papyrus alternative | P0 |
| `Actor.queueNiNodeUpdate` | F4SE `QueueUpdate` or `Actor::Reset3D` | F4SE / FP | P0 |
| `Game.disable/enablePlayerControls`, `enableFastTravel` | `InputEnableLayer` (SP3 must marshal the handle) | rename | P0 |
| `ObjectReference.setDisplayName` | F4SE `Form.SetName` on the clone; `ExtraTextDisplayData` native | F4SE / N | P1 |
| `NetImmerse.getNodeWorldPosition*`, `setNodeScale`, `setNodeTextureSet` | `GetObjectByName` natives; F4SE MatSwap | N / F4SE | P1 |
| `Game.getModCount/getModName` | port SP's `TESDataHandler` code, or F4SE `GetInstalledPlugins` | FP / F4SE | P1 |
| `Utility.getINIInt` | swap-chain size or `INISettingCollection` | FP | P1 |
| `Input.isKeyPressed/getNumKeysPressed` | platform key-state table | FP | P1 |
| `Game.getCurrentCrosshairRef` | pick-ref event sink | N | P1 |
| `Keyword.getKeyword` | `GetFormByEditorID` | N | P1 |
| `Actor.isSwimming` | `TESObjectREFR::IsInWater` | N | P2 |
| `ObjectReference.isHarvested`, `Flora.getIngredient` | server state / ESM data | server | P2 |
| `Weapon.getWeaponType` | keywords / `GetEquippedItemType` | Papyrus alternative | P2 |
| ActorBase face/morph/tint/headpart/hair, `Game` tint masks, `ColorForm.getColor`, `TESModPlatform` appearance natives | new FO4 appearance natives (`SetNpcMorphs`, `SetNpcTints`, …) + F4SE `GetHeadParts` / `Get/SetBodyWeight` | N | P1 |
| `TESModPlatform.addItemEx` / `pushWornState` / `resetContainer`, `setInventory`, `getExtraContainerChanges`, `getContainer` | OMOD-aware inventory natives | N / FP | P1 |
| `setCollision`, `get/applyAnimationVariables*` | FO4 RE | FP | P1 |
| `loadGame` | template save + MoveTo | platform | P0 |
| `getSpellCount/getNthSpell`, `castSpellImmediate`, `interruptCast`, `ActorValueInfo.*`, `spellCast`/`playerBowShot` events | — | drop / replace | — |

---

## 3. Server mapping (`skymp5-server/cpp/server_guest_lib/script_classes`)

### 3.0 How natives work today

**Registration.** Natives are registered through `AddMethod` / `AddStatic` in each `PapyrusX::Register`. The 22 classes are created in `PapyrusClassesFactory.cpp`. That gives **127 registrations**:
- `PapyrusEffectBase` serves both `EffectShader` and `VisualEffect`.
- `PapyrusLeveledBase` serves `LeveledActor`, `LeveledItem` and `LeveledSpell`.

**Lookup.** `papyrus-vm` tries script (non-native) functions first, then natives. It walks native classes up the parent chain recorded in the loaded `.pex` object tables [src: papyrus-vm/src/papyrus-vm-lib/VirtualMachine.cpp:295-330]. For FO4 this has two consequences:
- **Stub `.pex` files must cover the whole chain up to `ScriptObject`**: `Actor` → `ObjectReference` → `Form` → `ScriptObject`. Otherwise the `ScriptObject` natives (timers, registrations) are never found.
- **Where vanilla FO4 `.pex` bodies are loaded, the script-defined wrappers run instead of any native of the same name.** Examples: `Game.FindClosestReferenceOfTypeFromRef`, `Quest.SetStage/GetStage`, `Actor.AddToFaction`. The server must then implement the natives those wrappers call.

**How a native is implemented.** Each one is one of four kinds:
- **state**: answered from server state.
- **esm**: read from the loaded ESM data.
- **SpSnippet**: forwarded to one client, which runs the call (the result comes back as a promise).
- **stub**: returns a constant.

### 3.1 Every native the server implements today → FO4 counterpart

| Class | Native (impl) [src: script_classes/PapyrusX.cpp] | FO4 counterpart | Difference / action |
|---|---|---|---|
| Actor | `IsWeaponDrawn` (state) | `bool IsWeaponDrawn()` | Same |
| Actor | `DrawWeapon` (SpSnippet) | `DrawWeapon()` | Same |
| Actor | `UnequipAll` (SpSnippet) | `UnequipAll()` | Same |
| Actor | `PlayIdle` (SpSnippet) | `bool PlayIdle(Idle)` | Same; FO4 IDLE forms differ |
| Actor | `GetSitState` (SpSnippet, latent) | `int GetSitState()` | Same; should become state-based |
| Actor | `RestoreActorValue(string, float)` (state; health/stamina/magicka only, `ConvertToAV` [PapyrusActor.cpp:12-25]) | `ObjectReference.RestoreValue(ActorValue, float)` | **Rename + AVIF form**. The AV set becomes Health / ActionPoints / Rads … |
| Actor | `SetActorValue(string, float)` (SpSnippet to host) | `ObjectReference.SetValue(ActorValue, float)` | Rename + form. Should affect server AVs |
| Actor | `DamageActorValue(string, float)` (state) | `ObjectReference.DamageValue(ActorValue, float)` | Rename + form |
| Actor | `GetActorValuePercentage(string)` (state) | `ObjectReference.GetValuePercentage(ActorValue)` | Rename + form; 0..1 |
| Actor | `IsEquipped(Form)` (state; FormList aware) | `bool IsEquipped(Form)` | Same; must become instance-aware (OMOD) |
| Actor | `SetAlpha` (SpSnippet to listeners) | `SetAlpha(float, bool)` | Same |
| Actor | `EquipItem(Form, bool, bool)` (validate + SpSnippet) | `EquipItem(Form akItem, bool abPreventRemoval = false, bool abSilent = false)` | Same. Replace the torch/LIGH rule with FO4 rules |
| Actor | `EquipItemEx` (SKSE) | none | FalloutMP extension (OMOD-aware equip) or drop |
| Actor | `EquipSpell(Spell, int)` | `EquipSpell(Spell, int)` | Same, rarely used (P2) |
| Actor | `UnequipItem(Form, bool, bool)` | same | Same |
| Actor | `SetDontMove(bool)` | none | Map to client IEL (player) / `SetRestrained` (NPC) |
| Actor | `IsDead` (state) | `bool IsDead()` | Same |
| Actor | `WornHasKeyword(Keyword)` (esm) | same | Same; OMOD keywords count on FO4 instances [inference] |
| Actor | `AddToFaction(Faction)` (state) | **script-defined** in FO4 `Actor.psc:698` (calls `SetFactionRank(akFaction, 0)`) | Implement `SetFactionRank` / `GetFactionRank` / `ModFactionRank` natives |
| Actor | `IsInFaction`, `RemoveFromFaction` | same | Same; add `RemoveFromAllFactions` |
| Actor | `GetFactions(int, int)` (SKSE) | none | Extension or drop |
| Actor | `AddSpell(Spell, bool)`, `RemoveSpell(Spell)` | same | Same (P2) |
| Actor | `GetRace` (state/esm) | `Race GetRace()` | Same |
| Actor | `GetSpellCount`, `GetNthSpell` (SKSE) | none | Drop |
| Book | `GetSpell` (SKSE) | none (FO4 `Book` has no natives) | Drop |
| Cell | `IsAttached` (stub true), `IsInterior` (esm) | same (+ `IsLoaded`) | Same |
| Debug | `Notification`, `MessageBox` (SpSnippet) | same `[G]` | Same |
| Debug | `SendAnimationEvent` (SpSnippet) | none in FO4 | Keep as a FalloutMP extension (the client fast path exists) |
| Debug | `Trace(string, int)` (log) | `Trace(string asTextToPrint, int aiSeverity = 0)` | Same; add `TraceStack`, `TraceConditional` [S] |
| EffectShader / VisualEffect | `Play`, `Stop` (SpSnippet to listeners) | same signatures (§2.3.10) | Same |
| Faction | `GetReaction(Faction)` (SKSE, esm) | `int GetFactionReaction(Actor akOther)` | Different (takes an Actor). Keep the extension; add vanilla `GetFactionReaction` |
| Form | `RegisterForSingleUpdate(float)` (server timer → `OnUpdate`) | none | Replace with `ScriptObject.StartTimer` / `OnTimer` (§3.2) |
| Form | `GetType` (SKSE) | none | Extension only. If kept, return FO4 `ENUM_FORM_ID` values |
| Form | `HasKeyword(Keyword)` (esm) | same | Same; add `HasKeywordInFormList` |
| Form | `GetFormID` (returns a float for runtime ids [PapyrusForm.cpp:107-115]) | `Int GetFormID()` | Same; the signed-int caveat remains |
| Form | `GetName` (returns EDID, not FULL) | F4SE `string GetName()` | Return localized FULL |
| Form | `GetWeight` (state/esm) | F4SE `float GetWeight()` | Same name (F4SE) |
| FormList | `GetSize`, `GetAt`, `Find` (esm) | same | Same; add `HasForm`, `AddForm`, `RemoveAddedForm`, `Revert` (needs runtime FormList state) |
| Game | `IncrementStat` (stub) | `IncrementStat(string, int = 1)` | Same |
| Game | `ForceThirdPerson` (SpSnippet) | same | Same |
| Game | `DisablePlayerControls` / `EnablePlayerControls` (SpSnippet) | none (`InputEnableLayer`) | Implement the `InputEnableLayer` class with per-player handles; forward to the client |
| Game | `FindClosestReferenceOfAnyTypeInListFromRef`, `FindClosestReferenceOfTypeFromRef` (state, grid) | **script-defined** in FO4 (`Game.psc:43,53`); the natives are `FindClosestReferenceOfType(Form, x, y, z, r)` / `…OfAnyTypeInList(FormList, x, y, z, r)` | Implement the coordinate natives plus `FindRandom*`, `FindClosestActor`, `FindRandomActor` |
| Game | `GetPlayer` (compat policy default actor) | `Actor GetPlayer() [G]` | Same |
| Game | `ShowRaceMenu` / `ShowLimitedRaceMenu` (SpSnippet) | `ShowRaceMenu(5 optional params)` | Map Limited to uiMode 2-4 |
| Game | `GetCameraState` (SpSnippet) | F4SE same name | Same name, different value table |
| Game | `GetForm` (state/esm) | `Form GetForm(int)` | Same |
| Game | `GetFormEx` (SP extension) | none | Keep as an extension |
| Game | `ShakeController(float, float, float)` (SpSnippet) | same | Same |
| Keyword | `GetKeyword(string)` (SKSE, esm by EDID) | none | Keep as an extension; add vanilla `SendStoryEvent` (P2) |
| Leveled* | `GetNthForm(int)` (SKSE, esm) | none (FO4: `AddForm`, `Revert` only) | Extension or drop |
| Message | `Show` (SpSnippet, returns result) | `int Show(float ×9) [L]` | Same; add `ShowAsHelpMessage` (P1) |
| NetImmerse | `SetNodeTextureSet`, `SetNodeScale` (state + SpSnippet) | none | Keep as an extension (the client implements it natively) |
| ObjectReference | `IsHarvested` (state) | none | Extension |
| ObjectReference | `IsDisabled`, `IsDeleted` (state) | same | Same |
| ObjectReference | `GetScale` (stub 1.0), `SetScale` (stub) | same; `SetScale` is `[L]` | Implement + sync |
| ObjectReference | `EnableNoWait`, `DisableNoWait` (**stubs, do nothing**) | same | Bug: should call Enable/Disable |
| ObjectReference | `AddItem(Form, int, bool)` (state; FLST/LVLI aware) | same `[L]` | Same; OMOD instances |
| ObjectReference | `RemoveItem(Form, int, bool, ObjectReference)` (state) | same `[L]` | Same |
| ObjectReference | `RemoveAllItems(ObjectReference, bool, bool)` | `RemoveAllItems(ObjectReference akTransferTo = None, bool abKeepOwnership = false)` | 2 params in FO4 |
| ObjectReference | `GetItemCount(Form)` (state) | `int GetItemCount(Form akItem = None)` | Same; support `None` = all [inference] |
| ObjectReference | `GetAnimationVariableBool` (state) | same (+ Int/Float) | Same |
| ObjectReference | `PlaceAtMe(Form, int, bool, bool)` (state; EXPL unsupported) | `PlaceAtMe(…, bool abDeleteWhenAble = true)` | +1 param; angle degrees/radians TODO noted in source |
| ObjectReference | `SetAngle`, `SetPosition` (state + SpSnippet) | same `[L]` | Same |
| ObjectReference | `Enable`, `Disable`, `Delete` (state) | same `[L]` | Same |
| ObjectReference | `BlockActivation(bool)` (state) | `BlockActivation(bool = true, bool abHideActivateText = false)` | +1 param |
| ObjectReference | `IsActivationBlocked`, `Activate` (state) | same | Same |
| ObjectReference | `GetPositionX/Y/Z`, `GetBaseObject`, `GetDistance`, `GetParentCell` (state) | same | `GetParentCell` returns None for exteriors today |
| ObjectReference | `PlayAnimation`, `PlayAnimationAndWait`, `PlayGamebryoAnimation` (SpSnippet) | same | Same |
| ObjectReference | `MoveTo(ref, x, y, z, bool)` (state) | same `[L]` | Same |
| ObjectReference | `SetOpen`, `GetOpenState` (state; doors) | same | Same |
| ObjectReference | `Is3DLoaded` (stub true) | same | Same |
| ObjectReference | `GetLinkedRef(Keyword)` (keyword ignored) | `GetLinkedRef(Keyword apKeyword = NONE)` | **Must support keywords** (FO4 workshop/linked-ref logic relies on them) |
| ObjectReference | `GetNthLinkedRef(int)` | `GetNthLinkedRef(int, Keyword apKeyword = None)` | +keyword |
| ObjectReference | `GetAllItemsCount`, `IsContainerEmpty` (SkyMP custom) | none | Extensions |
| ObjectReference | `SetDisplayName(string, bool)` (state + SpSnippet) | none | Extension |
| ObjectReference | `GetTotalItemWeight` (SKSE) | F4SE `GetInventoryWeight()` | Rename; register both |
| Potion | `IsFood` (SKSE, esm) | `bool IsHostile()` only | Extension; FO4 food is keyword-based |
| Quest | `GetStage` (→ `GetCurrentStageID`) | **script-defined** in FO4 | Implement `GetCurrentStageID` / `SetCurrentStageID [L]` / `IsStageDone` for real (today a stub returning 0) |
| Quest | `GetCurrentStageID` (stub 0) | same | Implement |
| Skymp | `SetDefaultActor(Actor)` (compat policy) | none | Keep as a FalloutMP extension (consider renaming the class) |
| Sound | `Play` (SpSnippet; returns None) | `int Play(ObjectReference)` | Return an instance id |
| Utility | `Wait`, `WaitMenuMode`, `WaitGameTime` (timer promise; 1 game hour = 60 s) | same `[L]` | Same |
| Utility | `IsInMenuMode` (false), `RandomInt`, `RandomFloat`, `GetCurrentRealTime`, `GetCurrentGameTime` (days since 1 Jan), `GameTimeToString` (constant) | same | Same; base game time on the server clock |
| Utility | `Create{Alias,Bool,Float,Form,Int}Array`, `Resize*Array` (SKSE) | none | Not needed: FO4 arrays are dynamic (`new T[n]` with variable n, `Add`/`Remove` opcodes). Keep for compatibility |

### 3.2 FO4 natives the server must add

Priorities:
- **P0**: MVP. Needed to run our own gamemode scripts compiled with the FO4 compiler, plus the simplest vanilla object scripts (doors, activators, containers, triggers).
- **P1**: parity with SkyMP.
- **P2**: later (quests, workshop, AI).

**Infrastructure (P0).** These are VM and runtime features, not individual natives:
- FO4 PEX reader, `Var`, structs, the new opcodes, namespaced names (research doc §4.3).
- **Per-script-instance registration tables.** Timers keyed by (script instance, id). Remote-event registrations keyed by (receiver script, sender, event). Custom-event registrations. Hit-event filters (single-shot). Inventory filters.
- **Dispatch of remote events** to `Event <RootType>.<Name>(sender, …)` handlers.
- **Dispatch of custom events** to `Event <SenderScript>.<Name>(sender, Var[])` handlers.
- **Non-form ScriptObject handles**: `InputEnableLayer`, struct instances, aliases.

| Pri | Class | Natives |
|---|---|---|
| P0 | ScriptObject | `StartTimer`, `CancelTimer`, `StartTimerGameTime`, `CancelTimerGameTime`; `RegisterForRemoteEvent`, `UnregisterForRemoteEvent`, `UnregisterForAllRemoteEvents`; `RegisterForCustomEvent`, `UnregisterForCustomEvent`, `UnregisterForAllCustomEvents`, `SendCustomEvent(CustomEventName, Var[] = None)`; `AddInventoryEventFilter`, `RemoveInventoryEventFilter`, `RemoveAllInventoryEventFilters`; `RegisterForHitEvent`, `UnregisterForHitEvent`, `UnregisterForAllHitEvents`; `UnregisterForAllEvents`; `CastAs(string)`; `IsBoundGameObjectAvailable()`; `CallFunction(string, Var[]) [L]`, `CallFunctionNoWait`; `GetPropertyValue(string) [L]`, `SetPropertyValue(string, Var) [L]`, `SetPropertyValueNoWait`; `GetState`/`GotoState` (script-defined; VM `__state` support) |
| P0 | ObjectReference | AV API: `GetValue`, `SetValue`, `ModValue`, `DamageValue`, `RestoreValue`, `GetBaseValue`, `GetValuePercentage` (ActorValue forms). `GetCurrentLocation`, `GetLinkedRef(Keyword)`, `SetLinkedRef`, `GetRefsLinkedToMe`, `AddKeyword`, `RemoveKeyword`, `HasKeyword`, `Lock`, `IsLocked`, `GetLockLevel`, `SetLockLevel`, `GetContainer`, `WaitFor3DLoad [L]`, `EnableNoWait`, `DisableNoWait` (real), `IsCreated`, `GetActorRefOwner`, `SetActorRefOwner`, `SetFactionOwner`, `GetFactionOwner` |
| P0 | Actor | `GetFactionRank`, `SetFactionRank`, `ModFactionRank`, `RemoveFromAllFactions`, `AddPerk(Perk, bool)`, `RemovePerk`, `HasPerk`, `GetLevel`, `Kill(Actor = None)`, `KillSilent`, `Resurrect [L]`, `IsEssential`, `SetEssential`, `IsInCombat`, `EvaluatePackage` (forward to host), `StartCombat` / `StopCombat` (forward to host), `GetActorBase` [S] / `GetLeveledActorBase` |
| P0 | Game | `GetFormFromFile(int, string)` (ESL aware), `FindClosestReferenceOfType`, `FindClosestReferenceOfAnyTypeInList`, `FindRandomReferenceOfType`, `FindRandomReferenceOfAnyTypeInList`, `FindClosestActor`, `FindRandomActor` (the `…FromRef` variants are script-defined), `GetGameSettingFloat/Int/String` (from GMST), `GetHealthAV` / `GetStrengthAV` / … / `GetAggressionAV` (the 11 AV accessors), `GetCommonProperties` [S] (needs `GetFormFromFile`), `GetCaps` [S] (needs `GetForm`), `GetDifficulty`, `IsPluginInstalled`, `Warning`, `Error` |
| P0 | Utility | `CallGlobalFunction(string, string, Var[]) [L]`, `CallGlobalFunctionNoWait` |
| P0 | Math | all 13 natives (`abs`, `acos`, `asin`, `atan`, `Ceiling`, `cos`, `DegreesToRadians`, `Floor`, `pow`, `RadiansToDegrees`, `sin`, `sqrt`, `tan`). **Not implemented on the server today**. `Max`/`Min` are script-defined |
| P0 | GlobalVariable | `GetValue`, `SetValue` (+ script-defined `GetValueInt`, `SetValueInt`, `Mod`, `Value`). **Not implemented today** |
| P0 | Debug | `TraceStack`, `TraceUser`, `TraceFunction`, `OpenUserLog` (log only) |
| P0 | Quest | `GetCurrentStageID`, `SetCurrentStageID [L]`, `IsStageDone`, `IsRunning`, `Start [L]`, `Stop`, `IsCompleted`, `SetObjectiveDisplayed/Completed/Failed` (forward to clients) |
| P0 | FormList | `HasForm`, `AddForm`, `RemoveAddedForm`, `Revert` |
| P0 | InputEnableLayer | `Create`, `Delete`, `DisablePlayerControls`, `EnablePlayerControls`, `Reset`, `Enable*` / `Is*Enabled` (per-player handle, forwarded via SpSnippet) |
| P1 | ObjectReference (OMOD) | `AttachMod(ObjectMod, int = 0)`, `RemoveMod`, `RemoveAllMods`, `AttachModToInventoryItem(Form, ObjectMod)`, `RemoveModFromInventoryItem`, `RemoveAllModsFromInventoryItem`, F4SE `GetAllMods()` |
| P1 | ObjectReference (items) | `GetComponentCount(Form = None)`, `RemoveComponents(Component, int, bool) [L]`, `RemoveItemByComponent [L]`, `DropObject [L]`, `GetItemHealthPercent`, F4SE `GetInventoryItems() [L]` (returns `Form[]`), `GetInventoryWeight` |
| P1 | Actor | `GetEquippedWeapon(int = 0)`, `GetEquippedItemType(int)`, `UnequipItemSlot(int)`, `SwitchToPowerArmor(ObjectReference)`, `IsInPowerArmor` [S] (uses `HasPerk` on perk `0x1F8A9`), `SetRestrained`, `IsBleedingOut`, `SetNoBleedoutRecovery`, `ResetHealthAndLimbs`, `IsSneaking` / `IsRunning` / `IsSprinting` (state from movement), F4SE `GetWornItem(int, bool = false)` (returns the `WornItem` struct), `GetWornItemMods(int)` |
| P1 | MiscObject / Component / ConstructibleObject / ObjectMod | `GetObjectComponentCount(Component)`; F4SE `GetMiscComponents`, `GetConstructibleComponents`, `GetCreatedObject`, `GetWorkbenchKeyword`, `GetLooseMod`, `GetPropertyModifiers` (all esm-backed) |
| P1 | Weapon / InstanceData | `Weapon.GetAmmo()`; F4SE `InstanceData.GetAttackDamage/GetAmmoCapacity/GetDamageTypes/…` (esm + OMOD evaluation) |
| P1 | Message | `ShowAsHelpMessage`, `UnshowAsHelpMessage`, `ClearHelpMessages` [G] (forward) |
| P1 | ActiveMagicEffect | `GetTargetActor`, `GetCasterActor`, `Dispel`, `GetBaseObject` (chems/potions) |
| P1 | Location / Cell | `Location.IsChild`, `IsLoaded`, `HasCommonParent`, `GetKeywordData`, `IsCleared`, `SetCleared`; `Cell.IsLoaded`, `Get/SetActorOwner`, `Get/SetFactionOwner` |
| P1 | Terminal / Holotape | `Terminal.ShowOnPipboy` (forward); `OnMenuItemRun` dispatch |
| P1 | Form (F4SE) | `GetName` (FULL), `GetEditorID`, `GetKeywords`, `GetDescription`, `GetGoldValue` (vanilla), `HasKeywordInFormList` (vanilla) |
| P2 | Quest / Alias / ReferenceAlias / RefCollectionAlias / LocationAlias / Scene | full quest runtime (`GetAlias`, `ForceRefTo`, `GetReference`, `Clear`, `AddRef`, `GetAt`, `GetCount`, …); `Scene.Start/Stop/IsPlaying` |
| P2 | Keyword / Story | `SendStoryEvent`, `SendStoryEventAndWait [L]` |
| P2 | Workshop | `GetWorkshopOwnedObjects`, `GetWorkshopResourceObjects`, `IsPowered`, `HasSharedPowerGrid`, `StartWorkshop`, `IsWithinBuildableArea`, `RecalculateResources`, F4SE `AttachWire [L]` / `GetConnectPoints [L]` / `TransmitConnectedPower [L]` (broken on NG per F4SE notes [src: f4se/f4se_whatsnew.txt:50-54]) |
| P2 | Misc | `Weather.*` (server weather), `EncounterZone.GetActors/CountActors`, `Faction` crime API, `Actor.PathToReference [L]`, LOS/distance registrations |

Calls from our own gamemode that cannot run server-side are client-only and must be forwarded via SpSnippet, with the result routed back:
- UI: `Message.Show`, `Debug.Notification`, `Game.ShowRaceMenu`;
- camera: `Game.ForceThirdPerson`, `ShakeCamera`;
- input: `InputEnableLayer`;
- graphics: `EffectShader` / `VisualEffect` / `ImageSpaceModifier.Apply`, `Sound.Play`.

The serializer (`SpSnippetFunctionGen`) only handles forms (formId + type). It must be extended for `ActorValue` forms, which work as normal forms, and for non-form handles (IEL) and structs.

---

## 4. Events

### 4.1 FO4 event model vs Skyrim

| Aspect | Skyrim (what SkyMP's VM implements today) | Fallout 4 | Source |
|---|---|---|---|
| Root / registration scope | Registrations are per form (`RegisterForSingleUpdate` on Form) | `ScriptObject` root; **every registration is per script instance**. Other scripts on the same form do not see your events | [web: https://falloutck.uesp.net/wiki/Differences_from_Skyrim_to_Fallout_4] |
| Periodic logic | `RegisterForUpdate` / `RegisterForSingleUpdate` → `OnUpdate`; `…GameTime` → `OnUpdateGameTime` | **Removed.** `StartTimer(float, int id)` → `OnTimer(int)`; `StartTimerGameTime(hours, id)` → `OnTimerGameTime(int)`. Each start fires once; many timers per script by id; paused in menu mode; scaled by the VATS time multiplier. Implicit id 0 never starts on ObjectReference scripts | [web: https://falloutck.uesp.net/wiki/StartTimer_-_ScriptObject] |
| `OnHit` | Sent to every script on the target, 7 params | `ScriptObject` event with 9 params (`akTarget` first, `asMaterialName` last). **Requires `RegisterForHitEvent(target, filters…)`; single-shot**, so re-register inside the handler | [web: https://falloutck.uesp.net/wiki/OnHit_-_ScriptObject] |
| `OnMagicEffectApply` | automatic | `RegisterForMagicEffectApplyEvent`, single-shot | same page set |
| `OnItemAdded` / `OnItemRemoved` | automatic | **Only delivered to scripts that called `AddInventoryEventFilter`** (base form, keyword, component, FormList, or None = everything) | [web: https://falloutck.uesp.net/wiki/AddInventoryEventFilter_-_ScriptObject] |
| Equip events | `Actor.OnObjectEquipped/Unequipped` | `Actor.OnItemEquipped/OnItemUnequipped(Form akBaseObject, ObjectReference akReference)`; the item ref gets `OnEquipped/OnUnequipped(Actor)` | [src: f4se/scripts/vanilla/Actor.psc, ObjectReference.psc] |
| Triggers | `OnTriggerEnter`, `OnTriggerLeave`, `OnTrigger` (every frame) | `OnTriggerEnter`, `OnTriggerLeave` only | [src: vanilla/ObjectReference.psc] |
| Remote events | none | `RegisterForRemoteEvent(source, "OnX")`. Handler `Event <RootType>.OnX(<RootType> akSender, …original params)`; the type is the script where the event is *first* declared (e.g. `ObjectReference.OnItemAdded`, `Actor.OnDeath`, `ReferenceAlias.OnDeath`). Only game-originated events are relayed. Auto-unregistered when a quest restarts or an effect ends. The receiver is persisted, the sender is not | [web: https://falloutck.uesp.net/wiki/Remote_Papyrus_Event_Registration] |
| Custom events | none (Skyrim `ModEvent` is SKSE) | `CustomEvent Name` in the sender script; `SendCustomEvent("Name", Var[] args)` returns immediately; receivers call `RegisterForCustomEvent(sender, "Name")` and define `Event SenderScript.Name(SenderScript akSender, Var[] akArgs)`. Event names must be string literals | [web: https://falloutck.uesp.net/wiki/Custom_Papyrus_Events] |
| Declaring events | any script may declare a new `Event` | **Only native scripts may declare events**; the compiler rejects others | [web: https://falloutck.uesp.net/wiki/Differences_from_Skyrim_to_Fallout_4] |
| Animation events | `RegisterForAnimationEvent` on Form | on ScriptObject; the target's 3D must be loaded; `OnAnimationEventUnregistered` fires when the graph unloads | [web: https://falloutck.uesp.net/wiki/RegisterForAnimationEvent_-_ScriptObject] |
| Menu events | SKSE `RegisterForMenu` → `OnMenuOpen/OnMenuClose` | vanilla `RegisterForMenuOpenCloseEvent(name)` → `OnMenuOpenCloseEvent(string, bool)` | [src: vanilla/ScriptObject.psc] |
| Distance / LOS | LOS registrations (Skyrim) | `RegisterForDistanceLessThan/GreaterThanEvent` (single-shot, one per pair), `RegisterForDetection/DirectLOSGain/Lost` (single-shot, throttled) | [src: vanilla/ScriptObject.psc] |
| Input (script extender) | SKSE `RegisterForKey/Control` | F4SE `RegisterForKey` (DX scan code) → `OnKeyDown/Up`; `RegisterForControl` → `OnControlDown/Up`; `RegisterForGamepadButton`; `RegisterForCameraState` → `OnPlayerCameraState`; `RegisterForFurnitureEvent(Var)` → `OnFurnitureEvent`; `RegisterForExternalEvent(name, callbackFn)` calls a *function* | [src: f4se/scripts/modified/ScriptObject.psc] |
| Load game | `OnPlayerLoadGame` (player) | same; also on aliases/effects pointing at the player, or via remote registration | [web: https://falloutck.uesp.net/wiki/OnPlayerLoadGame_-_Actor] |
| New FO4-only events | — | workshop (`OnWorkshopMode`, `OnWorkshopObjectPlaced/Moved/Destroyed/Grabbed/Repaired`, `OnWorkshopNPCTransfer`), power (`OnPowerOn(ObjectReference akPowerGenerator)`, `OnPowerOff`), `OnCripple/OnPartialCripple(ActorValue, bool)`, `OnEnterBleedout`, `OnPlayerEnterVertibird`, `OnPlayerFireWeapon`, `OnPlayerModArmorWeapon`, `OnPlayerUseWorkBench`, `OnHolotapePlay/Chatter`, `OnRadiationDamage`, `OnLooksMenuEvent`, `OnTutorialEvent`, `Terminal.OnMenuItemRun` | [src: vanilla/*.psc] |

### 4.2 Every event in the FO4 native scripts

There are 135 events in this table plus the 37 `OnStory*` events listed below it. "auto" means the game sends the event to scripts attached to the form, and to aliases and active magic effects on it. `ActiveMagicEffect`, `ReferenceAlias` and `RefCollectionAlias` also receive every `ObjectReference` and `Actor` event; `RefCollectionAlias` adds a leading `ObjectReference akSenderRef` parameter [src: comparison of vanilla/ActiveMagicEffect.psc, ReferenceAlias.psc, RefCollectionAlias.psc].

The "SkyMP server today" column gives citations relative to `skymp5-server/cpp/server_guest_lib/`.

| Owner | Event (signature) | Registration / delivery | SkyMP server today | FalloutMP server plan |
|---|---|---|---|---|
| ScriptObject | `OnAnimationEvent(ObjectReference akSource, string asEventName)` | RegisterForAnimationEvent(akSender, name) - persists until Unregister/graph unload; target 3D must be loaded | — | P1: server already receives animation events (UpdateAnimation) → dispatch to RegisterForAnimationEvent registrants |
| ScriptObject | `OnAnimationEventUnregistered(ObjectReference akSource, string asEventName)` | auto, when a registered graph unloads | — | P1: when the sender despawns/unloads for everyone |
| ScriptObject | `OnBeginState(string asOldState)` | GotoState() | VM-internal | P0: FO4 GotoState is script code in ScriptObject.pex; VM must run it (see §5.1) |
| ScriptObject | `OnDistanceLessThan(ObjectReference akObj1, ObjectReference akObj2, float afDistance)` | RegisterForDistanceLessThanEvent(o1,o2,d) - single-shot, one per pair | — | P1: server-computable from positions (grid) |
| ScriptObject | `OnDistanceGreaterThan(ObjectReference akObj1, ObjectReference akObj2, float afDistance)` | RegisterForDistanceGreaterThanEvent(o1,o2,d) - single-shot, one per pair | — | P1: server-computable from positions (grid) |
| ScriptObject | `OnEndState(string asNewState)` | GotoState() | VM-internal | P0 (as above) |
| ScriptObject | `OnGainLOS(ObjectReference akViewer, ObjectReference akTarget)` | RegisterForDetectionLOSGain / RegisterForDirectLOSGain - single-shot, throttled | — | P2: needs a client raycast report; approximate with distance |
| ScriptObject | `OnHit(ObjectReference akTarget, ObjectReference akAggressor, Form akSource, Projectile akProjectile, bool abPowerAttack, bool abSneakAttack, bool abBashAttack, bool abHitBlocked, string asMaterialName)` | RegisterForHitEvent(target, filters...) - SINGLE-SHOT, per script; re-register in handler | fired unconditionally with the Skyrim 7-arg signature [src: skymp5-server/cpp/server_guest_lib/ActionListener.cpp:1410-1424] | P0 CHANGE: deliver only to scripts holding a matching RegisterForHitEvent registration (aggressor/source/projectile/power/sneak/bash/block filters, abMatch), FO4 9-arg order (akTarget first, asMaterialName last), then drop the registration |
| ScriptObject | `OnInit()` | auto (after properties are filled) | fired once on first player subscription [src: skymp5-server/cpp/server_guest_lib/MpObjectReference.cpp:989] | P0 keep; also after `PlaceAtMe` of scripted forms |
| ScriptObject | `OnLostLOS(ObjectReference akViewer, ObjectReference akTarget)` | RegisterForDetectionLOSLost / RegisterForDirectLOSLost - single-shot, throttled | — | P2 (as above) |
| ScriptObject | `OnMagicEffectApply(ObjectReference akTarget, ObjectReference akCaster, MagicEffect akEffect)` | RegisterForMagicEffectApplyEvent(target, filters) - single-shot | — | P2 (needs server magic effects) |
| ScriptObject | `OnMenuOpenCloseEvent(string asMenuName, bool abOpening)` | RegisterForMenuOpenCloseEvent(menuName) - persistent | — | P2: needs a client menu report message (today only race-menu state reaches the server) |
| ScriptObject | `OnPlayerSleepStart(float afSleepStartTime, float afDesiredSleepEndTime, ObjectReference akBed)` | RegisterForPlayerSleep | — | drop (time is server-wide; sleep/wait disabled) |
| ScriptObject | `OnPlayerSleepStop(bool abInterrupted, ObjectReference akBed)` | RegisterForPlayerSleep | — | drop |
| ScriptObject | `OnPlayerTeleport()` | RegisterForPlayerTeleport | — | P1: fire on server Teleport/MoveTo of a player |
| ScriptObject | `OnPlayerWaitStart(float afWaitStartTime, float afDesiredWaitEndTime)` | RegisterForPlayerWait | — | drop |
| ScriptObject | `OnPlayerWaitStop(bool abInterrupted)` | RegisterForPlayerWait | — | drop |
| ScriptObject | `OnRadiationDamage(ObjectReference akTarget, bool abIngested)` | RegisterForRadiationDamageEvent(target) - single-shot | — | P1 once rads are server-side |
| ScriptObject | `OnTimer(int aiTimerID)` | StartTimer(sec, id) - fires once per call; real time, paused in menus, scaled by VATS; ObjectReference scripts must pass explicit id | — | P0 NEW: per-script timer table (script instance, id) → OnTimer; CancelTimer |
| ScriptObject | `OnTimerGameTime(int aiTimerID)` | StartTimerGameTime(hours, id) - fires once; not sent in menu mode | — | P0 NEW: same on server game clock (today WaitGameTime uses 1 game hour = 60 s [src: script_classes/PapyrusUtility.cpp:50-58]) |
| ScriptObject | `OnTrackedStatsEvent(string arStatName, int aiStatValue)` | RegisterForTrackedStatsEvent(stat, threshold) - single-shot | — | drop / P2 |
| ScriptObject | `OnLooksMenuEvent(int aiFlavor)` | RegisterForLooksMenuEvent | — | drop (client UX) |
| ScriptObject | `OnTutorialEvent(String asEventName, Message aMessage)` | RegisterForTutorialEvent(name) | — | drop |
| ScriptObject | `OnKeyDown(int keyCode)` | F4SE RegisterForKey(dxScanCode) | — | client-only (F4SE); relay via custom packet if a gamemode needs it |
| ScriptObject | `OnKeyUp(int keyCode, float time)` | F4SE RegisterForKey(dxScanCode) | — | client-only |
| ScriptObject | `OnControlDown(string control)` | F4SE RegisterForControl(name) | — | client-only |
| ScriptObject | `OnControlUp(string control, float time)` | F4SE RegisterForControl(name) | — | client-only |
| ScriptObject | `OnPlayerCameraState(int oldState, int newState)` | F4SE RegisterForCameraState() | — | client-only |
| ScriptObject | `OnFurnitureEvent(Actor akActor, ObjectReference akFurniture, bool isGettingUp)` | F4SE RegisterForFurnitureEvent(Var filter) | — | client-only |
| ScriptObject | `OnGamepadButtonDown(int keyCode)` | F4SE RegisterForGamepadButton(xinput) | — | client-only |
| ScriptObject | `OnGamepadButtonUp(int keyCode, float time)` | F4SE RegisterForGamepadButton(xinput) | — | client-only |
| ObjectReference | `OnActivate(ObjectReference akActionRef)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnActivate") | fired after gamemode `onActivate` [src: MpObjectReference.cpp:502] | P0 keep |
| ObjectReference | `OnCellAttach()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnCellAttach") | — | P1: first/last player subscription to the ref |
| ObjectReference | `OnCellDetach()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnCellDetach") | — | P1 (as above) |
| ObjectReference | `OnCellLoad()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnCellLoad") | fired once, with OnInit [src: MpObjectReference.cpp:990] | P1: per load, not once |
| ObjectReference | `OnClose(ObjectReference akActionRef)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnClose") | — | P1 (as OnOpen) |
| ObjectReference | `OnContainerChanged(ObjectReference akNewContainer, ObjectReference akOldContainer)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnContainerChanged") | — | P1 (only meaningful for persistent item refs) |
| ObjectReference | `OnDestructionStageChanged(int aiOldStage, int aiCurrentStage)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnDestructionStageChanged") | — | P2 |
| ObjectReference | `OnEquipped(Actor akActor)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnEquipped") | — | P1: send to persistent item refs |
| ObjectReference | `OnExitFurniture(ObjectReference akActionRef)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnExitFurniture") | — | P1: SkyMP already tracks furniture via second activation (`SkympOnActivateClose`) |
| ObjectReference | `OnGrab()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnGrab") | — | P2 (client grab) |
| ObjectReference | `OnHolotapeChatter(string astrChatter, float afNumericData)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnHolotapeChatter") | — | P2 |
| ObjectReference | `OnHolotapePlay(ObjectReference aTerminalRef)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnHolotapePlay") | — | P2 |
| ObjectReference | `OnItemAdded(Form akBaseItem, int aiItemCount, ObjectReference akItemReference, ObjectReference akSourceContainer)` | needs AddInventoryEventFilter(filter or None) on the receiving script | fired unconditionally; akSourceContainer/akItemReference always None (bulk `AddItems` sends nothing) [src: MpObjectReference.cpp:817-838,859-863] | P0 CHANGE: only to scripts whose AddInventoryEventFilter list matches (base form / keyword / component / FormList / None=all); fill akSourceContainer |
| ObjectReference | `OnItemRemoved(Form akBaseItem, int aiItemCount, ObjectReference akItemReference, ObjectReference akDestContainer)` | needs AddInventoryEventFilter(filter or None) on the receiving script | — | P0 NEW: same filtering; fill akDestContainer |
| ObjectReference | `OnLoad()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnLoad") | fired once, with OnInit [src: MpObjectReference.cpp:991] | P1: per load, not once |
| ObjectReference | `OnLockStateChanged()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnLockStateChanged") | — | P2 |
| ObjectReference | `OnOpen(ObjectReference akActionRef)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnOpen") | — | P1: on server SetOpen / door activation (server tracks isOpen) |
| ObjectReference | `OnPipboyRadioDetection(bool abDetected)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPipboyRadioDetection") | — | drop |
| ObjectReference | `OnPlayerDialogueTarget()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPlayerDialogueTarget") | — | P2 |
| ObjectReference | `OnPowerOn(ObjectReference akPowerGenerator)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPowerOn") | — | P2 (server workshop power grid) |
| ObjectReference | `OnPowerOff()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPowerOff") | — | P2 |
| ObjectReference | `OnRead()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnRead") | — | P2 |
| ObjectReference | `OnRelease()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnRelease") | — | P2 |
| ObjectReference | `OnReset()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnReset") | — | P2 |
| ObjectReference | `OnSell(Actor akSeller)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnSell") | — | P2 (barter) |
| ObjectReference | `OnSpellCast(Form akSpell)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnSpellCast") | fired from SpellCast message [src: ActionListener.cpp:1189] | P2 (FO4 rarely uses spells; chems use OnItemEquipped) |
| ObjectReference | `OnTranslationAlmostComplete()` | after TranslateTo/SplineTranslateTo | — | P2 (only if server emulates TranslateTo) |
| ObjectReference | `OnTranslationComplete()` | after TranslateTo/SplineTranslateTo | — | P2 |
| ObjectReference | `OnTranslationFailed()` | after StopTranslation | — | P2 |
| ObjectReference | `OnTrapHitStart(ObjectReference akTarget, float afXVel, float afYVel, float afZVel, float afXPos, float afYPos, float afZPos, int aeMaterial, bool abInitialHit, int aeMotionType)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnTrapHitStart") | — | drop/P2 |
| ObjectReference | `OnTrapHitStop(ObjectReference akTarget)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnTrapHitStop") | — | drop/P2 |
| ObjectReference | `OnTriggerEnter(ObjectReference akActionRef)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnTriggerEnter") | fired from geo primitives [src: MpObjectReference.cpp:576] | P1 keep. FO4 has no `OnTrigger`; stop firing it [src: MpObjectReference.cpp:595] |
| ObjectReference | `OnTriggerLeave(ObjectReference akActionRef)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnTriggerLeave") | fired from geo primitives [src: MpObjectReference.cpp:576] | P1 keep |
| ObjectReference | `OnUnequipped(Actor akActor)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnUnequipped") | — | P1 |
| ObjectReference | `OnUnload()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnUnload") | — | P1 NEW |
| ObjectReference | `OnWorkshopMode(bool aStart)` | sent to workshop ref | — | P2 (server-side workshop) |
| ObjectReference | `OnWorkshopObjectDestroyed(ObjectReference akReference)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnWorkshopObjectDestroyed") | — | P2 |
| ObjectReference | `OnWorkshopObjectGrabbed(ObjectReference akReference)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnWorkshopObjectGrabbed") | — | P2 |
| ObjectReference | `OnWorkshopObjectMoved(ObjectReference akReference)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnWorkshopObjectMoved") | — | P2 |
| ObjectReference | `OnWorkshopObjectPlaced(ObjectReference akReference)` | sent to workshop ref AND placed ref | — | P2 |
| ObjectReference | `OnWorkshopObjectRepaired(ObjectReference akReference)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnWorkshopObjectRepaired") | — | P2 |
| ObjectReference | `OnWorkshopNPCTransfer(Location akNewWorkshop, Keyword akActionKW)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnWorkshopNPCTransfer") | — | P2 |
| Actor | `OnCombatStateChanged(Actor akTarget, int aeCombatState)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnCombatStateChanged") | — | P2 (from hosting client) |
| Actor | `OnCommandModeCompleteCommand(int aeCommandType, ObjectReference akTarget)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnCommandModeCompleteCommand") | — | P2 (companions) |
| Actor | `OnCommandModeEnter()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnCommandModeEnter") | — | P2 |
| Actor | `OnCommandModeExit()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnCommandModeExit") | — | P2 |
| Actor | `OnCommandModeGiveCommand(int aeCommandType, ObjectReference akTarget)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnCommandModeGiveCommand") | — | P2 |
| Actor | `OnCompanionDismiss()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnCompanionDismiss") | — | P2 |
| Actor | `OnConsciousnessStateChanged(bool abUnconscious)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnConsciousnessStateChanged") | — | P2 |
| Actor | `OnCripple(ActorValue akActorValue, bool abCrippled)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnCripple") | — | P1 if limb damage is server-side |
| Actor | `OnDeferredKill(Actor akKiller)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnDeferredKill") | — | P2 |
| Actor | `OnDeath(Actor akKiller)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnDeath") | — | P0 NEW: fire when server sets isDead (gamemode `onDeath` exists, Papyrus event does not) |
| Actor | `OnDifficultyChanged(int aOldDifficulty, int aNewDifficulty)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnDifficultyChanged") | — | drop |
| Actor | `OnDying(Actor akKiller)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnDying") | — | P0 NEW: fire just before OnDeath |
| Actor | `OnEnterBleedout()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnEnterBleedout") | — | P1 (downed/essential state) |
| Actor | `OnEnterSneaking()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnEnterSneaking") | — | P2 (from movement isSneaking) |
| Actor | `OnEscortWaitStart()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnEscortWaitStart") | — | drop |
| Actor | `OnEscortWaitStop()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnEscortWaitStop") | — | drop |
| Actor | `OnGetUp(ObjectReference akFurniture)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnGetUp") | — | P1 (furniture) |
| Actor | `OnItemEquipped(Form akBaseObject, ObjectReference akReference)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnItemEquipped") | Skyrim name `OnObjectEquipped(Form, None)` fired [src: MpActor.cpp:550] | P0 RENAME: `OnObjectEquipped` → `OnItemEquipped(Form akBaseObject, ObjectReference akReference)` |
| Actor | `OnItemUnequipped(Form akBaseObject, ObjectReference akReference)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnItemUnequipped") | — | P0 NEW |
| Actor | `OnKill(Actor akVictim)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnKill") | — | P0 NEW: on the killer |
| Actor | `OnLocationChange(Location akOldLoc, Location akNewLoc)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnLocationChange") | — | P1: needs LCTN lookup from cell/worldspace data |
| Actor | `OnPackageChange(Package akOldPackage)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPackageChange") | — | P2 (NPC AI on host) |
| Actor | `OnPackageEnd(Package akOldPackage)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPackageEnd") | — | P2 |
| Actor | `OnPackageStart(Package akNewPackage)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPackageStart") | — | P2 |
| Actor | `OnPartialCripple(ActorValue akActorValue, bool abCrippled)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPartialCripple") | — | P2 |
| Actor | `OnPickpocketFailed()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPickpocketFailed") | — | drop |
| Actor | `OnPlayerCreateRobot(Actor akNewRobot)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPlayerCreateRobot") | — | P2 |
| Actor | `OnPlayerEnterVertibird(ObjectReference akVertibird)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPlayerEnterVertibird") | — | P2 |
| Actor | `OnPlayerFallLongDistance(float afDamage)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPlayerFallLongDistance") | — | P2 (from client fall damage) |
| Actor | `OnPlayerFireWeapon(Form akBaseObject)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPlayerFireWeapon") | — | P1: from the new WeaponFire message |
| Actor | `OnPlayerHealTeammate(Actor akTeammate)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPlayerHealTeammate") | — | P2 |
| Actor | `OnPlayerLoadGame()` | player actor only (or alias/effect on player) | — | P1: fire on player spawn/reconnect (no save loads in MP) |
| Actor | `OnPlayerModArmorWeapon(Form akBaseObject, ObjectMod akModBaseObject)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPlayerModArmorWeapon") | — | P1: from the server-validated ModItem message |
| Actor | `OnPlayerModRobot(Actor akRobot, ObjectMod akModBaseObject)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPlayerModRobot") | — | P2 |
| Actor | `OnPlayerSwimming()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPlayerSwimming") | — | P2 |
| Actor | `OnPlayerUseWorkBench(ObjectReference akWorkBench)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPlayerUseWorkBench") | — | P1: from workbench activation |
| Actor | `OnRaceSwitchComplete()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnRaceSwitchComplete") | — | P2 |
| Actor | `OnSit(ObjectReference akFurniture)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnSit") | — | P1 (furniture; note power-armor triple fire) |
| Actor | `OnSpeechChallengeAvailable(ObjectReference akSpeaker)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnSpeechChallengeAvailable") | — | drop |
| ActiveMagicEffect | `OnEffectStart(Actor akTarget, Actor akCaster)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnEffectStart") | — | P1 (server ActiveMagicEffectsMap exists for potions; no script events yet) |
| ActiveMagicEffect | `OnEffectFinish(Actor akTarget, Actor akCaster)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnEffectFinish") | — | P1 |
| Alias | `OnAliasInit()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnAliasInit") | — | P2 (quests out of scope early) |
| Alias | `OnAliasReset()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnAliasReset") | — | P2 |
| Alias | `OnAliasShutdown()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnAliasShutdown") | — | P2 |
| Location | `OnLocationCleared()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnLocationCleared") | — | P2 |
| Location | `OnLocationLoaded()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnLocationLoaded") | — | P2 |
| Package | `OnStart(Actor akActor)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnStart") | — | P2 (AI packages) |
| Package | `OnEnd(Actor akActor)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnEnd") | — | P2 (AI packages) |
| Package | `OnChange(Actor akActor)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnChange") | — | P2 (AI packages) |
| Perk | `OnEntryRun(int auiEntryID, ObjectReference akTarget, Actor akOwner)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnEntryRun") | — | P2 |
| Quest | `OnQuestInit()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnQuestInit") | — | P2 |
| Quest | `OnQuestShutdown()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnQuestShutdown") | — | P2 |
| Quest | `OnReset()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnReset") | — | P2 |
| Quest | `OnStageSet(int auiStageID, int auiItemID)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnStageSet") | — | P2 (needed once quests run server-side) |
| Scene | `OnAction(int auiActionID, ReferenceAlias akAlias)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnAction") | — | P2 |
| Scene | `OnBegin()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnBegin") | — | P2 (scenes) |
| Scene | `OnEnd()` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnEnd") | — | P2 (scenes) |
| Scene | `OnPhaseBegin(int auiPhaseIndex)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPhaseBegin") | — | P2 |
| Scene | `OnPhaseEnd(int auiPhaseIndex)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnPhaseEnd") | — | P2 |
| Terminal | `OnMenuItemRun(int auiMenuItemID, ObjectReference akTerminalRef)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnMenuItemRun") | — | P1: terminals (server-validated terminal menus) |
| TopicInfo | `OnBegin(ObjectReference akSpeakerRef, bool abHasBeenSaid)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnBegin") | — | P2 (dialogue) |
| TopicInfo | `OnEnd(ObjectReference akSpeakerRef, bool abHasBeenSaid)` | auto to attached scripts; remote: RegisterForRemoteEvent(src, "OnEnd") | — | P2 (dialogue) |

**Quest story-manager events** (all auto on quests started by the Story Manager; server plan: **P2 / not emulated**; SkyMP does not fire them):

```papyrus
Event OnStoryActivateActor(Location akLocation, ObjectReference akActor)
Event OnStoryActorAttach(ObjectReference akActor, Location akLocation)
Event OnStoryAddToPlayer(ObjectReference akOwner, ObjectReference akContainer, Location akLocation, Form akItemBase, int aiAcquireType)
Event OnStoryArrest(ObjectReference akArrestingGuard, ObjectReference akCriminal, Location akLocation, int aiCrime)
Event OnStoryAssaultActor(ObjectReference akVictim, ObjectReference akAttacker, Location akLocation, int aiCrime)
Event OnStoryAttractionObject(ObjectReference akActor, ObjectReference akObject, Location akLocation, bool abCommanded)
Event OnStoryBribeNPC(ObjectReference akActor)
Event OnStoryCastMagic(ObjectReference akCastingActor, ObjectReference akSpellTarget, Location akLocation, Form akSpell)
Event OnStoryChangeLocation(ObjectReference akActor, Location akOldLocation, Location akNewLocation)
Event OnStoryClearLocation(Location akOldLocation)
Event OnStoryCraftItem(ObjectReference akBench, Location akLocation, Form akCreatedItem)
Event OnStoryCrimeGold(ObjectReference akVictim, ObjectReference akCriminal, Form akFaction, int aiGoldAmount, int aiCrime)
Event OnStoryCure(Form akInfection)
Event OnStoryDialogue(Location akLocation, ObjectReference akActor1, ObjectReference akActor2)
Event OnStoryDiscoverDeadBody(ObjectReference akActor, ObjectReference akDeadActor, Location akLocation)
Event OnStoryEscapeJail(Location akLocation, Form akCrimeGroup)
Event OnStoryFlatterNPC(ObjectReference akActor)
Event OnStoryHackTerminal(ObjectReference akComputer, bool abSucceeded)
Event OnStoryHello(Location akLocation, ObjectReference akActor1, ObjectReference akActor2)
Event OnStoryIncreaseLevel(int aiNewLevel)
Event OnStoryInfection(ObjectReference akTransmittingActor, Form akInfection)
Event OnStoryIntimidateNPC(ObjectReference akActor)
Event OnStoryIronSights(ObjectReference akActor, Form akWeapon)
Event OnStoryJail(ObjectReference akGuard, Form akCrimeGroup, Location akLocation, int aiCrimeGold)
Event OnStoryKillActor(ObjectReference akVictim, ObjectReference akKiller, Location akLocation, int aiCrimeStatus, int aiRelationshipRank)
Event OnStoryLocationLoaded(Location akLocation)
Event OnStoryMineExplosion(ObjectReference akVictim, ObjectReference akAttacker)
Event OnStoryNewVoicePower(ObjectReference akActor, Form akVoicePower)
Event OnStoryPickLock(ObjectReference akActor, ObjectReference akLock)
Event OnStoryPickPocket(ObjectReference akVictim, bool abSuccess)
Event OnStoryPayFine(ObjectReference akCriminal, ObjectReference akGuard, Form akCrimeGroup, int aiCrimeGold)
Event OnStoryPlayerGetsFavor(ObjectReference akActor)
Event OnStoryRelationshipChange(ObjectReference akActor1, ObjectReference akActor2, int aiOldRelationship, int aiNewRelationship)
Event OnStoryRemoveFromPlayer(ObjectReference akOwner, ObjectReference akItem, Location akLocation, Form akItemBase, int aiRemoveType)
Event OnStoryScript(Keyword akKeyword, Location akLocation, ObjectReference akRef1, ObjectReference akRef2, int aiValue1, int aiValue2)
Event OnStoryServedTime(Location akLocation, Form akCrimeGroup, int aiCrimeGold, int aiDaysJail)
Event OnStoryTrespass(ObjectReference akVictim, ObjectReference akTrespasser, Location akLocation, int aiCrime)
```

**Custom events defined by vanilla gameplay scripts.** These matter if workshop or follower content runs server-side:
- `WorkshopParentScript` defines 15: `WorkshopObjectBuilt`, `WorkshopObjectMoved`, `WorkshopDailyUpdate`, … (full list in §1.9).
- `FollowersScript` defines 5: `CompanionChange`, `Loitering`, `AffinityEvent`, `PossibleMurderEvent`, `AutonomyDisallowed`.
- `TeleportActorScript` defines 1: `TeleportDone`.

### 4.3 What the FalloutMP server must emulate

**P0** (without these, FO4-compiled scripts misbehave):
- `OnInit`.
- `OnActivate`.
- `OnTimer` / `OnTimerGameTime`.
- `OnHit` with registration and filter semantics (stop the unconditional 7-arg send at `ActionListener.cpp:1424`).
- `OnItemAdded` / `OnItemRemoved` with inventory-filter semantics. Also fire them from bulk `AddItems` (today they are commented out at `MpObjectReference.cpp:859-863`).
- `OnItemEquipped` / `OnItemUnequipped`, replacing `OnObjectEquipped` at `MpActor.cpp:550`.
- `OnDying` / `OnDeath` / `OnKill`.
- Remote-event relay for every event the server fires.
- Custom-event relay.
- `OnBeginState` / `OnEndState` through FO4 `GotoState`.

**P1:**
- Per-load `OnLoad` / `OnUnload` / `OnCellAttach` / `OnCellDetach` (today `OnInit` + `OnCellLoad` + `OnLoad` fire once at first subscription, `MpObjectReference.cpp:989-991`).
- `OnOpen` / `OnClose`.
- `OnTriggerEnter` / `OnTriggerLeave`. Stop sending `OnTrigger` (`MpObjectReference.cpp:595`).
- `OnSit` / `OnGetUp` / `OnExitFurniture`.
- `OnAnimationEvent`, from the animation stream the server already receives.
- `OnDistanceLessThan` / `OnDistanceGreaterThan`.
- `OnPlayerTeleport`.
- `OnPlayerLoadGame`, mapped to player spawn.
- `OnPlayerFireWeapon`, `OnPlayerModArmorWeapon`, `OnPlayerUseWorkBench`.
- `OnLocationChange`.
- `OnEnterBleedout`, `OnCripple`, `OnRadiationDamage`.
- `OnEffectStart` / `OnEffectFinish`.
- `Terminal.OnMenuItemRun`.

**P2:** workshop and power events, quest/alias/scene/package/topic events, LOS, menu events, combat state, and the story manager.

**Drop:** sleep/wait, difficulty, escort, pickpocket, tutorial and LooksMenu events, and the F4SE input events (client-only).

**SkyMP-specific events:**
- `SkympOnActivateClose` (`MpObjectReference.cpp:481`) must be declared in a **native** stub script, because FO4 forbids event declarations in non-native scripts. Alternatively, replace it with a custom event sent from a FalloutMP helper script.
- Gamemode JS can also fire any Papyrus event via `onPapyrusEvent:<name>` gamemode events [src: gamemode_events/PapyrusEventEvent.cpp:19,49].

---

## 5. Semantic gotchas for porting

### 5.1 `ScriptObject` is the root
- `Form`, `Alias`, `ActiveMagicEffect` and `InputEnableLayer` all extend `ScriptObject` [src: f4se/scripts/vanilla/*.psc headers].
- Registrations, timers and inventory filters belong to the **script instance**. Two scripts on one ref each need their own.
- `IsBoundGameObjectAvailable()` tells a script whether its form still exists, for example after the form moved into a container.
- `GetState` / `GotoState` are script-defined in `ScriptObject.psc`. They use the `__state` intrinsic and call `OnEndState` / `OnBeginState`.
- SkyMP's VM special-cases these two names when it looks up functions [src: papyrus-vm/src/papyrus-vm-lib/VirtualMachine.cpp:280]. Verify this still works when only `ScriptObject.pex` defines them [inference].
- Native lookup walks the `.pex` parent chain, so a `ScriptObject.pex` stub is mandatory (§3.0).

### 5.2 Actor values are forms (AVIF)
- Every AV function takes `ActorValue` (an AVIF record) and lives on **ObjectReference**: `GetValue`, `SetValue`, `ModValue`, `DamageValue`, `RestoreValue`, `GetBaseValue`, `GetValuePercentage` (0..1).
- Ways to obtain the forms:
  - script properties;
  - `Game.GetHealthAV()` and the other 10 accessors (Agility, Charisma, Endurance, Intelligence, Luck, Perception, Strength, Aggression, Confidence, Suspicious) [src: vanilla/Game.psc];
  - `Game.GetFormFromFile(id, "Fallout4.esm")`;
  - natively, `RE::ActorValue::GetSingleton()->health` and other named fields such as `actionPoints`, `carryWeight`, `aggression`, `attackDamageMult`, `rads`, `radHealthMax` [src: libxse include/RE/A/ActorValue.h:52-135], or `TESForm::GetFormByEditorID` for any AVIF (`Variable01`…`Variable10` [inference]).
- Recommended: resolve AV forms by EditorID once at startup (client and server) and keep a name → form map, so SkyMP's string AV names keep working through a shim.

| Skyrim AV string used by SkyMP | FO4 AVIF | Notes |
|---|---|---|
| `health` | `Health` (`Game.GetHealthAV()`) | |
| `stamina` | `ActionPoints` (gameplay equivalent). An engine `Stamina` AV also exists in the CommonLibF4 table | Server `ActorValues.h` must change |
| `magicka` | none | drop |
| `CarryWeight` | `CarryWeight` | |
| `Variable10` | `Variable10` [inference] | used as an animation-state flag (`sync/animation.ts:168`) |
| `Aggression` | `Aggression` (`Game.GetAggressionAV()`) | |
| `attackDamageMult` | `AttackDamageMult` | |

### 5.3 Keyword-based logic
FO4 content selects things by keyword far more often than Skyrim does:
- weapon types (`WeaponType*`), object types (`ObjectType*`), actor types;
- OMOD attach points (`ap_*`) and instance naming;
- location types (workshop settlements);
- linked refs (`GetLinkedRef(Keyword)` is used everywhere in workshop content). The server must honour the keyword argument (§3.1).

Filter parameters accept keywords and FormLists: `RegisterForHitEvent`, `AddInventoryEventFilter`, `FindAllReferencesWithKeyword`, `HasKeywordInFormList`.

### 5.4 Forms, ids and plugins
- **Use `Game.GetFormFromFile(int aiFormID, string asFilename)` instead of hard-coded load-order ids.** The top byte is ignored, and light (ESL) plugins are supported; F4SE 0.6.22 fixed handle resolution for ESL refs [src: f4se/f4se_whatsnew.txt:62-63].
- `Game.GetCaps()` is `GetForm(0xF)` (Caps001) [src: vanilla/Game.psc:104].
- `Game.GetCommonProperties()` is `GetFormFromFile(0x000A7D73, "Fallout4.esm")` [src: vanilla/Game.psc:112].
- `DefaultObject` (DFOB forms) exposes engine-referenced forms. F4SE adds `DefaultObject.GetDefaultObject(string editorId)` and `Get` / `Set` [src: f4se/scripts/modified/DefaultObject.psc]. It replaces Skyrim's `DefaultObjectManager` SP class.

### 5.5 Form type numbering differs
SP's `FormType` enum is Skyrim's. Regenerate a FO4 const enum from `libxse include/RE/E/ENUM_FORM_ID.h`.

| Type | Skyrim SP value | FO4 `ENUM_FORM_ID` |
|---|---|---|
| Keyword | 4 | 0x04 |
| TextureSet | 7 | 0x09 |
| Global | 9 | 0x0B |
| HeadPart | 12 | 0x0F |
| Race | 14 | 0x11 |
| Spell | 22 | 0x19 |
| Scroll | 23 | 0x1A |
| Activator | 24 | 0x1B |
| Armor | 26 | 0x1D |
| Book | 27 | 0x1E |
| Container | 28 | 0x1F |
| Door | 29 | 0x20 |
| Ingredient | 30 | 0x21 |
| Light | 31 | 0x22 |
| Misc | 32 | 0x23 |
| Static | 34 | 0x24 |
| MovableStatic | 36 | 0x26 |
| Tree | 38 | 0x28 |
| Flora | 39 | 0x29 |
| Furniture | 40 | 0x2A |
| Weapon | 41 | 0x2B |
| Ammo | 42 | 0x2C |
| NPC | 43 | 0x2D |
| Key | 45 | 0x2F |
| Potion | 46 | 0x30 (ALCH) |
| Note / Holotape | 48 | 0x32 |
| SoulGem | 52 | 0x36 |
| Cell | 60 | 0x3F |
| Reference | 61 | 0x40 |
| Actor | 62 | 0x41 |
| WorldSpace | 71 | 0x4A |
| Perk | 92 | 0x5F |

New in FO4:
- `CMPO` 0x08, `TERM` 0x37, `AVIF` 0x62, `OMOD` 0x90, `COBJ` 0x8F, `MSWP` 0x91, `INNR` 0x93, `EQUP` 0x7C, `DFOB` 0x6F, `LCTN` 0x6B, `MESG` 0x6C, `FLST` 0x5E, `QUST` 0x50.
- The server's `Form.GetType` table [src: script_classes/PapyrusForm.cpp:187-260] is Skyrim-only.

### 5.6 Menu names differ
Menu names verified for FO4 [src: f4se/f4se/GameMenus.h:106-137 (flag table); libxse `MENU_NAME` constants]:

`BarterMenu`, `BookMenu`, `Console`, `ContainerMenu`, `CookingMenu`, `CursorMenu`, `DialogueMenu`, `ExamineConfirmMenu`, `ExamineMenu`, `FaderMenu`, `FavoritesMenu`, `HUDMenu`, `HolotapeMenu`, `LevelUpMenu`, `LoadingMenu`, `LockpickingMenu`, `LooksMenu`, `MainMenu`, `MessageBoxMenu`, `PauseMenu`, `PipboyHolotapeMenu`, `PipboyMenu`, `PowerArmorHUDMenu`, `PowerArmorModMenu`, `PromptMenu`, `RobotModMenu`, `SitWaitMenu`, `SleepWaitMenu`, `SPECIALMenu`, `TerminalHolotapeMenu`, `TerminalMenu`, `TerminalMenuButtons`, `VATSMenu`, `VignetteMenu`, `WorkshopMenu`.

Mapping of the Skyrim names the client uses:

| Skyrim name | FO4 name |
|---|---|
| `RaceSex Menu` | `LooksMenu` |
| `InventoryMenu` | `PipboyMenu` (inventory is a Pip-Boy tab) |
| `MagicMenu` | none |
| `Crafting Menu` | `ExamineMenu` (weapon/armor bench), `CookingMenu`, `WorkshopMenu`, or `PowerArmorModMenu` / `RobotModMenu` |
| `Journal Menu` | `PauseMenu` |
| `MapMenu` / `StatsMenu` | `PipboyMenu` |
| `Loading Menu` | `LoadingMenu` |
| `Main Menu` | `MainMenu` |
| `HUD Menu` | `HUDMenu` |
| `Lockpicking Menu` | `LockpickingMenu` |
| `Book Menu` | `BookMenu` |
| `TweenMenu`, `GiftMenu` | none |
| `ContainerMenu`, `BarterMenu`, `FavoritesMenu`, `MessageBoxMenu`, `Console` | unchanged |

### 5.7 Other constant tables that differ
- **Motion types:** FO4 `Motion_Fixed=0`, `Motion_Dynamic=1`, `Motion_Keyframed=2`. SP uses `Keyframed=4` (§2.3.3).
- **Camera states:** F4SE `GetCameraState` table [src: modified/Game.psc:28-42].
- **Sit states:** 0, 2, 3, 4.
- **Equipped item types:** 9 Gun, 10 Grenade, 11 Mine.
- **Equip indices:** `GetEquippedWeapon(int aiEquipIndex = 0)` takes an index instead of `abLeftHand`.
- **Biped slots:** 30–61. F4SE gives masks `kSlotMask30..61` and `Form.GetMaskForSlot` [src: modified/Form.psc].
- **`ShowRaceMenu` uiMode:** 0–4.

### 5.8 Struct-returning and struct-taking natives
- Structs are **reference types**, passed by reference. They hold only primitives and forms, never arrays, `Var` or other structs [web: https://falloutck.uesp.net/wiki/Struct_Reference].
- SP3 marshalling and `papyrus-vm` `VarValue` both need struct support.
- Natives that return or take structs:
  - `Actor.GetWornItem(int slotIndex, bool firstPerson = false)` → `Actor:WornItem {Form item; Form model; string modelName; Form materialSwap; TextureSet texture}` [F4SE]
  - `ActorBase.Get/SetBodyWeight` → `BodyWeight {thin, muscular, large}` [F4SE]
  - `Game.GetInstalledPlugins()` / `GetInstalledLightPlugins()` → `PluginInfo[]` [F4SE]
  - `ConstructibleObject.Get/SetConstructibleComponents` → `ConstructibleComponent[] {Form object; int count}` [F4SE]
  - `MiscObject.Get/SetMiscComponents` → `MiscComponent[] {Component object; int count}` [F4SE]
  - `ObjectMod.GetPropertyModifiers` → `PropertyModifier[] {int target; int operator; Form object; float value1; float value2}` [F4SE]
  - `ObjectReference.GetConnectPoints` → `ConnectPoint[]` [F4SE, L]
  - `ApplyMaterialSwap` → `MatSwap:RemapData[]` [F4SE, L]
  - `InstanceData.*(Owner akOwner)`, with `InstanceData:Owner {Form owner; int slotIndex}` and `DamageTypeInfo[]` [F4SE]
  - `UI.RegisterCustomMenu(…, MenuData)`; `MenuData` has default member values [F4SE]
  - vanilla `Quest:QuestStage {Quest QuestToSet; int StageToSet}`, used by the script-defined global `Quest.SetQuestStage` / `GetQuestStageDone` [src: vanilla/Quest.psc]
- **`ObjectReference.GetInventoryItems()` returns `Form[]`, not a struct array** [src: f4se/scripts/modified/ObjectReference.psc]. You get base forms only: no counts and no instances. It is latent.

### 5.9 Item instances (OMODs)
- The same base form can exist as many different instances.
- `GetItemCount`, `RemoveItem`, `IsEquipped` and `EquipItem` work on base forms.
- `AttachModToInventoryItem` fails when the container holds more than one of the item, may silently fail on duplicates, and reloads the 3D of an equipped item [web: https://falloutck.uesp.net/wiki/AttachModToInventoryItem_-_ObjectReference].
- Address instances with the F4SE `InstanceData:Owner` struct: `slotIndex` = equip slot of an actor, or −1 for the reference itself [src: modified/InstanceData.psc].
- Item identity on the server must become base + OMOD list (research doc §4.7).

### 5.10 `Var` and var-arg natives
- `Var` holds any value except arrays.
- Var-arg natives: `CallFunction(string, Var[]) [L]`, `CallFunctionNoWait`, `Utility.CallGlobalFunction(string script, string fn, Var[]) [L]`, `CallGlobalFunctionNoWait`, `SendCustomEvent(CustomEventName, Var[] = None)`, `Get/SetPropertyValue(string, Var) [L]`, F4SE `UI.Get/Set/Invoke`, `RegisterForFurnitureEvent(Var)`, `Utility.VarToVarArray` / `VarArrayToVar`.
- **Parameter types must match exactly**: no auto-cast, and defaulted parameters must still be supplied [web: https://falloutck.uesp.net/wiki/CallFunction_-_ScriptObject].
- `CustomEventName` / `ScriptEventName` are compiler-checked string-literal parameter types. At runtime they behave as strings [inference: verify in a PEX dump].

### 5.11 Integer width
- Papyrus `int` is signed 32-bit. Runtime form ids ≥ 0x80000000 (the 0xFF… ids used for SkyMP-spawned forms) are negative.
- The server works around this by returning `GetFormID` as a float for refs [src: script_classes/PapyrusForm.cpp:107-115].
- SP's `getFormEx` exists for the same reason.

### 5.12 Script flags
- **`Const` scripts** (all fragments) cannot hold state, and the game may unload them.
- **`Const` properties and variables** ignore saved values. Server persistence must not save them.
- **`Mandatory`** properties produce editor warnings only.
- **`Native` scripts** cannot have states, variables or auto properties, but they are the only scripts allowed to declare events.
- **`DebugOnly` / `BetaOnly`** functions are removed when compiling in release / release-final mode. The whole `Debug` script, `Utility.SetINI*` and many profiling functions are `DebugOnly`. **Compile FalloutMP gamemode scripts in debug mode** or `Debug.*` calls vanish [web: https://falloutck.uesp.net/wiki/Differences_from_Skyrim_to_Fallout_4].
- **Namespaced scripts** `A:B:C` live at `Scripts/A/B/C.pex`; fragments are under `Fragments:*`. The `import` keyword understands namespaces.
- **Remote event handlers use the root declaring type** (`ObjectReference.OnItemAdded`, not `Actor.OnItemAdded`).

### 5.13 InputEnableLayer replaces global control toggles
- `InputEnableLayer.Create()` returns a ScriptObject handle, not a Form.
- Controls are disabled if **any** layer disables them. `DisablePlayerControls(false, …)` does not re-enable controls that are already disabled; use `EnablePlayerControls`.
- Call `Delete()` when done.
- SP3 must marshal the handle. The server needs a per-player handle table to forward IEL calls (§3.2).

### 5.14 Latent and delayed natives
- **Latent functions** (marked `[L]`) include `AddItem`, `RemoveItem`, `MoveTo`, `SetPosition`, `SetAngle`, `SetScale`, `SetMotionType`, `Enable`, `Disable`, `Delete`, `Resurrect`, `ApplyHavokImpulse`, `Message.Show`, `Quest.Start`, `SetCurrentStageID`, `CallFunction`, `Get/SetPropertyValue`, and the `Wait*` family.
  - SP returns Promises for them.
  - The server VM must return promises (as SkyMP's `ExecuteSpSnippetAndGetPromise` does).
  - Script-defined wrappers that call latent natives are latent too (`Quest.SetStage`).
- **Delayed natives.** Most native form functions sync to the next frame ("delayed"); only a listed set is non-delayed [web: https://falloutck.uesp.net/wiki/Threading_Notes_(Papyrus)].
  - This does not matter on the SP side: SP calls natives synchronously inside its tick hook.
  - On the server it explains why vanilla scripts tolerate latency.

### 5.15 Script-defined wrappers inside native scripts
These ship as Papyrus bodies inside the vanilla `.pex` files:
- `Game.Find*FromRef`, `GetCaps`, `GetCommonProperties`, `GetPlayerLevel`, `Give/RemovePlayerCaps`
- `Quest.GetStage/SetStage/GetStageDone/SetAllStages/SetObjectiveSkipped/ModObjectiveGlobal`
- `Actor.GetActorBase/AddToFaction/IsInPowerArmor/IsOwner/KillEssential/Follower*/SetCompanion*`
- `ObjectReference.IsEnabled/IsInInterior/IsNearPlayer/MoveToIfUnloaded/TranslateToRef/SellItem/GetSelfAsActor/HasOwner/IsInLocation`
- `GlobalVariable.GetValueInt/SetValueInt/Mod`
- `Math.Max/Min`
- the `ReferenceAlias.TryTo*` family

On the server, either load the vanilla `.pex` (licensing: users' own game files only, never committed) or reimplement them as natives. Do **not** register natives with the same names and expect them to win: the VM tries script functions first [src: papyrus-vm/src/papyrus-vm-lib/VirtualMachine.cpp:295-296].

### 5.16 Spells and magic are marginal
- FO4 keeps `Spell`, `MagicEffect`, `ActiveMagicEffect` and `Enchantment` for abilities, chems and legendary effects, but players do not cast spells.
- The client's magic sync (`castSpellImmediate`, `magicSyncService`) and the server's spell equip logic should be dropped or replaced by weapon-fire and consumable logic.
- `OnItemEquipped` is the event FO4 itself recommends for detecting consumables [web: https://falloutck.uesp.net/wiki/OnItemEquipped_-_Actor].

---

## Appendix A. How this document was produced

**Data sources**
- **FO4 signatures:** parsed from the F4SE repository's `scripts/vanilla` and `scripts/modified` `.psc` files (F4SE 0.7.9 tree).
- **Latent list:** MediaWiki API query of `Category:Latent_Functions` on falloutck.uesp.net (61 members), plus F4SE `LatentNativeFunction*` registrations.
- **Client calls:** extracted from `skymp5-client/src` by matching against `skyrim-platform/src/platform_se/codegen/convert-files/skyrimPlatform.ts`, then hand-verified.
- **Server natives:** every `AddMethod` / `AddStatic` in `skymp5-server/cpp/server_guest_lib/script_classes/*.cpp`.
- **Server events:** every `SendPapyrusEvent` call under `server_guest_lib`.

**To regenerate**
1. Clone `ianpatt/f4se` and parse `Scriptname`/`Function`/`Event`/`Struct` lines, joining `\` continuations.
2. F4SE additions are simply every function in `scripts/modified`.

Do not commit the parsed `.psc` contents. Bethesda signatures and names are fine to quote; script bodies are not.

**Open items to verify in game** (all marked `[inference]` above):
- `TranslateTo` on FO4 actors.
- `ObjectReference.PlayAnimation` on actors as a `sendAnimationEvent` stand-in.
- Whether the `ForceMovement*` natives work in the retail exe.
- `Game.GetForm` with negative ints.
- `Variable01..10` AVIF EditorIDs.
- GameHour/TimeScale global ids, and the GMST/INI keys used by `index.ts` and `ragdollService.ts`.
- FO4 behaviour-graph variable names.
- Runtime representation of `CustomEventName`.
