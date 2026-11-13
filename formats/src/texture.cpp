#include "bully/formats/texture.h"

#include <algorithm>
#include <cmath>

#include "bully/core/json.h"

namespace bully::formats {

namespace {

constexpr std::uint32_t kChunkInfo = fourcc("INFO");
constexpr std::uint32_t kChunkMip = fourcc("MIP ");
constexpr std::uint32_t kChunkAtlas = fourcc("ATLS");
constexpr std::uint32_t kChunkMeta = fourcc("META");

struct Rgba {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

std::uint8_t to_byte(float value)
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
}

/// Decodes a pixel from any supported encoding into normalized RGBA.
Rgba decode_pixel(const MipLevel& mip, PixelFormat format, std::uint32_t x, std::uint32_t y)
{
    Rgba out;
    const std::size_t row = static_cast<std::size_t>(y) * mip.stride;
    switch (format) {
        case PixelFormat::Rgba8: {
            const std::size_t offset = row + static_cast<std::size_t>(x) * 4u;
            if (offset + 3 < mip.pixels.size()) {
                out.r = static_cast<float>(mip.pixels[offset]) / 255.0f;
                out.g = static_cast<float>(mip.pixels[offset + 1]) / 255.0f;
                out.b = static_cast<float>(mip.pixels[offset + 2]) / 255.0f;
                out.a = static_cast<float>(mip.pixels[offset + 3]) / 255.0f;
            }
            break;
        }
        case PixelFormat::Rgb8: {
            const std::size_t offset = row + static_cast<std::size_t>(x) * 3u;
            if (offset + 2 < mip.pixels.size()) {
                out.r = static_cast<float>(mip.pixels[offset]) / 255.0f;
                out.g = static_cast<float>(mip.pixels[offset + 1]) / 255.0f;
                out.b = static_cast<float>(mip.pixels[offset + 2]) / 255.0f;
            }
            break;
        }
        case PixelFormat::R8: {
            const std::size_t offset = row + x;
            if (offset < mip.pixels.size()) {
                out.r = out.g = out.b = static_cast<float>(mip.pixels[offset]) / 255.0f;
            }
            break;
        }
        case PixelFormat::Rgba4444: {
            const std::size_t offset = row + static_cast<std::size_t>(x) * 2u;
            if (offset + 1 < mip.pixels.size()) {
                const std::uint16_t value = static_cast<std::uint16_t>(
                    mip.pixels[offset] | (mip.pixels[offset + 1] << 8));
                out.r = static_cast<float>((value >> 12) & 0xF) / 15.0f;
                out.g = static_cast<float>((value >> 8) & 0xF) / 15.0f;
                out.b = static_cast<float>((value >> 4) & 0xF) / 15.0f;
                out.a = static_cast<float>(value & 0xF) / 15.0f;
            }
            break;
        }
        case PixelFormat::Rgb565: {
            const std::size_t offset = row + static_cast<std::size_t>(x) * 2u;
            if (offset + 1 < mip.pixels.size()) {
                const std::uint16_t value = static_cast<std::uint16_t>(
                    mip.pixels[offset] | (mip.pixels[offset + 1] << 8));
                out.r = static_cast<float>((value >> 11) & 0x1F) / 31.0f;
                out.g = static_cast<float>((value >> 5) & 0x3F) / 63.0f;
                out.b = static_cast<float>(value & 0x1F) / 31.0f;
            }
            break;
        }
        default:
            break;
    }
    return out;
}

void encode_pixel(std::vector<std::uint8_t>& pixels, std::size_t offset, PixelFormat format, const Rgba& c)
{
    switch (format) {
        case PixelFormat::Rgba8:
            pixels[offset] = to_byte(c.r);
            pixels[offset + 1] = to_byte(c.g);
            pixels[offset + 2] = to_byte(c.b);
            pixels[offset + 3] = to_byte(c.a);
            break;
        case PixelFormat::Rgb8:
            pixels[offset] = to_byte(c.r);
            pixels[offset + 1] = to_byte(c.g);
            pixels[offset + 2] = to_byte(c.b);
            break;
        case PixelFormat::R8:
            pixels[offset] = to_byte(c.r);
            break;
        case PixelFormat::Rgba4444: {
            const auto value = static_cast<std::uint16_t>(
                ((to_byte(c.r) >> 4) << 12) | ((to_byte(c.g) >> 4) << 8) |
                ((to_byte(c.b) >> 4) << 4) | (to_byte(c.a) >> 4));
            pixels[offset] = static_cast<std::uint8_t>(value & 0xFF);
            pixels[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
            break;
        }
        case PixelFormat::Rgb565: {
            const auto value = static_cast<std::uint16_t>(
                ((to_byte(c.r) >> 3) << 11) | ((to_byte(c.g) >> 2) << 5) | (to_byte(c.b) >> 3));
            pixels[offset] = static_cast<std::uint8_t>(value & 0xFF);
            pixels[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
            break;
        }
        default:
            break;
    }
}

}  // namespace

std::uint32_t bytes_per_pixel(PixelFormat format) noexcept
{
    switch (format) {
        case PixelFormat::Rgba8: return 4;
        case PixelFormat::Rgb8: return 3;
        case PixelFormat::R8: return 1;
        case PixelFormat::Rgba4444: return 2;
        case PixelFormat::Rgb565: return 2;
        default: return 0;
    }
}

std::string_view to_string(PixelFormat format) noexcept
{
    switch (format) {
        case PixelFormat::Rgba8: return "rgba8";
        case PixelFormat::Rgb8: return "rgb8";
        case PixelFormat::R8: return "r8";
        case PixelFormat::Rgba4444: return "rgba4444";
        case PixelFormat::Rgb565: return "rgb565";
        case PixelFormat::Bc1: return "bc1";
        case PixelFormat::Bc3: return "bc3";
        case PixelFormat::Unknown: return "unknown";
    }
    return "unknown";
}

bool is_supported_encoding(PixelFormat format) noexcept
{
    return bytes_per_pixel(format) != 0;
}

std::uint64_t TextureDocument::total_pixel_bytes() const
{
    std::uint64_t total = 0;
    for (const auto& mip : mips) total += static_cast<std::uint64_t>(mip.pixels.size());
    return total;
}

std::vector<std::uint8_t> TextureDocument::serialize() const
{
    BinaryWriter w;
    w.chunk(kChunkInfo, [&](BinaryWriter& body) {
        body.string(asset_id);
        body.string(name);
        body.string(source_path);
        body.string(importer);
        body.u8(static_cast<std::uint8_t>(format));
        body.u32(width);
        body.u32(height);
        body.u32(usage_flags);
        body.boolean(srgb);
        body.boolean(is_atlas);
        body.boolean(premultiplied_alpha);
        body.string(sampler_filter);
        body.string(wrap_mode);
        body.u32(static_cast<std::uint32_t>(mips.size()));
    });
    for (const auto& mip : mips) {
        w.chunk(kChunkMip, [&](BinaryWriter& body) {
            body.u32(mip.width);
            body.u32(mip.height);
            body.u32(mip.stride);
            body.blob(mip.pixels);
        });
    }
    if (!atlas_regions.empty()) {
        w.chunk(kChunkAtlas, [&](BinaryWriter& body) {
            body.u32(static_cast<std::uint32_t>(atlas_regions.size()));
            for (const auto& region : atlas_regions) {
                body.string(region.name);
                body.u32(region.x);
                body.u32(region.y);
                body.u32(region.width);
                body.u32(region.height);
                body.f32(region.u0);
                body.f32(region.v0);
                body.f32(region.u1);
                body.f32(region.v1);
            }
        });
    }
    w.chunk(kChunkMeta, [&](BinaryWriter& body) {
        body.string(import_settings_json);
        body.string(optimization_report_json);
    });
    return encode_document(kTextureFormat, 0, w.bytes());
}

core::Result<TextureDocument> TextureDocument::deserialize(const std::vector<std::uint8_t>& bytes) const
{
    core::DiagnosticList diagnostics;
    auto payload = decode_document(kTextureFormat, bytes);
    if (!payload) return core::Result<TextureDocument>(payload.diagnostics());
    diagnostics.append(payload.diagnostics());

    TextureDocument document;
    BinaryReader reader(payload.value().data(), payload.value().size(), &diagnostics);
    while (reader.ok()) {
        std::uint32_t chunk_id = 0;
        std::uint32_t chunk_size = 0;
        if (!reader.next_chunk(chunk_id, chunk_size)) break;
        const std::size_t chunk_end = reader.position() + chunk_size;
        if (chunk_id == kChunkInfo) {
            document.asset_id = reader.string();
            document.name = reader.string();
            document.source_path = reader.string();
            document.importer = reader.string();
            document.format = static_cast<PixelFormat>(reader.u8());
            document.width = reader.u32();
            document.height = reader.u32();
            document.usage_flags = reader.u32();
            document.srgb = reader.boolean();
            document.is_atlas = reader.boolean();
            document.premultiplied_alpha = reader.boolean();
            document.sampler_filter = reader.string();
            document.wrap_mode = reader.string();
            (void)reader.u32();  // declared mip count
        } else if (chunk_id == kChunkMip) {
            MipLevel mip;
            mip.width = reader.u32();
            mip.height = reader.u32();
            mip.stride = reader.u32();
            mip.pixels = reader.blob();
            document.mips.push_back(std::move(mip));
        } else if (chunk_id == kChunkAtlas) {
            const std::uint32_t count = reader.u32();
            document.atlas_regions.resize(std::min<std::uint32_t>(count, 4096));
            for (auto& region : document.atlas_regions) {
                region.name = reader.string();
                region.x = reader.u32();
                region.y = reader.u32();
                region.width = reader.u32();
                region.height = reader.u32();
                region.u0 = reader.f32();
                region.v0 = reader.f32();
                region.u1 = reader.f32();
                region.v1 = reader.f32();
            }
        } else if (chunk_id == kChunkMeta) {
            document.import_settings_json = reader.string();
            document.optimization_report_json = reader.string();
        } else {
            diagnostics.info("BULLY-TEX-0001",
                             "skipping unknown texture chunk '" + fourcc_to_string(chunk_id) + "'");
        }
        if (reader.ok()) reader.seek(chunk_end);
    }
    if (!reader.ok() || diagnostics.has_errors()) {
        if (diagnostics.empty()) diagnostics.error("BULLY-TEX-0002", "failed to decode texture");
        return core::Result<TextureDocument>(std::move(diagnostics));
    }
    auto decoded = core::Result<TextureDocument>(std::move(document));
    // Warnings collected while decoding (unknown chunks, newer minor versions)
    // travel with the successful result so callers can surface them.
    decoded.attach_diagnostics(std::move(diagnostics));
    return decoded;
}

core::Status TextureDocument::validate() const
{
    core::DiagnosticList diagnostics;
    if (asset_id.empty()) diagnostics.error("BULLY-TEX-0003", "texture has no asset id");
    if (width == 0 || height == 0) {
        diagnostics.error("BULLY-TEX-0004", "texture dimensions must be non-zero", name);
    }
    if (!is_supported_encoding(format)) {
        diagnostics.error("BULLY-TEX-0005",
                          "pixel format '" + std::string(to_string(format)) +
                              "' is not decodable by this runtime",
                          name);
    }
    if (mips.empty()) {
        diagnostics.error("BULLY-TEX-0006", "texture has no mip levels", name);
    } else {
        const auto bpp = bytes_per_pixel(format);
        for (std::size_t i = 0; i < mips.size(); ++i) {
            const auto& mip = mips[i];
            if (mip.width == 0 || mip.height == 0) {
                diagnostics.error("BULLY-TEX-0007",
                                  "mip " + std::to_string(i) + " has zero dimensions", name);
                continue;
            }
            if (mip.stride < static_cast<std::uint64_t>(mip.width) * bpp) {
                diagnostics.error("BULLY-TEX-0008",
                                  "mip " + std::to_string(i) + " row stride is too small", name);
            }
            const auto required = static_cast<std::uint64_t>(mip.stride) * mip.height;
            if (mip.pixels.size() < required) {
                diagnostics.error("BULLY-TEX-0009",
                                  "mip " + std::to_string(i) + " payload is truncated", name);
            }
            if (i > 0) {
                const auto& previous = mips[i - 1];
                const auto expected_w = std::max<std::uint32_t>(1, previous.width / 2);
                const auto expected_h = std::max<std::uint32_t>(1, previous.height / 2);
                if (mip.width != expected_w || mip.height != expected_h) {
                    diagnostics.warn("BULLY-TEX-0010",
                                     "mip " + std::to_string(i) + " dimensions are not half of mip " +
                                         std::to_string(i - 1),
                                     name);
                }
            }
        }
        if (mips.front().width != width || mips.front().height != height) {
            diagnostics.warn("BULLY-TEX-0011", "mip 0 dimensions differ from INFO dimensions", name);
        }
    }
    for (const auto& region : atlas_regions) {
        if (region.width == 0 || region.height == 0) {
            diagnostics.warn("BULLY-TEX-0012", "atlas region '" + region.name + "' is empty", name);
        }
        if (static_cast<std::uint64_t>(region.x) + region.width > width ||
            static_cast<std::uint64_t>(region.y) + region.height > height) {
            diagnostics.error("BULLY-TEX-0013",
                              "atlas region '" + region.name + "' exceeds texture bounds", name);
        }
    }
    if (diagnostics.has_errors()) return core::Status(std::move(diagnostics));
    // Warnings are part of the report even when the document is valid.
    auto status = core::Status{};
    status.attach_diagnostics(std::move(diagnostics));
    return status;
}

std::string TextureDocument::to_json() const
{
    using core::json::Value;
    Value root = Value::make_object();
    root["format"] = "btexture";
    root["version"] = kTextureFormat.version.to_string();
    root["asset_id"] = asset_id;
    root["name"] = name;
    root["source_path"] = source_path;
    root["pixel_format"] = std::string(to_string(format));
    root["width"] = static_cast<std::uint64_t>(width);
    root["height"] = static_cast<std::uint64_t>(height);
    root["mip_count"] = static_cast<std::uint64_t>(mips.size());
    root["is_atlas"] = is_atlas;
    root["sampler_filter"] = sampler_filter;
    root["wrap_mode"] = wrap_mode;
    root["pixel_bytes"] = total_pixel_bytes();
    root["estimated_vram_kb"] = static_cast<std::uint64_t>((total_pixel_bytes() + 512) / 1024);
    Value regions = Value::make_array();
    for (const auto& region : atlas_regions) {
        Value entry = Value::make_object();
        entry["name"] = region.name;
        entry["x"] = static_cast<std::uint64_t>(region.x);
        entry["y"] = static_cast<std::uint64_t>(region.y);
        entry["width"] = static_cast<std::uint64_t>(region.width);
        entry["height"] = static_cast<std::uint64_t>(region.height);
        regions.push(std::move(entry));
    }
    root["atlas_regions"] = std::move(regions);
    return root.dump(2);
}

core::Result<std::vector<MipLevel>> TextureDocument::generate_mip_chain() const
{
    core::DiagnosticList diagnostics;
    if (mips.empty()) {
        diagnostics.error("BULLY-TEX-0014", "cannot generate mips without a base level", name);
        return core::Result<std::vector<MipLevel>>(std::move(diagnostics));
    }
    if (!is_supported_encoding(format)) {
        diagnostics.error("BULLY-TEX-0015", "cannot generate mips for compressed formats", name);
        return core::Result<std::vector<MipLevel>>(std::move(diagnostics));
    }
    const auto bpp = bytes_per_pixel(format);
    std::vector<MipLevel> chain;
    MipLevel current = mips.front();
    while (current.width > 1 || current.height > 1) {
        MipLevel next;
        next.width = std::max<std::uint32_t>(1, current.width / 2);
        next.height = std::max<std::uint32_t>(1, current.height / 2);
        next.stride = next.width * bpp;
        next.pixels.assign(static_cast<std::size_t>(next.stride) * next.height, 0);
        for (std::uint32_t y = 0; y < next.height; ++y) {
            for (std::uint32_t x = 0; x < next.width; ++x) {
                Rgba sum;
                int count = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        const auto sx = std::min(current.width - 1, x * 2 + static_cast<std::uint32_t>(dx));
                        const auto sy = std::min(current.height - 1, y * 2 + static_cast<std::uint32_t>(dy));
                        const Rgba sample = decode_pixel(current, format, sx, sy);
                        sum.r += sample.r;
                        sum.g += sample.g;
                        sum.b += sample.b;
                        sum.a += sample.a;
                        ++count;
                    }
                }
                const auto divisor = static_cast<float>(count);
                sum.r /= divisor;
                sum.g /= divisor;
                sum.b /= divisor;
                sum.a /= divisor;
                const std::size_t offset = static_cast<std::size_t>(y) * next.stride +
                                           static_cast<std::size_t>(x) * bpp;
                encode_pixel(next.pixels, offset, format, sum);
            }
        }
        chain.push_back(next);
        current = std::move(next);
    }
    return chain;
}

core::Result<MipLevel> TextureDocument::resize_nearest(std::uint32_t new_width,
                                                        std::uint32_t new_height) const
{
    core::DiagnosticList diagnostics;
    if (mips.empty() || !is_supported_encoding(format) || new_width == 0 || new_height == 0) {
        diagnostics.error("BULLY-TEX-0016", "invalid resize request", name);
        return core::Result<MipLevel>(std::move(diagnostics));
    }
    const auto bpp = bytes_per_pixel(format);
    const MipLevel& source = mips.front();
    MipLevel out;
    out.width = new_width;
    out.height = new_height;
    out.stride = new_width * bpp;
    out.pixels.assign(static_cast<std::size_t>(out.stride) * new_height, 0);
    for (std::uint32_t y = 0; y < new_height; ++y) {
        for (std::uint32_t x = 0; x < new_width; ++x) {
            const auto sx = std::min(source.width - 1,
                                     static_cast<std::uint32_t>(static_cast<std::uint64_t>(x) *
                                                                source.width / new_width));
            const auto sy = std::min(source.height - 1,
                                     static_cast<std::uint32_t>(static_cast<std::uint64_t>(y) *
                                                                source.height / new_height));
            const Rgba sample = decode_pixel(source, format, sx, sy);
            encode_pixel(out.pixels, static_cast<std::size_t>(y) * out.stride +
                                         static_cast<std::size_t>(x) * bpp,
                         format, sample);
        }
    }
    return out;
}

core::Result<MipLevel> TextureDocument::resize_bilinear(std::uint32_t new_width,
                                                         std::uint32_t new_height) const
{
    core::DiagnosticList diagnostics;
    if (mips.empty() || !is_supported_encoding(format) || new_width == 0 || new_height == 0) {
        diagnostics.error("BULLY-TEX-0017", "invalid resize request", name);
        return core::Result<MipLevel>(std::move(diagnostics));
    }
    const auto bpp = bytes_per_pixel(format);
    const MipLevel& source = mips.front();
    MipLevel out;
    out.width = new_width;
    out.height = new_height;
    out.stride = new_width * bpp;
    out.pixels.assign(static_cast<std::size_t>(out.stride) * new_height, 0);
    for (std::uint32_t y = 0; y < new_height; ++y) {
        for (std::uint32_t x = 0; x < new_width; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) * static_cast<float>(source.width) /
                                 static_cast<float>(new_width) -
                             0.5f;
            const float fy = (static_cast<float>(y) + 0.5f) * static_cast<float>(source.height) /
                                 static_cast<float>(new_height) -
                             0.5f;
            const auto x0 = static_cast<std::uint32_t>(std::max(0.0f, std::floor(fx)));
            const auto y0 = static_cast<std::uint32_t>(std::max(0.0f, std::floor(fy)));
            const auto x1 = std::min(source.width - 1, x0 + 1);
            const auto y1 = std::min(source.height - 1, y0 + 1);
            const float tx = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f);
            const float ty = std::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);
            const Rgba p00 = decode_pixel(source, format, x0, y0);
            const Rgba p10 = decode_pixel(source, format, x1, y0);
            const Rgba p01 = decode_pixel(source, format, x0, y1);
            const Rgba p11 = decode_pixel(source, format, x1, y1);
            auto mix = [tx, ty](float a, float b, float c, float d) {
                const float top = a + (b - a) * tx;
                const float bottom = c + (d - c) * tx;
                return top + (bottom - top) * ty;
            };
            Rgba result;
            result.r = mix(p00.r, p10.r, p01.r, p11.r);
            result.g = mix(p00.g, p10.g, p01.g, p11.g);
            result.b = mix(p00.b, p10.b, p01.b, p11.b);
            result.a = mix(p00.a, p10.a, p01.a, p11.a);
            encode_pixel(out.pixels, static_cast<std::size_t>(y) * out.stride +
                                         static_cast<std::size_t>(x) * bpp,
                         format, result);
        }
    }
    return out;
}

}  // namespace bully::formats
