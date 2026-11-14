// Bully Studio - persistent asset registry.
//
// The registry is the single source of truth for "which assets exist in this
// project". It stores AssetRecord rows in SQLite and offers the queries the
// Asset Browser, Build Manager and QA dashboard need. All writes go through
// a transaction so a crash never leaves a half-imported asset behind.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/pipeline/asset.h"
#include "bully/project/database.h"

namespace bully::project {

using pipeline::AssetRecord;
using pipeline::AssetType;
using pipeline::BuildStatus;

struct RegistryStatistics {
    std::uint32_t total = 0;
    std::uint32_t up_to_date = 0;
    std::uint32_t pending = 0;
    std::uint32_t failed = 0;
    std::uint32_t unknown = 0;
};

class AssetRegistry {
public:
    /// Requires an open, migrated database.
    explicit AssetRegistry(Database& database);

    [[nodiscard]] core::Status initialize_schema();

    [[nodiscard]] core::Status upsert(const AssetRecord& record);
    [[nodiscard]] core::Status upsert_all(const std::vector<AssetRecord>& records);
    [[nodiscard]] core::Status remove(std::string_view asset_id);

    [[nodiscard]] std::optional<AssetRecord> find(std::string_view asset_id) const;
    [[nodiscard]] std::optional<AssetRecord> find_by_source_path(std::string_view path) const;
    [[nodiscard]] std::vector<AssetRecord> all() const;
    [[nodiscard]] std::vector<AssetRecord> by_type(AssetType type) const;
    [[nodiscard]] std::vector<AssetRecord> by_status(BuildStatus status) const;
    /// Substring search over name, path and asset id (case-insensitive).
    [[nodiscard]] std::vector<AssetRecord> search(std::string_view query) const;

    [[nodiscard]] core::Status set_status(std::string_view asset_id, BuildStatus status,
                                          std::string error = {});
    [[nodiscard]] core::Status set_output(std::string_view asset_id, std::string runtime_path,
                                          std::string output_hash, std::string version);

    /// Replaces the asset's recorded dependencies (called after an import so the
    /// dependency graph, incremental builds, QA and packaging see the references
    /// the importer discovered).
    [[nodiscard]] core::Status set_dependencies(
        std::string_view asset_id, const std::vector<pipeline::AssetDependency>& dependencies);

    /// Assets that list `asset_id` as a dependency.
    [[nodiscard]] std::vector<AssetRecord> dependents_of(std::string_view asset_id) const;

    [[nodiscard]] RegistryStatistics statistics() const;
    [[nodiscard]] std::uint32_t count() const;

    /// Returns asset ids whose source file is missing on disk. `project_root`
    /// is used to resolve project-relative source paths.
    [[nodiscard]] std::vector<std::string> missing_source_files(
        const std::string& project_root) const;

private:
    Database& db_;
};

}  // namespace bully::project
