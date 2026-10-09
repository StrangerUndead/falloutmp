# F33 component — The save editor (`fos` library and `fmp_savetool`)

| Field | Value |
|---|---|
| Part of | [F33 — The entrance](F33-entrance.md) (tasks F33-T01, F33-T02, F33-T03) |
| Code | `fallout4-platform/fos/` (library), `fallout4-platform/tools/fmp_savetool.cpp` (command line), `unit/Fos*Test.cpp` (tests) |
| Format reference | [reference/fo4-save-entry.md](../reference/fo4-save-entry.md) §1–§3 (cited as "ref §n") |
| Status | built and tested 2026-10-09 (§11); used by the plugin from F33-T09 |

## 1. What it is for
Every player who joins a FalloutMP server goes through this code once per join. On the main menu, the client asks it to:
1. take the shipped entry template;
2. write the server's position, worldspace and time into a copy;
3. prove that the copy is correct.

It then loads the copy. A wrong byte here means a crash, a broken world, or a player in the wrong place, for every player. So the design favours correctness and refusal over cleverness.

**Users of the code:**

| User | Where | Uses |
|---|---|---|
| The F4SE plugin (F33-T09) | every client, every join | `WriteEntrySave`, `Sha256`, `SelectTemplate` |
| The maintainer | offline, when making a template | `fmp_savetool inspect / validate / normalize / manifest-entry` |
| CI | every push | unit tests; `fmp_savetool validate` and a round trip on the release template (F33-T21) |
| Developers and bug reports | offline | `fmp_savetool diff / patch / roundtrip` on saves kept with `"entry": {"keepSave": true}` |

## 2. Requirements
| # | Requirement | How it is met |
|---|---|---|
| R1 | **Byte-exact.** Reading a save and writing it back unchanged gives the identical file. | The model keeps every byte it does not interpret (§4). Tests run on synthetic files and on every real save in `FMP_FOS_SAMPLES` (§8). |
| R2 | **Minimal edits.** A patch changes only the Player Location block, two time globals, the player's `MOVE` data and the offsets that follow from them. | The patcher edits those fields in place; everything else is copied (§5). |
| R3 | **Self-verifying.** No patched file leaves the library unchecked. | After writing, the output is parsed again and compared with the template, field by field (§6). Any difference outside the expected set is an error. |
| R4 | **Refuse what it doesn't understand.** It never writes a guess. | Unknown versions and layouts, inconsistent offsets, and unexpected flags in the player record all raise an error (§7). The client then falls back (F33 §4.10). |
| R5 | **Safe on hostile or damaged input.** A broken file never crashes the game. | Every read is bounds-checked; sizes are capped (§7.2); inflate is bounded; fuzz tests run (§8). |
| R6 | **Portable and self-contained.** One code base for Linux tests, the offline tool and the Windows plugin. | C++20 and the standard library, plus zlib; no game headers, no Windows APIs. |
| R7 | **Fast.** Writing an entry save ≤ 250 ms (F33 E5). | One pass over ~5 MB, one small inflate and deflate; measured in the real-save tests (§8). |
| R8 | **Pure and thread-safe.** The plugin runs it on a worker thread. | No global state; inputs in, bytes out. |

## 3. The file format it covers
All little-endian; `wstr` = `u16` length + bytes (kept as raw bytes, never re-encoded). The layout below was measured on five real saves from 1.1.30, 1.9.4 and 1.11.240 (ref §1.1). The sections are contiguous, the file ends right after unknown table 3, and the unused FLT slots are zero.

```
"FO4_SAVEGAME"  u32 headerSize  header (version, saveNumber, name, level, location, playTime, race, sex, xp, nextXp, FILETIME, shotW, shotH)
screenshot      shotW × shotH × 4 bytes (RGBA)
u8 formVersion  wstr gameVersion  u32 pluginInfoSize  u8 n + n wstr  [u16 m + m wstr light plugins, when present]
FLT             25 × u32: [0] FormID array, [1] unknown table 3, [2..5] tables 1, 2, change forms, table 3, [6..8] block counts, [9] change-form count, [10..24] unused
table 1         blocks {u32 type, u32 length, data}: types 0–11 (1 Player Location, 3 Global Variables, 6 Weather, 7 Audio)
table 2         blocks, types 100 and up
change forms    {refId 3 B big-endian, u32 flags, u8 type (low 6 bits) | width (top 2 bits), u8 version, length1, length2, data[length1]}
table 3         blocks, types 1000 and up (Papyrus = 1001)
FormID array    u32 n + n × u32
visited WRLD    u32 n + n × u32
unknown table 3 u32 size + size bytes
```

**Interpreted, read-only unless stated:**

