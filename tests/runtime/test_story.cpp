#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>

#include "bully/runtime/story.h"

using namespace bully;
using Catch::Approx;

namespace {

formats::MissionDocument make_mission(std::string id, std::string name)
{
    formats::MissionDocument mission;
    mission.asset_id = std::move(id);
    mission.name = std::move(name);
    mission.start_node = "n.start";
    mission.completion_flags = {"mission." + mission.asset_id + ".done"};

    formats::MissionObjective first;
    first.objective_id = "obj.talk";
    first.description = "Talk to Gary";
    first.kind = formats::ObjectiveKind::TalkTo;
    first.target = "npc.gary";
    mission.objectives.push_back(first);

    formats::MissionObjective second;
    second.objective_id = "obj.collect";
    second.description = "Collect the slingshot";
    second.kind = formats::ObjectiveKind::Collect;
    second.required_count = 2;
    mission.objectives.push_back(second);

    formats::MissionNode start;
    start.node_id = "n.start";
    start.objective_id = "obj.talk";
    formats::MissionEdge edge;
    edge.to = "n.collect";
    start.edges.push_back(edge);
    mission.nodes.push_back(start);

    formats::MissionNode next;
    next.node_id = "n.collect";
    next.objective_id = "obj.collect";
    mission.nodes.push_back(next);
    return mission;
}

formats::DialogueDocument make_dialogue()
{
    formats::DialogueDocument dialogue;
    dialogue.asset_id = "dialogue.intro";
    dialogue.name = "intro";
    dialogue.entry_node = "n.line1";

    formats::DialogueCharacter gary;
    gary.character_id = "chr.gary";
    gary.display_name = "Gary";
    dialogue.characters.push_back(gary);

    formats::DialogueLine line1;
    line1.line_id = "dlg.001";
    line1.character = "chr.gary";
    line1.text = "Hey, new kid.";
    dialogue.lines.push_back(line1);

    formats::DialogueLine line2;
    line2.line_id = "dlg.002";
    line2.character = "chr.gary";
    line2.text = "Pick a fight, or get lost.";
    dialogue.lines.push_back(line2);

    formats::DialogueNode node1;
    node1.node_id = "n.line1";
    node1.kind = formats::NodeKind::Line;
    node1.line_id = "dlg.001";
    node1.next_target = "n.choice";
    dialogue.nodes.push_back(node1);

    formats::DialogueNode choice;
    choice.node_id = "n.choice";
    choice.kind = formats::NodeKind::Choice;
    formats::DialogueBranch fight;
    fight.label = "Fight";
    fight.target_node = "n.end_fight";
    choice.branches.push_back(fight);
    formats::DialogueBranch walk;
    walk.label = "Walk away";
    walk.target_node = "n.line2";
    walk.required_flag = "story.permission";
    walk.required_value = "true";
    choice.branches.push_back(walk);
    dialogue.nodes.push_back(choice);

    formats::DialogueNode line2_node;
    line2_node.node_id = "n.line2";
    line2_node.kind = formats::NodeKind::Line;
    line2_node.line_id = "dlg.002";
    line2_node.next_target = "n.end_fight";
    dialogue.nodes.push_back(line2_node);

    formats::DialogueNode end;
    end.node_id = "n.end_fight";
    end.kind = formats::NodeKind::End;
    dialogue.nodes.push_back(end);
    return dialogue;
}

}  // namespace

TEST_CASE("Story flags parse typed values and compare like conditions", "[runtime][story]")
{
    runtime::StoryFlags flags;
    CHECK_FALSE(flags.has("story.intro"));
    flags.set_bool("story.intro", true);
    CHECK(flags.has("story.intro"));
    CHECK(flags.get_bool("story.intro"));
    CHECK(flags.matches("story.intro", "1"));
    CHECK(flags.matches("story.intro", "true"));
    CHECK_FALSE(flags.matches("story.intro", "false"));

    flags.set("player.chapter", "3");
    CHECK(flags.get_int("player.chapter", 0) == 3);
    CHECK(flags.get_int("missing", 7) == 7);
    CHECK(flags.get("player.chapter") == "3");
    CHECK(flags.get("missing", "fallback") == "fallback");
    CHECK(flags.get_bool("missing", true));
    CHECK_FALSE(flags.matches("missing", "true"));

    flags.clear("story.intro");
    CHECK_FALSE(flags.has("story.intro"));
}

