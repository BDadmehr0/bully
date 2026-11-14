#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

#include <cstring>

#include "bully/core/fs.h"
#include "bully/core/json.h"
#include "bully/exporters/exporters.h"
#include "bully/pipeline/builtin_importers.h"
#include "bully/pipeline/image_codec.h"
#include "test_support.h"

using namespace bully;
using Catch::Approx;

namespace {

exporters::RgbaImage make_image(std::uint32_t width, std::uint32_t height)
{
    exporters::RgbaImage image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * height * 4);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
            image.pixels[index + 0] = static_cast<std::uint8_t>(x * 40);
            image.pixels[index + 1] = static_cast<std::uint8_t>(y * 60);
            image.pixels[index + 2] = 128;
            image.pixels[index + 3] = 255;
        }
    }
    return image;
}

formats::TextureDocument make_texture(formats::PixelFormat format)
{
    formats::TextureDocument texture;
    texture.asset_id = "texture.test";
    texture.name = "test";
    texture.width = 3;
    texture.height = 2;
    texture.format = format;
    formats::MipLevel mip;
    mip.width = 3;
    mip.height = 2;
    const std::uint32_t bpp = formats::bytes_per_pixel(format);
    mip.stride = 3 * bpp;
    mip.pixels.resize(static_cast<std::size_t>(mip.stride) * 2);
    for (std::size_t i = 0; i < mip.pixels.size(); ++i) {
        mip.pixels[i] = static_cast<std::uint8_t>(i * 7);
    }
    texture.mips.push_back(std::move(mip));
    return texture;
}

/// Runs the real WAV importer over a file and returns the compiled document.
core::Result<formats::AudioDocument> reimport_audio(const std::filesystem::path& path)
{
    auto importer = pipeline::make_wav_importer();
    pipeline::ImportContext context;
    context.source_path = path.string();
    context.relative_source_path = path.filename().string();
    context.asset_id = "audio.reimport";
    context.project_root = path.parent_path().string();
    context.settings.importer_id = importer->id();
    context.settings.set("embed_pcm", "true");
    auto imported = importer->import(context);
    if (!imported) return core::Result<formats::AudioDocument>(imported.diagnostics());
    formats::AudioDocument reader;
    auto decoded = reader.deserialize(imported.value().output_bytes);
    if (!decoded) return decoded;
    return decoded.value();
}

/// Runs the real OBJ importer over a file and returns the compiled document.
core::Result<formats::ModelDocument> reimport_model(const std::filesystem::path& path)
{
    auto importer = pipeline::make_obj_importer();
    pipeline::ImportContext context;
    context.source_path = path.string();
    context.relative_source_path = path.filename().string();
    context.asset_id = "model.reimport";
    context.project_root = path.parent_path().string();
    context.settings.importer_id = importer->id();
    auto imported = importer->import(context);
    if (!imported) return core::Result<formats::ModelDocument>(imported.diagnostics());
    formats::ModelDocument reader;
    auto decoded = reader.deserialize(imported.value().output_bytes);
    if (!decoded) return decoded;
    return decoded.value();
}

}  // namespace

TEST_CASE("PNG export produces a decodable by the importer image", "[exporters][texture]")
{
    const auto image = make_image(4, 3);
    auto png = exporters::encode_png(image);
    REQUIRE(png.has_value());

    // Round-trip through the *import* path: the pipeline decoder must accept
    // exactly what the exporter writes.
    auto decoded = pipeline::decode_image(png.value());
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().width == 4);
    CHECK(decoded.value().height == 3);
    CHECK(decoded.value().pixels.size() == 4 * 3 * 4);
    // Row order and channels must survive unchanged.
    for (std::size_t i = 0; i < image.pixels.size(); ++i) {
        INFO("pixel byte " << i);
        CHECK(decoded.value().pixels[i] == image.pixels[i]);
    }
}

TEST_CASE("PNG export is deterministic", "[exporters][texture]")
{
    const auto image = make_image(2, 2);
    auto first = exporters::encode_png(image);
    auto second = exporters::encode_png(image);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first.value() == second.value());
    // Signature + IHDR + IDAT + IEND.
    CHECK(first.value().size() > 40);
    CHECK(std::memcmp(first.value().data(), "\x89PNG\r\n\x1a\n", 8) == 0);
}

