#include "bully/plugins/manifest.h"

#include <algorithm>
#include <cstdlib>

#include "bully/core/fs.h"
#include "bully/core/strings.h"

namespace bully::plugins {

const std::vector<std::string>& known_permissions()
{
    static const std::vector<std::string> permissions{
        "project.read",     "project.write",   "assets.read",    "assets.write",
        "pipeline.run",     "editor.panel",    "runtime.hook",   "network",
        "process.spawn",    "filesystem.read", "filesystem.write", "ui.dialog"};
    return permissions;
}

bool is_known_permission(std::string_view permission) noexcept
{
    const std::string lowered = core::to_lower(permission);
    const auto& permissions = known_permissions();
    return std::find(permissions.begin(), permissions.end(), lowered) != permissions.end();
}

bool version_at_least(std::string_view version, std::string_view minimum)
{
    const auto a = core::split(std::string(version), '.');
    const auto b = core::split(std::string(minimum), '.');
    const std::size_t count = std::max(a.size(), b.size());
    for (std::size_t i = 0; i < count; ++i) {
        auto parse = [](const std::vector<std::string>& parts, std::size_t index) -> long {
            if (index >= parts.size()) return 0;
            try {
                return std::strtol(parts[index].c_str(), nullptr, 10);
            } catch (...) {
                return 0;
            }
        };
        const long left = parse(a, i);
        const long right = parse(b, i);
        if (left > right) return true;
        if (left < right) return false;
    }
    return true;
}

core::Status PluginManifest::validate() const
{
    core::DiagnosticList diagnostics;
    if (id.empty()) {
        diagnostics.error("BULLY-PLUG-0001", "plugin manifest is missing its id", directory.string());
    } else if (id.find('.') == std::string::npos) {
        diagnostics.warn("BULLY-PLUG-0002",
                         "plugin id '" + id + "' should use reverse-DNS notation (com.vendor.name)",
                         directory.string());
    }
    if (name.empty()) {
        diagnostics.warn("BULLY-PLUG-0003", "plugin manifest has no display name", id);
    }
    if (version.empty()) {
        diagnostics.error("BULLY-PLUG-0004", "plugin manifest has no version", id);
    }
    if (library.empty()) {
        diagnostics.error("BULLY-PLUG-0005", "plugin manifest has no library path", id);
    } else {
        const auto library_path = directory / library;
        if (!directory.empty() && !core::FileSystem::exists(library_path)) {
            diagnostics.error("BULLY-PLUG-0006",
                              "plugin library '" + library + "' does not exist", library_path.string());
        }
    }
    if (api_major == 0) {
        diagnostics.error("BULLY-PLUG-0007", "plugin manifest declares an invalid API version", id);
    }
    for (const auto& permission : permissions) {
        if (!is_known_permission(permission)) {
            diagnostics.error("BULLY-PLUG-0008",
                              "unknown permission '" + permission +
                                  "' - the host cannot enforce it, so the plugin is rejected",
                              id);
        }
    }
    for (const auto& dependency : dependencies) {
        if (dependency.plugin_id.empty()) {
            diagnostics.error("BULLY-PLUG-0009", "dependency entry has no plugin id", id);
        }
        if (!dependency.optional && dependency.plugin_id == id) {
            diagnostics.error("BULLY-PLUG-0010", "plugin depends on itself", id);
        }
    }
    for (const auto& capability : capabilities) {
        static const std::vector<std::string> known{"importer", "exporter", "processor",
                                                    "editor_panel", "script", "project_template",
                                                    "build_target", "runtime_system", "qa_rule"};
        if (std::find(known.begin(), known.end(), core::to_lower(capability)) == known.end()) {
            diagnostics.warn("BULLY-PLUG-0011", "unknown capability '" + capability + "'", id);
        }
    }
    if (diagnostics.has_errors()) return core::Status(std::move(diagnostics));
    return core::Status{};
}

core::json::Value PluginManifest::to_json() const
{
    core::json::Value root = core::json::Value::make_object();
    root["id"] = id;
    root["name"] = name;
    root["version"] = version;
    core::json::Value api = core::json::Value::make_object();
    api["major"] = api_major;
    api["minor"] = api_minor;
    root["api"] = std::move(api);
    root["description"] = description;
    root["vendor"] = vendor;
    root["library"] = library;
    root["reload_safe"] = reload_safe;
    root["enabled_by_default"] = enabled_by_default;
    core::json::Value dependencies_json = core::json::Value::make_array();
    for (const auto& dependency : dependencies) {
        core::json::Value entry = core::json::Value::make_object();
        entry["plugin_id"] = dependency.plugin_id;
        entry["minimum_version"] = dependency.minimum_version;
        entry["optional"] = dependency.optional;
        dependencies_json.push(std::move(entry));
    }
    root["dependencies"] = std::move(dependencies_json);
    core::json::Value permissions_json = core::json::Value::make_array();
    for (const auto& permission : permissions) permissions_json.push(permission);
    root["permissions"] = std::move(permissions_json);
    core::json::Value capabilities_json = core::json::Value::make_array();
    for (const auto& capability : capabilities) capabilities_json.push(capability);
    root["capabilities"] = std::move(capabilities_json);
    return root;
}

core::Result<PluginManifest> PluginManifest::from_json(const core::json::Value& value)
{
    core::DiagnosticList diagnostics;
    PluginManifest manifest;
    manifest.id = value.get_string("id");
    manifest.name = value.get_string("name", manifest.id);
    manifest.version = value.get_string("version");
    if (const core::json::Value* api = value.find("api")) {
        manifest.api_major = static_cast<std::uint32_t>(api->get_int("major", 1));
        manifest.api_minor = static_cast<std::uint32_t>(api->get_int("minor", 0));
    } else {
        manifest.api_major = static_cast<std::uint32_t>(value.get_int("api_major", 1));
        manifest.api_minor = static_cast<std::uint32_t>(value.get_int("api_minor", 0));
    }
    manifest.description = value.get_string("description");
    manifest.vendor = value.get_string("vendor");
    manifest.library = value.get_string("library");
    manifest.reload_safe = value.get_bool("reload_safe", false);
    manifest.enabled_by_default = value.get_bool("enabled_by_default", true);
    if (const core::json::Value* dependencies = value.find("dependencies");
        dependencies != nullptr && dependencies->is_array()) {
        for (const auto& entry : dependencies->items()) {
            PluginDependency dependency;
            dependency.plugin_id = entry.get_string("plugin_id", entry.as_string());
            dependency.minimum_version = entry.get_string("minimum_version");
            dependency.optional = entry.get_bool("optional", false);
            manifest.dependencies.push_back(std::move(dependency));
        }
    }
    if (const core::json::Value* permissions = value.find("permissions");
        permissions != nullptr && permissions->is_array()) {
        for (const auto& entry : permissions->items()) manifest.permissions.push_back(entry.as_string());
    }
    if (const core::json::Value* capabilities = value.find("capabilities");
        capabilities != nullptr && capabilities->is_array()) {
        for (const auto& entry : capabilities->items()) manifest.capabilities.push_back(entry.as_string());
    }
    return manifest;
}

core::Result<PluginManifest> PluginManifest::load(const std::filesystem::path& path)
{
    core::DiagnosticList diagnostics;
    const auto manifest_path = std::filesystem::is_directory(path) ? path / "manifest.json" : path;
    if (!core::FileSystem::exists(manifest_path)) {
        diagnostics.error("BULLY-PLUG-0012", "manifest.json not found", manifest_path.string());
        return core::Result<PluginManifest>(std::move(diagnostics));
    }
    auto text = core::FileSystem::read_text(manifest_path);
    if (!text) return core::Result<PluginManifest>(text.diagnostics());
    auto parsed = core::json::parse(text.value());
    if (!parsed) return core::Result<PluginManifest>(parsed.diagnostics());
    PluginManifest manifest = from_json(parsed.value()).value();
    manifest.directory = manifest_path.parent_path();
    return manifest;
}

}  // namespace bully::plugins
