#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

#include <filesystem>
#include <map>

#include "bully/runtime/clock.h"
#include "test_support.h"

using namespace bully;
using Catch::Approx;

TEST_CASE("Fixed-step clock yields whole steps only", "[runtime][clock]")
{
    runtime::FrameClock clock;  // 60 Hz
    double total_delta = 0.0;
    std::uint32_t total_steps = 0;
    // Feed 144 irregular frames of ~1/72 s and account for every step produced.
    for (int i = 0; i < 144; ++i) {
        const double delta = (i % 3 == 0) ? 0.009 : 0.0145;
        total_delta += delta;
        auto steps = clock.begin_frame(delta);
        for (std::uint32_t s = 0; s < steps; ++s) clock.end_step();
        clock.end_frame();
        total_steps += steps;
        CHECK(clock.alpha() >= 0.0);
        CHECK(clock.alpha() < 1.0);
    }
    // Every step is worth exactly one fixed timestep, and the accumulator holds
    // only the remainder - no time is created or lost beyond one step.
    const double simulated = static_cast<double>(total_steps) * clock.fixed_step();
    CHECK(simulated <= total_delta);
    CHECK(total_delta - simulated < clock.fixed_step());
    CHECK(clock.frame_index() == 144);
}

TEST_CASE("Clock catches up but never spirals", "[runtime][clock]")
{
    runtime::FrameClock::Config config;
    config.fixed_step_seconds = 1.0 / 60.0;
    config.max_steps_per_frame = 4;
    runtime::FrameClock clock(config);

    // A two second stall would be 120 steps; the budget caps it at 4 and the
    // excess is recorded instead of queued.
    const auto steps = clock.begin_frame(2.0);
    CHECK(steps == 4);
    CHECK(clock.dropped_steps() > 0);
}

TEST_CASE("Paused clock advances no steps and keeps its remainder", "[runtime][clock]")
{
    runtime::FrameClock clock;
    clock.set_paused(true);
    CHECK(clock.begin_frame(0.5) == 0);
    CHECK(clock.elapsed_seconds() == Approx(0.0));
    clock.set_paused(false);
    CHECK(clock.begin_frame(0.5) > 0);
}

TEST_CASE("Time scale slows and stops simulation", "[runtime][clock]")
{
    runtime::FrameClock clock;
    clock.set_time_scale(0.5);
    const auto steps = clock.begin_frame(1.0 / 60.0);
    CHECK(steps == 0);  // half a step does not complete a step

    clock.set_time_scale(0.0);
    clock.reset();
    CHECK(clock.begin_frame(1.0) == 0);
}

TEST_CASE("Clock reset clears indices and accumulated time", "[runtime][clock]")
{
    runtime::FrameClock clock;
    clock.begin_frame(0.2);
    clock.end_step();
    clock.reset();
    CHECK(clock.frame_index() == 0);
    CHECK(clock.step_index() == 0);
    CHECK(clock.elapsed_seconds() == Approx(0.0));
    CHECK(clock.alpha() == Approx(0.0));
}

TEST_CASE("Input map resolves defaults, strongest binding and axes", "[runtime][input]")
{
    runtime::InputMap input;
    input.bind_defaults();
    CHECK(input.bindings_for("move.forward").size() >= 2);
    CHECK(input.value("move.forward", {}) == Approx(0.0f));

    std::map<std::string, float> raw;
    raw["key:w"] = 1.0f;
    CHECK(input.pressed("move.forward", raw));
    CHECK(input.value("move.back", raw) == Approx(0.0f));

    // Two bindings for one action take the strongest input, not the sum.
    raw["pad:left_stick_y"] = -0.6f;
    CHECK(input.value("move.forward", raw) == Approx(1.0f));

    // Both directions pressed cancel out.
    raw["key:s"] = 1.0f;
    const auto [x, y] = input.axis("move.left", "move.right", "move.back", "move.forward", raw);
    CHECK(x == Approx(0.0f));
    CHECK(y == Approx(0.0f));

    raw.erase("key:s");
    const auto [x2, y2] = input.axis("move.left", "move.right", "move.back", "move.forward", raw);
    CHECK(x2 == Approx(0.0f));
    CHECK(y2 == Approx(1.0f));
}

TEST_CASE("Input map reports edges once per press", "[runtime][input]")
{
    runtime::InputMap input;
    input.bind("interact", {"key", "e"});
    std::map<std::string, float> raw;
    CHECK_FALSE(input.just_pressed("interact", raw));
    raw["key:e"] = 1.0f;
    CHECK(input.just_pressed("interact", raw));
    input.end_frame();
    CHECK_FALSE(input.just_pressed("interact", raw));
    input.end_frame();
    CHECK_FALSE(input.just_pressed("interact", raw));
    raw["key:e"] = 0.0f;
    CHECK_FALSE(input.just_pressed("interact", raw));
    input.end_frame();
    raw["key:e"] = 1.0f;
    CHECK(input.just_pressed("interact", raw));
}

TEST_CASE("Rebinding replaces action mappings", "[runtime][input]")
{
    runtime::InputMap input;
    input.bind_defaults();
    input.clear();
    CHECK(input.actions().empty());
    input.bind("camera.left", {"mouse", "motion_x"});
    std::map<std::string, float> raw;
    raw["mouse:motion_x"] = 0.4f;
    CHECK(input.value("camera.left", raw) == Approx(0.4f));
    CHECK(input.bindings_for("move.forward").empty());
}