TEST_CASE("Invalid images are refused rather than written", "[exporters][texture]")
{
    exporters::RgbaImage empty;
    auto png = exporters::encode_png(empty);
    CHECK_FALSE(png.has_value());
    CHECK(png.diagnostics().all().front().code == "BULLY-EXP-0004");

    exporters::RgbaImage truncated;
    truncated.width = 4;
    truncated.height = 4;  // pixels empty -> invalid
    CHECK_FALSE(exporters::encode_png(truncated).has_value());
}

TEST_CASE("Compiled textures denormalise to RGBA for every uncompressed format",
          "[exporters][texture]")
{
    SECTION("rgba8")
    {
        const auto texture = make_texture(formats::PixelFormat::Rgba8);
        auto rgba = exporters::to_rgba(texture);
        REQUIRE(rgba.has_value());
        CHECK(rgba.value().width == 3);
        CHECK(rgba.value().valid());
        CHECK(rgba.value().pixels[0] == 0);
        CHECK(rgba.value().pixels[4] == texture.mips[0].pixels[4]);
    }
    SECTION("rgb8 gains an opaque alpha")
    {
        const auto texture = make_texture(formats::PixelFormat::Rgb8);
        auto rgba = exporters::to_rgba(texture);
        REQUIRE(rgba.has_value());
        CHECK(rgba.value().pixels[3] == 255);
        CHECK(rgba.value().pixels[0] == texture.mips[0].pixels[0]);
    }
    SECTION("rgb565 expands to 8 bits per channel")
    {
        auto texture = make_texture(formats::PixelFormat::Rgb565);
        // Pure red in RGB565: 0xF800 little-endian.
        texture.mips[0].pixels[0] = 0x00;
        texture.mips[0].pixels[1] = 0xF8;
        auto rgba = exporters::to_rgba(texture);
        REQUIRE(rgba.has_value());
        CHECK(rgba.value().pixels[0] == 255);
        CHECK(rgba.value().pixels[1] == 0);
        CHECK(rgba.value().pixels[2] == 0);
        CHECK(rgba.value().pixels[3] == 255);
    }
    SECTION("bc1 is refused instead of exporting garbage")
    {
        const auto texture = make_texture(formats::PixelFormat::Bc1);
        auto rgba = exporters::to_rgba(texture);
        CHECK_FALSE(rgba.has_value());
        CHECK(rgba.diagnostics().all().front().code == "BULLY-EXP-0002");
    }
}

TEST_CASE("A texture with no mip levels cannot be exported", "[exporters][texture]")
{
    formats::TextureDocument texture;
    texture.name = "empty";
    auto rgba = exporters::to_rgba(texture);
    CHECK_FALSE(rgba.has_value());
    CHECK(rgba.diagnostics().all().front().code == "BULLY-EXP-0001");
}

TEST_CASE("PPM export matches the pipeline encoder byte for byte", "[exporters][texture]")
{
    const auto image = make_image(2, 2);
    auto encoded = exporters::encode_ppm(image);

    pipeline::ImageData data;
    data.width = image.width;
    data.height = image.height;
    data.pixels = image.pixels;
    CHECK(encoded == pipeline::encode_ppm(data));
}

TEST_CASE("Audio exports as a WAV the importer accepts", "[exporters][audio]")
{
    test::TempDir dir;
    formats::AudioDocument audio;
    audio.asset_id = "audio.blip";
    audio.name = "blip";
    audio.sample_rate = 22050;
    audio.channels = 1;
    audio.bits_per_sample = 16;
    // 0.1 s of a simple ramp; odd frame count would exercise the pad byte.
    for (int i = 0; i < 2205; ++i) {
        audio.pcm.push_back(static_cast<std::int16_t>((i * 13) % 3000 - 1500));
    }
    auto wav = exporters::encode_wav(audio);
    REQUIRE(wav.has_value());
    CHECK(std::memcmp(wav.value().data(), "RIFF", 4) == 0);
    CHECK(std::memcmp(wav.value().data() + 8, "WAVE", 4) == 0);

    // The importer parses it back to identical samples: export the compiled
    // asset and re-import it through the real WAV importer.
    const auto wav_path = dir.path() / "blip.wav";
    REQUIRE(core::FileSystem::write_binary(wav_path, wav.value()).has_value());
    auto reimported = reimport_audio(wav_path);
    REQUIRE(reimported.has_value());
    CHECK(reimported.value().sample_rate == 22050);
    CHECK(reimported.value().channels == 1);
    CHECK(reimported.value().pcm == audio.pcm);
}

