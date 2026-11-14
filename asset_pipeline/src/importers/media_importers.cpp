// Media importers: textures (image codec) and WAV audio.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "bully/core/fs.h"
#include "bully/core/strings.h"
#include "bully/formats/audio.h"
#include "bully/formats/texture.h"
#include "bully/pipeline/builtin_importers.h"
#include "importers/profile_defaults.h"
#include "bully/pipeline/image_codec.h"

namespace bully::pipeline {

namespace {

class ImageImporter final : public Importer {
public:
    [[nodiscard]] std::string id() const override { return "com.bully.importer.image"; }
    [[nodiscard]] std::string display_name() const override { return "Image (PNG/BMP/TGA/PPM)"; }
    [[nodiscard]] std::string version() const override { return "1.0.0"; }
    [[nodiscard]] std::vector<std::string> extensions() const override
    {
        return {".png", ".bmp", ".tga", ".ppm", ".targa"};
    }
    [[nodiscard]] AssetType produces() const override { return AssetType::Texture; }

    [[nodiscard]] std::vector<ImportSettingDescriptor> settings_schema() const override
    {
        using namespace settings;
        return {
            integer("max_size", "Maximum dimension", 0, 0, 8192,
                    "Downscale so the longest side fits; 0 keeps the source resolution"),
            boolean("generate_mips", "Generate mip chain", true, ""),
            boolean("nearest_downscale", "Nearest-neighbour downscale", false,
                    "Pixel-art safe; disable for photographic textures"),
            enumeration("pixel_format", "Pixel format", "rgba8",
                        {"rgba8", "rgb8", "r8", "rgba4444", "rgb565"}, ""),
            enumeration("sampler", "Sampler filter", "linear", {"linear", "nearest"}, ""),
            enumeration("wrap", "Wrap mode", "repeat", {"repeat", "clamp", "mirror"}, ""),
            boolean("srgb", "sRGB colour space", true, ""),
            enumeration("usage", "Texture usage", "diffuse",
                        {"diffuse", "normal", "specular", "alpha", "emissive"}, ""),
            enumeration("retro_preset", "Retro profile", "none",
                        {"none", "ps2", "xbox", "unlit-retro"},
                        "Constraints from assets/presets/<name>.json; the project default is "
                        "applied automatically"),
        };
    }

