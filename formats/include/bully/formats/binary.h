// Bully Studio - binary serialisation primitives shared by every .b* format.
//
// File layout (all integers little-endian):
//
//   offset  size  field
//   ------  ----  -----------------------------------------------------------
//   0       4     magic        ASCII fourcc, e.g. "BMOD"
//   4       2     version_major
//   6       2     version_minor
//   8       4     header_size  bytes of this header (32 today)
//   12      4     flags        format-specific bit flags
//   16      8     payload_size bytes of chunk data following the header
//   24      4     payload_crc  CRC-32 (IEEE) of the chunk data
//   28      4     reserved     must be 0
//
// The payload is a stream of chunks:
//
//   uint32 fourcc, uint32 size, uint8[size] data
//
// Unknown chunks are skipped by readers, which is the core of the
// forward-compatibility strategy: a v1.2 reader opens a v1.1 file (missing
// chunks get documented defaults) and a v1.1 reader opens a v1.2 file
// (unknown chunks are ignored). A major version bump means an intentionally
// incompatible layout and is rejected with a clear diagnostic.
//
// Diagnostic codes: BULLY-BIN-0001..0019
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/core/version.h"

namespace bully::formats {

inline constexpr std::size_t kHeaderSize = 32;

/// CRC-32 (IEEE 802.3) - used for payload integrity and asset hashing.
[[nodiscard]] std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t seed = 0);
[[nodiscard]] std::uint32_t crc32(std::string_view text);

/// Packs four ASCII characters into a chunk/magic id.
[[nodiscard]] constexpr std::uint32_t fourcc(const char (&text)[5])
{
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(text[0])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(text[1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(text[2])) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(text[3]));
}

/// Renders a fourcc back to text (for diagnostics).
[[nodiscard]] std::string fourcc_to_string(std::uint32_t id);

struct Mat4f;  // defined below

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec4f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

/// Column-major 4x4 matrix (OpenGL convention).
struct Mat4f {
    float m[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

    [[nodiscard]] static Mat4f identity() { return Mat4f{}; }
};

struct Aabb {
    Vec3f min{};
    Vec3f max{};
};

/// Append-only binary writer. Every write is checked at serialise() time.
class BinaryWriter {
public:
    // --- scalars ---
    void u8(std::uint8_t value);
    void u16(std::uint16_t value);
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);
    void i32(std::int32_t value);
    void i64(std::int64_t value);
    void f32(float value);
    void f64(double value);
    void boolean(bool value);

    // --- composites ---
    /// Length-prefixed (u32 byte count) UTF-8 string.
    void string(std::string_view value);
    void vec2(const Vec2f& value);
    void vec3(const Vec3f& value);
    void vec4(const Vec4f& value);
    void mat4(const Mat4f& value);
    void aabb(const Aabb& value);
    /// Length-prefixed raw blob (u32 byte count + bytes).
    void blob(const std::vector<std::uint8_t>& value);

    // --- chunks ---
    void begin_chunk(std::uint32_t id);
    void end_chunk();
    /// Writes a chunk whose body is produced by `body`; convenience for
    /// optional sections.
    template <typename Fn>
    void chunk(std::uint32_t id, Fn&& body);

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept { return buffer_; }
    [[nodiscard]] std::size_t size() const noexcept { return buffer_.size(); }

private:
    struct OpenChunk {
        std::uint32_t id;
        std::size_t start;
    };
    std::vector<std::uint8_t> buffer_;
    std::vector<OpenChunk> open_chunks_;
    bool failed_ = false;
};

template <typename Fn>
void BinaryWriter::chunk(std::uint32_t id, Fn&& body)
{
    begin_chunk(id);
    body(*this);
    end_chunk();
}

/// Bounds-checked binary reader. Any out-of-range read sets an error and
/// returns zeroes; callers check ok() once per document.
class BinaryReader {
public:
    BinaryReader(const std::uint8_t* data, std::size_t size, core::DiagnosticList* diagnostics = nullptr);

    [[nodiscard]] std::uint8_t u8();
    [[nodiscard]] std::uint16_t u16();
    [[nodiscard]] std::uint32_t u32();
    [[nodiscard]] std::uint64_t u64();
    [[nodiscard]] std::int32_t i32();
    [[nodiscard]] std::int64_t i64();
    [[nodiscard]] float f32();
    [[nodiscard]] double f64();
    [[nodiscard]] bool boolean();

    [[nodiscard]] std::string string();
    [[nodiscard]] Vec2f vec2();
    [[nodiscard]] Vec3f vec3();
    [[nodiscard]] Vec4f vec4();
    [[nodiscard]] Mat4f mat4();
    [[nodiscard]] Aabb aabb();
    [[nodiscard]] std::vector<std::uint8_t> blob();

    [[nodiscard]] bool ok() const noexcept { return !failed_; }
    [[nodiscard]] bool remaining(std::size_t count) const noexcept { return position_ + count <= size_; }
    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    void seek(std::size_t position);

    /// Reads a chunk header at the current position. Returns false at end of
    /// stream. The caller must consume the body (read or skip).
    [[nodiscard]] bool next_chunk(std::uint32_t& id, std::uint32_t& size);
    /// Skips the remainder of the current chunk body.
    void skip_chunk_body(std::uint32_t size);

    /// Marks a recoverable problem (e.g. unsupported enum value).
    void warn(std::string code, std::string message);
    void fail(std::string code, std::string message);

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t position_ = 0;
    bool failed_ = false;
    core::DiagnosticList* diagnostics_ = nullptr;
};

/// Describes one on-disk resource format (magic + supported version).
struct FormatDescriptor {
    std::uint32_t magic = 0;
    const char* name = "";
    core::Version version{1, 0, 0};
    const char* extension = "";
    const char* description = "";
};

/// Header written ahead of every payload by `write_document`.
[[nodiscard]] std::vector<std::uint8_t> encode_document(const FormatDescriptor& format,
                                                        std::uint32_t flags,
                                                        const std::vector<std::uint8_t>& payload);

/// Validates header magic/version/CRC and yields the payload.
[[nodiscard]] core::Result<std::vector<std::uint8_t>> decode_document(
    const FormatDescriptor& format, const std::vector<std::uint8_t>& file_bytes,
    std::uint32_t* flags_out = nullptr, core::Version* version_out = nullptr);

/// True when a reader with `reader` version can open a file of `file_version`:
/// same major, file minor <= reader minor (older or equal), and a one-minor
/// forward tolerance is accepted with a warning.
[[nodiscard]] bool version_compatible(core::Version reader, core::Version file,
                                      bool* is_newer = nullptr);

}  // namespace bully::formats
