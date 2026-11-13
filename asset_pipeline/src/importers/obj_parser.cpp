// Wavefront OBJ / MTL parser.
//
// Supports:
//   v / vn / vt records (with optional w and comments)
//   f records in every index form (v, v/vt, v//vn, v/vt/vn), negative
//   (relative) indices, and n-gon triangulation via a fan
//   o / g object grouping, usemtl, mtllib
//   .mtl: newmtl, Kd, Ks, Ns, d/Tr, map_Kd, map_Bump/bump
//
// Unsupported records (s, l, p, free-form) are reported as warnings rather
// than silently dropped, matching the "no silent import failures" rule.
#include <array>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <sstream>

#include "bully/core/strings.h"
#include "bully/pipeline/importer.h"

namespace bully::pipeline::importers {

namespace {

struct FaceIndex {
    int position = -1;
    int uv = -1;
    int normal = -1;
};

int resolve_index(int raw, std::size_t count)
{
    if (raw == 0) return -1;
    if (raw > 0) return raw - 1;
    const auto resolved = static_cast<int>(count) + raw;
    return resolved >= 0 ? resolved : -1;
}

FaceIndex parse_face_index(std::string_view token, std::size_t position_count,
                           std::size_t uv_count, std::size_t normal_count)
{
    FaceIndex index;
    const auto parts = core::split(std::string(token), '/');
    auto to_int = [](const std::string& text) -> int {
        if (text.empty()) return 0;
        try {
            return std::stoi(text);
        } catch (...) {
            return 0;
        }
    };
    if (!parts.empty()) index.position = resolve_index(to_int(parts[0]), position_count);
    if (parts.size() > 1 && !parts[1].empty()) index.uv = resolve_index(to_int(parts[1]), uv_count);
    if (parts.size() > 2 && !parts[2].empty()) {
        index.normal = resolve_index(to_int(parts[2]), normal_count);
    }
    return index;
}

}  // namespace

core::Result<ObjDocument> parse_obj(std::string_view text)
{
    core::DiagnosticList diagnostics;
    ObjDocument document;

    // Global vertex pools; faces reference them and meshes are built by
    // remapping into per-mesh vertex buffers.
    std::vector<float> positions;
    std::vector<float> uvs;
    std::vector<float> normals;

    struct MeshBuilder {
        ObjMesh mesh;
        // (position,uv,normal) tuple -> index in mesh
        std::map<std::array<int, 3>, std::uint32_t> unique;
    };
    std::vector<MeshBuilder> builders;
    MeshBuilder current;
    bool has_current = false;

    auto flush_current = [&]() {
        if (!has_current) return;
        if (!current.mesh.indices.empty()) builders.push_back(std::move(current));
        current = MeshBuilder{};
        has_current = false;
    };
    auto ensure_current = [&](std::string name) {
        if (has_current) return;
        current = MeshBuilder{};
        current.mesh.name = std::move(name);
        has_current = true;
    };

    std::istringstream stream{std::string(text)};
    std::string line;
    std::uint32_t line_number = 0;
    std::string active_material;
    std::uint32_t unsupported_records = 0;

    while (std::getline(stream, line)) {
        ++line_number;
        const std::string trimmed = core::trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        std::istringstream line_stream(trimmed);
        std::string keyword;
        line_stream >> keyword;

        if (keyword == "v") {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (!(line_stream >> x >> y >> z)) {
                diagnostics.warn("BULLY-OBJ-0001", "malformed vertex record", "obj", line_number);
                continue;
            }
            positions.push_back(x);
            positions.push_back(y);
            positions.push_back(z);
        } else if (keyword == "vt") {
            float u = 0.0f;
            float v = 0.0f;
            if (!(line_stream >> u >> v)) {
                diagnostics.warn("BULLY-OBJ-0002", "malformed UV record", "obj", line_number);
                continue;
            }
            uvs.push_back(u);
            uvs.push_back(v);
        } else if (keyword == "vn") {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (!(line_stream >> x >> y >> z)) {
                diagnostics.warn("BULLY-OBJ-0003", "malformed normal record", "obj", line_number);
                continue;
            }
            normals.push_back(x);
            normals.push_back(y);
            normals.push_back(z);
        } else if (keyword == "o" || keyword == "g") {
            std::string name;
            std::getline(line_stream, name);
            flush_current();
            ensure_current(core::trim(name).empty() ? "mesh" : core::trim(name));
        } else if (keyword == "usemtl") {
            std::string name;
            std::getline(line_stream, name);
            active_material = core::trim(name);
            ensure_current(active_material.empty() ? "mesh" : active_material);
            if (!active_material.empty() &&
                (current.mesh.material_names.empty() ||
                 current.mesh.material_names.back() != active_material)) {
                current.mesh.material_names.push_back(active_material);
            }
        } else if (keyword == "mtllib") {
            std::string name;
            std::getline(line_stream, name);
            document.mtllib = core::trim(name);
        } else if (keyword == "f") {
            ensure_current("mesh");
            std::vector<FaceIndex> face;
            std::string token;
            while (line_stream >> token) {
                const FaceIndex index = parse_face_index(
                    token, positions.size() / 3, uvs.size() / 2, normals.size() / 3);
                if (index.position < 0 ||
                    static_cast<std::size_t>(index.position) >= positions.size() / 3) {
                    diagnostics.warn("BULLY-OBJ-0004",
                                     "face references an out-of-range vertex", "obj", line_number);
                    face.clear();
                    break;
                }
                face.push_back(index);
            }
            if (face.size() < 3) {
                if (!face.empty()) {
                    diagnostics.warn("BULLY-OBJ-0005", "face has fewer than 3 vertices", "obj",
                                     line_number);
                }
                continue;
            }
            // Fan triangulation; matches the convention used by Blender's OBJ
            // exporter for convex polygons.
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                const FaceIndex triangle[3] = {face[0], face[i], face[i + 1]};
                for (const FaceIndex& corner : triangle) {
                    const std::array<int, 3> key{corner.position, corner.uv, corner.normal};
                    auto found = current.unique.find(key);
                    std::uint32_t index = 0;
                    if (found == current.unique.end()) {
                        index = static_cast<std::uint32_t>(current.mesh.positions.size() / 3);
                        current.unique.emplace(key, index);
                        current.mesh.positions.push_back(
                            positions[static_cast<std::size_t>(key[0]) * 3]);
                        current.mesh.positions.push_back(
                            positions[static_cast<std::size_t>(key[0]) * 3 + 1]);
                        current.mesh.positions.push_back(
                            positions[static_cast<std::size_t>(key[0]) * 3 + 2]);
                        if (key[1] >= 0 && static_cast<std::size_t>(key[1]) < uvs.size() / 2) {
                            current.mesh.uvs.push_back(uvs[static_cast<std::size_t>(key[1]) * 2]);
                            current.mesh.uvs.push_back(uvs[static_cast<std::size_t>(key[1]) * 2 + 1]);
                        } else {
                            current.mesh.uvs.push_back(0.0f);
                            current.mesh.uvs.push_back(0.0f);
                        }
                        if (key[2] >= 0 && static_cast<std::size_t>(key[2]) < normals.size() / 3) {
                            current.mesh.normals.push_back(
                                normals[static_cast<std::size_t>(key[2]) * 3]);
                            current.mesh.normals.push_back(
                                normals[static_cast<std::size_t>(key[2]) * 3 + 1]);
                            current.mesh.normals.push_back(
                                normals[static_cast<std::size_t>(key[2]) * 3 + 2]);
                        } else {
                            current.mesh.normals.push_back(0.0f);
                            current.mesh.normals.push_back(1.0f);
                            current.mesh.normals.push_back(0.0f);
                        }
                    } else {
                        index = found->second;
                    }
                    current.mesh.indices.push_back(index);
                }
                ++current.mesh.face_count;
            }
        } else if (keyword == "s" || keyword == "l" || keyword == "p" || keyword == "vp" ||
                   keyword == "cstype" || keyword == "curv" || keyword == "surf") {
            ++unsupported_records;
        } else {
            ++unsupported_records;
        }
    }
    flush_current();

