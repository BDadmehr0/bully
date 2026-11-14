#include "bully/formats/scene.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bully/core/json.h"

namespace bully::formats {

namespace {

constexpr std::uint32_t kChunkInfo = fourcc("INFO");
constexpr std::uint32_t kChunkEnt = fourcc("ENT ");
constexpr std::uint32_t kChunkLite = fourcc("LITE");
constexpr std::uint32_t kChunkTrig = fourcc("TRIG");
constexpr std::uint32_t kChunkNavm = fourcc("NAVM");
constexpr std::uint32_t kChunkSpaw = fourcc("SPAW");
constexpr std::uint32_t kChunkMeta = fourcc("META");

void write_transform(BinaryWriter& w, const Transform& transform)
{
    w.vec3(transform.position);
    w.vec3(transform.rotation_euler);
    w.vec3(transform.scale);
}

Transform read_transform(BinaryReader& r)
{
    Transform transform;
    transform.position = r.vec3();
    transform.rotation_euler = r.vec3();
    transform.scale = r.vec3();
    return transform;
}

}  // namespace

std::string_view to_string(LightType type) noexcept
{
    switch (type) {
        case LightType::Directional: return "directional";
        case LightType::Point: return "point";
        case LightType::Spot: return "spot";
        case LightType::Ambient: return "ambient";
    }
    return "point";
}

Mat4f Transform::to_matrix() const
{
    const float cx = std::cos(rotation_euler.x);
    const float sx = std::sin(rotation_euler.x);
    const float cy = std::cos(rotation_euler.y);
    const float sy = std::sin(rotation_euler.y);
    const float cz = std::cos(rotation_euler.z);
    const float sz = std::sin(rotation_euler.z);

    // R = Rz * Ry * Rx (Y-up, right-handed), then scale, then translate.
    Mat4f out;
    out.m[0] = (cy * cz) * scale.x;
    out.m[1] = (cy * sz) * scale.x;
    out.m[2] = (-sy) * scale.x;
    out.m[3] = 0.0f;

    out.m[4] = (sx * sy * cz - cx * sz) * scale.y;
    out.m[5] = (sx * sy * sz + cx * cz) * scale.y;
    out.m[6] = (sx * cy) * scale.y;
    out.m[7] = 0.0f;

    out.m[8] = (cx * sy * cz + sx * sz) * scale.z;
    out.m[9] = (cx * sy * sz - sx * cz) * scale.z;
    out.m[10] = (cx * cy) * scale.z;
    out.m[11] = 0.0f;

    out.m[12] = position.x;
    out.m[13] = position.y;
    out.m[14] = position.z;
    out.m[15] = 1.0f;
    return out;
}

const SceneEntity* SceneDocument::find_entity(std::string_view entity_id) const
{
    for (const auto& entity : entities) {
        if (entity.entity_id == entity_id) return &entity;
    }
    return nullptr;
}

Aabb SceneDocument::compute_bounds() const
{
    Aabb bounds;
    bool first = true;
    for (const auto& entity : entities) {
        const Vec3f& p = entity.transform.position;
        const Vec3f extent{std::fabs(entity.transform.scale.x), std::fabs(entity.transform.scale.y),
                           std::fabs(entity.transform.scale.z)};
        const Vec3f min{p.x - extent.x, p.y - extent.y, p.z - extent.z};
        const Vec3f max{p.x + extent.x, p.y + extent.y, p.z + extent.z};
        if (first) {
            bounds.min = min;
            bounds.max = max;
            first = false;
        } else {
            bounds.min.x = std::min(bounds.min.x, min.x);
            bounds.min.y = std::min(bounds.min.y, min.y);
            bounds.min.z = std::min(bounds.min.z, min.z);
            bounds.max.x = std::max(bounds.max.x, max.x);
            bounds.max.y = std::max(bounds.max.y, max.y);
            bounds.max.z = std::max(bounds.max.z, max.z);
        }
    }
    if (first) {
        bounds.min = Vec3f{-1.0f, 0.0f, -1.0f};
        bounds.max = Vec3f{1.0f, 1.0f, 1.0f};
    }
    return bounds;
}

std::vector<std::uint8_t> SceneDocument::serialize() const
{
    BinaryWriter w;
    w.chunk(kChunkInfo, [&](BinaryWriter& body) {
        body.string(asset_id);
        body.string(name);
        body.string(source_path);
        body.vec3(environment.ambient_color);
        body.vec3(environment.fog_color);
        body.f32(environment.fog_start);
        body.f32(environment.fog_end);
        body.boolean(environment.fog_enabled);
        body.f32(environment.time_of_day);
        body.string(environment.skybox_texture);
        body.u32(environment.lightmap_resolution);
        body.u32(static_cast<std::uint32_t>(entities.size()));
    });
    for (const auto& entity : entities) {
        w.chunk(kChunkEnt, [&](BinaryWriter& body) {
            body.string(entity.entity_id);
            body.string(entity.name);
            body.string(entity.prefab);
            body.string(entity.model_asset);
            body.string(entity.parent);
            write_transform(body, entity.transform);
            body.boolean(entity.visible);
            body.boolean(entity.static_entity);
            body.u32(static_cast<std::uint32_t>(entity.components.size()));
            for (const auto& component : entity.components) {
                body.string(component.type);
                body.string(component.data_json);
            }
        });
    }
    if (!lights.empty()) {
        w.chunk(kChunkLite, [&](BinaryWriter& body) {
            body.u32(static_cast<std::uint32_t>(lights.size()));
            for (const auto& light : lights) {
                body.string(light.light_id);
                body.u8(static_cast<std::uint8_t>(light.type));
                body.vec3(light.position);
                body.vec3(light.direction);
                body.vec3(light.color);
                body.f32(light.intensity);
                body.f32(light.radius);
                body.f32(light.spot_angle_degrees);
                body.boolean(light.cast_shadows);
                body.boolean(light.baked);
            }
        });
    }
    if (!triggers.empty()) {
        w.chunk(kChunkTrig, [&](BinaryWriter& body) {
            body.u32(static_cast<std::uint32_t>(triggers.size()));
            for (const auto& trigger : triggers) {
                body.string(trigger.trigger_id);
                body.string(trigger.kind);
                body.vec3(trigger.center);
                body.vec3(trigger.extents);
                body.string(trigger.payload_json);
                body.boolean(trigger.once);
            }
        });
    }
    if (!navigation.nodes.empty() || !navigation.triangles.empty()) {
        w.chunk(kChunkNavm, [&](BinaryWriter& body) {
            body.string(navigation.type);
            body.u32(static_cast<std::uint32_t>(navigation.nodes.size()));
            for (const auto& node : navigation.nodes) {
                body.string(node.node_id);
                body.vec3(node.position);
                body.f32(node.radius);
                body.u32(static_cast<std::uint32_t>(node.neighbors.size()));
                for (const auto& neighbor : node.neighbors) body.string(neighbor);
            }
            body.u32(static_cast<std::uint32_t>(navigation.triangles.size()));
            for (const auto& triangle : navigation.triangles) {
                body.vec3(triangle.a);
                body.vec3(triangle.b);
                body.vec3(triangle.c);
                body.u32(triangle.flags);
            }
        });
    }
    if (!spawns.empty()) {
        w.chunk(kChunkSpaw, [&](BinaryWriter& body) {
            body.u32(static_cast<std::uint32_t>(spawns.size()));
            for (const auto& spawn : spawns) {
                body.string(spawn.spawn_id);
                body.string(spawn.kind);
                body.vec3(spawn.position);
                body.vec3(spawn.rotation_euler);
                body.string(spawn.model_asset);
                body.string(spawn.payload_json);
            }
        });
    }
    w.chunk(kChunkMeta, [&](BinaryWriter& body) { body.string(metadata_json); });
    return encode_document(kSceneFormat, 0, w.bytes());
}

core::Result<SceneDocument> SceneDocument::deserialize(const std::vector<std::uint8_t>& bytes) const
{
    core::DiagnosticList diagnostics;
    auto payload = decode_document(kSceneFormat, bytes);
    if (!payload) return core::Result<SceneDocument>(payload.diagnostics());
    diagnostics.append(payload.diagnostics());

    SceneDocument document;
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
            document.environment.ambient_color = reader.vec3();
            document.environment.fog_color = reader.vec3();
            document.environment.fog_start = reader.f32();
            document.environment.fog_end = reader.f32();
            document.environment.fog_enabled = reader.boolean();
            document.environment.time_of_day = reader.f32();
            document.environment.skybox_texture = reader.string();
            document.environment.lightmap_resolution = reader.u32();
            (void)reader.u32();  // declared entity count
        } else if (chunk_id == kChunkEnt) {
            SceneEntity entity;
            entity.entity_id = reader.string();
            entity.name = reader.string();
            entity.prefab = reader.string();
            entity.model_asset = reader.string();
            entity.parent = reader.string();
            entity.transform = read_transform(reader);
            entity.visible = reader.boolean();
            entity.static_entity = reader.boolean();
            const std::uint32_t component_count = std::min<std::uint32_t>(reader.u32(), 256);
            entity.components.resize(component_count);
            for (auto& component : entity.components) {
                component.type = reader.string();
                component.data_json = reader.string();
            }
            document.entities.push_back(std::move(entity));
        } else if (chunk_id == kChunkLite) {
            const std::uint32_t count = std::min<std::uint32_t>(reader.u32(), 8192);
            document.lights.resize(count);
            for (auto& light : document.lights) {
                light.light_id = reader.string();
                light.type = static_cast<LightType>(reader.u8());
                light.position = reader.vec3();
                light.direction = reader.vec3();
                light.color = reader.vec3();
                light.intensity = reader.f32();
                light.radius = reader.f32();
                light.spot_angle_degrees = reader.f32();
                light.cast_shadows = reader.boolean();
                light.baked = reader.boolean();
            }
        } else if (chunk_id == kChunkTrig) {
            const std::uint32_t count = std::min<std::uint32_t>(reader.u32(), 8192);
            document.triggers.resize(count);
            for (auto& trigger : document.triggers) {
                trigger.trigger_id = reader.string();
                trigger.kind = reader.string();
                trigger.center = reader.vec3();
                trigger.extents = reader.vec3();
                trigger.payload_json = reader.string();
                trigger.once = reader.boolean();
            }
        } else if (chunk_id == kChunkNavm) {
            document.navigation.type = reader.string();
            const std::uint32_t node_count = std::min<std::uint32_t>(reader.u32(), 1u << 20);
            document.navigation.nodes.resize(node_count);
            for (auto& node : document.navigation.nodes) {
                node.node_id = reader.string();
                node.position = reader.vec3();
                node.radius = reader.f32();
                const std::uint32_t neighbor_count = std::min<std::uint32_t>(reader.u32(), 64);
                node.neighbors.resize(neighbor_count);
                for (auto& neighbor : node.neighbors) neighbor = reader.string();
            }
            const std::uint32_t triangle_count = std::min<std::uint32_t>(reader.u32(), 1u << 21);
            document.navigation.triangles.resize(triangle_count);
            for (auto& triangle : document.navigation.triangles) {
                triangle.a = reader.vec3();
                triangle.b = reader.vec3();
                triangle.c = reader.vec3();
                triangle.flags = reader.u32();
            }
        } else if (chunk_id == kChunkSpaw) {
            const std::uint32_t count = std::min<std::uint32_t>(reader.u32(), 8192);
            document.spawns.resize(count);
            for (auto& spawn : document.spawns) {
                spawn.spawn_id = reader.string();
                spawn.kind = reader.string();
                spawn.position = reader.vec3();
                spawn.rotation_euler = reader.vec3();
                spawn.model_asset = reader.string();
                spawn.payload_json = reader.string();
            }
        } else if (chunk_id == kChunkMeta) {
            document.metadata_json = reader.string();
        } else {
            diagnostics.info("BULLY-SCENE-0001",
                             "skipping unknown scene chunk '" + fourcc_to_string(chunk_id) + "'");
        }
        if (reader.ok()) reader.seek(chunk_end);
    }
    if (!reader.ok() || diagnostics.has_errors()) {
        if (diagnostics.empty()) diagnostics.error("BULLY-SCENE-0002", "failed to decode scene");
        return core::Result<SceneDocument>(std::move(diagnostics));
    }
    auto decoded = core::Result<SceneDocument>(std::move(document));
    // Warnings collected while decoding (unknown chunks, newer minor versions)
    // travel with the successful result so callers can surface them.
    decoded.attach_diagnostics(std::move(diagnostics));
    return decoded;
}

