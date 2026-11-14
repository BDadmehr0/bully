// Bully Studio - .bscene : compiled scene / level.
//
// Scenes are stored independently of the game executable: the level editor
// writes .bscene, the runtime loads it. Entity component payloads are JSON so
// plugins can register new component kinds without a format revision.
//
// v1.0 chunks:
//   INFO  identity, environment settings, entity count
//   ENT   one chunk per entity: transform, parent, component list (JSON)
//   LITE  placed lights
//   TRIG  trigger volumes (missions, cutscenes, streaming)
//   NAVM  navigation data (navmesh triangle soup or nav-graph nodes)
//   SPAW  spawn points and camera markers
//   META  JSON metadata (authoring notes, editor camera state)
//
// Diagnostic codes: BULLY-SCENE-0001..0029
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/formats/binary.h"

namespace bully::formats {

inline constexpr FormatDescriptor kSceneFormat{
    fourcc("BSCN"), "bscene", {1, 0, 0}, ".bscene",
    "Scene/level: entities, components, lights, triggers, nav data, spawns"};

struct Transform {
    Vec3f position{};
    Vec3f rotation_euler{};
    Vec3f scale{1.0f, 1.0f, 1.0f};

    [[nodiscard]] Mat4f to_matrix() const;
};

/// Extensible component entry: `type` is namespaced (e.g. "bully.render.mesh",
/// "studio.plugin.my_component") and `data` is JSON owned by the component's
/// registered serializer.
struct EntityComponent {
    std::string type;
    std::string data_json;
};

struct SceneEntity {
    std::string entity_id;   // stable id within the scene
    std::string name;        // display name
    std::string prefab;      // optional prefab asset id
    std::string model_asset; // optional .bmodel asset id
    std::string parent;      // parent entity id (empty = root)
    Transform transform;
    bool visible = true;
    bool static_entity = false;
    std::vector<EntityComponent> components;
};

enum class LightType : std::uint8_t { Directional = 0, Point = 1, Spot = 2, Ambient = 3 };

struct SceneLight {
    std::string light_id;
    LightType type = LightType::Point;
    Vec3f position{};
    Vec3f direction{0.0f, -1.0f, 0.0f};
    Vec3f color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float radius = 5.0f;
    float spot_angle_degrees = 45.0f;
    bool cast_shadows = false;
    bool baked = false;
};

struct TriggerVolume {
    std::string trigger_id;
    std::string kind;  // "mission", "cutscene", "stream", "audio", plugin-defined
    Vec3f center{};
    Vec3f extents{1.0f, 1.0f, 1.0f};
    std::string payload_json;
    bool once = false;
};

struct NavNode {
    std::string node_id;
    Vec3f position{};
    std::vector<std::string> neighbors;
    float radius = 0.5f;
};

struct NavMeshTriangle {
    Vec3f a{};
    Vec3f b{};
    Vec3f c{};
    std::uint32_t flags = 0;
};

struct NavigationData {
    std::string type = "navgraph";  // "navgraph" or "navmesh"
    std::vector<NavNode> nodes;
    std::vector<NavMeshTriangle> triangles;
};

struct SpawnPoint {
    std::string spawn_id;
    std::string kind;  // "player", "npc", "vehicle", "camera", "mission"
    Vec3f position{};
    Vec3f rotation_euler{};
    std::string model_asset;
    std::string payload_json;
};

struct EnvironmentSettings {
    Vec3f ambient_color{0.2f, 0.2f, 0.25f};
    Vec3f fog_color{0.5f, 0.5f, 0.55f};
    float fog_start = 50.0f;
    float fog_end = 200.0f;
    bool fog_enabled = false;
    float time_of_day = 12.0f;
    std::string skybox_texture;
    std::uint32_t lightmap_resolution = 1024;
};

struct SceneDocument {
    std::string asset_id;
    std::string name;
    std::string source_path;
    EnvironmentSettings environment;
    std::vector<SceneEntity> entities;
    std::vector<SceneLight> lights;
    std::vector<TriggerVolume> triggers;
    NavigationData navigation;
    std::vector<SpawnPoint> spawns;
    std::string metadata_json;

    [[nodiscard]] const SceneEntity* find_entity(std::string_view entity_id) const;
    /// Bounds of the scene computed from entity transforms (+1 unit padding).
    [[nodiscard]] Aabb compute_bounds() const;

    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    [[nodiscard]] core::Result<SceneDocument> deserialize(const std::vector<std::uint8_t>& bytes) const;
    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] std::string to_json() const;
};

[[nodiscard]] std::string_view to_string(LightType type) noexcept;

}  // namespace bully::formats
