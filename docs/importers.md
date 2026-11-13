# Importers

An importer turns a **source file** (something an artist edits) into a
**compiled asset** (a `.b*` runtime format). Importers are the only place where
"external" file formats are understood; everything downstream - QA, caching,
packaging, the runtime - only sees compiled assets.

```
source file ──importer──▶ compiled asset ──QA──▶ cache ──▶ .bpackage
```

## Built-in importers

```console
$ bully_asset_cli list
Registered importers (11):
  com.bully.importer.obj           v1.1.0  [.obj]         Wavefront OBJ
  com.bully.importer.glb           v1.1.0  [.glb]         glTF 2.0 Binary (.glb)
  com.bully.importer.gltf          v1.1.0  [.gltf]        glTF 2.0 (.gltf)
  com.bully.importer.fbx_bridge    v1.0.0  [.fbx]         FBX (external bridge)
  com.bully.importer.image         v1.0.0  [.png, .bmp, .tga, .ppm, .targa]
  com.bully.importer.wav           v1.0.0  [.wav]         WAV audio (PCM)
  com.bully.importer.material_json v1.0.0  [.bmaterial.json]
  com.bully.importer.anim_json     v1.0.0  [.banim.json]
  com.bully.importer.scene_json    v1.0.0  [.scene.json]
  com.bully.importer.mission_json  v1.0.0  [.mission.json]
  com.bully.importer.dialogue_json v1.0.0  [.dialogue.json]
```

| Importer | Produces | Extensions |
|---|---|---|
| Wavefront OBJ | `.bmodel` | `.obj` (+ its `.mtl`) |
| glTF / GLB | `.bmodel` | `.gltf`, `.glb` |
| FBX bridge | `.bmodel` | `.fbx` (requires an external converter) |
| Image | `.btexture` | `.png`, `.bmp`, `.tga`, `.ppm`, `.targa` |
| WAV | `.baudio` | `.wav` |
| Authoring JSON | `.bmaterial`, `.banim`, `.bscene`, `.bmission`, `.bdialogue` | `*.{bmaterial,banim,scene,mission,dialogue}.json` |

Everything else - textures, meshes, dialogue graphs, missions - is authored in
Bully Studio itself (or in an external DCC tool for geometry) and imported.

## Models

### Wavefront OBJ (`com.bully.importer.obj`)

Parses `v`, `vn`, `vt`, `f`, `o`, `g`, `usemtl` and `mtllib`. The parser emits
one vertex per unique `(position, uv, normal)` triple: a cube written with one
normal per face collapses to **20 unique vertices / 12 triangles** (24
corners minus the four shared by matching normals), which the test suite pins.

* `mtllib` is resolved relative to the OBJ and registered as a **hard source
  dependency**, so editing the `.mtl` invalidates the asset.
* A missing `mtllib` target is a warning (`BULLY-OBJ-0010`), not a failure.
* No vertices (`BULLY-OBJ-0006`) or no faces (`BULLY-OBJ-0007`) is a hard error.

Settings: `weld_vertices`, `generate_normals`, `scale`, `up_axis` (`y`/`z`),
`generate_lods`, `lod_count`, `retro_preset`, `bake_vertex_lighting`.

### glTF / GLB (`com.bully.importer.glb`, `.gltf`)

Reads the JSON document (or GLB container: magic `glTF`, version 2, JSON chunk +
BIN chunk), then the accessors/buffer views, building meshes, materials, the
skeleton and clip references. Animation clips are registered as asset
dependencies rather than inlined, so one rig can be re-exported independently.

Settings: `scale`, `import_skin`, `import_animations`, `generate_lods`,
`lod_count`, `retro_preset`.

### FBX bridge (`com.bully.importer.fbx_bridge`)

Bully Studio deliberately **does not embed an FBX parser**. FBX import is a
configurable external conversion step:

```console
# settings on the asset
converter       = /usr/bin/blender
converter_args  = --background --python scripts/fbx_to_glb.py -- "{input}" "{output}"
intermediate_format = glb
```

Without a configured converter the asset fails with a message explaining exactly
what to configure (`BULLY-FBX-0001`); when external process execution is disabled
in the build the error says so instead of silently doing nothing
(`BULLY-FBX-0002`). This keeps the build reproducible and free of a proprietary
SDK dependency.

