#include "bully/runtime/story.h"

#include <algorithm>
#include <charconv>

#include "bully/core/json.h"
#include "bully/core/log.h"
#include "bully/core/strings.h"

namespace bully::runtime {

namespace {

constexpr std::string_view kLogCategory = "story";

std::string_view mission_state_text(formats::MissionState state) noexcept
{
    switch (state) {
        case formats::MissionState::NotStarted: return "not_started";
        case formats::MissionState::Active: return "active";
        case formats::MissionState::Completed: return "completed";
        case formats::MissionState::Failed: return "failed";
    }
    return "not_started";
}

formats::MissionState mission_state_from_text(std::string_view text)
{
    const std::string lowered = core::to_lower(text);
    if (lowered == "active") return formats::MissionState::Active;
    if (lowered == "completed" || lowered == "complete") return formats::MissionState::Completed;
    if (lowered == "failed") return formats::MissionState::Failed;
    return formats::MissionState::NotStarted;
}

bool parse_bool_text(std::string_view text)
{
    const std::string lowered = core::to_lower(text);
    return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
}

}  // namespace

// ------------------------------------------------------------------ flags ---

void StoryFlags::set(std::string_view flag, std::string value)
{
    if (flag.empty()) return;
    values[std::string(flag)] = std::move(value);
}

void StoryFlags::set_bool(std::string_view flag, bool value)
{
    set(flag, value ? "true" : "false");
}

void StoryFlags::clear(std::string_view flag)
{
    values.erase(std::string(flag));
}

bool StoryFlags::has(std::string_view flag) const
{
    return values.find(std::string(flag)) != values.end();
}

std::string StoryFlags::get(std::string_view flag, std::string fallback) const
{
    const auto found = values.find(std::string(flag));
    if (found == values.end()) return fallback;
    return found->second;
}

bool StoryFlags::get_bool(std::string_view flag, bool fallback) const
{
    const auto found = values.find(std::string(flag));
    if (found == values.end()) return fallback;
    return parse_bool_text(found->second);
}

int StoryFlags::get_int(std::string_view flag, int fallback) const
{
    const auto found = values.find(std::string(flag));
    if (found == values.end()) return fallback;
    int value = fallback;
    const auto* begin = found->second.data();
    const auto* end = begin + found->second.size();
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{}) return fallback;
    return value;
}

bool StoryFlags::matches(std::string_view flag, std::string_view expected) const
{
    const auto found = values.find(std::string(flag));
    const std::string actual = found == values.end() ? std::string{} : found->second;
    if (actual == expected) return true;
    // "1" and "true" both mean true; an empty expectation only matches a set flag.
    if (expected.empty()) return found != values.end() && !actual.empty();
    return parse_bool_text(actual) && parse_bool_text(expected);
}

// -------------------------------------------------------------- progress ---

std::string_view to_string(ObjectiveState state) noexcept
{
    switch (state) {
        case ObjectiveState::Inactive: return "inactive";
        case ObjectiveState::Active: return "active";
        case ObjectiveState::Complete: return "complete";
        case ObjectiveState::Failed: return "failed";
    }
    return "inactive";
}

std::uint32_t MissionProgress::progress_of(std::string_view objective_id) const
{
    const auto found = objective_progress.find(std::string(objective_id));
    return found == objective_progress.end() ? 0 : found->second;
}

ObjectiveState MissionProgress::state_of(std::string_view objective_id) const
{
    const auto found = objective_state.find(std::string(objective_id));
    if (found == objective_state.end()) return ObjectiveState::Inactive;
    return found->second;
}

bool MissionProgress::all_required_complete(const formats::MissionDocument& mission) const
{
    for (const auto& objective : mission.objectives) {
        if (objective.optional) continue;
        if (state_of(objective.objective_id) != ObjectiveState::Complete) return false;
    }
    return true;
}

// -------------------------------------------------------------- missions ---

bool StoryState::dependency_satisfied(const formats::MissionDocument& mission) const
{
    for (const auto& dependency : mission.dependencies) {
        const auto found = missions_.find(dependency);
        if (found == missions_.end()) return false;
        if (found->second.state != formats::MissionState::Completed) return false;
    }
    return true;
}

