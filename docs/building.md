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
compiler, CMake and Ninja.

## Compiler support

The project targets C++20 and is built with `-Werror` under the `headless`
preset. It is expected to build warning-free on:

| Toolchain | Status |
|---|---|
| GCC 12 (Debian bookworm) | verified |
| GCC 16 | supported; drives the include-hygiene rules below |
| Clang 15+ | supported |

Every source file includes the standard headers it uses **explicitly**. This is
not stylistic: C++ code that relies on transitive includes compiles fine until a
newer standard library drops one, and then the whole project breaks on a new
toolchain while still building on the old one. That is what happened with GCC
16, where `<string>` stopped providing `<cstdint>` and `<vector>` stopped
providing `<algorithm>`, so `std::uint32_t` and `std::remove_if` stopped
resolving.

Two guards keep this from coming back:

* `scripts/check_includes.py` (`make check`) greps every source file for `std::`
  facilities used without the corresponding `#include`, and fails if any file is
  missing one. `--apply` fixes them in place.
* The `bully_header_check` CMake target compiles **one translation unit per
  public header**, each including exactly that header and nothing else, with
  `-Werror`. A header that forgets an include fails there, pointing at the
  header rather than at a confused downstream file.

Both run as part of `make test`. Qt 6 and SDL3 are optional: without them the Studio
shell and the *windowed* game host are skipped, and everything else - including
`bully_game --headless` - still builds and is tested.

## Headless build (CI, containers, agents)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DBULLY_BUILD_STUDIO=OFF
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Running the tests: **build first**, then use `ctest --preset <name>` from the
repository root (or `ctest --test-dir build/headless`).

```sh
cmake --preset headless
cmake --build --preset headless -j"$(nproc)"   # not optional
ctest --preset headless
```

`ctest` never compiles anything. If the build step is skipped - for example
`cmake --preset headless && build`, where `build` is not a command and `&&`
short-circuits - ctest runs whatever binaries are already in `build/`, which may
be from an older checkout entirely. That produces convincing, current-looking
failures against code that no longer exists. Two guards:

* configuring warns when the build tree is older than the sources,
* a test preset that finds no tests fails instead of exiting 0.

A bare `ctest` run in the *source* directory also finds nothing; the tree writes
a `CTestTestfile.cmake` there so that mistake is a hard error too.

### Where the binaries land

| Preset | CLI | Game host | Tests |
|---|---|---|---|
| `headless` | `build/headless/apps/asset_cli/bully_asset_cli` | `build/headless/apps/game/bully_game` (headless) | `build/headless/tests/` |
| `debug` | `build/apps/asset_cli/bully_asset_cli` | `build/apps/game/bully_game` | `build/tests/` |
| `release` | `build/release/apps/...` | `build/release/apps/game/...` | `build/release/tests/` |

Note the `debug` and `release` presets share `build/` while `headless` uses
`build/headless/`; `make` uses `build/` by default.

## Sanitizers and container assertions

Debug builds define `_GLIBCXX_ASSERTIONS` by default (`-DBULLY_STL_ASSERTIONS=OFF`
to disable). That turns out-of-bounds `std::vector` access into an immediate
abort instead of silent heap corruption. Distributions differ here - Arch's GCC
enables it in its default flags - which is how an out-of-bounds read in the
texture mip generator aborted on one machine and passed everywhere else.

```sh
cmake --preset asan          # ASan + UBSan + assertions
cmake --build --preset asan -j"$(nproc)"
ctest --preset asan
```

Run the `asan` preset before touching code that indexes raw buffers (image
codecs, binary readers, the mip chain). It has already caught a heap overflow
and a plugin ABI use-after-free that the normal suite could not see.

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
