# F28 — Pip-Boy, Radio, Lights, HUD & Menu Policy

| Field | Value |
|---|---|
| Tier | T1 (menu non-pause policy, Pip-Boy data policy, Pip-Boy light / PA headlamp, in-world radios, HUD), T2 (shared radio schedule, Pip-Boy stats feed) |
| Target level | L2 for cosmetic state (lights, radio playback, UI), L3 for data shown in the Pip-Boy (owned by F04/F08/F19/F22/F26/F27) and persisted radio refs |
| SkyMP analogue | No Pip-Boy. Nearest: `isBadMenuShown()` gating equipment apply (`skymp5-client/src/view/formView.ts:520-541`), `BrowserService` hiding the overlay on menus and the F1/F2/F6 bindings (`browserService.ts:25-50`). Lights: none (Skyrim torches are equipment). SkyMP level n/a |
| Milestone | M5 (menu non-pause policy: everything else assumes the world keeps running), M11 (Pip-Boy, lights, radios, HUD), M12 (radio schedule, stats) |
| Workstreams | PLAT, CLI, FRONT, SRV, GM |
| Depends on | PLAT-040 (event sources), PLAT-042 (menus), PLAT-060/PLAT-061 (overlay), PLAT-088 (node APIs), CLI-060, F01 (movement flags), F02 (Pip-Boy raise/lower actions), F07 (radio activation), F17 (PA), F25 (clock), F26/F27 (map and quest data), F31 (nameplates) |
| References | reference/fo4-systems-world-economy.md §2 S11, S12, S10, S20 ("Photo mode", "Achievements & MiscStats", "HUD markers & compass"), §3.1, §3.3 (`RadioState`); reference/fo4-systems-combat-character.md §1 (menu flags row), §1.6 item 2; reference/commonlib-port-map.md §1.11, §4.2, §4.3; reference/papyrus-api-map.md §5.6; reference/prior-art.md §3.1.8 (headlamp pitfall), §3.2.6, §5.3 Adopt 10 |

## 1. Summary
The Pip-Boy stays the vanilla, local UI. Everything it displays comes from server-authoritative state that other specs already sync: inventory, stats, perks, quests, map, workshops. Opening it, or any other menu, **never pauses the game**. Remote players keep moving and shooting, and the local player stays vulnerable, as in SkyMP. Everyone nearby sees a player's Pip-Boy light or power armor headlamp. Radio plays locally. In-world radios (settlement radios, receivers) have a shared, persisted on/off state and frequency. At T2, custom gamemode stations can play in lock-step from the server clock. Holotape games are local. The HUD adds nameplates (F31), compass targets (F26/F27) and gamemode widgets. FO4 has no vanilla photo mode.

## 2. Vanilla Fallout 4 behaviour (engine facts that matter for sync)
- **Pip-Boy:**
  - `PipboyMenu`, tabs `PIPBOY_PAGES {kStat, kInv, kData, kMap, kRadio}`;
  - `PipboyDataManager` with `statsData, specialData, perksData, inventoryData, questData, workshopData, logData, mapData, radioData, playerInfoData, statusData`;
  - `PipboyManager` (`InitPipboy`, `LowerPipboy(reason)`, `ClosedownPipboy`) [src: CLF4 P/PipboyDataManager.h, P/PIPBOY_PAGES.h, P/PipboyManager.h];
  - `IsPipboyActiveEvent`;
  - the companion app mirrors the data over the LAN (world-economy S11).
- **Lights:**
  - holding the Pip-Boy button toggles the Pip-Boy light, coloured like the HUD;
  - in power armor the same input toggles the helmet headlamp;
  - event sources `PipboyLightEvent : BSTValueEvent<bool>` (getter 4803571) and `PowerArmorLightData` (2701547) [src: CLF4 P/PipboyLightEvent.h; commonlib-port-map §4.2];
  - headlamps live on a global add-on list (`AddOnNode158`), and clones must skip them (prior-art §3.1.8 pitfall).
- **Radio:**
  - stations are quest scenes on transmitter refs;
  - Papyrus `MakeRadioReceiver`, `SetRadioOn/Frequency/Volume`, `IsRadioOn`, `GetRadioFrequency`, `Game.Get/SetPlayerRadioFrequency`, `IsPlayerRadioOn`, `TurnPlayerRadioOn`;
  - `ExtraRadioData`/`ExtraRadioReceiver` [src: F4SE vanilla/ObjectReference.psc:468-471, 650-653, 922-928; Game.psc:173, 253-260];
  - availability depends on vanilla quest stages: Radio Freedom, Classical Radio (world-economy S12).
