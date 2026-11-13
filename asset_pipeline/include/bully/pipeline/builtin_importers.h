// Bully Studio - importer implementations shipped with the platform.
//
// Each importer is independently constructible so tests can exercise it in
// isolation, and each documents the source formats it accepts. Importers are
// registered into the ImporterRegistry at startup; nothing in the pipeline
// kernel knows their names.
#pragma once

#include <memory>

#include "bully/pipeline/importer.h"

namespace bully::pipeline {

class ImporterRegistry;

/// Registers every built-in importer into `registry`.
void register_builtin_importers(ImporterRegistry& registry);

// --- model importers -------------------------------------------------------
/// Wavefront OBJ + MTL -> .bmodel (parses geometry, materials, texture deps).
std::shared_ptr<Importer> make_obj_importer();
/// glTF 2.0 binary (.glb) -> .bmodel. Pure glTF nodes, meshes, skins, materials.
std::shared_ptr<Importer> make_glb_importer();
/// glTF 2.0 JSON (.gltf) with optional external buffers/images -> .bmodel.
std::shared_ptr<Importer> make_gltf_importer();
/// FBX is handled by an external conversion step; this importer drives the
/// configurable DCC bridge and reports a clear diagnostic when no converter
/// is configured (instead of pretending to parse FBX in-process).
std::shared_ptr<Importer> make_fbx_bridge_importer();

// --- texture importers -----------------------------------------------------
/// PNG / TGA / PPM / BMP (uncompressed) decoder -> .btexture, with optional
/// nearest/bilinear downscale, mip generation and retro quantisation.
std::shared_ptr<Importer> make_image_importer();

// --- audio importers -------------------------------------------------------
/// WAV (PCM 8/16/24/32) -> .baudio with embedded PCM or reference mode.
std::shared_ptr<Importer> make_wav_importer();

// --- data importers --------------------------------------------------------
/// .dialogue.json (the structured narrative format) -> .bdialogue.
std::shared_ptr<Importer> make_dialogue_importer();
/// .scene.json authored by the level editor -> .bscene.
std::shared_ptr<Importer> make_scene_importer();
/// .mission.json authored by the mission editor -> .bmission.
std::shared_ptr<Importer> make_mission_importer();
/// .bmaterial.json authored by the material studio -> .bmaterial.
std::shared_ptr<Importer> make_material_importer();
/// .banim.json authored by the animation studio -> .banim.
std::shared_ptr<Importer> make_animation_importer();

}  // namespace bully::pipeline
