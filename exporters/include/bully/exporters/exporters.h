// Bully Studio - exporters (compiled asset -> external format).
//
// Exporters are the mirror image of importers and exist for three concrete
// jobs, not for completeness:
//
//   * **round-trip verification** - re-exporting a compiled asset and
//     re-importing it proves the format carries everything it claims to,
//   * **hand-off** - geometry and textures leave the pipeline for DCC tools,
//     audio leaves for a DAW or a dubbing studio,
//   * **QA** - the `asset.outputs` rule can prove an asset survives a full
//     export/import cycle.
//
// Every exporter is deterministic (same input bytes out) and reports failures
// with diagnostics instead of returning a half-written file.
//
// Diagnostic codes: BULLY-EXP-0001..0029
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/formats/animation.h"
#include "bully/formats/audio.h"
#include "bully/formats/dialogue.h"
#include "bully/formats/material.h"
#include "bully/formats/mission.h"
#include "bully/formats/model.h"
#include "bully/formats/scene.h"
#include "bully/formats/texture.h"

namespace bully::exporters {

/// Pixel data normalised to RGBA8 for the encoders.
struct RgbaImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;  // width * height * 4

    [[nodiscard]] bool valid() const
    {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<std::size_t>(width) * height * 4;
    }
};

/// Denormalises the first mip level of a compiled texture. Fails for block
/// formats (`BULLY-EXP-0002`) rather than writing garbage - BC decoding is not
/// implemented, and pretending otherwise would corrupt the user's data.
[[nodiscard]] core::Result<RgbaImage> to_rgba(const formats::TextureDocument& texture);

/// RGBA8 -> PNG (colour type 6, stored/uncompressed deflate blocks).
[[nodiscard]] core::Result<std::vector<std::uint8_t>> encode_png(const RgbaImage& image);

/// RGBA8 -> binary PPM (P6). Lossy for alpha, exact for RGB.
[[nodiscard]] std::vector<std::uint8_t> encode_ppm(const RgbaImage& image);

/// 16-bit PCM -> RIFF/WAVE.
[[nodiscard]] core::Result<std::vector<std::uint8_t>> encode_wav(
    const formats::AudioDocument& audio);

/// Model -> Wavefront OBJ (+ companion MTL when materials exist).
struct ObjExport {
    std::string obj;
    std::string mtl;          // empty when the model has no materials
    std::string material_library;  // file name referenced by mtllib
};

[[nodiscard]] core::Result<ObjExport> encode_obj(const formats::ModelDocument& model);

/// Scene -> human-readable JSON (entities, lights, triggers, spawns).
[[nodiscard]] std::string encode_scene_json(const formats::SceneDocument& scene);

/// Mission -> human-readable JSON (objectives, nodes, edges, conditions).
[[nodiscard]] std::string encode_mission_json(const formats::MissionDocument& mission);

/// Dialogue -> authoring JSON (characters, lines, takes, nodes).
[[nodiscard]] std::string encode_dialogue_json(const formats::DialogueDocument& dialogue);

/// Animation -> authoring JSON (tracks, events, root motion).
[[nodiscard]] std::string encode_animation_json(const formats::AnimationDocument& animation);

/// Material -> authoring JSON (shader preset, slots, parameters).
[[nodiscard]] std::string encode_material_json(const formats::MaterialDocument& material);

/// The exporter registry the CLI and the Studio list.
struct ExporterInfo {
    std::string id;          // "com.bully.exporter.png"
    std::string display_name;
    std::string extension;   // ".png"
    std::string asset_kind;  // "texture"
};

[[nodiscard]] const std::vector<ExporterInfo>& exporters();
/// Picks an exporter for a compiled asset by extension.
[[nodiscard]] const ExporterInfo* exporter_for(std::string_view compiled_extension);

/// Exports a compiled file to `destination`, choosing the format from the
/// destination extension. Writes atomically (temp file + rename).
[[nodiscard]] core::Status export_file(const std::filesystem::path& compiled_asset,
                                       const std::filesystem::path& destination);

}  // namespace bully::exporters
