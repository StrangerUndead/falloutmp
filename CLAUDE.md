
# Build & Test Tips

All commands below must be run **inside the build directory**  
(e.g., `mkdir build && cd build && cmake ..`).

## Build
```bash
cmake --build .
````

This compiles the project

## Test

```bash
ctest --verbose
```

Runs all tests with detailed output.

## Test Partuicular Unit Test

This example runs tests with only [Respawn] tag. Tags you can see in test files (.cpp).
If you see more than 1 unit test failed, please select one to work on and iterate with the following command.
```bash
cd build
./unit/unit [Respawn]
```

# FalloutMP plan

This is FalloutMP, a Fallout 4 multiplayer framework (forked from SkyMP; the Skyrim parts were removed, see STATUS.md decisions log).
The full project plan lives in `docs/falloutmp/` — **start every session with `docs/falloutmp/README.md` and `docs/falloutmp/STATUS.md`**.
Run unit tests with `./unit/unit` (no game data needed: tests use synthetic plugins; future real-data tests are tagged `[fo4data]`).
