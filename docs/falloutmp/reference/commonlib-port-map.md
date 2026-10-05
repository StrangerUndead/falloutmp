# CommonLibSSE-NG → CommonLibF4 port map for SkyrimPlatform

> Purpose: a symbol-by-symbol map for porting `skyrim-platform` (SP) from SKSE + CommonLibSSE-NG to F4SE + CommonLibF4, written for the Claude Code session that will implement "FalloutPlatform".
> Orientation and strategy are in `docs/FALLOUT4_PORT_RESEARCH.md`. This file does not repeat them.

## 0. Conventions, sources, versions

**Provenance marks**
- `[src: path:line]`: verified in code. The repo paths are relative to `/home/user/falloutmp`. The library paths use the prefixes below.
- `[web: URL]`: taken from an external page.
- `[inference]`: my reasoning, not verified. Each one comes with a way to verify it.

**Library prefixes** (clones in the session scratchpad; pin these commits)

| Prefix | Repo | Commit | Notes |
|---|---|---|---|
| `lx/` | libxse/commonlibf4 | `7c8c6f8` (2026-09-22) | AE 1.11.x. xmake only, C++23, one header per class (`include/RE/<Letter>/<Class>.h`). Its submodule `lib/commonlib-shared` is pinned to `29fbdb0`. |
| `cs/` | libxse/commonlib-shared | HEAD `987147b`; lx pins `29fbdb0` | Holds `REL::` and `REX::`. Has a **CMakeLists.txt and a vcpkg.json**. The pinned and HEAD commits differ only in `REX/T*Setting*` [src: diff of cs vs cs_pinned]. |
| `al/` | alandtse/CommonLibF4 | `ba22620` | OG + NG + VR. CMake/vcpkg. Header paths are `al/CommonLibF4/include/RE/Bethesda/*.h`. Uses `REL::RelocationID(OG, NG)`. |
| `f4se/` | ianpatt/f4se | `6f6a7ca` | F4SE 0.7.9. All `RelocAddr` RVAs target runtime **1.11.240** [src: f4se/f4se_common/f4se_version.h:85-93]. |
| `te/` | tiltedphoques/TiltedEvolution (FO4 code) | `b850014` | `POINTER_FALLOUT4(...)` entries are **OG 1.10.163** Address Library IDs (summary list in `scratchpad/te_fo4_pointers.txt`). |

**Status vocabulary**
- **Same**: same name and semantics, so it is a mechanical rename of the include only.
- **Renamed**: same concept under a different name or signature.
- **Diff-sem**: exists, but behaves or is used differently.
- **Missing-RE**: the concept exists in FO4 but lx has no symbol or ID for it, so it needs reverse engineering.
- **N/A**: Skyrim-only, so drop it or replace it with something else.

**ID spaces** (this matters for every offset below)
- lx `include/RE/IDs.h` uses the NG/AE ID space: functions are about 2.19M–2.28M and data about 4.79M–4.80M. The same numbers appear as the second (NG) argument of al's `REL::RelocationID(OG, NG)`. For example, `TESDeathEvent::GetEventSource` is `{1465690, 2201833}` in al [src: al/.../Events.h:585] and `2201833` in lx [src: lx/include/RE/IDs.h:2120].
- lx `IDs_RTTI.h` has also been converted to AE IDs (`Actor{4839606}`).
- **lx `IDs_VTABLE.h` still holds OG-space IDs.** They are identical to al's, for example `BSScript__Internal__VirtualMachine{614600,413974,1464591,1058438}` [src: lx/include/RE/IDs_VTABLE.h:3233 vs al/.../VTABLE_IDs.h:3205].
  - lx's own `IDs_RTTI.h` was renumbered, so whether these vtable IDs resolve in the AE bin is **unverified**. `REL::ID::offset()` calls `REX::FAIL` on a missing ID [src: cs/src/REL/IDDB.cpp:425-449].
  - Treat every `VTABLE::X[n]` as **verify-on-target** (risk item R1, §2.4).
- `te/` gives OG IDs. They are useful for finding a function in a 1.10.163 binary and then porting it to AE by signature or xref.

**The Address Library file lx loads**
- The file is `Data/F4SE/Plugins/version-<maj>-<min>-<build>-0.bin`, for example `version-1-11-191-0.bin`. It must sit in the same folder as the DLL that contains the commonlib code [src: cs/src/REL/IDDB.cpp:129-176].
- The format is "V0": a raw `{u64 id, u64 offset}[]` table preceded by a u64 count [src: cs/src/REL/IDDB.cpp:170-171, 207-221].
- See §3.6 for why this breaks SP's two-DLL layout.

---

## 1. Symbol map: every RE:: / SKSE:: / REL:: / Offsets:: use, by subsystem

### 1.0 Scope and how the list was produced

- All 213 `.cpp/.h/.hpp` files under `skyrim-platform/src` were scanned for `RE::`, `SKSE::`, `REL::`, `RELOCATION_ID`, `REL::VariantOffset` and `Offsets::`. That gave 1406 hits and 370 distinct qualified symbols (script output kept in `scratchpad/work/syms.tsv`).
- **`src/tilted/**` and `src/platform_lib/**` have zero `RE::/SKSE::/REL::` uses.**
  - Their engine coupling is limited to Win32/D3D/DInput IAT hooks and one CRT import hook (§5).
  - The only way they reach the engine is through `SkyrimPlatformApp::GetMainAddress()`, which uses `Offsets::WinMain` [src: skyrim-platform/src/platform_se/skyrim_platform/main.cpp:435-439].
- **`skymp5-server/cpp/client/main.cpp` has zero game symbols.** It is only the SLikeNet/JSON `MpClientPlugin.dll` [src: skymp5-server/cpp/client/main.cpp:1-83]. It is portable; only its install path `Data/SKSE/Plugins/` changes [src: skyrim_platform/MpClientPluginApi.cpp:27].
- SP aliases in `PCH.h`: `IVM=RE::BSScript::IVirtualMachine`, `VM=…Internal::VirtualMachine`, `StackID=RE::VMStackID`, `Variable`, `FixedString=RE::BSFixedString`, `TypeInfo`, `logger=SKSE::log`, `stl=SKSE::stl` [src: skyrim_platform/PCH.h:78-88]. Below, `sp/` means `skyrim-platform/src/platform_se/skyrim_platform/`.
- SP relies on **four local patches to CommonLibSSE-NG**. Each one needs an FO4 answer [src: overlay_ports/commonlibsse-ng-flatrim/patches/01-04]:

| SP patch | FO4 (lx) situation |
|---|---|
| 01 make `TESObjectREFR::MoveTo_Impl` public | lx has no MoveTo_Impl. Use F4SE `MoveRefrToPosition(TESObjectREFR*, u32* targetHandle, TESObjectCELL*, TESWorldSpace*, NiPoint3* pos, NiPoint3* rot)` at RVA `0x01181020`@1.11.240 [src: f4se/f4se/GameReferences.h:29, GameReferences.cpp:14]. OG ID `1332435` ("InternalMoveTo") [src: te_fo4_pointers.txt]. **Missing-RE** for the AE ID. |
| 02 make `Variable` members public | lx `Variable::varType/value` are private. Use `operator=`, `is<T>()`, `BSScript::get<T>()` or `detail::variable_raw_accessor` [src: lx/include/RE/B/BSScript_Variable.h:21-240]. No patch needed. |
| 03 add `StackFrame::args[0]` | Not needed. Use `Stack::GetStackFrameVariable(frame, idx, page)` (ID) and `StackFrame::GetPageForFrame()` [src: lx/include/RE/B/BSScript_Internal_Stack.h:59-70; lx/src/RE/B/BSScript_StackFrame.cpp]. |
| 04 make `ExtraDataList::_extraData/GetByTypeImpl/MarkType` public | lx `ExtraDataList` is ref-counted (`BSIntrusiveRefCounted`, size 0x28) and already has public `AddExtra`/`GetByType`/`HasType` [src: lx/include/RE/E/ExtraDataList.h:27-217]. Not needed. |

### 1.1 Plugin loader / init / versioning