- **Holotapes:** `HolotapeMenu`, `PipboyHolotapeMenu`, `TerminalHolotapeMenu`. The SWF games (Atomic Command, Grognak, Pipfall, Red Menace, Zeta Invaders) are local programs. `OnHolotapeChatter` passes data from program to script (world-economy S10).
- **Menus:**
  - FO4 menu names are listed in papyrus-api-map §5.6;
  - the `kPausesGame` flag is bit 0 of `UI_MENU_FLAGS` [src: CLF4 U/UI_MENU_FLAGS.h, via fo4-systems-combat-character §1];
  - clearing `UIMF_PAUSES_GAME`, `UIMF_FREEZE_FRAME_BACKGROUND` and `UIMF_FREEZE_FRAME_PAUSE` on an IMenu unpauses it (Fallout Together, prior-art §3.2.6);
  - `IMenu::menuFlags` is at 0x58 (commonlib-port-map §4.3).
- **HUD:** `HUDMenu`, `PowerArmorHUDMenu`, compass with quest/location/enemy markers, `HUDModeEvent`. Engine projection `NiCamera::WorldPtToScreenPt3` (PLAT-062).
- **Photo mode:** none in any vanilla version. There is no `PhotoMenu` RTTI. Only mods add one (world-economy S20).
- **Single-player assumptions that break:**
  - pausing menus freeze the local world, which looks like lag and invites "pause-heal" abuse (fo4-systems-combat-character §1.6 item 2);
  - the Pip-Boy light is a player-only engine light that remote clients never see;
  - station availability follows vanilla quests.

## 3. SkyMP baseline
- **Menus:** SkyMP gates remote equipment application while "bad" menus are open (`isBadMenuShown`) and hides or unfocuses the CEF browser while menus are open (`badMenusOpen` in `browserService.ts`). It does not unpause Skyrim's pausing menus [inference].
- **Lights, radio, Pip-Boy:** no analogue.
- **Reuse:** the menu-tracking pattern, browser hiding (CLI-060), SpSnippet `Debug.Notification` for HUD text, property bindings for per-ref state (world-economy B18).

## 4. Design

### 4.1 Authority model
| State | Class |
|---|---|
| Pip-Boy UI, holotape games, local radio playback, custom map markers, HUD rendering | D |
| Data shown in the Pip-Boy | A, in its owning spec (F04 inventory, F08/F19 stats and perks, F22 workshops, F26 map, F27 quests) |
| Light on/off (Pip-Boy light or PA headlamp) | B: owner-authoritative cosmetic, relayed after F01 validation. It matters for F29, because hosts' NPC detection sees the light |
| In-world radio on/off and frequency | A (per-ref property, changed through F07 activation or Papyrus) |
| Station availability | A (server settings) |
| Pip-Boy MiscStats (T2) | A (server stats, SRV-060) |

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Light on | bit | `MpActor::lastMovementFlags` bit `lightOn` (F01 §4.2) | no (off on spawn) | off |
| `lightState` (read-only view: `{on, kind: "pipboy"|"paHeadlamp"}`) | derived | `LightStateBinding` (new) | no | — |
| Radio state on radio refs | `RadioState{on bool, freq f32}` | `MpChangeFormREFR.radio` (world-economy §3.3) | yes | ESM (`ExtraRadioData`) |
| Station availability | `map<stationKey, bool>` | settings `radio.stations` | no | GameProfile list |
| Custom station schedule (T2) | `{stationKey, epochMs, tracks[{path, durationMs}]}` | gamemode property (F31) | gamemode-defined | — |
| Pip-Boy stats (T2) | `map<string,int>` | `PlayerProfile.stats` (SRV-060) | yes | 0 |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `UpdateMovementFo4` (65) | C→S, relay | `flags` bit 11 `lightOn` (Pip-Boy light, or PA headlamp when `inPowerArmor`) | U | F01 rate; immediate on toggle | reused (F01 must reserve bit 11) |
| `CreateActorFo4` (64) | S→C | `movementFlags` incl. `lightOn` in the snapshot | R | stream-in | reused |
| `UpdateProperty` (7) `radioState` | S→C | `{on, freq}` | R | on change | reused |
| `SpSnippet` (30) | S→C | `Debug.Notification`, `Game.TurnPlayerRadioOn`, `Game.SetPlayerRadioFrequency`, `setMiscStat` (T2) | R | on server call | reused |

