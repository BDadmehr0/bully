#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>

#include "bully/formats/audio.h"
#include "bully/pipeline/builtin_importers.h"
#include "fixture.h"
#include "test_support.h"

using namespace bully;
using namespace bully::pipeline;

namespace {

ImportContext audio_context(test::TempDir& dir, std::string relative, std::string asset_id)
{
    ImportContext context;
    context.project_root = dir.str();
    context.relative_source_path = relative;
    context.source_path = (dir.path() / relative).string();
    context.output_path = (dir.path() / "built/out.baudio").string();
    context.asset_id = std::move(asset_id);
    return context;
}

}  // namespace

TEST_CASE("WAV importer embeds PCM and reports analysis", "[pipeline][audio]")
{
    test::TempDir dir;
    const std::string relative = "assets/audio/beep.wav";
    dir.write_bytes(relative, test::sine_wav(8000, 0.1f));
    auto importer = make_wav_importer();
    auto result = importer->import(audio_context(dir, relative, "audio-id"));
    REQUIRE(result.has_value());
    CHECK(result.value().asset_type == AssetType::Audio);

    formats::AudioDocument document;
    auto decoded = document.deserialize(result.value().output_bytes);
    REQUIRE(decoded.has_value());
    const auto& audio = decoded.value();
    CHECK(audio.asset_id == "audio-id");
    CHECK(audio.sample_rate == 8000);
    CHECK(audio.channels == 1);
    CHECK(audio.is_embedded());
    CHECK(audio.frame_count() == 800);
    CHECK(audio.peak > 0.4f);
    CHECK(audio.loudness_lufs < 0.0f);
    CHECK(audio.validate().has_value());
}

TEST_CASE("WAV importer can produce reference-mode assets", "[pipeline][audio]")
{
    test::TempDir dir;
    const std::string relative = "assets/audio/voice.wav";
    dir.write_bytes(relative, test::sine_wav());
    auto importer = make_wav_importer();
    auto context = audio_context(dir, relative, "id");
    context.settings.set("embed_pcm", "false");
    context.settings.set("category", "voice");
    auto result = importer->import(context);
    REQUIRE(result.has_value());
    formats::AudioDocument document;
    auto decoded = document.deserialize(result.value().output_bytes);
    REQUIRE(decoded.has_value());
    CHECK_FALSE(decoded.value().is_embedded());
    CHECK(decoded.value().category == formats::AudioCategory::Voice);
    CHECK(decoded.value().source_format == "wav");
}

TEST_CASE("WAV importer applies loop points and normalisation", "[pipeline][audio]")
{
    test::TempDir dir;
    const std::string relative = "assets/audio/loop.wav";
    dir.write_bytes(relative, test::sine_wav(8000, 0.2f));
    auto importer = make_wav_importer();
    auto context = audio_context(dir, relative, "id");
    context.settings.set("loop", "true");
    context.settings.set("loop_start", "100");
    context.settings.set("normalize", "true");
    context.settings.set("target_peak", "0.5");
    auto result = importer->import(context);
    REQUIRE(result.has_value());
    formats::AudioDocument document;
    auto decoded = document.deserialize(result.value().output_bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().loop.enabled);
    CHECK(decoded.value().loop.loop_start_samples == 100);
    CHECK(decoded.value().loop.loop_end_samples == 1600);
    CHECK(decoded.value().analyse().peak == Catch::Approx(0.5f).margin(0.01f));
}

TEST_CASE("WAV importer rejects unsupported or truncated files", "[pipeline][audio]")
{
    test::TempDir dir;
    auto importer = make_wav_importer();

    dir.write("assets/audio/fake.wav", "RIFFxxxxWAVEnope");
    auto bad = importer->import(audio_context(dir, "assets/audio/fake.wav", "id"));
    CHECK_FALSE(bad.has_value());
    CHECK(bad.diagnostics().has_errors());

    auto truncated = test::sine_wav();
    truncated.resize(30);
    dir.write_bytes("assets/audio/short.wav", truncated);
    auto short_result = importer->import(audio_context(dir, "assets/audio/short.wav", "id"));
    CHECK_FALSE(short_result.has_value());
}

TEST_CASE("Pipeline builds audio end to end", "[pipeline][audio][integration]")
{
    test::ProjectFixture fixture("audio-project");
    const std::string source = fixture.write_asset_bytes("audio/beep.wav", test::sine_wav());
    const auto report = fixture.build_all();
    REQUIRE(report.success());
    const auto record = fixture.record_for(source);
    REQUIRE(record.has_value());
    CHECK(record->type == AssetType::Audio);
    CHECK(record->runtime_path.find(".baudio") != std::string::npos);

    formats::AudioDocument document;
    auto decoded = document.deserialize(fixture.read_compiled(source));
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().frame_count() == 800);
}
