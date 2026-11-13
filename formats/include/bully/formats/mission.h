// Bully Studio - .bmission : compiled mission / quest.
//
// A mission is a node graph of objectives with conditions, triggers, rewards
// and failure states. The visual editor edits the JSON source; the compiler
// emits this binary; the runtime executes the graph.
//
// v1.0 chunks:
//   INFO  identity, giver NPC, dependencies, reward summary, flags
//   OBJ   one chunk per objective (kind, target, count, optional dialogue)
//   NODE  graph nodes with typed edges (sequential, parallel, optional)
//   CND   completion / failure conditions (flag, timer, objective state)
//   META  JSON metadata (authoring notes, scene bindings)
//
// Diagnostic codes: BULLY-MSN-0001..0029
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/formats/binary.h"

namespace bully::formats {

inline constexpr FormatDescriptor kMissionFormat{
    fourcc("BMSN"), "bmission", {1, 0, 0}, ".bmission",
    "Mission/quest: objectives, node graph, conditions, rewards, dependencies"};

enum class ObjectiveKind : std::uint8_t {
    GoTo = 0,
    TalkTo = 1,
    Collect = 2,
    Defeat = 3,
    Escort = 4,
    UseObject = 5,
    Survive = 6,
    Custom = 255,
};

struct MissionObjective {
    std::string objective_id;
    std::string description;
    ObjectiveKind kind = ObjectiveKind::GoTo;
    std::string target;       // entity / npc / item id
    std::string location;     // scene id or trigger id
    std::uint32_t required_count = 1;
    bool optional = false;
    bool hidden = false;
    std::string dialogue_line;  // .bdialogue line id to play on completion
    std::string reward_item;
    std::uint32_t reward_currency = 0;
};

enum class ConditionKind : std::uint8_t {
    FlagEquals = 0,
    ObjectiveComplete = 1,
    ObjectiveFailed = 2,
    Timer = 3,
    PlayerStat = 4,
    Custom = 255,
};

struct MissionCondition {
    std::string condition_id;
    ConditionKind kind = ConditionKind::FlagEquals;
    std::string key;    // flag name, objective id, stat name
    std::string value;  // expected value
    float threshold = 0.0f;
    bool negate = false;
};

enum class EdgeKind : std::uint8_t { Sequential = 0, Parallel = 1, Optional = 2, OnFailure = 3 };

struct MissionEdge {
    std::string to;
    EdgeKind kind = EdgeKind::Sequential;
    std::string condition_id;  // optional gate
};

struct MissionNode {
    std::string node_id;
    std::string objective_id;
    std::vector<MissionEdge> edges;
    /// Conditions that must all hold before following the sequential edge.
    std::vector<std::string> gates;
};

enum class MissionState : std::uint8_t { NotStarted = 0, Active = 1, Completed = 2, Failed = 3 };

struct MissionDocument {
    std::string asset_id;
    std::string name;
    std::string description;
    std::string giver_npc;
    std::string start_node;
    std::string scene_asset;
    std::vector<std::string> dependencies;  // other mission asset ids
    std::vector<MissionObjective> objectives;
    std::vector<MissionNode> nodes;
    std::vector<MissionCondition> conditions;
    std::vector<std::string> completion_flags;
    std::vector<std::string> failure_flags;
    std::string metadata_json;

    bool repeatable = false;
    bool timed = false;
    float time_limit_seconds = 0.0f;

    [[nodiscard]] const MissionObjective* find_objective(std::string_view objective_id) const;
    [[nodiscard]] const MissionNode* find_node(std::string_view node_id) const;

    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    [[nodiscard]] core::Result<MissionDocument> deserialize(
        const std::vector<std::uint8_t>& bytes) const;
    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] std::string to_json() const;
};

[[nodiscard]] std::string_view to_string(ObjectiveKind kind) noexcept;
[[nodiscard]] std::string_view to_string(ConditionKind kind) noexcept;

}  // namespace bully::formats
