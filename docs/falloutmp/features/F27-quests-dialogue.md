# F27 — Quests, Dialogue & Scenes

| Field | Value |
|---|---|
| Tier | T0 (vanilla quests blocked + template-save hygiene), T2 (gamemode quest framework, dialogue, scenes) |
| Target level | L3 for gamemode quests (SkyMP L0–L1: `Quest` never serialized, `GetStage` returns 0) |
| SkyMP analogue | `blockPapyrusEventsService` on the client; the server `Quest` struct exists but is not persisted; `PapyrusQuest::GetStage` returns 0. SkyMP level L0–L1 — code: `skymp5-client/src/services/services/blockPapyrusEventsService.ts`, `falloutmp-server/cpp/server_guest_lib/Quest.h:6-43`, `falloutmp-server/cpp/server_guest_lib/script_classes/PapyrusQuest.cpp:5-38` |
| Milestone | M4 (allow-list + hygiene, needed by F00-T05), M12 (framework, dialogue; GM-040 demo) |
| Workstreams | CLI, PLAT, SRV, PVM, NET, GM, FRONT, ESPM |
| Depends on | PLAT-036 (`blockPapyrusEvents`), PLAT-040 (quest events), SRV-030 (script stripping), PVM-007, PVM-013, PVM-015, PVM-016, CLI-070, SRV-060, F00-T05, F07 (NPC activation), F19 (Charisma), F13 |
| References | reference/fo4-systems-world-economy.md §2 S14, S12 (radio quests), S10 (scene holotapes), S19; reference/skymp-sync-inventory.md §1.13, §2 ("Quests" row), §3.2 I11, I16; reference/papyrus-api-map.md §1 Quest, Alias, ReferenceAlias, Scene, Topic, TopicInfo, Message, §5.6; reference/prior-art.md §3.2.6, §5.1 C14; ADR-011 |

## 1. Summary
Vanilla quests do not run in FalloutMP, which is SkyMP's policy (ADR-011, a non-goal in 00-vision §6). Clients stop every vanilla quest that is not on an allow-list. The allow-list keeps cosmetic systems such as radio stations. The server strips vanilla scripts. The template save leaves no quest objectives on the HUD. Servers then author their own quests:
- in server Papyrus, or in gamemode JS;
- with stages, objectives, aliases and quest targets;
- shown in the real Pip-Boy quest log and HUD through generic "quest slot" records in a FalloutMP plugin.

Quest state lives on the server and is persisted. It is either per player (instanced), server-wide (shared) or per party. At T2, gamemode NPCs can hold server-driven dialogue with choices and Charisma speech checks.

## 2. Vanilla Fallout 4 behaviour (engine facts that matter for sync)
- **Quests:** 191 in the base game, 272 with add-ons. The main quest MQ101 → MQ102 → … runs in the prologue and Vault 111. Faction lines are mutually exclusive. Story-manager radiants randomize their aliases [web: wiki:Fallout_4_quests].
- **Records:** QUST (stages, objectives, aliases, fragments), DIAL/INFO, SCEN with phases and actions [src: xEdit wbDefinitionsFO4.pas:6309, 8879, 9672, 10684].
- **Quest natives** (papyrus-api-map §1 Quest):
  - `SetStage`/`GetStage`/`GetStageDone` are script wrappers over `SetCurrentStageID` [L], `GetCurrentStageID`, `IsStageDone`;
  - also `SetObjectiveDisplayed/Completed/Failed`, `CompleteQuest`, `Start` [L], `Stop`, `Reset`, `IsRunning`, `GetAlias`;
  - events `OnQuestInit`, `OnQuestShutdown`, `OnStageSet(stage, item)`, and about 40 `OnStory*` events.
  - `ReferenceAlias.ForceRefTo/GetReference/Clear`. An alias receives every event of the reference that fills it.
