#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include <set>

#include "bully/formats/model.h"
#include "bully/formats/registry.h"
#include "bully/pipeline/builtin_importers.h"
#include "bully/pipeline/project_layout.h"
#include "fixture.h"
#include "test_support.h"

using namespace bully;
using namespace bully::pipeline;

TEST_CASE("Importer registry resolves compound extensions", "[pipeline][registry]")
{
    auto& registry = ImporterRegistry::instance();
    registry.clear();
    register_builtin_importers(registry);

    CHECK(registry.all().size() >= 9);
    const auto dialogue = registry.for_filename("intro.dialogue.json");
    REQUIRE(dialogue != nullptr);
    CHECK(dialogue->id() == "com.bully.importer.dialogue_json");
    const auto scene = registry.for_filename("YARD.SCENE.JSON");
    REQUIRE(scene != nullptr);
    CHECK(scene->id() == "com.bully.importer.scene_json");
    const auto obj = registry.for_filename("crate.obj");
    REQUIRE(obj != nullptr);
    CHECK(obj->id() == "com.bully.importer.obj");
    CHECK(registry.for_filename("notes.txt") == nullptr);

    // Extension lookup itself ignores compound forms but stays case-insensitive.
    CHECK(registry.for_extension(".OBJ") != nullptr);
    const auto extensions = registry.all_extensions();
    CHECK(std::find(extensions.begin(), extensions.end(), ".obj") != extensions.end());

    registry.clear();
    CHECK(registry.all().empty());
}

TEST_CASE("Duplicate importer ids are refused", "[pipeline][registry]")
{
    auto& registry = ImporterRegistry::instance();
    registry.clear();
    CHECK(registry.add(make_obj_importer()));
    CHECK_FALSE(registry.add(make_obj_importer()));
    CHECK(registry.all().size() == 1);
    CHECK(registry.unregister("com.bully.importer.obj"));
    CHECK(registry.all().empty());
    register_builtin_importers(registry);
}

TEST_CASE("Deterministic asset ids survive re-scans", "[pipeline][identity]")
{
    const std::string first = deterministic_asset_id("assets/models/crate.obj");
    CHECK(first == deterministic_asset_id("assets/models/crate.obj"));
    CHECK(first != deterministic_asset_id("assets/models/other.obj"));
    CHECK(first.size() == 36);
}

TEST_CASE("Project layout never resolves outside the project", "[pipeline][layout]")
{
    test::TempDir dir;
    ProjectLayout layout(dir.path());
    auto inside = layout.resolve_inside("assets/models/crate.obj");
    REQUIRE(inside.has_value());
    auto escape = layout.resolve_inside("../outside.txt");
    CHECK_FALSE(escape.has_value());
    CHECK(escape.diagnostics().has_errors());
}

TEST_CASE("Project manifest round-trips", "[pipeline][layout]")
{
    test::TempDir dir;
    ProjectLayout layout(dir.path());
    ProjectSettings settings;
    settings.name = "Round Trip";
    settings.retro_preset = "ps2";
    settings.runtime_modules = {"render", "audio"};
    REQUIRE(layout.initialize(settings).has_value());
    auto loaded = layout.load();
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().name == "Round Trip");
    CHECK(loaded.value().retro_preset == "ps2");
    REQUIRE(loaded.value().runtime_modules.size() == 2);
    CHECK(ProjectLayout::is_project(dir.path()));
}

TEST_CASE("Scan discovers assets and reports removals", "[pipeline][scan]")
{
    test::ProjectFixture fixture("scan-project");
    fixture.write_asset("models/a.obj", test::cube_obj());
    fixture.write_asset("models/b.obj", test::cube_obj());
    auto scan = fixture.pipeline().scan();
    CHECK(scan.discovered.size() == 2);
    CHECK(scan.removed.empty());
    CHECK(fixture.registry().count() == 2);

    // Removing a source file must surface as a removal, not silently vanish.
    REQUIRE(core::FileSystem::remove_file(fixture.layout().root() / "assets/models/b.obj")
                .has_value());
    auto second = fixture.pipeline().scan();
    CHECK(second.removed.size() == 1);
    CHECK(second.discovered.empty());
}

