// Bully Studio - project layout and manifest (.bullyproj).
//
// A project directory has this documented structure (no hardcoded absolute
// paths anywhere in the platform):
//
//   <project>/
//     project.bullyproj        JSON manifest (name, version, settings)
//     project.db               SQLite metadata (assets, cache, reports)
//     assets/                  source assets - never modified by the pipeline
//     content/                 authored JSON sources (scenes, missions, dialogue)
//     built/                   compiled runtime assets (.bmodel, ...)
//     packages/                .bpackage output
//     reports/                 QA / build reports (JSON + Markdown)
//     .bully-cache/            import cache (safe to delete)
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/core/json.h"
#include "bully/core/version.h"

namespace bully::pipeline {

struct ProjectSettings {
    std::string name = "Untitled Project";
    core::Version format_version{1, 0, 0};
    core::Version engine_version = core::kPlatformVersion;
    std::string template_name;
    std::string target_platform = "linux-x86_64";
    std::string retro_preset = "none";
    std::string default_language = "en";
    std::string content_version = "0.1.0";
    /// Additional runtime module names declared by the project template.
    std::vector<std::string> runtime_modules;
    std::string build_profile = "release";
};

class ProjectLayout {
public:
    explicit ProjectLayout(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

    [[nodiscard]] std::filesystem::path manifest_path() const { return root_ / "project.bullyproj"; }
    [[nodiscard]] std::filesystem::path database_path() const { return root_ / "project.db"; }
    [[nodiscard]] std::filesystem::path cache_dir() const { return root_ / ".bully-cache"; }
    [[nodiscard]] std::filesystem::path artifacts_dir() const { return cache_dir() / "artifacts"; }
    [[nodiscard]] std::filesystem::path source_dir() const { return root_ / "assets"; }
    [[nodiscard]] std::filesystem::path content_dir() const { return root_ / "content"; }
    [[nodiscard]] std::filesystem::path built_dir() const { return root_ / "built"; }
    [[nodiscard]] std::filesystem::path packages_dir() const { return root_ / "packages"; }
    [[nodiscard]] std::filesystem::path reports_dir() const { return root_ / "reports"; }
    [[nodiscard]] std::filesystem::path templates_dir() const { return root_ / "templates"; }

    /// Directories a project needs on disk, in creation order.
    [[nodiscard]] std::vector<std::filesystem::path> required_directories() const;

    /// Loads the manifest. Fails with a clear diagnostic when absent/corrupt.
    [[nodiscard]] core::Result<ProjectSettings> load() const;
    [[nodiscard]] core::Status save(const ProjectSettings& settings) const;

    /// Creates the directory skeleton. Never deletes anything.
    [[nodiscard]] core::Status initialize(const ProjectSettings& settings) const;

    /// True when `path` contains a project manifest.
    [[nodiscard]] static bool is_project(const std::filesystem::path& path);

    /// Resolves a project-relative path and refuses path escapes.
    [[nodiscard]] core::Result<std::filesystem::path> resolve_inside(std::string_view relative) const;

    /// Where the compiled asset for `relative_source` should be written.
    /// Always inside built/, mirroring the source tree.
    [[nodiscard]] std::filesystem::path runtime_output_path(std::string_view relative_source,
                                                           std::string_view runtime_extension) const;

private:
    std::filesystem::path root_;
};

[[nodiscard]] core::json::Value project_settings_to_json(const ProjectSettings& settings);
[[nodiscard]] ProjectSettings project_settings_from_json(const core::json::Value& value);

}  // namespace bully::pipeline