core::Status SceneDocument::validate() const
{
    core::DiagnosticList diagnostics;
    if (asset_id.empty()) diagnostics.error("BULLY-SCENE-0003", "scene has no asset id");
    std::set<std::string> ids;
    for (const auto& entity : entities) {
        if (entity.entity_id.empty()) {
            diagnostics.error("BULLY-SCENE-0004", "scene contains an entity without an id", name);
            continue;
        }
        if (!ids.insert(entity.entity_id).second) {
            diagnostics.error("BULLY-SCENE-0005", "duplicate entity id '" + entity.entity_id + "'", name);
        }
        if (!entity.parent.empty() && entity.parent == entity.entity_id) {
            diagnostics.error("BULLY-SCENE-0006",
                              "entity '" + entity.entity_id + "' is its own parent", name);
        }
        for (const auto& component : entity.components) {
            if (component.type.empty()) {
                diagnostics.warn("BULLY-SCENE-0007",
                                 "entity '" + entity.entity_id + "' has an untyped component", name);
                continue;
            }
            if (!component.data_json.empty()) {
                auto parsed = core::json::parse(component.data_json);
                if (!parsed) {
                    diagnostics.error("BULLY-SCENE-0008",
                                      "component '" + component.type + "' on entity '" +
                                          entity.entity_id + "' contains invalid JSON",
                                      name);
                }
            }
        }
    }
    for (const auto& entity : entities) {
        if (!entity.parent.empty() && ids.find(entity.parent) == ids.end()) {
            diagnostics.error("BULLY-SCENE-0009",
                              "entity '" + entity.entity_id + "' has missing parent '" + entity.parent +
                                  "'",
                              name);
        }
    }
    // Cycle detection over the parent graph.
    for (const auto& entity : entities) {
        std::string cursor = entity.parent;
        std::size_t guard = 0;
        while (!cursor.empty() && guard++ <= entities.size()) {
            if (cursor == entity.entity_id) {
                diagnostics.error("BULLY-SCENE-0010",
                                  "entity hierarchy contains a cycle at '" + entity.entity_id + "'", name);
                break;
            }
            const SceneEntity* parent = find_entity(cursor);
            if (parent == nullptr) break;
            cursor = parent->parent;
        }
    }
    std::set<std::string> light_ids;
    for (const auto& light : lights) {
        if (!light.light_id.empty() && !light_ids.insert(light.light_id).second) {
            diagnostics.warn("BULLY-SCENE-0011", "duplicate light id '" + light.light_id + "'", name);
        }
        if (light.intensity < 0.0f) {
            diagnostics.warn("BULLY-SCENE-0012",
                             "light '" + light.light_id + "' has negative intensity", name);
        }
    }
    std::set<std::string> nav_ids;
    for (const auto& node : navigation.nodes) {
        nav_ids.insert(node.node_id);
    }
    for (const auto& node : navigation.nodes) {
        for (const auto& neighbor : node.neighbors) {
            if (nav_ids.find(neighbor) == nav_ids.end()) {
                diagnostics.warn("BULLY-SCENE-0013",
                                 "nav node '" + node.node_id + "' links to unknown node '" + neighbor +
                                     "'",
                                 name);
            }
        }
    }
    for (const auto& trigger : triggers) {
        if (trigger.kind.empty()) {
            diagnostics.warn("BULLY-SCENE-0014",
                             "trigger '" + trigger.trigger_id + "' has no kind", name);
        }
    }
    if (diagnostics.has_errors()) return core::Status(std::move(diagnostics));
    // Warnings are part of the report even when the document is valid.
    auto status = core::Status{};
    status.attach_diagnostics(std::move(diagnostics));
    return status;
}

