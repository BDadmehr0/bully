// Minimal, self-contained DEFLATE (RFC 1951) + zlib (RFC 1950) decompressor.
//
// Written from the specification for the texture importer. Handles stored,
// fixed-Huffman and dynamic-Huffman blocks, and reports structural errors as
// diagnostics instead of reading out of bounds.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "bully/pipeline/image_codec.h"

namespace bully::pipeline {

namespace {

class BitReader {
public:
    BitReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    /// Reads `count` bits (<= 24), LSB first.
    bool read_bits(int count, std::uint32_t& out)
    {
        std::uint32_t value = 0;
        for (int i = 0; i < count; ++i) {
            if (bit_position_ >= size_ * 8) return false;
            const std::uint8_t byte = data_[bit_position_ >> 3];
            const auto bit = static_cast<std::uint32_t>((byte >> (bit_position_ & 7)) & 1u);
            value |= bit << i;
            ++bit_position_;
        }
        out = value;
        return true;
    }

    void align_to_byte() { bit_position_ = (bit_position_ + 7) & ~static_cast<std::size_t>(7); }

    [[nodiscard]] bool read_byte(std::uint8_t& out)
    {
        if (bit_position_ + 8 > size_ * 8) return false;
        out = data_[bit_position_ >> 3];
        bit_position_ += 8;
        return true;
    }

    [[nodiscard]] std::size_t bit_position() const { return bit_position_; }
    [[nodiscard]] bool exhausted() const { return bit_position_ >= size_ * 8; }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t bit_position_ = 0;
};

struct HuffmanTable {
    // Canonical Huffman decoding table: counts per bit length + symbols sorted
    // by code. Simple and fast enough for texture decoding.
    std::array<std::uint16_t, 16> counts{};
    std::vector<std::uint16_t> symbols;

    [[nodiscard]] bool build(const std::uint8_t* lengths, int count)
    {
        counts.fill(0);
        symbols.assign(static_cast<std::size_t>(count), 0);
        for (int i = 0; i < count; ++i) {
            if (lengths[i] > 15) return false;
            counts[lengths[i]] = static_cast<std::uint16_t>(counts[lengths[i]] + 1);
        }
        counts[0] = 0;
        std::array<std::uint16_t, 16> offsets{};
        for (std::size_t i = 1; i < 16; ++i) {
            offsets[i] = static_cast<std::uint16_t>(offsets[i - 1] + counts[i - 1]);
        }
        for (int i = 0; i < count; ++i) {
            if (lengths[i] != 0) {
                auto& offset = offsets[static_cast<std::size_t>(lengths[i])];
                symbols[offset++] = static_cast<std::uint16_t>(i);
            }
        }
        return true;
    }

