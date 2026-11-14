// Bully Studio - .baudio : audio asset metadata + optional PCM payload.
//
// Two modes:
//   * Reference mode (default): the document carries metadata (loop points,
//     loudness, spatial settings) and points at a source file kept outside the
//     repository. Used while iterating on dialogue/music.
//   * Embedded mode: the pipeline has baked decoded PCM into the chunk stream
//     for a self-contained distribution package.
//
// v1.0 chunks:
//   INFO  identity, sample rate, channels, duration, loudness, loop points
//   WAVE  optional PCM payload (16-bit little-endian interleaved) + mip/chunk
//   SPAT  optional spatial audio configuration
//   EVNT  optional audio event triggers (footsteps, ambience swaps)
//   META  JSON metadata (source app, export profile, notes)
//
// Diagnostic codes: BULLY-AUD-0001..0029
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/formats/binary.h"

namespace bully::formats {

inline constexpr FormatDescriptor kAudioFormat{
    fourcc("BAUD"), "baudio", {1, 0, 0}, ".baudio",
    "Audio asset: metadata, optional PCM, loop points, spatial + event config"};

enum class AudioCategory : std::uint8_t {
    Voice = 0,
    Music = 1,
    SoundEffect = 2,
    Ambience = 3,
    UiSound = 4,
    Other = 255,
};

struct LoopSettings {
    bool enabled = false;
    std::int64_t loop_start_samples = 0;
    std::int64_t loop_end_samples = 0;
};

struct SpatialSettings {
    bool enabled = false;
    float min_distance = 1.0f;
    float max_distance = 50.0f;
    float attenuation = 1.0f;
    float panning = 0.0f;
    bool doppler = true;
};

struct AudioClip {
    std::int64_t start_sample = 0;
    std::int64_t end_sample = 0;
    std::string name;
};

struct AudioEvent {
    std::string name;      // "footstep.concrete"
    std::string clip;      // clip name within this asset
    float volume = 1.0f;
    float pitch_variance = 0.0f;
    /// False = the event is *described* but its audio is not included in the
    /// package, letting the game ship its own recordings.
    bool embedded = false;
};

struct AudioDocument {
    std::string asset_id;
    std::string name;
    std::string source_path;    // original WAV/OGG/FLAC path (never mutated)
    std::string source_format;  // "wav", "ogg", "flac", "mp3"
    AudioCategory category = AudioCategory::SoundEffect;
    std::uint32_t sample_rate = 44100;
    std::uint16_t channels = 1;
    std::uint16_t bits_per_sample = 16;
    float duration_seconds = 0.0f;
    float loudness_lufs = 0.0f;
    float peak = 0.0f;
    LoopSettings loop;
    SpatialSettings spatial;
    std::vector<AudioClip> clips;
    std::vector<AudioEvent> events;
    std::vector<std::int16_t> pcm;  // empty unless embedded
    bool normalize_requested = false;
    std::string metadata_json;

    [[nodiscard]] bool is_embedded() const noexcept { return !pcm.empty(); }
    [[nodiscard]] std::uint64_t frame_count() const noexcept;

    /// Real peak/RMS analysis over the embedded PCM (used by the audio studio
    /// and normalisation processor).
    struct Analysis {
        float peak = 0.0f;
        float rms = 0.0f;
        float crest_factor = 0.0f;
    };
    [[nodiscard]] Analysis analyse() const;

    /// Scales embedded PCM toward `target_peak` (linear, no clipping).
    /// Returns a diagnostic when the payload is empty.
    [[nodiscard]] core::Status normalize(float target_peak = 0.89f);

    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    [[nodiscard]] core::Result<AudioDocument> deserialize(const std::vector<std::uint8_t>& bytes) const;
    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] std::string to_json() const;
};

[[nodiscard]] std::string_view to_string(AudioCategory category) noexcept;

}  // namespace bully::formats
