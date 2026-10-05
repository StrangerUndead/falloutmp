# F31 — Names, Nameplates, Custom Properties & Gamemode Extensibility

| Field | Value |
|---|---|
| Tier | T0 |
| Target level | L3 for nameplates and display names (SkyMP L3); L4 for custom properties and event sources (SkyMP L4) |
| SkyMP analogue | Nameplates drawn over `"NPC Head [Head]"` with `worldPointToScreenPoint` + `createText`, F1 toggle (`skymp5-client/src/view/formView.ts:546-590`, `browserService.ts:25-29`); `displayName` with the `%original_name%` sentinel (`remoteServer.ts:323-337`, `PapyrusObjectReference.cpp:874-897`, `MpChangeForms.h:123`); property system and signed client functions (reference/skymp-sync-inventory.md §1.11). SkyMP level L3 / L4 |
| Milestone | M4 (`ctx.sp` = falloutPlatform, CLI-071), M5 (nameplates, display names), M6 (I5/I6 regression, property limits), M12 (DOCS-003 shape registry complete) |
| Workstreams | CLI, PLAT, SRV, GM, DOCS, FRONT |
| Depends on | PLAT-011 (Text), PLAT-062 (`worldPointToScreenPoint`), PLAT-088 (node world position), PLAT-070 (per-player runtime TESNPC), CLI-030, CLI-031, CLI-070, CLI-071, NET-003, NET-008, F03 (`appearance.name`), DOCS-003 |
| References | reference/skymp-sync-inventory.md §1.11, §1.12, §1.14, §2 ("Display names / nicknames", "Custom properties", "Enabled / disabled" rows), §3.2 I5, I6, I19; reference/papyrus-api-map.md §0 item 3, §2.4 (`setDisplayName`, `getName/setName` rows); reference/commonlib-port-map.md §4.11; reference/skyrim-coupling-index.md (gamemodeUpdateService row, gamemode JSON shapes note); reference/prior-art.md §3.4 |

## 1. Summary
Every player has a name. It is shown above their head when you are close enough, they are in line of sight, and they are not sneaking; F1 toggles it, as in SkyMP. Servers can rename any player, NPC or object for everyone (`displayName`, including `%original_name%`). Gamemode authors extend the game exactly as on SkyMP:
- `mp.makeProperty` defines synced per-object state with client-side render code;
- `mp.makeEventSource` defines client-side code that reports events to the server.

On Fallout 4, `ctx.sp` is the `falloutPlatform` module. Client code arrives signed and is verified before it runs. FalloutMP fixes SkyMP's two property bugs (I5, I6) and publishes the JSON shape of every FO4 built-in property, so gamemodes have a stable API (DOCS-003).

## 2. Vanilla Fallout 4 behaviour (engine facts that matter for sync)
- **Names:**
  - names live on the base form (`TESFullName`);
  - F4SE adds `Form.GetName()`/`SetName(string)`;
  - vanilla FO4 and F4SE have **no `ObjectReference.SetDisplayName`** (papyrus-api-map §1.2 ObjectReference note, §2.4);
  - a per-reference name needs `ExtraTextDisplayData` written by a native [src: libxse IDs_VTABLE.h:6699; include/RE/E/ExtraDataList.h:125];
  - remote players have unique runtime TESNPC bases (PLAT-070), so `SetName` on the base names exactly one actor.
- **Head node:** FO4 skeleton node names differ from Skyrim's (`"Head"`, not `"NPC Head [Head]"`) (commonlib-port-map §4.11) [inference: verify for human, power armor and creature skeletons with G-self].
- **Projection:** `NiCamera::WorldPtToScreenPt3` NG/AE 2270344 (PLAT-062). F4MP draws HUD markers with `HUDMenuUtils::WorldPtToScreenPt3` (prior-art §3.4).
- **Crosshair name:** the HUD already shows the crosshair target's name in its activate prompt, so a renamed base shows up there too [inference].
- **Keys:** F1 has no vanilla binding on PC [inference: check the default `ControlMap`]. It is configurable (CLI-060).

## 3. SkyMP baseline
- **Nameplates** (`formView.ts:546-590`), drawn when all of these hold:
  - `FormView.isDisplayingNicknames` (F1);
  - `appearance.name` is set;
  - distance ≤ 1000 u and `hasLOS`;
  - not sneaking;
  - no `SweetHidePerson` keyword worn.
