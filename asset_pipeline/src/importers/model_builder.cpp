#include "importers/model_builder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bully/core/json.h"
#include "bully/core/strings.h"

namespace bully::pipeline::model_builder {

namespace {

struct VertexKey {
    std::int64_t px, py, pz;
    std::int64_t nx, ny, nz;
    std::int64_t tu, tv;

    auto operator<=>(const VertexKey&) const = default;
};

std::int64_t quantise(float value, float epsilon)
{
    return static_cast<std::int64_t>(std::llround(static_cast<double>(value / epsilon)));
}

}  // namespace

void compute_normals(RawMesh& mesh)
{
    for (auto& vertex : mesh.vertices) {
        vertex.normal = formats::Vec3f{0.0f, 0.0f, 0.0f};
    }
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const auto i0 = mesh.indices[i];
        const auto i1 = mesh.indices[i + 1];
        const auto i2 = mesh.indices[i + 2];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
            continue;
        }
        const auto& p0 = mesh.vertices[i0].position;
        const auto& p1 = mesh.vertices[i1].position;
        const auto& p2 = mesh.vertices[i2].position;
        const formats::Vec3f e1{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
        const formats::Vec3f e2{p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
        const formats::Vec3f normal{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z,
                                    e1.x * e2.y - e1.y * e2.x};
        for (auto index : {i0, i1, i2}) {
            mesh.vertices[index].normal.x += normal.x;
            mesh.vertices[index].normal.y += normal.y;
            mesh.vertices[index].normal.z += normal.z;
        }
    }
    for (auto& vertex : mesh.vertices) {
        const float length = std::sqrt(vertex.normal.x * vertex.normal.x +
                                       vertex.normal.y * vertex.normal.y +
                                       vertex.normal.z * vertex.normal.z);
        if (length > 1e-8f) {
            vertex.normal.x /= length;
            vertex.normal.y /= length;
            vertex.normal.z /= length;
        } else {
            vertex.normal = formats::Vec3f{0.0f, 1.0f, 0.0f};
        }
    }
}

void update_bounds(formats::ModelMesh& mesh)
{
    if (mesh.vertices.empty()) {
        mesh.bounds = formats::Aabb{};
        return;
    }
    formats::Aabb bounds;
    bounds.min = mesh.vertices.front().position;
    bounds.max = mesh.vertices.front().position;
    for (const auto& vertex : mesh.vertices) {
        bounds.min.x = std::min(bounds.min.x, vertex.position.x);
        bounds.min.y = std::min(bounds.min.y, vertex.position.y);
        bounds.min.z = std::min(bounds.min.z, vertex.position.z);
        bounds.max.x = std::max(bounds.max.x, vertex.position.x);
        bounds.max.y = std::max(bounds.max.y, vertex.position.y);
        bounds.max.z = std::max(bounds.max.z, vertex.position.z);
    }
    mesh.bounds = bounds;
}

std::uint32_t weld_vertices(RawMesh& mesh, float epsilon)
{
    std::map<VertexKey, std::uint32_t> unique;
    std::vector<formats::ModelVertex> welded;
    std::vector<std::uint32_t> remap(mesh.vertices.size(), 0);
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto& vertex = mesh.vertices[i];
        const VertexKey key{quantise(vertex.position.x, epsilon), quantise(vertex.position.y, epsilon),
                            quantise(vertex.position.z, epsilon), quantise(vertex.normal.x, epsilon),
                            quantise(vertex.normal.y, epsilon), quantise(vertex.normal.z, epsilon),
                            quantise(vertex.uv0.x, epsilon), quantise(vertex.uv0.y, epsilon)};
        auto found = unique.find(key);
        if (found == unique.end()) {
            const auto index = static_cast<std::uint32_t>(welded.size());
            unique.emplace(key, index);
            welded.push_back(vertex);
            remap[i] = index;
        } else {
            remap[i] = found->second;
        }
    }
    if (welded.size() == mesh.vertices.size()) return 0;
    const auto removed = static_cast<std::uint32_t>(mesh.vertices.size() - welded.size());
    mesh.vertices = std::move(welded);
    for (auto& index : mesh.indices) {
        if (index < remap.size()) index = remap[index];
    }
    return removed;
}

