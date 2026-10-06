# Fallout 4 Papyrus: PEX format, opcodes, type system, VM semantics and VM internals

> **Audience:** the Claude Code session (or human) that will make `papyrus-vm` run Fallout 4 scripts and port SkyrimPlatform's Papyrus reflection/call layer to Fallout 4.
> **Scope:** byte-exact PEX layout (Skyrim vs FO4), the full opcode table 0x00–0x2E, the FO4 type system and runtime semantics a server VM must emulate, CommonLibF4 VM internals for the client, and a per-file implementation plan with acceptance tests.
> **Companion:** the broad survey is [`docs/FALLOUT4_PORT_RESEARCH.md` §4.3](../../FALLOUT4_PORT_RESEARCH.md).

## Provenance tags

| Tag | Meaning |
|---|---|
| `[src: path:line]` | Read in source. Paths starting `papyrus-vm/`, `falloutmp-server/`, `skyrim-platform/`, `unit/`, `libespm/`, `overlay_ports/` are in this repo. Other paths are in the scratchpad clones listed below. |
| `[caprica-run]` | Checked by compiling FO4 test scripts with a Linux build of Caprica (Styyx1/Caprica `500be9c`, a portability fork of Orvid/Caprica). The bytes were dumped with a small independent reader written from this spec. See §1.12. |
| `[ck-pex]` | Checked against real Creation-Kit-compiled **Skyrim** PEX files in `unit/papyrus_test_files/pex/*.pex`. |
| `[web: URL]` | External documentation. The FO4 CK wiki pages were fetched as wikitext via `https://falloutck.uesp.net/w/api.php?action=parse&page=<Title>&prop=wikitext`. |
| `[inference]` | My own conclusion. Verify it in game before relying on it. |

### Source clones used

These were all read-only, under `/tmp/claude-0/-home-user-falloutmp/5eb948eb-daed-540d-b329-e0521df78518/scratchpad/`. The tags below abbreviate them.

| Short name | Clone | What it is |
|---|---|---|
| `Champollion/` | Orvid/Champollion `bc961a0` | PEX reader and decompiler for Skyrim, FO4, FO76 and Starfield. `Pex/` holds the binary reader |
| `Caprica/` | Orvid/Caprica `e4dee08` | Open-source FO4/Skyrim/Starfield Papyrus compiler. `Caprica/pex/` holds the PEX reader and writer, `Caprica/papyrus/` the code generation |
| `lx/` | libxse/commonlibf4 `7c8c6f8` | FO4 AE CommonLib. Headers are flat: `include/RE/B/BSScript_*.h` |
| `alandtse/` | alandtse/CommonLibF4 `ba22620` | Headers in `CommonLibF4/include/RE/Bethesda/BSScript/*.h`. Same layouts as lx for everything checked here |
| `f4se/` | ianpatt/f4se `6f6a7ca` | `f4se/f4se/Papyrus*.h` and `scripts/vanilla/*.psc` (vanilla FO4 script sources, `Institute_Papyrus_Flags.flg`) |
| `xEdit` | `pexvm/wbDefinitionsFO4.pas` | TES5Edit `dev-4.1.6` `Core/wbDefinitionsFO4.pas`. Source of the FO4 VMAD layout |

---

## 0. TL;DR (key numbers)

**File format**
- FO4 PEX is **little-endian**. On disk the magic bytes are `DE C0 57 FA`. Version is **3.9** and **gameID = 2**.
- Skyrim PEX is **big-endian**. On disk the magic bytes are `FA 57 C0 DE`. Version is 3.1 (base game) or 3.2 (DLC/SE), and gameID = 1.
- Other games, which a FO4 server must reject:
  - FO76: LE, 3.15, gameID 3, adds opcode 0x2F.
  - Starfield: LE, 3.12, gameID 4, adds a guard table and opcodes 0x30–0x32. `[src: Caprica/Caprica/common/GameID.h:7-14; Caprica/Caprica/pex/PexFile.h:71-91; Champollion/Pex/FileReader.cpp:57-71,89-115]`
- FO4 adds the following to the Skyrim layout:
  - Debug info: a property-group table and a struct-order table, present only when the debug flag is set.
  - Object: a `const` byte after the docstring.
  - Object: a **struct table** after `autoStateName`.
  - Variable: a `const` byte after the initial value.
- Everything else is identical: all counts and string indices are uint16, user flags are uint32, and the value encoding (type bytes 0–5) is the same.

**Opcodes and types**
- FO4 opcodes are 0x00–0x23 (same as Skyrim) plus 0x24 `is`, 0x25 `struct_create`, 0x26 `struct_get`, 0x27 `struct_set`, 0x28 `array_findstruct`, 0x29 `array_rfindstruct`, 0x2A `array_add`, 0x2B `array_insert`, 0x2C `array_removelast`, 0x2D `array_remove` and 0x2E `array_clear`. None of the new opcodes takes varargs.
- PEX type strings:
  - base types: `Int`, `Float`, `Bool`, `String`, `Var`, `None`
  - objects: the full namespaced script name, written lowercased by the compiler, e.g. `actor`, `pexVm:Sender`
  - structs: `script#Struct`
  - arrays: append `[]`
  - **All comparisons must be case-insensitive.**

**Compiler naming conventions**
- Remote and custom event handlers compile to functions named `::remote_<SenderType>_<Event>`.
- Custom event names are compiled into string literals of the form `"<DeclaringScript>_<Event>"`, e.g. `"PexVm:Sender_Ping"`.
- `CustomEvent` declarations themselves are **not stored in PEX**.

**Client VM internals (CommonLibF4)**
- `IVirtualMachine::BindNativeMethod` is vtable slot **0x1B** in FO4. Skyrim SP hooks slot 0x18.
- `NativeFunctionBase::isLatent` is at **+0x42** and the callback is at **+0x50**, the same offsets as Skyrim.
- `StackFrame` is 0x40 bytes and has **no inline `args[]`**. Frame variables must be read with `Stack::GetStackFrameVariable(frame, i, page)`.
- `IStackCallbackFunctor` and the dispatch-argument types differ from Skyrim. See §5.

**papyrus-vm and dual-game support**
- `papyrus-vm` can support both games in one build because a PEX file self-describes its game (endianness plus gameID).
- A server should still run **one game per VM instance**: the natives, root class and event semantics differ (§6.10).

---

## 1. PEX binary layout, Skyrim vs FO4

### 1.1 Primitives and endianness

| Primitive | Encoding |
|---|---|
| `u8/u16/u32/u64/i32/f32` | File endianness: BE for Skyrim, LE for FO4/FO76/Starfield. `f32` is IEEE-754 |
| `wstring` | `u16 length` + `length` bytes. No NUL and no padding. Code page is effectively ANSI/Windows-1252 |
| `sidx` | `u16` index into the string table. Readers must bounds-check it; Champollion throws "Invalid string index" `[src: Champollion/Pex/FileReader.cpp:504-514]` |
| `time_t` | `u64` seconds |

**Endianness detection**
- Read the first 4 bytes as **LE** u32. `0xFA57C0DE` means LE (FO4 family). `0xDEC057FA` means BE (Skyrim). Anything else is not a PEX file.
- Switch the reader endianness after this test. Every later multi-byte field uses the file endianness. `[src: Champollion/Pex/FileReader.hpp:29-30, FileReader.cpp:89-106]`
- ⚠ Caprica's read-side constant `PEX_MAGIC_NUM_BE = 0xDE57C0FA` is not a correct byte-swap. Use Champollion's `0xDEC057FA`. `[src: Caprica/Caprica/pex/PexFile.h:32]`
- Today `papyrus-vm` reads everything big-endian and never checks the magic. `[src: papyrus-vm/src/papyrus-vm-lib/Reader.cpp:70-77,420-452]`

### 1.2 Header

| Offset | Field | Type | Skyrim | FO4 |
|---|---|---|---|---|
| 0x00 | magic | u32 | `FA57C0DE` (BE bytes `FA 57 C0 DE`) | `FA57C0DE` (LE bytes `DE C0 57 FA`) |
| 0x04 | majorVersion | u8 | 3 | 3 |
| 0x05 | minorVersion | u8 | 1 (vanilla), 2 (DLC/SE/AE) `[ck-pex: 3.2]` | **9** `[caprica-run]` |
| 0x06 | gameID | u16 | 1 | **2** |
| 0x08 | compilationTime | u64 time_t | | |
| 0x10 | sourceFileName | wstring | CK writes `Name.psc` `[ck-pex]` | Caprica writes the **absolute path** `[caprica-run]`. CK FO4 output is unverified. **Do not derive the script name from this field** |
| … | userName | wstring | | |
| … | computerName | wstring | | |

`[src: Champollion/Pex/FileReader.cpp:107-114; Caprica/Caprica/pex/PexFile.cpp:79-102]`

**Validation a reader must do** `[inference, matching Caprica PexFile.cpp:91-97]`
- Require `major == 3`.
- Accept FO4 only when the file is LE and `gameID == 2`. Accept minor ≥ 9 to tolerate future 3.x.
- Accept Skyrim only when the file is BE and `gameID == 1`, with minor 1 or 2.
- The real game rejects a PEX from the other game with "Game ID number doesn't match" `[web: falloutck "Differences from Skyrim to Fallout 4"]`.

### 1.3 String table

`u16 count; wstring strings[count]` — identical in both games. `[src: Champollion/Pex/FileReader.cpp:121-131]`

### 1.4 Debug info

```
u8   hasDebugInfo                 ; 0 => NOTHING else follows in this section
if hasDebugInfo != 0:
  u64  modificationTime
  u16  functionCount
  DebugFunction[functionCount]:
      sidx objectName
      sidx stateName              ; "" for empty state and for property accessors
      sidx functionName           ; for property accessors: the PROPERTY name
      u8   functionType           ; 0 = normal method, 1 = property getter, 2 = property setter (UESP: "valid 0-3")
      u16  instructionCount
      u16  lineNumbers[instructionCount]   ; one per instruction
  ---- FO4+ only (LE files) ----
  u16  propertyGroupCount
  PropertyGroup[propertyGroupCount]:
      sidx objectName
      sidx groupName              ; "" = the implicit ungrouped group
      sidx docString
      u32  userFlags              ; e.g. collapsedOnRef = bit 3 -> 0x8
      u16  nameCount
      sidx propertyNames[nameCount]   ; source order
  u16  structOrderCount
  StructOrder[structOrderCount]:
      sidx objectName
      sidx structName
      u16  memberCount
      sidx memberNames[memberCount]   ; source order
```

- Sources: `[src: Champollion/Pex/FileReader.cpp:136-198; Caprica/Caprica/pex/PexDebugInfo.cpp, PexDebugFunctionInfo.cpp, PexDebugPropertyGroup.cpp, PexDebugStructOrder.cpp; web: UESP Skyrim Mod:Compiled Script File Format]`.
- Verified `[caprica-run]`: group `Tuning` with uflags=0x8 lists props `['Speed','Scale']`, and an unnamed group exists for ungrouped properties.
- ⚠ **papyrus-vm bug:** `Reader::FillDebugInfo` always reads the modification time and function count, even when `hasDebugInfo == 0`. Any PEX compiled without debug info, for example with Caprica `--enable-debug-info=0`, is misparsed. `[src: papyrus-vm/src/papyrus-vm-lib/Reader.cpp:116-127]`
- The VM itself never needs debug info. Line numbers are only useful for error stack traces.

### 1.5 User-flag table

`u16 count; { sidx name; u8 bitIndex }[count]` — identical in both games.
- Names are the flag names from the `.flg` file. Caprica writes them lowercased: `hidden`, `collapsedOnRef`.
- FO4 `Institute_Papyrus_Flags.flg` defines: Hidden 0 (Script/Property/StructVar), Conditional 1 (Script/Variable), Default 2 (Script), CollapsedOnRef 3 (Group), CollapsedOnBase 4 (Group), Mandatory 5 (Property). `Collapsed` is the combination of 3 and 4.
- Skyrim `TESV_Papyrus_Flags.flg` defines: hidden 0, conditional 1 `[ck-pex]`.
- `userFlags` fields elsewhere in the file are u32 bitsets over these indices.
- `[src: f4se/scripts/vanilla/Institute_Papyrus_Flags.flg; Champollion/Pex/FileReader.cpp:204-213]`

### 1.6 Object table

