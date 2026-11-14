# Testing

Two independent layers must stay green:

| Layer | Command | What it catches |
|---|---|---|
| code | `ctest --preset headless` (or `make test`) | format/decoder bugs, pipeline and plugin regressions |
| content | `scripts/qa_examples.sh` (`make test-all`) | broken references, missing recordings, stale or corrupt packages |

## Code tests (Catch2 v3)

The suite is five executables registered with CTest:

| Suite | Binary | Scope |
|---|---|---|
| `core` | `tests/test_core` | version, errors/diagnostics, UUID, strings, logging, filesystem, JSON |
| `formats` | `tests/test_formats` | the nine runtime formats: round-trip, versioning, corruption handling, validation |
| `pipeline` | `tests/test_pipeline` | importers, cache, build graph, packaging, QA rules, presets, templates |
| `plugins` | `tests/test_plugins` | the real plugin host: discovery, ABI check, load, import, QA, unload |
| `runtime` | `tests/test_runtime` | asset database mounts, story state, clock and input map |
| `exporters` | `tests/test_exporters` | compiled asset → PNG/WAV/OBJ/JSON export and round-trip re-import |
| `headless` | `tests/test_headless` | the CLI and game host end-to-end in a scratch project |
| `launcher` | `tests/qa/test_launcher.py` | `scripts/run_game.py`: dry run, auto-build, package run, usage errors |

Build and run the `asan` preset as well when changing anything that handles raw
byte buffers. Two real bugs (an out-of-bounds read in mip generation and a
use-after-free across the plugin ABI) were invisible to the default build.

Two checks run alongside the suites:

* `scripts/check_includes.py` - every source file includes the standard headers
  it uses (see [building.md](building.md) for why this is enforced).
* the `bully_header_check` target - one translation unit per public header, so
  each header is proven self-contained.

```console
$ ctest --preset headless
    Start 1: core
1/6 Test #1: core .............................   Passed    0.02 sec
...
100% tests passed out of 6
```

Run one suite with its filter:

```console
$ build/headless/tests/test_pipeline "[presets]"
All tests passed (58 assertions in 6 test cases)
$ build/headless/tests/test_formats "Package*"
```

`tests/support/` holds shared helpers:

* `TempDir` - a scratch project directory that cleans itself up,
* `ProjectFixture` - a real project (layout, SQLite registry, cache, importers,
  pipeline) for pipeline tests,
* source builders - `cube_obj()`, `dialogue_source_json()`, `scene_source_json()`,
  `mission_source_json()`, minimal PNG/BMP/WAV/TGA payloads.

## What the tests are allowed to assume

Diagnostics are part of the contract, so tests assert on **codes**, not on
message text. Examples pinned by the suite:

* OBJ: a cube becomes **20 vertices / 12 triangles**; `BULLY-OBJ-0006` (no
  vertices) and `BULLY-OBJ-0007` (no faces) are hard errors, a missing `mtllib`
  target is a warning.
* Images: BMP and TGA decode to **top-left origin**; a truncated PNG IDAT stream
  is an error (`BULLY-IMG-0019`), never a partial image.
* Packages: duplicate ids (`BULLY-PKG-0005`) and empty payloads
  (`BULLY-PKG-0013`) are refused; `verify()` names the broken entry.
* Presets: `../../etc/passwd` never resolves to a file; `$BULLY_PRESET_PATH`
  overrides the shipped files; a project preset reaches the compiled
  `ModelBuildProfile`.
* References: an authored `"model": "crate"` compiles to the model's asset id
  **and** persists as a dependency row; an unresolvable reference is preserved
  verbatim and reported by `scene.references` (`BULLY-QA-0060`).

When a test and the documentation disagree, fix whichever is wrong - and say
which one in the commit message.

## QA rules

`bully_asset_cli qa` runs 13 built-in rules. Each is independent and can be run
alone (`--rule NAME`) and extended by plugins.

| Rule | Fails when | Codes |
|---|---|---|
| `asset.references` | a declared dependency is not a registered asset | `BULLY-QA-0001` |
| `asset.import` | an asset does not import in a dry run | forwards `BULLY-*` |
| `runtime.load` | a compiled asset cannot be decoded by the runtime decoders | forwards `BULLY-BIN-*` |
| `dialogue.recordings` | a line has text but no selected take with audio | `BULLY-QA-0020` |
| `mission.dependencies` | the cross-mission dependency graph has a cycle | `BULLY-QA-0030` |
| `asset.orphans` | (warning) an asset is referenced by nothing | `BULLY-QA-0040` |
| `asset.outputs` | an asset has no compiled output or it is stale | `BULLY-QA-0050..0052` |
| `dialogue.subtitles` | subtitle timing drifts from the audio window > 0.15 s | `BULLY-QA-0025` |
| `dialogue.graph` | a graph has dangling branches or no terminating node | `BULLY-QA-0026` |
| `animation.references` | a clip targets a bone missing from its rig | `BULLY-QA-0055` |
| `scene.references` | a scene references an unknown model/mission | `BULLY-QA-0060` |
| `plugin.compatibility` | a plugin manifest declares an incompatible API major | `BULLY-QA-0070..0074` |
| `package.integrity` | an existing `.bpackage` fails CRC verification | `BULLY-QA-0080` |

Reports are written to `reports/qa-report.md` (rendered by the Studio QA panel)
and `reports/qa-report.json` (consumed by CI). The markdown starts with a table
of `checked / passed / failed / warnings / duration` per rule, followed by one
section per rule with its findings.

Warnings never fail the command. Errors do, and the exit code is what CI sees.

### Writing a QA rule

```cpp
runner.add_rule("my.rule", "short description", [](QaContext& context) {
    QaRuleResult result;
    for (const auto& record : context.registry.all()) {
        ++result.checked;
        if (/* problem */) {
            ++result.failed;
            result.findings.push_back(finding(core::Severity::Error, result.rule,
                                              "BULLY-QA-0090", "what is wrong",
                                              record.asset_id, record.source_path));
        } else {
            ++result.passed;
        }
    }
    return result;
});
```

Rules receive the layout, the registry, the importer registry and the pipeline,
so they can decode compiled assets, re-run importers in dry-run mode and read
project files. Rules must be side-effect free: a QA run never changes a project
(see the `asset.import` dry-run guarantee).

## Plugin tests

`tests/test_plugins.cpp` exercises the real load path against the sample plugin
built in-tree: discovery, manifest validation, ABI check, `dlopen`, import, QA
rule execution, unload, and the assertion that unload retracts the importer, the
QA rule id and the panel descriptor. Copy it as the integration test for a
third-party plugin.

## Headless CLI test

`tests/test_headless.cpp` runs the real `bully_asset_cli` binary in a temporary
project (`new` → `build` → `info` → `verify` → `qa` → `package`) and asserts on
exit codes and report files, so the CLI's contract is covered without a GUI.

## Continuous integration recipe

```sh
python3 -m pip install --user cmake ninja      # or use system CMake >= 3.21
cmake --preset headless
cmake --build --preset headless -j"$(nproc)"
ctest --preset headless
scripts/qa_examples.sh build/headless/apps/asset_cli/bully_asset_cli
```

The `headless` preset builds with `-Werror` and the full warning set; the `asan`
preset additionally runs ASan/UBSan for a slower, deeper run.
