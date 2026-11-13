#include <catch2/catch_test_macros.hpp>

#include "bully/formats/package.h"
#include "bully/pipeline/packager.h"
#include "fixture.h"
#include "test_support.h"

using namespace bully;
using namespace bully::pipeline;

TEST_CASE("Packager bundles compiled assets with a manifest", "[pipeline][package]")
{
    test::ProjectFixture fixture("package-project");
    fixture.write_asset("models/a.obj", test::cube_obj());
    fixture.write_asset_bytes("textures/tile.png", test::tiny_png());
    REQUIRE(fixture.build_all().success());

    Packager packager(fixture.layout(), fixture.registry());
    PackageOptions options;
    options.package_name = "Demo";
    options.include_timestamp = false;
    const auto result = packager.build_package(options);
    REQUIRE(result.success);
    CHECK(result.entry_count == 2);
    CHECK(result.total_bytes > 0);
    CHECK(core::FileSystem::exists(result.package_path));
    CHECK(core::FileSystem::exists(result.manifest_path));

    auto bytes = core::FileSystem::read_binary(result.package_path);
    REQUIRE(bytes.has_value());
    formats::PackageDocument document;
    auto decoded = document.deserialize(bytes.value());
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().name == "Demo");
    CHECK(decoded.value().entries.size() == 2);
    CHECK(decoded.value().verify().has_value());
    CHECK(decoded.value().manifest_json.find("stale_assets") != std::string::npos);
}

TEST_CASE("Package manifests are reproducible without timestamps", "[pipeline][package]")
{
    test::ProjectFixture fixture("repro-project");
    fixture.write_asset("models/a.obj", test::cube_obj());
    REQUIRE(fixture.build_all().success());
    Packager packager(fixture.layout(), fixture.registry());

    PackageOptions options;
    options.include_timestamp = false;
    options.output_name = "first";
    REQUIRE(packager.build_package(options).success);
    options.output_name = "second";
    REQUIRE(packager.build_package(options).success);

    auto first = core::FileSystem::read_text(fixture.layout().packages_dir() / "first.manifest.json");
    auto second = core::FileSystem::read_text(fixture.layout().packages_dir() / "second.manifest.json");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first.value() == second.value());
}

TEST_CASE("Packager can restrict packaging to specific asset types", "[pipeline][package]")
{
    test::ProjectFixture fixture("filter-project");
    fixture.write_asset("models/a.obj", test::cube_obj());
    fixture.write_asset_bytes("textures/tile.png", test::tiny_png());
    REQUIRE(fixture.build_all().success());

    Packager packager(fixture.layout(), fixture.registry());
    PackageOptions options;
    options.include_types = {AssetType::Texture};
    const auto result = packager.build_package(options);
    REQUIRE(result.success);
    CHECK(result.entry_count == 1);
}

TEST_CASE("Packaging warns about stale and missing outputs", "[pipeline][package]")
{
    test::ProjectFixture fixture("stale-project");
    fixture.write_asset("models/a.obj", test::cube_obj());
    REQUIRE(fixture.build_all().success());
    // Invalidate the build status without rebuilding.
    const auto record = fixture.registry().all().front();
    REQUIRE(fixture.registry().set_status(record.asset_id, BuildStatus::Pending).has_value());

    Packager packager(fixture.layout(), fixture.registry());
    PackageOptions options;
    options.include_stale = true;
    const auto result = packager.build_package(options);
    CHECK(result.success);
    CHECK(result.diagnostics.count(core::Severity::Warning) >= 1);
    bool saw_stale = false;
    for (const auto& item : result.diagnostics.all()) {
        if (item.code == "BULLY-PKG-0022") saw_stale = true;
    }
    CHECK(saw_stale);

    PackageOptions strict;
    strict.include_stale = false;
    const auto excluded = packager.build_package(strict);
    CHECK(excluded.entry_count == 0);
}

TEST_CASE("Packaging an empty project produces a valid empty package", "[pipeline][package]")
{
    test::ProjectFixture fixture("empty-project");
    Packager packager(fixture.layout(), fixture.registry());
    PackageOptions options;
    const auto result = packager.build_package(options);
    CHECK(result.success);
    CHECK(result.entry_count == 0);
    auto bytes = core::FileSystem::read_binary(result.package_path);
    REQUIRE(bytes.has_value());
    formats::PackageDocument document;
    auto decoded = document.deserialize(bytes.value());
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().entries.empty());
}

TEST_CASE("Package manifest can be written standalone", "[pipeline][package]")
{
    test::ProjectFixture fixture("manifest-project");
    fixture.write_asset("models/a.obj", test::cube_obj());
    REQUIRE(fixture.build_all().success());
    Packager packager(fixture.layout(), fixture.registry());
    PackageOptions options;
    REQUIRE(packager.build_package(options).success);

    auto text = core::FileSystem::read_text(fixture.layout().packages_dir() / "game.manifest.json");
    REQUIRE(text.has_value());
    auto manifest = formats::PackageManifest::from_json(text.value());
    REQUIRE(manifest.has_value());
    CHECK(manifest.value().entries.size() == 1);
    CHECK(manifest.value().tool_version == core::kPlatformVersion.to_string());

    formats::PackageManifest copy = manifest.value();
    REQUIRE(packager.write_manifest(copy, fixture.layout().packages_dir() / "copy.json")
                .has_value());
    CHECK(core::FileSystem::exists(fixture.layout().packages_dir() / "copy.json"));
}