    if (positions.empty()) {
        diagnostics.error("BULLY-OBJ-0006", "OBJ file contains no vertices", "obj");
        return core::Result<ObjDocument>(std::move(diagnostics));
    }
    if (builders.empty()) {
        diagnostics.error("BULLY-OBJ-0007", "OBJ file contains no faces", "obj");
        return core::Result<ObjDocument>(std::move(diagnostics));
    }
    if (unsupported_records > 0) {
        document.warnings.push_back("ignored " + std::to_string(unsupported_records) +
                                    " unsupported OBJ record(s)");
        diagnostics.warn("BULLY-OBJ-0008",
                         "ignored " + std::to_string(unsupported_records) +
                             " unsupported OBJ record(s)",
                         "obj");
    }
    for (auto& builder : builders) document.meshes.push_back(std::move(builder.mesh));
    // Non-fatal warnings that fired during parsing are echoed into the document
    // so callers that only keep ImportResult::diagnostics still see them.
    for (const auto& item : diagnostics.all()) {
        if (item.severity == core::Severity::Warning && !item.message.empty()) {
            document.warnings.push_back(item.message);
        }
    }
    return document;
}

}  // namespace bully::pipeline::importers

namespace bully::pipeline::importers {

core::Result<std::vector<ObjMaterial>> parse_mtl(std::string_view text)
{
    core::DiagnosticList diagnostics;
    std::vector<ObjMaterial> materials;
    std::istringstream stream{std::string(text)};
    std::string line;
    std::uint32_t line_number = 0;
    ObjMaterial* current = nullptr;
    while (std::getline(stream, line)) {
        ++line_number;
        const std::string trimmed = core::trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        std::istringstream line_stream(trimmed);
        std::string keyword;
        line_stream >> keyword;
        if (keyword == "newmtl") {
            std::string name;
            std::getline(line_stream, name);
            materials.push_back(ObjMaterial{});
            materials.back().name = core::trim(name);
            current = &materials.back();
        } else if (current == nullptr) {
            continue;
        } else if (keyword == "Kd") {
            line_stream >> current->diffuse[0] >> current->diffuse[1] >> current->diffuse[2];
        } else if (keyword == "Ks") {
            line_stream >> current->specular[0] >> current->specular[1] >> current->specular[2];
        } else if (keyword == "Ns") {
            line_stream >> current->shininess;
        } else if (keyword == "d") {
            line_stream >> current->alpha;
        } else if (keyword == "Tr") {
            float transparency = 0.0f;
            line_stream >> transparency;
            current->alpha = 1.0f - transparency;
        } else if (keyword == "map_Kd") {
            std::string path;
            std::getline(line_stream, path);
            current->diffuse_map = core::trim(path);
        } else if (keyword == "map_Bump" || keyword == "bump" || keyword == "map_bump") {
            std::string path;
            std::getline(line_stream, path);
            current->normal_map = core::trim(path);
        }
    }
    if (materials.empty()) {
        diagnostics.warn("BULLY-OBJ-0009", "material library contains no materials", "mtl",
                         line_number);
    }
    return materials;
}

}  // namespace bully::pipeline::importers
