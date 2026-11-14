// Bully Studio - plugin manifest (manifest.json) model + validation.
//
// Every plugin directory contains a manifest.json:
// {
//   "id": "com.example.my_plugin",
//   "name": "My Plugin",
//   "version": "1.2.0",
//   "api": { "major": 1, "minor": 0 },
//   "description": "...",
//   "vendor": "...",
//   "library": "libmy_plugin.so",
//   "dependencies": ["com.bully.importer.obj"],
//   "permissions": ["project.read", "project.write", "network"],
//   "capabilities": ["importer", "qa_rule"],
//   "reload_safe": true,
//   "enabled_by_default": true
// }
//
// Diagnostic codes: BULLY-PLUG-0001..0029
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/core/json.h"

namespace bully::plugins {

struct PluginDependency {
    std::string plugin_id;
    std::string minimum_version;
    bool optional = false;
};

struct PluginManifest {
    std::string id;
    std::string name;
    std::string version;
    std::uint32_t api_major = 1;
    std::uint32_t api_minor = 0;
    std::string description;
    std::string vendor;
    std::string library;  // relative path inside the plugin directory
    std::vector<PluginDependency> dependencies;
    std::vector<std::string> permissions;
    std::vector<std::string> capabilities;
    bool reload_safe = false;
    bool enabled_by_default = true;
    std::filesystem::path directory;

    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] core::json::Value to_json() const;
    [[nodiscard]] static core::Result<PluginManifest> from_json(const core::json::Value& value);
    [[nodiscard]] static core::Result<PluginManifest> load(const std::filesystem::path& path);
};

/// Permissions the host understands. Unknown permissions are rejected so a
/// plugin cannot silently request something the host does not enforce.
[[nodiscard]] bool is_known_permission(std::string_view permission) noexcept;
[[nodiscard]] const std::vector<std::string>& known_permissions();

/// Compares dotted numeric versions ("1.2.10" >= "1.2.9").
[[nodiscard]] bool version_at_least(std::string_view version, std::string_view minimum);

}  // namespace bully::plugins