| Field | Layout | Used for |
|---|---|---|
| Player Location (table 1 type 1, **30 bytes**) | `u32 nextObjectId`, `refId worldspace`, `s32 gridX`, `s32 gridY`, `refId worldOrCell`, `f32 x, y, z` | **patched**: worldspace, grid, worldOrCell, position; `nextObjectId` kept |
| Global Variables (table 1 type 3) | `vsval n` + n × `{refId, f32}`; the length must equal the vsval size + 7n | **patched**: GameHour `0x38` and GameDaysPassed `0x39`, in place; read: TimeScale `0x3A`, the date `0x35`–`0x37` |
| Weather (table 1 type 6) | sky mode `u32` at offset 58 (3 = outdoors, 1 = indoors) | template validation |
| Player ACHR (change form refId `40 00 14`, type 1) | body (zlib if `length2 > 0`) starts with initial data: `refId cell/worldspace`, `f32 pos[3]`, `f32 rot[3]` (27 bytes); type 4 when `MOVE`/`HAVOK_MOVE` is set, type 6 (34 bytes) when `CELL_CHANGED`/`PROMOTED` is set | **patched**: the first 27 bytes |
| RefID | 24 bits; top 2 bits = kind: 0 → `FormID[v−1]`, 1 → `Fallout4.esm` form `v`, 2 → created `0xFF000000 \| v`, 3 → invalid | encoding targets as kind 1 |
| Created references (change forms with kind-2 RefIDs, type 0) | initial data type 5: 27-byte prefix, `u8`, `refId base` | template validation (no power-armor frame) |

**Not interpreted:**
- the other table 1–3 blocks (Papyrus included);
- the other change forms;
- the header's text;
- unknown table 3.

These are kept as bytes. `vsval` values are never re-encoded (sources disagree on the 3/4-byte form, ref §2.8).

## 4. Architecture

```
fos/Fos.h          public API: the model, Parse, Serialize, views, patch, validate, diff, sha256, templates
fos/Bytes.h        internal: bounds-checked reader and writer
fos/Reader.cpp     bytes → SaveFile
fos/Writer.cpp     SaveFile → bytes (recomputes headerSize, pluginInfoSize, the FLT)
fos/Views.cpp      PlayerLocation, Globals, Weather, the player ACHR, RefID helpers, inflate/deflate
fos/Patch.cpp      EntryPatch → patched SaveFile → verified bytes
fos/Validate.cpp   template rules → findings
fos/Diff.cpp       SaveFile × SaveFile → differences
fos/Sha256.cpp     SHA-256 (template integrity, no extra dependency)
fos/Templates.cpp  templates.json: parse, select by runtime
tools/fmp_savetool.cpp   the command line (§9)
```

**The model** (`fmp::fos::SaveFile`) holds every section as data. Interpreted fields are plain members; everything else is a byte vector:
- `Header` (with any unknown tail of `headerSize`);
- `screenshot`;
- `formVersion`, `gameVersion`, `plugins`, `lightPlugins` (and whether the light list exists);
- the 15 unused FLT words;
- `table1` / `table2` / `table3` as `{type, bytes}`;
- `changeForms` as `{refId, flags, type, version, width, length2, stored bytes}`;
- `formIds`, `visitedWorldspaces`, `unknownTable3`.

**The writer** recomputes every derived number from the content: `headerSize`, `pluginInfoSize`, all FLT offsets and counts, and each change form's lengths. So a consistent file round-trips byte for byte, and a patched file is consistent by construction. The verify step (§6) then proves it from the outside.

**Views** read and write one interpreted structure inside the model (for example `PlayerLocation::Read(save)` / `Write(save)`). The patcher is built only from views, which keeps every byte rule in one place.

## 5. The entry patch
```cpp
struct Placement { uint32_t worldspace; float pos[3]; float yawRadians; };   // a Fallout4.esm exterior
struct EntryPatch { std::optional<Placement> placement; std::optional<float> gameHour, gameDaysPassed; };
Bytes WriteEntrySave(std::span<const uint8_t> templateBytes, const EntryPatch&);  // throws fos::Error
```

**Preconditions, checked on the template** (otherwise `Error::NotATemplate`):
- it parses;
- Player Location is 30 bytes and describes an exterior (worldspace = worldOrCell, kind 1);
- the Weather block's sky mode is 3;
- the player ACHR exists, has `MOVE` and no `HAVOK_MOVE`, and its initial data is type 4 or 6, with a prefix that agrees with Player Location (same space, position within 1 unit).

**Edits:**

