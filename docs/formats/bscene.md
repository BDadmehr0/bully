# `.bscene` - scene / level

Magic `BSCN`, version **1.0**. Authored as `*.scene.json` by the World/Level
Editor and compiled by `com.bully.importer.scene_json`.

| Chunk | Body |
|---|---|
| `INFO` | identity, environment settings, entity count |
| `ENT` | one chunk per entity: transform, parent, component list |
| `LITE` | placed lights |
| `TRIG` | trigger volumes (mission, cutscene, streaming, audio) |
| `NAVM` | navigation data (nav graph nodes or navmesh triangles) |
| `SPAW` | spawn points and camera markers |
| `META` | authoring notes, editor camera state |

## Components are extensible

`EntityComponent{type, data_json}`. `type` is namespaced (`bully.render.mesh`,
`studio.plugin.my_component`) and the JSON payload belongs to the component's
registered serializer. A plugin can therefore add component kinds **without a
format revision**: unknown types travel through the pipeline untouched.

## Enemy/entity references

`model_asset` and `prefab` hold asset ids. The `scene.references` QA rule warns
when a referenced model is neither registered nor an existing project path.

## Validation

Duplicate entity ids, self-parents, missing parents, hierarchy cycles and
malformed component JSON are errors (`BULLY-SCENE-0004`..`0010`); untyped
components, duplicate light ids, negative intensities and dangling nav links are
warnings.
