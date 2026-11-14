#include "test_support.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bully/core/json.h"

namespace bully::test {

namespace {

std::atomic<std::uint64_t> g_counter{0};

std::vector<std::uint8_t> zlib_stored(const std::vector<std::uint8_t>& raw)
{
    // zlib header (CMF=0x78, FLG such that (CMF<<8|FLG) % 31 == 0) with a single
    // stored deflate block. This keeps the PNG fixture byte-exact without
    // depending on the inflate implementation that the test verifies.
    std::vector<std::uint8_t> out{0x78, 0x01};
    // Stored blocks carry at most 65535 bytes.
    std::size_t offset = 0;
    while (offset < raw.size() || raw.empty()) {
        const std::size_t chunk = std::min<std::size_t>(65535, raw.size() - offset);
        const bool last = offset + chunk >= raw.size();
        out.push_back(last ? 0x01 : 0x00);
        out.push_back(static_cast<std::uint8_t>(chunk & 0xFF));
        out.push_back(static_cast<std::uint8_t>((chunk >> 8) & 0xFF));
        const auto complement = static_cast<std::uint16_t>(~chunk);
        out.push_back(static_cast<std::uint8_t>(complement & 0xFF));
        out.push_back(static_cast<std::uint8_t>((complement >> 8) & 0xFF));
        out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                   raw.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        offset += chunk;
        if (raw.empty()) break;
    }
    // Adler-32 checksum of the uncompressed data.
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::uint8_t byte : raw) {
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

void append_be32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void append_chunk(std::vector<std::uint8_t>& out, const char* type,
                  const std::vector<std::uint8_t>& body)
{
    append_be32(out, static_cast<std::uint32_t>(body.size()));
    std::vector<std::uint8_t> type_and_body;
    type_and_body.insert(type_and_body.end(), type, type + 4);
    type_and_body.insert(type_and_body.end(), body.begin(), body.end());
    out.insert(out.end(), type_and_body.begin(), type_and_body.end());
    append_be32(out, bully::formats::crc32(type_and_body.data(), type_and_body.size()));
}

}  // namespace

TempDir::TempDir(std::string_view tag)
{
    std::random_device device;
    const auto suffix = std::to_string(device()) + "-" + std::to_string(g_counter.fetch_add(1));
    path_ = std::filesystem::temp_directory_path() /
            (std::string(tag) + "-" + suffix);
    std::error_code ec;
    std::filesystem::create_directories(path_, ec);
    REQUIRE_FALSE(ec);
}

TempDir::~TempDir()
{
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
}

TempDir::TempDir(TempDir&& other) noexcept : path_(std::move(other.path_))
{
    other.path_.clear();
}

TempDir& TempDir::operator=(TempDir&& other) noexcept
{
    if (this != &other) {
        std::error_code ec;
        if (!path_.empty()) std::filesystem::remove_all(path_, ec);
        path_ = std::move(other.path_);
        other.path_.clear();
    }
    return *this;
}

std::filesystem::path TempDir::operator/(std::string_view relative) const
{
    return path_ / std::filesystem::path(std::string(relative));
}

void TempDir::write(std::string_view relative, std::string_view content)
{
    const auto status = bully::core::FileSystem::write_text(
        path_ / std::filesystem::path(std::string(relative)), content);
    REQUIRE(status.has_value());
}

void TempDir::write_bytes(std::string_view relative, const std::vector<std::uint8_t>& bytes)
{
    const auto status = bully::core::FileSystem::write_binary(
        path_ / std::filesystem::path(std::string(relative)), bytes);
    REQUIRE(status.has_value());
}

std::string cube_obj()
{
    return R"(# test cube
mtllib cube.mtl
o cube
v -1 0 -1
v 1 0 -1
v 1 2 -1
v -1 2 -1
v -1 0 1
v 1 0 1
v 1 2 1
v -1 2 1
vt 0 0
vt 1 0
vt 1 1
vt 0 1
vn 0 0 -1
usemtl cube_mat
f 1/1/1 2/2/1 3/3/1 4/4/1
f 5/1/1 6/2/1 7/3/1 8/4/1
f 1/1/1 5/2/1 6/3/1 2/4/1
f 2/1/1 6/2/1 7/3/1 3/4/1
f 3/1/1 7/2/1 8/3/1 4/4/1
f 4/1/1 8/2/1 5/3/1 1/4/1
)";
}

std::string relative_index_obj()
{
    return R"(o quad
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
f -4 -3 -2 -1
)";
}

