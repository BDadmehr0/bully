# Runtime format index

Nine versioned binary formats. All share one container header and one chunk
stream, all are little-endian, and all are validated on read.

| Extension | Magic | Version | Purpose | Document |
|---|---|---|---|---|
| `.bmodel` | `BMOD` | 1.0 | Meshes, LODs, materials, rig, collision, baked AO | [bmodel.md](bmodel.md) |
| `.btexture` | `BTEX` | 1.0 | Pixels, mip chain, atlas regions | [btexture.md](btexture.md) |
| `.bmaterial` | `BMAT` | 1.0 | Shader preset, texture slots, parameters | [bmaterial.md](bmaterial.md) |
| `.banim` | `BANI` | 1.0 | Bone tracks, events, facial tracks | [banim.md](banim.md) |
| `.baudio` | `BAUD` | 1.0 | Audio metadata + optional PCM | [baudio.md](baudio.md) |
| `.bdialogue` | `BDLG` | 1.0 | Lines, takes, subtitles, branch graph | [bdialogue.md](bdialogue.md) |
| `.bscene` | `BSCN` | 1.0 | Entities, components, lights, triggers | [bscene.md](bscene.md) |
| `.bmission` | `BMSN` | 1.0 | Objectives, node graph, conditions | [bmission.md](bmission.md) |
| `.bpackage` | `BPKG` | 1.0 | Table of contents + embedded resources | [bpackage.md](bpackage.md) |

`bully_asset_cli formats` prints this table from the same registry the code uses
(`formats::known_formats()`), so the docs cannot drift from the implementation
without the CLI test failing.

## Shared container header (32 bytes)

```
offset  size  field
0       4     magic          ASCII fourcc stored little-endian, e.g. "BMOD"
4       2     version_major
6       2     version_minor
8       4     header_size    bytes of this header (32)
12      4     flags          format-specific bit flags
16      8     payload_size   bytes of chunk data following the header
24      4     payload_crc    CRC-32 (IEEE 802.3) of the chunk data
28      4     reserved       must be zero
```

Because the magic is a little-endian `uint32`, the first four bytes of every
file spell the fourcc in reading order: `44 4f 4d 42` is `BMOD`.

## Chunk stream

The payload is a sequence of chunks:

```
uint32 fourcc       little-endian
uint32 size         byte length of the chunk body
uint8  body[size]
```

Chunks may appear in any order; readers that meet an unexpected chunk skip it,
log an informational diagnostic and resynchronise on the declared boundary.
Missing optional chunks mean documented defaults.

## Versioning policy

* **Minor bump** - chunks added, or a chunk gains trailing fields that older
  readers can ignore. A reader accepts a file up to one minor newer and emits
  `BULLY-BIN-0010` (warning). Beyond that it refuses the file rather than
  guessing.
* **Major bump** - an existing chunk changes layout or meaning. Files of a
  different major version are rejected with `BULLY-BIN-0009`.
* The version is written by the writer (`FormatDescriptor::version`) and checked
  before a single byte of payload is interpreted.

## Compatibility guarantees

1. Asset ids embedded in every format are UUID strings and never change once
   assigned. Rebuilds preserve them (`deterministic_asset_id` derives them from
   the project-relative source path).
2. Cross-references between assets are always asset ids, never file paths, so a
   package can be mounted without the source tree.
3. Source assets are never modified: importers open them read-only and the
   pipeline writes only into `built/`, `.bully-cache/`, `packages/` and
   `reports/`.
4. Every decoder reports structural problems as diagnostics with a stable code
   instead of returning partially-initialised data.
