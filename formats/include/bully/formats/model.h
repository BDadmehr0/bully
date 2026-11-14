// Bully Studio - .bmodel : compiled 3D model.
//
// Version 1.0 chunks:
//   INFO  asset identity, bounds, source provenance, retro statistics
//   MTRL  material bindings (asset id + slot name + shader preset)
//   MESH  one chunk per mesh (vertices, indices, LOD level, material slot)
//   SKEL  optional skeleton with bone hierarchy + inverse bind matrices
//   ANIM  optional list of animation asset ids that target this rig
//   COLL  optional collision geometry
//   LITE  optional per-vertex baked lighting (AO) produced by the pipeline
//   META  importer settings + QA report (JSON text, informational)
//
// Compatibility: readers skip unknown chunks; a missing chunk means documented
// defaults (no skeleton, no collision, no baked light). Adding chunks is a
// minor version bump; changing an existing chunk layout is a major bump.
//
// Diagnostic codes: BULLY-MODEL-0001..0029
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/formats/binary.h"

namespace bully::formats {

inline constexpr FormatDescriptor kModelFormat{
    fourcc("BMOD"), "bmodel", {1, 0, 0}, ".bmodel",
    "Compiled 3D model: meshes, LODs, materials, rig, collision, baked lighting"};

/// Vertex component bitmask stored in INFO.flags and per mesh.
enum VertexFlags : std::uint32_t {
    kVertexPosition = 1u << 0,
    kVertexNormal = 1u << 1,
    kVertexTangent = 1u << 2,
    kVertexUv0 = 1u << 3,
    kVertexUv1 = 1u << 4,
    kVertexColor = 1u << 5,
    kVertexSkin = 1u << 6,
};

enum class Topology : std::uint8_t { Triangles = 0, TriangleStrip = 1, Lines = 2, Points = 3 };

struct ModelVertex {
    Vec3f position{};
    Vec3f normal{0.0f, 1.0f, 0.0f};
    Vec4f tangent{1.0f, 0.0f, 0.0f, 1.0f};
    Vec2f uv0{};
    Vec2f uv1{};
    Vec4f color{1.0f, 1.0f, 1.0f, 1.0f};
    Vec4f weights{};
    std::array<std::uint16_t, 4> joints{0, 0, 0, 0};
    float ambient_occlusion = 1.0f;  // baked lighting, 0..1
};

struct ModelMesh {
    /// Sub-object name, e.g. "body", "hair", "wheel_fl".
    std::string name;
    std::uint8_t lod = 0;
    Topology topology = Topology::Triangles;
    std::uint16_t material_slot = 0xFFFF;
    std::vector<ModelVertex> vertices;
    std::vector<std::uint32_t> indices;
    Aabb bounds{};

    [[nodiscard]] std::uint32_t triangle_count() const;
};

struct MaterialBinding {
    /// Referenced .bmaterial asset id (uuid string) or empty for a runtime
    /// default material.
    std::string material_asset;
    std::string slot_name;
    std::string shader_preset;
};

struct Bone {
    std::string name;
    std::int32_t parent = -1;
    Mat4f inverse_bind = Mat4f::identity();
    Vec3f local_translation{};
    Vec3f local_rotation_euler{};
    Vec3f local_scale{1.0f, 1.0f, 1.0f};
};

struct Skeleton {
    std::string name;
    std::vector<Bone> bones;

    /// Validates parent indices / cycles. Returns diagnostics, never throws.
    [[nodiscard]] core::Status validate(std::string_view source = {}) const;
};

enum class CollisionKind : std::uint8_t { Box = 0, Sphere = 1, Capsule = 2, ConvexHull = 3, TriangleMesh = 4 };

struct CollisionShape {
    std::string name;
    CollisionKind kind = CollisionKind::Box;
    Vec3f center{};
    Vec3f extents{0.5f, 0.5f, 0.5f};
    float radius = 0.5f;
    float height = 1.0f;
    std::vector<Vec3f> hull_points;  // ConvexHull / TriangleMesh vertices
    std::vector<std::uint32_t> hull_indices;
};

struct AnimationReference {
    std::string animation_asset;
    std::string name;
    bool looping = false;
};

/// Technical characteristics used by the Retro Graphics toolkit reports.
struct ModelStatistics {
    std::uint32_t mesh_count = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t lod_count = 1;
    std::uint32_t bone_count = 0;
    std::uint32_t draw_call_estimate = 0;
    std::uint32_t texture_budget_bytes = 0;
};

/// Retro / technical profile applied when compiling the model. Presets are
/// data driven (see assets/presets), this struct only records what was used.
struct ModelBuildProfile {
    std::string preset_name = "none";
    std::uint32_t target_triangles = 0;  // 0 = unchecked
    std::uint32_t max_texture_size = 0;  // 0 = unchecked
    bool generate_lods = false;
    std::uint32_t lod_count = 1;
    bool bake_vertex_lighting = false;
    bool weld_vertices = true;
    float simplify_ratio = 1.0f;
};

class ModelDocument {
public:
    // ---- identity / provenance ----
    std::string asset_id;      // UUID string, stable for the asset's lifetime
    std::string name;          // display name
    std::string source_path;   // project-relative path of the source asset
    std::string source_format; // "glb", "gltf", "obj", "fbx", ...
    std::string importer;      // importer id that produced this document
    std::string importer_version;

    // ---- content ----
    Aabb bounds{};
    std::vector<ModelMesh> meshes;
    std::vector<MaterialBinding> materials;
    std::optional<Skeleton> skeleton;
    std::vector<AnimationReference> animations;
    std::vector<CollisionShape> collision;
    ModelBuildProfile profile;
    std::string import_settings_json;  // informational echo of importer settings
    std::string build_report_json;     // QA/warning report from the pipeline

    [[nodiscard]] ModelStatistics compute_statistics() const;

    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    [[nodiscard]] core::Result<ModelDocument> deserialize(const std::vector<std::uint8_t>& bytes) const;

    /// Structural validation independent of file encoding.
    [[nodiscard]] core::Status validate() const;

    /// Compact JSON summary for the Studio inspector, CLI `info` and reports.
    [[nodiscard]] std::string to_json() const;
};

}  // namespace bully::formats