- **Engine:** `TESQuest::SetStage` NG/AE 2207743 (prior-art §5.1 C14). Fallout Together synced quests through `TESQuestStageEvent`/`TESQuestStartStopEvent` sinks (prior-art §3.2.6).
- **Scenes:** `Scene.Start/ForceStart/Stop/IsPlaying/Pause`, events `OnBegin/OnEnd/OnPhaseBegin/OnPhaseEnd/OnAction`. `TopicInfo.OnBegin/OnEnd`. Radio stations are quest scenes on transmitter refs (world-economy S12).
- **Dialogue:** a 4-option wheel. `DialogueMenu{dialogueButtonOBJs[4], speechChallengeAnimObj, isLookingAtPlayer}` [src: CLF4 D/DialogueMenu.h]. Speech-check chance = CHA × 15 % − difficulty, clamped to 5–100 % [web: wiki:Charisma_(Fallout_4)].
- **Single-player assumptions that break:**
  - quests change shared world state (enable parents, deaths, faction relations, settlement ownership);
  - aliases assume one player;
  - scenes run on one client;
  - outcomes are exclusive.
  - The template save carries MQ state from Vault 111 (F00 §4.5 step 5).

## 3. SkyMP baseline
- **Vanilla quests:** not synced. The client blocks vanilla Papyrus events with an allow-list (`blockPapyrusEventsService`). The server strips vanilla scripts by whitelist/blacklist (`MpObjectReference::InitScripts`, skymp-sync-inventory §1.13).
- **Quest state:** `MpChangeFormREFR.quests` exists but is never written or serialized (I11, world-economy B15). `GetStage` returns 0.
- **Server Papyrus:** `HeuristicPolicy` picks the "player" for natives such as `Game.GetPlayer()`. SpSnippet mirrors client-visible natives; it reaches only player actors (I16).
- **Reuse:** the blocking allow-list concept, script stripping, `HeuristicPolicy`, SpSnippet, `GameModeEvent`.
- **Replace:** the quest state model, which is a full FO4 implementation.

## 4. Design

### 4.1 Authority model
- **Vanilla quests:** not synced. They are forced to "stopped" on every client by policy. Allow-listed quests (radio, ambient) run locally as class D.
- **Gamemode quest state:** class A. Stage, objectives, aliases and targets are decided by the server. The client only displays them.
- **Dialogue with gamemode NPCs:** class A. The server chooses lines, valid choices and outcomes, and rolls speech checks.
- **Ambient vanilla greetings and barks** on hosted NPCs: class D.

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Vanilla quest policy | `{mode: "block"|"allowList", allow[] EDID}` | settings `quests.vanillaPolicy`, `quests.vanillaWhitelist` | no (settings) | GameProfile default list (F27-T01) |
| Instanced quest progress | `QuestProgress{questKey, stage u16, stagesDone set, objectives map<u8,{state,text?}>, aliases map<string,FormDesc>, vars json, startedGameDays, flags}` | `MpChangeFormREFR.quests` of the player actor (fixes I11) | yes | empty |
| Shared quest state | same struct, one per `questKey` | ADR-010 record `questWorld` | yes | — |
| Party quest state | same struct keyed `party:<id>` (party id from the gamemode) | ADR-010 record `questWorld` | yes | — |
| Slot assignment | `questKey → slot 0..15` per player | `MpActor` (derived on spawn) | no | — |
| Dialogue session | `{nonce, speakerRef, nodeId, openedMs}` | `MpActor` (memory) | no | — |
| Quest definitions | JS objects, or QUST + PEX in the server data dir | gamemode / data | no (code) | — |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `QuestUpdate` (114) | S→C | `slot u8`, `op u8` (set, clear), `questKey str≤64`, `title str≤128`, `stage u16`, `flags u8` (active, completed, failed, tracked), `objectives[≤16] {index u8, state u8 (hidden, displayed, completed, failed), text str≤256}`, `targets[≤8] {objectiveIndex u8, refId u32, pos f32[3], worldOrCell u32}` | R | on change (deferred overwrite channel per slot); all slots after spawn; ≤ 1.5 KB | new (114, this spec) |
| `DialogueAction` (115) | C→S | `nonce u32`, `op u8` (open, choose, close), `speakerRefId u32`, `choiceId u16` | R | user action | new (115, this spec) |
| `DialogueAction` (115) | S→C | `nonce u32`, `op u8` (show, close, denied), `speakerRefId u32`, `line {text, voicePath?, durationMs}`, `choices[≤4] {id u16, text, flags u8 (speechCheck), chancePct u8}`, `reason u8` | R | per node | new (115) |
| `SpSnippet` (30) | S→C | one-shot UI: `Debug.Notification`, `Message.Show`, `Message.ShowAsHelpMessage`, `InputEnableLayer` | R | on native call | reused |

