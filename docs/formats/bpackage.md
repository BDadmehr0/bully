# `.bpackage` - distributable asset package

Magic `BPKG`, version **1.0**, produced by the packager
(`bully_asset_cli package`, Studio Build Manager).

| Chunk | Body |
|---|---|
| `INFO` | package identity, name, target platform, build profile, content version |
| `ENTR` | table of contents: one record per asset |
| `DATA` | concatenated resource payloads addressed by `ENTR` offsets |
| `META` | JSON manifest (build settings, QA summary, provenance) |

## Entries

```
string asset_id
u32    kind       0 model, 1 texture, 2 material, 3 animation, 4 audio,
                  5 dialogue, 6 scene, 7 mission, 255 other
string logical_path   "models/students/missy.bmodel"
u32    offset         relative to the start of the DATA chunk
u32    size
u32    crc            CRC-32 of the resource bytes
u32    flags
```

`extract(asset_id)` verifies the CRC before returning bytes;
`verify()` walks every entry and reports the exact broken resource instead of
failing the whole package.

## Determinism

Entries are sorted by logical path, duplicate asset ids are rejected
(`BULLY-PKG-0005`), empty resources are rejected (`BULLY-PKG-0013`), and the
manifest can omit the creation timestamp so two builds of the same inputs
produce byte-identical manifests. `metadata.stale_assets` records how many
entries were packaged while their build status was not `UpToDate` - incomplete
packages are never silent.

## Manifest sidecar

`packages/<name>.manifest.json` (schema in `PackageManifest`) lists every entry
with size and CRC plus `target_platform`, `build_profile`, `tool_version` and
`deterministic`. It is intentionally JSON so CI can diff builds without decoding
the package.
