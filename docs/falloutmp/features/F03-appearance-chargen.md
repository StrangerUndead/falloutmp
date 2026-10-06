# F03 — Appearance & Character Creation (LooksMenu)

| Field | Value |
|---|---|
| Tier | T0 |
| Target level | L3 (SkyMP L3; plus server-side content validation, which SkyMP lacks) |
| SkyMP analogue | `UpdateAppearance` (4) + `SetRaceMenuOpen` (29), accepted only while `isRaceMenuOpen` (`ActionListener.cpp:209-228`), `appearanceDump` persistence, `sync/appearance.ts`. SkyMP level L3 (reference/skymp-sync-inventory.md §2 "Appearance / character creation") |
| Milestone | M5 (MVP: race, sex, head parts, hair colour, presets), M7 (full LooksMenu flow, live morphs, surgery) |
| Workstreams | PLAT, CLI, SRV, NET, ESPM, GM |
| Depends on | F00, F33 (world entry), PLAT-070 (per-player runtime `TESNPC`), PLAT-082 (appearance natives + Reset3D), ESPM-006 (NPC_/RACE layouts), NET-002, NET-003, F19 (creation SPECIAL), F31 (character name) |
| References | reference/prior-art.md §3.1.4, §3.2.3, §5.1 B2/B3/B15/C2/C3, §5.3 Adopt 3, §5.4 Q1; reference/fo4-data-formats.md §4.4, §4.5; reference/fo4-systems-world-economy.md S19; reference/papyrus-api-map.md (`Game.showRaceMenu` row, menu table); reference/skymp-sync-inventory.md §1.3 rows 7/9/10, §1.10; reference/skyrim-coupling-index.md (SetRaceMenuOpenMessage row) |

## 1. Summary
A new player skips the pre-war intro and Vault 111. They land in a quiet staging spot and the FO4 face/body editor (`LooksMenu`) opens. There they pick sex, race, preset, head parts (hair, beard, eyes, scars), face morph sliders, facial-bone regions, body shape (thin/muscular/large triangle), tints/face paint and hair/skin colour. When they confirm, the server validates the result against the catalogue of playable values in the loaded plugins, stores it, and every other client builds the same face and body on that player's actor. Late joiners and reconnects see the same face. A server can let players re-edit at a plastic surgeon (Diamond City's Mega Surgery Center) for caps. The server can also open the editor at any time through the gamemode.

## 2. Vanilla Fallout 4 behaviour
- **Data model** (TESNPC face block, size 0x308; prior-art §5.1 B2) [src: F4SE@6f6a7ca:f4se/GameObjects.h:116-246; CLF4-libxse@7c8c6f8:include/RE/T/TESNPC.h:174-209]:
  - `headRelatedData` 0x248 (hair colour CLFM at +0);
  - `morphWeight` 0x278 (thin/muscular/large);
  - `headParts` 0x2D0 + `numHeadParts` 0x2E8;
  - `morphRegionSliderValues` 0x2D8 (5 floats);
  - `facialBoneRegionSliderValues` 0x2E0 (FMRI → 8 floats);
  - `bodyTintColor` RGBA 0x2EA;
  - `morphSliderValues` 0x2F8 (MSDK → MSDV map);
  - `tintingData` 0x300.
- **ESM mirror** of the same data in NPC_ (fo4-data-formats §4.4): `RNAM` race, `PNAM[]` head parts, `HCLF`/`BCLF` hair/facial-hair colour, `FTST` head texture, `MWGT` 3 floats, `MRSV` 5 floats, `MSDK`/`MSDV` parallel arrays, `FMRI`+`FMRS` (pos[3], rot[3], scale), `FMIN` intensity, `TETI`+`TEND` tint layers, `QNAM` RGBA skin tint.
- **Catalogue sources** (fo4-data-formats §4.5):
  - RACE: `DATA` flag 0x1 Playable; per-sex head-part lists (`INDX`+`HEAD`), presets (`RPRM`/`RPRF`), hair colours (`AHCM`/`AHCF`), face details (`FTSM`/`FTSF`);
  - tint groups (`TTGP` → `TETI` slot/index, `TTEC` CLFM colour lists, `TTED` default);
  - morph groups (`MPGN`, `MPGS` slider indices, `MPPI` presets), face morphs (`FMRI`+`FMRN`), `MSID`/`MSM0`/`MSM1` morph values.
  - CLFM flags at +0x40 (`Playable` 1, `RemappingIndex` 2) separate the 32 real hair colours from 134 tint swatches (prior-art B15).
