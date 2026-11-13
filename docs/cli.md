# `bully_asset_cli` - headless asset pipeline

`bully_asset_cli` is the command-line front end to the same pipeline the Studio
uses: it scans a project, imports sources, runs QA, verifies compiled assets and
builds packages. It needs no Qt and no GPU, which makes it the CI and
content-server entry point.

```console
$ bully_asset_cli --help
bully_asset_cli 0.1.0
Headless asset pipeline / QA / packaging for Bully Studio.

Usage: bully_asset_cli <command> [options] [arguments]
```

## Global options

| Option | Effect |
|---|---|
| `--project DIR` | project root (defaults to the current directory) |
| `--quiet` | only print errors |
| `--json` | machine-readable output where supported |

Exit codes: `0` success, `1` failure (build error, QA error, invalid package),
`2` usage error.

## Commands

### `new <dir> [--template NAME] [--name NAME]`

Creates a project from a template: manifest, directory skeleton, a starter
model with its MTL and a first scene/mission/dialogue.

```console
$ bully_asset_cli new mygame --template retro-action
Created project 'mygame' at mygame (template: retro-action)
```

Layout:

```
mygame/
├── project.bullyproj     # manifest: name, template, retro_preset, build profile
├── assets/{models,textures,audio}/
├── content/{scenes,missions,dialogue,story}/
├── built/                # compiled assets (generated)
├── packages/             # .bpackage + manifest (generated)
├── reports/              # build/QA reports (generated)
└── project.db            # asset registry + import cache (generated, SQLite)
```

### `list`

Every registered importer with id, version, extensions and display name,
including importers contributed by plugins.

```console
$ bully_asset_cli list
Registered importers (11):
  com.bully.importer.obj  v1.1.0  [.obj]  - Wavefront OBJ
  ...
  com.bully.importer.dialogue_json  v1.0.0  [.dialogue.json]  - Bully dialogue source (.json)
```

### `formats`

The nine runtime formats with magic and version. Matches
[docs/formats/README.md](formats/README.md).

### `import <file...>`

Registers existing files as project assets and builds them. Files outside the
project are copied into `assets/imported/` first, so the project stays
self-contained and the registry never holds an absolute path.

```console
$ bully_asset_cli import ../downloads/desk.obj --project mygame
```

### `build [--force] [--dry-run]`

Builds every pending asset in dependency order.

* `--force` ignores the import cache.
* `--dry-run` imports and validates **without writing** compiled assets, updating
  the registry or touching the cache - useful in pre-commit hooks.

```console
$ bully_asset_cli build --project mygame
[1/4] assets/models/crate.obj: built - 1 mesh(es), 20 vertices, 12 triangles, 1 LOD(s)
[2/4] content/dialogue/intro.dialogue.json: built - 3 lines, 5 nodes, 3 unrecorded
...
Build succeeded: 4 built, 0 cached, 0 failed (3 ms)
Report: mygame/reports/build-report.md
```

Each line is `[n/total] source: built|cached|failed - summary`. `cached` means
the import cache returned a byte-identical artifact (source hash, settings hash
and importer version all matched).

### `info <asset-id|source-path>`

Prints the asset record and its compiled summary as JSON.

### `verify <file>`

Decodes a compiled `.b*` file with the runtime decoder and validates it:

```console
$ bully_asset_cli verify mygame/built/models/crate.bmodel
Compiled 3D model ... (.bmodel v1.0)
OK: mygame/built/models/crate.bmodel
```

A corrupt file prints the diagnostics (`BULLY-BIN-*`, `BULLY-MODEL-*`, ...) and
exits `1`. This is the command to use when a build looks fine but the runtime
rejects an asset.

### `qa [--rule NAME]`

Runs the QA dashboard: 13 built-in rules covering references, imports, runtime
loading, dialogue recordings and subtitles, mission graphs, orphans, compiled
outputs, animation/scene references, plugin compatibility and package integrity.
Writes `reports/qa-report.md` and `reports/qa-report.json`.

```console
$ bully_asset_cli qa --project mygame
| rule | checked | passed | failed | warnings | duration |
| asset.references | 4 | 4 | 0 | 0 | 0 ms |
...
```

`--rule NAME` runs a single rule (repeatable). Exit code is `1` when any rule
reports an error; warnings are informational. See
[docs/testing.md](testing.md) for the rule list.

### `package [--name NAME]`

Builds `<name>.bpackage` (default: the project name) plus a JSON manifest into
`packages/`. Entries are sorted by logical path, CRCs are computed per entry and
the manifest omits the timestamp, so two builds of identical inputs are
byte-identical.

```console
$ bully_asset_cli package --project mygame
Packaged 4 assets (3887 bytes)
  package: mygame/packages/mygame.bpackage
  manifest: mygame/packages/mygame.manifest.json
```

### `plugins <dir>`

Discovers plugin manifests in a directory and prints state, ABI and description.
Does not load libraries, so it is safe to run against plugins built for another
platform.

```console
$ bully_asset_cli plugins build/plugins/samples
com.bully.samples.hello_importer v1.0.0 [discovered] api 1.0 Hello Importer (sample)
```

### `templates`

Lists the built-in project templates with a one-line description.

## Environment

| Variable | Effect |
|---|---|
| `BULLY_PRESET_PATH` | extra directories searched for retro presets |
| `BULLY_PLUGIN_PATH` | plugin directories read by the `plugin.compatibility` QA rule and the plugin host |

## Typical CI job

```sh
cmake --preset headless && cmake --build --preset headless
ctest --preset headless
scripts/qa_examples.sh build/headless/apps/asset_cli/bully_asset_cli
```
