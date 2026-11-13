#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "bully/formats/animation.h"
#include "bully/formats/dialogue.h"
#include "bully/formats/material.h"
#include "bully/formats/mission.h"
#include "bully/formats/scene.h"
#include "bully/pipeline/builtin_importers.h"
#include "bully/pipeline/qa.h"
#include "fixture.h"
#include "test_support.h"

using namespace bully;
using namespace bully::pipeline;

namespace {

ImportContext data_context(test::TempDir& dir, std::string relative, std::string asset_id)
{
    ImportContext context;
    context.project_root = dir.str();
    context.relative_source_path = relative;
    context.source_path = (dir.path() / relative).string();
    context.output_path = (dir.path() / "built/out.bin").string();
    context.asset_id = std::move(asset_id);
    return context;
}

}  // namespace

TEST_CASE("Dialogue importer compiles the story graph", "[pipeline][data]")
{
    test::TempDir dir;
    const std::string relative = "content/dialogue/intro.dialogue.json";
    dir.write(relative, test::dialogue_source_json());
    auto importer = make_dialogue_importer();
    auto result = importer->import(data_context(dir, relative, "dlg-id"));
    REQUIRE(result.has_value());
    CHECK(result.value().asset_type == AssetType::Dialogue);

    formats::DialogueDocument document;
    auto decoded = document.deserialize(result.value().output_bytes);
    REQUIRE(decoded.has_value());
    const auto& dialogue = decoded.value();
    CHECK(dialogue.name == "test_conversation");
    CHECK(dialogue.entry_node == "n_start");
    CHECK(dialogue.characters.size() == 2);
    CHECK(dialogue.lines.size() == 2);
    CHECK(dialogue.nodes.size() == 4);
    CHECK(dialogue.find_node("n_choice")->kind == formats::NodeKind::Choice);
    CHECK(dialogue.find_line("line.1")->selected_take == "t1");
    CHECK(dialogue.validate().has_value());
}

TEST_CASE("Dialogue importer reports unresolved references without dropping data",
          "[pipeline][data]")
{
    test::TempDir dir;
    const std::string relative = "content/dialogue/broken.dialogue.json";
    std::string source = test::dialogue_source_json();
    const std::string missing = "\"next\": \"n_choice\"";
    source.replace(source.find(missing), missing.size(), "\"next\": \"n_gone\"");
    dir.write(relative, source);
    auto result = make_dialogue_importer()->import(data_context(dir, relative, "id"));
    // Validation errors are attached to the result; the pipeline turns any
    // error diagnostic into a Failed build, so a broken graph is never shipped.
    REQUIRE(result.has_value());
    CHECK(result.value().diagnostics.has_errors());
}

TEST_CASE("Scene importer compiles entities and triggers", "[pipeline][data]")
{
    test::TempDir dir;
    const std::string relative = "content/scenes/yard.scene.json";
    dir.write(relative, test::scene_source_json());
    auto result = make_scene_importer()->import(data_context(dir, relative, "scene-id"));
    REQUIRE(result.has_value());
    formats::SceneDocument document;
    auto decoded = document.deserialize(result.value().output_bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().entities.size() == 2);
    CHECK(decoded.value().lights.size() == 1);
    CHECK(decoded.value().triggers.size() == 1);
    CHECK(decoded.value().spawns.size() == 1);
    CHECK(decoded.value().environment.fog_enabled);
    CHECK(decoded.value().validate().has_value());
}

TEST_CASE("Mission importer compiles the objective graph", "[pipeline][data]")
{
    test::TempDir dir;
    const std::string relative = "content/missions/first.mission.json";
    dir.write(relative, test::mission_source_json());
    auto result = make_mission_importer()->import(data_context(dir, relative, "mission-id"));
    REQUIRE(result.has_value());
    formats::MissionDocument document;
    auto decoded = document.deserialize(result.value().output_bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().objectives.size() == 1);
    CHECK(decoded.value().nodes.size() == 2);
    CHECK(decoded.value().conditions.size() == 1);
    CHECK(decoded.value().validate().has_value());
}

TEST_CASE("Data importers reject malformed JSON with a diagnostic", "[pipeline][data]")
{
    test::TempDir dir;
    dir.write("content/scenes/bad.scene.json", "{ \"entities\": [ ");
    auto result = make_scene_importer()->import(data_context(dir, "content/scenes/bad.scene.json", "id"));
    CHECK_FALSE(result.has_value());
    CHECK(result.diagnostics().has_errors());

    dir.write("content/dialogue/empty.dialogue.json", "{}");
    auto empty = make_dialogue_importer()->import(
        data_context(dir, "content/dialogue/empty.dialogue.json", "id"));
    // An empty document compiles but reports validation warnings/errors.
    if (empty.has_value()) {
        CHECK(empty.value().output_bytes.empty() == false);
    } else {
        CHECK(empty.diagnostics().has_errors());
    }
}

TEST_CASE("Material importer resolves shader presets and texture slots", "[pipeline][data]")
{
    test::TempDir dir;
    dir.write("content/materials/skin.bmaterial.json", R"({
  "name": "school_skin",
  "shader_preset": "retro.lambert",
  "blend": 0,
  "cull": 0,
  "alpha_cutoff": 0.4,
  "textures": [{"slot": 0, "texture": "tex-diffuse-id", "sampler": "nearest"}],
  "parameters": [{"name": "tint", "type": 4, "values": [1.0, 0.8, 0.6, 1.0]}]
})");
    auto result = make_material_importer()->import(
        data_context(dir, "content/materials/skin.bmaterial.json", "mat-id"));
    REQUIRE(result.has_value());
    REQUIRE(result.value().dependencies.size() == 1);
    CHECK(result.value().dependencies[0].asset_id == "tex-diffuse-id");

    formats::MaterialDocument document;
    auto decoded = document.deserialize(result.value().output_bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().shader_preset == "retro.lambert");
    CHECK(decoded.value().state.alpha_cutoff == Catch::Approx(0.4f));
    REQUIRE(decoded.value().textures.size() == 1);
    CHECK(decoded.value().textures[0].sampler_override == "nearest");
    REQUIRE(decoded.value().parameters.size() == 1);
    CHECK(decoded.value().parameters[0].values[1] == Catch::Approx(0.8f));
    CHECK(decoded.value().validate().has_value());
}

