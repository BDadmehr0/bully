# `.bmodel` - compiled 3D model

Magic `BMOD`, version **1.0**, produced by the OBJ / glTF / FBX-bridge importers.

| Chunk | Body | Notes |
|---|---|---|
| `INFO` | identity, bounds, build profile, statistics | required, exactly one |
| `MTRL` | material binding (asset id, slot name, shader preset) | one per slot |
| `MESH` | vertices, indices, LOD, material slot | one per mesh/LOD |
| `SKEL` | bone table with inverse bind matrices | optional |
| `ANIM` | animation asset ids targeting this rig | optional |
| `COLL` | collision shapes | optional |
| `LITE` | baked per-vertex ambient occlusion | optional |
| `META` | importer settings + build report JSON | optional |

A missing `SKEL`/`COLL`/`LITE` chunk means "none": that is a documented default,
not an error.

## INFO body

```
string asset_id, name, source_path, source_format, importer, importer_version
Aabb   bounds                      (min.xyz, max.xyz)
u32    declared_mesh_count, declared_material_count
string profile.preset_name         ("none", "ps2", "xbox", ...)
u32    profile.target_triangles, profile.max_texture_size
bool   profile.generate_lods
u32    profile.lod_count
bool   profile.bake_vertex_lighting, profile.weld_vertices
f32    profile.simplify_ratio
u32    stored vertex / triangle / bone / draw-call counts
```

The stored counts are informational: readers recompute statistics from `MESH`
chunks, so a truncated file cannot inflate reported budgets.

## MESH body

```
string  name
u8      lod
u8      topology       0 triangles, 1 triangle strip, 2 lines, 3 points
u16     material_slot  0xFFFF = no material
u32     vertex_flags   bitmask of position|normal|tangent|uv0|uv1|colour|skin
Aabb    bounds
u32     vertex_count, then per vertex:
          Vec3f position, Vec3f normal, Vec4f tangent, Vec2f uv0, Vec2f uv1,
          Vec4f colour, Vec4f skin_weights, u16[4] joints, f32 ambient_occlusion
u32     index_count, then u32 indices
```

## Compatibility

* Unknown chunks are skipped and reported as `BULLY-MODEL-0005` (info); the
  reader resynchronises on the declared chunk boundary.
* A payload written by a newer minor version decodes and returns
  `BULLY-BIN-0010` as a warning attached to the successful result.
* A different major version is refused (`BULLY-BIN-0009`).

## Validation (`ModelDocument::validate`)

| Code | Meaning |
|---|---|
| `BULLY-MODEL-0006` | missing asset id |
| `BULLY-MODEL-0020` | skeleton exceeds 4096 bones |
| `BULLY-MODEL-0021` | bone parent index out of range |
| `BULLY-MODEL-0022` | bone is its own parent |
| `BULLY-MODEL-0023` | bone hierarchy cycle |

Index bounds are checked against the vertex count of the same mesh; errors make
the build fail instead of producing a model that would crash the runtime.
