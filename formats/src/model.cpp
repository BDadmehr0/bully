#include "bully/formats/model.h"

#include <algorithm>

#include "bully/core/json.h"
#include "bully/core/strings.h"

namespace bully::formats {

namespace {

constexpr std::uint32_t kChunkInfo = fourcc("INFO");
constexpr std::uint32_t kChunkMesh = fourcc("MESH");
constexpr std::uint32_t kChunkMatl = fourcc("MTRL");
constexpr std::uint32_t kChunkSkel = fourcc("SKEL");
constexpr std::uint32_t kChunkAnim = fourcc("ANIM");
constexpr std::uint32_t kChunkColl = fourcc("COLL");
constexpr std::uint32_t kChunkMeta = fourcc("META");

void write_vertex(BinaryWriter& w, const ModelVertex& v, std::uint32_t flags)
{
    if (flags & kVertexPosition) w.vec3(v.position);
    if (flags & kVertexNormal) w.vec3(v.normal);
    if (flags & kVertexTangent) w.vec4(v.tangent);
    if (flags & kVertexUv0) w.vec2(v.uv0);
    if (flags & kVertexUv1) w.vec2(v.uv1);
    if (flags & kVertexColor) w.vec4(v.color);
    if (flags & kVertexSkin) {
        for (auto joint : v.joints) w.u16(joint);
        w.vec4(v.weights);
    }
}

void read_vertex(BinaryReader& r, ModelVertex& v, std::uint32_t flags)
{
    if (flags & kVertexPosition) v.position = r.vec3();
    if (flags & kVertexNormal) v.normal = r.vec3();
    if (flags & kVertexTangent) v.tangent = r.vec4();
    if (flags & kVertexUv0) v.uv0 = r.vec2();
    if (flags & kVertexUv1) v.uv1 = r.vec2();
    if (flags & kVertexColor) v.color = r.vec4();
    if (flags & kVertexSkin) {
        for (auto& joint : v.joints) joint = r.u16();
        v.weights = r.vec4();
    }
}

/// Vertex flags actually needed by a mesh: only write channels that carry
/// information, so a retro unlit mesh stays small.
std::uint32_t mesh_vertex_flags(const ModelMesh& mesh, bool skinned)
{
    std::uint32_t flags = kVertexPosition;
    const bool has_normal = std::any_of(mesh.vertices.begin(), mesh.vertices.end(), [](const ModelVertex& v) {
        return v.normal.x != 0.0f || v.normal.y != 0.0f || v.normal.z != 0.0f;
    });
    if (has_normal) flags |= kVertexNormal;
    if (std::any_of(mesh.vertices.begin(), mesh.vertices.end(), [](const ModelVertex& v) {
            return v.uv0.x != 0.0f || v.uv0.y != 0.0f;
        })) {
        flags |= kVertexUv0;
    }
    if (std::any_of(mesh.vertices.begin(), mesh.vertices.end(), [](const ModelVertex& v) {
            return v.uv1.x != 0.0f || v.uv1.y != 0.0f;
        })) {
        flags |= kVertexUv1;
    }
    if (skinned) flags |= kVertexSkin;
    return flags;
}

void write_collision(BinaryWriter& w, const CollisionShape& shape)
{
    w.string(shape.name);
    w.u8(static_cast<std::uint8_t>(shape.kind));
    w.vec3(shape.center);
    w.vec3(shape.extents);
    w.f32(shape.radius);
    w.f32(shape.height);
    w.u32(static_cast<std::uint32_t>(shape.hull_points.size()));
    for (const auto& point : shape.hull_points) w.vec3(point);
    w.u32(static_cast<std::uint32_t>(shape.hull_indices.size()));
    for (auto index : shape.hull_indices) w.u32(index);
}

CollisionShape read_collision(BinaryReader& r)
{
    CollisionShape shape;
    shape.name = r.string();
    shape.kind = static_cast<CollisionKind>(r.u8());
    shape.center = r.vec3();
    shape.extents = r.vec3();
    shape.radius = r.f32();
    shape.height = r.f32();
    const std::uint32_t point_count = r.u32();
    shape.hull_points.resize(std::min<std::uint32_t>(point_count, 1u << 20));
    for (auto& point : shape.hull_points) point = r.vec3();
    const std::uint32_t index_count = r.u32();
    shape.hull_indices.resize(std::min<std::uint32_t>(index_count, 1u << 22));
    for (auto& index : shape.hull_indices) index = r.u32();
    return shape;
}

}  // namespace

std::uint32_t ModelMesh::triangle_count() const
{
    if (topology == Topology::Triangles) return static_cast<std::uint32_t>(indices.size() / 3);
    if (topology == Topology::TriangleStrip) {
        return indices.size() >= 3 ? static_cast<std::uint32_t>(indices.size() - 2) : 0u;
    }
    return 0;
}

core::Status Skeleton::validate(std::string_view source) const
{
    core::DiagnosticList diagnostics;
    const std::string where = source.empty() ? name : std::string(source);
    if (bones.size() > 4096) {
        diagnostics.error("BULLY-MODEL-0020", "skeleton exceeds 4096 bones", where);
    }
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const auto parent = bones[i].parent;
        if (parent >= static_cast<std::int32_t>(bones.size())) {
            diagnostics.error("BULLY-MODEL-0021",
                              "bone '" + bones[i].name + "' references out-of-range parent " +
                                  std::to_string(parent),
                              where);
            continue;
        }
        if (parent == static_cast<std::int32_t>(i)) {
            diagnostics.error("BULLY-MODEL-0022",
                              "bone '" + bones[i].name + "' is its own parent", where);
            continue;
        }
        // Cycle detection: walk up at most bones.size() steps. A bone further up
        // the chain can itself have an out-of-range parent, so the cursor is
        // bounds-checked on every hop instead of trusting the first link.
        std::int32_t cursor = parent;
        std::size_t guard = 0;
        while (cursor >= 0 && guard++ <= bones.size()) {
            if (cursor >= static_cast<std::int32_t>(bones.size())) break;
            if (cursor == static_cast<std::int32_t>(i)) {
                diagnostics.error("BULLY-MODEL-0023",
                                  "bone hierarchy contains a cycle at '" + bones[i].name + "'", where);
                break;
            }
            cursor = bones[static_cast<std::size_t>(cursor)].parent;
        }
    }
    if (diagnostics.has_errors()) return core::Status(std::move(diagnostics));
    // Warnings are part of the report even when the document is valid.
    auto status = core::Status{};
    status.attach_diagnostics(std::move(diagnostics));
    return status;
}