std::vector<std::uint8_t> tiny_png()
{
    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> header;
    append_be32(header, 2);  // width
    append_be32(header, 2);  // height
    header.push_back(8);     // bit depth
    header.push_back(6);     // RGBA
    header.push_back(0);     // compression
    header.push_back(0);     // filter
    header.push_back(0);     // interlace
    append_chunk(png, "IHDR", header);

    // Two rows, each prefixed with filter type 0. Colours:
    // row 0: red, green ; row 1: blue, white
    const std::vector<std::uint8_t> raw{
        0, 255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 0, 255, 255, 255, 255, 255, 255};
    append_chunk(png, "IDAT", zlib_stored(raw));
    append_chunk(png, "IEND", {});
    return png;
}

std::vector<std::uint8_t> tiny_bmp()
{
    // 2x2, 24bpp, bottom-up, rows padded to 4 bytes (2*3 = 6 -> pad to 8).
    const std::uint32_t pixel_bytes = 8 * 2;
    const std::uint32_t file_size = 54 + pixel_bytes;
    std::vector<std::uint8_t> bmp(file_size, 0);
    auto put16 = [&](std::size_t offset, std::uint16_t value) {
        bmp[offset] = static_cast<std::uint8_t>(value & 0xFF);
        bmp[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    };
    auto put32 = [&](std::size_t offset, std::uint32_t value) {
        bmp[offset] = static_cast<std::uint8_t>(value & 0xFF);
        bmp[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
        bmp[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
        bmp[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    };
    bmp[0] = 'B';
    bmp[1] = 'M';
    put32(2, file_size);
    put32(10, 54);
    put32(14, 40);
    put32(18, 2);
    put32(22, 2);  // positive height = bottom-up
    put16(26, 1);
    put16(28, 24);
    put32(30, 0);
    put32(34, pixel_bytes);
    // Bottom row (blue, white) then top row (red, green); BGR order.
    const std::uint8_t rows[2][6] = {{255, 0, 0, 255, 255, 255}, {0, 0, 255, 0, 255, 0}};
    for (int row = 0; row < 2; ++row) {
        std::memcpy(bmp.data() + 54 + row * 8, rows[row], 6);
    }
    return bmp;
}

std::vector<std::uint8_t> tiny_tga()
{
    std::vector<std::uint8_t> tga(18 + 2 * 2 * 3, 0);
    tga[2] = 2;  // uncompressed true colour
    tga[12] = 2;  // width
    tga[13] = 0;
    tga[14] = 2;  // height
    tga[15] = 0;
    tga[16] = 24;
    tga[17] = 0x20;  // top-left origin
    const std::uint8_t pixels[4][3] = {{0, 0, 255}, {0, 255, 0}, {255, 0, 0}, {255, 255, 255}};
    std::memcpy(tga.data() + 18, pixels, sizeof(pixels));
    return tga;
}

std::vector<std::uint8_t> sine_wav(std::uint32_t sample_rate, float seconds)
{
    const auto frame_count = static_cast<std::uint32_t>(
        static_cast<float>(sample_rate) * seconds);
    std::vector<std::int16_t> samples(frame_count);
    for (std::uint32_t i = 0; i < frame_count; ++i) {
        const double phase = 2.0 * 3.141592653589793 * 440.0 * static_cast<double>(i) /
                             static_cast<double>(sample_rate);
        samples[i] = static_cast<std::int16_t>(std::sin(phase) * 20000.0);
    }
    const std::uint32_t data_size = static_cast<std::uint32_t>(samples.size() * 2);
    std::vector<std::uint8_t> wav(44 + data_size, 0);
    auto put32 = [&](std::size_t offset, std::uint32_t value) {
        wav[offset] = static_cast<std::uint8_t>(value & 0xFF);
        wav[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
        wav[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
        wav[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    };
    auto put16 = [&](std::size_t offset, std::uint16_t value) {
        wav[offset] = static_cast<std::uint8_t>(value & 0xFF);
        wav[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    };
    std::memcpy(wav.data(), "RIFF", 4);
    put32(4, 36 + data_size);
    std::memcpy(wav.data() + 8, "WAVE", 4);
    std::memcpy(wav.data() + 12, "fmt ", 4);
    put32(16, 16);
    put16(20, 1);  // PCM
    put16(22, 1);  // mono
    put32(24, sample_rate);
    put32(28, sample_rate * 2);
    put16(32, 2);
    put16(34, 16);
    std::memcpy(wav.data() + 36, "data", 4);
    put32(40, data_size);
    std::memcpy(wav.data() + 44, samples.data(), data_size);
    return wav;
}

std::vector<std::uint8_t> triangle_glb()
{
    // Buffer layout: 3 positions (vec3 float) then 3 indices (uint16) + pad.
    const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const float normals[9] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    const std::uint16_t indices[3] = {0, 1, 2};
    std::vector<std::uint8_t> bin;
    bin.resize(36 + 36 + 6 + 2, 0);
    std::memcpy(bin.data(), positions, 36);
    std::memcpy(bin.data() + 36, normals, 36);
    std::memcpy(bin.data() + 72, indices, 6);

    const std::string json = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"name": "triangle", "mesh": 0}],
  "meshes": [{"name": "triangle", "primitives": [
      {"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "mode": 4}]}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 6}
  ],
  "buffers": [{"byteLength": 78}]
})";

    std::vector<std::uint8_t> glb;
    auto put32 = [&](std::vector<std::uint8_t>& out, std::uint32_t value) {
        out.push_back(static_cast<std::uint8_t>(value & 0xFF));
        out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    };
    // Pad JSON to a 4-byte boundary with spaces, as the spec requires.
    std::string json_padded = json;
    while (json_padded.size() % 4 != 0) json_padded.push_back(' ');
    const std::uint32_t json_length = static_cast<std::uint32_t>(json_padded.size());
    const std::uint32_t bin_length = static_cast<std::uint32_t>(bin.size());
    const std::uint32_t total = 12 + 8 + json_length + 8 + bin_length;
    std::memcpy(glb.data(), "", 0);
    put32(glb, 0x46546C67u);  // "glTF"
    put32(glb, 2);            // version
    put32(glb, total);
    put32(glb, json_length);
    put32(glb, 0x4E4F534Au);  // "JSON"
    glb.insert(glb.end(), json_padded.begin(), json_padded.end());
    put32(glb, bin_length);
    put32(glb, 0x004E4942u);  // "BIN"
    glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

std::string dialogue_source_json()
{
    return R"({
  "name": "test_conversation",
  "language": "en",
  "entry_node": "n_start",
  "characters": [
    {"id": "chr.a", "name": "Alice"},
    {"id": "chr.b", "name": "Bob"}
  ],
  "lines": [
    {"id": "line.1", "character": "chr.a", "text": "Hello.", "audio_start": 0.0,
     "audio_end": 1.0, "selected_take": "t1",
     "takes": [{"id": "t1", "audio": "audio.hello", "start": 0.0, "end": 1.0, "selected": true}]},
    {"id": "line.2", "character": "chr.b", "text": "Hi there.", "audio_start": 1.0,
     "audio_end": 2.5}
  ],
  "nodes": [
    {"id": "n_start", "kind": 0, "line": "line.1", "next": "n_choice"},
    {"id": "n_choice", "kind": 1, "branches": [
        {"label": "Continue", "target": "n_second"},
        {"label": "Leave", "target": "n_end", "flag": "met_alice", "value": "true"}]},
    {"id": "n_second", "kind": 0, "line": "line.2", "next": "n_end"},
    {"id": "n_end", "kind": 5}
  ]
})";
}

std::string scene_source_json()
{
    return R"({
  "name": "test_scene",
  "environment": {"fog_enabled": true},
  "entities": [
    {"id": "ent.a", "name": "A", "position": [1.0, 2.0, 3.0]},
    {"id": "ent.b", "name": "B", "parent": "ent.a", "position": [0.0, 0.0, 0.0]}
  ],
  "lights": [{"id": "light.a", "type": 1, "intensity": 2.0}],
  "triggers": [{"id": "trg.a", "kind": "mission"}],
  "spawns": [{"id": "spawn.a", "kind": "player"}]
})";
}

std::string mission_source_json()
{
    return R"({
  "name": "test_mission",
  "start_node": "n.a",
  "objectives": [
    {"id": "obj.a", "description": "Do the thing", "kind": 0, "count": 1}
  ],
  "nodes": [
    {"id": "n.a", "objective": "obj.a", "edges": [{"to": "n.b"}]},
    {"id": "n.b"}
  ],
  "conditions": [{"id": "cnd.a", "kind": 1, "key": "obj.a"}]
})";
}

}  // namespace bully::test