```
u16 objectCount        ; always 1 in practice; the VM uses objectTable[0]
Object:
  sidx name                    ; FULL namespaced name, e.g. "PexVm:Sender", "Fragments:Quests:QF_X_000123"
  u32  size                    ; IGNORE. CK: bytes after this field + 4 [ck-pex: 231 vs 227];
                               ;         Caprica: bytes after this field [caprica-run: 346 vs 346];
                               ;         Caprica BE writer does not even byte-swap it [caprica-run: 0x7E000000]
  sidx parentClassName         ; "" = none. FO4: scripts without 'extends' get "ScriptObject" written explicitly
  sidx docString
  u8   isConst                 ; FO4+ only
  u32  userFlags
  sidx autoStateName           ; "" = starts in empty state
  ---- FO4+ only: struct table ----
  u16  structCount
  Struct[structCount]:
      sidx name                ; bare struct name, e.g. "Point" (type refs use "script#Point")
      u16  memberCount
      Member[memberCount]:
          sidx name
          sidx typeName
          u32  userFlags       ; e.g. Hidden -> 0x1
          Value defaultValue   ; None when no initializer (NOT the type default! [caprica-run])
          u8   isConst         ; compiler forbids const members -> always 0
          sidx docString       ; struct members may carry docstrings
  ---- both games ----
  u16  variableCount
  Variable[variableCount]:
      sidx name
      sidx typeName
      u32  userFlags
      Value initialValue
      u8   isConst             ; FO4+ only. Also set on "Auto Const" property backing vars [caprica-run]
  ---- Starfield (gameID 4) only: u16 guardCount; sidx guardNames[guardCount] ----
  u16  propertyCount
  Property[propertyCount]:
      sidx name
      sidx typeName
      sidx docString
      u32  userFlags
      u8   flags               ; bit0 READ, bit1 WRITE, bit2 AUTOVAR
      if (flags & 4):  sidx autoVarName          ; "::<Name>_var"
      else:
         if (flags & 1): Function readHandler     ; Function WITHOUT leading name
         if (flags & 2): Function writeHandler
  u16  stateCount
  State[stateCount]:
      sidx name                ; "" = empty state
      u16  functionCount
      { sidx functionName; Function body }[functionCount]
```

- Sources: `[src: Champollion/Pex/FileReader.cpp:220-358; Caprica/Caprica/pex/PexObject.cpp:9-41, PexStruct.cpp, PexStructMember.cpp, PexVariable.cpp, PexProperty.cpp, PexState.cpp; caprica-run]`.
- How property kinds compile `[src: Caprica/Caprica/papyrus/PapyrusProperty.cpp; caprica-run]`:
  - `Auto` → `flags=0x07` plus `autoVarName "::X_var"`, and the backing variable is added to the variable table.
  - `Auto Const` → also `flags=0x07`; the backing variable's `isConst=1`.
  - `AutoReadOnly` → `flags=0x01` with a getter body `return <literal>`.
  - Full properties → `flags=0x01`, `0x02` or `0x03` with handler bodies.
- Struct example `[caprica-run]`: `Struct Point { float X = 1.5 {doc}; float Y; int Tag = 7 Hidden }` produces members:
  - `X Float uf=0 default=1.5f const=0 doc="doc for X"`
  - `Y Float default=None`
  - `Tag Int uf=0x1 default=7`
- **There is no "Native" flag in PEX.** Champollion uses a hardcoded native-class list. `Native` is a compile-time property. `[src: Champollion/Decompiler/PscCoder.cpp:161-186]`

### 1.7 Function

```
[sidx name]          ; present in State lists; ABSENT for property handlers
sidx returnTypeName  ; "None" for no return
sidx docString
u32  userFlags
u8   flags           ; bit0 GLOBAL (static), bit1 NATIVE
u16  paramCount;  { sidx name; sidx typeName }[paramCount]
u16  localCount;  { sidx name; sidx typeName }[localCount]
u16  instructionCount; Instruction[instructionCount]
```

- Identical in both games. `[src: Champollion/Pex/FileReader.cpp:364-403; Caprica/Caprica/pex/PexFunction.cpp:7-29]`
- Native functions have 0 instructions.
- Events are ordinary functions; vanilla native scripts declare events with empty bodies, e.g. `Event OnDeath(Actor akKiller) EndEvent` `[src: f4se/scripts/vanilla/Actor.psc:904]`.
- ⚠ papyrus-vm bug: `FillFuncInfo` appends locals into `params`. `MakeLocals` compensates by treating params beyond the argument count as locals. Fix both together. `[src: papyrus-vm/src/papyrus-vm-lib/Reader.cpp:298-306; ActivePexInstance.cpp:665-703]`

### 1.8 Value encoding ("variable data")

| Type byte | Kind | Payload |
|---|---|---|
| 0 | None | — |
| 1 | Identifier (variable, parameter, local, `self`, `::State`, function/property/struct-member/type name) | sidx |
| 2 | String literal | sidx |
| 3 | Integer | i32 |
| 4 | Float | f32 |
| 5 | Bool | u8 (≠0 = true) |

- Identical in both games. **No other type byte is legal in a file.**
- Var, struct and array values are never literals: they are created at runtime.
- `[src: Champollion/Pex/Value.hpp:12-20, FileReader.cpp:597-641; Caprica/Caprica/pex/PexValue.h:17-28]`
- Caprica's 20 (Label) and 21 (TemporaryVar) are internal to the compiler and never written.
- ⚠ papyrus-vm's `FillVariableData` accepts bytes 11–15 and reads 4 bytes. That is not valid PEX and should throw. `[src: papyrus-vm/src/papyrus-vm-lib/Reader.cpp:256-270]`

### 1.9 Instruction encoding

```
u8    opcode
Value args[fixedArgCount(opcode)]
if hasVarargs(opcode):          ; only 0x17 callmethod, 0x18 callparent, 0x19 callstatic in FO4
   Value count                  ; MUST be type 3 (Integer)
   Value varargs[count]
```

- Sources: `[src: Champollion/Pex/FileReader.cpp:409-444; Caprica/Caprica/pex/PexInstruction.cpp:98-176]`.
- **Jump targets** (`jmp`, `jmpt`, `jmpf`) are **relative to the jump instruction itself**: target = ip + offset, so offset 1 means "next instruction" `[src: Caprica/Caprica/pex/PexFunctionBuilder.cpp:14-24]`. papyrus-vm already implements this as `jumpStep = off - 1` followed by the loop `++` `[src: papyrus-vm/src/papyrus-vm-lib/ActivePexInstance.cpp:287-302]`.
- The CK and Caprica fill in default arguments at the call site, so call varargs always supply every parameter `[web: falloutck "Function Reference"; caprica-run: StartTimerGameTime(1.0) → varargs 1.0, 0]`.
- A `None` default for an array or object parameter is materialised with `cast ::tempN, None` `[caprica-run]`.

### 1.10 Compiler-generated names (both games unless noted)

| Name | Meaning |
|---|---|
| `self` | Identifier for the current script object |
| `::State` | The script's state variable. FO4 `GetState()` / `GotoState()` read and write it inside `ScriptObject.pex` |
| `::nonevar` (CK Skyrim: `::NoneVar` `[ck-pex]`) | Local of type `None` used as the destination of void calls |
| `::temp<N>` | Compiler temporaries. Reused per type |
| `::mangled_<name>_<n>` | Shadowed block-local renamed `[src: Caprica/Caprica/papyrus/PapyrusFunction.cpp:146]` |
| `::<Prop>_var` | Auto-property backing variable `[src: Caprica/Caprica/papyrus/PapyrusProperty.h:45-49]` |
| `::remote_<Type>_<Event>` | **FO4:** remote or custom event handler, e.g. `::remote_Actor_OnDeath`, `::remote_ObjectReference_OnActivate`, `::remote_PexVm:Sender_Ping`. The first parameter is the sender `[src: Caprica/Caprica/papyrus/parser/PapyrusParser.cpp:528-534; caprica-run]` |
| `GetState` / `GotoState` / `onBeginState` / `onEndState` | **Skyrim only:** generated into every root script `[ck-pex; src: Caprica/Caprica/papyrus/PapyrusState.h:68-125]`. FO4 has them **only** in `ScriptObject.pex`, as real functions with bodies, and `OnBeginState(string asOldState)` / `OnEndState(string asNewState)` take one argument `[src: f4se/scripts/vanilla/ScriptObject.psc:32-43,258-275]` |

**CK optimisation:** reads and writes of an `Auto` property through `self` compile to direct access of `::X_var` `[caprica-run: OnInit uses ::TheSender_var]`. The CK applies this even to auto properties declared in a **parent** script `[src: Caprica/README.md "Deliberate Differences"]`. So the VM's variable lookup must find a parent's `::X_var`.

### 1.11 FO76 and Starfield additions (reject for FO4)

- FO76 adds opcode 0x2F `array_getallmatchingstructs`.
- Starfield adds a guard table in the object (between variables and properties) and opcodes 0x30 `lock_guards` (varargs), 0x31 `unlock_guards` (varargs) and 0x32 `try_lock_guards` (1 + varargs).
- `Guard`/`TryGuard` are **Starfield-only** keywords. They do not exist in FO4 PEX or in the FO4 compiler `[src: Caprica/Caprica/papyrus/parser/PapyrusLexer.h:101-139; Caprica/Caprica/pex/PexObject.cpp:28-32]`.
- A FO4 reader must reject opcodes > 0x2E. Caprica enforces `Fallout4OpcodeMax = ArrayClear` `[src: Caprica/Caprica/pex/PexInstruction.h:72-73; PexInstruction.cpp:104-131]`.

### 1.12 Verification artifacts

- `pexvm/pexdump.py` is a ~180-line Python reader written from this section. It parses all of these with "EOF ok":
  - Caprica FO4 output: `pexvm/out2/PexVmOpsTest.pex` and `pexvm/out2/PexVm/Sender.pex`, compiled from `pexvm/fo4src/*.psc` against `f4se/scripts/vanilla`.
  - A Caprica Skyrim sample.
  - The three CK Skyrim files in `unit/papyrus_test_files/pex/`.
- Copy the `.psc` files and the dumper into the repo when implementing (see §6.9).
- Compile command used:

```
Caprica --game fallout4 --ignorecwd -i <src> -i <vanilla-psc-only-dir> -f Institute_Papyrus_Flags.flg -o <out> <files>
```

(The import directory must not contain the `.flg` file: Caprica tries to compile every file it finds there.)

---

## 2. Opcode table 0x00–0x2E

**Operand notation**

| Code | Meaning | Value type | Must the VM dereference it? |
|---|---|---|---|
| **D** | Destination | Identifier | Resolve to a storage slot (lvalue) |
| **V** | Any value | Literal or Identifier | Yes, if Identifier |
| **N** | Name operand | Identifier | **Never** dereference it as a variable |
| **S** | Name operand | String literal | Read as a literal |
| **L** | Relative jump offset | Integer | — |

Sources:
- Operand roles and order: `[src: Caprica/Caprica/pex/PexFunctionBuilder.h:29-135 (OPCODES table); Champollion/Pex/Instruction.cpp:28-80; Champollion/Decompiler/PscDecompiler.cpp:475-708]`.
- Mnemonics: Champollion short names and CK `.pas` names `[src: Caprica/Caprica/pex/PexInstruction.cpp:267-320]`.
- New-op encodings checked by `[caprica-run]`.

### 2.1 Skyrim and FO4 common opcodes (0x00–0x23)