- **Change flags** that make the engine save and rebuild the face: 0x800 (head data, parts, skin colour, tints), 0x4000 (weights + morphs), 0x2000000 (race) (prior-art B3).
- **Apply needs an engine rebuild.** Writing the fields alone does nothing visible. `Actor::Reset3D(1,0,1,0)` (NG/AE 2229913) with `bUseFaceGenPreprocessedHeads=0` rebuilds the head (TE). It is asynchronous (prior-art C3, §3.2.3).
- **Tint pitfalls** (FO4_Wrld): there are two tint arrays (TESNPC + an actor-side mirror the compositor reads); intensity 0 means OFF; the "None" swatch removes the layer; template defaults must be re-applied first because removal compares against defaults with exact float equality (prior-art §3.1.4).
- **Menu:** `Game.ShowRaceMenu(akMenuTarget, uiMode, spouseF, spouseM, vendor)` opens `LooksMenu`. `uiMode` 0 = start-of-game chargen (spouse logic), 1 = remake (player only, no sex change), 2 = haircut, 3 = surgery, 4 = face paint [web: https://falloutck.uesp.net/wiki/ShowRaceMenu_-_Game, via papyrus-api-map]. `SetInChargen`, `PrecacheCharGen`, `OnLooksMenuEvent` exist (world-economy S19).
- **Single-player assumptions that break:**
  - the editor is reached only through the MQ101 intro (mirror, spouse, Vault-Tec rep); the result is committed locally;
  - there is one player base (0x7). Remote players need their own runtime `TESNPC` (PLAT-070; never base 0x7, prior-art Adopt 1);
  - `ShowRaceMenu` pauses the local game, but the server world keeps running.
- **Optional LooksMenu mod** (F4SE plugin) adds BodyGen body morphs (named morph → float, per keyword) and overlays. Its Papyrus API (`BodyGen.SetMorph(actor, isFemale, morph, keyword, value)`, `UpdateMorphs`) is out-of-tree [web: https://www.nexusmods.com/fallout4/mods/12631] [inference: API names, verify against the mod's .psc].

## 3. SkyMP baseline
- Server: `OnUpdateAppearance` accepts only while `IsRaceMenuOpen()`, then clears the flag, stores `appearanceDump` and relays to neighbours. There is **no content validation** (race, ranges, head parts) [src: ActionListener.cpp:209-228]. `onUpdateAppearanceAttempt` is a warning-only event.
- Actors with an empty `appearanceDump` get `isRaceMenuOpen = true` on load [src: MpActor.cpp:590-597]. Spawn opens the menu for new characters [src: ts/systems/spawn.ts:293-332].
- Persistence: `appearanceDump` JSON string, parsed on every `GetAppearance` (hotspot, skymp-sync-inventory §5). Invalid appearance on load skips the whole form [src: WorldState.cpp:182-210].
- Client: race-menu close → `getAppearance` → `UpdateAppearance`; `applyAppearance` recreates remote clones.
- **Reuse:** the open-flag gate, `isRaceMenuOpen` persistence, `SetRaceMenuOpen` (29) with a game-neutral meaning ("character editor open", skyrim-coupling-index), the deferred appearance channel, the event.
- **Replace:** the `Appearance` payload (twin `UpdateAppearanceFo4` 66, ADR-009), capture and apply natives, the validation (new), the onboarding flow.

## 4. Design

### 4.1 Authority model
Class A. The client edits locally in `LooksMenu`, but the result is only an **intent**. The server validates it against the catalogue and decides what is stored and relayed. The editor window is server-controlled (`isRaceMenuOpen` + `looksMenuMode`). Remote rendering is class D once the validated appearance has been delivered.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Appearance (FO4) | `AppearanceFo4` | `MpActor` (parsed cache) | `appearanceDump` (JSON, `"schema":"fo4-1"`) | empty → editor forced open |
| Editor open | bool | `isRaceMenuOpen` | yes (existing) | true when dump empty |
| Editor mode | enum `create\|remake\|surgery\|haircut\|facepaint` | `looksMenuMode` (new) | yes | `create` when dump empty, else `remake` |
| Appearance revision | u32 | `appearanceRev` (new) | yes | 0 |
| Pending surgery charge | {caps, vendorRef} | `MpActor` (transient) | no (cleared on close) | — |
| Chargen catalogue | per race×sex sets/ranges | `Fo4ChargenCatalogue` (WorldState, built once from ESM) | no | ESM |

`AppearanceFo4` fields (IDs are global form ids on the wire, `FormDesc` in JSON):
- `isFemale`, `raceId`, `hairColorId` (CLFM), `facialHairColorId` (CLFM), `headTextureSetId` (TXST);
- `headPartIds[]` (sorted, ≤ 16);
- `bodyMorph[3]` (thin, muscular, large);
- `morphRegions[5]` (head, upperTorso, arms, lowerTorso, legs);
- `morphSliders[] {key u32, value f32}` (≤ 128);
- `faceRegions[] {index u32, pos[3], rot[3], scale}` (≤ 64), `faceMorphIntensity`;
- `tints[] {tintIndex u16, dataType u8, value u8, rgba u32, templateColorIndex s16}` (≤ 64);
- `skinTone` RGBA u32;
- `bodyGen?[] {morph string ≤ 32, value f32}` (≤ 64; only when `appearance.bodyGen.enabled`).

The display name is **not** part of `AppearanceFo4`. SkyMP's `Appearance.name` moves to F31's `displayName` property (documented deviation).

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `UpdateAppearanceFo4` (66) | C→S | `idx` (own actor only), `rev`, `data: AppearanceFo4` | R | once, when LooksMenu closes | twin of 4 |
| `UpdateAppearanceFo4` (66) | S→C | `idx`, `rev`, `data` | R | on accept (relay to neighbours), on `mp.set(appearance)`, or as a correction to the owner | twin of 4 |
| `SetRaceMenuOpen` (29) | S→C | `open` (unchanged) | R | `mp.setRaceMenuOpen`, login of a new character, reject with retry | reused, game-neutral meaning |
| `UpdateProperty` (7) | S→C | `looksMenuMode` (owner only), sent before `SetRaceMenuOpen{open:true}` | R | on mode change | reused |
| `CreateActorFo4` (64) | S→C | `appearance?` (validated `AppearanceFo4`), owner-only `props.isRaceMenuOpen`, `props.looksMenuMode` | R | on subscribe | reused (F00) |
| `ProgressionRequest` (90) | C→S | `creationSpecial` (F19) | R | after appearance accepted in `create` mode | reused (F19) |

Binary budget: `AppearanceFo4` ≤ 2 KB (typical ~0.8 KB); a larger message is rejected (NET-008).

### 4.4 Client capture (owner side)
- `MenuOpenCloseEvent` for `LooksMenu` (PLAT-042). On **close**, call `getAppearanceFo4(player)` (PLAT-082: reads the face block of the player `TESNPC`, resolves form ids to global ids) and send `UpdateAppearanceFo4` with `rev = lastRev + 1`.
- Only send if the server opened the editor (`isRaceMenuOpen` mirrored in `sp.storage`, S21). A LooksMenu opened by anything else (a stray vanilla script or console) is closed immediately and the stored appearance is re-applied.
- Opening: on `SetRaceMenuOpen{open:true}` (or `props.isRaceMenuOpen` in the `isMe` snapshot), after world entry has finished (F00 §4.5 step 2):
  1. call `Game.ShowRaceMenu(None, uiMode, None, None, None)` with all five arguments (papyrus-api-map Sig-diff);
  2. map modes: `create` → 0 (no spouse refs passed; fallback 1 plus a client-side sex toggle if 0 misbehaves, open question), `remake` → 1, `surgery` → 3, `haircut` → 2, `facepaint` → 4.
- `SetRaceMenuOpen{open:false}` while open → close the menu and re-apply the stored appearance.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Native path** (PLAT-082): `applyAppearanceFo4(actor, data)`:
  1. writes the face block of the actor's runtime `TESNPC` (PLAT-070), with race switch if needed;
  2. re-applies the race/sex template defaults before the tints (FO4_Wrld tint rule), writes both tint arrays, skipping intensity-0 layers;
  3. sets change flags 0x800 | 0x4000 (| 0x2000000 on a race change);
  4. calls `Reset3D(1,0,1,0)` with `bUseFaceGenPreprocessedHeads=0` (TE); the native resolves a promise when the 3D is loaded again.
- **Stream-in order** (F02 §4.5): appearance first; equipment (F05) and the animation keyframe wait for the `Reset3D` promise.
- **Late joiners:** `CreateActorFo4.appearance`. **An actor without a stored appearance is not rendered to others**: the server omits it from neighbours' snapshots until the first accepted appearance, so nobody sees a default-faced puppet.
- **Owner reconnect:** the `isMe` snapshot carries the appearance. The client applies it to the player under the entry curtain before handing control (F33 §4.5.5). FO4_Wrld lesson: never publish the save's default look (prior-art §3.1.4).
- **Correction:** the owner gets `UpdateAppearanceFo4` with the stored appearance and re-applies it (no editor). If the stored appearance is empty (`create`), the correction is `SetRaceMenuOpen{open:true}` plus a notification with the reason.
- **Respawn:** nothing to do (appearance is base data). **Power armor:** the race switch to `PowerArmorRace` keeps the face block (F17); after exit, re-apply if the head looks wrong (G-self check).
- **Optional BodyGen:** if the server enables it and the client has the LooksMenu plugin (manifest check, F00-T09), call `BodyGen.SetMorph` for each morph and then `UpdateMorphs`. Without the plugin, BodyGen data is ignored (vanilla body).

### 4.6 Validation & anti-cheat
Checked in this order. Each failure rejects the whole message (no partial accept) and sends the §4.5 correction:
1. Sender owns `idx` (S6). Hosts can never send appearance for NPCs (SkyMP rule).
2. `isRaceMenuOpen` is true (SkyMP gate). `rev` is greater than the stored `appearanceRev`.
3. **Mode rules:**
   - `create`: everything may change;
   - `remake`/`surgery`: race and sex are unchanged;
   - `haircut`: only hair, beard and hair colours change;
   - `facepaint`: only tints in face-paint groups change.
4. **Catalogue** (`Fo4ChargenCatalogue`, built from RACE/HDPT/CLFM at load):
   - race is playable (RACE DATA 0x1) and in the GameProfile list of chargen races (Human by default; Ghoul/Synth only if configured);
   - each head part is in the race×sex list and passes the vanilla menu's four HDPT tests (prior-art §3.1.4); at most one per HDPT type;
   - colours are CLFM with `Playable`; tints are indices of that race×sex's tint groups with a colour from that group's `TTEC` list;
   - morph slider keys exist in the race's `MPGS`/`MSID`; face-region indices exist in the race's `FMRI`.
5. **Ranges** (GameProfile, calibrated by F03-T11): `bodyMorph` each in [0,1] with a sum of 1 ± 0.01; `morphRegions` in [−1,1]; sliders in [0,1]; face-region pos/rot/scale within calibrated bounds [inference: bounds to be measured]. Counts and sizes are within §4.2 limits. Every float is finite (no NaN/Inf).
6. **Gamemode veto:** `onUpdateAppearanceAttempt(actor, appearance, isAllowed)` becomes **blockable** (return `false` → reject).
7. **Surgery payment:** the server re-checks caps at commit (S13: copy, deduct, commit). If caps are missing, reject and keep the menu closed.

On accept:
- clear `isRaceMenuOpen`, store, `appearanceRev = rev`;
- relay `UpdateAppearanceFo4` to neighbours;
- fire `onCharacterCreated` (`create` mode only);
- if F19 creation SPECIAL is pending, prompt it next (§4.9).

A no-change close (data equal to the stored appearance) is accepted as a no-op: no charge, no relay.

### 4.7 Audience / visibility
- `UpdateAppearanceFo4`: grid neighbours (3×3) plus the owner on correction.
- `SetRaceMenuOpen`, `looksMenuMode`: owner only (S8).
- The snapshot carries the appearance to everyone who subscribes.

### 4.8 NPC parity
- ESM NPCs: the appearance comes from the base `NPC_` and the server-evaluated template chain (F13/F14). The client builds it from data, so the server sends no appearance.
- `mp.createActor` actors and gamemode overrides (`mp.set(npc,'appearance')`) carry an explicit `AppearanceFo4` in the snapshot. It is validated against the catalogue with the NPC's race (non-playable races allowed for NPCs).
- Hosts cannot change NPC appearance.

### 4.9 Gamemode API & server Papyrus
- **Properties:**
  - `mp.get/set(actor, 'appearance')`: FO4 shape. `set` validates against the catalogue unless `{force:true}` (SkyMP-plus);
  - `mp.get(actor,'isRaceMenuOpen')`.
- **Natives:** `mp.setRaceMenuOpen(actor, open, mode?)`, where `mode` defaults to `remake` (or `create` with no stored appearance).
- **Events:** `onUpdateAppearanceAttempt` (blockable now), `onCharacterCreated(actor)` (not blockable), `onLooksMenuOpen(actor, mode)` (observe).
- **Default gamemode (GM-010):**
  1. new profile → spawn in the staging spot (the start point, or the template's spawn from F33-T06, invulnerable while the editor is open; F11 rejects damage to actors with `isRaceMenuOpen`, setting `appearance.protectWhileEditing`, default true);
  2. editor `create`;
  3. on accept, F19 creation SPECIAL (`ProgressionRequest.creationSpecial`), then F31 name;
  4. teleport to the start point (F00-T06).
- **Plastic surgery** (gamemode option `appearance.surgery = {enabled, costCaps: 100, vendors: [FormDesc], modes}`): activating a configured surgeon or chair (F07 `onActivate`) calls `setRaceMenuOpen(actor, true, "surgery")` and records the pending charge. Caps are deducted on accept (F04/F23).
- **Server Papyrus:** `Game.ShowRaceMenu(akTarget, uiMode, …)` → `SetRaceMenuOpen` with uiMode→mode mapping (S15). `OnLooksMenuEvent` is dropped (papyrus-api-map).

### 4.10 Edge cases & failure modes
- **Disconnect with the editor open:** `isRaceMenuOpen` persists, so the editor reopens on reconnect. A pending surgery charge is dropped.
- **Reject loop:** after `appearance.maxRejects` (default 5) failed commits in `create` mode, the gamemode is notified (`onUpdateAppearanceAttempt` sees the count) and may apply a preset.
- **Load-order change** that removes a head part or colour: per-field defensive parse (§8 rule 2) drops the unknown entries, logs them, and flags the actor for the editor in `remake` mode. The form is never skipped.
- **Concurrency:** `mp.set(appearance)` while the editor is open bumps `appearanceRev`, so the late client commit is rejected as stale and gets a correction.
- **Hot reload** (S21): the editor state lives in `sp.storage`.
- **Server restart:** the dump is parsed once per actor and cached (fixes the SkyMP hotspot).

### 4.11 Performance budget
- Appearance ≤ 2 KB per snapshot and per change. Changes are rare (≤ 1/min per player).
- Server validation ≤ 50 µs (hash-set lookups in the catalogue).
- Client `Reset3D` is asynchronous. Remote face builds are queued so at most 2 per frame start (stream-in bursts).

## 5. Engine / platform work required
- PLAT-082:
  - `getAppearanceFo4(actor)`, `applyAppearanceFo4(actor, data)` (face block write, both tint arrays, change flags, `Reset3D` promise, `bUseFaceGenPreprocessedHeads` toggle);
  - head-part/colour lookups by global id.
- PLAT-070: a per-player runtime `TESNPC` so that each remote player has its own face block.
- PLAT-042: `LooksMenu` open/close events; a forced menu close.
- PLAT-034: `ShowRaceMenu` with five explicit arguments.
- **RE/verification (prior-art §5.4 Q1):** does `Reset3D` after writing morph sliders, facial-bone regions, morph regions and body weights rebuild a *live* actor correctly? Prototype in F03-T05.

## 6. Tests
- `L-unit` (`[F03]`, `[Appearance]`):
  - `AppearanceFo4` binary/JSON round trip, size limits;
  - accept while open → stored, relayed to neighbours (not the sender), flag cleared;
  - reject when closed → correction `UpdateAppearanceFo4` to the owner;
  - reject non-playable race / foreign head part / non-playable CLFM / tint colour not in its group / out-of-range slider / NaN → correction;
  - mode rules (surgery race change rejected; haircut changing morphs rejected);
  - stale `rev` rejected;
  - surgery without caps rejected, with caps deducted exactly once;
  - no-op close not charged;
  - gamemode veto (FakeListener `false`);
  - wrong owner (`idx` of another actor) rejected;
  - late-joiner snapshot contains the appearance; an actor without appearance is absent from neighbours' snapshots;
  - persistence round trip incl. `looksMenuMode`, `appearanceRev`; backward compat (missing fields → defaults);
  - unknown head part on load drops only that field.
- `L-fixture`: catalogue builder against a synthetic RACE/HDPT/CLFM plugin (ESPM-002 `PluginBuilder`).
- `L-ts`: mode→uiMode mapping; the editor-state machine (open/close/correction) with a mocked platform.
- `D-real` (`[fo4data]`): the catalogue from Fallout4.esm has 32 playable hair colours, and HumanRace head-part counts match FO4_Wrld's numbers (420 HDPT records, 9 tint groups / 143 templates / 546 colours, prior-art §3.1.4).
- `G-self`: apply 5 fixed recipes to a remote test actor; screenshot hashes; read back with `getAppearanceFo4` → equal.
- `G-manual`: two players. A creates a character, B sees it; A reconnects; A uses surgery; B joins late and sees A's new face; repeat in 1st/3rd person.

## 7. Tasks
- [ ] **F03-T01** `AppearanceFo4` struct + `UpdateAppearanceFo4` (66) binary/JSON + FO4 `appearanceDump` schema with a cached parse — M — Depends: NET-002, NET-003 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/fo4/AppearanceFo4.{h,cpp}, falloutmp-server/cpp/messages/UpdateAppearanceFo4Message.h, Messages.h; falloutmp-client/src/services/messages/updateAppearanceFo4Message.ts; unit/AppearanceFo4Test.cpp
  - Accept: round trips pass; a 3 KB payload is rejected by the bounds-checked reader; the Skyrim `Appearance` path is untouched (`./unit/unit "~[espm]"` green).
- [ ] **F03-T02** `Fo4ChargenCatalogue` from RACE/HDPT/CLFM (minimal HDPT and CLFM views on top of ESPM-006) — M — Depends: ESPM-006 — Verify: L-fixture, D-real — Files: libespm/include/libespm/fo4/{HDPT,CLFM}.h, falloutmp-server/cpp/server_guest_lib/fo4/ChargenCatalogue.{h,cpp}; unit/ChargenCatalogueTest.cpp
  - Accept: the synthetic-plugin test enumerates head parts/colours/tints per race×sex; the `[fo4data]` counts match §6.
- [ ] **F03-T03** Server handler: open gate, `rev`, mode rules, catalogue + range validation, blockable `onUpdateAppearanceAttempt`, correction, relay, `onCharacterCreated` — M — Depends: F03-T01, F03-T02, NET-007 — Verify: L-unit — Files: ActionListener.cpp (FO4 handler), falloutmp-server/cpp/server_guest_lib/fo4/AppearanceService.{h,cpp}, gamemode_events/UpdateAppearanceAttemptEvent.cpp; unit/PartOne_UpdateLookFo4Test.cpp
  - Accept: every §6 accept/reject case passes, with a correction asserted for each reject.
- [ ] **F03-T04** Snapshot and audience: `CreateActorFo4.appearance`, actors without appearance hidden from neighbours, owner-only `looksMenuMode` — S — Depends: F03-T01, F00 — Verify: L-unit — Files: MpActor.cpp, PartOne.cpp; unit/PartOne_ActorTest.cpp
- [ ] **F03-T05** **Prototype:** live morph/weight/region rebuild via PLAT-082 natives on a runtime `TESNPC` and on the player (answers prior-art §5.4 Q1; tint default re-apply rule) — L — Depends: PLAT-070, PLAT-082 — Verify: G-self — Files: fallout4-platform/src/platform_fo4/AppearanceApi.cpp, tools/fo4-selftest/appearance.js
  - Accept: 5 recipes (including extreme sliders and face paint) round-trip with `getAppearanceFo4` equality and matching screenshots on two clients; the result is recorded in prior-art §5.4.
- [ ] **F03-T06** Client capture and editor flow: LooksMenu close → send; server-controlled open/close; uiMode mapping; stray-menu guard; `sp.storage` state — M — Depends: F03-T05, PLAT-042, CLI-001 — Verify: L-ts, G-manual — Files: falloutmp-client/src/sync/appearanceFo4.ts, falloutmp-client/src/services/services/looksMenuService.ts
- [ ] **F03-T07** Client apply: remote stream-in step 1 with a `Reset3D` promise gate for F05/F02; owner correction; reconnect adopt-server-first — M — Depends: F03-T05, F02-T08 — Verify: G-manual — Files: falloutmp-client/src/view/formView.ts, falloutmp-client/src/sync/appearanceFo4.ts
- [ ] **F03-T08** Gamemode: `setRaceMenuOpen(actor, open, mode)`, `mp.set(appearance)` validation/`force`, onboarding in the default gamemode (staging → create → SPECIAL → name → start point), surgery service with caps — M — Depends: F03-T03, GM-010, F19 (creation SPECIAL) — Verify: L-int — Files: falloutmp-server/ts (mp API typings), falloutmp-gamemode/src/systems/{chargen,surgery}.ts
  - Accept: an `L-int` bot completes onboarding; surgery charges 100 caps once.
- [ ] **F03-T09** Persistence: `looksMenuMode`, `appearanceRev`, per-field defensive parse of the FO4 dump, unknown-id handling → editor `remake` — S — Depends: F03-T01, REF-020 — Verify: L-unit — Files: MpChangeForms.{h,cpp}; unit/AppearanceFo4Test.cpp
- [ ] **F03-T10** Optional BodyGen (LooksMenu mod) capture/apply behind `appearance.bodyGen.enabled` with a manifest requirement — S — Depends: F03-T06, F00-T09 — Verify: G-manual — Files: falloutmp-client/src/sync/bodyGen.ts
- [ ] **F03-T12** LooksMenu integration (ADR-021): evaluate LooksMenu's preset JSON (head parts, morph sliders, region morphs, tints, body morphs) as the `AppearanceFo4` schema and its F4SE/Papyrus API to apply presets to the player and to remote actors; if viable, route capture/apply through it with the PLAT-082 native path as fallback when the plugin is absent or the API fails — M — Depends: PLAT-095, F03-T06 — Verify: G-self — Files: falloutmp-client/src/sync/appearanceLooksMenu.ts, fallout4-platform natives
  - Accept: a face created in LooksMenu round-trips through the server and appears identical on a second client; without LooksMenu the native path produces the same result for vanilla fields.
- [ ] **F03-T11** Range calibration: dump LooksMenu slider min/max and default recipes per race×sex into GameProfile data — S — Depends: F03-T05 — Verify: G-self — Files: falloutmp-server/cpp/server_guest_lib/game_profile/fallout4/chargen_ranges.json
- [ ] **F03-T13** `G-manual` script and sign-off — S — Depends: F03-T07, F03-T08 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F03-appearance.md

## 8. Open questions & risks
- R: does `Reset3D` rebuild morphs on a live actor (prior-art §5.4 Q1)? Fallback: TE's change-form serializer blob (`TESNPC::Serialize` with form-id remapping, prior-art §3.2.3) as the apply path, with the server still storing the parsed recipe.
- Does `ShowRaceMenu(uiMode 0)` without spouse refs work outside MQ101? Otherwise use uiMode 1 plus a sex toggle (the engine forbids sex change in remake).
- Which races are playable by default (Human only vs. Ghoul/Synth via mods)? This is a GameProfile list, decided by the server owner.
- Face-texture compositing cost with many remote players (FO4_Wrld: global render-target pool slots 16–18, prior-art §3.1.4). Measure FPS with 20 distinct faces (G-self).
