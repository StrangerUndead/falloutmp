# SkyMP Sync Inventory & Standards (reference for FalloutMP)

> **Purpose.** This is the reference for "sync every FalloutMP feature by the same standards SkyMP uses". It lists every sync mechanism in SkyMP, every feature from end to end, the conventions those features share (the "SkyMP standard"), the testing harness, and step-by-step recipes for extending the system.
> **Code base.** Repo HEAD `c16c7b9` (upstream SkyMP `f926944`). All paths are relative to the repo root. Line numbers are valid at this commit.
> **Provenance tags.**
> - **[src: path:line]**: verified by reading the code at that location.
> - **[inference]**: my reasoning from the code, not stated in it.
> - **[doc]**: taken from `docs/*.md`, which may be stale.
>
> **Vocabulary.**
> - `S` = server, `C` = client.
> - "FF form": a runtime form with id ≥ `0xff000000`.
> - "ESM form": a form from a plugin, id < `0xff000000`.
> - "owner": the user whose actor it is.
> - "hoster": the user that simulates an NPC.
> - "listeners": the actors subscribed to a reference through the grid.
>
> **Companion.** `docs/FALLOUT4_PORT_RESEARCH.md` is the broad survey. This file assumes it and goes one level deeper.

---

## 0. Most important facts for designing FalloutMP's sync standard

1. **One `Serialize(Archive&)` per message** drives the binary BitStream format, the JSON form and tests.
   - JS on the client only ever sees JSON; the C++ `MpClientPlugin` converts both ways.
   - There are 33 message types.
   - A message type that is not registered is **silently dropped** by the server. The client **throws** on an unknown server→client type. [src: messages/MessageSerializerFactory.cpp:188-291; server_guest_lib/PacketParser.cpp:61-62; skymp5-client/src/services/services/networkingService.ts:188-191]
2. **The server is semi-authoritative.**
   - Trusted: the client's own transform, animation events, and *decreases* of its own health/magicka/stamina.
   - Everything else is a request that the server validates: inventory, containers, equipment, appearance, activation, crafting, hits/damage, death/respawn, NPC ownership, cell transitions. [src: ActionListener.cpp]
3. **Visibility is a 4096-unit grid with a 3×3 neighbourhood per world/cell.**
   - Every subscription sends a full `CreateActor` snapshot; every unsubscription sends `DestroyActor`.
   - Live changes go out as `UpdateProperty` deltas or as relayed packets.
   - No gameplay message is broadcast world-wide. [src: Grid.h:75-95; MpObjectReference.cpp:683-724; PartOne.cpp:759-896]
4. **Persistence is one `MpChangeFormREFR` per changed reference, stored as JSON**, with ids saved as `FormDesc` (`"hexId:Plugin.esm"`).
   - Every write goes through `ChangeFormGuard::EditChangeForm`, which queues a save.
   - Position is saved at most every 30 s while the actor moves. [src: MpChangeForms.h:57-150; ChangeFormGuard.h:27-35; MpObjectReference.cpp:1349-1354]
5. **NPCs are simulated by a "hoster" client.**
   - The hoster's local AI drives them; it streams movement and animation, and sends hits, activations and spell casts on their behalf.
   - The server grants hosting when nobody has sent movement for that NPC within 2 s.
   - The server checks hoster identity on every NPC-scoped message. [src: ActionListener.cpp:653-738, 39-96]
6. **Gamemode (JS) can veto almost every action.** Each action is a `GameModeEvent` whose default effect runs in `OnFireSuccess` only if no handler returned `false`. Papyrus events are themselves gamemode events (`onPapyrusEvent:<Name>`). [src: gamemode_events/GameModeEvent.cpp:7-45; MpForm.cpp:34-40]
7. **The server runs Papyrus.** Natives that have client-visible effects mirror themselves to clients with **SpSnippet** (a server→client RPC "call `sp.<Class>.<Function>`"), which can return a value back through `FinishSpSnippet`. [src: SpSnippet.cpp:23-61; spSnippetService.ts]
8. **Rejections usually send a correction:**
   - `Teleport2` snap-back for movement.
   - `SetInventory` resend plus SpSnippet `UnequipItem`/`RemoveSpell` for equipment.
   - `ChangeValues` for cropped regeneration.
   - `HostStop` for a bad hoster.
   - SpSnippet `RemoveSpell` for a blocked book read.

   Some rejections send nothing (put/take, drop). [src: §1.9]
9. **SkyMP is inconsistent in places**, so "parity" must be defined per feature (§3.2):
   - Melee reach check is commented out.
   - Spell magic effects are TODO.
   - Power/sneak attack flags are trusted.
   - Stamina increases are not cropped.
   - Actor-value percentages are reset on load.
   - `lastAnimation` and quests are not persisted.
   - The client uses the wrong property name for `isDisabled`.
   - Property updates for NPCs with ESM ids are dropped on the client.
10. **Tests.** `PartOne` can be driven without networking (a fake send target, `DoConnect`/`DoMessage`). Almost every server feature has a Catch2 test in `unit/`. JS integration tests in `misc/tests` run a real server with a test gamemode. [src: unit/TestUtils.*; cmake/run_integration_test.cmake]

---

## 1. Core sync mechanisms

### 1.1 End-to-end data path

```
[C] ClientListener service --emit("sendMessage"| "sendMessageWithRefrId")-->
    NetworkingService.onSendMessage / onSendMessageWithRefrId   (skymp5-client/src/services/services/networkingService.ts:21-50)
      (_refrId → idx resolved from WorldModel; undefined _refrId = own character)
    sp.mpClientPlugin.send(JSON, reliable) → MpClientPlugin::Send   (falloutmp-server/cpp/mp_common/MpClientPlugin.cpp:91-102)
      → MessageSerializer::Serialize(json): JSON→binary; unknown 't' → raw JSON  (cpp/messages/MessageSerializerFactory.cpp:188-222)
      → RakNet Client::Send  RELIABLE | UNRELIABLE, MEDIUM_PRIORITY, channel 0      (cpp/mp_common/Networking.cpp:88-92)
[S] node loop: server.tick(); await setTimeout(1)                         (falloutmp-server/ts/index.ts:221-235)
    ScampServer::Tick → ServerCombined::Tick → Server::Tick (drain all packets) (cpp/addon/ScampServer.cpp:470-508; Networking.cpp:184-207)
    → HandlePacketServerside → PartOne::HandlePacket (connect/disconnect/message)  (Networking.cpp:359+; server_guest_lib/PartOne.cpp:429-474)
    → PartOne::HandleMessagePacket (packet-history record, playback suppression)  (PartOne.cpp:915-960)
    → PacketParser::TransformPacketIntoAction → MessageSerializer::Deserialize     (PacketParser.cpp:46-202)
    → ActionListener::On<Msg>(RawMessageData{unparsed bytes, length, userId}, msg) (ActionListener.cpp)
    → MpObjectReference / MpActor / WorldState / GameModeEvent / Papyrus VM
    → outputs: raw relay ActionListener::SendToNeighbours (ActionListener.cpp:39-96)
               FormCallbacks.sendToUser / sendToUserDeferred (PartOne.cpp:654-735)
               PartOneSendTargetWrapper::Send(IMessageBase) (PartOne.cpp:34-44)
    → Server::Send  RELIABLE_ORDERED | UNRELIABLE, channel 0                       (Networking.cpp:171-182)
    then PartOne::Tick: TickPacketHistoryPlaybacks → TickDeferredMessages → WorldState::Tick (save storage, timers) (PartOne.cpp:146-151)
[C] NetworkingService.onTick → mpClientPlugin.tick → MpClientPlugin::Tick: binary→JSON via Deserialize+WriteJson (MpClientPlugin.cpp:53-89; cpp/client/main.cpp)
    → route by msg.t to typed emitter event (networkingService.ts:69-195)
    → RemoteServer handlers mutate WorldModel kept in sp.storage            (skymp5-client/src/services/services/remoteServer.ts:101-993)
      (+ SpSnippetService, GamemodeUpdateService, GamemodeEventSourceService, AuthService…)
    → WorldView.onUpdate → FormViewArray.updateAll → FormView.update/applyAll (src/view/worldView.ts:104-138; formViewArray.ts:193-237; formView.ts:38-587)
```

### 1.2 Wire format and serialization

| Aspect | Behaviour | Evidence |
|---|---|---|
| Packet layout | byte 0 = `Networking::MinPacketId` (134); byte 1 = `MsgType` (written by `Serialize("t", kMsgType)`); then the fields in `Serialize()` order | [src: messages/MinPacketId.h:3; MessageSerializerFactory.cpp:74-78; messages/MessageBase.h:73-107] |
| Primitive encoding | Arithmetic types are written raw through `SLNet::BitStream::Write`; a `bool` takes 1 bit. Strings and `std::vector` are a uint32 length plus elements. `std::array<T,N>` is N elements with no length. `std::optional` is a 1-bit flag plus the value. `std::variant` is a uint32 index plus the value. Structs recurse into their own `Serialize` | [src: serialization/include/archives/BitStreamOutputArchive.h:23-109; serialization/src/BitStreamUtil.cpp] |
| JSON form | Same `Serialize`. `JsonOutputArchive` omits `nullopt` fields. `SimdJsonInputArchive` throws on a missing **required** key; a missing optional key becomes `nullopt` | [src: JsonOutputArchive.h:38-48; SimdJsonInputArchive.h ~l.261-305] |
| Registration | `REGISTER_MESSAGES` list in `messages/Messages.h:35-68` → arrays indexed by `MsgType` in `MessageSerializerFactory::CreateMessageSerializer` | [src: MessageSerializerFactory.cpp:158-178] |
| Binary deserialize | Looked up by header byte (byte 1) | [src: MessageSerializerFactory.cpp:259-290] |
| JSON deserialize | Used when byte 1 is `'{'`. Tries **every** registered deserializer (re-parsing the JSON each time) until one has a matching integer `"t"`. Legacy string `"type"` messages return `nullopt` | [src: MessageSerializerFactory.cpp:118-145, 239-257] |
| Unregistered C→S type | `MpClientPlugin` sends raw JSON (`MessageSerializer::Serialize` fallback). The server's `Deserialize` returns `nullopt`, so `PacketParser` does nothing: a **silent drop** | [src: MessageSerializerFactory.cpp:205-219; PacketParser.cpp:61-62] |
| Unregistered S→C type | The client throws `Unhandled MsgType` inside the tick handler | [src: networkingService.ts:188-191] |
| `UpdateProperty` from client | Ignored | [src: PacketParser.cpp:150-152] |
| Protocol version | `kMessagingProtocolVersion = "7_"` is also the RakNet password prefix (plus the optional server `password` and the client file `Data/Platform/Distribution/password`). The code says to bump it on **any** protocol change | [src: mp_common/Config.h:5-12; addon/ScampServer.cpp:344-347; MpClientPlugin.cpp:15-34] |
| `idx` | `FormIndex` of an `MpObjectReference`. It is assigned by `MakeID` in `WorldState::AddForm` and **recycled** after `DestroyForm`. Clients map it to local slots with `IdManager` | [src: WorldState.cpp:118-132, 198-201; skymp5-client/src/lib/idManager.ts] |
| Long form ids | For an actor with an ESM id the server sends `formId + 0x100000000` (in `CreateActor.refrId`, `HostStart`/`HostStop.target`, SpSnippet object args). The client then spawns an FF clone instead of using the vanilla actor. `localIdToRemoteId` strips the offset | [src: PartOne.cpp:805-809; ActionListener.cpp:699-702; PartOne.cpp:644-647; SpSnippet.cpp:87-101; skymp5-client/src/view/worldViewMisc.ts:53-87] |
| Player alias | The client uses `0x14` for its own actor in `Activate`, `OnHit`, `SpellCast` and SpSnippet self ids; the server substitutes the real actor id | [src: ActionListener.cpp:456,1020-1022,1138-1141; SpSnippet.cpp:40-44; SpSnippetFunctionGen.cpp:51-58] |

### 1.3 Connection and login lifecycle

| # | Side | Step | Code |
|---|---|---|---|
| 1 | C | `SkympClient` restores `authGameData` from `sp.storage`, or emits `authNeeded`. `AuthService` shows the login UI. On `authAttempt` it calls `startClient` | [src: skymp5-client/src/services/services/skympClient.ts:21-78] |
| 2 | C | `establishConnectionConditional` → `SettingsService.getTargetPeer` → `NetworkingService.connect(host, port)` → `mpClientPlugin.createClient`. RakNet connects with password `"7_"+filePassword` and a 60 s timeout | [src: skympClient.ts:87-103; networkingService.ts:52-55,197-207; MpClientPlugin.cpp:12-41] |
| 3 | S | `ID_NEW_INCOMING_CONNECTION` → `IdManager.allocateId` → `ServerCombined` maps it to a combined user id (**id 0 is reserved**, see `login.ts`) → `PartOne::HandlePacket(ServerSideUserConnect, guid)` → `AddUser`: `serverState.Connect(userId, guid)`, listener `OnConnect` (JS `"connect"`), then the cached **`UpdateGamemodeData`** bytes (reliable) | [src: Networking.cpp:378-394; mp_common/NetworkingCombined.h:17-29,127-146; PartOne.cpp:438-451,899-913; addon/ScampServerListener.cpp:10-17] |
| 4 | C | `connectionAccepted` → `RemoteServer.handleConnectionAccepted` clears the `WorldModel`. `AuthService.handleConnectionAccepted` sends `CustomPacket{contentJsonDump: {"customPacketType":"loginWithSkympIo","gameData":{session}\|{profileId}}}` reliably. Login is re-attempted (reconnect or show the login dialog) after 15 s | [src: remoteServer.ts:813-819; authService.ts:607-651,653-676] |
| 5 | S | `OnCustomPacket` → listener `OnCustomPacket` → JS `"customPacket"` → `index.ts` strips `customPacketType` → `Login.customPacket` | [src: ActionListener.cpp:106-114; ScampServerListener.cpp:28-39; ts/index.ts:281-295; ts/systems/login.ts:84-92] |
| 6 | S | **Offline mode:** emit `spawnAllowed(userId, profileId, [], undefined)` | [src: login.ts:233-237] |
| 6' | S | **Online mode:** async master-API session lookup with fetch retries ×10. Then a guid-unchanged check (protects against "soul transmission" when a user slot is reused), the gamemode hook `mp.onLoginAttempt(profileId)`, Discord guild membership/ban/IP checks, then emit `spawnAllowed(userId, profile.id, roles, discordId)` | [src: login.ts:102-228] |
| 7 | S | `Spawn` system: `getActorsByProfileId(profileId)[0]`. If it exists: `setEnabled(actor, true)` then `setUserActor`. Otherwise `createActor(0, startPoints[rand].pos, angleZ, worldOrCell, profileId)`, `setUserActor`, `setRaceMenuOpen(actor, true)`. Then `mp.set(actor, "private.discordRoles", …)` and `"private.indexed.discordId"` | [src: ts/systems/spawn.ts:293-332] |
| 8 | S | `PartOne::SetUserActor`: throws if the actor is disabled. It clears any hoster entry for the actor, runs `UnsubscribeFromAll` + `RemoveFromGridAndUnsubscribeAll`, `actorsMap.Set`, then `ForceSubscriptionsUpdate` (→ `CreateActor isMe=true` to the user, `CreateActor` of each neighbour to the user, and `CreateActor` of the user to each neighbour). A dead actor gets `RespawnWithDelay`. The last anim event is reset | [src: PartOne.cpp:175-221] |
| 9 | C | `CreateActor isMe`: sets `playerCharacterFormIdx/RefrId`. If in game (`update` fires first), `moveRefrToPosition` loops until within 256 u. If in the main menu (only `tick` fires), `LoadGameService.loadGame(pos, rot, worldOrCell, ChangeFormNpc(appearance), loadOrder, time)` loads a template save. Inventory and equipment are applied after 1 s and 1.3 s; then appearance, base AVs and percentages, learned spells, death state; `isRaceMenuOpen` → `Game.showRaceMenu()` after 0.3 s | [src: remoteServer.ts:282-619; loadGameService.ts:114-124] |
| 10 | C→S | When the race menu closes → `UpdateAppearance` (reliable) → accepted only while `isRaceMenuOpen` (§2 Appearance) | [src: sendInputsService.ts:236-259; ActionListener.cpp:209-228] |
| 11 | S | Disconnect (`ID_DISCONNECTION_NOTIFICATION`/`CONNECTION_LOST`) → `disconnectingUserId = id` → listener `OnDisconnect` → `Spawn.disconnect`: `setEnabled(actor, false)` → `MpActor::Disable`. This releases container/furniture occupancy, removes the actor from the grid (`DestroyActor` to neighbours, but not to the leaving user) and resolves pending snippet promises with `None`. Then `animationSystem.ClearInfo` and `serverState.Disconnect` (erases the `actorsMap` entry) | [src: PartOne.cpp:452-467; spawn.ts:334-339; MpActor.cpp:392-417; PartOne.cpp:881-896] |
| 12 | C | On `disconnect`/`connectionFailed`/`connectionDenied` the client reconnects automatically. `SinglePlayerService` closes networking if a non-SP save is loaded | [src: networkingService.ts:72-86; singlePlayerService.ts] |

