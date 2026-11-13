# Retro presets

Bully Studio's premise is *retro-first*: constraints from early-2000s hardware
are part of the asset pipeline, not an afterthought. A **retro preset** is a
JSON file describing those constraints - polygon budgets, texture sizes, pixel
formats, filtering, audio rates. Presets are **data**, so adding a new target
never touches C++.

```
assets/presets/
├── none.json          # modern defaults, no constraints
├── ps2.json           # PlayStation 2 (2000)
├── xbox.json          # Xbox (2001)
└── unlit-retro.json   # deliberately flat / pixel-art safe
```

## Selecting a preset

There are three levels, each overriding the previous:

| Level | Where | Effect |
|---|---|---|
| project | `<project>/project.json` → `"retro_preset": "ps2"` | default for every asset whose importer exposes the setting |
| asset | the asset's import settings (`bully_asset_cli` or the Inspector) | this asset only |
| explicit fields | e.g. `"max_texture_size": 128` | wins over the preset value |

The pipeline applies the project preset **after** importer schema defaults and
**before** per-asset overrides, so an artist only sets what differs.

```console
$ bully_asset_cli new mygame --template retro-action
$ python3 - <<'EOF'
import json, pathlib
p = pathlib.Path("mygame/project.json")
d = json.loads(p.read_text()); d["retro_preset"] = "ps2"; p.write_text(json.dumps(d, indent=2))
EOF
$ bully_asset_cli build --project mygame
```

## Resolution order

Presets are looked up by name (`retro_profile(name)`):

1. `$BULLY_PRESET_PATH` - colon-separated directories (CI, per-user overrides).
2. `<cwd>/presets` and `<cwd>/assets/presets` - project-local presets.
3. `<source tree>/assets/presets` - compiled in as
   `BULLY_DEFAULT_PRESET_DIR`.

The first readable `<name>.json` wins. Files are cached per process because an
import asks for the same preset for every asset. Names are sanitised - a name
containing `/` or `..` never resolves to a file. A malformed preset is skipped
with a warning; if nothing is found, the documented built-in numbers below are
used, so a missing data directory degrades to the same behaviour instead of an
empty profile.

## File schema

```json
{
  "name": "ps2",
  "display_name": "PlayStation 2 (2000)",
  "description": "Why the numbers are what they are.",
  "version": "1.0.0",
  "model": {
    "triangle_budget_per_character": 3000,
    "triangle_budget_per_scene": 120000,
    "max_bones_per_mesh": 32,
    "generate_lods": true,
    "lod_count": 3,
    "lod_triangle_ratios": [1.0, 0.5, 0.25],
    "bake_vertex_lighting": true,
    "weld_vertices": true
  },
  "texture": {
    "max_size": 256,
    "pixel_format": "rgba4444",
    "generate_mips": true,
    "sampler": "linear",
    "wrap": "repeat",
    "atlas_max_size": 512
  },
  "material": {
    "shader_preset": "retro.lambert",
    "alpha_cutoff": 0.5,
    "allow_per_pixel_lighting": false
  },
  "audio": {
    "sample_rate": 32000,
    "channels": 2,
    "codec": "adpcm",
    "max_voices": 48
  },
  "render": {
    "resolution": [640, 448],
    "progressive": true,
    "colour_depth": 24,
    "dither": true,
    "fog": true
  }
}
```

Every section is optional. `name` defaults to the file name, and unknown keys are
ignored so a preset written for a newer build still loads.

### `model`

| Key | Default | Meaning |
|---|---|---|
| `triangle_budget_per_character` | 0 (unlimited) | drives the build report budget check |
| `triangle_budget_per_scene` | 0 | informational, for the scene inspector |
| `max_bones_per_mesh` | 256 | skinning limit; the format cap is 4096 |
| `generate_lods` | false | whether importers emit a LOD chain by default |
| `lod_count` | 1 | levels including LOD0 (1-6) |
| `lod_triangle_ratios` | `[1.0]` | target ratio per level |
| `bake_vertex_lighting` | false | implies baked lighting only |
| `weld_vertices` | true | merge identical position/normal/uv |

### `texture`

`max_size` (0 = source), `pixel_format` (`rgba8`, `rgb8`, `r8`, `rgba4444`,
`rgb565`), `generate_mips`, `sampler` (`linear`/`nearest`), `wrap`
(`repeat`/`clamp`/`mirror`), `atlas_max_size`.

### `material`

`shader_preset` (a name the material importer resolves), `alpha_cutoff`,
`allow_per_pixel_lighting`. Shader presets are strings, never enum values:
adding a look is a data change.

### `audio`

`sample_rate`, `channels`, `codec` (`pcm16`, `pcm8`, `adpcm`), `max_voices`.
When a source exceeds the preset's sample rate the WAV importer warns
(`BULLY-AUD-0024`) rather than resampling silently - the Audio Studio shows
"needs resampling" and the artist decides.

### `render`

Target `resolution`, `progressive`, `colour_depth`, `dither`, `fog`. Consumed by
the runtime/exporters; purely informational for import.

## Preset matrix

| | `none` | `ps2` | `xbox` | `unlit-retro` |
|---|---|---|---|---|
| tris / character | unlimited | 3 000 | 8 000 | unlimited |
| max texture | source | 256 | 512 | 128 |
| pixel format | `rgba8` | `rgba4444` | `rgba8` | `rgba8` |
| mipmaps | yes | yes | yes | no |
| sampler | linear | linear | linear | nearest |
| LODs | no | 3 | 3 | 1 |
| baked lighting | no | yes | no | no |
| per-pixel light | yes | no | yes | no |
| shader preset | `standard.lit` | `retro.lambert` | `retro.phong` | `retro.unlit` |
| audio | 48 kHz pcm16 | 32 kHz adpcm | 44.1 kHz pcm16 | 22.05 kHz pcm8 |
| resolution | 1920x1080 | 640x448 | 640x480 | 320x240 |

## Adding your own

Drop `<name>.json` in `<project>/presets/` (or any `$BULLY_PRESET_PATH`
directory), then select it by name. Everything is data - no rebuild, no plugin:

```console
$ cat > myproject/presets/ps1.json <<'EOF'
{ "name": "ps1", "texture": { "max_size": 128, "pixel_format": "rgb565" },
  "model": { "triangle_budget_per_character": 1200, "generate_lods": true } }
EOF
$ bully_asset_cli build --project myproject
```

Only the keys you override change; the rest fall back to the built-in `none`
defaults. Tests covering resolution order, path-traversal refusal, environment
override and project propagation live in `tests/pipeline/test_presets.cpp`.