## Textures (`com.bully.importer.image`)

Decodes PNG (zlib inflate, all five colour types, interlace rejected explicitly),
BMP (1/4/8/24/32 bpp, including RLE-free top-down images) and TGA (uncompressed
and RLE, 8/16/24/32 bpp), and PPM (P3/P6). Every decoder normalises to
**top-left-origin RGBA8** before the texture builder runs, so mip generation and
atlas UVs never depend on the source convention.

Then: optional downscale (`max_size`, `nearest_downscale`), pixel-format
conversion (`rgba8`, `rgb8`, `r8`, `rgba4444`, `rgb565`), mip chain
(box filter), sampler/wrap defaults, sRGB flag and a `usage` slot.

Diagnostic codes: `BULLY-IMG-0010..0019` (PNG structure), `0030..0046`
(BMP/TGA), `0050..0052` (PPM). A truncated IDAT stream is an error, never a
partially decoded image.

## Audio (`com.bully.importer.wav`)

RIFF/WAVE parsing with 8/16/24/32-bit PCM and float, downmixed to 16-bit
interleaved PCM. `embed_pcm` decides between a self-contained `.baudio` and a
lightweight metadata-only asset that references the original file. The importer
records peak and loudness, sets loop points (using `frame_count()` when looping
is enabled with `loop_end = 0`), and optionally normalises to `target_peak`.

## Authoring JSON

These importers read hand-authored/Studio-authored JSON and produce the compiled
format. They are intentionally tolerant about unknown keys (forward compatible)
and strict about structure (a scene with a cycle fails the build).

| Importer | Source | Key JSON sections |
|---|---|---|
| `material_json` | `*.bmaterial.json` | `shader_preset`, `parent`, `textures`, `parameters`, `render_state` |
| `anim_json` | `*.banim.json` | `duration`, `sample_rate`, `loop`, `bones`, `tracks`, `events`, `root_motion` |
| `scene_json` | `*.scene.json` | `environment`, `entities`, `lights`, `triggers`, `nav`, `spawns` |
| `mission_json` | `*.mission.json` | `giver`, `dependencies`, `objectives`, `nodes`, `edges`, `conditions` |
| `dialogue_json` | `*.dialogue.json` | `language`, `characters`, `lines`, `takes`, `nodes`, `flags` |

## Story import alias

`*.dialogue.json` is the canonical dialogue source. Markdown story documents are
converted to that JSON by the Studio's story importer, so the pipeline only ever
sees one dialogue source format.

## Retro presets apply to every importer

Every importer exposes a `retro_preset` setting, and the pipeline fills it from
the project manifest (`project.retro_preset`) unless the asset overrides it. The
preset is data: `assets/presets/<name>.json`, resolved through

1. `$BULLY_PRESET_PATH` (colon separated),
2. `<cwd>/presets`, `<cwd>/assets/presets`,
3. the source tree's `assets/presets` baked in at configure time.

A missing file falls back to the documented built-in numbers, and a malformed
file is ignored with a warning instead of breaking a build. Preset names are
validated so `../../etc/passwd` cannot be used as a preset.

See [docs/retro-presets.md](retro-presets.md) for the full field list.

## Adding an importer

1. Implement `bully::pipeline::Importer` (id, display name, version, extensions,
   `produces()`, `settings_schema()`, `import()`).
2. Return a compiled document's bytes (`serialize()`), a human summary, the JSON
   report and any diagnostics. Push extra sources into
   `ImportResult::dependencies` so the cache invalidates correctly.
3. Register it in `make_builtin_importers()`, or ship it as a plugin
   ([docs/plugins.md](plugins.md)).

Rules of thumb:

* **Report, don't guess.** Unsupported constructs are errors with a code and the
  file path; lossy but acceptable conversions are warnings.
* **Never write outside `output_path`.**
* **Be deterministic.** Same input + same settings ⇒ same bytes, or the asset
  cache can never hit.
* **Attach diagnostics to the returned value** when the result is still usable
  (`Result::attach_diagnostics`), so warnings survive a successful build.