| Op | Mnemonic (asm) | Fixed operands | VA | Semantics |
|---|---|---|---|---|
| 00 | nop (NOOP) | — | | — |
| 01 | iadd | D, V a, V b | | D = a + b (int32, wraps) |
| 02 | fadd | D, V, V | | float add |
| 03 | isub (ISUBTRACT) | D, V, V | | |
| 04 | fsub | D, V, V | | |
| 05 | imul | D, V, V | | |
| 06 | fmul | D, V, V | | |
| 07 | idiv | D, V, V | | Division by 0 → error "Cannot divide by zero" `[web: falloutck Papyrus Runtime Errors]` |
| 08 | fdiv | D, V, V | | |
| 09 | imod | D, V, V | | Result has the sign of the dividend `[web: falloutck Operator Reference]` |
| 0A | not | D, V | | D = !bool(V). Any type is cast to bool first (§3.4) |
| 0B | ineg (INEGATE) | D, V | | |
| 0C | fneg | D, V | | |
| 0D | assign | D, V | | Copy. Reference types share the reference; Var copies its box |
| 0E | cast | D, V | | Convert V to the **declared type of D** (a local or variable type string). Also used for every implicit coercion: int→float, child→parent object, x→Var, x→String, x→Bool `[src: Caprica/Caprica/papyrus/PapyrusResolutionContext.cpp coerceExpression; caprica-run]` |
| 0F | cmp_eq | D, V, V | | D = (a == b). `!=` compiles to `cmp_eq` followed by `not` `[src: Caprica/Caprica/papyrus/expressions/PapyrusBinaryOpExpression.h:68-73]` |
| 10 | cmp_lt | D, V, V | | int/float only |
| 11 | cmp_lte | D, V, V | | |
| 12 | cmp_gt | D, V, V | | |
| 13 | cmp_gte | D, V, V | | |
| 14 | jmp | L | | ip += L |
| 15 | jmpt | V cond, L | | if bool(cond): ip += L. `&&`/`||` compile to assign + jmpf/jmpt (short-circuit) |
| 16 | jmpf | V cond, L | | |
| 17 | callmethod | N func, V object (`self` allowed), D result | ✓ | Call a method on an object. A void result goes to `::nonevar` |
| 18 | callparent | N func, D result | ✓ | Call the parent script's version on self |
| 19 | callstatic | N script (full lowered name), N func, D result | ✓ | Global function |
| 1A | return | V | | |
| 1B | strcat | D, V, V | | Both operands are already strings (the compiler casts them) |
| 1C | propget | N prop, V object, D result | | Getter or auto variable |
| 1D | propset | N prop, V object, V value | | |
| 1E | array_create | D, V size | | Element type comes from D's declared type. Elements get their default value. Size 0–128 |
| 1F | array_length | D, V array | | A None array gives 0 |
| 20 | array_getelement | D, V array, V index | | Errors: "Array index X is out of range (0-Y)", "Cannot access an element of a None array" |
| 21 | array_setelement | V array, V index, V value | | |
| 22 | array_findelement | V array, D result, V value, V startIndex | | Search forward from the start index. Returns -1 if not found. Default start is 0 |
| 23 | array_rfindelement | V array, D result, V value, V startIndex | | Search backward. Start -1 means the last element. Default -1 |

### 2.2 FO4 additions (0x24–0x2E)

| Op | Mnemonic (CK asm) | Fixed operands (order) | Runtime semantics |
|---|---|---|---|
| **24** | is (IS) | **D** bool, **V** value, **N** typeName (Identifier, e.g. `Int`, `form`, `pexVm:Sender#Point`) | See below |
| **25** | struct_create (STRUCTCREATE) | **D** | See below |
| **26** | struct_get (STRUCTGET) | **D**, **V** struct, **N** memberName (Identifier) | See below |
| **27** | struct_set (STRUCTSET) | **V** struct, **N** memberName (Identifier), **V** value | See below |
| **28** | array_findstruct (ARRAYFINDSTRUCT) | **V** array, **D** int, **S** memberName (**String literal**), **V** value, **V** startIndex | See below |
| **29** | array_rfindstruct (ARRAYRFINDSTRUCT) | same as 28 | See below |
| **2A** | array_add (ARRAYADDELEMENTS) | **V** array, **V** value, **V** count | See below |
| **2B** | array_insert (ARRAYINSERTELEMENT) | **V** array, **V** value, **V** index | See below |
| **2C** | array_removelast (ARRAYREMOVELASTELEMENT) | **V** array | Remove the last element. Empty or None array → error, no-op `[inference]` |
| **2D** | array_remove (ARRAYREMOVEELEMENTS) | **V** array, **V** index, **V** count | See below |
| **2E** | array_clear (ARRAYCLEARELEMENTS) | **V** array | Length becomes 0. The same array object is kept, so other references see it empty |

**0x24 `is`**
- For base types (`Int`, `Float`, `Bool`, `String`) the check is **strict**: the runtime type must match exactly. A Var operand is checked by its content type.
- For object types the check is **loose**: true if the value would cast successfully to that type, e.g. an ObjectReference `is Form`.
- A None value gives false `[inference]`.
- Struct and array types: exact type match `[inference]`.
- `[web: falloutck Operator Reference "Type Check", Differences page "Is Operator"]`

**0x25 `struct_create`**
- Create a new struct whose type is **D's declared type string** (`script#Struct`). Struct values are references.
- Each member gets the PEX member `defaultValue`. If that is None, use the member type's default: `0`, `0.0`, `False`, `""`, None `[caprica-run + web: falloutck Struct Reference]`.

**0x26 `struct_get`**
- D = member value.
- Unknown member → error "Struct type X does not contain a variable named Y" `[web]`.
- None struct → error, and D gets the default for its type `[inference]`.

**0x27 `struct_set`**
- Set the member, casting the value to the member type `[inference]`.
- A None struct → error, no-op.

**0x28 `array_findstruct` / 0x29 `array_rfindstruct`**
- Search the elements of a struct array whose `member == value`. Elements that are None are skipped `[inference]`.
- Start indices and defaults are as for 0x22/0x23: findstruct default start 0, rfindstruct default start -1.
- Not found → -1 (the wiki says "a negative value").
- Unknown member → error "Struct does not have a variable with the name X and so it cannot be searched for" `[web]`.

**0x2A `array_add`**
- Append `value` **`count` times**. `Add(x)` compiles with count = 1 `[caprica-run]`.
- Structs and objects are appended **by reference**: `Add(struct, 3)` adds the same struct 3 times `[web: falloutck Add - Array notes]`.
- The total is limited to 128 elements `[web: falloutck Arrays (Papyrus)]`.
- count ≤ 0 → no-op `[inference]`.
- A None array → error, no-op `[inference]`. The array is not created.

**0x2B `array_insert`**
- Insert the value before `index`; `index == length` appends.
- Out of range → error, no-op `[inference]`. The 128 limit applies.

**0x2D `array_remove`**
- Remove `count` elements starting at `index` and compact the rest. `Remove(i)` compiles with count = 1.
- Clamp `count` to the end of the array; an invalid index → error `[inference]`.

**Notes for papyrus-vm**
- The new ops are all fixed-arity and **none take varargs**. Champollion's table gives fixed counts (`is`=3, `struct_create`=1, `struct_get`=3, `struct_set`=3, `findstruct`=5, `rfindstruct`=5, `add`=3, `insert`=3, `removelast`=1, `remove`=3, `clear`=1), which matches Caprica.
- The mutating array ops (0x2A–0x2E) take the array as a **value**. Arrays are references, so they mutate the shared array in place.
- papyrus-vm's `TransformInstructions` dereferences every Identifier operand from a single start index. For FO4 this must become a per-opcode mask of N operands that are **not** dereferenced:
  - 0x17 arg0, 0x18 arg0, 0x19 args 0–1, 0x1C/0x1D arg0
  - **0x24 arg2, 0x26 arg2, 0x27 arg1**
  - S operands (0x28/0x29 arg2) are string literals and are not affected.
  - `[src: papyrus-vm/src/papyrus-vm-lib/ActivePexInstance.cpp:705-755]`

---

## 3. FO4 type system as seen in PEX

### 3.1 Type name strings

| Papyrus type | PEX string (Caprica, `[caprica-run]`) | Notes |
|---|---|---|
| int, float, bool, string | `Int`, `Float`, `Bool`, `String` | CK Skyrim writes the same capitalisation `[ck-pex]` |
| var | `Var` | |
| no return | `None` | |
| `CustomEventName`, `ScriptEventName` parameters | `String` | Compile-time-only pseudo-types `[src: Caprica/Caprica/papyrus/PapyrusType.cpp:56-59]` |
| object | Full namespaced script name. Caprica writes it via `loweredName()`; observed as `actor`, `form`, `objectReference`, `scriptObject`, `pexVm:Sender` | Case differs between compilers, so **always compare case-insensitively** |
| struct | `<script>#<Struct>`, e.g. `pexVm:Sender#Point`, `quest#QuestStage` | Source syntax is `Script:Struct`. Champollion maps `#`→`:` when decompiling `[src: Champollion/Decompiler/PscCoder.cpp:834]` |
| arrays | `<elem>[]`: `Int[]`, `Var[]`, `form[]`, `pexVmOpsTest#Pair[]` | No nested arrays |
| `callstatic` script operand | Full lowered script name, e.g. `utility`, `pexVm:Sender` | |
| `is` type operand | Same strings as above, as an Identifier | |

### 3.2 Runtime value categories (the game VM's `TypeInfo::RawType`, reused by VMAD)

| Raw | Kind | Raw | Kind |
|---|---|---|---|
| 0 | None | 11 (0xB) | Object[] |
| 1 | Object | 12 | String[] |
| 2 | String | 13 | Int[] |
| 3 | Int | 14 | Float[] |
| 4 | Float | 15 | Bool[] |
| 5 | Bool | **16 (0x10)** | **Var[]** |
| **6** | **Var** | **17 (0x11)** | **Struct[]** |
| **7** | **Struct** | 18 | end marker |

`[src: lx/include/RE/B/BSScript_TypeInfo.h:14-34; f4se/f4se/PapyrusValue.h:122-146]`

These numbers deliberately match papyrus-vm's existing `VarValue::Type` (0–5, 11–15) and libespm's VMAD `Property::Type`. Add 6, 7, 16 and 17 in the same numbering (§6.3).

### 3.3 Reference vs value semantics, and defaults

- **Value types:** int, float, bool, string. **Reference types:** objects, arrays, structs. Assigning a reference type shares it `[web: falloutck Variable Reference]`.
- **Var** is a box. It remembers both the value and its runtime type. Copying a Var deep-copies the box: the game's `Variable::copy` does `new Variable(*rhs.v)` `[src: lx/src/RE/B/BSScript_Variable.cpp copy()]`. A reference held inside the box is still shared.
- Var can hold anything except arrays at compile level. F4SE's `Utility.VarArrayToVar` can smuggle arrays in, so the VM should tolerate it `[src: f4se/scripts/modified/Utility.psc:1-13]`.
- Defaults: Bool False, Int 0, Float 0.0, String "", and None for Objects, Arrays, Structs and Var `[web: falloutck Default Value Reference]`.
- A struct variable starts as **None** until `new` creates the struct.
- Struct members cannot be arrays, structs, Var or const. Caprica notes this is only a compiler rule; the engine tolerates nesting, so do not crash on it `[web: falloutck Struct Reference; search result on Caprica/xEdit #544]`.
- Struct arrays are created full of None. Each element must be assigned with `new` `[web: falloutck Structs (Papyrus)]`.

### 3.4 Cast rules (the `cast` opcode; the target is D's declared type)

| To ↓ / from → | Notes |
|---|---|
| **Bool** | int ≠ 0; float ≠ 0 (with epsilon); string non-empty; object non-None; array length ≥ 1; **struct non-None**; Var → per its contents |
| **Int** | From float (truncate), string (parse, else 0), bool, Var (contents, else 0) |
| **Float** | From int, string, bool, Var |
| **String** | Anything. Bool → `True`/`False`. Object → `[ScriptName <EditorID (FormID)>]`. Array → comma list, possibly truncated with `...`. Struct → comma list of values. Var → its contents |
| **Object** | From an object or Var only. Upcast always works. Downcast gives None if the object is not an instance. A non-object Var gives None |
| **Array** | **New in FO4:** explicit cast to another array type **makes a copy** with per-element casts; elements that fail become None, 0 or equivalent `[web: Differences "Array Casting"; caprica-run: `cast ::temp5(objectReference[]), forms`]`. papyrus-vm's `default:` branch currently aliases instead of copying `[src: papyrus-vm/src/papyrus-vm-lib/ActivePexInstance.cpp:265-269]` |
| **Struct** | The CK wiki says "nothing can be cast to a struct". Caprica allows explicit `Var → anything` `[src: Caprica/Caprica/papyrus/PapyrusResolutionContext.cpp:52-60]` and emits `cast ::temp1(pexVm:Sender#Point), s(Var)` `[caprica-run]`. Implement: if the Var contains a struct of exactly that type (case-insensitive) return it, else None `[inference]` |
| **Var** | Box anything except arrays. None → None Var |

`[web: falloutck Cast Reference]`

### 3.5 Equality and comparison

- Strings compare **case-insensitively**, and floats use a small epsilon `[web: falloutck Operator Reference]`. papyrus-vm compares strings case-sensitively and floats exactly — an existing bug for Skyrim too `[src: papyrus-vm/src/papyrus-vm-lib/VarValue.cpp:436-458]`.
- Objects are equal when they are the same game object (handle).
- Arrays and structs compare by reference identity `[inference]`.
- `Var == Var` compares the boxed type and value `[inference]`. The compiler casts the non-Var side to Var first `[caprica-run: cmp_eq ::temp3, w(Var), v(Var)]`.
- Mixed int and float operands are coerced to float by the compiler. A string operand coerces the other side to string. A bool operand coerces the other side to bool `[src: Caprica/Caprica/papyrus/expressions/PapyrusBinaryOpExpression.h coerceToSameType]`.