TEST_CASE("Reference-mode audio refuses to export silence", "[exporters][audio]")
{
    formats::AudioDocument audio;
    audio.asset_id = "audio.voice";
    audio.name = "voice";
    audio.source_path = "assets/audio/voice.wav";
    auto wav = exporters::encode_wav(audio);
    CHECK_FALSE(wav.has_value());
    CHECK(wav.diagnostics().all().front().code == "BULLY-EXP-0010");
    // The message names the original file, which is the actionable instruction.
    CHECK(wav.diagnostics().all().front().message.find("assets/audio/voice.wav") !=
          std::string::npos);
}

TEST_CASE("OBJ export round-trips geometry through the importer", "[exporters][model]")
{
    formats::ModelDocument model;
    model.asset_id = "model.crate";
    model.name = "crate";
    formats::ModelMesh mesh;
    mesh.name = "body";
    for (int i = 0; i < 4; ++i) {
        formats::ModelVertex vertex;
        vertex.position = formats::Vec3f{static_cast<float>(i), 0.0f, 1.0f};
        vertex.normal = formats::Vec3f{0.0f, 1.0f, 0.0f};
        vertex.uv0 = formats::Vec2f{static_cast<float>(i) * 0.25f, 0.0f};
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    model.meshes.push_back(mesh);
    formats::MaterialBinding material;
    material.slot_name = "crate_surface";
    model.materials.push_back(material);
    mesh.material_slot = 0;
    model.meshes[0] = mesh;

    auto exported = exporters::encode_obj(model);
    REQUIRE(exported.has_value());
    CHECK(exported.value().obj.find("mtllib crate.mtl") != std::string::npos);
    CHECK(exported.value().obj.find("usemtl crate_surface") != std::string::npos);
    CHECK(exported.value().obj.find("f 1/1/1 2/2/2 3/3/3") != std::string::npos);
    CHECK_FALSE(exported.value().mtl.empty());

    // Re-import through the real importer rather than the parser: geometry,
    // material slots and the MTL reference all have to survive.
    test::TempDir dir;
    const auto obj_path = dir.path() / "crate.obj";
    REQUIRE(core::FileSystem::write_text(obj_path, exported.value().obj).has_value());
    REQUIRE(core::FileSystem::write_text(dir.path() / "crate.mtl", exported.value().mtl)
                .has_value());
    auto reimported = reimport_model(obj_path);
    REQUIRE(reimported.has_value());
    REQUIRE(reimported.value().meshes.size() == 1);
    CHECK(reimported.value().meshes[0].vertices.size() == 4);
    CHECK(reimported.value().meshes[0].indices.size() == 6);
    CHECK(reimported.value().meshes[0].name == "body");
}

TEST_CASE("OBJ export skips derived LOD meshes and says so", "[exporters][model]")
{
    formats::ModelDocument model;
    model.name = "lodded";
    for (std::uint8_t lod = 0; lod < 3; ++lod) {
        formats::ModelMesh mesh;
        mesh.name = "body_lod" + std::to_string(lod);
        mesh.lod = lod;
        mesh.vertices.resize(3);
        mesh.indices = {0, 1, 2};
        model.meshes.push_back(mesh);
    }
    auto exported = exporters::encode_obj(model);
    REQUIRE(exported.has_value());
    CHECK(exported.value().obj.find("o body_lod0") != std::string::npos);
    CHECK(exported.value().obj.find("o body_lod1") == std::string::npos);
    bool warned = false;
    for (const auto& diagnostic : exported.diagnostics().all()) {
        if (diagnostic.code == "BULLY-EXP-0022") warned = true;
    }
    CHECK(warned);

    formats::ModelDocument none;
    none.name = "empty";
    auto failed = exporters::encode_obj(none);
    CHECK_FALSE(failed.has_value());
    CHECK(failed.diagnostics().all().front().code == "BULLY-EXP-0020");
}

TEST_CASE("Authored content exports to the JSON shape the importers read",
          "[exporters][json]")
{
    formats::SceneDocument scene;
    scene.asset_id = "scene.yard";
    scene.name = "yard";
    scene.environment.fog_enabled = true;
    formats::SceneEntity entity;
    entity.entity_id = "ent.crate";
    entity.name = "Crate";
    entity.model_asset = "model.crate";
    entity.transform.position = formats::Vec3f{1.0f, 2.0f, 3.0f};
    scene.entities.push_back(entity);
    formats::SpawnPoint spawn;
    spawn.spawn_id = "spawn.player";
    spawn.kind = "player";
    scene.spawns.push_back(spawn);

    const std::string json = exporters::encode_scene_json(scene);
    auto parsed = core::json::parse(json);
    REQUIRE(parsed.has_value());
    CHECK(parsed.value().get_string("name") == "yard");
    REQUIRE(parsed.value().find("entities") != nullptr);
    const auto& entities = parsed.value().at("entities");
    REQUIRE(entities.size() == 1);
    CHECK(entities[0].get_string("model") == "model.crate");
    CHECK(entities[0].at("position")[1].as_number() == Approx(2.0));
    CHECK(parsed.value().at("environment").get_bool("fog_enabled"));

    formats::MissionDocument mission;
    mission.asset_id = "mission.first";
    mission.name = "First";
    mission.start_node = "n.a";
    mission.repeatable = true;
    formats::MissionObjective objective;
    objective.objective_id = "obj.a";
    objective.description = "Do it";
    objective.required_count = 3;
    mission.objectives.push_back(objective);
    const auto mission_json = core::json::parse(exporters::encode_mission_json(mission));
    REQUIRE(mission_json.has_value());
    CHECK(mission_json.value().get_bool("repeatable"));
    CHECK(mission_json.value().at("objectives")[0].get_int("count") == 3);

    formats::DialogueDocument dialogue;
    dialogue.asset_id = "dialogue.intro";
    dialogue.name = "intro";
    dialogue.entry_node = "n.a";
    formats::DialogueLine line;
    line.line_id = "dlg.a";
    line.text = "Hello";
    dialogue.lines.push_back(line);
    const auto dialogue_json = core::json::parse(exporters::encode_dialogue_json(dialogue));
    REQUIRE(dialogue_json.has_value());
    CHECK(dialogue_json.value().at("lines")[0].get_string("text") == "Hello");
}

TEST_CASE("export_file writes files atomically and picks the format by extension",
          "[exporters][files]")
{
    test::TempDir dir;
    // A compiled texture on disk, exported to PNG.
    const auto texture = make_texture(formats::PixelFormat::Rgba8);
    const auto compiled = dir.path() / "test.btexture";
    REQUIRE(core::FileSystem::write_binary(compiled, texture.serialize()).has_value());

    const auto png_path = dir.path() / "test.png";
    REQUIRE(exporters::export_file(compiled, png_path).has_value());
    CHECK(core::FileSystem::exists(png_path));
    CHECK_FALSE(core::FileSystem::exists(dir.path() / "test.png.tmp"));
    auto bytes = core::FileSystem::read_binary(png_path);
    REQUIRE(bytes.has_value());
    CHECK(pipeline::decode_image(bytes.value()).has_value());

    // Unsupported target extension is refused with the actionable message.
    auto refused = exporters::export_file(compiled, dir.path() / "test.jpg");
    CHECK_FALSE(refused.has_value());
    CHECK(refused.diagnostics().all().front().code == "BULLY-EXP-0031");

    // Unknown compiled format is refused too.
    const auto junk = dir.path() / "junk.bin";
    REQUIRE(core::FileSystem::write_binary(junk, {1, 2, 3}).has_value());
    auto unknown = exporters::export_file(junk, dir.path() / "junk.json");
    CHECK_FALSE(unknown.has_value());
    CHECK(unknown.diagnostics().all().front().code == "BULLY-EXP-0034");
}

TEST_CASE("The exporter registry maps compiled formats to exporter ids",
          "[exporters][registry]")
{
    const auto& list = exporters::exporters();
    CHECK(list.size() >= 9);
    for (const auto& info : list) {
        REQUIRE_FALSE(info.id.empty());
        REQUIRE_FALSE(info.asset_kind.empty());
    }
    const auto* texture = exporters::exporter_for(".btexture");
    REQUIRE(texture != nullptr);
    CHECK(texture->id == "com.bully.exporter.png");
    const auto* model = exporters::exporter_for(".BMODEL");
    REQUIRE(model != nullptr);
    CHECK(model->id == "com.bully.exporter.obj");
    CHECK(exporters::exporter_for(".bpackage") == nullptr);
    CHECK(exporters::exporter_for(".obj") == nullptr);
}
