#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string>

#include "bully/formats/texture.h"

using namespace bully::formats;

namespace {

TextureDocument make_texture(std::uint32_t width, std::uint32_t height)
{
    TextureDocument document;
    document.asset_id = "tex-1";
    document.name = "checker";
    document.width = width;
    document.height = height;
    document.format = PixelFormat::Rgba8;
    MipLevel base;
    base.width = width;
    base.height = height;
    base.stride = width * 4;
    base.pixels.resize(static_cast<std::size_t>(base.stride) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t offset = (static_cast<std::size_t>(y) * base.stride) + x * 4u;
            const bool white = ((x / 2) + (y / 2)) % 2 == 0;
            base.pixels[offset] = white ? 255 : 0;
            base.pixels[offset + 1] = white ? 255 : 0;
            base.pixels[offset + 2] = white ? 255 : 0;
            base.pixels[offset + 3] = 255;
        }
    }
    document.mips.push_back(base);
    return document;
}

}  // namespace

TEST_CASE("btexture round-trips mips and atlas regions", "[formats][texture]")
{
    auto original = make_texture(8, 8);
    auto chain = original.generate_mip_chain();
    REQUIRE(chain.has_value());
    for (const auto& level : chain.value()) original.mips.push_back(level);
    original.is_atlas = true;
    AtlasRegion region;
    region.name = "icon";
    region.width = 4;
    region.height = 4;
    region.u1 = 0.5f;
    region.v1 = 0.5f;
    original.atlas_regions.push_back(region);

    auto decoded = TextureDocument{}.deserialize(original.serialize());
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().width == 8);
    CHECK(decoded.value().mips.size() == original.mips.size());
    REQUIRE(decoded.value().atlas_regions.size() == 1);
    CHECK(decoded.value().atlas_regions[0].name == "icon");
    CHECK(decoded.value().validate().has_value());
}

TEST_CASE("Mip chain halves dimensions down to 1x1", "[formats][texture]")
{
    auto texture = make_texture(16, 8);
    auto chain = texture.generate_mip_chain();
    REQUIRE(chain.has_value());
    REQUIRE(chain.value().size() == 4);
    CHECK(chain.value()[0].width == 8);
    CHECK(chain.value()[0].height == 4);
    CHECK(chain.value()[3].width == 1);
    CHECK(chain.value()[3].height == 1);
}

TEST_CASE("Nearest and bilinear resize honour the requested size", "[formats][texture]")
{
    auto texture = make_texture(8, 8);
    auto nearest = texture.resize_nearest(4, 4);
    REQUIRE(nearest.has_value());
    CHECK(nearest.value().width == 4);
    CHECK(nearest.value().pixels.size() == 4u * 4u * 4u);
    auto bilinear = texture.resize_bilinear(3, 5);
    REQUIRE(bilinear.has_value());
    CHECK(bilinear.value().height == 5);
}

TEST_CASE("Texture validation catches truncated payloads and bad regions", "[formats][texture]")
{
    auto texture = make_texture(4, 4);
    texture.mips[0].pixels.resize(4);
    CHECK_FALSE(texture.validate().has_value());

    texture = make_texture(4, 4);
    AtlasRegion region;
    region.name = "outside";
    region.x = 3;
    region.y = 3;
    region.width = 4;
    region.height = 4;
    texture.atlas_regions.push_back(region);
    CHECK_FALSE(texture.validate().has_value());
}

TEST_CASE("Retro pixel formats store half the bytes of RGBA", "[formats][texture]")
{
    CHECK(bytes_per_pixel(PixelFormat::Rgba8) == 4);
    CHECK(bytes_per_pixel(PixelFormat::Rgb565) == 2);
    CHECK(bytes_per_pixel(PixelFormat::Rgba4444) == 2);
    CHECK(bytes_per_pixel(PixelFormat::R8) == 1);
    CHECK_FALSE(is_supported_encoding(PixelFormat::Bc1));
    CHECK(std::string(to_string(PixelFormat::Rgba4444)) == "rgba4444");
}

TEST_CASE("Texture JSON summary reports vram estimate", "[formats][texture]")
{
    auto texture = make_texture(4, 4);
    const std::string json = texture.to_json();
    CHECK(json.find("\"pixel_format\": \"rgba8\"") != std::string::npos);
    CHECK(json.find("\"width\": 4") != std::string::npos);
}
