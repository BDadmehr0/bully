#include <catch2/catch_test_macros.hpp>

#include "bully/runtime/asset_database.h"
#include "test_support.h"

using namespace bully;
using namespace bully::runtime;

namespace {

/// Builds a compiled .bmodel in `directory` with a stable asset id.
std::filesystem::path write_model(const std::filesystem::path& directory, std::string name,
                                 std::string asset_id)
{
    formats::ModelDocument document;
    document.asset_id = std::move(asset_id);
    document.name = name;
    formats::ModelMesh mesh;
    mesh.name = "body";
    mesh.vertices.push_back(formats::ModelVertex{});
    mesh.vertices.push_back(formats::ModelVertex{});
    mesh.vertices.push_back(formats::ModelVertex{});
    mesh.indices = {0, 1, 2};
    document.meshes.push_back(std::move(mesh));
    REQUIRE(document.validate().has_value());
    std::filesystem::create_directories(directory / std::filesystem::path(name).parent_path());
    const auto path = directory / (name + ".bmodel");
    REQUIRE(core::FileSystem::write_binary(path, document.serialize()).has_value());
    return path;
}

/// A package holding two compiled models, built the same way the packager does.
std::filesystem::path write_package(const std::filesystem::path& directory)
{
    formats::PackageDocument package;
    package.asset_id = "package.dlc";
    package.name = "dlc";
    formats::ModelDocument first;
    first.asset_id = "dlc.model.a";
    first.name = "a";
    formats::ModelMesh mesh;
    mesh.vertices.resize(3);
    mesh.indices = {0, 1, 2};
    first.meshes.push_back(mesh);
    REQUIRE(package
                .add(formats::PackageEntryKind::Model, "models/a.bmodel", "dlc.model.a",
                     first.serialize())
                .has_value());

    formats::TextureDocument texture;
    texture.asset_id = "dlc.tex.b";
    texture.name = "b";
    texture.width = 2;
    texture.height = 2;
    texture.format = formats::PixelFormat::Rgba8;
    formats::MipLevel mip;
    mip.width = 2;
    mip.height = 2;
    mip.stride = 8;
    mip.pixels.resize(16, 0x80);
    texture.mips.push_back(std::move(mip));
    REQUIRE(package
                .add(formats::PackageEntryKind::Texture, "textures/b.btexture", "dlc.tex.b",
                     texture.serialize())
                .has_value());
    REQUIRE(package.validate().has_value());
    std::filesystem::create_directories(directory);
    const auto path = directory / "dlc.bpackage";
    REQUIRE(core::FileSystem::write_binary(path, package.serialize()).has_value());
    return path;
}

}  // namespace

TEST_CASE("Directory mounts index compiled assets and ignore debris", "[runtime][assets]")
{
    test::TempDir dir;
    const auto built = dir.path() / "built";
    write_model(built, "models/crate", "model.crate");
    dir.write("built/models/readme.txt", "not an asset");
    dir.write("built/reports/qa-report.md", "not an asset");

    AssetDatabase database;
    REQUIRE(database.mount_directory(built).has_value());
    CHECK(database.entry_count() == 1);

    const auto& info = database.mounts();
    REQUIRE(info.size() == 1);
    CHECK(info[0].name == "project");
    CHECK_FALSE(info[0].is_package);

    auto by_path = database.find("models/crate.bmodel");
    REQUIRE(by_path.has_value());
    CHECK(by_path->kind == AssetKind::Model);
    CHECK(by_path->size > 0);
    // Paths are case-insensitive and slash-normalised.
    CHECK(database.find("MODELS\\CRATE.BMODEL").has_value());

    CHECK(database.entries(AssetKind::Model).size() == 1);
    CHECK(database.entries(AssetKind::Texture).empty());
}

TEST_CASE("Typed loaders decode, validate and cache", "[runtime][assets]")
{
    test::TempDir dir;
    const auto built = dir.path() / "built";
    write_model(built, "models/crate", "model.crate");

    AssetDatabase database;
    REQUIRE(database.mount_directory(built).has_value());

    auto model = database.load_model("model.crate");  // by asset id
    REQUIRE(model.has_value());
    CHECK(model.value()->asset_id == "model.crate");
    CHECK(model.value()->meshes.size() == 1);

    // The same document instance is returned: decoding happens once.
    auto again = database.load_model("models/crate.bmodel");
    REQUIRE(again.has_value());
    CHECK(again.value().get() == model.value().get());
    CHECK(database.statistics().decoded == 1);

    // Wrong typed loader on a model: decode fails with a format diagnostic, not
    // a crash or an empty document.
    auto as_texture = database.load_texture("model.crate");
    CHECK_FALSE(as_texture.has_value());
    CHECK_FALSE(as_texture.diagnostics().empty());

    database.clear_caches();
    CHECK(database.statistics().decoded == 0);
    auto reloaded = database.load_model("model.crate");
    REQUIRE(reloaded.has_value());
    CHECK(reloaded.value().get() != model.value().get());
}

