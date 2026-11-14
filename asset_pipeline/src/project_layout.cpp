
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "bully/pipeline/project_layout.h"

#include "bully/core/fs.h"
#include "bully/core/strings.h"

namespace bully::pipeline {

namespace {

constexpr const char* kManifestFormat = "bully-project";

}  // namespace

ProjectLayout::ProjectLayout(std::filesystem::path root) : root_(std::move(root)) {}

std::vector<std::filesystem::path> ProjectLayout::required_directories() const
{
    return {root_, source_dir(),    content_dir(),  built_dir(),   packages_dir(),
            reports_dir(), cache_dir(), artifacts_dir()};
}

core::Result<ProjectSettings> ProjectLayout::load() const
{
    core::DiagnosticList diagnostics;
    if (!core::FileSystem::exists(manifest_path())) {
        diagnostics.error("BULLY-PROJ-0001",
                          "no project manifest found at '" + manifest_path().string() + "'",
                          manifest_path().string());
        return core::Result<ProjectSettings>(std::move(diagnostics));
    }
    auto text = core::FileSystem::read_text(manifest_path());
    if (!text) return core::Result<ProjectSettings>(text.diagnostics());
    auto parsed = core::json::parse(text.value());
    if (!parsed) return core::Result<ProjectSettings>(parsed.diagnostics());
    const auto& root = parsed.value();
    if (root.get_string("format") != kManifestFormat) {
        diagnostics.error("BULLY-PROJ-0002", "manifest format marker is missing or unknown",
                          manifest_path().string());
        return core::Result<ProjectSettings>(std::move(diagnostics));
    }
    const auto version = core::Version::from_string(root.get_string("format_version", "1.0.0"));
    if (version.major != 1) {
        diagnostics.error("BULLY-PROJ-0003",
                          "project format v" + version.to_string() +
                              " is not supported by this build (expected 1.x)",
                          manifest_path().string());
        return core::Result<ProjectSettings>(std::move(diagnostics));
    }
    return project_settings_from_json(root);
}

core::Status ProjectLayout::save(const ProjectSettings& settings) const
{
    return core::json::save_file(manifest_path().string(),
                                 project_settings_to_json(settings), 2);
}

core::Status ProjectLayout::initialize(const ProjectSettings& settings) const
{
    for (const auto& directory : required_directories()) {
        auto status = core::FileSystem::create_directories(directory);
        if (!status) return status;
    }
    // Keep placeholder files so the skeleton survives git clones.
    for (const auto& directory :
         {source_dir(), content_dir(), built_dir(), packages_dir(), reports_dir()}) {
        const auto keep = directory / ".gitkeep";
        if (!core::FileSystem::exists(keep)) (void)core::FileSystem::write_text(keep, "");
    }
    return save(settings);
}

bool ProjectLayout::is_project(const std::filesystem::path& path)
{
    return core::FileSystem::exists(path / "project.bullyproj");
}

core::Result<std::filesystem::path> ProjectLayout::resolve_inside(std::string_view relative) const
{
    core::DiagnosticList diagnostics;
    const auto resolved = core::FileSystem::resolve(root_, relative);
    if (!core::FileSystem::is_within(resolved, root_)) {
        diagnostics.error("BULLY-PROJ-0004",
                          "path '" + std::string(relative) + "' escapes the project root",
                          root_.string());
        return core::Result<std::filesystem::path>(std::move(diagnostics));
    }
    return resolved;
}

std::filesystem::path ProjectLayout::runtime_output_path(std::string_view relative_source,
                                                        std::string_view runtime_extension) const
{
    std::filesystem::path relative{std::string(relative_source)};
    // Strip the first directory component (assets/ or content/) so the mirror
    // stays flat under built/.
    auto it = relative.begin();
    if (it != relative.end()) {
        const std::string first = it->string();
        if (first == "assets" || first == "content") relative = relative.lexically_relative(first);
    }
    relative.replace_extension(runtime_extension);
    return built_dir() / relative;
}

core::json::Value project_settings_to_json(const ProjectSettings& settings)
{
    core::json::Value root = core::json::Value::make_object();
    root["format"] = kManifestFormat;
    root["format_version"] = settings.format_version.to_string();
    root["name"] = settings.name;
    root["engine_version"] = settings.engine_version.to_string();
    root["template"] = settings.template_name;
    root["target_platform"] = settings.target_platform;
    root["retro_preset"] = settings.retro_preset;
    root["default_language"] = settings.default_language;
    root["content_version"] = settings.content_version;
    root["build_profile"] = settings.build_profile;
    core::json::Value modules = core::json::Value::make_array();
    for (const auto& module : settings.runtime_modules) modules.push(module);
    root["runtime_modules"] = std::move(modules);
    return root;
}

ProjectSettings project_settings_from_json(const core::json::Value& value)
{
    ProjectSettings settings;
    settings.name = value.get_string("name", settings.name);
    settings.format_version = core::Version::from_string(value.get_string("format_version", "1.0.0"));
    settings.engine_version = core::Version::from_string(value.get_string("engine_version", "0.1.0"));
    settings.template_name = value.get_string("template");
    settings.target_platform = value.get_string("target_platform", "linux-x86_64");
    settings.retro_preset = value.get_string("retro_preset", "none");
    settings.default_language = value.get_string("default_language", "en");
    settings.content_version = value.get_string("content_version", "0.1.0");
    settings.build_profile = value.get_string("build_profile", "release");
    if (const core::json::Value* modules = value.find("runtime_modules");
        modules != nullptr && modules->is_array()) {
        for (const auto& module : modules->items()) settings.runtime_modules.push_back(module.as_string());
    }
    return settings;
}

}  // namespace bully::pipeline