TEST_CASE("Missions start, gate on dependencies and complete", "[runtime][story]")
{
    runtime::StoryState story;
    const auto mission = make_mission("mission.first_day", "First Day");

    REQUIRE(story.start_mission(mission).has_value());
    REQUIRE(story.mission("mission.first_day").has_value());
    CHECK(story.active_missions().size() == 1);
    // Starting twice is an error, not a reset.
    CHECK_FALSE(story.start_mission(mission).has_value());

    const auto first = story.advance_objective(mission, "obj.talk");
    CHECK(first.changed);
    CHECK_FALSE(first.completed);
    CHECK(first.node == "n.collect");
    CHECK(story.mission("mission.first_day")->state_of("obj.talk") ==
          runtime::ObjectiveState::Complete);

    // The collect objective needs two pickups; one is not enough.
    const auto partial = story.advance_objective(mission, "obj.collect");
    CHECK(partial.changed);
    CHECK_FALSE(partial.completed);
    CHECK(story.mission("mission.first_day")->progress_of("obj.collect") == 1);

    const auto done = story.advance_objective(mission, "obj.collect");
    CHECK(done.completed);
    CHECK(story.mission_complete("mission.first_day"));
    CHECK(story.flags().get_bool("mission.mission.first_day.done"));

    // A mission that is not repeatable cannot be restarted.
    CHECK_FALSE(story.start_mission(mission).has_value());

    // Dependents only start once their dependency is complete.
    formats::MissionDocument dependent = make_mission("mission.after", "After");
    dependent.dependencies.push_back("mission.first_day");
    REQUIRE(story.start_mission(dependent).has_value());

    formats::MissionDocument blocked = make_mission("mission.later", "Later");
    blocked.dependencies.push_back("mission.nowhere");
    const auto status = story.start_mission(blocked);
    CHECK_FALSE(status.has_value());
    CHECK(status.diagnostics().all().front().code == "BULLY-RT-0103");
}

TEST_CASE("Timed missions fail when the clock expires", "[runtime][story]")
{
    runtime::StoryState story;
    auto mission = make_mission("mission.timed", "Timed");
    mission.timed = true;
    mission.time_limit_seconds = 5.0f;
    mission.failure_flags = {"mission.timed.failed"};
    REQUIRE(story.start_mission(mission).has_value());

    CHECK_FALSE(story.tick_mission(mission, 2.0f).failed);
    CHECK(story.mission("mission.timed")->elapsed_seconds == Approx(2.0f));

    const auto expired = story.tick_mission(mission, 3.5f);
    CHECK(expired.changed);
    CHECK(expired.failed);
    CHECK(story.mission("mission.timed")->state == formats::MissionState::Failed);
    CHECK(story.flags().get_bool("mission.timed.failed"));
    // A failed mission no longer ticks.
    CHECK_FALSE(story.tick_mission(mission, 1.0f).failed);
}

TEST_CASE("Dialogue walks lines, honours conditions and records what was heard",
          "[runtime][story]")
{
    runtime::StoryState story;
    const auto dialogue = make_dialogue();
    REQUIRE(story.begin_dialogue(dialogue).has_value());
    CHECK(story.current_dialogue_node(dialogue).value() == "n.line1");

    // Line nodes advance in order and mark the line heard.
    CHECK(story.advance_dialogue(dialogue).value() == "n.choice");
    CHECK(story.line_heard("dlg.001"));
    CHECK(story.dialogue("dialogue.intro")->current_node == "n.choice");

    // A gated branch is refused until the flag is set.
    const auto refused = story.advance_dialogue(dialogue, 1);
    CHECK_FALSE(refused.has_value());
    CHECK(refused.diagnostics().all().front().code == "BULLY-RT-0118");

    // The ungated branch is always available.
    CHECK(story.advance_dialogue(dialogue, 0).value() == "n.end_fight");
    CHECK(story.advance_dialogue(dialogue).value().empty());  // End node
    CHECK(story.dialogue("dialogue.intro")->finished);
    CHECK_FALSE(story.advance_dialogue(dialogue).has_value());
}

