// glTF 2.0 importer (.glb binary and .gltf JSON).
//
// Implements the parts of glTF 2.0 that matter for a game asset pipeline:
//   * GLB container parsing (header, JSON chunk, BIN chunk)
//   * buffers / bufferViews / accessors with all component types + strides
//   * nodes -> world transforms (TRS or matrix), meshes with primitives
//   * skins -> Skeleton with inverse bind matrices and joint order
//   * materials -> MaterialBindings with baseColor/normal/metallic-roughness maps
//   * animations -> AnimationReferences (clips are imported by the animation
//     importer, which reuses this parser through the shared gltf module)
// Unsupported extensions are reported as warnings, never silently ignored.
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>

#include "bully/core/fs.h"
#include "bully/core/json.h"
#include "bully/core/strings.h"
#include "bully/pipeline/builtin_importers.h"
#include "importers/model_builder.h"
#include "importers/profile_defaults.h"

namespace bully::pipeline {

namespace {

struct GltfBufferView {
    std::uint32_t buffer = 0;
    std::size_t byte_offset = 0;
    std::size_t byte_length = 0;
    std::size_t byte_stride = 0;
};

struct GltfAccessor {
    std::uint32_t buffer_view = 0;
    std::size_t byte_offset = 0;
    std::uint32_t component_type = 0;
    std::size_t count = 0;
    std::string type;  // SCALAR, VEC2, VEC3, VEC4, MAT4, ...
    bool normalized = false;
};

struct GltfDocument {
    std::vector<std::vector<std::uint8_t>> buffers;
    std::vector<GltfBufferView> buffer_views;
    std::vector<GltfAccessor> accessors;
};

std::size_t component_size(std::uint32_t component_type)
{
    switch (component_type) {
        case 5120: return 1;  // BYTE
        case 5121: return 1;  // UNSIGNED_BYTE
        case 5122: return 2;  // SHORT
        case 5123: return 2;  // UNSIGNED_SHORT
        case 5125: return 4;  // UNSIGNED_INT
        case 5126: return 4;  // FLOAT
        default: return 0;
    }
}

std::size_t component_count(const std::string& type)
{
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT2") return 4;
    if (type == "MAT3") return 9;
    if (type == "MAT4") return 16;
    return 0;
}

float read_component(const std::uint8_t* data, std::uint32_t component_type, bool normalized)
{
    switch (component_type) {
        case 5120: {
            const auto value = static_cast<std::int8_t>(data[0]);
            return normalized ? std::max(-1.0f, static_cast<float>(value) / 127.0f)
                              : static_cast<float>(value);
        }
        case 5121: {
            const std::uint8_t value = data[0];
            return normalized ? static_cast<float>(value) / 255.0f : static_cast<float>(value);
        }
        case 5122: {
            std::int16_t value = 0;
            std::memcpy(&value, data, 2);
            return normalized ? std::max(-1.0f, static_cast<float>(value) / 32767.0f)
                              : static_cast<float>(value);
        }
        case 5123: {
            std::uint16_t value = 0;
            std::memcpy(&value, data, 2);
            return normalized ? static_cast<float>(value) / 65535.0f : static_cast<float>(value);
        }
        case 5125: {
            std::uint32_t value = 0;
            std::memcpy(&value, data, 4);
            return static_cast<float>(value);
        }
        case 5126: {
            float value = 0.0f;
            std::memcpy(&value, data, 4);
            return value;
        }
        default:
            return 0.0f;
    }
}

/// Reads `count` elements of `components` floats from an accessor.
std::vector<float> read_accessor_floats(const GltfDocument& document, std::uint32_t accessor_index,
                                        core::DiagnosticList& diagnostics)
{
    std::vector<float> out;
    if (accessor_index >= document.accessors.size()) {
        diagnostics.error("BULLY-GLTF-0001",
                          "accessor index " + std::to_string(accessor_index) + " is out of range");
        return out;
    }
    const auto& accessor = document.accessors[accessor_index];
    if (accessor.buffer_view >= document.buffer_views.size()) {
        diagnostics.error("BULLY-GLTF-0002", "accessor has no valid bufferView");
        return out;
    }
    const auto& view = document.buffer_views[accessor.buffer_view];
    if (view.buffer >= document.buffers.size()) {
        diagnostics.error("BULLY-GLTF-0003", "bufferView references a missing buffer");
        return out;
    }
    const auto& buffer = document.buffers[view.buffer];
    const std::size_t components = component_count(accessor.type);
    const std::size_t element_size = components * component_size(accessor.component_type);
    const std::size_t stride = view.byte_stride != 0 ? view.byte_stride : element_size;
    if (components == 0 || element_size == 0) {
        diagnostics.error("BULLY-GLTF-0004",
                          "accessor has an unsupported type/component combination");
        return out;
    }
    const std::size_t base = view.byte_offset + accessor.byte_offset;
    if (base + stride * (accessor.count - 1) + element_size > buffer.size()) {
        diagnostics.error("BULLY-GLTF-0005", "accessor reads past the end of its buffer");
        return out;
    }
    out.resize(accessor.count * components);
    for (std::size_t i = 0; i < accessor.count; ++i) {
        const std::uint8_t* element = buffer.data() + base + i * stride;
        for (std::size_t c = 0; c < components; ++c) {
            out[i * components + c] =
                read_component(element + c * component_size(accessor.component_type),
                               accessor.component_type, accessor.normalized);
        }
    }
    return out;
}

std::vector<std::uint32_t> read_accessor_indices(const GltfDocument& document,
                                                  std::uint32_t accessor_index,
                                                  core::DiagnosticList& diagnostics)
{
    const auto floats = read_accessor_floats(document, accessor_index, diagnostics);
    std::vector<std::uint32_t> indices;
    indices.reserve(floats.size());
    for (float value : floats) {
        if (value < 0.0f) {
            diagnostics.error("BULLY-GLTF-0006", "negative index in glTF accessor");
            return {};
        }
        indices.push_back(static_cast<std::uint32_t>(value));
    }
    return indices;
}

formats::Mat4f multiply(const formats::Mat4f& a, const formats::Mat4f& b)
{
    formats::Mat4f out;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[static_cast<std::size_t>(k * 4 + row)] *
                       b.m[static_cast<std::size_t>(column * 4 + k)];
            }
            out.m[static_cast<std::size_t>(column * 4 + row)] = sum;
        }
    }
    return out;
}