core::Status StoryState::start_mission(const formats::MissionDocument& mission)
{
    core::DiagnosticList diagnostics;
    if (mission.asset_id.empty()) {
        diagnostics.error("BULLY-RT-0100", "mission has no asset id");
        return core::Status(std::move(diagnostics));
    }
    const auto existing = missions_.find(mission.asset_id);
    if (existing != missions_.end()) {
        if (existing->second.state == formats::MissionState::Active) {
            diagnostics.error("BULLY-RT-0101", "mission is already active", mission.asset_id);
            return core::Status(std::move(diagnostics));
        }
        if (existing->second.state == formats::MissionState::Completed && !mission.repeatable) {
            diagnostics.error("BULLY-RT-0102",
                              "mission is complete and not repeatable", mission.asset_id);
            return core::Status(std::move(diagnostics));
        }
    }
    if (!dependency_satisfied(mission)) {
        diagnostics.error("BULLY-RT-0103",
                          "mission dependencies are not complete: " +
                              core::join(mission.dependencies, ", "),
                          mission.asset_id);
        return core::Status(std::move(diagnostics));
    }
    auto structure = mission.validate();
    if (!structure) {
        diagnostics.append(structure.diagnostics());
        return core::Status(std::move(diagnostics));
    }

    MissionProgress progress;
    progress.mission_asset = mission.asset_id;
    progress.state = formats::MissionState::Active;
    progress.current_node = mission.start_node;
    if (existing != missions_.end()) progress.completions = existing->second.completions;
    for (const auto& objective : mission.objectives) {
        progress.objective_state[objective.objective_id] = ObjectiveState::Active;
        progress.objective_progress[objective.objective_id] = 0;
    }
    missions_[mission.asset_id] = std::move(progress);
    core::log_info(kLogCategory, "mission started: " + mission.asset_id);
    return core::Status{};
}

std::optional<MissionProgress> StoryState::mission(std::string_view asset_id) const
{
    const auto found = missions_.find(std::string(asset_id));
    if (found == missions_.end()) return std::nullopt;
    return found->second;
}

std::vector<MissionProgress> StoryState::active_missions() const
{
    std::vector<MissionProgress> active;
    for (const auto& [id, progress] : missions_) {
        if (progress.state == formats::MissionState::Active) active.push_back(progress);
    }
    std::sort(active.begin(), active.end(), [](const MissionProgress& a, const MissionProgress& b) {
        return a.mission_asset < b.mission_asset;
    });
    return active;
}

bool StoryState::mission_complete(std::string_view asset_id) const
{
    const auto found = missions_.find(std::string(asset_id));
    return found != missions_.end() &&
           found->second.state == formats::MissionState::Completed;
}

MissionTick StoryState::advance_objective(const formats::MissionDocument& mission,
                                          std::string_view objective_id,
                                          std::uint32_t amount)
{
    MissionTick tick;
    auto found = missions_.find(mission.asset_id);
    if (found == missions_.end() || found->second.state != formats::MissionState::Active) {
        tick.messages.push_back("mission is not active");
        return tick;
    }
    const auto* objective = mission.find_objective(objective_id);
    if (objective == nullptr) {
        tick.messages.push_back("unknown objective: " + std::string(objective_id));
        return tick;
    }
    auto& progress = found->second;
    if (progress.state_of(objective->objective_id) == ObjectiveState::Complete) return tick;

    const std::uint32_t before = progress.progress_of(objective->objective_id);
    const std::uint32_t required = std::max<std::uint32_t>(1, objective->required_count);
    const std::uint32_t after = std::min(required, before + std::max<std::uint32_t>(1, amount));
    progress.objective_progress[objective->objective_id] = after;
    if (after == before) return tick;
    tick.changed = true;
    tick.messages.push_back(objective->description + " (" + std::to_string(after) + "/" +
                            std::to_string(required) + ")");

    if (after >= required) {
        progress.objective_state[objective->objective_id] = ObjectiveState::Complete;
        if (!objective->dialogue_line.empty()) {
            tick.messages.push_back("plays line " + objective->dialogue_line);
        }
        // Follow the mission graph: prefer a sequential edge whose target node
        // gates on this objective, otherwise keep the current node.
        if (const auto* node = mission.find_node(progress.current_node); node != nullptr) {
            for (const auto& edge : node->edges) {
                if (edge.kind != formats::EdgeKind::Sequential) continue;
                const auto* next = mission.find_node(edge.to);
                if (next == nullptr) continue;
                progress.current_node = next->node_id;
                break;
            }
        }
    }

    if (progress.all_required_complete(mission)) {
        progress.state = formats::MissionState::Completed;
        ++progress.completions;
        tick.completed = true;
        tick.messages.push_back("mission complete: " + mission.name);
        for (const auto& flag : mission.completion_flags) flags_.set_bool(flag, true);
        core::log_info(kLogCategory, "mission complete: " + mission.asset_id);
    }
    tick.node = progress.current_node;
    return tick;
}

