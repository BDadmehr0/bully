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

Or with the Makefile: `make`, `make test`, `make test-all`, `make run`.

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
