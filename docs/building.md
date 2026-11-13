# Building

## Dependencies (Arch Linux)

```sh
sudo pacman -S --needed base-devel cmake ninja git sqlite catch2
# Studio GUI (optional)
sudo pacman -S --needed qt6-base qt6-multimedia
# Windowed game host (optional; the game also runs --headless)
sudo pacman -S --needed sdl3 mesa
```

SQLite and Catch2 have vendored fallbacks, so the headless build needs only a
compiler, CMake and Ninja. Qt 6 and SDL3 are optional: without them the Studio
shell and the *windowed* game host are skipped, and everything else - including
`bully_game --headless` - still builds and is tested.

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

`make` (or `make help`) lists every target. The Makefile is a thin wrapper over
CMake - there is one source of truth for the build:

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

Variables: `BUILD_DIR` (default `build`), `BUILD_TYPE` (default `Debug`),
`JOBS` (default `nproc`), `EXAMPLE` (default `examples/hello-retro`).

`test-all` runs `scripts/qa_examples.sh`, which builds, QAs and packages every
committed example project - a content regression fails just like a code one.

## Presets in detail

| Preset | Binary dir | Studio | Runtime | Tests | Notes |
|---|---|---|---|---|---|
| `debug` | `build/` | ON | ON | ON | default local build |
| `headless` | `build/headless/` | OFF | OFF | ON | `-Werror` on, for CI |
| `release` | `build/release/` | ON | ON | ON | optimised |
| `asan` | `build/asan/` | OFF | OFF | ON | ASan + UBSan |

```sh
cmake --preset headless
cmake --build --preset headless
ctest --preset headless
```

The `headless` preset sets `BULLY_WARNINGS_AS_ERRORS=ON`, so the tree must build
clean with the full warning set (`-Wconversion`, `-Wsign-conversion`, `-Wshadow`,
`-Wold-style-cast`, `-Wnon-virtual-dtor`, ...). Third-party code (SQLite, Catch2)
is compiled with warnings disabled and its headers included as `SYSTEM`, so a
strict build never fails inside a vendored dependency.

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

When Qt 6 is missing the Studio target is skipped; when SDL3 or OpenGL is
missing, `bully_game` is still built but without the window (its `--headless`
mode is unaffected). Neither case fails the configure step, and the summary
banner prints exactly what was built:

```console
--   Qt6 studio shell  : OFF
--   game runtime      : ON
--   asset CLI         : ON
--   tests             : ON
```
