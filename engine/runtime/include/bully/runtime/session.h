// Bully Studio runtime - game session.
//
// GameSession is the piece that ties the runtime subsystems together in the
// order a real frame runs:
//
//   input snapshot -> fixed steps (story tick, mission timers, dialogue) -> render
//
// It owns the asset database, the story state, the frame clock and the input
// map, and it knows how to boot a project: mount `built/` (developer loop) or a
// `.bpackage` (shipped build), load the entry scene and enter it.
//
// It is headless by construction. apps/game adds an SDL3 window around
// `begin_frame`/`step`, and `--headless` runs the exact same code path for CI
// and for reproducing a bug without a display.
//
// Diagnostic codes: BULLY-RT-0300..0319
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/runtime/asset_database.h"
#include "bully/runtime/clock.h"
#include "bully/runtime/story.h"

namespace bully::runtime {

/// Where a session gets its assets from.
enum class SessionSource : std::uint8_t {
    BuildDirectory = 0,  // <project>/built
    Package = 1,         // an explicit .bpackage
};

struct SessionConfig {
    std::filesystem::path project_root;   // <project> for BuildDirectory
    std::filesystem::path package_path;   // used when source == Package
    SessionSource source = SessionSource::BuildDirectory;
    std::string entry_scene;              // scene asset id or logical path
    bool load_plugins_qa = false;         // reserved: plugin runtime systems
    FrameClock::Config clock{};
};

/// One frame's worth of decisions, produced by `begin_frame` and consumed by
/// `step`. Keeping it explicit means the renderer never has to guess what the
/// simulation did.
struct FramePlan {
    std::uint32_t steps = 0;
    double alpha = 0.0;
    bool paused = false;
};

struct SessionStatistics {
    std::uint64_t frames = 0;
    std::uint64_t steps = 0;
    double simulated_seconds = 0.0;
    std::uint32_t missions_active = 0;
    std::uint32_t missions_completed = 0;
    std::uint32_t dialogue_lines = 0;
    std::size_t assets_decoded = 0;
};

class GameSession {
public:
    GameSession();
    ~GameSession();
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    /// Mounts assets and enters the configured scene.
    [[nodiscard]] core::Status boot(const SessionConfig& config);

    [[nodiscard]] const SessionConfig& config() const noexcept { return config_; }
    [[nodiscard]] bool booted() const noexcept { return booted_; }

    [[nodiscard]] AssetDatabase& assets() { return assets_; }
    [[nodiscard]] const AssetDatabase& assets() const { return assets_; }
    [[nodiscard]] StoryState& story() { return story_; }
    [[nodiscard]] const StoryState& story() const { return story_; }
    [[nodiscard]] FrameClock& clock() { return clock_; }
    [[nodiscard]] const FrameClock& clock() const { return clock_; }
    [[nodiscard]] InputMap& input() { return input_; }
    [[nodiscard]] const InputMap& input() const { return input_; }

    /// The scene currently simulated (null when the project has no scene).
    [[nodiscard]] const formats::SceneDocument* scene() const { return scene_.get(); }

    /// Registers the raw input snapshot for this frame ("key:w" -> 1.0f).
    void set_input_snapshot(std::map<std::string, float> raw);

    /// Starts a frame: paces the clock and decides how many fixed steps to run.
    [[nodiscard]] FramePlan begin_frame(double delta_seconds);

    /// Runs one fixed step (story/mission/dialogue update) and advances the
    /// clock. Call `begin_frame`'s `steps` times.
    void step();

    /// Ends the frame: releases input edges and counts statistics.
    void end_frame();

    /// Convenience for headless runs: simulates `frames` frames at a fixed
    /// delta. Returns the statistics after the run.
    [[nodiscard]] SessionStatistics simulate(std::uint32_t frames, double delta_seconds,
                                            const std::map<std::string, float>& raw = {});

    [[nodiscard]] SessionStatistics statistics() const;

    /// Serialises the story state (the save game).
    [[nodiscard]] std::string save_game() const { return story_.to_json(); }
    [[nodiscard]] core::Status load_game(std::string_view json);

    /// Makes a mission tick-able (loaded from the mounts). Missions started
    /// directly through `story()` are picked up automatically on the next step.
    [[nodiscard]] core::Status register_mission(const std::string& mission_asset);

    /// Queues a dialogue document to play on the next steps (used by mission
    /// objectives and triggers).
    void play_dialogue(std::string dialogue_asset);

private:
    void update_dialogue();

    SessionConfig config_{};
    bool booted_ = false;
    AssetDatabase assets_;
    StoryState story_;
    FrameClock clock_;
    InputMap input_;
    std::shared_ptr<const formats::SceneDocument> scene_;
    std::map<std::string, float> raw_input_;
    std::map<std::string, std::shared_ptr<const formats::MissionDocument>> missions_;
    std::optional<std::string> active_dialogue_;
    std::uint32_t steps_this_frame_ = 0;
    SessionStatistics stats_{};
};

}  // namespace bully::runtime