TEST_CASE("Animation importer compiles bone tracks", "[pipeline][data]")
{
    test::TempDir dir;
    dir.write("content/anims/walk.banim.json", R"({
  "name": "walk",
  "duration": 1.0,
  "sample_rate": 30.0,
  "loop": 1,
  "tracks": [{"bone": "hip", "interpolation": 0}],
  "events": [{"name": "footstep", "time": 0.5}]
})");
    auto result = make_animation_importer()->import(
        data_context(dir, "content/anims/walk.banim.json", "anim-id"));
    REQUIRE(result.has_value());
    formats::AnimationDocument document;
    auto decoded = document.deserialize(result.value().output_bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().name == "walk");
    CHECK(decoded.value().duration == Catch::Approx(1.0f));
    CHECK(decoded.value().loop == formats::LoopMode::Loop);
    REQUIRE(decoded.value().tracks.size() == 1);
    CHECK(decoded.value().tracks[0].bone_name == "hip");
    REQUIRE(decoded.value().events.size() == 1);
    CHECK(decoded.value().events[0].name == "footstep");
    CHECK(decoded.value().validate().has_value());
}

TEST_CASE("Pipeline builds every authored content type", "[pipeline][data][integration]")
{
    test::ProjectFixture fixture("content-project");
    fixture.write_content("dialogue/intro.dialogue.json", test::dialogue_source_json());
    fixture.write_content("scenes/yard.scene.json", test::scene_source_json());
    fixture.write_content("missions/first.mission.json", test::mission_source_json());
    const auto scan = fixture.pipeline().scan();
    REQUIRE(scan.diagnostics.has_errors() == false);
    REQUIRE(scan.discovered.size() == 3);

    const auto report = fixture.build_all();
    CHECK(report.success());
    CHECK(report.built == 3);

    for (const auto& result : report.results) {
        CHECK(result.success);
        CHECK(result.output_path.find("built/") == 0);
        CHECK_FALSE(result.output_hash.empty());
    }
}

TEST_CASE("Authored references resolve to asset ids and persist as dependencies",
          "[pipeline][data][integration]")
{
    test::ProjectFixture fixture("reference-project");
    const std::string model_source = fixture.write_asset("models/crate.obj", test::cube_obj());
    // A scene authored the way an artist writes it: the model by name, not by
    // generated UUID.
    fixture.write_content("scenes/yard.scene.json", R"({
  "name": "yard",
  "entities": [{"id": "ent.crate", "name": "Crate", "model": "crate"}],
  "spawns": [{"id": "spawn.player", "kind": "player"}]
})");
    const auto scan = fixture.pipeline().scan();
    REQUIRE(scan.discovered.size() == 2);
    const auto report = fixture.build_all();
    REQUIRE(report.success());

    const auto model = fixture.record_for(model_source);
    REQUIRE(model.has_value());
    const auto scene = fixture.record_for("content/scenes/yard.scene.json");
    REQUIRE(scene.has_value());

    // The compiled scene stores the resolved asset id, so the runtime can load it.
    auto bytes = fixture.read_compiled("content/scenes/yard.scene.json");
    formats::SceneDocument reader;
    auto decoded = reader.deserialize(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded.value().entities.size() == 1);
    CHECK(decoded.value().entities[0].model_asset == model->asset_id);

    // ...and the dependency edge survives a fresh registry read (it is in SQLite,
    // not just in the in-memory record), which is what makes incremental builds
    // and QA's orphan check work.
    const auto reloaded = fixture.registry().find(scene->asset_id);
    REQUIRE(reloaded.has_value());
    REQUIRE(reloaded->dependencies.size() == 1);
    CHECK(reloaded->dependencies[0].asset_id == model->asset_id);
    CHECK(reloaded->dependencies[0].required);
    CHECK(fixture.registry().dependents_of(model->asset_id).size() == 1);
}

TEST_CASE("Unresolved authored references are preserved verbatim", "[pipeline][data]")
{
    test::ProjectFixture fixture("unresolved-project");
    fixture.write_content("scenes/empty.scene.json", R"({
  "name": "empty",
  "entities": [{"id": "ent.ghost", "model": "does/not/exist.obj"}]
})");
    REQUIRE(fixture.pipeline().scan().discovered.size() == 1);
    const auto report = fixture.build_all();
    REQUIRE(report.success());

    // The reference is kept as authored so QA can report it; it is not silently
    // rewritten into an empty string.
    auto bytes = fixture.read_compiled("content/scenes/empty.scene.json");
    formats::SceneDocument reader;
    auto decoded = reader.deserialize(bytes);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded.value().entities.size() == 1);
    CHECK(decoded.value().entities[0].model_asset == "does/not/exist.obj");

    bully::pipeline::QaRunner runner(fixture.layout(), fixture.registry(), fixture.importers(),
                                    fixture.pipeline());
    runner.register_builtin_rules();
    const auto qa = runner.run({"scene.references"});
    REQUIRE(qa.rules.size() == 1);
    CHECK(qa.rules[0].warnings == 1);
    CHECK(qa.rules[0].findings[0].code == "BULLY-QA-0060");
}
