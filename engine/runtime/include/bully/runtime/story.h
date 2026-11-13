// Bully Studio runtime - story state.
//
// StoryState owns everything a save game needs to reproduce a playthrough:
// story flags, mission progress, dialogue progress and the current scene. It is
// deliberately free of rendering, audio and windowing so the whole progression
// system can be unit-tested headlessly.
//
// Diagnostic codes: BULLY-RT-0100..0119
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/formats/dialogue.h"
#include "bully/formats/mission.h"

namespace bully::runtime {

/// A single flag value. Flags are strings so a designer can store "true",
/// "3" or "chapter2" without a schema change; typed accessors parse on demand.
struct StoryFlags {
    std::map<std::string, std::string> values;

    void set(std::string_view flag, std::string value);
    void set_bool(std::string_view flag, bool value);
    void clear(std::string_view flag);
    [[nodiscard]] bool has(std::string_view flag) const;
    [[nodiscard]] std::string get(std::string_view flag, std::string fallback = {}) const;
    [[nodiscard]] bool get_bool(std::string_view flag, bool fallback = false) const;
    [[nodiscard]] int get_int(std::string_view flag, int fallback = 0) const;
    /// Compares a flag against an expected value with the same rules the
    /// dialogue and mission condition evaluators use ("1"/"true"/"yes" are all
    /// true; a missing flag equals the empty string).
    [[nodiscard]] bool matches(std::string_view flag, std::string_view expected) const;
};

enum class ObjectiveState : std::uint8_t { Inactive = 0, Active = 1, Complete = 2, Failed = 3 };
[[nodiscard]] std::string_view to_string(ObjectiveState state) noexcept;

/// Live progress for one mission instance.
struct MissionProgress {
    std::string mission_asset;
    formats::MissionState state = formats::MissionState::NotStarted;
    std::string current_node;
    std::map<std::string, std::uint32_t> objective_progress;  // objective id -> count
    std::map<std::string, ObjectiveState> objective_state;
    float elapsed_seconds = 0.0f;
    std::uint32_t completions = 0;

    [[nodiscard]] std::uint32_t progress_of(std::string_view objective_id) const;
    [[nodiscard]] ObjectiveState state_of(std::string_view objective_id) const;
    [[nodiscard]] bool all_required_complete(const formats::MissionDocument& mission) const;
};

/// Live progress for one dialogue document.
struct DialogueProgress {
    std::string dialogue_asset;
    std::string current_node;
    std::vector<std::string> visited_lines;
    bool finished = false;
};

/// The result of advancing a mission: what the game loop should do next.
struct MissionTick {
    bool changed = false;
    bool completed = false;
    bool failed = false;
    std::string node;
    std::vector<std::string> messages;  // designer-facing log, shown in the HUD
};

class StoryState {
public:
    StoryFlags& flags() { return flags_; }
    [[nodiscard]] const StoryFlags& flags() const { return flags_; }

    // ------------------------------------------------------------ missions --
    /// Starts a mission instance. Fails when a required dependency is not
    /// complete, or when the mission is already active.
    [[nodiscard]] core::Status start_mission(const formats::MissionDocument& mission);
    [[nodiscard]] std::optional<MissionProgress> mission(std::string_view asset_id) const;
    [[nodiscard]] std::vector<MissionProgress> active_missions() const;

    /// Records progress towards an objective. When every required objective is
    /// complete the mission completes and its completion flags are set.
    [[nodiscard]] MissionTick advance_objective(const formats::MissionDocument& mission,
                                                std::string_view objective_id,
                                                std::uint32_t amount = 1);
    /// Advances the mission clock; fails a timed mission when it expires.
    [[nodiscard]] MissionTick tick_mission(const formats::MissionDocument& mission,
                                           float delta_seconds);
    [[nodiscard]] MissionTick fail_mission(const formats::MissionDocument& mission,
                                           std::string_view reason = {});
    [[nodiscard]] bool mission_complete(std::string_view asset_id) const;

    // ------------------------------------------------------------ dialogue --
    [[nodiscard]] core::Status begin_dialogue(const formats::DialogueDocument& dialogue);
    [[nodiscard]] std::optional<DialogueProgress> dialogue(std::string_view asset_id) const;
    /// The node the player is currently on ("nullopt" when the document ended).
    [[nodiscard]] core::Result<std::string> current_dialogue_node(
        const formats::DialogueDocument& dialogue) const;
    /// Chooses a branch (for Choice nodes) or continues (Line/Cutscene/Jump).
    /// `branch_index` is ignored by non-choice nodes.
    [[nodiscard]] core::Result<std::string> advance_dialogue(
        const formats::DialogueDocument& dialogue, std::optional<std::size_t> branch_index = {});
    /// Records a line as heard (drives "already heard" barks and journal state).
    void mark_line_heard(std::string_view line_id);
    [[nodiscard]] bool line_heard(std::string_view line_id) const;

    // --------------------------------------------------------------- scene --
    [[nodiscard]] const std::string& current_scene() const { return current_scene_; }
    void enter_scene(std::string scene_asset);

    // ---------------------------------------------------------------- save --
    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] static core::Result<StoryState> from_json(std::string_view text);

private:
    StoryFlags flags_;
    std::map<std::string, MissionProgress> missions_;
    std::map<std::string, DialogueProgress> dialogues_;
    std::vector<std::string> heard_lines_;
    std::string current_scene_;

    [[nodiscard]] bool dependency_satisfied(const formats::MissionDocument& mission) const;
};

}  // namespace bully::runtime
