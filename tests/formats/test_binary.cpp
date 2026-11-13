#include <catch2/catch_test_macros.hpp>

#include "bully/formats/binary.h"

using namespace bully::formats;
namespace core = bully::core;

TEST_CASE("crc32 matches known vectors", "[formats][binary]")
{
    CHECK(crc32(std::string_view("123456789")) == 0xCBF43926u);
    CHECK(crc32(std::string_view("")) == 0u);
}

TEST_CASE("fourcc packs in little-endian order", "[formats][binary]")
{
    const std::uint32_t id = fourcc("BMOD");
    CHECK(fourcc_to_string(id) == "BMOD");
    // `fourcc()` packs ASCII in reading order ('B' in the high byte), while the
    // file header stores the same integer little-endian, so the first byte on
    // disk is always the first letter of the magic.
    CHECK((id >> 24) == static_cast<std::uint32_t>('B'));
    CHECK((id & 0xFFu) == static_cast<std::uint32_t>('D'));
}

TEST_CASE("BinaryWriter/BinaryReader round-trip every primitive", "[formats][binary]")
{
    BinaryWriter writer;
    writer.u8(200);
    writer.u16(65535);
    writer.u32(4000000000u);
    writer.u64(0xDEADBEEFCAFEBABEull);
    writer.i32(-42);
    writer.i64(-9000000000ll);
    writer.f32(3.5f);
    writer.f64(1.0 / 3.0);
    writer.boolean(true);
    writer.string("hello formats");
    writer.vec3(Vec3f{1.0f, 2.0f, 3.0f});
    writer.mat4(Mat4f::identity());

    BinaryReader reader(writer.bytes().data(), writer.bytes().size());
    CHECK(reader.u8() == 200);
    CHECK(reader.u16() == 65535);
    CHECK(reader.u32() == 4000000000u);
    CHECK(reader.u64() == 0xDEADBEEFCAFEBABEull);
    CHECK(reader.i32() == -42);
    CHECK(reader.i64() == -9000000000ll);
    CHECK(reader.f32() == 3.5f);
    CHECK(reader.f64() == 1.0 / 3.0);
    CHECK(reader.boolean());
    CHECK(reader.string() == "hello formats");
    const Vec3f vector = reader.vec3();
    CHECK(vector.y == 2.0f);
    CHECK(reader.mat4().m[0] == 1.0f);
    CHECK(reader.ok());
}

TEST_CASE("BinaryReader reports truncated reads", "[formats][binary]")
{
    const std::uint8_t data[2] = {1, 2};
    core::DiagnosticList diagnostics;
    BinaryReader reader(data, sizeof(data), &diagnostics);
    CHECK(reader.u16() == 0x0201);
    CHECK(reader.u32() == 0);
    CHECK_FALSE(reader.ok());
    CHECK(diagnostics.has_errors());
}

TEST_CASE("Chunks patch their size on close", "[formats][binary]")
{
    BinaryWriter writer;
    writer.chunk(fourcc("TEST"), [](BinaryWriter& body) {
        body.u32(1);
        body.u32(2);
    });
    BinaryReader reader(writer.bytes().data(), writer.bytes().size());
    std::uint32_t id = 0;
    std::uint32_t size = 0;
    REQUIRE(reader.next_chunk(id, size));
    CHECK(fourcc_to_string(id) == "TEST");
    CHECK(size == 8);
    CHECK(reader.u32() == 1);
    CHECK(reader.u32() == 2);
}

TEST_CASE("Document envelope validates magic, version and CRC", "[formats][binary]")
{
    FormatDescriptor format{fourcc("BTST"), "btest", {1, 2, 0}, ".btest", "test format"};
    const std::vector<std::uint8_t> payload{1, 2, 3, 4, 5};
    const auto encoded = encode_document(format, 7, payload);
    std::uint32_t flags = 0;
    core::Version version;
    auto decoded = decode_document(format, encoded, &flags, &version);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value() == payload);
    CHECK(flags == 7);
    CHECK(version.major == 1);
    CHECK(version.minor == 2);

    SECTION("wrong magic is rejected")
    {
        auto corrupted = encoded;
        corrupted[0] = 'X';
        CHECK_FALSE(decode_document(format, corrupted).has_value());
    }
    SECTION("corrupted payload fails the CRC")
    {
        auto corrupted = encoded;
        corrupted.back() ^= 0xFF;
        auto result = decode_document(format, corrupted);
        CHECK_FALSE(result.has_value());
        CHECK(result.diagnostics().has_errors());
    }
    SECTION("incompatible major version is rejected")
    {
        auto newer = encoded;
        newer[4] = 9;  // magic major 9
        newer[5] = 0;
        CHECK_FALSE(decode_document(format, newer).has_value());
    }
    SECTION("one newer minor is accepted with a warning")
    {
        auto newer = encoded;
        newer[6] = 3;  // minor 3 while reader is 2
        auto result = decode_document(format, newer);
        CHECK(result.has_value());
        bool warned = false;
        for (const auto& item : result.diagnostics().all()) {
            if (item.severity == core::Severity::Warning) warned = true;
        }
        CHECK(warned);
    }
}

TEST_CASE("version_compatible implements the forward-compat policy", "[formats][binary]")
{
    bool newer = false;
    CHECK(version_compatible({1, 0, 0}, {1, 0, 0}, &newer));
    CHECK_FALSE(newer);
    CHECK(version_compatible({1, 2, 0}, {1, 1, 0}));
    CHECK(version_compatible({1, 1, 0}, {1, 2, 0}, &newer));
    CHECK(newer);
    CHECK_FALSE(version_compatible({1, 1, 0}, {1, 3, 0}));
    CHECK_FALSE(version_compatible({1, 0, 0}, {2, 0, 0}));
}