ModelStatistics ModelDocument::compute_statistics() const
{
    ModelStatistics stats;
    stats.mesh_count = static_cast<std::uint32_t>(meshes.size());
    stats.lod_count = 1;
    std::uint8_t max_lod = 0;
    for (const auto& mesh : meshes) {
        stats.vertex_count += static_cast<std::uint32_t>(mesh.vertices.size());
        stats.triangle_count += mesh.triangle_count();
        max_lod = std::max(max_lod, mesh.lod);
        if (mesh.material_slot != 0xFFFF) ++stats.draw_call_estimate;
    }
    stats.lod_count = static_cast<std::uint32_t>(max_lod) + 1;
    stats.bone_count = skeleton ? static_cast<std::uint32_t>(skeleton->bones.size()) : 0u;
    if (stats.draw_call_estimate == 0) stats.draw_call_estimate = stats.mesh_count;
    return stats;
}

std::vector<std::uint8_t> ModelDocument::serialize() const
{
    BinaryWriter w;

    w.chunk(kChunkInfo, [&](BinaryWriter& body) {
        body.string(asset_id);
        body.string(name);
        body.string(source_path);
        body.string(source_format);
        body.string(importer);
        body.string(importer_version);
        body.aabb(bounds);
        body.u32(static_cast<std::uint32_t>(meshes.size()));
        body.u32(static_cast<std::uint32_t>(materials.size()));
        // Profile + statistics are embedded so tools can report on a compiled
        // model without decoding every mesh.
        const auto stats = compute_statistics();
        body.string(profile.preset_name);
        body.u32(profile.target_triangles);
        body.u32(profile.max_texture_size);
        body.boolean(profile.generate_lods);
        body.u32(profile.lod_count);
        body.boolean(profile.bake_vertex_lighting);
        body.boolean(profile.weld_vertices);
        body.f32(profile.simplify_ratio);
        body.u32(stats.vertex_count);
        body.u32(stats.triangle_count);
        body.u32(stats.bone_count);
        body.u32(stats.draw_call_estimate);
    });

    for (const auto& material : materials) {
        w.chunk(kChunkMatl, [&](BinaryWriter& body) {
            body.string(material.material_asset);
            body.string(material.slot_name);
            body.string(material.shader_preset);
        });
    }

    const bool skinned = skeleton.has_value();
    for (const auto& mesh : meshes) {
        w.chunk(kChunkMesh, [&](BinaryWriter& body) {
            const std::uint32_t flags = mesh_vertex_flags(mesh, skinned);
            body.string(mesh.name);
            body.u8(mesh.lod);
            body.u8(static_cast<std::uint8_t>(mesh.topology));
            body.u16(mesh.material_slot);
            body.u32(flags);
            body.aabb(mesh.bounds);
            body.u32(static_cast<std::uint32_t>(mesh.vertices.size()));
            for (const auto& vertex : mesh.vertices) write_vertex(body, vertex, flags);
            body.u32(static_cast<std::uint32_t>(mesh.indices.size()));
            for (auto index : mesh.indices) body.u32(index);
        });
    }

    if (skeleton) {
        w.chunk(kChunkSkel, [&](BinaryWriter& body) {
            body.string(skeleton->name);
            body.u32(static_cast<std::uint32_t>(skeleton->bones.size()));
            for (const auto& bone : skeleton->bones) {
                body.string(bone.name);
                body.i32(bone.parent);
                body.mat4(bone.inverse_bind);
                body.vec3(bone.local_translation);
                body.vec3(bone.local_rotation_euler);
                body.vec3(bone.local_scale);
            }
        });
    }

    if (!animations.empty()) {
        w.chunk(kChunkAnim, [&](BinaryWriter& body) {
            body.u32(static_cast<std::uint32_t>(animations.size()));
            for (const auto& animation : animations) {
                body.string(animation.animation_asset);
                body.string(animation.name);
                body.boolean(animation.looping);
            }
        });
    }

    if (!collision.empty()) {
        w.chunk(kChunkColl, [&](BinaryWriter& body) {
            body.u32(static_cast<std::uint32_t>(collision.size()));
            for (const auto& shape : collision) write_collision(body, shape);
        });
    }

    if (!import_settings_json.empty() || !build_report_json.empty()) {
        w.chunk(kChunkMeta, [&](BinaryWriter& body) {
            body.string(import_settings_json);
            body.string(build_report_json);
        });
    }

    return encode_document(kModelFormat, 0, w.bytes());
}

