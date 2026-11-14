#include "bully/runtime/session.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>

#include "bully/core/json.h"
#include "bully/core/log.h"
#include "bully/core/strings.h"

namespace bully::runtime {

namespace {

constexpr std::string_view kLogCategory = "session";

}  // namespace

GameSession::GameSession() = default;
GameSession::~GameSession() = default;

core::Status GameSession::boot(const SessionConfig& config)
{
    core::DiagnosticList diagnostics;
    config_ = config;
    clock_.set_config(config.clock);
    input_.bind_defaults();

    if (config_.source == SessionSource::Package) {
        if (config_.package_path.empty()) {
            diagnostics.error("BULLY-RT-0300", "package session without a package path");
            return core::Status(std::move(diagnostics));
        }
        auto status = assets_.mount_package(config_.package_path, "package");
        if (!status) {
            diagnostics.append(status.diagnostics());
            return core::Status(std::move(diagnostics));
        }
    } else {
        if (config_.project_root.empty()) {
            diagnostics.error("BULLY-RT-0301", "build-directory session without a project root");
            return core::Status(std::move(diagnostics));
        }
        auto status = assets_.mount_directory(config_.project_root / "built", "project");
        if (!status) {
            diagnostics.append(status.diagnostics());
            return core::Status(std::move(diagnostics));
        }
    }

    // Entry scene: explicit id/path, otherwise the first compiled scene.
    std::string entry = config_.entry_scene;
    if (entry.empty()) {
        const auto scenes = assets_.entries(AssetKind::Scene);
        if (!scenes.empty()) entry = scenes.front().logical_path;
    }
    if (!entry.empty()) {
        auto loaded = assets_.load_scene(entry);
        if (!loaded) {
            diagnostics.append(loaded.diagnostics());
            return core::Status(std::move(diagnostics));
        }
        scene_ = loaded.value();
        story_.enter_scene(scene_->asset_id.empty() ? entry : scene_->asset_id);
        // Mission assets referenced by the scene's triggers become playable.
        for (const auto& trigger : scene_->triggers) {
            if (trigger.kind != "mission" || trigger.payload_json.empty()) continue;
            const auto payload = core::json::parse(trigger.payload_json);
            if (!payload) continue;
            const std::string mission_id = payload.value().get_string("mission");
            if (mission_id.empty()) continue;
            auto mission = assets_.load_mission(mission_id);
            if (!mission) {
                diagnostics.warn("BULLY-RT-0302",
                                 "trigger '" + trigger.trigger_id + "' references mission '" +
                                     mission_id + "' which is not in the mounts",
                                 scene_->name);
                continue;
            }
            missions_[mission_id] = mission.value();
        }
        core::log_info(kLogCategory, "entered scene " + entry + " with " +
                                         std::to_string(missions_.size()) + " mission(s)");
    } else {
        diagnostics.warn("BULLY-RT-0303", "no scene asset in the mounts; running without a scene");
    }

    booted_ = true;
    stats_ = SessionStatistics{};
    stats_.assets_decoded = assets_.statistics().decoded;
    // A boot that only produced warnings succeeded; the warnings stay attached
    // so the shell can show them (a missing trigger mission is common while a
    // project is being authored).
    if (diagnostics.has_errors()) return core::Status(std::move(diagnostics));
    return core::Status{}.attach_diagnostics(std::move(diagnostics));
}

void GameSession::set_input_snapshot(std::map<std::string, float> raw)
{
    raw_input_ = std::move(raw);
}

FramePlan GameSession::begin_frame(double delta_seconds)
{
    FramePlan plan;
    plan.steps = clock_.begin_frame(delta_seconds);
    plan.paused = clock_.paused();
    steps_this_frame_ = plan.steps;
    plan.alpha = clock_.alpha();
    stats_.frames = clock_.frame_index();
    return plan;
}

void GameSession::step()
{
    if (!booted_) return;
    const double delta = clock_.fixed_step();

    // 1. mission timers and completion, then the dialogue currently playing.
    //    Missions can also be started directly through the story layer (script,
    //    console, test); pick those up lazily so they still tick.
    for (const auto& active : story_.active_missions()) {
        if (missions_.find(active.mission_asset) != missions_.end()) continue;
        auto loaded = assets_.load_mission(active.mission_asset);
        if (loaded) missions_[active.mission_asset] = loaded.value();
    }
    for (auto& [id, mission] : missions_) {
        auto tick = story_.tick_mission(*mission, static_cast<float>(delta));
        for (const auto& message : tick.messages) {
            core::log_info(kLogCategory, id + ": " + message);
        }
    }
    update_dialogue();

    // 2. player-driven progress: interaction buttons advance objectives whose
    //    kind is a conversation/interaction, which is the one place the shell
    //    needs to tell the story layer "the player did the thing".
    if (input_.just_pressed("interact", raw_input_)) {
        for (auto& [id, mission] : missions_) {
            const auto progress = story_.mission(id);
            if (!progress || progress->state != formats::MissionState::Active) continue;
            for (const auto& objective : mission->objectives) {
                if (objective.kind != formats::ObjectiveKind::TalkTo &&
                    objective.kind != formats::ObjectiveKind::UseObject &&
                    objective.kind != formats::ObjectiveKind::GoTo) {
                    continue;
                }
                if (progress->state_of(objective.objective_id) == ObjectiveState::Complete) {
                    continue;
                }
                (void)story_.advance_objective(*mission, objective.objective_id);
                break;
            }
        }
    }
    if (input_.just_pressed("attack", raw_input_)) {
        for (auto& [id, mission] : missions_) {
            const auto progress = story_.mission(id);
            if (!progress || progress->state != formats::MissionState::Active) continue;
            for (const auto& objective : mission->objectives) {
                if (objective.kind != formats::ObjectiveKind::Defeat) continue;
                if (progress->state_of(objective.objective_id) == ObjectiveState::Complete) {
                    continue;
                }
                (void)story_.advance_objective(*mission, objective.objective_id);
                break;
            }
        }
    }

    clock_.end_step();
    stats_.steps = clock_.step_index();
    stats_.simulated_seconds = clock_.elapsed_seconds();
}

void GameSession::update_dialogue()
{
    if (!active_dialogue_.has_value()) return;
    auto loaded = assets_.load_dialogue(*active_dialogue_);
    if (!loaded) {
        active_dialogue_.reset();
        return;
    }
    const auto& dialogue = *loaded.value();
    auto progress = story_.dialogue(dialogue.asset_id);
    if (!progress) {
        if (!story_.begin_dialogue(dialogue)) {
            active_dialogue_.reset();
            return;
        }
        progress = story_.dialogue(dialogue.asset_id);
        if (!progress) return;
    }
    if (progress->finished) {
        active_dialogue_.reset();
        return;
    }
    const auto* node = dialogue.find_node(progress->current_node);
    if (node == nullptr) {
        active_dialogue_.reset();
        return;
    }
    // Line and Cutscene nodes advance on the interact button; choices wait for
    // an explicit branch selection from the UI or a script.
    if (node->kind == formats::NodeKind::Line || node->kind == formats::NodeKind::Cutscene ||
        node->kind == formats::NodeKind::Jump) {
        if (input_.just_pressed("interact", raw_input_)) {
            (void)story_.advance_dialogue(dialogue);
        }
    }
    ++stats_.dialogue_lines;
}

void GameSession::end_frame()
{
    input_.end_frame();
    steps_this_frame_ = 0;
    std::uint32_t active = 0;
    std::uint32_t completed = 0;
    for (const auto& [id, mission] : missions_) {
        const auto progress = story_.mission(id);
        if (!progress) continue;
        if (progress->state == formats::MissionState::Active) ++active;
        if (progress->state == formats::MissionState::Completed) ++completed;
    }
    stats_.missions_active = active;
    stats_.missions_completed = completed;
    stats_.assets_decoded = assets_.statistics().decoded;
}

SessionStatistics GameSession::simulate(std::uint32_t frames, double delta_seconds,
                                       const std::map<std::string, float>& raw)
{
    set_input_snapshot(raw);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto plan = begin_frame(delta_seconds);
        for (std::uint32_t index = 0; index < plan.steps; ++index) step();
        end_frame();
    }
    return statistics();
}

SessionStatistics GameSession::statistics() const { return stats_; }

core::Status GameSession::load_game(std::string_view json)
{
    core::DiagnosticList diagnostics;
    auto loaded = StoryState::from_json(json);
    if (!loaded) {
        diagnostics.append(loaded.diagnostics());
        return core::Status(std::move(diagnostics));
    }
    story_ = std::move(loaded.value());
    return core::Status{};
}

core::Status GameSession::register_mission(const std::string& mission_asset)
{
    auto loaded = assets_.load_mission(mission_asset);
    if (!loaded) return core::Status(loaded.diagnostics());
    missions_[mission_asset] = loaded.value();
    return core::Status{};
}

void GameSession::play_dialogue(std::string dialogue_asset)
{
    if (dialogue_asset.empty()) return;
    active_dialogue_ = std::move(dialogue_asset);
}

}  // namespace bully::runtime
