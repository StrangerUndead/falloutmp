# PVM — papyrus-vm Fallout 4 Support & Server Natives

Context: [reference/fo4-papyrus-pex-vm.md](../reference/fo4-papyrus-pex-vm.md) (PEX layout, opcodes 0x24–0x2E, type strings, events, per-file plan, 14 existing defects); [reference/papyrus-api-map.md](../reference/papyrus-api-map.md) §3–4 (server natives P0/P1/P2, events).

Each running VM serves a single game: root class, event semantics and natives differ. The library supports both formats.

- [ ] **PVM-001** Reader: magic/endianness detection (Skyrim BE `FA57C0DE` / FO4 LE), version and game-id validation, bounds-checked reads; ignore the object size field; script name taken from the object name — M — Depends: REF-002 — Verify: L-fixture
- [ ] **PVM-002** FO4 object table: const byte on objects/variables, struct table, debug sections (property groups, struct orders) — M — Depends: PVM-001 — Verify: L-fixture (golden PEX)
- [ ] **PVM-003** `VarValue`: `Var` (boxed), `Struct` (reference type, named members, None default), Var arrays, struct arrays; case-insensitive type names (`script#Struct`) — L — Depends: PVM-002 — Verify: L-unit
- [ ] **PVM-004** Opcodes 0x24–0x2E (`is`, `struct_create/get/set`, `array_findstruct/rfindstruct`, `array_add/insert/removelast/remove/clear`) with the exact operand order from the reference — L — Depends: PVM-003 — Verify: L-unit
- [ ] **PVM-005** `ScriptObject` root: parent-chain native lookup reaching ScriptObject; ship our own `ScriptObject.pex` stub — S — Depends: PVM-002 — Verify: L-unit
- [ ] **PVM-006** Namespaced scripts (`A:B` → `A/B.pex`) in `CIMap` lookups and the directory/BA2 script storages — S — Depends: PVM-001 — Verify: L-unit
- [ ] **PVM-007** FO4 event model: remote events (`::remote_<Type>_<Event>` handlers, `RegisterForRemoteEvent`), custom events (`SendCustomEvent`, string-literal names `"Script_Event"`), registration-gated events (OnHit single-shot + filters; OnItemAdded inventory filters), native-script event declaration rule — L — Depends: PVM-004, PVM-005 — Verify: L-unit
- [ ] **PVM-008** Timers: `StartTimer`/`CancelTimer`/`StartTimerGameTime`/`CancelTimerGameTime`/`OnTimer`/`OnTimerGameTime`, driven by the server clock (F25) — M — Depends: PVM-005 — Verify: L-unit
- [ ] **PVM-009** `CallFunction`/`CallFunctionNoWait`/`GetPropertyValue`/`SetPropertyValue`/`CastAs`/`GetState`/`GotoState` on ScriptObject with `Var[]` — M — Depends: PVM-003 — Verify: L-unit
- [ ] **PVM-010** Golden FO4 PEX fixtures (Caprica-compiled, committed with sources) + byte-level fixture-free tests + behaviour suites — M — Depends: ENV-013 — Verify: L-fixture
- [ ] **PVM-011** Fix the existing papyrus-vm defects listed in the reference (opcode enum mismatch at 0x22, locals pushed into params, unchecked `arrayBytes`, etc.) — M — Depends: PVM-001 — Verify: L-unit
- [ ] **PVM-012** FO4 standard script stubs for the server: our own minimal `.psc`/`.pex` for Form, ObjectReference, Actor, Quest, … with native signatures only (no Bethesda bodies) — M — Depends: BUILD-005 — Verify: L-unit — Files: skymp5-server/standard_scripts_fo4/
- [ ] **PVM-013** Server natives **P0** (MVP) from papyrus-api-map §3: ScriptObject registration/timers, Form basics, ObjectReference core (Enable/Disable/Delete/MoveTo/PlaceAtMe/AddItem/RemoveItem/GetItemCount/Activate/Lock state), Actor core with `GetValue/SetValue/ModValue/DamageValue/RestoreValue(ActorValue)`, Game.GetPlayer/GetForm/GetFormFromFile, Debug.Notification/MessageBox/Trace, Utility.Wait/Random*, **Math** (all), **GlobalVariable** (Get/Set/Mod), Quest stage (fix the `GetCurrentStageID` stub) — L — Depends: PVM-005, REF-011 — Verify: L-unit
- [ ] **PVM-014** Server natives **P1** (parity): items/OMOD (`AttachMod`, `RemoveMod`, `AttachModToInventoryItem`), Message.Show, FormList, Keyword, Faction, Cell/Location, Sound, Perk queries, Container/Door/Furniture/Terminal hooks, and events OnDeath/OnDying/OnKill fired by the server — L — Depends: PVM-013 — Verify: L-unit
- [ ] **PVM-015** Server natives **P2** (later): Scene/Topic stubs, InputEnableLayer (as client SpSnippet), Workshop-related natives for gamemode scripts, F4SE-style helpers we choose to emulate — M — Depends: PVM-014 — Verify: L-unit
- [ ] **PVM-016** SpSnippet bridge for FO4 client-visible natives (sound, effects, notifications, InputEnableLayer) routed to owner/hoster (I16) — M — Depends: SRV-040 — Verify: L-unit