MissionTick StoryState::tick_mission(const formats::MissionDocument& mission, float delta_seconds)
{
    MissionTick tick;
    auto found = missions_.find(mission.asset_id);
    if (found == missions_.end() || found->second.state != formats::MissionState::Active) {
        return tick;
    }
    found->second.elapsed_seconds += delta_seconds;
    if (mission.timed && mission.time_limit_seconds > 0.0f &&
        found->second.elapsed_seconds >= mission.time_limit_seconds) {
        return fail_mission(mission, "time limit reached");
    }
    return tick;
}

MissionTick StoryState::fail_mission(const formats::MissionDocument& mission,
                                     std::string_view reason)
{
    MissionTick tick;
    auto found = missions_.find(mission.asset_id);
    if (found == missions_.end() || found->second.state != formats::MissionState::Active) {
        return tick;
    }
    found->second.state = formats::MissionState::Failed;
    for (const auto& flag : mission.failure_flags) flags_.set_bool(flag, true);
    tick.changed = true;
    tick.failed = true;
    tick.node = found->second.current_node;
    if (!reason.empty()) tick.messages.push_back(std::string(reason));
    core::log_warn(kLogCategory, "mission failed: " + mission.asset_id);
    return tick;
}

// -------------------------------------------------------------- dialogue ---

core::Status StoryState::begin_dialogue(const formats::DialogueDocument& dialogue)
{
    core::DiagnosticList diagnostics;
    if (dialogue.asset_id.empty()) {
        diagnostics.error("BULLY-RT-0110", "dialogue document has no asset id");
        return core::Status(std::move(diagnostics));
    }
    if (dialogue.entry_node.empty()) {
        diagnostics.error("BULLY-RT-0111", "dialogue document has no entry node",
                          dialogue.asset_id);
        return core::Status(std::move(diagnostics));
    }
    if (dialogue.find_node(dialogue.entry_node) == nullptr) {
        diagnostics.error("BULLY-RT-0112",
                          "entry node '" + dialogue.entry_node + "' does not exist",
                          dialogue.asset_id);
        return core::Status(std::move(diagnostics));
    }
    DialogueProgress progress;
    progress.dialogue_asset = dialogue.asset_id;
    progress.current_node = dialogue.entry_node;
    dialogues_[dialogue.asset_id] = std::move(progress);
    return core::Status{};
}

std::optional<DialogueProgress> StoryState::dialogue(std::string_view asset_id) const
{
    const auto found = dialogues_.find(std::string(asset_id));
    if (found == dialogues_.end()) return std::nullopt;
    return found->second;
}

core::Result<std::string> StoryState::current_dialogue_node(
    const formats::DialogueDocument& dialogue) const
{
    core::DiagnosticList diagnostics;
    const auto found = dialogues_.find(dialogue.asset_id);
    if (found == dialogues_.end()) {
        diagnostics.error("BULLY-RT-0113", "dialogue has not been started", dialogue.asset_id);
        return core::Result<std::string>(std::move(diagnostics));
    }
    if (found->second.finished) {
        diagnostics.error("BULLY-RT-0114", "dialogue has already finished", dialogue.asset_id);
        return core::Result<std::string>(std::move(diagnostics));
    }
    return found->second.current_node;
}

