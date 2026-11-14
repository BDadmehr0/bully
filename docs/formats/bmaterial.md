# `.bmaterial` - material and shader parameters

Magic `BMAT`, version **1.0**, produced by the material importer and the
Material/Texture Studio.

| Chunk | Body |
|---|---|
| `INFO` | identity, shader preset id, parent material, render state |
| `TEXS` | texture slot bindings |
| `PARM` | named parameters (float/vec2/vec3/vec4/colour/bool/int) |
| `META` | import settings + notes |

Materials never embed pixels: they reference `.btexture` **asset ids**, so
re-texturing never touches material files and the packager can re-pack textures
independently.

## Render state

```
u8 blend   0 opaque, 1 alpha test, 2 alpha blend, 3 additive
u8 cull    0 back, 1 front, 2 none
bool depth_test, depth_write
f32  alpha_cutoff
```

## Texture slots

`Diffuse(0)`, `Normal(1)`, `Specular(2)`, `Alpha(3)`, `Emissive(4)`,
`AmbientOcclusion(5)`, `Custom0..(16+)`. Each binding stores the texture asset
id, an optional sampler override (`nearest` for retro looks) and a UV scale.

## Shader presets are data

`shader_preset` is a string resolved through the preset registry
([docs/retro-presets.md](../retro-presets.md)); adding a look never requires a
format revision or a code change.