core::Result<ModelDocument> ModelDocument::deserialize(const std::vector<std::uint8_t>& bytes) const
{
    core::DiagnosticList diagnostics;
    std::uint32_t flags = 0;
    core::Version version;
    auto payload = decode_document(kModelFormat, bytes, &flags, &version);
    if (!payload) return core::Result<ModelDocument>(payload.diagnostics());
    diagnostics.append(payload.diagnostics());

    ModelDocument document;
    BinaryReader reader(payload.value().data(), payload.value().size(), &diagnostics);

    while (reader.ok()) {
        std::uint32_t chunk_id = 0;
        std::uint32_t chunk_size = 0;
        if (!reader.next_chunk(chunk_id, chunk_size)) break;
        const std::size_t chunk_end = reader.position() + chunk_size;

        if (chunk_id == kChunkInfo) {
            document.asset_id = reader.string();
            document.name = reader.string();
            document.source_path = reader.string();
            document.source_format = reader.string();
            document.importer = reader.string();
            document.importer_version = reader.string();
            document.bounds = reader.aabb();
            (void)reader.u32();  // declared mesh count (authoritative count comes from chunks)
            (void)reader.u32();  // declared material count
            document.profile.preset_name = reader.string();
            document.profile.target_triangles = reader.u32();
            document.profile.max_texture_size = reader.u32();
            document.profile.generate_lods = reader.boolean();
            document.profile.lod_count = reader.u32();
            document.profile.bake_vertex_lighting = reader.boolean();
            document.profile.weld_vertices = reader.boolean();
            document.profile.simplify_ratio = reader.f32();
            (void)reader.u32();  // stored vertex count
            (void)reader.u32();  // stored triangle count
            (void)reader.u32();  // stored bone count
            (void)reader.u32();  // stored draw-call estimate
        } else if (chunk_id == kChunkMatl) {
            MaterialBinding material;
            material.material_asset = reader.string();
            material.slot_name = reader.string();
            material.shader_preset = reader.string();
            document.materials.push_back(std::move(material));
        } else if (chunk_id == kChunkMesh) {
            ModelMesh mesh;
            mesh.name = reader.string();
            mesh.lod = reader.u8();
            mesh.topology = static_cast<Topology>(reader.u8());
            mesh.material_slot = reader.u16();
            const std::uint32_t vertex_flags = reader.u32();
            mesh.bounds = reader.aabb();
            const std::uint32_t vertex_count = reader.u32();
            if (vertex_count > (1u << 24)) {
                diagnostics.error("BULLY-MODEL-0002", "mesh vertex count is implausible", mesh.name);
                reader.fail("BULLY-MODEL-0002", "implausible vertex count");
                break;
            }
            mesh.vertices.resize(vertex_count);
            for (auto& vertex : mesh.vertices) read_vertex(reader, vertex, vertex_flags);
            const std::uint32_t index_count = reader.u32();
            if (index_count > (1u << 26)) {
                diagnostics.error("BULLY-MODEL-0003", "mesh index count is implausible", mesh.name);
                reader.fail("BULLY-MODEL-0003", "implausible index count");
                break;
            }
            mesh.indices.resize(index_count);
            for (auto& index : mesh.indices) index = reader.u32();
            document.meshes.push_back(std::move(mesh));
        } else if (chunk_id == kChunkSkel) {
            Skeleton parsed_skeleton;
            parsed_skeleton.name = reader.string();
            const std::uint32_t bone_count = reader.u32();
            if (bone_count > 4096) {
                diagnostics.error("BULLY-MODEL-0004", "skeleton bone count exceeds 4096",
                                  parsed_skeleton.name);
                reader.fail("BULLY-MODEL-0004", "implausible bone count");
                break;
            }
            parsed_skeleton.bones.resize(bone_count);
            for (auto& bone : parsed_skeleton.bones) {
                bone.name = reader.string();
                bone.parent = reader.i32();
                bone.inverse_bind = reader.mat4();
                bone.local_translation = reader.vec3();
                bone.local_rotation_euler = reader.vec3();
                bone.local_scale = reader.vec3();
            }
            document.skeleton = std::move(parsed_skeleton);
        } else if (chunk_id == kChunkAnim) {
            const std::uint32_t count = reader.u32();
            document.animations.resize(std::min<std::uint32_t>(count, 4096));
            for (auto& animation : document.animations) {
                animation.animation_asset = reader.string();
                animation.name = reader.string();
                animation.looping = reader.boolean();
            }
        } else if (chunk_id == kChunkColl) {
            const std::uint32_t count = reader.u32();
            document.collision.resize(std::min<std::uint32_t>(count, 1024));
            for (auto& shape : document.collision) shape = read_collision(reader);
        } else if (chunk_id == kChunkMeta) {
            document.import_settings_json = reader.string();
            document.build_report_json = reader.string();
        } else {
            diagnostics.info("BULLY-MODEL-0005",
                             "skipping unknown chunk '" + fourcc_to_string(chunk_id) + "'");
        }

        // Always resynchronise on the declared chunk boundary. This keeps the
        // reader tolerant of chunks written by newer minor versions.
        if (reader.ok()) reader.seek(chunk_end);
    }

    if (!reader.ok() || diagnostics.has_errors()) {
        if (diagnostics.empty()) {
            diagnostics.error("BULLY-MODEL-0001", "failed to decode model document");
        }
        return core::Result<ModelDocument>(std::move(diagnostics));
    }
    if (document.asset_id.empty()) {
        diagnostics.error("BULLY-MODEL-0006", "model document is missing its asset id");
        return core::Result<ModelDocument>(std::move(diagnostics));
    }
    auto decoded = core::Result<ModelDocument>(std::move(document));
    // Warnings collected while decoding (unknown chunks, newer minor versions)
    // travel with the successful result so callers can surface them.
    decoded.attach_diagnostics(std::move(diagnostics));
    return decoded;
}