TEST_CASE("Missing and unmounted assets report a diagnostic", "[runtime][assets]")
{
    AssetDatabase database;
    auto nothing = database.load_model("nope");
    CHECK_FALSE(nothing.has_value());
    CHECK(nothing.diagnostics().all().front().code == "BULLY-RT-0006");

    test::TempDir dir;
    const auto built = dir.path() / "built";
    write_model(built, "models/crate", "model.crate");
    REQUIRE(database.mount_directory(built).has_value());
    auto missing = database.read("models/ghost.bmodel");
    CHECK_FALSE(missing.has_value());
    CHECK(missing.diagnostics().all().front().code == "BULLY-RT-0003");

    database.unmount("project");
    CHECK(database.entry_count() == 0);
    CHECK(database.mounts().empty());
}

TEST_CASE("Missing mount directories are refused", "[runtime][assets]")
{
    AssetDatabase database;
    auto status = database.mount_directory("/nonexistent/bully/built");
    CHECK_FALSE(status.has_value());
    CHECK(status.diagnostics().all().front().code == "BULLY-RT-0001");
}

TEST_CASE("Packages mount, verify CRCs and load by id and path", "[runtime][assets]")
{
    test::TempDir dir;
    const auto package = write_package(dir.path() / "packages");

    AssetDatabase database;
    REQUIRE(database.mount_package(package).has_value());
    REQUIRE(database.mounts().size() == 1);
    CHECK(database.mounts()[0].is_package);
    CHECK(database.mounts()[0].name == "dlc");

    // Both the asset id and the logical path resolve.
    CHECK(database.find("dlc.model.a").has_value());
    CHECK(database.find("models/a.bmodel").has_value());
    CHECK(database.find("textures/b.btexture").has_value());

    auto model = database.load_model("dlc.model.a");
    REQUIRE(model.has_value());
    CHECK(model.value()->name == "a");

    auto texture = database.load_texture("textures/b.btexture");
    REQUIRE(texture.has_value());
    CHECK(texture.value()->width == 2);

    CHECK(database.statistics().bytes_mounted > 0);
}

TEST_CASE("A corrupt package is refused at mount time", "[runtime][assets]")
{
    test::TempDir dir;
    const auto package = write_package(dir.path() / "packages");
    auto bytes = core::FileSystem::read_binary(package);
    REQUIRE(bytes.has_value());
    // Flip a byte inside the data block: the header still parses, the CRC fails.
    auto corrupted = bytes.value();
    REQUIRE(corrupted.size() > 80);
    corrupted[corrupted.size() - 8] ^= 0xFF;
    const auto broken = dir.path() / "packages/broken.bpackage";
    REQUIRE(core::FileSystem::write_binary(broken, corrupted).has_value());

    AssetDatabase database;
    auto status = database.mount_package(broken);
    CHECK_FALSE(status.has_value());
    CHECK(database.entry_count() == 0);
    CHECK_FALSE(status.diagnostics().empty());
}

TEST_CASE("Later mounts override earlier ones and unmount restores them",
          "[runtime][assets]")
{
    test::TempDir dir;
    const auto project = dir.path() / "project";
    const auto override_dir = dir.path() / "override";
    write_model(project, "models/crate", "model.crate");

    formats::ModelDocument replacement;
    replacement.asset_id = "model.crate";
    replacement.name = "crate_hot_reload";
    formats::ModelMesh mesh;
    mesh.name = "body";
    mesh.vertices.resize(3);
    mesh.indices = {0, 1, 2};
    replacement.meshes.push_back(std::move(mesh));
    // Same logical path as the project asset: this is what a rebuild-and-reload
    // looks like, and it is why later mounts must win.
    std::filesystem::create_directories(override_dir / "models");
    REQUIRE(core::FileSystem::write_binary(override_dir / "models/crate.bmodel",
                                           replacement.serialize())
                .has_value());

    AssetDatabase database;
    REQUIRE(database.mount_directory(project, "project").has_value());
    REQUIRE(database.mount_directory(override_dir, "live").has_value());
    // Two entries with the same logical path: the later mount wins.
    CHECK(database.mounts().size() == 2);
    auto hot = database.load_model("models/crate.bmodel");
    REQUIRE(hot.has_value());
    CHECK(hot.value()->name == "crate_hot_reload");

    database.unmount("live");
    CHECK(database.mounts().size() == 1);
    auto original = database.load_model("models/crate.bmodel");
    REQUIRE(original.has_value());
    CHECK(original.value()->name == "models/crate");
}

TEST_CASE("Scene and dialogue documents load through the runtime", "[runtime][assets]")
{
    test::TempDir dir;
    const auto built = dir.path() / "built";
    std::filesystem::create_directories(built / "scenes");

    formats::SceneDocument scene;
    scene.asset_id = "scene.school";
    scene.name = "school";
    formats::SceneEntity entity;
    entity.entity_id = "ent.player";
    scene.entities.push_back(entity);
    REQUIRE(core::FileSystem::write_binary(built / "scenes/school.bscene", scene.serialize())
                .has_value());

    AssetDatabase database;
    REQUIRE(database.mount_directory(built).has_value());
    auto loaded = database.load_scene("scene.school");
    REQUIRE(loaded.has_value());
    CHECK(loaded.value()->name == "school");
    REQUIRE(loaded.value()->entities.size() == 1);

    auto missing_dialogue = database.load_dialogue("dialogue.intro");
    CHECK_FALSE(missing_dialogue.has_value());
}
