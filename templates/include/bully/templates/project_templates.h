// Bully Studio - project templates.
//
// Templates define the folder structure, runtime modules, asset profiles, UI
// presets, build settings and sample content a new project starts from. They
// are data driven: `templates/<name>/template.json` plus example assets. The
// built-in catalogue is embedded so `bully_asset_cli new` works without the
// repository, and templates found in the project's `templates/` directory
// override/extend the built-ins.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bully/core/error.h"
#include "bully/pipeline/project_layout.h"

namespace bully::templates {

struct TemplateInfo {
    std::string name;
    std::string description;
    std::string retro_preset;
    std::vector<std::string> runtime_modules;
    std::vector<std::string> directories;
    /// Sample files created by the template: relative path -> content.
    std::vector<std::pair<std::string, std::string>> samples;
};

/// Catalog of built-in templates.
[[nodiscard]] const std::vector<TemplateInfo>& template_catalog();

/// Finds a template by name (nullptr when unknown).
[[nodiscard]] const TemplateInfo* find_template(std::string_view name);

/// Creates a project directory from a template.
/// Fails cleanly when the target directory exists and is non-empty.
[[nodiscard]] core::Result<pipeline::ProjectSettings> create_project(
    const std::filesystem::path& target, std::string_view template_name,
    std::string project_name = {});

}  // namespace bully::templates