### 1.4 User ↔ actor binding

| Item | Detail | Evidence |
|---|---|---|
| Map | `ActorsMap`: `vector<MpActor*> actorByUserId(kMaxPlayers)` plus `unordered_map<MpActor*, UserId>`. One actor per user and one user per actor | [src: server_guest_lib/ActorsMap.h:40-62; ActorsMap.cpp:9] |
| API | `PartOne::SetUserActor(userId, formId \| 0)`, `GetUserActor`, `GetUserByActor`. JS: `mp.setUserActor/getUserActor/getUserByActor` | [src: PartOne.cpp:175-249; ScampServer.cpp:538-559,764-773] |
| Profile | `ChangeForm.profileId` is indexed in `WorldState::actorIdByProfileId` (`RegisterProfileId`); JS `getActorsByProfileId` | [src: MpObjectReference.cpp:910-935; WorldState.h:236] |
| Player NPC guard | When the save storage loads, every form with `profileId != -1` is forced to `isDisabled=true` ("do not let players become NPCs"). The login flow enables it again | [src: PartOne.cpp:341-345; spawn.ts:301] |
| Player-created test | `IsCreatedAsPlayer()` = `formId >= 0xff000000 && baseId <= 0x7`. It gates SpSnippet execution and spawn-point logic | [src: MpActor.cpp:968-971; SpSnippet.cpp:27-32; MpActor.cpp:1456-1466] |
| Send to the right user | `MpActor::SendToUser` → `FormCallbacks.sendToUser` → `UserByActor`. Messages are skipped for `disconnectingUserId`. `GetActorToSendTo()` returns the hoster's actor when this actor has no user | [src: PartOne.cpp:668-681; MpActor.cpp:419-472] |

### 1.5 Visibility and subscription (grid)

| Mechanism | Detail | Evidence |
|---|---|---|
| Grid | One `GridImpl<MpObjectReference*>` per world/cell form id (`WorldState::grids[cellOrWorld]`). Cell coordinates are `int16(pos.x/4096), int16(pos.y/4096)`, applied to interiors too. `Move` inserts the object into the 9 neighbour sets around its cell (`DSLine<DSLine<std::set>>`), so `GetNeighboursByPosition(x,y)` returns everything within ±1 cell | [src: Grid.h:13-106; MpObjectReference.cpp:151-154; WorldState.h:289-298] |
| Lazy world loading | `WorldState::GetNeighborsByPosition(cellOrWorld, x, y)` loads ESM references for the 3×3 chunks on first touch, for every plugin (`GetRecordsAtPos` → `LoadForm` → `AttachEspmRecord` → apply deferred ChangeForm → `ForceSubscriptionsUpdate`). `loadedChunks[x][y]` caches this | [src: WorldState.cpp:840-877, 661-699] |
| `ForceSubscriptionsUpdate` | Moves the object in the grid, then diffs current `listeners` against the grid neighbours. For removed ones: `Unsubscribe(this,l)` + `Unsubscribe(l,this)`. For added ones: `Subscribe` both ways. Self-subscription is kept | [src: MpObjectReference.cpp:683-724] |
| Callers | `SetPos` when the grid cell changes or on first call; `SetCellOrWorld`; `Enable`; `SetUserActor`; `LoadForm`; `mp.place`; Papyrus `PlaceAtMe`; Papyrus `SetPosition` | [src: MpObjectReference.cpp:544-545,671-675,521-533; PartOne.cpp:206; WorldState.cpp:695; ScampServer.cpp:1105; PapyrusObjectReference.cpp:421,581] |
| `Subscribe` rules | Only if the emitter or the listener is an actor. Emitters with a **primitive** (trigger volume) get no `CreateActor`; they are tracked in `emittersWithPrimitives` for `OnTriggerEnter`/`Leave`/`OnTrigger`. The first subscription by a player (`profileId != -1`) fires Papyrus `OnInit`, `OnCellLoad` and `OnLoad` on the emitter | [src: MpObjectReference.cpp:977-1018] |
| Emission | Subscribe → `onSubscribe` → **`CreateActor`** (reliable) to the listener's user. Unsubscribe → `onUnsubscribe` → **`DestroyActor{idx}`** (reliable), not sent to the disconnecting user | [src: PartOne.cpp:759-879, 881-896] |
| Disable/Delete | FF refs and actors: `Disable`/`Delete` → `RemoveFromGridAndUnsubscribeAll` (→ `DestroyActor`). ESM non-actor refs stay in the grid when disabled (the client is told via props/SpSnippet) | [src: MpObjectReference.cpp:507-533, 781-793, 1699-1713] |
| Client side | `CreateActor` with `refrId < 0xff000000` and `baseRecordType != "DOOR"` means an ESM static object: the client applies props to the existing vanilla reference and creates no FormView. Everything else becomes a `FormModel` in `worldModel.forms[IdManager.allocateIdFor(idx)]` | [src: remoteServer.ts:282-345, 915-920] |
| Client stream-out | `DestroyActor` → frees the slot and destroys the FormView (deletes the FF clone). If it is the player's own slot → quit to main menu | [src: remoteServer.ts:621-649] |
| World change | The client destroys all FormViews when the player's world/cell changes, and destroys any FormView whose `movement.worldOrCell` differs from the player's | [src: worldView.ts:71-85; formView.ts:40-77] |

**`CreateActorMessage` contents** [src: messages/CreateActorMessage.h:11-173; PartOne.cpp:759-879; MpObjectReference.cpp:372-438; MpActor.cpp:350-390; GetBaseActorValues.cpp:6-22]

