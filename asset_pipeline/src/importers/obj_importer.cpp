// Wavefront OBJ importer: OBJ + MTL -> .bmodel
#include <filesystem>

#include "bully/core/fs.h"
#include "bully/core/strings.h"
#include "bully/pipeline/builtin_importers.h"
#include "importers/model_builder.h"
#include "importers/profile_defaults.h"

namespace bully::pipeline {

namespace {

class ObjImporter final : public Importer {
public:
    [[nodiscard]] std::string id() const override { return "com.bully.importer.obj"; }
    [[nodiscard]] std::string display_name() const override { return "Wavefront OBJ"; }
    [[nodiscard]] std::string version() const override { return "1.1.0"; }
    [[nodiscard]] std::vector<std::string> extensions() const override { return {".obj"}; }
    [[nodiscard]] AssetType produces() const override { return AssetType::Model; }

    [[nodiscard]] std::vector<ImportSettingDescriptor> settings_schema() const override
    {
        using namespace settings;
        return {
            boolean("weld_vertices", "Weld duplicate vertices", true,
                    "Merge vertices that share position, normal and UV"),
            boolean("generate_normals", "Generate normals when missing", true,
                    "Compute smooth normals if the OBJ has no vn records"),
            number("scale", "Uniform scale", 1.0, 0.001, 1000.0,
                   "Multiplier applied to all positions"),
            enumeration("up_axis", "Up axis", "y", {"y", "z"},
                        "Source up axis; Z-up sources are rotated to Y-up"),
            boolean("generate_lods", "Generate LOD chain", false,
                    "Produce simplified LOD meshes for the retro toolkit"),
            integer("lod_count", "LOD count", 3, 1, 6, "Number of LOD levels including LOD0"),
            enumeration("retro_preset", "Retro profile", "none", {"none", "ps2", "xbox"},
                        "Records polygon/texture budgets in the build report"),
            boolean("bake_vertex_lighting", "Bake vertex AO", false,
                    "Generate ambient occlusion into the vertex colour channel"),
        };
    }

