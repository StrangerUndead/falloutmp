# Fallout 4 animation sync: reference and recommendation for FalloutMP

> Audience: the Claude Code session (or human) that will implement animation/locomotion sync in the
> FalloutMP fork (`fallout4-platform` + `falloutmp-client` + the game-pluggable `skymp5-server`).
> Companion to the broad survey `docs/FALLOUT4_PORT_RESEARCH.md`.
>
> Provenance tags: **[src: path:line]** = read in code (external repos pinned below);
> **[web: URL]** = taken from a web page; **[inference]** = my own analysis, verify before relying on it.
> Line numbers for this repo refer to commit `c16c7b9` (upstream SkyMP `f926944`).

## 0. Pinned sources

| Short name | Repository | Commit / date | What it was used for |
|---|---|---|---|
| SkyMP | this repo | `c16c7b9` | current Skyrim implementation |
| STR-FO4 | github.com/tiltedphoques/TiltedEvolution | `b850014949ab6356aef2e1bbaa116fb7f862bd9f` (2024-10-25; = head of branch `origin/falloutTogether`) | **last snapshot that still contains Fallout 4 code** |
| STR-dev | same | `4917189f6ac382e874f9ee735c4fef6a350e449e` (2026-10-03) | current Skyrim design (replay cache, ModCompat) |
| FO4_Wrld | github.com/ThePie88/FO4_Wrld | `4200f32ee3fa09cc77086955dbe4a0da19f9b8b1` (2026-10-02, v0.10.0, game 1.11.191) | first-person graph RE, bone replication |
| F4-PIP-OS | github.com/DCCStudios/F4-PIP-OS | `6bb9806290cd1538091701b942208a2716de5438` (2026-09-02, OG 1.10.163) | private 3rd-person graph that mirrors the player (best public example of FO4 event/variable mirroring) |
| icodei | github.com/icodei/f4se-code | `46466c120af9e86b30475ea99444e74085bc7936` | FO4 1st-person root graph event names |
| Mutagen FK | github.com/Mutagen-Modding/Mutagen.Bethesda.FormKeys | `650e147f854086b47b91ea80a881751466135256` | Fallout4.esm form IDs of AACT / KYWD / RACE / IDLE |
| xEdit | github.com/TES5Edit/TES5Edit | `9fb016884bec138ea6c7b872cec831537d464c3e` | FO4 record layouts (RACE subgraphs, WEAP, IDLE, AACT) |
| CommonLibF4 (libxse) | scratchpad `lx` = libxse/commonlibf4 | `7c8c6f8a349ac85abf6fc8a7e5a71732d9339954` | AE 1.11.x layouts and IDs |
| CommonLibF4 (alandtse) | scratchpad | `ba22620e455dffaf1d7021c16b0728dc28f1ddae` | OG/NG IDs |
| F4SE | scratchpad `f4se/scripts/vanilla` | — | FO4 Papyrus signatures |
| Commonwealth Online | github.com/G-A-R-D-E-N/Commonwealth-Online | `7b52878b` (website + changelog only; mod source is not public) | feature claims |
| F4MP | github.com/cokwa/F4MP-Archive | `67fd2900` (2020) | historical |

Correction to the brief: TiltedEvolution's Fallout 4 **code** was removed in PR #729 "tweak/yeetFallout"
(merged `23e5d407`, 2024-11-03; commits `5e01be49` client `TP_FALLOUT4`, `3e4fd7d1` deletes
`Code/client/Games/Fallout4/`, `11a54fda` deletes `Code/encoding/Structs/Fallout4/`, `f44b8d68` FT server).
`a2b9994f` "Remove Fallout 4 reference (#780)" (2025-05-15, first tag v1.8.0) only edits `README.md`.
`jjnorris/FalloutTogether` exists but is an unmodified fork of TiltedEvolution `dev` at `33a4c87a`
(2025-09-22): the FO4 code is only reachable through history. **[src: git log of both repos]**

Key FO4 commits in TiltedEvolution history: `2ea72915` (2020-11-03) "Sync fallout 4 root graph variables";
`ca45795f`, `6d89183a`, `7a521ef8` (2021-07) FO4 graph descriptors; `fc1cf1ca` (2021-07-24) "Force graph index
for player char anim sync"; `79ef9b54` (2021-08-14) "Refactor Fallout 4 anim keys"; `cce6f47e` sneak sync;
`3f304bb9`/`c609499a` projectile sync; `033f78c5` (2022-06-30) **"feat(ft): first person animation sync"**.

---

## TL;DR

1. **SkyMP** replays *animation-event names* captured with a hook on the engine's `NotifyAnimationGraph`
   and drives locomotion with `translateTo` + an AI trick (`keepOffsetFromActor(self, …)`). Only the **latest**
   event per frame is sent, over an **unreliable** channel, with no timestamps; Havok variables are synced only
   for spellcasting. Fallout 4 Papyrus has **neither `Debug.SendAnimationEvent` nor `KeepOffsetFromActor`**
   **[src: f4se/scripts/vanilla/Actor.psc, Debug.psc]**, so the locomotion half does not port at all.
2. **Skyrim Together Reborn (STR)** replays *actions* (`BGSAction` form + target + resolved event/idle +
   `ActorState` bits) captured at `ActorMediator::PerformAction`, and streams a **graph-variable snapshot**
   (descriptor-selected bool/float/int variables) at 10 Hz with a 300 ms interpolation buffer. Its FO4 branch had
   descriptors for the human graph and 22 creature graphs (tables reproduced in §4) and a **third→first person
   variable translation** for the local player. It never handled power armor or robots and never shipped FO4.
3. **FO4 first person is the central problem.** The player owns two behaviour graphs. In first person graph[1]
   (`_1stPerson/FirstPersonBase.hkx`) is active; graph[0] (third person) is **deactivated** (no events, no
   locomotion populate, no pose), and every frame `PlayerCharacter`'s post-update hook copies the 1st-person arm
   bones onto the 3rd-person skeleton. Events go **only to the active graph**; holder-level
   `SetGraphVariable*` broadcast to both graphs. **[src: FO4_Wrld first_person_graph.cpp, CHANGELOG v0.6.5;
   cross-checked against CommonLibF4 `PlayerCharacter` offsets, §3.2]**
4. **Recommendation: hybrid "state + action + curated events + name-resolved variables"** (strategy D, §6).
   Engine state (`ActorState` gun/weapon/knock/life/sit bits, sneak, sprint, sighted, power armor, furniture)
   and `BGSAction`s are **perspective-independent**, so they solve first person by construction; event replay
   is kept for a whitelist; variables are resolved **by name per graph at runtime** (not STR's hash-keyed index
   tables). Bone replication (FO4_Wrld) is rejected as the main path (≈26 KB/s per remote player, no gameplay
   semantics, per-skeleton retargeting) but its "drive the parked 3rd-person graph" technique is kept as an
   optional fidelity layer.
5. **Prototype first** (§7.2): a logging F4SE probe that records, in both perspectives, the event stream at
   holder and manager level, `PerformAction` calls, graph output events, camera/active-graph switches, and per-graph
   variable values; then replays the logs onto an AI-disabled NPC with each strategy and measures acceptance.

---

## 1. SkyMP today (Skyrim), precisely

### 1.1 Capture (local player and hosted NPCs)

**Native hook.** `skyrim-platform` installs a Frida hook on `Offsets::Hooks::SendAnimation =
RELOCATION_ID(37020, 38048)` **[src: skyrim-platform/src/platform_se/skyrim_platform/game/Offsets.h:24]**. The
callee receives the `IAnimationGraphManagerHolder` sub-object, so the hook computes `refr = rcx - 0x38` and reads
the event name from argument 1 **[src: FridaHooks.cpp:160-188]**. `enter` passes `(formId, name)` to JS
(`EventsApi::SendAnimationEventEnter`), and JS may rewrite or blank the name (the string is replaced via
`gum_invocation_context_replace_nth_argument`). `leave` reports the boolean return value as `animationSucceeded`
**[src: FridaHooks.cpp:190-206]**. Calls made by SP itself (`Override::IsOverriden()`) are skipped.
JS API: `hooks.sendAnimationEvent.add({enter, leave}, minSelfId, maxSelfId, pattern)`
**[src: docs/skyrim_platform/events.md:39-98]**. The SP fast path for `Debug.SendAnimationEvent` calls
`refr->NotifyAnimationGraph(name)` directly **[src: SendAnimationEvent.cpp:4-29]**.

**Client filter** (`AnimationSource`, one per sending actor) **[src: skymp5-client/src/sync/animation.ts:212-294]**:
- events are only used when `animationSucceeded` is true (except `OffsetCarryBasketStart`) (:222-229);
- ignored names: `moveStart, moveStop, turnStop, CyclicCrossBlend, CyclicFreeze, TurnLeft, TurnRight` (:296-304);
- any name containing `unequip` becomes `SkympFakeUnequip`, any containing `equip` becomes `SkympFakeEquip`
  (torch events excepted), and a 300 ms weapon-drawn override is applied to the movement filter (:265-272, 234-240);
- `SneakStart`/`SneakStop` are *not* sent as events; they set a `sneakBlocker` that forces `isSneaking` in the
  next movement packet (:274-281, 242-248);
- only **the latest** name is stored (`numChanges++`, `animEventName = …`) (:283-284).

**Send.** `SendInputsService.sendAnimation` runs every `update` (every frame) for the player and every hosted NPC;
it sends `UpdateAnimation {animEventName, numChanges}` when `numChanges` changed, **unreliable**, skipping `''`
and `DrinkPotion_*` **[src: skymp5-client/src/services/services/sendInputsService.ts:198-232]**. `JumpLand`,
`JumpLandDirectional` and `DeathAnim` also trigger an actor-value refresh (:315-324).

**Movement packet** every **130 ms**, unreliable **[src: sendInputsService.ts:113-133]**, built by `getMovement`
**[src: skymp5-client/src/sync/movementGet.ts:68-129]**: `worldOrCell, pos, rot`, `runMode`
(Standing/Walking/Running/Sprinting from `isSprinting()`, `SpeedSampled`, `isRunning()`, furniture, `IsBlocking`,
encumbrance; :134-168), `direction = 360 * Direction` (Havok var), `isInJumpState` (`bInJumpState`),
`isSneaking` (`isSneaking()` or `IsSneaking`), `isBlocking` (`IsBlocking`), `isWeapDrawn`, `isDead`,
`healthPercentage`, `lookAt` (combat target position, NPCs only), `speed` (`SpeedSampled` for NPCs; for the player
computed from position deltas because `SpeedSampled` stays high when running into walls, :94-108).

**Havok variable snapshot** (spells only): `MagicSyncService` sends `UpdateAnimVariables` **reliably every
500 ms while magic is equipped**, and embeds a snapshot in `SpellCast` messages
**[src: skymp5-client/src/services/services/magicSyncService.ts:30-56, 178]**. The snapshot reads fixed indices of
Skyrim's master behaviour (14 ints, 19 floats, 60 bools; table copied from STR's Skyrim descriptor)
**[src: skyrim-platform/.../Magic/AnimVariableMasterGraphIndexes.cpp; AnimationGraphMasterBehaviourDescriptor.h:103-213]**.
For the player it always reads `graphs[0]` (third person) (:117-119).

### 1.2 Replay on remote actors

`FormView.update` applies movement when `numMovementChanges` changes or every 2 s, then
`applyAnimation` when the 3D is loaded **[src: skymp5-client/src/view/formView.ts:403-481]**.

`applyAnimation` **[src: animation.ts:122-199]**: deduplicates by `numChanges`; on the first application after
spawn maps sit/eat/table idles to `*EnterInstant` variants (`animOverridesLowerCase`, :70-96);
`SkympFakeEquip/Unequip` → `applyWeapDrawn` → `TESModPlatform.setWeaponDrawnMode` (backed by Frida hooks on the
draw/sheathe functions, `FridaHooks.cpp:94-156`); `Ragdoll` → `pushActorAway(self, 0)` + `Variable10 = -1000` (which
suspends `translateTo`); otherwise `Debug.sendAnimationEvent(refr, name)`; `GetUpBegin` restores `Variable10` after
1 s; sit/get-up idles toggle collision. Idles on `0xFF…` actors are blocked unless replayed (`allowedIdles`), and
attack events on non-hosted actors are blocked unless replayed (`refsWithDefaultAnimsDisabled`) (:306-339). Kill
moves and `staggerStart` are blanked on remote actors **[src: services/deathService.ts:31-58]**.

`applyMovement` **[src: skymp5-client/src/sync/movementApply.ts:15-69]**:
- `translateTo(pos + speed*0.2 s along heading, rot, speed=dist/0.2)` when moving, jumping, >8 units off,
  >80° off or sitting (:155-206);
- `keepOffsetFromActor(self, 3*sin(dir), 3*cos(dir), -512 (walk) / -1024 (run), 0, 0, Δyaw, catchUp 2048/1, 1)`:
  the AI thinks it must follow an offset and so plays walk/run locomotion in the right direction (:71-107);
- state re-assertion each packet: `SprintStart/Stop`, `BlockStart/Stop` (+ sneak), `SneakStart/Stop`,
  `setWeaponDrawnMode`, health percentage, head tracking via `setLookAt` (:36-66, 109-150).

### 1.3 Server

`ActionListener::OnUpdateAnimation` relays the raw message to neighbours (`SendToNeighbours`, which also checks
NPC-host ownership), then, for the sender's own actor only, runs `AnimationSystem::Process` and stores
`SetLastAnimEvent` **[src: skymp5-server/cpp/server_guest_lib/ActionListener.cpp:39-80, 186-207]**. The last event is
sent to newly visible clients in `CreateActor.animation` **[src: PartOne.cpp:802]**. `AnimationSystem` maps
**event names to stamina effects**: `blockStart/blockStop` always; with `SweetPie.esp` also `attackStart*`,
`AttackStartH2H*`, `JumpStandingStart` (10), `JumpDirectionalStart` (15), `bowAttackStart`, `attackRelease`,
`attackPower*` (30), weapon-keyword modifiers **[src: AnimationSystem.cpp:10-34, 79-234, 279-291]**.

### 1.4 Known weaknesses (what FalloutMP must do better)

| # | Weakness | Evidence |
|---|---|---|
| W1 | Events coalesce: several events in one frame → only the last is sent | animation.ts:283-284, sendInputsService.ts:212-217 |
| W2 | Events are unreliable; a lost `attackStart`/`IdleChairEnter` is never repaired | sendInputsService.ts:227-231 |
| W3 | No timestamps or ordering between the 130 ms movement and the events; remote applies on arrival, no jitter buffer | formView.ts:403-481 |
| W4 | `NotifyAnimationGraph` silently fails when the remote graph is in a different state (weapon type, equip state, subgraph not loaded); only sprint/sneak/block/drawn are re-asserted | movementApply.ts:62-65 |
| W5 | Locomotion is AI-approximated (`keepOffsetFromActor` with fake offsets): strafing, walk/run thresholds and foot sliding are approximate; **not portable to FO4** | movementApply.ts:71-107; FO4 Actor.psc has no KeepOffsetFromActor |
| W6 | Behaviour variables are synced only for magic, from a hash-specific index table (breaks with behaviour mods) | magicSyncService.ts:30-56; Magic/AnimVariableMasterGraphIndexes.cpp |
| W7 | Late joiners get only the last event plus a hand-written instant-override table | PartOne.cpp:802; animation.ts:70-96 |
| W8 | Hard-coded Skyrim name lists everywhere (ignore list, equip heuristics, sit lists, DrinkPotion_) | animation.ts |
| W9 | Server validation keyed on spoofable, game-specific event names | AnimationSystem.cpp |
| W10 | Assumes the local player's event stream is valid for a third-person body; never had to deal with a perspective-specific graph **[inference]** | — |

