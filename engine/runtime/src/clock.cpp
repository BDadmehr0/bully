#include "bully/runtime/clock.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bully::runtime {

// ------------------------------------------------------------------ clock ---

std::uint32_t FrameClock::begin_frame(double delta_seconds)
{
    ++frame_index_;
    if (paused_ || config_.fixed_step_seconds <= 0.0) {
        steps_this_frame_ = 0;
        frame_delta_ = 0.0;
        return 0;
    }
    // Clamp pathological deltas (debugger halt, first frame after load) instead
    // of silently eating them: the accumulator gets at most one second.
    const double scaled = std::clamp(delta_seconds, 0.0, 1.0) * config_.time_scale;
    frame_delta_ = scaled;
    accumulator_ += scaled;
    const double step = config_.fixed_step_seconds;
    std::uint32_t steps = 0;
    while (accumulator_ >= step && steps < config_.max_steps_per_frame) {
        accumulator_ -= step;
        ++steps;
    }
    if (accumulator_ >= step) {
        const auto remaining =
            static_cast<std::uint32_t>(std::floor(accumulator_ / step));
        dropped_steps_ += remaining;
        accumulator_ = std::fmod(accumulator_, step);
    }
    steps_this_frame_ = steps;
    return steps;
}

void FrameClock::end_step()
{
    ++step_index_;
    if (config_.time_scale > 0.0) elapsed_ += config_.fixed_step_seconds;
    if (steps_this_frame_ > 0) --steps_this_frame_;
}

void FrameClock::end_frame() { steps_this_frame_ = 0; }

void FrameClock::set_time_scale(double scale)
{
    config_.time_scale = std::clamp(scale, 0.0, 8.0);
}

void FrameClock::set_paused(bool paused) { paused_ = paused; }

void FrameClock::set_config(Config config)
{
    config_ = config;
    if (config_.fixed_step_seconds <= 0.0) config_.fixed_step_seconds = 1.0 / 60.0;
    config_.max_steps_per_frame = std::clamp<std::uint32_t>(config_.max_steps_per_frame, 1, 64);
    accumulator_ = 0.0;
}

double FrameClock::alpha() const noexcept
{
    if (config_.fixed_step_seconds <= 0.0) return 0.0;
    return std::clamp(accumulator_ / config_.fixed_step_seconds, 0.0, 1.0);
}

void FrameClock::reset()
{
    accumulator_ = 0.0;
    elapsed_ = 0.0;
    frame_delta_ = 0.0;
    frame_index_ = 0;
    step_index_ = 0;
    steps_this_frame_ = 0;
    dropped_steps_ = 0;
}

// ------------------------------------------------------------------ input ---

void InputMap::bind(std::string action, InputBinding binding)
{
    if (action.empty() || binding.control.empty()) return;
    auto& list = bindings_[std::move(action)];
    for (auto& existing : list) {
        if (existing == binding) return;  // idempotent
    }
    list.push_back(std::move(binding));
}

void InputMap::bind_defaults()
{
    clear();
    // Keyboard + mouse (the layout the sample game and templates document).
    bind("move.forward", {"key", "w"});
    bind("move.forward", {"key", "up"});
    bind("move.back", {"key", "s"});
    bind("move.back", {"key", "down"});
    bind("move.left", {"key", "a"});
    bind("move.left", {"key", "left"});
    bind("move.right", {"key", "d"});
    bind("move.right", {"key", "right"});
    bind("look.x", {"mouse", "motion_x"});
    bind("look.y", {"mouse", "motion_y", -1.0f});
    bind("interact", {"key", "e"});
    bind("interact", {"pad", "south"});
    bind("attack", {"mouse", "left"});
    bind("attack", {"pad", "west"});
    bind("run", {"key", "shift"});
    bind("crouch", {"key", "ctrl"});
    bind("jump", {"key", "space"});
    bind("pause", {"key", "escape"});
    bind("pause", {"pad", "start"});
    bind("camera.recenter", {"key", "c"});
    bind("debug.overlay", {"key", "f3"});
    // Gamepad analogues of the movement keys, so both devices work at once.
    bind("move.forward", {"pad", "left_stick_y", -1.0f});
    bind("move.back", {"pad", "left_stick_y"});
    bind("move.left", {"pad", "left_stick_x", -1.0f});
    bind("move.right", {"pad", "left_stick_x"});
}

std::vector<InputBinding> InputMap::bindings_for(std::string_view action) const
{
    const auto found = bindings_.find(std::string(action));
    if (found == bindings_.end()) return {};
    return found->second;
}

std::vector<std::string> InputMap::actions() const
{
    std::vector<std::string> names;
    names.reserve(bindings_.size());
    for (const auto& [action, list] : bindings_) {
        (void)list;
        names.push_back(action);
    }
    return names;
}

float InputMap::value(std::string_view action, const std::map<std::string, float>& raw) const
{
    const auto found = bindings_.find(std::string(action));
    if (found == bindings_.end()) return 0.0f;
    float result = 0.0f;
    for (const auto& binding : found->second) {
        const auto raw_value = raw.find(binding.device + ":" + binding.control);
        if (raw_value == raw.end()) continue;
        const float scaled = raw_value->second * binding.scale;
        // Multiple bindings for one action take the strongest input, so holding
        // W and pushing the stick forward does not sum to 2.0.
        if (std::abs(scaled) > std::abs(result)) result = scaled;
    }
    return std::clamp(result, -1.0f, 1.0f);
}

bool InputMap::pressed(std::string_view action, const std::map<std::string, float>& raw) const
{
    return value(action, raw) >= 0.5f;
}

bool InputMap::just_pressed(std::string_view action, const std::map<std::string, float>& raw,
                            float threshold)
{
    const bool now = value(action, raw) >= threshold;
    const std::string key(action);
    const auto previous = previous_state_.find(key);
    const bool before = previous != previous_state_.end() && previous->second;
    previous_state_[key] = now;
    return now && !before;
}

void InputMap::end_frame()
{
    // Edges are already latched by just_pressed(); ending the frame must not
    // forget them, or a held button would report a new press every frame.
}

void InputMap::clear()
{
    bindings_.clear();
    previous_state_.clear();
}

std::pair<float, float> InputMap::axis(std::string_view negative_x, std::string_view positive_x,
                                      std::string_view negative_y, std::string_view positive_y,
                                      const std::map<std::string, float>& raw) const
{
    // Each direction contributes only its positive side, so pressing both keys
    // cancels out (as players expect) instead of doubling the value.
    const float x = std::max(0.0f, value(positive_x, raw)) - std::max(0.0f, value(negative_x, raw));
    const float y = std::max(0.0f, value(positive_y, raw)) - std::max(0.0f, value(negative_y, raw));
    return {std::clamp(x, -1.0f, 1.0f), std::clamp(y, -1.0f, 1.0f)};
}

}  // namespace bully::runtime