Why objective state is not sent as SpSnippet calls:
- SpSnippet is an append-only RPC, so a reconnecting client would miss the state.
- `QuestUpdate` is a snapshot per slot, so reconnect and late load re-render it (S7, S10).
- SpSnippet stays the path for one-shot UI.

### 4.4 Client capture (owner side)
- **Hygiene (with F00-T05):**
  - after `postLoadGame`, enumerate running quests (`getRunningQuests()` native);
  - `Stop()` every quest not on the allow-list;
  - hide its displayed objectives (`SetObjectiveDisplayed(i, false)`);
  - repeat on every `TESQuestStartStopEvent`, because the story manager starts radiants on location changes;
  - Papyrus events from non-allow-listed scripts are blocked (PLAT-036).
- **Dialogue:**
  - a vanilla `DialogueMenu` is closed on open, unless the session belongs to a gamemode dialogue in `dialogue.ui = "vanilla"` mode (T2+);
  - activating an NPC goes through F07 `Activate`. The server decides whether a gamemode dialogue opens;
  - the front widget's choices arrive as `browserMessage` and are sent as `DialogueAction{choose}`.
- **Tracking:** the active quest toggle in the Pip-Boy stays local.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
- **Quest slots:** a generated `FalloutMP.esl` (F27-T07, our own records) contains `FMP_QuestSlot00..15`:
  - each slot has 16 objectives, and objective *n* targets alias `ObjTargetN`;
  - `ObjTargetN` is an optional ReferenceAlias, 8 per slot.
