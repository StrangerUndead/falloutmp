# Writing a plugin feature module

The plugin (`FalloutMP.dll`) connects the TypeScript client (`falloutmp-client`) to Fallout 4. The client calls *natives* by name. The plugin answers them, and it reports what the player does as *platform events*. The contract is `falloutmp-client/src/platform/falloutPlatform.ts`: every native and event is declared there with its argument types.

## Layout

| File | Role |
|---|---|
| `src/Main.cpp` | F4SE entry. At `kGameDataReady`: `Platform::Start`, `modules::InstallAll`, the player update hook |
| `src/Platform.h` | Native registry, event queue, async results, frame and save callbacks, config |
| `src/Config.h` | `FalloutMP.json` (server, profile id, puppet options, feature switches) |
| `src/GameUtil.h` | Form and reference lookup, spaces (worldspace/interior), angles, `CreateRef`, menus |
| `src/Papyrus.h` | `papyrus::CallMethod` / `CallStatic` with optional result callbacks |
| `src/Puppets.h` | Registry of puppets (other players), post-update callbacks after the engine's update |
| `src/ItemKeys.h` | Protocol items (base + mods + condition) ↔ game inventory |
| `src/modules/Modules.h` | One `Install<Name>(Platform&)` per feature, with its natives |

## Rules

1. **Natives.** `p.RegisterNative("name", [](const Json& args) -> Json {...})`. `args` is a JSON array in the TypeScript parameter order. Return `nullptr` for `void` or `undefined`, else the value in the TypeScript shape. Throwing turns into a script exception, so check the arguments with `.at()`.
2. **Events.** `p.Emit("eventName", data)` with the payload shape of `PlatformEvents`. Emit is thread safe; events are delivered on the next frame.
3. **Async natives** (those returning `Promise` in TypeScript): return `Platform::Pending(id)` with `id = p.NewAsyncId()`, and call `p.Resolve(id, value)` later, from any thread.
4. **Threads.** Natives and `OnFrame` callbacks run on the main thread, inside `PlayerCharacter::Update`. Event sinks and Papyrus callbacks may run on other threads. Never touch game objects there: copy what you need and `p.QueueTask(...)` the rest. The `done` callbacks of `papyrus::Call*` already run on the main thread.
5. **Papyrus.** Pass every parameter, defaults included. The signatures are in the Fallout 4 `.psc` files (vanilla, plus F4SE additions). A wrong count fails silently, with an error in the Papyrus log only.
6. **Engine functions.** Use CommonLibF4 (`RE::`, `REL::ID`), pinned in `CMakeLists.txt` (`COMMONLIBF4_COMMIT`). Every member and function you call must exist in that version's headers. Prefer, in order:
   1. CommonLibF4 functions (`REL::ID` relocations);
   2. vtable hooks (`REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE::X[0] }; vtbl.write_vfunc(idx, Thunk)`);
   3. event sinks (`RE::BSTEventSink<E>`, sources from `GetEventSource` / `RegisterSink`);
   4. Papyrus calls.

   Call-site hooks (`REL::GetTrampoline().write_call<5>`) need an exact address. Use them only with an ID and offset you can justify.
7. **No `windows.h`.** CommonLibF4 declares its own Win32 API in `REX::W32`, and `REX::ERROR` clashes with the `ERROR` macro. Log with `REX::INFO/WARN/ERROR/DEBUG` or `p.Log`.
8. **Puppets.** Other players are puppets (`puppets::IsPuppet`). Modules that change remote actors must only touch puppets, never arbitrary references a script names.
9. **Saves.** Network-only references (puppets, server-placed objects) must not be saved. Remove them in `p.OnBeforeSave` and restore them in `p.OnAfterSave`, or let the client recreate them; the client gets `saveFinished`.
10. **Unknowns.** Mark anything that can only be confirmed in game with `// [verify]` and a short note on what to check. The probe module and STATUS.md collect these.

## Testing

The plugin builds only on Windows, in the workflow *FalloutMP client (Windows)*. The portable core and the client script are tested on Linux: `unit/Fo4ClientCoreTest.cpp`, `falloutmp-client` `npm test`, and `fallout4-platform/tools/fmp_e2e.sh`.