---

## 2. Skyrim Together Reborn

### 2.1 Capture: actions, not events

`HookPerformAction` detours `ActorMediator::PerformAction(TESActionData*)` (FO4 OG Address Library ID **502377**,
Skyrim 38949) **[src: STR-FO4 Code/client/Games/Animation.cpp:17-69, 192-201]**. For local actors it fills an
`ActionEvent` *before* calling the original:

```
ActionEvent { Tick, ActorId, ActionId (BGSAction formID), TargetId, IdleId, State1, State2 (ActorState flags1/flags2),
              Type = unkInput | (someFlag ? 4 : 0), EventName, TargetEventName, AnimationVariables Variables }
```
**[src: Code/encoding/Structs/ActionEvent.h:9-38]**. `Variables` is a snapshot taken by `SaveAnimationVariables`.
After the original returns it records `eventName`, `targetEventName`, `idleForm` (what the idle tree resolved),
drops actions with `someFlag == 1` ("spammed"), stores `LatestAnimation` when the call succeeded and posts the event
(:30-57). For **remote** actors all `PerformAction` calls are swallowed (`return 0`), except on FO4 action
`0x5704C` `ActionInstantInitializeGraphToBaseState`, which is let through (:62-66).

### 2.2 What is sent and how often

- Client: `CharacterService::RunLocalUpdates` every **100 ms** builds `ClientReferencesMoveRequest` with, per local
  actor, `Movement {CellId, WorldSpaceId, Position, Rotation(x,z), Variables, Direction}` plus all `ActionEvent`s
  queued since the last send **[src: client/Services/Generic/CharacterService.cpp:1431-1456;
  client/Systems/AnimationSystem.cpp:84-123]**. Note: no frame coalescing of actions (unlike SkyMP W1).
- Encoding: `ActionEvent::GenerateDifferential` writes an 8-bit change mask + varint fields; event names go through
  a shared `StringCache` (id or literal) **[src: encoding/Structs/ActionEvent.cpp:70-146; CachedString.cpp:13-53]**.
  `AnimationVariables::GenerateDiff` writes a change bitmask over (bools-as-one-u64, ints varint, floats raw 32-bit)
  **[src: encoding/Structs/AnimationVariables.cpp:252-320]** — but both `Movement` and `ReferenceUpdate` diff
  against an *empty* object, so effectively every non-zero value is resent each time
  **[src: encoding/Structs/Movement.cpp:22; ReferenceUpdate.cpp:25]**.
- Server: stores movement + appends actions (`CurrentAction` kept for spawns) and re-broadcasts at **50 Hz** to
  players in range, only when something changed **[src: server/Services/CharacterService.cpp:384-433, 783-855]**.

### 2.3 Replay

`CharacterService::RunRemoteUpdates` runs with `tick = now − 300 ms` **[src: client CharacterService.cpp:1459-1462]**.
- `InterpolationSystem::Update` lerps position, `ForcePosition`s the actor, calls
  `LoadAnimationVariables(second.Variables)` every frame, writes `middleProcess->direction`, and sets rotation —
  on FO4 with pitch forced to 0 **[src: client/Systems/InterpolationSystem.cpp:12-76, 64-70]**.
- `AnimationSystem::Update` pops due actions, restores `actorState.flags1/flags2`, loads the action's variables,
  builds `TESActionData(Type&3, actor, action, target)` with the **sender's** `eventName`, `idleForm`, `someFlag`,
  and calls `ActorMediator::ForceAction` → `PerformComplexAction` (FO4 OG ID 1445653)
  **[src: client/Systems/AnimationSystem.cpp:24-56; Games/Animation.cpp:110-133]**.
- STR-dev additionally waits for `animationGraphHolder.IsReady()` and can `RevertAnimationGraphManager()` before a
  replay chain **[src: STR-dev Code/client/Systems/AnimationSystem.cpp:29-40, 80-88]**.

### 2.4 Descriptors: generation and hashing

- Key = CRC64 of the **lower-cased concatenation of every variable name, ordered by variable index**, read from the
  graph's `BShkbHkxDB` variable hash table **[src: client/Games/BSAnimationGraphManager.cpp:57-106]**. The FO4
  player is forced to use graph[0]'s key **[src: Games/References.cpp:213-217]**.
- Generation: the in-game ImGui `AnimDebugView` dumps `k<Name> = <index>` enums and the key, and has a "variable
  recording" mode that watches which indices change; a developer then hand-picks bool/float/int lists
  **[src: client/Services/Debug/Views/AnimDebugView.cpp:27-38, 122-135, 279-300]**. Types are **guessed from observed
  values**, which explains misclassifications in the FO4 tables (§4.4).
- Limits: ≤64 bools and ≤63 floats+ints per descriptor (`static_assert`)
  **[src: encoding/Structs/AnimationGraphDescriptor.h:7-15]** (STR-dev made bools a `Vector<bool>`).
- Mods change the variable list → new hash → descriptor not found → no variable sync. STR-dev's
  `ModCompat/BehaviorVar` re-resolves the original descriptor's variables **by name** in modded graphs
  **[src: STR-dev Code/client/ModCompat/BehaviorVar.cpp header comment; README-ANIMATION-MODS.md]**.

### 2.5 Server role

Pure relay plus state for late joiners: `AnimationComponent {Actions, CurrentAction, LastSerializedAction}`
**[src: server/Components/AnimationComponent.h]**; script hooks may cancel a move (`HandleCharacterMove`). No
validation. STR-dev adds `ActionReplayCache`: last 32 actions per actor, an ignore list (turns, bumps, jumps, weapon
equip), rewriting idles to their `*Instant` counterparts, and trimming at the last "exit" action (in which case
clients reset the graph first); comment: *"the best solution … is to implement animation graphs serialization …
let's do simple action replays until better days"* **[src: STR-dev Code/server/Game/Animation/ActionReplayCache.{h,cpp};
AnimationEventLists.cpp]**.

### 2.6 STR's Fallout 4 specifics

- Human third-person descriptor `AnimationGraphDescriptor_Master_Behavior` (key `8074503569708505439`) with 48 bools,
  31 floats, 30 ints (full list §4.1), plus `TranslateThirdToFirstPerson()` mapping 50 of those to indices in the
  1st-person graph **[src: encoding/Structs/Fallout4/AnimationGraphDescriptor_Master_Behavior.cpp:500-691]**.
- **Local player** (`formID == 0x14`): descriptor from graph[0], but **values always read from graph[1]** (first
  person) through the translation; untranslatable bools are skipped and untranslatable floats/ints sent as 0
  **[src: Games/References.cpp:199-308]** (commit `033f78c5`).
- **Remote players**: only translatable variables are written; the rest keep the NPC graph's own values
  **[src: References.cpp:350-396]**.
- 22 creature descriptors (Alien … YaoGuai, cats = `RootState`), §4.3. No power armor, robots, mirelurks
  (Mirelurk/HermitCrab were added in `6d89183a` and deleted in "Cleanup" `1a313e33e`).
- Weapons: remote `Fire` (OG ID 1056038) and `Projectile::Launch` (OG ID 1452335) are blocked for remote shooters;
  local launches are sent as `ProjectileLaunchedEvent` (origin, angles, ammo, projectile, power, flags) and re-launched
  remotely **[src: client/Games/Fallout4/Projectiles/Projectile.cpp:18-99]**.
- Weapon drawn: synced separately, applied in two passes after 0.5 s / 2 s **[src: client CharacterService.cpp:1655-1680]**.
- Likely bug **[inference, from §3.2]**: in third-person view graph[1] is the *parked* graph, so its
  graph-internal variables are stale while STR keeps reading it.

---

## 3. Fallout 4 animation architecture

### 3.1 Objects and layouts

