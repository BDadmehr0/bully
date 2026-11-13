#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "bully/formats/animation.h"

using namespace bully::formats;

namespace {

AnimationDocument make_walk_clip()
{
    AnimationDocument document;
    document.asset_id = "anim-1";
    document.name = "walk";
    document.source_path = "assets/anims/walk.fbx";
    document.importer = "gltf";
    document.clip_name = "Walk_Loop";
    document.duration = 1.0f;
    document.loop = LoopMode::Loop;
    document.sample_rate = 30.0f;

    AnimationBone root;
    root.name = "root";
    AnimationBone hip;
    hip.name = "hip";
    hip.parent = 0;
    document.bones = {root, hip};

    BoneTrack track;
    track.bone_name = "hip";
    track.interpolation = InterpolationMode::Linear;
    track.translations = {{0.0f, Vec3f{0.0f, 0.0f, 0.0f}}, {1.0f, Vec3f{0.0f, 1.0f, 0.0f}}};
    track.rotations = {{0.0f, Quat::identity()}, {1.0f, Quat{0.0f, 0.0f, 0.70710678f, 0.70710678f}}};
    track.scales = {{0.0f, Vec3f{1.0f, 1.0f, 1.0f}}};
    document.tracks.push_back(track);

    FacialTrack facial;
    facial.target = "face.smile";
    facial.weights = {{0.0f, Vec3f{0.0f, 0.0f, 0.0f}}, {0.5f, Vec3f{1.0f, 0.0f, 0.0f}}};
    document.facial_tracks.push_back(facial);

    document.events.push_back(AnimationEvent{"footstep", "{\"surface\":\"concrete\"}", 0.25f});
    document.root_motion.enabled = true;
    document.root_motion.bone = "root";
    return document;
}

}  // namespace

TEST_CASE("banim round-trips tracks, facial data and events", "[formats][animation]")
{
    const auto original = make_walk_clip();
    auto decoded = AnimationDocument{}.deserialize(original.serialize());
    REQUIRE(decoded.has_value());
    const auto& clip = decoded.value();
    CHECK(clip.name == "walk");
    CHECK(clip.loop == LoopMode::Loop);
    CHECK(clip.bones.size() == 2);
    REQUIRE(clip.tracks.size() == 1);
    CHECK(clip.tracks[0].translations.size() == 2);
    REQUIRE(clip.facial_tracks.size() == 1);
    CHECK(clip.facial_tracks[0].target == "face.smile");
    REQUIRE(clip.events.size() == 1);
    CHECK(clip.events[0].name == "footstep");
    CHECK(clip.root_motion.enabled);
    CHECK(clip.validate().has_value());
}

TEST_CASE("Sampling interpolates between keyframes", "[formats][animation]")
{
    const auto clip = make_walk_clip();
    const auto& track = clip.tracks[0];
    CHECK(clip.sample_translation(track, 0.0f).y == 0.0f);
    CHECK(clip.sample_translation(track, 0.5f).y == Catch::Approx(0.5f));
    CHECK(clip.sample_translation(track, 0.25f).y == Catch::Approx(0.25f));
    // Clamped outside the key range.
    CHECK(clip.sample_translation(track, -1.0f).y == 0.0f);
    CHECK(clip.sample_translation(track, 5.0f).y == 1.0f);
    const Quat middle = clip.sample_rotation(track, 0.5f);
    CHECK(middle.w == Catch::Approx(0.9238795f).margin(1e-5));
    CHECK(clip.sample_scale(track, 0.5f).x == 1.0f);
}

TEST_CASE("Step interpolation holds the previous key", "[formats][animation]")
{
    auto clip = make_walk_clip();
    clip.tracks[0].interpolation = InterpolationMode::Step;
    CHECK(clip.sample_translation(clip.tracks[0], 0.75f).y == 0.0f);
    CHECK(clip.sample_translation(clip.tracks[0], 1.0f).y == 1.0f);
}

TEST_CASE("banim validation catches dangling bone references", "[formats][animation]")
{
    auto clip = make_walk_clip();
    clip.tracks[0].bone_name = "does-not-exist";
    CHECK_FALSE(clip.validate().has_value());

    clip = make_walk_clip();
    clip.duration = -1.0f;
    CHECK_FALSE(clip.validate().has_value());

    // A zero duration still parses; it is a warning, not a hard error.
    clip = make_walk_clip();
    clip.duration = 0.0f;
    REQUIRE(clip.validate().has_value());
    CHECK(clip.validate().diagnostics().count(bully::core::Severity::Warning) >= 1);

    clip = make_walk_clip();
    // 2 translations + 2 rotations + 1 scale + 2 facial weights.
    CHECK(clip.keyframe_count() == 7);
}

TEST_CASE("banim JSON summary lists loop mode and events", "[formats][animation]")
{
    const auto clip = make_walk_clip();
    const std::string json = clip.to_json();
    CHECK(json.find("\"loop\": \"loop\"") != std::string::npos);
    CHECK(json.find("footstep") != std::string::npos);
}
