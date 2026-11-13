#include <catch2/catch_test_macros.hpp>

#include "bully/pipeline/builtin_importers.h"
#include "bully/pipeline/import_cache.h"
#include "bully/pipeline/pipeline.h"
#include "bully/pipeline/project_layout.h"
#include "bully/project/asset_registry.h"
#include "bully/project/database.h"
#include "bully/templates/project_templates.h"
#include "test_support.h"

using namespace bully;

TEST_CASE("Template catalogue is data driven and non-empty", "[pipeline][templates]")
{
    const auto& catalog = templates::template_catalog();
    REQUIRE(catalog.size() >= 5);
    for (const auto& info : catalog) {
        CHECK_FALSE(info.name.empty());
        CHECK_FALSE(info.description.empty());
        CHECK_FALSE(info.retro_preset.empty());
        CHECK_FALSE(info.runtime_modules.empty());
    }
    CHECK(templates::find_template("retro-action") != nullptr);
    CHECK(templates::find_template("does-not-exist") == nullptr);
}

TEST_CASE("Scaffolding a project creates the documented layout", "[pipeline][templates]")
{
    test::TempDir dir;
    const auto target = dir / "game";
    auto created = templates::create_project(target, "retro-action", "Demo Game");
    REQUIRE(created.has_value());
    CHECK(created.value().name == "Demo Game");
    CHECK(created.value().template_name == "retro-action");
    CHECK(created.value().retro_preset == "ps2");

    pipeline::ProjectLayout layout(target);
    CHECK(pipeline::ProjectLayout::is_project(target));
    for (const auto& directory : layout.required_directories()) {
        CHECK(core::FileSystem::exists(directory));
    }
    CHECK(core::FileSystem::exists(target / "README.md"));
    CHECK(core::FileSystem::exists(target / "assets/models/crate.obj"));
    CHECK(core::FileSystem::exists(target / "content/dialogue/intro.dialogue.json"));
    CHECK(core::FileSystem::exists(target / "content/scenes/schoolyard.scene.json"));
    CHECK(core::FileSystem::exists(target / "content/missions/m_first_day.mission.json"));

    auto loaded = layout.load();
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().name == "Demo Game");
    CHECK(loaded.value().retro_preset == "ps2");
}

TEST_CASE("Templates refuse to overwrite a non-empty directory", "[pipeline][templates]")
{
    test::TempDir dir;
    const auto target = dir / "occupied";
    REQUIRE(core::FileSystem::write_text(target / "keep.txt", "existing").has_value());
    auto created = templates::create_project(target, "retro-action", "Nope");
    CHECK_FALSE(created.has_value());
    CHECK(created.diagnostics().all()[0].code == "BULLY-TPL-0002");
    CHECK(core::FileSystem::exists(target / "keep.txt"));
}

TEST_CASE("Unknown templates fail with a catalogue hint", "[pipeline][templates]")
{
    test::TempDir dir;
    auto created = templates::create_project(dir / "x", "no-such-template", "X");
    CHECK_FALSE(created.has_value());
    const auto& diagnostics = created.diagnostics().all();
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics[0].code == "BULLY-TPL-0001");
    CHECK(diagnostics[0].message.find("templates") != std::string::npos);
}

TEST_CASE("Scaffolded sample assets are importable by the pipeline", "[pipeline][templates][integration]")
{
    test::TempDir dir;
    const auto target = dir / "playable";
    auto created = templates::create_project(target, "retro-action", "Playable");
    REQUIRE(created.has_value());

    pipeline::ProjectLayout layout(target);
    auto database = project::Database::open(layout.database_path());
    REQUIRE(database.has_value());
    project::AssetRegistry registry(database.value());
    REQUIRE(registry.initialize_schema().has_value());
    pipeline::ImportCache cache(database.value(), layout.cache_dir());
    REQUIRE(cache.initialize().has_value());
    auto& importers = pipeline::ImporterRegistry::instance();
    importers.clear();
    pipeline::register_builtin_importers(importers);
    pipeline::Pipeline pipeline(layout, database.value(), registry, importers, cache);

    const auto scan = pipeline.scan();
    CHECK(scan.discovered.size() >= 4);
    pipeline::BuildOptions options;
    const auto report = pipeline.build_all(options);
    for (const auto& result : report.results) {
        INFO(result.source_path << ": " << result.diagnostics.format());
        CHECK(result.success);
    }
    CHECK(report.success());
    importers.clear();
}