| Field | New value |
|---|---|
| Player Location: worldspace, worldOrCell | kind-1 RefID of `placement.worldspace` (`< 0x400000`, else `Error::PatchRefused`) |
| Player Location: grid | `floor(x / 4096)`, `floor(y / 4096)` |
| Player Location: position | `placement.pos` |
| Player Location: `nextObjectId` | unchanged |
| Player ACHR initial data, bytes 0–26 | the same RefID, `pos`, rotation `(0, 0, yaw)` |
| Player ACHR storage | re-deflated if it was compressed (`length2` = body size), stored as is otherwise; the length width is kept when it fits, widened otherwise |
| GameHour / GameDaysPassed | the float in place, when given. GameHour must be in `[0, 24)` and GameDaysPassed `≥ 0`, finite |

Nothing else changes. Without `placement`, only the time is written; that is the interior and "other plugin" path in F33 §4.5.2.

**Inputs are validated** (`Error::PatchRefused`):
- all numbers are finite;
- `|x|, |y| ≤ 500 000`, `|z| ≤ 100 000`;
- `|yaw| ≤ 7`.

## 6. Verify after write
`WriteEntrySave` serializes the patched model, parses the bytes again, and runs `VerifyEntryPatch(template, output, patch)`. It requires:
1. the header, screenshot, plugin block, unused FLT words, every block of tables 2 and 3, the FormID array, the visited worldspaces and unknown table 3 are byte-identical;
2. in table 1, every block other than types 1 and 3 is byte-identical. In type 3, every entry other than the patched globals is identical, and the patched ones hold the new values;
3. every change form other than the player ACHR is identical (refId, flags, type, version, width, lengths, bytes);
4. the player ACHR's flags, type and version are identical, and its inflated body is identical except bytes 0–26, which hold the new values;
5. the change-form count, the table counts and the FLT offsets agree with an independent recomputation from the parsed sections;
6. Player Location reads back as the patch.

Any failure is `Error::VerifyFailed` with the first difference named. The same comparison, without expectations, is `fmp_savetool diff`.

## 7. Errors, limits, safety
### 7.1 Errors
`fos::Error` (derives from `std::runtime_error`) carries a code, the byte offset when known, and a sentence for the log.

| Code | Meaning | Client reaction (F33 §4.10) |
|---|---|---|
| `Truncated` | a read ran past the end of the file or a block | template damaged → F1 |
| `BadMagic` | not `FO4_SAVEGAME` | template damaged → F1 |
| `Unsupported` | header version not 11–15, or `formVersion` not 60–69 | no template for this runtime → F1 |
| `BadLayout` | sections don't line up with the FLT, bad counts, invalid RefID kind, inconsistent vsval | template damaged → F1 |
| `Inflate` | a change form's zlib data is bad or the wrong size | template damaged → F1 |
| `TooLarge` | a limit in §7.2 is exceeded | template damaged → F1 |
| `NotATemplate` | the file parses but fails §5's preconditions | wrong template → F1 |
| `PatchRefused` | the ticket's values are out of range or not encodable | `entryFailed{ticket}` |
| `VerifyFailed` | §6 found a difference | `entryFailed{write}` → F1 (a writer bug: logged loudly) |

### 7.2 Limits
| Item | Limit |
|---|---|
| File size | 256 MB |
| Screenshot | width and height ≤ 8192 |
| Header size | ≤ 4096 bytes |
| Plugins | ≤ 255 full, ≤ 4096 light |
| Blocks per table | ≤ 4096 |
| Change forms | ≤ 4 000 000, and never more than the remaining bytes allow |
| One inflated change form | ≤ 64 MB, and it must inflate to exactly `length2` |
| FormID array, visited worldspaces | bounded by the remaining bytes |

### 7.3 Safety
- The template is read-only input. The patch only takes numbers, so nothing from the server reaches a path or a raw byte.
- The plugin checks the template's SHA-256 against `templates.json` before using it (F33 §4.6).
- No exceptions other than `fos::Error` and `std::bad_alloc` leave the library; the fuzz tests check this.

## 8. Tests
| Suite | Tag | What it proves |
|---|---|---|
| Raw fixtures | `[Fos]` | A test-side builder writes saves byte by byte, independently of the library's writer. It covers header versions 11 and 15, with and without light plugins, compressed and stored change forms, length widths u8/u16/u32, initial data types 4 and 6, empty and non-empty FormID arrays, and several blocks per table. Parse reads every field back; Serialize reproduces the bytes exactly. |
| Views | `[Fos]` | Player Location, globals (with vsval forms 0 and 1), weather sky mode, the player ACHR prefix and RefID kinds read and write correctly. |
| Patch | `[FosPatch]` | Patched fields read back. Every other byte stays identical (verified independently of `VerifyEntryPatch`). The FLT offsets after the change forms shift by exactly the growth. Compressed and stored player records both work, and a width change u8 → u16 works. Time-only patches work. |
| Refusals | `[FosPatch]` | Each precondition and range rule of §5 raises its error code; a deliberately corrupted output is caught by verify. |
| Fuzz | `[FosFuzz]` | 5 000 mutations (truncation, bit flips, random FLT words, random lengths) of a valid save. Parse either succeeds or throws `fos::Error`, and never crashes or reads out of bounds. A local AddressSanitizer build runs the same driver. |
| SHA-256 and templates | `[Fos]` | NIST test vectors; `templates.json` parsing; runtime selection (newest ≤ runtime, same `1.11` family). |
| Real saves | `[FosReal]` | For every `.fos` in the folder named by `FMP_FOS_SAMPLES` (skipped when unset): byte-identical round trip, sane views, a patch to three Commonwealth positions plus a time change, verify, and a timing check (≤ 250 ms). Real saves are never committed. |

