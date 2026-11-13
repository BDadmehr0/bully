// Bully Studio - pipeline kernel.
//
// Responsibilities:
//   * scanning source trees and assigning stable asset ids,
//   * running importers in dependency order,
//   * incremental rebuilds driven by source/settings hashes and the cache,
//   * hot reload notifications,
//   * build reports with per-asset diagnostics.
//
// The kernel is headless and thread-free by default (deterministic ordering);
// callers that want parallelism run multiple Pipeline instances over disjoint
// asset sets. This keeps the first implementation honest and testable.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/pipeline/asset.h"
#include "bully/pipeline/import_cache.h"
#include "bully/pipeline/importer.h"
#include "bully/pipeline/project_layout.h"
#include "bully/project/asset_registry.h"
#include "bully/project/database.h"

namespace bully::pipeline {

struct BuildOptions {
    /// Re-import even when hashes match.
    bool force = false;
    /// Verify every asset and report problems without writing outputs.
    bool dry_run = false;
    /// Stop after the first failed asset (dependency order preserved either way).
    bool stop_on_error = false;
    /// Collect `.bdialogue`/QA level reports for every asset.
    bool detailed_reports = true;
};

struct AssetBuildResult {
    std::string asset_id;
    std::string source_path;
    bool success = false;
    bool skipped = false;      // up-to-date or cache hit
    bool from_cache = false;
    std::uint32_t duration_ms = 0;
    std::string summary;
    std::string output_path;
    std::string output_hash;
    core::DiagnosticList diagnostics;
    AssetType asset_type = AssetType::Unknown;
    std::string report_json;
};

struct BuildReport {
    std::string project_name;
    std::string started_utc;
    std::uint32_t total = 0;
    std::uint32_t built = 0;
    std::uint32_t cached = 0;
    std::uint32_t up_to_date = 0;
    std::uint32_t failed = 0;
    std::uint32_t skipped = 0;
    std::uint32_t duration_ms = 0;
    std::vector<AssetBuildResult> results;

    [[nodiscard]] bool success() const noexcept { return failed == 0; }
    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] std::string to_markdown() const;
};

/// Receives build progress; used by the Studio build manager and the CLI.
using ProgressCallback = std::function<void(std::size_t index, std::size_t total,
                                            const AssetBuildResult&)>;
using HotReloadCallback = std::function<void(const AssetBuildResult&)>;

struct ScanResult {
    std::vector<AssetRecord> discovered;  // new assets found on disk
    std::vector<std::string> removed;     // ids no longer present
    std::vector<std::string> changed;     // ids whose source hash changed
    core::DiagnosticList diagnostics;
};

class Pipeline {
public:
    Pipeline(ProjectLayout layout, project::Database& database, project::AssetRegistry& registry,
             ImporterRegistry& importers, ImportCache& cache);

    /// Scans the source and content directories for importable files.
    /// `extensions` defaults to every extension claimed by a registered importer.
    [[nodiscard]] ScanResult scan(const std::vector<std::string>& extensions = {});

    /// Imports a single asset by id (rebuilding its dependencies first when
    /// `build_dependencies` is true).
    [[nodiscard]] AssetBuildResult build_asset(std::string_view asset_id, const BuildOptions& options,
                                               bool build_dependencies = true);

    /// Builds everything pending (or every asset when `options.force`).
    [[nodiscard]] BuildReport build_all(const BuildOptions& options,
                                        ProgressCallback progress = {});

    /// Builds a subset, in dependency order.
    [[nodiscard]] BuildReport build_assets(const std::vector<std::string>& asset_ids,
                                           const BuildOptions& options,
                                           ProgressCallback progress = {});

    /// Which assets would be rebuilt by `build_all` right now.
    [[nodiscard]] std::vector<std::string> pending_assets() const;

    /// Topologically sorted asset ids (dependencies first). Cyclic dependencies
    /// are reported as diagnostics and excluded.
    [[nodiscard]] core::Result<std::vector<std::string>> build_order(
        const std::vector<std::string>& asset_ids) const;

    /// Writes the report into <project>/reports/.
    [[nodiscard]] core::Status write_report(const BuildReport& report,
                                            std::string_view file_stem = "build-report") const;

    /// Files changed on disk since the last successful build (polling hot
    /// reload; watches are the Studio's job, verification is the kernel's).
    [[nodiscard]] std::vector<AssetBuildResult> poll_changes();

    void set_hot_reload_callback(HotReloadCallback callback) { hot_reload_ = std::move(callback); }

    [[nodiscard]] const ProjectLayout& layout() const noexcept { return layout_; }

private:
    [[nodiscard]] std::optional<AssetRecord> make_record_for_source(
        const std::filesystem::path& source) const;
    [[nodiscard]] std::string assign_asset_id(std::string_view relative_source) const;
    /// Retro preset declared by the project manifest ("none" when unset).
    [[nodiscard]] std::string project_retro_preset() const;

    ProjectLayout layout_;
    project::Database& database_;
    project::AssetRegistry& registry_;
    ImporterRegistry& importers_;
    ImportCache& cache_;
    HotReloadCallback hot_reload_;
    std::map<std::string, std::int64_t> known_mtimes_;
    mutable std::string project_retro_preset_;
    mutable bool project_preset_loaded_ = false;
};

/// Utility: assigns a deterministic asset id from a project-relative source
/// path. Importers use this so re-imports of the same file keep their identity.
[[nodiscard]] std::string deterministic_asset_id(std::string_view relative_source);

}  // namespace bully::pipeline
