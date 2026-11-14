# `.btexture` - compiled texture

Magic `BTEX`, version **1.0**, produced by the PNG/BMP/TGA/PPM importer and the
texture processor.

| Chunk | Body | Notes |
|---|---|---|
| `INFO` | identity, dimensions, pixel format, usage, sampler state | required |
| `MIP` | one chunk per mip level: width, height, stride, pixels | at least one |
| `ATLS` | named atlas regions with precomputed UVs | optional |
| `META` | import settings + optimisation report | optional |

Pixel payloads are stored raw (uncompressed) in the declared pixel format.

## Pixel formats

| Value | Name | Bytes/px | Notes |
|---|---|---|---|
| 0 | `Rgba8` | 4 | default |
| 1 | `Rgb8` | 3 | |
| 2 | `R8` | 1 | masks, height maps |
| 3 | `Rgba4444` | 2 | PS2-era 16-bit with alpha |
| 4 | `Rgb565` | 2 | PS2-era 16-bit opaque |
| 5, 6 | `Bc1`, `Bc3` | block | reserved; readers reject them explicitly |
| 255 | `Unknown` | 0 | always rejected |

`is_supported_encoding()` is the single source of truth: a runtime that cannot
decode a block format reports `BULLY-TEX-*` instead of rendering garbage.

## Mip chain

`MipLevel` stores width, height and a row stride, so padded rows produced by
external tools round-trip unchanged. `generate_mip_chain()` box-filters down to
1x1; `resize_nearest()` (pixel-art safe) and `resize_bilinear()` produce a single
resized level.

## Atlas regions

`AtlasRegion` records the pixel rectangle **and** the normalised UV rectangle
(`u0,v0,u1,v1`) so the runtime never recomputes UVs. Regions must lie inside the
base level; `validate()` reports `BULLY-TEX-*` for out-of-bounds rects and for
payloads that are shorter than `stride * height`.