## 9. `fmp_savetool`
```
fmp_savetool inspect <save> [--json]          header, plugins, FLT, blocks, player location, time, weather, player record, change-form histogram
fmp_savetool validate <save> [--rules r.json]  template rules (default: the FalloutMP set below); exit 0 = ok, 1 = errors, 2 = usage
fmp_savetool roundtrip <save>                 parse + serialize; exit 0 when identical
fmp_savetool patch <in> <out> (--ticket t.json | --worldspace 0x3c --pos X Y Z --yaw DEG) [--hour H] [--days D]
fmp_savetool diff <a> <b> [--json]            section-by-section differences
fmp_savetool normalize <in> <out> [--name FalloutMP] [--level 1] [--screenshot image.ppm | --black]
fmp_savetool sha256 <file>
fmp_savetool manifest-entry <save> --id ID [--file NAME] [--notes TEXT]   a templates.json entry with size and sha256
```

**Default template rules** (`validate` without `--rules`):
- header version 15, `formVersion` 69;
- plugins exactly `[Fallout4.esm]`;
- light plugins exactly the nine free Creations (STATUS decision 2026-10-09);
- an exterior in `0x3C` with sky mode 3;
- GameHour and GameDaysPassed present;
- the player ACHR as in §5;
- every RefID of kind 0 indexes the FormID array;
- every FormID's plugin index exists in the save's lists;
- no created reference based on the power-armor frame `0x2079E` (a warning when an initial data block can't be read).

`--ticket` reads the F33 `entryTicket` JSON: `target.worldOrCell` "3c:Fallout4.esm", `target.pos`, `target.angleZ` in degrees, `world.gameHour`, `world.gameDaysPassed`.

## 10. Build order and acceptance
| Step | Task | Done when |
|---|---|---|
| 1 | Model, reader, writer, views (F33-T01) | `[Fos]` passes; every sample save round-trips byte-identically |
| 2 | Patch and verify (F33-T02) | `[FosPatch]` and `[FosFuzz]` pass; sample saves patch and verify within 250 ms |
| 3 | Validate, diff, SHA-256, templates, `fmp_savetool` (F33-T03) | the CLI works on the sample saves; `validate` accepts the user's 1.11.240 save on the plugin rules and flags what the capture command still has to do (quests, inventory) only as a note |
| 4 | Builds | Linux CI (unit tests) and the Windows plugin CI compile the library |

What this component does **not** do:
- Stopping quests, stripping the inventory and disabling actors happen in game, in the capture command (F33-T06). Writing Papyrus or quest state offline is out of scope by design.
- Interiors need the donor blocks of F33-T18.
- Faces are applied through natives (F03).

## 11. Results (2026-10-09)
| Check | Result |
|---|---|
| Unit tests `[Fos]`, `[FosPatch]`, `[FosFuzz]` | 22 test cases, 515 assertions, pass (full suite: 280 cases, 2,708 assertions) |
| Real saves `[FosReal]` | 7 files from 1.1.30, 1.9.4 and 1.11.240 (the user's own, two Nexus archives): all round-trip byte for byte; the template-shaped ones patch and verify at three positions |
| Speed | parse + serialize 6–12 ms; patch + verify of the user's 1.11.240 save 15 ms (budget 250 ms) |
| AddressSanitizer + UBSan | every save test passes; 200,000 damaged synthetic saves and 1,000 damaged copies of each real save: no crash, no out-of-bounds read, nothing but `fos::Error` escapes |
| `fmp_savetool validate` | the user's re-saved 1.11.240 save (`Save5`) passes the FalloutMP rules. The save with the texture pack is refused for its extra plugin. Indoor and old-runtime saves are refused as templates |
| `fmp_savetool diff` after a patch | exactly three differences: Player Location, the Global Variables, the player record |
| Windows | the plugin build compiles the library and `fmp_savetool.exe` (CI artifact `fmp_savetool`) |

One observation from real data: the player record's rotation keeps a small pitch (`rot.x ≈ 0.098` in the user's save). The patch writes `(0, 0, yaw)` as FO4_Wrld does; whether the game uses the stored pitch is part of the in-game checks (F33 §9).