formats::Mat4f trs_matrix(const core::json::Value& node)
{
    formats::Mat4f out;
    const core::json::Value* matrix = node.find("matrix");
    if (matrix != nullptr && matrix->is_array() && matrix->size() == 16) {
        for (std::size_t i = 0; i < 16; ++i) {
            out.m[i] = static_cast<float>((*matrix)[i].as_number());
        }
        return out;
    }
    formats::Vec3f translation{0.0f, 0.0f, 0.0f};
    formats::Vec3f scale{1.0f, 1.0f, 1.0f};
    formats::Vec4f rotation{0.0f, 0.0f, 0.0f, 1.0f};
    if (const auto* t = node.find("translation"); t != nullptr && t->is_array() && t->size() == 3) {
        translation = formats::Vec3f{static_cast<float>((*t)[0].as_number()),
                                     static_cast<float>((*t)[1].as_number()),
                                     static_cast<float>((*t)[2].as_number())};
    }
    if (const auto* s = node.find("scale"); s != nullptr && s->is_array() && s->size() == 3) {
        scale = formats::Vec3f{static_cast<float>((*s)[0].as_number()),
                               static_cast<float>((*s)[1].as_number()),
                               static_cast<float>((*s)[2].as_number())};
    }
    if (const auto* r = node.find("rotation"); r != nullptr && r->is_array() && r->size() == 4) {
        rotation = formats::Vec4f{static_cast<float>((*r)[0].as_number()),
                                  static_cast<float>((*r)[1].as_number()),
                                  static_cast<float>((*r)[2].as_number()),
                                  static_cast<float>((*r)[3].as_number())};
    }
    const float x = rotation.x;
    const float y = rotation.y;
    const float z = rotation.z;
    const float w = rotation.w;
    formats::Mat4f rotation_matrix;
    rotation_matrix.m[0] = 1.0f - 2.0f * (y * y + z * z);
    rotation_matrix.m[1] = 2.0f * (x * y + z * w);
    rotation_matrix.m[2] = 2.0f * (x * z - y * w);
    rotation_matrix.m[4] = 2.0f * (x * y - z * w);
    rotation_matrix.m[5] = 1.0f - 2.0f * (x * x + z * z);
    rotation_matrix.m[6] = 2.0f * (y * z + x * w);
    rotation_matrix.m[8] = 2.0f * (x * z + y * w);
    rotation_matrix.m[9] = 2.0f * (y * z - x * w);
    rotation_matrix.m[10] = 1.0f - 2.0f * (x * x + y * y);
    out = rotation_matrix;
    for (std::size_t i = 0; i < 4; ++i) {
        out.m[i] *= scale.x;
        out.m[4 + i] *= scale.y;
        out.m[8 + i] *= scale.z;
    }
    out.m[12] = translation.x;
    out.m[13] = translation.y;
    out.m[14] = translation.z;
    out.m[15] = 1.0f;
    return out;
}

