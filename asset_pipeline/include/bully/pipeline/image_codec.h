// Bully Studio - image decoding for the texture pipeline.
//
// The platform needs to import textures on machines where the pipeline is built
// without libpng/libjpeg (headless CI, minimal containers). Rather than shelling
// out to an external tool, we ship a small, well-tested decoder set:
//
//   * PNG  - full zlib inflate (stored/fixed/dynamic Huffman), all colour types
//            (grey, RGB, palette, grey+alpha, RGBA), bit depths 1..16, all five
//            scanline filters, interlacing rejected with a clear diagnostic.
//   * BMP  - 24/32-bit uncompressed and 8-bit palette.
//   * TGA  - uncompressed and RLE true-colour / greyscale.
//   * PPM  - binary P6 and ASCII P3 (used by tests and tooling).
//
// Every decoder returns normalized RGBA8 pixels plus diagnostics; nothing fails
// silently. Diagnostic codes: BULLY-IMG-0001..0049
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/formats/texture.h"

namespace bully::pipeline {

struct ImageData {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    /// Tightly packed RGBA8, row-major, stride == width * 4.
    std::vector<std::uint8_t> pixels;
    std::string format_name;  // "png", "bmp", "tga", "ppm"

    [[nodiscard]] std::size_t stride() const { return static_cast<std::size_t>(width) * 4u; }
};

/// Detects the container format from the first bytes (nullptr when unknown).
[[nodiscard]] std::string_view sniff_image_format(const std::vector<std::uint8_t>& bytes);

/// Decodes any supported container.
[[nodiscard]] core::Result<ImageData> decode_image(const std::vector<std::uint8_t>& bytes);

[[nodiscard]] core::Result<ImageData> decode_png(const std::vector<std::uint8_t>& bytes);
[[nodiscard]] core::Result<ImageData> decode_bmp(const std::vector<std::uint8_t>& bytes);
[[nodiscard]] core::Result<ImageData> decode_tga(const std::vector<std::uint8_t>& bytes);
[[nodiscard]] core::Result<ImageData> decode_ppm(const std::vector<std::uint8_t>& bytes);

/// zlib stream decompression (RFC 1950/1951). Exposed because the asset
/// packaging layer also needs it (e.g. to inspect compressed payloads).
[[nodiscard]] core::Result<std::vector<std::uint8_t>> inflate_zlib(
    const std::uint8_t* data, std::size_t size, std::size_t expected_hint = 0);

/// Encodes RGBA8 pixels to a binary PPM (debug/export helper, no compression).
[[nodiscard]] std::vector<std::uint8_t> encode_ppm(const ImageData& image);

/// Converts decoded RGBA8 data into a TextureDocument with mip chain and
/// optional retro downscale/quantisation.
struct TextureBuildOptions {
    std::uint32_t max_size = 0;               // 0 = keep source resolution
    bool generate_mips = true;
    bool nearest_downscale = false;           // pixel-art safe
    formats::PixelFormat target_format = formats::PixelFormat::Rgba8;
    std::string sampler_filter = "linear";
    std::string wrap_mode = "repeat";
    bool srgb = true;
};

[[nodiscard]] core::Result<formats::TextureDocument> build_texture_document(
    ImageData image, const TextureBuildOptions& options, std::string asset_id,
    std::string name, std::string source_path);

}  // namespace bully::pipeline
