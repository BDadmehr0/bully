#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

#include "bully/formats/model.h"
#include "bully/pipeline/builtin_importers.h"
#include "bully/pipeline/pipeline.h"
#include "fixture.h"
#include "importers/profile_defaults.h"
#include "test_support.h"

using namespace bully;
using namespace bully::pipeline;

TEST_CASE("Retro presets are loaded from data files", "[pipeline][presets]")
{
    // The build defines BULLY_DEFAULT_PRESET_DIR pointing at assets/presets, so
    // a fresh build finds the shipped presets without configuration.
    const auto paths = preset_search_paths();
    REQUIRE_FALSE(paths.empty());

    const auto ps2 = retro_profile("ps2");
    CHECK(ps2.name == "ps2");
    CHECK(ps2.triangle_budget_per_character == 3000);
    CHECK(ps2.max_texture_size == 256);
    CHECK(ps2.pixel_format == "rgba4444");
    CHECK(ps2.generate_lods);
    CHECK(ps2.generate_mips);
    CHECK(ps2.shader_preset == "retro.lambert");
    CHECK(ps2.audio_codec == "adpcm");
    CHECK_FALSE(ps2.source_path.empty());  // a real file was read

    const auto xbox = retro_profile("xbox");
    CHECK(xbox.triangle_budget_per_character == 8000);
    CHECK(xbox.max_texture_size == 512);
    CHECK(xbox.pixel_format == "rgba8");
    CHECK(xbox.allow_per_pixel_lighting);
    CHECK(xbox.sampler == "linear");

    const auto unlit = retro_profile("unlit-retro");
    CHECK(unlit.sampler == "nearest");
    CHECK_FALSE(unlit.generate_mips);
    CHECK(unlit.wrap_mode == "clamp");
    CHECK(unlit.audio_channels == 1);

    const auto none = retro_profile("none");
    CHECK(none.triangle_budget_per_character == 0);
    CHECK(none.generate_mips);
}

TEST_CASE("Unknown presets fall back to unconstrained defaults", "[pipeline][presets]")
{
    const auto unknown = retro_profile("no-such-preset");
    CHECK(unknown.name == "none");
    CHECK(unknown.triangle_budget_per_character == 0);
}

TEST_CASE("Preset names cannot escape the preset directory", "[pipeline][presets]")
{
    // Path traversal is refused before any file is opened; the built-in
    // fallback is still returned so a bad manifest cannot break a build.
    const auto escaped = retro_profile("../../etc/passwd");
    CHECK(escaped.name == "none");
    const auto absolute = retro_profile("/etc/passwd");
    CHECK(absolute.name == "none");
}

TEST_CASE("A preset supplied through BULLY_PRESET_PATH overrides the built-ins",
          "[pipeline][presets]")
{
    test::TempDir dir;
    dir.write("custom.json", R"({
  "name": "custom-test",
  "model": {"triangle_budget_per_character": 123, "generate_lods": true, "lod_count": 2},
  "texture": {"max_size": 64, "sampler": "nearest", "pixel_format": "rgb565"},
  "material": {"shader_preset": "retro.unlit"},
  "audio": {"sample_rate": 11025, "channels": 1, "codec": "pcm8"}
})");
    const std::string previous = std::getenv("BULLY_PRESET_PATH") != nullptr
                                     ? std::getenv("BULLY_PRESET_PATH")
                                     : "";
    ::setenv("BULLY_PRESET_PATH", dir.str().c_str(), 1);
    const auto profile = retro_profile("custom");
    ::setenv("BULLY_PRESET_PATH", previous.c_str(), 1);

    CHECK(profile.name == "custom-test");
    CHECK(profile.triangle_budget_per_character == 123);
    CHECK(profile.max_texture_size == 64);
    CHECK(profile.pixel_format == "rgb565");
    CHECK(profile.sampler == "nearest");
    CHECK(profile.shader_preset == "retro.unlit");
    CHECK(profile.audio_sample_rate == 11025);
    CHECK(profile.lod_count == 2);
}

TEST_CASE("Build profiles derive from import settings and the project preset",
          "[pipeline][presets]")
{
    ImportSettings settings;
    settings.set("retro_preset", "ps2");
    const auto profile = build_profile_from_settings(settings);
    CHECK(profile.preset_name == "ps2");
    CHECK(profile.max_texture_size == 256);
    CHECK(profile.target_triangles == 3000);
    CHECK(profile.generate_lods);
    CHECK(profile.bake_vertex_lighting);

    // Explicit per-asset settings always win over the preset defaults.
    settings.set("generate_lods", "false");
    settings.set("target_triangles", "500");
    const auto overridden = build_profile_from_settings(settings);
    CHECK_FALSE(overridden.generate_lods);
    CHECK(overridden.target_triangles == 500);
}

TEST_CASE("A project's retro preset is applied to scanned assets", "[pipeline][presets]")
{
    test::ProjectFixture fixture("preset-project");
    fixture.write_asset("models/crate.obj", test::cube_obj());

    // Persist the preset in the project manifest, then scan through a fresh
    // pipeline: the project-level preset becomes the asset's importer default.
    auto settings = fixture.layout().load();
    REQUIRE(settings.has_value());
    auto project = settings.value();
    project.retro_preset = "ps2";
    REQUIRE(fixture.layout().save(project).has_value());

    Pipeline pipeline(fixture.layout(), fixture.database(), fixture.registry(),
                      fixture.importers(), fixture.cache());
    const auto scan = pipeline.scan();
    REQUIRE(scan.discovered.size() == 1);
    const auto record = fixture.registry().find(scan.discovered.front().asset_id);
    REQUIRE(record.has_value());
    CHECK(record->settings.get("retro_preset") == "ps2");

    BuildOptions options;
    const auto report = pipeline.build_all(options);
    REQUIRE(report.success());
    const auto built = core::FileSystem::read_binary(
        fixture.layout().root() / fixture.registry().find(record->asset_id)->runtime_path);
    REQUIRE(built.has_value());
    formats::ModelDocument document;
    auto decoded = document.deserialize(built.value());
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().profile.preset_name == "ps2");
    CHECK(decoded.value().profile.max_texture_size == 256);
}