formats::ModelMesh build_lod(const formats::ModelMesh& source, float cell_size, std::uint8_t lod)
{
    formats::ModelMesh lod_mesh;
    lod_mesh.name = source.name + ".lod" + std::to_string(lod);
    lod_mesh.material_slot = source.material_slot;
    lod_mesh.topology = source.topology;
    lod_mesh.lod = lod;
    if (cell_size <= 0.0f || source.vertices.empty()) {
        lod_mesh.vertices = source.vertices;
        lod_mesh.indices = source.indices;
        update_bounds(lod_mesh);
        return lod_mesh;
    }
    // Vertex clustering: every triangle contributes its cluster centroid.
    struct Cluster {
        formats::Vec3f position_sum{};
        std::uint32_t count = 0;
        const formats::ModelVertex* representative = nullptr;
    };
    std::map<std::array<std::int64_t, 3>, Cluster> clusters;
    std::vector<std::array<std::int64_t, 3>> vertex_cluster(source.vertices.size());
    for (std::size_t i = 0; i < source.vertices.size(); ++i) {
        const auto& position = source.vertices[i].position;
        const std::array<std::int64_t, 3> key{
            static_cast<std::int64_t>(std::floor(position.x / cell_size)),
            static_cast<std::int64_t>(std::floor(position.y / cell_size)),
            static_cast<std::int64_t>(std::floor(position.z / cell_size))};
        vertex_cluster[i] = key;
        auto& cluster = clusters[key];
        cluster.position_sum.x += position.x;
        cluster.position_sum.y += position.y;
        cluster.position_sum.z += position.z;
        ++cluster.count;
        if (cluster.representative == nullptr) cluster.representative = &source.vertices[i];
    }
    std::map<std::array<std::int64_t, 3>, std::uint32_t> cluster_index;
    for (const auto& [key, cluster] : clusters) {
        const auto index = static_cast<std::uint32_t>(lod_mesh.vertices.size());
        cluster_index.emplace(key, index);
        formats::ModelVertex vertex = *cluster.representative;
        vertex.position = formats::Vec3f{cluster.position_sum.x / static_cast<float>(cluster.count),
                                         cluster.position_sum.y / static_cast<float>(cluster.count),
                                         cluster.position_sum.z / static_cast<float>(cluster.count)};
        lod_mesh.vertices.push_back(vertex);
    }
    for (std::size_t i = 0; i + 2 < source.indices.size(); i += 3) {
        const auto a = source.indices[i];
        const auto b = source.indices[i + 1];
        const auto c = source.indices[i + 2];
        if (a >= source.vertices.size() || b >= source.vertices.size() || c >= source.vertices.size()) {
            continue;
        }
        const auto ca = cluster_index[vertex_cluster[a]];
        const auto cb = cluster_index[vertex_cluster[b]];
        const auto cc = cluster_index[vertex_cluster[c]];
        if (ca == cb || cb == cc || ca == cc) continue;  // degenerate after collapse
        lod_mesh.indices.push_back(ca);
        lod_mesh.indices.push_back(cb);
        lod_mesh.indices.push_back(cc);
    }
    update_bounds(lod_mesh);
    return lod_mesh;
}

formats::ModelMesh finalize_mesh(const RawMesh& raw, std::uint16_t material_slot, bool skinned,
                                 bool weld)
{
    RawMesh working = raw;
    if (weld) (void)weld_vertices(working);
    const bool has_normals = std::any_of(working.vertices.begin(), working.vertices.end(),
                                         [](const formats::ModelVertex& vertex) {
                                             return vertex.normal.x != 0.0f ||
                                                    vertex.normal.y != 0.0f ||
                                                    vertex.normal.z != 0.0f;
                                         });
    if (!has_normals) compute_normals(working);
    (void)skinned;
    formats::ModelMesh mesh;
    mesh.name = working.name;
    mesh.material_slot = material_slot;
    mesh.vertices = std::move(working.vertices);
    mesh.indices = std::move(working.indices);
    update_bounds(mesh);
    return mesh;
}