| Object | Facts | Source |
|---|---|---|
| `TESObjectREFR` | bases: `BSTEventSink<BSAnimationGraphEvent>` at **+0x38** (graph *output* events), `IAnimationGraphManagerHolder` at **+0x48** (SkyMP's Skyrim hook used +0x38) | [src: lx RE/T/TESObjectREFR.h:72-81] |
| `IAnimationGraphManagerHolder` vtable | 01 `NotifyAnimationGraphImpl`, 04 `GetAnimationGraphManagerImpl`, 07 `ConstructAnimationGraph`, 08 `InitializeAnimationGraphVariables` (player override = "PopulateGraphVariables"), 0B `CreateAnimationChannels`, 10-12 `GetGraphVariableImpl(id,…)`, 13-15 `GetGraphVariableImpl{Float,Int,Bool}(name)`, 16/17 `Pre/PostUpdateAnimationGraphManager`; non-virtual `SetGraphVariable{Bool,Float,Int}` (AE IDs 2214543/2214545/2214544), `RevertAnimationGraphManager` (2214541) | [src: lx RE/I/IAnimationGraphManagerHolder.h:20-75; RE/IDs.h:1319-1325] |
| `BSAnimationGraphManager` (0xE0) | `graph` = `BSTSmallArray<BSTSmartPointer<BShkbAnimationGraph>,1>` at +0x40, `subManagers` +0x58, `variableCache` +0x88, `updateLock` +0xC8, **`activeGraph` +0xD8** | [src: lx RE/B/BSAnimationGraphManager.h:96-121]; STR static_asserts agree for OG [src: STR-FO4 Games/Fallout4/BSAnimationGraphManager.h:138-140] |
| `BShkbAnimationGraph` | +0x370 `BShkbHkxDB*`, +0x378 `hkbBehaviorGraph*` (OG and 1.11.191 agree); 1.11.191: +0x1C8 inline `hkbCharacter`, +0x388 root node, +0x3C3 "mute" byte (do not touch), +0x3CA ragdoll-pending | [src: STR-FO4 Havok/BShkbAnimationGraph.h; FO4_Wrld first_person_graph.cpp:18-110] |
| `hkbBehaviorGraph` | +0xC0 root state machine (name at +0x38), +0x110 `hkbVariableValueSet*` (`data` +0x10, `size` +0x18, one 32-bit word per variable); +0xE0 active nodes, +0x1AA `m_isActive`, +0x1AC nodes-dirty (1.11.191) | [src: STR-FO4 Havok/hkbBehaviorGraph.h, hkbVariableValueSet.h; FO4_Wrld first_person_graph.cpp:96-100] |
| `BShkbHkxDB` | +0x84 hash table name → variable index (what STR hashes) | [src: STR-FO4 Havok/BShkbHkxDB.h] |
| `ActorState` (Actor+0x128) | bitfields at +0x08: `moveMode:14, flyState:3, lifeState:4, knockState:4, meleeAttackState:3, …`; at +0x0C: `weaponState:3, wantBlocking, recoil:2, staggered, stance:3, gunState:4, interactingState:2, inSyncAnim`; swimming = moveMode bit 0x400 | [src: lx RE/A/ActorState.h:31-55] |
| `GUN_STATE` | Drawn, Relaxed, Blocked, Alert, Reloading, Throwing, Sighted, Fire, FireSighted | [src: lx RE/G/GUN_STATE.h] |
| `MiddleHighProcessData` | `animationGraphManager` +0x258, `subGraphIdleManagerRoots` +0x268, `current/requested Default/WeaponSubGraphID` (+0x2D0..0x330, two entries = 3rd/1st), `currentFurnitureSubgraphID` +0x470 | [src: lx RE/M/MiddleHighProcessData.h:78-130] |
| `PlayerCharacter` | `firstPersonBipedAnim` +0xB70, **`firstPerson3D` +0xB78**, **`boneMapping1stTo3rd` +0xDC8**, `lastUsedPowerArmor` +0xD90, `lastUsedThrownWeapon` +0xDA0, **`is3rdPersonModelShown` bit at +0xDFE:4** | [src: lx RE/P/PlayerCharacter.h:403-515] |
| `PlayerCamera` | `TESCamera::currentState` +0x28; states FirstPerson 0, AutoVanity 1, VATS 2, Free 3, IronSights 4, PCTransition 5, Tween 6, Animated 7, 3rdPerson 8, Furniture 9, Mount 10, Bleedout 11, Dialogue 12 | [src: lx RE/C/CameraStates.h; RE/T/TESCamera.h:25-31] |
| `BGSAction` | `BGSKeyword` + `index`; resolved at runtime through `BGSDefaultObjectManager::GetDefaultObject(DEFAULT_OBJECT::kAction…)` | [src: lx RE/B/BGSAction.h; alandtse BGSDefaultObjectManager.h:41-168] |
| `ActionInput` / `ActionOutput` | input: `ref`, `targetRef`, `action`, `priority` (Imperative/Queue/Try), data; output: `animEvent`, `targetAnimEvent`, `result`, `sequence` (TESIdleForm), `animObjIdle`, `sequenceIndex` | [src: lx RE/A/ActionInput.h, ActionOutput.h] |
| `Actor::PerformAction(BGSAction*, TESObjectREFR*)` | AE ID 2231177, OG 1057231; Papyrus `Actor.PlayIdleAction(Action, ObjectReference)` | [src: lx RE/IDs.h:45; alandtse Actor.h:1256-1261; Actor.psc:520] |
| `PlayerControls::DoAction(DEFAULT_OBJECT, priority)` | OG ID 818081 (player input → actions) | [src: alandtse PlayerControls.h:183-188] |
| `RACE` subgraph data | `SRAC` template race, `SADD` additive race, repeated {`SAKD` actor keywords, `SGNM` behaviour graph, `SAPT` animation paths, `STKD` target keywords, `SRAF` role (MT, Weapon, Furniture, Idle, Pipboy) + perspective (3rd, 1st)} | [src: xEdit Core/wbDefinitionsFO4.pas:11304-11329] |
| `TESRace` | `behaviorGraph[2]` +0x2E8, `rootBehaviorGraphName[2]` +0x348, `behaviorGraphProjectName[2]` +0x358 | [src: lx RE/T/TESRace.h:94-96] |
| `IDLE` record | conditions, `DNAM` behaviour graph, `ENAM` animation event, parent/previous (AACT or IDLE) | [src: xEdit wbDefinitionsFO4.pas:9646-9669] |

### 3.2 The player's two graphs and what "parked" means

Facts (FO4_Wrld, game 1.11.191, RVAs below are for that exe only) **[src: FO4_Wrld fw_native/src/hooks/first_person_graph.cpp:18-200, 520-900; CHANGELOG.md "First-person ghost animation (2026-08-06) — v0.6.5"]**:

1. The player's `BSAnimationGraphManager` holds exactly **two graphs**: index 0 third person, index 1
   `_1stPerson/FirstPersonBase.hkx`. "Size == 2" is the engine's own player test. NPCs have one graph.
2. `SetActiveGraph` (`sub_14130EDF0`) writes `activeGraph` (+0xD8) and **calls `hkbBehaviorGraph::deactivate`** on the
   outgoing graph (`sub_141326370`). Every per-graph entry point early-outs when `m_isActive` (+0x1AA) is 0, so the
   inactive graph produces **no pose, no clip time and no events**.
3. `BSAnimationGraphManager::NotifyAnimationGraph` (`sub_14130EAE0`) **delivers events only to `graph[activeGraph]`**.
4. Holder-level `SetGraphVariable*` **loop over every graph** (both get the value). But
   `PlayerCharacter::PopulateGraphVariables` (`sub_140D7DB10`, holder slot 8) — which writes `speed`,
   `iSyncIdleLocomotion`, `iSyncTurnState`, `iSyncForwardState`, `iSyncStrafeState`, `iSyncJumpState` and player flags —
   and the bound-channel flush (`sub_141320EA0`) run **for the active graph only**. Any variable written before the
   flush that shares a name with a bound channel is overwritten by it.
5. `PlayerCharacter` overrides holder slot 23 (`PostUpdateAnimationGraphManager`, `sub_140D7DE50`): while the
   3rd-person model is hidden it **copies rotation, translation and scale from the 1st-person skeleton to the
   3rd-person skeleton** for every pair in an index map. FO4_Wrld's decompile uses holder-relative offsets
   (`+3510 & 0x10`, `+2864`, `+3456`, `+3472`); adding the holder offset 0x48 gives exactly the CommonLibF4 members
   `is3rdPersonModelShown` (0xDFE bit 4), `firstPerson3D` (0xB78), `boneMapping1stTo3rd` (0xDC8, size at +0x10)
   **[inference: cross-check of the two sources]**. Result: first-person arms grafted onto a body whose lower half
   never moves (FO4_Wrld's "V/T-pose ghost").
6. Re-activating a graph rebuilds its active-node list from the root, i.e. **resets the state machine** (measured
   59 → 19 active nodes); a fresh graph sits in its initial state (renders as T-pose) until an event moves it.
   `g_archetypeBaseStateStartInstant` followed by `MoveStop` wakes it.
7. Camera perspective: compare `PlayerCamera->currentState` vtable with `FirstPersonState` (or
   `QCameraEquals(kFirstPerson)`), never infer it from bone values.

So: **FO4 does not update the 3rd-person graph in first person.** What remains valid in the parked graph: variables
written by holder-level setters. What is stale: everything the graph computes itself (state machine, `iState_*`,
`bEquipOk`, `IsAttackReady`, clip timing) and the locomotion snapshot written by PopulateGraphVariables.

### 3.3 How others handled first person

| Project | Approach | Result |
|---|---|---|
| STR (FO4 branch) | Read variables from graph[1] for the player, translate TP→FP indices, ship only the 50 translatable ones; actions replayed as on Skyrim | Worked for the shared subset; locomotion ints such as `iSyncWalkRun`/`iSyncTurnState`/`bInJumpState` have no FP counterpart and are not sent; likely stale in 3rd person (§2.6). Never shipped |
| FO4_Wrld | Ghosts are raw scene-graph bodies, not Actors (PlaceAtMe from their WndProc thread crashed — **[src: re/M8_strategic_decision.txt "PIVOT 2026-04-26"]**), so they replicate **per-bone local rotations** (≤80 bones × 16 B, 20 Hz, ~26 KB/s/peer) **[src: net/protocol.h:524-560; CHANGELOG M8P3.20]**. In first person they **drive the parked 3rd-person graph**: revive it, refresh active nodes, run the engine's flush/generate/pose-apply under the manager lock with a forced update context, mirror every event onto it, re-run PopulateGraphVariables each frame, keep it alive across camera switches, suppress the 1P→3P bone copy | Works; residue: walk clip rate wrong (locomotion scalars), fingers in PA, creature pose gating |
| F4-PIP-OS (Pip-Boy 3D preview) | Builds a **private third-person graph** (`IAnimationGraphManagerHolder` subclass) for a preview skeleton; loads the player's current 3rd-person default/weapon subgraphs from `MiddleHighProcessData` (skips `forFirstPerson` roots); every frame copies a name list of variables from the live player holder; mirrors a whitelist of events captured by hooking `BSAnimationGraphManager::ProcessGraphEvent` (OG REL::ID 1199489) | Proves that **events captured from the live (usually 1st-person) player can drive a 3rd-person graph** for the listed subset **[src: F4-PIP-OS dll/src/CharacterAnimation.cpp:62-101, 312-371, 690-704; CharacterCapture.cpp:4000-4039]** |
| Commonwealth Online | "Runtime proxy actors"; syncs idle/movement state, sprint, sneak, jump, weapon-drawn, teleports; firing/reload/melee/grenades/aim "in development" | State-based, which is perspective-independent by design **[web: https://commonwealth-online.com/ ; https://commonwealth-online.com/roadmap/ ; changelogs/1.1.0.md]**; source not public |
| F4MP (2020) | Own baked clips (idle, 8 jog directions) applied to nodes | Stalled **[src: F4MP-Archive f4mp/Animator.h, Animations.h]** |

### 3.4 Feature notes

Action IDs are Fallout4.esm form IDs (§5.1). Event names in `code` are from the sources cited in §5.2.

- **Weapon animation archetypes / subgraphs.** The base graph loads subgraphs chosen from the race's Subgraph Data
  by *actor keywords* (e.g. `AnimArchetype*`, `AnimFlavor*`, `ActorType*`) and *target keywords* on the weapon or
  furniture (e.g. `AnimsGripPistol` 0x1F948, `AnimsGripRifleAssault` 0x1F947, `AnimsGripRifleStraight` 0x464EF,
  `AnimsGripShoulderFired` 0xAA937, `AnimsMinigun` 0x212FE, `AnimsFatMan` 0xBC240, `Anims1hmWeapon` 0x23465,
  `Anims2hmWeapon` 0xA7C45, `AnimsUnarmed` 0x2405E, `AnimsMine` 0xE56C4 …), per role and perspective. OMODs add
  keywords, so **the same base weapon can use a different subgraph** (pistol vs rifle grip). `WEAP.DNAM.Animation
  Type` is HandToHandMelee … Gun, Grenade, Mine; relevant flags: Automatic, Charging Attack, Charging Reload,
  Bolt Action, Hold Input To Power, Repeatable Single Fire **[src: xEdit wbDefinitionsFO4.pas:12911-12965]**.
  Archetype changes: `Actor.ChangeAnimArchetype/ChangeAnimFlavor/ChangeAnimFaceArchetype` (Actions
  `ActionChangeAnimArchetype` 0xB343A, `ActionChangeAnimFlavor` 0xB3444); subgraph-wide calls
  `PlaySubGraphAnimation`, `SetSubGraphFloatVariable` **[src: Actor.psc:63-73, 525-526, 749-750]**.
  ⇒ Remote actors must carry **the same equipped instance (OMODs) and archetype/flavor keywords** before any event or
  action is replayed, and replay must wait until the subgraph is loaded (F4-PIP-OS polls `IsSubgraphLoaded`).
- **Sighted / aim.** `ActionSighted` 0x4A57, `ActionSightedRelease` 0x4A58; `GUN_STATE` Sighted/FireSighted;
  `Actor.IsInIronSights()`; vars `iSyncSightedState`, `bAimActive`, `bAimEnabled`, `AimPitchCurrent`,
  `AimHeadingCurrent`, `fDirectAtSavedGain`, `Pitch`, `PitchOffset`; events `rifleSightedStart/End`,
  `sightedStateEnter/Exit`, `UpdateSighted`. Remote pitch must go through graph variables / direct-at, not actor
  X rotation (STR forces FO4 pitch to 0, InterpolationSystem.cpp:64-70).
- **Firing.** `ActionFireSingle` 0x4A5A, `ActionFireAuto` 0x4A5C, `ActionFireCharge` 0x4A5B,
  `ActionFireChargeHold` 0x11A186, `ActionFireEmpty` 0x20A702, `ActionGunChargeStart` 0xC4F72, `ActionBoltCharge`
  0xB259D; vars `isFiring`, `iSyncFireState` (TP 84), `iAttackState`, `weaponSpeedMult`, `iWeaponChargeMode`; input
  events `attackStart`, `attackStartAuto`, `attackRelease`, `attackStop`; output `WeaponFire`/`weaponFire`. Gameplay
  (projectile, ammo, damage) must not come from the replayed animation: block remote `Fire`/`Launch` and send explicit
  fire messages (STR pattern, §2.6).
- **Recoil.** Player weapon recoil is camera-side (Aim Model `AMDL`, `WEAP.WAMD`) and NPC recoil is inside the gun
  graph. `ActionRecoil` 0x13AF5 / `ActionRecoilLarge` 0x13EC8 and `ActorState.recoil` are **hit** recoil, not weapon kick.
- **Reload.** `ActionReload` 0x4A56; `GUN_STATE` Reloading; vars `isReloading`, `ReloadSpeedMult`; events
  `reloadStart`, `reloadStateEnter/Exit`, `reloadReserveStart`, `ReloadComplete`, `ReloadEnd`,
  `reloadSequentialStart`, `reloadSequentialReserveStart`; engine events `PlayerWeaponReloadEvent`, `PlayerAmmoCountEvent`.
- **Grenades / mines.** `ActionThrow` 0x4E32, `ActionFlipThrow` 0x238E3; `GUN_STATE` Throwing; var `bIsThrowing`;
  events `grenadeThrowStart`, `mineThrowStart`, `throwEnd`; idles `CharacterWeaponGrenadeThrow` 0xCF927,
  `CharacterMTGrenadeThrow` 0xEE0F1, `CharacterMeleeGrenadeThrow` 0xD9C6A (+ `…Over`, `…OverLow`).
- **Melee / bash / power attacks.** `ActionMelee` 0x4A59 (gun bash), `ActionRightAttack` 0x13005,
  `ActionRightPowerAttack` 0x13383, `ActionRightRelease` 0x13454, `ActionLeftAttack` 0x13004, `ActionDualAttack`
  0x50C96, `ActionBlockHit` 0x13AF4; `ActorState.meleeAttackState` (`ATTACK_STATE_ENUM` Draw/Swing/Hit/…/Bash); vars
  `iMeleeState`, `IsBlocking`, `iWantBlock`, `IsAttackReady`; events `meleeattackStart`, `meleeattackSprintStart`,
  `meleeAttackGun`, `blockStart`, `weaponSwing` (output).
- **VATS and kill cams.** VATS is a camera state + menu that slows time locally; kill moves are paired idles
  (`MeleeRightSync*`, `PairedKill*`, `ActionRightSyncAttack` 0x24F13, `ActionLeftSyncAttack` 0x24F14,
  `Actor.IsInKillMove()`). MVP: disable VATS and kill moves (SkyMP already blanks `KillMove*` on remote actors).
- **Furniture / idles / sitting.** `ActionInteractionEnter` 0x2248F, `…EnterInstant` 0x2248E, `…Exit` 0x2248D,
  `…ExitQuick` 0x2248C, `…ExitAlt` 0xA8C2F, `ActionIdle` 0x13002, `ActionIdleStop` 0x18BA8, `ActionIdleStopInstant`
  0x7F8E3; `TESFurnitureEvent`; `SIT_SLEEP_STATE`, `INTERACTING_STATE`; furniture keywords `AnimFurn*`; Papyrus
  `SnapIntoInteraction(ref)` (instant, for late join), `PlayIdle(Idle)`, `PlayIdleAction(Action, target)`.
- **Power armor.** Frame = furniture (`FurnitureTypePowerArmor` 0x3430B, `isPowerArmorFrame` 0x15503F). Entering
  switches the actor's visual race to `PowerArmorRace` 0x1D31E (own skeleton and behaviour project, so the graph is
  rebuilt and any cached variable indices are invalid); idles `InitializePowerArmorGraphRoot/Base/FromSitting/Ragdoll`
  0x45BC1/0x45BC2/0x45BC3/0x1A671E, `FurnitureExitToStandCombatPowerArmor` 0x18B892. `IsInPowerArmor()` = has perk
  0x1F8A9 **[src: Actor.psc:390-393]**; `SwitchToPowerArmor(frame)` pops in with no animation **[src: Actor.psc:784-785]**.
  Engine events `PreloadPowerArmor`, `ExitPowerArmor` **[src: lx RE/P/PowerArmorGeometry.h:34-48]**. FO4_Wrld: frame
  mesh skinned to 53 bones, 20 PA-only bones, different proportions **[src: CHANGELOG v0.7.5]**. No PA descriptor exists
  anywhere: **dump it** (§7.2).
- **Jetpack** (PA torso mod). `ActionJetpackStart` 0x125C7E / `Stop` 0x125C7F; idles `MTJetpackStart/Stop`
  0x125C8B/0x125C8C, `MeleeJetpackStart/Stop` 0x125C89/0x125C8A.
- **Ragdoll / stagger / knockdown.** `ActionRagdollInstant` 0x9BB4E, `ActionKnockDown` 0xD1FDC, `ActionGetUp`
  0xD1FDD, `ActionStaggerStart` 0x138D2, `ActionStumbleStart` 0x3F219, `ActionFlinchStart` 0x1ABC86;
  `KNOCK_STATE_ENUM` (Normal, Explode, ExplodeLeadIn, Out, OutLeadIn, Queued, GetUp, Down, WaitForTaskQueue);
  vars `IsStaggering`, `staggerMagnitude`, `staggerDirection`, `iGetUpType`, `fRagdollAnimAmount`; Papyrus
  `PushActorAway`, `ApplyHavokImpulse`.
- **Death.** `ActionDeath` 0x489ED, `ActionDeathWait` 0x5DD59, `ActionDeferredKill` 0xFFD26, `ActionBleedoutStart/Stop`
  0x13EC9/0x13ECA, `ActionUnconsciousEnter/Exit` 0x1244CB/0x1244CC; `ACTOR_LIFE_STATE` (Alive, Dying, Dead,
  Unconscious, Reanimate, Recycle, Restrained, EssentialDown, Bleedout); var `bSupportedDeathAnim`; Papyrus `Kill`,
  `KillSilent`, `Start/EndDeferredKill` (same pattern SkyMP uses).
- **Swimming.** `ActionSwimStart` 0x14DB5D, `ActionSwimStop` 0x14DB5E, `ActionSwimStateChange` 0x13003;
  `ActorState::IsSwimming()`; `OnPlayerSwimming`.
- **Creatures** each have their own behaviour project and variable set (§4.3); robots (Assaultron, Protectron,
  Mr. Handy, Sentry Bot, Eyebot), mirelurks, turrets, vertibirds, Liberty Prime and power armor have **no** public
  descriptor.

---

## 4. Behaviour-graph variables

### 4.1 Human third person (STR "Master_Behavior", key 8074503569708505439)

Source: **[src: STR-FO4 Code/encoding/Structs/Fallout4/AnimationGraphDescriptor_Master_Behavior.cpp:193-691]**, dumped
on OG 1.10.163 in 2021-2022. Indices are positions in the graph's variable set and are valid **only** for that
unmodified graph (and only if AE did not change the behaviour files — verify by recomputing the CRC64 key, §7.2).
"STR type" is STR's guess (§4.4). FP column = index in the 1st-person graph returned by `TranslateThirdToFirstPerson`.

| # | Variable | STR type | TP idx | FP idx (via TranslateThirdToFirstPerson) |
|---|---|---|---|---|
| 1 | `m_bEnablePitchTwistModifier` | bool | 10 | 11 (`m_bEnablePitchTwistModifier`) |
| 2 | `IsSprinting` | bool | 23 | 23 (`IsSprinting`) |
| 3 | `isFiring` | bool | 28 | 28 (`isFiring`) |
| 4 | `isReloading` | bool | 31 | 31 (`isReloading`) |
| 5 | `IsAttackReady` | bool | 37 | — |
| 6 | `isAttackNotReady` | bool | 47 | — |
| 7 | `iIsInSneak` | bool | 51 | 81 (`iIsInSneak`) |
| 8 | `isJumping` | bool | 52 | 51 (`isJumping`) |
| 9 | `bEquipOk` | bool | 55 | 79 (`bEquipOk`) |
| 10 | `bInJumpState` | bool | 79 | — |
| 11 | `IsStaggering` | bool | 81 | 98 (`isStagger`) |
| 12 | `IsSneaking` | bool | 85 | — |
| 13 | `isMirrored` | bool | 86 | — |
| 14 | `bNotHeadTrack` | bool | 106 | — |
| 15 | `bCCSupport` | bool | 109 | — |
| 16 | `bCCOnStairs` | bool | 110 | — |
| 17 | `bGraphDriven` | bool | 113 | 74 (`bGraphDriven`) |
| 18 | `bGraphDrivenTranslation` | bool | 114 | — |
| 19 | `bGraphDrivenRotation` | bool | 115 | 73 (`bGraphDrivenRotation`) |
| 20 | `bGraphWantsFootIK` | bool | 171 | 115 (`bGraphWantsFootIK`) |
| 21 | `bIsFemale` | bool | 186 | — |
| 22 | `bIsThrowing` | bool | 208 | 100 (`bIsThrowing`) |
| 23 | `Enable_bEquipOK` | bool | 213 | — |
| 24 | `IsBlocking` | bool | 224 | 114 (`IsBlocking`) |
| 25 | `bUseRifleReadyDirectAt` | bool | 239 | — |
| 26 | `bEquipOkIsActiveEnabled` | bool | 242 | — |
| 27 | `bIsSneaking` | bool | 249 | — |
| 28 | `bAimEnabled` | bool | 273 | — |
| 29 | `bForceIdleStop` | bool | 257 | 165 (`bForceIdleStop`) |
| 30 | `bActorMobilityNotFullyCrippled` | bool | 253 | — |
| 31 | `bSyncDirection` | bool | 252 | — |
| 32 | `bDisableAttackReady` | bool | 251 | — |
| 33 | `bAllowHeadTracking` | bool | 112 | 116 (`bAllowHeadTracking`) |
| 34 | `bInLandingState` | bool | 194 | — |
| 35 | `bIsInFlavor` | bool | 296 | 180 (`bIsInFlavor`) |
| 36 | `bAimActive` | bool | 65 | 131 (`bAimActive`) |
| 37 | `bAllowRotation` | bool | 53 | 64 (`bAllowRotation`) |
| 38 | `bUseLeftHandIKDefaults` | bool | 270 | — |
| 39 | `LeftHandIKOn` | bool | 207 | — |
| 40 | `bEnableRoot_IsActiveMod` | bool | 195 | 173 (`bEnableRoot_IsActiveMod`) |
| 41 | `bIsInMT` | bool | 216 | — |
| 42 | `bRootRifleEquipOk` | bool | 254 | — |
| 43 | `bAimCaptureEnabled` | bool | 276 | — |
| 44 | `bDisableSpineTracking` | bool | 287 | 125 (`bDisableSpineTracking`) |
| 45 | `IsNPC` | bool | 68 | 139 (`IsNPC`) |
| 46 | `IsPlayer` | bool | 184 | 126 (`IsPlayer`) |
| 47 | `bFreezeSpeedUpdate` | bool | 192 | 124 (`bFreezeSpeedUpdate`) |
| 48 | `bFreezeRotationUpdate` | bool | 218 | — |
| 49 | `Direction` | float | 2 | 3 (`Direction`) |
| 50 | `fSpeedRun` | float | 1 | 1 (`fSpeedRun`) |
| 51 | `fSpeedWalk` | float | 0 | 0 (`fSpeedWalk`) |
| 52 | `SpineTwist` | float | 6 | 7 (`SpineTwist`) |
| 53 | `Speed` | float | 54 | 2 (`Speed`) |
| 54 | `PitchOffset` | float | 11 | 12 (`PitchOffset`) |
| 55 | `Pitch` | float | 21 | 21 (`Pitch`) |
| 56 | `TurnDelta` | float | 24 | 24 (`TurnDelta`) |
| 57 | `DirectionSmoothed` | float | 49 | 49 (`DirectionSmoothed`) |
| 58 | `AimStability` | float | 50 | 50 (`AimStability`) |
| 59 | `SampledSpeed` | float | 71 | — |
| 60 | `SpeedSmoothed` | float | 205 | 90 (`SpeedSmoothed`) |
| 61 | `ReloadSpeedMult` | float | 265 | 94 (`ReloadSpeedMult`) |
| 62 | `TurnDeltaSmoothed` | float | 39 | 39 (`TurnDeltaSmoothed`) |
| 63 | `WalkSpeedMult` | float | 197 | — |
| 64 | `runSpeedMult` | float | 199 | — |
| 65 | `DirectionDegrees` | float | 220 | — |
| 66 | `JogSpeedMult` | float | 198 | — |
| 67 | `weaponSpeedMult` | float | 248 | 53 (`weaponSpeedMult`) |
| 68 | `fLocomotionWalkPlaybackSpeed` | float | 233 | — |
| 69 | `fLocomotionJogPlaybackSpeed` | float | 234 | — |
| 70 | `fLocomotionRunPlaybackSpeed` | float | 235 | — |
| 71 | `fLocomotionSneakRunPlaybackSpeed` | float | 246 | — |
| 72 | `fLocomotionSneakWalkPlaybackSpeed` | float | 245 | — |
| 73 | `fik_footplantedgain` | float | 176 | — |
| 74 | `VelocityZ` | float | 48 | 48 (`VelocityZ`) |
| 75 | `AimHeadingCurrent` | float | 66 | 132 (`AimHeadingCurrent`) |
| 76 | `AimPitchCurrent` | float | 67 | 133 (`AimPitchCurrent`) |
| 77 | `fDirectAtSavedGain` | float | 225 | — |
| 78 | `fPlaybackMult` | float | 295 | — |
| 79 | `bAnimateWeaponBones` | float | 255 | — |
| 80 | `iState` | int | 8 | 9 (`iState`) |
| 81 | `iSyncSprintState` | int | 22 | 22 (`iSyncSprintState`) |
| 82 | `iWeaponChargeMode` | int | 263 | 104 (`iWeaponChargeMode`) |
| 83 | `iAttackState` | int | 36 | 36 (`iAttackState`) |
| 84 | `iGetUpType` | int | 83 | — |
| 85 | `iState_Raider_Stumble_Rifle` | int | 105 | — |
| 86 | `iState_NPCSneaking` | int | 118 | 54 (`iState_NPCSneaking`) |
| 87 | `iState_PlayerDefault` | int | 163 | 13 (`iState_PlayerDefault`) |
| 88 | `iState_NPCMelee` | int | 167 | — |
| 89 | `iState_NPCGun` | int | 185 | 82 (`iState_NPCGun`) |
| 90 | `iState_PlayerMelee` | int | 189 | 80 (`iState_PlayerMelee`) |
| 91 | `iState_NPCFastWalk` | int | 190 | — |
| 92 | `iControlsIdleSync` | int | 201 | — |
| 93 | `iSyncWalkRun` | int | 209 | — |
| 94 | `iState_NPCBlocking` | int | 222 | — |
| 95 | `iLocomotionSpeedState` | int | 238 | — |
| 96 | `iMeleeState` | int | 240 | — |
| 97 | `CurrentJumpState` | int | 285 | — |
| 98 | `iSyncTurnState` | int | 72 | — |
| 99 | `bPathingInterruptibleIdle` | int | 202 | — |
| 100 | `iSyncLocomotionSpeed` | int | 200 | — |
| 101 | `iSyncShuffleState` | int | 258 | — |
| 102 | `iSyncSneakWalkRun` | int | 243 | — |
| 103 | `iSyncDirection` | int | 244 | — |
| 104 | `iSyncForwardBackward00` | int | 228 | — |
| 105 | `iSyncForwardBackward` | int | 223 | — |
| 106 | `iSyncIdleLocomotion` | int | 3 | 4 (`iSyncIdleLocomotion`) |
| 107 | `iSyncJumpState` | int | 211 | 108 (`iSyncJumpState`) |
| 108 | `iSyncReadyAlertRelaxed` | int | 266 | — |
| 109 | `iIsPlayer` | int | 204 | — |

### 4.2 Full variable lists

Third-person graph (299 variables, name=index):

`fSpeedWalk=0`, `fSpeedRun=1`, `Direction=2`, `iSyncIdleLocomotion=3`, `CamPitchForward=4`, `CamPitchBackward=5`, `SpineTwist=6`, `CamRoll=7`, `iState=8`, `IsFirstPerson=9`, `m_bEnablePitchTwistModifier=10`, `PitchOffset=11`, `iState_NPCDefault=12`, `CamPitch=13`, `CamPitchDamped=14`, `CamRollDamped=15`, `damper_kP=16`, `damper_kI=17`, `damper_kD=18`, `SpineTwistDamped=19`, `Test=20`, `Pitch=21`, `iSyncSprintState=22`, `IsSprinting=23`, `TurnDelta=24`, `fTEST=25`, `fDampenTwist=26`, `speedDamped=27`, `isFiring=28`, `fDampenSighted=29`, `fTest3=30`, `isReloading=31`, `fTurnDeltaSpeedLimited=32`, `iState_NPCSighted=33`, `fTurnDeltaSpeedLimit=34`, `TurnDeltaSpeedLimitedDampened=35`, `iAttackState=36`, `IsAttackReady=37`, `PitchDelta=38`, `TurnDeltaSmoothed=39`, `PitchDeltaSmoothed=40`, `TurnDeltaDamped=41`, `DirectionDamped=42`, `PitchDeltaDamped=43`, `TurnDeltaSmoothedDamped=44`, `PitchDeltaSmoothedDamped=45`, `bAnimationDriven=46`, `isAttackNotReady=47`, `VelocityZ=48`, `DirectionSmoothed=49`, `AimStability=50`, `iIsInSneak=51`, `isJumping=52`, `bAllowRotation=53`, `Speed=54`, `bEquipOk=55`, `forceDirectionVector=56`, `camerafromx=57`, `camerafromy=58`, `camerafromz=59`, `LookAtOutsideLimit=60`, `AimHeadingMax=61`, `AimPitchMax=62`, `BowAimOffsetHeading=63`, `BowAimOffsetPitch=64`, `bAimActive=65`, `AimHeadingCurrent=66`, `AimPitchCurrent=67`, `IsNPC=68`, `bHeadTrackingOn=69`, `bHeadTrackingOff=70`, `SampledSpeed=71`, `iSyncTurnState=72`, `isLevitating=73`, `bFailMoveStart=74`, `bIsSynced=75`, `bDelayMoveStart=76`, `bVoiceReady=77`, `FootIKDisable=78`, `bInJumpState=79`, `bWantCastVoice=80`, `IsStaggering=81`, `staggerMagnitude=82`, `iGetUpType=83`, `iSyncFireState=84`, `IsSneaking=85`, `isMirrored=86`, `iSyncRunDirection=87`, `fTimeStep=88`, `SpineXTwist=89`, `SpineYTwist=90`, `SpineZTwist=91`, `fSpineTwistGain=92`, `fSpineTwistGainAdj=93`, `HeadZTwist=94`, `HeadYTwist=95`, `HeadXTwist=96`, `fHeadTwistGain=97`, `fHeadTwistGainAdj=98`, `fStumbleTimerThreshold=99`, `fStumbleSpeedStopThreshold=100`, `fStumbleDir=101`, `fStumbleDirDeltaStopThreshold=102`, `cHitReactionDir=103`, `cHitReactionBodyPart=104`, `iState_Raider_Stumble_Rifle=105`, `bNotHeadTrack=106`, `bShouldAimHeadTrack=107`, `bSupportedDeathAnim=108`, `bCCSupport=109`, `bCCOnStairs=110`, `fMaxForce=111`, `bAllowHeadTracking=112`, `bGraphDriven=113`, `bGraphDrivenTranslation=114`, `bGraphDrivenRotation=115`, `bGraphMotionIsAdditive=116`, `bShouldBeDrawn=117`, `iState_NPCSneaking=118`, `Pose=119`, `LookAtSpine2_LimitAngleDeg=120`, `LookAtSpine2_OnGain=121`, `LookAtSpine2_OffGain=122`, `LookAtSpine2_OnLeadIn=123`, `LookAtSpine2_OffLeadIn=124`, `LookAtSpine2_Enabled=125`, `LookAtSpine2_FwdAxisLS=126`, `LookAtChest_FwdAxisLS=127`, `LookAtChest_LimitAngleDeg=128`, `LookAtChest_OnGain=129`, `LookAtChest_OffGain=130`, `LookAtChest_OnLeadIn=131`, `LookAtChest_OffLeadIn=132`, `LookAtChest_Enabled=133`, `LookAtNeck_FwdAxisLS=134`, `LookAtNeck_LimitAngleDeg=135`, `LookAtNeck_OnGain=136`, `LookAtNeck_OffGain=137`, `LookAtNeck_OnLeadIn=138`, `LookAtNeck_OffLeadIn=139`, `LookAtNeck_Enabled=140`, `LookAtHead_FwdAxisLS=141`, `LookAtHead_LimitAngleDeg=142`, `LookAtHead_OnGain=143`, `LookAtHead_OffGain=144`, `LookAtHead_OnLeadIn=145`, `LookAtHead_OffLeadIn=146`, `LookAtHead_Enabled=147`, `LookAtLimitAngleDeg=148`, `LookAtLimitAngleThresholdDeg=149`, `LookAtOnGain=150`, `LookAtOffGain=151`, `LookAtOnLeadIn=152`, `LookAtOffLeadIn=153`, `LookAtUseBoneGains=154`, `LookAtAdditive=155`, `LookAtOutOfRange=156`, `LookAtAdditiveClamp=157`, `LookAtUseIndividualLimits=158`, `LookAtHead_LimitAngleDegVert=159`, `LookAtNeck_LimitAngleDegVert=160`, `LookAtChest_LimitAngleDegVert=161`, `LookAtSpine2_LimitAngleDegVert=162`, `iState_PlayerDefault=163`, `LookAt_RotateBoneAboutAxis=164`, `iState_NPCSneakingScreenspace=165`, `iState_NPCScreenspace=166`, `iState_NPCMelee=167`, `m_errorOutTranslation=168`, `m_alignWithGroundRotation=169`, `m_worldFromModelFeedbackGain=170`, `bGraphWantsFootIK=171`, `bClampAdditive=172`, `fik_OnOffGain=173`, `fik_GroundAscendingGain=174`, `fik_GroundDescendingGain=175`, `fik_footplantedgain=176`, `fik_FootRaisedGain=177`, `fik_FootUnlockGain=178`, `fik_ErrorUpDownBias=179`, `LookAtHead_LimitAngleVert_Dwn=180`, `LookAtNeck_LimitAngleVert_Dwn=181`, `bHumanoidFootIKDisable=182`, `BoolVariable=183`, `IsPlayer=184`, `iState_NPCGun=185`, `bIsFemale=186`, `fRagdollAnimAmount=187`, `fHitReactionEndTimer=188`, `iState_PlayerMelee=189`, `iState_NPCFastWalk=190`, `bBlockPipboy=191`, `bFreezeSpeedUpdate=192`, `bDoNotInterrupt=193`, `bInLandingState=194`, `bEnableRoot_IsActiveMod=195`, `fRandomClipStartTimePercentage=196`, `WalkSpeedMult=197`, `JogSpeedMult=198`, `runSpeedMult=199`, `iSyncLocomotionSpeed=200`, `iControlsIdleSync=201`, `bPathingInterruptibleIdle=202`, `bBlockPOVSwitch=203`, `iIsPlayer=204`, `SpeedSmoothed=205`, `testBlend=206`, `LeftHandIKOn=207`, `bIsThrowing=208`, `iSyncWalkRun=209`, `fIdleStopTime=210`, `iSyncJumpState=211`, `bTalkableWithItem=212`, `Enable_bEquipOK=213`, `iDynamicLoopStartState=214`, `fBodyMorphOffset=215`, `bIsInMT=216`, `bManualGraphChange=217`, `bFreezeRotationUpdate=218`, `iPcapTalkGenerator=219`, `DirectionDegrees=220`, `iWantBlock=221`, `iState_NPCBlocking=222`, `iSyncForwardBackward=223`, `IsBlocking=224`, `fDirectAtSavedGain=225`, `AimHeadingMaxCCW=226`, `AimHeadingMaxCW=227`, `iSyncForwardBackward00=228`, `fPitchUpLimit=229`, `fPitchDownLimit=230`, `GunGripPointer=231`, `bDisableIsAttackReady=232`, `fLocomotionWalkPlaybackSpeed=233`, `fLocomotionJogPlaybackSpeed=234`, `fLocomotionRunPlaybackSpeed=235`, `AimPitchMaxUp=236`, `AimPitchMaxDown=237`, `iLocomotionSpeedState=238`, `bUseRifleReadyDirectAt=239`, `iMeleeState=240`, `TEMPIsPlayer=241`, `bEquipOkIsActiveEnabled=242`, `iSyncSneakWalkRun=243`, `iSyncDirection=244`, `fLocomotionSneakWalkPlaybackSpeed=245`, `fLocomotionSneakRunPlaybackSpeed=246`, `RightArmInjuredPowerFist=247`, `weaponSpeedMult=248`, `bIsSneaking=249`, `fVaultDistance=250`, `bDisableAttackReady=251`, `bSyncDirection=252`, `bActorMobilityNotFullyCrippled=253`, `bRootRifleEquipOk=254`, `bAnimateWeaponBones=255`, `fVaultHeight=256`, `bForceIdleStop=257`, `iSyncShuffleState=258`, `isSightedOver=259`, `bPartialCover=260`, `RightHandIKOn=261`, `iSyncSightedState=262`, `iWeaponChargeMode=263`, `LeftHandIKControlsModifierActive=264`, `ReloadSpeedMult=265`, `iSyncReadyAlertRelaxed=266`, `_TestInt=267`, `fLeftHandIKFadeOut=268`, `fLeftHandIKTransformOnFraction=269`, `bUseLeftHandIKDefaults=270`, `iSyncGunDown=271`, `iRifleDrawnStateID=272`, `bAimEnabled=273`, `bShuffleSighted2=274`, `bShuffleSighted=275`, `bAimCaptureEnabled=276`, `Int32Variable00=277`, `_test=278`, `_Test2=279`, `GunGripPointer_Mirrored=280`, `HandIKControlsDataActive=281`, `HandIKControlsDataActive_Mirrored=282`, `RifleDrawnCurrentState=283`, `_TestBool=284`, `CurrentJumpState=285`, `bRenderFirstPersonInWorld=286`, `bDisableSpineTracking=287`, `bAdjust1stPersonFOV=288`, `pipboyUp=289`, `fControllerXSum=290`, `fControllerYSum=291`, `fPACameraAdd=292`, `fTestVar=293`, `fBodyCameraRotation=294`, `fPlaybackMult=295`, `bIsInFlavor=296`, `iTalkGenerator=297`, `staggerDirection=298`

First-person graph (181 variables, name=index):

`fSpeedWalk=0`, `fSpeedRun=1`, `Speed=2`, `Direction=3`, `iSyncIdleLocomotion=4`, `CamPitchForward=5`, `CamPitchBackward=6`, `SpineTwist=7`, `CamRoll=8`, `iState=9`, `IsFirstPerson=10`, `m_bEnablePitchTwistModifier=11`, `PitchOffset=12`, `iState_PlayerDefault=13`, `CamPitch=14`, `CamPitchDamped=15`, `CamRollDamped=16`, `damper_kP=17`, `damper_kI=18`, `damper_kD=19`, `SpineTwistDamped=20`, `Pitch=21`, `iSyncSprintState=22`, `IsSprinting=23`, `TurnDelta=24`, `fTest=25`, `fDampenTwist=26`, `speedDamped=27`, `isFiring=28`, `fDampenSighted=29`, `fTest3=30`, `isReloading=31`, `fTurnDeltaSpeedLimited=32`, `iState_NPCSighted=33`, `fTurnDeltaSpeedLimit=34`, `TurnDeltaSpeedLimitedDampened=35`, `iAttackState=36`, `IsAttackReady=37`, `PitchDelta=38`, `TurnDeltaSmoothed=39`, `PitchDeltaSmoothed=40`, `TurnDeltaDamped=41`, `DirectionDamped=42`, `PitchDeltaDamped=43`, `TurnDeltaSmoothedDamped=44`, `PitchDeltaSmoothedDamped=45`, `bAnimationDriven=46`, `isAttackNotReady=47`, `VelocityZ=48`, `DirectionSmoothed=49`, `AimStability=50`, `isJumping=51`, `isAttacking=52`, `weaponSpeedMult=53`, `iState_NPCSneaking=54`, `iZoomBehavior=55`, `fControllerYSum=56`, `iZoomState=57`, `bDisableLooking=58`, `bEnableLooking=59`, `fControllerXSmoothed=60`, `fControllerYSmoothed=61`, `fPitchFactor=62`, `fTurnFactor=63`, `bAllowRotation=64`, `fArmTurnFactor=65`, `fArmPitchFactor=66`, `fIdleSpeedMult=67`, `iCategory=68`, `fZoomLevel1=69`, `Test=70`, `bAdjust1stPersonFOV=71`, `bIsSynced=72`, `bGraphDrivenRotation=73`, `bGraphDriven=74`, `bRenderFirstPersonInWorld=75`, `staggerMagnitude=76`, `staggerDirection=77`, `iState_NPCAttacking=78`, `bEquipOk=79`, `iState_PlayerMelee=80`, `iIsInSneak=81`, `iState_NPCGun=82`, `iState_Player1HM=83`, `iState_Player2HM=84`, `iState_PlayerPistol=85`, `iState_PlayerShoulderMounted=86`, `iState_PlayerBigGuns=87`, `iState_NPCFastWalk_1stP=88`, `iState_PlayerSneakMelee=89`, `SpeedSmoothed=90`, `fControllerXRaw=91`, `fControllerYRaw=92`, `iSyncChargeState=93`, `ReloadSpeedMult=94`, `AimWobble=95`, `iSyncSightedState=96`, `iSyncWeaponDrawState=97`, `isStagger=98`, `bBlockPOVSwitch=99`, `bIsThrowing=100`, `bBlockPipboy=101`, `FirstPersonSpeedSmoothed=102`, `LeftHandTarget=103`, `iWeaponChargeMode=104`, `iSyncGunDown=105`, `SightedSpeedMult=106`, `AimWobbleSpeedMult=107`, `iSyncJumpState=108`, `fCrippledWobbleWeight=109`, `bDoNotAllowMelee=110`, `bDoNotAllowThrow=111`, `bIsAnimationDriven=112`, `iWantBlock=113`, `IsBlocking=114`, `bGraphWantsFootIK=115`, `bAllowHeadTracking=116`, `LookAtOutOfRange=117`, `camerafromx=118`, `camerafromy=119`, `camerafromz=120`, `fRandomClipStartTimePercentage=121`, `LookAtChest_Enabled=122`, `LookAtSpine2_Enabled=123`, `bFreezeSpeedUpdate=124`, `bDisableSpineTracking=125`, `IsPlayer=126`, `Pose=127`, `bManualGraphChange=128`, `pipboyUp=129`, `fControllerXSum=130`, `bAimActive=131`, `AimHeadingCurrent=132`, `AimPitchCurrent=133`, `AimHeadingMaxCCW=134`, `AimHeadingMaxCW=135`, `fDirectAtSavedGain=136`, `AimPitchMaxUp=137`, `AimPitchMaxDown=138`, `IsNPC=139`, `fPACameraAdd=140`, `fPipboyInputYScaled=141`, `fPipboyInputXScaled=142`, `pipboyState=143`, `fPipboyInputX=144`, `fPipboyInputY=145`, `fPipboyInputMagnitude=146`, `test_holotape=147`, `fIdleTimer=148`, `fRadioTune=149`, `fZeroToOneAngle=150`, `RH_HandIKTarget_WeaponLeft=151`, `testFloat=152`, `testPipboyIK=153`, `testModCamPitch=154`, `iPipboyRadio=155`, `testRadioBlend=156`, `fZoom2=157`, `fZoom1=158`, `fZoom0=159`, `fRadioZoom1=160`, `fRadioZoom2=161`, `iFromRadio=162`, `iPipboyRootStartState=163`, `LookAtOutsideLimit=164`, `bForceIdleStop=165`, `bDoNotInterrupt=166`, `LookAtLimitAngleDeg=167`, `LookAtChest_LimitAngleDeg=168`, `LookAtNeck_LimitAngleDeg=169`, `LookAtHead_LimitAngleDeg=170`, `LookAtHead_OnGain=171`, `LookAtAdditive=172`, `bEnableRoot_IsActiveMod=173`, `LookAtOnGain=174`, `LookAtOffGain=175`, `bTalkableWithItem=176`, `iTalkGenerator=177`, `bFreezeRotationUpdate=178`, `iPcapTalkGenerator=179`, `bIsInFlavor=180`

Names in the 1st-person graph that do not exist in the 3rd-person root graph:

`fTest`, `isAttacking`, `iZoomBehavior`, `iZoomState`, `bDisableLooking`, `bEnableLooking`, `fControllerXSmoothed`, `fControllerYSmoothed`, `fPitchFactor`, `fTurnFactor`, `fArmTurnFactor`, `fArmPitchFactor`, `fIdleSpeedMult`, `iCategory`, `fZoomLevel1`, `iState_NPCAttacking`, `iState_Player1HM`, `iState_Player2HM`, `iState_PlayerPistol`, `iState_PlayerShoulderMounted`, `iState_PlayerBigGuns`, `iState_NPCFastWalk_1stP`, `iState_PlayerSneakMelee`, `fControllerXRaw`, `fControllerYRaw`, `iSyncChargeState`, `AimWobble`, `iSyncWeaponDrawState`, `isStagger`, `FirstPersonSpeedSmoothed`, `LeftHandTarget`, `SightedSpeedMult`, `AimWobbleSpeedMult`, `fCrippledWobbleWeight`, `bDoNotAllowMelee`, `bDoNotAllowThrow`, `bIsAnimationDriven`, `fPipboyInputYScaled`, `fPipboyInputXScaled`, `pipboyState`, `fPipboyInputX`, `fPipboyInputY`, `fPipboyInputMagnitude`, `test_holotape`, `fIdleTimer`, `fRadioTune`, `fZeroToOneAngle`, `RH_HandIKTarget_WeaponLeft`, `testFloat`, `testPipboyIK`, `testModCamPitch`, `iPipboyRadio`, `testRadioBlend`, `fZoom2`, `fZoom1`, `fZoom0`, `fRadioZoom1`, `fRadioZoom2`, `iFromRadio`, `iPipboyRootStartState`

### 4.3 Creature descriptors (STR-FO4)

Keys are STR's CRC64 graph keys; "vars in graph" is the size of the dumped enum. Files:
`Code/encoding/Structs/Fallout4/AnimationGraphDescriptor_<Name>.cpp` @ `b850014`.

| Descriptor (STR file) | CRC64 key | vars in graph | bool | float | int |
|---|---|---|---|---|---|
| Alien | 1050516629324185412 | 106 | 7 | 21 | 4 |
| Angler | 2566467023248089962 | 86 | 8 | 12 | 3 |
| Bloatfly | 10733037448866675267 | 68 | 5 | 7 | 3 |
| Bloodbug | 7385599169756089322 | 84 | 8 | 9 | 6 |
| Bloodworm | 7786656801015324445 | 56 | 5 | 10 | 7 |
| Brahmin | 9156151190671507217 | 108 | 5 | 9 | 6 |
| CaveCricketRoot | 7359588577465619653 | 42 | 4 | 8 | 6 |
| Deathclaw | 13518681907060316898 | 104 | 16 | 17 | 6 |
| Deer | 1426621359402524832 | 114 | 5 | 5 | 4 |
| FogCrawler | 453515791105675987 | 68 | 5 | 9 | 7 |
| Ghoul | 18279284073093955153 | 128 | 7 | 17 | 7 |
| Gorilla | 9822742478992769303 | 87 | 5 | 15 | 5 |
| InsectsSmall | 11398773395717218432 | 69 | 3 | 6 | 4 |
| MutantHound | 13422174473106868592 | 111 | 6 | 19 | 7 |
| RabbitChicken | 16006527083653121093 | 9 | 1 | 5 | 3 |
| Radscorpion | 18391308120865710389 | 94 | 4 | 16 | 6 |
| Rat | 10665350860146563200 | 93 | 7 | 11 | 7 |
| RootState | 16544277667400076734 | 54 | 2 | 7 | 3 |
| Stingwing | 1567904913354835406 | 77 | 5 | 7 | 4 |
| SuperMutantRootBehavior | 4192192227136413005 | 205 | 25 | 28 | 10 |
| Wolflike | 884686398289769216 | 136 | 10 | 25 | 6 |
| YaoGuai | 837991345629064437 | 90 | 5 | 14 | 7 |

- **Alien**: bool: `bAnimationDriven, IsAttackReady, LookAtOutOfRange, bAimActive, bAimEnabled, bGraphWantsHeadTracking, bEquipOk`; float: `Speed, staggerDirection, fRunSpeed, Direction, HeadZTwist, fHeadTwistGainAdj, SpineZTwist, fSpineTwistGainAdj, fWalkSpeed, fDirectAtSavedGain, AimHeadingCurrent, TurnDeltaSmoothed, HeadYTwist, SpeedSmoothed, SpineYTwist, fTimeStep, AimPitchCurrent, HeadXTwist, SpineXTwist, TurnDelta, staggerMagnitude`; int: `iSyncTurnState, iSyncIdleLocomotion, iCombatState, iMovementSpeed`;
- **Angler**: bool: `bAllowRotation, bAnimationDriven, LookAtOutOfRange, bInCombat, bGraphWantsHeadTracking, IsEquipping, isAttacking, bIsAttackStanding`; float: `SpeedSmoothed, fTimeStep, HitReactionTimer_Interp, SpeedSampled, SpineZTwist, fSpineTwistGainAdj, TurnDeltaSmoothed, Speed, SpineYTwist, fHeadTwistGainAdj, Direction, SpineXTwist`; int: `iSyncIdleLocomotion, iSyncTurnState, cHitReactionBodyPart`;
- **Bloatfly**: bool: `bAnimationDriven, bEquipOk, LookAtOutOfRange, IsAttackReady, bGraphWantsHeadTracking`; float: `staggerDirection, Direction, TurnDeltaSmoothed, SpeedSmoothed, Speed, TurnDelta, staggerMagnitude`; int: `iCombatState, iSyncTurnState, iSyncIdleLocomotion`;
- **Bloodbug**: bool: `bGraphDrivenRotation, bManualGraphChange, bIsSynced, IsAttackReady, bGraphDriven, bAnimationDriven, bEquipOk, LookAtOutOfRange`; float: `staggerDirection, SpineZTwist, TurnDeltaSmoothed, TurnDelta, SpineYTwist, SpeedSmoothed, Direction, Speed, staggerMagnitude`; int: `iSyncIdleLocomotion, cHitReactionBodyPart, iRecoilSelector, iCombatState, iSyncTurnState, iSyncDirectionState`;
- **Bloodworm**: bool: `IsAttackReady, bAnimationDriven, bGraphWantsHeadTracking, bEquipOk, bIsTunneling`; float: `Direction, SpineYTwist, walkBackSpeedMult, SpineXTwist, TurnDeltaSmoothed, TurnDelta, Speed, runForwardSpeedMult, SpineZTwist, walkForwardSpeedMult`; int: `iSyncTurnState, iCombatState, iLocomotionSpeed, iDynamicAnimSelector, cHitReactionBodyPart, iSyncIdleLocomotion, iSyncForwardState`;
- **Brahmin**: bool: `bAnimationDriven, bSupportedDeathAnim, bGraphWantsHeadTrackingLeft, IsAttackReady, bGraphWantsHeadTrackingRight`; float: `TurnDelta, staggerDirection, Speed, runSpeedMult, SpeedSmoothed, walkForwardSpeedMult, Direction, trotSpeedMult, TurnDeltaSmoothed`; int: `cHitReactionBodyPart, iSyncIdleLocomotion, cHitReactionDir, iSyncTurnState, iLocomotionSpeed, iCombatState`;
- **CaveCricketRoot**: bool: `bManualGraphChange, bEquipOk, IsAttackReady, bAnimationDriven`; float: `runForwardSlowSpeedMult, TurnDelta, Speed, WalkBackSpeedMult, TurnDeltaSmoothed, runForwardSpeedMult, WalkForwardSpeedMult, Direction`; int: `iSyncTurnState, iSyncIdleLocomotion, iCombatState, cHitReactionBodyPart, iLocomotionSpeed, iSyncForwardState`;
- **Deathclaw**: bool: `LookAtOutOfRange, bEquipOk, bGraphDrivenRotation, bEnableFootIK, bAnimationDriven, bCCSupport, IsSprinting, bInCombat, bGraphDriven, bInJumpState, isAttacking, bIsAttackStanding, bIsSynced, bGraphWantsHeadTracking, bAllowRotation, IsEquipping`; float: `HeadYTwist, TurnDeltaSmoothed, fTimeStep, SpineZTwist, HeadXTwist, Speed, fSpineTwistGainAdj, SpineYTwist, TurnDelta, SpineXTwist, Direction, staggerDirection, HitReactionTimer_Interp, HeadZTwist, SpeedSampled, SpeedSmoothed, fHeadTwistGainAdj`; int: `iSyncIdleLocomotion, iSyncTurnState, iSyncFootState, cHitReactionBodyPart, iState, iSyncSprintState`;
- **Deer**: bool: `IsSprinting, bEquipOk, bGraphWantsHeadTracking_Right, bGraphWantsHeadTracking_Left, IsAttackReady`; float: `TurnDelta, Speed, TurnDeltaSmoothed, SpeedSmoothed, Direction`; int: `iCombatState, iSyncTurnState, iSyncIdleLocomotion, iSyncSprintState`;
- **FogCrawler**: bool: `IsAttackReady, bEquipOk, bGraphWantsHeadTracking, bAnimationDriven, LookAtOutOfRange`; float: `SpineYTwist, walkForwardSpeedMult, walkBackSpeedMult, SpineXTwist, Direction, Speed, TurnDeltaSmoothed, SpineZTwist, runForwardSpeedMult`; int: `cHitReactionDir, iCombatState, iLocomotionSpeed, cHitReactionBodyPart, iDynamicAnimSelector, iSyncIdleLocomotion, iSyncForwardState`;
- **Ghoul**: bool: `bEquipOk, bAllowHeadTracking, bAnimationDriven, LookAtOutOfRange, bManualGraphChange, IsAttackReady, IsSprinting`; float: `fHeadTwistGainAdj, fSpineTwistGainAdj, staggerMagnitude, fRunSpeedPlaybackMult, SpineZTwist, fik_footplantedgain, staggerDirection, fTimeStep, fHitReactionEndTimer, TurnDeltaSmoothed, fWalkPlaybackSpeedMult, SpeedSmoothed, Direction, SpineYTwist, SpineXTwist, TurnDelta, Speed`; int: `iCombatState, iSyncTurnState, iState, iSyncSprintState, iSyncIdleLocomotion, iRecoilSelector, cHitReactionBodyPart`;
- **Gorilla**: bool: `bEquipOk, bAnimationDriven, IsAttackReady, LookAtOutOfRange, bGraphWantsHeadTracking`; float: `SpeedSmoothed, staggerDirection, fHeadTwistGainAdj, Speed, SpineZTwist, fSpineTwistGainAdj, fHitReactionEndTimer, SpineYTwist, Direction, fTimeStep, fLocomotionWalkMult, TurnDeltaSmoothed, SpineXTwist, TurnDelta, fLocomotionRunMult`; int: `iRecoilSelector, iSyncTurnState, iCombatState, iSyncIdleLocomotion, cHitReactionBodyPart`;
- **InsectsSmall**: bool: `bAnimationDriven, IsAttackReady, bEquipOk`; float: `Direction, staggerDirection, TurnDeltaSmoothed, SpeedSmoothed, TurnDelta, Speed`; int: `iSyncIdleLocomotion, iSyncTurnState, iCombatState, iRecoilSelector`;
- **MutantHound**: bool: `LookAtOutOfRange, bAnimationDriven, bGraphWantsHeadTracking, bEquipOk, cHitReactionDir, IsAttackReady`; float: `HeadXTwist, HeadZTwist, TurnDelta, SpeedSmoothed, SpineXTwist, staggerDirection, TurnDeltaSmoothed, HeadYTwist, Speed, runSpeedMult, SpineZTwist, fHitReactionEndTimer, trotSpeedMult, fTimeStep, fHeadTwistGainAdj, SpineYTwist, fSpineTwistGainAdj, Direction, walkForwardSpeedMult`; int: `iDynamicAnimSelector, iSyncTurnState, iRecoilSelector, cHitReactionBodyPart, iSyncIdleLocomotion, iCombatState, iLocomotionState`;
- **RabbitChicken**: bool: `bGraphDriven`; float: `Speed, TurnDelta, WalkSpeedMult, runSpeedMult, TurnDeltaSmoothed`; int: `iLocomotionState, iSyncIdleLocomotion, iSyncTurnState`;
- **Radscorpion**: bool: `IsAttackReady, bEquipOk, bAnimationDriven, bIsTunneling`; float: `staggerDirection, WalkSpeedMult, TurnDeltaSmoothed, fHitReactionEndTimer, fRArmTwistGainAdj, TurnDelta, LArmXTwist, LArmYTwist, Speed, SpeedSmoothed, fSpineTwistGainAdj, LArmZTwist, Direction, fTimeStep, runSpeedMult, fLArmTwistGainAdj`; int: `cHitReactionBodyPart, iCombatState, iRecoilSelector, iSyncIdleLocomotion, iLocomotionSpeed, iSyncTurnState`;
- **Rat**: bool: `cHitReactionBodyPart, bEquipOk, bGraphWantsHeadTracking, LookAtOutOfRange, bAnimationDriven, IsAttackReady, bSupportedDeathAnim`; float: `staggerDirection, fLocomotionWalkSpeedMult, SpineXTwist, Direction, SpeedSmoothed, TurnDelta, Speed, SpineZTwist, TurnDeltaSmoothed, fLocomotionRunSpeedMult, SpineYTwist`; int: `iDynamicAnimSelector, iRecoilSelector, iSyncTurnState, iSyncLocomotionSpeed, iCombatState, iSyncForwardState, iSyncIdleLocomotion`;
- **RootState**: bool: `bAnimationDriven, bForceIdleStop`; float: `Direction, Speed, fWalkPlaybackSpeedMult, SpeedSmoothed, TurnDelta, TurnDeltaSmoothed, fRunPlaybackSpeedMult`; int: `iSyncTurnState, iSyncLocomotionState, iSyncIdleLocomotion`;
- **Stingwing**: bool: `bSupportedDeathAnim, bAnimationDriven, IsAttackReady, bEquipOk, bManualGraphChange`; float: `Speed, TurnDelta, TurnDeltaSmoothed, SpeedSmoothed, staggerMagnitude, staggerDirection, Direction`; int: `iCombatState, iSyncTurnState, iSyncIdleLocomotion, iRecoilSelector`;
- **SuperMutantRootBehavior**: bool: `isReloading, bAnimateWeaponBones, bAimActive, bCCSupport, bUseLeftHandIKDefaults, bIsInMT, bBlockPipboy, bAnimationDriven, HandIKControlsDataActive_Mirrored, LeftHandIKControlsModifierActive, bEquipOk, HandIKControlsDataActive, LeftHandIKOn, isMirrored, bAllowHeadTracking, bAimEnabled, bAllowRotation, bDisableAttackReady, bPartialCover, bManualGraphChange, IsSneaking, bIsSynced, bIsThrowing, IsAttackReady, LookAtOutOfRange`; float: `fSpineTwistGainAdj, TurnDelta, HeadYTwist, fLocomotionRunPlaybackSpeed, SpeedSmoothed, fTimeStep, HeadXTwist, AimHeadingMaxCW, SpineXTwist, Speed, AimPitchCurrent, fLeftHandIKTransformOnFraction, AimHeadingMaxCCW, Direction, HeadZTwist, AimHeadingCurrent, fHitReactionEndTimer, fik_footplantedgain, SpineZTwist, staggerDirection, fDirectAtSavedGain, SpineYTwist, fLocomotionWalkPlaybackSpeed, DirectionDegrees, TurnDeltaSmoothed, staggerMagnitude, fLeftHandIKFadeOut, fHeadTwistGainAdj`; int: `iSyncIdleLocomotion, cHitReactionBodyPart, cHitReactionDir, iSyncTurnState, iSyncDirection, iLocomotionSpeedState, iSyncSightedState, iSyncReadyAlertRelaxed, RifleDrawnCurrentState, iState`;
- **Wolflike**: bool: `bIsMoving, IsAttackReady, bGraphWantsHeadTracking, bWalkForwardRandomize, bSupportedDeathAnim, LookAtOutOfRange, cHitReactionDir, bAnimationDriven, bUpdateSpineTwistTarget, cHitReactionBodyPart`; float: `TurnDelta, fBodyPartBlendDamped, SpineXTwist, fTurnDeltaTarget, fHeadBlendDampedClamped, TurnDeltaSmoothed, fEarBlendDampedClamped, fTrotFastClipMult, SpineZTwist, fEarBlendDamped, HeadXTwist, fAccelOrDecel, fRunClipMult, SpineYTwist, fHeadBlendDamped, fRoll, fTrotClipMult, fHitReactionEndTimer, fTimeStep, TurnDeltaDamped, Speed, fBodyPartBlendDampedClamped, fRollTarget, fWalkClipMult, HeadTwistGainAdj`; int: `iSyncLocomotionRangeID, iSyncIdleLocomotion, iRecoilSelection, iSyncCombatState, iSyncTurnState, iSyncWalkPose`;
- **YaoGuai**: bool: `bGraphWantsHeadTracking, bAnimationDriven, IsAttackReady, LookAtOutOfRange, bEquipOk`; float: `Direction, SpineYTwist, fTimeStep, TurnDelta, SpeedSmoothed, SpineXTwist, Speed, TurnDeltaSmoothed, runForwardSpeedMult, staggerDirection, walkForwardSpeedMult, fHitReactionEndTimer, fSpineTwistGainAdj, SpineZTwist`; int: `iLocomotionSpeed, iSyncTurnState, iDynamicAnimSelector, iCombatState, iSyncIdleLocomotion, cHitReactionBodyPart, iRecoilSelector`;

### 4.4 Caveats

1. **Types were guessed** (§2.4). Visible errors: `bAnimateWeaponBones` in the float list and
   `bPathingInterruptibleIdle` in the int list of the human descriptor; `cHitReactionDir/BodyPart` in bool lists of
   MutantHound, Rat, Wolflike. Havok stores one 32-bit word per variable, so a wrong type corrupts the value. Read the
   real type from `hkbBehaviorGraphData` variable infos (RE task) or validate with typed getters.
2. **Root graph only.** STR hashed `BShkbHkxDB`'s variables of the root project. Variables used by loaded subgraphs
   may be missing: F4-PIP-OS sets `iSyncWeaponDrawState` on a 3rd-person graph although STR's TP dump has it only in
   the 1st-person graph; FO4_Wrld writes `iSyncForwardState`/`iSyncStrafeState`, which are in neither dump.
3. **Name conflicts to resolve with a dump:** STR's TP graph has `SampledSpeed` (71); FO4_Wrld code writes
   `SpeedSampled` (also used by several creature graphs). STR has `Direction` and `DirectionDegrees`; FO4_Wrld treated
   `Direction` as degrees in [−180, 180] (code disabled). Skyrim's `Direction` is 0..1.
4. Indices are invalidated by mods, by subgraph changes and by race switches (power armor). **Resolve names → index
   per `BShkbAnimationGraph*` at runtime and cache by graph pointer + CRC64 key.**
5. STR also synced `IsPlayer`, `IsNPC`, `iIsPlayer` to remote actors (the human graph branches on player vs NPC
   states such as `iState_PlayerDefault` vs `iState_NPCDefault`). Test whether a remote "player" NPC needs
   `IsPlayer=true` to pick player-style states.

### 4.5 Recommended subset for FalloutMP (human; validate in the prototype)

Read by **name** from the active graph; send only names that exist in the remote 3rd-person graph; write on the
remote **after** the bound-channel flush (Actor implements `IPostAnimationChannelUpdateFunctor` at +0x168) or feed the
source (process direction/velocity), otherwise channels overwrite `Speed`/`Direction`/`TurnDelta`.

| Tier | Contents | Rate / channel |
|---|---|---|
| T0 movement | pos, rot (yaw), worldOrCell, `Speed`, `Direction`, `TurnDelta`, `VelocityZ`, aim pitch (`AimPitchCurrent` or `Pitch`), `AimHeadingCurrent`; ints `iSyncIdleLocomotion`, `iSyncTurnState`, `iSyncWalkRun`/`iSyncSneakWalkRun`, `iSyncLocomotionSpeed`, `iSyncDirection`, `iSyncForwardBackward`, `iSyncJumpState`, `iSyncSprintState`; bools `bInJumpState`, `IsSneaking`/`iIsInSneak`, `IsSprinting` | 15-20 Hz unreliable-sequenced, delta vs last acked, interpolated with a 100-150 ms buffer |
| T1 state | `ActorState` words (weaponState, gunState, knockState, lifeState, meleeAttackState, stance, interactingState, moveMode incl. swimming), sit state + furniture ref, in-power-armor, `iSyncSightedState`, `iSyncGunDown`, `iSyncReadyAlertRelaxed`, `bAimActive`, `bAimEnabled`, `isFiring`, `isReloading`, `IsAttackReady`, `iAttackState`, `iMeleeState`, `bIsThrowing`, `IsBlocking`/`iWantBlock`, `IsStaggering`, `staggerMagnitude`, `staggerDirection`, `iGetUpType`, `weaponSpeedMult`, `ReloadSpeedMult`, `iWeaponChargeMode`, equipped weapon instance id, archetype/flavor keywords | on change, piggy-backed on T0 with a "keyframe" every 1 s |
| T2 actions | `{seq, ts, actionFormId, targetId, priority, resolvedEvent, idleFormId, perspective}` | reliable ordered, immediate |
| T3 events | whitelisted names (§5.2) with result and graph index | reliable ordered, batched per frame (never "latest only") |

Do **not** sync: IK/look-at gains (`LookAt*`, `fik_*`), `cam*`, controller inputs (`fController*`), Pip-Boy and zoom
variables, `test*`, `_Test*`.

---

## 5. Actions and animation events

### 5.1 BGSAction forms (Fallout4.esm, all 173 AACT records)

**[src: Mutagen FK Mutagen.Bethesda.FormKeys.Fallout4/Fallout4/ActionRecord.cs]**. Cross-check: STR hard-codes
`0x5704C` for `ActionInstantInitializeGraphToBaseState` (Animation.cpp:64) — identical. Prefer resolving actions at
runtime via `DEFAULT_OBJECT::kAction*` (list in alandtse `BGSDefaultObjectManager.h:41-168`, 394 entries).

| Action | Form ID | Action | Form ID | Action | Form ID |
|---|---|---|---|---|---|
| `ActionAOEAttack` | 0x43363 | `ActionActivate` | 0x13009 | `ActionActivateLoopingBegin` | 0x2AE5D |
| `ActionActivateLoopingEnd` | 0x2AE5C | `ActionAttackMissed` | 0xB0ED1 | `ActionAvailableCondition1Heal` | 0x12D9D4 |
| `ActionBark` | 0xDB7F1 | `ActionBleedoutStart` | 0x13EC9 | `ActionBleedoutStop` | 0x13ECA |
| `ActionBlockAnticipate` | 0x193CE | `ActionBlockHit` | 0x13AF4 | `ActionBoltCharge` | 0xB259D |
| `ActionBumpedInto` | 0x3DE4D | `ActionCameraAToCameraB` | 0x180E46 | `ActionCameraBToCameraA` | 0x180E47 |
| `ActionChangeAnimArchetype` | 0xB343A | `ActionChangeAnimFlavor` | 0xB3444 | `ActionCombatEnter` | 0x2C5A0 |
| `ActionCombatExit` | 0x2C5A1 | `ActionCoverSprintStart` | 0x212E3 | `ActionCower` | 0x47166 |
| `ActionCustomBooing` | 0x1120D8 | `ActionCustomCheering` | 0x10FF87 | `ActionCustomLaughing` | 0x118010 |
| `ActionDeath` | 0x489ED | `ActionDeathWait` | 0x5DD59 | `ActionDeferredKill` | 0xFFD26 |
| `ActionDialogueEnter` | 0x22D22 | `ActionDialogueExit` | 0x22D23 | `ActionDodge` | 0x42651 |
| `ActionDraw` | 0x132AF | `ActionDualAttack` | 0x50C96 | `ActionDualPowerAttack` | 0x2E2F7 |
| `ActionDualRelease` | 0x50C97 | `ActionEnterCover` | 0x19A61 | `ActionEnterDialogueCameraState` | 0xD4138 |
| `ActionEscortWait` | 0x7BC04 | `ActionEvade` | 0x42650 | `ActionExitCover` | 0x19A62 |
| `ActionFall` | 0x937F4 | `ActionFireAuto` | 0x4A5C | `ActionFireCharge` | 0x4A5B |
| `ActionFireChargeHold` | 0x11A186 | `ActionFireEmpty` | 0x20A702 | `ActionFireSingle` | 0x4A5A |
| `ActionFlinchStart` | 0x1ABC86 | `ActionFlipThrow` | 0x238E3 | `ActionFlyStart` | 0x3B4E3 |
| `ActionFlyStop` | 0x3B4E4 | `ActionForceEquip` | 0x2ADF1 | `ActionFurnitureFull` | 0x307E2 |
| `ActionFurnitureNoLongerFull` | 0x307E3 | `ActionGetUp` | 0xD1FDD | `ActionGunAlert` | 0x523AC |
| `ActionGunChargeStart` | 0xC4F72 | `ActionGunDown` | 0x22A35 | `ActionGunReady` | 0x10770E |
| `ActionGunRelaxed` | 0x3B248 | `ActionHide` | 0x195A0E | `ActionHoverStart` | 0x3B4E5 |
| `ActionHoverStop` | 0x3B4E6 | `ActionIdle` | 0x13002 | `ActionIdleFlavor` | 0x60E4D |
| `ActionIdlePlayful` | 0xB88E7 | `ActionIdleStop` | 0x18BA8 | `ActionIdleStopInstant` | 0x7F8E3 |
| `ActionIdleWarn` | 0x98886 | `ActionInitializeGraphToBaseState` | 0x2FFA9 | `ActionInstantAttackReset` | 0x12BC9F |
| `ActionInstantInitializeGraphToBaseState` | 0x5704C | `ActionInteractionEnter` | 0x2248F | `ActionInteractionEnterInstant` | 0x2248E |
| `ActionInteractionExit` | 0x2248D | `ActionInteractionExitAlt` | 0xA8C2F | `ActionInteractionExitQuick` | 0x2248C |
| `ActionIntimidate` | 0x2279D | `ActionJetpackStart` | 0x125C7E | `ActionJetpackStop` | 0x125C7F |
| `ActionJump` | 0x13006 | `ActionKnockDown` | 0xD1FDC | `ActionLand` | 0x937F5 |
| `ActionLargeMovementDelta` | 0x2E444 | `ActionLeadingArrival` | 0x32AAA | `ActionLeadingArrivalEmote` | 0x32AA9 |
| `ActionLeadingDeparture` | 0x32AA6 | `ActionLeadingDoneEmote` | 0x32AA8 | `ActionLeftArmHeal` | 0x12D9D2 |
| `ActionLeftAttack` | 0x13004 | `ActionLeftInterrupt` | 0x13453 | `ActionLeftPowerAttack` | 0x2E2F6 |
| `ActionLeftReady` | 0x13452 | `ActionLeftRelease` | 0x13451 | `ActionLeftSyncAttack` | 0x24F14 |
| `ActionLegsCritical` | 0x340FF | `ActionLegsHeal` | 0x3DAFE | `ActionLimbCritical` | 0x2CBA4 |
| `ActionListen` | 0x489F2 | `ActionListenNegative` | 0x22818 | `ActionListenNeutral` | 0x2281A |
| `ActionListenPositive` | 0x22817 | `ActionListenQuestion` | 0x22819 | `ActionLook` | 0x1300A |
| `ActionMantle` | 0x299B0 | `ActionMelee` | 0x4A59 | `ActionMoveBackward` | 0x5EDCC |
| `ActionMoveForward` | 0x5EDC9 | `ActionMoveLeft` | 0x5EDCD | `ActionMoveRight` | 0x5EDCE |
| `ActionMoveStart` | 0x959F8 | `ActionMoveStop` | 0x959F9 | `ActionNonSupportContact` | 0xF3CD7 |
| `ActionPanic` | 0x47167 | `ActionPathEnd` | 0x2EDB9 | `ActionPathStart` | 0x2EDB8 |
| `ActionPerkCannibal` | 0xAC560 | `ActionPerkSandman` | 0xAC561 | `ActionPipboyClose` | 0x222CD |
| `ActionPipboyData` | 0x1DB41 | `ActionPipboyInspect` | 0x42D82 | `ActionPipboyInventory` | 0x1DB42 |
| `ActionPipboyLoadHolotape` | 0x22A80 | `ActionPipboyMap` | 0x1DB43 | `ActionPipboyOpen` | 0x1901F |
| `ActionPipboyRadioOff` | 0x21E9C | `ActionPipboyRadioOn` | 0x21E9B | `ActionPipboySelect` | 0x21B7D |
| `ActionPipboyStats` | 0x1DB40 | `ActionPipboyTab` | 0x1DB44 | `ActionPipboyTabPrevious` | 0x21B7C |
| `ActionPipboyZoom` | 0x1A954 | `ActionPropellersOff` | 0x116811 | `ActionPropellersOn` | 0x116812 |
| `ActionRagdollInstant` | 0x9BB4E | `ActionRecoil` | 0x13AF5 | `ActionRecoilLarge` | 0x13EC8 |
| `ActionReload` | 0x4A56 | `ActionRightArmHeal` | 0x12D9D3 | `ActionRightAttack` | 0x13005 |
| `ActionRightInterrupt` | 0x13456 | `ActionRightPowerAttack` | 0x13383 | `ActionRightReady` | 0x13455 |
| `ActionRightRelease` | 0x13454 | `ActionRightSyncAttack` | 0x24F13 | `ActionSheath` | 0x46BAF |
| `ActionShieldChange` | 0x94065 | `ActionShuffle` | 0x42655 | `ActionSighted` | 0x4A57 |
| `ActionSightedRelease` | 0x4A58 | `ActionSneak` | 0x13007 | `ActionSprintStart` | 0x3B4A7 |
| `ActionSprintStop` | 0x13219 | `ActionStaggerStart` | 0x138D2 | `ActionStopEffect` | 0x20941 |
| `ActionStumbleStart` | 0x3F219 | `ActionSummonedStart` | 0x45B5B | `ActionSwimStart` | 0x14DB5D |
| `ActionSwimStateChange` | 0x13003 | `ActionSwimStop` | 0x14DB5E | `ActionTalking` | 0x489F1 |
| `ActionThrow` | 0x4E32 | `ActionTrick` | 0x20E952 | `ActionTunnel` | 0xAE4B5 |
| `ActionTurnLeft` | 0x959FD | `ActionTurnRight` | 0x959FC | `ActionTurnStop` | 0x959FE |
| `ActionUnconsciousEnter` | 0x1244CB | `ActionUnconsciousExit` | 0x1244CC | `ActionVoice` | 0x13008 |
| `ActionVoiceInterrupt` | 0x13459 | `ActionVoiceReady` | 0x13458 | `ActionVoiceRelease` | 0x13457 |
| `ActionWardHit` | 0x24B3A | `ActionWeaponHotkey` | 0x1BEAD4 |  |  |

Most relevant for sync: locomotion (`MoveStart/Stop`, `TurnLeft/Right/Stop`, `Jump`, `Fall`, `Land`, `Mantle`,
`SprintStart/Stop`, `Sneak`, `Swim*`), weapon (`Draw`, `Sheath`, `ForceEquip`, `Sighted`, `SightedRelease`, `Fire*`,
`Reload`, `BoltCharge`, `GunChargeStart`, `GunAlert/Down/Ready/Relaxed`, `Throw`, `FlipThrow`, `Melee`, `RightAttack`,
`RightPowerAttack`, `RightRelease`, `BlockAnticipate`, `BlockHit`, `InstantAttackReset`), interaction (`Activate`,
`Interaction*`, `Idle*`, `Pipboy*`), reactions (`StaggerStart`, `StumbleStart`, `FlinchStart`, `Recoil*`, `KnockDown`,
`GetUp`, `RagdollInstant`, `Bleedout*`, `Death`, `DeathWait`, `Unconscious*`), PA (`JetpackStart/Stop`), graph resets
(`InitializeGraphToBaseState` 0x2FFA9, `InstantInitializeGraphToBaseState` 0x5704C).

### 5.2 Animation event names found in sources

| Group | Names | Source |
|---|---|---|
| Graph output events of the 1st-person root (what gameplay listens to) | `Event00`, `pipboyClosed`, `pipboyOpened`, `ReloadComplete`, `ReloadEnd`, `sightedStateEnter`, `sightedStateExit`, `throwEnd`, `weapEquip`, `weapForceEquip`, `weaponDraw`, `WeaponFire`, `weaponInstantDown`, `weaponSwing`, `weapUnequip` | [src: icodei CommonLibF4/src/RE/Bethesda/BSAnimation/FirstPersonRootEvents.cpp; ExtendedWeaponSystem/src/Hooks.cpp:183-201] |
| Reload variants | `reloadStateEnter`, `reloadStateExit`, `reloadSequentialStart`, `reloadSequentialReserveStart` | [src: icodei ExtendedWeaponSystem/src/Hooks.cpp:188-191] |
| Input events mirrored live → private 3rd-person graph | `sneakStart`, `sneakStop`, `sneakStateEnter`, `sneakStateExit`, `jumpStart`, `jumpStartFromWalk`, `jumpFall`, `jumpLand`, `jumpLandSoft`, `jumpLandToWalk`, `jumpLandToRun`, `jumpEnd`, `jumpEndToRun`, `attackStart`, `attackStartAuto`, `attackRelease`, `attackStop`, `attackStateEnter`, `attackStateExit`, `attackInterrupt`, `AttackEnd`, `reloadStart`, `reloadStateEnter`, `reloadStateExit`, `reloadReserveStart`, `meleeattackStart`, `meleeattackSprintStart`, `meleeAttackGun`, `blockStart`, `grenadeThrowStart`, `mineThrowStart`, `SyncLeft`, `SyncRight`, `SyncCycleEnd` | [src: F4-PIP-OS CharacterAnimation.cpp:94-102] |
| Pose / state entry | `g_archetypeBaseStateStart[Instant]`, `g_archetypeRelaxedStateStart[Instant]`, `weapEquip`, `rifleSightedStart`, `rifleSightedEnd`, `sightedStateEnter`, `sightedStateExit`, `UpdateSighted` | [src: F4-PIP-OS CharacterAnimation.cpp:510-543] |
| Wake a reset graph | `g_archetypeBaseStateStartInstant`, `MoveStop` | [src: FO4_Wrld first_person_graph.cpp kWakeEvents] |
| CK wiki | `weaponFire` (one per shot, several for automatic fire), `reloadStartSlave`, `reloadComplete` (refills ammo), `reloadEnd` | [web: https://falloutck.uesp.net/wiki/Animation_Event_List] (page behind a bot challenge; snippet via search) |

Event names are `BSFixedString` and compare case-insensitively **[inference]**. Furniture, power armor, death and
creature events were not found in any accessible source: take them from the `ENAM` of the `IDLE` records the
idle manager resolves (log `ActionOutput.animEvent` and `sequence` in the prototype).

Skyrim → FO4 mapping of what SkyMP special-cases: `JumpStandingStart/JumpDirectionalStart` → `jumpStart`,
`jumpStartFromWalk`; `SneakStart/Stop` → `sneakStart/Stop`; `SprintStart/Stop` → `ActionSprintStart/Stop`;
`BlockStart/Stop` → `blockStart`, `iWantBlock`; `weapEquip/Unequip` → `weapEquip`/`weapUnequip` (draw/holster via
`ActionDraw`/`ActionSheath` or a ported `SetWeaponDrawnMode`); `attackStart*`/`bowAttackStart` → `attackStart`,
`attackStartAuto` + `ActionFire*`; `IdleChair*Enter` → `ActionInteractionEnter` + furniture; `Ragdoll`/`GetUpBegin`
→ `PushActorAway` + `iGetUpType` / `ActionGetUp`.

---

## 6. Strategy comparison

| Criterion | A. Event replay (SkyMP) | B. Action + variable sync (STR) | C. Bone transforms (FO4_Wrld) | D. Hybrid (recommended) |
|---|---|---|---|---|
| First-person local player | Events come from the 1st-person graph; some do not exist in 3rd person, 3rd-person-only transitions are missing | Actions are perspective-independent; only ~50 of 109 variables have a 1P counterpart | Requires driving the parked 3rd-person graph on the sender (revive, mirror events, forced update, suppress bone copy) — version-fragile RE | Engine state + actions are perspective-independent; variables read by name from the active graph; parked-graph driving optional |
| Locomotion | AI keepOffset trick — **not available in FO4** | Variables + `ForcePosition` + process direction | Exact pose, but feet slide/float on remote terrain (no IK) | Variables + interpolation + native position; derived speed/direction from the movement controller |
| Bandwidth per remote player (est.) | ~1 KB/s (7.7 Hz × ~100 B + events) | ~2-3 KB/s (10 Hz full snapshots of ~200 B); ~1 KB/s with deltas | ~26 KB/s measured (20 Hz × 80 × 16 B); ~5 KB/s quantized at 10 Hz | ~1-2 KB/s |
| 20 players in one area (download per client) | ~20 KB/s | ~40-60 KB/s | ~500 KB/s (server egress ~10 MB/s) | ~20-40 KB/s |
| NPC hosting (host runs AI, 3rd person only) | Fine | Fine | Pose stream per NPC (FO4_Wrld: 30 Hz NPC pose) — expensive | Fine |
| Power armor | Needs PA event names; graph rebuilt on enter | Needs a PA descriptor (none exists) and re-hash on race switch | Needs PA skeleton graft + retarget (53 + 20 bones) | Enter/exit as server-validated furniture interaction; PA variable config by name, re-resolved on graph rebuild |
| Creatures | Generic | 22 descriptors exist; robots/mirelurks missing | Per-skeleton schema (FO4_Wrld bug: mole rat stretched) | Generic actions/events + per-race name config |
| Mod robustness | Medium (names) | Low (hash/index tables) | High for visuals | High (names, runtime resolution) |
| Server validation | Event names (SkyMP stamina) — spoofable, game-specific | Actions (semantic) — STR does none | None possible (no semantics) | Actions + explicit fire/reload messages + state machine (AP for sprint/power attacks, fire rate vs WEAP/OMOD, ammo) |
| Late join | Last event only | Last action / replay cache (STR-dev) | Immediate (pose) | State keyframe + instant variants (`SnapIntoInteraction`, `SwitchToPowerArmor`, `ActionInstantInitializeGraphToBaseState`, `g_archetypeBaseStateStartInstant`) + short action replay |
| Visual fidelity | Medium | Medium-high | Highest (any clip, scripted idles) | Medium-high; upgradeable with a parked-graph layer |
| Implementation risk in FO4 | Medium (names), high (locomotion) | Medium | High (deep RE, per-patch RVAs, FO4_Wrld took months) | Medium |

**Recommendation: D.** Justification:
1. First person is FO4's default view. The only signals that are identical in both perspectives are **engine state**
   (`ActorState`, sneak/sprint/sighted/PA/furniture) and **actions** (`BGSAction`, resolved independently by each
   graph's idle tree). Building on them removes the first-person problem instead of working around it. Even option C
   had to solve first person at the source (FO4_Wrld v0.6.5).
2. FO4 removes SkyMP's locomotion trick, so variable-driven locomotion (STR's half) is needed anyway.
3. Name-based runtime resolution fixes STR's biggest weakness (hash-keyed tables, wrong types, missing subgraph
   variables) and makes power armor/race switches and mods manageable.
4. Bandwidth and server load stay at SkyMP's level (SkyMP targets 100+ players per server; C does not scale).
5. Actions give the server semantics for validation (fire rate, ammo, AP), replacing name-keyed stamina rules.
6. Remote replays use `Actor::PerformAction`/`PlayIdleAction`, letting the *remote* 3rd-person idle tree pick the
   clip; STR's `PerformComplexAction` with the sender's resolved idle risks feeding a 1st-person idle to a
   3rd-person graph **[inference — verify in the prototype; keep the resolved event name as fallback]**.

Fallback rule if the prototype shows D leaves visible gaps: add FO4_Wrld's parked-graph driving on the **sender**
and read graph-internal 3rd-person variables (and, at most, a handful of aim/spine bones) from it. Do not stream full
skeletons.

---

## 7. Implementation plan

### 7.1 Tasks (in order)

**fallout4-platform (fork of `skyrim-platform/src/platform_se/skyrim_platform`)**

1. `game/Offsets.h` (FO4 AE IDs): holder notify (base `IAnimationGraphManagerHolder::NotifyAnimationGraphImpl`, OG
   REL::ID 1379025 per F4-PIP-OS), `BSAnimationGraphManager::ProcessGraphEvent` (OG 1199489) /
   `NotifyAnimationGraph` (1.11.191 `sub_14130EAE0`), `SetActiveGraph` (`sub_14130EDF0`), `ActorMediator::PerformAction`
   (OG 502377), `ActorMediator` singleton (OG 1358859), `PerformComplexAction` (OG 1445653), `Actor::PerformAction`
   (AE 2231177), weapon `Fire` (OG 1056038), `Projectile::Launch` (OG 1452335), draw/sheathe functions. **RE:** map
   every OG ID / 1.11.191 RVA to the targeted AE build (Address Library ID database or signatures), verify in x64dbg.
2. `FridaHooks.cpp`: port `HOOK_SEND_ANIMATION_EVENT` with `refr = holder − 0x48`; add `graphIndex`,
   `isPlayerFirstPerson` and the result to the ctx. Add `HOOK_PERFORM_ACTION` (enter/leave with action, target,
   priority, resolved `animEvent`, `sequence` idle, result), `HOOK_SET_ACTIVE_GRAPH`, and blocking hooks for remote
   `Fire`/`Launch`. Reentrancy guard so platform-initiated replays are not captured (STR's `g_forceAnimation`).
3. New `AnimApi.cpp` + typings in `codegen/convert-files/Definitions.txt`:
   `getGraphVariables(refId, names, graph?)`, `setGraphVariables(refId, values, {afterChannelFlush})`,
   `dumpGraphVariables(refId, graphIndex)` → `{name, index, type}` + CRC64 key, `notifyAnimationGraph(refId, name)`
   (FO4 has no `Debug.SendAnimationEvent`), `performAction(refId, actionId, targetId, priority)`,
   `getActorAnimState(refId)` (ActorState words decoded, sit state, furniture, PA, swimming),
   `getCameraState()`, `on('animationGraphEvent', {refId, tag, payload})` from the `BSAnimationGraphEvent` sink (+0x38).
4. Replace `Magic/*` with `Anim/GraphVariableTable.{h,cpp}`: per-`BShkbAnimationGraph*` name→index→type cache keyed
   by CRC64, invalidated on graph rebuild (race switch, PA, `RevertAnimationGraphManager`, subgraph activation —
   sink `BSSubGraphActivationUpdate` on Actor at +0x148).
5. Missing CommonLibF4 types (add locally): `BShkbAnimationGraph` (+0x370/+0x378), `BShkbHkxDB` (+0x84),
   `hkbBehaviorGraph` (+0xC0, +0x110, +0x1AA), `hkbVariableValueSet`, `ActorMediator`, `BGSActionData`/`TESActionData`
   (ActionInput 0x28 + ActionOutput 0x30 + flags; STR layout `TESActionData.h` static_asserts eventName 0x28,
   idleForm 0x48 on OG). **RE:** `hkbBehaviorGraphData` variable infos (types), FO4 Havok 2014 layout.
6. `TESModPlatform` natives: `SetWeaponDrawnMode` (FO4 draw/sheathe), template NPC + DoNothing-style package for
   remote actors, `SetGraphVariable*AfterChannels` (post-channel functor or Actor `PostUpdateAnimationGraphManager`).
7. Optional fidelity module `Anim/ParkedGraphDriver.cpp` (FO4_Wrld algorithm, §3.3), behind a setting.

**falloutmp-client (fork of `skymp5-client`)**

8. `src/sync/animation.ts`: FO4 tables (ignore list, perspective map, instant overrides); event **queue** with
   sequence numbers and timestamps (fixes W1); reliable ordered send (fixes W2).
9. New `src/sync/actions.ts`: capture from `hooks.performAction`, replay with `performAction`, fallback to
   `notifyAnimationGraph(resolvedEvent)`; per-action rules (e.g. drop `ActionMoveStart/Stop/Turn*` — locomotion is
   variable-driven; never replay `Fire*` gameplay).
10. New `src/sync/animState.ts` and `src/sync/graphVars.ts` with per-race configs
    `src/sync/graphDescriptors/{human3p,powerArmor,deathclaw,…}.json` (names + expected types).
11. Rewrite `src/sync/movementGet.ts`/`movementApply.ts`: no `keepOffsetFromActor`; 100-150 ms jitter buffer;
    native position each frame; T0 variables written after channels; pitch via aim variables, not actor X rotation.
12. Split `sendInputsService.ts` into Movement/Animation/Action services; hosted NPCs use the same path.
13. `src/view/formView.ts`: spawn order = appearance → equipment (OMOD instance) → archetype/flavor → wait for
    subgraph load → state keyframe (instant variants) → movement → timed actions/events.
14. `deathService.ts`, `hitService.ts`, new `weaponFireService.ts` (explicit fire/projectile messages, STR pattern).
15. Port `animDebugService.ts` overlay; add a graph-variable inspector.

**skymp5-server (GameProfile = fallout4)**

16. Messages: extend `UpdateAnimationMessage`/`AnimationData` or add `UpdateActionsMessage` and an anim-state block
    in `UpdateMovementMessage`; add `animState` + `actionReplay` to `CreateActorMessage`.
17. `AnimationSystem.cpp`: callbacks keyed by action form ID (plus event-name fallback): AP drain for sprint, AP for
    power attacks, fire-rate and ammo checks, reload resets magazine; config-driven.
18. `MpActor`: last anim state + `ActionReplayCache`-style refined chain (STR-dev design) for late joiners.
19. Unit tests (Catch2): serialization round-trips, replay-cache refinement, AP/fire-rate rules.

**Verification in game**: two clients side by side (FO4_Wrld's single-instance patch, RVA 0xC2FB62 on 1.11.191,
shows local two-client testing is possible), scripted scenario matrix of §7.2 in 1st and 3rd person, video capture,
plus automatic counters: remote `PerformAction`/`NotifyAnimationGraph` acceptance rate, state mismatches between
sender keyframe and remote `getActorAnimState`, bandwidth per service.

### 7.2 Prototype first: de-risk first-person capture

Build a small standalone F4SE plugin (CommonLibF4, AE) before the platform exists; port it into fallout4-platform
later.

**Hooks and what to log** (CSV, one row per call, high-resolution timestamp, frame number):

| Probe | Log fields |
|---|---|
| Holder notify (base `NotifyAnimationGraphImpl`) | refId, event, result, manager graph count, `activeGraph`, camera state |
| Manager notify / `ProcessGraphEvent` | manager ptr (map to player), event, result |
| `ActorMediator::PerformAction` | actor, action EDID + form ID, target, priority, result, `animEvent`, `targetAnimEvent`, `sequence` idle EDID/form ID (+ its `ENAM`), flags |
| `BSAnimationGraphEvent` sink (+0x38) on the player and a test NPC | tag, payload |
| `SetActiveGraph` + camera state | old/new index, `m_isActive` of both graphs |
| Subgraph activation | current/requested default and weapon subgraph IDs |
| 20 Hz sampler | for graph[0] and graph[1] separately (direct `hkbVariableValueSet` read) and through holder getters: every name of §4.5 plus everything in §4.2; `ActorState` words; `IsSneaking/IsSprinting/IsInIronSights/IsWeaponDrawn`; speed/direction from the movement controller |
| Variable dump on demand | name, index, type, CRC64 key for both player graphs, a human NPC, the player in power armor, and each creature at hand |

**Scenario matrix** (each in 1st person, 3rd person, and switching view mid-action): idle → walk → run → sprint →
stop; strafe in 8 directions; turn in place; sneak walk/run; jump standing/moving, landing; draw/holster pistol,
rifle, shotgun, minigun, 1H melee, 2H melee, unarmed, grenade; sighted enter/exit; fire single, automatic, charge
(Gauss), bolt action, empty; reload normal/empty/sequential; throw grenade and mine; gun bash; melee light/power,
block; chair and workbench enter/exit; power armor enter, walk, sprint, jetpack, exit; swim; stagger, knockdown,
death; Pip-Boy open/close; VATS enter/exit.

**Questions the logs must answer**
1. Which event names reach the player holder in 1st vs 3rd person, and which exist only in one perspective?
2. Do `PerformAction` actions and their target/idle resolution differ between perspectives?
3. Which variables stay fresh in the parked graph; units of `Direction`; does `SampledSpeed` or `SpeedSampled`
   exist; where do `iSyncWeaponDrawState`, `iSyncForwardState`, `iSyncStrafeState` live (root or subgraph)?
4. Does AE 1.11.x produce STR's OG key `8074503569708505439` for the human graph (if yes, STR's indices are
   reusable as defaults)?
5. What does the power armor graph contain (no descriptor exists)?

**Replay check**: spawn an AI-disabled human NPC with the same equipment; feed the logs with (a) events only,
(b) actions via `Actor::PerformAction`, (c) actions via `PerformComplexAction` with the sender's idle, (d) variables
only, (e) D = state + actions + whitelisted events + variables. Record acceptance rate, time to visual convergence and
side-by-side video. Exit criterion: (e) reproduces all scenarios except VATS/kill moves without the parked-graph
layer, or the logs show exactly which states need it.

---

## 8. Open questions

- AE Address Library IDs for every function used by STR (OG) and FO4_Wrld (1.11.191 RVAs).
- Whether `ObjectReference.PlayAnimation(string)` on actors is equivalent to `NotifyAnimationGraph` in FO4 [inference:
  likely; use the native call anyway].
- Whether the debug-only Papyrus natives `ForceMovementDirection/Speed` (Actor.psc:1040-1060) are bound in release
  builds; they could drive remote locomotion through the movement controller instead of variable writes.
- Event names for furniture, power armor, death and creatures (take from IDLE `ENAM` at runtime).
- Commonwealth Online's implementation (closed source).

## 9. Web sources

- https://falloutck.uesp.net/wiki/Animation_Event_List (bot challenge; search snippet only)
- https://falloutck.uesp.net/wiki/Subgraphs
- https://commonwealth-online.com/ and https://commonwealth-online.com/roadmap/
- https://www.nexusmods.com/fallout4/mods/107542 (Commonwealth Online)
- Repositories and commits listed in §0.