- **Nameplate rendering:**
  - head node + 32 u projected with `worldPointToScreenPoint`;
  - `createText` at 0.5 size;
  - text ids kept in `sp.storage` for hot reload (`sweetTaffyNicknamesService.ts`).
- **SweetPie variant:** `SweetHidePlayerNamesService` renames others to "Stranger" per listener on stream-in.
- **Display names:**
  - `props.displayName` is applied with `refr.setDisplayName(name.replace('%original_name%', baseName), true)`;
  - Papyrus `SetDisplayName` stores the field and sends a SpSnippet to listeners;
  - there is no `displayName` property binding.
- **Properties** (§1.11):
  - built-ins live in `PropertyBindingFactory.cpp:31-77`;
  - custom properties go into `dynamicFields`;
  - `private.*` is never sent, and `private.indexed.*` is searchable;
  - `updateOwner`/`updateNeighbor` are shipped in `UpdateGamemodeData` and verified (`// skymp:sig:y:<key>:<sig>`, `serverJsVerificationService.ts`);
  - they run each frame with `ctx.{sp, value, refr, get, state, respawn, getFormIdInServerFormat, getFormIdInClientFormat, getMyFormIdInServerFormat}`;
  - event sources run once with `ctx.sendEvent` → `CustomEvent` (15) `_<name>` (`gamemodeEventSourceService.ts:75-120`).
- **Bugs:** I5 (the client reads `disabled` instead of `isDisabled`) and I6 (UpdateProperty for ESM-id actors is dropped).

## 4. Design

### 4.1 Authority model
- **Class A:** character name (F03), `displayName`, custom property values, property definitions and their client code.
- **Class D:** nameplate rendering and the F1 toggle, each client's evaluation of `updateOwner`/`updateNeighbor`.
- **Event sources** produce intents. The gamemode validates them on the server (class A).

### 4.2 Server state & persistence
| State | Type | Where (class/field) | Persisted (ChangeForm field) | Default source |
|---|---|---|---|---|
| Character name | string | `appearance.name` (F03) | yes (`appearanceDump`) | character creation |
| Display name | optional string (may contain `%original_name%`) | `MpChangeFormREFR.displayName` | yes | none (base name) |
| Custom property values | JSON | `dynamicFields` | yes | — |
| `private.*` / `private.indexed.*` | JSON | `dynamicFields` (never sent / indexed) | yes | — |
| Property definitions, event sources | flags + signed JS | gamemode → `UpdateGamemodeData` | no (code) | — |
| Name rules | `names.{maxLength: 32, charset, unique, profanityHook}` | server settings | no | defaults |
| Nameplate policy | `{maxDistance: 1000, requireLos, hideWhenSneaking, hideKeyword: "FMP_HidePerson"}` | client defaults; server override via a gamemode owner property | no | client config |