No new message IDs. The radio schedule (T2) needs only the clock in `WorldTimeWeather` (105) plus a gamemode property.

### 4.4 Client capture (owner side)
- **Light:** sink `PipboyLightEvent` and `PowerArmorLightData` (PLAT-040 extension). Fall back to a native `getPlayerLightState()` polled at 4 Hz. The value goes into the F01 movement flags.
- **Menus:** `MenuOpenCloseEvent` (PLAT-042) feeds `MenuPolicyService` (§4.5 table) and `BrowserService` (CLI-060).
- **Pip-Boy actions:** equip, drop, use stimpak and the rest are already captured by F05, F06 and F20. The Pip-Boy adds no capture of its own. The raise/lower wrist pose is an F02 action (`ActionPipboy`/`ActionPipboyClose` [inference: confirm in the F02-T01 probe]) and must be whitelisted there.
- **Radio:** the player's own Pip-Boy radio is never sent (class D).

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
**Menu policy** (local; every pausing menu is unpaused; the exact flags come from the F28-T01 G-self dump):

| Menu | MP policy |
|---|---|
| `PipboyMenu`, `PipboyHolotapeMenu`, `HolotapeMenu`, `FavoritesMenu`, `MessageBoxMenu`, `BookMenu`, `Console`, `PauseMenu` | Allowed, **unpaused**. PauseMenu save/load entries are disabled (F00) |
| `ContainerMenu`, `BarterMenu`, `ExamineMenu`, `CookingMenu`, `PowerArmorModMenu`, `RobotModMenu`, `LockpickingMenu`, `TerminalMenu`, `WorkshopMenu`, `LooksMenu`, `LevelUpMenu`, `SPECIALMenu` | Allowed, **unpaused**. The owning spec (F06/F23/F15/F16/F17/F24/F22/F03/F19) handles state |
| `SleepWaitMenu`, `SitWaitMenu` | Closed on open; F25 rest policy |
| `VATSMenu` | Blocked at T0; F18 VATS-lite later |
| `DialogueMenu` | Closed on open; F27 |
| `LoadingMenu`, `MainMenu`, `FaderMenu`, `CursorMenu`, `HUDMenu` | Untouched |
| Unknown menus (mods, e.g. photo modes) | Unpaused when `kPausesGame` is set, unless listed in `menus.allowPause` |

- **Mechanism:** a hook clears the pause and freeze-frame flags when a menu is registered or opened (prior-art §5.3 Adopt 10). Menus whose camera needs a paused world (LooksMenu, Lockpicking) are checked with G-self.
- **Vulnerability:** no menu grants invulnerability. A player in the Pip-Boy can be shot.

**Equipment apply gate:** the FO4 port of `isBadMenuShown()` lists `PipboyMenu`, `ContainerMenu`, `BarterMenu`, `ExamineMenu`, `WorkshopMenu`, `PowerArmorModMenu` and `LooksMenu`. It applies to the owner's own inventory re-apply only.

**Remote light:**
- the native `setActorLight(ref, on, kind)` attaches a LIGH (the vanilla Pip-Boy light form for `pipboy` [inference: EDID to verify, D-real]; the PA helmet light for `paHeadlamp`) to the wrist or helmet node via the PLAT-088 node APIs;
- it is re-attached after a graph or 3D rebuild (PA enter/exit, F03 base recreation);
- at most `lights.maxRemote` (8) lit remote actors, nearest first, because FO4's shadowed-light budget is small;
- clones skip `AddOnNode158` headlamps (prior-art pitfall);
- remotes use one colour (`lights.remoteColor`, default Pip-Boy green).

**In-world radios:**
- `radioState` → `SetRadioOn(on)` + `SetRadioFrequency(freq)` on the local ref, at stream-in and on change;
- radio activation goes through F07 (`Activate` → server toggles `on`).

**Station availability:**
- station quests are on the F27-T01 allow-list, so the local Pip-Boy radio works;
- stations gated by quests that are blocked use their GameProfile default (on or off) [inference: set by starting or skipping their scene quest at load].

**Holotape games:** local only. Gamemodes may collect `OnHolotapeChatter` scores (F24 owns `HolotapePlay`).

