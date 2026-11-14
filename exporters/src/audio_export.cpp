// Audio exporter: .baudio -> 16-bit PCM RIFF/WAVE.

#include <cstdint>
#include <utility>
#include <vector>
#include "bully/core/fs.h"
#include "bully/exporters/exporters.h"

namespace bully::exporters {

namespace {

void append_u16_le(std::vector<std::uint8_t>& out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void append_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void append_tag(std::vector<std::uint8_t>& out, const char (&tag)[5])
{
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(tag[i]));
}

}  // namespace

core::Result<std::vector<std::uint8_t>> encode_wav(const formats::AudioDocument& audio)
{
    core::DiagnosticList diagnostics;
    if (audio.pcm.empty()) {
        // Reference-mode assets deliberately keep no PCM; exporting them means
        // copying the original recording, not synthesising silence.
        diagnostics.error("BULLY-EXP-0010",
                          "audio asset has no embedded PCM; export the original source '" +
                              audio.source_path + "' instead",
                          audio.name);
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    if (audio.channels == 0 || audio.sample_rate == 0) {
        diagnostics.error("BULLY-EXP-0011", "audio asset has no sample rate or channel count",
                          audio.name);
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    const std::uint16_t channels = audio.channels;
    const std::uint16_t bits = 16;
    const std::uint32_t frame_count = static_cast<std::uint32_t>(audio.frame_count());
    const std::uint32_t data_bytes = frame_count * channels * (bits / 8u);
    // RIFF chunks are word aligned: an odd payload gets one pad byte, which the
    // RIFF size field must account for.
    const std::uint32_t padding = data_bytes % 2u;

    std::vector<std::uint8_t> out;
    out.reserve(44 + data_bytes + padding);
    append_tag(out, "RIFF");
    append_u32_le(out, 36 + data_bytes + padding);
    append_tag(out, "WAVE");
    append_tag(out, "fmt ");
    append_u32_le(out, 16);                                    // PCM chunk size
    append_u16_le(out, 1);                                     // format: PCM
    append_u16_le(out, channels);
    append_u32_le(out, audio.sample_rate);
    append_u32_le(out, audio.sample_rate * channels * (bits / 8u));  // byte rate
    append_u16_le(out, static_cast<std::uint16_t>(channels * (bits / 8u)));  // block align
    append_u16_le(out, bits);
    append_tag(out, "data");
    append_u32_le(out, data_bytes);
    for (const auto sample : audio.pcm) {
        const auto value = static_cast<std::uint16_t>(sample);
        append_u16_le(out, value);
    }
    if (padding != 0) out.push_back(0);
    return out;
}

}  // namespace bully::exporters
