// Bully Studio - .bpackage : distributable asset package.
//
// A package is a container: it embeds compiled resources (.bmodel, .btexture,
// ...) with a table of contents so the runtime can mount a package and load
// assets by id without extracting files to disk.
//
// v1.0 chunks:
//   INFO  package identity, target platform, build profile, counts
//   ENTR  table-of-contents entries (one record per asset)
//   DATA  concatenated resource payloads, addressed by ENTR offsets
//   META  JSON manifest (build settings, QA summary, provenance)
//
// Entries keep the CRC-32 of each resource, so mounting a package validates
// integrity per asset and reports the exact broken entry instead of failing
// the whole package.
//
// Diagnostic codes: BULLY-PKG-0001..0029
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/formats/binary.h"

namespace bully::formats {

inline constexpr FormatDescriptor kPackageFormat{
    fourcc("BPKG"), "bpackage", {1, 0, 0}, ".bpackage",
    "Distributable asset package: table of contents + embedded resources"};

/// Resource kind stored in a package entry.
enum class PackageEntryKind : std::uint8_t {
    Model = 0,
    Texture = 1,
    Material = 2,
    Animation = 3,
    Audio = 4,
    Dialogue = 5,
    Scene = 6,
    Mission = 7,
    Other = 255,
};

[[nodiscard]] std::string_view to_string(PackageEntryKind kind) noexcept;
[[nodiscard]] PackageEntryKind package_kind_from_extension(std::string_view extension) noexcept;

struct PackageEntry {
    std::string asset_id;
    PackageEntryKind kind = PackageEntryKind::Other;
    std::string logical_path;  // "models/students/missy.bmodel"
    std::uint32_t offset = 0;  // relative to the DATA chunk body start
    std::uint32_t size = 0;
    std::uint32_t crc = 0;
    std::uint32_t flags = 0;
};

struct PackageDocument {
    std::string asset_id;   // package id
    std::string name;
    std::string target_platform = "linux-x86_64";
    std::string build_profile = "release";
    core::Version content_version{1, 0, 0};
    std::vector<PackageEntry> entries;
    std::vector<std::uint8_t> data;
    std::string manifest_json;

    [[nodiscard]] const PackageEntry* find(std::string_view lookup_id) const;
    /// Extracts one resource by asset id, verifying its CRC.
    [[nodiscard]] core::Result<std::vector<std::uint8_t>> extract(std::string_view lookup_id) const;
    /// Verifies every entry CRC. Reports per-entry failures.
    [[nodiscard]] core::Status verify() const;

    /// Adds a resource, computing size/CRC and appending to the data block.
    /// `asset_id` must be unique inside the package.
    [[nodiscard]] core::Status add(PackageEntryKind kind, std::string logical_path,
                                   std::string new_asset_id, const std::vector<std::uint8_t>& bytes);

    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    [[nodiscard]] core::Result<PackageDocument> deserialize(const std::vector<std::uint8_t>& bytes) const;
    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] std::string to_json() const;
};

/// Manifest describing a package build, written next to the .bpackage.
/// Keeping the manifest in JSON makes build reproducibility auditable and lets
/// the CLI print a summary without decoding the package.
struct PackageManifest {
    std::string package_name;
    std::string target_platform;
    std::string build_profile;
    std::string created_utc;
    std::string tool_version;
    bool deterministic = true;
    std::vector<PackageEntry> entries;
    std::map<std::string, std::string> metadata;

    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] static core::Result<PackageManifest> from_json(std::string_view text);
};

}  // namespace bully::formats
