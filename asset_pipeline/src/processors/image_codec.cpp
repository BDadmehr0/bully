// Image decoding: PNG (via the in-tree inflate), BMP, TGA, PPM.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "bully/core/strings.h"
#include "bully/pipeline/image_codec.h"

namespace bully::pipeline {

namespace {

std::uint32_t read_be32(const std::uint8_t* data)
{
    return (static_cast<std::uint32_t>(data[0]) << 24) |
           (static_cast<std::uint32_t>(data[1]) << 16) |
           (static_cast<std::uint32_t>(data[2]) << 8) | static_cast<std::uint32_t>(data[3]);
}

std::uint32_t read_le32(const std::uint8_t* data)
{
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
}

std::uint16_t read_le16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

int paeth_predictor(int a, int b, int c)
{
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

}  // namespace

core::Result<ImageData> decode_png(const std::vector<std::uint8_t>& bytes)
{
    core::DiagnosticList diagnostics;
    static constexpr std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kSignature, 8) != 0) {
        diagnostics.error("BULLY-IMG-0010", "file is not a PNG image");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    std::size_t offset = 8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t bit_depth = 0;
    std::uint8_t colour_type = 0;
    std::uint8_t interlace = 0;
    std::vector<std::uint8_t> palette;
    std::vector<std::uint8_t> transparency;
    std::vector<std::uint8_t> idat;

    while (offset + 8 <= bytes.size()) {
        const std::uint32_t length = read_be32(bytes.data() + offset);
        const std::string type(reinterpret_cast<const char*>(bytes.data() + offset + 4), 4);
        const std::size_t body = offset + 8;
        if (body + length + 4 > bytes.size()) {
            diagnostics.error("BULLY-IMG-0011", "PNG chunk '" + type + "' is truncated");
            return core::Result<ImageData>(std::move(diagnostics));
        }
        if (type == "IHDR") {
            if (length < 13) {
                diagnostics.error("BULLY-IMG-0012", "PNG IHDR chunk is too small");
                return core::Result<ImageData>(std::move(diagnostics));
            }
            width = read_be32(bytes.data() + body);
            height = read_be32(bytes.data() + body + 4);
            bit_depth = bytes[body + 8];
            colour_type = bytes[body + 9];
            interlace = bytes[body + 12];
        } else if (type == "PLTE") {
            palette.assign(bytes.begin() + static_cast<std::ptrdiff_t>(body),
                           bytes.begin() + static_cast<std::ptrdiff_t>(body + length));
        } else if (type == "tRNS") {
            transparency.assign(bytes.begin() + static_cast<std::ptrdiff_t>(body),
                                bytes.begin() + static_cast<std::ptrdiff_t>(body + length));
        } else if (type == "IDAT") {
            idat.insert(idat.end(), bytes.begin() + static_cast<std::ptrdiff_t>(body),
                        bytes.begin() + static_cast<std::ptrdiff_t>(body + length));
        } else if (type == "IEND") {
            break;
        }
        offset = body + length + 4;  // skip CRC
    }
    if (width == 0 || height == 0) {
        diagnostics.error("BULLY-IMG-0013", "PNG has no valid IHDR dimensions");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    if (width > 16384 || height > 16384) {
        diagnostics.error("BULLY-IMG-0014", "PNG dimensions exceed the 16384 pixel limit");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    if (interlace != 0) {
        diagnostics.error("BULLY-IMG-0015",
                          "interlaced (Adam7) PNG is not supported; re-export without interlacing");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    if (idat.empty()) {
        diagnostics.error("BULLY-IMG-0016", "PNG contains no image data");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    int channels = 0;
    switch (colour_type) {
        case 0: channels = 1; break;  // greyscale
        case 2: channels = 3; break;  // truecolour
        case 3: channels = 1; break;  // palette
        case 4: channels = 2; break;  // greyscale + alpha
        case 6: channels = 4; break;  // truecolour + alpha
        default:
            diagnostics.error("BULLY-IMG-0017", "unsupported PNG colour type");
            return core::Result<ImageData>(std::move(diagnostics));
    }
    if (bit_depth != 1 && bit_depth != 2 && bit_depth != 4 && bit_depth != 8 && bit_depth != 16) {
        diagnostics.error("BULLY-IMG-0018", "unsupported PNG bit depth");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    const std::size_t bits_per_pixel =
        static_cast<std::size_t>(channels) * static_cast<std::size_t>(bit_depth);
    const std::size_t row_bytes = (static_cast<std::size_t>(width) * bits_per_pixel + 7) / 8;
    const std::size_t expected = (row_bytes + 1) * height;

    auto raw = inflate_zlib(idat.data(), idat.size(), expected);
    if (!raw) return core::Result<ImageData>(raw.diagnostics());
    if (raw.value().size() < expected) {
        diagnostics.error("BULLY-IMG-0019",
                          "PNG image data is shorter than expected (" +
                              std::to_string(raw.value().size()) + " of " +
                              std::to_string(expected) + " bytes)");
        return core::Result<ImageData>(std::move(diagnostics));
    }

    // Un-filter scanlines in place.
    std::vector<std::uint8_t> pixels(expected);
    const std::size_t filter_stride = row_bytes;
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t filter = raw.value()[y * (row_bytes + 1)];
        const std::uint8_t* source = raw.value().data() + y * (row_bytes + 1) + 1;
        std::uint8_t* target = pixels.data() + y * row_bytes;
        const std::uint8_t* previous = y > 0 ? pixels.data() + (y - 1) * row_bytes : nullptr;
        for (std::size_t x = 0; x < row_bytes; ++x) {
            const int left = x >= filter_stride ? target[x - filter_stride] : 0;
            const int up = previous != nullptr ? previous[x] : 0;
            const int up_left = (previous != nullptr && x >= filter_stride)
                                    ? previous[x - filter_stride]
                                    : 0;
            int value = source[x];
            switch (filter) {
                case 0: break;
                case 1: value += left; break;
                case 2: value += up; break;
                case 3: value += (left + up) / 2; break;
                case 4: value += paeth_predictor(left, up, up_left); break;
                default:
                    diagnostics.error("BULLY-IMG-0020", "invalid PNG scanline filter");
                    return core::Result<ImageData>(std::move(diagnostics));
            }
            target[x] = static_cast<std::uint8_t>(value & 0xFF);
        }
    }

    ImageData image;
    image.width = width;
    image.height = height;
    image.format_name = "png";
    image.pixels.resize(static_cast<std::size_t>(width) * height * 4, 255);

    auto sample_bits = [&](std::size_t row_base, std::uint32_t x, int channel) -> std::uint32_t {
        const std::size_t bit_index =
            static_cast<std::size_t>(x) * bits_per_pixel + static_cast<std::size_t>(channel) * bit_depth;
        const std::size_t byte_index = row_base + bit_index / 8;
        if (byte_index >= pixels.size()) return 0;
        std::uint32_t value = pixels[byte_index];
        if (bit_depth == 16) {
            const std::uint32_t high = pixels[byte_index];
            const std::uint32_t low = byte_index + 1 < pixels.size() ? pixels[byte_index + 1] : 0;
            return (high << 8) | low;
        }
        if (bit_depth == 8) return value;
        if (bit_depth == 4) {
            const int shift = static_cast<int>((bit_index % 8));
            return (value >> (4 - shift)) & 0x0Fu;
        }
        if (bit_depth == 2) {
            const int shift = static_cast<int>(bit_index % 8);
            return (value >> (6 - shift)) & 0x03u;
        }
        const int shift = static_cast<int>(bit_index % 8);
        return (value >> (7 - shift)) & 0x01u;
    };
    auto to_byte = [&](std::uint32_t value) -> std::uint8_t {
        if (bit_depth == 16) return static_cast<std::uint8_t>(value >> 8);
        const std::uint32_t max_value = (1u << bit_depth) - 1u;
        if (max_value == 0) return 0;
        return static_cast<std::uint8_t>((value * 255u) / max_value);
    };

    for (std::uint32_t y = 0; y < height; ++y) {
        const std::size_t row_base = static_cast<std::size_t>(y) * row_bytes;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t destination = (static_cast<std::size_t>(y) * width + x) * 4;
            switch (colour_type) {
                case 0: {  // greyscale
                    const std::uint8_t grey = to_byte(sample_bits(row_base, x, 0));
                    image.pixels[destination] = grey;
                    image.pixels[destination + 1] = grey;
                    image.pixels[destination + 2] = grey;
                    image.pixels[destination + 3] =
                        transparency.size() >= 2 && grey == transparency[1] ? 0 : 255;
                    break;
                }
                case 2: {  // truecolour
                    image.pixels[destination] = to_byte(sample_bits(row_base, x, 0));
                    image.pixels[destination + 1] = to_byte(sample_bits(row_base, x, 1));
                    image.pixels[destination + 2] = to_byte(sample_bits(row_base, x, 2));
                    image.pixels[destination + 3] = 255;
                    break;
                }
                case 3: {  // palette
                    const std::uint32_t index = sample_bits(row_base, x, 0);
                    const std::size_t palette_offset = static_cast<std::size_t>(index) * 3;
                    if (palette_offset + 2 < palette.size()) {
                        image.pixels[destination] = palette[palette_offset];
                        image.pixels[destination + 1] = palette[palette_offset + 1];
                        image.pixels[destination + 2] = palette[palette_offset + 2];
                    } else {
                        image.pixels[destination] = 0;
                        image.pixels[destination + 1] = 0;
                        image.pixels[destination + 2] = 0;
                    }
                    image.pixels[destination + 3] = index < transparency.size() ? transparency[index] : 255;
                    break;
                }
                case 4: {  // grey + alpha
                    const std::uint8_t grey = to_byte(sample_bits(row_base, x, 0));
                    image.pixels[destination] = grey;
                    image.pixels[destination + 1] = grey;
                    image.pixels[destination + 2] = grey;
                    image.pixels[destination + 3] = to_byte(sample_bits(row_base, x, 1));
                    break;
                }
                default: {  // RGBA
                    image.pixels[destination] = to_byte(sample_bits(row_base, x, 0));
                    image.pixels[destination + 1] = to_byte(sample_bits(row_base, x, 1));
                    image.pixels[destination + 2] = to_byte(sample_bits(row_base, x, 2));
                    image.pixels[destination + 3] = to_byte(sample_bits(row_base, x, 3));
                    break;
                }
            }
        }
    }
    return image;
}

core::Result<ImageData> decode_bmp(const std::vector<std::uint8_t>& bytes)
{
    core::DiagnosticList diagnostics;
    if (bytes.size() < 54 || bytes[0] != 'B' || bytes[1] != 'M') {
        diagnostics.error("BULLY-IMG-0030", "file is not a BMP image");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    const std::uint32_t pixel_offset = read_le32(bytes.data() + 10);
    const std::uint32_t header_size = read_le32(bytes.data() + 14);
    if (header_size < 40) {
        diagnostics.error("BULLY-IMG-0031", "only BITMAPINFOHEADER BMP files are supported");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    const auto width = static_cast<std::int32_t>(read_le32(bytes.data() + 18));
    const auto height_raw = static_cast<std::int32_t>(read_le32(bytes.data() + 22));
    const std::uint16_t bits_per_pixel = read_le16(bytes.data() + 28);
    const std::uint32_t compression = read_le32(bytes.data() + 30);
    if (compression != 0) {
        diagnostics.error("BULLY-IMG-0032", "compressed BMP files are not supported");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    if (width <= 0 || height_raw == 0) {
        diagnostics.error("BULLY-IMG-0033", "BMP has invalid dimensions");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    const bool bottom_up = height_raw > 0;
    const auto height = static_cast<std::uint32_t>(bottom_up ? height_raw : -height_raw);
    if (bits_per_pixel != 24 && bits_per_pixel != 32 && bits_per_pixel != 8) {
        diagnostics.error("BULLY-IMG-0034", "unsupported BMP bit depth");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    std::vector<std::uint8_t> palette;
    if (bits_per_pixel == 8) {
        const std::uint32_t palette_entries = read_le32(bytes.data() + 46);
        const std::size_t palette_start = 14u + header_size;
        const std::uint32_t count = palette_entries == 0 ? 256 : palette_entries;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t entry = palette_start + static_cast<std::size_t>(i) * 4;
            if (entry + 3 >= bytes.size()) break;
            palette.push_back(bytes[entry + 2]);
            palette.push_back(bytes[entry + 1]);
            palette.push_back(bytes[entry]);
        }
    }
    const std::size_t row_stride =
        ((static_cast<std::size_t>(width) * bits_per_pixel + 31) / 32) * 4;
    if (pixel_offset + row_stride * height > bytes.size()) {
        diagnostics.error("BULLY-IMG-0035", "BMP pixel data is truncated");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    ImageData image;
    image.width = static_cast<std::uint32_t>(width);
    image.height = height;
    image.format_name = "bmp";
    image.pixels.resize(static_cast<std::size_t>(width) * height * 4, 255);
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t source_y = bottom_up ? height - 1 - y : y;
        const std::uint8_t* row = bytes.data() + pixel_offset + source_y * row_stride;
        for (std::uint32_t x = 0; x < static_cast<std::uint32_t>(width); ++x) {
            const std::size_t destination = (static_cast<std::size_t>(y) * static_cast<std::uint32_t>(width) + x) * 4;
            if (bits_per_pixel == 8) {
                const std::size_t index = static_cast<std::size_t>(row[x]) * 3;
                if (index + 2 < palette.size()) {
                    image.pixels[destination] = palette[index];
                    image.pixels[destination + 1] = palette[index + 1];
                    image.pixels[destination + 2] = palette[index + 2];
                }
            } else {
                const std::size_t source = static_cast<std::size_t>(x) * (bits_per_pixel / 8u);
                image.pixels[destination] = row[source + 2];
                image.pixels[destination + 1] = row[source + 1];
                image.pixels[destination + 2] = row[source];
                if (bits_per_pixel == 32) image.pixels[destination + 3] = row[source + 3];
            }
        }
    }
    return image;
}

core::Result<ImageData> decode_tga(const std::vector<std::uint8_t>& bytes)
{
    core::DiagnosticList diagnostics;
    if (bytes.size() < 18) {
        diagnostics.error("BULLY-IMG-0040", "file is too small to be a TGA image");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    const std::uint8_t id_length = bytes[0];
    const std::uint8_t colour_map_type = bytes[1];
    const std::uint8_t image_type = bytes[2];
    const std::uint16_t width = read_le16(bytes.data() + 12);
    const std::uint16_t height = read_le16(bytes.data() + 14);
    const std::uint8_t bits_per_pixel = bytes[16];
    const std::uint8_t descriptor = bytes[17];
    if (colour_map_type != 0) {
        diagnostics.error("BULLY-IMG-0041", "colour-mapped TGA files are not supported");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    if (width == 0 || height == 0) {
        diagnostics.error("BULLY-IMG-0042", "TGA has invalid dimensions");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    const bool rle = image_type == 10 || image_type == 11;
    const bool greyscale = image_type == 3 || image_type == 11;
    if (image_type != 2 && image_type != 3 && image_type != 10 && image_type != 11) {
        diagnostics.error("BULLY-IMG-0043", "unsupported TGA image type");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    if (greyscale ? (bits_per_pixel != 8 && bits_per_pixel != 16)
                  : (bits_per_pixel != 24 && bits_per_pixel != 32)) {
        diagnostics.error("BULLY-IMG-0044", "unsupported TGA bit depth");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    const std::size_t bytes_per_pixel = bits_per_pixel / 8u;
    std::size_t offset = 18u + id_length;
    ImageData image;
    image.width = width;
    image.height = height;
    image.format_name = "tga";
    image.pixels.resize(static_cast<std::size_t>(width) * height * 4, 255);
    const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
    const bool top_origin = (descriptor & 0x20u) != 0;
    std::size_t written = 0;
    auto store = [&](const std::uint8_t* pixel) {
        if (written >= pixel_count) return;
        const std::size_t source_x = written % width;
        const std::size_t source_y = written / width;
        const std::size_t y = top_origin ? source_y : height - 1 - source_y;
        const std::size_t destination = (y * width + source_x) * 4;
        if (greyscale) {
            image.pixels[destination] = pixel[0];
            image.pixels[destination + 1] = pixel[0];
            image.pixels[destination + 2] = pixel[0];
            if (bytes_per_pixel == 2) image.pixels[destination + 3] = pixel[1];
        } else {
            image.pixels[destination] = pixel[2];
            image.pixels[destination + 1] = pixel[1];
            image.pixels[destination + 2] = pixel[0];
            if (bytes_per_pixel == 4) image.pixels[destination + 3] = pixel[3];
        }
        ++written;
    };
    while (written < pixel_count) {
        if (!rle) {
            if (offset + bytes_per_pixel > bytes.size()) {
                diagnostics.error("BULLY-IMG-0045", "TGA pixel data is truncated");
                return core::Result<ImageData>(std::move(diagnostics));
            }
            store(bytes.data() + offset);
            offset += bytes_per_pixel;
            continue;
        }
        if (offset >= bytes.size()) {
            diagnostics.error("BULLY-IMG-0046", "TGA RLE stream is truncated");
            return core::Result<ImageData>(std::move(diagnostics));
        }
        const std::uint8_t packet = bytes[offset++];
        const std::size_t count = static_cast<std::size_t>(packet & 0x7Fu) + 1;
        if ((packet & 0x80u) != 0) {
            if (offset + bytes_per_pixel > bytes.size()) {
                diagnostics.error("BULLY-IMG-0046", "TGA RLE stream is truncated");
                return core::Result<ImageData>(std::move(diagnostics));
            }
            const std::uint8_t* pixel = bytes.data() + offset;
            offset += bytes_per_pixel;
            for (std::size_t i = 0; i < count; ++i) store(pixel);
        } else {
            for (std::size_t i = 0; i < count; ++i) {
                if (offset + bytes_per_pixel > bytes.size()) {
                    diagnostics.error("BULLY-IMG-0046", "TGA RLE stream is truncated");
                    return core::Result<ImageData>(std::move(diagnostics));
                }
                store(bytes.data() + offset);
                offset += bytes_per_pixel;
            }
        }
    }
    return image;
}

core::Result<ImageData> decode_ppm(const std::vector<std::uint8_t>& bytes)
{
    core::DiagnosticList diagnostics;
    std::istringstream stream(std::string(bytes.begin(), bytes.end()));
    std::string magic;
    stream >> magic;
    const bool binary = magic == "P6";
    if (!binary && magic != "P3") {
        diagnostics.error("BULLY-IMG-0050", "file is not a PPM image");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t max_value = 0;
    stream >> width >> height >> max_value;
    if (width == 0 || height == 0 || max_value == 0) {
        diagnostics.error("BULLY-IMG-0051", "PPM header is invalid");
        return core::Result<ImageData>(std::move(diagnostics));
    }
    ImageData image;
    image.width = width;
    image.height = height;
    image.format_name = "ppm";
    image.pixels.resize(static_cast<std::size_t>(width) * height * 4, 255);
    if (binary) {
        stream.get();  // single whitespace after header
        std::vector<std::uint8_t> body{std::istreambuf_iterator<char>(stream),
                                       std::istreambuf_iterator<char>{}};
        if (body.size() < static_cast<std::size_t>(width) * height * 3) {
            diagnostics.error("BULLY-IMG-0052", "PPM pixel data is truncated");
            return core::Result<ImageData>(std::move(diagnostics));
        }
        for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
            image.pixels[i * 4] = body[i * 3];
            image.pixels[i * 4 + 1] = body[i * 3 + 1];
            image.pixels[i * 4 + 2] = body[i * 3 + 2];
        }
    } else {
        for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
            std::uint32_t r = 0;
            std::uint32_t g = 0;
            std::uint32_t b = 0;
            if (!(stream >> r >> g >> b)) {
                diagnostics.error("BULLY-IMG-0052", "PPM pixel data is truncated");
                return core::Result<ImageData>(std::move(diagnostics));
            }
            image.pixels[i * 4] = static_cast<std::uint8_t>(r * 255u / max_value);
            image.pixels[i * 4 + 1] = static_cast<std::uint8_t>(g * 255u / max_value);
            image.pixels[i * 4 + 2] = static_cast<std::uint8_t>(b * 255u / max_value);
        }
    }
    return image;
}

std::string_view sniff_image_format(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' &&
        bytes[3] == 'G') {
        return "png";
    }
    if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') return "bmp";
    if (bytes.size() >= 2 && bytes[0] == 'P' && (bytes[1] == '6' || bytes[1] == '3')) return "ppm";
    if (bytes.size() >= 18) {
        const std::uint8_t type = bytes[2];
        if (type == 2 || type == 3 || type == 10 || type == 11) {
            const std::uint16_t width = read_le16(bytes.data() + 12);
            const std::uint16_t height = read_le16(bytes.data() + 14);
            if (width > 0 && height > 0) return "tga";
        }
    }
    return {};
}

core::Result<ImageData> decode_image(const std::vector<std::uint8_t>& bytes)
{
    core::DiagnosticList diagnostics;
    const std::string_view format = sniff_image_format(bytes);
    if (format == "png") return decode_png(bytes);
    if (format == "bmp") return decode_bmp(bytes);
    if (format == "tga") return decode_tga(bytes);
    if (format == "ppm") return decode_ppm(bytes);
    diagnostics.error("BULLY-IMG-0053",
                      "unrecognized image format (supported: PNG, BMP, TGA, PPM). Use the Blender "
                      "bridge or an external converter for other formats.");
    return core::Result<ImageData>(std::move(diagnostics));
}

std::vector<std::uint8_t> encode_ppm(const ImageData& image)
{
    std::string header = "P6\n" + std::to_string(image.width) + " " + std::to_string(image.height) +
                         "\n255\n";
    std::vector<std::uint8_t> out(header.begin(), header.end());
    out.reserve(out.size() + static_cast<std::size_t>(image.width) * image.height * 3);
    for (std::size_t i = 0; i < static_cast<std::size_t>(image.width) * image.height; ++i) {
        out.push_back(image.pixels[i * 4]);
        out.push_back(image.pixels[i * 4 + 1]);
        out.push_back(image.pixels[i * 4 + 2]);
    }
    return out;
}

core::Result<formats::TextureDocument> build_texture_document(ImageData image,
                                                              const TextureBuildOptions& options,
                                                              std::string asset_id, std::string name,
                                                              std::string source_path)
{
    core::DiagnosticList diagnostics;
    formats::TextureDocument document;
    document.asset_id = std::move(asset_id);
    document.name = std::move(name);
    document.source_path = std::move(source_path);
    document.format = options.target_format;
    document.sampler_filter = options.sampler_filter;
    document.wrap_mode = options.wrap_mode;
    document.srgb = options.srgb;

    std::uint32_t width = image.width;
    std::uint32_t height = image.height;
    if (options.max_size > 0 && (width > options.max_size || height > options.max_size)) {
        // Preserve aspect ratio, never upscale.
        const float scale = static_cast<float>(options.max_size) /
                            static_cast<float>(std::max(width, height));
        const auto new_width = std::max(1u, static_cast<std::uint32_t>(
                                                 static_cast<float>(width) * scale));
        const auto new_height = std::max(1u, static_cast<std::uint32_t>(
                                                  static_cast<float>(height) * scale));
        std::vector<std::uint8_t> resized(static_cast<std::size_t>(new_width) * new_height * 4, 255);
        for (std::uint32_t y = 0; y < new_height; ++y) {
            for (std::uint32_t x = 0; x < new_width; ++x) {
                const std::size_t destination = (static_cast<std::size_t>(y) * new_width + x) * 4;
                if (options.nearest_downscale) {
                    const auto source_x = std::min(width - 1, x * width / new_width);
                    const auto source_y = std::min(height - 1, y * height / new_height);
                    const std::size_t source =
                        (static_cast<std::size_t>(source_y) * width + source_x) * 4;
                    for (std::size_t c = 0; c < 4; ++c) resized[destination + c] = image.pixels[source + c];
                } else {
                    // Box filter over the source footprint.
                    const auto x0 = static_cast<std::uint32_t>(static_cast<std::uint64_t>(x) * width / new_width);
                    const auto x1 = std::max(x0 + 1, static_cast<std::uint32_t>(
                                                          static_cast<std::uint64_t>(x + 1) * width /
                                                          new_width));
                    const auto y0 = static_cast<std::uint32_t>(static_cast<std::uint64_t>(y) * height / new_height);
                    const auto y1 = std::max(y0 + 1, static_cast<std::uint32_t>(
                                                          static_cast<std::uint64_t>(y + 1) * height /
                                                          new_height));
                    std::uint32_t sums[4] = {0, 0, 0, 0};
                    std::uint32_t count = 0;
                    for (std::uint32_t sy = y0; sy < y1 && sy < height; ++sy) {
                        for (std::uint32_t sx = x0; sx < x1 && sx < width; ++sx) {
                            const std::size_t source =
                                (static_cast<std::size_t>(sy) * width + sx) * 4;
                            for (std::size_t c = 0; c < 4; ++c) sums[c] += image.pixels[source + c];
                            ++count;
                        }
                    }
                    if (count > 0) {
                        for (std::size_t c = 0; c < 4; ++c) {
                            resized[destination + c] =
                                static_cast<std::uint8_t>(sums[c] / count);
                        }
                    }
                }
            }
        }
        image.pixels = std::move(resized);
        width = new_width;
        height = new_height;
    }
    document.width = width;
    document.height = height;

    formats::MipLevel base;
    base.width = width;
    base.height = height;
    base.stride = width * 4;
    base.pixels = image.pixels;
    document.mips.push_back(base);

    if (options.generate_mips) {
        auto chain = document.generate_mip_chain();
        if (chain) {
            for (auto& level : chain.value()) document.mips.push_back(std::move(level));
        } else {
            diagnostics.append(chain.diagnostics());
        }
    }
    if (!formats::is_supported_encoding(options.target_format)) {
        diagnostics.error("BULLY-IMG-0054", "selected pixel format is not encodable");
        return core::Result<formats::TextureDocument>(std::move(diagnostics));
    }
    // Quantise if a retro format was requested: the mip chain is generated in
    // RGBA8 above, then every level is converted here.
    if (options.target_format != formats::PixelFormat::Rgba8) {
        const auto bytes_per_pixel = formats::bytes_per_pixel(options.target_format);
        for (auto& mip : document.mips) {
            std::vector<std::uint8_t> converted(static_cast<std::size_t>(mip.width) * mip.height *
                                                    bytes_per_pixel,
                                                0);
            for (std::uint32_t y = 0; y < mip.height; ++y) {
                for (std::uint32_t x = 0; x < mip.width; ++x) {
                    const std::size_t source =
                        (static_cast<std::size_t>(y) * mip.stride) + static_cast<std::size_t>(x) * 4;
                    const std::size_t destination =
                        (static_cast<std::size_t>(y) * mip.width + x) * bytes_per_pixel;
                    const auto r = mip.pixels[source];
                    const auto g = mip.pixels[source + 1];
                    const auto b = mip.pixels[source + 2];
                    const auto a = mip.pixels[source + 3];
                    switch (options.target_format) {
                        case formats::PixelFormat::Rgb8:
                            converted[destination] = r;
                            converted[destination + 1] = g;
                            converted[destination + 2] = b;
                            break;
                        case formats::PixelFormat::R8:
                            converted[destination] = r;
                            break;
                        case formats::PixelFormat::Rgb565: {
                            const auto value = static_cast<std::uint16_t>(
                                ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                            converted[destination] = static_cast<std::uint8_t>(value & 0xFF);
                            converted[destination + 1] = static_cast<std::uint8_t>(value >> 8);
                            break;
                        }
                        case formats::PixelFormat::Rgba4444: {
                            const auto value = static_cast<std::uint16_t>(
                                ((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | (a >> 4));
                            converted[destination] = static_cast<std::uint8_t>(value & 0xFF);
                            converted[destination + 1] = static_cast<std::uint8_t>(value >> 8);
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
            mip.pixels = std::move(converted);
            mip.stride = mip.width * bytes_per_pixel;
        }
    }
    auto validation = document.validate();
    if (!validation) return core::Result<formats::TextureDocument>(validation.diagnostics());
    return document;
}

}  // namespace bully::pipeline