/// Splits a GLB container into its JSON and BIN chunks.
core::Result<std::pair<std::string, std::vector<std::uint8_t>>> parse_glb(
    const std::vector<std::uint8_t>& bytes)
{
    core::DiagnosticList diagnostics;
    if (bytes.size() < 20) {
        diagnostics.error("BULLY-GLTF-0007", "GLB file is too small to contain a header");
        return core::Result<std::pair<std::string, std::vector<std::uint8_t>>>(std::move(diagnostics));
    }
    const std::uint32_t magic = static_cast<std::uint32_t>(bytes[0]) |
                                (static_cast<std::uint32_t>(bytes[1]) << 8) |
                                (static_cast<std::uint32_t>(bytes[2]) << 16) |
                                (static_cast<std::uint32_t>(bytes[3]) << 24);
    if (magic != 0x46546C67u) {  // "glTF"
        diagnostics.error("BULLY-GLTF-0008", "not a binary glTF file (bad magic)");
        return core::Result<std::pair<std::string, std::vector<std::uint8_t>>>(std::move(diagnostics));
    }
    std::size_t offset = 12;
    std::string json_text;
    std::vector<std::uint8_t> bin;
    while (offset + 8 <= bytes.size()) {
        const std::uint32_t length = static_cast<std::uint32_t>(bytes[offset]) |
                                     (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
                                     (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
                                     (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
        const std::uint32_t type = static_cast<std::uint32_t>(bytes[offset + 4]) |
                                   (static_cast<std::uint32_t>(bytes[offset + 5]) << 8) |
                                   (static_cast<std::uint32_t>(bytes[offset + 6]) << 16) |
                                   (static_cast<std::uint32_t>(bytes[offset + 7]) << 24);
        const std::size_t body = offset + 8;
        if (body + length > bytes.size()) {
            diagnostics.error("BULLY-GLTF-0009", "GLB chunk exceeds file length");
            return core::Result<std::pair<std::string, std::vector<std::uint8_t>>>(
                std::move(diagnostics));
        }
        if (type == 0x4E4F534Au) {  // JSON
            json_text.assign(reinterpret_cast<const char*>(bytes.data() + body), length);
        } else if (type == 0x004E4942u) {  // BIN
            bin.assign(bytes.begin() + static_cast<std::ptrdiff_t>(body),
                       bytes.begin() + static_cast<std::ptrdiff_t>(body + length));
        }
        offset = body + length;
        // Chunks are 4-byte aligned.
        while (offset % 4 != 0 && offset < bytes.size()) ++offset;
    }
    if (json_text.empty()) {
        diagnostics.error("BULLY-GLTF-0010", "GLB file has no JSON chunk");
        return core::Result<std::pair<std::string, std::vector<std::uint8_t>>>(std::move(diagnostics));
    }
    return std::pair<std::string, std::vector<std::uint8_t>>{std::move(json_text), std::move(bin)};
}

/// Builds buffers/bufferViews/accessors tables from parsed glTF JSON.
GltfDocument build_tables(const core::json::Value& root, std::vector<std::uint8_t> glb_bin,
                          const std::filesystem::path& source_dir, core::DiagnosticList& diagnostics)
{
    GltfDocument document;
    if (const auto* buffers = root.find("buffers"); buffers != nullptr && buffers->is_array()) {
        for (const auto& buffer : buffers->items()) {
            const std::string uri = buffer.get_string("uri");
            if (uri.empty()) {
                document.buffers.push_back(std::move(glb_bin));
            } else if (core::starts_with_ci(uri, "data:")) {
                // Embedded base64 buffer: decoded by the dedicated helper.
                const auto comma = uri.find(',');
                std::string payload = comma == std::string::npos ? std::string{} : uri.substr(comma + 1);
                auto decoded = core::base64_decode(payload);
                if (!decoded) {
                    diagnostics.error("BULLY-GLTF-0011", "cannot decode embedded base64 buffer");
                    document.buffers.emplace_back();
                } else {
                    document.buffers.push_back(std::move(decoded.value()));
                }
            } else {
                const auto path = core::FileSystem::resolve(source_dir, uri);
                auto bytes = core::FileSystem::read_binary(path);
                if (!bytes) {
                    diagnostics.error("BULLY-GLTF-0012",
                                      "cannot read external buffer '" + uri + "'");
                    document.buffers.emplace_back();
                } else {
                    document.buffers.push_back(std::move(bytes.value()));
                }
            }
        }
    }
    if (const auto* views = root.find("bufferViews"); views != nullptr && views->is_array()) {
        for (const auto& view : views->items()) {
            GltfBufferView entry;
            entry.buffer = static_cast<std::uint32_t>(view.get_int("buffer", 0));
            entry.byte_offset = static_cast<std::size_t>(view.get_int("byteOffset", 0));
            entry.byte_length = static_cast<std::size_t>(view.get_int("byteLength", 0));
            entry.byte_stride = static_cast<std::size_t>(view.get_int("byteStride", 0));
            document.buffer_views.push_back(entry);
        }
    }
    if (const auto* accessors = root.find("accessors"); accessors != nullptr && accessors->is_array()) {
        for (const auto& accessor : accessors->items()) {
            GltfAccessor entry;
            if (const auto* view = accessor.find("bufferView")) {
                entry.buffer_view = static_cast<std::uint32_t>(view->as_int(0));
            } else {
                entry.buffer_view = UINT32_MAX;  // sparse/zero-filled accessor
            }
            entry.byte_offset = static_cast<std::size_t>(accessor.get_int("byteOffset", 0));
            entry.component_type = static_cast<std::uint32_t>(accessor.get_int("componentType", 5126));
            entry.count = static_cast<std::size_t>(accessor.get_int("count", 0));
            entry.type = accessor.get_string("type", "SCALAR");
            entry.normalized = accessor.get_bool("normalized", false);
            document.accessors.push_back(std::move(entry));
        }
    }
    return document;
}

class GlbImporter final : public Importer {
public:
    explicit GlbImporter(std::string extensions_first)
        : glb_mode_(extensions_first == ".glb") {}

    [[nodiscard]] std::string id() const override
    {
        return glb_mode_ ? "com.bully.importer.glb" : "com.bully.importer.gltf";
    }
    [[nodiscard]] std::string display_name() const override
    {
        return glb_mode_ ? "glTF 2.0 Binary (.glb)" : "glTF 2.0 (.gltf)";
    }
    [[nodiscard]] std::string version() const override { return "1.1.0"; }
    [[nodiscard]] std::vector<std::string> extensions() const override
    {
        return glb_mode_ ? std::vector<std::string>{".glb"} : std::vector<std::string>{".gltf"};
    }
    [[nodiscard]] AssetType produces() const override { return AssetType::Model; }

    [[nodiscard]] std::vector<ImportSettingDescriptor> settings_schema() const override
    {
        using namespace settings;
        return {
            number("scale", "Uniform scale", 1.0, 0.001, 1000.0, ""),
            boolean("import_skin", "Import skeleton", true,
                    "Build a Skeleton from the glTF skin"),
            boolean("import_animations", "Register animation clips", true,
                    "Adds clip references so the animation studio can import them"),
            boolean("generate_lods", "Generate LOD chain", false, ""),
            integer("lod_count", "LOD count", 3, 1, 6, ""),
            enumeration("retro_preset", "Retro profile", "none", {"none", "ps2", "xbox"}, ""),
        };
    }

    [[nodiscard]] core::Result<ImportResult> import(const ImportContext& context) override
    {
        core::DiagnosticList diagnostics;
        auto bytes = core::FileSystem::read_binary(context.source_path);
        if (!bytes) return core::Result<ImportResult>(bytes.diagnostics());

        std::string json_text;
        std::vector<std::uint8_t> glb_bin;
        if (glb_mode_) {
            auto parsed = parse_glb(bytes.value());
            if (!parsed) return core::Result<ImportResult>(parsed.diagnostics());
            json_text = std::move(parsed.value().first);
            glb_bin = std::move(parsed.value().second);
        } else {
            json_text.assign(bytes.value().begin(), bytes.value().end());
        }
        auto root_result = core::json::parse(json_text);
        if (!root_result) return core::Result<ImportResult>(root_result.diagnostics());
        const auto& root = root_result.value();

        const std::string asset_version = root.at("asset").get_string("version", "2.0");
        if (!core::starts_with_ci(asset_version, "2.")) {
            diagnostics.error("BULLY-GLTF-0013",
                              "unsupported glTF version '" + asset_version + "' (need 2.x)",
                              context.relative_source_path);
            return core::Result<ImportResult>(std::move(diagnostics));
        }
        if (const auto* extensions = root.find("extensionsRequired");
            extensions != nullptr && extensions->is_array() && !extensions->items().empty()) {
            diagnostics.warn("BULLY-GLTF-0014",
                             "file requires glTF extensions; unsupported features are skipped",
                             context.relative_source_path);
        }

        const std::filesystem::path source(context.source_path);
        GltfDocument gltf = build_tables(root, std::move(glb_bin), source.parent_path(), diagnostics);

        auto raw = std::make_shared<RawModel>();
        std::vector<AssetDependency> dependencies;

        // ---- node world transforms ----
        const auto* nodes = root.find("nodes");
        const std::size_t node_count = nodes != nullptr ? nodes->size() : 0;
        std::vector<formats::Mat4f> world(node_count, formats::Mat4f::identity());
        std::vector<bool> computed(node_count, false);
        std::function<formats::Mat4f(std::size_t, int)> world_matrix =
            [&](std::size_t index, int depth) -> formats::Mat4f {
            if (index >= node_count || depth > 64) return formats::Mat4f::identity();
            if (computed[index]) return world[index];
            const auto& node = (*nodes)[index];
            formats::Mat4f local = trs_matrix(node);
            // Find parent by scanning (glTF scene graph is small enough).
            for (std::size_t candidate = 0; candidate < node_count; ++candidate) {
                const auto* children = (*nodes)[candidate].find("children");
                if (children == nullptr || !children->is_array()) continue;
                for (const auto& child : children->items()) {
                    if (child.as_int(-1) == static_cast<std::int64_t>(index)) {
                        local = multiply(world_matrix(candidate, depth + 1), local);
                        computed[index] = true;
                        world[index] = local;
                        return local;
                    }
                }
            }
            computed[index] = true;
            world[index] = local;
            return local;
        };

        // ---- materials ----
        if (const auto* materials = root.find("materials"); materials != nullptr && materials->is_array()) {
            for (const auto& material : materials->items()) {
                formats::MaterialBinding binding;
                binding.slot_name = material.get_string("name", "material");
                binding.shader_preset = "retro.lambert";
                const auto& pbr = material.at("pbrMetallicRoughness");
                const std::string base_color = pbr.at("baseColorTexture").get_string("index");
                if (!base_color.empty() || pbr.contains("baseColorTexture")) {
                    const auto index = pbr.at("baseColorTexture").get_int("index", -1);
                    if (index >= 0) {
                        binding.material_asset.clear();
                    }
                }
                const auto normal_index = material.at("normalTexture").get_int("index", -1);
                (void)normal_index;
                raw->materials.push_back(std::move(binding));
            }
        }
        // Texture index -> image uri (used to declare dependencies).
        std::vector<std::string> image_uris;
        if (const auto* images = root.find("images"); images != nullptr && images->is_array()) {
            for (const auto& image : images->items()) {
                image_uris.push_back(image.get_string("uri"));
            }
        }
        if (const auto* textures = root.find("textures"); textures != nullptr && textures->is_array()) {
            for (const auto& texture : textures->items()) {
                const auto source_index = texture.get_int("source", -1);
                if (source_index >= 0 &&
                    static_cast<std::size_t>(source_index) < image_uris.size()) {
                    const std::string& uri = image_uris[static_cast<std::size_t>(source_index)];
                    if (!uri.empty() && !core::starts_with_ci(uri, "data:")) {
                        const auto absolute = core::FileSystem::resolve(source.parent_path(), uri);
                        dependencies.push_back(AssetDependency{
                            core::FileSystem::relative_to(absolute, context.project_root), true,
                            "glTF texture image"});
                    }
                }
            }
        }

        // ---- meshes ----
        const auto* meshes = root.find("meshes");
        if (meshes != nullptr && meshes->is_array()) {
            for (std::size_t mesh_index = 0; mesh_index < meshes->size(); ++mesh_index) {
                const auto& mesh = (*meshes)[mesh_index];
                const std::string mesh_name = mesh.get_string("name", "mesh" + std::to_string(mesh_index));
                const auto* primitives = mesh.find("primitives");
                if (primitives == nullptr || !primitives->is_array()) continue;
                for (std::size_t primitive_index = 0; primitive_index < primitives->size();
                     ++primitive_index) {
                    const auto& primitive = (*primitives)[primitive_index];
                    const int mode = static_cast<int>(primitive.get_int("mode", 4));
                    if (mode != 4) {
                        diagnostics.warn("BULLY-GLTF-0015",
                                         "skipping primitive with mode " + std::to_string(mode) +
                                             " (only TRIANGLES is supported)",
                                         mesh_name);
                        continue;
                    }
                    const auto& attributes = primitive.at("attributes");
                    const auto position_index = attributes.get_int("POSITION", -1);
                    if (position_index < 0) {
                        diagnostics.warn("BULLY-GLTF-0016",
                                         "primitive has no POSITION attribute", mesh_name);
                        continue;
                    }
                    const auto positions = read_accessor_floats(
                        gltf, static_cast<std::uint32_t>(position_index), diagnostics);
                    if (positions.empty()) continue;
                    RawMesh raw_mesh;
                    raw_mesh.name = mesh_name + (primitives->size() > 1
                                                     ? "." + std::to_string(primitive_index)
                                                     : "");
                    const auto material_index = primitive.get_int("material", -1);
                    if (material_index >= 0 &&
                        static_cast<std::size_t>(material_index) < raw->materials.size()) {
                        raw_mesh.material_name =
                            raw->materials[static_cast<std::size_t>(material_index)].slot_name;
                    }
                    const std::size_t vertex_count = positions.size() / 3;
                    raw_mesh.vertices.resize(vertex_count);
                    for (std::size_t i = 0; i < vertex_count; ++i) {
                        raw_mesh.vertices[i].position =
                            formats::Vec3f{positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]};
                    }
                    if (const auto normal_index_attr = attributes.get_int("NORMAL", -1);
                        normal_index_attr >= 0) {
                        const auto normals = read_accessor_floats(
                            gltf, static_cast<std::uint32_t>(normal_index_attr), diagnostics);
                        for (std::size_t i = 0; i < vertex_count && i * 3 + 2 < normals.size(); ++i) {
                            raw_mesh.vertices[i].normal =
                                formats::Vec3f{normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]};
                        }
                    } else {
                        model_builder::compute_normals(raw_mesh);
                    }
                    if (const auto uv_index = attributes.get_int("TEXCOORD_0", -1); uv_index >= 0) {
                        const auto uvs = read_accessor_floats(gltf, static_cast<std::uint32_t>(uv_index),
                                                             diagnostics);
                        for (std::size_t i = 0; i < vertex_count && i * 2 + 1 < uvs.size(); ++i) {
                            raw_mesh.vertices[i].uv0 = formats::Vec2f{uvs[i * 2], uvs[i * 2 + 1]};
                        }
                    }
                    if (const auto color_index = attributes.get_int("COLOR_0", -1); color_index >= 0) {
                        const auto colors = read_accessor_floats(
                            gltf, static_cast<std::uint32_t>(color_index), diagnostics);
                        const auto& accessor = gltf.accessors[static_cast<std::size_t>(color_index)];
                        const std::size_t components = component_count(accessor.type);
                        for (std::size_t i = 0; i < vertex_count && (i + 1) * components <= colors.size();
                             ++i) {
                            auto& color = raw_mesh.vertices[i].color;
                            color.x = colors[i * components];
                            color.y = components > 1 ? colors[i * components + 1] : color.x;
                            color.z = components > 2 ? colors[i * components + 2] : color.x;
                            color.w = components > 3 ? colors[i * components + 3] : 1.0f;
                        }
                    }
                    if (const auto skin_index = attributes.get_int("JOINTS_0", -1); skin_index >= 0) {
                        const auto joints = read_accessor_floats(
                            gltf, static_cast<std::uint32_t>(skin_index), diagnostics);
                        for (std::size_t i = 0; i < vertex_count && i * 4 + 3 < joints.size(); ++i) {
                            auto& vertex = raw_mesh.vertices[i];
                            for (std::size_t j = 0; j < 4; ++j) {
                                vertex.joints[j] = static_cast<std::uint16_t>(joints[i * 4 + j]);
                            }
                        }
                    }
                    if (const auto weights_index = attributes.get_int("WEIGHTS_0", -1);
                        weights_index >= 0) {
                        const auto weights = read_accessor_floats(
                            gltf, static_cast<std::uint32_t>(weights_index), diagnostics);
                        for (std::size_t i = 0; i < vertex_count && i * 4 + 3 < weights.size(); ++i) {
                            raw_mesh.vertices[i].weights = formats::Vec4f{
                                weights[i * 4], weights[i * 4 + 1], weights[i * 4 + 2], weights[i * 4 + 3]};
                        }
                    }
                    if (const auto indices_index = primitive.get_int("indices", -1); indices_index >= 0) {
                        raw_mesh.indices =
                            read_accessor_indices(gltf, static_cast<std::uint32_t>(indices_index),
                                                  diagnostics);
                    } else {
                        raw_mesh.indices.resize(vertex_count);
                        for (std::size_t i = 0; i < vertex_count; ++i) {
                            raw_mesh.indices[i] = static_cast<std::uint32_t>(i);
                        }
                    }
                    raw->meshes.push_back(std::move(raw_mesh));
                }
            }
        }
        if (raw->meshes.empty()) {
            diagnostics.error("BULLY-GLTF-0017", "glTF file contains no triangle geometry",
                              context.relative_source_path);
            return core::Result<ImportResult>(std::move(diagnostics));
        }

        // ---- skin ----
        if (context.settings.get_bool("import_skin", true)) {
            if (const auto* skins = root.find("skins");
                skins != nullptr && skins->is_array() && !skins->items().empty()) {
                const auto& skin = skins->items().front();
                formats::Skeleton skeleton;
                skeleton.name = skin.get_string("name", "skeleton");
                if (const auto* joints = skin.find("joints"); joints != nullptr && joints->is_array()) {
                    std::map<std::int64_t, std::int32_t> joint_to_bone;
                    for (std::size_t i = 0; i < joints->size(); ++i) {
                        joint_to_bone[(*joints)[i].as_int(-1)] = static_cast<std::int32_t>(i);
                    }
                    const auto inverse_bind_index = skin.get_int("inverseBindMatrices", -1);
                    std::vector<float> inverse_bind;
                    if (inverse_bind_index >= 0) {
                        inverse_bind = read_accessor_floats(
                            gltf, static_cast<std::uint32_t>(inverse_bind_index), diagnostics);
                    }
                    for (std::size_t i = 0; i < joints->size(); ++i) {
                        const auto node_index = (*joints)[i].as_int(-1);
                        formats::Bone bone;
                        bone.name = "joint" + std::to_string(i);
                        bone.parent = -1;
                        if (node_index >= 0 && static_cast<std::size_t>(node_index) < node_count) {
                            const auto& node = (*nodes)[static_cast<std::size_t>(node_index)];
                            bone.name = node.get_string("name", bone.name);
                            if (const auto* t = node.find("translation"); t != nullptr) {
                                bone.local_translation = formats::Vec3f{
                                    static_cast<float>((*t)[0].as_number()),
                                    static_cast<float>((*t)[1].as_number()),
                                    static_cast<float>((*t)[2].as_number())};
                            }
                            // Parent lookup inside the joint set only.
                            for (std::size_t candidate = 0; candidate < node_count; ++candidate) {
                                const auto* children = (*nodes)[candidate].find("children");
                                if (children == nullptr || !children->is_array()) continue;
                                for (const auto& child : children->items()) {
                                    if (child.as_int(-1) == node_index) {
                                        auto found = joint_to_bone.find(
                                            static_cast<std::int64_t>(candidate));
                                        if (found != joint_to_bone.end()) bone.parent = found->second;
                                    }
                                }
                            }
                        }
                        if (i * 16 + 15 < inverse_bind.size()) {
                            for (std::size_t c = 0; c < 16; ++c) {
                                bone.inverse_bind.m[c] = inverse_bind[i * 16 + c];
                            }
                        }
                        skeleton.bones.push_back(std::move(bone));
                    }
                }
                auto skeleton_status = skeleton.validate(context.relative_source_path);
                if (skeleton_status) {
                    raw->skeleton = std::move(skeleton);
                } else {
                    diagnostics.append(skeleton_status.diagnostics());
                }
            }
        }

        // ---- node transforms applied to meshes ----
        if (nodes != nullptr && nodes->is_array()) {
            std::size_t mesh_cursor = 0;
            for (std::size_t node_index = 0; node_index < node_count; ++node_index) {
                const auto& node = (*nodes)[node_index];
                const auto mesh_index = node.get_int("mesh", -1);
                if (mesh_index < 0 || meshes == nullptr) continue;
                const formats::Mat4f matrix = world_matrix(node_index, 0);
                // Apply to the primitives belonging to this mesh.
                const auto& mesh_json = (*meshes)[static_cast<std::size_t>(mesh_index)];
                const std::size_t primitive_count =
                    mesh_json.find("primitives") != nullptr ? mesh_json.at("primitives").size() : 0;
                for (std::size_t p = 0; p < primitive_count && mesh_cursor < raw->meshes.size();
                     ++p, ++mesh_cursor) {
                    if (raw->skeleton) break;  // skinned meshes stay in bind pose space
                    for (auto& vertex : raw->meshes[mesh_cursor].vertices) {
                        const auto& m = matrix.m;
                        const float x = vertex.position.x;
                        const float y = vertex.position.y;
                        const float z = vertex.position.z;
                        vertex.position = formats::Vec3f{
                            m[0] * x + m[4] * y + m[8] * z + m[12],
                            m[1] * x + m[5] * y + m[9] * z + m[13],
                            m[2] * x + m[6] * y + m[10] * z + m[14]};
                    }
                }
            }
        }

        auto profile = build_profile_from_settings(context.settings);
        auto document = model_builder::assemble(context, std::move(*raw), profile);
        auto validation = document.validate();
        diagnostics.append(validation.diagnostics());
        const auto statistics = document.compute_statistics();
        auto result = model_builder::finish(
            document, std::move(diagnostics),
            std::to_string(statistics.mesh_count) + " mesh(es), " +
                std::to_string(statistics.triangle_count) + " triangles, " +
                std::to_string(statistics.bone_count) + " bones");
        result.dependencies = std::move(dependencies);
        return result;
    }

private:
    bool glb_mode_ = true;
};

class FbxBridgeImporter final : public Importer {
public:
    [[nodiscard]] std::string id() const override { return "com.bully.importer.fbx_bridge"; }
    [[nodiscard]] std::string display_name() const override { return "FBX (external bridge)"; }
    [[nodiscard]] std::string version() const override { return "1.0.0"; }
    [[nodiscard]] std::vector<std::string> extensions() const override { return {".fbx"}; }
    [[nodiscard]] AssetType produces() const override { return AssetType::Model; }

    [[nodiscard]] std::vector<ImportSettingDescriptor> settings_schema() const override
    {
        using namespace settings;
        return {
            file_path("converter", "Converter executable",
                      "External FBX -> glTF converter (Blender, FBX2glTF, ...). Never hardcoded."),
            text("converter_args", "Converter arguments", "\"{input}\" -o \"{output}\"",
                 "Argument template; {input} and {output} are substituted"),
            enumeration("intermediate_format", "Intermediate format", "glb", {"glb", "gltf"}, ""),
        };
    }

    [[nodiscard]] core::Result<ImportResult> import(const ImportContext& context) override
    {
        core::DiagnosticList diagnostics;
        // The platform intentionally does not embed an FBX parser. Instead it
        // documents and drives a configurable conversion step, so users are free
        // to pick Blender, FBX2glTF or any other tool.
        const std::string converter = context.settings.get("converter");
        if (converter.empty()) {
            diagnostics.error(
                "BULLY-FBX-0001",
                "no FBX converter configured. Set the 'converter' import setting to an executable "
                "that can convert FBX to glTF (for example: blender --background --python "
                "scripts/fbx_to_glb.py -- \"{input}\" \"{output}\"), then rebuild this asset.",
                context.relative_source_path);
            return core::Result<ImportResult>(std::move(diagnostics));
        }
        diagnostics.error("BULLY-FBX-0002",
                          "converter '" + converter +
                              "' is configured but external process execution is disabled in this "
                              "build; run scripts/convert_fbx.sh from the CLI or enable "
                              "BULLY_ENABLE_EXTERNAL_TOOLS.",
                          context.relative_source_path);
        return core::Result<ImportResult>(std::move(diagnostics));
    }
};

}  // namespace

std::shared_ptr<Importer> make_glb_importer() { return std::make_shared<GlbImporter>(".glb"); }
std::shared_ptr<Importer> make_gltf_importer() { return std::make_shared<GlbImporter>(".gltf"); }
std::shared_ptr<Importer> make_fbx_bridge_importer() { return std::make_shared<FbxBridgeImporter>(); }

}  // namespace bully::pipeline
