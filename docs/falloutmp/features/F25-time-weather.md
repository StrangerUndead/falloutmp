# F25 — Time, Weather & Rest

| Field | Value |
|---|---|
| Tier | T0 (server clock + globals), T1 (weather, radstorms, rest policy), T2 (per-region weather, rest votes) |
| Target level | L3 (SkyMP L2: client-side UTC clock, no weather; SkyMP-plus fixes I18) |
| SkyMP analogue | `TimeService` sets GameHour/Day/Month/Year/TimeScale globals from the UTC wall clock every 2 s; server `GetCurrentGameTime` = days since 1 Jan of the real year; no weather sync. SkyMP level L2 — code: `skymp5-client/src/services/services/timeService.ts:24-53`, `skymp5-server/cpp/server_guest_lib/script_classes/PapyrusUtility.cpp:49-79` |
| Milestone | M6 (server clock SRV-070, needed by SRV-080 and PVM-008), M9 (globals sync, weather, radstorms, rest), M12 (per-region weather) |
| Workstreams | SRV, CLI, PLAT, ESPM, NET, PVM, GM |
| Depends on | SRV-070, PLAT-086, ESPM-010 (WTHR), PVM-008, NET-002, F08 (Rads AV), F20 (effects pipeline, SRV-020), F07 (bed occupancy) |
| References | reference/fo4-systems-world-economy.md §1 (B16, B17), §2 S17, S20 ("Sleeping & beds"), §3.2, §3.3 (`WorldClockState`, `WeatherState`), §3.7, §5 item 5; reference/skymp-sync-inventory.md §1.16, §3.2 I18; reference/prior-art.md §3.2.6, §5.1 C15, §5.2; reference/papyrus-api-map.md §1.2 Utility/GlobalVariable, §1.5 Weather; reference/skyrim-coupling-index.md (timeService row) |

## 1. Summary
Every player on a server sees the same time of day, the same date and the same weather. One server clock drives everything that depends on game time: Papyrus timers, respawn and reloot timers, vendor restock, survival needs and the client sky. The server rolls weather per worldspace from the game's climate tables. Radstorms are a server event: the server, not the client's local weather spell, applies their radiation. Waiting and sleeping never skip time for everyone. By default they are disabled, as in SkyMP. A server can instead let a rest grant its benefits (healing, Well Rested) while the clock runs on.

