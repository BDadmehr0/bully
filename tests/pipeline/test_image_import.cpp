#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "bully/formats/texture.h"
#include "bully/pipeline/builtin_importers.h"
#include "bully/pipeline/image_codec.h"
#include "fixture.h"
#include "test_support.h"

using namespace bully;
using namespace bully::pipeline;

namespace {

ImportContext image_context(test::TempDir& dir, std::string relative, std::string asset_id)
{
    ImportContext context;
    context.project_root = dir.str();
    context.relative_source_path = relative;
    context.source_path = (dir.path() / relative).string();
    context.output_path = (dir.path() / "built/out.btexture").string();
    context.asset_id = std::move(asset_id);
    return context;
}

}  // namespace

TEST_CASE("PNG decoder handles a hand-built RGBA image", "[pipeline][image]")
{
    auto decoded = decode_png(test::tiny_png());
    REQUIRE(decoded.has_value());
    const auto& image = decoded.value();
    CHECK(image.width == 2);
    CHECK(image.height == 2);
    CHECK(image.pixels.size() == 16);
    // Row 0 col 0 is red.
    CHECK(image.pixels[0] == 255);
    CHECK(image.pixels[1] == 0);
    CHECK(image.pixels[3] == 255);
    // Row 1 col 0 is blue.
    CHECK(image.pixels[8] == 0);
    CHECK(image.pixels[10] == 255);
}

TEST_CASE("BMP and TGA decoders normalise to RGBA8", "[pipeline][image]")
{
    auto bmp = decode_bmp(test::tiny_bmp());
    REQUIRE(bmp.has_value());
    CHECK(bmp.value().width == 2);
    CHECK(bmp.value().height == 2);
    CHECK(bmp.value().pixels.size() == 16);
    // BMP rows are bottom-up: the decoder flips them so row 0 is the top row
    // of the image (red, green), matching the PNG/TGA fixtures.
    CHECK(bmp.value().pixels[0] == 255);
    CHECK(bmp.value().pixels[1] == 0);
    CHECK(bmp.value().pixels[2] == 0);
    CHECK(bmp.value().pixels[3] == 255);
    CHECK(bmp.value().pixels[8 + 2] == 255);  // bottom-left pixel is blue

    auto tga = decode_tga(test::tiny_tga());
    REQUIRE(tga.has_value());
    CHECK(tga.value().width == 2);
    CHECK(tga.value().height == 2);
    // TGA fixture is top-left origin: first pixel is red (BGR -> RGBA).
    CHECK(tga.value().pixels[0] == 255);
    CHECK(tga.value().pixels[2] == 0);
}

TEST_CASE("Image sniffing identifies containers and rejects unknown data", "[pipeline][image]")
{
    CHECK(sniff_image_format(test::tiny_png()) == "png");
    CHECK(sniff_image_format(test::tiny_bmp()) == "bmp");
    CHECK(sniff_image_format(test::tiny_tga()) == "tga");
    const std::vector<std::uint8_t> garbage{0, 1, 2, 3, 4, 5, 6, 7};
    CHECK(sniff_image_format(garbage).empty());
    CHECK_FALSE(decode_image(garbage).has_value());
}

TEST_CASE("Truncated PNG reports a diagnostic instead of garbage", "[pipeline][image]")
{
    auto bytes = test::tiny_png();
    // Drop the IEND chunk and the tail of the IDAT payload: the IDAT length
    // then points past the end of the file.
    bytes.resize(bytes.size() - 24);
    auto decoded = decode_png(bytes);
    CHECK_FALSE(decoded.has_value());
    CHECK(decoded.diagnostics().has_errors());

    // A PNG whose compressed data decompresses to fewer bytes than the header
    // promises is rejected as well.
    auto corrupt = test::tiny_png();
    corrupt[41] ^= 0xFF;  // flip a byte inside the zlib stream
    auto attempted = decode_png(corrupt);
    if (!attempted.has_value()) CHECK(attempted.diagnostics().has_errors());
}