    [[nodiscard]] core::Result<ImportResult> import(const ImportContext& context) override
    {
        core::DiagnosticList diagnostics;
        // Provenance inside the compiled asset must name this importer even when
        // the importer is invoked directly (tests, tooling) instead of through
        // the pipeline, which normally fills the setting in.
        ImportContext local = context;
        if (local.settings.importer_id.empty()) local.settings.importer_id = id();
        const ImportContext& ctx = local;
        if (ctx.report_progress) ctx.report_progress(0.05f, "reading source");

        auto text = core::FileSystem::read_text(context.source_path);
        if (!text) return core::Result<ImportResult>(text.diagnostics());

        auto parsed = importers::parse_obj(text.value());
        if (!parsed) return core::Result<ImportResult>(parsed.diagnostics());
        diagnostics.append(parsed.diagnostics());
        importers::ObjDocument obj = std::move(parsed.value());
        if (ctx.report_progress) ctx.report_progress(0.35f, "parsed geometry");

        // --- materials: load the MTL library next to the OBJ ---
        RawModel raw;
        for (const auto& warning : obj.warnings) raw.warnings.push_back(warning);
        const std::filesystem::path source(context.source_path);
        const std::filesystem::path source_dir = source.parent_path();
        std::vector<importers::ObjMaterial> mtl_materials;
        std::vector<AssetDependency> dependencies;
        if (!obj.mtllib.empty()) {
            const std::filesystem::path mtl_path = core::FileSystem::resolve(source_dir, obj.mtllib);
            if (core::FileSystem::exists(mtl_path)) {
                // The material library is a real source dependency: editing it
                // must invalidate this asset, so it is tracked explicitly.
                dependencies.push_back(AssetDependency{
                    core::FileSystem::relative_to(mtl_path, ctx.project_root), true,
                    "material library " + obj.mtllib});
                auto mtl_text = core::FileSystem::read_text(mtl_path);
                if (mtl_text) {
                    auto materials = importers::parse_mtl(mtl_text.value());
                    if (materials) mtl_materials = std::move(materials.value());
                }
            } else {
                diagnostics.warn("BULLY-OBJ-0010",
                                 "material library '" + obj.mtllib + "' not found; using defaults",
                                 context.relative_source_path);
                raw.warnings.push_back("missing mtllib: " + obj.mtllib);
            }
        }

        // --- build materials + texture dependencies ---
        for (const auto& material : mtl_materials) {
            formats::MaterialBinding binding;
            binding.slot_name = material.name;
            binding.shader_preset = "retro.lambert";
            if (!material.diffuse_map.empty()) {
                const std::filesystem::path texture_path =
                    core::FileSystem::resolve(source_dir, material.diffuse_map);
                binding.material_asset.clear();
                dependencies.push_back(AssetDependency{
                    core::FileSystem::relative_to(texture_path, ctx.project_root), true,
                    "diffuse texture of material '" + material.name + "'"});
            }
            raw.materials.push_back(std::move(binding));
        }

        // --- geometry ---
        const double scale = ctx.settings.get_number("scale", 1.0);
        const bool up_axis_z = core::to_lower(ctx.settings.get("up_axis", "y")) == "z";
        for (const auto& mesh : obj.meshes) {
            RawMesh raw_mesh;
            raw_mesh.name = mesh.name;
            raw_mesh.indices = mesh.indices;
            if (!mesh.material_names.empty()) raw_mesh.material_name = mesh.material_names.front();
            const std::size_t vertex_count = mesh.positions.size() / 3;
            raw_mesh.vertices.resize(vertex_count);
            const bool has_uvs = mesh.uvs.size() / 2 >= vertex_count;
            const bool has_normals = mesh.normals.size() / 3 >= vertex_count;
            for (std::size_t i = 0; i < vertex_count; ++i) {
                auto& vertex = raw_mesh.vertices[i];
                float x = mesh.positions[i * 3] * static_cast<float>(scale);
                float y = mesh.positions[i * 3 + 1] * static_cast<float>(scale);
                float z = mesh.positions[i * 3 + 2] * static_cast<float>(scale);
                if (up_axis_z) {
                    // Z-up -> Y-up: (x, y, z) => (x, z, -y)
                    const float rotated_y = z;
                    const float rotated_z = -y;
                    y = rotated_y;
                    z = rotated_z;
                }
                vertex.position = formats::Vec3f{x, y, z};
                if (has_uvs) vertex.uv0 = formats::Vec2f{mesh.uvs[i * 2], mesh.uvs[i * 2 + 1]};
                if (has_normals) {
                    vertex.normal = formats::Vec3f{mesh.normals[i * 3], mesh.normals[i * 3 + 1],
                                                   mesh.normals[i * 3 + 2]};
                }
            }
            if (!has_normals &&
                ctx.settings.get_bool("generate_normals", true)) {
                model_builder::compute_normals(raw_mesh);
            }
            raw.meshes.push_back(std::move(raw_mesh));
        }

        if (raw.meshes.empty()) {
            diagnostics.error("BULLY-OBJ-0011", "no importable geometry found",
                              context.relative_source_path);
            return core::Result<ImportResult>(std::move(diagnostics));
        }

        // --- retro profile + document assembly ---
        auto profile = build_profile_from_settings(context.settings);
        profile.weld_vertices = ctx.settings.get_bool("weld_vertices", true);
        auto document = model_builder::assemble(ctx, std::move(raw), profile);
        if (ctx.settings.get_bool("bake_vertex_lighting", false)) {
            apply_vertex_ao(document);
            document.profile.bake_vertex_lighting = true;
        }

        auto validation = document.validate();
        diagnostics.append(validation.diagnostics());
        if (ctx.report_progress) ctx.report_progress(0.9f, "serialising .bmodel");

        const auto statistics = document.compute_statistics();
        const std::string summary =
            std::to_string(statistics.mesh_count) + " mesh(es), " +
            std::to_string(statistics.vertex_count) + " vertices, " +
            std::to_string(statistics.triangle_count) + " triangles, " +
            std::to_string(statistics.lod_count) + " LOD(s)";
        auto result = model_builder::finish(document, std::move(diagnostics), summary);
        result.dependencies = std::move(dependencies);
        if (ctx.report_progress) ctx.report_progress(1.0f, "import complete");
        return result;
    }
};

}  // namespace

std::shared_ptr<Importer> make_obj_importer() { return std::make_shared<ObjImporter>(); }

}  // namespace bully::pipeline