**HUD:**
- nameplates (F31); compass markers (F26 discovered locations, F27 targets);
- server text via SpSnippet `Debug.Notification`;
- gamemode widgets in the overlay (FRONT-006);
- the overlay hides while Pip-Boy, container, terminal and the other CLI-060 menus are open.

**Stream-in, respawn, reconnect:** the light comes from the `CreateActorFo4` flags. Radios come from `CreateActorFo4.props.radioState`. Lights are off after respawn.

### 4.6 Validation & anti-cheat
- **`lightOn`:** passes F01 validation (ownership, seq). Toggles faster than 4 per second are coalesced: the server relays the last value at most every 250 ms. `kind = paHeadlamp` requires the `inPowerArmor` flag; otherwise it is treated as `pipboy`. There is no gameplay effect beyond detection on hosts (F29).
- **Radio toggles:** through F07 `Activate` (distance and occupancy checks, I15). A rejected activation re-sends `radioState` (S12).
- **Menus:** never trusted. The server has no "menu open" state, so menus give no protection, no time stop and no extra healing. Stimpaks used from the Pip-Boy follow F20 timing.
- **Stats (T2):** server-incremented only. The client's MiscStats are display copies.

### 4.7 Audience / visibility
- `lightOn`: grid neighbours via the movement relay.
- `radioState`: listeners of the radio ref.
- Stats and SpSnippet radio calls: owner only.

### 4.8 NPC parity
- Hosted NPCs in power armor: if `PowerArmorLightData` reflects only the player [inference], NPC headlamps stay local AI cosmetics (class D on the host, not synced) at T1.
- Companions' Pip-Boy: n/a (player only).
- Radio refs work the same for every ref, regardless of who activates them.

### 4.9 Gamemode API & server Papyrus
- **Properties:**
  - `mp.get(actorId, 'lightState')` (read only);
  - `mp.get/set(refId, 'radioState')` (persisted);
  - T2 `mp.get(actorId, 'stats')` and `mp.set` with deltas.
- **Settings:** `radio.stations`, `lights.{maxRemote, remoteColor}`, `menus.allowPause` (S22).
- **Events:** `onActivate` covers radios. No new blockable events: there are no gameplay actions here.
- **Papyrus natives (server):**
  - `ObjectReference.SetRadioOn/SetRadioFrequency/IsRadioOn/GetRadioFrequency` read and write `radioState`;
  - `Game.TurnPlayerRadioOn/SetPlayerRadioFrequency/IsPlayerRadioOn` → SpSnippet to the default actor's owner;
  - `Game.IncrementStat/QueryStat` (T2) → `PlayerProfile.stats`;
  - `UI.IsMenuOpen` returns false on the server, as `IsInMenuMode` does in SkyMP.

### 4.10 Edge cases & failure modes
- **PA enter/exit while lit:** the light switches kind and is re-attached.
- **Graph or 3D rebuild:** re-attach.
- **Stream-out:** detach and delete the placed LIGH.
- **Loading screens:** re-apply lights and radios after load.
- **Console open:** unpaused, so an admin types while the world runs (F30).
- **Alt-tab:** `bAlwaysActive=1` (CLI-010) keeps the client simulating.
- **Hot reload:** `MenuPolicyService` restores the original flags of open menus on unload, and keeps its state in `sp.storage`.
- **Mods that pause through other means** (time multiplier = 0) are not covered. They are listed as unsupported.

### 4.11 Performance budget
- Light state costs 0 extra bytes (a flag bit).
- Radio property updates are rare.
- Client: ≤ 8 attached lights. Light attach ≤ 1 ms per event. Menu-flag hook ≤ 10 µs per menu open.

## 5. Engine / platform work required
- A menu-flag hook (clear `kPausesGame` and freeze-frame flags on open). TE's approach: `UI.cpp:56-66` (prior-art §3.2.6).
- Event-source getters `PipboyLightEvent` (4803571) and `PowerArmorLightData` (2701547).
- `setActorLight` (LIGH placement and node attach; PLAT-088).
- `getPlayerLightState` fallback.
- (T2) `setMiscStat(name, value)` (RE `MiscStatManager`).

## 6. Tests
- `L-unit` `[F28]`:
  - `lightOn` relayed to neighbours and present in the `CreateActorFo4` snapshot;
  - toggle coalescing;
  - `paHeadlamp` requires `inPowerArmor`;
  - `radioState` persistence round trip, `UpdateProperty` audience, rejected activation re-sends state;
  - Papyrus `SetRadioOn` reflected;
  - (T2) stats increments persisted.
