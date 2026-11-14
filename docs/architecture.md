# Architecture

```
apps/studio        Qt 6 desktop application (panels, docking, tool integration)
apps/game          runtime host executable (window, input, scene loading)
apps/asset_cli     headless pipeline driver (CI, agents, packaging)
        |
        v
studio/*           editor-facing services: project database, panels, tools
templates/         project scaffolding catalogue
plugins/           stable C ABI host, manifest model, importer bridge
        |
        v
asset_pipeline/    importers, processors, cache, packaging, QA reports
formats/           versioned binary runtime formats + registry
engine/runtime     asset database, story state, clock + input (headless, tested)
engine/core        errors, diagnostics, JSON, UUIDs, filesystem, logging
third_party/       SQLite amalgamation, Catch2 (vendored)
```

## Dependency rules

1. `engine/core` depends on nothing but the standard library. Every failure is a
   `Result<T>`/`Status` carrying a `DiagnosticList`; nothing throws for expected
   failures.
2. `formats` depends on `core` only. Formats never include pipeline or project
   headers, so the runtime can link them without the editor stack.
3. `asset_pipeline` is split in two:
   * `bully_asset_model` - asset identity/settings/build-state types (core only),
   * `bully_pipeline` - importers, processors, cache, packaging, QA.
   The split exists because `studio/project` (project database + asset registry)
   needs the asset types while the pipeline kernel needs the registry. Keeping
   the model in its own library removes the include cycle entirely.
4. `studio/project` owns the SQLite database and the asset registry. Only this
   library opens `project.db`.
5. `plugins` depends on `pipeline` (for the importer interface) and on `dl`.
   Plugins themselves never link host C++ symbols.
6. `engine/runtime` depends on `core` and `formats` only - never on the
   pipeline, project database or plugins. The runtime loads compiled assets, it
   does not build them; that is what keeps a shipped game free of SQLite.
7. `studio/*` (Qt) is the only layer allowed to depend on Qt. Nothing below
   `studio/` may include a Qt header; this is what keeps the CLI, QA dashboard
   and tests buildable in a container without Qt.

## Error model

* `Diagnostic` = severity + stable code + message + source + line.
* `DiagnosticList` aggregates diagnostics; `has_errors()` distinguishes fatal
  from informational problems.
* `Result<T>` either holds a value or a diagnostic list. Successful results can
  still carry warnings (`attach_diagnostics`), which is how forward-compatible
  decoding reports unknown chunks without failing.
* Build results, QA findings and plugin states all expose the same code space so
  `reports/*.md` can be grepped in CI.

## Platform assumptions

Linux/Arch first (Omarchy), C++20, CMake + Ninja, GCC and Clang. Qt 6, SDL3,
Assimp, GLM, OpenAL Soft, FreeType and SQLite are discovered with
`find_package`; none of them is required to build the headless pipeline, the CLI
or the tests.
