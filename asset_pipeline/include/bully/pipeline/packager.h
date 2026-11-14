// Bully Studio - asset packaging.
//
// Takes the compiled assets of a project and produces a distributable
// .bpackage plus a JSON manifest. Packaging is deterministic: entries are
// sorted by logical path, timestamps are optional, and the manifest records the
// exact content hashes so builds can be compared byte-for-byte.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "bully/core/error.h"
#include "bully/formats/package.h"
#include "bully/pipeline/pipeline.h"

namespace bully::pipeline {

struct PackageOptions {
    std::string package_name;
    std::string target_platform = "linux-x86_64";
    std::string build_profile = "release";
    /// Include assets whose build status is not UpToDate (default: include them
    /// and record a warning, so incomplete packages are never silent).
    bool include_stale = true;
    /// Only package these asset types (empty = all runtime assets).
    std::vector<AssetType> include_types;
    /// Embed an ISO-8601 creation timestamp in the manifest. Disable for
    /// reproducible builds.
    bool include_timestamp = true;
    std::string output_name = "game";
};

struct PackageResult {
    bool success = false;
    std::string package_path;
    std::string manifest_path;
    std::uint32_t entry_count = 0;
    std::uint64_t total_bytes = 0;
    core::DiagnosticList diagnostics;
};

class Packager {
public:
    Packager(ProjectLayout layout, project::AssetRegistry& registry);

    /// Packs every compiled asset that lives under <project>/built.
    [[nodiscard]] PackageResult build_package(const PackageOptions& options) const;

    /// Writes a manifest only (useful for CI diffing).
    [[nodiscard]] core::Status write_manifest(const formats::PackageManifest& manifest,
                                             const std::filesystem::path& path) const;

private:
    ProjectLayout layout_;
    project::AssetRegistry& registry_;
};

}  // namespace bully::pipeline