- **Applying `QuestUpdate{set}`:**
  1. `Start()` the slot quest.
  2. Set the title with F4SE `Form.SetName` on the QUST [inference: verify that QUST's TESFullName is honoured by the Pip-Boy].
  3. Write objective texts with the native `setQuestObjectiveText(quest, index, text)` (RE `BGSQuestObjective` display text).
  4. Apply objective states with `SetObjectiveDisplayed/Completed/Failed`. The engine shows its own HUD banners.
  5. Targets:
     - `ForceRefTo(remoteIdToLocalId(refId))` when the ref is loaded;
     - otherwise a local XMarker is placed at `pos` and used, so the compass points at unloaded targets.
- **Fallback:** if the RE natives are missing, a CEF quest-tracker widget (FRONT) shows title and objectives. Compass targets still work through the aliases.
- **`QuestUpdate{clear}`:** `Stop()` + `Reset()` the slot.
- **Reconnect and respawn:** all slots are cleared on load, then the server replays them after `CreateActorFo4 isMe`.
- **Dialogue:**
  - the front widget shows the speaker name, the line and the choices;
  - an `InputEnableLayer` disables movement and fighting while it is open;
  - `voicePath` (T2) plays a loose sound at the speaker;
  - the speaker's host is asked to face the player through a SpSnippet `SetLookAt` (I16, SRV-040).

### 4.6 Validation & anti-cheat
- Clients never write quest state. The only C→S path is `DialogueAction`.

| Check (DialogueAction) | Reject → correction |
|---|---|
| Sender owns the actor; the speaker exists, is alive (F12), same `worldOrCell`, ≤ `dialogue.maxDistance` (512 u, I15), not hostile or in combat (F11/F13) | `DialogueAction{denied, reason}` |
| `open`: a gamemode dialogue is registered for the speaker (ref or base); gamemode `onDialogueOpen` allows it | `denied` |
| `choose`: the session exists, `nonce` matches, `choiceId` is offered by the current node and its predicate still passes; ≤ 5 actions/s | `DialogueAction{show}` re-sent with the current node |
| Speech check: rolled on the server with `CombatRng`, chance = clamp(CHA × 15 % − difficulty, 5, 100) using F19 SPECIAL plus F20 effects | result node chosen by the server |
| Gamemode `onDialogueChoice` returns false | current node re-sent |

- **Stage changes:** validated by the definition (unknown stage → `SetStage` returns false) and by `onQuestStage`.
- **Allow-listed vanilla quests** run only locally. The server ignores everything they would claim, because their scripts are stripped on the server and their Papyrus events are blocked on the client except for allow-listed scripts.

### 4.7 Audience / visibility
- **`QuestUpdate`:**
  - instanced: owner only;
  - shared: every online player in scope (all, or a gamemode filter);
  - party: each member.
- **`DialogueAction`:** owner only. An optional T2 `dialogue.broadcastLines` sends `show` lines as subtitles to grid neighbours.

### 4.8 NPC parity
- Aliases that point at hosted NPCs hold server ids. The host's AI is unaffected.
- Quest-driven NPC behaviour uses server natives mirrored to the host through SpSnippet (I16, SRV-040): `Actor.EvaluatePackage`, `SetLookAt`, `StartCombat`.
- Alias-attached AI packages are **not** supported (they would run only on the host). This is documented as a limitation.

### 4.9 Gamemode API & server Papyrus
**JS core API:**
- `mp.questStart(target, questKey, def?)`, `mp.questSetStage(target, questKey, stage)`, `mp.questGet(target, questKey)`;
- `mp.questSetObjective(target, questKey, index, state, text?)`, `mp.questSetTarget(target, questKey, objectiveIndex, refId|null)`, `mp.questStop(target, questKey)`;
- `target` = actorId (instanced), `0` (shared), or `"party:<id>"`.

**Dialogue:** `mp.dialogueShow(actorId, speakerId, node)` and `mp.dialogueClose(actorId)`. Dialogue graphs live in the gamemode.

**Gamemode TS layer:** `falloutmp-gamemode/src/quests/` declares quests and dialogue graphs.

**Events:**
- `onQuestStage(target, questKey, newStage, oldStage)` [blockable];
- `onQuestObjective(target, questKey, index, state)` (not blockable);
- `onDialogueOpen(actorId, speakerId)` [blockable];
- `onDialogueChoice(actorId, speakerId, nodeId, choiceId)` [blockable].

**Papyrus natives (server)**, for QUST forms of server-side plugins:
- `Quest`: `SetStage/GetStage/GetStageDone/SetCurrentStageID/GetCurrentStageID/IsStageDone`, `SetObjectiveDisplayed/Completed/Failed`, `IsObjective*`, `CompleteQuest`, `Start`, `Stop`, `Reset`, `IsRunning`, `IsCompleted`, `GetAlias`;
- `ReferenceAlias`: `ForceRefTo/GetReference/Clear`.

Papyrus quests are **shared** by default. One VM object exists per quest. A quest declared instanced resolves the target player through `HeuristicPolicy`; its script variables stay shared, and a load-time warning says so.

**Papyrus events:** `OnQuestInit` (server start for start-game-enabled gamemode quests), `OnStageSet`, `OnQuestShutdown`, `OnAliasInit`. Story events `OnStoryKillActor`, `OnStoryCraftItem`, `OnStoryPickLock`, `OnStoryClearLocation` and `OnStoryHackTerminal` are fired by the server for gamemode radiants (T2). `Scene` natives are stubs at P2 (PVM-015).

### 4.10 Edge cases & failure modes
- **Template-save leakage:** MQ102, Codsworth and radiant starts on location change are handled by the hygiene loop and its start-event trigger. G-self proves the HUD is clean.
- **Allow-listed quests with world effects:** only cosmetic quests may be allow-listed (review rule in F27-T01).
- **Disconnect mid-dialogue:** the session closes. Choices already applied are kept.
- **Server restart:** quest state reloads; dialogue sessions are dropped.
- **Gamemode hot reload:** definitions reload, and state is kept by `questKey`. If a stage no longer exists, the stage is kept and logged.
- **More than 16 active quests for one player:** the oldest untracked ones are not displayed but stay persisted.
- **Text:** UTF-8 from the server. Characters outside the FO4 font coverage render as boxes, so the front widget is the fallback.
- **Quest items:** F04 owns non-droppable items. The gamemode marks them, e.g. with a `SweetCantDrop`-style keyword.
- **Vanilla "Trade" topic:** routing it to F23 barter belongs to this dialogue layer (F23 §8). Until then, barter opens on activation.

### 4.11 Performance budget
- `QuestUpdate` ≤ 1.5 KB worst case; normally < 300 B, and rare.
- `DialogueAction` ≤ 1 KB.
- Hygiene scan ≤ 5 ms once per load.
- Server quest operations O(1) plus one deferred send.

## 5. Engine / platform work required
- `getRunningQuests()` (iterate the `TESDataHandler` quests), `stopQuest` / `resetQuest`.
- `TESQuestStartStopEvent` and `TESQuestStageEvent` sinks (PLAT-040).
- `setQuestObjectiveText` (RE `BGSQuestObjective`).
- F4SE `SetName` on QUST.
- Alias `ForceRefTo` through reflection.
- `DialogueMenu` suppression (close on open).
- `FalloutMP.esl` generator tool (own records only; built on the ESPM-002 PluginBuilder).
- The plugin must be in the server load order and the manifest (F00-T09).

## 6. Tests
- `L-unit` `[F27][Quest]`:
  - `QuestProgress` JSON round trip and back-compat (`quests` absent → empty);
  - shared, instanced and party scopes;
  - `onQuestStage` veto keeps the old stage and sends no update;
  - `QuestUpdate` sent to the right audience;
  - snapshot of every slot after spawn;
  - slot exhaustion;
  - message round trips.
- `L-unit` `[F27][PapyrusQuest]`:
  - SetStage/GetStage/GetStageDone/objective natives on a synthetic QUST + FO4 PEX fixture (PVM-010);
  - `OnStageSet` fired and `onPapyrusEvent:OnStageSet` observed;
  - alias `ForceRefTo` reflected in targets.
- `L-unit` `[F27][Dialogue]`:
  - open/choose/close accept path;
  - every reject (distance, dead, combat, bad nonce, bad choice) with its correction;
  - speech-check probability with a seeded RNG;
  - `onDialogueChoice` veto.
- `L-ts`: the hygiene allow-list filter; QuestUpdate → slot apply plan (mocked platform); dialogue widget state machine.
- `L-fixture`: the generated `FalloutMP.esl` parses with libespm and contains 16 slots × 16 objectives × 8 aliases.
- `L-int`: GM-040 demo; a bot gets a quest, kills targets, the stage advances, reconnects, and the quest is restored.
- `G-self`: after loading the template, no vanilla quest is running except the allow-list, and the HUD has no objectives. A slot shows the title, an objective and a compass target.
- `G-manual`: two players. A shared quest advances for both; an instanced quest advances for one only. A dialogue with a speech check.

## 7. Tasks
- [ ] **F27-T01** (M4, T0 — needed by F00-T05) Vanilla quest policy + allow-list data: default list from a G-self running-quest dump (radio stations and other cosmetic quests), settings `quests.vanillaPolicy/vanillaWhitelist`, review rule "cosmetic only" — S — Depends: PLAT-036, SRV-030 — Verify: L-ts, G-self — Files: falloutmp-client/src/config/vanillaQuestAllowList.ts, falloutmp-server/ts/settings.ts
  - Accept: the list is committed with its EDIDs and the reason for each entry. F00-T05 consumes it.
- [ ] **F27-T02** Extend the F00-T05 guard: re-run on `TESQuestStartStopEvent`, block vanilla scenes, suppress `DialogueMenu` — S — Depends: F00-T05, PLAT-040 — Verify: G-self — Files: falloutmp-client/src/services/services/questGuardService.ts
  - Accept: walking from Sanctuary to Concord starts no vanilla quest objective on the HUD.
- [ ] **F27-T03** Server quest state model: serialize `quests` (fix I11/B15), `QuestProgress`, ADR-010 `questWorld` record, scopes — M — Depends: REF-020, F00-T08 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/Quest.h, MpChangeForms.{h,cpp}, QuestService.{h,cpp} (new), unit/QuestServiceTest.cpp
  - Accept: the `[Quest]` persistence cases pass.
- [ ] **F27-T04** Server Papyrus Quest/Alias natives and events on `QuestService` — M — Depends: F27-T03, PVM-007, PVM-013 — Verify: L-unit — Files: falloutmp-server/cpp/server_guest_lib/script_classes/PapyrusQuest.cpp, PapyrusAlias.cpp (new), unit/PapyrusQuestTest.cpp
  - Accept: the `[PapyrusQuest]` cases pass. The Skyrim `Quest` behaviour is unchanged.
- [ ] **F27-T05** JS API `mp.quest*`, `onQuestStage`/`onQuestObjective`, and the gamemode TS framework layer — M — Depends: F27-T03 — Verify: L-int — Files: falloutmp-server/cpp/addon/ScampServer.cpp, falloutmp-server/cpp/server_guest_lib/gamemode_events/QuestStageEvent.cpp, falloutmp-gamemode/src/quests/framework.ts
- [ ] **F27-T06** `QuestUpdate` (114) message, slot assignment, deferred send, snapshot on spawn — S — Depends: NET-002, F27-T03 — Verify: L-unit — Files: falloutmp-server/cpp/messages/QuestUpdateMessage.h, Messages.h, falloutmp-client/src/services/messages/
- [ ] **F27-T07** `FalloutMP.esl` generator (quest slots, objectives, target aliases, a hide-person keyword for F31) — L — Depends: ESPM-002, ESPM-003 — Verify: L-fixture, G-self — Files: tools/falloutmp-plugin-gen/, falloutmp-client-deps/FalloutMP.esl
  - Accept: the plugin loads in game with no errors and the slots are startable.
- [ ] **F27-T08** Client `QuestDisplayService` + natives (`getRunningQuests`, `setQuestObjectiveText`), CEF tracker fallback — L — Depends: F27-T06, F27-T07, PLAT-031, CLI-050 — Verify: L-ts, G-self, G-manual — Files: falloutmp-client/src/services/services/questDisplayService.ts, fallout4-platform/src/.../QuestApi.cpp
- [ ] **F27-T09** (T2) Dialogue: `DialogueAction` (115), server `DialogueService` (sessions, validation, speech checks), `mp.dialogueShow/Close`, `onDialogueOpen/Choice`, front dialogue widget — L — Depends: F07, F19, FRONT-001, NET-002 — Verify: L-unit, G-manual — Files: falloutmp-server/cpp/server_guest_lib/DialogueService.{h,cpp} (new), falloutmp-front/src/features/dialogue/
  - Accept: the `[Dialogue]` cases pass.
- [ ] **F27-T10** (T2) Scenes and radiant story events: server-timeline scenes (`DialogueAction{show}` to listeners + F02 actions), `OnStory*` events for gamemode quests — M — Depends: F27-T04, PVM-015 — Verify: L-unit
- [ ] **F27-T11** GM-040 demo ("clear location" radiant), DOCS-003 quest section, G-manual script — S — Depends: F27-T05, F27-T08 — Verify: L-int, G-manual — Files: falloutmp-gamemode/src/quests/clearLocation.ts, docs/falloutmp/test-scripts/F27-quests.md

## 8. Open questions & risks
- Can objective text be rewritten at runtime without patching `BGSQuestObjective` (RE)? If not, the CEF tracker becomes the primary UI and the Pip-Boy quest tab shows generic slot names.
- Instanced Papyrus quests share script variables. Should Papyrus quests be shared-only and instanced quests JS-only? This is the proposed default.
- Using the vanilla `DialogueMenu` for gamemode dialogue needs a generic DIAL/INFO set plus text replacement. This is deferred until the CEF widget proves insufficient.
- Radio stations depend on quest scenes. If some cannot run under the allow-list without world side effects, F28 falls back to silencing them.