## 2. Vanilla Fallout 4 behaviour (engine facts that matter for sync)
- **Globals:** GameYear `0x35`, GameMonth `0x36`, GameDay `0x37`, GameHour `0x38`, GameDaysPassed `0x39`, TimeScale `0x3A`. TimeScale `GLOB:0000003A` is confirmed. The other five are inferred from the `Calendar` layout, which is the same as Skyrim's: `gameYear, gameMonth, gameDay, gameHour, gameDaysPassed, timeScale, midnightsPassed, rawDaysPassed` [src: CLF4 C/Calendar.h, via fo4-systems-world-economy §2 S17]. Verify them in game with `help gamehour 4`, and by EDID in data (D-real). The default timescale is 20.
- **Engine singletons:** `Calendar::Singleton` (OG 1444952, AE 2689092/4796378). `Sky::Singleton` NG/AE 2192448, `ForceWeather` 2208861, `ResetWeather` 2208860. Fallout Together flags these as unverified (prior-art §5.1 C15, §5.2). `Sky::currentGameHour` [src: CLF4 S/Sky.h:159].
- **Start date:** a new game starts on 23 Oct 2287 [inference]. Read the real defaults from the GLOB `FLTV` values in Fallout4.esm; do not hard-code them.
- **Time skips:** waiting (`SleepWaitMenu`, `SitWaitMenu`), sleeping, fast travel and `Game.PassTime(hours)` all advance time. Events `OnPlayerSleepStart(start, desiredEnd, bed)`, `OnPlayerSleepStop(interrupted, bed)`, `OnPlayerWaitStart/Stop` fire after `RegisterForPlayerSleep/Wait` [src: F4SE vanilla/ScriptObject.psc:112-123, 300-322]. `Game.SetInChargen(abDisableSaving, abDisableWaiting, abShowControlsDisabledMessage)` exists [src: F4SE vanilla/Game.psc:342-345].
- **Weather records:** WTHR has flags Pleasant/Cloudy/Rainy/Snow [src: xEdit wbDefinitionsFO4.pas:13066-13080]. **`UNAM` holds the magic: Lightning Strike {spell, threshold} and Weather Activate {spell, threshold}**. This is how radstorms deal rads [src: xEdit wbDefinitionsCommon.pas:9897-9911]. CLMT holds the per-worldspace weather list (chances). REGN weather entries override it per region.
- **Weather natives:** `Weather.FindWeather(class)`, `ForceActive(override)`, `SetActive(override, accelerate)`, `GetCurrentWeather`, `GetOutgoingWeather`, `GetCurrentWeatherTransition`, `ReleaseOverride`, `GetSkyMode`, `GetClassification` (papyrus-api-map §1.5 Weather).
- **Radstorms** last about 2 game hours: green fog and lightning, and outdoor exposure deals rads. Interiors are safe. Far Harbor's island is covered by radioactive fog [web: wiki:Radstorm], [web: wiki:Weather]. How the Far Harbor fog applies rads (a weather spell, or hazards and triggers) is **unverified** [inference].
- **Single-player assumptions that break:**
  - each client runs its own clock and its own weather roll (the Sky picks a region's weather locally);
  - waiting, sleeping and fast travel skip time for the whole world;
  - radstorm rads come from a local spell cast on the local player.

## 3. SkyMP baseline
- **Client clock:** `TimeService.every2seconds` writes Hour/Day/Month/Year from `Date.now() + hoursOffset` (UTC) whenever the drift is ≥ 1 game minute. It then nudges `TimeScale` to 0.6 or 1.2 to converge. The year is computed as `UTCYear − 2020 + 199`, which is Skyrim-era specific [src: timeService.ts:24-53].
- **Server time:** `GetCurrentGameTime` returns the days since the start of the real year. `WaitGameTime` treats 1 game hour as 60 real seconds (`fHourSeconds`). The two disagree with each other and with the client (I18) [src: PapyrusUtility.cpp:49-79].
- **Waiting:** blocked with `setInChargen(true, true, false)` on every load [src: enforceLimitationsService.ts:23-29].
- **Weather:** none.
- **Reuse:** the update-loop pattern and `setInChargen`.
- **Replace:** the client-computed clock becomes a server clock. Add weather and rest.

## 4. Design

### 4.1 Authority model
- **Class A:** world clock (game days passed, timescale), weather per slot, radstorm exposure and its rads, rest outcomes (effects, AV restoration).
- **Class D:** sky rendering, cloud and precipitation particles, lightning bolt positions, transition visuals, the local fade during a rest.
- A client that edits its own globals (console `set gamehour`) changes only its own sky. Nothing gameplay-related reads client time.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| World clock | `WorldClockState{epochMs u64, gameDaysAtEpoch f64, timeScale f32, paused bool}` | `WorldClockService` (SRV-070), `WorldState::worldClock` | yes: ADR-010 world record `worldClock`, saved on change and every 60 s | settings `time.*`; start = ESM GLOB defaults |
| Calendar epoch | Y/M/D/H at GameDaysPassed = 0 | `GameProfile::CalendarStart()` | no (data) | Fallout4.esm GLOB 0x35–0x38 |
| Weather slot | `WeatherSlot{weather FormDesc, previous FormDesc, transitionStartMs, transitionSec, nextChangeMs, forcedUntilMs, rngState u64}` | `WeatherService`, keyed by worldspace (T1) or (worldspace, region) (T2) | yes: ADR-010 record `weatherState` | CLMT roll |
| Climate tables | weather lists with chances, region overrides, UNAM spells | ESPM views (F25-T03) | no | ESM |
| Rest session | `{nonce, kind, hours, bedRef, startMs, endMs}` | `MpActor::restSession` (new) | no (cancelled on disconnect) | — |
| Radstorm exposure accumulator | float rads | `MpActor` (memory) | no | 0 |
| Well Rested / survival fatigue | active effects | F20 effect store | yes (F20) | — |

Rules:
- Downtime does not count. After a restart the clock resumes from the saved `gameDays`, with `epochMs` reset to the current time.
- Setting `time.countDowntime=true` makes downtime count.

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `WorldTimeWeather` (105) | S→C | `serverNowMs u64`, `gameDaysPassed f64`, `gameYear u16`, `gameMonth u8`, `gameDay u8`, `gameHour f32`, `timeScale f32`, `flags u8` (clockChanged, weatherChanged, forced, radstorm, interiorOnly), `slotKey u32` (worldspace id), `weatherId u32`, `prevWeatherId u32`, `transitionElapsedSec f32`, `transitionSec f32` | R | after `CreateActorFo4 isMe`; on clock change; on weather change in the actor's slot; on the actor's worldspace change; keepalive every 60 s; ≤ 48 B | registry 105 |
| `RestAction` (107) | C→S | `nonce u32`, `op u8` (request, cancel), `kind u8` (sleep, wait), `hours u8`, `furnitureRefId u32` | R | user action | new (107, this spec) |
| `RestAction` (107) | S→C | `nonce u32`, `op u8` (accepted, denied, finished), `reason u8`, `endServerMs u64` | R | reply / end | new (107) |

- The server computes Y/M/D/H. The client never derives the calendar, so there is one tested implementation.
- Effects of a finished rest arrive through `EffectsUpdate` (92) and `ChangeValuesAv` (72). They are owned by F20/F08.

### 4.4 Client capture (owner side)
- **Clock and weather:** nothing is captured. These are S→C only.
- **Rest, `rest.mode = "disabled"` (default):**
  - `Game.SetInChargen(true, true, false)` on first update and on every load (SkyMP behaviour);
  - `SleepWaitMenu` and `SitWaitMenu` are closed on open (CLI-021).
- **Rest, `rest.mode = "benefitsOnly"`:**
  - The menus are intercepted on open: closed immediately, and a FalloutMP rest widget is shown instead (hours slider, FRONT).
  - Sleep is offered only while the player occupies a bed furniture (F07).
  - Confirming sends `RestAction{request}`. Moving, being hit or pressing Esc sends `RestAction{cancel}`.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
**Clock** (`TimeWeatherService`, replaces `timeService.ts`):
1. On a message, store `(serverNowMs, gameDaysPassed, timeScale, localRecvMs)`.
2. Each update (throttled to 1 s), compute `estDays = gameDaysPassed + (localNow − localRecvMs) × timeScale / 86 400 000`.
3. Hard-write all six globals plus `Sky::currentGameHour` through the PLAT-086 native when `clockChanged` is set, after a load, or when |drift| > 30 game seconds. `TimeScale` is always written exactly: no 0.6/1.2 nudging.
4. Between corrections the engine advances time itself at `TimeScale`.

**Weather:**
- Apply only if the local player is in an exterior cell of `slotKey`.
- Stream-in, post-load, `forced`, or `transitionElapsedSec ≥ transitionSec`: use `forceWeather(weatherId, override=true)`.
- Otherwise use `Weather.SetActive(true, accelerate=false)` so the transition runs locally.
- Interiors skip. On the first update in an exterior, apply instantly.
- If the engine switches weather on its own (region change), re-force the server weather.

**Local weather spells:** neutralized. At `gameDataLoaded` the platform clears the UNAM spell pointers on the runtime `TESWeather` forms, or blocks Sky-originated casts on the player (F25-T06, RE) [inference]. Rads come only from the server (§4.6).

**Reconnect:** `CreateActorFo4 isMe` is followed by `WorldTimeWeather`. The template save's time and weather are overwritten before the loading screen ends.

**Rest accepted:**
- an `InputEnableLayer` disables movement and fighting;
- a fade plays (FaderMenu or imagespace) for `rest.fadeSec`;
- the actor stays in the bed furniture;
- on `finished`, controls are restored.

**Rest denied:** the fade is cancelled and the reason is shown with `Debug.Notification`.

### 4.6 Validation & anti-cheat
| Check | Reject → correction |
|---|---|
| `RestAction` sender is the actor's owner (no NPC idx), `rest.mode ≠ disabled` | `RestAction{denied, reason=disabled}` |
| `sleep`: the actor occupies `furnitureRefId` (F07 occupancy, ≤ 256 u, per world-economy B3), it is a bed, and it is owned by the profile or a settlement bed it may use (F22), or is unowned | `denied, reason=bed` |
| Not in combat: no damage dealt or taken within `rest.combatLockSec` (10 s, F11), and no hosted NPC with this actor as combat target within 2048 u (F13) | `denied, reason=combat` |
| `hours` within 1–24. Survival caps when F20 survival is on: sleeping bag 3 h, mattress 5 h, Well Rested needs ≥ 7 h in a bed (world-economy S20) | `denied, reason=hours` |
| Not in power armor (F17), not swimming, not mid-transfer (container occupancy) | `denied, reason=state` |
| Gamemode `onRest` returns false | `denied, reason=gamemode` |
| The actor moves more than 64 u, takes damage, or the session expires during a rest | `finished` with partial benefits pro rata |

- **Radstorm exposure** is computed on the server only:
  - every 1 s, for each player in an exterior cell of a slot whose weather has an UNAM *Weather Activate* spell, once the transition has passed that spell's threshold;
  - rads = MGEF magnitude × dt, applied through F20/F08;
  - `onRadstormExposure` can veto per actor.
- **Client Rads changes** that do not come from the server are rejected by F08's AV authority.
- **Clock and weather changes** come only from server code: gamemode, Papyrus, or admin console with permission (F30).

### 4.7 Audience / visibility
- **Clock fields:** every connected user. This is the one deliberate world-wide state: low-rate, global by nature, comparable to `UpdateGamemodeData`.
- **Weather fields:** only to users whose actor is in that slot's worldspace. A user changing worldspace gets the new slot immediately.
- **`RestAction`:** owner only.

### 4.8 NPC parity
- The clock is shared. Hosted NPC packages and schedules run on the host's local clock, which is within 30 game seconds of the server.
- NPCs never send `RestAction`. Their sleep packages are local AI (class C).
- Radstorm rads apply to players only by default. `weather.radstormAffectsNpcs` (default false) [inference: vanilla applies the spell to the player].

### 4.9 Gamemode API & server Papyrus
**JS:**
- `mp.getWorldClock()` → `{gameDaysPassed, gameYear, gameMonth, gameDay, gameHour, timeScale, paused}`.
- `mp.setWorldClock({gameHour?, gameDaysPassed?, timeScale?, paused?})` broadcasts.
- `mp.getWeather(worldspaceDesc)`, `mp.setWeather(worldspaceDesc, weatherDesc, {transitionSec, forceMinutes})`, `mp.releaseWeather(worldspaceDesc)`.
- These are API methods, not `mp.get/set(0, …)`, because SkyMP properties need a form.

**Events:**
- `onWeatherChange(worldspaceDesc, fromDesc, toDesc)` [blockable: keep the current weather and re-roll after `weather.changeMinutes`];
- `onRest(actorId, kind, hours, bedRefId)` [blockable];
- `onRadstormExposure(actorId, rads)` [blockable, ≤ 1 Hz per actor].

**Settings:** `time.{mode: "scaled"|"realtime", timeScale: 20, startGameDay, hoursOffset, countDowntime}` (`realtime` reproduces SkyMP: UTC + `hoursOffset`, timescale 1), `weather.{changeMinutes, allowRadstorms, radstormAffectsNpcs, seed}`, `rest.{mode: "disabled"|"benefitsOnly", combatLockSec, fadeSec}` (S22).

**Papyrus natives (server):**
- `Utility.GetCurrentGameTime()` = `gameDaysPassed`;
- `Utility.WaitGameTime(h)` = h × 3600 / timeScale real seconds (fixes I18);
- `Utility.GameTimeToString()`;
- `Game.PassTime(h)` advances the clock only if `time.allowPassTime`; otherwise it is a no-op plus a warning;
- `GlobalVariable.GetValue/SetValue` on 0x35–0x3A read and write the server clock (virtual globals);
- `Weather.*`, resolved for the slot of the `HeuristicPolicy` actor (fallback: the default worldspace);
- `StartTimerGameTime`/`OnTimerGameTime` run on this clock (PVM-008).

**Papyrus events:** `OnPlayerSleepStart/Stop` and `OnPlayerWaitStart/Stop` are fired by the rest service through `MpForm::SendPapyrusEvent` (S16).

### 4.10 Edge cases & failure modes
- **Admin moves the clock backwards:** game-time timers (PVM-008, SRV-080, vendors) keep their remaining game-time duration, rather than firing or never firing.
- **Server restart:** the clock and weather slots reload. Clients get a fresh `WorldTimeWeather` on spawn.
- **Hot reload of the gamemode:** no clock state lives in JS.
- **Loading screens:** re-apply globals and weather on `postLoadGame` and after each load-door transition.
- **Child worldspaces** (Diamond City and other walled worldspaces) follow the parent's slot when WRLD says "use parent climate" [inference; verify WRLD flags in ESPM-005].
- **Interior cells with sky lighting:** treated as interiors [inference].
- **DLC worldspaces** (Far Harbor, Nuka-World) get their own slots from their CLMT. Far Harbor fog rads use this pipeline if their UNAM carries a spell (D-real check). Otherwise they become a server hazard zone in F20.
- **Disconnect during rest:** the session is cancelled and nothing is granted.

### 4.11 Performance budget
- `WorldTimeWeather` ≤ 48 B, at about 1 per minute per client plus changes.
- Weather roll: O(slots) every `weather.changeMinutes`.
- Radstorm tick: 1 Hz, O(exterior players in the slot), ≤ 1 µs per player.
- Client: a write of 6 globals at most once per second.

## 5. Engine / platform work required
- **PLAT-086:**
  - `setGameTime({year, month, day, hour, daysPassed, timeScale})`: writes the Calendar and refreshes `Sky::currentGameHour`;
  - `forceWeather(id, override)` / `resetWeather()` (Sky IDs from prior-art C15, verify);
  - `getCurrentWeather()`;
  - a `Weather.SetActive` wrapper.
- **Weather-spell neutralization** (RE): the `TESWeather` UNAM fields in CommonLibF4, or a hook on the Sky spell application.
- **ESPM:** CLMT (weather list with chances), REGN (RDWT weather entries; RPLD areas for T2), WTHR UNAM. ESPM-010 covers WTHR only.

## 6. Tests
- `L-unit` `[F25][WorldClock]`:
  - scaled and realtime math;
  - Y/M/D/H rollover across month, year and leap days;
  - persistence round trip with and without `countDowntime`;
  - `WaitGameTime`/`GetCurrentGameTime` values;
  - the `PassTime` policy;
  - the virtual-global reads.
- `L-unit` `[F25][Weather]`:
  - seeded roll determinism against CLMT chances;
  - transitions and forced weather;
  - `onWeatherChange` veto;
  - audience is only the slot's worldspace;
  - late joiner gets current weather;
  - a worldspace change sends the new slot;
  - message round trip.
- `L-unit` `[F25][Rest]`:
  - accept path, and every reject reason with its `RestAction{denied}` correction;
  - `onRest` veto;
  - Papyrus `OnPlayerSleepStart` observed;
  - Well Rested applied via F20;
  - interruption grants pro-rata benefits.
- `L-unit` `[F25][Radstorm]`:
  - only exterior players in the slot accrue rads;
  - rate from a synthetic MGEF magnitude;
  - interior players accrue none;
  - NPC option.
- `L-fixture`: CLMT/REGN/WTHR UNAM parsing on PluginBuilder fixtures.
- `L-ts`: drift-correction thresholds with a mocked clock (CLI-050); weather apply rules (interior skip, reapply after load, re-force after an engine switch).
- `L-int`: two bots get identical clocks; `mp.setWeather` reaches the bot in the Commonwealth and not the bot in an interior.
- `G-self`:
  - dump EDID and value of 0x35–0x3A;
  - `forceWeather` takes effect;
  - the sky hour matches GameHour.
- `G-manual`: two players see the same time of day and the same radstorm start within 2 s; rads rise only outdoors; a bed rest in `benefitsOnly` mode.
- `D-real`: GLOB EDIDs and defaults; UNAM spells of the radstorm weathers; the Far Harbor fog mechanism.

## 7. Tasks
- [ ] **F25-T01** Server clock service (implements SRV-070, fixes I18): `WorldClockService`, `time.*` settings, ADR-010 `worldClock` record, `mp.getWorldClock/setWorldClock` — M — Depends: REF-020, REF-021 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/WorldClock.{h,cpp} (new), WorldState.{h,cpp}, skymp5-server/cpp/addon/ScampServer.cpp, skymp5-server/ts/settings.ts, unit/WorldClockTest.cpp
  - Accept: the `[WorldClock]` cases pass. The clock survives a restart. SRV-080 and PVM-008 can read `NowGameDays()`.
- [ ] **F25-T02** Papyrus time natives on the server clock (`GetCurrentGameTime`, `WaitGameTime`, `GameTimeToString`, `PassTime` policy, virtual globals 0x35–0x3A) — S — Depends: F25-T01, PVM-013 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/script_classes/PapyrusUtility.cpp, PapyrusGame.cpp, unit/PapyrusUtilityTest.cpp
  - Accept: `WaitGameTime(1)` at timescale 20 waits 180 s. The Skyrim profile behaviour is unchanged (gated by GameProfile).
- [ ] **F25-T03** ESPM CLMT and REGN records plus WTHR UNAM views; GameProfile climate table per worldspace — M — Depends: ESPM-005, ESPM-010, ESPM-002 — Verify: L-fixture, D-real — Files: libespm/include/libespm/{CLMT,REGN}.h (new), libespm/src/, unit/Fo4ClimateRecordsTest.cpp
  - Accept: fixture weather lists and chances parse. D-real lists the Commonwealth climate and the radstorm weathers' UNAM spells.
- [ ] **F25-T04** `WorldTimeWeather` (105) and `RestAction` (107) messages + client mirrors — S — Depends: NET-002 — Verify: L-unit — Files: skymp5-server/cpp/messages/{WorldTimeWeatherMessage,RestActionMessage}.h, Messages.h, falloutmp-client/src/services/messages/
  - Accept: binary and JSON round trips. `WorldTimeWeather` ≤ 48 B.
- [ ] **F25-T05** `WeatherService`: per-worldspace slots, seeded roll, transitions, forced weather, `onWeatherChange`, `mp.getWeather/setWeather/releaseWeather`, audience, persistence — M — Depends: F25-T01, F25-T03, F25-T04 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/WeatherService.{h,cpp} (new), unit/WeatherServiceTest.cpp
  - Accept: the `[Weather]` cases pass, including the late-joiner snapshot and a worldspace change.
- [ ] **F25-T06** Platform natives (PLAT-086: Calendar write, Sky force/reset, SetActive wrapper), global-id verification, weather-spell neutralization — M — Depends: PLAT-086, PLAT-031 — Verify: W-ci, G-self — Files: fallout4-platform/src/.../TimeWeatherApi.cpp, Definitions.txt
  - Accept: the self-test sets 03:00 and 15:00 and the sky follows. A forced radstorm causes no local Rads change.
- [ ] **F25-T07** Client `TimeWeatherService` (replaces `timeService.ts`): globals, drift correction, weather apply rules, load and interior handling — M — Depends: F25-T04, F25-T06, CLI-050 — Verify: L-ts, G-manual — Files: falloutmp-client/src/services/services/timeWeatherService.ts
  - Accept: two clients differ by ≤ 30 game seconds after 1 h, and their weather matches.
- [ ] **F25-T08** Radstorm and weather radiation on the server (UNAM spell → F20 effect / F08 Rads), exterior check, `onRadstormExposure`; Far Harbor fog per the D-real finding — M — Depends: F25-T05, SRV-020, F08 — Verify: L-unit, G-manual
  - Accept: the `[Radstorm]` cases pass. In game, rads rise only outdoors during a radstorm.
- [ ] **F25-T09** Rest policy: `disabled` (SetInChargen; SleepWaitMenu/SitWaitMenu closed, with CLI-021) and `benefitsOnly` (`RestService`, validation, effects, Papyrus sleep/wait events, client widget) — M — Depends: F25-T01, F25-T04, F07, F20 — Verify: L-unit, G-manual — Files: skymp5-server/cpp/server_guest_lib/RestService.{h,cpp} (new), falloutmp-client/src/services/services/restService.ts, unit/RestServiceTest.cpp
  - Accept: the `[Rest]` cases pass. In `disabled` mode the wait key does nothing.
- [ ] **F25-T10** (T2) Per-region weather slots (REGN areas, e.g. the Glowing Sea), with region resolved from the server position — M — Depends: F25-T05, F25-T03 — Verify: L-unit, G-manual
  - Accept: a player inside the Glowing Sea region sees its weather while the rest of the Commonwealth is clear.
- [ ] **F25-T11** Admin and docs: console `mp settime` / `mp setweather` (F30-T01), DOCS-003 section, G-manual script — S — Depends: F25-T05, F30-T01 — Verify: L-int, G-manual — Files: docs/falloutmp/test-scripts/F25-time-weather.md
  - Accept: an admin changes the time and the weather and both clients follow within 2 s.

## 8. Open questions & risks
- Global IDs 0x35–0x39 are inferred (world-economy §5 item 5). They must be verified before F25-T07 ships.
- Do radstorm rads apply under roofs in exterior cells? Measure in vanilla (G-manual) before choosing the exterior-only rule.
- `rest.mode` T2 "vote" (all players in a region asleep → advance the clock) affects every timer. Leave it to gamemodes via `mp.setWorldClock`, or build it in? Decide with the user.
- The Sky IDs are unverified in TE. The fallback is the Papyrus `Weather.ForceActive` path through reflection.