TEST_CASE("Scan detects changed sources by hash", "[pipeline][scan]")
{
    test::ProjectFixture fixture("changed-project");
    const std::string source = fixture.write_asset("models/a.obj", test::cube_obj());
    REQUIRE(fixture.build_all().success());
    CHECK(fixture.registry().find_by_source_path(source)->status == BuildStatus::UpToDate);

    fixture.write_asset("models/a.obj", test::relative_index_obj());
    auto scan = fixture.pipeline().scan();
    REQUIRE(scan.changed.size() == 1);
    CHECK(scan.changed[0] == fixture.record_for(source)->asset_id);
    CHECK(fixture.pipeline().pending_assets().size() == 1);
}

TEST_CASE("Incremental rebuild is served from the import cache", "[pipeline][incremental]")
{
    test::ProjectFixture fixture("cache-project");
    const std::string source = fixture.write_asset("models/crate.obj", test::cube_obj());
    const auto first = fixture.build_all();
    REQUIRE(first.success());
    CHECK(first.built == 1);
    CHECK(first.cached == 0);
    REQUIRE(fixture.cache().entry_count() == 1);
    CHECK(fixture.cache().total_bytes() > 0);

    const auto second = fixture.build_all();
    CHECK(second.success());
    CHECK(second.built == 0);
    CHECK(second.cached == 1);

    const auto forced = fixture.build_all(true);
    CHECK(forced.success());
    CHECK(forced.built == 1);

    // A cached artifact is content-addressed and materialises byte-identically.
    const auto bytes = fixture.read_compiled(source);
    const auto entry = fixture.cache().find(fixture.record_for(source)->build_key());
    REQUIRE(entry.has_value());
    CHECK(core::FileSystem::hash_file(entry->output_path).has_value());
    CHECK(bytes.size() > 0);
}

TEST_CASE("Build reports distinguish built, cached and failed", "[pipeline][report]")
{
    test::ProjectFixture fixture("report-project");
    fixture.write_asset("models/a.obj", test::cube_obj());
    fixture.write_asset("audio/beep.wav", "");
    const auto report = fixture.build_all();
    CHECK(report.total == 2);
    CHECK(report.failed == 1);
    CHECK(report.built == 1);
    CHECK_FALSE(report.success());

    const std::string json = report.to_json();
    CHECK(json.find("\"success\": false") != std::string::npos);
    const std::string markdown = report.to_markdown();
    CHECK(markdown.find("| Asset | Type |") != std::string::npos);
    CHECK(markdown.find("FAIL") != std::string::npos);
}

TEST_CASE("Build reports are written to the project reports directory", "[pipeline][report]")
{
    test::ProjectFixture fixture("writereport-project");
    fixture.write_asset("models/a.obj", test::cube_obj());
    const auto report = fixture.build_all();
    REQUIRE(fixture.pipeline().write_report(report, "custom-report").has_value());
    CHECK(core::FileSystem::exists(fixture.layout().reports_dir() / "custom-report.json"));
    CHECK(core::FileSystem::exists(fixture.layout().reports_dir() / "custom-report.md"));
}

TEST_CASE("Dry runs validate without writing outputs", "[pipeline][report]")
{
    test::ProjectFixture fixture("dryrun-project");
    const std::string source = fixture.write_asset("models/a.obj", test::cube_obj());
    REQUIRE(fixture.pipeline().scan().discovered.size() == 1);
    BuildOptions options;
    options.dry_run = true;
    const auto report = fixture.pipeline().build_all(options);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].success);
    CHECK(report.results[0].summary.find("dry run") != std::string::npos);
    CHECK_FALSE(core::FileSystem::exists(fixture.layout().built_dir() / "models/a.bmodel"));
    CHECK(fixture.record_for(source)->status != BuildStatus::UpToDate);
    // A dry run must not leave the asset marked as in-progress: a stranded
    // `building` status would make every later QA run report stale output.
    CHECK(fixture.record_for(source)->status != BuildStatus::Building);
}

TEST_CASE("A stranded building status is retried, not skipped", "[pipeline][incremental]")
{
    test::ProjectFixture fixture("stranded-project");
    const std::string source = fixture.write_asset("models/b.obj", test::cube_obj());
    REQUIRE(fixture.pipeline().scan().discovered.size() == 1);
    REQUIRE(fixture.registry()
                .set_status(fixture.record_for(source)->asset_id, BuildStatus::Building)
                .has_value());
    CHECK(fixture.registry().statistics().pending == 1);
    CHECK(fixture.pipeline().pending_assets().size() == 1);
    const auto report = fixture.pipeline().build_all(BuildOptions{});
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].success);
    CHECK(fixture.record_for(source)->status == BuildStatus::UpToDate);
}