### 3.6 Struct, object and namespace name resolution

- All type references in PEX are **fully qualified**. `import` is resolved at compile time and does not appear in PEX `[src: Caprica/Caprica/papyrus/PapyrusType.cpp:46-77]`.
- Namespaces map to folders: `MyMod:Quests:MyQuest` is stored at `Scripts/MyMod/Quests/MyQuest.pex` `[web: falloutck Identifier Reference "Namespaces"]`.
- A struct type `a:b#C` is resolved by loading script `a:b` and looking up struct `C` in its struct table.

### 3.7 Custom events and remote events: what the compiler emits

| Source | Compiled form |
|---|---|
| `CustomEvent Ping` in `PexVm:Sender` | **Nothing in PEX**. Champollion cannot recover these declarations `[src: Caprica/Caprica/papyrus/PapyrusObject.cpp:28-80 has no customEvents emission; web: search summary of nexusmods thread 8272748]` |
| `SendCustomEvent("Ping", kargs)` | `callmethod SendCustomEvent, self, ::nonevar` with varargs `["PexVm:Sender_Ping"(Str), kargs]`. The literal becomes `"<script that declares the event>_<event>"` `[src: Caprica/Caprica/papyrus/expressions/PapyrusFunctionCallExpression.cpp:470-497; caprica-run]` |
| `RegisterForCustomEvent(TheSender, "Ping")` | The sender is cast to `scriptObject`; varargs `[::temp0, "PexVm:Sender_Ping"]` `[caprica-run]` |
| `Event PexVm:Sender.Ping(PexVm:Sender akSender, Var[] akArgs)` | Function `::remote_PexVm:Sender_Ping(akSender: pexVm:Sender, akArgs: Var[])` |
| `RegisterForRemoteEvent(Victim, "OnDeath")` | Varargs `[cast-to-scriptObject(Victim), "OnDeath"]`. A `ScriptEventName` literal is **unchanged** when it names a game event, and rewritten to `Script_Event` only if it resolves to a custom event `[src: same file :492-497]` |
| `Event Actor.OnDeath(Actor akSender, Actor akKiller)` | Function `::remote_Actor_OnDeath(akSender: actor, akKiller: actor)` |

**Dispatch rules the VM must implement (derived from the above)**
- **Custom event:** `SendCustomEvent(name, args)` on sender object O. For every registration `(O, name) → receiver script R`, queue a call of `"::remote_" + name` on R with `(O, args)`. If `args` is None, pass None (`Var[]`) `[inference]`.
- **Remote event:** when game event E is delivered to object O, also dispatch to each `(O, E) → R` registration. The function called is `"::remote_" + T + "_" + E`, where T is the **least-derived script in O's type chain that declares E**:
  - Actor OnDeath → `Actor`; Actor OnItemAdded or OnActivate → `ObjectReference`.
  - A registration made on a ReferenceAlias goes to `ReferenceAlias.<E>` instead.
  - `[web: falloutck Remote Papyrus Event Registration]`
  - Events declared in ScriptObject cannot be registered remotely.
- Relays go **only to the registering script**: not to other scripts on the same form, and not to aliases or magic effects. A direct Papyrus call of an event is never relayed `[web: falloutck RegisterForRemoteEvent/RegisterForCustomEvent]`.
- The game stores both kinds in one table, `Internal::VirtualMachine::eventRelays : handle → (eventName → set<Object>)` `[src: lx/include/RE/B/BSScript_Internal_EventRelay.h; BSScript_Internal_VirtualMachine.h:281]`.

### 3.8 Property groups, Const and Native flags

- **Groups** exist only in debug info (§1.4). They have no runtime effect `[web: falloutck Group Reference]`.
- **Const script** (object `isConst = 1`): no states, no non-const variables.
- **Const variable** and **Auto Const property:** the backing variable has `isConst = 1`. The game ignores save-game values for them. `SetPropertyValue` on a const auto property is an error `[web: falloutck Property Reference, ScriptObject.psc:157-161]`.
- **Native:** not stored in PEX (§1.6).

### 3.9 Things that are not in FO4

- No `Guard`/`TryGuard` (Starfield only).
- No `OnUpdate`/`RegisterForSingleUpdate`: timers replace them.
- Script authors cannot define new events in non-native scripts `[web: falloutck Differences page]`.

---

## 4. FO4 runtime semantics a server-side VM must emulate

### 4.1 ScriptObject: the implicit root

- `Scriptname ScriptObject Native Hidden` is the parent of `Form`, `Alias`, `ActiveMagicEffect`, and every script written without `extends`. For those scripts the compiler writes `ScriptObject` into `parentClassName` `[src: Caprica/Caprica/papyrus/parser/PapyrusParser.cpp:157-165; caprica-run]`.
- **A ScriptObject is a script instance** — the pair (bound game object, script type) — not the form. Every registration and timer is **per script** `[web: falloutck Differences "Event Registration", "ScriptObject script"]`.
- F4SE keys its registrations the same way, by `(handle, objectTypeName)` `[src: f4se/f4se/PapyrusScriptObject.cpp:240-244]`.
- The full list of natives is in `[src: f4se/scripts/vanilla/ScriptObject.psc]`. Behaviour required on the server:

**Dynamic calls and casts**

| Native | Semantics |
|---|---|
| `Var CallFunction(string fn, Var[] params)` | **Latent** `[web: falloutck Category:Latent Functions]`. Call `fn` on *this script instance*. Parameter types must match exactly: no auto-cast, and optional parameters must be supplied. The result is boxed into a Var. An unknown function is an error |
| `CallFunctionNoWait` | Queue the same call. Return None immediately |
| `Var GetPropertyValue(string)` | Latent. Runs the getter or reads the auto variable. Error if missing or write-only |
| `SetPropertyValue(string, Var)` | Latent. Type must match exactly. Error if read-only or const auto |
| `SetPropertyValueNoWait` | Queued version of `SetPropertyValue` |
| `ScriptObject CastAs(string)` | Find a script of that type (or a subtype) attached to the same bound object. None if there is none. No need to cast down via ObjectReference first. Choice is undefined when two scripts match |
| `bool IsBoundGameObjectAvailable()` | |
| `string GetState()` / `GotoState(string)` | Non-native bodies in ScriptObject.pex (see below) |

`GotoState(s)` does three things in order: `OnEndState(s)`, then `::State = s`, then `OnBeginState(old)`. It does not return until both events finish. The events still fire if the state is unchanged. State names are **case-insensitive**. An auto state does not receive `OnBeginState` at init `[web: falloutck GotoState, OnBeginState]`.

**Timers**

| Native | Semantics |
|---|---|
| `StartTimer(float sec, int id = 0)` | Per-script timer keyed by id. Restarting an existing id resets it. Single shot. Counts only outside menu mode and is affected by the global time multiplier. Fires `OnTimer(id)` on **this instance only** |
| `CancelTimer(id)` | |
| `StartTimerGameTime(hours, id)` | Minimum 0.033 h. Fires `OnTimerGameTime(id)`. Real-time and game-time ids do not conflict |
| `CancelTimerGameTime(id)` | |

Quests and aliases cancel their timers when the quest stops; magic effects cancel theirs when removed. The wiki notes the default id 0 "never starts" on ObjectReference scripts; treat that as a game bug and do not emulate it `[web: falloutck StartTimer/OnTimer]`.

**Event registrations**

| Native | Semantics |
|---|---|
| `bool RegisterForRemoteEvent(ScriptObject src, ScriptEventName e)` | §3.7 |
| `UnregisterForRemoteEvent`, `UnregisterForAllRemoteEvents` | |
| `RegisterForCustomEvent(ScriptObject sender, CustomEventName)` | §3.7 |
| `UnregisterForCustomEvent`, `UnregisterForAllCustomEvents` | |
| `SendCustomEvent(name, Var[] args = None)` | Returns immediately. Receivers run as if it were a game event, in parallel. The sender passed to receivers is the object the call was made on |
| `RegisterForHitEvent(target, aggressorFilter=None, sourceFilter=None, projectileFilter=None, power=-1, sneak=-1, bash=-1, block=-1, abMatch=true)` | **Single-shot**. `OnHit` is **not delivered without registration**. Filters may be a ref, base object, keyword, faction, form list (not recursed), ref alias or collection alias. -1 = either, 0 = must be false, >0 = must be true. `abMatch=false` inverts the whole filter. When several registrations match, which one is consumed is undefined |
| `RegisterForMagicEffectApplyEvent` | Same pattern (caster and effect filters) |
| `AddInventoryEventFilter(Form)`, `RemoveInventoryEventFilter`, `RemoveAllInventoryEventFilters` | Per script. `OnItemAdded`/`OnItemRemoved` are delivered **only to scripts that have at least one filter**. A None filter lets everything through |
| `RegisterForDistanceLessThanEvent` / `RegisterForDistanceGreaterThanEvent(obj1, obj2, dist)` | Single-shot. Fires immediately if already true. One registration per unordered pair and kind; re-registering updates the distance |
| `RegisterForDirectLOSGain`/`Lost`, `RegisterForDetectionLOSGain`/`Lost` | Single-shot. One LOS registration per viewer/target pair |
| `RegisterForAnimationEvent`, `RegisterForMenuOpenCloseEvent`, `RegisterForPlayerSleep`/`Wait`/`Teleport`, `RegisterForTrackedStatsEvent`, `RegisterForRadiationDamageEvent` | Per script |
| `UnregisterForAllEvents` | Removes every registration for the script |

Unless stated otherwise, every registration is removed automatically when the quest stops (for quests and aliases) or when the effect is removed (for magic effects).

Sources for this section: `[web: falloutck ScriptObject pages fetched into pexvm/ck/*.txt]`.

### 4.2 Function resolution and inheritance

The VM picks a function in this order:
1. The current script's **current state**.
2. Each **parent script's current state**, walking up the chain.
3. The current script's **empty state**.
4. Each **parent script's empty state**.

`[web: falloutck States (Papyrus) "How Functions Are Picked"]`

- The child's auto state wins over the parent's. If the child has none, the parent's auto state is used `[web: falloutck State Reference]`.
- ⚠ papyrus-vm only searches the attached instance's own PEX, compares state names case-sensitively, and special-cases `GotoState`/`GetState`. Tests work around this by attaching parent scripts separately. For FO4 the chain lookup is **mandatory**, because `GetState`/`GotoState` live only in `ScriptObject.pex` `[src: papyrus-vm/src/papyrus-vm-lib/ActivePexInstance.cpp:57-77; VirtualMachine.cpp:256-293; unit/VirtualMachineTest.cpp:142-149]`.

### 4.3 Threading and latency model

- Only one thread is active in a script instance at a time.
- **Any external call** releases the instance lock while it waits. External calls include another object, a global function, any native (even non-latent ones such as `Debug.Trace`), and a property on another object. Reading a property of the same script and array operations are not external `[web: falloutck Threading Notes (Papyrus)]`.
- **Latent natives** suspend the stack until the native returns.
  - The 61 FO4 latent natives include `Utility.Wait`/`WaitGameTime`/`WaitMenuMode`, `ScriptObject.CallFunction`/`GetPropertyValue`/`SetPropertyValue`, `Utility.CallGlobalFunction`, `ObjectReference.MoveTo`/`SetPosition`/`SetAngle`/`AddItem`/`RemoveItem`/`Enable`/`Disable`/`Delete`, `Quest.Start`/`SetCurrentStageID`, `Message.Show` and `Actor.Resurrect`.
  - The full list is in `pexvm/ck/latent_functions.txt` `[web: falloutck Category:Latent Functions]`.
  - papyrus-vm already supports latent natives through `VarValue(Viet::Promise)` and `EnsureCallResultIsSynchronous` `[src: papyrus-vm/src/papyrus-vm-lib/ActivePexInstance.cpp:180-204; unit/VirtualMachineTest.cpp:155-257]`.