    /// Decodes one symbol; returns -1 on invalid code.
    [[nodiscard]] int decode(BitReader& reader) const
    {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int length = 1; length < 16; ++length) {
            std::uint32_t bit = 0;
            if (!reader.read_bits(1, bit)) return -1;
            code |= static_cast<int>(bit);
            const int count = counts[static_cast<std::size_t>(length)];
            if (code - first < count) {
                const auto position = static_cast<std::size_t>(index + (code - first));
                if (position >= symbols.size()) return -1;
                return symbols[position];
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return -1;
    }
};

constexpr std::array<std::uint16_t, 30> kLengthBase{
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 258};
constexpr std::array<std::uint8_t, 30> kLengthExtra{
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 0};
constexpr std::array<std::uint16_t, 30> kDistanceBase{
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<std::uint8_t, 30> kDistanceExtra{
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

struct InflateResult {
    bool ok = false;
    std::string error;
    std::vector<std::uint8_t> out;
};

InflateResult inflate_raw(const std::uint8_t* data, std::size_t size, std::size_t expected_hint)
{
    InflateResult result;
    BitReader reader(data, size);
    std::vector<std::uint8_t> output;
    if (expected_hint > 0) output.reserve(expected_hint);

    bool final_block = false;
    while (!final_block) {
        std::uint32_t header = 0;
        if (!reader.read_bits(3, header)) {
            result.error = "truncated deflate block header";
            return result;
        }
        final_block = (header & 1u) != 0;
        const std::uint32_t type = (header >> 1) & 3u;
        if (type == 0) {
            reader.align_to_byte();
            std::uint8_t length_bytes[4] = {0, 0, 0, 0};
            for (auto& byte : length_bytes) {
                if (!reader.read_byte(byte)) {
                    result.error = "truncated stored block header";
                    return result;
                }
            }
            const std::size_t length =
                static_cast<std::size_t>(length_bytes[0]) | (static_cast<std::size_t>(length_bytes[1]) << 8);
            const std::size_t complement =
                static_cast<std::size_t>(length_bytes[2]) | (static_cast<std::size_t>(length_bytes[3]) << 8);
            if ((length ^ 0xFFFFu) != complement) {
                result.error = "stored block length check failed";
                return result;
            }
            for (std::size_t i = 0; i < length; ++i) {
                std::uint8_t byte = 0;
                if (!reader.read_byte(byte)) {
                    result.error = "truncated stored block body";
                    return result;
                }
                output.push_back(byte);
            }
            continue;
        }
        if (type == 3) {
            result.error = "invalid deflate block type 3";
            return result;
        }

        HuffmanTable literal_table;
        HuffmanTable distance_table;
        if (type == 1) {
            // Fixed Huffman tables from RFC 1951 section 3.2.6.
            std::array<std::uint8_t, 288> literal_lengths{};
            for (int i = 0; i < 144; ++i) literal_lengths[static_cast<std::size_t>(i)] = 8;
            for (int i = 144; i < 256; ++i) literal_lengths[static_cast<std::size_t>(i)] = 9;
            for (int i = 256; i < 280; ++i) literal_lengths[static_cast<std::size_t>(i)] = 7;
            for (int i = 280; i < 288; ++i) literal_lengths[static_cast<std::size_t>(i)] = 8;
            std::array<std::uint8_t, 30> distance_lengths{};
            distance_lengths.fill(5);
            if (!literal_table.build(literal_lengths.data(), 288) ||
                !distance_table.build(distance_lengths.data(), 30)) {
                result.error = "cannot build fixed Huffman tables";
                return result;
            }
        } else {
            std::uint32_t hlit = 0;
            std::uint32_t hdist = 0;
            std::uint32_t hclen = 0;
            if (!reader.read_bits(5, hlit) || !reader.read_bits(5, hdist) ||
                !reader.read_bits(4, hclen)) {
                result.error = "truncated dynamic block header";
                return result;
            }
            const auto literal_count = static_cast<int>(hlit) + 257;
            const auto distance_count = static_cast<int>(hdist) + 1;
            const auto code_count = static_cast<int>(hclen) + 4;
            static constexpr int kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
                                               11, 4, 12, 3, 13, 2, 14, 1, 15};
            std::array<std::uint8_t, 19> code_lengths{};
            for (int i = 0; i < code_count; ++i) {
                std::uint32_t value = 0;
                if (!reader.read_bits(3, value)) {
                    result.error = "truncated code length header";
                    return result;
                }
                code_lengths[static_cast<std::size_t>(kOrder[i])] = static_cast<std::uint8_t>(value);
            }
            HuffmanTable code_table;
            if (!code_table.build(code_lengths.data(), 19)) {
                result.error = "invalid code length table";
                return result;
            }
            std::vector<std::uint8_t> lengths(
                static_cast<std::size_t>(literal_count + distance_count), 0);
            int index = 0;
            while (index < literal_count + distance_count) {
                const int symbol = code_table.decode(reader);
                if (symbol < 0) {
                    result.error = "invalid code length symbol";
                    return result;
                }
                if (symbol < 16) {
                    lengths[static_cast<std::size_t>(index++)] = static_cast<std::uint8_t>(symbol);
                    continue;
                }
                int repeat = 0;
                std::uint8_t value = 0;
                if (symbol == 16) {
                    if (index == 0) {
                        result.error = "repeat with no previous code length";
                        return result;
                    }
                    value = lengths[static_cast<std::size_t>(index - 1)];
                    std::uint32_t extra = 0;
                    if (!reader.read_bits(2, extra)) return result;
                    repeat = 3 + static_cast<int>(extra);
                } else if (symbol == 17) {
                    std::uint32_t extra = 0;
                    if (!reader.read_bits(3, extra)) return result;
                    repeat = 3 + static_cast<int>(extra);
                } else {
                    std::uint32_t extra = 0;
                    if (!reader.read_bits(7, extra)) return result;
                    repeat = 11 + static_cast<int>(extra);
                }
                if (index + repeat > literal_count + distance_count) {
                    result.error = "code length repeat overflows table";
                    return result;
                }
                for (int i = 0; i < repeat; ++i) {
                    lengths[static_cast<std::size_t>(index++)] = value;
                }
            }
            if (!literal_table.build(lengths.data(), literal_count) ||
                !distance_table.build(lengths.data() + literal_count, distance_count)) {
                result.error = "invalid dynamic Huffman tables";
                return result;
            }
        }

        // Decode symbols.
        while (true) {
            const int symbol = literal_table.decode(reader);
            if (symbol < 0) {
                result.error = "invalid literal/length symbol";
                return result;
            }
            if (symbol < 256) {
                output.push_back(static_cast<std::uint8_t>(symbol));
                continue;
            }
            if (symbol == 256) break;  // end of block
            const int length_index = symbol - 257;
            if (length_index < 0 || length_index >= static_cast<int>(kLengthBase.size())) {
                result.error = "invalid length code";
                return result;
            }
            std::uint32_t extra_bits = 0;
            if (kLengthExtra[static_cast<std::size_t>(length_index)] > 0 &&
                !reader.read_bits(kLengthExtra[static_cast<std::size_t>(length_index)], extra_bits)) {
                result.error = "truncated length extra bits";
                return result;
            }
            const std::size_t length = kLengthBase[static_cast<std::size_t>(length_index)] + extra_bits;
            const int distance_symbol = distance_table.decode(reader);
            if (distance_symbol < 0 ||
                distance_symbol >= static_cast<int>(kDistanceBase.size())) {
                result.error = "invalid distance code";
                return result;
            }
            std::uint32_t distance_extra = 0;
            if (kDistanceExtra[static_cast<std::size_t>(distance_symbol)] > 0 &&
                !reader.read_bits(kDistanceExtra[static_cast<std::size_t>(distance_symbol)],
                                  distance_extra)) {
                result.error = "truncated distance extra bits";
                return result;
            }
            const std::size_t distance =
                kDistanceBase[static_cast<std::size_t>(distance_symbol)] + distance_extra;
            if (distance == 0 || distance > output.size()) {
                result.error = "distance exceeds output size";
                return result;
            }
            // Overlapping copy is intentional and must be byte-by-byte.
            for (std::size_t i = 0; i < length; ++i) {
                output.push_back(output[output.size() - distance]);
            }
        }
    }
    result.ok = true;
    result.out = std::move(output);
    return result;
}

}  // namespace

core::Result<std::vector<std::uint8_t>> inflate_zlib(const std::uint8_t* data, std::size_t size,
                                                     std::size_t expected_hint)
{
    core::DiagnosticList diagnostics;
    if (size < 6) {
        diagnostics.error("BULLY-IMG-0001", "zlib stream is too short");
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    const std::uint8_t cmf = data[0];
    const std::uint8_t flg = data[1];
    if ((cmf & 0x0Fu) != 8) {
        diagnostics.error("BULLY-IMG-0002", "unsupported zlib compression method");
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    if (((static_cast<std::uint32_t>(cmf) << 8) + flg) % 31u != 0) {
        diagnostics.error("BULLY-IMG-0003", "zlib header checksum mismatch");
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    if ((flg & 0x20u) != 0) {
        diagnostics.error("BULLY-IMG-0004", "zlib preset dictionaries are not supported");
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    auto result = inflate_raw(data + 2, size - 2, expected_hint);
    if (!result.ok) {
        diagnostics.error("BULLY-IMG-0005", "deflate error: " + result.error);
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    return result.out;
}

}  // namespace bully::pipeline
