// Shared test helpers: temporary directories, sample fixtures and small
// filesystem utilities used across the suite.
#pragma once

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string_view>

#include <filesystem>
#include <string>
#include <vector>

#include "bully/core/fs.h"
#include "bully/formats/binary.h"

namespace bully::test {

/// Creates a unique temporary directory that is removed on destruction.
class TempDir {
public:
    explicit TempDir(std::string_view tag = "bully-test");
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&& other) noexcept;
    TempDir& operator=(TempDir&& other) noexcept;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::string str() const { return path_.string(); }
    [[nodiscard]] std::filesystem::path operator/(std::string_view relative) const;

    /// Writes a text file, creating parents. Fails the test on I/O errors.
    void write(std::string_view relative, std::string_view content);
    /// Writes a binary file, creating parents.
    void write_bytes(std::string_view relative, const std::vector<std::uint8_t>& bytes);

private:
    std::filesystem::path path_;
};

// ---------------------------------------------------------------- fixtures --

/// A 10x10x10 cube OBJ (24 vertices after using per-face UVs, 12 triangles).
[[nodiscard]] std::string cube_obj();

/// OBJ with negative (relative) indices and a quad face.
[[nodiscard]] std::string relative_index_obj();

/// A minimal valid 2x2 RGBA PNG built by hand (stored/uncompressed-zlib-free
/// block). Used to verify the PNG decoder without external files.
[[nodiscard]] std::vector<std::uint8_t> tiny_png();

/// A 2x2 24-bit BMP.
[[nodiscard]] std::vector<std::uint8_t> tiny_bmp();

/// A 2x2 uncompressed TGA.
[[nodiscard]] std::vector<std::uint8_t> tiny_tga();

/// An 8-bit mono 8000 Hz WAV containing a 440 Hz sine burst.
[[nodiscard]] std::vector<std::uint8_t> sine_wav(std::uint32_t sample_rate = 8000,
                                                 float seconds = 0.1f);

/// A glTF 2.0 binary (.glb) containing a single triangle with normals.
[[nodiscard]] std::vector<std::uint8_t> triangle_glb();

/// Minimal dialogue source JSON matching the documented schema.
[[nodiscard]] std::string dialogue_source_json();

/// Minimal scene source JSON.
[[nodiscard]] std::string scene_source_json();

/// Minimal mission source JSON.
[[nodiscard]] std::string mission_source_json();

}  // namespace bully::test
