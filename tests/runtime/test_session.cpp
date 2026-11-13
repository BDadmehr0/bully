#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "bully/runtime/session.h"
#include "test_support.h"

using namespace bully;
using Catch::Approx;

namespace {

/// Writes a compiled scene + mission + dialogue set that a session can boot.
void write_world(const std::filesystem::path& built)
{
    std::filesystem::create_directories(built / "scenes");
    std::filesystem::create_directories(built / "missions");
    std::filesystem::create_directories(built / "dialogue");

    formats::SceneDocument scene;
    scene.asset_id = "scene.yard";
    scene.name = "yard";
    formats::SceneEntity entity;
    entity.entity_id = "ent.crate";
    scene.entities.push_back(entity);
    formats::TriggerVolume trigger;
    trigger.trigger_id = "trg.mission";
    trigger.kind = "mission";
    trigger.payload_json = R"({"mission":"mission.first"})";
    scene.triggers.push_back(trigger);
    REQUIRE(core::FileSystem::write_binary(built / "scenes/yard.bscene", scene.serialize())
                .has_value());

    formats::MissionDocument mission;
    mission.asset_id = "mission.first";
    mission.name = "First";
    mission.start_node = "n.start";
    formats::MissionObjective objective;
    objective.objective_id = "obj.talk";
    objective.description = "Talk to the kid";
    objective.kind = formats::ObjectiveKind::TalkTo;
    mission.objectives.push_back(objective);
    formats::MissionNode node;
    node.node_id = "n.start";
    node.objective_id = "obj.talk";
    mission.nodes.push_back(node);
    REQUIRE(mission.validate().has_value());
    REQUIRE(core::FileSystem::write_binary(built / "missions/first.bmission", mission.serialize())
                .has_value());

    formats::DialogueDocument dialogue;
    dialogue.asset_id = "dialogue.intro";
    dialogue.name = "intro";
    dialogue.entry_node = "n.end";
    formats::DialogueNode end;
    end.node_id = "n.end";
    end.kind = formats::NodeKind::End;
    dialogue.nodes.push_back(end);
    REQUIRE(dialogue.validate().has_value());
    REQUIRE(core::FileSystem::write_binary(built / "dialogue/intro.bdialogue",
                                          dialogue.serialize())
                .has_value());
}

}  // namespace

TEST_CASE("A session boots a project directory and loads its entry scene",
          "[runtime][session]")
{
    test::TempDir dir;
    write_world(dir.path() / "built");

    runtime::GameSession session;
    runtime::SessionConfig config;
    config.project_root = dir.path();
    REQUIRE(session.boot(config).has_value());
    CHECK(session.booted());
    REQUIRE(session.scene() != nullptr);
    CHECK(session.scene()->name == "yard");
    CHECK(session.story().current_scene() == "scene.yard");
    // The scene trigger pulled the mission into the playable set.
    CHECK(session.statistics().assets_decoded >= 1);
}

TEST_CASE("A session refuses a missing build directory", "[runtime][session]")
{
    runtime::GameSession session;
    runtime::SessionConfig config;
    config.project_root = "/nonexistent/bully/project";
    auto status = session.boot(config);
    CHECK_FALSE(status.has_value());
    CHECK_FALSE(session.booted());
}

TEST_CASE("A session can boot from a package instead of a build directory",
          "[runtime][session]")
{
    test::TempDir dir;
    write_world(dir.path() / "built");

    // Pack the compiled scene exactly like `bully_asset_cli package` does.
    formats::PackageDocument package;
    package.asset_id = "package.demo";
    package.name = "demo";
    auto scene_bytes = core::FileSystem::read_binary(dir.path() / "built/scenes/yard.bscene");
    REQUIRE(scene_bytes.has_value());
    REQUIRE(package
                .add(formats::PackageEntryKind::Scene, "scenes/yard.bscene", "scene.yard",
                     scene_bytes.value())
                .has_value());
    const auto package_path = dir.path() / "demo.bpackage";
    REQUIRE(core::FileSystem::write_binary(package_path, package.serialize()).has_value());

    runtime::GameSession session;
    runtime::SessionConfig config;
    config.source = runtime::SessionSource::Package;
    config.package_path = package_path;
    REQUIRE(session.boot(config).has_value());
    REQUIRE(session.scene() != nullptr);
    CHECK(session.scene()->name == "yard");
}

TEST_CASE("Fixed steps drive mission progression from input", "[runtime][session]")
{
    test::TempDir dir;
    write_world(dir.path() / "built");

    runtime::GameSession session;
    runtime::SessionConfig config;
    config.project_root = dir.path();
    REQUIRE(session.boot(config).has_value());

    // Boot does not auto-start missions: the trigger does, when the player walks
    // into it. Simulate that by starting the mission the scene declared.
    auto mission = session.assets().load_mission("mission.first");
    REQUIRE(mission.has_value());
    REQUIRE(session.story().start_mission(*mission.value()).has_value());

    // No input: nothing progresses.
    auto idle = session.simulate(30, 1.0 / 60.0);
    CHECK(idle.missions_active == 1);
    CHECK(idle.missions_completed == 0);
    CHECK_FALSE(session.story().mission_complete("mission.first"));

    // Holding interact completes the TalkTo objective within one step.
    std::map<std::string, float> raw;
    raw["key:e"] = 1.0f;
    const auto played = session.simulate(2, 1.0 / 60.0, raw);
    CHECK(played.missions_completed == 1);
    CHECK(session.story().mission_complete("mission.first"));
    CHECK(played.simulated_seconds > 0.0);
}