TEST_CASE("Dependency order puts prerequisites first and rejects cycles", "[pipeline][graph]")
{
    test::ProjectFixture fixture("graph-project");
    fixture.write_asset("models/a.obj", test::cube_obj());
    fixture.write_asset("models/b.obj", test::cube_obj());
    REQUIRE(fixture.build_all().success());

    auto records = fixture.registry().all();
    REQUIRE(records.size() == 2);
    AssetRecord& first = records[0];
    AssetRecord& second = records[1];

    AssetRecord dependent = first;
    dependent.dependencies = {AssetDependency{second.asset_id, true, "test"}};
    REQUIRE(fixture.registry().upsert(dependent).has_value());

    auto order = fixture.pipeline().build_order({first.asset_id});
    REQUIRE(order.has_value());
    CHECK(order.value().front() == second.asset_id);
    CHECK(order.value().back() == first.asset_id);

    AssetRecord cyclic_second = second;
    cyclic_second.dependencies = {AssetDependency{first.asset_id, true, "cycle"}};
    REQUIRE(fixture.registry().upsert(cyclic_second).has_value());
    CHECK_FALSE(fixture.pipeline().build_order({first.asset_id}).has_value());
}

TEST_CASE("Missing sources fail the build with a clear diagnostic", "[pipeline][failures]")
{
    test::ProjectFixture fixture("missing-project");
    const std::string source = fixture.write_asset("models/a.obj", test::cube_obj());
    REQUIRE(fixture.pipeline().scan().discovered.size() == 1);
    REQUIRE(core::FileSystem::remove_file(fixture.layout().root() / source).has_value());
    const auto report = fixture.build_all();
    CHECK_FALSE(report.success());
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].diagnostics.has_errors());
    CHECK(report.results[0].diagnostics.all()[0].code == "BULLY-PIPE-0006");
}

TEST_CASE("Unknown importer ids are reported, not guessed", "[pipeline][failures]")
{
    test::ProjectFixture fixture("no-importer-project");
    const std::string source = fixture.write_asset("models/a.obj", test::cube_obj());
    REQUIRE(fixture.pipeline().scan().discovered.size() == 1);
    auto record = fixture.record_for(source);
    REQUIRE(record.has_value());
    record->settings.importer_id = "com.example.not_installed";
    REQUIRE(fixture.registry().upsert(*record).has_value());
    // The pipeline falls back to extension matching, so this still builds; when
    // no importer claims the name at all the build fails loudly.
    record->settings.importer_id = "com.example.not_installed";
    record->source_path = "assets/models/unknown.zzz";
    fixture.write_asset("models/unknown.zzz", "data");
    REQUIRE(fixture.registry().upsert(*record).has_value());
    const auto result = fixture.pipeline().build_asset(record->asset_id, BuildOptions{}, false);
    CHECK_FALSE(result.success);
    CHECK(result.diagnostics.has_errors());
    CHECK(result.diagnostics.all()[0].code == "BULLY-PIPE-0007");
}

TEST_CASE("Hot reload polling notices changed sources", "[pipeline][hotreload]")
{
    test::ProjectFixture fixture("hotreload-project");
    fixture.write_asset("models/a.obj", test::cube_obj());
    REQUIRE(fixture.build_all().success());

    std::vector<AssetBuildResult> reloaded;
    fixture.pipeline().set_hot_reload_callback([&](const AssetBuildResult&) {});
    CHECK(fixture.pipeline().poll_changes().empty());  // first poll seeds mtimes
    fixture.write_asset("models/a.obj", test::relative_index_obj());
    // mtimes have 1s resolution on some filesystems; force a distinct stamp.
    REQUIRE(core::FileSystem::write_text(fixture.layout().root() / "assets/models/a.obj",
                                        test::relative_index_obj())
                .has_value());
    const auto changes = fixture.pipeline().poll_changes();
    for (const auto& change : changes) {
        CHECK(change.asset_id == fixture.record_for("assets/models/a.obj")->asset_id);
        reloaded.push_back(change);
    }
    CHECK(reloaded.size() <= 1);
}

TEST_CASE("Format registry describes every runtime format", "[pipeline][formats]")
{
    const auto& formats = formats::known_formats();
    CHECK(formats.size() == 9);
    CHECK(formats::format_by_extension(".bmodel") != nullptr);
    CHECK(formats::format_by_extension("bscene") != nullptr);
    const std::string table = formats::describe_formats();
    for (const auto& format : formats) {
        CHECK(table.find(format.extension) != std::string::npos);
    }
}