core::Status ModelDocument::validate() const
{
    core::DiagnosticList diagnostics;
    if (asset_id.empty()) diagnostics.error("BULLY-MODEL-0006", "model has no asset id");
    if (meshes.empty()) diagnostics.warn("BULLY-MODEL-0007", "model contains no meshes", name);
    for (const auto& mesh : meshes) {
        if (mesh.vertices.empty()) {
            diagnostics.warn("BULLY-MODEL-0008", "mesh '" + mesh.name + "' has no vertices", name);
        }
        if (mesh.topology == Topology::Triangles && mesh.indices.size() % 3 != 0) {
            diagnostics.warn("BULLY-MODEL-0009",
                             "mesh '" + mesh.name + "' triangle index count is not a multiple of 3",
                             name);
        }
        for (auto index : mesh.indices) {
            if (index >= mesh.vertices.size()) {
                diagnostics.error("BULLY-MODEL-0010",
                                  "mesh '" + mesh.name + "' index " + std::to_string(index) +
                                      " out of range (" + std::to_string(mesh.vertices.size()) +
                                      " vertices)",
                                  name);
                break;
            }
        }
        if (mesh.material_slot != 0xFFFF && mesh.material_slot >= materials.size()) {
            diagnostics.warn("BULLY-MODEL-0011",
                             "mesh '" + mesh.name + "' references missing material slot " +
                                 std::to_string(mesh.material_slot),
                             name);
        }
    }
    if (skeleton) {
        auto skeleton_status = skeleton->validate(name);
        diagnostics.append(skeleton_status.diagnostics());
    }
    if (diagnostics.has_errors()) return core::Status(std::move(diagnostics));
    // Warnings are part of the report even when the document is valid.
    auto status = core::Status{};
    status.attach_diagnostics(std::move(diagnostics));
    return status;
}

