#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "bully/formats/model.h"
#include "bully/pipeline/builtin_importers.h"
#include "bully/pipeline/importer.h"
#include "bully/plugins/bridge.h"
#include "bully/plugins/host.h"
#include "bully/plugins/manifest.h"
#include "bully/project/database.h"
#include "test_support.h"

#ifndef BULLY_SAMPLE_PLUGIN_BUILD_DIR
#error "BULLY_SAMPLE_PLUGIN_BUILD_DIR must be provided by the test build"
#endif
#ifndef BULLY_SAMPLE_PLUGIN_SOURCE_DIR
#error "BULLY_SAMPLE_PLUGIN_SOURCE_DIR must be provided by the test build"
#endif

using namespace bully;

namespace {

constexpr const char* kSampleId = "com.bully.samples.hello_importer";

std::shared_ptr<plugins::PluginHost> make_host(test::TempDir& state)
{
    auto host = std::make_shared<plugins::PluginHost>();
    plugins::PluginHost::Options options;
    options.plugin_directories = {std::filesystem::path(BULLY_SAMPLE_PLUGIN_BUILD_DIR)};
    options.state_directory = state / "plugin-state";
    options.project_root = state.str();
    host->set_options(std::move(options));
    return host;
}

}  // namespace

TEST_CASE("Plugin manifests describe the sample plugin", "[plugins]")
{
    auto manifest = plugins::PluginManifest::load(std::filesystem::path(BULLY_SAMPLE_PLUGIN_SOURCE_DIR));
    if (!manifest) {
        // The source directory has the template, not the configured manifest;
        // fall back to the generated manifest next to the built library.
        manifest = plugins::PluginManifest::load(std::filesystem::path(BULLY_SAMPLE_PLUGIN_BUILD_DIR));
    }
    REQUIRE(manifest.has_value());
    CHECK(manifest.value().id == kSampleId);
    CHECK(manifest.value().api_major == 1);
    CHECK(manifest.value().reload_safe);
    CHECK(manifest.value().validate().has_value());
}

TEST_CASE("Manifest validation rejects unknown permissions and missing libraries", "[plugins]")
{
    plugins::PluginManifest manifest;
    manifest.id = "com.example.bad";
    manifest.name = "Bad";
    manifest.version = "1.0.0";
    manifest.api_major = 1;
    manifest.library = "libdoesnotexist.so";
    manifest.directory = std::filesystem::path(BULLY_SAMPLE_PLUGIN_BUILD_DIR);
    auto status = manifest.validate();
    CHECK_FALSE(status.has_value());
    CHECK(status.diagnostics().all()[0].code == "BULLY-PLUG-0006");

    manifest.library = "manifest.json";
    manifest.permissions = {"superuser"};
    status = manifest.validate();
    CHECK_FALSE(status.has_value());
    CHECK(status.diagnostics().has_errors());
}

TEST_CASE("Plugin host discovers the sample plugin", "[plugins]")
{
    test::TempDir state;
    auto host = make_host(state);
    const auto found = host->discover();
    REQUIRE(found.size() == 1);
    CHECK(found[0].manifest.id == kSampleId);
    CHECK(found[0].state == plugins::PluginState::Discovered);
    CHECK(found[0].enabled);
}

TEST_CASE("Loading the sample plugin activates it and registers capabilities", "[plugins]")
{
    test::TempDir state;
    auto host = make_host(state);
    host->discover();
    REQUIRE(host->load(kSampleId).has_value());
    const auto info = host->plugin(kSampleId);
    CHECK(info.state == plugins::PluginState::Active);
    CHECK(info.runtime_api_major == 1);

    const auto importers = host->plugin_importers();
    REQUIRE(importers.size() == 1);
    CHECK(importers[0].plugin_id == kSampleId);
    CHECK(importers[0].importer_id == "com.bully.samples.hello_importer.hello");
    CHECK(importers[0].extensions.size() == 2);
    CHECK(importers[0].extensions[0] == ".hello");
    CHECK(host->run_qa_rule("samples.hello_ok").has_value());
}

