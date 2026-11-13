// Shared fixture for pipeline/integration tests: a throwaway project directory
// with an open database, registry, import cache and pipeline instance.
#pragma once

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>

#include "bully/pipeline/builtin_importers.h"
#include "bully/pipeline/import_cache.h"
#include "bully/pipeline/pipeline.h"
#include "bully/project/asset_registry.h"
#include "bully/project/database.h"
#include "test_support.h"

namespace bully::test {

/// Builds a scratch project (manifest + database + directories) and keeps the
/// pipeline objects alive for the duration of the test.
class ProjectFixture {
public:
    explicit ProjectFixture(std::string_view name = "fixture")
    {
        layout_ = std::make_unique<pipeline::ProjectLayout>(dir_.path());
        pipeline::ProjectSettings settings;
        settings.name = std::string(name);
        const auto status = layout_->initialize(settings);
        REQUIRE(status.has_value());

        auto database = project::Database::open(layout_->database_path());
        REQUIRE(database.has_value());
        database_ = std::make_unique<project::Database>(std::move(database.value()));
        registry_ = std::make_unique<project::AssetRegistry>(*database_);
        REQUIRE(registry_->initialize_schema().has_value());
        cache_ = std::make_unique<pipeline::ImportCache>(*database_, layout_->cache_dir());
        REQUIRE(cache_->initialize().has_value());

        importers_.clear();
        pipeline::register_builtin_importers(importers_);
        pipeline_ = std::make_unique<pipeline::Pipeline>(*layout_, *database_, *registry_, importers_,
                                                         *cache_);
    }

    ~ProjectFixture() { importers_.clear(); }

    ProjectFixture(const ProjectFixture&) = delete;
    ProjectFixture& operator=(const ProjectFixture&) = delete;

    [[nodiscard]] pipeline::Pipeline& pipeline() { return *pipeline_; }
    [[nodiscard]] pipeline::ProjectLayout& layout() { return *layout_; }
    [[nodiscard]] project::AssetRegistry& registry() { return *registry_; }
    [[nodiscard]] project::Database& database() { return *database_; }
    [[nodiscard]] pipeline::ImportCache& cache() { return *cache_; }
    [[nodiscard]] pipeline::ImporterRegistry& importers() { return importers_; }
    [[nodiscard]] TempDir& temp_dir() { return dir_; }

    /// Writes a source file inside assets/ and returns its project-relative path.
    std::string write_asset(std::string_view relative, std::string_view content)
    {
        const std::string path = "assets/" + std::string(relative);
        dir_.write(path, content);
        return path;
    }
    std::string write_asset_bytes(std::string_view relative, const std::vector<std::uint8_t>& bytes)
    {
        const std::string path = "assets/" + std::string(relative);
        dir_.write_bytes(path, bytes);
        return path;
    }
    /// Writes a file under content/ (authored scenes/missions/dialogue).
    std::string write_content(std::string_view relative, std::string_view content)
    {
        const std::string path = "content/" + std::string(relative);
        dir_.write(path, content);
        return path;
    }

    /// Scans and builds everything; returns the report.
    pipeline::BuildReport build_all(bool force = false)
    {
        const auto scan = pipeline_->scan();
        REQUIRE(scan.diagnostics.has_errors() == false);
        pipeline::BuildOptions options;
        options.force = force;
        return pipeline_->build_all(options);
    }

    [[nodiscard]] std::optional<pipeline::AssetRecord> record_for(std::string_view source_path)
    {
        return registry_->find_by_source_path(source_path);
    }

    [[nodiscard]] std::vector<std::uint8_t> read_compiled(std::string_view source_path)
    {
        auto record = record_for(source_path);
        REQUIRE(record.has_value());
        auto bytes = core::FileSystem::read_binary(layout_->root() / record->runtime_path);
        REQUIRE(bytes.has_value());
        return bytes.value();
    }

private:
    TempDir dir_;
    std::unique_ptr<pipeline::ProjectLayout> layout_;
    std::unique_ptr<project::Database> database_;
    std::unique_ptr<project::AssetRegistry> registry_;
    std::unique_ptr<pipeline::ImportCache> cache_;
    pipeline::ImporterRegistry& importers_ = pipeline::ImporterRegistry::instance();
    std::unique_ptr<pipeline::Pipeline> pipeline_;
};

}  // namespace bully::test
