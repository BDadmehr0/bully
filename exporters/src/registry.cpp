// Exporter registry + the file-level dispatcher used by the CLI and Studio.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bully/core/fs.h"
#include "bully/core/strings.h"
#include "bully/exporters/exporters.h"

namespace bully::exporters {

namespace {

std::string lower_extension(const std::filesystem::path& path)
{
    return core::to_lower(path.extension().string());
}

/// Writes `bytes` to a temporary file next to `destination` and renames it, so a
/// failed export never leaves a half-written file that looks valid.
core::Status write_atomic(const std::filesystem::path& destination,
                          const std::vector<std::uint8_t>& bytes)
{
    core::DiagnosticList diagnostics;
    const auto temporary = destination.string() + ".tmp";
    auto status = core::FileSystem::write_binary(temporary, bytes);
    if (!status) return status;
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        diagnostics.error("BULLY-EXP-0030",
                          "cannot move the exported file into place: " + error.message(),
                          destination.string());
        return core::Status(std::move(diagnostics));
    }
    return core::Status{};
}

core::Status write_atomic(const std::filesystem::path& destination, const std::string& text)
{
    return write_atomic(destination,
                        std::vector<std::uint8_t>(text.begin(), text.end()));
}

}  // namespace

const std::vector<ExporterInfo>& exporters()
{
    static const std::vector<ExporterInfo> list = {
        {"com.bully.exporter.png", "PNG image", ".png", "texture"},
        {"com.bully.exporter.ppm", "PPM image (debug)", ".ppm", "texture"},
        {"com.bully.exporter.wav", "WAV audio (16-bit PCM)", ".wav", "audio"},
        {"com.bully.exporter.obj", "Wavefront OBJ + MTL", ".obj", "model"},
        {"com.bully.exporter.scene_json", "Scene source JSON", ".scene.json", "scene"},
        {"com.bully.exporter.mission_json", "Mission source JSON", ".mission.json", "mission"},
        {"com.bully.exporter.dialogue_json", "Dialogue source JSON", ".dialogue.json",
         "dialogue"},
        {"com.bully.exporter.anim_json", "Animation source JSON", ".banim.json", "animation"},
        {"com.bully.exporter.material_json", "Material source JSON", ".bmaterial.json",
         "material"},
    };
    return list;
}

const ExporterInfo* exporter_for(std::string_view compiled_extension)
{
    const std::string extension = core::to_lower(compiled_extension);
    std::string kind;
    if (extension == ".btexture") kind = "texture";
    else if (extension == ".baudio") kind = "audio";
    else if (extension == ".bmodel") kind = "model";
    else if (extension == ".bscene") kind = "scene";
    else if (extension == ".bmission") kind = "mission";
    else if (extension == ".bdialogue") kind = "dialogue";
    else if (extension == ".banim") kind = "animation";
    else if (extension == ".bmaterial") kind = "material";
    if (kind.empty()) return nullptr;
    for (const auto& info : exporters()) {
        if (info.asset_kind == kind) return &info;
    }
    return nullptr;
}