TEST_CASE("Dialogue flags unlock gated branches", "[runtime][story]")
{
    runtime::StoryState story;
    story.flags().set_bool("story.permission", true);
    const auto dialogue = make_dialogue();
    REQUIRE(story.begin_dialogue(dialogue).has_value());
    REQUIRE(story.advance_dialogue(dialogue).has_value());  // to the choice
    CHECK(story.advance_dialogue(dialogue, 1).value() == "n.line2");
    CHECK(story.dialogue("dialogue.intro")->visited_lines.size() == 1);
    CHECK(story.advance_dialogue(dialogue).value() == "n.end_fight");
    CHECK(story.dialogue("dialogue.intro")->visited_lines.size() == 2);
}

TEST_CASE("Invalid dialogue graphs are refused at start", "[runtime][story]")
{
    runtime::StoryState story;
    formats::DialogueDocument empty;
    empty.asset_id = "dialogue.empty";
    auto status = story.begin_dialogue(empty);
    CHECK_FALSE(status.has_value());
    CHECK(status.diagnostics().all().front().code == "BULLY-RT-0111");

    formats::DialogueDocument dangling;
    dangling.asset_id = "dialogue.dangling";
    dangling.entry_node = "n.missing";
    auto missing = story.begin_dialogue(dangling);
    CHECK_FALSE(missing.has_value());
    CHECK(missing.diagnostics().all().front().code == "BULLY-RT-0112");
    // Nothing was half-started.
    CHECK_FALSE(story.dialogue("dialogue.dangling").has_value());
}

TEST_CASE("Story state round-trips through a save file", "[runtime][story]")
{
    runtime::StoryState story;
    story.enter_scene("scene.school");
    story.flags().set("player.reputation", "42");
    const auto mission = make_mission("mission.first_day", "First Day");
    REQUIRE(story.start_mission(mission).has_value());
    (void)story.advance_objective(mission, "obj.talk");

    const auto dialogue = make_dialogue();
    REQUIRE(story.begin_dialogue(dialogue).has_value());
    REQUIRE(story.advance_dialogue(dialogue).has_value());

    const std::string saved = story.to_json();
    auto loaded = runtime::StoryState::from_json(saved);
    REQUIRE(loaded.has_value());
    const auto& restored = loaded.value();
    CHECK(restored.current_scene() == "scene.school");
    CHECK(restored.flags().get_int("player.reputation") == 42);
    REQUIRE(restored.mission("mission.first_day").has_value());
    CHECK(restored.mission("mission.first_day")->state == formats::MissionState::Active);
    CHECK(restored.mission("mission.first_day")->state_of("obj.talk") ==
          runtime::ObjectiveState::Complete);
    CHECK(restored.line_heard("dlg.001"));
    REQUIRE(restored.dialogue("dialogue.intro").has_value());
    CHECK(restored.dialogue("dialogue.intro")->current_node == "n.choice");

    // A restored save keeps playing exactly where it stopped.
    auto continued = loaded.value();
    CHECK(continued.advance_dialogue(dialogue, 0).value() == "n.end_fight");
}

TEST_CASE("Malformed save data is rejected with a diagnostic", "[runtime][story]")
{
    auto not_json = runtime::StoryState::from_json("{ this is not json");
    CHECK_FALSE(not_json.has_value());
    CHECK_FALSE(not_json.diagnostics().empty());

    auto not_object = runtime::StoryState::from_json("[1, 2, 3]");
    CHECK_FALSE(not_object.has_value());
    CHECK(not_object.diagnostics().all().front().code == "BULLY-RT-0120");
}