| Field | Filled when | Audience filter |
|---|---|---|
| `idx`, `isMe`, `transform{worldOrCell,pos,rot}` | Always | – |
| `refrId` | Always (long id for ESM-id actors) | – |
| `baseId` | Base ≠ 0 and ≠ 0x7 (player base) | – |
| `baseRecordType` | `"DOOR"` only (client perf hack, issue #1186) | – |
| `appearance` | Actors with an appearance. `onActorStreamIn` hook can rewrite it (SweetPie hides names) | – |
| `equipment` | Actors | – |
| `animation` | Actor's last anim event (in memory only) | – |
| `isDead` | Dead actors (duplicated in `props.isDead`) | – |
| `props.isHarvested` / `props.isOpen` | When true | – |
| `props.inventory` | Non-empty inventory | **owner only** (`VisitPropertiesMode::All`) |
| `props.isDisabled` | ESM forms that are disabled | – (client reads the wrong key, see §3.2) |
| `props.lastAnimation`, `setNodeScale[]`, `setNodeTextureSet[]`, `displayName` | When set (`displayName` is skipped if it equals the `%original_name%` sentinel) | – |
| `props.isRaceMenuOpen` | Actor whose race menu is open | **owner only** |
| `props.health/magicka/stamina`, `*Rate`, `*RateMult`, `*Percentage` | Actors | **owner only** |
| `props.learnedSpells` | Actors | all listeners |
| `props.templateChain` | Actors with a template chain | all listeners |
| `props.isHostedByOther` | Emitter has a user, or a hoster ≠ this listener | – |
| `customPropsJsonDumps[]` | `dynamicFields` entries | dropped if `!isVisibleByOwner`, or if `!isVisibleByNeighbors` and not the owner |

### 1.6 Ownership and NPC hosting

| Aspect | Detail | Evidence |
|---|---|---|
| State | `WorldState::hosters`: NPC formId → hoster actor formId. `lastMovUpdateByIdx[idx]` = time of the last accepted `UpdateMovement` for that idx | [src: WorldState.h:241,244-245; ActionListener.cpp:177-182] |
| C: when to try | `FormView.applyAll`: if the model is not `isHostedByOther` and has already been applied once, `tryHostIfNeed`. Also every ≥1 s when no movement has been seen for >1.5 s. Max one attempt per NPC per second, and only if it is in the same world/cell as the player. Attempts are queued (`hostAttempts`) and drained **one per frame** as `Host{remoteId}` (unreliable) | [src: formView.ts:403-473, 648-662; view/hostAttempts.ts:5-15; sendInputsService.ts:287-300] |
| S: grant rule | `OnHostAttempt`: requires the sender to have an actor. Target must exist. Ignored if the target is a user's actor. Granted if there is no hoster, or no movement for the target within **2 s**. Effects: `hosters[id] = me`; `UpdateHoster` (UpdateProperty `isHostedByOther`: true for others, false for the new hoster); `lastMovUpdate = now` (anti-flap); NPC `EquipBestWeapon` (broadcasts `UpdateEquipment`); `HostStart{target}` reliable to the requester; **after 1 s** `ChangeValues{health,magicka,stamina}` to the new hoster; `HostStop` to the previous hoster | [src: ActionListener.cpp:653-738; MpObjectReference.cpp:733-744; MpActor.cpp:140-192] |
| C: hosting | `HostStart`/`HostStop` maintain `storage['hosted']` (remote ids). `sendInputs` loops `[player, ...hosted]` and sends movement and animation for each (appearance and equipment only for the player). The hosted NPC's own anims are allowed and `keepOffsetFromActor` is cleared, so local AI drives it | [src: remoteServer.ts:129-155; sendInputsService.ts:93-111,236-285; formView.ts:387-401] |
| Revocation | (a) The previous hoster gets `HostStop` when someone else takes over. (b) `SendToNeighbours` sends `HostStop` to a sender that is not the hoster, and erases `hosters[id]` if the target is a player actor. (c) `SetUserActor` erases the hoster entry of the actor it binds. There is **no** explicit release on stream-out or disconnect; entries go stale and the 2 s rule lets others take over | [src: ActionListener.cpp:58-86; PartOne.cpp:193-196; inference] |
| Messages a hoster may send *for* a hosted NPC | `UpdateMovement` (NPC idx), `UpdateAnimation` (NPC idx; relayed, but **not** stored or processed by `AnimationSystem`), `Activate` (caster = NPC; afterwards the server calls `EquipBestWeapon` on it), `OnHit` (aggressor = NPC), `SpellCast` (caster = NPC) | [src: ActionListener.cpp:39-96,186-207,452-484,1023-1033,1142-1154] |
| Not accepted for NPCs | `ChangeValues` (idx ignored; always the sender's own actor), `UpdateEquipment`, `UpdateAppearance`, `UpdateAnimVariables`, `PutItem`/`TakeItem`/`DropItem`/`CraftItem`/`PlayerBowShot`/`OnEquip` (player actor only) | [src: ActionListener.cpp:233,212,1117-1122,768,490,520,550,576,622; CraftService.cpp:41] |
| Server→hoster routing | Messages addressed to an NPC (`ChangeValues`, `Teleport`, `DeathStateContainer`, owner-only custom properties, `UpdateEquipment` from `EquipBestWeapon`) use `GetActorToSendTo()`, which resolves to the hoster when the NPC has no user | [src: MpActor.cpp:449-472, 740, 1012, 1443; MpObjectReference.cpp:757-760] |
| SpSnippet and NPCs | `SpSnippet::Execute` does nothing (returns a never-resolving promise) unless the executor `IsCreatedAsPlayer()`. Papyrus `SetActorValue` explicitly redirects to the hoster | [src: SpSnippet.cpp:27-32; PapyrusActor.cpp:96-107] |
| Trust | The server does **not** validate hosted-NPC movement (snap-back only for `isMe`) or NPC AI state. The hoster is trusted for NPC transform and animation | [src: MovementValidation.cpp:24-33] |


### 1.7 Per-message reliability, direction, rate and handler (all 33 MsgTypes)

Ids come from `[src: falloutmp-server/cpp/messages/MsgType.h]`; the client mirror is `skymp5-client/src/services/messages.ts`.

Column meanings:
- **Rel.**: `R` = reliable, `U` = unreliable. The client sends `R` as RakNet `RELIABLE`; the server sends `R` as `RELIABLE_ORDERED` [src: Networking.cpp:91,181].
- **Dir**: direction of the message.
- **Rate**: how often it is sent.
- **Server action**: the C→S handler in `ActionListener.cpp`, or where the S→C message is produced.

| Id | Type | Dir | Rel. | Rate | Server action / producer | Client sender / receiver |
|---|---|---|---|---|---|---|
| 1 | CustomPacket | both | R | event | `OnCustomPacket` 106-114 → JS `customPacket`; S→C via `PartOne::SendCustomPacket` (PartOne.cpp:282) / `mp.sendCustomPacket` | authService.ts:607-651 (login); networkingService route `customPacket` |
| 2 | UpdateMovement | both | U | C: every **130 ms** per controlled actor (player + hosted), plus immediately on change of some flags [inference] | `OnUpdateMovement` 116-184; raw relay to neighbours (incl. echo) | sendInputsService.ts:113-135; remoteServer.ts:651-670 |
| 3 | UpdateAnimation | both | U | on each anim event that passes the filter | `OnUpdateAnimation` 186-207; raw relay | sendInputsService.ts:198-234; animation.ts:212-285 |
| 4 | UpdateAppearance | both | R | once, when RaceMenu closes; S: on accept or on `mp.set(appearance)` (deferred ch. 2) | `OnUpdateAppearance` 209-228 | sendInputsService.ts:236-259; remoteServer.ts:671-690 |
| 5 | UpdateEquipment | both | R | on change (diff, `numChanges`) | `OnUpdateEquipment` 230-440 | sendInputsService.ts:261-285; remoteServer.ts:691-726 |
| 6 | Activate | C→S | R | event | `OnActivate` 442-485 | activationService.ts:18-71 |
| 7 | UpdateProperty | S→C | R | on property change | `CreatePropertyMessage_`/`PreparePropertyMessage_` (MpObjectReference.cpp:44-76); C→S ignored (PacketParser.cpp:150-152) | remoteServer.ts:728-755 |
| 8 | PutItem | C→S | R | event (container menu diff) | `OnPutItem` 487-515 | containersService.ts:19-99 (send at 70) |
| 9 | TakeItem | C→S | R | event | `OnTakeItem` 517-544 | containersService.ts |
| 10 | FinishSpSnippet | C→S | R | reply to snippet | `OnFinishSpSnippet` 604-617 → resolves promise | spSnippetService.ts:171-208 |
| 11 | OnEquip | C→S | **U** | on equip of consumables/books | `OnEquip` 619-630 → `MpActor::OnEquip` (MpActor.cpp:474-553) | sendInputsService.ts:54-75 |
| 12 | ConsoleCommand | C→S | R | event | `OnConsoleCommand` 632-644 → `ConsoleCommands::Execute` | consoleCommandsService.ts:65-93 |
| 13 | CraftItem | C→S | R | event | `OnCraftItem` 646-651 → `CraftService::OnCraftItem` | craftService.ts:18-70 (send at 61) |
| 14 | Host | C→S | U | ≤1 per frame, ≤1/s per NPC | `OnHostAttempt` 653-738 | sendInputsService.ts:287-300 |
| 15 | CustomEvent | C→S | R | gamemode-driven | `OnCustomEvent` 740-763 (name must start `_`) | gamemodeEventSourceService.ts:32-127 (`ctx.sendEvent`) |
| 16 | ChangeValues | both | R | C: on AV change (≥ threshold) [inference]; S: corrections, host start, respawn | `OnChangeValues` 765-825; S→C `NetSendChangeValues` (MpActor.cpp:695-750) | sendInputsService.ts:137-196; remoteServer.ts:821-843 |
| 17 | OnHit | C→S | R | per hit event | `OnHit` 1006-1112 | hitService.ts:15-69 |
| 18 | DeathStateContainer | S→C | R | on death/respawn | `SendAndSetDeathState` (MpActor.cpp:1003-1059) → `GetActorToSendTo` | remoteServer.ts:757-811 |
| 19 | DropItem | C→S | R | event | `OnDropItem` 546-571 | dropItemService.ts:15-87 (send at 80) |
| 20 | Teleport | S→C | R | on `MoveTo`, respawn, `locationalData` set | `MpActor::Teleport` (MpActor.cpp:1436-1448) | remoteServer.ts:245-280 |
| 21 | OpenContainer | S→C | R | on accepted container activation | `ProcessActivateNormal` CONT branch (MpObjectReference.cpp:1512+) | remoteServer.ts:176-243 |
| 22 | PlayerBowShot | C→S | **U** | per shot | `OnPlayerBowShot` 573-602 | playerBowShotService.ts:52-63 |
| 23 | SpellCast | both | U | per cast | `OnSpellCast` 1125-1204; unreliable relay (1177) | magicSyncService.ts:30-92; remoteServer.ts:941-967 |
| 24 | UpdateAnimVariables | both | U | with spell casts / periodic for casters [inference] | `OnUpdateAnimVariables` 1114-1123; unreliable relay | magicSyncService.ts; remoteServer.ts:969-990 |
| 25 | DestroyActor | S→C | R | on unsubscribe | `onUnsubscribe` (PartOne.cpp:881-896) | remoteServer.ts:621-649 |
| 26 | HostStart | S→C | R | on host grant | ActionListener.cpp:699-705 | remoteServer.ts:129-142 |
| 27 | HostStop | S→C | R | on takeover / bad sender | `PartOne::SendHostStop` (PartOne.cpp:639-652) | remoteServer.ts:143-155 |
| 28 | SetInventory | S→C | R | deferred channel 0 (overwrite; one per tick per actor) | `SendInventoryUpdate` (MpObjectReference.cpp:1854-1863) | remoteServer.ts:157-174 |
| 29 | SetRaceMenuOpen | S→C | R | on `setRaceMenuOpen` | `PartOne::SetRaceMenuOpen` (PartOne.cpp:259-280) | remoteServer.ts:845-860 |
| 30 | SpSnippet | S→C | R | deferred channel 1 (append) | `SpSnippet::Execute` (SpSnippet.cpp:23-61) | spSnippetService.ts:19-147 |
| 31 | Teleport2 | S→C | R | movement correction | `MovementValidation::Validate` (MovementValidation.cpp:11-33) | remoteServer.ts (teleport handler) |
| 32 | UpdateGamemodeData | S→C | R | on connect (cached) + on gamemode reload if broadcast enabled | `NotifyGamemodeApiStateChanged` (PartOne.cpp:503-564), `AddUser` (899-913) | gamemodeUpdateService.ts:91-265 |
| 33 | CreateActor | S→C | R | on subscribe | `onSubscribe` (PartOne.cpp:759-879) | remoteServer.ts:282-619 |
| 0 | Invalid | – | – | – | never sent | – |

Message field lists, in `Serialize` order, are in Appendix A.

### 1.8 Send primitives and audiences (server)

| Primitive | Audience | Reliability | Evidence |
|---|---|---|---|
| `ActionListener::SendToNeighbours(idx, rawMsg, reliable)` | All actor listeners of the ref that have a user, **including the sender** (echo); the bytes are relayed as-is | caller-chosen | [src: ActionListener.cpp:39-96] |
| `MpObjectReference::SendMessageToActorListeners(msg, reliable)` | Every listener that is an actor with a user | **always reliable** (the parameter is ignored) | [src: MpObjectReference.cpp:2028-2034] |
| `CreatePropertyMessage_(self, name, valueDump)` + `SendMessageToActorListeners` | All actor listeners (e.g. `SetHarvested`/`SetOpen`) | R | [src: MpObjectReference.cpp:44-76, 609-630] |
| `CreatePropertyMessage_` + `actor->SendToUser` / `GetActorToSendTo` | One actor (owner-only custom props, `isHostedByOther` in `UpdateHoster`) | R | [src: MpObjectReference.cpp:733-763] |
| `MpActor::SendToUser(msg, reliable)` | Owner of this actor (if online) | caller | [src: MpActor.cpp:419-447] |
| `MpActor::SendToUserDeferred(..., channel, overwrite)` | Owner; flushed in `TickDeferredMessages` | R | [src: PartOne.cpp:688-735, 1008-1035] |
| `GetActorToSendTo()->SendToUser` | Owner, or hoster if NPC | R | [src: MpActor.cpp:449-472] |
| `PartOne::SendCustomPacket(userId, json)` | One user | R | [src: PartOne.cpp:282-288] |
| `UpdateGamemodeData` broadcast | All connected users | R | [src: PartOne.cpp:540-563] |

Deferred channels [src: PartOne.cpp:654-735; MpObjectReference.cpp:1854-1863; SpSnippet.cpp:57-58; PropertyBindings/AppearanceBinding]:

| Channel | Use | Mode |
|---|---|---|
| 0 | `SetInventory` | overwrite (last wins per tick) |
| 1 | `SpSnippet` | append (order preserved) |
| 2 | `UpdateAppearance` from `mp.set(..., "appearance")` | append |

### 1.9 Server validation and correction patterns

| Pattern | Used by | On reject | Evidence |
|---|---|---|---|
| **Relay-then-validate** (raw bytes forwarded before the checks) | UpdateMovement | Teleport2 to the sender only; neighbours already received the bad position | [src: ActionListener.cpp:119 vs 136-144] |
| **Validate-then-relay** | UpdateAppearance, UpdateEquipment | Equipment: `SendInventoryUpdate` + SpSnippet `UnequipItem`/`RemoveSpell`; appearance: silently ignored | [src: ActionListener.cpp:209-228, 230-440] |
| **Validate-then-mutate-then-notify** (server computes the new state, sends `SetInventory`/`UpdateProperty`) | Put/Take/Drop/Craft/Activate/BowShot/Console | Mostly an exception that is logged; **no correction** for put/take/drop | [src: ActionListener.cpp:487-651] |
| **Crop** (accept but clamp) | ChangeValues | `ChangeValues` with the cropped values to the sender | [src: ActionListener.cpp:765-825; CropRegeneration.cpp:21-112] |
| **Ownership gate** | Everything with an idx/caster/aggressor | Throw (logged) or `HostStop` | [src: ActionListener.cpp:58-86, 452-466, 1020-1033] |
| **Gamemode veto** (`GameModeEvent::Fire` → false) | activate, put/take, drop, eat, read book, craft, death | Default action skipped; some send a correction (`onReadBook` → RemoveSpell) | [src: gamemode_events/*.cpp] |
| **Exceptions as rejection** | All handlers | `PartOne::HandlePacket` catches, logs `spdlog::error`, continues; no response to the client | [src: PartOne.cpp:468-473] |
| **Periodic client reconcile** | Inventory | The client re-applies the server inventory every 5 s (`pcInv`) | [src: remoteServer.ts:67-91] |

### 1.10 Persistence

**Pipeline** [src: ChangeFormGuard.h:27-35; WorldState.cpp:290-308, 701-790; AsyncSaveStorage.cpp; DatabaseFactory.cpp:16-56]

1. A mutator calls `EditChangeForm(lambda, mode)` → `changeForm` is mutated → `RequestSave()` unless `blockSaving` or `NoRequestSave`.
2. `WorldState::RequestSave(ref)` copies the **entire** ChangeForm into `changesByIdx[idx]` (coalesces per tick).
3. `WorldState::TickSaveStorage` → `saveStorage->Upsert(changeForms)` when `!changesByIdx.empty()` and the previous upsert finished. `AsyncSaveStorage` runs the DB on a worker thread; callbacks run on the main thread.
4. DB backends (setting `databaseDriver`): `file` (default, `world/changeForms/<hex>_<Plugin.esm>.json`, pretty-printed, one file per form), `mongodb` (`databaseUri`, `databaseName`), `migration` (`databaseOld` → `databaseNew`), `zip`.
5. Load: `PartOne::AttachSaveStorage` iterates all change forms → `WorldState::LoadChangeForm`. Profile actors are forced disabled. Deleted forms and FF items are skipped. ESM forms are deferred until their chunk loads (`LoadForm` → `ApplyChangeForm`). FF forms are created immediately (actor or object).
6. A form whose JSON fails to parse is **skipped entirely** (logged) [src: FileDatabase.cpp:119-124]. An invalid appearance or race also skips the form [src: WorldState.cpp:182-210].

**FormDesc** [src: mp_common/FormDesc.cpp]

| Function | Behaviour |
|---|---|
| `ToString(delim=':')` | `"<hex shortId>:<file>"`; FF forms have an empty file → `"ff000123"` |
| `FromString` | Inverse |
| `ToFormId(files)` | `fileIdx << 24 \| shortId`. FF forms → `0xff000000 + shortId`. Special case `0x3c` (Tamriel) [src: 46-74] |
| `FromFormId(id, files)` | Maps the high byte to the file name [src: 76-99] |
| Effect | Saves survive load-order changes; the server load order comes from settings `loadOrder` [src: ScampServer.cpp:315-321] |

**MpChangeFormREFR fields** [src: MpChangeForms.h:57-150; MpChangeForms.cpp:23-384]

| Field | Type / default | Written by (mutator) | Saved | Restored by (`ApplyChangeForm` / load) | Sent to clients as |
|---|---|---|---|---|---|
| `recType` | REFR/ACHR | creation | yes | picks MpActor vs MpObjectReference in `LoadChangeForm` | – |
| `formDesc` | FormDesc | creation | yes (also file name) | form id | `refrId` |
| `baseDesc` | FormDesc | creation | yes | base id | `baseId` |
| `position`, `angle` | NiPoint3 | `SetPos`/`SetAngle` (movement: only if `IsLocationSavingNeeded`, i.e. ≥30 s since the last save; `SetPosAndAngleSilent` doesn't save) | yes | `SetPosAndAngleSilent` | `transform` / `UpdateMovement` |
| `worldOrCellDesc` | FormDesc | `SetCellOrWorld` | yes | grid insert | `transform.worldOrCell` |
| `inv` | Inventory | `SetInventory`/`AddItem`/`RemoveItem(s)` | yes | `SetInventory` silently | `props.inventory` (owner), `SetInventory` |
| `learnedSpells` | LearnedSpells | `AddSpell`/`RemoveSpell` | yes | – | `props.learnedSpells` |
| `isHarvested` | bool | `SetHarvested` | yes | flag | `props.isHarvested`, `UpdateProperty` |
| `isOpen` | bool | `SetOpen` | yes | flag | `props.isOpen`, `UpdateProperty` |
| `baseContainerAdded` | bool | `EnsureBaseContainerAdded` | yes | prevents double-adding CONT items | – |
| `nextRelootDatetime` | uint64 ms | `RequestReloot` | yes | re-arms the timer (or 1 ms if past) | – |
| `isDisabled` | bool | `Enable`/`Disable` | yes | removes from grid | `props.isDisabled` (ESM) / Create/Destroy |
| `profileId` | int32, -1 | `RegisterProfileId` | yes | index `actorIdByProfileId` | – |
| `isDeleted` | bool | `Delete` (FF only) | yes | load skip | – |
| `count` | uint32 | (dropped items) | yes | – | – |
| `isRaceMenuOpen` | bool | `SetRaceMenuOpen` | yes | – | `props.isRaceMenuOpen` (owner), `SetRaceMenuOpen` |
| `isDead` | bool | `SetIsDead` | yes | `RespawnWithDelay` on load if true | `isDead`, `DeathStateContainer`, `UpdateProperty` |
| `activeMagicEffects` | list | `ApplyMagicEffect` | yes (JSON) but **not in `ToTuple`** | `ReapplyMagicEffects` (MpActor.cpp:1960) | – |
| `consoleCommandsAllowed` | bool | `mp.set` | yes | gate in `EnsureAdmin` | – |
| `appearanceDump` | JSON string | `SetAppearance` | yes | parsed on each `GetAppearance` | `appearance` |
| `equipment` | JSON | `SetEquipment` | yes | – | `equipment` |
| `actorValues` | percentages + values | `SetPercentage`/`NetSetPercentages`/`SetActorValue` | yes | **overwritten to 1.0** when ESM data is attached (MpActor.cpp:603-608) | `props.*`, `ChangeValues` |
| `healthRespawnPercentage`, `magickaRespawnPercentage`, `staminaRespawnPercentage` | float 1.0 | `mp.set(respawnPercentages)` | yes | used by `Respawn` | – |
| `spawnPoint` | LocationalData {133857,-61130,14662}, rot z 72, Tamriel | `mp.set(spawnPoint)` / `SetSpawnPoint` | yes | `GetSpawnPoint` (ESM NPC: editor location) | – |
| `spawnDelay` | float 25 s | `mp.set(spawnDelay)` | yes | `RespawnWithDelay` | – |
| `templateChain` | vector<FormDesc> | `EnsureTemplateChainEvaluated` | yes | leveled NPC identity | `props.templateChain` |
| `lastAnimation` | optional<string> | Papyrus `PlayAnimation` | **no** (ToJson line commented out, MpChangeForms.cpp:84-87) | – | `props.lastAnimation` (in-memory) |
| `setNodeTextureSet` | map | Papyrus `NetImmerse.SetNodeTextureSet` | yes | – | `props.setNodeTextureSet` |
| `setNodeScale` | map | Papyrus `NetImmerse.SetNodeScale` | yes | – | `props.setNodeScale` |
| `displayName` | optional<string> | Papyrus `SetDisplayName` | yes | – | `props.displayName` |
| `factions` | optional<vector> | Papyrus `AddToFaction` etc. | yes, **not in `ToTuple`** | – | – |
| `quests` | optional | – | **never written by ToJson** | – | – |
| `dynamicFields` | DynamicFields (JSON) | `mp.set(custom prop)` / `SetPropertyValueDump` | yes | – | `customPropsJsonDumps`, `UpdateProperty` |

`ToTuple` is used for equality in tests and save diffing; it omits `activeMagicEffects`, `factions` and `quests` [src: MpChangeForms.h:135-146].

### 1.11 Property system

| Aspect | Detail | Evidence |
|---|---|---|
| JS API | `mp.get(formId, name)`, `mp.set(formId, name, value)`, `mp.makeProperty(name, {isVisibleByOwner, isVisibleByNeighbors, updateOwner, updateNeighbor})`, `mp.findFormsByPropertyValue` | [src: ScampServer.cpp:925-971, 997-1070, 1462] |
| Built-ins | 25 `PropertyBinding`s registered in `PropertyBindingFactory.cpp:31-77` (table below) | [src] |
| Custom props | Any name not built-in → `CustomPropertyBinding`: stored in `ChangeForm.dynamicFields`. `Set` → `SetPropertyValueDump` → `UpdateProperty` to the owner and/or neighbours according to the `makeProperty` flags | [src: CustomPropertyBinding.cpp:38-80; MpObjectReference.cpp:746-763] |
| Prefixes | `private.*`: never sent. `private.indexed.*`: also indexed for `findFormsByPropertyValue` | [src: CustomPropertyBinding.cpp; MpObjectReference.cpp] |
| Client application | `updateOwner`/`updateNeighbor` JS bodies are shipped in `UpdateGamemodeData` (signed, `// skymp:sig:y:<serverKey>:<sig>`); the client runs them every frame in `GamemodeUpdateService` with `ctx.value`, `ctx.refr`, `ctx.get` | [src: PartOne.cpp:579-594; gamemodeUpdateService.ts:178-265; serverJsVerificationService.ts] |

Built-in bindings [src: addon/property_bindings/*; PropertyBindingFactory.cpp:31-77]:

| Property | Get | Set | Note |
|---|---|---|---|
| `actorNeighbors`, `neighbors` | yes | – | Grid listeners |
| `angle`, `pos` | yes | throws | Use `locationalData` [docs say settable: mismatch] |
| `worldOrCellDesc`, `baseDesc`, `type`, `idx`, `templateChain`, `lastAnimEvent`, `isOnline`, `onlinePlayers`, `equipment` | yes | – | |
| `appearance` | yes | yes | Deferred UpdateAppearance ch. 2, **no validation** |
| `inventory` | yes | yes | → SetInventory |
| `isDead` | yes | yes | → `SetIsDead` / kill / respawn |
| `isDisabled` | yes | yes | Enable/Disable |
| `isOpen` | yes | yes | |
| `locationalData` | yes | yes | Actors only → `Teleport` |
| `percentages` | yes | yes | Clamped [0,1]; inside `onDeath` handled specially |
| `profileId`, `spawnPoint`, `consoleCommandsAllowed`, `spawnDelay`, `respawnPercentages` | yes | yes | Persisted fields |

### 1.12 Gamemode events

| Event (JS name) | Args | Fired from | If not blocked (`OnFireSuccess`) | If blocked |
|---|---|---|---|---|
| `onActivate` | `[refrId, casterRefrId]` | `MpObjectReference::Activate` | `ProcessActivate…` | nothing happens (no correction) |
| `onPutItem` | `[target, actor, baseId, count]` | `OnPutItem` | `PutItem` | no correction |
| `onTakeItem` | `[sourceRefr, actor, baseId, count]` (extra data TODO) | `OnTakeItem` | `TakeItem` | no correction |
| `onDropItem` | `[refrId, baseId, count]` | `MpActor::DropItem` | spawn the dropped ref | no correction |
| `onEatItem` | `[actor, baseId]` | `MpActor::OnEquip` (ALCH/INGR) | ALCH effects applied; INGR effects commented out | **item still removed** [src: MpActor.cpp:474-553] |
| `onReadBook` | `[actor, baseId]` | `MpActor::OnEquip` (BOOK with spell) | learn the spell | SpSnippet `RemoveSpell` |
| `onCraft` | `[actor, item, count, recipe]` | `CraftService::UseCraftRecipe` | items swapped | nothing |
| `onDeath` | `[actor, killer]` | `MpActor::Kill`/`SetIsDead` | `RespawnWithDelay` | stays dead without auto-respawn |
| `onRespawn` | `[actor]` | `MpActor::Respawn` | teleport + revive | – |
| `onUpdateAppearanceAttempt` | `[actor, appearanceJson, isAllowed]` | `OnUpdateAppearance` | – | not blockable (warning only) |
| `onUpdateEquipmentAttempt` | `[actor, equipmentJson, isAllowed]` | `OnUpdateEquipment` | – | not blockable (warning only) |
| `onPapyrusEvent:<Name>` | `[selfFormId, ...args]` | `MpForm::SendPapyrusEvent` (MpForm.cpp:34-40) | Papyrus VM runs the event | Papyrus event suppressed |
| `_<customName>` | client args | `OnCustomEvent` | calls `mp[_name]` listeners directly | – |
| emitter `connect` / `disconnect` / `customPacket` | `userId`(, json) | ScampServerListener.cpp:10-39 | – | – |
| `mp.onLoginAttempt`, `mp.onHttpRpcRunAttempt` | profileId / rpc | login.ts:129-135; ui.ts:52-61 | – | false rejects |

Mechanics [src: GameModeEvent.cpp:7-45; ScampServerListener.cpp:41-152]: `Fire(worldState)` pushes onto `WorldState` event stack (reentrancy detection), calls `PartOne::Listener::OnMpApiEvent(event)` → JS handler `mp[name](...args)`. A result of `false` blocks; `undefined` or an exception allows.

### 1.13 Server-side Papyrus

| Item | Detail | Evidence |
|---|---|---|
| VM | Own C++ Papyrus VM (`papyrus_vm_lib`) executes `.pex` on the server | [src: WorldState.cpp:1002-1045] |
| Script sources | `dataDir/scripts`, BSA archives (`archives` setting), 133 embedded `standard_scripts` (stubs for natives) | [src: ScriptStorageFactory] |
| Attach | `MpObjectReference::InitScripts` reads VMAD from the base and the ref, filtered by the "Sweet" whitelist and a blacklist; `disableVanillaScriptsInExterior` option | [src: MpObjectReference.cpp:1722-1837; WorldState.h:254] |
| Natives | 22 classes registered in `PapyrusClassesFactory.cpp:26-61` (Actor, ObjectReference, Game, Debug, Form, Utility, Message, Sound, NetImmerse, Faction, Quest, Cell, Keyword, LeveledItem, …) | [src] |
| Events fired | OnActivate, SkympOnActivateClose, OnObjectEquipped, OnItemAdded (AddItem path only), OnInit/OnCellLoad/OnLoad (first player subscribe), OnUpdate (`RegisterForSingleUpdate`), OnTriggerEnter/Leave/OnTrigger, OnSpellCast, OnHit (`akProjectile` = None) | [src: MpObjectReference.cpp:481,502,575,595,838,989-991; MpActor.cpp:550; MpForm.cpp:31; ActionListener.cpp:1189,1410-1425] |
| Default actor | `HeuristicPolicy` chooses which actor a native like `Game.GetPlayer()` returns: OnActivate → arg0; OnObjectEquipped/OnInit/OnUpdate → self; triggers → arg0; OnHit → aggressor | [src: HeuristicPolicy.cpp] |
| JS ↔ Papyrus | `mp.callPapyrusFunction(callType, class, fn, self, args)`, `mp.registerPapyrusFunction(callType, class, fn, jsFn)` | [src: ScampServer.cpp:1308, 1356] |

**SpSnippet round trip** [src: SpSnippet.cpp:23-61; IPapyrusClass.cpp; ActionListener.cpp:604-617; spSnippetService.ts:19-208]

```
Papyrus native (e.g. PapyrusDebug::Notification) on server
 → SpSnippet(class, fn, SerializeArguments(args), selfId).Execute(actor, mode)
   - if !actor->IsCreatedAsPlayer(): return never-resolving promise   (SpSnippet.cpp:27-32)
   - snippetIdx = actor->pendingSnippets.size() (promise stored)
   - actor->SendToUserDeferred(SpSnippetMessage, channel 1, append)     (57-58)
 → [C] SpSnippetService: queue → on update: resolve selfId (0x14 → player; long id → FF clone),
       arguments {formId,type} → Game.getFormEx → cast; call sp[class][fn](...)
       (await if Promise) → if snippetIdx>=0 send FinishSpSnippet{returnValue, snippetIdx} (reliable)
 → [S] OnFinishSpSnippet → actor->ResolveSnippet(idx, VarValue)  → Papyrus await resumes
   (on disconnect/disable pending snippets resolve to None)
```

Modes: `NoReturnResult` (fire and forget; idx −1) vs `ReturnResult` (Papyrus `Latent` natives such as `GetSitState`, `GetCameraState`, `PlayAnimationAndWait` with a 15 s timeout).

Mirrored natives (server effect + SpSnippet to the owner or listeners): Actor `DrawWeapon`, `UnequipAll`, `PlayIdle`, `SetAlpha` (listeners), `EquipItem/Ex`, `EquipSpell`, `UnequipItem`, `SetDontMove`, `AddSpell/RemoveSpell`, `SetActorValue` (to the hoster); Debug `Notification`, `MessageBox`, `SendAnimationEvent`; Game `Force/Disable/EnablePlayerControls`, `ShowRaceMenu`; `Message.Show`; `Sound.Play`; `EffectShader/VisualEffect.Play/Stop`; ObjectReference `Enable/Disable` (ESM refs), `PlayAnimation(AndWait)`, `PlayGamebryoAnimation`, `AddItem/RemoveItem` notifications. [src: script_classes/*.cpp]

### 1.14 Hot reload

| What | How | Evidence |
|---|---|---|
| Gamemode JS | `index.ts` watches `gamemodePath`; on change it clears `mp` handlers, re-evaluates, then `NotifyGamemodeApiStateChanged` (re-sends `UpdateGamemodeData` if `enableGamemodeDataUpdatesBroadcast`) | [src: ts/index.ts:63-96,125-183; PartOne.cpp:503-564] |
| Client-side gamemode code | `GamemodeUpdateService` re-evaluates `updateOwner`/`updateNeighbor` and event sources after verifying signatures | [src: gamemodeUpdateService.ts:91-138] |
| Papyrus | `CreatePexScriptLazy` re-reads `.pex` files on next use (lazy cache) | [src: WorldState.cpp:966-1000] |
| C++ / ESM data | requires server restart [inference] | – |

### 1.15 Anti-cheat: every server-side validation

| # | Area | Check | Evidence |
|---|---|---|---|
| 1 | Transport | RakNet password = protocol version + server password | Config.h:5-12; ScampServer.cpp:344-347 |
| 2 | Transport | IP connection frequency limit; max players | Networking.cpp:148,168 |
| 3 | Transport | Packets from users without `Connect` ignored | PartOne.cpp:429-474 |
| 4 | Login | master session / guid unchanged / `onLoginAttempt` / Discord ban + IP | login.ts:102-228 |
| 5 | Ownership | idx must be own actor or hosted; otherwise HostStop | ActionListener.cpp:58-86 |
| 6 | Movement | world/cell change or jump ≥ 4096 u → Teleport2 | MovementValidation.cpp:11-33 |
| 7 | Appearance | Only while `isRaceMenuOpen` | ActionListener.cpp:214 |
| 8 | Equipment | Spells learned; items owned; BOD2 slot overlap | ActionListener.cpp:259-371 |
| 9 | Activation | Caster 0x14 or hosted; same worldspace; activation-parent-only refs; `activationBlocked`; gamemode veto; occupancy (CONT 512 u, FURN 256 u) | ActionListener.cpp:442-485; MpObjectReference.cpp:1419-1697 |
| 10 | Containers | Occupant must be the actor; `SweetCantDrop` keyword; atomic counts (`RemoveItems` on a copy) | ActionListener.cpp:487-544; MpObjectReference.cpp:632-658; Inventory.cpp:106-136 |
| 11 | Drop | keyword, Gold forbidden, 10-drop queue, 120 s lifetime, gamemode | MpActor.cpp:1588-1735 |
| 12 | Bow | ammo is AMMO and owned → remove 1 | ActionListener.cpp:573-602 |
| 13 | Craft | exact recipe inputs (temper excluded), COBJ conditions, bench keyword | CraftService.cpp:20-277 |
| 14 | Console | `consoleCommandsAllowed` (admin) | ConsoleCommands.cpp:58 |
| 15 | Host | no user-controlled targets; 2 s takeover | ActionListener.cpp:653-738 |
| 16 | Custom events | name prefix `_` | ActionListener.cpp:740-763 |
| 17 | AVs | regen crop by elapsed time × rate × mult (health, magicka only); 5 s potion window skip | ActionListener.cpp:765-825; CropRegeneration.cpp; MpActor.cpp:85-102 |
| 18 | Hits | aggressor own/hosted; same cell; ≤4096 u unless bow; dead aggressor → respawn; source equipped; weapon-speed cooldown (`CanHit`); splash 0.1 s / 4 targets; block angle < 1 rad | ActionListener.cpp:1006-1408 |
| 19 | Spells | spell equipped; caster alive | ActionListener.cpp:1125-1204 |
| 20 | OnEquip | item owned | MpActor.cpp:474-553 |
| 21 | Client-side | periodic inventory re-apply, signature verification of server JS, load-order CRC, SP version check, `blockActivation`, WorldCleaner, `setInChargen`, `BlockPapyrusEvents`, fast travel/difficulty/skill gain disabled | remoteServer.ts:67-91; serverJsVerificationService.ts; worldCleanerService.ts |
| 22 | Audit | packet history recording + playback | PartOne.cpp:596-637, 969-1006 |
| **Not checked** | | melee reach (commented out, 1315-1334), pickup/activation distance, power/sneak flags, stamina increases, hosted NPC movement, BitStream size bounds | [src] |

### 1.16 Time sync

| Side | Behaviour | Evidence |
|---|---|---|
| Client | `TimeService` sets globals GameHour (0x38), GameDay (0x37), GameMonth (0x36), GameYear (0x35), TimeScale (0x3a) every update from **UTC wall clock** (×timescale settings) | [src: timeService.ts:24-64] |
| Server | Papyrus `Utility.GetCurrentGameTime` = days since server start | [src: PapyrusUtility.cpp:69-79] |
| Weather | Not synced (client local) [inference: no message] | – |
| Gap | Server and client game time disagree (the client's is UTC-based) | [inference] |

### 1.17 Death and respawn

```
damage → MpActor::NetSetPercentages(health=0) or SetIsDead(true) or mp.set(isDead)
 → Kill(killer): SetIsDead(true) → EditChangeForm(isDead) → SendAndSetDeathState(true)
     DeathStateContainer{tIsDead UpdateProperty, …} → GetActorToSendTo (owner/hoster only)
     neighbours learn via owner's next UpdateMovement.isDead=true (and anim)
 → DeathEvent::Fire → onDeath(actor, killer) → success: RespawnWithDelay(spawnDelay=25 s)
 → timer → [NPC: if death item → reset inventory to base, keep SweetCantDrop]
 → Respawn(): RespawnEvent onRespawn → SetIsDead(false) → SendAndSetDeathState(false, teleport spawnPoint,
     ChangeValues = respawnPercentages) + UpdateProperty isDead=false to listeners
 Reconnect/LoadChangeForm while isDead → RespawnWithDelay
```
[src: MpActor.cpp:1003-1059, 1327-1466, 1526-1563, 614-618; PartOne.cpp:214-216]

Client: `DeathService` handles `DeathStateContainer` (kill with `Actor.kill` / ragdoll, then `resurrect`, teleport, AV restore) [src: deathService.ts:77-102; remoteServer.ts:757-811].

### 1.18 Reloot

| Trigger | Delay (default) | Evidence |
|---|---|---|
| Item pickup (ref disabled) | 1 h | MpObjectReference.cpp:289-311, 1447 |
| FLOR/TREE harvest | 1 h | same |
| Teleport door open | 3 s (auto-close) | 1467 |
| Container emptied | 1 h | 1512 |
| Other types | 0 (none) | 289-311 |

`RequestReloot` → `nextRelootDatetime` (persisted) + `WorldState::RequestReloot` timer → `DoReloot`: `SetOpen(false)`, `SetHarvested(false)`, `RelootContainer` (re-add base CONT items), re-enable picked items. Settings: `reloot: {TYPE: ms}`, `forbiddenReloot: [TYPE]`; ESM forms only. [src: MpObjectReference.cpp:1118-1152; WorldState.cpp:258-288; ScampServer.cpp:404-417]


---

## 2. Feature table, end to end

Conventions for this table:
- Server files are under `falloutmp-server/cpp/server_guest_lib/` and client files under `skymp5-client/src/`, unless a cell says otherwise.
- "AL" means `ActionListener.cpp`, "MOR" means `MpObjectReference.cpp`, "RS" means `services/services/remoteServer.ts`, and "SIS" means `services/services/sendInputsService.ts`.
- All cells are [src] unless they say [inference].
- Tests list the file and its Catch2 tag(s), and are in `unit/` unless a cell says otherwise.

| Feature | Client capture | Message(s) & key fields | Reliability / rate | Server handler & validation | Authoritative state & persistence field | Broadcast audience | Apply on remote / owner | Gamemode & Papyrus events | Tests | Gaps / TODOs |
|---|---|---|---|---|---|---|---|---|---|---|
| **Movement** | `getMovement` (sync/movementGet.ts:34-95; runMode 100-134) for the player and each hosted NPC, in `SIS.sendMovement` 113-135 | `UpdateMovement{idx, data{worldOrCell,pos,rot,direction,healthPercentage,speed,runMode,isInJumpState,isSneaking,isBlocking,isWeapDrawn,isDead,lookAt?}}` | U, every 130 ms per controlled actor | `AL::OnUpdateMovement` 116-184: ownership via `SendToNeighbours`; **relayed first**; `MovementValidation::Validate` (world change or ≥4096 u → Teleport2 to self); block-anim bookkeeping; `SetPos(CalledByUpdateMovement)`, `SetAngle`; anim bools; `lastMovUpdateByIdx` | `position`/`angle`/`worldOrCellDesc`, saved at most every 30 s (`IsLocationSavingNeeded`, MOR:1349-1354) | actor listeners incl. sender (raw relay) | RS:651-670 → `FormModel.movement` → `applyMovement` (sync/movementApply.ts:15-69): `translateTo`, teleport if >~ distance, health ×0.25 display | OnTriggerEnter/Leave via `SetPos` (MOR:535-599) | PartOne_MovementTest.cpp [PartOne]; MovementValidationTest.cpp [MovementValidation]; MovementSerializationTest.cpp [Serialization]; Grid*Test [Grid] | relay before validate (bad pos reaches peers); `teleportFlag` dead code (AL:121-142); hosted NPCs never snapped back; no speed check |
| **Animation** | `AnimationSource` hooks `sendAnimationEvent` (sync/animation.ts:212-285, setupHooks 306-339); `ignoredAnims` 296-304 | `UpdateAnimation{idx, data{animEventName, numChanges}}` | U, per event | `AL::OnUpdateAnimation` 186-207: ownership; relay; for own actor only: `AnimationSystem::Process` (block start/stop, SweetPie stamina), `SetLastAnimEvent` | `lastAnimEvent` in memory only; `CreateActor.animation` | listeners incl. sender | `applyAnimation` (animation.ts:122-199) with `numChanges` ordering | – | AnimationSystemTest.cpp [AnimationSystem] | unreliable plus no catch-up apart from last anim; hosted-NPC anims not stored |
| **Appearance / character creation** | RaceMenu close detected → `getAppearance` (sync/appearance.ts:35) in `SIS` 236-259 | `UpdateAppearance{idx, data: Appearance{isFemale,raceId,weight,skinColor,hairColor,headpartIds,headTextureSetId,options[19],presets[4],tints[],name}}`; S→C `SetRaceMenuOpen{open}` | R, once | `AL::OnUpdateAppearance` 209-228: accepted only if `IsRaceMenuOpen` → `SetRaceMenuOpen(false)`, `SetAppearance`, relay | `appearanceDump`, `isRaceMenuOpen` | listeners (reliable relay) | RS:671-690 → FormView recreates the clone with `applyAppearance` (appearance.ts:153); owner: `applyAppearanceToPlayer` 162 | `onUpdateAppearanceAttempt` (warning only); `mp.set(appearance)` (deferred ch. 2, no validation) | PartOne_UpdateLookTest.cpp [PartOne]; PartOne_ActorTest "createActor message contains Appearance" [PartOne]; SweetHidePlayerNamesTest [SweetHide] | no content validation (race, ranges, headparts); invalid race on load skips the form (WorldState.cpp:182-210) |
| **Equipment** | equip/unequip events → `getEquipment` (sync/equipment.ts:82) diff in `SIS` 261-285 | `UpdateEquipment{idx, data{inv, leftSpell?, rightSpell?, voiceSpell?, instantSpell?, numChanges}}` | R, on change | `AL::OnUpdateEquipment` 230-440: spells learned, `HasItem`, BOD2/BODT slot overlap → accept: relay + `SetEquipment`; reject: `SendInventoryUpdate` + SpSnippet `UnequipItem`/`RemoveSpell` | `equipment` | listeners | RS:691-726 → `applyEquipment` (equipment.ts:109), skipped while a menu is open (`isBadMenuShown` 128) | `onUpdateEquipmentAttempt` (warning only); Papyrus `OnObjectEquipped` via OnEquip path | PartOne_UpdateEquipmentTest.cpp [PartOne] | TODO at AL:418; NPC equipment only via `EquipBestWeapon` |
| **Inventory** | container-change events; periodic diff `getDiff`/`getInventory` (sync/inventory.ts:283-306) | S→C `SetInventory{inventory}`; `CreateActor.props.inventory` (owner) | R, deferred ch. 0 (overwrite) | All changes are server-side (`AddItem`/`RemoveItem(s)`/`SetInventory`, MOR:808-898) | `inv` | owner only | RS:157-174 → `applyInventory` (inventory.ts:334); `pcInv` re-applied every 5 s (RS:67-91) | Papyrus `OnItemAdded` (AddItem path only, MOR:825-843) | InventoryTest.cpp [Inventory]; PartOne_ActorTest inventory [PartOne]; TemplateInventoryTest [TemplateInventory] | other players' inventories are never sent; OnItemRemoved not fired |
| **Containers** | `OpenContainer` → menu; `containerChanged` diff in containersService.ts:19-99 | `Activate` → S `OpenContainer{target}`; C `PutItem`/`TakeItem{baseId,count,target,+ExtraData}` | R | `ProcessActivateNormal` CONT branch (MOR:1512): occupancy, distance ≤512 u; `AL::OnPutItem`/`OnTakeItem` 487-544: occupant check, `SweetCantDrop` keyword; `Inventory::RemoveItems` atomic | container `inv`, `isOpen`; `baseContainerAdded`; `nextRelootDatetime` | `SetInventory` to actor; `isOpen` UpdateProperty to listeners | RS:176-243 opens menu (`ResetContainer` + `AddItemEx`) | `onPutItem`, `onTakeItem`, `onActivate`; `SkympOnActivateClose` | PartOne_ActivateTest.cpp [PartOne][espm] | blocked put/take sends no correction; container not re-sent to other viewers |
| **Item pickup & harvesting** | `activate` event → activationService.ts:18-71 (`blockActivation` locally) | `Activate{caster:0x14, target, isSecondActivation}` | R | `AL::OnActivate` → `MpObjectReference::Activate` (MOR:440-505) → pickable branch 1447: `GivePickupItemsToActivationSource` (1356-1417; FLOR/TREE ingredient + count) → Disable / `SetHarvested` → `RequestReloot` | `isDisabled` / `isHarvested`; actor `inv`; `nextRelootDatetime` | `isHarvested` UpdateProperty / DestroyActor to listeners; SetInventory to picker | RS:728-755 `isHarvested` → static-object path; FormView disable | `onActivate`; Papyrus `OnActivate` | PartOne_ActivateTest [PartOne][espm]; PickUpItemCountTest [PickUpItemCountTest] | no distance check beyond worldspace |
| **Item drop** | dropItemService.ts:15-87 (container-change with no destination) | `DropItem{baseId, count}` | R | `AL::OnDropItem` 546-571 → `MpActor::DropItem` (MpActor.cpp:1588-1735): keyword, Gold forbidden, owns item, global queue of 10, places an FF ref with 120 s lifetime | actor `inv`; new FF ref (`count`) | CreateActor of dropped ref to neighbours | standard FF object FormView | `onDropItem` | DropItemTest.cpp [DropItemTest] | entry has no ExtraData (items with extra data fail [inference]); one timer per item |
| **Activation: doors (incl. teleport)** | activationService | `Activate` | R | DOOR branch (MOR:1467): load-door teleport → `MpActor::Teleport` → S→C `Teleport`; normal door → `SetOpen(!isOpen)`; teleport door auto-close/reloot 3 s | `isOpen`; actor `position`/`worldOrCellDesc` | `isOpen` UpdateProperty to listeners; Teleport to owner | RS:245-280 teleport (`moveRefrToPosition`/`LoadGame`?) [inference]; doors are FormViews (`baseRecordType=DOOR`) | `onActivate`; Papyrus `OnActivate` | PartOne_ActivateTest [PartOne][espm] | door FormView hack (issue #1186) |
| **Activation: furniture** | activationService | `Activate` (+ second activation on exit) | R | FURN branch (MOR:1543): occupancy, ≤256 u; `ProcessActivateSecond` (1570-1605) releases | occupant in memory | – | client sits locally; no remote sit sync except anims | `onActivate`, `SkympOnActivateClose` | PartOne_ActivateTest | occupancy not persisted |
| **Activation: activators / parents** | activationService | `Activate` | R | ACTI branch (MOR:1539) → Papyrus `OnActivate`; `ActivateChilds` (1607-1638) for activation parents; parent-only children are rejected | `activationBlocked` (**not saved**, MOR:677-681) | – | effects via SpSnippet / UpdateProperty | `onActivate`; `OnActivate` | ActivateParentTest.cpp [ActivateParentTest] | blocked state not persisted |
| **Crafting** | craftService.ts:18-70 (crafting-menu inventory diff) | `CraftItem{workbench, craftInputObjects: Inventory, resultObjectId}` | R | `CraftService::OnCraftItem` (20-73): bench keyword, exact recipe match (temper excluded), COBJ conditions (232-277), `UseCraftRecipe` | actor `inv` | `SetInventory` to crafter | applyInventory | `onCraft` | CraftTest.cpp [Craft][espm] | no workbench occupancy/distance; full COBJ scan per craft |
| **Eating / potions** | `SIS.onEquip` 54-75 (equip of ALCH/INGR) | `OnEquip{baseId}` | **U** | `AL::OnEquip` 619-630 → `MpActor::OnEquip` (474-553): owns item → `onEatItem` → ALCH effects (`ApplyMagicEffect`), remove 1; restoration window 5 s | `inv`, `activeMagicEffects`, `actorValues` | SetInventory to owner | – | `onEatItem`; `OnObjectEquipped` (MpActor.cpp:550) | HealthRestorationTest [Restoration]; ChangeValuesTest [ChangeValues] | unreliable; blocked event still removes item; INGR effects commented out |
| **Books** | as eating | `OnEquip{baseId}` | U | `MpActor::OnEquip` BOOK with teaches-spell → `onReadBook` → `AddSpell` | `learnedSpells` | – | – | `onReadBook` (blocked → SpSnippet RemoveSpell) | none | skill books not handled [inference] |
| **Spells** | magicSyncService.ts:30-92 (`spellCast` event + anim vars) | `SpellCast{caster,target,spell,isDualCasting,interruptCast,castingSource,aimAngle,aimHeading,actorAnimationVariables}`; `UpdateAnimVariables`; `OnHit` with spell source | U (relay U) | `AL::OnSpellCast` 1125-1204: caster own/hosted, alive, spell equipped → relay, Papyrus `OnSpellCast`; `OnSpellHit` 1211-1248 | none (effects TODO, AL:1202) | listeners | RS:941-967 → cast on the clone; RS:969-990 anim vars | Papyrus `OnSpellCast` | none | magic effects/damage of spells not applied server-side; unreliable |
| **Bow shots / ammo** | playerBowShotService.ts:52-63 (`playerBowShot` event) | `PlayerBowShot{weaponId, ammoId, power, isSunGazing}` | **U** | `AL::OnPlayerBowShot` 573-602: ammo is AMMO, owned → `RemoveItem(1)` | `inv` | SetInventory to owner | (projectile visuals come from anim relay [inference]) | – | none | unreliable → ammo desync; damage via OnHit `IsBowOrCrossbowShot` |
| **Melee hits & damage** | hitService.ts:15-69 (`hit` event) | `OnHit{aggressor,isBashAttack,isHitBlocked,isPowerAttack,isSneakAttack,projectile,source,target}` | R | `AL::OnHit` 1006-1112 → `OnWeaponHit` 1250-1408: aggressor own/hosted, same cell, ≤4096 u, dead→respawn, `CanHit` speed cooldown, splash 0.1 s/4, block, `CalculateDamage` (TES5DamageFormula.cpp:151-163) → `NetSetPercentages` | target `actorValues` | `ChangeValues` to target owner/hoster; death → DeathStateContainer | target applies own health | Papyrus `OnHit` (`akProjectile` None) | HitTest.cpp [Hit]; TES5DamageFormulaTest [TES5DamageFormula] | reach check commented out (1315-1334); power/sneak flags trusted |
| **Blocking** | `isBlocking` in movement; block anims | `UpdateMovement.isBlocking`; `UpdateAnimation blockStart/blockStop`; `OnHit.isHitBlocked` | U | `AnimationSystem` block state (AnimationSystem.cpp:10-34); `ShouldBeBlocked` angle < 1 rad (AL:992-1003); 5 non-blocking moves → inactive (AL:146-171) | in memory | – | anim relay | – | AnimationSystemTest [AnimationSystem]; HitTest [Hit] | – |
| **Actor values & regen** | `SIS` 137-196 (player AVs; also used for hosted NPCs, line 148) | `ChangeValues{idx?, data{health?,magicka?,stamina?}}` (percentages) | R | `AL::OnChangeValues` 765-825: ignores idx; skips during potion window; `CropRegeneration` (health, magicka only) → `SetPercentage`; correction if cropped | `actorValues` (reset to 1.0 on load with ESM, MpActor.cpp:603-608) | owner/hoster only (corrections, host start) | RS:821-843 → `actorvalues.ts` set | – | ChangeValuesTest [ChangeValues]; CropRegenerationTest [CropRegeneration]; GetBaseActorValuesTest [GetBaseActorValues] | stamina increases not cropped; hosted NPC values carry the player's AVs; neighbours see only movement `healthPercentage` |
| **Death & respawn** | deathService.ts:77-102 | S→C `DeathStateContainer{tTeleport?, tChangeValues?, tIsDead?}`; `UpdateProperty isDead` | R | `MpActor::Kill`/`SetIsDead`/`RespawnWithDelay`/`Respawn` (§1.17) | `isDead`, `spawnPoint`, `spawnDelay`, `*RespawnPercentage` | owner/hoster (death); listeners (isDead=false on respawn) | RS:757-811 kill/resurrect | `onDeath`, `onRespawn` | RespawnTest [Respawn]; misc/tests test_isdead | neighbours learn death from movement only |
| **NPC spawning** | – (server streams) | `CreateActor{refrId(long), baseId, appearance?, equipment?, templateChain, isHostedByOther, ...}`; `DestroyActor` | R | `AttachEspmRecord` filters (WorldState.cpp:368-659); `npcEnabled`/`npcSettings`; lazy chunk load | ESM ChangeForm only when changed | grid listeners | FormView creates FF clone via `TESModPlatform.CreateNpc`/`EvaluateLeveledNpc` | `OnInit`/`OnLoad` Papyrus | NpcExists.cpp [NpcExists][espm]; LoadCellsTest [LoadCells] | NPCs off by default; leveled visual variant not synced (formView.ts:132-147) |
| **NPC hosting / AI** | `tryHostIfNeed` (formView.ts:648-662) → hostAttempts | `Host{remoteId}`; `HostStart/HostStop{target}`; then NPC `UpdateMovement`/`UpdateAnimation` | Host U; start/stop R | `AL::OnHostAttempt` 653-738 (§1.6) | `worldState.hosters` (memory) | `isHostedByOther` UpdateProperty | hoster runs AI; others interpolate | – | none | stale hoster entries; log format bug (AL:685-686) |
| **Leveled lists** | – | (resolved server-side) | – | `LeveledListUtils::EvaluateList` (chance none bug at line 35); `EnsureTemplateChainEvaluated` (MpActor.cpp:1077-1180) | `templateChain`, `inv` | via CreateActor | – | – | LeveledListUtilsTest [espm]; TemplateInventoryTest | LVLI with global chance-none never yields |
| **Reloot** | – | UpdateProperty `isOpen`/`isHarvested`, CreateActor | R | `RequestReloot`/`DoReloot` (§1.18) | `nextRelootDatetime` | listeners | – | – | PartOne_ActivateTest | ESM forms only |
| **Console commands** | consoleCommandsService.ts:65-93 (hooks console) | `ConsoleCommand{commandName, args[]}` | R | `ConsoleCommands::Execute` (175): `EnsureAdmin`; AddItem, EquipItem, PlaceAtMe, Disable, Mp | effects of the command | per command | – | – | ConsoleCommandTest [ConsoleCommand] | small command set |
| **Chat** | skymp5-front chat widget (constructorComponents/chat/index.js:78-104) → browser event → gamemode event source | `CustomEvent{eventName:"_..."}` / `CustomPacket` | R | gamemode JS (ChatSystem lives in a private `src`, not in the repo) | gamemode-defined | gamemode-defined (`mp.sendCustomPacket`) | browser UI | `_name` custom events | none in repo | entirely gamemode; not in core |
| **Time / weather** | timeService.ts:24-64 | none | – | – | – | – | client sets GameHour etc. from UTC | – | none | server game time differs; weather not synced |
| **Display names / nicknames** | – | `Appearance.name`; `props.displayName`; UpdateProperty `displayName` | R | Papyrus `SetDisplayName` (PapyrusObjectReference.cpp:874-897) | `appearanceDump.name`, `displayName` | listeners | FormView sets the name | – | SweetHidePlayerNamesTest [SweetHide] | `%original_name%` sentinel |
| **Node scale / texture** | – | `props.setNodeScale[]`, `setNodeTextureSet[]` + SpSnippet | R | Papyrus `NetImmerse.SetNodeScale/SetNodeTextureSet` (PapyrusNetImmerse.cpp:6-75) | `setNodeScale`, `setNodeTextureSet` | listeners | modelApplyUtils.ts:13-128 | – | none | – |
| **Factions** | – | none | – | Papyrus Faction natives | `factions` (not in ToTuple) | none | – | – | misc/tests test_factions | not sent to clients |
| **Quests** | – | none | – | `Quest.GetStage` returns 0 | `quests` (never written) | none | – | – | none | essentially unimplemented |
| **Learned spells** | – | `props.learnedSpells` | R | `AddSpell`/`RemoveSpell` (Papyrus + book) + SpSnippet | `learnedSpells` | all listeners (CreateActor) | client `addSpell` on owner | `onReadBook` | PapyrusActorTest [Papyrus][Actor] | no UpdateProperty for live changes (uses SpSnippet) |
| **Enabled / disabled** | – | `props.isDisabled` (ESM) / Create-Destroy (FF, actors) | R | `Enable`/`Disable` (MOR:507-533), Papyrus Enable/Disable (SpSnippet for ESM refs) | `isDisabled` | listeners | **client reads `disabled`** (RS:303,746), so the CreateActor prop is ignored | – | PapyrusObjectReferenceTest [Papyrus][ObjectReference] | key mismatch; ESM disable sends no UpdateProperty |
| **isOpen** | – | `props.isOpen`, UpdateProperty | R | `SetOpen` (MOR:620-630) | `isOpen` | listeners | static-object path `setOpen` | – | PartOne_ActivateTest | – |
| **isHarvested** | – | `props.isHarvested`, UpdateProperty | R | `SetHarvested` (MOR:609-618) | `isHarvested` | listeners | static-object path | – | PartOne_ActivateTest | – |
| **lastAnimation** | – | `props.lastAnimation` | R | Papyrus `PlayAnimation` (PapyrusObjectReference.cpp:611-630) | `lastAnimation` (**not persisted**) | listeners | replays the animation on stream-in | – | none | lost on restart |
| **Custom properties** | `ctx.get` in `updateOwner`/`updateNeighbor` | `customPropsJsonDumps[]`; `UpdateProperty{propName, dataDump}` | R | `CustomPropertyBinding::Set` (CustomPropertyBinding.cpp:55) → `SetPropertyValueDump` (MOR:746-763) | `dynamicFields` | owner and/or neighbours by flags; `private.*` never | gamemodeUpdateService.ts runs signed functions | – | misc/tests (unit does not link addon) | ESM-id NPC UpdateProperty dropped on client (RS:732-751, 915-920) |


---

## 3. The "SkyMP standard": checklist

### 3.1 Checklist for a synced feature (S1–S23)

Each item is followed by SkyMP in the "Example" location. FalloutMP features should pass every item that applies, unless a deviation is deliberate and documented.

| # | Standard | Example implementation |
|---|---|---|
| S1 | The message is defined once as a C++ struct with `Serialize(Archive&)` and `kMsgType`. It is registered in `Messages.h` and mirrored by a TS interface plus an enum value | `messages/HostStartMessage.h`; `Messages.h:15,55`; `skymp5-client/src/services/messages/hostStartMessage.ts` |
| S2 | Binary on the wire: the client sends JSON, the plugin converts it. No legacy `"type"` string messages | `MpClientPlugin.cpp:91-102`; `PacketParser.cpp:63-69` |
| S3 | The client sends **intent**; the server computes the result and sends authoritative state (`SetInventory`, `UpdateProperty`) | Put/Take (`AL:487-544` → `SendInventoryUpdate`) |
| S4 | High-frequency transient state (movement, animation, anim variables, spell casts) is unreliable and relayed as raw bytes to grid neighbours | `AL::SendToNeighbours` 39-96 |
| S5 | State changes and RPCs are reliable | Equipment, appearance, activate, containers |
| S6 | Every handler that takes an idx/caster/aggressor checks ownership: the user's own actor, or an NPC that user hosts | `AL:58-86, 452-466, 1020-1033` |
| S7 | Visibility is grid-scoped (3×3 cells of 4096 u). New listeners get the full state in `CreateActor`; changes go out as deltas through `UpdateProperty` or relays | `PartOne.cpp:759-879`; `MOR:609-630` |
| S8 | Private data (inventory, AVs, race menu flag) goes to the owner only (`VisitPropertiesMode::All` vs `OnlyPublic`) | `PartOne.cpp:820-827`; `MpActor.cpp:350-390` |
| S9 | Persistent state lives in `MpChangeFormREFR`, is changed only through `EditChangeForm`, is serialized in `ToJson`/`JsonToChangeForm` with guarded optional reads, and uses `FormDesc` for ids | `MpChangeForms.cpp:23-384`; `ChangeFormGuard.h:27` |
| S10 | State is restored in `ApplyChangeForm` (side effects such as timers and respawn re-armed) and reflected in `CreateActor` props | `MOR:1180-1251`; `MpActor.cpp:582-625` |
| S11 | Every gameplay action is wrapped in a `GameModeEvent` so the gamemode can veto it; the default effect lives in `OnFireSuccess` | `gamemode_events/EatItemEvent.*`; `ActivateEvent.*` |
| S12 | A rejection sends a correction that restores the client to server truth | Teleport2 (`MovementValidation.cpp:21-33`); equipment (`AL:376-435`); ChangeValues crop (`AL:822-824`) |
| S13 | Atomic inventory mutation: work on a copy, throw if insufficient | `Inventory.cpp:106-136` |
| S14 | Messages addressed to an NPC go to `GetActorToSendTo()` (owner, else hoster) | `MpActor.cpp:449-472` |
| S15 | Server-side Papyrus natives with visible effects mirror them through SpSnippet to the owner or listeners; latent natives return values via `FinishSpSnippet` | `PapyrusSound.cpp:8-31`; `SpSnippet.cpp:23-61` |
| S16 | Papyrus events go through `MpForm::SendPapyrusEvent` so the gamemode sees `onPapyrusEvent:X` | `MpForm.cpp:34-40` |
| S17 | Ids on the wire: `idx` for live refs, long ids (`+0x100000000`) for ESM-id actors, `0x14` alias for self | `PartOne.cpp:805-809`; `worldViewMisc.ts:53-87` |
| S18 | Ordered application uses `numChanges` counters where reliable-unordered client sends could reorder | `UpdateEquipment.numChanges`, `UpdateAnimation.numChanges` |
| S19 | Deferred, coalesced sends for bursty state (inventory overwrite channel; SpSnippet append channel) | `PartOne.cpp:688-735, 1008-1035` |
| S20 | Exceptions are the rejection mechanism; `HandlePacket` logs them and never crashes or disconnects | `PartOne.cpp:468-473` |
| S21 | Client changes the game only inside `update`; state shared across hot reloads lives in `sp.storage` | `remoteServer.ts`; `formView.ts` |
| S22 | Gamemode-tunable behaviour is a property (`mp.get/set`) or a server setting, not a hard-coded constant | `PropertyBindingFactory.cpp:31-77`; `ScampServer.cpp:176-443` |
| S23 | Every server feature has a Catch2 test that drives `PartOne` through `DoConnect`/`DoMessage` and inspects `FakeSendTarget` messages | `unit/PartOne_*Test.cpp`; `TestUtils.hpp:26-35` |

### 3.2 SkyMP's own inconsistencies (decide parity vs SkyMP-plus per item)

| # | Inconsistency | Evidence | Parity (copy) | SkyMP-plus (recommended for FalloutMP) |
|---|---|---|---|---|
| I1 | Movement is relayed before it is validated | AL:119 vs 136-144 | relay first | validate first; drop and snap back |
| I2 | Client "reliable" = RELIABLE (unordered); server = RELIABLE_ORDERED | Networking.cpp:91,181 | keep | RELIABLE_ORDERED both ways, or keep `numChanges` everywhere |
| I3 | `OnEquip`, `PlayerBowShot` are unreliable although they change inventory | SIS:54-75; playerBowShotService.ts | U | R |
| I4 | `SendMessageToActorListeners` ignores `reliable` | MOR:2028-2034 | – | honour it |
| I5 | Disabled-state key: server `isDisabled`, client reads `disabled` | CreateActorMessage.h:73; RS:303,746 | (broken) | fix the key; send UpdateProperty on ESM disable |
| I6 | UpdateProperty for ESM-id actors goes down the static-object path, so `isHostedByOther`, custom props and `isDead` are lost | RS:732-751, 915-920 | (broken) | route by long id |
| I7 | AV percentages are reset to 1.0 on load when ESM data is attached | MpActor.cpp:603-608 | – | restore saved values |
| I8 | Stamina increases are not cropped | AL:801-805 | – | crop all three |
| I9 | Melee reach check commented out; power/sneak flags trusted | AL:1315-1334 | – | enable reach; derive flags from anim state |
| I10 | Spell effects are TODO; INGR effects disabled | AL:1202; MpActor.cpp OnEquip | – | implement the FO4 equivalents (chems, etc.) |
| I11 | `lastAnimation`, `activationBlocked`, Papyrus `SetPosition` are not persisted; `quests` never written; `factions`/effects missing from `ToTuple` | MpChangeForms.cpp:84-87; MOR:677-681; PapyrusObjectReference.cpp:563-591; MpChangeForms.h:135-146 | – | persist all, add to `ToTuple` |
| I12 | Blocked put/take/drop send no correction; blocked eat still removes the item | AL:487-571; MpActor.cpp:474-553 | – | always send `SetInventory` on reject |
| I13 | Hosted NPC `ChangeValues` carry the player's AVs; server ignores `idx` | SIS:148; AL:768 | – | per-actor AVs with idx validation |
| I14 | Death is sent only to the owner/hoster; neighbours infer it from movement | MpActor.cpp:1003-1059 | – | UpdateProperty `isDead` to listeners on death too |
| I15 | No pickup/activation distance check beyond worldspace; crafting needs no bench occupancy | MOR:1419-1568; CraftService.cpp | – | distance + occupancy |
| I16 | SpSnippet only for player-created actors | SpSnippet.cpp:27-32 | – | route to hoster for NPCs |
| I17 | LVLI with a global chance-none never yields | LeveledListUtils.cpp:35 | – | evaluate the global |
| I18 | Server game time (days since start) ≠ client clock (UTC-based) | PapyrusUtility.cpp:73-79; timeService.ts | – | one server-driven clock |
| I19 | Docs say `pos`/`angle`/`worldOrCellDesc` are settable; code throws | docs/docs_properties_system.md vs PropertyBindingFactory | – | fix docs or add setters |
| I20 | BitStream reader has no size bounds checks; integral constants not verified | BitStreamInputArchive (TODO) | – | add bounds checks (DoS) |
| I21 | Unregistered C→S message silently dropped; unknown S→C throws on the client | MessageSerializerFactory.cpp:205-219; networkingService.ts:188-191 | – | log both; ignore unknown on the client |
| I22 | `OnItemAdded` fires only on the `AddItem` path | MOR:825-843 | – | fire for every inventory addition |

### 3.3 Definition of Done for a FalloutMP synced feature

1. It has a row in the §2 table with every column filled.
2. It passes S1–S23 where they apply, and each deviation is listed as I-style with a reason.
3. It has a unit test (PartOne-level) for: the accept path, each reject path (asserting the correction message), and persistence round trip (`SaveStorageTest`-style) if it adds a ChangeForm field.
4. A late joiner sees the correct state (`CreateActor` contents test), and the state survives a restart (ChangeForm test).
5. If the change touches the protocol, `kMessagingProtocolVersion` is bumped.

---

## 4. Testing infrastructure and recipes

### 4.1 Unit harness (Catch2)

| Item | Detail | Evidence |
|---|---|---|
| Build/run | In `build/`: `cmake --build .`; `ctest --verbose`; `./unit/unit "[Tag]"` | CLAUDE.md |
| Main | `unit/main.cpp:19-55`: adds `~[espm]` when `Skyrim.esm` is not found in the data dir (CMake option `SKYRIM_DIR`/data dir) | [src] |
| Linkage | `unit` links `server_guest_lib` + `espm`; **not** the N-API addon, so property bindings and `mp.*` cannot be unit-tested | [src: unit/CMakeLists.txt] |
| Fixture | `GetPartOne()` (defined in `PartOne_ActivateTest.cpp:27-43`): `PartOne` with `FakeDamageFormula` (25), Papyrus scripts from `TEST_PEX_DIR`, espm attached if available | [src] |
| Helpers | `MakeMessage(json)`, `DoMessage(partOne, userId, json)`, `DoConnect`, `DoDisconnect`, `DoUpdateMovement(partOne, actorFormId, userId)`; JSON templates `jMovement`, `jAppearance`, `jEquipment`; `FakeListener` (records `OnConnect`/`OnMpApiEvent` into a string; can be extended to return `false`); `FindRefrMessage<T>(partOne, refrId)` / `FindRefrMessageIdx<T>` | [src: unit/TestUtils.hpp:13-160] |
| Output capture | `PartOne::Messages()` from `FakeSendTarget` (PartOne.cpp:46-69): vector of `{userId, reliable, j (JSON), message (IMessageBase)}`. Typical assertion: `REQUIRE(partOne.Messages().size() == N); auto& m = partOne.Messages()[0]; REQUIRE(m.reliable); REQUIRE(m.j["t"] == MsgType::X);` | [src] |
| Count | 255 TEST_CASEs across ~70 files | [src] |
| Uncovered | Host/HostStart/HostStop, ReadBook, SpellCast, PlayerBowShot, NetImmerse, SetDisplayName, PlayAnimation, custom properties | [src: grep] |

Tag index (file → tags):

| Area | File(s) → tag |
|---|---|
| PartOne core | PartOneTest, PartOne_ActorTest, PartOne_MovementTest, PartOne_UpdateLookTest, PartOne_UpdateEquipmentTest, PartOne_BotTest → `[PartOne]`; PartOne_ActivateTest → `[PartOne][espm]`; PartOne_IdGen → `[IdGen]`; PartOne_ProfileId → `[ProfileId]` |
| Gameplay | ActivateParentTest `[ActivateParentTest]`; ActorTest `[Actor]`; AnimationSystemTest `[AnimationSystem]`; ChangeValuesTest `[ChangeValues]`; CraftTest `[Craft][espm]`; CropRegenerationTest `[CropRegeneration]`; DropItemTest `[DropItemTest]`; HealthRestorationTest `[Restoration]`; HitTest `[Hit]`; TES5DamageFormulaTest `[TES5DamageFormula]`; InventoryTest `[Inventory]`; PickUpItemCountTest `[PickUpItemCountTest]`; RespawnTest `[Respawn]`; ConsoleCommandTest `[ConsoleCommand]`; MovementValidationTest `[MovementValidation]`; SweetHidePlayerNamesTest `[SweetHide]`; TemplateInventoryTest `[TemplateInventory]`; TemplateScriptTest `[TemplateScript]`; GetBaseActorValuesTest `[GetBaseActorValues]` |
| World / persistence | WorldStateTest `[WorldState]`; SaveStorageTest `[save]`; MigrationDatabaseTest `[MigrationDatabase]`; MongoDatabaseTest `[MongoDatabase]`; FormDescTest `[FormDesc]`; LoadCellsTest `[LoadCells]`; NpcExists `[NpcExists][espm]`; PrimitiveTest `[primitive][espm]`; ObjectReferenceTest `[ObjectReference]`; GridTest/Grid_MoveTest `[Grid]`; ActorsMapTest `[ActorsMap]`; ServerStateTest `[ServerState]`; EspmTest, LeveledListUtilsTest `[espm]`; DistContentsTest `[DistContents]` |
| Wire | ArchiveTestBinary/ArchiveTestSimdJson `[Archives][Serialization]`; MovementSerializationTest `[Serialization]`; Networking*Test `[Networking]`; IdManagerTest `[IdManager]` |
| Papyrus | Papyrus{Actor,Debug,Form,FormList,Game,ObjectReference,Skymp,Utility}Test `[Papyrus][<Class>]` (+`[espm]`); PapyrusCompatibilityTest `[Papyrus][espm]`; HeuristicPolicyTest `[HeuristicPolicy]`; VarValueTest `[VarValue]`; VirtualMachineTest `[VirtualMachine]` |
| Perf | Benchmarks `[Benchmarks]` |

### 4.2 Papyrus tests

- `.psc` test scripts are compiled to `TEST_PEX_DIR` and loaded by `GetPartOne()`. `VirtualMachineTest`/`PapyrusCompatibilityTest` run scripts directly [src: unit/CMakeLists.txt; PartOne_ActivateTest.cpp:27-43].
- Natives can be tested by instantiating the class (`PapyrusActor actor; actor.compatibilityPolicy = …; actor.SetActorValue(VarValue(&mpActorGameObject), {…})`) and asserting on the SpSnippet messages that `partOne.Messages()` captures [src: PapyrusActorTest.cpp].

### 4.3 Bots, mock networking, packet history

| Tool | Use | Evidence |
|---|---|---|
| `mp.createBot()` | Server-side fake client (`Bot.connect/send/disconnect`) through `MockServer`. Packets go through the real `PartOne::HandlePacket` | ScampServer.cpp:687-762; PartOne_BotTest.cpp `[PartOne]` |
| `Networking_MockTest` | `MockServer`/`MockClient` pair | `[Networking]` |
| Packet history | `PartOne::SetPacketHistoryRecording(userId, bool)`, `GetPacketHistory`, `ClearPacketHistory`, `RequestPacketHistoryPlayback(userId, history)`; JS wrappers through `PacketHistoryWrapper::FromNapiValue/ToNapiValue`. Playback is ticked in `TickPacketHistoryPlaybacks`; recorded packets of the played-back user are suppressed | PartOne.cpp:596-637, 969-1006 |

### 4.4 misc/tests (JS integration)

- Registered in `CMakeLists.txt:253-292` and run by `cmake/run_integration_test.cmake`. The runner:
  1. Merges `server-settings-base.json` with `<test>.settings.json`.
  2. Wipes `world/changeForms` and copies a satellite DB folder if present.
  3. Copies the test to `gamemode.js`.
  4. Runs `node dist_back/falloutmp-server.js` (60 s).
  5. Passes on exit code 0 and `Test passed!` in stdout.
- ctest properties: `TIMEOUT 120`, `RESOURCE_LOCK server_directory`, `FIXTURES_REQUIRED unit_passed`.
- Existing tests: `test_crash`, `test_dlc1chauruscocoonscript_stack_overflow`, `test_factions`, `test_findclosestreferenceoftypefromref`, `test_isdead`, `test_onPapyrusEvent_OnItemAdded`, `test_onactivate`, `test_partial_location_save`, `test_registerforsingleupdate`, `test_removeallitems`, `test_spawnpoint_of_placed_actors`.
- This is the **only** place to test property bindings, `mp.*` and gamemode events end to end.

### 4.5 Recipes

#### (a) New message type end to end

| Step | File | Action |
|---|---|---|
| 1 | `falloutmp-server/cpp/messages/MsgType.h` | Append `Foo = 34` before `Max` (never renumber) |
| 2 | `falloutmp-server/cpp/messages/FooMessage.h` | New struct (skeleton below) |
| 3 | `falloutmp-server/cpp/messages/Messages.h` | `#include "FooMessage.h"` + `REGISTER_MESSAGE(FooMessage)` |
| 4 | `server_guest_lib/PacketParser.cpp` | `case MsgType::Foo:` → `actionListener.OnFoo(rawMsgData, *message)` (C→S only) |
| 5 | `server_guest_lib/ActionListener.h/.cpp` | `virtual void OnFoo(const RawMessageData&, const FooMessage&)`; ownership check (S6), validation, `GameModeEvent` (S11), correction (S12) |
| 6 | `mp_common/Config.h` | Bump `kMessagingProtocolVersion` |
| 7 | `skymp5-client/src/services/messages.ts` | `Foo = 34` |
| 8 | `skymp5-client/src/services/messages/fooMessage.ts` + `anyMessage.ts` union | TS interface |
| 9 | `skymp5-client/src/services/events/events.ts` | `'fooMessage': [ConnectionMessage<FooMessage>]` (S→C) |
| 10 | `services/services/networkingService.ts` (~line 160 pattern) | `else if (msgAny.t === MsgType.Foo) this.controller.emitter.emit("fooMessage", event)`, otherwise it throws |
| 11 | Client service | send: `this.controller.emitter.emit("sendMessage", {message, reliability: "reliable"})`; receive: `controller.emitter.on("fooMessage", …)` |
| 12 | `unit/FooTest.cpp` | Serialization round trip + PartOne test |

```cpp
// messages/FooMessage.h
#pragma once
#include "MessageBase.h"
#include "MsgType.h"
#include <type_traits>
struct FooMessage : public MessageBase<FooMessage> {
  static constexpr auto kMsgType =
    std::integral_constant<char, static_cast<char>(MsgType::Foo)>{};
  template <class Archive> void Serialize(Archive& archive) {
    archive.Serialize("t", kMsgType)          // always first
           .Serialize("idx", idx)
           .Serialize("value", value);        // std::optional<T> = optional field
  }
  uint32_t idx = 0;
  std::optional<float> value;
};
```
```cpp
// unit/FooTest.cpp
TEST_CASE("Foo is validated and broadcast", "[Foo]") {
  PartOne& partOne = GetPartOne();
  DoConnect(partOne, 0);
  partOne.CreateActor(0xff000000, {0,0,0}, 0, 0x3c);
  partOne.SetUserActor(0, 0xff000000);
  partOne.Messages().clear();
  DoMessage(partOne, 0, {{"t", MsgType::Foo}, {"idx", 0}, {"value", 1.0}});
  REQUIRE(partOne.Messages().size() == 1);
  REQUIRE(partOne.Messages()[0].j["t"] == MsgType::Foo);
  partOne.DestroyActor(0xff000000); DoDisconnect(partOne, 0);
}
```
```ts
// skymp5-client/src/services/messages/fooMessage.ts
import { MsgType } from "../../messages";
export interface FooMessage { t: MsgType.Foo; idx: number; value?: number; }
```

#### (b) New persisted ChangeForm field

| Step | File | Action |
|---|---|---|
| 1 | `server_guest_lib/MpChangeForms.h` | Add `std::optional<T> fooBar;` to `MpChangeFormREFR` **and** to `ToTuple()` |
| 2 | `MpChangeForms.cpp::ToJson` | `if (cf.fooBar) res["fooBar"] = *cf.fooBar;` |
| 3 | `MpChangeForms.cpp::JsonToChangeForm` | Guarded read: `static JsonPointer fooBar("fooBar"); if (element.at_pointer(fooBar.GetData()).error() == simdjson::error_code::SUCCESS) { ReadEx(element, fooBar, &tmp); res.fooBar = tmp; }` (an unguarded read of a missing key throws, which **skips the whole form**) |
| 4 | `MpObjectReference`/`MpActor` | Setter `SetFooBar(v)` → `EditChangeForm([&](auto& cf){ cf.fooBar = v; })` + `SendMessageToActorListeners(CreatePropertyMessage_(this, "fooBar", dump), true)` |
| 5 | `ApplyChangeForm` | Re-apply side effects on load |
| 6 | `CreateActorMessage.h` (`CreateActorMessageAdditionalProps`) + `VisitProperties` | Late-joiner snapshot (choose owner-only vs public, S8) |
| 7 | Client | `createActorMessage.ts` props; `remoteServer.ts` createActor + `updateProperty` (both FormModel and the ESM static path); FormView apply |
| 8 | Tests | `SaveStorageTest.cpp [save]` round trip; `WorldStateTest`/`ActorTest`; CreateActor contents (`PartOne_ActorTest` pattern) |

#### (c) New built-in property (PropertyBindingFactory)

```cpp
// falloutmp-server/cpp/addon/property_bindings/FooBarBinding.h
#pragma once
#include "PropertyBinding.h"
class FooBarBinding : public PropertyBinding {
public:
  std::string GetPropertyName() const override { return "fooBar"; }
  Napi::Value Get(Napi::Env env, ScampServer& scampServer, uint32_t formId) override;
  void Set(Napi::Env env, ScampServer& scampServer, uint32_t formId, Napi::Value newValue) override;
};
// .cpp — pattern from IsOpenBinding.cpp
Napi::Value FooBarBinding::Get(Napi::Env env, ScampServer& s, uint32_t formId) {
  auto& refr = s.GetPartOne()->worldState.GetFormAt<MpObjectReference>(formId);
  return Napi::Boolean::New(env, refr.GetFooBar());
}
void FooBarBinding::Set(Napi::Env env, ScampServer& s, uint32_t formId, Napi::Value v) {
  auto& refr = s.GetPartOne()->worldState.GetFormAt<MpObjectReference>(formId);
  refr.SetFooBar(v.As<Napi::Boolean>().Value());   // validate type/range here
}
```
Register it in `addon/property_bindings/PropertyBindingFactory.cpp:31-77`, then update `docs/docs_properties_system.md`. Test it in `misc/tests/test_foobar.js` (`mp.set` / `mp.get`, then `console.log("Test passed!")`) because `unit` does not link the addon.

#### (d) New gamemode event

```cpp
// server_guest_lib/gamemode_events/FooEvent.h
class FooEvent : public GameModeEvent {
public:
  FooEvent(MpActor* actor_, uint32_t arg_) : actor(actor_), arg(arg_) {}
  const char* GetName() const override { return "onFoo"; }
  std::string GetArgumentsJsonArray() const override {   // must be valid JSON
    return "[" + std::to_string(actor->GetFormId()) + "," + std::to_string(arg) + "]";
  }
private:
  void OnFireSuccess(WorldState*) override { /* default action */ }
  void OnFireBlocked(WorldState*) override { /* send correction (S12) */ } // virtual in GameModeEvent.h:28
  MpActor* actor; uint32_t arg;
};
// call site:  FooEvent(&actor, arg).Fire(worldState);
```
Test it in unit with a `PartOne::Listener` subclass whose `OnMpApiEvent` returns `false` (see the `FakeListener` in TestUtils.hpp:82-103). Do the end-to-end test in misc/tests with `mp.onFoo = (actor, arg) => false`.

#### (e) New server Papyrus native

```cpp
// server_guest_lib/script_classes/PapyrusFoo.h   (pattern: PapyrusSound.h/.cpp)
class PapyrusFoo final : public IPapyrusClass<PapyrusFoo> {
public:
  const char* GetName() override { return "Foo"; }
  VarValue DoThing(VarValue self, const std::vector<VarValue>& args) {
    auto refr = GetFormPtr<MpObjectReference>(self);
    if (!refr) return VarValue::None();
    /* 1. mutate server state (EditChangeForm) */
    /* 2. mirror to clients */
    auto serialized = SpSnippetFunctionGen::SerializeArguments(args, refr->GetParent());
    for (auto listener : refr->GetActorListeners())
      SpSnippet(GetName(), "DoThing", serialized, refr->GetFormId())
        .Execute(listener, SpSnippetMode::kNoReturnResult);
    return VarValue::None();
  }
  void Register(VirtualMachine& vm, std::shared_ptr<IPapyrusCompatibilityPolicy> policy) override {
    compatibilityPolicy = policy;
    AddMethod(vm, "DoThing", &PapyrusFoo::DoThing);   // AddStatic for global functions
  }
};
```
Add it to `PapyrusClassesFactory.cpp:26-61`. The script stub (`.pex` with the `native` declaration) must exist in the script storage (the embedded `standard_scripts`, or for FO4 the FO4 base scripts). Test by calling the native directly in `unit/PapyrusFooTest.cpp [Papyrus][Foo]` and asserting the SpSnippet in `partOne.Messages()`, plus `mp.callPapyrusFunction` in misc/tests.

#### (f) New client service

```ts
// skymp5-client/src/services/services/fooService.ts   (pattern: hitService.ts)
import { ClientListener, CombinedController, Sp } from "./clientListener";
import { MsgType } from "../../messages";
export class FooService extends ClientListener {
  constructor(private sp: Sp, private controller: CombinedController) {
    super();
    this.controller.on("update", () => this.onUpdate());                 // game API only here
    this.controller.emitter.on("fooMessage", (e) => this.onFoo(e.message));
  }
  private onUpdate() { /* capture → this.controller.emitter.emit("sendMessage",
       { message: { t: MsgType.Foo, idx: 0 }, reliability: "reliable" }) */ }
  private onFoo(msg: FooMessage) { /* write model in sp.storage / apply via FormView */ }
}
```
Register it in `skymp5-client/src/index.ts:70-120` (`new FooService(sp, controller)`). Use `sendMessageWithRefrId` (`_refrId`) to target a non-self ref. Map ids with `localIdToRemoteId`/`remoteIdToLocalId` (`view/worldViewMisc.ts`).

---

## 5. Performance

| Topic | Fact | Evidence |
|---|---|---|
| Tick loop | `server.tick(); await setTimeout(1)` in a JS loop, so it runs as fast as possible with about a 1 ms yield. `ScampServer::Tick` drains every packet, then `PartOne::Tick` | ts/index.ts:221-235; ScampServer.cpp:470-508 |
| Per-tick fixed costs | `TickDeferredMessages` loops over `maxConnectedId`; `TickPacketHistoryPlaybacks`; `WorldState::Tick` (save upsert, timers) | PartOne.cpp:146-151, 1008-1035 |
| Player cap | Compile-time `MAX_PLAYERS=1000` (`kMaxPlayers`, sizes the arrays); settings `maxPlayers` default 100 | cpp/CMakeLists.txt:46; settings.ts:23 |
| NPCs | Off by default (`npcEnabled=false`); when on, every ESM actor in the 3×3 area gets a FormView + host traffic | ScampServer.cpp:235-247 |
| Movement bandwidth | ~60-70 bytes per `UpdateMovement` [inference], ~7.7 Hz per controlled actor; relayed to all grid neighbours including the sender, so it is O(N²) within one 3×3 area | SIS:113-135; AL:39-96 |
| Stream-in burst | One `CreateActor` per ESM ref per player when cells load (doors and statics with props included) | PartOne.cpp:759-879 |
| Hotspots (server) | JSON fallback parse up to 33× per packet; `GetAppearance` parses JSON on every call; `RequestSave` copies the full ChangeForm; pretty-printed file DB (one file per form); `CraftService` scans all COBJ; one timer per dropped item; per-call `random_device` in leveled lists | MessageSerializerFactory.cpp:118-145; WorldState.cpp:290-308; FileDatabase.cpp:36; CraftService.cpp:142-194; LeveledListUtils.cpp |
| Hotspots (client) | `FormView.update` every frame for all forms; inventory re-apply every 5 s; binary→JSON→JS conversion per packet | formView.ts:38-311; RS:67-91; MpClientPlugin.cpp:53-89 |
| Benchmarks | `unit/Benchmarks.cpp [Benchmarks]`; `metricsSystem.ts` (tick duration histogram/summary, Prometheus) | [src] |
| Recommendations for FalloutMP [inference] | Keep binary-only on the wire; add interest management inside the 3×3 area (distance-based movement rate); no echo to the sender; cache parsed appearance; incremental saves; validate before relay (also saves bandwidth on rejects) | – |

---

## Appendix A: message fields (Serialize order)

| Message | Fields |
|---|---|
| Activate | `data{caster u64, target u64, isSecondActivation bool}` |
| ChangeValues | `idx?`, `data{health?, magicka?, stamina?}` (0..1) |
| ConsoleCommand | `data{commandName, args: variant<int64,string>[]}` |
| CraftItem | `data{workbench, craftInputObjects: Inventory, resultObjectId}` |
| CreateActor | `idx, baseRecordType?, transform{worldOrCell,pos,rot}, isMe, props{…§1.5}, customPropsJsonDumps[{propName, propValueJsonDump}]` |
| CustomEvent | `argsJsonDumps[], eventName` |
| CustomPacket | `contentJsonDump` |
| DeathStateContainer | `tTeleport?, tChangeValues?, tIsDead?` |
| DestroyActor | `idx` |
| DropItem | `baseId u64, count` |
| FinishSpSnippet | `returnValue? (bool/double/string), snippetIdx i64` |
| Host | `remoteId u64` |
| HostStart / HostStop | `target u64` |
| OnEquip | `baseId` |
| OnHit | `data{aggressor, isBashAttack, isHitBlocked, isPowerAttack, isSneakAttack, projectile, source, target}` |
| OpenContainer | `target` |
| PlayerBowShot | `weaponId, ammoId, power, isSunGazing` |
| PutItem / TakeItem | `baseId, count, target` + `Inventory::ExtraData` (health, enchantmentId, maxCharge, removeEnchantmentOnUnequip, chargePercent, name, soul, poisonId, poisonCount, worn, wornLeft) |
| SetInventory | `inventory` |
| SetRaceMenuOpen | `open` |
| SpSnippet | `class, function, arguments[optional<variant<bool,double,string,{formId u64,type}>>], selfId u64, snippetIdx i64` |
| SpellCast | `data{caster, target, spell, isDualCasting, interruptCast, castingSource i32, aimAngle, aimHeading, actorAnimationVariables{booleans, floats, integers}}` |
| Teleport | `idx, pos, rot, worldOrCell`; Teleport2: `pos, rot, worldOrCell` |
| UpdateAnimVariables | `data{actorRemoteId, actorAnimationVariables}` |
| UpdateAnimation | `idx, data{animEventName, numChanges}` |
| UpdateAppearance | `idx, data?: Appearance` |
| UpdateEquipment | `idx, data{inv, leftSpell?, rightSpell?, voiceSpell?, instantSpell?, numChanges}` |
| UpdateGamemodeData | `eventSources[{name, content}], updateOwnerFunctions[], updateNeighborFunctions[]` |
| UpdateMovement | `idx, data{worldOrCell, pos[3], rot[3], direction, healthPercentage, speed, runMode="Standing", isInJumpState, isSneaking, isBlocking, isWeapDrawn, isDead, lookAt?}` |
| UpdateProperty | `idx, propName, refrId u32, dataDump (JSON string), baseRecordType?` |

## Appendix B: platform (SkyrimPlatform) surface the sync layer depends on

These are the APIs FalloutMP's platform must provide equivalents for [src: skyrim-platform/src/platform_se/skyrim_platform/EventHandler.h; PapyrusTESModPlatform.cpp].

| Need | SkyrimPlatform API |
|---|---|
| Events | `activate` (461), `containerChanged` (472), `equip`/`unequip` (480), `hit` (487), `playerBowShot` (511), `spellCast` (524), `update`, `tick`, `menuOpen/Close`, `sendAnimationEvent` hook |
| Natives (`TESModPlatform`) | `MoveRefrToPosition` (1007), `SetWeaponDrawnMode` (1012), `IsPlayerRunningEnabled` (1023), `CreateNpc` (1034), `EvaluateLeveledNpc` (1040), `AddItemEx` (1099), `ResetContainer` (1115), `BlockPapyrusEvents` (1120) |
| Other | `mpClientPlugin` (createClient/send/tick), `loadGame` (template save + ChangeFormNpc), `storage` (survives hot reload), `browser` (CEF UI), Inventory/Magic APIs, `Game.getFormEx`, Papyrus class bindings generated from `.psc` |

## Appendix C: bug and quirk list (quick reference)

1. `isDisabled`/`disabled` key mismatch (I5). 2. ESM-id actor UpdateProperty dropped (I6). 3. AV reset on load (I7). 4. Stamina not cropped (I8). 5. LVLI chance-none global (I17). 6. DropItem has no ExtraData [inference]. 7. `teleportFlag` dead code (AL:121-142). 8. `SendMessageToActorListeners` ignores `reliable` (I4). 9. Host log format bug (AL:685-686). 10. Hosted NPC ChangeValues use player AVs (I13). 11. `lastAnimation` not persisted. 12. Papyrus `SetPosition` not saved. 13. ToTuple omissions. 14. Docs/code property mismatch (I19). 15. Blocked `onEatItem` still consumes the item. 16. Relay before validation (I1). 17. SpSnippet player-only (I16). 18. Time mismatch (I18). 19. Leveled NPC variant not synced. 20. Death is not broadcast (I14). 21. `OnItemAdded` partial (I22). 22. No put/take correction (I12). 23. Trusted power/sneak (I9). 24. Reach check off (I9). 25. Spell/ingredient effects TODO (I10). 26. BitStream no bounds check (I20). 27. `OnEquip`/`PlayerBowShot` unreliable (I3). 28. Spell/anim relays unreliable (by design). 29. Activation-blocked not persisted. 30. No pickup distance check (I15). 31. No bench occupancy (I15). 32. Reliable ordering asymmetry (I2). 33. Hosted NPC movement never validated. 34. Cell transitions are server-authoritative (client world change → Teleport2 snap-back; load doors must go through `Activate`).
