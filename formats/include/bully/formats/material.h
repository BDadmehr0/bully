// Bully Studio - .bmaterial : material + shader parameter set.
//
// v1.0 chunks:
//   INFO  identity, shader preset id, blend/depth/cull states
//   TEXS  texture slot bindings (diffuse/normal/specular/alpha/emissive/...)
//   PARM  named shader parameters (float/vec3/vec4/color/bool)
//   META  import settings + report (JSON)
//
// Materials never embed pixels: they reference .btexture asset ids, which keeps
// re-texturing cheap and lets the pipeline re-pack textures without touching
// material files.
//
// Diagnostic codes: BULLY-MAT-0001..0019
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/formats/binary.h"

namespace bully::formats {

inline constexpr FormatDescriptor kMaterialFormat{
    fourcc("BMAT"), "bmaterial", {1, 0, 0}, ".bmaterial",
    "Material: shader preset, texture slots, named parameters, render states"};

enum class ParamType : std::uint8_t { Float = 0, Vec2 = 1, Vec3 = 2, Vec4 = 3, Color = 4, Bool = 5, Int = 6 };

struct MaterialParameter {
    std::string name;
    ParamType type = ParamType::Float;
    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

enum class TextureSlot : std::uint8_t {
    Diffuse = 0,
    Normal = 1,
    Specular = 2,
    Alpha = 3,
    Emissive = 4,
    AmbientOcclusion = 5,
    Custom0 = 16,
};

struct TextureBinding {
    TextureSlot slot = TextureSlot::Diffuse;
    std::string texture_asset;
    std::string sampler_override;  // e.g. "nearest" for retro looks
    float uv_scale_u = 1.0f;
    float uv_scale_v = 1.0f;
};

enum class BlendMode : std::uint8_t { Opaque = 0, AlphaTest = 1, AlphaBlend = 2, Additive = 3 };
enum class CullMode : std::uint8_t { Back = 0, Front = 1, None = 2 };

struct RenderState {
    BlendMode blend = BlendMode::Opaque;
    CullMode cull = CullMode::Back;
    bool depth_test = true;
    bool depth_write = true;
    float alpha_cutoff = 0.5f;
};

struct MaterialDocument {
    std::string asset_id;
    std::string name;
    /// Shader preset identity, resolved through the preset registry (see
    /// docs/retro-presets.md). Presets are data, never hardcoded enum values.
    std::string shader_preset = "retro.unlit";
    std::string parent_material;  // optional inheritance chain
    RenderState state;
    std::vector<TextureBinding> textures;
    std::vector<MaterialParameter> parameters;
    std::string import_settings_json;
    std::string notes;

    [[nodiscard]] const MaterialParameter* find_parameter(std::string_view name) const;
    [[nodiscard]] MaterialParameter& set_parameter(std::string name, ParamType type);
    [[nodiscard]] const TextureBinding* find_texture(TextureSlot slot) const;
    void bind_texture(TextureSlot slot, std::string texture_asset, std::string sampler = {});

    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    [[nodiscard]] core::Result<MaterialDocument> deserialize(const std::vector<std::uint8_t>& bytes) const;
    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] std::string to_json() const;
};

[[nodiscard]] std::string_view to_string(TextureSlot slot) noexcept;
[[nodiscard]] std::string_view to_string(BlendMode mode) noexcept;

}  // namespace bully::formats