- **NoWait variants** (`CallFunctionNoWait`, `SetPropertyValueNoWait`, `Utility.CallGlobalFunctionNoWait`, `SendCustomEvent`) start a new stack and return at once. A server should put them on a VM job queue that drains each tick. That avoids unbounded re-entrancy, which synchronous `SendEvent` would cause `[inference]`.
- **OnInit:** until it finishes, the script receives no events and other callers block `[web: falloutck OnInit]`.
- **Stack depth:** "Stack too deep … returning None". papyrus-vm caps depth at 128 and the instruction quota at 100k `[src: papyrus-vm/src/papyrus-vm-lib/ActivePexInstance.cpp:775-786,826-833]`.
- **Error behaviour:** a failed call returns None. Assigning None to a non-object variable logs an error `[web: falloutck Papyrus Runtime Errors]`.

### 4.4 Event dispatch to related objects

- The game sends events with `GameVM::SendEventToObjectAndRelated(handle, name, argsFiller, filter, callback)` `[src: lx/include/RE/G/GameScript.h:538-548]`. The related objects are:
  - every script on the object;
  - **ReferenceAlias** scripts of aliases filled by the ref (ObjectReference events, plus Actor events if the ref is an actor);
  - **RefCollectionAlias** scripts, with the **sender ref prepended as the first argument**, e.g. `OnDeath(ObjectReference akSenderRef, Actor akKiller)`;
  - **ActiveMagicEffect** scripts on the actor, which receive Actor events.
  - `[web: falloutck ReferenceAlias/RefCollectionAlias/ActiveMagicEffect Script; src: f4se/scripts/vanilla/RefCollectionAlias.psc:293-309]`
- Timers and registered single-shot events (OnTimer, OnHit, distance, LOS) go **only** to the registering script.
- The server's current `WorldState::SendPapyrusEvent` sends to all scripts on the object. It must add the FO4 gates: OnHit and OnMagicEffectApply need a registration, and OnItemAdded/Removed need a filter. It must also relay remote events after the local dispatch `[src: falloutmp-server/cpp/server_guest_lib/WorldState.cpp:802-836]`.

### 4.5 VMAD (FO4 v6) and property initialisation, including structs

**Layout**

```
VMAD: i16 version (=6 for FO4; Skyrim 5)  i16 objFormat (=2)  u16 scriptCount
Script: wstring name; u8 flags (0 Local,1 Inherited,2 Removed,3 Inherited+Removed); u16 propertyCount
Property: wstring name; u8 type; u8 flags (1 Edited, 3 Removed); value by type:
  1 Object  (objFormat 2): u16 unused; i16 aliasID; u32 formID
  2 String  wstring | 3 Int i32 | 4 Float f32 | 5 Bool u8
  6 Var     : no payload decoded by xEdit (CK cannot fill Var properties) [inference]
  7 Struct  : u32 memberCount; Member{wstring name; u8 type; u8 flags; value}[memberCount]   (recursive)
  11..15 arrays: u32 count; elements
  16 Var[]  : u32 count only (xEdit decodes nothing further)
  17 Struct[]: u32 count; Struct[count] (each = u32 memberCount + members)
```

`[src: xEdit pexvm/wbDefinitionsFO4.pas:3697-3890]`

- Today libespm knows only types 1–5 and 11–15, and its `GetScriptData` is `noexcept` but throws `[src: libespm/include/libespm/Property.h:10-23; FALLOUT4_PORT_RESEARCH.md §4.2]`.

**Initialising a struct property** `[web: falloutck Struct Reference "If you make a struct property, then the Creation Kit will create and set up the struct for you"; inference for details]`
1. Take the property's declared type from the PEX (e.g. `quest#QuestStage`).
2. Create the struct and apply the PEX member defaults (§2.2 `struct_create`).
3. Overwrite the members present in VMAD by **name**, case-insensitively. Skip members with the Removed flag.
4. For a `Struct[]` property, build an array with one new struct per VMAD element.
5. A struct property that is absent from VMAD stays None.

**Name mapping**
- Store the value in the backing variable `::<Prop>_var`, as the server already does `[src: falloutmp-server/cpp/server_guest_lib/ScriptVariablesHolder.cpp:118-143]`.
- The VMAD lists the attached script's properties, including inherited ones. The variable holder must therefore cover the **whole inheritance chain** (§6.6).

**Game warnings to mirror:** "X does not have a property named Y", "read-only", "type mismatch … property skipped" `[web: falloutck Papyrus Runtime Errors]`.

---

## 5. CommonLibF4 VM internals for porting SP's reflection and call layer

Header paths are given for lx as `lx/include/RE/B/<file>`. alandtse has the same names under `CommonLibF4/include/RE/Bethesda/BSScript/<file-without-prefix>`. The Internal::VirtualMachine offsets match alandtse, which supports OG, NG and VR `[src: alandtse/.../Internal/VirtualMachine.h:249,273,283]`.

### 5.1 Singletons

- `RE::GameVM::GetSingleton()` uses REL ID 4796420 in lx. `GameVM::impl` (a `BSTSmartPointer<IVirtualMachine>`) is at **+0xB0**. `sizeof(GameVM) == 0x87B8` `[src: lx/include/RE/G/GameScript.h:498-594; lx/include/RE/IDs.h:1133-1139]`.
- `BSScript::Internal::VirtualMachine::GetSingleton()` returns `GameVM->impl` `[src: lx/src/RE/B/BSScript_Internal_VirtualMachine.cpp]`.
- F4SE writes this as `(*g_gameVM)->m_virtualMachine` `[src: f4se/f4se/PapyrusVM.h:263-302]`.
- REL IDs differ per runtime. Verify them against the Address Library for the target runtime.

### 5.2 `IVirtualMachine` vtable (`BSScript_IVirtualMachine.h`)

Indices are as annotated by lx. MSVC places overloads in reverse declaration order, and lx's comments already account for that. F4SE's names (`f4se/f4se/PapyrusVM.h:23-93`) are given where they cross-check.

| Idx | Function | F4SE |
|---|---|---|
| 07 | `RegisterObjectType(u32 typeID, const char*)` | RegisterForm |
| 08 | `GetScriptObjectType(const BSFixedString&, BSTSmartPointer<ObjectTypeInfo>&)` | — |
| 09 | `GetScriptObjectType(u32 typeID, …)` | GetObjectTypeInfo |
| 0A/0B | `GetScriptObjectTypeNoLoad(name / typeID)` | 0A "GetObjectTypeInfoByName" |
| 0C | `GetTypeIDForScriptObject(name, u32&)` | |
| 0E | `GetParentNativeType` | |
| 0F | `TypeIsValid(name)` | |
| 10 | `ReloadType(const char*)` | |
| 13 | `GetScriptStructType(name, BSTSmartPointer<StructTypeInfo>&)` | GetStructTypeInfo |
| 14 | `GetScriptStructTypeNoLoad` | |
| 15 | `GetChildStructTypes(parentObjName, …)` | |
| 16/17 | `CreateObject(name, props, out)` / `CreateObject(name, out)` | 17 CreateObjectIdentifier |
| 18 | `CreateStruct(name, BSTSmartPointer<Struct>&)` | CreateStruct |
| 19/1A | `CreateArray(const TypeInfo&, u32, out)` / `CreateArray(RawType, elemObjName, u32, out)` | 19 CreateArray |
| **1B** | **`BindNativeMethod(IFunction*)`** (returns bool) | **RegisterFunction** |
| 1C/1D | `SetCallableFromTasklets(obj, state, fn, bool)` / `(obj, fn, bool)` | SetFunctionFlagsEx/Flags |
| 1E | `ForEachBoundObject(handle, functor)` | ForEachIdentifier |
| 1F | `FindBoundObject(u64 handle, const char* type, bool allowConst, BSTSmartPointer<Object>&, bool exactMatch)` | GetObjectIdentifier |
| 22 | `CastObject(src, targetType, out)` | CastAs |
| 23/24 | `SetPropertyValue(obj, name, value, callback)` / `GetPropertyValue(obj, name, callback)` | ✓ |
| 25/26 | `GetVariableValue(obj, idx, out)` / `(handle, scriptName, idx, out)` | |
| 27 | `HandleImplementsEvent(handle, name)` | |
| 28/29/2A | `AddEventRelay` / `RemoveEventRelay` / `RemoveAllEventRelays` | |
| **2B** | `SendEvent(u64 handle, const BSFixedString& name, const std::function<bool(BSScrapArray<Variable>&)>& args, const std::function<bool(const BSTSmartPointer<Object>&)>& filter, const BSTSmartPointer<IStackCallbackFunctor>& cb)` | QueueEvent |
| **2C** | `DispatchStaticCall(obj, fn, argsFiller, cb)` | |
| 2D/2E | `DispatchMethodCall(const BSTSmartPointer<Object>& self, fn, argsFiller, cb)` / `DispatchMethodCall(u64 handle, objName, fn, argsFiller, cb)` | |
| 2F | `DispatchUnboundMethodCall(handle, BoundScript, fn, args, cb)` | |
| 30/31 | `IsWaitingOnLatent(stackID)` / `ReturnFromLatent(stackID, const Variable&)` | 31 ResumeStack |
| 32 | `GetErrorLogger` | |
| 33/34 | `GetObjectHandlePolicy()` (non-const / const) | 33 GetHandlePolicy |
| 35/36 | `GetObjectBindPolicy()` | 35 |
| 3C/3D | `PostCachedErrorToLogger` | |

`[src: lx/include/RE/B/BSScript_IVirtualMachine.h:46-109]`

**Skyrim vs FO4 differences that matter to SP**
- Hook **0x1B** instead of 0x18 `[src: skyrim-platform/src/platform_se/skyrim_platform/Hooks.cpp:62-71]`.
- Skyrim's Dispatch and SendEvent take an `IFunctionArguments*` object with `operator()(BSScrapArray<Variable>&)` `[src: skyrim-platform/.../VmFunctionArguments.h; VmCall.h:9-21]`. **FO4 takes a `std::function<bool(BSScrapArray<Variable>&)>`** plus an extra **filter** functor on SendEvent. Rewrite `VmFunctionArguments` and `VmCall` accordingly; lx has template helpers at `BSScriptUtil.h:1416-1450`.
- SP's Frida SendEvent hook reads `handle` from arg 1 and `eventName` from arg 2, and blocks events by replacing the name. That still works at vtable index 0x2B, but SP hooks Skyrim by address. In FO4 hook the vtable slot, or the address via `GameVM::SendEventToObjectAndRelated` `[src: skyrim-platform/.../FridaHooks.cpp:15-22,45-70]`.

### 5.3 `Internal::VirtualMachine` members (`BSScript_Internal_VirtualMachine.h:216-293`, `sizeof == 0xC080`)

| Offset | Member | SP use (Skyrim name) |
|---|---|---|
| 0x0090 | `IObjectHandlePolicy* handlePolicy` | |
| 0x0098 | `ObjectBindPolicy* objectBindPolicy` | |
| 0x00C0 | `BSSpinLock typeInfoLock` | |
| **0x0168** | `BSTHashMap<BSFixedString, BSTSmartPointer<ObjectTypeInfo>> objectTypeMap` | `vm->objectTypeMap` (VmProvider.cpp:15, CallNative.cpp:290) |
| **0x0198** | `BSTHashMap<BSFixedString, BSTSmartPointer<StructTypeInfo>> structTypeMap` | new for FO4 |
| 0x01C8 / 0x01F8 | `typeIDToObjectType` / `objectTypeToTypeID` | |
| 0xBD58 | `BSSpinLock runningStacksLock` | |
| **0xBD60** | `BSTHashMap<u32, BSTSmartPointer<Stack>> allRunningStacks` | `vm->allRunningStacks` (CallNative.cpp:286) |
| 0xBD90 | `waitingLatentReturns` | |
| 0xBDF8 | `BSSpinLock attachedScriptsLock` | FridaHooks.cpp:45 |
| **0xBE00** | `BSTHashMap<u64, BSTSmallSharedArray<AttachedScript>> attachedScripts`. `AttachedScript` = `BSTPointerAndFlags<BSTSmartPointer<Object>,1>`: mask off the low bit | FridaHooks.cpp:46 |
| 0xBE68 | `BSTArray<BSTSmartPointer<Struct>> allStructs` | |
| 0xBFB8 | `BSTHashMap<u64, BSTSmartPointer<EventRelay>> eventRelays` | |

F4SE asserts the same offsets: 0x168, 0x198, 0x1C8, BD58/BD60/BD90, BDF8/BE00 `[src: f4se/f4se/PapyrusVM.h:244-260]`.

### 5.4 `IFunction` and `NativeFunctionBase` (`BSScript_Internal_IFunction.h:45-67`, `BSScript_Internal_NativeFunctionBase.h:113-128`)

**IFunction vtable**

