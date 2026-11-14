#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include <cmath>

#include "bully/formats/audio.h"
#include "test_support.h"

using namespace bully::formats;

namespace {

AudioDocument make_voice_clip()
{
    AudioDocument document;
    document.asset_id = "audio-1";
    document.name = "hello";
    document.source_path = "assets/audio/hello.wav";
    document.source_format = "wav";
    document.category = AudioCategory::Voice;
    document.sample_rate = 8000;
    document.channels = 1;
    document.duration_seconds = 0.1f;
    document.pcm = {1000, -1000, 2000, -2000, 3000, -3000, 4000, -4000};
    document.loop.enabled = true;
    document.loop.loop_start_samples = 0;
    document.loop.loop_end_samples = 8;
    document.spatial.enabled = true;
    document.spatial.max_distance = 20.0f;
    document.clips.push_back(AudioClip{0, 4, "attack"});
    document.events.push_back(AudioEvent{"sfx.hit", "attack", 1.0f, 0.1f, true});
    return document;
}

}  // namespace

TEST_CASE("baudio round-trips metadata and embedded PCM", "[formats][audio]")
{
    const auto original = make_voice_clip();
    auto decoded = AudioDocument{}.deserialize(original.serialize());
    REQUIRE(decoded.has_value());
    const auto& audio = decoded.value();
    CHECK(audio.name == "hello");
    CHECK(audio.category == AudioCategory::Voice);
    CHECK(audio.is_embedded());
    CHECK(audio.pcm == original.pcm);
    CHECK(audio.loop.enabled);
    CHECK(audio.loop.loop_end_samples == 8);
    CHECK(audio.spatial.enabled);
    REQUIRE(audio.clips.size() == 1);
    CHECK(audio.clips[0].name == "attack");
    REQUIRE(audio.events.size() == 1);
    CHECK(audio.events[0].embedded);
    // 8 mono samples = 8 frames.
    CHECK(audio.frame_count() == 8);
    CHECK(audio.validate().has_value());
}

TEST_CASE("Reference-mode audio documents carry no PCM", "[formats][audio]")
{
    AudioDocument document;
    document.asset_id = "audio-2";
    document.name = "ambience";
    document.source_path = "/external/library/ambience.ogg";
    document.source_format = "ogg";
    document.category = AudioCategory::Ambience;
    CHECK_FALSE(document.is_embedded());
    auto decoded = AudioDocument{}.deserialize(document.serialize());
    REQUIRE(decoded.has_value());
    CHECK_FALSE(decoded.value().is_embedded());
    CHECK(decoded.value().source_path == "/external/library/ambience.ogg");
}

TEST_CASE("Audio analysis computes peak, RMS and crest factor", "[formats][audio]")
{
    AudioDocument document;
    document.asset_id = "a";
    document.name = "tone";
    document.pcm = {0, 8000, -8000, 0};
    const auto analysis = document.analyse();
    CHECK(analysis.peak == Catch::Approx(8000.0f / 32768.0f).margin(1e-6));
    CHECK(analysis.rms > 0.0f);
    CHECK(analysis.crest_factor > 1.0f);
}

TEST_CASE("Normalisation scales toward the target peak", "[formats][audio]")
{
    AudioDocument document;
    document.asset_id = "a";
    document.name = "quiet";
    document.pcm = {1000, -1000};
    REQUIRE(document.normalize(0.5f).has_value());
    const auto analysis = document.analyse();
    CHECK(analysis.peak == Catch::Approx(0.5f).margin(1e-3));
    // Scaling up is idempotent when already at target.
    REQUIRE(document.normalize(0.5f).has_value());
    CHECK(document.analyse().peak == Catch::Approx(0.5f).margin(1e-3));
}

TEST_CASE("Normalising an empty payload reports a diagnostic", "[formats][audio]")
{
    AudioDocument document;
    document.asset_id = "a";
    const auto status = document.normalize();
    CHECK_FALSE(status.has_value());
    CHECK(status.diagnostics().has_errors());
}

TEST_CASE("Audio validation rejects nonsensical sample rates", "[formats][audio]")
{
    auto document = make_voice_clip();
    document.sample_rate = 0;
    CHECK_FALSE(document.validate().has_value());

    document = make_voice_clip();
    document.channels = 0;
    CHECK_FALSE(document.validate().has_value());
}

TEST_CASE("WAV fixture decodes through the importer path", "[formats][audio][importer]")
{
    // The pipeline WAV importer is exercised in tests/pipeline; here we assert
    // the fixture itself is a well-formed RIFF/WAVE file with a data chunk.
    const auto wav = bully::test::sine_wav();
    REQUIRE(wav.size() > 44);
    CHECK(std::string(reinterpret_cast<const char*>(wav.data()), 4) == "RIFF");
    CHECK(std::string(reinterpret_cast<const char*>(wav.data() + 8), 4) == "WAVE");
}
