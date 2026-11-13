// Bully Studio - import cache.
//
// The cache maps a *build key* (importer version + source hash + settings hash)
// to the compiled artifact on disk. Two benefits:
//   * incremental rebuilds: unchanged assets are not re-imported,
//   * content-addressed artifacts: two assets with identical inputs share one
//     output file, and reverting a source file instantly hits a known-good
//     artifact.
//
// Backed by the project SQLite database (build_cache table) and the
// `.bully-cache/artifacts` directory inside the project.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "bully/core/error.h"
#include "bully/project/database.h"

namespace bully::pipeline {

struct CacheEntry {
    std::string build_key;
    std::string asset_id;
    std::string output_hash;
    std::string output_path;   // absolute path of the cached artifact
    std::int64_t created_unix = 0;
    std::string tool_version;
};

class ImportCache {
public:
    ImportCache(project::Database& database, std::filesystem::path cache_root);

    [[nodiscard]] core::Status initialize();

    [[nodiscard]] std::optional<CacheEntry> find(std::string_view build_key) const;
    /// Stores an artifact. `bytes` are written under the content hash name so
    /// identical outputs are deduplicated.
    [[nodiscard]] core::Status store(std::string_view build_key, std::string_view asset_id,
                                     const std::vector<std::uint8_t>& bytes,
                                     std::string_view tool_version);
    /// Copies a cached artifact to `destination`. Verifies the hash first.
    [[nodiscard]] core::Status materialize(std::string_view build_key,
                                           const std::filesystem::path& destination) const;

    [[nodiscard]] core::Status clear();
    [[nodiscard]] std::uint32_t entry_count() const;
    [[nodiscard]] std::uint64_t total_bytes() const;
    const std::filesystem::path& root() const noexcept { return cache_root_; }

private:
    [[nodiscard]] std::filesystem::path artifact_path(std::string_view output_hash) const;

    project::Database& db_;
    std::filesystem::path cache_root_;
};

}  // namespace bully::pipeline