formats::ModelDocument assemble(const ImportContext& context, RawModel&& raw,
                                const formats::ModelBuildProfile& profile, AssetType asset_type)
{
    (void)asset_type;
    formats::ModelDocument document;
    document.asset_id = context.asset_id;
    const std::filesystem::path source(context.relative_source_path);
    document.name = source.stem().string();
    document.source_path = context.relative_source_path;
    document.source_format = source.extension().string().empty()
                                 ? std::string{}
                                : source.extension().string().substr(1);
    document.importer = context.settings.importer_id;
    document.importer_version = context.settings.get("__importer_version", "1.0.0");
    document.profile = profile;
    document.materials = std::move(raw.materials);
    document.animations = std::move(raw.animations);
    document.skeleton = std::move(raw.skeleton);
    document.import_settings_json = context.settings.to_json().dump(0);

    const bool skinned = document.skeleton.has_value();
    const bool weld = profile.weld_vertices;
    formats::Aabb bounds{};
    bool bounds_initialised = false;
    for (const auto& mesh : raw.meshes) {
        std::uint16_t slot = 0xFFFF;
        if (!mesh.material_name.empty()) {
            for (std::size_t i = 0; i < document.materials.size(); ++i) {
                if (document.materials[i].slot_name == mesh.material_name) {
                    slot = static_cast<std::uint16_t>(i);
                    break;
                }
            }
        }
        document.meshes.push_back(finalize_mesh(mesh, slot, skinned, weld));
        if (profile.generate_lods && profile.lod_count > 1) {
            // Cell size grows with the LOD index, giving coarser versions.
            const formats::ModelMesh& lod_zero = document.meshes.back();
            const formats::Vec3f extent{lod_zero.bounds.max.x - lod_zero.bounds.min.x,
                                        lod_zero.bounds.max.y - lod_zero.bounds.min.y,
                                        lod_zero.bounds.max.z - lod_zero.bounds.min.z};
            const float max_extent = std::max({extent.x, extent.y, extent.z, 0.001f});
            const formats::ModelMesh base = lod_zero;
            for (std::uint32_t level = 1; level < profile.lod_count; ++level) {
                const float cell = max_extent * 0.02f * static_cast<float>(level);
                document.meshes.push_back(build_lod(base, cell, static_cast<std::uint8_t>(level)));
            }
        }
    }
    for (const auto& mesh : document.meshes) {
        if (!bounds_initialised) {
            bounds = mesh.bounds;
            bounds_initialised = true;
        } else {
            bounds.min.x = std::min(bounds.min.x, mesh.bounds.min.x);
            bounds.min.y = std::min(bounds.min.y, mesh.bounds.min.y);
            bounds.min.z = std::min(bounds.min.z, mesh.bounds.min.z);
            bounds.max.x = std::max(bounds.max.x, mesh.bounds.max.x);
            bounds.max.y = std::max(bounds.max.y, mesh.bounds.max.y);
            bounds.max.z = std::max(bounds.max.z, mesh.bounds.max.z);
        }
    }
    document.bounds = bounds;

    // Build report: the QA dashboard reads this without re-opening the model.
    core::json::Value report = core::json::Value::make_object();
    core::json::Value warnings = core::json::Value::make_array();
    for (const auto& warning : raw.warnings) warnings.push(warning);
    report["warnings"] = std::move(warnings);
    const auto statistics = document.compute_statistics();
    report["vertices"] = statistics.vertex_count;
    report["triangles"] = statistics.triangle_count;
    report["meshes"] = statistics.mesh_count;
    report["lods"] = statistics.lod_count;
    report["bones"] = statistics.bone_count;
    if (profile.target_triangles > 0 && statistics.triangle_count > profile.target_triangles) {
        report["budget_exceeded"] = true;
    }
    document.build_report_json = report.dump(0);
    return document;
}

ImportResult finish(const formats::ModelDocument& document, core::DiagnosticList diagnostics,
                    std::string summary)
{
    ImportResult result;
    result.output_bytes = document.serialize();
    result.asset_type = AssetType::Model;
    result.summary = std::move(summary);
    result.report_json = document.build_report_json.empty() ? "{}" : document.build_report_json;
    result.diagnostics = std::move(diagnostics);
    return result;
}

}  // namespace bully::pipeline::model_builder
