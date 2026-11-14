#include <catch2/catch_test_macros.hpp>
#include <string>

#include "bully/formats/mission.h"

using namespace bully::formats;

namespace {

MissionDocument make_mission()
{
    MissionDocument document;
    document.asset_id = "mission-1";
    document.name = "First Day";
    document.description = "Attend class.";
    document.start_node = "n.start";

    MissionObjective objective;
    objective.objective_id = "obj.a";
    objective.description = "Go to the classroom";
    objective.kind = ObjectiveKind::GoTo;
    objective.location = "scene-1";
    document.objectives.push_back(objective);

    MissionNode start;
    start.node_id = "n.start";
    start.objective_id = "obj.a";
    start.edges.push_back(MissionEdge{"n.end", EdgeKind::Sequential, {}});
    document.nodes.push_back(start);

    MissionNode end;
    end.node_id = "n.end";
    document.nodes.push_back(end);

    MissionCondition condition;
    condition.condition_id = "cnd.a";
    condition.kind = ConditionKind::FlagEquals;
    condition.key = "flag.met_teacher";
    condition.value = "true";
    document.conditions.push_back(condition);

    document.completion_flags = {"flag.mission1_done"};
    return document;
}

}  // namespace

TEST_CASE("bmission round-trips objectives, nodes and conditions", "[formats][mission]")
{
    const auto original = make_mission();
    auto decoded = MissionDocument{}.deserialize(original.serialize());
    REQUIRE(decoded.has_value());
    const auto& mission = decoded.value();
    CHECK(mission.name == "First Day");
    REQUIRE(mission.objectives.size() == 1);
    CHECK(mission.find_objective("obj.a")->kind == ObjectiveKind::GoTo);
    REQUIRE(mission.nodes.size() == 2);
    REQUIRE(mission.nodes[0].edges.size() == 1);
    CHECK(mission.nodes[0].edges[0].to == "n.end");
    REQUIRE(mission.conditions.size() == 1);
    CHECK(mission.conditions[0].kind == ConditionKind::FlagEquals);
    CHECK(mission.validate().has_value());
}

TEST_CASE("Mission validation catches dangling graph references", "[formats][mission]")
{
    auto mission = make_mission();
    mission.nodes[0].edges[0].to = "n.missing";
    CHECK_FALSE(mission.validate().has_value());

    mission = make_mission();
    mission.nodes[0].objective_id = "obj.missing";
    CHECK_FALSE(mission.validate().has_value());

    mission = make_mission();
    mission.start_node = "n.missing";
    CHECK_FALSE(mission.validate().has_value());

    mission = make_mission();
    mission.nodes[0].edges[0].condition_id = "cnd.missing";
    CHECK_FALSE(mission.validate().has_value());

    mission = make_mission();
    mission.timed = true;  // no time limit
    CHECK_FALSE(mission.validate().has_value());

    mission = make_mission();
    mission.dependencies = {mission.asset_id};
    CHECK_FALSE(mission.validate().has_value());
}

TEST_CASE("Unreachable mission nodes are reported", "[formats][mission]")
{
    auto mission = make_mission();
    MissionNode orphan;
    orphan.node_id = "n.orphan";
    mission.nodes.push_back(orphan);
    const auto status = mission.validate();
    REQUIRE(status.has_value());
    CHECK(status.diagnostics().count(bully::core::Severity::Warning) >= 1);
}

TEST_CASE("Mission JSON summary reports objectives", "[formats][mission]")
{
    const auto mission = make_mission();
    const std::string json = mission.to_json();
    CHECK(json.find("\"format\": \"bmission\"") != std::string::npos);
    CHECK(json.find("Go to the classroom") != std::string::npos);
}
