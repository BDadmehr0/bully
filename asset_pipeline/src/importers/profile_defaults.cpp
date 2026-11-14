#include "importers/profile_defaults.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bully/core/fs.h"
#include "bully/core/json.h"
#include "bully/core/log.h"
#include "bully/core/strings.h"

namespace bully::pipeline {

namespace {

/// Directories searched for `<preset>.json`, in priority order:
///   1. $BULLY_PRESET_PATH (colon separated) - CI and per-user overrides,
///   2. <cwd>/presets and <cwd>/assets/presets - project-local presets,
///   3. the preset directory of the source tree this binary was built from.
std::vector<std::filesystem::path> preset_search_dirs()
{
    std::vector<std::filesystem::path> dirs;
    if (const char* env = std::getenv("BULLY_PRESET_PATH"); env != nullptr) {
        for (const auto& entry : core::split(env, ':')) {
            if (!entry.empty()) dirs.emplace_back(entry);
        }
    }
    const auto cwd = std::filesystem::current_path();
    dirs.push_back(cwd / "presets");
    dirs.push_back(cwd / "assets" / "presets");
#ifdef BULLY_DEFAULT_PRESET_DIR
    dirs.emplace_back(BULLY_DEFAULT_PRESET_DIR);
#endif
    return dirs;
}

/// Presets are read once and cached: importers ask for the same profile for
/// every asset in a build.
std::optional<RetroProfileData> load_preset_file(std::string_view preset_name)
{
    const std::string lower = core::to_lower(preset_name);
    if (lower.empty() || lower.find('/') != std::string::npos ||
        lower.find("..") != std::string::npos) {
        return std::nullopt;  // never resolve outside the preset directories
    }
    static std::mutex mutex;
    static std::map<std::string, std::optional<RetroProfileData>> cache;
    std::lock_guard<std::mutex> lock(mutex);
    if (const auto found = cache.find(lower); found != cache.end()) return found->second;

    std::optional<RetroProfileData> result;
    for (const auto& directory : preset_search_dirs()) {
        const auto path = directory / (lower + ".json");
        if (!core::FileSystem::exists(path)) continue;
        auto parsed = core::json::load_file(path.string());
        if (!parsed) {
            core::log_warn("presets", "ignoring malformed preset '" + path.string() + "': " +
                                          parsed.diagnostics().format());
            continue;
        }
        const auto& root = parsed.value();
        RetroProfileData profile;
        profile.name = root.get_string("name", lower);
        if (const core::json::Value* model = root.find("model"); model != nullptr) {
            profile.triangle_budget_per_character = static_cast<std::uint32_t>(
                std::max<std::int64_t>(0, model->get_int("triangle_budget_per_character", 0)));
            profile.generate_lods = model->get_bool("generate_lods", false);
            profile.lod_count = static_cast<std::uint32_t>(
                std::max<std::int64_t>(1, model->get_int("lod_count", 1)));
            profile.lod_triangle_ratios.clear();
            if (const core::json::Value* ratios = model->find("lod_triangle_ratios");
                ratios != nullptr && ratios->is_array()) {
                for (const auto& ratio : ratios->items()) {
                    profile.lod_triangle_ratios.push_back(static_cast<float>(ratio.as_number(1.0)));
                }
            }
            profile.bake_vertex_lighting = model->get_bool("bake_vertex_lighting", false);
            profile.weld_vertices = model->get_bool("weld_vertices", true);
        }
        if (const core::json::Value* texture = root.find("texture"); texture != nullptr) {
            profile.max_texture_size = static_cast<std::uint32_t>(
                std::max<std::int64_t>(0, texture->get_int("max_size", 0)));
            profile.pixel_format = texture->get_string("pixel_format", "rgba8");
            profile.generate_mips = texture->get_bool("generate_mips", true);
            profile.sampler = texture->get_string("sampler", "linear");
            profile.wrap_mode = texture->get_string("wrap", "repeat");
        }
        if (const core::json::Value* material = root.find("material"); material != nullptr) {
            profile.shader_preset = material->get_string("shader_preset", "retro.lambert");
            profile.allow_per_pixel_lighting =
                material->get_bool("allow_per_pixel_lighting", false);
        }
        if (const core::json::Value* audio = root.find("audio"); audio != nullptr) {
            profile.audio_sample_rate = static_cast<std::uint32_t>(
                std::max<std::int64_t>(8000, audio->get_int("sample_rate", 44100)));
            profile.audio_channels =
                static_cast<std::uint16_t>(std::max<std::int64_t>(1, audio->get_int("channels", 2)));
            profile.audio_codec = audio->get_string("codec", "pcm16");
        }
        profile.source_path = path.string();
        result = std::move(profile);
        break;
    }
    cache[lower] = result;
    return result;
}

/// Documented fallback values, mirroring assets/presets/*.json. Used only when
/// no preset file can be found, so a missing data directory degrades to the
/// documented numbers instead of an empty profile.
RetroProfileData builtin_profile(std::string_view preset_name)
{
    const std::string name = core::to_lower(preset_name);
    RetroProfileData profile;
    profile.name = "none";
    if (name == "ps2") {
        profile.name = "ps2";
        profile.triangle_budget_per_character = 3000;
        profile.max_texture_size = 256;
        profile.default_filter = "linear";
        profile.vertex_format = "rgba4444";
        profile.pixel_format = "rgba4444";
        profile.allow_vertex_colors = true;
        profile.baked_lighting_only = true;
        profile.bake_vertex_lighting = true;
        profile.generate_lods = true;
        profile.lod_count = 3;
        profile.shader_preset = "retro.lambert";
        profile.audio_sample_rate = 32000;
        profile.audio_codec = "adpcm";
    } else if (name == "xbox") {
        profile.name = "xbox";
        profile.triangle_budget_per_character = 8000;
        profile.max_texture_size = 512;
        profile.default_filter = "linear";
        profile.vertex_format = "rgba8";
        profile.pixel_format = "rgba8";
        profile.allow_vertex_colors = true;
        profile.baked_lighting_only = false;
        profile.generate_lods = true;
        profile.lod_count = 3;
        profile.shader_preset = "retro.phong";
        profile.allow_per_pixel_lighting = true;
        profile.audio_sample_rate = 44100;
        profile.audio_codec = "pcm16";
    } else if (name == "unlit-retro") {
        profile.name = "unlit-retro";
        profile.triangle_budget_per_character = 0;
        profile.max_texture_size = 128;
        profile.default_filter = "nearest";
        profile.sampler = "nearest";
        profile.wrap_mode = "clamp";
        profile.generate_mips = false;
        profile.pixel_format = "rgba8";
        profile.shader_preset = "retro.unlit";
        profile.audio_sample_rate = 22050;
        profile.audio_channels = 1;
        profile.audio_codec = "pcm8";
    }
    return profile;
}

}  // namespace

std::vector<std::string> preset_search_paths()
{
    std::vector<std::string> paths;
    for (const auto& directory : preset_search_dirs()) paths.push_back(directory.string());
    return paths;
}

RetroProfileData retro_profile(std::string_view preset_name)
{
    if (const auto loaded = load_preset_file(preset_name)) return *loaded;
    RetroProfileData profile = builtin_profile(preset_name);
    if (core::to_lower(preset_name) != "none" && !preset_name.empty()) {
        core::log_debug("presets",
                        "no preset file for '" + std::string(preset_name) +
                            "'; using the documented built-in values");
    }
    return profile;
}

formats::ModelBuildProfile build_profile_from_settings(const ImportSettings& settings)
{
    formats::ModelBuildProfile profile;
    profile.preset_name = settings.get("retro_preset", "none");
    const auto retro = retro_profile(profile.preset_name);
    profile.generate_lods = settings.get_bool("generate_lods", retro.generate_lods);
    profile.lod_count = static_cast<std::uint32_t>(std::max(
        1.0, settings.get_number("lod_count", static_cast<double>(retro.lod_count))));
    profile.max_texture_size = static_cast<std::uint32_t>(std::max(
        0.0, settings.get_number("max_texture_size", static_cast<double>(retro.max_texture_size))));
    profile.target_triangles = static_cast<std::uint32_t>(std::max(
        0.0, settings.get_number("target_triangles",
                                 static_cast<double>(retro.triangle_budget_per_character))));
    profile.simplify_ratio = static_cast<float>(settings.get_number("simplify_ratio", 1.0));
    profile.bake_vertex_lighting = settings.get_bool("bake_vertex_lighting",
                                                     retro.bake_vertex_lighting) ||
                                   retro.baked_lighting_only;
    profile.weld_vertices = settings.get_bool("weld_vertices", retro.weld_vertices);
    return profile;
}

void apply_vertex_ao(formats::ModelDocument& document)
{
    for (auto& mesh : document.meshes) {
        if (mesh.vertices.empty()) continue;
        const float height = std::max(0.001f, mesh.bounds.max.y - mesh.bounds.min.y);
        for (auto& vertex : mesh.vertices) {
            // Upward-facing vertices receive full light; crevices darken.
            const float upward = std::clamp(vertex.normal.y * 0.5f + 0.5f, 0.0f, 1.0f);
            const float normalised_height =
                std::clamp((vertex.position.y - mesh.bounds.min.y) / height, 0.0f, 1.0f);
            const float ao =
                std::clamp(0.35f + 0.45f * upward + 0.20f * normalised_height, 0.0f, 1.0f);
            vertex.ambient_occlusion = ao;
        }
    }
}

}  // namespace bully::pipeline
