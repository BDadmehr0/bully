// Bully Studio - .banim : compiled skeletal animation.
//
// v1.0 chunks:
//   INFO  identity, duration, loop mode, root-motion settings, event markers
//   BONE  static bone table (names + rest pose) so the clip can be inspected
//         without the owning model
//   TRKS  per-bone translation/rotation/scale keyframe tracks
//   FACE  facial animation cushion tracks (blend-shape weights)
//   META  JSON metadata (importer settings, staff annotations)
//
// Time is stored in seconds as float. Rotation is a quaternion (x,y,z,w).
//
// Diagnostic codes: BULLY-ANIM-0001..0019
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bully/core/error.h"
#include "bully/formats/binary.h"

namespace bully::formats {

inline constexpr FormatDescriptor kAnimationFormat{
    fourcc("BANI"), "banim", {1, 0, 0}, ".banim",
    "Skeletal animation clip: bone tracks, events, facial tracks, root motion"};

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    [[nodiscard]] static Quat identity() { return Quat{}; }
};

enum class InterpolationMode : std::uint8_t { Linear = 0, Step = 1, CubicSpline = 2 };

enum class LoopMode : std::uint8_t { Once = 0, Loop = 1, PingPong = 2 };

/// Namespaced animation event. The runtime maps these to gameplay callbacks,
/// the studio shows them on the timeline. Types are strings so plugins can add
/// new event kinds without touching this header.
struct AnimationEvent {
    std::string name;      // e.g. "footstep", "sfx.grab", "dialog.cue"
    std::string payload;   // free-form JSON (sound id, marker value, ...)
    float time = 0.0f;
};

struct VectorKey {
    float time = 0.0f;
    Vec3f value{};
};

struct QuaternionKey {
    float time = 0.0f;
    Quat value{};
};

struct BoneTrack {
    std::string bone_name;
    InterpolationMode interpolation = InterpolationMode::Linear;
    std::vector<VectorKey> translations;
    std::vector<QuaternionKey> rotations;
    std::vector<VectorKey> scales;
};

/// Facial track: drives a named blend shape / face bone.
struct FacialTrack {
    std::string target;  // blend shape name or "face.jaw" style bone
    InterpolationMode interpolation = InterpolationMode::Linear;
    /// Keys are (time, weight) pairs stored as VectorKey.x for uniformity.
    std::vector<VectorKey> weights;
};

struct RootMotionSettings {
    bool enabled = false;
    std::string bone = "root";
    bool strips_translation = true;
    bool strips_rotation = false;
};

struct AnimationBone {
    std::string name;
    std::int32_t parent = -1;
    Vec3f rest_translation{};
    Quat rest_rotation{};
    Vec3f rest_scale{1.0f, 1.0f, 1.0f};
};

struct AnimationDocument {
    std::string asset_id;
    std::string name;
    std::string source_path;
    std::string importer;
    std::string clip_name;  // original clip name inside the source file
    float duration = 0.0f;
    float sample_rate = 30.0f;
    LoopMode loop = LoopMode::Once;
    RootMotionSettings root_motion;
    std::vector<AnimationBone> bones;
    std::vector<BoneTrack> tracks;
    std::vector<FacialTrack> facial_tracks;
    std::vector<AnimationEvent> events;
    std::string metadata_json;

    [[nodiscard]] std::uint32_t keyframe_count() const;

    /// Samples a bone track at `time` (seconds), clamped to the track range.
    /// Real interpolation used by Studio scrubbing and the runtime.
    [[nodiscard]] Vec3f sample_translation(const BoneTrack& track, float time) const;
    [[nodiscard]] Quat sample_rotation(const BoneTrack& track, float time) const;
    [[nodiscard]] Vec3f sample_scale(const BoneTrack& track, float time) const;

    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    [[nodiscard]] core::Result<AnimationDocument> deserialize(
        const std::vector<std::uint8_t>& bytes) const;
    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] std::string to_json() const;
};

}  // namespace bully::formats