    [[nodiscard]] core::Result<ImportResult> import(const ImportContext& context) override
    {
        core::DiagnosticList diagnostics;
        auto bytes = core::FileSystem::read_binary(context.source_path);
        if (!bytes) return core::Result<ImportResult>(bytes.diagnostics());
        auto image = decode_image(bytes.value());
        if (!image) return core::Result<ImportResult>(image.diagnostics());

        // The asset's import settings already carry the project's retro preset
        // defaults, so a ps2 project gets 16-bit textures and 256px caps without
        // the artist configuring anything per texture.
        const auto retro = retro_profile(context.settings.get("retro_preset", "none"));
        TextureBuildOptions options;
        options.max_size = static_cast<std::uint32_t>(
            context.settings.get_number("max_size", static_cast<double>(retro.max_texture_size)));
        options.generate_mips = context.settings.get_bool("generate_mips", retro.generate_mips);
        options.nearest_downscale = context.settings.get_bool(
            "nearest_downscale", retro.sampler == "nearest");
        options.sampler_filter = context.settings.get("sampler", retro.sampler);
        options.wrap_mode = context.settings.get("wrap", retro.wrap_mode);
        options.srgb = context.settings.get_bool("srgb", true);
        const std::string pixel_format =
            context.settings.get("pixel_format", retro.pixel_format);
        if (pixel_format == "rgb8") options.target_format = formats::PixelFormat::Rgb8;
        else if (pixel_format == "r8") options.target_format = formats::PixelFormat::R8;
        else if (pixel_format == "rgba4444") options.target_format = formats::PixelFormat::Rgba4444;
        else if (pixel_format == "rgb565") options.target_format = formats::PixelFormat::Rgb565;

        const std::string usage = context.settings.get("usage", "diffuse");
        std::uint32_t usage_flags = formats::kTextureDiffuse;
        if (usage == "normal") usage_flags = formats::kTextureNormal;
        else if (usage == "specular") usage_flags = formats::kTextureSpecular;
        else if (usage == "alpha") usage_flags = formats::kTextureAlpha;
        else if (usage == "emissive") usage_flags = formats::kTextureEmissive;

        const std::string name = std::filesystem::path(context.relative_source_path).stem().string();
        auto document = build_texture_document(std::move(image.value()), options, context.asset_id,
                                              name, context.relative_source_path);
        if (!document) return core::Result<ImportResult>(document.diagnostics());
        document.value().usage_flags = usage_flags;
        document.value().import_settings_json = context.settings.to_json().dump(0);

        diagnostics.append(document.value().validate().diagnostics());
        ImportResult result;
        result.asset_type = AssetType::Texture;
        result.output_bytes = document.value().serialize();
        result.summary = std::to_string(document.value().width) + "x" +
                         std::to_string(document.value().height) + " " +
                         std::string(formats::to_string(document.value().format)) + ", " +
                         std::to_string(document.value().mips.size()) + " mip level(s)";
        result.report_json = document.value().to_json();
        result.diagnostics = std::move(diagnostics);
        return result;
    }
};

/// Parses a RIFF/WAVE file into 16-bit interleaved PCM.
core::Result<bully::formats::AudioDocument> parse_wav(const std::vector<std::uint8_t>& bytes)
{
    core::DiagnosticList diagnostics;
    bully::formats::AudioDocument document;
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        diagnostics.error("BULLY-AUD-0020", "file is not a RIFF/WAVE audio file");
        return core::Result<formats::AudioDocument>(std::move(diagnostics));
    }
    std::size_t offset = 12;
    std::uint16_t channels = 1;
    std::uint32_t sample_rate = 44100;
    std::uint16_t bits_per_sample = 16;
    std::uint16_t format_tag = 1;
    bool have_format = false;
    std::vector<std::int16_t> pcm;
    while (offset + 8 <= bytes.size()) {
        char id[5] = {0, 0, 0, 0, 0};
        std::memcpy(id, bytes.data() + offset, 4);
        const std::uint32_t size = static_cast<std::uint32_t>(bytes[offset + 4]) |
                                   (static_cast<std::uint32_t>(bytes[offset + 5]) << 8) |
                                   (static_cast<std::uint32_t>(bytes[offset + 6]) << 16) |
                                   (static_cast<std::uint32_t>(bytes[offset + 7]) << 24);
        const std::size_t body = offset + 8;
        if (body + size > bytes.size()) break;
        if (std::memcmp(id, "fmt ", 4) == 0 && size >= 16) {
            format_tag = static_cast<std::uint16_t>(bytes[body] | (bytes[body + 1] << 8));
            channels = static_cast<std::uint16_t>(bytes[body + 2] | (bytes[body + 3] << 8));
            sample_rate = static_cast<std::uint32_t>(bytes[body + 4]) |
                          (static_cast<std::uint32_t>(bytes[body + 5]) << 8) |
                          (static_cast<std::uint32_t>(bytes[body + 6]) << 16) |
                          (static_cast<std::uint32_t>(bytes[body + 7]) << 24);
            bits_per_sample = static_cast<std::uint16_t>(bytes[body + 14] | (bytes[body + 15] << 8));
            have_format = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            if (format_tag != 1 && format_tag != 0xFFFE) {
                diagnostics.error("BULLY-AUD-0021",
                                  "compressed WAV (format tag " + std::to_string(format_tag) +
                                      ") is not supported; convert to PCM");
                return core::Result<formats::AudioDocument>(std::move(diagnostics));
            }
            const std::size_t bytes_per_sample = bits_per_sample / 8u;
            if (bytes_per_sample == 0) {
                diagnostics.error("BULLY-AUD-0022", "invalid WAV bit depth");
                return core::Result<formats::AudioDocument>(std::move(diagnostics));
            }
            const std::size_t sample_count = size / bytes_per_sample;
            pcm.reserve(sample_count);
            for (std::size_t i = 0; i < sample_count; ++i) {
                const std::uint8_t* sample = bytes.data() + body + i * bytes_per_sample;
                switch (bytes_per_sample) {
                    case 1:
                        pcm.push_back(static_cast<std::int16_t>(
                            (static_cast<int>(sample[0]) - 128) * 256));
                        break;
                    case 2:
                        pcm.push_back(static_cast<std::int16_t>(
                            static_cast<std::uint16_t>(sample[0] | (sample[1] << 8))));
                        break;
                    case 3: {
                        std::int32_t value = static_cast<std::int32_t>(sample[0]) |
                                             (static_cast<std::int32_t>(sample[1]) << 8) |
                                             (static_cast<std::int32_t>(sample[2]) << 16);
                        if ((value & 0x800000) != 0) value |= ~0xFFFFFF;
                        pcm.push_back(static_cast<std::int16_t>(value >> 8));
                        break;
                    }
                    default:
                        pcm.push_back(static_cast<std::int16_t>(
                            static_cast<std::uint16_t>(sample[2] | (sample[3] << 8))));
                        break;
                }
            }
        }
        offset = body + size + (size % 2);
    }
    if (!have_format) {
        diagnostics.error("BULLY-AUD-0023", "WAV file has no fmt chunk");
        return core::Result<formats::AudioDocument>(std::move(diagnostics));
    }
    if (pcm.empty()) {
        diagnostics.error("BULLY-AUD-0024", "WAV file contains no audio samples");
        return core::Result<formats::AudioDocument>(std::move(diagnostics));
    }
    document.channels = channels;
    document.sample_rate = sample_rate;
    document.bits_per_sample = 16;
    document.pcm = std::move(pcm);
    document.duration_seconds = static_cast<float>(
        static_cast<double>(document.pcm.size()) / (static_cast<double>(channels) * sample_rate));
    return document;
}

class WavImporter final : public Importer {
public:
    [[nodiscard]] std::string id() const override { return "com.bully.importer.wav"; }
    [[nodiscard]] std::string display_name() const override { return "WAV audio (PCM)"; }
    [[nodiscard]] std::string version() const override { return "1.0.0"; }
    [[nodiscard]] std::vector<std::string> extensions() const override { return {".wav"}; }
    [[nodiscard]] AssetType produces() const override { return AssetType::Audio; }

