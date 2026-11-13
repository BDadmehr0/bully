// Texture exporters: .btexture -> RGBA8 -> PNG / PPM.
#include <algorithm>
#include <cstring>

#include "bully/core/fs.h"
#include "bully/formats/binary.h"
#include "bully/exporters/exporters.h"

namespace bully::exporters {

namespace {

void append_u32_be(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void append_chunk(std::vector<std::uint8_t>& out, const char (&type)[5],
                  const std::vector<std::uint8_t>& body)
{
    append_u32_be(out, static_cast<std::uint32_t>(body.size()));
    const std::size_t start = out.size();
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(type[i]));
    out.insert(out.end(), body.begin(), body.end());
    const std::uint32_t crc = formats::crc32(out.data() + start, out.size() - start);
    append_u32_be(out, crc);
}

/// Uncompressed deflate (RFC 1951 stored blocks) wrapped in a zlib header. Real
/// compression would need a deflate encoder; a stored stream is valid PNG and
/// keeps the exporter dependency-free and byte-deterministic, which is what the
/// round-trip tests rely on.
std::vector<std::uint8_t> zlib_stored(const std::vector<std::uint8_t>& raw)
{
    std::vector<std::uint8_t> out;
    out.push_back(0x78);  // CMF: deflate, 32K window
    out.push_back(0x01);  // FLG: no dictionary, fastest
    std::size_t offset = 0;
    constexpr std::size_t kMaxBlock = 65535;
    while (offset < raw.size()) {
        const std::size_t block = std::min(kMaxBlock, raw.size() - offset);
        const bool last = offset + block >= raw.size();
        out.push_back(last ? 1 : 0);
        // LEN then its one's complement, little-endian.
        out.push_back(static_cast<std::uint8_t>(block & 0xFF));
        out.push_back(static_cast<std::uint8_t>((block >> 8) & 0xFF));
        const std::uint16_t nlen = static_cast<std::uint16_t>(~block);
        out.push_back(static_cast<std::uint8_t>(nlen & 0xFF));
        out.push_back(static_cast<std::uint8_t>((nlen >> 8) & 0xFF));
        out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                   raw.begin() + static_cast<std::ptrdiff_t>(offset + block));
        offset += block;
    }
    if (raw.empty()) out.push_back(1);  // empty final block
    // Adler-32 of the uncompressed data.
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const auto byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    const std::uint32_t adler = (b << 16) | a;
    out.push_back(static_cast<std::uint8_t>((adler >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((adler >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((adler >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(adler & 0xFF));
    return out;
}

std::uint8_t quantise_4bit(std::uint8_t value)
{
    // Rgba4444 stores 4 bits per channel; expand back so the exported image
    // looks like what the runtime shows, not like the raw nibble.
    const std::uint8_t nibble = static_cast<std::uint8_t>(value >> 4);
    return static_cast<std::uint8_t>((nibble << 4) | nibble);
}

std::uint8_t expand_5bit(std::uint16_t value)
{
    const std::uint16_t five = static_cast<std::uint16_t>(value & 0x1F);
    return static_cast<std::uint8_t>((five << 3) | (five >> 2));
}

std::uint8_t expand_6bit(std::uint16_t value)
{
    const std::uint16_t six = static_cast<std::uint16_t>(value & 0x3F);
    return static_cast<std::uint8_t>((six << 2) | (six >> 4));
}

}  // namespace

core::Result<RgbaImage> to_rgba(const formats::TextureDocument& texture)
{
    core::DiagnosticList diagnostics;
    if (texture.mips.empty()) {
        diagnostics.error("BULLY-EXP-0001", "texture has no mip levels", texture.name);
        return core::Result<RgbaImage>(std::move(diagnostics));
    }
    const auto& mip = texture.mips.front();
    if (mip.width == 0 || mip.height == 0) {
        diagnostics.error("BULLY-EXP-0001", "texture base level has zero dimensions",
                          texture.name);
        return core::Result<RgbaImage>(std::move(diagnostics));
    }
    RgbaImage image;
    image.width = mip.width;
    image.height = mip.height;
    image.pixels.resize(static_cast<std::size_t>(mip.width) * mip.height * 4);

    const std::size_t pixels = static_cast<std::size_t>(mip.width) * mip.height;
    const std::size_t row_stride = mip.stride != 0 ? mip.stride : 0;
    switch (texture.format) {
        case formats::PixelFormat::Rgba8: {
            const std::size_t stride = row_stride != 0 ? row_stride : pixels / mip.height * 4;
            if (mip.pixels.size() < stride * mip.height) {
                diagnostics.error("BULLY-EXP-0003", "texture payload is shorter than its stride",
                                  texture.name);
                return core::Result<RgbaImage>(std::move(diagnostics));
            }
            for (std::uint32_t y = 0; y < mip.height; ++y) {
                std::memcpy(image.pixels.data() + static_cast<std::size_t>(y) * mip.width * 4,
                            mip.pixels.data() + static_cast<std::size_t>(y) * stride,
                            static_cast<std::size_t>(mip.width) * 4);
            }
            break;
        }
        case formats::PixelFormat::Rgb8: {
            const std::size_t stride = row_stride != 0 ? row_stride : pixels / mip.height * 3;
            if (mip.pixels.size() < stride * mip.height) {
                diagnostics.error("BULLY-EXP-0003", "texture payload is shorter than its stride",
                                  texture.name);
                return core::Result<RgbaImage>(std::move(diagnostics));
            }
            for (std::uint32_t y = 0; y < mip.height; ++y) {
                for (std::uint32_t x = 0; x < mip.width; ++x) {
                    const auto* src = mip.pixels.data() + static_cast<std::size_t>(y) * stride +
                                      static_cast<std::size_t>(x) * 3;
                    auto* dst = image.pixels.data() +
                                (static_cast<std::size_t>(y) * mip.width + x) * 4;
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = 255;
                }
            }
            break;
        }
        case formats::PixelFormat::R8: {
            const std::size_t stride = row_stride != 0 ? row_stride : pixels / mip.height;
            for (std::uint32_t y = 0; y < mip.height; ++y) {
                for (std::uint32_t x = 0; x < mip.width; ++x) {
                    const std::size_t index = static_cast<std::size_t>(y) * stride + x;
                    const std::uint8_t value = index < mip.pixels.size() ? mip.pixels[index] : 0;
                    auto* dst = image.pixels.data() +
                                (static_cast<std::size_t>(y) * mip.width + x) * 4;
                    dst[0] = value;
                    dst[1] = value;
                    dst[2] = value;
                    dst[3] = 255;
                }
            }
            break;
        }
        case formats::PixelFormat::Rgba4444: {
            const std::size_t stride = row_stride != 0 ? row_stride : pixels / mip.height * 2;
            for (std::uint32_t y = 0; y < mip.height; ++y) {
                for (std::uint32_t x = 0; x < mip.width; ++x) {
                    const std::size_t offset = static_cast<std::size_t>(y) * stride +
                                               static_cast<std::size_t>(x) * 2;
                    if (offset + 1 >= mip.pixels.size()) continue;
                    const std::uint16_t word = static_cast<std::uint16_t>(
                        mip.pixels[offset] | (mip.pixels[offset + 1] << 8));
                    auto* dst = image.pixels.data() +
                                (static_cast<std::size_t>(y) * mip.width + x) * 4;
                    dst[0] = quantise_4bit(static_cast<std::uint8_t>((word >> 12) & 0xF));
                    dst[1] = quantise_4bit(static_cast<std::uint8_t>((word >> 8) & 0xF));
                    dst[2] = quantise_4bit(static_cast<std::uint8_t>((word >> 4) & 0xF));
                    dst[3] = quantise_4bit(static_cast<std::uint8_t>(word & 0xF));
                }
            }
            break;
        }
        case formats::PixelFormat::Rgb565: {
            const std::size_t stride = row_stride != 0 ? row_stride : pixels / mip.height * 2;
            for (std::uint32_t y = 0; y < mip.height; ++y) {
                for (std::uint32_t x = 0; x < mip.width; ++x) {
                    const std::size_t offset = static_cast<std::size_t>(y) * stride +
                                               static_cast<std::size_t>(x) * 2;
                    if (offset + 1 >= mip.pixels.size()) continue;
                    const std::uint16_t word = static_cast<std::uint16_t>(
                        mip.pixels[offset] | (mip.pixels[offset + 1] << 8));
                    auto* dst = image.pixels.data() +
                                (static_cast<std::size_t>(y) * mip.width + x) * 4;
                    dst[0] = expand_5bit(static_cast<std::uint16_t>((word >> 11) & 0x1F));
                    dst[1] = expand_6bit(static_cast<std::uint16_t>((word >> 5) & 0x3F));
                    dst[2] = expand_5bit(static_cast<std::uint16_t>(word & 0x1F));
                    dst[3] = 255;
                }
            }
            break;
        }
        case formats::PixelFormat::Bc1:
        case formats::PixelFormat::Bc3:
        case formats::PixelFormat::Unknown:
            diagnostics.error("BULLY-EXP-0002",
                              "cannot decode " + std::string(formats::to_string(texture.format)) +
                                  " pixels; export requires an uncompressed format",
                              texture.name);
            return core::Result<RgbaImage>(std::move(diagnostics));
    }
    return image;
}

core::Result<std::vector<std::uint8_t>> encode_png(const RgbaImage& image)
{
    core::DiagnosticList diagnostics;
    if (!image.valid()) {
        diagnostics.error("BULLY-EXP-0004", "invalid RGBA image for PNG encoding");
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    // Raw scanlines: one filter byte (0 = None) per row, then RGBA pixels.
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(image.height) * (image.width * 4 + 1));
    for (std::uint32_t y = 0; y < image.height; ++y) {
        raw.push_back(0);
        const auto* row = image.pixels.data() + static_cast<std::size_t>(y) * image.width * 4;
        raw.insert(raw.end(), row, row + static_cast<std::size_t>(image.width) * 4);
    }

    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> ihdr;
    append_u32_be(ihdr, image.width);
    append_u32_be(ihdr, image.height);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // colour type: RGBA
    ihdr.push_back(0);  // deflate
    ihdr.push_back(0);  // adaptive filtering
    ihdr.push_back(0);  // no interlace
    append_chunk(out, "IHDR", ihdr);
    append_chunk(out, "IDAT", zlib_stored(raw));
    append_chunk(out, "IEND", {});
    return out;
}

std::vector<std::uint8_t> encode_ppm(const RgbaImage& image)
{
    const std::string header = "P6\n" + std::to_string(image.width) + " " +
                               std::to_string(image.height) + "\n255\n";
    std::vector<std::uint8_t> out(header.begin(), header.end());
    out.reserve(out.size() + static_cast<std::size_t>(image.width) * image.height * 3);
    for (std::size_t i = 0; i < static_cast<std::size_t>(image.width) * image.height; ++i) {
        out.push_back(image.pixels[i * 4]);
        out.push_back(image.pixels[i * 4 + 1]);
        out.push_back(image.pixels[i * 4 + 2]);
    }
    return out;
}

}  // namespace bully::exporters
