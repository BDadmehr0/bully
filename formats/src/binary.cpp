#include "bully/formats/binary.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bully::formats {

namespace {

const std::array<std::uint32_t, 256>& crc_table()
{
    static const auto table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t crc = i;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
            }
            values[i] = crc;
        }
        return values;
    }();
    return table;
}

std::uint32_t read_u32_le(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

void write_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t seed)
{
    const auto& table = crc_table();
    std::uint32_t crc = seed ^ 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t crc32(std::string_view text)
{
    return crc32(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

std::string fourcc_to_string(std::uint32_t id)
{
    std::string out(4, ' ');
    for (int i = 0; i < 4; ++i) {
        const auto shift = static_cast<unsigned>(24 - 8 * i);
        const auto c = static_cast<char>((id >> shift) & 0xFFu);
        out[static_cast<std::size_t>(i)] = (c >= 32 && c < 127) ? c : '.';
    }
    return out;
}

// --------------------------------------------------------- BinaryWriter -----

void BinaryWriter::u8(std::uint8_t value) { buffer_.push_back(value); }

void BinaryWriter::u16(std::uint16_t value)
{
    buffer_.push_back(static_cast<std::uint8_t>(value & 0xFF));
    buffer_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void BinaryWriter::u32(std::uint32_t value)
{
    buffer_.push_back(static_cast<std::uint8_t>(value & 0xFF));
    buffer_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    buffer_.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    buffer_.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void BinaryWriter::u64(std::uint64_t value)
{
    u32(static_cast<std::uint32_t>(value & 0xFFFFFFFFull));
    u32(static_cast<std::uint32_t>((value >> 32) & 0xFFFFFFFFull));
}

void BinaryWriter::i32(std::int32_t value)
{
    u32(static_cast<std::uint32_t>(value));
}

void BinaryWriter::i64(std::int64_t value)
{
    u64(static_cast<std::uint64_t>(value));
}

void BinaryWriter::f32(float value)
{
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    u32(bits);
}

void BinaryWriter::f64(double value)
{
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    u64(bits);
}

void BinaryWriter::boolean(bool value) { u8(value ? 1 : 0); }

void BinaryWriter::string(std::string_view value)
{
    u32(static_cast<std::uint32_t>(value.size()));
    buffer_.insert(buffer_.end(), value.begin(), value.end());
}

void BinaryWriter::vec2(const Vec2f& value)
{
    f32(value.x);
    f32(value.y);
}

void BinaryWriter::vec3(const Vec3f& value)
{
    f32(value.x);
    f32(value.y);
    f32(value.z);
}

void BinaryWriter::vec4(const Vec4f& value)
{
    f32(value.x);
    f32(value.y);
    f32(value.z);
    f32(value.w);
}

void BinaryWriter::mat4(const Mat4f& value)
{
    for (float component : value.m) f32(component);
}

void BinaryWriter::aabb(const Aabb& value)
{
    vec3(value.min);
    vec3(value.max);
}

void BinaryWriter::blob(const std::vector<std::uint8_t>& value)
{
    u32(static_cast<std::uint32_t>(value.size()));
    buffer_.insert(buffer_.end(), value.begin(), value.end());
}

void BinaryWriter::begin_chunk(std::uint32_t id)
{
    open_chunks_.push_back(OpenChunk{id, buffer_.size()});
    u32(id);
    u32(0);  // size placeholder, patched in end_chunk()
}

void BinaryWriter::end_chunk()
{
    if (open_chunks_.empty()) {
        failed_ = true;
        return;
    }
    const OpenChunk chunk = open_chunks_.back();
    open_chunks_.pop_back();
    const std::size_t body_start = chunk.start + 8;
    const auto body_size = static_cast<std::uint32_t>(buffer_.size() - body_start);
    buffer_[chunk.start + 4] = static_cast<std::uint8_t>(body_size & 0xFF);
    buffer_[chunk.start + 5] = static_cast<std::uint8_t>((body_size >> 8) & 0xFF);
    buffer_[chunk.start + 6] = static_cast<std::uint8_t>((body_size >> 16) & 0xFF);
    buffer_[chunk.start + 7] = static_cast<std::uint8_t>((body_size >> 24) & 0xFF);
}

// --------------------------------------------------------- BinaryReader -----

BinaryReader::BinaryReader(const std::uint8_t* data, std::size_t size, core::DiagnosticList* diagnostics)
    : data_(data), size_(size), diagnostics_(diagnostics)
{
}

void BinaryReader::fail(std::string code, std::string message)
{
    if (failed_) return;  // only the first error is interesting
    failed_ = true;
    if (diagnostics_ != nullptr) diagnostics_->error(std::move(code), std::move(message), "binary");
}

void BinaryReader::warn(std::string code, std::string message)
{
    if (diagnostics_ != nullptr) diagnostics_->warn(std::move(code), std::move(message), "binary");
}

void BinaryReader::seek(std::size_t position)
{
    if (position > size_) {
        fail("BULLY-BIN-0001", "seek past end of stream");
        position_ = size_;
        return;
    }
    position_ = position;
}

std::uint8_t BinaryReader::u8()
{
    if (!remaining(1)) {
        fail("BULLY-BIN-0002", "unexpected end of stream reading u8");
        return 0;
    }
    return data_[position_++];
}

std::uint16_t BinaryReader::u16()
{
    if (!remaining(2)) {
        fail("BULLY-BIN-0002", "unexpected end of stream reading u16");
        return 0;
    }
    const auto value = static_cast<std::uint16_t>(data_[position_] | (data_[position_ + 1] << 8));
    position_ += 2;
    return value;
}

std::uint32_t BinaryReader::u32()
{
    if (!remaining(4)) {
        fail("BULLY-BIN-0002", "unexpected end of stream reading u32");
        return 0;
    }
    const std::uint32_t value = read_u32_le(data_ + position_);
    position_ += 4;
    return value;
}

std::uint64_t BinaryReader::u64()
{
    const std::uint64_t low = u32();
    const std::uint64_t high = u32();
    return low | (high << 32);
}

std::int32_t BinaryReader::i32() { return static_cast<std::int32_t>(u32()); }
std::int64_t BinaryReader::i64() { return static_cast<std::int64_t>(u64()); }

float BinaryReader::f32()
{
    const std::uint32_t bits = u32();
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

double BinaryReader::f64()
{
    const std::uint64_t bits = u64();
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool BinaryReader::boolean() { return u8() != 0; }

std::string BinaryReader::string()
{
    const std::uint32_t length = u32();
    if (!remaining(length)) {
        fail("BULLY-BIN-0003", "string length exceeds remaining bytes");
        return {};
    }
    std::string out(reinterpret_cast<const char*>(data_ + position_), length);
    position_ += length;
    return out;
}

Vec2f BinaryReader::vec2()
{
    Vec2f value;
    value.x = f32();
    value.y = f32();
    return value;
}

Vec3f BinaryReader::vec3()
{
    Vec3f value;
    value.x = f32();
    value.y = f32();
    value.z = f32();
    return value;
}

Vec4f BinaryReader::vec4()
{
    Vec4f value;
    value.x = f32();
    value.y = f32();
    value.z = f32();
    value.w = f32();
    return value;
}

Mat4f BinaryReader::mat4()
{
    Mat4f value;
    for (float& component : value.m) component = f32();
    return value;
}

Aabb BinaryReader::aabb()
{
    Aabb value;
    value.min = vec3();
    value.max = vec3();
    return value;
}

std::vector<std::uint8_t> BinaryReader::blob()
{
    const std::uint32_t length = u32();
    if (!remaining(length)) {
        fail("BULLY-BIN-0004", "blob length exceeds remaining bytes");
        return {};
    }
    std::vector<std::uint8_t> out(data_ + position_, data_ + position_ + length);
    position_ += length;
    return out;
}

bool BinaryReader::next_chunk(std::uint32_t& id, std::uint32_t& size)
{
    if (position_ >= size_) return false;
    if (!remaining(8)) {
        fail("BULLY-BIN-0005", "truncated chunk header");
        return false;
    }
    id = u32();
    size = u32();
    if (!remaining(size)) {
        fail("BULLY-BIN-0006", "chunk '" + fourcc_to_string(id) + "' exceeds stream length");
        return false;
    }
    return true;
}

void BinaryReader::skip_chunk_body(std::uint32_t size)
{
    if (!remaining(size)) {
        fail("BULLY-BIN-0006", "cannot skip truncated chunk body");
        return;
    }
    position_ += size;
}

// ------------------------------------------------------- Document header ----

std::vector<std::uint8_t> encode_document(const FormatDescriptor& format, std::uint32_t flags,
                                          const std::vector<std::uint8_t>& payload)
{
    std::vector<std::uint8_t> out;
    out.reserve(payload.size() + kHeaderSize);
    write_u32_le(out, format.magic);
    // version major/minor
    out.push_back(static_cast<std::uint8_t>(format.version.major & 0xFF));
    out.push_back(static_cast<std::uint8_t>((format.version.major >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(format.version.minor & 0xFF));
    out.push_back(static_cast<std::uint8_t>((format.version.minor >> 8) & 0xFF));
    write_u32_le(out, static_cast<std::uint32_t>(kHeaderSize));
    write_u32_le(out, flags);
    write_u32_le(out, static_cast<std::uint32_t>(payload.size() & 0xFFFFFFFFull));
    write_u32_le(out, static_cast<std::uint32_t>((payload.size() >> 32) & 0xFFFFFFFFull));
    write_u32_le(out, crc32(payload.data(), payload.size()));
    write_u32_le(out, 0);  // reserved
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

core::Result<std::vector<std::uint8_t>> decode_document(const FormatDescriptor& format,
                                                        const std::vector<std::uint8_t>& file_bytes,
                                                        std::uint32_t* flags_out,
                                                        core::Version* version_out)
{
    core::DiagnosticList diagnostics;
    const std::string source = std::string(format.name) + " document";
    if (file_bytes.size() < kHeaderSize) {
        diagnostics.error("BULLY-BIN-0007", "file is smaller than the format header", source);
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    const std::uint32_t magic = read_u32_le(file_bytes.data());
    if (magic != format.magic) {
        diagnostics.error("BULLY-BIN-0008",
                          "bad magic: expected '" + fourcc_to_string(format.magic) + "', found '" +
                              fourcc_to_string(magic) + "'",
                          source);
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    const core::Version file_version{
        static_cast<std::uint32_t>(file_bytes[4] | (file_bytes[5] << 8)),
        static_cast<std::uint32_t>(file_bytes[6] | (file_bytes[7] << 8)), 0};
    bool newer = false;
    if (!version_compatible(format.version, file_version, &newer)) {
        diagnostics.error("BULLY-BIN-0009",
                          "incompatible version: reader supports " +
                              std::to_string(format.version.major) + ".x, file is " +
                              std::to_string(file_version.major) + "." +
                              std::to_string(file_version.minor),
                          source);
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    if (newer) {
        diagnostics.warn("BULLY-BIN-0010",
                         "file was written by a newer minor version (" +
                             std::to_string(file_version.major) + "." +
                             std::to_string(file_version.minor) + "); unknown sections ignored",
                         source);
    }
    const std::uint32_t header_size = read_u32_le(file_bytes.data() + 8);
    if (header_size < kHeaderSize || header_size > file_bytes.size()) {
        diagnostics.error("BULLY-BIN-0011", "invalid header size", source);
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    const std::uint32_t flags = read_u32_le(file_bytes.data() + 12);
    const std::uint64_t payload_size = static_cast<std::uint64_t>(read_u32_le(file_bytes.data() + 16)) |
                                       (static_cast<std::uint64_t>(read_u32_le(file_bytes.data() + 20)) << 32);
    const std::uint32_t expected_crc = read_u32_le(file_bytes.data() + 24);
    if (payload_size > file_bytes.size() - header_size) {
        diagnostics.error("BULLY-BIN-0012", "payload size exceeds file length", source);
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    std::vector<std::uint8_t> payload(file_bytes.begin() + static_cast<std::ptrdiff_t>(header_size),
                                      file_bytes.begin() +
                                          static_cast<std::ptrdiff_t>(header_size + payload_size));
    const std::uint32_t actual_crc = crc32(payload.data(), payload.size());
    if (actual_crc != expected_crc) {
        diagnostics.error("BULLY-BIN-0013",
                          "payload checksum mismatch (expected " + std::to_string(expected_crc) +
                              ", computed " + std::to_string(actual_crc) + ")",
                          source);
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    if (flags_out != nullptr) *flags_out = flags;
    if (version_out != nullptr) *version_out = file_version;
    // Forward-compatibility warnings (a newer minor version) travel with the
    // decoded payload so nothing is silently dropped.
    core::Result<std::vector<std::uint8_t>> decoded(std::move(payload));
    decoded.attach_diagnostics(std::move(diagnostics));
    return decoded;
}

bool version_compatible(core::Version reader, core::Version file, bool* is_newer)
{
    if (is_newer != nullptr) *is_newer = false;
    if (file.major != reader.major) return false;
    if (file.minor > reader.minor) {
        if (is_newer != nullptr) *is_newer = true;
        // One forward minor is tolerated: unknown chunks are skipped.
        return (file.minor - reader.minor) <= 1;
    }
    return true;
}

}  // namespace bully::formats