| Idx | Function | Idx | Function |
|---|---|---|---|
| 01 | `GetName` | 0B | `GetFunctionType` (0 normal, 1 getter, 2 setter) |
| 02 | `GetObjectTypeName` | 0C | `GetUserFlags` |
| 03 | `GetStateName` | 0D | `GetDocString` |
| 04 | `GetReturnType` (TypeInfo by value) | 0E | `InsertLocals(StackFrame&)` |
| 05 | `GetParamCount` | **0F** | **`Call(const BSTSmartPointer<Stack>&, ErrorLogger&, Internal::VirtualMachine&, bool inScriptTasklet)` → `CallResult`** (0 Completed, 1 SetupForVM, 2 InProgress, 3 FailedRetry, 4 FailedAbort) |
| 06 | `GetParam(u32, BSFixedString&, TypeInfo&)` | 10 | `GetSourceFilename` |
| 07 | `GetStackFrameSize` | 11 | `TranslateIPToLineNumber` |
| 08 | `GetIsNative` | 12 | `GetVarNameForStackIndex` |
| 09 | `GetIsStatic` | 13/14 | `CanBeCalledFromTasklets` / `SetCallableFromTasklets` |
| 0A | `GetIsEmpty` | | |

NativeFunctionBase adds 15 `HasStub` and 16 `MarshallAndDispatch(Variable& self, VM&, u32 stackID, Variable& ret, const StackFrame&)`. There is **no virtual `GetIsLatent`**: read the field.

**NativeFunctionBase layout**

| Offset | Field |
|---|---|
| 0x10 | `name` |
| 0x18 | `objName` |
| 0x20 | `stateName` |
| 0x28 | `TypeInfo retType` |
| 0x30 | `VDescTable descTable` (entries ptr, u16 paramCount, u16 totalEntries) |
| 0x40 | `bool isStatic` |
| 0x41 | `isCallableFromTasklet` |
| **0x42** | **`bool isLatent`** |
| 0x44 | `u32 userFlags` |
| 0x48 | `docString` |

`sizeof == 0x50`, so the callback begins at **+0x50**. F4SE agrees: `m_isLatent // 42`, `void* m_callback // 50`, `sizeof(NativeFunction) == 0x58` `[src: f4se/f4se/PapyrusNativeFunctions.h:118-156]`.

**What SP must change**
- `GetNativeFunctionAddr` (isLatent @0x42, fn @ `sizeof(NativeFunctionBase)`) ports unchanged `[src: skyrim-platform/.../GetNativeFunctionAddr.cpp:10-21]`.
- The Skyrim `raw+0x58` "long signature" byte is SKSE-specific `[src: Hooks.cpp:85-91]`. **F4SE has no such byte.** F4SE uses the long signature `(VirtualMachine*, u32 stackId, Base*, …)` exactly for latent natives (`LatentNativeFunctionN`, which returns bool) `[src: f4se/f4se/PapyrusNativeFunctionDef_Base.inl:90-110]`.
- lx's own `NativeFunction<F,LONG,…>` stores a **`std::function<F> _stub` at +0x50**, not a raw pointer `[src: lx/include/RE/B/BSScriptUtil.h:1280-1348]`. So `+0x50` is a code address only for game and F4SE natives. Detect lx and std::function natives by module and vtable before dumping offsets.
- `FunctionsDumpFactory` calls `nativeFunction->GetIsLatent()`. Replace that with a read of the `isLatent` field `[src: skyrim-platform/.../FunctionsDumpFactory.cpp:99-112]`.

### 5.5 `ObjectTypeInfo` (`BSScript_ObjectTypeInfo.h:196-218`, `sizeof 0x58`) and `StructTypeInfo` (`BSScript_StructTypeInfo.h:33-59`, `sizeof 0x70`)

**ObjectTypeInfo layout**

| Offset | Field |
|---|---|
| 0x10 | `name` |
| 0x18 | `parentTypeInfo` |
| 0x20 | `docString` |
| 0x28 | `BSTArray<BSTSmartPointer<PropertyGroupInfo>> propertyGroups` |
| 0x40 | bitfield: `linkedValid:2`, `isConst:1`, `userFlagCount:5`, `variableCount:10`, `variableUserFlagCount:6` |
| 0x44 | bitfield: `initialValueCount:10`, `propertyCount:10`, `staticFunctionCount:9` |
| 0x48 | bitfield: `emptyStateMemberFunctionCount:11`, `namedStateCount:7`, `initialState:7` |
| 0x50 | `void* data` |

- Iterators: `GetGlobalFuncIter()/GetNumGlobalFuncs()`, `GetMemberFuncIter()/GetNumMemberFuncs()` (empty state), `GetNamedStateIter()`, `GetPropertyIter()` (each `PropertyInfo { name; PropertyTypeInfo info }`, 0x48 bytes), `GetVariableIter()`, `GetParent()`, `GetName()`. These are the **same API names SP's VmProvider uses** `[src: skyrim-platform/.../VmProvider.cpp:57-72]`.
- `PropertyTypeInfo` (0x40): parentObjName, propertyName, type, permissions, `getFunction` @0x20, `setFunction` @0x28, `autoVarIndex` @0x30, userFlags, docString.
- The bitfield widths are effective engine limits: ≤1023 variables, ≤1023 properties, ≤511 global functions, ≤2047 empty-state functions, ≤127 named states `[inference from bitfields]`.

**StructTypeInfo layout**

| Offset | Field |
|---|---|
| 0x10 | `name` |
| 0x18 | `containingObjTypeInfo` |
| 0x20 | `BSTArray<StructVar> variables` — each `{ Variable initialValue; TypeInfo varType; BSFixedString docString; u32 userFlags; bool isConst }`, 0x28 bytes |
| 0x38 | `BSTHashMap<BSFixedString,u32> varNameIndexMap` |
| 0x68 | `linkedValid` |

### 5.6 `TypeInfo` raw encoding (`BSScript_TypeInfo.h`, 8 bytes)

- A `uintptr_t`. Values below `kArrayEnd` (0x12) are raw types (§3.2).
- Larger values are an `IComplexType*` (`ObjectTypeInfo` or `StructTypeInfo`). **Bit 0 set on the pointer means "array of"**.
- `GetRawType()` reads the complex type's `GetRawType()` (Object 1 or Struct 7) and adds 10 for arrays `[src: lx/src/RE/B/BSScript_TypeInfo.cpp:7-23]`.
- Skyrim's version has no Var/Struct, uses `kNoneArray`=10 and ends at 16. SP's switch on `GetUnmangledRawType()` must gain Var (6), Struct (7), Var[] (16) and Struct[] (17) `[src: skyrim-platform/.../CallNative.cpp:128-197; FunctionsDumpFactory.cpp:35-62]`.

### 5.7 `Variable`, `Array`, `Struct`, `Object`

| Class | Header | Layout |
|---|---|---|
| `Variable` (0x10) | `BSScript_Variable.h:194-228` | `TypeInfo varType` @0; union @8: `BSTSmartPointer<Object> o`, `BSFixedString s`, `u32/i32`, `float`, `bool`, **`Variable* v`** (Var box: heap inner value, deep-copied, inner is never itself a Var), `BSTSmartPointer<Struct> t`, `BSTSmartPointer<Array> a` |
| `Array` (0x30) | `BSScript_Array.h:74-79` | refcount @0, `TypeInfo elementType` @8, `BSSpinLock` @0x10, `BSTArray<Variable> elements` @0x18 |
| `Struct` (0x20 + n·0x10) | `BSScript_Struct.h:19-25` | `BSSpinLock` @4, `BSTSmartPointer<StructTypeInfo> type` @0x10, `constructed` @0x18, `valid` @0x19, `Variable variables[]` @0x20, ordered as `StructTypeInfo::variables` |
| `Object` (0x30 + n·0x10) | `BSScript_Object.h:52-64` | flags bitfield @0, `type` @8, `currentState` @0x10, `lockStructure` @0x18, `handle` @0x20, refCountAndHandleLock @0x28, `variables[]` @0x30 |

Var boxing in F4SE: `dst->SetVariable(new VMValue(m_value))`; unbox from `value->data.var`, where null means None `[src: f4se/f4se/PapyrusArgs.h:169-229]`.

### 5.8 `Stack` and `StackFrame`: the biggest change for `CallNative.cpp`

**StackFrame layout** (`BSScript_StackFrame.h:20-30`, `sizeof 0x40`)

| Offset | Field |
|---|---|
| 0x00 | `Stack* parent` |
| 0x08 | `StackFrame* previousFrame` |
| 0x10 | `BSTSmartPointer<IFunction> owningFunction` |
| 0x18 | `BSTSmartPointer<ObjectTypeInfo> owningObjectType` |
| 0x20 | `u32 ip` |
| 0x28 | `Variable self` |
| 0x38 | `u32 size` |
| 0x3C | `bool instructionsValid` |

**Stack layout** (`BSScript_Internal_Stack.h:73-89`, `sizeof 0xA8`)

| Offset | Field |
|---|---|
| 0x18 | `BSTSmallArray<MemoryPageData,3> pages` |
| 0x58 | `u32 frames` |
| **0x60** | **`StackFrame* top`** |
| 0x68 | `state` |
| **0x70** | **`Variable returnValue`** |
| 0x80 | `u32 stackID` |
| 0x90 | `callback` |

**Frame variables (arguments then locals) live in memory pages.** Access them only through:
- `u32 page = stack->GetPageForFrame(frame)` (lx REL ID 2314680)
- `Variable& v = stack->GetStackFrameVariable(frame, index, page)` (REL ID 2314681)

`[src: lx/include/RE/B/BSScript_Internal_Stack.h:59-71; lx/include/RE/IDs.h:731-735]`. F4SE's `VMArgList::GetOffset` and `Get` are the same two functions `[src: f4se/f4se/PapyrusArgs.h:17-34]`.

**SP's Skyrim approach relies on things that do not exist in FO4**
- SP patches CommonLibSSE to expose `Variable args[0]` at `StackFrame+0x40` `[src: overlay_ports/commonlibsse-ng-flatrim/patches/03-stackframe-uncomment_top_args.patch]`.
- It writes `top->args[i]` directly `[src: skyrim-platform/.../CallNative.cpp:429-436]`.

**Port of the stack-hijack trick to FO4**
1. Keep the tick trick: a native `TESModPlatform.Add` with 12 int parameters (`g_maxArgs = 12`), so its frame reserves 12 slots `[src: skyrim-platform/.../PapyrusTESModPlatform.cpp:103-121; CallNative.h:5]`. In lx, bind it with the LONG signature `(IVirtualMachine&, u32 stackID, std::monostate, int×12)`.
2. Find the stack in `allRunningStacks` by stack ID.
3. Set `top->owningFunction`, `top->owningObjectType`, `top->self` and `top->size = n`.
4. Write each argument via `GetStackFrameVariable(top, i, page)`.
5. Call `f->Call(stack, *errorLogger, *vm, false)` (vtable 0x0F).
6. Read `stack->returnValue` at +0x70.

Latent natives must instead go through `DispatchStaticCall` / `DispatchMethodCall` with a callback, as today `[src: CallNative.cpp:438-468]`.

### 5.9 `IStackCallbackFunctor` (`BSScript_IStackCallbackFunctor.h:18-26`)

| Idx | FO4 function |
|---|---|
| 00 | dtor |
| 01 | `CallQueued()` |
| 02 | `CallCanceled()` |
| 03 | `StartMultiDispatch()` |
| 04 | `EndMultiDispatch()` |
| **05** | **`operator()(Variable)`** |
| 06 | `CanSave()` |

Skyrim's layout, which SP's `VmCallback` overrides, is `operator()(Variable)`, `CanSave()`, `SetObject(...)` `[src: skyrim-platform/.../VmCallback.h:3-33; PapyrusTESModPlatform.cpp:66-75]`. Rewrite both callback classes.

### 5.10 Handle policy (`BSScript_IObjectHandlePolicy.h:15-32`)

| Idx | Function | Idx | Function |
|---|---|---|---|
| 01 | `HandleIsType(u32 type, size_t handle)` | 0C | `GetObjectForHandle(type, handle)` |
| 02 | `GetHandleType` | 0D | `PersistHandle` |
| 03 | `IsHandleLoaded` | 0E | `ReleaseHandle` |
| 04 | `IsHandleObjectAvailable` | 0F | `ConvertHandleToString` |
| 06 | `EmptyHandle` | | |
| 07 | `GetHandleForObject(u32 type, const void*)` | | |

