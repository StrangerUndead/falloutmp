# Contributing

Contributions are welcome. We ask for three things: test what you change, keep the code style, and keep the documentation up to date. Contributions are released under the license of the subproject they change.

Start with [docs/falloutmp/README.md](docs/falloutmp/README.md) and [docs/falloutmp/STATUS.md](docs/falloutmp/STATUS.md): they hold the plan, the feature specs and the current state.

## Building

Build steps for Linux and Windows, the requirements and the test commands are in the [README](README.md#4-building). In short, on Linux:

```sh
git submodule update --init --recursive
./build.sh --configure -DCMAKE_BUILD_TYPE=RelWithDebInfo
./build.sh --build --parallel $(nproc)
cd build && ./unit/unit
```

The tests need no game data. Never commit Bethesda game files.

Test coverage (Windows only): install [OpenCppCoverage](https://github.com/OpenCppCoverage/OpenCppCoverage/releases), configure with `-DCPPCOV_PATH="C:\Program Files\OpenCppCoverage"` and run `ctest -C Debug --verbose`. The report goes to `build/__coverage`.

## Pull requests

- **Your branch must build** and the tests must pass (the CI workflows *FalloutMP server (Linux)* and *FalloutMP client (Windows)* run them).
- **One pull request per feature.** Use a feature branch, not your main branch.
- **Add tests with every change.** C++ tests go in `unit/`, client tests in `falloutmp-client/test/`.
- **New network messages** follow the checklist in [guides/implementation.md](docs/falloutmp/guides/implementation.md) §6; the parity test enforces it.
- **Format C++ code** with the rules in `.clang-format`.
- **Format TypeScript** like the surrounding code. Strings use double quotes.
- **Spaces, not tabs**, in every language.
- **Remove code instead of commenting it out.** Git keeps the history.
- **Keep Git history clean.** Push only necessary changes; move files only when needed.
- **Resolve merge conflicts** with the main branch before review.
- **No offensive language** in code or comments.
