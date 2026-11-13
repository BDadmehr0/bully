# Building

## Dependencies (Arch Linux)

```sh
sudo pacman -S --needed base-devel cmake ninja git sqlite catch2
# Studio GUI
sudo pacman -S --needed qt6-base qt6-multimedia
# Game runtime
sudo pacman -S --needed sdl3 glm openal assimp freetype2
```

SQLite and Catch2 have vendored fallbacks, so the headless build needs only a
compiler, CMake and Ninja.

## Headless build (CI, containers, agents)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DBULLY_BUILD_STUDIO=OFF -DBULLY_BUILD_RUNTIME=OFF
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

## Full build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
```

CMake presets cover the common combinations:

```sh
cmake --preset debug            # full local build
cmake --preset headless         # no Qt, no SDL
cmake --preset release
cmake --preset asan             # sanitizer build
```

## Make targets

`make` (or `make help`) lists every target:

| Target | Effect |
|---|---|
| `setup` | install system dependencies (Arch `pacman -S`) |
| `configure` | configure the `build/` directory |
| `build` | incremental build |
| `editor` | build + launch the Studio GUI |
| `run` | build + launch the example game |
| `test` | fast test suite |
| `test-all` | full suite plus QA over `examples/` projects |
| `package` | build a `.bpackage` from the example project |
| `clean` | remove `build/` |
| `docs` | print the documentation index |

## Options

| Option | Default | Meaning |
|---|---|---|
| `BULLY_BUILD_STUDIO` | ON | Build the Qt 6 desktop application |
| `BULLY_BUILD_RUNTIME` | ON | Build the SDL3 game runtime and `apps/game` |
| `BULLY_BUILD_TESTS` | ON | Build the Catch2 suite |
| `BULLY_BUILD_CLI` | ON | Build `bully_asset_cli` |
| `BULLY_WARNINGS_AS_ERRORS` | OFF | Treat warnings as errors (used by CI) |
| `BULLY_ENABLE_SANITIZERS` | OFF | Address + UB sanitizers |
| `BULLY_USE_SYSTEM_SQLITE` | ON | Prefer system SQLite over the amalgamation |

When Qt 6 or SDL3 is missing, CMake disables the corresponding target with a
`message(STATUS ...)` note rather than failing the configure step, and the
summary banner prints exactly what was built.
