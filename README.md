# Bully Studio

A modular game development platform built around a **retro-first asset
pipeline**: import sources, compile them into versioned runtime formats, check
them with an automated QA dashboard, package them, and run them.

The pipeline, the runtime and the CLI build and test in a container with no GUI
toolkit, no GPU and no audio device. Qt 6 (Studio) and SDL3 (windowed game) are
optional on top.

```
apps/studio        Qt 6 desktop shell            (needs Qt 6)
apps/game          SDL3 + headless game host
apps/asset_cli     headless pipeline driver
engine/runtime     asset database, story state, clock, input  (headless)
asset_pipeline     importers, cache, packaging, QA rules
formats            nine versioned .b* runtime formats
engine/core        diagnostics, JSON, UUID, filesystem, logging
plugins            stable C ABI host + manifest model
studio/project     SQLite project database + asset registry
templates          project scaffolding
```

## Quick start

```sh
# headless build (no Qt, no SDL) - what CI runs
cmake --preset headless
cmake --build --preset headless -j"$(nproc)"
ctest --preset headless

# create and build a project
build/headless/apps/asset_cli/bully_asset_cli new mygame --template retro-action
cd mygame
../build/headless/apps/asset_cli/bully_asset_cli build
../build/headless/apps/asset_cli/bully_asset_cli qa
../build/headless/apps/asset_cli/bully_asset_cli package

# run it (same runtime loop as the windowed build)
../build/headless/apps/game/bully_game . --headless --frames 300
```

Or with the Makefile: `make`, `make test`, `make test-all`, `make play`, `make run`.

## Running the game

`bully_game` takes a **project directory** (the folder containing
`project.bullyproj`) or a built `.bpackage`, and needs the project to be built
first - it runs compiled assets, it does not compile them.

The quickest way is the launcher, which finds the build tree, compiles the
project's assets if they are missing and then runs the game:

```sh
python3 scripts/run_game.py                          # examples/hello-retro, windowed
python3 scripts/run_game.py --headless --frames 300   # no window
python3 scripts/run_game.py mygame --headless         # any project
python3 scripts/run_game.py mygame/packages/mygame.bpackage
python3 scripts/run_game.py --list                    # what can be run?
python3 scripts/run_game.py --headless -n             # dry run: show the commands
```

It needs only Python 3 (no packages) and forwards every game flag. `make launcher`
does the same thing.

Or call the binaries directly:

```sh
BIN=build/headless/apps/game/bully_game

$BIN examples/hello-retro                          # windowed (needs SDL3)
$BIN examples/hello-retro --headless --frames 300  # no window, fixed frames
$BIN examples/hello-retro/packages/hello-retro.bpackage   # run a shipped package
$BIN --help
```

| Option | Effect |
|---|---|
| `--headless` | run without a window (CI, bug reports); prints statistics |
| `--frames N` | stop after N frames (headless default: 600) |
| `--scene <id>` | entry scene asset id or logical path |
| `--delta <s>` | fixed frame delta for headless runs (default 1/60) |
| `--verbose` | debug logging |

Exit codes: `0` ran, `1` boot failure, `2` usage error (missing project, wrong
path, nothing compiled - each with a message saying what to do).

Headless output is the same loop the window uses:

```
frames:            300
fixed steps:       300
simulated seconds: 5.000
assets decoded:    2
mounted assets:    4 (1 mounts)
```

**About the window:** the SDL3 host opens a window, clears it to the loaded
scene's ambient colour and draws a moving marker so you can see the loop
running. It is not a 3D renderer - there is no mesh rasteriser yet, and
`docs/runtime.md` says so. The simulation (clock, story, input, asset mounts) is
fully live in both modes.

Without SDL3 installed the binary still builds, and `--headless` works; asking
for a window prints how to install SDL3 and exits 1.

## What it does

| Area | Highlights |
|---|---|
| Import | OBJ, glTF/GLB, FBX (via an explicit external converter), PNG/BMP/TGA/PPM, WAV, and authoring JSON for materials, animation, scenes, missions and dialogue |
| Compile | Nine versioned `.b*` formats with a shared 32-byte header, chunk streams, CRC-32 and a documented forward/backward compatibility policy |
| Retro toolkit | Presets as data (`assets/presets/*.json`): PS2/Xbox-style polygon budgets, 16-bit textures, LODs, baked vertex lighting, audio targets |
| Cache & build | Content-hash import cache, SQLite asset registry, dependency-ordered incremental builds, cross-asset reference resolution |
| QA | 13 dashboard rules: imports, references, runtime decoding, dialogue recordings and subtitles, mission graphs, orphans, stale outputs, plugin compatibility, package integrity |
| Package | Deterministic `.bpackage` with per-entry CRC plus a JSON manifest for CI diffing |
| Export | Compiled assets back out to PNG/PPM/WAV/OBJ+MTL and re-importable authoring JSON, with atomic writes |
| Runtime | Ordered asset mounts (directory or package), story flags/missions/dialogue with save-load, fixed-step clock, rebindable input map |
| Extend | Stable C99 plugin ABI: importers, exporters, QA rules, editor panels, templates, runtime systems |

## Repository conventions

* Every failure is a `Result<T>`/`Status` carrying a `DiagnosticList`. Nothing
  throws for expected failures, and every diagnostic has a stable code
  (`BULLY-<AREA>-<NNNN>`) that is greppable and documented.
* Sources are never modified by the pipeline; compiled output goes to `built/`,
  packages to `packages/`, reports to `reports/`. All three are generated and
  git-ignored.
* A retro constraint is data, not code: budgets and formats live in preset files
  that users and plugins can override.
* Third-party code is vendored only where it is required (SQLite, Catch2) and is
  always compiled with warnings disabled - the project's own tree builds clean
  under `-Werror`.

## Documentation

Start at [docs/README.md](docs/README.md):

* [docs/architecture.md](docs/architecture.md) - layers and dependency rules
* [docs/building.md](docs/building.md) - dependencies, presets, make targets
* [docs/formats/README.md](docs/formats/README.md) - format index and container header
* [docs/importers.md](docs/importers.md) - importer catalogue and contract
* [docs/exporters.md](docs/exporters.md) - exporter catalogue and round-trip guarantees
* [docs/retro-presets.md](docs/retro-presets.md) - budgets and preset schema
* [docs/runtime.md](docs/runtime.md) - runtime session, story state, clock, input
* [docs/plugins.md](docs/plugins.md) - plugin ABI and lifecycle
* [docs/cli.md](docs/cli.md) - `bully_asset_cli` reference
* [docs/testing.md](docs/testing.md) - test layout, QA rules, CI recipe

## Status

| Component | State |
|---|---|
| core, formats (9), pipeline, QA (13 rules), packaging, cache | complete, tested |
| plugin host + sample plugin | complete, tested |
| CLI + templates + example project | complete, tested end to end |
| runtime (assets, story, clock, input, session) | complete, tested |
| exporters (texture, audio, model, JSON) | complete, tested |
| `apps/game` | builds headless today; windowed with SDL3 installed |
| `apps/studio` (Qt 6 shell, panels, extension manager) | not built in this environment (no Qt 6) |

Test suite: `core`, `formats`, `pipeline`, `plugins`, `exporters`, `runtime`, `headless` -
all green under the `headless` preset (which also builds with `-Werror`).
