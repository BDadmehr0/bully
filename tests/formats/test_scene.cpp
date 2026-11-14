#include <catch2/catch_test_macros.hpp>

#include "bully/formats/scene.h"

using namespace bully::formats;

namespace {

SceneDocument make_scene()
{
    SceneDocument document;
    document.asset_id = "scene-1";
    document.name = "schoolyard";
    document.environment.fog_enabled = true;

    SceneEntity root;
    root.entity_id = "ent.root";
    root.name = "Root";
    SceneEntity child;
    child.entity_id = "ent.child";
    child.name = "Child";
    child.parent = "ent.root";
    child.transform.position = Vec3f{1.0f, 0.0f, 2.0f};
    child.model_asset = "model-1";
    EntityComponent component;
    component.type = "bully.render.mesh";
    component.data_json = "{\"cast_shadows\":true}";
    child.components.push_back(component);
    document.entities = {root, child};

    SceneLight light;
    light.light_id = "light.a";
    light.type = LightType::Directional;
    light.intensity = 1.5f;
    document.lights.push_back(light);

    TriggerVolume trigger;
    trigger.trigger_id = "trg.a";
    trigger.kind = "mission";
    trigger.payload_json = "{\"mission\":\"m1\"}";
    document.triggers.push_back(trigger);

    NavNode a;
    a.node_id = "nav.a";
    a.neighbors = {"nav.b"};
    NavNode b;
    b.node_id = "nav.b";
    b.neighbors = {"nav.a"};
    document.navigation.nodes = {a, b};

    SpawnPoint spawn;
    spawn.spawn_id = "spawn.player";
    spawn.kind = "player";
    spawn.position = Vec3f{0.0f, 1.0f, 0.0f};
    document.spawns.push_back(spawn);
    return document;
}

}  // namespace

TEST_CASE("bscene round-trips entities, hierarchy and components", "[formats][scene]")
{
    const auto original = make_scene();
    auto decoded = SceneDocument{}.deserialize(original.serialize());
    REQUIRE(decoded.has_value());
    const auto& scene = decoded.value();
    CHECK(scene.name == "schoolyard");
    REQUIRE(scene.entities.size() == 2);
    REQUIRE(scene.entities[1].components.size() == 1);
    CHECK(scene.entities[1].components[0].type == "bully.render.mesh");
    CHECK(scene.find_entity("ent.child")->parent == "ent.root");
    CHECK(scene.find_entity("ent.child")->transform.position.z == 2.0f);
    REQUIRE(scene.lights.size() == 1);
    CHECK(scene.lights[0].type == LightType::Directional);
    CHECK(scene.navigation.nodes.size() == 2);
    CHECK(scene.validate().has_value());
}

TEST_CASE("Scene bounds cover child transforms with padding", "[formats][scene]")
{
    const auto scene = make_scene();
    const Aabb bounds = scene.compute_bounds();
    CHECK(bounds.min.x <= 1.0f);
    CHECK(bounds.max.z >= 2.0f);
    CHECK(bounds.max.x >= bounds.min.x);
}

TEST_CASE("Scene validation rejects cycles and missing parents", "[formats][scene]")
{
    auto scene = make_scene();
    scene.entities[0].parent = "ent.child";  // root -> child -> root
    CHECK_FALSE(scene.validate().has_value());

    scene = make_scene();
    scene.entities[1].parent = "ent.absent";
    CHECK_FALSE(scene.validate().has_value());

    scene = make_scene();
    scene.entities[1].parent = "ent.child";
    CHECK_FALSE(scene.validate().has_value());
}

TEST_CASE("Scene validation rejects malformed component JSON", "[formats][scene]")
{
    auto scene = make_scene();
    scene.entities[1].components[0].data_json = "{not json";
    CHECK_FALSE(scene.validate().has_value());

    scene = make_scene();
    scene.entities[1].components[0].type.clear();
    // Untyped components only warn.
    CHECK(scene.validate().has_value());
}

TEST_CASE("Transform composes into a column-major matrix", "[formats][scene]")
{
    Transform transform;
    transform.position = Vec3f{1.0f, 2.0f, 3.0f};
    const Mat4f matrix = transform.to_matrix();
    // Translation lives in the last column of a column-major matrix.
    CHECK(matrix.m[12] == 1.0f);
    CHECK(matrix.m[13] == 2.0f);
    CHECK(matrix.m[14] == 3.0f);
    CHECK(matrix.m[0] == 1.0f);
}
