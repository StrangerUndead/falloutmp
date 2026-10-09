# fos — Fallout 4 save files for FalloutMP's entrance

The save editor behind F33: it reads a `.fos`, writes it back byte for byte, and patches the player's position and the time into a copy of the entry template, checking the result before anything is written to disk.

- Design and rules: [docs/falloutmp/features/F33-save-editor.md](../../docs/falloutmp/features/F33-save-editor.md)
- File format and measurements: [docs/falloutmp/reference/fo4-save-entry.md](../../docs/falloutmp/reference/fo4-save-entry.md)

## Use from code
```cpp
#include "Fos.h"
using namespace fmp::fos;

EntryPatch patch;
patch.placement = Placement{ kCommonwealth, { -79800.f, 90500.f, 7800.f }, 3.14159f };
patch.gameHour = 13.5f;
patch.gameDaysPassed = 12.25f;
Bytes save = WriteEntrySave(templateBytes, patch); // throws fos::Error
```

`WriteEntrySave` parses the template, checks that it can be an entry template, patches a copy, writes it, parses the result again and verifies that nothing else changed. Every failure is a `fos::Error` with an `ErrorCode`; F33 §4.10 says what the client does for each one.

Other entry points:
- `Parse` / `Serialize`;
- the views (`PlayerLocation`, `GetGlobal` / `SetGlobal`, `ReadSkyMode`, `FindPlayerRecord`, `ReadInitialPrefix`);
- `ValidateTemplate` with `FalloutMpTemplateRules()`, and `Diff`;
- `Sha256Hex`;
- `ParseTemplates` / `SelectTemplate` for `templates.json`.

No game headers and no Windows APIs: the same sources build for the Linux tests, `fmp_savetool` and the F4SE plugin. Only zlib and nlohmann-json are needed.

## fmp_savetool
Built with the repository (`build/fallout4-platform/fmp_savetool`). The Windows CI also uploads `fmp_savetool.exe` as an artifact.

```
fmp_savetool inspect save.fos              # what's in it
fmp_savetool validate save.fos             # can it be FalloutMP's entry template?
fmp_savetool roundtrip save.fos            # does it read and write back unchanged?
fmp_savetool patch template.fos out.fos --pos -79800 90500 7800 --yaw 180 --hour 13.5
fmp_savetool patch template.fos out.fos --ticket entryTicket.json
fmp_savetool diff a.fos b.fos
fmp_savetool normalize in.fos out.fos --name FalloutMP --black
fmp_savetool manifest-entry template.fos --id fo4-1.11.240-r1 --notes "Vault 111 exterior"
```

Exit codes: 0 ok, 1 failed check or error, 2 bad usage.

## Tests
```
./unit/unit "[Fos],[FosPatch],[FosFuzz]"                 # synthetic saves, always run
FMP_FOS_SAMPLES=/path/to/saves ./unit/unit "[FosReal]"   # real saves (never commit them)
FMP_FOS_FUZZ_ITERATIONS=200000 ./unit/unit "[FosFuzz]"   # a longer fuzz run
```

**Sanitizer build** (what was run before the first commit), from the repository root after a normal build:
```
V=build/vcpkg_installed/x64-linux
printf '#include <catch2/catch_session.hpp>\nint main(int c, char** v) { return Catch::Session().run(c, v); }\n' > /tmp/fos_main.cpp
g++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
  -DWITH_FMP_FOS=1 -Ifallout4-platform/fos -I$V/include \
  fallout4-platform/fos/*.cpp unit/FosTest.cpp /tmp/fos_main.cpp $V/lib/libCatch2.a $V/lib/libz.a -o /tmp/fos_asan
FMP_FOS_FUZZ_ITERATIONS=200000 /tmp/fos_asan "[FosFuzz]"
```