F4SE names: 01 IsType, 07 Create, 0C Resolve `[src: f4se/f4se/PapyrusInterfaces.h:148-169]`.

**Packing a form into a Variable**
1. `vm->GetScriptObjectType(typeID, typeInfo)`.
2. `h = policy.GetHandleForObject(typeID, ptr)`.
3. `vm->FindBoundObject(h, typeInfo->name, false, obj, false)`. If none is found, `CreateObject` + `ObjectBindPolicy::BindObject(obj, h)`.
4. `var = obj`.

`[src: lx/include/RE/B/BSScriptUtil.h:547-596]`. This replaces SP's Skyrim `PackHandle`. SP's `GetSingleObjectPtr` loop over `HandleIsType` / `Resolve` ports directly.

- FO4 also has inventory-item script objects: `GameScript::RefrOrInventoryObj` in lx, `VMRefOrInventoryObj` in F4SE. Calls on ObjectReference natives may receive a container plus unique ID instead of a ref.

### 5.11 F4SE native registration (for reference)

- `F4SEPapyrusInterface::Register(bool(*)(VirtualMachine*))` `[src: f4se/f4se/PluginAPI.h:184-196]`. Inside the callback:
  - `vm->RegisterFunction(new NativeFunctionN<Base, Ret, Args…>("Fn", "Class", cb, vm))`
  - optionally `vm->SetFunctionFlags("Class", "Fn", IFunction::kFunctionFlag_NoWait)`, which makes the function callable from tasklets `[src: f4se/f4se/PapyrusUtility.cpp:23-33]`.
- Argument types:
  - `StaticFunctionTag*` for globals; `VMObject*` (a ScriptObject) for ScriptObject methods.
  - `VMArray<T>`, `VMVariable` (Var), `VMStruct<name>` (needs `GetStructTypeInfo` + member lookup by name), `BSFixedString`.
  - `[src: f4se/f4se/PapyrusArgs.h, PapyrusStruct.h:1-80]`
- In lx: `vm->BindNativeMethod("Class", "Fn", fn, taskletCallable, isLatent)`. `fn` is `R(S self, Args…)` or `R(IVirtualMachine&, u32, S, Args…)`. Structs use `structure_wrapper<"Script", "Struct">` `[src: lx/include/RE/B/BSScriptUtil.h:55-124,1376-1396]`.

### 5.12 Summary of SP changes