core::Status export_file(const std::filesystem::path& compiled_asset,
                         const std::filesystem::path& destination)
{
    core::DiagnosticList diagnostics;
    auto bytes = core::FileSystem::read_binary(compiled_asset);
    if (!bytes) return core::Status(bytes.diagnostics());
    const std::string source_extension = lower_extension(compiled_asset);
    const std::string target_extension = lower_extension(destination);
    const auto& payload = bytes.value();

    // Composite extensions (".scene.json") are matched first.
    const std::string destination_text = core::to_lower(destination.filename().string());
    auto ends_with = [&](std::string_view suffix) {
        return destination_text.size() >= suffix.size() &&
               destination_text.compare(destination_text.size() - suffix.size(), suffix.size(),
                                        suffix) == 0;
    };

    if (source_extension == ".btexture") {
        formats::TextureDocument texture;
        auto decoded = texture.deserialize(payload);
        if (!decoded) return core::Status(decoded.diagnostics());
        auto rgba = to_rgba(decoded.value());
        if (!rgba) return core::Status(rgba.diagnostics());
        if (target_extension == ".png") {
            auto encoded = encode_png(rgba.value());
            if (!encoded) return core::Status(encoded.diagnostics());
            return write_atomic(destination, encoded.value());
        }
        if (target_extension == ".ppm") {
            return write_atomic(destination, encode_ppm(rgba.value()));
        }
        diagnostics.error("BULLY-EXP-0031",
                          "no texture exporter for '" + target_extension +
                              "' (use .png or .ppm)",
                          destination.string());
        return core::Status(std::move(diagnostics));
    }

    if (source_extension == ".baudio") {
        formats::AudioDocument audio;
        auto decoded = audio.deserialize(payload);
        if (!decoded) return core::Status(decoded.diagnostics());
        if (target_extension != ".wav") {
            diagnostics.error("BULLY-EXP-0032",
                              "no audio exporter for '" + target_extension + "' (use .wav)",
                              destination.string());
            return core::Status(std::move(diagnostics));
        }
        auto encoded = encode_wav(decoded.value());
        if (!encoded) return core::Status(encoded.diagnostics());
        return write_atomic(destination, encoded.value());
    }

    if (source_extension == ".bmodel") {
        formats::ModelDocument model;
        auto decoded = model.deserialize(payload);
        if (!decoded) return core::Status(decoded.diagnostics());
        if (target_extension != ".obj") {
            diagnostics.error("BULLY-EXP-0033",
                              "no model exporter for '" + target_extension + "' (use .obj)",
                              destination.string());
            return core::Status(std::move(diagnostics));
        }
        auto exported = encode_obj(decoded.value());
        if (!exported) return core::Status(exported.diagnostics());
        auto status = write_atomic(destination, exported.value().obj);
        if (!status) return status;
        if (!exported.value().mtl.empty()) {
            const auto mtl_path = destination.parent_path() / exported.value().material_library;
            status = write_atomic(mtl_path, exported.value().mtl);
            if (!status) return status;
        }
        return core::Status{}.attach_diagnostics(exported.diagnostics());
    }

    // Authored-content formats export to their source JSON shape.
    std::string text;
    if (source_extension == ".bscene") {
        formats::SceneDocument scene;
        auto decoded = scene.deserialize(payload);
        if (!decoded) return core::Status(decoded.diagnostics());
        text = encode_scene_json(decoded.value());
    } else if (source_extension == ".bmission") {
        formats::MissionDocument mission;
        auto decoded = mission.deserialize(payload);
        if (!decoded) return core::Status(decoded.diagnostics());
        text = encode_mission_json(decoded.value());
    } else if (source_extension == ".bdialogue") {
        formats::DialogueDocument dialogue;
        auto decoded = dialogue.deserialize(payload);
        if (!decoded) return core::Status(decoded.diagnostics());
        text = encode_dialogue_json(decoded.value());
    } else if (source_extension == ".banim") {
        formats::AnimationDocument animation;
        auto decoded = animation.deserialize(payload);
        if (!decoded) return core::Status(decoded.diagnostics());
        text = encode_animation_json(decoded.value());
    } else if (source_extension == ".bmaterial") {
        formats::MaterialDocument material;
        auto decoded = material.deserialize(payload);
        if (!decoded) return core::Status(decoded.diagnostics());
        text = encode_material_json(decoded.value());
    } else {
        diagnostics.error("BULLY-EXP-0034",
                          "no exporter for compiled format '" + source_extension + "'",
                          compiled_asset.string());
        return core::Status(std::move(diagnostics));
    }

    if (!ends_with(".json")) {
        diagnostics.error("BULLY-EXP-0035",
                          "authored-content exports must target a .json file, got '" +
                              target_extension + "'",
                          destination.string());
        return core::Status(std::move(diagnostics));
    }
    return write_atomic(destination, text);
}

}  // namespace bully::exporters
