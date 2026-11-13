// Bully Studio runtime - frame clock and input mapping.
//
// Both are headless: the clock is a pure accumulator, the input map is a pure
// projection of physical inputs onto named actions. The SDL layer (apps/game)
// feeds them; tests drive them directly, so timing and key-binding bugs are
// covered without a window.
//
// Diagnostic codes: BULLY-RT-0200..0219
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"

namespace bully::runtime {

/// Fixed-timestep accumulator with a maximum catch-up budget.
class FrameClock {
public:
    struct Config {
        double fixed_step_seconds = 1.0 / 60.0;
        /// Never simulate more than this many fixed steps per frame: a long
        /// stall (breakpoint, disk hitch) must not trigger a spiral of death.
        std::uint32_t max_steps_per_frame = 8;
        double time_scale = 1.0;
    };

    FrameClock() = default;
    explicit FrameClock(Config config) : config_(config) {}

    /// Feeds a frame delta. Returns how many fixed steps should run now.
    std::uint32_t begin_frame(double delta_seconds);
    /// Marks one fixed step as simulated (called by the loop after each step).
    void end_step();
    /// Ends the frame, keeping the remainder in the accumulator.
    void end_frame();

    void set_time_scale(double scale);
    void set_paused(bool paused);
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    void set_config(Config config);
    [[nodiscard]] const Config& config() const noexcept { return config_; }

    [[nodiscard]] double fixed_step() const noexcept { return config_.fixed_step_seconds; }
    /// Interpolation alpha in [0,1) between the previous and current step.
    [[nodiscard]] double alpha() const noexcept;
    [[nodiscard]] double accumulator() const noexcept { return accumulator_; }
    [[nodiscard]] std::uint64_t frame_index() const noexcept { return frame_index_; }
    [[nodiscard]] std::uint64_t step_index() const noexcept { return step_index_; }
    [[nodiscard]] double elapsed_seconds() const noexcept { return elapsed_; }
    /// Steps dropped because of the catch-up budget (visible in the debug HUD).
    [[nodiscard]] std::uint32_t dropped_steps() const noexcept { return dropped_steps_; }
    /// Reset for a new scene / new game.
    void reset();

private:
    Config config_{};
    double accumulator_ = 0.0;
    double elapsed_ = 0.0;
    double frame_delta_ = 0.0;
    std::uint64_t frame_index_ = 0;
    std::uint64_t step_index_ = 0;
    std::uint32_t steps_this_frame_ = 0;
    std::uint32_t dropped_steps_ = 0;
    bool paused_ = false;
};

/// A physical input: keyboard key, mouse button, gamepad button or axis.
struct InputBinding {
    std::string device = "key";  // "key", "mouse", "pad"
    std::string control;         // "space", "left_trigger", ...
    float scale = 1.0f;          // -1 for inverted axes

    [[nodiscard]] bool operator==(const InputBinding&) const = default;
};

/// Named actions ("move.forward", "interact", "pause") mapped to bindings.
/// Rebinding is a data edit, which is what the Studio's controls panel writes.
class InputMap {
public:
    void bind(std::string action, InputBinding binding);
    /// Compiles the classic WASD + gamepad layout configured in the templates.
    void bind_defaults();
    [[nodiscard]] std::vector<InputBinding> bindings_for(std::string_view action) const;
    [[nodiscard]] std::vector<std::string> actions() const;

    /// Evaluates the current raw device state into 0..1 action values.
    /// `raw` keys are "device:control" (e.g. "key:space") with a 0..1 value.
    [[nodiscard]] float value(std::string_view action,
                              const std::map<std::string, float>& raw) const;
    [[nodiscard]] bool pressed(std::string_view action,
                               const std::map<std::string, float>& raw) const;
    /// Edge detection: true only when the action crossed `threshold` since the
    /// previous query. Call once per frame; a held button reports a single
    /// press until it is released.
    [[nodiscard]] bool just_pressed(std::string_view action,
                                    const std::map<std::string, float>& raw,
                                    float threshold = 0.5f);
    void end_frame();
    void clear();

    /// Two-axis helper for movement (forward/back + left/right).
    [[nodiscard]] std::pair<float, float> axis(std::string_view negative_x,
                                               std::string_view positive_x,
                                               std::string_view negative_y,
                                               std::string_view positive_y,
                                               const std::map<std::string, float>& raw) const;

private:
    std::map<std::string, std::vector<InputBinding>> bindings_;
    std::map<std::string, bool> previous_state_;
};

}  // namespace bully::runtime
