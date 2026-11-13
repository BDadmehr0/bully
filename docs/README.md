# Bully Studio documentation

Bully Studio is a modular game development platform: a headless asset pipeline
(importers, cache, QA, packaging), a Qt 6 desktop studio that drives it, and a
game runtime that consumes its compiled formats.

| Document | Contents |
|---|---|
| [architecture.md](architecture.md) | Layer diagram, module boundaries, dependency rules |
| [building.md](building.md) | Toolchain, CMake presets, make targets, headless vs full build |
| [formats/README.md](formats/README.md) | Format index and the shared container header |
| [formats/bmodel.md](formats/bmodel.md) | Compiled 3D model |
| [formats/btexture.md](formats/btexture.md) | Compiled texture |
| [formats/bmaterial.md](formats/bmaterial.md) | Material and shader parameters |
| [formats/banim.md](formats/banim.md) | Skeletal animation clip |
| [formats/baudio.md](formats/baudio.md) | Audio asset |
| [formats/bdialogue.md](formats/bdialogue.md) | Dialogue and voice-over graph |
| [formats/bscene.md](formats/bscene.md) | Scene / level |
| [formats/bmission.md](formats/bmission.md) | Mission / quest graph |
| [formats/bpackage.md](formats/bpackage.md) | Distributable asset package |
| [plugins.md](plugins.md) | Plugin ABI, manifest, lifecycle, isolation limits |
| [retro-presets.md](retro-presets.md) | Retro graphics toolkit presets and budgets |
| [importers.md](importers.md) | Importer contract and the built-in catalogue |
| [cli.md](cli.md) | `bully_asset_cli` reference |
| [testing.md](testing.md) | Test layout, ctest, CI usage |

Every format document states its version, chunk table and compatibility rules.
Diagnostic codes (`BULLY-XXX-NNNN`) are stable and greppable; each module's
header comment lists the range it owns.