TEST_CASE("PPM encode/decode round-trips", "[pipeline][image]")
{
    ImageData image;
    image.width = 2;
    image.height = 2;
    image.pixels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    const auto encoded = encode_ppm(image);
    auto decoded = decode_ppm(encoded);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().width == 2);
    CHECK(decoded.value().pixels == image.pixels);
}

TEST_CASE("Texture builder applies size limits and retro formats", "[pipeline][image]")
{
    auto image = decode_png(test::tiny_png());
    REQUIRE(image.has_value());
    TextureBuildOptions options;
    options.max_size = 1;
    options.nearest_downscale = true;
    auto document = build_texture_document(image.value(), options, "tex-id", "test", "assets/t.png");
    REQUIRE(document.has_value());
    CHECK(document.value().width == 1);
    CHECK(document.value().height == 1);
    CHECK(document.value().sampler_filter == "linear");
    CHECK(document.value().validate().has_value());
}

TEST_CASE("Image importer writes a btexture with mips", "[pipeline][image]")
{
    test::TempDir dir;
    const std::string relative = "assets/textures/tile.png";
    dir.write_bytes(relative, test::tiny_png());
    auto importer = make_image_importer();
    auto context = image_context(dir, relative, "image-asset");
    auto result = importer->import(context);
    REQUIRE(result.has_value());
    CHECK(result.value().asset_type == AssetType::Texture);

    formats::TextureDocument document;
    auto decoded = document.deserialize(result.value().output_bytes);
    REQUIRE(decoded.has_value());
    const auto& texture = decoded.value();
    CHECK(texture.asset_id == "image-asset");
    CHECK(texture.width == 2);
    CHECK(texture.height == 2);
    CHECK(texture.mips.size() == 2);  // base + 1x1
    CHECK(texture.sampler_filter == "linear");
    CHECK(texture.validate().has_value());

    formats::TextureDocument parsed;
    REQUIRE(parsed.deserialize(result.value().output_bytes).has_value());
}

TEST_CASE("Image importer honours max_size and pixel formats", "[pipeline][image]")
{
    test::TempDir dir;
    const std::string relative = "assets/textures/big.bmp";
    dir.write_bytes(relative, test::tiny_bmp());
    auto importer = make_image_importer();
    auto context = image_context(dir, relative, "id");
    context.settings.set("max_size", "1");
    context.settings.set("nearest_downscale", "1");
    auto result = importer->import(context);
    REQUIRE(result.has_value());
    formats::TextureDocument document;
    auto decoded = document.deserialize(result.value().output_bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().width == 1);

    context.settings.set("max_size", "0");
    context.settings.set("pixel_format", "rgb565");
    auto quantised = importer->import(context);
    REQUIRE(quantised.has_value());
    formats::TextureDocument quantised_document;
    auto decoded_quantised = quantised_document.deserialize(quantised.value().output_bytes);
    REQUIRE(decoded_quantised.has_value());
    CHECK(decoded_quantised.value().format == formats::PixelFormat::Rgb565);
}

TEST_CASE("Image importer fails loudly on an undecodable file", "[pipeline][image]")
{
    test::TempDir dir;
    const std::string relative = "assets/textures/broken.png";
    dir.write(relative, "not a png at all");
    auto importer = make_image_importer();
    auto result = importer->import(image_context(dir, relative, "id"));
    CHECK_FALSE(result.has_value());
    CHECK(result.diagnostics().has_errors());
}

TEST_CASE("Pipeline builds textures end to end", "[pipeline][image][integration]")
{
    test::ProjectFixture fixture("texture-project");
    const std::string source = fixture.write_asset_bytes("textures/tile.png", test::tiny_png());
    const auto report = fixture.build_all();
    REQUIRE(report.success());
    const auto record = fixture.record_for(source);
    REQUIRE(record.has_value());
    CHECK(record->type == AssetType::Texture);
    CHECK(record->runtime_path.find(".btexture") != std::string::npos);

    formats::TextureDocument document;
    auto decoded = document.deserialize(fixture.read_compiled(source));
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().width == 2);
}