TEST_CASE("Timed missions expire while the session runs", "[runtime][session]")
{
    test::TempDir dir;
    write_world(dir.path() / "built");

    runtime::GameSession session;
    runtime::SessionConfig config;
    config.project_root = dir.path();
    REQUIRE(session.boot(config).has_value());

    formats::MissionDocument timed;
    timed.asset_id = "mission.timed";
    timed.name = "Timed";
    timed.start_node = "n.start";
    timed.timed = true;
    timed.time_limit_seconds = 0.1f;
    formats::MissionObjective objective;
    objective.objective_id = "obj.survive";
    objective.kind = formats::ObjectiveKind::Survive;
    timed.objectives.push_back(objective);
    formats::MissionNode node;
    node.node_id = "n.start";
    node.objective_id = "obj.survive";
    timed.nodes.push_back(node);
    REQUIRE(timed.validate().has_value());
    REQUIRE(core::FileSystem::write_binary(dir.path() / "built/missions/timed.bmission",
                                          timed.serialize())
                .has_value());

    // A second session picks the new mission up through a reloaded mount.
    runtime::GameSession second;
    runtime::SessionConfig second_config;
    second_config.project_root = dir.path();
    REQUIRE(second.boot(second_config).has_value());
    auto loaded = second.assets().load_mission("mission.timed");
    REQUIRE(loaded.has_value());
    REQUIRE(second.story().start_mission(*loaded.value()).has_value());
    const auto stats = second.simulate(30, 1.0 / 60.0);  // 0.5 s of simulation
    CHECK(stats.steps > 0);
    // The mission failed via the timer; it is no longer counted as active.
    CHECK(second.story().mission("mission.timed")->state == formats::MissionState::Failed);
    CHECK(stats.missions_active == 0);
}

TEST_CASE("Sessions save and restore mid-playthrough", "[runtime][session]")
{
    test::TempDir dir;
    write_world(dir.path() / "built");

    runtime::GameSession session;
    runtime::SessionConfig config;
    config.project_root = dir.path();
    REQUIRE(session.boot(config).has_value());
    session.story().flags().set("player.name", "Jimmy");
    auto mission = session.assets().load_mission("mission.first");
    REQUIRE(mission.has_value());
    REQUIRE(session.story().start_mission(*mission.value()).has_value());
    (void)session.simulate(10, 1.0 / 60.0);

    const std::string saved = session.save_game();
    REQUIRE_FALSE(saved.empty());

    runtime::GameSession restored;
    runtime::SessionConfig restored_config;
    restored_config.project_root = dir.path();
    REQUIRE(restored.boot(restored_config).has_value());
    REQUIRE(restored.load_game(saved).has_value());
    CHECK(restored.story().flags().get("player.name") == "Jimmy");
    REQUIRE(restored.story().mission("mission.first").has_value());
    CHECK(restored.story().mission("mission.first")->state == formats::MissionState::Active);

    // Bad save data is refused without destroying the running state.
    CHECK_FALSE(restored.load_game("{broken").has_value());
    CHECK(restored.story().flags().get("player.name") == "Jimmy");
}

TEST_CASE("Dialogue playback advances on interact", "[runtime][session]")
{
    test::TempDir dir;
    write_world(dir.path() / "built");

    // A two-line dialogue with an end node.
    formats::DialogueDocument dialogue;
    dialogue.asset_id = "dialogue.two";
    dialogue.name = "two";
    dialogue.entry_node = "n.one";
    formats::DialogueLine line;
    line.line_id = "dlg.one";
    line.text = "Hello.";
    dialogue.lines.push_back(line);
    formats::DialogueNode one;
    one.node_id = "n.one";
    one.kind = formats::NodeKind::Line;
    one.line_id = "dlg.one";
    one.next_target = "n.done";
    dialogue.nodes.push_back(one);
    formats::DialogueNode done;
    done.node_id = "n.done";
    done.kind = formats::NodeKind::End;
    dialogue.nodes.push_back(done);
    REQUIRE(dialogue.validate().has_value());
    REQUIRE(core::FileSystem::write_binary(dir.path() / "built/dialogue/two.bdialogue",
                                          dialogue.serialize())
                .has_value());

    runtime::GameSession session;
    runtime::SessionConfig config;
    config.project_root = dir.path();
    REQUIRE(session.boot(config).has_value());
    session.play_dialogue("dialogue.two");

    // First frame without input starts the document but does not advance it.
    (void)session.simulate(2, 1.0 / 60.0);
    REQUIRE(session.story().dialogue("dialogue.two").has_value());
    CHECK(session.story().dialogue("dialogue.two")->current_node == "n.one");

    std::map<std::string, float> raw;
    raw["key:e"] = 1.0f;
    (void)session.simulate(2, 1.0 / 60.0, raw);
    CHECK(session.story().line_heard("dlg.one"));
    CHECK(session.story().dialogue("dialogue.two")->current_node == "n.done");
}

TEST_CASE("Frame plans expose paused and interpolation state", "[runtime][session]")
{
    test::TempDir dir;
    write_world(dir.path() / "built");
    runtime::GameSession session;
    runtime::SessionConfig config;
    config.project_root = dir.path();
    REQUIRE(session.boot(config).has_value());

    const auto plan = session.begin_frame(1.0 / 60.0);
    CHECK(plan.steps == 1);
    CHECK_FALSE(plan.paused);
    CHECK(plan.alpha == Approx(0.0).margin(0.001));
    session.step();
    session.end_frame();

    session.clock().set_paused(true);
    const auto paused_plan = session.begin_frame(1.0 / 60.0);
    CHECK(paused_plan.steps == 0);
    CHECK(paused_plan.paused);
    session.end_frame();
}