### 4.3 Protocol
| Message | Dir | Fields | Reliability | Rate / trigger | New or reused |
|---|---|---|---|---|---|
| `UpdateProperty` (7) `displayName` | S→C | `{refrId (long id for ESM actors, I6), propName, dataDump}` | R | on change | reused (replaces SkyMP's listener SpSnippet for FO4) |
| `CreateActorFo4` (64) | S→C | `props.displayName`, `customPropsJsonDumps[]` | R | stream-in | reused |
| `UpdateProperty` (7) custom | S→C | per the `makeProperty` flags | R | on `mp.set` | reused |
| `UpdateGamemodeData` (32) | S→C | property definitions, event sources, signed sources | R | connect + hot reload | reused |
| `CustomEvent` (15) | C→S | `eventName` (`_`-prefixed), `argsJsonDumps[]` | R | event source | reused |

No new message IDs. The protocol is SkyMP's, which is the point of this spec.

### 4.4 Client capture (owner side)
- **Names:** nothing captured. Names are server-driven.
- **Event sources:** gamemode code subscribes through `ctx.sp.on(...)` (falloutPlatform events: `menuOpen`, `browserMessage`, `activate`, …) and calls `ctx.sendEvent(...)`.
- **Hot reload:** registered sources live in `sp.storage['eventSourceContexts']` (SkyMP pattern), and expired contexts are neutralized.

### 4.5 Apply (owner correction, remote rendering, stream-in, respawn, reconnect)
**Display name** (`applyDisplayName(ref, raw)`):
1. Replace `%original_name%` with `getBaseObject().getName()` (F4SE).
2. If the ref is a remote player (unique runtime base), call F4SE `SetName` on the base.
3. Otherwise (ESM NPCs and objects with shared bases), call the native `setRefDisplayName(ref, name)` (`ExtraTextDisplayData`).
4. Re-apply after any base recreation (F03) or 3D reload.

**Nameplates** (`nameplates.ts`; for each remote actor with a name, every frame, throttled to 30 Hz):
- Conditions: F1 on; distance ≤ `maxDistance`; LOS (`Actor.HasDetectionLOS` [inference: confirm cost]); `sneaking` flag (F01) off; `hideKeyword` not worn; no menu from the F28 policy open; overlay visible.
- Position:
  - `getNodeWorldPosition(actor, "Head")` (PLAT-088) + 32 u, or + 48 u in power armor;
  - fallback: actor position + 128 u.
- Draw: project with `worldPointToScreenPoint` (PLAT-062), and draw with `createText` (PLAT-011) or the front HTML layer when FRONT-006 styling is on.
- Hidden when screen z < 0. Text ids are stored in `sp.storage` for hot reload.

**Custom properties** (FO4 `GamemodeUpdateService` and `GamemodeEventSourceService`, CLI-071):
- the same `ctx` as SkyMP, with `ctx.sp = falloutPlatform`, plus `ctx.game = "fallout4"`;
- functions are verified with the target peer's `publicKeys` before `new Function('ctx', src)`;
- an unsigned source is refused whenever keys are configured;
- the signature line format stays `// skymp:sig:y:<keyId>:<sig>` (upstream-compatible).

**Stream-in, reconnect:** `CreateActorFo4.props` gives `displayName` and every visible custom property. Owner-visible properties re-apply on reconnect.

**I5/I6 fixes** (CLI-030, CLI-031):
- `isDisabled` is read under the server key, and an ESM disable sends `UpdateProperty`;
- `UpdateProperty` for ESM-id actors is routed by long id, so `displayName`, `isDead`, `isHostedByOther` and custom properties reach FormViews of ESM NPCs.

### 4.6 Validation & anti-cheat
- **Who writes:** `displayName` and properties are written only by server code (gamemode `mp.set`, Papyrus, admin console). Clients cannot set them.
- **Name rules** (shared helper `validateDisplayName`, also used by F03 character creation):
  - length 1…`names.maxLength`;
  - letters, digits, space, `-`, `'`, `.`;
  - no control characters, bidi overrides or markup;
  - optional uniqueness among profiles;
  - `onSetName(actorId, name)` [blockable];
  - a rejected name is returned to the caller as an error, and F03 shows it in the creation UI.
- **Property limits (S20, NET-008):**
  - a value is ≤ `properties.maxValueBytes` (16 KB) serialized, otherwise `mp.set` throws;
  - `makeProperty` on a built-in name throws;
  - property names match `^[A-Za-z_][A-Za-z0-9_.]{0,63}$`.
- **Event sources:**
  - names must start with `_` (existing check);
  - ≤ `customEvents.maxArgsBytes` (8 KB);
  - ≤ `customEvents.maxPerSec` (20) per user and event. The excess is dropped and counted in `custom_event_drop_total`. Events carry no state, so there is nothing to correct.
- **Signatures:**
  - the server signs gamemode client code with its key (OpenSSLSigner);
  - the client refuses bad or missing signatures and logs the reason;
  - this blocks a malicious relay from injecting code.

### 4.7 Audience / visibility
- `displayName`: all listeners of the ref (grid).
- Custom properties: owner and/or neighbours per `isVisibleByOwner`/`isVisibleByNeighbors`. `private.*` goes to nobody (S8).
- `UpdateGamemodeData`: every connected user.

### 4.8 NPC parity
- ESM-id NPCs share bases, so their names go through `setRefDisplayName` (ExtraTextDisplayData), not `SetName`.
- Properties on NPCs reach the host and every other listener, which requires the I6 fix.
- Nameplates on NPCs are off by default (`nameplates.npcs = false`). A gamemode may enable them for named NPCs.

### 4.9 Gamemode API & server Papyrus
- **`displayName`:** new built-in binding `DisplayNameBinding`, get/set. Set writes the ChangeForm and sends `UpdateProperty`. Setting `null` restores the base name.
- **Unchanged:** `mp.makeProperty`, `mp.makeEventSource`, `mp.get/set`, `mp.findFormsByPropertyValue`.
- **New `mp.getGame()`:** returns `"fallout4"` or `"skyrim"`, for cross-game gamemodes [skyrim-coupling-index inference].
- **Event:** `onSetName` [blockable].
- **FO4 property shape registry (DOCS-003):**
  - lives in `skymp5-server/ts/gamemodeApi/fo4PropertyShapes.ts` as JSON Schema;
  - covers every built-in: `appearance` (F03), `equipment` (F05), `inventory` (F04, entries with OMOD lists), `percentages` and `actorValues` (F08), `powerArmor` (F17), `progression` (F19), `discoveredMarkers`/`travelMarkers` (F26), `lightState`/`radioState` (F28), `displayName` (here);
  - each owning spec defines its shape, and F31 keeps the registry plus a test that each binding's `Get` output validates against it.
- **Papyrus:** `ObjectReference.SetDisplayName(string, bool)` is a FalloutMP extension. It is declared in our FO4 `ObjectReference` stub (PVM-012) and maps onto the binding. F4SE `Form.SetName` on the server applies only to runtime player bases; on ESM bases it is logged and ignored.

### 4.10 Edge cases & failure modes
- **`%original_name%` on a nameless base:** falls back to the raw string without the sentinel, and the error is logged (SkyMP behaviour).
- **Power armor enter/exit:** the skeleton and head node change, so the node is looked up again every frame.
- **Resolution changes and ultrawide screens:** `getScreenResolution` is read each frame.
- **`show-me` clones:** these debug clones skip nameplates.
- **Gamemode hot reload:** property functions are re-evaluated and `ctx.state` resets. Old event-source contexts are expired (`_expired`).
- **A property function throws every frame:** it is disabled after 100 consecutive errors, with a log line, so one bad property cannot stall the client.

### 4.11 Performance budget
- Nameplates: ≤ 0.02 ms per visible actor, at most 50 drawn (nearest first).
- Property functions: a 0.05 ms per function per frame warning threshold, reported by the profiling service (CLI-040).
- `UpdateProperty` ≤ 16 KB per value. Typical name updates are < 64 B.

## 5. Engine / platform work required
- PLAT-062 `worldPointToScreenPoint`.
- PLAT-088 `getNodeWorldPosition(ref, node)`.
- PLAT-011 `createText`/`setTextPos`/`setTextSize`/`destroyText`.
- `setRefDisplayName(ref, name)` (ExtraTextDisplayData native).
- F4SE `Form.SetName`/`GetName` through reflection.
- `getScreenResolution`.

## 6. Tests
- `L-unit` `[F31]`:
  - `DisplayNameBinding` get/set;
  - `UpdateProperty` to listeners (long id for ESM actors);
  - `CreateActorFo4` carries `displayName`;
  - Papyrus `SetDisplayName` → binding;
  - name rules (each reject) and `onSetName` veto;
  - property value size cap, built-in name collision, event rate cap;
  - persistence round trip of `displayName` and `dynamicFields`.
- `L-unit` `[F31][Shapes]`: each FO4 built-in binding's JSON validates against `fo4PropertyShapes`.
- `L-ts`:
  - nameplate visibility rules (distance, LOS, sneak, keyword, menu) with a mocked platform (CLI-050);
  - `%original_name%` substitution;
  - signature verification (good, bad, missing, unknown key);
  - `ctx.sp` is falloutPlatform;
  - the I5 and I6 regression cases (CLI-030/031).
- `L-int`: a gamemode property with `updateNeighbor` reaches bot B but not bot C out of range; a renamed ESM NPC shows the new name on a late-joining bot.
- `G-self`: head node names for human, PA and two creatures; `SetName` on a clone base; `ExtraTextDisplayData` on a shared-base NPC.
- `G-manual`: two players see each other's nameplates and toggle with F1; sneaking hides the plate; an admin renames a player and both see it.

## 7. Tasks
- [ ] **F31-T01** Nameplate renderer FO4 (port of `formView.ts:546-590`): head node + PA fallback, visibility rules, F1 toggle, hide in F28 menus, `sp.storage` text-id cleanup — M — Depends: PLAT-011, PLAT-062, PLAT-088, F01-T06 — Verify: L-ts, G-manual — Files: falloutmp-client/src/view/nameplates.ts, falloutmp-client/src/view/formView.ts
  - Accept: the G-manual nameplate scenario passes in 1st and 3rd person.
- [ ] **F31-T02** Display-name apply: `%original_name%`, F4SE `SetName` on the clone base, `setRefDisplayName` native for shared bases, re-apply after base recreation — S — Depends: PLAT-070, PLAT-031 — Verify: W-ci, G-self — Files: falloutmp-client/src/view/displayName.ts, fallout4-platform/src/.../NameApi.cpp
- [ ] **F31-T03** Server `DisplayNameBinding` + `UpdateProperty` to listeners + `CreateActorFo4` prop + Papyrus `SetDisplayName` extension (FO4 profile) — S — Depends: NET-003, PVM-012 — Verify: L-unit — Files: skymp5-server/cpp/addon/property_bindings/DisplayNameBinding.{h,cpp}, PropertyBindingFactory.cpp, skymp5-server/cpp/server_guest_lib/script_classes/PapyrusObjectReference.cpp, unit/DisplayNameTest.cpp
  - Accept: the `[F31]` binding cases pass. The Skyrim SpSnippet path is unchanged.
- [ ] **F31-T04** Name rules helper `validateDisplayName`, `names.*` settings, `onSetName` event (shared with F03) — S — Depends: F31-T03 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/NameRules.{h,cpp} (new), gamemode_events/SetNameEvent.cpp
- [ ] **F31-T05** `ctx.sp = falloutPlatform` + `ctx.game` in `GamemodeUpdateService`/`GamemodeEventSourceService` (implements CLI-071); signature verification kept; signed-source tests — S — Depends: CLI-001, CLI-050 — Verify: L-ts — Files: falloutmp-client/src/services/services/{gamemodeUpdateService,gamemodeEventSourceService,serverJsVerificationService}.ts
- [ ] **F31-T06** I5/I6 regression suite on top of CLI-030/CLI-031: `isDisabled`, `isDead`, `displayName` and custom properties on ESM-id NPCs — S — Depends: CLI-030, CLI-031 — Verify: L-ts, L-int
  - Accept: a late-joining bot sees an ESM NPC's disabled state and custom property.
- [ ] **F31-T07** Property and event-source limits (value size, name pattern, built-in collision, per-event rate, function error disabling) — S — Depends: NET-008 — Verify: L-unit, L-ts — Files: skymp5-server/cpp/addon/ScampServer.cpp, CustomPropertyBinding.cpp, ActionListener.cpp
- [ ] **F31-T08** FO4 property shape registry + `mp.getGame()` + DOCS-003 generation and validation tests — M — Depends: DOCS-003, per-feature bindings — Verify: L-unit, L-int — Files: skymp5-server/ts/gamemodeApi/fo4PropertyShapes.ts, unit/PropertyShapesTest.cpp, docs/falloutmp/reference/gamemode-api-fo4.md
- [ ] **F31-T09** (T2) Per-listener name policy: generalize `SweetHidePlayerNamesService` into a gamemode hook (e.g. "Stranger" until introduced), Skyrim behaviour kept behind the SweetPie gate — M — Depends: F31-T03 — Verify: L-unit — Files: skymp5-server/cpp/server_guest_lib/SweetHidePlayerNamesService.cpp, NamePolicyService.{h,cpp} (new)
- [ ] **F31-T10** Front nameplate styling option (FRONT-006) + G-manual script — S — Depends: F31-T01 — Verify: G-manual — Files: docs/falloutmp/test-scripts/F31-names.md

## 8. Open questions & risks
- The `HasDetectionLOS` cost for up to 50 actors per frame is unknown. If it is too slow, run LOS at 5 Hz per actor and cache it.
- Should the signature prefix stay `skymp:sig` for upstream compatibility, or become `falloutmp:sig`? Keeping it is proposed.
- Name uniqueness across profiles needs an indexed lookup (`private.indexed.name`). Decide whether to make it the default.
