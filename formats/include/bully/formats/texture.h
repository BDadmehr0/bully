// Bully Studio - .btexture : optimized texture (optionally an atlas).
//
// v1.0 chunks:
//   INFO  identity, provenance, dimensions, pixel format, mip count, flags
//   MIP   one chunk per mip level: width, height, stride, payload
//   ATLS  optional atlas layout: named regions mapped into the atlas
//   META  import settings + optimization report (JSON)
//
// Pixel payload is stored raw (uncompressed). The pipeline is free to encode
// platform-specific blocks later; unknown encodings are flagged by
// `PixelFormat` so a runtime that cannot decode them reports a clear error
// instead of rendering garbage.
//
// Diagnostic codes: BULLY-TEX-0001..0019
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/formats/binary.h"

namespace bully::formats {

inline constexpr FormatDescriptor kTextureFormat{
    fourcc("BTEX"), "btexture", {1, 0, 0}, ".btexture",
    "Optimized texture: RGBA/RGB/R8 pixels, mip chain, optional atlas regions"};

enum class PixelFormat : std::uint8_t {
    Rgba8 = 0,
    Rgb8 = 1,
    R8 = 2,
    Rgba4444 = 3,   // PS2-era 16-bit with alpha
    Rgb565 = 4,     // PS2-era 16-bit opaque
    Bc1 = 5,        // reserved for future DXT-style compression
    Bc3 = 6,
    Unknown = 255,
};

/// Bytes per pixel for uncompressed formats, 0 for block-compressed.
[[nodiscard]] std::uint32_t bytes_per_pixel(PixelFormat format) noexcept;
[[nodiscard]] std::string_view to_string(PixelFormat format) noexcept;
[[nodiscard]] bool is_supported_encoding(PixelFormat format) noexcept;

enum TextureUsage : std::uint32_t {
    kTextureDiffuse = 1u << 0,
    kTextureNormal = 1u << 1,
    kTextureSpecular = 1u << 2,
    kTextureAlpha = 1u << 3,
    kTextureEmissive = 1u << 4,
};

struct MipLevel {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    /// Row stride in bytes (supports padded rows produced by external tools).
    std::uint32_t stride = 0;
    std::vector<std::uint8_t> pixels;
};

struct AtlasRegion {
    std::string name;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    /// UV rectangle in normalized [0,1] space (precomputed for the runtime).
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

struct TextureDocument {
    std::string asset_id;
    std::string name;
    std::string source_path;
    std::string importer;
    PixelFormat format = PixelFormat::Rgba8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t usage_flags = kTextureDiffuse;
    bool srgb = true;
    bool is_atlas = false;
    bool premultiplied_alpha = false;
    std::string sampler_filter = "linear";  // "nearest" for retro presets
    std::string wrap_mode = "repeat";
    std::vector<MipLevel> mips;
    std::vector<AtlasRegion> atlas_regions;
    std::string import_settings_json;
    std::string optimization_report_json;

    [[nodiscard]] std::uint64_t total_pixel_bytes() const;

    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    [[nodiscard]] core::Result<TextureDocument> deserialize(const std::vector<std::uint8_t>& bytes) const;
    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] std::string to_json() const;

    /// Generates a box-filtered mip chain from mip 0. Returns the generated
    /// levels (excluding mip 0). Real implementation, used by the pipeline's
    /// texture optimizer.
    [[nodiscard]] core::Result<std::vector<MipLevel>> generate_mip_chain() const;

    /// Nearest-neighbour resize of mip 0 (retro pixel-art safe downscaling).
    [[nodiscard]] core::Result<MipLevel> resize_nearest(std::uint32_t new_width,
                                                       std::uint32_t new_height) const;
    /// Bilinear resize of mip 0 (smooth downscaling for photo-style textures).
    [[nodiscard]] core::Result<MipLevel> resize_bilinear(std::uint32_t new_width,
                                                        std::uint32_t new_height) const;
};

}  // namespace bully::formats