core::Result<std::string> StoryState::advance_dialogue(const formats::DialogueDocument& dialogue,
                                                      std::optional<std::size_t> branch_index)
{
    core::DiagnosticList diagnostics;
    auto found = dialogues_.find(dialogue.asset_id);
    if (found == dialogues_.end()) {
        diagnostics.error("BULLY-RT-0113", "dialogue has not been started", dialogue.asset_id);
        return core::Result<std::string>(std::move(diagnostics));
    }
    auto& progress = found->second;
    if (progress.finished) {
        diagnostics.error("BULLY-RT-0114", "dialogue has already finished", dialogue.asset_id);
        return core::Result<std::string>(std::move(diagnostics));
    }
    const auto* node = dialogue.find_node(progress.current_node);
    if (node == nullptr) {
        diagnostics.error("BULLY-RT-0115",
                          "current node '" + progress.current_node + "' no longer exists",
                          dialogue.asset_id);
        return core::Result<std::string>(std::move(diagnostics));
    }

    // Record the line as heard before moving on.
    if (node->kind == formats::NodeKind::Line && !node->line_id.empty()) {
        mark_line_heard(node->line_id);
        if (const auto* line = dialogue.find_line(node->line_id); line != nullptr) {
            if (!progress.visited_lines.empty() &&
                progress.visited_lines.back() == line->line_id) {
                // fallthrough
            }
            progress.visited_lines.push_back(line->line_id);
        }
    }

    std::string next;
    switch (node->kind) {
        case formats::NodeKind::End:
            progress.finished = true;
            return std::string{};
        case formats::NodeKind::Choice: {
            if (!branch_index.has_value()) {
                diagnostics.error("BULLY-RT-0116", "choice node needs a branch index",
                                  dialogue.asset_id);
                return core::Result<std::string>(std::move(diagnostics));
            }
            if (*branch_index >= node->branches.size()) {
                diagnostics.error("BULLY-RT-0117",
                                  "branch index " + std::to_string(*branch_index) +
                                      " is out of range (" +
                                      std::to_string(node->branches.size()) + " branches)",
                                  dialogue.asset_id);
                return core::Result<std::string>(std::move(diagnostics));
            }
            const auto& branch = node->branches[*branch_index];
            if (!branch.required_flag.empty() &&
                !flags_.matches(branch.required_flag, branch.required_value)) {
                diagnostics.error("BULLY-RT-0118",
                                  "branch requires flag '" + branch.required_flag + "' = '" +
                                      branch.required_value + "'",
                                  dialogue.asset_id);
                return core::Result<std::string>(std::move(diagnostics));
            }
            next = branch.target_node;
            break;
        }
        case formats::NodeKind::Condition: {
            // Condition nodes prefer the explicit flag branch; when the node
            // declares no flag it behaves like a jump on the true target.
            const bool condition = node->condition_flag.empty() ||
                                   flags_.matches(node->condition_flag, node->condition_value);
            next = condition ? node->true_target : node->false_target;
            break;
        }
        case formats::NodeKind::Jump:
            next = node->next_target;
            break;
        case formats::NodeKind::Line:
        case formats::NodeKind::Cutscene:
            next = node->next_target;
            if (next.empty()) next = node->true_target;
            break;
    }

    if (next.empty()) {
        progress.finished = true;
        return std::string{};
    }
    if (dialogue.find_node(next) == nullptr) {
        diagnostics.error("BULLY-RT-0119", "branch target '" + next + "' does not exist",
                          dialogue.asset_id);
        return core::Result<std::string>(std::move(diagnostics));
    }
    progress.current_node = next;
    return next;
}

void StoryState::mark_line_heard(std::string_view line_id)
{
    if (line_id.empty()) return;
    const std::string id(line_id);
    if (std::find(heard_lines_.begin(), heard_lines_.end(), id) != heard_lines_.end()) return;
    heard_lines_.push_back(id);
}

bool StoryState::line_heard(std::string_view line_id) const
{
    return std::find(heard_lines_.begin(), heard_lines_.end(), std::string(line_id)) !=
           heard_lines_.end();
}

void StoryState::enter_scene(std::string scene_asset) { current_scene_ = std::move(scene_asset); }

// ------------------------------------------------------------------ save ---

