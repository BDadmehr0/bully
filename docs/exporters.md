# Exporters - compiled assets back to editable files

Importers are one direction of travel: source file → compiled `.b*` asset.
**Exporters** are the other: compiled asset → an external or authoring file that
other tools understand. They exist so a project is never trapped inside the
pipeline - art can leave for Blender/Aseprite/Godot, and content can come back
into the studio as source JSON.

Everything here is headless: no Qt, no GPU, no network. `libbully_exporters` is
a small static library over `bully_core` and `bully_formats`.

```cpp
#include "bully/exporters/exporters.h"

// One call, extension-driven, atomic write.
auto status = bully::exporters::export_file("built/models/crate.bmodel",
                                            "out/crate.obj");
if (!status) { std::cerr << status.diagnostics().format(); }
```

## The catalogue

`bully::exporters::exporters()` returns nine `ExporterInfo` rows in a stable
order. `exporter_for(path)` picks one from the destination extension
(case-insensitive, longest suffix wins, so `.scene.json` beats `.json`) and
`exporter_for_kind(kind)` picks one from the asset kind.

| id | extension | source kind | output |
|---|---|---|---|
| `com.bully.exporter.png` | `.png` | texture | PNG (RGBA8 / RGB8 / grey / 16-bit RGB) |
| `com.bully.exporter.ppm` | `.ppm` | texture | binary PPM, for eyeballing raw data |
| `com.bully.exporter.wav` | `.wav` | audio | RIFF/WAVE, 16-bit PCM, mono or stereo |
| `com.bully.exporter.obj` | `.obj` | model | OBJ geometry + companion `.mtl` |
| `com.bully.exporter.scene_json` | `.scene.json` | scene | authored scene source |
| `com.bully.exporter.mission_json` | `.mission.json` | mission | mission graph source |
| `com.bully.exporter.dialogue_json` | `.dialogue.json` | dialogue | dialogue graph source |
| `com.bully.exporter.anim_json` | `.banim.json` | animation | clip, tracks and root motion |
| `com.bully.exporter.material_json` | `.bmaterial.json` | material | material parameters |

The JSON exporters emit **the same shapes the corresponding importers accept**.
That is a hard requirement and is covered by tests: export → write → re-import
must yield an equivalent asset. This is what makes an exported `.scene.json`
useful as a diffable, reviewable snapshot of a compiled scene.

## Textures

`to_rgba(const TextureDocument&)` denormalises every supported storage type to
8-bit RGBA (`RGBA8`, `RGB8`, `R8`, `RGBA4444`, `RGB565`). Exports read mip 0
only; a texture with no mips, or with a payload shorter than the declared
dimensions require, is rejected instead of producing a truncated image.

Block-compressed storage (BC1/BC3) is **refused** with `BULLY-EXP-0002` rather
than silently reinterpreted as RGBA. Refusing is deliberate: a wrong-but-plausible
texture export is far more expensive to debug than a clear error. The same
applies to a texture in reference mode - there is no pixel data to export.

PNG output uses stored (uncompressed) deflate blocks plus an Adler-32 checksum.
That keeps the encoder ~100 lines with no dependency, and the files are accepted
by every decoder. It is not size-optimised.

## Models

OBJ export writes LOD 0, deduplicated into a shared `v`/`vt`/`vn` table, with
1-based indices, one `o` group per submesh, and one `usemtl` per material slot.
A companion `.mtl` is written next to the `.obj` and referenced with `mtllib`,
mapping PBR-ish parameters back onto `Kd`/`Ks`/`Ns`/`d` and `map_Kd`.

Skipped LODs are reported as a warning (`BULLY-EXP-0022`) rather than dropped
silently - exported geometry that is smaller than the source is exactly the kind
of surprise that should be visible in a build log.

## Audio

WAV export writes canonical 44-byte-header RIFF/WAVE with 16-bit little-endian
PCM, correctly word-aligned for mono payloads, and reports a warning when high
bit depth has to be requantised. Audio assets in reference mode (no embedded
samples) cannot be exported and fail with `BULLY-EXP-0010`.

## Safety of the write path

`export_file()` resolves the exporter from the destination extension, encodes
fully in memory, then writes to a temporary file in the destination directory and
renames it into place. A failed or interrupted export therefore never leaves a
half-written file where a consumer might read it; a rename failure surfaces as
`BULLY-EXP-0030`. An unknown destination extension (`0031`), unknown source kind
for that extension (`0032`), or an undecodable source (`0033`/`0034`/`0035`) is
reported without touching the filesystem.

## Diagnostics

| Code | Meaning |
|---|---|
| `BULLY-EXP-0001` | texture has no mip levels |
| `BULLY-EXP-0002` | block-compressed texture cannot be exported as RGBA |
| `BULLY-EXP-0003` | texture payload shorter than declared dimensions |
| `BULLY-EXP-0004` | invalid RGBA span |
| `BULLY-EXP-0010` | audio has no embedded samples (reference mode) |
| `BULLY-EXP-0011` | invalid audio format for WAV export |
| `BULLY-EXP-0020` | model has no geometry |
| `BULLY-EXP-0021` | malformed mesh index |
| `BULLY-EXP-0022` | warning: only LOD 0 exported |
| `BULLY-EXP-0030` | temporary file / rename failed |
| `BULLY-EXP-0031` | no exporter for the destination extension |
| `BULLY-EXP-0032` | exporter does not support this asset kind |
| `BULLY-EXP-0033` | source file could not be read |
| `BULLY-EXP-0034` | source is not a Bully runtime asset |
| `BULLY-EXP-0035` | document failed to deserialize |

## Command line

```console
$ bully_asset_cli exporters
Exporters (9):
  com.bully.exporter.png  [.png]  PNG image  <- texture
  ...

$ bully_asset_cli export built/models/crate.bmodel exported/crate.obj
Exported built/models/crate.bmodel -> exported/crate.obj
```

With `--project DIR` (or when run at a project root), a relative destination is
resolved inside the project, so `export built/x.bmodel exported/x.obj` writes
`<project>/exported/x.obj`.

## Testing

`tests/test_exporters` (13 cases) covers the PNG decoder round trip, WAV sample
equality after re-import, OBJ geometry and MTL re-import, LOD-skip warnings,
determinism of repeated exports, refusal paths for BC1 and reference-mode audio,
the JSON shapes, atomic `export_file` behaviour and case-insensitive extension
dispatch. The CLI round trip lives in `tests/qa/test_headless.cpp`.