std::string ModelDocument::to_json() const
{
    using core::json::Value;
    const auto stats = compute_statistics();
    Value root = Value::make_object();
    root["format"] = "bmodel";
    root["version"] = kModelFormat.version.to_string();
    root["asset_id"] = asset_id;
    root["name"] = name;
    root["source_path"] = source_path;
    root["source_format"] = source_format;
    root["importer"] = importer;
    root["importer_version"] = importer_version;

    Value stats_json = Value::make_object();
    stats_json["meshes"] = static_cast<std::uint64_t>(stats.mesh_count);
    stats_json["vertices"] = static_cast<std::uint64_t>(stats.vertex_count);
    stats_json["triangles"] = static_cast<std::uint64_t>(stats.triangle_count);
    stats_json["lods"] = static_cast<std::uint64_t>(stats.lod_count);
    stats_json["bones"] = static_cast<std::uint64_t>(stats.bone_count);
    stats_json["draw_calls"] = static_cast<std::uint64_t>(stats.draw_call_estimate);
    root["statistics"] = std::move(stats_json);

    Value profile_json = Value::make_object();
    profile_json["preset"] = profile.preset_name;
    profile_json["target_triangles"] = profile.target_triangles;
    profile_json["max_texture_size"] = profile.max_texture_size;
    profile_json["generate_lods"] = profile.generate_lods;
    profile_json["lod_count"] = profile.lod_count;
    profile_json["bake_vertex_lighting"] = profile.bake_vertex_lighting;
    profile_json["simplify_ratio"] = static_cast<double>(profile.simplify_ratio);
    root["profile"] = std::move(profile_json);

    Value meshes_json = Value::make_array();
    for (const auto& mesh : meshes) {
        Value entry = Value::make_object();
        entry["name"] = mesh.name;
        entry["lod"] = static_cast<std::int64_t>(mesh.lod);
        entry["topology"] = mesh.topology == Topology::Triangles
                                ? "triangles"
                                : (mesh.topology == Topology::Lines ? "lines" : "points");
        entry["vertices"] = static_cast<std::uint64_t>(mesh.vertices.size());
        entry["triangles"] = mesh.triangle_count();
        entry["material_slot"] = static_cast<std::int64_t>(mesh.material_slot);
        meshes_json.push(std::move(entry));
    }
    root["meshes"] = std::move(meshes_json);

    Value materials_json = Value::make_array();
    for (const auto& material : materials) {
        Value entry = Value::make_object();
        entry["slot"] = material.slot_name;
        entry["asset"] = material.material_asset;
        entry["shader_preset"] = material.shader_preset;
        materials_json.push(std::move(entry));
    }
    root["materials"] = std::move(materials_json);

    if (skeleton) {
        Value bones = Value::make_array();
        for (const auto& bone : skeleton->bones) {
            Value entry = Value::make_object();
            entry["name"] = bone.name;
            entry["parent"] = static_cast<std::int64_t>(bone.parent);
            bones.push(std::move(entry));
        }
        root["skeleton"] = Value::make_object();
        root["skeleton"]["name"] = skeleton->name;
        root["skeleton"]["bones"] = std::move(bones);
    }

    Value collision_json = Value::make_array();
    for (const auto& shape : collision) {
        Value entry = Value::make_object();
        entry["name"] = shape.name;
        entry["kind"] = static_cast<std::int64_t>(shape.kind);
        collision_json.push(std::move(entry));
    }
    root["collision"] = std::move(collision_json);

    Value animations_json = Value::make_array();
    for (const auto& animation : animations) {
        Value entry = Value::make_object();
        entry["asset"] = animation.animation_asset;
        entry["name"] = animation.name;
        entry["looping"] = animation.looping;
        animations_json.push(std::move(entry));
    }
    root["animations"] = std::move(animations_json);
    root["valid"] = validate().has_value();
    return root.dump(2);
}

}  // namespace bully::formats