std::string StoryState::to_json() const
{
    core::json::Value root = core::json::Value::make_object();
    core::json::Value flags = core::json::Value::make_object();
    for (const auto& [flag, value] : flags_.values) flags[flag] = value;
    root["flags"] = std::move(flags);
    root["scene"] = current_scene_;

    core::json::Value missions = core::json::Value::make_array();
    for (const auto& [id, progress] : missions_) {
        core::json::Value entry = core::json::Value::make_object();
        entry["asset"] = id;
        entry["state"] = std::string(mission_state_text(progress.state));
        entry["node"] = progress.current_node;
        entry["elapsed"] = static_cast<double>(progress.elapsed_seconds);
        entry["completions"] = static_cast<std::int64_t>(progress.completions);
        core::json::Value objectives = core::json::Value::make_object();
        for (const auto& [objective, count] : progress.objective_progress) {
            objectives[objective] = static_cast<std::int64_t>(count);
        }
        entry["objectives"] = std::move(objectives);
        core::json::Value states = core::json::Value::make_object();
        for (const auto& [objective, state] : progress.objective_state) {
            states[objective] = std::string(to_string(state));
        }
        entry["objective_states"] = std::move(states);
        missions.push(std::move(entry));
    }
    root["missions"] = std::move(missions);

    core::json::Value dialogues = core::json::Value::make_array();
    for (const auto& [id, progress] : dialogues_) {
        core::json::Value entry = core::json::Value::make_object();
        entry["asset"] = id;
        entry["node"] = progress.current_node;
        entry["finished"] = progress.finished;
        core::json::Value lines = core::json::Value::make_array();
        for (const auto& line : progress.visited_lines) lines.push(line);
        entry["visited"] = std::move(lines);
        dialogues.push(std::move(entry));
    }
    root["dialogues"] = std::move(dialogues);

    core::json::Value heard = core::json::Value::make_array();
    for (const auto& line : heard_lines_) heard.push(line);
    root["heard_lines"] = std::move(heard);
    return root.dump(2);
}

core::Result<StoryState> StoryState::from_json(std::string_view text)
{
    core::DiagnosticList diagnostics;
    auto parsed = core::json::parse(text);
    if (!parsed) {
        diagnostics.append(parsed.diagnostics());
        return core::Result<StoryState>(std::move(diagnostics));
    }
    const auto& root = parsed.value();
    if (!root.is_object()) {
        diagnostics.error("BULLY-RT-0120", "save data root is not an object");
        return core::Result<StoryState>(std::move(diagnostics));
    }
    StoryState state;
    if (const auto* flags = root.find("flags"); flags != nullptr && flags->is_object()) {
        for (const auto& [key, value] : flags->object().entries()) {
            state.flags_.values[key] = value.as_string();
        }
    }
    state.current_scene_ = root.get_string("scene", "");
    if (const auto* missions = root.find("missions"); missions != nullptr && missions->is_array()) {
        for (const auto& entry : missions->items()) {
            MissionProgress progress;
            progress.mission_asset = entry.get_string("asset");
            progress.state = mission_state_from_text(entry.get_string("state", "not_started"));
            progress.current_node = entry.get_string("node");
            progress.elapsed_seconds = static_cast<float>(entry.get_number("elapsed", 0.0));
            progress.completions = static_cast<std::uint32_t>(entry.get_int("completions", 0));
            if (const auto* objectives = entry.find("objectives");
                objectives != nullptr && objectives->is_object()) {
                for (const auto& [key, value] : objectives->object().entries()) {
                    progress.objective_progress[key] =
                        static_cast<std::uint32_t>(std::max<std::int64_t>(0, value.as_int(0)));
                }
            }
            if (const auto* states = entry.find("objective_states");
                states != nullptr && states->is_object()) {
                for (const auto& [key, value] : states->object().entries()) {
                    const std::string text_state = value.as_string("inactive");
                    ObjectiveState objective_state = ObjectiveState::Inactive;
                    if (text_state == "active") objective_state = ObjectiveState::Active;
                    else if (text_state == "complete") objective_state = ObjectiveState::Complete;
                    else if (text_state == "failed") objective_state = ObjectiveState::Failed;
                    progress.objective_state[key] = objective_state;
                }
            }
            if (progress.mission_asset.empty()) continue;
            state.missions_[progress.mission_asset] = std::move(progress);
        }
    }
    if (const auto* dialogues = root.find("dialogues");
        dialogues != nullptr && dialogues->is_array()) {
        for (const auto& entry : dialogues->items()) {
            DialogueProgress progress;
            progress.dialogue_asset = entry.get_string("asset");
            progress.current_node = entry.get_string("node");
            progress.finished = entry.get_bool("finished", false);
            if (const auto* lines = entry.find("visited"); lines != nullptr && lines->is_array()) {
                for (const auto& line : lines->items()) progress.visited_lines.push_back(line.as_string());
            }
            if (progress.dialogue_asset.empty()) continue;
            state.dialogues_[progress.dialogue_asset] = std::move(progress);
        }
    }
    if (const auto* heard = root.find("heard_lines"); heard != nullptr && heard->is_array()) {
        for (const auto& line : heard->items()) state.mark_line_heard(line.as_string());
    }
    return state;
}

}  // namespace bully::runtime
