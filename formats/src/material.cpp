#include "bully/formats/material.h"

#include <algorithm>

#include "bully/core/json.h"

namespace bully::formats {

namespace {

constexpr std::uint32_t kChunkInfo = fourcc("INFO");
constexpr std::uint32_t kChunkTexs = fourcc("TEXS");
constexpr std::uint32_t kChunkParm = fourcc("PARM");
constexpr std::uint32_t kChunkMeta = fourcc("META");

}  // namespace

std::string_view to_string(TextureSlot slot) noexcept
{
    switch (slot) {
        case TextureSlot::Diffuse: return "diffuse";
        case TextureSlot::Normal: return "normal";
        case TextureSlot::Specular: return "specular";
        case TextureSlot::Alpha: return "alpha";
        case TextureSlot::Emissive: return "emissive";
        case TextureSlot::AmbientOcclusion: return "ambient_occlusion";
        default: return "custom";
    }
}

std::string_view to_string(BlendMode mode) noexcept
{
    switch (mode) {
        case BlendMode::Opaque: return "opaque";
        case BlendMode::AlphaTest: return "alpha_test";
        case BlendMode::AlphaBlend: return "alpha_blend";
        case BlendMode::Additive: return "additive";
    }
    return "opaque";
}

const MaterialParameter* MaterialDocument::find_parameter(std::string_view param_name) const
{
    for (const auto& parameter : parameters) {
        if (parameter.name == param_name) return &parameter;
    }
    return nullptr;
}

MaterialParameter& MaterialDocument::set_parameter(std::string param_name, ParamType type)
{
    for (auto& parameter : parameters) {
        if (parameter.name == param_name) {
            parameter.type = type;
            return parameter;
        }
    }
    MaterialParameter parameter;
    parameter.name = std::move(param_name);
    parameter.type = type;
    parameters.push_back(std::move(parameter));
    return parameters.back();
}

const TextureBinding* MaterialDocument::find_texture(TextureSlot slot) const
{
    for (const auto& binding : textures) {
        if (binding.slot == slot) return &binding;
    }
    return nullptr;
}

void MaterialDocument::bind_texture(TextureSlot slot, std::string texture_asset, std::string sampler)
{
    for (auto& binding : textures) {
        if (binding.slot == slot) {
            binding.texture_asset = std::move(texture_asset);
            binding.sampler_override = std::move(sampler);
            return;
        }
    }
    TextureBinding binding;
    binding.slot = slot;
    binding.texture_asset = std::move(texture_asset);
    binding.sampler_override = std::move(sampler);
    textures.push_back(std::move(binding));
}

std::vector<std::uint8_t> MaterialDocument::serialize() const
{
    BinaryWriter w;
    w.chunk(kChunkInfo, [&](BinaryWriter& body) {
        body.string(asset_id);
        body.string(name);
        body.string(shader_preset);
        body.string(parent_material);
        body.u8(static_cast<std::uint8_t>(state.blend));
        body.u8(static_cast<std::uint8_t>(state.cull));
        body.boolean(state.depth_test);
        body.boolean(state.depth_write);
        body.f32(state.alpha_cutoff);
    });
    w.chunk(kChunkTexs, [&](BinaryWriter& body) {
        body.u32(static_cast<std::uint32_t>(textures.size()));
        for (const auto& binding : textures) {
            body.u8(static_cast<std::uint8_t>(binding.slot));
            body.string(binding.texture_asset);
            body.string(binding.sampler_override);
            body.f32(binding.uv_scale_u);
            body.f32(binding.uv_scale_v);
        }
    });
    w.chunk(kChunkParm, [&](BinaryWriter& body) {
        body.u32(static_cast<std::uint32_t>(parameters.size()));
        for (const auto& parameter : parameters) {
            body.string(parameter.name);
            body.u8(static_cast<std::uint8_t>(parameter.type));
            for (float value : parameter.values) body.f32(value);
        }
    });
    w.chunk(kChunkMeta, [&](BinaryWriter& body) {
        body.string(import_settings_json);
        body.string(notes);
    });
    return encode_document(kMaterialFormat, 0, w.bytes());
}

core::Result<MaterialDocument> MaterialDocument::deserialize(
    const std::vector<std::uint8_t>& bytes) const
{
    core::DiagnosticList diagnostics;
    auto payload = decode_document(kMaterialFormat, bytes);
    if (!payload) return core::Result<MaterialDocument>(payload.diagnostics());
    diagnostics.append(payload.diagnostics());

    MaterialDocument document;
    BinaryReader reader(payload.value().data(), payload.value().size(), &diagnostics);
    while (reader.ok()) {
        std::uint32_t chunk_id = 0;
        std::uint32_t chunk_size = 0;
        if (!reader.next_chunk(chunk_id, chunk_size)) break;
        const std::size_t chunk_end = reader.position() + chunk_size;
        if (chunk_id == kChunkInfo) {
            document.asset_id = reader.string();
            document.name = reader.string();
            document.shader_preset = reader.string();
            document.parent_material = reader.string();
            document.state.blend = static_cast<BlendMode>(reader.u8());
            document.state.cull = static_cast<CullMode>(reader.u8());
            document.state.depth_test = reader.boolean();
            document.state.depth_write = reader.boolean();
            document.state.alpha_cutoff = reader.f32();
        } else if (chunk_id == kChunkTexs) {
            const std::uint32_t count = reader.u32();
            document.textures.resize(std::min<std::uint32_t>(count, 256));
            for (auto& binding : document.textures) {
                binding.slot = static_cast<TextureSlot>(reader.u8());
                binding.texture_asset = reader.string();
                binding.sampler_override = reader.string();
                binding.uv_scale_u = reader.f32();
                binding.uv_scale_v = reader.f32();
            }
        } else if (chunk_id == kChunkParm) {
            const std::uint32_t count = reader.u32();
            document.parameters.resize(std::min<std::uint32_t>(count, 1024));
            for (auto& parameter : document.parameters) {
                parameter.name = reader.string();
                parameter.type = static_cast<ParamType>(reader.u8());
                for (float& value : parameter.values) value = reader.f32();
            }
        } else if (chunk_id == kChunkMeta) {
            document.import_settings_json = reader.string();
            document.notes = reader.string();
        } else {
            diagnostics.info("BULLY-MAT-0001",
                             "skipping unknown material chunk '" + fourcc_to_string(chunk_id) + "'");
        }
        if (reader.ok()) reader.seek(chunk_end);
    }
    if (!reader.ok() || diagnostics.has_errors()) {
        if (diagnostics.empty()) diagnostics.error("BULLY-MAT-0002", "failed to decode material");
        return core::Result<MaterialDocument>(std::move(diagnostics));
    }
    auto decoded = core::Result<MaterialDocument>(std::move(document));
    // Warnings collected while decoding (unknown chunks, newer minor versions)
    // travel with the successful result so callers can surface them.
    decoded.attach_diagnostics(std::move(diagnostics));
    return decoded;
}

core::Status MaterialDocument::validate() const
{
    core::DiagnosticList diagnostics;
    if (asset_id.empty()) diagnostics.error("BULLY-MAT-0003", "material has no asset id");
    if (shader_preset.empty()) {
        diagnostics.warn("BULLY-MAT-0004",
                         "material '" + name + "' has no shader preset; the runtime falls back to "
                         "'retro.unlit'",
                         name);
    }
    for (const auto& binding : textures) {
        if (binding.texture_asset.empty()) {
            diagnostics.warn("BULLY-MAT-0005",
                             "texture slot '" + std::string(to_string(binding.slot)) +
                                 "' is bound to an empty texture id",
                             name);
        }
    }
    if (state.blend == BlendMode::AlphaTest && (state.alpha_cutoff < 0.0f || state.alpha_cutoff > 1.0f)) {
        diagnostics.error("BULLY-MAT-0006", "alpha cutoff must be within [0,1]", name);
    }
    for (const auto& parameter : parameters) {
        if (parameter.name.empty()) {
            diagnostics.error("BULLY-MAT-0007", "material contains an unnamed parameter", name);
        }
    }
    if (diagnostics.has_errors()) return core::Status(std::move(diagnostics));
    // Warnings are part of the report even when the document is valid.
    auto status = core::Status{};
    status.attach_diagnostics(std::move(diagnostics));
    return status;
}

std::string MaterialDocument::to_json() const
{
    using core::json::Value;
    Value root = Value::make_object();
    root["format"] = "bmaterial";
    root["version"] = kMaterialFormat.version.to_string();
    root["asset_id"] = asset_id;
    root["name"] = name;
    root["shader_preset"] = shader_preset;
    root["parent"] = parent_material;
    root["blend"] = std::string(to_string(state.blend));
    root["depth_test"] = state.depth_test;
    root["depth_write"] = state.depth_write;
    root["alpha_cutoff"] = static_cast<double>(state.alpha_cutoff);

    Value textures_json = Value::make_array();
    for (const auto& binding : textures) {
        Value entry = Value::make_object();
        entry["slot"] = std::string(to_string(binding.slot));
        entry["texture"] = binding.texture_asset;
        entry["sampler"] = binding.sampler_override;
        textures_json.push(std::move(entry));
    }
    root["textures"] = std::move(textures_json);

    Value params_json = Value::make_array();
    for (const auto& parameter : parameters) {
        Value entry = Value::make_object();
        entry["name"] = parameter.name;
        entry["type"] = static_cast<std::int64_t>(parameter.type);
        Value values = Value::make_array();
        for (float value : parameter.values) values.push(static_cast<double>(value));
        entry["values"] = std::move(values);
        params_json.push(std::move(entry));
    }
    root["parameters"] = std::move(params_json);
    root["valid"] = validate().has_value();
    return root.dump(2);
}

}  // namespace bully::formats