TEST_CASE("Plugin importers produce a compiled model through the bridge", "[plugins]")
{
    test::TempDir state;
    auto host = make_host(state);
    host->discover();
    REQUIRE(host->load(kSampleId).has_value());

    auto& registry = pipeline::ImporterRegistry::instance();
    registry.clear();
    CHECK(plugins::register_plugin_importers(host, registry) == 1);
    const auto bridge = registry.for_filename("object.hello");
    REQUIRE(bridge != nullptr);
    CHECK(bridge->id() == "com.bully.samples.hello_importer.hello");

    state.write("object.hello", "name: Greeting Object\nvertices: 1.0,2.0,3.0; 4.0,5.0,6.0; 0.0,0.0,0.0\n");
    pipeline::ImportContext context;
    context.project_root = state.str();
    context.relative_source_path = "object.hello";
    context.source_path = (state / "object.hello").string();
    context.asset_id = "plugin-asset";
    context.output_path = (state / "object.bmodel").string();
    auto imported = bridge->import(context);
    REQUIRE(imported.has_value());
    CHECK_FALSE(imported.value().output_bytes.empty());

    formats::ModelDocument document;
    auto decoded = document.deserialize(imported.value().output_bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().asset_id == "plugin-asset");
    REQUIRE(decoded.value().meshes.size() == 1);
    CHECK(decoded.value().compute_statistics().vertex_count == 3);

    registry.clear();
}

TEST_CASE("Broken plugin sources are reported by the plugin importer", "[plugins]")
{
    test::TempDir state;
    auto host = make_host(state);
    host->discover();
    REQUIRE(host->load(kSampleId).has_value());
    auto& registry = pipeline::ImporterRegistry::instance();
    registry.clear();
    plugins::register_plugin_importers(host, registry);
    const auto bridge = registry.for_filename("broken.hello");
    REQUIRE(bridge != nullptr);

    state.write("broken.hello", "this file has no vertices\n");
    pipeline::ImportContext context;
    context.project_root = state.str();
    context.relative_source_path = "broken.hello";
    context.source_path = (state / "broken.hello").string();
    context.asset_id = "x";
    auto imported = bridge->import(context);
    CHECK_FALSE(imported.has_value());
    CHECK(imported.diagnostics().has_errors());
    registry.clear();
}

TEST_CASE("Disabled plugins refuse to load", "[plugins]")
{
    test::TempDir state;
    auto host = make_host(state);
    host->discover();
    REQUIRE(host->enable(kSampleId, false).has_value());
    const auto status = host->load(kSampleId);
    CHECK_FALSE(status.has_value());
    CHECK(host->plugin(kSampleId).state == plugins::PluginState::Disabled);
    REQUIRE(host->enable(kSampleId, true).has_value());
    CHECK(host->plugin(kSampleId).state == plugins::PluginState::Discovered);
}

TEST_CASE("Reload requires a reload-safe plugin and cycles it", "[plugins]")
{
    test::TempDir state;
    auto host = make_host(state);
    host->discover();
    REQUIRE(host->load(kSampleId).has_value());
    REQUIRE(host->reload(kSampleId).has_value());
    CHECK(host->plugin(kSampleId).state == plugins::PluginState::Active);

    REQUIRE(host->unload(kSampleId).has_value());
    CHECK(host->plugin(kSampleId).state == plugins::PluginState::Discovered);
    CHECK(host->plugin_importers().empty());
}

TEST_CASE("A crash marker prevents auto-loading", "[plugins]")
{
    test::TempDir state;
    auto host = make_host(state);
    host->discover();
    REQUIRE(core::FileSystem::write_text(host->options().state_directory /
                                             (std::string(kSampleId) + ".crashed"),
                                         "crashed")
                .has_value());
    const auto status = host->load(kSampleId);
    CHECK_FALSE(status.has_value());
    CHECK(host->plugin(kSampleId).state == plugins::PluginState::Disabled);
    CHECK(host->plugin(kSampleId).error.find("crash") != std::string::npos);
}

TEST_CASE("Missing plugins fail with a discovery hint instead of crashing", "[plugins]")
{
    test::TempDir state;
    auto host = make_host(state);
    const auto status = host->load("com.example.not_there");
    CHECK_FALSE(status.has_value());
    CHECK(status.diagnostics().all()[0].code == "BULLY-PLUG-0018");
}

TEST_CASE("SQLite plugin state store persists enable flags", "[plugins]")
{
    test::TempDir state;
    auto database = project::Database::open(state / "plugins.db");
    REQUIRE(database.has_value());
    plugins::SqlitePluginStateStore store(database.value());
    REQUIRE(store.initialize().has_value());
    CHECK(store.enabled(kSampleId, true));
    store.set_enabled(kSampleId, false);
    CHECK_FALSE(store.enabled(kSampleId, true));
    store.set_error(kSampleId, "test failure");

    plugins::SqlitePluginStateStore reopened(database.value());
    REQUIRE(reopened.initialize().has_value());
    CHECK_FALSE(reopened.enabled(kSampleId, true));
}
