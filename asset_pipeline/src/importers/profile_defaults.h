// Shared helpers for building retro/technical profiles from import settings.
//
// Retro presets are data, not hardcoded behaviour: they are defined in
// assets/presets/*.json and can be edited by users or supplied by plugins. The
// values below are the documented defaults used when a preset file is absent;
// they mirror docs/retro-presets.md.
#pragma once

#include <string>
#include <vector>

#include "bully/formats/model.h"
#include "bully/pipeline/asset.h"

namespace bully::pipeline {

/// Technical characteristics of early-2000s hardware. Ships as editable data in
/// assets/presets/ps2.json and assets/presets/xbox.json - these constants are
/// only the fallback used when no preset file is available.
struct RetroProfileData {
    std::string name;
    // model budgets
    std::uint32_t triangle_budget_per_character = 0;
    std::uint32_t max_texture_size = 0;
    bool generate_lods = false;
    std::uint32_t lod_count = 1;
    std::vector<float> lod_triangle_ratios;
    bool bake_vertex_lighting = false;
    bool weld_vertices = true;
    // texture defaults
    std::string pixel_format = "rgba8";
    std::string sampler = "linear";
    std::string wrap_mode = "repeat";
    bool generate_mips = true;
    // material defaults
    std::string shader_preset = "retro.lambert";
    bool allow_per_pixel_lighting = false;
    // audio defaults
    std::uint32_t audio_sample_rate = 44100;
    std::uint16_t audio_channels = 2;
    std::string audio_codec = "pcm16";
    // legacy fields kept for the importer settings that still read them
    std::string default_filter = "linear";
    std::string vertex_format = "rgba8";
    bool allow_vertex_colors = true;
    bool baked_lighting_only = false;
    float simplify_ratio = 1.0f;
    /// Absolute path of the preset file that was loaded (empty for built-ins).
    std::string source_path;
};

[[nodiscard]] RetroProfileData retro_profile(std::string_view preset_name);

/// Absolute paths searched for `<preset>.json`, in priority order. Exposed so
/// the CLI can print its preset search path.
[[nodiscard]] std::vector<std::string> preset_search_paths();

/// Builds a ModelBuildProfile from ImportSettings ("generate_lods",
/// "lod_count", "retro_preset", "target_triangles", "max_texture_size").
[[nodiscard]] formats::ModelBuildProfile build_profile_from_settings(const ImportSettings& settings);

/// Applies a cheap, real ambient-occlusion approximation: per-vertex AO from the
/// normal's upward component and the vertex's depth relative to the mesh bounds.
void apply_vertex_ao(formats::ModelDocument& document);

}  // namespace bully::pipeline