| SP fact (Skyrim) | FO4 |
|---|---|
| Hook vtable 0x18 `BindNativeMethod` | **0x1B** |
| `isLatent` @0x42, callback @0x50 | same; but lx natives keep a `std::function` at 0x50, and F4SE has no long-signature byte at 0x58 |
| `StackFrame::args[]` @0x40 (patched) | **none**: use `GetStackFrameVariable` |
| `allRunningStacks`, `objectTypeMap`, `attachedScripts` | same names (offsets in §5.3) plus `structTypeMap` |
| `IFunctionArguments*` dispatch args | `std::function<bool(BSScrapArray<Variable>&)>` |
| `IStackCallbackFunctor` 3 virtuals | 6 virtuals (operator() is #05) |
| RawType 0–5, 10–15 | adds 6 Var, 7 Struct, 16 Var[], 17 Struct[]; arrays of complex types use pointer bit 0 |
| `GetScriptObjectType1` (Skyrim by-name overload) | `GetScriptObjectType(const BSFixedString&,…)` @0x08 |
| `DumpFunctions` uses papyrus-vm `Reader` | needs the FO4 Reader (§6.1) |

---

## 6. papyrus-vm implementation plan for FO4

Guiding rule: **one library, two games**. Parse both formats. The executor treats opcodes 0x24–0x2E as FO4-only. Game-specific runtime behaviour sits behind a `GameProfile` or flags on `VirtualMachine`.

### 6.1 `Reader.h` / `Reader.cpp`

- Add `enum class Endian { Big, Little } endian;` and make `Read16/32/64` endian-aware. Add `ReadFloat` (bit-cast from u32) and `ReadI32`. Replace the byte-by-byte `ifstream::get` loop with a bulk read.
- `FillHeader`: detect the magic as LE first (§1.1). Validate major, minor and gameID (§1.2). Throw `std::runtime_error` with the path on mismatch.
- `FillSource`: store the raw string in a new `PexScript::sourceFileName`. **Set `PexScript::source` from `objectTable[0].NameIndex` after parsing.** This gives the object name, e.g. `PexVm:Sender`, because FO4 headers contain absolute paths `[caprica-run]`. Skyrim is unaffected: the name equals the file stem `[ck-pex]`. `GetSourcePexName()` is used for script identity in `IGameObject::HasScript`, `MpForm`, CallParent and casts `[src: papyrus-vm/src/papyrus-vm-lib/IGameObject.cpp:7; ActivePexInstance.cpp:141-149,313-316,1076; falloutmp-server/cpp/server_guest_lib/MpForm.cpp:65-68]`.
- `FillDebugInfo`: honour the `hasDebugInfo == 0` early exit. For LE files read the property groups and struct orders into new `DebugInfo::propertyGroups` / `structOrders`.
- `FillObject`: after the docstring, `if (fo4) object.isConst = Read8_bit()`. After `autoStateName`, `if (fo4) FillStructTable(object.structs)`. Reject gameID 4 (guards).
- `FillVariable`: after the value, `if (fo4) var.isConst = Read8_bit()`.
- `FillVariableData`: accept only type bytes 0–5. Read floats with the file endianness. Throw on anything else and **remove** the 11–15 branch.
- `FillFuncInfo`: put locals into `info.locals`. Fix `MakeLocals` at the same time (§6.4).
- `numArgumentsForOpcodes`: extend to 47 entries: `…, 4, 4, /*0x24*/3, 1, 3, 3, 5, 5, 3, 3, 1, 3, 1`. Bounds-check the opcode; throw "Invalid opcode 0xNN" for anything > 0x2E, and for anything > 0x23 when the file is BE (Skyrim). Varargs: require the count value to be type 3.
- Add `bool PexScript::IsFallout4() const` (gameID == 2).

### 6.2 `ScriptHeader.h`, `PexScript.h`, `Object.h`, `DebugInfo.h`

- `ScriptHeader`: `kSignature` plus game constants `kGameSkyrim = 1`, `kGameFallout4 = 2`, and an `Endian` field. Drop the single `kVerMinor`.
- `Object`:
  - add `bool isConst`
  - add `std::vector<StructInfo> structs` with `StructInfo { std::string name; std::vector<Member{ name, typeName; uint32 userFlags; VarValue defaultValue; bool isConst; std::string docstring }> members; }`
  - add `VarInfo::isConst`
- `DebugInfo`: add `PropertyGroup` and `StructOrder` (§1.4).
- `FunctionCode.h`: delete the stale `kOp_Invalid` (= 0x22, which collides with `array_findelement`). Keep one opcode enum.

### 6.3 `VarValue.h` / `VarValue.cpp`

- Extend `Type`:
  - `kType_Var = 6` — payload `std::shared_ptr<const VarValue> boxed`; nullptr = None. Treat the box as immutable, which gives the game's deep-copy semantics.
  - `kType_Struct = 7` — payload `std::shared_ptr<StructValue>`; nullptr = None.
  - `kType_VarArray = 16`, `kType_StructArray = 17`.
  - `_ArraysEnd = 18`.
- `StructValue { std::shared_ptr<const StructInfo> type; CIString typeName; std::vector<VarValue> values; }`. Look members up case-insensitively.
- `operator=`: copy `pArray` for every array type in [11, 18). Copy `structPtr` and `boxed`. **Keep** the rule that `objectType` (the declared type) is not copied `[src: papyrus-vm/src/papyrus-vm-lib/VarValue.cpp:574-607]`. STRUCT_CREATE, ARRAY_CREATE and `cast` all depend on D's `objectType`.
- Implement for Var and Struct: `CastToBool`, `CastToInt`, `CastToFloat`, `CastToString` (§3.4); `operator==` (§3.5); `operator<<`; `GetElementsArrayAtString`.
- Fix string equality to be case-insensitive and float equality to use an epsilon. This is a behaviour change for Skyrim too, so gate it behind a flag if Skyrim tests need it.
- Object identity for FO4: add `ActivePexInstance* boundScript` (non-owning) to object VarValues. That lets `self`, `CastAs` and casts to non-native script types refer to a **specific script instance**, which per-script natives need (§6.5).
  - Set it on each instance's `activeInstanceOwner` in `VirtualMachine::AddObject`.
  - `TryCastMultipleInheritance` already returns `script->activeInstanceOwner`, so the binding propagates through casts for free `[src: papyrus-vm/src/papyrus-vm-lib/ActivePexInstance.cpp:1062-1083]`.
  - Equality still compares the game object only.

### 6.4 `OpcodesImplementation.h/.cpp`, `ActivePexInstance.h/.cpp`

- `Opcodes` enum: add `op_Is = 0x24`, `op_StructCreate`, `op_StructGet`, `op_StructSet`, `op_ArrayFindStruct`, `op_ArrayRFindStruct`, `op_ArrayAdd`, `op_ArrayInsert`, `op_ArrayRemoveLast`, `op_ArrayRemove`, `op_ArrayClear = 0x2E`.
- Implement the semantics of §2.2 in `OpcodesImplementation.cpp`, as free functions with unit tests. Enforce the 128-element cap on create, add and insert.
- `TransformInstructions`: replace `dereferenceStart` with a per-opcode "name operand" mask (§2.2 notes).
- `ExecuteOpCode`:
  - `op_Cast`: dispatch on **D's declared type string**, not `GetType()`, so it can tell `Var`, `script#Struct` and `X[]` apart. Implement an array cast that copies the array (§3.4).
  - `op_CallMethod`: drop the special-casing of `onBeginState`/`onEndState` for FO4 scripts, since FO4 has real `OnBeginState(string)`/`OnEndState(string)`.
  - `op_PropGet`/`op_PropSet`: pass **no** call arguments to the getter and only the value to the setter. The current code passes `argsForCall`, which is flagged as wrong in comments at 441-446 and 504-547.
  - `op_PropGet`/`op_PropSet`: search the property through the inheritance chain.
  - The `op_PropGet` `GetProperty` helper never searches write properties because of a nested `if` `[src: papyrus-vm/src/papyrus-vm-lib/ActivePexInstance.cpp:107-139]`. Fix that.
- `GetTypeByName`: lowercase the input, then check in order:
  1. `var` → Var
  2. `var[]` → VarArray
  3. contains `#` and ends with `[]` → StructArray
  4. contains `#` → Struct
  5. otherwise the existing rules (`int[]` etc., then any `[]` → ObjectArray)
- `GetFunctionByName(name, state)`: implement the four-step chain lookup of §4.2 across `parentInstance`, with case-insensitive state comparison.
- `MakeLocals`: initialise `function.locals` with their declared types (set `objectType`), then params.
- `ExecuteAll`: keep the latent-resume logic. The result index for `callmethod`/`callstatic` is 2 and for `callparent` it is 1.

### 6.5 `VirtualMachine.h` / `VirtualMachine.cpp`

- **Script registry:** key `allLoadedScripts` by `PexScript::source` (now the object name), case-insensitively.
- **Struct types:** add `std::shared_ptr<const StructInfo> GetStructType(const CIString& "script#Struct")`. It splits at `#`, loads the script lazily (through the missing-script handler) and finds the struct.
- **Root class:** FO4 scripts have `parentClassName = "ScriptObject"`, and `CreateActivePexInstance` throws if `ScriptObject.pex` is missing `[src: papyrus-vm/src/papyrus-vm-lib/VirtualMachine.cpp:424-445]`. Ship a compiled `ScriptObject.pex` in the FO4 standard scripts, or treat `ScriptObject` as a built-in empty native parent. In that case implement `GetState`/`GotoState` natively with FO4 argument semantics.
- **Native calling context:** add
  ```cpp
  using NativeFunctionEx = std::function<VarValue(NativeCallContext&, VarValue self, std::vector<VarValue>& args)>;
  struct NativeCallContext { VirtualMachine& vm; ActivePexInstance* selfScript; std::shared_ptr<StackData> stack; };
  ```
  `selfScript` comes from `self.boundScript`, or from the caller instance when the target is `self`. ScriptObject natives use the key `(IGameObject*, selfScript->GetSourcePexName())`. Keep the old `NativeFunction` overload for Skyrim.
- **ScriptObject natives** (register under class `ScriptObject`; they are reached through the existing parent walk in `CallMethod`):
  - `CallFunction` (promise-based) and `CallFunctionNoWait` (queued). Use `CallMethod(..., activePexInstancesOverride = {selfScript})`, unbox the Var params and box the result.
  - `GetPropertyValue`, `SetPropertyValue`, `SetPropertyValueNoWait`
  - `CastAs`
  - `StartTimer`, `CancelTimer`, `StartTimerGameTime`, `CancelTimerGameTime`. These hook into the server timer service through a `ITimerHost` interface and fire `SendEvent(instance, "OnTimer", {id})`.
  - `RegisterForRemoteEvent`, `UnregisterForRemoteEvent`, `UnregisterForAllRemoteEvents`
  - `RegisterForCustomEvent`, `UnregisterForCustomEvent`, `UnregisterForAllCustomEvents`, `SendCustomEvent`
  - `UnregisterForAllEvents`
  - The hit, inventory-filter, distance and LOS registrations belong to the server, because they need world data. Keep them in registration tables exposed by the VM.
  - `Utility.CallGlobalFunction` / `CallGlobalFunctionNoWait`.
- **Event relays:** add
  ```cpp
  std::unordered_map<IGameObject*, CIMap<std::vector<Registrant>>> relays;  // Registrant { std::weak_ptr<ActivePexInstance>; }
  ```
  - `SendEvent(object, E, args)` dispatches locally first, then for each registrant calls `"::remote_" + T + "_" + E` with `(senderVarValue, args…)`. T is computed as the least-derived script declaring E (§3.7). Cache it per (native type, E).
  - `SendCustomEvent(name, args)` calls `"::remote_" + name` with `(sender, args)`.
- **Job queue:** add `void Enqueue(std::function<void()>)` and `void Tick()` for the NoWait and custom-event paths (§4.3).
- `SendEvent(instance, …)`, used for timers, must not fan out to other scripts.

### 6.6 Server glue (outside papyrus-vm, but needed)

- **`falloutmp-server/.../script_storages/DirectoryScriptStorage.cpp` and `ScriptStorageUtils.cpp`:**
  - Use `recursive_directory_iterator`.
  - The script name is the path relative to `scripts/` with separators turned into `:` and the `.pex` extension dropped (`MyMod/Quests/Q.pex` → `MyMod:Quests:Q`).
  - `GetScriptPex("A:B")` opens `A/B.pex`, replacing `:` with the OS separator. Match case-insensitively; on Linux resolve case by scanning the directory.
  - Today the listing is flat and keyed by file stem `[src: falloutmp-server/cpp/server_guest_lib/script_storages/ScriptStorageUtils.cpp:31-48; DirectoryScriptStorage.cpp:15-43]`.
- **`BsaArchiveScriptStorage`:** FO4 uses BA2 (`BTDX`, GNRL). Strip `scripts\` and apply the same namespace mapping.
- **`AssetsScriptStorage` / `standard_scripts`:** an FO4 set compiled with the FO4 compiler — `ScriptObject`, `Form`, `ObjectReference`, `Actor`, `Quest`, `Utility`, `Debug`, … taken from `f4se/scripts/vanilla`.
- **`ScriptVariablesHolder`:**
  - `FillNormalVariables` over every PEX in the inheritance chain. Today it only covers the first PEX, which breaks the CK's parent `::X_var` reads `[src: falloutmp-server/cpp/server_guest_lib/ScriptVariablesHolder.cpp:105-158]`.
  - Set `objectType` on script variables.
  - Fall back to the parent's auto state when the child has none.
  - Add VMAD types 6, 7, 16 and 17 and struct initialisation (§4.5).
- **`WorldState::SendPapyrusEvent`:** add the FO4 registration gates and the related-object fan-out (§4.4).

### 6.7 `CIString` / `CIMap`

- `CIHash` lowercases correctly, but it copies into a `std::string` on every hash. Fine for now.
- Make sure every name lookup uses the CI containers: script names with `:`, struct names, state names, member names. Today `GetFunctionByName` compares state names with `==` `[src: papyrus-vm/src/papyrus-vm-lib/ActivePexInstance.cpp:64]`.

### 6.8 Unit tests: fixture-free

These run on Linux without a compiler. Put them in `unit/PexReaderFo4Test.cpp` with tag `[PexFo4]`.

1. **Byte-built PEX.** Add a tiny test-only `PexBuilder` (≈150 lines) that emits LE or BE PEX from C++ descriptions. Round-trip:
   - header validation (wrong magic, wrong gameID, FO4 opcode in a BE file → throws)
   - `hasDebugInfo = 0` file parses
   - struct table, const bytes, property groups and struct orders are read
   - an unknown opcode throws
2. **Golden files.** Commit `out2/PexVmOpsTest.pex` and `out2/PexVm/Sender.pex` together with the `.psc` sources under `unit/papyrus_test_files_fo4/`. Assert the parsed structure matches the dump in §1.12, e.g.:
   - `Sender` object name `PexVm:Sender`, `isConst = 0`
   - struct `Point` members `X/Y/Tag`, with `Y.default == None`
   - variable `::Speed_var.isConst == 1`
   - 2 property groups
   - instruction 14 of `StructOps` is 0x28 with arg2 `Str "Key"`
   - function `::remote_PexVm:Sender_Ping` exists
3. **Opcode unit tests** (no PEX needed): `OpcodesImplementation::ArrayAdd/Insert/Remove/RemoveLast/Clear/FindStruct/RFindStruct/Is/StructGet/StructSet` on hand-made `VarValue`s, including the 128 cap, None arrays and negative start indices.

### 6.9 Acceptance tests: compiled Papyrus

Add `cmake/add_papyrus_library_fo4.cmake`. It runs:
- the FO4 CK `PapyrusCompiler.exe <dir> -i=<vanilla>;<dir> -o=<out> -f=Institute_Papyrus_Flags.flg -all` `[web: falloutck Papyrus Compiler Reference]`, or
- Caprica `--game fallout4` (§1.12).

Commit the outputs, as is done for Skyrim today, so CI needs no compiler `[src: cmake/add_papyrus_library_ck.cmake; unit/CMakeLists.txt:10-15]`.

Test scripts (start from `pexvm/fo4src/`) and expected behaviour, in `unit/VirtualMachineFo4Test.cpp` with tag `[VirtualMachineFo4]`:

| Script | Exercises | Expected |
|---|---|---|
| `Fo4OpsTest.psc` (port of `OpcodesTest.psc`) | All 0x00–0x23 ops under LE | Same assertions as the Skyrim test |
| `Fo4StructTest.psc` | `new`, defaults (incl. None→type default), `struct_get/set`, structs passed by reference, struct arrays, FindStruct/RFindStruct with start index, Var holding a struct, `as` back from Var | `Assert` natives pass |
| `Fo4ArrayTest.psc` | Add with count 1/3, Insert at 0 and at end, Remove(i, n), RemoveLast, Clear, sharing an array between two variables, 128 cap, `Form[] as ObjectReference[]` copy semantics | Lengths and contents as in falloutck "Arrays (Papyrus)" examples |
| `Fo4VarTest.psc` | Boxing int/float/string/bool/object/struct; `is` strict and loose; `as`; Var == Var; Var[] | |
| `Fo4StateTest.psc` | Inherited `GotoState`/`GetState` from ScriptObject.pex; `OnBeginState(asOldState)`/`OnEndState(asNewState)` argument values; case-insensitive state names; parent-state lookup order | |
| `Fo4InheritTest.psc` + `Fo4InheritParent.psc` | Child calls a parent function **without** attaching the parent separately; `Parent.X()`; reading a parent auto property (`::X_var`) | |
| `PexVm/Sender.psc` + `Fo4Receiver.psc` | `RegisterForCustomEvent` / `SendCustomEvent` (dispatched on `VirtualMachine::Tick()`); `RegisterForRemoteEvent(obj, "OnActivate")`, then `vm.SendEvent(obj, "OnActivate", …)` reaches `::remote_ObjectReference_OnActivate` on the receiver only; Unregister stops it | |
| `Fo4TimerTest.psc` | `StartTimer` with a fake `ITimerHost`; `OnTimer(id)` only on the starting instance; restart resets; `CancelTimer` | |
| `Fo4DynamicTest.psc` | `CallFunction` (latent, through promise) and `CallFunctionNoWait`; `Get`/`SetPropertyValue`; `CastAs` between two scripts on one object; `Utility.CallGlobalFunction` | |
| `Fo4Namespaced/Deep:Script.psc` | `DirectoryScriptStorage` resolves `Deep:Script` → `Deep/Script.pex`; `callstatic deep:script` | |

Also keep **all existing Skyrim tests green**: `./unit/unit [VirtualMachine]`, `[VarValue]`, and the Papyrus* tests.

### 6.10 Can Skyrim and FO4 coexist in one VM build?

- **Yes, at the library level.**
  - The format is self-identifying per file: endianness plus `gameID`.
  - Opcodes 0x00–0x23 have identical encodings and semantics, so a single executor works.
  - The extra FO4 PEX sections are additive.
  - Store `game` on each `PexScript` and reject FO4-only opcodes in Skyrim files.
- **No, within a single running VM** `[inference]`.
  - Runtime semantics diverge:
    - FO4 has a ScriptObject root; Skyrim has compiler-generated GetState/GotoState in every script.
    - OnBegin/EndState arguments differ.
    - Event registration is per script in FO4 and per form in Skyrim.
    - Timers replace OnUpdate.
    - Native signatures differ, e.g. `Actor.GetValue(ActorValue)` vs `GetActorValue(string)`.
  - Configure each `VirtualMachine` with a `Game` (from the server `GameProfile`). Reject scripts whose `gameID` differs, as the real game does.
  - SkyMP upstream merges keep working because the Skyrim paths remain the default.

---

## Appendix A. Quick checklist of current papyrus-vm defects that FO4 will hit

1. Big-endian-only reads and no magic, version or gameID validation `[Reader.cpp:70-77,420-452]`.
2. The debug-info flag is ignored `[Reader.cpp:116-127]`.
3. Value types 11–15 are accepted `[Reader.cpp:256-270]`.
4. Locals are stored as params `[Reader.cpp:298-306]`.
5. The opcode table has 36 entries and the index is unchecked `[Reader.h:12-15; Reader.cpp:347-358]`.
6. The script name comes from the header source path `[Reader.cpp:79-87]`.
7. No inheritance-chain function lookup, and case-sensitive state comparison `[ActivePexInstance.cpp:57-77]`.
8. Single-index identifier dereferencing would corrupt struct member and `is` type operands `[ActivePexInstance.cpp:723-752]`.
9. Cast to an array aliases instead of copying `[ActivePexInstance.cpp:265-269]`.
10. String and float equality are not case-insensitive / epsilon `[VarValue.cpp:436-458]`.
11. `GetProperty` cannot find write properties `[ActivePexInstance.cpp:114-136]`.
12. The variable holder fills only the first PEX's variables `[ScriptVariablesHolder.cpp:105-158]`.
13. Flat, stem-keyed directory listing `[ScriptStorageUtils.cpp:31-48]`.
14. No native calling context for per-script natives `[VirtualMachine.h:9-10]`.

## Appendix B. External sources

- FO4 CK wiki wikitext (falloutck.uesp.net) for these pages: Struct Reference, Structs (Papyrus), Array Reference, Arrays (Papyrus), Add/Insert/Remove/RemoveLast/Clear/Find/FindStruct/RFind/RFindStruct - Array, Cast Reference, Operator Reference, Variable Reference, Property Reference, Function Reference, Events Reference, Script File Structure, Identifier Reference, Group Reference, Default Value Reference, Threading Notes (Papyrus), Remote Papyrus Event Registration, Custom Papyrus Events, Differences from Skyrim to Fallout 4, Papyrus Runtime Errors, Papyrus Compiler Reference, States (Papyrus), State Reference, ScriptObject Script and its member pages, ReferenceAlias/RefCollectionAlias/ActiveMagicEffect Script, Category:Latent Functions. Local copies are in `scratchpad/pexvm/ck/`.
- UESP, *Skyrim Mod:Compiled Script File Format* (wikitext via `en.uesp.net/w/api.php`). Local copy: `scratchpad/pexvm/uesp_pex.txt`.
- TES5Edit `Core/wbDefinitionsFO4.pas` (VMAD), branch `dev-4.1.6`.
- Nexus forum thread 8272748 ("Champollion … interesting tidbit"), seen only through a search summary (Cloudflare-blocked): decompiled output shows `SendCustomEvent("<script>_<event>")` and lost `CustomEvent` declarations.
