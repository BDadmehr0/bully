#include "bully/formats/audio.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bully/core/json.h"

namespace bully::formats {

namespace {

constexpr std::uint32_t kChunkInfo = fourcc("INFO");
constexpr std::uint32_t kChunkWave = fourcc("WAVE");
constexpr std::uint32_t kChunkSpat = fourcc("SPAT");
constexpr std::uint32_t kChunkEvnt = fourcc("EVNT");
constexpr std::uint32_t kChunkMeta = fourcc("META");

}  // namespace

std::string_view to_string(AudioCategory category) noexcept
{
    switch (category) {
        case AudioCategory::Voice: return "voice";
        case AudioCategory::Music: return "music";
        case AudioCategory::SoundEffect: return "sfx";
        case AudioCategory::Ambience: return "ambience";
        case AudioCategory::UiSound: return "ui";
        case AudioCategory::Other: return "other";
    }
    return "other";
}

std::uint64_t AudioDocument::frame_count() const noexcept
{
    if (channels == 0) return 0;
    return static_cast<std::uint64_t>(pcm.size()) / channels;
}

AudioDocument::Analysis AudioDocument::analyse() const
{
    Analysis analysis;
    if (pcm.empty()) return analysis;
    float peak_amplitude = 0.0f;
    double sum_squares = 0.0;
    for (std::int16_t sample : pcm) {
        const float value = static_cast<float>(sample) / 32768.0f;
        peak_amplitude = std::max(peak_amplitude, std::fabs(value));
        sum_squares += static_cast<double>(value) * static_cast<double>(value);
    }
    analysis.peak = peak_amplitude;
    analysis.rms = static_cast<float>(std::sqrt(sum_squares / static_cast<double>(pcm.size())));
    if (analysis.rms > 0.0f) analysis.crest_factor = peak_amplitude / analysis.rms;
    return analysis;
}

core::Status AudioDocument::normalize(float target_peak)
{
    core::DiagnosticList diagnostics;
    if (pcm.empty()) {
        diagnostics.error("BULLY-AUD-0001", "cannot normalise an audio asset without embedded PCM", name);
        return core::Status(std::move(diagnostics));
    }
    const auto analysis = analyse();
    if (analysis.peak <= 0.0f) {
        diagnostics.warn("BULLY-AUD-0002", "audio asset is silent; nothing to normalise", name);
        return core::Status{};
    }
    const float gain = target_peak / analysis.peak;
    for (auto& sample : pcm) {
        const float scaled = static_cast<float>(sample) * gain;
        const float clamped = std::clamp(scaled, -32768.0f, 32767.0f);
        sample = static_cast<std::int16_t>(std::lround(clamped));
    }
    const auto updated = analyse();
    peak = updated.peak;
    loudness_lufs = 20.0f * std::log10(std::max(updated.rms, 1e-6f));
    return core::Status{};
}

std::vector<std::uint8_t> AudioDocument::serialize() const
{
    BinaryWriter w;
    w.chunk(kChunkInfo, [&](BinaryWriter& body) {
        body.string(asset_id);
        body.string(name);
        body.string(source_path);
        body.string(source_format);
        body.u8(static_cast<std::uint8_t>(category));
        body.u32(sample_rate);
        body.u16(channels);
        body.u16(bits_per_sample);
        body.f32(duration_seconds);
        body.f32(loudness_lufs);
        body.f32(peak);
        body.boolean(loop.enabled);
        body.i64(loop.loop_start_samples);
        body.i64(loop.loop_end_samples);
        body.boolean(normalize_requested);
        body.u32(static_cast<std::uint32_t>(clips.size()));
        for (const auto& clip : clips) {
            body.string(clip.name);
            body.i64(clip.start_sample);
            body.i64(clip.end_sample);
        }
    });
    if (!pcm.empty()) {
        w.chunk(kChunkWave, [&](BinaryWriter& body) {
            body.u32(static_cast<std::uint32_t>(pcm.size()));
            body.u16(channels);
            body.u32(sample_rate);
            for (std::int16_t sample : pcm) body.u16(static_cast<std::uint16_t>(sample));
        });
    }
    if (spatial.enabled) {
        w.chunk(kChunkSpat, [&](BinaryWriter& body) {
            body.boolean(spatial.enabled);
            body.f32(spatial.min_distance);
            body.f32(spatial.max_distance);
            body.f32(spatial.attenuation);
            body.f32(spatial.panning);
            body.boolean(spatial.doppler);
        });
    }
    if (!events.empty()) {
        w.chunk(kChunkEvnt, [&](BinaryWriter& body) {
            body.u32(static_cast<std::uint32_t>(events.size()));
            for (const auto& event : events) {
                body.string(event.name);
                body.string(event.clip);
                body.f32(event.volume);
                body.f32(event.pitch_variance);
                body.boolean(event.embedded);
            }
        });
    }
    w.chunk(kChunkMeta, [&](BinaryWriter& body) { body.string(metadata_json); });
    return encode_document(kAudioFormat, 0, w.bytes());
}

core::Result<AudioDocument> AudioDocument::deserialize(const std::vector<std::uint8_t>& bytes) const
{
    core::DiagnosticList diagnostics;
    auto payload = decode_document(kAudioFormat, bytes);
    if (!payload) return core::Result<AudioDocument>(payload.diagnostics());
    diagnostics.append(payload.diagnostics());

    AudioDocument document;
    BinaryReader reader(payload.value().data(), payload.value().size(), &diagnostics);
    while (reader.ok()) {
        std::uint32_t chunk_id = 0;
        std::uint32_t chunk_size = 0;
        if (!reader.next_chunk(chunk_id, chunk_size)) break;
        const std::size_t chunk_end = reader.position() + chunk_size;
        if (chunk_id == kChunkInfo) {
            document.asset_id = reader.string();
            document.name = reader.string();
            document.source_path = reader.string();
            document.source_format = reader.string();
            document.category = static_cast<AudioCategory>(reader.u8());
            document.sample_rate = reader.u32();
            document.channels = reader.u16();
            document.bits_per_sample = reader.u16();
            document.duration_seconds = reader.f32();
            document.loudness_lufs = reader.f32();
            document.peak = reader.f32();
            document.loop.enabled = reader.boolean();
            document.loop.loop_start_samples = reader.i64();
            document.loop.loop_end_samples = reader.i64();
            document.normalize_requested = reader.boolean();
            const std::uint32_t clip_count = std::min<std::uint32_t>(reader.u32(), 4096);
            document.clips.resize(clip_count);
            for (auto& clip : document.clips) {
                clip.name = reader.string();
                clip.start_sample = reader.i64();
                clip.end_sample = reader.i64();
            }
        } else if (chunk_id == kChunkWave) {
            const std::uint32_t sample_count = std::min<std::uint32_t>(reader.u32(), 1u << 28);
            (void)reader.u16();
            (void)reader.u32();
            document.pcm.resize(sample_count);
            for (auto& sample : document.pcm) {
                sample = static_cast<std::int16_t>(reader.u16());
            }
        } else if (chunk_id == kChunkSpat) {
            document.spatial.enabled = reader.boolean();
            document.spatial.min_distance = reader.f32();
            document.spatial.max_distance = reader.f32();
            document.spatial.attenuation = reader.f32();
            document.spatial.panning = reader.f32();
            document.spatial.doppler = reader.boolean();
        } else if (chunk_id == kChunkEvnt) {
            const std::uint32_t count = std::min<std::uint32_t>(reader.u32(), 4096);
            document.events.resize(count);
            for (auto& event : document.events) {
                event.name = reader.string();
                event.clip = reader.string();
                event.volume = reader.f32();
                event.pitch_variance = reader.f32();
                event.embedded = reader.boolean();
            }
        } else if (chunk_id == kChunkMeta) {
            document.metadata_json = reader.string();
        } else {
            diagnostics.info("BULLY-AUD-0003",
                             "skipping unknown audio chunk '" + fourcc_to_string(chunk_id) + "'");
        }
        if (reader.ok()) reader.seek(chunk_end);
    }
    if (!reader.ok() || diagnostics.has_errors()) {
        if (diagnostics.empty()) diagnostics.error("BULLY-AUD-0004", "failed to decode audio asset");
        return core::Result<AudioDocument>(std::move(diagnostics));
    }
    auto decoded = core::Result<AudioDocument>(std::move(document));
    // Warnings collected while decoding (unknown chunks, newer minor versions)
    // travel with the successful result so callers can surface them.
    decoded.attach_diagnostics(std::move(diagnostics));
    return decoded;
}

core::Status AudioDocument::validate() const
{
    core::DiagnosticList diagnostics;
    if (asset_id.empty()) diagnostics.error("BULLY-AUD-0005", "audio asset has no asset id");
    if (sample_rate == 0) diagnostics.error("BULLY-AUD-0006", "audio sample rate must be non-zero", name);
    if (channels == 0) diagnostics.error("BULLY-AUD-0007", "audio channel count must be non-zero", name);
    if (!is_embedded() && source_path.empty()) {
        diagnostics.error("BULLY-AUD-0008",
                          "audio asset is neither embedded nor references a source file", name);
    }
    if (loop.enabled) {
        if (loop.loop_end_samples <= loop.loop_start_samples) {
            diagnostics.error("BULLY-AUD-0009", "loop end must be after loop start", name);
        }
        if (is_embedded() && loop.loop_end_samples > static_cast<std::int64_t>(frame_count())) {
            diagnostics.error("BULLY-AUD-0010", "loop end exceeds the audio length", name);
        }
    }
    for (const auto& clip : clips) {
        if (clip.end_sample <= clip.start_sample) {
            diagnostics.warn("BULLY-AUD-0011", "clip '" + clip.name + "' has non-positive length", name);
        }
        if (is_embedded() && clip.end_sample > static_cast<std::int64_t>(frame_count())) {
            diagnostics.warn("BULLY-AUD-0012", "clip '" + clip.name + "' exceeds the audio length", name);
        }
    }
    for (const auto& event : events) {
        if (event.name.empty()) {
            diagnostics.error("BULLY-AUD-0013", "audio event has no name", name);
        }
        if (!event.clip.empty()) {
            const bool found = std::any_of(clips.begin(), clips.end(), [&](const AudioClip& clip) {
                return clip.name == event.clip;
            });
            if (!found) {
                diagnostics.warn("BULLY-AUD-0014",
                                 "audio event '" + event.name + "' references missing clip '" +
                                     event.clip + "'",
                                 name);
            }
        }
    }
    if (is_embedded()) {
        const std::uint64_t expected_frames = static_cast<std::uint64_t>(
            std::max(0.0f, duration_seconds) * static_cast<float>(sample_rate));
        const std::uint64_t actual_frames = frame_count();
        // 5% tolerance: encoders routinely pad.
        if (expected_frames > 0 && actual_frames > 0) {
            const auto diff = static_cast<double>(actual_frames) - static_cast<double>(expected_frames);
            if (std::fabs(diff) > static_cast<double>(expected_frames) * 0.05) {
                diagnostics.warn("BULLY-AUD-0015",
                                 "stated duration does not match embedded PCM length", name);
            }
        }
    }
    if (diagnostics.has_errors()) return core::Status(std::move(diagnostics));
    // Warnings are part of the report even when the document is valid.
    auto status = core::Status{};
    status.attach_diagnostics(std::move(diagnostics));
    return status;
}

std::string AudioDocument::to_json() const
{
    using core::json::Value;
    const auto analysis = analyse();
    Value root = Value::make_object();
    root["format"] = "baudio";
    root["version"] = kAudioFormat.version.to_string();
    root["asset_id"] = asset_id;
    root["name"] = name;
    root["category"] = std::string(to_string(category));
    root["source_path"] = source_path;
    root["source_format"] = source_format;
    root["sample_rate"] = static_cast<std::uint64_t>(sample_rate);
    root["channels"] = static_cast<std::uint64_t>(channels);
    root["duration_seconds"] = static_cast<double>(duration_seconds);
    root["loudness_lufs"] = static_cast<double>(loudness_lufs);
    root["embedded"] = is_embedded();
    root["frames"] = frame_count();
    root["loop_enabled"] = loop.enabled;
    root["loop_start"] = loop.loop_start_samples;
    root["loop_end"] = loop.loop_end_samples;
    Value analysis_json = Value::make_object();
    analysis_json["peak"] = static_cast<double>(analysis.peak);
    analysis_json["rms"] = static_cast<double>(analysis.rms);
    analysis_json["crest_factor"] = static_cast<double>(analysis.crest_factor);
    root["analysis"] = std::move(analysis_json);
    Value clips_json = Value::make_array();
    for (const auto& clip : clips) {
        Value entry = Value::make_object();
        entry["name"] = clip.name;
        entry["start"] = clip.start_sample;
        entry["end"] = clip.end_sample;
        clips_json.push(std::move(entry));
    }
    root["clips"] = std::move(clips_json);
    Value events_json = Value::make_array();
    for (const auto& event : events) {
        Value entry = Value::make_object();
        entry["name"] = event.name;
        entry["clip"] = event.clip;
        entry["embedded"] = event.embedded;
        events_json.push(std::move(entry));
    }
    root["events"] = std::move(events_json);
    root["valid"] = validate().has_value();
    return root.dump(2);
}

}  // namespace bully::formats