| SP symbol | SP usage | FO4 in lx (header :: name) | al if different | Status | Notes |
|---|---|---|---|---|---|
| `SKSEPlugin_Load` (entry DLL export) | sp_entry `skyrim_platform_entry/main.cpp:113-121` | `F4SE_PLUGIN_LOAD(const F4SE::LoadInterface*)` macro = `extern "C" __declspec(dllexport) bool F4SEPlugin_Load(...)` [lx/include/F4SE/Interfaces.h:575-577] | `F4SE::LoadInterface` in al/.../F4SE/Interfaces.h | Renamed | F4SE 0.7.x needs no Query function. |
| plugin version data (`target_commonlibsse_properties(... NAME AUTHOR)` generates `SKSEPlugin_Version`) | `platform_se/CMakeLists.txt:130-133` | `F4SE_PLUGIN_VERSION = []{ F4SE::PluginVersionData v{}; v.PluginVersion(...); v.PluginName(...); v.AuthorName(...); v.UsesAddressLibrary(true); v.IsLayoutDependent(true); v.CompatibleVersions({F4SE::RUNTIME_LATEST}); return v; }();` [lx/res/commonlibf4-plugin.cpp.in:3-15; struct lx/include/F4SE/Interfaces.h:477-573] | — | Renamed | Must export the **data symbol** `F4SEPlugin_Version` (0x45C bytes). `UsesAddressLibrary` sets the bit `kAddressIndependence_AddressLibrary_1_11_137` (`1<<2`) and `IsLayoutDependent` sets `kStructureIndependence_1_11_137Layout` (`1<<2`) [lx/include/F4SE/Interfaces.h:511-517]. F4SE refuses plugins without these bits (or an exact `compatibleVersions` match) [f4se/f4se/PluginManager.cpp:663-707]. There is no CMake helper, so write this by hand in the entry DLL. |
| `SKSE::LoadInterface` | sp/main.cpp:155 | `F4SE::LoadInterface` [lx/include/F4SE/Interfaces.h:161] | | Same | |
| `SKSE::Init(skse)` | sp/main.cpp:163 | `F4SE::Init(const LoadInterface*, F4SE::InitInfo{ .log, .logLevel, .logName, .logPattern, .logRotate, .trampoline, .trampolineSize, .hook })` [lx/include/F4SE/API.h:22-39; impl lx/src/F4SE/API.cpp:163-182] | | Diff-sem | `Init` also creates the spdlog default logger (`My Games/<SaveFolder>/F4SE/<plugin>.log`) unless `.log=false` [lx/src/F4SE/API.cpp:75-115]. SP installs its own logger, so pass `.log=false`. |
| `SKSE::AllocTrampoline(64)` | sp/main.cpp:164 | `InitInfo{.trampoline=true,.trampolineSize=64}` or the deprecated `F4SE::AllocTrampoline(64)` [lx/src/F4SE/API.cpp:117-136, 273-279] | | Renamed | Memory is taken from F4SE's branch pool (`F4SETrampolineInterface::AllocateFromBranchPool`). |
| `SKSE::GetTrampoline()` | sp/Hooks.h:10 | `REL::GetTrampoline()` [cs/include/REL/Trampoline.h; lx/include/F4SE/Trampoline.h:7-13 deprecated alias] | | Renamed | `write_call<5>(src,dst)` is the same API [cs/include/REL/Trampoline.h:75-88]. |
| `SKSE::GetPapyrusInterface()->Register(fn)` | sp/main.cpp:166-172 | `F4SE::GetPapyrusInterface()->Register(bool F4SEAPI(*)(RE::BSScript::IVirtualMachine*))` [lx/include/F4SE/Interfaces.h:354-379] | | Same | The callback signature is identical. |
| `SKSE::GetMessagingInterface()->RegisterListener(cb)` | sp/main.cpp:174-184 | `F4SE::GetMessagingInterface()->RegisterListener(EventCallback*)` (sender defaults to "F4SE") [lx/include/F4SE/Interfaces.h:231-233] | | Diff-sem | Takes a plain function pointer `void F4SEAPI(Message*)`, not a capturing lambda. Message types differ (§3.2). |
| `SKSE::MessagingInterface::Message` / `kDataLoaded…kDeleteGame` | sp/EventHandler.cpp:65-90; sp/BrowserApiNirnLab.cpp:11 | `F4SE::MessagingInterface::Message` and the enum `kPostLoad…kGameDataReady` [lx/include/F4SE/Interfaces.h:204-225] | | Renamed | Mapping in §3.2. |
| `SKSE::GetTaskInterface()` / `SKSE::TaskInterface` | sp/TickHandler.h:200, 212 | `F4SE::GetTaskInterface()` → `AddTask(std::function<void()>)`, `AddUITask`, `AddTaskPermanent` [lx/include/F4SE/Interfaces.h:388-424] | | Same | |
| `RE::Main::GetSingleton()->quitGame` | sp/Win32Api.cpp:20 | `RE::Main::GetSingleton()->quitGame` (bool @0x24) [lx/include/RE/M/Main.h:26-30, 82] | al/.../Main.h:26 | Same | |
| `Offsets::WinMain` (35545/36544) | sp/main.cpp:437 (`GetMainAddress`) | none in lx, al or F4SE | — | Missing-RE | OG ID `668529` [src: te_fo4_pointers.txt line 1]. Better option: call `BeginMain()` from the `_get_narrow_winmain_command_line` hook (§2.2 #W). |
| `Offsets::BaseAddress` | sp/FridaHookHandler.cpp:90-107; sp/PapyrusTESModPlatform.cpp:199 | `REL::Relocation<>::base()` / `REX::FModule::GetExecutingModule().GetBaseAddress()` [cs/include/REL/Relocation.h:377; cs/include/REL/ID.h] | | Renamed | The SP definition is self-referential (`const uintptr_t BaseAddress = Offsets::BaseAddress;` at sp/game/Offsets.h:9). It only compiles because of a quirk; replace it. |
| `REL::Relocate(se,ae)`, `RELOCATION_ID`, `REL::VariantOffset` | sp/game/Offsets.h:12-102 | none (cs is single-runtime) | al: `REL::RelocationID(og,ng)`, `REL::VariantOffset` [al/CommonLibF4/include/REL/ID.h:33-60] | N/A in lx | Use one `REL::ID` per AE ID. For a future OG/NG build, use al's dual IDs. |
| `REL::ID`, `REL::Relocation<T>` | Offsets.h, Hooks.cpp, game/classes/*.h | `REL::ID{u64}` → `.address()/.offset()`; `REL::Relocation<T>{ID}` / `{ID, ptrdiff}` [cs/include/REL/ID.h; cs/include/REL/Relocation.h:192-240] | | Same | |
| `REL::Relocation::write_vfunc(idx, fn)` | sp/Hooks.cpp:90 | same [cs/include/REL/Relocation.h:359-373] | | Same | |
| `REL::safe_write(addr, src, n)` | sp/ConsoleApi.cpp:49, 110, 136, 163, 370 | `REL::WriteSafe(dst, src, size)` [cs/include/REL/Utility.h:52-55] | | Renamed | |
| `FlowManager::CloseProcess(L"SkyrimSE.exe")` | sp/main.cpp:445 | — | | Adapt | Use `L"Fallout4.exe"`. |
| `GetModuleHandleA("skse64_1_6_1170.dll")` | sp/Hooks.cpp:113 | — | | Adapt | The F4SE runtime DLL is named per game version (`f4se_1_11_191.dll` style) [inference: check `f4se_loader` / `Data` folder]. Detect it by enumerating modules with the prefix `f4se_`. |

### 1.2 Logging

| SP symbol | SP usage | FO4 | Status | Notes |
|---|---|---|---|---|
| `SKSE::log::log_directory()` | sp/main.cpp:91 | none. Build it from `SHGetKnownFolderPath(FOLDERID_Documents)` + `My Games/` + `F4SE::GetSaveFolderName()` + `/F4SE`, as lx does [lx/src/F4SE/API.cpp:83-92]. `GetSaveFolderName` = `F4SEInterface::GetSaveFolderName` (0.7.1+) [f4se/f4se/PluginAPI.h:50-52] | Missing (trivial) | |
| `SKSE::stl::report_and_fail` | sp/main.cpp:93 | `REX::FAIL(fmt, ...)` [cs/include/REX/LOG.h] | Renamed | |
| `logger::info/debug/critical(FMT_STRING…)` (= spdlog) | ~40 sites | keep spdlog; or `REX::INFO/ERROR/CRITICAL` (std::format) [cs/include/REX/LOG.h:103-200] | Same | REX logs through `spdlog::default_logger_raw()` with pre-formatted strings, so fmt-based spdlog works. The `wstring_view` overloads need spdlog's **wchar** feature [cs/src/REX/LOG.cpp:10-58; cs/vcpkg.json]. |

### 1.3 Task / threading / the tick

| SP symbol | SP usage | FO4 | Status | Notes |
|---|---|---|---|---|
| `TickHandler` (`AddTask(onTick)` loop) | sp/TickHandler.h:192-213 | `F4SE::TaskInterface::AddTask(std::function)` | Same | Unchanged logic. `AddTaskPermanent` [lx/include/F4SE/Interfaces.h:422-423] could replace the self-rescheduling. |
| "tick trick" `vm->impl->DispatchStaticCall("TESModPlatform","Add", &FunctionArguments, functor)` | sp/PapyrusTESModPlatform.cpp:921-959 | `RE::GameVM::GetSingleton()->GetVM()->DispatchStaticCall(obj, fn, std::function<bool(BSScrapArray<Variable>&)>, BSTSmartPointer<IStackCallbackFunctor>)` (vfunc 0x2C) [lx/include/RE/B/BSScript_IVirtualMachine.h:92]; or the template `DispatchStaticCall(obj, fn, callback, args...)` [lx/include/RE/B/BSScriptUtil.h:1414-1428] | Diff-sem | The arguments are a **std::function** (`BSTThreadScrapFunction<F> = std::function<F>`) [lx/include/RE/B/BSScript_IVirtualMachine.h:16-17], not an `IFunctionArguments*`. FO4's `TESModPlatform.psc` must be recompiled with Caprica / the FO4 compiler, with 12 `Int` params. |
| `RE::SkyrimVM::GetSingleton()` | sp/PapyrusTESModPlatform.cpp:935 | `RE::GameVM::GetSingleton()` (ID 4796420), member `impl` (`BSTSmartPointer<IVirtualMachine>` @0xB0), `GetVM()` [lx/include/RE/G/GameScript.h:498-540; IDs.h:1135] | Renamed | |

### 1.4 Papyrus VM and reflection (SP3, CallNative, VmProvider, GetNativeFunctionAddr, BindNativeMethod hook)

| SP symbol | SP usage | FO4 in lx | al | Status | Notes |
|---|---|---|---|---|---|
| `RE::BSScript::IVirtualMachine` (`IVM`) | everywhere via PCH alias | `RE::BSScript::IVirtualMachine` [lx/include/RE/B/BSScript_IVirtualMachine.h:39-154] | al/.../BSScript/IVirtualMachine.h:63 | Same name | The vtable differs (§4.1). |
| `RE::BSScript::Internal::VirtualMachine` (`VM`) | sp/CallNative.cpp:117,155,342,601; VmProvider.cpp:31; FridaHooks.cpp:24; Sp3Api.cpp:212; SkyrimPlatform.cpp:381,456 | `RE::BSScript::Internal::VirtualMachine` (size 0xC080) [lx/include/RE/B/BSScript_Internal_VirtualMachine.h:47-293] | al/.../BSScript/Internal/VirtualMachine.h:70 | Same | |
| `VM::GetSingleton()` | as above | `VirtualMachine::GetSingleton()` = `static_cast<VirtualMachine*>(GameVM::GetSingleton()->impl.get())` [lx/src/RE/B/BSScript_Internal_VirtualMachine.cpp:11-15] | | Same | |
| `VM::VTABLE[0]` + `write_vfunc(0x18, BindNativeMethod)` | sp/Hooks.cpp:83-91 | `VirtualMachine::VTABLE` = `VTABLE::BSScript__Internal__VirtualMachine` (OG-space IDs, R1). **BindNativeMethod is slot 0x1B** [lx/include/RE/B/BSScript_IVirtualMachine.h:75] | | Diff-sem | F4SE agrees: `RegisterFunction(IFunction*)` is the 0x1B-th virtual [f4se/f4se/PapyrusVM.h:27-55]. Install before GameVM is constructed (F4SE plugin load is early enough, §2.2). |
| BindNativeMethod hook reads `func+0x50` (callback ptr), `func+0x58` (SKSE "long signature" byte) | sp/Hooks.cpp:106-116 | `NF_util::NativeFunctionBase` size 0x50 [lx/include/RE/B/BSScript_Internal_NativeFunctionBase.h:146-157] | | Diff-sem | +0x50 holds: F4SE natives = `void* m_callback` (class size 0x58, **no flag at 0x58**) [f4se/f4se/PapyrusNativeFunctions.h:134-155]; CommonLibF4 natives = `std::function<F> _stub` (64 bytes, **not a code pointer**) [lx/include/RE/B/BSScriptUtil.h:1280-1340]; vanilla natives = **Missing-RE**. F4SE "long" (VM*, stackId, self, …) == latent [f4se/f4se/PapyrusNativeFunctionDef_Base.inl:87-99]. Use this data only as diagnostics. |
| `RE::BSScript::IFunction` (+ `GetName/GetObjectTypeName/GetParam/GetParamCount/GetReturnType/GetIsNative/GetIsStatic/Call`) | sp/Hooks.h:16; Hooks.cpp:78-133; CallNative.cpp:341,493,503; VmProvider.cpp:62-100; FunctionsDumpFactory.cpp:94-133; SkyrimPlatform.cpp:378-385 | `RE::BSScript::IFunction` [lx/include/RE/B/BSScript_Internal_IFunction.h:22-70] | al/.../BSScript/IFunction.h:32 | Same | Vtable 01 GetName … 0F `Call(const BSTSmartPointer<Stack>&, ErrorLogger&, Internal::VirtualMachine&, bool)`. lx takes **references** for logger/vm; SP passes pointers (sp/SkyrimPlatform.cpp:383). |
| `IFunction::CallResult::{kCompleted,kFailedAbort}` | sp/CallNative.cpp:530-535 | `IFunction::CallResult {kCompleted, kSetupForVM, kInProgress, kFailedRetry, kFailedAbort}` [lx/…IFunction.h:29-36] | | Same | |
| `RE::BSScript::NF_util::NativeFunctionBase` (+`GetIsLatent`) | sp/GetNativeFunctionAddr.cpp:13; FunctionsDumpFactory.cpp:100-118 | `RE::BSScript::NF_util::NativeFunctionBase` (file named `BSScript_Internal_NativeFunctionBase.h`). Members `name 10, objName 18, stateName 20, retType 28, descTable 30, isStatic 40, isCallableFromTasklet 41, isLatent 42, userFlags 44, docString 48` | al/.../NF_util/NativeFunctionBase.h:34 | Diff-sem | There is no `GetIsLatent()`. Read the member `isLatent`. The **+0x42 offset is unchanged** (§2.3). |
| `RE::BSScript::NativeFunction<true, decltype(f), R, StaticFunctionTag*, …>` | sp/PapyrusTESModPlatform.cpp:997-1133 (24 binds) | `vm->BindNativeMethod("TESModPlatform"sv, "Add"sv, Add /*fn*/, taskletCallable?, isLatent)` → `NativeFunction<F, LONG, R, S, Args...>` with CTAD [lx/include/RE/B/BSScriptUtil.h:1274-1396] | | Renamed | The static tag is `std::monostate` [BSScriptUtil.h:213]. The long form is `R f(IVirtualMachine&, std::uint32_t stackID, std::monostate, Args...)`. Allowed params are forms (pointers), ints, float, bool, string types, `Variable`, `std::vector`, `structure_wrapper`, `std::optional` [BSScriptUtil.h:150-200]. **`std::string_view` arg in CloseMenu (sp/PapyrusTESModPlatform.cpp:986) works (string_type concept).** |
| `RE::StaticFunctionTag` | 72 uses (PapyrusTESModPlatform.h/.cpp) | `std::monostate` | `RE::BSScript::StaticFunctionTag`? (al uses `std::monostate` too) | Renamed | |
| `RE::BSScript::IStackCallbackFunctor` | sp/VmCallback.h:45-77; PapyrusTESModPlatform.cpp:66-76, 941 | `IStackCallbackFunctor : BSIntrusiveRefCounted`. Virtuals: `01 CallQueued, 02 CallCanceled, 03 StartMultiDispatch, 04 EndMultiDispatch, 05 operator()(Variable), 06 CanSave` [lx/include/RE/B/BSScript_IStackCallbackFunctor.h:11-24] | al/.../IStackCallbackFunctor.h:21 | Diff-sem | Implement the four extra pure virtuals. **No `SetObject`.** |
| `RE::BSScript::IFunctionArguments` (+`BSScrapArray<Variable>`) | sp/VmFunctionArguments.h:80-107; PapyrusTESModPlatform.cpp:53-64 | none. Use a lambda `[](BSScrapArray<Variable>& out){…; return true;}` passed as `BSTThreadScrapFunction<bool(BSScrapArray<Variable>&)>`. `BSScrapArray` is at [lx/include/RE/B/BSTArray.h:585] | | Missing→replace | |
| `IVM::DispatchMethodCall2(handle, cls, fn, args, functor)` | sp/VmCall.h:17 | `DispatchMethodCall(std::uint64_t handle, const BSFixedString& obj, const BSFixedString& fn, const std::function<…>& args, const BSTSmartPointer<IStackCallbackFunctor>&)` (vfunc 0x2E) [lx/…IVirtualMachine.h:93] | | Renamed | |
| `IVM::DispatchStaticCall(cls, fn, args, functor)` | sp/VmCall.h:29; PapyrusTESModPlatform.cpp:953 | vfunc 0x2C (above) | | Diff-sem | Args are a std::function. |
| `IVM::ReloadType(name)` | sp/CallNative.cpp:296; VmProvider.cpp:40 | `ReloadType(const char*)` vfunc 0x10 | | Same | |
| `IVM::GetTypeIDForScriptObject(name, id&)` | sp/CallNative.cpp:123, 608-611 | vfunc 0x0C `(const BSFixedString&, std::uint32_t&) const` | | Same | |
| `VM::GetScriptObjectType(typeId, ptr&)` / `GetScriptObjectType1(name, ptr&)` | sp/CallNative.cpp:570; Sp3Api.cpp:216 | two overloads `GetScriptObjectType(std::uint32_t, …)` (0x09) and `(const BSFixedString&, …)` (0x08) | | Renamed (`…1` → overload) | |
| `IVM::GetObjectHandlePolicy()` → `HandleIsType(type, h)`, `GetObjectForHandle(type, h)` | sp/CallNative.cpp:165-170; FridaHooks.cpp:28-37 | returns a **reference** `IObjectHandlePolicy&` (0x33/0x34). Policy virtuals: `HandleIsType` 01, `EmptyHandle` 06, `GetHandleForObject` 07, `GetObjectForHandle` 0C [lx/include/RE/B/BSScript_IObjectHandlePolicy.h:9-30] | | Diff-sem | `auto& policy = …`; remove the null check. |
| `VM::GetErrorLogger()` | sp/CallNative.cpp:533 | returns a **reference** `ErrorLogger&` (0x32) | | Diff-sem | |
| `VM::allRunningStacks` | sp/CallNative.cpp:346-348 | `BSTHashMap<u32, BSTSmartPointer<Stack>> allRunningStacks` @0xBD60, plus `runningStacksLock` @0xBD58 [lx/…VirtualMachine.h:247-248] | | Same | |
| `VM::objectTypeMap` | sp/CallNative.cpp:350; VmProvider.cpp:36 | `BSTHashMap<BSFixedString, BSTSmartPointer<ObjectTypeInfo>> objectTypeMap` @0x168 [lx/…VirtualMachine.h:226] | | Same | Guard with `typeInfoLock` @0xC0. |
| `VM::attachedScriptsLock/attachedScripts` | sp/FridaHooks.cpp:45-70 | `attachedScriptsLock` @0xBDF8; `BSTHashMap<u64, BSTSmallSharedArray<AttachedScript>> attachedScripts` @0xBE00 [lx/…VirtualMachine.h:257-258]. `AttachedScript : BSTPointerAndFlags<BSTSmartPointer<Object>,1>` [lx/…Internal_AttachedScript.h:13-16] | | Same (lock is a `BSSpinLock`) | `scripts[i].get()` returns a smart pointer. |
| `RE::BSScript::Stack` (`top`, `returnValue`) | sp/CallNative.cpp:354-357, 489, 539; SkyrimPlatform.cpp:379 | `Stack` with `top` @0x60, `returnValue` @0x70, `stackID` @0x80 [lx/include/RE/B/BSScript_Internal_Stack.h:24-89] | | Same | |
| `StackFrame::{owningFunction, owningObjectType, self, size, args}` | sp/CallNative.cpp:354-357, 489-496 | `StackFrame {parent 00, previousFrame 08, owningFunction 10, owningObjectType 18, ip 20, self 28, size 38, instructionsValid 3C}` [lx/include/RE/B/BSScript_StackFrame.h:9-25] | | Diff-sem | Use `GetStackFrameVariable(i, GetPageForFrame())` instead of `args[i]`. |
| `RE::BSScript::Variable` (`SetNone/SetSInt/SetFloat/SetBool/SetString/GetObject/GetString/GetSInt/GetFloat/GetBool/GetArray/varType/value.obj`) | sp/CallNative.cpp:76-147, 160-253, 511; Sp3NativeValueCasts.cpp | `Variable`: `operator=(nullptr / BSFixedString / int32 / uint32 / float / bool / Variable* / BSTSmartPointer<Object\|Struct\|Array>)`, `is<T>()`, `BSScript::get<T>(var)`, `GetType()` [lx/include/RE/B/BSScript_Variable.h:21-330] | | Renamed | Rewrite the AnySafe↔Variable converters. Add `Var` and `Struct` cases (§4.1). |
| `TypeInfo::RawType::{kNone,kObject,kString,kInt,kFloat,kBool,kNoneArray,kObjectArray,kStringArray,kIntArray,kFloatArray,kBoolArray}`; `GetUnmangledRawType()`, `GetTypeInfo()`, `IsInt()` | sp/CallNative.cpp:193-255; FunctionsDumpFactory.cpp:36-72; VmProvider.cpp:159-170 | `RawType {kNone 0, kObject 1, kString 2, kInt 3, kFloat 4, kBool 5, kVar 6, kStruct 7, kArrayObject 0xB, kArrayString 0xC, kArrayInt 0xD, kArrayFloat 0xE, kArrayBool 0xF, kArrayVar 0x10, kArrayStruct 0x11}`; `GetRawType()`, `GetObjectTypeInfo()`, `GetStructTypeInfo()`, `IsArray()` [lx/include/RE/B/BSScript_TypeInfo.h:11-125] | | Renamed | There is no `kNoneArray`. `IsInt()` becomes `GetRawType()==RawType::kInt`. |
| `RE::BSScript::ObjectTypeInfo` (`GetName, GetParent, GetNumGlobalFuncs, GetGlobalFuncIter()[i].func, GetNumMemberFuncs, GetMemberFuncIter`) | sp/VmProvider.cpp:73-100, 192-213; CallNative.cpp:545-577; Sp3Api.cpp:215-227 | same names [lx/include/RE/B/BSScript_ObjectTypeInfo.h:144-186] | | Same | |
| `RE::BSScript::Object` (`handle`, `Resolve(type)`, `GetTypeInfo()`) | sp/CallNative.cpp:160-171; FridaHooks.cpp:52-54; VmCall.h:17 | same (`handle` @0x20, `Resolve(u32)`, `GetTypeInfo()`) [lx/include/RE/B/BSScript_Object.h:12-64] | | Same | |
| `RE::BSScript::PackHandle(&var, form, typeId)` | sp/CallNative.cpp:140 | none with a runtime type id. `PackVariable<T>(Variable&, const T*)` (static type) [lx/include/RE/B/BSScriptUtil.h:547-595] | | Diff-sem | Write `PackHandle(Variable&, void*, u32 vmTypeID)` by copying that body: `GetHandleForObject(id, ptr)` → `FindBoundObject` → `CreateObject` + `GetObjectBindPolicy().BindObject`. |
| `RE::VMTypeID/VMHandle/VMStackID` | CallNative.cpp, FridaHooks.cpp:21, PCH | same typedefs [lx/include/RE/B/BSCoreTypes.h:7-9] | | Same | FO4 VM type ids equal `ENUM_FORM_ID` for forms; aliases are 160-165 [lx/include/RE/E/ENUM_TYPE_ID.h]. The cast `(VMTypeID)form->formType` still holds. |
| `RE::BSScript::ErrorLogger` | SkyrimPlatform.h:18; .cpp:380 | `ErrorLogger` [lx/include/RE/B/BSScript_ErrorLogger.h:15] | | Same | |
| `FunctionsDump` (`DumpFunctions`, `FunctionsDumpFactory`) | sp/DumpFunctions.cpp:15-52; FunctionsDumpFactory.cpp | same VM types | | Adapt | `Data\Scripts` has no loose vanilla PEX in FO4 (they are in `Fallout4 - Misc.ba2`). The pex reader must become FO4-aware (papyrus-vm work). |

### 1.5 Events and sinks

The global API differences:
- `BSTEventSink<E>::ProcessEvent(const E& a_event, BSTEventSource<E>*)` takes a **reference**, not a pointer [lx/include/RE/B/BSTEvent.h:19-27].
- `BSTEventSource<E>::RegisterSink / UnregisterSink / Notify` replace `AddEventSink / RemoveEventSink / SendEvent` [lx/include/RE/B/BSTEvent.h:36-117].
- There is **no `ScriptEventSourceHolder`**. Each script event has a static `GetEventSource()` (an ID'd getter) [lx/include/RE/T/TES*Event.h].
- `InputEvent*` is **not** a `BSInputDeviceManager` event source (§1.10).

| SP symbol | SP usage | FO4 in lx | Status | Notes |
|---|---|---|---|---|
| `RE::BSTEventSink<E>` / `BSTEventSource<E>` | EventHandler.h:29-94 (63 bases); LoadGame.cpp:16-33; PapyrusTESModPlatform.cpp:79-100 | [lx/include/RE/B/BSTEvent.h:19-127] | Diff-sem | Change every `ProcessEvent(const E* e, …)` to `(const E& e, …)`. |
| `RE::BSEventNotifyControl::kContinue` | ~70 returns | same enum `{kContinue, kStop}` [BSTEvent.h:10-14] | Same | |
| `RE::ScriptEventSourceHolder::GetSingleton()->GetEventSource<E>() / AddEventSink / SendEvent<T>` | EventUtils.h:4-7; PapyrusTESModPlatform.cpp:84-89; LoadGame.cpp:21-27; EventEmitter.h:273-277 | `E::GetEventSource()` per event (table in §4.2) | Missing→replace | |
| `SKSE::ActionEvent` + `GetActionEventSource()` | EventHandler.h:73, 542-568; .cpp:1630-1702; EventEmitter.h:247-250 | none (F4SE implements no dispatchers: "Currently none implemented yet" [f4se/f4se/PluginAPI.h:126-128]) | N/A | Replacements: `PlayerSetWeaponStateEvent`, `PlayerWeaponReloadEvent`, `WeaponFiredEvent`, `MeleeAttackJustReleasedEvent` (global events, §4.2); hook `Actor::PerformAction` (ID 2231177 [lx/include/RE/IDs.h:36]); `BSAnimationGraphEvent` sinks. |
| `SKSE::CameraEvent` + `GetCameraEventSource()` | EventHandler.h:74, 570-592; .cpp:1703-1738 | none | N/A | Hook `PlayerCamera::SetState` (2214742) / `PushState` (2248422) [lx IDs.h:1813-1823], or the story event `FirstThirdPersonSwitch::Event` (RTTI present, §4.2). |
| `SKSE::CrosshairRefEvent` | EventHandler.h:75, 594-616; .cpp:1739-1767 | `PlayerCharacter : BSTEventSource<PickRefUpdateEvent>` @0x5D0 [lx/include/RE/P/PlayerCharacter.h:81] | Renamed/Diff-sem | Register with `static_cast<BSTEventSource<PickRefUpdateEvent>*>(PlayerCharacter::GetSingleton())`. |
| `SKSE::NiNodeUpdateEvent` | EventHandler.h:76, 642-664; .cpp:1768-1793 | none | N/A | Hook `Actor::Reset3D` (2229913) or `TESObjectREFR::Set3D` (vfunc 0x88) [lx/include/RE/T/TESObjectREFR.h:176]. |
| `SKSE::ModCallbackEvent` | EventHandler.h:77, 618-640; .cpp:1794-1825 | none (FO4 Papyrus uses CustomEvent / `SendCustomEvent`) | N/A | Emulate with a TESModPlatform native if needed. |
| `RE::UI` as the source of `MenuOpenCloseEvent` | EventHandler.h:667-668 | `UI : BSTEventSource<MenuOpenCloseEvent>` @0x18; `ui->GetEventSource<T>()`, `RegisterSink<T>` [lx/include/RE/U/UI.h:19-121] | Same idea | `MenuOpenCloseEvent{BSFixedString menuName; bool opening}` [lx/include/RE/M/MenuOpenCloseEvent.h]. |
| `RE::BSInputDeviceManager` as the `InputEvent*` source | EventHandler.h:669-671 | **not an event source** [lx/include/RE/B/BSInputDeviceManager.h:9-48] | Diff-sem | See §1.10. |
| `RE::BGSFootstepManager` / `BGSFootstepEvent` | EventHandler.h:78, 672-673; .cpp:2087-2105 | `BSTEventSink<BGSFootstepEvent>` exists in RTTI; no lx class or source | Missing-RE | |
| `RE::PlayerCharacter` as the `PositionPlayerEvent` source | EventHandler.h:674-675; EventUtils.h:23-30 (disabled on AE Skyrim) | `PlayerCharacter : BSTEventSource<PositionPlayerEvent>` @0x578 [lx/include/RE/P/PlayerCharacter.h] | Same (works) | `PositionPlayerEvent{EVENT_TYPE type; bool NoLoadScreen}` [lx/include/RE/P/PositionPlayerEvent.h]. |
| Story events `RE::ActorKill/BooksRead/CriticalHit/DisarmedEvent/DragonSoulsGained/ItemHarvested/LevelIncrease/LocationDiscovery/ShoutAttack/SkillIncrease/SoulsTrapped/SpellsLearned` + `Offsets::EventSource::*` | EventHandler.h:82-93, 679-690; .cpp:2126-2478; game/events/ItemHarvested.h | see the §4.2 story table: ActorKill (struct only), BooksRead (struct only), LevelIncrease (2233736), TESHarvestEvent::ItemHarvested (2193351); CriticalHitEvent/DisarmedEvent in RTTI only; the rest are N/A | Mixed | |
| `RE::ChestsLooted::SendEvent`, `ItemsPickpocketed::SendEvent` | EventEmitter.h:240-245 | `ChestLooted::Event`, `ItemPickpocketed::Event` (RTTI only) | N/A | **EventEmitter.h is unreferenced dead code** (only self-references); drop it. |
| Per-event field use | EventHandler.cpp | — | — | Table §4.2b. |

### 1.6 Hooks and offsets (Frida, trampoline, vtable)

The full offset table is in §2. Hook plumbing:

| SP piece | SP usage | FO4 | Status |
|---|---|---|---|
| `Hooks::write_thunk_call<T>` (trampoline `write_call<5>`) | sp/Hooks.h:7-12; Hooks.cpp:54-76 | `REL::GetTrampoline().write_call<5>()`. cs also has a hook framework (`REL::THook`, `REL::FHookStore`, `EHookStep::{PreLoad,Load}` driven by `F4SE::Init(.hook=true)`) [cs/include/REL/IHook.h:17-40; lx/src/F4SE/API.cpp:138-145] | Same |
| Frida `HookHandler` / `gum_interceptor_attach` | sp/FridaHookHandler.cpp | unchanged (game-agnostic) | Same |
| `RE::BSScript::Internal::VirtualMachine::VTABLE` | sp/Hooks.cpp:86-87 | R1 caveat (§2.4) | Diff-sem |

### 1.7 Forms / references / cells / worldspaces / data handler

| SP symbol | SP usage | FO4 in lx | al | Status | Notes |
|---|---|---|---|---|---|
| `RE::TESForm::LookupByID<T>(id)` (118 uses) / `LookupByEditorID` | everywhere; ConsoleApi.cpp:234 | `TESForm::GetFormByID<T>(id)`, `GetFormByEditorID<T>(const BSFixedString&)` [lx/include/RE/T/TESForm.h:154-190] | | Renamed | |
| `TESForm::formID / GetFormID() / formType(.get()) / GetFormType() / As<T>()` | everywhere | `formID` @0x14; `formType` = `REX::TEnumSet<ENUM_FORM_ID,u8>` @0x1A; `GetFormType()`; `As<T>()`; `Is<T>()`; `IsPlayerRef()` (0x14) [TESForm.h:228-331] | | Same | |
| `RE::FormType::{ActorCharacter, Reference, NPC, Armor, Weapon, Ammo, Light, AlchemyItem, Spell, Scroll, Ingredient, Enchantment, Max}` | CallNative.cpp:169,267-268,380; FridaHooks.cpp:31-35; PapyrusTESModPlatform.cpp:240-787; InventoryApi.cpp:176-177; MagicApi.cpp:82-84 | `ENUM_FORM_ID::{kACHR(0x41), kREFR(0x40), kNPC_(0x2D), kARMO(0x1D), kWEAP(0x2B), kAMMO(0x2C), kLIGH(0x22), kALCH(0x30), kSPEL(0x19), kSCRL(0x1A), kINGR(0x21), kENCH(0x18), kTotal}` [lx/include/RE/E/ENUM_FORM_ID.h:38-182] | | Renamed | |
| `RE::FormID` | Events.h:9-148 | `RE::TESFormID` | | Renamed | |
| `RE::TESDataHandler::GetSingleton()` (+`files`, `compiledFileCollection.{files,smallFiles}`) | CallNativeApi.cpp:29-70; LoadGame.cpp:147-155; PapyrusTESModPlatform.cpp:858 | `TESDataHandler::GetSingleton()` (4796135); `BSSimpleList<TESFile*> files` @0xFB0; `TESFileCollection compiledFileCollection{files, smallFiles}` @0xFC0 [lx/include/RE/T/TESDataHandler.h:28, 183-184] | al/.../TESDataHandler.h:61 | Same | `TESFile::filename` naming must be checked (SP uses `fileName`). |
| `TESDataHandlerExtension::CreateReferenceAtLocationImpl` (13625/13723, 10 args) | sp/game/Offsets.h:88-109; PapyrusTESModPlatform.cpp:849-874 | `TESDataHandler::CreateReferenceAtLocation(NEW_REFR_DATA&)` → `ObjectRefHandle` (ID **2192301**) [lx/include/RE/T/TESDataHandler.h:48-53; IDs.h:2114]; `NEW_REFR_DATA` (0x70: location, direction, object, interior, world, reference, addPrimitive, additionalData, extra, instanceFilter, modExtra, maxLevel, forcePersist, clearStillLoadingFlag, initializeScripts, initiallyDisabled) [lx/include/RE/N/NEW_REFR_DATA.h] | OG `500304` [al/.../TESDataHandler.h:142-147]; te OG `500305` "SpawnNewREFR" | Renamed | `NEW_REFR_DATA()` calls `REX::EMPLACE_VTABLE` (VTABLE ID, R1). |
| `RE::TESObjectREFR` members: `MoveTo_Impl` | PapyrusTESModPlatform.cpp:130-142 | none (see patch 01) | | Missing-RE | F4SE `MoveRefrToPosition` RVA 0x1181020@1.11.240. |
| `TESObjectREFR::GetPosition()/GetPositionZ()` | MagicApi.cpp:121-141; TextApi.cpp:440 | same (`data.location`) [TESObjectREFR.h:381-387] | | Same | |
| `TESObjectREFR::Get3D()/Get3D(bool firstPerson)` | TextApi.cpp:379-397 | `Get3D()` (vfunc 0x8C), `Get3D(bool)` (0x8B) [TESObjectREFR.h:179-180]; `PlayerCharacter::firstPerson3D` @0xB78 | | Same | |
| `Actor::Get3D2()` | MagicApi.cpp:132 | `Get3D(false)` | | Renamed | |
| `TESObjectREFR::GetBoundMin/Max()` | MagicApi.cpp:124-125 | same (vfunc 0x93/0x94) | | Same | |
| `TESObjectREFR::SetCollision(bool)` | ObjectReferenceApi.cpp:20 | none in lx | | Missing-RE | FO4 Papyrus `ObjectReference.SetCollision`? Not found among the natives checked. Alternatively toggle the `bhkCharacterController` collision layer [inference]. |
| `TESObjectREFR::IsCrimeToActivate()` | EventHandler.cpp:106 | same (2201180) [TESObjectREFR.h:452-456] | | Same | |
| `TESObjectREFR::AddObjectToContainer(obj, extra*, count, from)` | CallNative.cpp:458; InventoryApi.cpp:331; PapyrusTESModPlatform.cpp:747 | vfunc 0x7A `AddObjectToContainer(TESBoundObject*, BSTSmartPointer<ExtraDataList>, i32, TESObjectREFR* oldContainer, ITEM_REMOVE_REASON)` [TESObjectREFR.h:162] | | Diff-sem | |
| `TESObjectREFR::RemoveItem(obj, count, reason, extra, moveTo)` | CallNative.cpp:483; PapyrusTESModPlatform.cpp:750 | vfunc 0x6D `RemoveItem(RemoveItemData&)` with `RemoveItemData{stackData, object, count, reason, otherContainer, dropLoc, rotate}` [TESObjectREFR.h:89-109, 149] | | Diff-sem | |
| `TESObjectREFR::extraList.GetByType<ExtraContainerChanges>()` → `changes->entryList` | InventoryApi.cpp:181-201 | `BGSInventoryList* inventoryList` @0xF8 → `data` (`BSTArray<BGSInventoryItem>`) with stacks [TESObjectREFR.h:608; lx/include/RE/B/BGSInventoryList.h:125] | | Diff-sem | Rewrite `getExtraContainerChanges` (§4.5). |
| `RE::ObjectRefHandle` (+`get().get()`), `Offsets::GetInvalidRefHandle` (514164/400312) | Offsets.h:60-65; PapyrusTESModPlatform.cpp:139, 866-872 | `ObjectRefHandle` (`BSPointerHandle<TESObjectREFR>`); invalid = default-constructed (`_handle 0`) [lx/include/RE/B/BSPointerHandle.h:33-61, 104-116] | | Diff-sem | te OG `888642` "s_nullHandle" exists, but a value of 0 is sufficient [inference: has_value() == (_handle != 0)]. |
| `RE::TESObjectCELL`, `RE::TESWorldSpace`, `RE::BGSLocation` | PapyrusTESModPlatform signatures; EventHandler.cpp:229-230,296 | [lx/include/RE/T/TESObjectCELL.h:31; T/TESWorldSpace.h:25; B/BGSLocation.h:19] | | Same | |
| `RE::BGSPrimitive` | Offsets.h:95 | [lx/include/RE/B/BGSPrimitive.h:13] | | Same | |
| `RE::IFormFactory::GetConcreteFormFactoryByType<T>()->Create()` | PapyrusTESModPlatform.cpp:35-39 | `RE::ConcreteFormFactory<T>::GetFormFactory()->Create()` [lx/include/RE/C/ConcreteFormFactory.h:4-18] | | Renamed | |
| `SetFormIdUnsafe` (writes `form->formID`) | PapyrusTESModPlatform.cpp:479-486 | same field | | Same | |
| `RE::BGSDefaultObjectManager::GetSingleton()` | PapyrusTESModPlatform.cpp:778 (result unused) | `BGSDefaultObjectManager` (2192850) | | Same | |
| hardcoded form IDs `0x10D13E` (template NPC), `0x654E2` (DoNothing PACK), `0x6AF62`, `0x13F42/43/45` (EquipSlot R/L/Both) | PapyrusTESModPlatform.cpp:261-262, 302, 768-773; InventoryApi.cpp:248-253 | look up in Fallout4.esm | | Missing-data | FO4 equip slots are EQUP records (BothHands/LeftHand/RightHand…). Pick a new template NPC_ and a DoNothing-style package (`Fallout4.esm`). |

### 1.8 Actors / AI / movement / animation graph

| SP symbol | SP usage | FO4 in lx | Status | Notes |
|---|---|---|---|---|
| `RE::Actor` (61 uses), `RE::PlayerCharacter::GetSingleton()` | everywhere; Sp3Api.cpp:278; PapyrusTESModPlatform.cpp:462-545 | `Actor` (size ≥0x490) [lx/include/RE/A/Actor.h:73-690]; `PlayerCharacter::GetSingleton()` (303411 OG te; lx `PlayerCharacter.h:167`) | Same | There is no `Character` class in FO4 (absent from RTTI) [src: lx/include/RE/IDs_RTTI.h]. |
| `Actor::AsActorState()->IsWeaponDrawn()` | PapyrusTESModPlatform.cpp:166-171 | `ActorState::GetWeaponMagicDrawn()`; `weaponState: WEAPON_STATE{kSheathed…kSheathing}`; `gunState: GUN_STATE{kDrawn,kRelaxed,kBlocked,kAlert,kReloading,kThrowing,kSighted,kFire,kFireSighted}` [lx/include/RE/A/ActorState.h:21-49; W/WEAPON_STATE.h; G/GUN_STATE.h] | Renamed | `Actor : ActorState` @0x128 [Actor.h:76]. |
| `Actor::DrawWeaponMagicHands(bool)` | PapyrusTESModPlatform.cpp:168,173 | `virtual void DrawWeaponMagicHands(bool)` **vfunc 0xC9** [lx/include/RE/A/Actor.h:135] | Same | Also `SetGunState(GUN_STATE,bool)` (2231175) [Actor.h:528-533]. |
| `Actor::GetActorRuntimeData().currentProcess->GetEquippedLeft/RightHand()` | PapyrusTESModPlatform.cpp:816-821 | `AIProcess* currentProcess` @0x300; `Actor::GetEquippedItem(BGSObjectInstance*, BGSEquipIndex)` (2231089) [Actor.h:563-567, 640] | Diff-sem | `UpdateEquipment` is a no-op bug in SP (it assigns a local copy). Drop it. |
| `Actor::GetActorRuntimeData().selectedSpells[SlotTypes::…]` | EventHandler.cpp:1358-1369 | `MagicItem* selectedSpell[4]` @0x3E8 [Actor.h:666] | Diff-sem | Only for the spellCast event, which is N/A in FO4. |
| `Actor::GetMagicCaster(src)`, `GetMagicTarget()`, `AsMagicTarget()->GetActiveEffectList()`, `GetAimAngle/Heading`, `InterruptCast` | EventHandler.cpp:161-169, 1387-1404; MagicApi.cpp:98-185 | `ActorMagicCaster* magicCasters[4]` @0x3C8; `MagicTarget::GetActiveEffectList()` (vfunc 07) [lx/include/RE/M/MagicTarget.h:26]; `ActiveEffect::uniqueID` @0x8C | Diff-sem / N/A | effectStart/effectFinish → `TESActiveEffectApplyRemoveEvent` (RTTI present, needs a source ID). |
| `Actor::DoReset3D(false)` ("QueueNiNodeUpdate") | CallNative.cpp:389-401 | `Actor::Reset3D(bool reloadAll, u32 additionalFlags, bool queueReset, u32 excludeFlags)` (**2229913**) [Actor.h:500-505] | Renamed | te OG `302889` "QueueUpdate". |
| `Offsets::PushActorAway(vm, stack, self, target, mag)` (55682/56213) | Offsets.h:78-84; CallNative.cpp:404-434 | FO4 Papyrus native `ObjectReference.PushActorAway(Actor, float)` exists [f4se/scripts/vanilla/ObjectReference.psc:730] | Missing-RE (direct) | Call the bound `IFunction` through the normal CallNative path, or capture its callback in the BindNativeMethod hook. |
| `Offsets::Unknown(nullptr,nullptr,refr)` (55622/56151, used for `ClearDestruction`) | Offsets.h:71-76; CallNative.cpp:369-387 | FO4 Papyrus native `ObjectReference.ClearDestruction()` [ObjectReference.psc:256] | Missing-RE (direct) | Same approach. |
| `IAnimationGraphManagerHolder::NotifyAnimationGraph(BSFixedString)` | SendAnimationEvent.cpp:28 | `NotifyAnimationGraphImpl(const BSFixedString&)` (vfunc 01) [lx/include/RE/I/IAnimationGraphManagerHolder.h:14] | Renamed | `TESObjectREFR : IAnimationGraphManagerHolder` @**0x48** [TESObjectREFR.h:78]. Call it on `static_cast<IAnimationGraphManagerHolder*>(refr)`. |
| `Actor::GetAnimationGraphManager(BSTSmartPointer<BSAnimationGraphManager>&)` | Magic/AnimationGraphMasterBehaviourDescriptor.h:105-107,164-166 | `GetAnimationGraphManagerImpl(...)` (vfunc 04) | Renamed | |
| `BSAnimationGraphManager::{graphs, GetRuntimeData().activeGraph, updateLock}` | same file 111-119 | `graph` (`BSTSmallArray<BSTSmartPointer<BShkbAnimationGraph>,1>`) @0x40, `updateLock` @0xC8, `activeGraph` @0xD8 [lx/include/RE/B/BSAnimationGraphManager.h:18-43] | Renamed | |
| `RE::BShkbAnimationGraph::behaviorGraph->variableValueSet/rootGenerator` + SP `hkbVariableValueSet<T>{varSet @0x10}` | AnimationGraphMasterBehaviourDescriptor.h:117-188; Magic/hkbVariableValueSet.h | `BShkbAnimationGraph` is forward-declared only (lx and al) | Missing-RE | Use **name-based** `GetGraphVariableImplFloat/Int/Bool(name, out)` (vfuncs 0x13-0x15) and `SetGraphVariableBool/Float/Int` (2214543/2214545/2214544) [IAnimationGraphManagerHolder.h:33-60]. The Skyrim index tables (`Magic/AnimationVariablesForMasterBehavior.h`) are meaningless in FO4. |
| `RE::PlayerControls::GetSingleton()->data.running` | PapyrusTESModPlatform.cpp:209-210 | `PlayerControls::GetSingleton()` (4799306); `PlayerControlsData data` @0x44 → `running` @+0x45 [lx/include/RE/P/PlayerControls.h:75; P/PlayerControlsData.h:19] | Same | |
| `RE::ACTOR_COMBAT_STATE::{kCombat,kSearching}` | EventHandler.cpp:327-328 | `ACTOR_COMBAT_STATE` [lx/include/RE/A/ACTOR_COMBAT_STATE.h:5] | Same | `TESCombatEvent` has no lx struct (§4.2). |
| `RE::Projectile::Launch/LaunchData/ProjectileRot`, `RE::ProjectileHandle` | MagicApi.cpp:64-151 | `ProjectileLaunchData` (0xA0) only [lx/include/RE/P/ProjectileLaunchData.h:17-54]; no Launch ID | Missing-RE / N/A | te OG `1452335` Launch, `1056038` Fire. FO4 Papyrus has `Weapon.Fire(ObjectReference, Ammo)` [f4se/scripts/vanilla/Weapon.psc:4]. |
| `RE::MagicSystem::{CastingSource, CastingType, Delivery}`, `RE::SpellItem/EffectSetting/EnchantmentItem/AlchemyItem/ActiveEffect` | MagicApi.cpp; EventHandler.cpp; PapyrusTESModPlatform.h:81-84 | exist [lx/include/RE/S/SpellItem.h:17, E/EffectSetting.h:27, E/EnchantmentItem.h:14, A/AlchemyItem.h:17, A/ActiveEffect.h:24] | N/A (feature) | The MagicApi feature is dropped for FO4. |
| `RE::TESPackage::packData`, `TESNPC::aiPackages.packages` | PapyrusTESModPlatform.cpp:265-271 | `TESPackage::packData` @0x30 [lx/include/RE/T/TESPackage.h:35]; `TESAIForm::aiPackList` (`PackageList`) [lx/include/RE/T/TESAIForm.h:18] | Renamed | |

### 1.9 TESNPC / appearance (TESModPlatform natives)

| SP symbol / field | SP usage | FO4 in lx | Status | Notes |
|---|---|---|---|---|
| `TESNPC` memcpy clone (`sizeof RE::TESNPC`) | PapyrusTESModPlatform.cpp:232-288 | `TESNPC` size **0x308** [lx/include/RE/T/TESNPC.h:212] | Diff-sem | Deep-copy pointers: `headParts`, `morphSliderValues`, `morphRegionSliderValues`, `facialBoneRegionSliderValues`, `tintingData`, `headRelatedData`. |
| `numContainerObjects / containerObjects` (TESContainer) | PapyrusTESModPlatform.cpp:248-249, 839-840; InventoryApi.cpp:227-234 | `TESContainer::containerObjects` @0x08, `numContainerObjects` @0x10; `ContainerObject{count, obj, itemExtra}` [lx/include/RE/T/TESContainer.h:83-84; C/ContainerObject.h:28-30] | Same | |
| `crimeFaction`, `faceNPC`, `race` | 250-251, 412 | `crimeFaction` @0x2C8, `faceNPC` @0x270, race via `TESRaceForm` base | Same | |
| `actorData.actorBaseFlags.{set,reset}(ACTOR_BASE_DATA::Flag::{kPCLevelMult,kUnique,kSimpleActor,kFemale})` | 252-254, 400-402 | same names [lx/include/RE/A/ACTOR_BASE_DATA.h:7-60]; `TESActorBaseData::actorData` @0x08 [T/TESActorBaseData.h:57] | Same | |
| `baseTemplateForm`, `CopyFromTemplateForms(TESActorBase**)` | 363, 378 | `baseTemplateForm` @0x30, `templateForms` @0x38, `CopyFromTemplateForms` vfunc 07 [TESActorBaseData.h:27, 61-62] | Same | |
| `faceData` (`TESNPC::FaceData`) | 280-285 | none. Replaced by `morphSliderValues` (`BSTHashMap<u32,float>*` @0x2F8), `morphRegionSliderValues` (`BSTArray<float>*` @0x2D8), `facialBoneRegionSliderValues` (`BSTHashMap<u32, BGSCharacterMorph::Transform>*` @0x2E0), `morphWeight` (NiPoint3 thin/muscular/large @0x278) | N/A → rewrite | |
| `bodyTintColor{red,green,blue}` | 222-226, 423-425 | `bodyTintColorR/G/B/A` (int8 @0x2EA-0x2ED) | Renamed | |
| `SetHairColor(BGSColorForm*)`; `BGSColorForm::color.{red,green,blue}` | 432-437 | `TESNPC::SetHairColor` [TESNPC.h:160]; `HeadRelatedData{hairColor, facialHairColor, faceDetails}` @0x248; **`BGSColorForm::color` is a packed `u32`** [lx/include/RE/B/BGSColorForm.h:25-29] | Diff-sem | |
| `headParts` / `numHeadParts` | 447-455 | `BGSHeadPart** headParts` @0x2D0, `int8 numHeadParts` @0x2E8; `GetHeadParts(bool alternate)`; `GetAlternateHeadPartListMap()` [TESNPC.h:74-93, 199-202] | Same + alternate list | |
| `PlayerCharacter::GetTintList()` + SP `TintMask` copy (`game/classes/TintMask.h`) | 462-567; FridaHooks.cpp:211-252 | `TESNPC::tintingData` / `PlayerCharacter::tintingData` (`BGSCharacterTint::Entries*`, entries with `idLink`, `tintingValue`, `tintingColor`, `swatchID`) [TESNPC.h:210; P/PlayerCharacter.h:462; B/BGSCharacterTint.h:142-183] | N/A → rewrite | FO4 tints are per-NPC data. The Skyrim per-actor render-target swap hook is not needed. Apply by editing `tintingData` and calling `Actor::Reset3D`. |
| `RE::TESTexture`, `RE::BGSHeadPart`, `RE::TESRace`, `RE::TESActorBase` | various | [lx/include/RE/T/TESTexture.h:9; B/BGSHeadPart.h:14; T/TESRace.h:32; T/TESActorBase.h:19] | Same | |

### 1.10 Input

| SP symbol | SP usage | FO4 in lx | Status | Notes |
|---|---|---|---|---|
| `BSTEventSink<RE::InputEvent*>` on `BSInputDeviceManager` | EventHandler.h:79, 669-671; .cpp:1852-2085 | implement `RE::BSInputEventUser` (virtuals `ShouldHandleEvent`, `OnKinectEvent`, `OnDeviceConnectEvent`, `OnThumbstickEvent`, `OnCursorMoveEvent`, `OnMouseMoveEvent`, `OnCharacterEvent`, `OnButtonEvent`) and add it with `MenuControls::GetSingleton()->RegisterHandler(h)` [lx/include/RE/B/BSInputEventUser.h:16-39; M/MenuControls.h:55-67] | Diff-sem | F4SE itself pushes into `MenuControls::inputEvents/handlers` [f4se/f4se/Hooks_Input.cpp:240-243]. An alternative is to hook `BSInputEventReceiver::PerformInputProcessing(const InputEvent*)` (vfunc 00) on `MenuControls`/`PlayerControls` [lx/include/RE/B/BSInputEventReceiver.h]. |
| `RE::InputEvent{eventType, device, next}`, `INPUT_EVENT_TYPE::{kButton,kMouseMove,kDeviceConnect,kThumbstick,kKinect}`, `INPUT_DEVICE::kNone` | EventHandler.cpp:1906-2081 | `InputEvent{device @08, deviceID @0C, eventType @10, next @18, timeCode @20, handled @24}`; `INPUT_EVENT_TYPE{kButton 0, kMouseMove 1, kCursorMove 2, kChar 3, kThumbstick 4, kDeviceConnect 5, kKinect 6, kNone 7}`; `INPUT_DEVICE{kNone -1, kKeyboard, kMouse, kGamepad, kKinect 3, kVirtualKeyboard}` [lx/include/RE/I/InputEvent.h; I/INPUT_EVENT_TYPE.h; I/INPUT_DEVICE.h] | Same + new types | New in FO4: `CharacterEvent{charCode}` and `CursorMoveEvent{cursorPosX,Y}`. |
| `ButtonEvent{idCode, userEvent, value, heldDownSecs, IsPressed/IsUp/IsDown/IsHeld/IsRepeating}` | EventHandler.cpp:1874-1886 | `IDEvent{strUserEvent @28, idCode @30, disabled @34}`; `ButtonEvent{value @38, heldDownSecs @3C, QPressed, QJustPressed, QReleased, QHeldDown, QHeldDownSecs}` [lx/include/RE/I/IDEvent.h; B/ButtonEvent.h] | Renamed | IsRepeating has no equivalent. |
| `MouseMoveEvent{mouseInputX/Y}` | 1920-1927 | same names, **int32** [lx/include/RE/M/MouseMoveEvent.h] | Same | |
| `ThumbstickEvent{xValue,yValue,IsLeft/IsRight}` | 1974-1983 | `xValue/yValue`; left/right via `idCode == THUMBSTICK_ID::kLeft(0xB)/kRight(0xC)` [T/ThumbstickEvent.h] | Renamed | |
| `DeviceConnectEvent{connected}`, `KinectEvent{heard}` | 1951-2016 | `connected`; `strHeard` | Renamed | |
| `RE::MenuEventHandler` (`CanProcess/ProcessButton/...`), `MenuControls::screenshotHandler`, `AddHandler/RemoveHandler`, `InputEvent::QUserEvent()` | DevApi.cpp:193-238 | `BSInputEventUser`; `MenuControls::screenshotHandler` (`ScreenshotHandler*` @0x58); `RegisterHandler/UnregisterHandler`; `InputEvent::QUserEvent()` [M/MenuControls.h:55-78; I/InputEvent.h] | Renamed | |
| `RE::MenuScreenData::GetSingleton()->mousePos` (SP class, 517043/403551) | main.cpp:257-258 | `RE::MenuCursor::GetSingleton()` (4803837) → `cursorPosX/Y` (int32 @0x24/0x28) [lx/include/RE/M/MenuCursor.h] | Renamed | SP stores `float*` into the struct, so it must read and convert each frame. |
| Tilted DInput8 hook (`DirectInput8Create` IAT → fake device) + `RegisterRawInputDevices` toggling | tilted/hooks/DInputHook.cpp:224-443 | — | Verify | FO4 imports `user32!RegisterRawInputDevices/GetRawInputData` (F4SE has a disabled hook for them) [f4se/f4se/Hooks_Input.cpp:15-56, 250-256]. Whether FO4 still creates DInput8 keyboard/mouse devices is **unverified** [inference: dump the Fallout4.exe import table for `dinput8.dll`]. If it does not, base the CEF input path on the WndProc hook (`WM_INPUT`/`WM_CHAR`, `tilted/hooks/WindowsHook.cpp`; note `sp/TPInputService.cpp` already contains a WndProc input path, currently unused). |

### 1.11 UI / menus / Scaleform / cursor

| SP symbol | SP usage | FO4 in lx | Status | Notes |
|---|---|---|---|---|
| `RE::UI::GetSingleton()` | main.cpp:352,391; BrowserApiTilted.cpp:51; FridaHooksUtils.h:9-34; PapyrusTESModPlatform.cpp:650 | `UI::GetSingleton()` (4796314) [lx/include/RE/U/UI.h:56-60] | Same | |
| `UI::IsMenuOpen(name)` | main.cpp:356,395; BrowserApiTilted.cpp:58 | `UI::GetMenuOpen(name)` / `GetMenuOpen<T>()` [UI.h:77-88] | Renamed | |
| `UI::GetMenu(name)` | FridaHooksUtils.h:10 | same → `Scaleform::Ptr<IMenu>` [UI.h:62-67] | Same | |
| `UI::GetMovieView(name)` | FridaHooksUtils.h:22,38 | `ui->GetMenu(name)->uiMovie` (`Scaleform::Ptr<Scaleform::GFx::Movie>` @0x40) [lx/include/RE/I/IMenu.h:199] | Renamed | |
| `UI::GameIsPaused()` | PapyrusTESModPlatform.cpp:651 | none (use `ui->menuMode` @0x1E0 / `UI::freezeFramePause`) [UI.h:128-132] | Missing (approx) | |
| `RE::GFxValue`, `GFxMovie::SetVarType::kSticky`, `GetVariable/SetVariable` | FridaHooksUtils.h:26-46 | `Scaleform::GFx::Value`; `Movie::GetVariable(Value*, const char*)`, `SetVariable(const char*, const Value&, SetVarType::kSticky)` [lx/include/Scaleform/G/GFx_Movie.h:41-44,185-186] | Renamed | **FO4 menus are AS3.** The AS2 paths `_root…._alpha` are invalid (AS3 uses `root1.…` and `alpha` in 0..1). |
| `RE::CursorMenu::MENU_NAME`, `RE::MainMenu::MENU_NAME` | main.cpp:356,395; FridaHooks.cpp:261-289; BrowserApiTilted.cpp:58-68 | `CursorMenu::MENU_NAME "CursorMenu"` (`cursor` = `BSGFxShaderFXTarget` @0xE0) [lx/include/RE/C/CursorMenu.h]; `MainMenu::MENU_NAME "MainMenu"` [M/MainMenu.h:9] | Same | |
| `RE::UIMessageQueue::GetSingleton()->AddMessage(name, UI_MESSAGE_TYPE::kShow/kHide, nullptr)` | BrowserApiTilted.cpp:52-69; PapyrusTESModPlatform.cpp:989-990 | `AddMessage(const BSFixedString&, UI_MESSAGE_TYPE)` (**2 args**); `UI_MESSAGE_TYPE{kUpdate 0, kShow 1, kReshow 2, kHide 3, kForceHide 4, …}` [lx/include/RE/U/UIMessageQueue.h; U/UI_MESSAGE_TYPE.h] | Renamed | |
| `RE::MenuOpenCloseEvent` | EventHandler.cpp:1826-1851 | same fields | Same | |
| `Offsets::Hooks::RenderCursorMenu` (32867/33632) | FridaHooks.cpp:254-300 | `IMenu::AdvanceMovie(float,u64)` (vfunc 04) / `PostDisplay` (06) of `CursorMenu` [IMenu.h:90-99]; `VTABLE::CursorMenu{245579,324946}` (R1) | Missing-RE | Resolve the slot from a live `CursorMenu` object (`ui->GetMenu("CursorMenu")` → `*(uintptr_t**)menu`) and Frida-attach to `vtbl[4]`. |

### 1.12 Rendering / D3D / camera

| SP symbol | SP usage | FO4 in lx | Status | Notes |
|---|---|---|---|---|
| `RE::BSRenderManager::GetSingleton()->swapChain` (SP class, 524907/411393) | main.cpp:558-564; game/classes/BSRenderManager.h | `RE::BSGraphics::GetRendererData()` (**2704429**) → `RendererData{device @0x48, context @0x50, renderWindow[32] @0x58}`; `RendererWindow{hwnd 00, …, swapChain @0x18}`; `GetCurrentRendererWindow()` (**2704431**) [lx/include/RE/B/BSGraphics.h:249-303, 366-376; IDs.h:586-587] | Renamed | At `BeginMain` time this is still null, as on Skyrim. The Present hook supplies the real swap chain. |
| `RE::BSGraphics::State::GetSingleton()->screenWidth/Height` | TextApi.cpp:477-517 | `BSGraphics::State::GetSingleton()` (2704621); `screenWidth` @0x80, `screenHeight` @0x84 (also `backBufferWidth/Height` @0x78/0x7C) [BSGraphics.h:589-606] | Same | |
| `RE::PlayerCamera::GetSingleton()->cameraRoot`, `IsInFirstPerson()` | CameraApi.cpp:6-22; TextApi.cpp:383-424 | `PlayerCamera::GetSingleton()` (4796065); `TESCamera::cameraRoot` (`NiPointer<NiNode>` @0x20); `QCameraEquals(CameraState::kFirstPerson)`; `CameraState{kFirstPerson 0 … k3rdPerson 8 …}` [lx/include/RE/P/PlayerCamera.h:35-120; T/TESCamera.h:29; C/CameraStates.h] | Renamed | Simpler: `RE::Main::WorldRootCamera()` returns `NiCamera*` (2228956) [lx/include/RE/M/Main.h:35-40]. |
| `RE::NiCamera::WorldPtToScreenPt3(worldToCam, port, pt, x, y, z, tol)`, `netimmerse_cast<NiCamera*>` | CameraApi.cpp:19-44; TextApi.cpp:419-453 | identical static (2270344); `worldToCam` @0x120, `port` @0x184 [lx/include/RE/N/NiCamera.h] | Same | `netimmerse_cast` exists in lx [inference: `RE/N/NiRTTI.h`]. |
| `RE::NiAVObject::GetObjectByName(BSFixedString)`, `world.translate/rotate` | TextApi.cpp:392-404, 444; MagicApi.cpp:121,132 | `GetObjectByName` (vfunc 0x2E); `world` @0x70 (NiTransform); `BSUtilities::GetObjectByName(root, name, tryInternal, dontAttach)` [lx/include/RE/N/NiAVObject.h:38-78; B/BSUtilities.h:26-31] | Same | |
| `RE::NiPoint2/NiPoint3/NiPointer` | many | same [lx/include/RE/N/NiPoint2.h; N/NiPoint3.h; N/NiPointer.h] | Same | SP's own `sp/NiPoint3.h` is game-agnostic math. |
| `RE::BSReadWriteLock`, `BSReadLockGuard/BSWriteLockGuard/BSSpinLockGuard` | BSRenderManager.h:39; InventoryApi.cpp:127; PapyrusTESModPlatform.cpp:642; AnimationGraphMasterBehaviourDescriptor.h:111 | `BSReadWriteLock`, `BSAutoReadLock`, `BSAutoWriteLock`, `BSAutoLock` (BSSpinLock) [lx/include/RE/B/BSSpinLock.h] | Renamed | |

### 1.13 Inventory / ExtraData / equipment

| SP symbol | SP usage | FO4 in lx | Status | Notes |
|---|---|---|---|---|
| `RE::ExtraDataList` (raw 32-byte malloc + presence 0x18) | PapyrusTESModPlatform.cpp:584-648 | `ExtraDataList : BSIntrusiveRefCounted` {`BaseExtraList extraData{_head,_tail,_flags}` @0x08, `extraRWLock` @0x20}, size 0x28; `AddExtra`, `GetByType<T>`, `HasType` [lx/include/RE/E/ExtraDataList.h:27-217; B/BaseExtraList.h:7-127] | Diff-sem | Allocate it properly (heap `new` with `F4_HEAP_REDEFINE_NEW`) and pass it as `BSTSmartPointer`. |
| `RE::BaseExtraList::PresenceBitfield`, `_extraData.GetPresence/GetData` | 608, 622-646 | `BaseExtraList::_flags` (u8*) | Diff-sem | Prefer `AddExtra`. |
| `RE::ExtraDataType::{kHealth,kCount,kEnchantment,kCharge,kTextDisplayData,kSoul,kPoison,kWorn,kWornLeft}` | InventoryApi.cpp:92-114; PapyrusTESModPlatform.cpp:684-733 | `EXTRA_DATA_TYPE::{kHealth,kCount,kEnchantment,kCharge,kTextDisplayData,kSoul,kPoison}` + FO4 `kObjectInstance` (BGSObjectInstanceExtra), `kInstanceData`, `kAmmo`, `kPowerArmor`, `kModRank`, `kPowerArmorPreload`; **no kWorn/kWornLeft** [lx/include/RE/E/EXTRA_DATA_TYPE.h:43-222] | Renamed | FO4 worn state = `BGSInventoryItem::Stack::flags & kSlotMask` (`IsEquipped()`) [lx/include/RE/B/BGSInventoryItem.h:20-40]. |
| `RE::ExtraHealth/ExtraCharge/ExtraTextDisplayData/ExtraPoison` | InventoryApi.cpp:9-70; PapyrusTESModPlatform.cpp:681-736 | exist [lx/include/RE/E/ExtraHealth.h:7; E/ExtraCharge.h:7; E/ExtraTextDisplayData.h:10; E/ExtraPoison.h:9] | Same/verify fields | |
| `RE::ExtraEnchantment/ExtraSoul/ExtraCount/ExtraWorn/ExtraWornLeft/ExtraContainerChanges`, `RE::SOUL_LEVEL` | same | not in lx (RTTI exists for ExtraEnchantment/ExtraSoul/ExtraCount only) | N/A / Missing | FO4 legendary effects come through OMODs (`BGSObjectInstanceExtra`). |
| `RE::BSSimpleList<ExtraDataList*>` (entry `extraLists`) | InventoryApi.cpp:143-165 | `BGSInventoryItem::Stack` chain (`nextStack`, `extra`, `count`, `flags`) | Diff-sem | |
| `RE::ActorEquipManager::GetSingleton()->EquipObject(actor, obj, extra, count, slot[, queue, force, sounds, applyNow])` / `UnequipObject(...)` | InventoryApi.cpp:334-337; PapyrusTESModPlatform.cpp:765-802 | `ActorEquipManager::GetSingleton()` (4798287); `EquipObject(Actor*, const BGSObjectInstance&, u32 stackID, u32 number, const BGSEquipSlot*, bool queueEquip, bool forceEquip, bool playSounds, bool applyNow, bool locked)` (2231392); `UnequipObject(Actor*, const BGSObjectInstance*, u32 number, const BGSEquipSlot*, u32 stackID, bool queue, bool force, bool sounds, bool applyNow, const BGSEquipSlot* replaced)` (2231395) [lx/include/RE/A/ActorEquipManager.h:56-96] | Diff-sem | Takes `BGSObjectInstance{object, instanceData}` and a stackID instead of an ExtraDataList. |
| `RE::BGSEquipSlot` | InventoryApi.cpp:246-310; PapyrusTESModPlatform.cpp:779-784 | `BGSEquipSlot` [lx/include/RE/B/BGSEquipSlot.h:8] | Same | Form IDs differ. |
| `RE::TESObjectARMO::IsShield()`, `RE::TESObjectWEAP`, `RE::TESAmmo`, `RE::TESBoundObject` | PapyrusTESModPlatform.cpp:665 | [lx/include/RE/T/TESObjectARMO.h:25; T/TESObjectWEAP.h:32; T/TESAmmo.h:20; T/TESBoundObject.h:18] | Verify | FO4 has no shield concept. Drop `isShieldLike`. |
| `RE::ITEM_REMOVE_REASON::kRemove` | CallNative.cpp:483; PapyrusTESModPlatform.cpp:751 | `ITEM_REMOVE_REASON` [lx/include/RE/I/ITEM_REMOVE_REASON.h:5] | Same | |
| `RE::malloc<T>/RE::free` | PapyrusTESModPlatform.cpp:46-613; PCH.h:109 | `RE::malloc/RE::free` (MemoryManager) [lx/include/RE/M/MemoryManager.h] | Same [inference: names; verify] | |

### 1.14 Console

| SP symbol | SP usage | FO4 in lx | Status | Notes |
|---|---|---|---|---|
| `RE::ConsoleLog::GetSingleton()->Print(fmt, ...)` | DumpFunctions.cpp:20-21; ExceptionPrinter.cpp:36; InGameConsolePrinter.cpp:7-42; PapyrusTESModPlatform.cpp:120-121,930-937; SkyrimPlatform.cpp:54-55 | `ConsoleLog::GetSingleton()` (4797437); `PrintLine(fmt, ...)` (printf style), `Print(fmt, va_list)` (2248591), `AddString(const char*)` (2248593), `Log(std::format…)` [lx/include/RE/C/ConsoleLog.h:8-59; IDs.h:962-967] | Renamed (`Print(...)` → `PrintLine(...)`) | |
| `Offsets::Hooks::VPrint` (50180/51110 + 0x163/0x300; call-site thunk `(void*, const char*)`) | Hooks.cpp:60-76 | detour `ConsoleLog::AddString` (2248593). Every console line passes through it [inference: `Print` → `AddString`] | Diff-sem | Use a Frida attach on `REL::ID(2248593).address()`, arg1 = `const char*`. |
| `RE::SCRIPT_FUNCTION::{GetFirstConsoleCommand, GetFirstScriptCommand, Commands::kConsoleCommandsEnd/kScriptCommandsEnd}` | ConsoleApi.cpp:382-389 | `SCRIPT_FUNCTION::GetConsoleFunctions()` (span[523], ID 901511), `GetScriptFunctions()` (span[818], ID 75173), `LocateConsoleCommand(name)`, `LocateScriptCommand(name)` [lx/include/RE/S/SCRIPT_FUNCTION.h:17-50; IDs.h:1950-1955] | Renamed | |
| `SCRIPT_FUNCTION::{functionName, shortName, numParams, executeFunction}`, `Execute_t` | ConsoleApi.cpp:19-22, 76-170, 280-370 | `functionName`, `shortName`, **`paramCount`**, `parameters`, `executeFunction` (`ExecuteFunction_t = bool(const SCRIPT_PARAMETER*, const char* compiledParams, TESObjectREFR*, TESObjectREFR*, Script*, ScriptLocals*, **float&** ret, u32& offset)`) [SCRIPT_FUNCTION.h:13-63] | Renamed + Diff-sem | The return type is `float&`, not `double&`. |
| `RE::Script::GetCommand()` | ConsoleApi.cpp:294 | `Script::text` (`char*` @0x38), `SetText()` [lx/include/RE/S/Script.h:55-69] | Renamed | |
| `RE::SCRIPT_PARAMETER{paramType}`, `RE::SCRIPT_PARAM_TYPE::{kChar,kInt,kFloat,kInventoryObject,kObjectRef,kActorValue,kSpellItem,kAxis,kStage,kActorBase,kPerk,kInvObjectOrFormList, 0x1A}` | ConsoleApi.cpp:246-278, 313 | same names and values; `0x1A` = `kContainerRef` [lx/include/RE/S/SCRIPT_PARAM_TYPE.h:7-60; S/SCRIPT_PARAMETER.h] | Same | |
| `RE::ScriptLocals` | ConsoleApi.cpp:284 | [lx/include/RE/S/ScriptLocals.h:11] | Same | |

### 1.15 Save / load

| SP symbol | SP usage | FO4 | Status | Notes |
|---|---|---|---|---|
| `RE::BGSSaveLoadManager::GetSingleton()->Load(name)` | LoadGame.cpp:118-122 | `BGSSaveLoadManager::GetSingleton()` (2697802) [lx/include/RE/B/BGSSaveLoadManager.h]. There is **no `Load(name)`**, only `QueueSaveLoadTask(QUEUED_TASK::kLoadGame)` (2228080), `queuedEntryToLoad` @0x48, `BuildSaveGameList` (2228053) | Missing-RE | F4SE hooks the real function: `bool LoadGame(BGSSaveLoadManager*, const char* name, u8 unk1, void* unk2)` at RVA `0xBEE760`@1.11.240; SaveGame is at 0xBEE0B0 and DeleteSaveGame at 0xBF9170 [f4se/f4se/Hooks_SaveLoad.cpp:15-27]. Convert the RVA to an AE ID with `REL::Offset2ID::GetSingleton()->load_v2(); get_id(0xBEE760)` on a 1.11.240 install [cs/include/REL/Offset2ID.h; cs/src/REL/Offset2ID.cpp:8-60]. |
| `.ess` writer (`savefile` lib), `template.ess`, `My Games\Skyrim Special Edition\Saves` | LoadGame.cpp:49-130 | `.fos` + `F4SE::GetSaveFolderName()` → `My Games/Fallout4/Saves` | Rewrite | Recommended: a template `.fos` plus `MoveRefrToPosition` (see the research doc). |
| `RE::TESLoadGameEvent` | LoadGame.cpp:16-65; PapyrusTESModPlatform.cpp:79-100; EventHandler.cpp:741-748 | `TESLoadGameEvent::GetEventSource()` (**2201848**) [lx/include/RE/T/TESLoadGameEvent.h] | Same | |

### 1.16 Magic (MagicApi)

All of MagicApi (`castSpellImmediate`, `interruptCast`, anim-variable blobs) is Skyrim combat-magic sync, so it is **N/A**. Replace it with a weapon-fire path based on `Weapon.Fire`, `Actor::SetGunState` and graph variables by name. See §1.8.

---

## 2. Offsets table

### 2.1 Every entry in `sp/game/Offsets.h` (SE/AE IDs from the file; FO4 = AE 1.11.x unless noted)

| Name (Offsets.h line) | Skyrim meaning | FO4 counterpart | FO4 AE ID / slot | Other refs | Status / approach |
|---|---|---|---|---|---|
| `Hooks::VPrint` (12-14) | call inside ConsoleLog::VPrint at +0x163/0x300. Thunk `(void* this, const char* msg)` = the AddString call | `ConsoleLog::AddString` (hook the entry) | **2248593** [lx IDs.h:965] | `Print` 2248591 | Found (different hook style) |
| `Hooks::FrameUpdate` (15-17) | call in Main::Update (+0x53/0x6e); **hook commented out** [Hooks.cpp:145] | Main loop | — | te OG `633525` "cMainLoop" | Not needed. Use `AddTaskPermanent` or the D3D Present hook. |
| `Hooks::SendEvent` (18-19) | `BSScript::Internal::VirtualMachine::SendEvent(VMHandle, const BSFixedString&, IFunctionArguments*)` | `IVirtualMachine` **vfunc 0x2B** `SendEvent(u64 handle, const BSFixedString&, const std::function<bool(BSScrapArray<Variable>&)>& args, const std::function<bool(const BSTSmartPointer<Object>&)>& filter, const BSTSmartPointer<IStackCallbackFunctor>&)` | `VTABLE::BSScript__Internal__VirtualMachine[0]` (R1) + `0x2B*8` | F4SE `QueueEvent` at 0x2B [f4se/f4se/PapyrusVM.h:75] | Found. Frida arg indices are unchanged (1 = handle, 2 = name). Blocking by swapping the name (FridaHooks.cpp:73-76) still works [inference]. |
| `Hooks::DrawSheatheWeaponPC` (20-21) | PlayerCharacter override of `DrawWeaponMagicHands(bool)` | `PlayerCharacter` vtable slot **0xC9** | `VTABLE::PlayerCharacter[0]` (R1) or a live `*(uintptr_t**)PlayerCharacter::GetSingleton()` | — | Found (slot). Semantics differ: also consider `ActorState::SetWeaponState` (ActorState vfunc 0x22) and `SetGunState`. |
| `Hooks::DrawSheatheWeaponActor` (22-23) | `Actor::DrawWeaponMagicHands(bool)` | `Actor` vtable slot **0xC9** [lx/include/RE/A/Actor.h:135] | `VTABLE::Actor[0]` (R1) or a live actor object | te OG `835807` "SetWeaponState" | Found (slot) |
| `Hooks::SendAnimation` (24-25) | `IAnimationGraphManagerHolder::NotifyAnimationGraph` impl reached by actors (`this` = holder subobject; refr = **rcx−0x38**) | `NotifyAnimationGraphImpl` = holder sub-vtable slot **1**; refr = **rcx−0x48** | likely `VTABLE::Actor[5]` / `VTABLE::PlayerCharacter[5]` [inference: TESObjectREFR has 8 polymorphic bases and an 8-entry VTABLE array; IAnimationGraphManagerHolder is the 6th] | — | Missing-RE (verify). Robust path: read `*(uintptr_t**)((char*)actor + 0x48)` → `[1]` at runtime, then Frida-attach. Hook both the Actor and the PlayerCharacter vtables. |
| `Hooks::QueueNinodeUpdate` (26-27) | Actor 3D update; captures the actor id for the tint swap | `Actor::Reset3D` | **2229913** | te OG `302889` | Found, but only useful for a tint redesign |
| `Hooks::ApplyMasksToRenderTargets` (28-30) | Skyrim facegen tint-mask rendering (AE only, FridaHooks.cpp:374-376) | none | — | — | N/A (FO4 tint pipeline differs) |
| `Hooks::RenderCursorMenu` (31-32) | CursorMenu render/advance (rcx = menu) | `CursorMenu` IMenu vfunc 4 `AdvanceMovie` (or 6 `PostDisplay`) | `VTABLE::CursorMenu[0]` (R1) | — | Missing-RE (pick the slot). The swf paths need RE (AS3). |
| `EventSource::ActorKill` (36) | story event getter | `ActorKill::Event` | — (struct only) [lx/include/RE/A/ActorKill.h] | RTTI `BSTEventSink_ActorKill__Event_` | Missing-RE |
| `EventSource::BooksRead` (37) | ″ | `BooksRead::Event` | — | RTTI | Missing-RE |
| `EventSource::CriticalHit` (38) | ″ | `CriticalHitEvent::Event` | — | RTTI | Missing-RE |
| `EventSource::DisarmedEvent` (39) | ″ | `DisarmedEvent::Event` | — | RTTI | Missing-RE |
| `EventSource::DragonSoulsGained` (40) | ″ | none | — | — | N/A |
| `EventSource::ItemHarvested` (41) | ″ | `TESHarvestEvent::ItemHarvested{itemHarvested, referenceHarvested, harvestedBy}` | **2193351** [lx IDs.h:2187] | | Found |
| `EventSource::LevelIncrease` (42) | ″ | `LevelIncrease::Event{newLevel}` | **2233736** [lx IDs.h:1449] | | Found (fields differ: no `player`) |
| `EventSource::LocationDiscovery` (43) | ″ | `LocationMarkerArrayUpdate::Event` / `LocationLinked::Event` (RTTI) | — | | Missing-RE |
| `EventSource::ShoutAttack` (44) | ″ | none | — | | N/A |
| `EventSource::SkillIncrease` (45) | ″ | none (no skills); nearest is `PerkPointIncreaseEvent` | **4804734** [lx IDs.h:1647] | | N/A / replace |
| `EventSource::SoulsTrapped` (46) | ″ | none | — | | N/A |
| `EventSource::SpellsLearned` (47) | ″ | none | — | | N/A |
| `BSRenderManager::Singleton` (51) | renderer singleton (swapChain @0x70) | `BSGraphics::GetRendererData()` (`RendererData**`) | **2704429**; window **2704431** | te OG `1235450` renderer, `454122` device | Found |
| `MenuScreenData::Singleton` (55) | cursor pos / screen size | `MenuCursor` + `BSGraphics::State` | **4803837**, **2704621** | | Found |
| `WinMain` (58) | game WinMain (Tilted hooks it to run BeginMain) | WinMain | — | te OG **668529** | Missing-RE, or remove the need for it (§2.2 #W) |
| `GetInvalidRefHandle()` (60-65) | global "invalid handle" | `ObjectRefHandle{}` (0) | — | te OG `888642` | Not needed |
| `Unknown(unk1, unk2, refr)` (71-76) | Skyrim Papyrus native impl of `ObjectReference.ClearDestruction` | Papyrus native `ClearDestruction` | — | — | Missing-RE (direct). Use the VM path instead. |
| `PushActorAway(vm, stack, self, target, mag)` (78-84) | Papyrus native impl `ObjectReference.PushActorAway` | Papyrus native `PushActorAway` | — | te OG `803088` "knockExplosion"? [inference] | Missing-RE (direct). Use the VM path instead. |
| `TESDataHandlerExtension::CreateReferenceAtLocationImpl` (88-109) | `TESDataHandler::CreateReferenceAtLocation` (10 args) | `TESDataHandler::CreateReferenceAtLocation(NEW_REFR_DATA&)` | **2192301** [lx IDs.h:2114] | al OG 500304; te OG 500305 | Found |

### 2.2 Other hardcoded IDs, offsets and vtable slots

| # | Skyrim value (SP location) | Meaning | FO4 value | Provenance / status |
|---|---|---|---|---|
| V1 | vtable slot **0x18** (Hooks.cpp:90) | `IVirtualMachine::BindNativeMethod` | **0x1B** | [lx/include/RE/B/BSScript_IVirtualMachine.h:75] + [f4se/f4se/PapyrusVM.h:55]. Found. |
| V2 | `func+0x50` (Hooks.cpp:107; GetNativeFunctionAddr.cpp:131-134 via `sizeof(NativeFunctionBase)`) | native callback ptr | +0x50 = F4SE `m_callback`; CLib-F4 = `std::function`; vanilla = ? | [f4se/f4se/PapyrusNativeFunctions.h:150; lx/include/RE/B/BSScriptUtil.h:1280-1340]. Partly Missing-RE. |
| V3 | `func+0x58` byte when module == skse64 (Hooks.cpp:115-116; GetNativeFunctionAddr.cpp:136-137) | SKSE long-signature flag | no equivalent. F4SE: "long" ⇔ latent | [f4se/f4se/PapyrusNativeFunctionDef_Base.inl:87-99] |
| V4 | `IFunction+0x42` (GetNativeFunctionAddr.cpp:139-140) | `isLatent` | **0x42** (unchanged) | [lx/include/RE/B/BSScript_Internal_NativeFunctionBase.h:153; f4se/f4se/PapyrusNativeFunctions.h:126] |
| V5 | `BSRenderManager+0x70` (BSRenderManager.h:33) | swap chain | `RendererData+0x58+0x18` = **0x70** (coincidence; use the named member) | [lx/include/RE/B/BSGraphics.h:269, 303] |
| V6 | `BSRenderManager+0x48/0x50` device/context | | `RendererData+0x48/0x50` | ″ |
| V7 | `rcx-0x38` (FridaHooks.cpp:166) | holder → TESObjectREFR | **rcx-0x48** | [lx/include/RE/T/TESObjectREFR.h:78] |
| V8 | `MenuScreenData+0x04` (mousePos floats) | cursor | `MenuCursor+0x24/+0x28` (int32) | [lx/include/RE/M/MenuCursor.h] |
| V9 | `hkbVariableValueSet::varSet @0x10`, `size @0x18` (Magic/hkbVariableValueSet.h:12-13) | Havok var array | unknown (FO4 Havok 2014) | Missing-RE. Use name-based graph vars instead. |
| V10 | Skyrim master-behavior var indices (Magic/AnimationVariablesForMasterBehavior.h) | | n/a | N/A. Rediscover by dumping FO4 graphs by name. |
| V11 | ExtraDataList 24+8 bytes, presence 0x18 (PapyrusTESModPlatform.cpp:586-607) | raw extra list | 0x28, ref-counted | [lx/include/RE/E/ExtraDataList.h:217] |
| V12 | `0xFF000000` dynamic-form base (PapyrusTESModPlatform.cpp:497-552, 969-975) | created forms | same convention (0xFF…) [inference] | |
| V13 | `0x14` player ref (InventoryApi.cpp:335; TextApi.cpp:382; AnimationGraphMasterBehaviourDescriptor.h:117) | | **0x14** (player NPC 0x7) | [lx/include/RE/T/TESForm.h:275-276] |
| V14 | `0x38` GameHour GLOB (LoadGame.cpp:171-173) | | check `Fallout4.esm` | Missing-data |
| V15 | `0xFF0014FE` nextObjectId (LoadGame.cpp:307) | `.ess` field | `.fos` rewrite | N/A |
| V16 | `kLightModOffset 0x100` (CallNativeApi.cpp:42) | ESL index | same model (`compiledFileCollection.smallFiles`) | [lx/include/RE/T/TESDataHandler.h:115-153] |
| V17 | `SCRIPT_PARAM_TYPE 0x1A` (ConsoleApi.cpp:259) | container ref | `kContainerRef = 0x1A` | [lx/include/RE/S/SCRIPT_PARAM_TYPE.h:33] |
| V18 | DXGI `Present` = vtable index 8 (tilted/hooks/D3D11Hook.cpp:58) | IDXGISwapChain | 8 (COM ABI) | Same |
| V19 | `msvcr110.dll!__crtGetShowWindowMode` + `api-ms-win-crt-runtime-l1-1-0.dll!_get_narrow_winmain_command_line` (tilted/reverse/Entry.cpp:104-110) | pre-WinMain hook | FO4 imports `_get_narrow_winmain_command_line` from `api-ms-win-crt-runtime-l1-1-0.dll`, and F4SE loads plugins from that exact import hook | [f4se/f4se/f4se.cpp:53-87, 177-198]. The msvcr110 hook is dead weight. |
| V20 | AS2 paths `_root.mc_Cursor._alpha`, `_root.MenuHolder.Menu_mc.MainListHolder.List_mc._alpha` (FridaHooks.cpp:270-288) | hide cursor / main list | AS3 paths in `Interface/CursorMenu.swf`, `MainMenu.swf` | Missing-RE (decompile with JPEXS from `Fallout4 - Interface.ba2`) |
| V21 | `"SkyrimSE.exe"`, `"skse64_1_6_1170.dll"`, `"Data/SKSE/Plugins/…"`, `"My Games\\Skyrim Special Edition"` | | `Fallout4.exe`, `f4se_*.dll`, `Data/F4SE/Plugins/`, `F4SE::GetSaveFolderName()` | Adapt |

**#W, removing the WinMain dependency** [inference, high confidence]
- F4SE calls `F4SEPlugin_Load` from inside its `_get_narrow_winmain_command_line` IAT hook. The CRT calls that import immediately before calling WinMain [src: f4se/f4se/f4se.cpp:53-60, 177-198].
- The Impl DLL is loaded during `F4SEPlugin_Load`. At that moment no D3D device, DInput object or window class exists yet.
- So `SkyrimPlatformApp::BeginMain()` (IAT hooks for `D3D11CreateDeviceAndSwapChain`, `DirectInput8Create`, `RegisterClass*`) can run directly in the load path, or in Tilted's own `_get_narrow_winmain_command_line` detour. `GetMainAddress()` can then return `nullptr`; `Entry.cpp:48-49` already handles null.
- Run `EndMain()` from `DLL_PROCESS_DETACH`.
- Verify by checking that the game imports `d3d11.dll!D3D11CreateDeviceAndSwapChain` [inference: inspect the IAT with dumpbin]. If it creates the device another way, hook `IDXGISwapChain::Present` through a dummy swap chain instead.

### 2.3 Unchanged layout facts SP depends on (FO4 = Skyrim)

`IVirtualMachine` +0x08 refcount; `NativeFunctionBase` size 0x50 with `isLatent` @0x42; `StackFrame` 0x40 with `self` @0x28 and `size` @0x38; `Variable` 0x10 = `{TypeInfo, union}`; `TypeInfo` 8 bytes; `Object::handle` @0x20; VM handles are u64 and stack ids are u32.
[src: lx/include/RE/B/BSScript_*.h static_asserts]

### 2.4 Risk R1: VTABLE IDs

- lx `IDs_VTABLE.h` holds **OG-space** IDs; `IDs.h` and `IDs_RTTI.h` use AE space. Evidence: lx VTABLE `Actor{1455516,…}` == al VTABLE `Actor{1455516,…}`, while lx RTTI `Actor{4839606}` ≠ al `{1162828, 2764355}` [src: lx/include/RE/IDs_VTABLE.h:25; lx/include/RE/IDs_RTTI.h:27; al/CommonLibF4/include/RE/RTTI_IDs.h:27].
- lx uses `REX::EMPLACE_VTABLE` (`T::VTABLE[0].address()`) in 68 places [src: cs/include/REX/CAST.h:23-26], so they may still resolve on AE. This is **unverified**.
- Verification (first-run self-test): call `REL::ID(614600).address()` inside `__try`, or simply check `IDDB::offset` for each needed vtable ID.
- Fallbacks, from easiest:
  1. Read the vtable from a live object (works for UI menus, PlayerCharacter and actors after data load).
  2. RTTI scan: take the AE RTTI ID (e.g. `RTTI::BSScript__Internal__VirtualMachine = 4862735` [lx/include/RE/IDs_RTTI.h:3296]), find the `CompleteObjectLocator` in `.rdata` whose `typeDescriptor` RVA matches and whose `offset == 0`, then take the vtable that follows the pointer to that COL. lx ships the RTTI structs [lx/include/RE/RTTI.h].
  3. `REL::Offset2ID` on a known RVA.
- The VM vtable is needed **before** GameVM exists, so fallback 1 cannot be used for BindNativeMethod. Use 2 or 3.

---

## 3. F4SE vs SKSE interfaces used by SP

### 3.1 Init and load

| SKSE (SP) | F4SE / lx | Notes |
|---|---|---|
| exports `SKSEPlugin_Load(void*)` (entry) and `SKSEPlugin_Version` (via CMake macro) | exports `F4SEPlugin_Load(const F4SE::LoadInterface*)` + data `F4SEPlugin_Version` | F4SE scans `Data\F4SE\Plugins\*.dll` with `LoadLibraryEx(LOAD_LIBRARY_AS_IMAGE_RESOURCE)`, reads the version **data export** and checks compatibility before calling any code [f4se/f4se/PluginManager.cpp:481-541, 618-707]. A DLL without `F4SEPlugin_Version` is skipped with "no version data", and its **DllMain is never run** (image-resource load). |
| `SKSE::Init` | `F4SE::Init(intfc, InitInfo)` | also queries all interfaces [lx/src/F4SE/API.cpp:163-182] |
| — | `F4SEPlugin_Preload(const F4SE::PreLoadInterface*)` optional (`F4SE_PLUGIN_PRELOAD`) | runs before global initializers [f4se/f4se/f4se.cpp:43-51, 176] |

**Interface ids** `kMessaging 1, kScaleform 2, kPapyrus 3, kSerialization 4, kTask 5, kObject 6, kTrampoline 7` [src: lx/include/F4SE/Interfaces.h:165-175; f4se/f4se/PluginAPI.h:18-29].

### 3.2 Messaging: SKSE → F4SE type mapping (SP's `HandleSKSEMessage`, EventHandler.cpp:65-90)

| SKSE type | SP JS event | F4SE type (lx `F4SE::MessagingInterface::k…`) | Notes |
|---|---|---|---|
| `kDataLoaded` | `skyrimLoaded` + `EventManager::Init()` + start TickHandler | `kGameDataReady` (10) | Sent with `data == false` before loading and `true` when finished [f4se/f4se/PluginAPI.h:119]. React only when `(bool)msg->data == true`. Rename the event to e.g. `falloutLoaded` (keep an alias). |
| `kNewGame` | `newGame` | `kNewGame` (8) | data = chargen `TESQuest*` |
| `kPreLoadGame` | `preLoadGame` | `kPreLoadGame` (2) | data = save path |
| `kPostLoadGame` | `postLoadGame` | `kPostLoadGame` (3) | data = bool success |
| `kSaveGame` | `saveGame` | `kPreSaveGame` (4) / `kPostSaveGame` (5) | Pick post. |
| `kDeleteGame` | `deleteGame` | `kDeleteGame` (6) | |
| (`kPostLoad`, `kInputLoaded`) | — | `kPostLoad` 0, `kPostPostLoad` 1, `kInputLoaded` 7, `kGameLoaded` 9 | `kGameLoaded` = "after the game has finished loading (only sent once)" |

Enum values from [lx/include/F4SE/Interfaces.h:204-217; f4se/f4se/PluginAPI.h:100-120]. The listener is a plain function pointer with the `F4SEAPI` (`__cdecl`) convention. NirnLab's `HandleSkseMessage` is dropped.

### 3.3 Task interface
- `AddTask`, `AddUITask`, `AddTaskPermanent`, with std::function overloads [lx/include/F4SE/Interfaces.h:388-424].
- Interface version 2 [f4se/f4se/PluginAPI.h:199-210].

### 3.4 Papyrus interface
- `Register(bool(*)(IVirtualMachine*))` and `GetExternalEventRegistrations` [lx/include/F4SE/Interfaces.h:354-379].
- Plugin callbacks run from F4SE's hook at `RegisterPapyrusFunctions_Start` (RVA `0x0113EC60+0x461`@1.11.240). That is **after** vanilla natives are bound [f4se/f4se/Hooks_Papyrus.cpp:1, 37-60].
- So the BindNativeMethod vtable hook must be installed in `F4SEPlugin_Load`, not in the Papyrus callback, or vanilla natives are missed.

### 3.5 Trampoline
- `F4SETrampolineInterface::AllocateFromBranchPool/LocalPool` [f4se/f4se/PluginAPI.h:226-237].
- lx wires it through `InitInfo.trampoline` [lx/src/F4SE/API.cpp:117-136].
- F4SE's pool is 64 KiB minus a 512-byte reserve [f4se/f4se/f4se.cpp:32-33].

### 3.6 Plugin version data and the Address Library in lx: consequences for SP's two-DLL layout

- SP layout: `SkyrimPlatform.dll` sits in `Data/SKSE/Plugins`. `SkyrimPlatformImpl.dll` sits in `Data/Platform/Distribution/RuntimeDependencies` and is loaded after PATH is extended [src: skyrim_platform_entry/main.cpp:40-57; tools/dev_service/index.js:172-177].
- lx's `IDDB` finds its loader from **the module that contains the commonlib code** (`__ImageBase`). It requires `parent(parent(dll)) == "F4SE"` and looks for `version-*.bin` in `parent(dll)` [src: cs/src/REL/IDDB.cpp:143-185; cs/src/REX/W32.cpp:521-524]. An Impl DLL in `Data/Platform/...` therefore triggers **`REX::FAIL("Failed to determine Address Library loader!")`**.
- `PluginVersionData::GetSingleton()` = `GetProcAddress(GetCurrentModule(), "F4SEPlugin_Version")` [lx/src/F4SE/Interfaces.cpp:129-132]. In the Impl DLL this returns null, which is harmless (it falls back to the file stem).
- **Recommended layout:**
  - `Data/F4SE/Plugins/FalloutPlatform.dll`: entry. Exports `F4SEPlugin_Version` and `F4SEPlugin_Load`. Contains no commonlib code.
  - `Data/F4SE/Plugins/FalloutPlatformImpl.dll`: no version export, so F4SE only image-loads it and skips it. The entry DLL `LoadLibrary`s it after adding `Data/Platform/Distribution/RuntimeDependencies` to the DLL search path (`AddDllDirectory` + `SetDefaultDllDirectories`, or PATH as today).
  - `Data/F4SE/Plugins/version-1-11-<build>-0.bin` (Address Library, Nexus mod 47327) [web: https://www.nexusmods.com/fallout4/mods/47327].
- Alternative: merge both into one DLL and `/DELAYLOAD` `libnode.dll` and `libcef.dll`.
- `MpClientPlugin.dll` can also live in `Data/F4SE/Plugins`. F4SE will log "no version data" and ignore it.

---

## 4. FO4 engine facilities in lx relevant to multiplayer

### 4.1 Papyrus VM internals

**`RE::GameVM`**
- Singleton ID 4796420; `impl` @0xB0; `handlePolicy` @0x1E0; `objectBindPolicy` @0x288.
- Bases include sinks for `PositionPlayerEvent`, `TESInitScriptEvent` and others.
- `QueuePostRenderCall(BSTSmartPointer<GameScript::DelayFunctor>)` and `SendEventToObjectAndRelated(handle, name, args, filter, callback)`.
- [src: lx/include/RE/G/GameScript.h:498-580]

**`IVirtualMachine` vtable (index: name)** [src: lx/include/RE/B/BSScript_IVirtualMachine.h:46-109]

`01 SetLoader, 02 SetLinkedCallback, 03 Update, 04 UpdateTasklets, 05 SetOverstressed, 06 IsCompletelyFrozen, 07 RegisterObjectType, 08 GetScriptObjectType(name), 09 GetScriptObjectType(id), 0A/0B …NoLoad, 0C GetTypeIDForScriptObject, 0D GetScriptObjectsWithATypeID, 0E GetParentNativeType, 0F TypeIsValid, 10 ReloadType, 11 TasksToJobs, 12 CalculateFullReloadList, 13 GetScriptStructType, 14 GetScriptStructTypeNoLoad, 15 GetChildStructTypes, 16 CreateObject(props), 17 CreateObject, 18 CreateStruct, 19 CreateArray(TypeInfo), 1A CreateArray(raw, name), **1B BindNativeMethod**, 1C/1D SetCallableFromTasklets, 1E ForEachBoundObject, 1F FindBoundObject, 20 MoveBoundObjects, 21 ResetAllBoundObjects, 22 CastObject, 23 SetPropertyValue, 24 GetPropertyValue, 25/26 GetVariableValue, 27 HandleImplementsEvent, 28 AddEventRelay, 29 RemoveEventRelay, 2A RemoveAllEventRelays, **2B SendEvent**, **2C DispatchStaticCall**, 2D DispatchMethodCall(obj), **2E DispatchMethodCall(handle, objName, fn, …)**, 2F DispatchUnboundMethodCall, 30 IsWaitingOnLatent, 31 ReturnFromLatent, 32 GetErrorLogger, 33/34 GetObjectHandlePolicy, 35/36 GetObjectBindPolicy, 37 GetSavePatcherInterface, 38-3B Log/Stats event registration, 3C/3D PostCachedErrorToLogger.`

**Signatures**
- `DispatchStaticCall(const BSFixedString& obj, const BSFixedString& fn, const BSTThreadScrapFunction<bool(BSScrapArray<Variable>&)>& args, const BSTSmartPointer<IStackCallbackFunctor>& cb)`
- `DispatchMethodCall(u64 handle, const BSFixedString& obj, const BSFixedString& fn, args, cb)`
- Templates `DispatchStaticCall(obj, fn, cb, Args...)` / `DispatchMethodCall(handle, obj, fn, cb, Args...)` pack the arguments via `PackVariable` [lx/include/RE/B/BSScriptUtil.h:1398-1447].
- Both lx and al alias `BSTThreadScrapFunction<F> = std::function<F>`, and F4SE passes a `std::function` too (`SendPapyrusEvent`) [f4se/f4se/PapyrusVM.h:293]. The MSVC STL `std::function` ABI matches the game's [inference: works in shipped plugins].

**`Internal::VirtualMachine` members** (offsets)
- `errorLogger 80`, `handlePolicy 90`, `objectBindPolicy 98`, `typeInfoLock C0`
- `objectTypeMap 168`, `structTypeMap 198`, `typeIDToObjectType 1C8`, `objectTypeToTypeID 1F8`
- `funcMsgQueue 8260`
- `runningStacksLock BD58`, `allRunningStacks BD60`, `waitingLatentReturns BD90`, `nextStackID BDC0`
- `attachedScriptsLock BDF8`, `attachedScripts BE00`
- `allStructs BE68`, `arrays BE90`, `eventRelays BFB8`
- [src: lx/include/RE/B/BSScript_Internal_VirtualMachine.h:216-291]
- It also implements `IVMObjectBindInterface` (`GetBoundHandle`, `BindObject`, …), `IVMSaveLoadInterface` and `IVMDebugInterface` (`DumpRunningStacksToLog` …) [same file 168-209].

**Functions and frames**
- `NativeFunction` / `NativeFunctionBase` / `IFunction`: §1.4.
- `IFunction::Call(const BSTSmartPointer<Stack>&, ErrorLogger&, Internal::VirtualMachine&, bool inTasklet)` (vfunc 0F).
- `NativeFunctionBase` adds `15 HasStub`, `16 MarshallAndDispatch(Variable& self, VM&, u32 stackID, Variable& ret, const StackFrame&)`.
- `Stack` / `StackFrame`: §1.4. `Stack::State` = `kRunning … kRetryCall`. `GetStackFrameVariable` / `GetPageForFrame` have IDs.

**Variables and types**
- `Variable`: union of `o` (Object), `s`, `u`, `i`, `f`, `b`, `v` (**Var**: `Variable*`), `t` (**Struct**), `a` (Array).
- `TypeInfo::RawType` includes `kVar = 6`, `kStruct = 7`, `kArrayVar = 0x10`, `kArrayStruct = 0x11`.
- `TypeInfo` holds an `IComplexType*` for objects and structs; use `GetObjectTypeInfo()` / `GetStructTypeInfo()`.
- [src: lx/include/RE/B/BSScript_Variable.h:195-226; B/BSScript_TypeInfo.h]

**`Struct` and `StructTypeInfo`**
- `Struct{structLock 04, type 10, constructed 18, valid 19, variables[] 20}` [lx/include/RE/B/BSScript_Struct.h].
- `StructTypeInfo{name 10, containingObjTypeInfo 18, variables (StructVar{initialValue, varType, docString, userFlags, isConst}) 20, varNameIndexMap 38}` [B/BSScript_StructTypeInfo.h:33-59].
- The typed helper is `structure_wrapper<"Script","Struct">` with `find<T>` / `insert` [B/BSScriptUtil.h:54-110].
- `CreateStruct` is vfunc 0x18.

**`Array`**: `{elementType 08, elementsLock 10, elements BSTArray<Variable> 18}` with `size()` and `operator[]` [B/BSScript_Array.h:65-79]. `CreateArray` is vfunc 0x19/0x1A.

**`ObjectTypeInfo`**: `GetName`, `GetParent`, global, member and state function iterators, variables and properties [B/BSScript_ObjectTypeInfo.h:18-186].

**Handle policy**
- `IObjectHandlePolicy` vtable: §1.4.
- Game impl is `GameScript::HandlePolicy` in `GameVM` @0x1E0.
- VM type ids: `ENUM_TYPE_ID` [E/ENUM_TYPE_ID.h].
- Names are namespaced (`Workshop:Foo`).

### 4.2 Event sources

**(a) Script / TES events with an lx getter** (`static BSTEventSource<E>* E::GetEventSource()`, header `lx/include/RE/T/<E>.h`):

| Event | AE ID | Fields |
|---|---|---|
| TESActivateEvent | 2201819 | `objectActivated`, `actionRef` (NiPointer) |
| TESCellAttachDetachEvent | 2201823 | `refr`, `isAttaching` |
| TESCellFullyLoadedEvent | 2201824 | `cell` |
| TESContainerChangedEvent | 2201832 | `oldContainerFormID, newContainerFormID, baseObjectFormID, itemCount, referenceFormID, uniqueID` |
| TESDeathEvent | 2201833 | `actorDying, actorKiller, dead` |
| TESEnterSneakingEvent | 2201837 | |
| TESEquipEvent | 2201838 | `actor, baseObject (formID), originalRefr, uniqueID, equipped` |
| TESFormDeleteEvent | 2201842 | |
| TESFurnitureEvent | 2201844 | `actor, targetFurniture, type{kEnter,kExit}` |
| TESInitScriptEvent | 2201846 | `hObjectInitialized` |
| TESLoadGameEvent | 2201848 | (empty) |
| TESLocationClearedEvent | 2201849 | |
| TESMagicEffectApplyEvent | 2201851 | `target, caster, magicEffectFormID` |
| TESObjectLoadedEvent | 2201853 | `formID, loaded` |
| TESSwitchRaceCompleteEvent | 2201874 | `actor` |
| TESHitEvent | 2201886 | `hitData (HitData: flags{kBlocked,kCritical,kSneakAttack,kBash,kPowerAttack,kMeleeAttack,kRicochet,kExplosion,…}, damageLimb, weapon (BGSObjectInstanceT), ammo, totalDamage, physicalDamage, …), target, cause, material, sourceFormID, projectileFormID, usesHitData` [lx/include/RE/H/HitData.h:20-76] |
| CellAttachDetachEventSource | 2192250 | |
| TESHarvestEvent::ItemHarvested | 2193351 | |
| LevelIncrease::Event | 2233736 | `newLevel` |
| LocksPicked::Event | 2249292 | |
| TerminalHacked::Event | 2233751 | |

[src: lx/include/RE/IDs.h:905-2410 and each header]

**(b) TES events that exist in FO4 but have no lx getter.**
Their existence is confirmed by `BSTEventSink<…>` RTTI names in [lx/include/RE/IDs_RTTI.h].

- **Candidate AE IDs [inference]:**
  - All 15 known `TES*Event::GetEventSource` IDs fit an alphabetical sequence, `2201819 + slot` (only TESHitEvent breaks it). The F4SE 1.11.240 RVAs of TESCombat/Death/Furniture/InitScript/LoadGame/ObjectLoaded are spaced exactly `0xD0` per alphabetical slot: 0x530EE0 (slot 26), 0x531490 (33), 0x531D80 (44), 0x531F20 (46), 0x5320C0 (48), 0x5324D0 (53) [src: f4se/f4se/GameEvents.h:225-230].
  - So `ID ≈ 2201793 + slot` and `RVA(1.11.240) ≈ 0x530EE0 + (slot−26)·0xD0`.
- **Verify each candidate** two ways:
  1. Confirm the function at the ID returns the same pointer shape as its neighbours.
  2. Register a sink and observe the event in game.

| Event (SP uses) | candidate ID | candidate RVA@1.11.240 |
|---|---|---|
| TESActiveEffectApplyRemoveEvent | 2201820 | 0x5309F0 |
| TESActorLocationChangeEvent | 2201821 | 0x530AC0 |
| TESBookReadEvent | 2201822 | 0x530B90 |
| TESCellReadyToApplyDecalsEvent | 2201825 | 0x530E00 (off-pattern; verify) |
| TESCombatEvent | 2201826 (F4SE RVA **0x530EE0** known) | 0x530EE0 |
| TESDestructionStageChangedEvent | 2201835 | 0x531630 |
| TESEnterBleedoutEvent | 2201836 | 0x531700 |
| TESGrabReleaseEvent | 2201845 | 0x531E50 |
| TESLockChangedEvent | 2201850 | 0x532260 |
| TESObjectREFRTranslationEvent | 2201854 | 0x5325A0 |
| TESOpenCloseEvent | 2201856 | 0x532740 |
| TESPackageEvent | 2201857 | 0x532810 |
| TESPerkEntryRunEvent | 2201858 | 0x5328E0 |
| TESQuestInitEvent / QuestStage / QuestStartStop | 2201861 / 2201862 / 2201864 | 0x532B50 / 0x532C20 / 0x532DC0 |
| TESResetEvent | 2201865 | 0x532E90 |
| TESSceneActionEvent | 2201867 | 0x533030 |
| TESSellEvent | 2201870 | 0x5332A0 |
| TESSleepStart / Stop | 2201871 / 2201872 | 0x533370 / 0x533440 |
| TESSpellCastEvent | 2201873 | 0x533510 |
| TESTrackedStatsEvent | 2201876 | 0x5336B0 |
| TESTriggerEnter / Leave | 2201878 / 2201879 | 0x533850 / 0x533920 |
| TESUniqueIDChangeEvent | 2201880 | 0x5339F0 |
| TESWaitStart / Stop | 2201881 / 2201882 | 0x533AC0 / 0x533B90 |
| (FO4 extras) TESCommandMode{CompleteCommand,Enter,Exit,GiveCommand} 27-30, TESConsciousness 31, TESDeferredKill 34, TESEscortWaitStart/Stop 39/40, TESExitFurniture 41, TESFormIDRemap 43, TESLimbCripple 47, TESOnPCDialogueTarget 55, TESPickNewIdle 59, TESPickpocketFailed 60, TESQuestStageItemDone 63, TESResolveNPCTemplates 66, TESSceneEvent 68, TESScenePhase 69, TESTopicInfo 75, TESTrapHit 77 | 2201793+slot | |

- Slot 52 (between MagicEffectApply and ObjectLoaded) is occupied by an event that has no sink class; possibly vestigial `TESMagicWardHitEvent` or `TESMoveAttachDetachEvent`.
- **Absent in FO4** (no RTTI sink): `TESTriggerEvent` (plain), `TESFastTravelEndEvent`, `TESMagicWardHitEvent`, `TESMoveAttachDetachEvent`, `TESPlayerBowShotEvent`. For these SP sinks, drop the sink or replace it (e.g. `PlayerAmmoCountEvent`/`WeaponFiredEvent` instead of bow shot).
- **No lx structs** exist for the candidate events. Field layouts must be RE'd. Start from al (`al/.../Events.h` has a few) and SP's own Skyrim layouts in `sp/game/Events.h`, which are often identical in FO4 [inference].

**(c) Story events (`X::Event`) present in FO4** (from RTTI)
- ActorKill, AssaultCrime, Bleedout, BobbleheadCollected, BooksRead, ChestLooted, CorpseEaten, CriticalHitEvent, DaysPassed, DisarmedEvent, ExitPowerArmor, FatmanDeaths, **FirstThirdPersonSwitch**, FusionCoreConsumed/Ejected, HolotapeStateChanged, HourPassed/HoursPassed, InventoryKill, InvestmentMade, ItemCrafted, ItemPickpocketed, ItemSteal, JunkItemFound, LevelIncrease, LocationCleared, LocationLinked, LocationMarkerArrayUpdate, LocksPicked, MineDisarmed, MurderCrime, MysteriousStrangerVisits, ObjectiveState, PerkAdded, PlayerActiveEffectChanged, PlayerAddicted, PlayerCharacterQuestEvent, PlayerDifficultySettingChanged, PlayerInDialogueChanged, PlayerLifeStateChanged, **PreloadPowerArmor**, QuestStatus, RicochetHit, SandmanKill, SpeechChallengeSucceeded, TerminalHacked, TravelMarkerStateChange, Trespass.
- Only LevelIncrease, LocksPicked and TerminalHacked have lx getters. The others need RE: find the `BSTGlobalEvent::EventSource<X::Event>` singleton via RTTI → vtable → ctor xref.

**(d) `BSTGlobalEvent::EventSource<T>` (UI/HUD/player-state) present in FO4** [lx/include/RE/IDs_RTTI.h]
- MP-relevant: **PlayerAmmoCountEvent, PlayerWeaponReloadEvent, PlayerSetWeaponStateEvent, MeleeAttackJustReleasedEvent, PlayerUpdateEvent, PlayerActivatePickRefEvent, PickRefStateChangedEvent, VATSCommandTargetEvent, EndLoadGameEvent, IsPipboyActiveEvent, PlayerCrosshairModeEvent, HUDModeEvent, PerkPointIncreaseEvent, PipboyLightEvent, PowerArmorLightData**.
- lx getters exist for HUDModeEvent (4801988), PerkPointIncreaseEvent (4804734), PipboyLightEvent (4803571), PlayerCrosshairModeEvent (4801808), PowerArmorLightData (2701547), CanDisplayNextHUDMessage, ColorUpdateEvent, ApplyColorUpdateEvent, CurrentRadiationSourceCount, DoBeforeNewOrLoadCompletedEvent and UIAdvanceMenusFunctionCompleteEvent. All return `EventSource_t*` via a singleton pointer.
- The rest need RE.
- Global holder: `BSTGlobalEvent::GetSingleton()` (4796078) [lx/include/RE/B/BSTEvent.h:189-238].

**(e) Singleton / object sources**

| Source | Events |
|---|---|
| `UI` | MenuOpenCloseEvent, MenuModeChangeEvent, MenuModeCounterChangedEvent, TutorialEvent |
| `PlayerCharacter` | BGSActorCellEvent @0x4C8, BGSActorDeathEvent @0x520, PositionPlayerEvent @0x578, PickRefUpdateEvent @0x5D0 |
| `ActorEquipManager` | `ActorEquipManagerEvent::Event` |
| `BGSInventoryList` (per ref) | `BGSInventoryListEvent::Event` |
| `BSInputEnableManager` | UserEventEnabledEvent, OtherEventEnabledEvent, InputEnableLayerDestroyedEvent |
| `Actor` | ActorCPMEvent, PerkValueEvents, MovementMessageUpdateRequestImmediate |
| `TESObjectREFR` | `ActorValueEvents::ActorValueChangedEvent` (per ref, @0x60) |
| `bhkCharacterController` | MoveFinish, StateChange, NonSupportContact |
| `Internal::VirtualMachine` | `StatsEvent` |
| `Workshop::RegisterForItemPlaced / ItemMoved / ItemDestroyed / WorkshopModeEvent` | (helpers) [lx/include/RE/W/Workshop.h:272-356] |

[src: grep `public BSTEventSource<` in lx/include/RE]

### 4.3 UI

- `UI` (4796314): `GetMenu`, `GetMenuOpen`, `RegisterMenu(name, Create_t*, StaticUpdate_t*)`, `RefreshCursor`, `menuStack` @0x190, `menuMap` @0x1A8, `menuMode` @0x1E0 [lx/include/RE/U/UI.h].
- `IMenu`: `menuObj` (GFx Value) @0x20, `uiMovie` @0x40, `menuFlags` @0x58; virtuals `AdvanceMovie 04`, `PreDisplay 05`, `PostDisplay 06`, `CanAdvanceMovie 0D` [lx/include/RE/I/IMenu.h]. FO4 menus derive from `GameMenuBase`.
- **Menu-name constants in lx** (22): BarterMenu, Console, ContainerMenu, CursorMenu, DialogueMenu, ExamineConfirmMenu, ExamineMenu, HUDMenu, HolotapeMenu, LoadingMenu, LockpickingMenu, MainMenu, MessageBoxMenu, PauseMenu, PipboyHolotapeMenu, PipboyMenu, PowerArmorModMenu, SitWaitMenu, TerminalHolotapeMenu, TerminalMenu, TerminalMenuButtons, WorkshopMenu.
- Other FO4 menus without lx classes: LooksMenu [src: f4se scripts], VATSMenu, FaderMenu, FavoritesMenu, LevelUpMenu, BookMenu, CookingMenu, SPECIALMenu, PromptMenu, ScopeMenu, MultiActivateMenu, CreditsMenu, VignetteMenu, GenericMenu [inference: verify against `menuMap` at runtime].
- `UIMessageQueue::AddMessage(name, UI_MESSAGE_TYPE)` (4796377). `MenuCursor` (4803837). `CursorMenu::cursor`.
- Scaleform is AS3 (`Scaleform::GFx::Movie`, `Value`, `GFx_AS3_*` headers) [lx/include/Scaleform/G/*].
- F4SE also offers the Scaleform interface (`Register(name, cb(Movie*, Value*))`) and custom menus via `UI.RegisterCustomMenu` [lx/include/F4SE/Interfaces.h:241-260; f4se/scripts/modified/UI.psc:15-31].

### 4.4 Rendering, camera and input

**Rendering**
- `BSGraphics::GetRendererData()` → `device`, `context`, `renderWindow[0].swapChain`.
- `BSGraphics::State::GetSingleton()` → screen size.
- `RenderTargetManager` (2666735).
- [lx/include/RE/B/BSGraphics.h; IDs.h:555-587]
- The game is D3D11 + DXGI. Tilted's IAT hook on `D3D11CreateDeviceAndSwapChain` must be verified (§2.2 #W).

**Camera**
- `PlayerCamera` (4796065): `cameraStates[13]`, `QCameraEquals`, `PushState/PopState/SetState`, `worldFOV/firstPersonFOV`.
- `Main::WorldRootCamera()` returns `NiCamera*` directly.
- `NiCamera::WorldPtToScreenPt3`, `BoundInFrustum`, `ViewPointToRay`.

**Input**
- `BSInputDeviceManager` (4807767) has `devices[5]` (keyboard, mouse, gamepad, debug, virtual) [lx/include/RE/B/BSInputDeviceManager.h]. Device classes in RTTI: `BSPCKeyboardDevice`, `BSPCMouseDevice`, `BSPCGamepadDevice`.
- Event delivery goes through `BSInputEventReceiver` / `BSInputEventUser` (MenuControls, PlayerControls handlers) [§1.10].
- **Raw input vs DirectInput:** FO4 imports `RegisterRawInputDevices/GetRawInputData` [src: f4se/f4se/Hooks_Input.cpp:250-256]. A DInput8 keyboard/mouse is unverified (§1.10).
- `ControlMap` (4799307) holds user-event mapping.

### 4.5 Save / load

- `BGSSaveLoadManager` (2697802): `QueueSaveLoadTask`, `GetSaveDirectoryPath`, `BuildSaveGameList`, `saveGameList`, `queuedEntryToLoad`, `mostRecentSaveGame` [lx/include/RE/B/BGSSaveLoadManager.h].
- F4SE function RVAs @1.11.240: LoadGame 0xBEE760, SaveGame 0xBEE0B0, DeleteSaveGame 0xBF9170.
- te OG IDs: manager instance 1247321 (al: 1247320).
- Loading a named save directly is **Missing-RE** for AE IDs (use Offset2ID).

### 4.6 Forms and references

**Data handler and creation**
- `TESDataHandler` (4796135): `formArrays[kTotal]`, `files`, `compiledFileCollection`, `LookupForm(rawID, modName)`, `LookupModByName`, `CreateReferenceAtLocation(NEW_REFR_DATA&)` (2192301), `AddFormToDataHandler` (2192271) [lx/include/RE/T/TESDataHandler.h].
- PlaceAtMe equivalent: F4SE `PlaceAtMe_Native(VM*, u32 stackId, TESObjectREFR** target, TESForm*, i32 count, bool persist, bool disabled, bool deleteWhenAble)` [f4se/f4se/GameObjects.h:25-26], or the Papyrus `ObjectReference.PlaceAtMe` through SP3.

**`TESObjectREFR`** [lx/include/RE/T/TESObjectREFR.h:72-615]
- Members: `parentCell B8`, `data (OBJ_REFR: location/angle/objectReference) C0`, `loadedData F0`, `inventoryList F8`, `extraList 100`.
- Functions: `GetPosition/X/Y/Z`, `SetLocationOnReference` (2201138), `SetAngleOnReference` (2201134), `MoveRefToNewSpace` (2201149), `Enable` (2201150), `Disable` (vfunc 0xB0), `MarkAsDeleted`, `SetWantsDelete`, `SetScale` (2200893), `UpdateReference3D` (2201071), `Update3DPosition(bool warp)` (vfunc 0x50), `Load3D` (0x86), `Release3DRelatedData` (0x87), `Set3D` (0x88), `Get3D` (0x8B/0x8C), `GetHandle` (2201196), `GetCurrentLocation`, `GetDistanceFromReference`, `GetItemCount`, `GetLinkedRef/SetLinkedRef`, `IsCrimeToActivate`, `ActivateRef` (2201147), `AddInventoryItem` (2200949).
- MoveTo and SetPosition equivalents: `Actor::SetPosition(const NiPoint3&, bool updateCharController)` (vfunc 0xCA) [lx/include/RE/A/Actor.h:136]; F4SE `MoveRefrToPosition`; te OG `1101833` SetPosition. Papyrus has `MoveTo`, `SetPosition`, `TranslateTo` [f4se/scripts/vanilla/ObjectReference.psc:659, 919, 940].
- DoReset3D equivalent: `Actor::Reset3D` (2229913).

**ExtraData types in lx**
- Headers: ExtraAliasInstanceArray, ExtraAmmo, ExtraBendableSplineParams, ExtraCellSkyRegion, ExtraCellWaterType, ExtraCharge, ExtraEditorID, ExtraFactionChanges, ExtraFavorite, ExtraHealth, ExtraInstanceData, ExtraLeveledCreature, ExtraLeveledItem, ExtraLight, ExtraLinkedRef, ExtraLinkedRefChildren, ExtraLocation, ExtraLock, ExtraMapMarker, ExtraMaterialSwap, ExtraOutfitItem, ExtraPoison, ExtraPowerLinks, ExtraRadioData, ExtraRadioReceiver, ExtraRadioRepeater, ExtraRadius, ExtraRagDollData, ExtraReferenceHandles, ExtraStartingWorldOrCell, ExtraTeleport, ExtraTextDisplayData, ExtraTresPassPackage, ExtraUniqueID, plus `BGSObjectInstanceExtra`.
- `EXTRA_DATA_TYPE` has 215 entries [lx/include/RE/E/EXTRA_DATA_TYPE.h].

### 4.7 Actors

**Core**
- `Actor`: `currentProcess (AIProcess*) 300`, `avStorage 338`, `magicCasters 3C8`, `race 418`, `biped 428`, `boolFlags 43C`, `healthModifiers 444`, `actionPointsModifiers 450`, `radsModifiers 468`, `armorRating 47C`.
- Functions: `PerformAction(BGSAction*, TESObjectREFR*)` (2231177), `SetGunState`, `SetHeading` (2229625), `Move(dt, NiPoint3, defer)` (2229934), `Jump` (2229650), `IsJumping`, `IsSneaking` (2207655), `GetCurrentAmmoCount/SetCurrentAmmoCount`, `GetEquippedItem`, `UnequipObject` (2230479), `AddPerk/RemovePerk`, `GetLevel`, `Reset3D`, `InitiateDoNothingPackage` (2229807), `EndInterruptPackage`.
- [lx/include/RE/A/Actor.h; IDs.h:7-71]
- `PlayerCharacter`: `firstPerson3D B78`, `tintingData D00`, sources (§4.2e) [lx/include/RE/P/PlayerCharacter.h].
- `AIProcess` [lx/include/RE/A/AIProcess.h].
- te OG IDs that are MP-useful: ActorMediator 1358859, PerformComplexAction 1445653, PerformAction 502377.

**Equipment**: `ActorEquipManager` (§1.13); `BGSObjectInstance` [lx/include/RE/B/BGSObjectInstance.h].

**Actor values**
- `ActorValueOwner` virtuals: `01 GetActorValue, 02 GetPermanentActorValue, 03 GetBaseActorValue, 04 SetBaseActorValue, 05 ModBaseActorValue, 06 ModActorValue(ACTOR_VALUE_MODIFIER,…), 07 GetModifier, 08 RestoreActorValue, 09 SetActorValue` [lx/include/RE/A/ActorValueOwner.h:13-25].
- AV forms come from `ActorValue::GetSingleton()` (2189587). It has fields `health`, `actionPoints`, `carryWeight`, `strength…luck`, `rads` and others [lx/include/RE/A/ActorValue.h:60+]. `ActorValueInfo` is the AVIF form [lx/include/RE/A/ActorValueInfo.h].

**Power armor**: `RE::PowerArmor::{ActorInPowerArmor(const Actor&), PlayerInPowerArmor(), GetArmorKeyword(), GetBatteryKeyword(), GetDefaultBatteryObject(), IsPowerArmorBattery(), SyncFurnitureVisualsToInventory(furniture, force3D, tempItem, hideCore), GetNewBatteryCapacity()}`; `PowerArmorGeometry` (sinks `PreloadPowerArmor::Event` and `ExitPowerArmor::Event`); `TESNPC::powerArmorFurn` @0x2A8; Papyrus `Actor.SwitchToPowerArmor(ObjectReference)` [lx/include/RE/P/PowerArmor.h:12-62; P/PowerArmorGeometry.h:34-48; f4se/scripts/vanilla/Actor.psc:785].

**Workshop**: `RE::Workshop::{FindNearestValidWorkshop, FreeBuild, IsLocationWithinBuildableArea, PlaceCurrentReference, ScrapReference, StartWorkshop, RequestExitWorkshop, ToggleEditMode, UpdateActiveEdit, WorkshopCanShowRecipe, GetCurrentPlacementItemData, GetPlacementItem, Register/UnregisterForItemPlaced/Moved/Destroyed/WorkshopModeEvent}` and `ContextData` [lx/include/RE/W/Workshop.h:137-389].

**VATS**: `VATS` (4797733) sinks WeaponFiredEvent, ProjectileBeginUpdateEvent, TESHitEvent and TESDeathEvent; members `commands`, `mode`, `killCount` [lx/include/RE/V/VATS.h:20-61].

**Pip-Boy**: `PipboyDataManager` (4796372) holds stats/special/perks/inventory/quest/workshop/log/map/radio/playerInfo/status data. `PipboyManager` (4799238) [lx/include/RE/P/PipboyDataManager.h:20-44].

**TESNPC face data** (exact names): `headRelatedData{hairColor, facialHairColor, faceDetails}`, `morphWeight` (NiPoint3), `headParts`/`numHeadParts`, `morphRegionSliderValues`, `facialBoneRegionSliderValues` (`BGSCharacterMorph::Transform`), `morphSliderValues`, `bodyTintColorR/G/B/A`, `tintingData` (`BGSCharacterTint::Entries{entriesA}` → `Entry{templateEntry, idLink, tintingValue}`; palette entries add `tintingColor`, `swatchID`), `faceNPC`, `originalRace`, `height/heightMax`, `GetSex()`, `SetHairColor`, `GetHeadParts(bool)` [lx/include/RE/T/TESNPC.h:41-210; B/BGSCharacterTint.h:75-183]. Papyrus `Actor.ChangeHeadPart` [Actor.psc:76].

### 4.8 Inventory and OMODs

- `BGSInventoryList{data BSTArray<BGSInventoryItem> 58, cachedWeight 70, owner 74, rwLock 78}`: `AddItem2`, `RemoveItem1`, `FindItemIndex`, `ForEachStack`, `GetItemCount`, `BuildFromContainer`, `FindAndWriteStackDataForItem` [lx/include/RE/B/BGSInventoryList.h:19-138].
- `BGSInventoryItem::Stack{nextStack 10, extra 18, count 20, flags 24 (kSlotIndex1..3, kEquipStateLocked, kInvShouldEquip, kTemporary)}` and functors (`CheckStackIDFunctor`, `FindEquippedStackFunctor`, …) [lx/include/RE/B/BGSInventoryItem.h:10-160].
- `BGSObjectInstanceExtra`: static `AttachModToReference(TESObjectREFR&, BGSMod::Attachment::Mod&, u8 attachIndex, u8 rank)` (2189033), `AddMod(mod, attachIndex, rank, removeInvalid)` (2189025), `RemoveMod(mod*, attachIndex)` (2189027), `HasMod` (2189026), `RemoveInvalidMods`, `GetNumMods`, `GetIndexData()` → `span<BGSMod::ObjectIndexData>` [lx/include/RE/B/BGSObjectInstanceExtra.h:18-80; IDs.h:335-339].
- `BGSMod::Attachment::Mod : TESForm, TESFullName, TESDescription, BGSModelMaterialSwap, Container` [lx/include/RE/B/BGSMod.h:159-172].
- Papyrus: `ObjectReference.AttachMod`, `AttachModToInventoryItem` [ObjectReference.psc:230-233].

### 4.9 Animation

- `IAnimationGraphManagerHolder` (§1.8): `NotifyAnimationGraphImpl` (01), `GetAnimationGraphManagerImpl` (04), `GetGraphVariableImplFloat/Int/Bool` (13-15), `SetGraphVariableBool/Float/Int` (IDs), `RevertAnimationGraphManager`.
- `BSAnimationGraphManager` (§1.8).
- `Actor::PerformAction` and the `BGSAction` forms; `ActionInput`/`ActionOutput` [lx/include/RE/A/ActionInput.h, ActionOutput.h].
- Papyrus: `PlayIdle`, `PlayIdleAction`, `Get/SetAnimationVariable*` [Actor.psc:517-523; ObjectReference.psc:360-871].
- First-person risk: `FirstThirdPersonSwitch::Event` exists (§4.2c).

### 4.10 Console

`ConsoleLog` (§1.14), `SCRIPT_FUNCTION::LocateConsoleCommand` / `GetConsoleFunctions` (523) / `GetScriptFunctions` (818), `SCRIPT_OUTPUT` enum (`kConsole_*`) [lx/include/RE/S/SCRIPT_OUTPUT.h].

### 4.11 Nodes

- `NiAVObject::GetObjectByName` (vfunc 0x2E), `BSUtilities::GetObjectByName(root, name, tryInternal, dontAttach)`, `SetLocalScale`, `GetWorldScale`, `CullNode`, `SetAppCulled` (0x2D), `Update(NiUpdateData&)` [lx/include/RE/N/NiAVObject.h].
- `NiNode::AttachChild/DetachChild` [lx/include/RE/N/NiNode.h:32-35].
- `TESObjectREFR::SetScale` (2200893).
- Texture sets: `BGSTextureSet` [lx/include/RE/B/BGSTextureSet.h].
- FO4 skeleton node names differ (e.g. `"Head"`).

---

## 5. Per-file portability verdict

Verdicts:
- **P**: portable as-is.
- **A**: adapt (small API differences).
- **R**: rewrite (engine-specific).
- **D**: drop (Skyrim-only or dead).

`sp/` = `skyrim-platform/src/platform_se/skyrim_platform/`.

| File(s) | Verdict | Reason |
|---|---|---|
| `skyrim_platform_entry/main.cpp`, `PCH.h` | A | Export `F4SEPlugin_Version` and `F4SEPlugin_Load`. Load the Impl DLL from `Data/F4SE/Plugins` (§3.6). |
| `sp/main.cpp` | A | F4SE init and messaging; MenuCursor; RendererData; WinMain removal; `Fallout4.exe`. NirnLab removed. |
| `sp/PCH.h` | A | Includes become `<F4SE/F4SE.h>`, `<RE/Fallout.h>`, REL/REX. Remove the `SKSE::log`/`stl` aliases. Compile as C++23. |
| `sp/game/Offsets.h` | R | All IDs replaced (§2). |
| `sp/game/Classes.h`, `game/classes/BSRenderManager.h`, `game/classes/MenuScreenData.h` | D | Replaced by lx `BSGraphics::RendererData` and `MenuCursor`. |
| `sp/game/classes/TintMask.h` | D | Skyrim tint struct. |
| `sp/game/Events.h`, `game/events/ItemHarvested.h` | R | FO4 struct layouts must be RE'd. lx provides ItemHarvested. |
| `sp/Hooks.cpp/.h` | R | Slot 0x1B, VTABLE R1, callback layout, AddString detour instead of VPrint. |
| `sp/FridaHooks.cpp`, `FridaHooksUtils.h` | R | All hook targets and arguments change (0x48 offset, AS3 paths, tint hooks dropped). |
| `sp/FridaHookHandler.cpp/.h`, `Hook.cpp/.h`, `HookPattern.cpp/.h`, `Handler.cpp/.h`, `Override.cpp/.h` | P | Game-agnostic. |
| `sp/CallNative.cpp/.h` | R | Variable/TypeInfo API, frame variable access, PackHandle, special cases (ClearDestruction/PushActorAway/QueueNiNodeUpdate/AddItem/RemoveItem), Struct/Var support. |
| `sp/CallNativeApi.cpp/.h` | A | GetModCount/GetModName are the same fields. Remove `ENABLE_SKYRIM_VR`. |
| `sp/VmProvider.cpp/.h` | A | Same ObjectTypeInfo API. Type names are namespaced. |
| `sp/GetNativeFunctionAddr.cpp/.h` | A | `isLatent` read stays at 0x42. The callback interpretation changes (V2/V3). |
| `sp/FunctionInfoProvider.h` | A | `TypeInfo::RawType` renames. |
| `sp/VmCall.h`, `VmCallback.h`, `VmFunctionArguments.h` | R | std::function args. IStackCallbackFunctor has 4 extra virtuals and no SetObject. |
| `sp/TickHandler.h` | A | `F4SE::GetTaskInterface`. |
| `sp/PapyrusTESModPlatform.cpp/.h` | R | New native bindings (`std::monostate`); TESNPC/appearance, ExtraData, equipment, MoveRefr and CreateRef all differ. |
| `sp/Sp3Api.cpp/.h`, `sp/Sp3NativeValueCasts.cpp/.h`, `assets/sp3.js` | A | Same reflection idea. Add Struct/Var marshalling. `GetScriptObjectType1` → overload. |
| `sp/DumpFunctions.cpp/.h`, `FunctionsDumpFactory.cpp/.h`, `FunctionsDumpFormat.h` | A | Needs an FO4 PEX reader (papyrus-vm) and scripts read from BA2. |
| `sp/EventHandler.cpp/.h` | R | ProcessEvent by reference, per-event sources, ~35 Skyrim events dropped or remapped, InputEvent via BSInputEventUser. |
| `sp/EventUtils.h`, `Concepts.h` | A | Remove ScriptEventSourceHolder. The `HasEvent` concept works with `E::GetEventSource()`. |
| `sp/EventEmitter.h` | D | Unreferenced dead code. |
| `sp/EventManager.cpp/.h`, `EventsApi.cpp/.h` | A | Rename the custom events (`skyrimLoaded`). Otherwise portable. |
| `sp/InventoryApi.cpp/.h` | R | BGSInventoryList/stacks/OMODs instead of ExtraContainerChanges. New EquipObject signature. |
| `sp/ConsoleApi.cpp/.h` | A | SCRIPT_FUNCTION renames (`paramCount`, `float&`), `Script::text`, `REL::WriteSafe`. |
| `sp/InGameConsolePrinter.cpp/.h`, `ExceptionPrinter.cpp/.h`, `WindowsConsolePrinter.cpp/.h`, `IConsolePrinter.h` | A / P | `Print` → `PrintLine` (the printers using ConsoleLog); the rest are portable. |
| `sp/MagicApi.cpp/.h`, `sp/Magic/*` (5 files) | D | Skyrim magic plus raw behaviour-index tables. Replace with name-based graph-var and weapon APIs. |
| `sp/LoadGame.cpp/.h`, `LoadGameApi.cpp/.h`, `assets/template.ess` | R | `.fos` / template-save strategy; LoadGame function is Missing-RE. |
| `sp/CameraApi.cpp/.h` | A | Same NiCamera API (or `Main::WorldRootCamera`). |
| `sp/TextApi.cpp/.h`, `TextsCollection.cpp/.h` | A | PlayerCamera/BSGraphics::State renames; FO4 node names. |
| `sp/DevApi.cpp/.h` | A | Screenshot handler via `BSInputEventUser` and `Un/RegisterHandler`. |
| `sp/ObjectReferenceApi.cpp/.h` | A | `SetCollision` is Missing-RE. |
| `sp/Win32Api.cpp/.h` | A | `Main::quitGame` is the same. |
| `sp/BrowserApi.cpp/.h`, `BrowserApiTilted.cpp/.h` | A | UIMessageQueue 2-arg form; GetMenuOpen. |
| `sp/BrowserApiNirnLab.cpp/.h` | D | NirnLab is Skyrim-only. |
| `sp/TPOverlayService.cpp/.h`, `TPRenderSystemD3D11.cpp/.h` | P | Generic D3D11/CEF. |
| `sp/TPInputService.cpp/.h` | P (unused) | WndProc input path. Useful if FO4 has no DInput (§1.10). |
| `sp/MyInputListener` (in main.cpp), `InputConverter.cpp/.h`, `IInputConverter.h` | A / P | Cursor source changes to MenuCursor; the converter is portable. |
| `sp/JsEngine.cpp/.h`, `NodeInstance.cpp/.h`, `NapiHelper.h`, `JsUtils.h`, `EncodingApi.*`, `FileInfoApi.*`, `DirectoryMonitor.*`, `IPC.*`, `MpClientPluginApi.*`, `Settings.*`, `StringHolder.h`, `ProxyGetter.h`, `NullPointerException.h`, `InvalidArgumentException.h`, `NiPoint3.h` (SP math), `FlowManager.*`, `SkyrimPlatform.cpp/.h` (except the `IFunction::Call` signature and the "Hello SE" print) | P / A | Game-agnostic. MpClientPlugin path → `Data/F4SE/Plugins`. `JsUtils.h` `AddObjProperty(RE::TESForm*)` is the same. |
| `sp/ConstEnumApi.cpp/.h` | R | Skyrim enums (FormType, ActorValue, SlotMask, Menu, MarkerType, WeaponType …). Regenerate from lx enums. |
| `sp/codegen/*`, `psc/*.psc`, `pex/*` | R | FO4 Papyrus dump plus Caprica/FO4 compiler. |
| `src/tilted/core_library/*` | P | Allocator/Signal/Meta. |
| `src/tilted/reverse/{AutoPtr,AutoPtrManager,FunctionHook,Pattern,ProcessMemory,ThisCall,Debug}*` | P | Generic. |
| `src/tilted/reverse/Entry.cpp/.hpp`, `App.cpp/.hpp` | A | Drop the msvcr110 hook. Allow `GetMainAddress()==nullptr` and call BeginMain from the cmdline hook (§2.2 #W). |
| `src/tilted/hooks/D3D11Hook.*`, `WindowsHook.*` | P (verify IAT import) | |
| `src/tilted/hooks/DInputHook.*` | A / verify | FO4 input model (§1.10). |
| `src/tilted/hooks/D3D9Hook.*` | D | Unused for FO4. |
| `src/tilted/ui/*`, `src/tilted/ui_process/*` | P | CEF. Rename strings ("Skyrim Platform", MyChromiumApp.cpp:104-115). |
| `src/platform_lib/*` | P | HTTP/threads/validators. |
| `skymp5-server/cpp/client/main.cpp` | P | No game symbols. |

---

## 6. Build integration

### 6.1 Facts

**How SP consumes CommonLibSSE today**
- SP uses the vcpkg overlay port `overlay_ports/commonlibsse-ng-flatrim`: `vcpkg_from_github(CharmedBaryon/CommonLibSSE @b93280e8)` + 4 patches + `vcpkg_configure_cmake(-DENABLE_SKYRIM_VR=off -DSKSE_SUPPORT_XBYAK=on)`. It installs `CommonLibSSE.cmake`, which provides `target_commonlibsse_properties` [src: overlay_ports/commonlibsse-ng-flatrim/portfile.cmake; vcpkg.json].
- It is selected through the manifest feature `skyrim-flatrim` [src: vcpkg.json features; CMakeLists.txt:59-65].
- Targets link `CommonLibSSE::CommonLibSSE` and get their WINVER patched to 0x0A00 [src: skyrim-platform/src/platform_se/CMakeLists.txt:119-128, 226-233].

**Project settings**
- Triplet `x64-windows-sp`: static CRT and static libraries [src: overlay_triplets/x64-windows-sp.cmake].
- `CMAKE_CXX_STANDARD 20` [src: CMakeLists.txt:155].
- MSVC flags from `apply_default_settings`: `/WX /permissive- /Zc:preprocessor /utf-8 /Zi`, static runtime [src: cmake/apply_default_settings.cmake].
- CI uses `windows-2022` (VS 2022) [src: .github/workflows/pr-windows-flatrim.yml:23].

**lx build**
- **xmake only**: no CMakeLists, no vcpkg.json.
- `set_languages("c++23")`, target `commonlibf4` (static), `add_files("src/**.cpp")`, public include `include/`, PCH `include/F4SE/Impl/PCH.h`, `add_deps("commonlib-shared")` [src: lx/xmake.lua:1-46].

**cs (commonlib-shared) build**
- Has **CMakeLists.txt** (static lib, `cxx_std_23`, `find_package(spdlog)`, options `COMMONLIB_INI/JSON/TOML/XBYAK/RANDOM`, links advapi32/bcrypt/d3d11/d3dcompiler/dbghelp/dxgi/ole32/shell32/user32/version/ws2_32, MSVC `/we4715 /wd4200 /wd4201 /wd4324`) and **vcpkg.json** (dependency `spdlog[wchar]`) [src: cs/CMakeLists.txt; cs/vcpkg.json].
- Its xmake build also adds public flags `/EHsc /permissive- /bigobj /cgthreads8 /diagnostics:caret /external:W0 /fp:contract /fp:except- /guard:cf- /Zc:enumTypes /Zc:preprocessor /Zc:templateScope`, and builds spdlog with `std_format=true` [src: cs/xmake.lua:42-150].

**C++ requirements**
- lx headers use C++23 library features (`std::to_underlying` in 13 headers) [src: grep lx/include, cs/include]. Every TU that includes them must be C++23: CMake `cxx_std_23` maps to `/std:c++latest` on VS 2022.
- MSVC: VS 2022 v143, a recent 17.x [inference: no version gate in code].

**Existing vcpkg ports**
- `Monitor221hz/modding-vcpkg-ports` has a `commonlibf4` port, but it builds **Ryan-rsm-McKenzie/CommonLibF4 @a05fffe (2022, OG only)**, so it is unusable for AE [src: scratchpad/mvp/ports/commonlibf4/portfile.cmake; web: https://github.com/Monitor221hz/modding-vcpkg-ports].
- No maintained vcpkg port of libxse/commonlibf4 was found [web: search results above; https://github.com/voidei/vcpkg-registry has no F4 ports].
- al builds with CMake (`find_package(mmio spdlog)`, rapidcsv, options `ENABLE_FALLOUT_F4/NG/VR`) but has **no AE IDs** [src: al/CommonLibF4/CMakeLists.txt:1-60; al/README.md].

**Repo leftovers**: `overlay_ports/commonlibse` and `commonlibae` exist but are unused (their descriptions wrongly say F4SE) [src: overlay_ports/commonlibse/vcpkg.json].

### 6.2 Recommendation: an overlay port `commonlibf4-ae` with a port-supplied CMakeLists

**Steps**
1. Create `overlay_ports/commonlib-shared/`.
   - `portfile.cmake`: `vcpkg_from_github(REPO libxse/commonlib-shared REF 29fbdb0e2dc548c9ab22f6964981d75090dc9094 …)`, then `vcpkg_cmake_configure(SOURCE_PATH … OPTIONS -DCOMMONLIB_XBYAK=OFF)`, `vcpkg_cmake_install()`, `vcpkg_cmake_config_fixup(PACKAGE_NAME commonlib-shared CONFIG_PATH lib/cmake/commonlib-shared)`.
   - `vcpkg.json`: depend on `{"name":"spdlog","features":["wchar"]}` and `vcpkg-cmake` / `vcpkg-cmake-config` (host).
   - This works directly because upstream ships CMake.
2. Create `overlay_ports/commonlibf4-ae/`.
   - `vcpkg_from_github(REPO libxse/commonlibf4 REF 7c8c6f8a349ac85abf6fc8a7e5a71732d9339954 …)`.
   - `file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")`, then configure / install / config-fixup as `CommonLibF4`.
   - Port-supplied `CMakeLists.txt`:
     - `file(GLOB_RECURSE SRC src/*.cpp)`
     - `add_library(CommonLibF4 STATIC ${SRC})`
     - `target_include_directories(... PUBLIC $<BUILD_INTERFACE:include> $<INSTALL_INTERFACE:include>)`
     - `target_compile_features(PUBLIC cxx_std_23)`
     - `find_package(commonlib-shared CONFIG REQUIRED)` + `target_link_libraries(PUBLIC commonlib-shared::commonlib-shared)`
     - `target_precompile_headers(PRIVATE include/F4SE/Impl/PCH.h)`
     - MSVC public options `/EHsc /permissive- /bigobj /Zc:preprocessor /Zc:enumTypes /Zc:templateScope /utf-8 /wd4200 /wd4201 /wd4324 /we4715`
     - install the `F4SE`, `RE` and `Scaleform` include dirs and export `CommonLibF4::CommonLibF4`
   - Add patches here if needed (e.g. VTABLE ID fixes, extra event getters, `BShkbAnimationGraph`). Patches mirror SP's existing practice.
3. In the root `vcpkg.json`, add the feature `fallout4-ae` → dependency `commonlibf4-ae` (windows). In `CMakeLists.txt`, add `option(GAME …)` / `FALLOUT4` that appends `fallout4-ae` to `VCPKG_MANIFEST_FEATURES`. This mirrors lines 59-65.
4. In the new `fallout4-platform/src/.../CMakeLists.txt` (copy of `platform_se`):
   - `find_package(CommonLibF4 CONFIG REQUIRED)`; link `CommonLibF4::CommonLibF4` to both DLL targets.
   - Set `CXX_STANDARD 23` on those targets only; root code stays C++20.
   - Keep `apply_default_settings` (static CRT matches the triplet).
   - Replace `target_commonlibsse_properties` with a hand-written `F4SE_PLUGIN_VERSION` in the entry DLL (template: `lx/res/commonlibf4-plugin.cpp.in`).
   - Drop the WINVER patch unless conflicts appear: cs does not force WINVER [src: cs/CMakeLists.txt].
5. Add `spdlog[wchar]` to the root manifest. With a single root-level spdlog both ports agree. fmt-based spdlog is fine (§1.2).
6. Packaging (`tools/dev_service`):
   - Copy `FalloutPlatform.dll`, `FalloutPlatformImpl.dll` and `MpClientPlugin.dll` to `Data/F4SE/Plugins/`.
   - Ship or require `version-1-11-<build>-0.bin` there. Put the bin under `client-deps/fo4/Data/F4SE/Plugins/` the way `client-deps/ae/Data/SKSE/Plugins/versionlib-*.bin` is handled today [src: client-deps/ae/Data/SKSE/Plugins], subject to Address Library licensing.
   - The game exe is `Fallout4.exe` and the launcher is `f4se_loader.exe`.
7. Pin and verify:
   - Add a first-run self-test that resolves every ID FalloutPlatform uses, especially VTABLE IDs (R1), and logs failures before installing hooks.
   - Gate on `REX::FModule::GetExecutingModule().GetFileVersion()` ∈ {1.11.137…1.11.240} and fail fast with a clear message.

**Why not xmake-in-vcpkg?** It would require xmake on every developer and CI machine, and xmake fetches its own spdlog. That mixes CRTs and duplicates spdlog with the vcpkg one. A ~40-line port CMakeLists is simpler and follows the existing overlay-port pattern.

**Alternative**: `add_subdirectory` a vendored copy (or a git submodule) with the same CMakeLists. It is faster to iterate when patching lx heavily, but has to be kept out of the repo-wide `GLOB_RECURSE` and clang-format paths.

---

## 7. Biggest blockers ("needs RE"), ranked

1. **VTABLE IDs on AE (R1).** `BindNativeMethod` (slot 0x1B), `SendEvent` (0x2B), the actor `NotifyAnimationGraphImpl` and the DrawWeapon slots depend on vtables. The lx VTABLE IDs are OG-space and unverified, and the VM hook must go in before GameVM exists. Fix: an RTTI-COL vtable finder or Offset2ID.
2. **TES event getters.** ~30 events SP exposes have no lx getter or struct. Candidate IDs and RVAs are in §4.2b; each needs verification plus a field-layout RE.
3. **The `NotifyAnimationGraph` hook** (animation sync core): which vtable and slot to hook for Actor and PlayerCharacter, and the first- vs third-person graph question.
4. **Loading a named save** (`BGSSaveLoadManager::LoadGame`, F4SE RVA 0xBEE760@1.11.240 → AE ID) and **`MoveRefrToPosition`** (RVA 0x1181020).
5. **Animation variables**: `BShkbAnimationGraph`/`hkbVariableValueSet` layouts are absent. Switch to name-based graph variables.
6. **Input model**: whether FO4 has DInput8 for keyboard/mouse at all; otherwise the CEF input path moves to WndProc/raw input.
7. **Vanilla NativeFunction layout past 0x50**: diagnostics only; F4SE natives (raw pointer) differ from CommonLibF4 natives (std::function).
8. **AS3 Scaleform paths** for CursorMenu/MainMenu, `SetCollision`, `Projectile::Launch`, story-event and global-event sources, and the Skyrim→FO4 form IDs (template NPC, DoNothing package, EquipSlots, GameHour).