std::string SceneDocument::to_json() const
{
    using core::json::Value;
    Value root = Value::make_object();
    root["format"] = "bscene";
    root["version"] = kSceneFormat.version.to_string();
    root["asset_id"] = asset_id;
    root["name"] = name;
    root["source_path"] = source_path;
    root["entity_count"] = static_cast<std::uint64_t>(entities.size());
    root["light_count"] = static_cast<std::uint64_t>(lights.size());
    root["trigger_count"] = static_cast<std::uint64_t>(triggers.size());
    root["spawn_count"] = static_cast<std::uint64_t>(spawns.size());
    root["nav_node_count"] = static_cast<std::uint64_t>(navigation.nodes.size());
    root["nav_triangle_count"] = static_cast<std::uint64_t>(navigation.triangles.size());
    const Aabb bounds = compute_bounds();
    Value bounds_json = Value::make_object();
    Value min_json = Value::make_array();
    min_json.push(static_cast<double>(bounds.min.x));
    min_json.push(static_cast<double>(bounds.min.y));
    min_json.push(static_cast<double>(bounds.min.z));
    Value max_json = Value::make_array();
    max_json.push(static_cast<double>(bounds.max.x));
    max_json.push(static_cast<double>(bounds.max.y));
    max_json.push(static_cast<double>(bounds.max.z));
    bounds_json["min"] = std::move(min_json);
    bounds_json["max"] = std::move(max_json);
    root["bounds"] = std::move(bounds_json);

    Value entities_json = Value::make_array();
    for (const auto& entity : entities) {
        Value entry = Value::make_object();
        entry["id"] = entity.entity_id;
        entry["name"] = entity.name;
        entry["parent"] = entity.parent;
        entry["model"] = entity.model_asset;
        entry["components"] = static_cast<std::uint64_t>(entity.components.size());
        Value position = Value::make_array();
        position.push(static_cast<double>(entity.transform.position.x));
        position.push(static_cast<double>(entity.transform.position.y));
        position.push(static_cast<double>(entity.transform.position.z));
        entry["position"] = std::move(position);
        entities_json.push(std::move(entry));
    }
    root["entities"] = std::move(entities_json);

    Value lights_json = Value::make_array();
    for (const auto& light : lights) {
        Value entry = Value::make_object();
        entry["id"] = light.light_id;
        entry["type"] = std::string(to_string(light.type));
        entry["intensity"] = static_cast<double>(light.intensity);
        lights_json.push(std::move(entry));
    }
    root["lights"] = std::move(lights_json);
    root["valid"] = validate().has_value();
    return root.dump(2);
}

}  // namespace bully::formats
