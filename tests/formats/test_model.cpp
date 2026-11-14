#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "bully/formats/model.h"

using namespace bully::formats;
namespace core = bully::core;

namespace {

ModelDocument make_triangle_model()
{
    ModelDocument document;
    document.asset_id = "11111111-1111-4111-8111-111111111111";
    document.name = "triangle";
    document.source_path = "assets/models/triangle.obj";
    document.source_format = "obj";
    document.importer = "test";

    ModelMesh mesh;
    mesh.name = "triangle";
    mesh.vertices.resize(3);
    mesh.vertices[1].position = Vec3f{1.0f, 0.0f, 0.0f};
    mesh.vertices[2].position = Vec3f{0.0f, 1.0f, 0.0f};
    mesh.indices = {0, 1, 2};
    mesh.bounds = Aabb{Vec3f{0.0f, 0.0f, 0.0f}, Vec3f{1.0f, 1.0f, 0.0f}};
    document.meshes.push_back(mesh);
    document.bounds = mesh.bounds;

    MaterialBinding material;
    material.slot_name = "triangle_mat";
    material.shader_preset = "retro.unlit";
    document.materials.push_back(material);
    document.meshes.front().material_slot = 0;
    return document;
}

}  // namespace

TEST_CASE("bmodel round-trips geometry, materials and statistics", "[formats][model]")
{
    const auto original = make_triangle_model();
    const auto bytes = original.serialize();

    auto decoded = ModelDocument{}.deserialize(bytes);
    REQUIRE(decoded.has_value());
    const auto& model = decoded.value();
    CHECK(model.asset_id == original.asset_id);
    CHECK(model.name == "triangle");
    REQUIRE(model.meshes.size() == 1);
    CHECK(model.meshes[0].indices == std::vector<std::uint32_t>{0, 1, 2});
    CHECK(model.meshes[0].vertices[1].position.x == 1.0f);
    REQUIRE(model.materials.size() == 1);
    CHECK(model.materials[0].slot_name == "triangle_mat");
    const auto statistics = model.compute_statistics();
    CHECK(statistics.triangle_count == 1);
    CHECK(statistics.vertex_count == 3);
    CHECK(model.validate().has_value());
}

TEST_CASE("bmodel preserves skeleton and animation references", "[formats][model]")
{
    auto document = make_triangle_model();
    Skeleton skeleton;
    skeleton.name = "rig";
    Bone root;
    root.name = "root";
    Bone child;
    child.name = "spine";
    child.parent = 0;
    skeleton.bones = {root, child};
    document.skeleton = skeleton;
    AnimationReference reference;
    reference.animation_asset = "anim-id";
    reference.name = "idle";
    reference.looping = true;
    document.animations.push_back(reference);

    auto decoded = ModelDocument{}.deserialize(document.serialize());
    REQUIRE(decoded.has_value());
    REQUIRE(decoded.value().skeleton.has_value());
    CHECK(decoded.value().skeleton->bones.size() == 2);
    CHECK(decoded.value().skeleton->bones[1].parent == 0);
    REQUIRE(decoded.value().animations.size() == 1);
    CHECK(decoded.value().animations[0].looping);
}

TEST_CASE("bmodel validation catches bad indices and missing ids", "[formats][model]")
{
    auto document = make_triangle_model();
    document.meshes[0].indices = {0, 1, 99};
    auto invalid = document.validate();
    CHECK_FALSE(invalid.has_value());

    document = make_triangle_model();
    document.asset_id.clear();
    CHECK_FALSE(document.validate().has_value());
}

TEST_CASE("Skeleton validation rejects cycles and bad parents", "[formats][model]")
{
    Skeleton skeleton;
    Bone a;
    a.name = "a";
    a.parent = 1;
    Bone b;
    b.name = "b";
    b.parent = 0;
    skeleton.bones = {a, b};
    CHECK_FALSE(skeleton.validate().has_value());

    skeleton.bones[1].parent = 5;
    CHECK_FALSE(skeleton.validate().has_value());

    skeleton.bones[1].parent = -1;
    CHECK(skeleton.validate().has_value());
}

TEST_CASE("bmodel tolerates unknown chunks from newer minor versions", "[formats][model]")
{
    // Write a document, then append an unknown chunk inside the payload and fix
    // up the header so the CRC still matches: this is exactly what a newer minor
    // version that added a chunk would produce.
    auto document = make_triangle_model();
    auto bytes = document.serialize();
    BinaryWriter extra;
    extra.chunk(fourcc("FUTR"), [](BinaryWriter& body) { body.u32(1234); });

    // Rebuild the document with the extra chunk appended to the payload.
    BinaryWriter payload;
    const std::size_t header = kHeaderSize;
    std::vector<std::uint8_t> body(bytes.begin() + static_cast<std::ptrdiff_t>(header), bytes.end());
    body.insert(body.end(), extra.bytes().begin(), extra.bytes().end());
    const auto rebuilt = encode_document(kModelFormat, 0, body);
    auto decoded = ModelDocument{}.deserialize(rebuilt);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().meshes.size() == 1);
    bool saw_info = false;
    for (const auto& item : decoded.diagnostics().all()) {
        if (item.severity == core::Severity::Info) saw_info = true;
    }
    CHECK(saw_info);
}

TEST_CASE("bmodel rejects truncated files", "[formats][model]")
{
    auto bytes = make_triangle_model().serialize();
    bytes.resize(bytes.size() / 2);
    auto decoded = ModelDocument{}.deserialize(bytes);
    CHECK_FALSE(decoded.has_value());
}
