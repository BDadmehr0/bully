// Registration of the importer set that ships with Bully Studio.
//
// This is the *only* place that knows the full built-in importer list. The
// pipeline kernel, the CLI and the Studio all interact with importers through
// ImporterRegistry, which is also how plugins add their own.
#include "bully/pipeline/builtin_importers.h"

namespace bully::pipeline {

void register_builtin_importers(ImporterRegistry& registry)
{
    registry.add(make_obj_importer());
    registry.add(make_glb_importer());
    registry.add(make_gltf_importer());
    registry.add(make_fbx_bridge_importer());
    registry.add(make_image_importer());
    registry.add(make_wav_importer());
    registry.add(make_material_importer());
    registry.add(make_animation_importer());
    registry.add(make_scene_importer());
    registry.add(make_mission_importer());
    registry.add(make_dialogue_importer());
}

}  // namespace bully::pipeline