    [[nodiscard]] std::vector<ImportSettingDescriptor> settings_schema() const override
    {
        using namespace settings;
        return {
            boolean("embed_pcm", "Embed decoded PCM", true,
                    "Embed samples so the package is self-contained"),
            enumeration("category", "Category", "sfx", {"voice", "music", "sfx", "ambience", "ui"},
                        ""),
            boolean("normalize", "Normalise peak", false, ""),
            number("target_peak", "Target peak", 0.89, 0.1, 1.0, ""),
            boolean("loop", "Loop", false, ""),
            integer("loop_start", "Loop start (samples)", 0, 0, 1000000000, ""),
            integer("loop_end", "Loop end (samples)", 0, 0, 1000000000, ""),
            boolean("spatial", "Spatial audio", false, ""),
            number("min_distance", "Min distance", 1.0, 0.0, 1000.0, ""),
            number("max_distance", "Max distance", 50.0, 0.0, 10000.0, ""),
            enumeration("retro_preset", "Retro profile", "none",
                        {"none", "ps2", "xbox", "unlit-retro"},
                        "Audio targets from assets/presets/<name>.json"),
        };
    }

    [[nodiscard]] core::Result<ImportResult> import(const ImportContext& context) override
    {
        core::DiagnosticList diagnostics;
        auto bytes = core::FileSystem::read_binary(context.source_path);
        if (!bytes) return core::Result<ImportResult>(bytes.diagnostics());
        auto parsed = parse_wav(bytes.value());
        if (!parsed) return core::Result<ImportResult>(parsed.diagnostics());
        auto document = std::move(parsed.value());
        document.asset_id = context.asset_id;
        document.name = std::filesystem::path(context.relative_source_path).stem().string();
        document.source_path = context.relative_source_path;
        document.source_format = "wav";
        const auto analysis = document.analyse();
        document.peak = analysis.peak;
        document.loudness_lufs = analysis.rms > 0.0f
                                     ? 20.0f * std::log10(analysis.rms)
                                     : -96.0f;
        const auto retro = retro_profile(context.settings.get("retro_preset", "none"));
        // Retro presets declare a sample-rate/codec target; the importer records
        // the target in the metadata report so the audio studio can show "needs
        // resampling" instead of silently resampling the source.
        if (retro.audio_sample_rate != 0 && document.sample_rate > retro.audio_sample_rate) {
            diagnostics.warn("BULLY-AUD-0024",
                             "source is " + std::to_string(document.sample_rate) +
                                 " Hz; preset '" + retro.name + "' targets " +
                                 std::to_string(retro.audio_sample_rate) + " Hz",
                             context.relative_source_path);
        }
        const std::string category = context.settings.get("category", "sfx");
        if (category == "voice") document.category = formats::AudioCategory::Voice;
        else if (category == "music") document.category = formats::AudioCategory::Music;
        else if (category == "ambience") document.category = formats::AudioCategory::Ambience;
        else if (category == "ui") document.category = formats::AudioCategory::UiSound;
        else document.category = formats::AudioCategory::SoundEffect;

        document.loop.enabled = context.settings.get_bool("loop", false);
        document.loop.loop_start_samples = static_cast<std::int64_t>(
            context.settings.get_number("loop_start", 0.0));
        document.loop.loop_end_samples = static_cast<std::int64_t>(
            context.settings.get_number("loop_end", 0.0));
        if (document.loop.enabled && document.loop.loop_end_samples == 0) {
            document.loop.loop_end_samples = static_cast<std::int64_t>(document.frame_count());
        }
        if (context.settings.get_bool("normalize", false)) {
            const float target =
                static_cast<float>(context.settings.get_number("target_peak", 0.89));
            auto status = document.normalize(target);
            diagnostics.append(status.diagnostics());
        }
        document.spatial.enabled = context.settings.get_bool("spatial", false);
        document.spatial.min_distance =
            static_cast<float>(context.settings.get_number("min_distance", 1.0));
        document.spatial.max_distance =
            static_cast<float>(context.settings.get_number("max_distance", 50.0));
        if (!context.settings.get_bool("embed_pcm", true)) document.pcm.clear();
        document.metadata_json = context.settings.to_json().dump(0);

        auto validation = document.validate();
        diagnostics.append(validation.diagnostics());
        ImportResult result;
        result.asset_type = AssetType::Audio;
        result.output_bytes = document.serialize();
        char duration[32];
        std::snprintf(duration, sizeof(duration), "%.2f", static_cast<double>(document.duration_seconds));
        result.summary = std::string(duration) + "s, " + std::to_string(document.sample_rate) +
                         " Hz, " + std::to_string(document.channels) + " ch" +
                         (document.is_embedded() ? ", embedded PCM" : ", reference only");
        result.report_json = document.to_json();
        result.diagnostics = std::move(diagnostics);
        return result;
    }
};

}  // namespace

std::shared_ptr<Importer> make_image_importer() { return std::make_shared<ImageImporter>(); }
std::shared_ptr<Importer> make_wav_importer() { return std::make_shared<WavImporter>(); }

}  // namespace bully::pipeline
