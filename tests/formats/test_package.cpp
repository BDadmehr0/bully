#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "bully/formats/package.h"

using namespace bully::formats;

namespace {

PackageDocument make_package()
{
    PackageDocument document;
    document.asset_id = "pkg-1";
    document.name = "demo";
    document.target_platform = "linux-x86_64";
    document.build_profile = "release";
    REQUIRE(document.add(PackageEntryKind::Model, "models/crate.bmodel", "asset-model",
                         std::vector<std::uint8_t>{1, 2, 3}).has_value());
    REQUIRE(document.add(PackageEntryKind::Dialogue, "dialogue/intro.bdialogue", "asset-dialogue",
                         std::vector<std::uint8_t>{9, 9, 9, 9}).has_value());
    document.manifest_json = "{\"built_by\":\"test\"}";
    return document;
}

}  // namespace

TEST_CASE("bpackage round-trips entries and payloads", "[formats][package]")
{
    const auto original = make_package();
    auto decoded = PackageDocument{}.deserialize(original.serialize());
    REQUIRE(decoded.has_value());
    const auto& package = decoded.value();
    REQUIRE(package.entries.size() == 2);
    CHECK(package.find("asset-model") != nullptr);
    auto extracted = package.extract("asset-dialogue");
    REQUIRE(extracted.has_value());
    CHECK(extracted.value() == std::vector<std::uint8_t>{9, 9, 9, 9});
    CHECK(package.verify().has_value());
    CHECK(package.validate().has_value());
    CHECK(package.manifest_json == "{\"built_by\":\"test\"}");
}

TEST_CASE("Package rejects duplicate asset ids and empty payloads", "[formats][package]")
{
    auto package = make_package();
    CHECK_FALSE(package.add(PackageEntryKind::Model, "models/crate.bmodel", "asset-model",
                            std::vector<std::uint8_t>{1})
                    .has_value());
    CHECK_FALSE(package.add(PackageEntryKind::Model, "models/x.bmodel", "asset-x",
                            std::vector<std::uint8_t>{})
                    .has_value());
}

TEST_CASE("Package verify reports the broken entry", "[formats][package]")
{
    auto package = make_package();
    package.data[1] ^= 0xFF;  // corrupt a byte inside the first resource
    const auto status = package.verify();
    CHECK_FALSE(status.has_value());
    const auto& diagnostics = status.diagnostics().all();
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics[0].message.find("asset-model") != std::string::npos);
    CHECK_FALSE(package.extract("asset-model").has_value());
}

TEST_CASE("Missing package entries are reported, not silently empty", "[formats][package]")
{
    const auto package = make_package();
    CHECK(package.find("nope") == nullptr);
    const auto extracted = package.extract("nope");
    CHECK_FALSE(extracted.has_value());
    CHECK(extracted.diagnostics().has_errors());
}

TEST_CASE("Entry kinds map from compiled extensions", "[formats][package]")
{
    CHECK(package_kind_from_extension(".bmodel") == PackageEntryKind::Model);
    CHECK(package_kind_from_extension("btexture") == PackageEntryKind::Texture);
    CHECK(package_kind_from_extension(".banim") == PackageEntryKind::Animation);
    CHECK(package_kind_from_extension(".zzt") == PackageEntryKind::Other);
    CHECK(std::string(to_string(PackageEntryKind::Dialogue)) == "dialogue");
}

TEST_CASE("Package manifest serialises to auditable JSON", "[formats][package]")
{
    PackageManifest manifest;
    manifest.package_name = "demo";
    manifest.target_platform = "linux-x86_64";
    manifest.build_profile = "release";
    manifest.created_utc = "2026-01-01T00:00:00Z";
    manifest.tool_version = "0.1.0";
    manifest.entries = make_package().entries;
    manifest.metadata["template"] = "retro-action";
    const std::string json = manifest.to_json();
    auto round_trip = PackageManifest::from_json(json);
    REQUIRE(round_trip.has_value());
    CHECK(round_trip.value().package_name == "demo");
    REQUIRE(round_trip.value().entries.size() == 2);
    CHECK(round_trip.value().metadata.at("template") == "retro-action");
}