- `L-ts`: `MenuPolicyService` decision table (each menu → block / unpause / untouched); light capture → flag; nearest-8 remote light selection; (T2) radio schedule position from a mocked clock.
- `G-self`:
  - dump `menuFlags` for every menu opened in a scripted run (feeds the §4.5 table);
  - verify the Pip-Boy no longer pauses (the world clock advances while it is open);
  - list the available radio stations.
- `G-manual`:
  - A opens the Pip-Boy and B shoots A, who takes damage;
  - B sees A's Pip-Boy light and PA headlamp;
  - a settlement radio toggled by A is heard by B;
  - a holotape game runs while the world continues.

## 7. Tasks
- [ ] **F28-T01** Menu policy: flag-clearing hook, `MenuPolicyService` with the §4.5 table, FO4 `isBadMenuShown` list, G-self `menuFlags` dump — M — Depends: PLAT-042, QA-010 — Verify: G-self, G-manual — Files: fallout4-platform/src/.../MenuFlagsHook.cpp, falloutmp-client/src/services/services/menuPolicyService.ts, falloutmp-client/src/config/menuPolicy.ts
  - Accept: no menu pauses the world (game time advances while it is open), and the dump is committed to the reference doc.
- [ ] **F28-T02** Light capture: `PipboyLightEvent`/`PowerArmorLightData` sources + `lightOn` flag in movement capture — S — Depends: PLAT-040, F01-T04 — Verify: L-ts, G-self
- [ ] **F28-T03** Remote light apply (`setActorLight`, kind by PA state, nearest-8 budget, re-attach on rebuild) — M — Depends: PLAT-088, F01-T06 — Verify: W-ci, G-manual — Files: fallout4-platform/src/.../LightApi.cpp, falloutmp-client/src/view/remoteLights.ts
- [ ] **F28-T04** Server: `lightOn` in F01 flags (bit 11), `LightStateBinding`, `CreateActorFo4` field, coalescing — S — Depends: F01-T05 — Verify: L-unit — Files: falloutmp-server/cpp/addon/property_bindings/LightStateBinding.{h,cpp}, unit/LightStateTest.cpp
- [ ] **F28-T05** In-world radios: `radioState` field + binding, F07 activation branch, Papyrus radio natives, client apply — S — Depends: F07 — Verify: L-unit, G-manual — Files: falloutmp-server/cpp/server_guest_lib/MpChangeForms.{h,cpp}, addon/property_bindings/RadioStateBinding.{h,cpp}, falloutmp-client/src/services/services/radioService.ts
- [ ] **F28-T06** Station availability: allow-listed station quests (with F27-T01), `radio.stations` defaults for quest-gated stations — S — Depends: F27-T01 — Verify: G-self, G-manual
- [ ] **F28-T07** HUD integration: CLI-060 FO4 menu list, nameplate hiding in menus (F31-T01), HUD notifications via SpSnippet — S — Depends: CLI-060, F31-T01 — Verify: G-manual
- [ ] **F28-T08** (T2) Shared radio schedule for gamemode stations from the server clock — M — Depends: F25-T07 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/radioScheduleService.ts, falloutmp-gamemode/src/radio/
  - Accept: two clients play the same custom track within 1 s of each other.
- [ ] **F28-T09** (T2) Pip-Boy stats feed: `PlayerProfile.stats`, `Game.IncrementStat/QueryStat` server natives, `setMiscStat` client native — M — Depends: SRV-060, PVM-014 — Verify: L-unit, G-self
- [ ] **F28-T10** G-manual script (Pip-Boy under fire, lights, radios, holotape, HUD) — S — Depends: F28-T01…T05 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F28-pipboy-radio-hud.md

## 8. Open questions & risks
- Unpausing LooksMenu, LockpickingMenu or TerminalMenu may break their cameras or animations. Per-menu exceptions are possible, but each one must stay vulnerable.
- The vanilla Pip-Boy light may not be a normal LIGH form that can be attached to other actors (RE). The fallback is a FalloutMP LIGH in `FalloutMP.esl` (F27-T07).
- Light state travels as F01 flag bit 11 `lightOn` (already reserved in F01 §4.3); `CreateActorFo4.movementFlags` carries it for late joiners (F00-T11).
- Pip-Boy companion app data (stats, map) would show template-save values unless the T2 stats feed lands.
