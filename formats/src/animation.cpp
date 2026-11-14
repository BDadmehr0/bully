#include "bully/formats/animation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "bully/core/json.h"

namespace bully::formats {

namespace {

constexpr std::uint32_t kChunkInfo = fourcc("INFO");
constexpr std::uint32_t kChunkBone = fourcc("BONE");
constexpr std::uint32_t kChunkTrks = fourcc("TRKS");
constexpr std::uint32_t kChunkFace = fourcc("FACE");
constexpr std::uint32_t kChunkMeta = fourcc("META");

void write_quat(BinaryWriter& w, const Quat& q)
{
    w.f32(q.x);
    w.f32(q.y);
    w.f32(q.z);
    w.f32(q.w);
}

Quat read_quat(BinaryReader& r)
{
    Quat q;
    q.x = r.f32();
    q.y = r.f32();
    q.z = r.f32();
    q.w = r.f32();
    return q;
}

Vec3f lerp(const Vec3f& a, const Vec3f& b, float t)
{
    return Vec3f{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

Quat slerp(const Quat& a, const Quat& b, float t)
{
    float cos_theta = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Quat to = b;
    if (cos_theta < 0.0f) {
        to = Quat{-b.x, -b.y, -b.z, -b.w};
        cos_theta = -cos_theta;
    }
    if (cos_theta > 0.9995f) {
        Quat out{a.x + (to.x - a.x) * t, a.y + (to.y - a.y) * t, a.z + (to.z - a.z) * t,
                 a.w + (to.w - a.w) * t};
        const float length = std::sqrt(out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w);
        if (length > 1e-6f) {
            out.x /= length;
            out.y /= length;
            out.z /= length;
            out.w /= length;
        }
        return out;
    }
    const float theta = std::acos(std::clamp(cos_theta, -1.0f, 1.0f));
    const float sin_theta = std::sin(theta);
    if (std::fabs(sin_theta) < 1e-6f) return a;
    const float scale_a = std::sin((1.0f - t) * theta) / sin_theta;
    const float scale_b = std::sin(t * theta) / sin_theta;
    return Quat{a.x * scale_a + to.x * scale_b, a.y * scale_a + to.y * scale_b,
                a.z * scale_a + to.z * scale_b, a.w * scale_a + to.w * scale_b};
}

}  // namespace

std::uint32_t AnimationDocument::keyframe_count() const
{
    std::uint32_t total = 0;
    for (const auto& track : tracks) {
        total += static_cast<std::uint32_t>(track.translations.size() + track.rotations.size() +
                                            track.scales.size());
    }
    for (const auto& track : facial_tracks) {
        total += static_cast<std::uint32_t>(track.weights.size());
    }
    return total;
}

Vec3f AnimationDocument::sample_translation(const BoneTrack& track, float time) const
{
    if (track.translations.empty()) return Vec3f{};
    if (time <= track.translations.front().time) return track.translations.front().value;
    if (time >= track.translations.back().time) return track.translations.back().value;
    for (std::size_t i = 1; i < track.translations.size(); ++i) {
        const auto& next = track.translations[i];
        if (time <= next.time) {
            const auto& previous = track.translations[i - 1];
            if (track.interpolation == InterpolationMode::Step || next.time <= previous.time) {
                return previous.value;
            }
            const float t = (time - previous.time) / (next.time - previous.time);
            return lerp(previous.value, next.value, t);
        }
    }
    return track.translations.back().value;
}

Quat AnimationDocument::sample_rotation(const BoneTrack& track, float time) const
{
    if (track.rotations.empty()) return Quat::identity();
    if (time <= track.rotations.front().time) return track.rotations.front().value;
    if (time >= track.rotations.back().time) return track.rotations.back().value;
    for (std::size_t i = 1; i < track.rotations.size(); ++i) {
        const auto& next = track.rotations[i];
        if (time <= next.time) {
            const auto& previous = track.rotations[i - 1];
            if (track.interpolation == InterpolationMode::Step || next.time <= previous.time) {
                return previous.value;
            }
            const float t = (time - previous.time) / (next.time - previous.time);
            return slerp(previous.value, next.value, t);
        }
    }
    return track.rotations.back().value;
}

Vec3f AnimationDocument::sample_scale(const BoneTrack& track, float time) const
{
    if (track.scales.empty()) return Vec3f{1.0f, 1.0f, 1.0f};
    if (time <= track.scales.front().time) return track.scales.front().value;
    if (time >= track.scales.back().time) return track.scales.back().value;
    for (std::size_t i = 1; i < track.scales.size(); ++i) {
        const auto& next = track.scales[i];
        if (time <= next.time) {
            const auto& previous = track.scales[i - 1];
            if (track.interpolation == InterpolationMode::Step || next.time <= previous.time) {
                return previous.value;
            }
            const float t = (time - previous.time) / (next.time - previous.time);
            return lerp(previous.value, next.value, t);
        }
    }
    return track.scales.back().value;
}

std::vector<std::uint8_t> AnimationDocument::serialize() const
{
    BinaryWriter w;
    w.chunk(kChunkInfo, [&](BinaryWriter& body) {
        body.string(asset_id);
        body.string(name);
        body.string(source_path);
        body.string(importer);
        body.string(clip_name);
        body.f32(duration);
        body.f32(sample_rate);
        body.u8(static_cast<std::uint8_t>(loop));
        body.boolean(root_motion.enabled);
        body.string(root_motion.bone);
        body.boolean(root_motion.strips_translation);
        body.boolean(root_motion.strips_rotation);
        body.u32(static_cast<std::uint32_t>(events.size()));
        for (const auto& event : events) {
            body.string(event.name);
            body.string(event.payload);
            body.f32(event.time);
        }
    });
    if (!bones.empty()) {
        w.chunk(kChunkBone, [&](BinaryWriter& body) {
            body.u32(static_cast<std::uint32_t>(bones.size()));
            for (const auto& bone : bones) {
                body.string(bone.name);
                body.i32(bone.parent);
                body.vec3(bone.rest_translation);
                write_quat(body, bone.rest_rotation);
                body.vec3(bone.rest_scale);
            }
        });
    }
    w.chunk(kChunkTrks, [&](BinaryWriter& body) {
        body.u32(static_cast<std::uint32_t>(tracks.size()));
        for (const auto& track : tracks) {
            body.string(track.bone_name);
            body.u8(static_cast<std::uint8_t>(track.interpolation));
            body.u32(static_cast<std::uint32_t>(track.translations.size()));
            for (const auto& key : track.translations) {
                body.f32(key.time);
                body.vec3(key.value);
            }
            body.u32(static_cast<std::uint32_t>(track.rotations.size()));
            for (const auto& key : track.rotations) {
                body.f32(key.time);
                write_quat(body, key.value);
            }
            body.u32(static_cast<std::uint32_t>(track.scales.size()));
            for (const auto& key : track.scales) {
                body.f32(key.time);
                body.vec3(key.value);
            }
        }
    });
    if (!facial_tracks.empty()) {
        w.chunk(kChunkFace, [&](BinaryWriter& body) {
            body.u32(static_cast<std::uint32_t>(facial_tracks.size()));
            for (const auto& track : facial_tracks) {
                body.string(track.target);
                body.u8(static_cast<std::uint8_t>(track.interpolation));
                body.u32(static_cast<std::uint32_t>(track.weights.size()));
                for (const auto& key : track.weights) {
                    body.f32(key.time);
                    body.f32(key.value.x);
                }
            }
        });
    }
    w.chunk(kChunkMeta, [&](BinaryWriter& body) { body.string(metadata_json); });
    return encode_document(kAnimationFormat, 0, w.bytes());
}

core::Result<AnimationDocument> AnimationDocument::deserialize(
    const std::vector<std::uint8_t>& bytes) const
{
    core::DiagnosticList diagnostics;
    auto payload = decode_document(kAnimationFormat, bytes);
    if (!payload) return core::Result<AnimationDocument>(payload.diagnostics());
    diagnostics.append(payload.diagnostics());

    AnimationDocument document;
    BinaryReader reader(payload.value().data(), payload.value().size(), &diagnostics);
    constexpr std::uint32_t kMaxKeys = 1u << 22;
    while (reader.ok()) {
        std::uint32_t chunk_id = 0;
        std::uint32_t chunk_size = 0;
        if (!reader.next_chunk(chunk_id, chunk_size)) break;
        const std::size_t chunk_end = reader.position() + chunk_size;
        if (chunk_id == kChunkInfo) {
            document.asset_id = reader.string();
            document.name = reader.string();
            document.source_path = reader.string();
            document.importer = reader.string();
            document.clip_name = reader.string();
            document.duration = reader.f32();
            document.sample_rate = reader.f32();
            document.loop = static_cast<LoopMode>(reader.u8());
            document.root_motion.enabled = reader.boolean();
            document.root_motion.bone = reader.string();
            document.root_motion.strips_translation = reader.boolean();
            document.root_motion.strips_rotation = reader.boolean();
            const std::uint32_t event_count = reader.u32();
            document.events.resize(std::min<std::uint32_t>(event_count, 8192));
            for (auto& event : document.events) {
                event.name = reader.string();
                event.payload = reader.string();
                event.time = reader.f32();
            }
        } else if (chunk_id == kChunkBone) {
            const std::uint32_t count = reader.u32();
            document.bones.resize(std::min<std::uint32_t>(count, 4096));
            for (auto& bone : document.bones) {
                bone.name = reader.string();
                bone.parent = reader.i32();
                bone.rest_translation = reader.vec3();
                bone.rest_rotation = read_quat(reader);
                bone.rest_scale = reader.vec3();
            }
        } else if (chunk_id == kChunkTrks) {
            const std::uint32_t count = reader.u32();
            if (count > 4096) {
                diagnostics.error("BULLY-ANIM-0001", "animation has an implausible track count");
                reader.fail("BULLY-ANIM-0001", "implausible track count");
                break;
            }
            document.tracks.resize(count);
            for (auto& track : document.tracks) {
                track.bone_name = reader.string();
                track.interpolation = static_cast<InterpolationMode>(reader.u8());
                const std::uint32_t translation_count = std::min(reader.u32(), kMaxKeys);
                track.translations.resize(translation_count);
                for (auto& key : track.translations) {
                    key.time = reader.f32();
                    key.value = reader.vec3();
                }
                const std::uint32_t rotation_count = std::min(reader.u32(), kMaxKeys);
                track.rotations.resize(rotation_count);
                for (auto& key : track.rotations) {
                    key.time = reader.f32();
                    key.value = read_quat(reader);
                }
                const std::uint32_t scale_count = std::min(reader.u32(), kMaxKeys);
                track.scales.resize(scale_count);
                for (auto& key : track.scales) {
                    key.time = reader.f32();
                    key.value = reader.vec3();
                }
            }
        } else if (chunk_id == kChunkFace) {
            const std::uint32_t count = reader.u32();
            document.facial_tracks.resize(std::min<std::uint32_t>(count, 512));
            for (auto& track : document.facial_tracks) {
                track.target = reader.string();
                track.interpolation = static_cast<InterpolationMode>(reader.u8());
                const std::uint32_t weight_count = std::min(reader.u32(), kMaxKeys);
                track.weights.resize(weight_count);
                for (auto& key : track.weights) {
                    key.time = reader.f32();
                    key.value.x = reader.f32();
                }
            }
        } else if (chunk_id == kChunkMeta) {
            document.metadata_json = reader.string();
        } else {
            diagnostics.info("BULLY-ANIM-0002",
                             "skipping unknown animation chunk '" + fourcc_to_string(chunk_id) + "'");
        }
        if (reader.ok()) reader.seek(chunk_end);
    }
    if (!reader.ok() || diagnostics.has_errors()) {
        if (diagnostics.empty()) diagnostics.error("BULLY-ANIM-0003", "failed to decode animation");
        return core::Result<AnimationDocument>(std::move(diagnostics));
    }
    auto decoded = core::Result<AnimationDocument>(std::move(document));
    // Warnings collected while decoding (unknown chunks, newer minor versions)
    // travel with the successful result so callers can surface them.
    decoded.attach_diagnostics(std::move(diagnostics));
    return decoded;
}

core::Status AnimationDocument::validate() const
{
    core::DiagnosticList diagnostics;
    if (asset_id.empty()) diagnostics.error("BULLY-ANIM-0004", "animation has no asset id");
    if (duration <= 0.0f) {
        diagnostics.warn("BULLY-ANIM-0005", "animation '" + name + "' has a non-positive duration", name);
    }
    if (duration < 0.0f) {
        diagnostics.error("BULLY-ANIM-0011", "animation '" + name + "' has a negative duration", name);
    }
    std::set<std::string> bone_names;
    for (const auto& bone : bones) bone_names.insert(bone.name);
    if (tracks.empty()) {
        diagnostics.warn("BULLY-ANIM-0006", "animation '" + name + "' has no bone tracks", name);
    }
    for (const auto& track : tracks) {
        auto check_times = [&](const auto& keys, const char* what) {
            float previous = -1.0f;
            for (const auto& key : keys) {
                if (key.time < previous) {
                    diagnostics.warn("BULLY-ANIM-0007",
                                     "track '" + track.bone_name + "' has non-monotonic " + what +
                                         " keyframe times",
                                     name);
                    break;
                }
                previous = key.time;
            }
        };
        check_times(track.translations, "translation");
        check_times(track.rotations, "rotation");
        check_times(track.scales, "scale");
        if (track.bone_name.empty()) {
            diagnostics.warn("BULLY-ANIM-0008", "animation contains an unnamed bone track", name);
        } else if (!bone_names.empty() &&
                   bone_names.find(track.bone_name) == bone_names.end()) {
            // The clip declares its bone table, so a track targeting an absent
            // bone would animate nothing at runtime - report it as an error.
            diagnostics.error("BULLY-ANIM-0012",
                              "track targets bone '" + track.bone_name +
                                  "' which is not in the clip's bone table",
                              name);
        }
    }
    for (const auto& event : events) {
        if (event.time < 0.0f || (duration > 0.0f && event.time > duration + 0.001f)) {
            diagnostics.warn("BULLY-ANIM-0009",
                             "event '" + event.name + "' falls outside the clip duration", name);
        }
    }
    if (root_motion.enabled && root_motion.bone.empty()) {
        diagnostics.error("BULLY-ANIM-0010", "root motion is enabled without a root bone", name);
    }
    if (diagnostics.has_errors()) return core::Status(std::move(diagnostics));
    // Warnings are part of the report even when the document is valid.
    auto status = core::Status{};
    status.attach_diagnostics(std::move(diagnostics));
    return status;
}

std::string AnimationDocument::to_json() const
{
    using core::json::Value;
    Value root = Value::make_object();
    root["format"] = "banim";
    root["version"] = kAnimationFormat.version.to_string();
    root["asset_id"] = asset_id;
    root["name"] = name;
    root["clip"] = clip_name;
    root["duration"] = static_cast<double>(duration);
    root["sample_rate"] = static_cast<double>(sample_rate);
    root["loop"] = loop == LoopMode::Loop ? "loop" : (loop == LoopMode::PingPong ? "pingpong" : "once");
    root["root_motion"] = root_motion.enabled;
    root["bones"] = static_cast<std::uint64_t>(bones.size());
    root["tracks"] = static_cast<std::uint64_t>(tracks.size());
    root["keyframes"] = static_cast<std::uint64_t>(keyframe_count());
    root["facial_tracks"] = static_cast<std::uint64_t>(facial_tracks.size());
    Value events_json = Value::make_array();
    for (const auto& event : events) {
        Value entry = Value::make_object();
        entry["name"] = event.name;
        entry["time"] = static_cast<double>(event.time);
        entry["payload"] = event.payload;
        events_json.push(std::move(entry));
    }
    root["events"] = std::move(events_json);
    root["valid"] = validate().has_value();
    return root.dump(2);
}

}  // namespace bully::formats
