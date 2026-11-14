// Bully Studio - plugin host.
//
// Discovery -> manifest validation -> dependency resolution -> ABI check ->
// dlopen -> describe -> initialize -> registration. Any step can fail without
// affecting the editor: the plugin is marked Disabled and the reason is
// recorded in PluginInfo::error and written to the project's plugin_state table.
//
// Crash isolation strategy (documented in docs/plugins.md):
//   * manifests and every plugin callback are wrapped in try/catch,
//   * the ABI is pure C with size-tagged structs, so a plugin built against an
//     older header can still be loaded,
//   * a fault inside a plugin cannot be contained in-process on Linux; the
//     runtime therefore treats a plugin that crashes as failed and the Studio
//     writes a crash marker so it is not auto-loaded next start. This is stated
//     explicitly rather than pretending isolates exist.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bully/core/error.h"
#include "bully/plugins/manifest.h"

struct bully_host_api;
struct bully_plugin_api;

namespace bully::plugins {

enum class PluginState { Discovered, Loaded, Active, Disabled, Failed, Incompatible };

[[nodiscard]] std::string_view to_string(PluginState state) noexcept;

/// Result of a plugin import call.
struct PluginImportOutput {
    std::vector<std::uint8_t> bytes;
    std::string summary;
    std::string report_json;
    std::uint32_t warning_count = 0;
};

struct PluginInfo {
    PluginManifest manifest;
    PluginState state = PluginState::Discovered;
    std::string error;
    std::string runtime_version;   // version reported by the descriptor
    std::uint32_t runtime_api_major = 0;
    std::uint32_t runtime_api_minor = 0;
    bool reload_safe = false;
    bool enabled = false;
};

/// An importer offered by a plugin through the C ABI.
struct PluginImporterDescriptor {
    std::string plugin_id;
    std::string importer_id;
    std::string display_name;
    std::string version;
    std::vector<std::string> extensions;
};

/// An editor panel offered by a plugin (JSON descriptor).
struct PluginPanelDescriptor {
    std::string plugin_id;
    std::string panel_json;
};

/// Opaque handle to a loaded shared library (dlopen on Unix).
class SharedLibrary {
public:
    SharedLibrary() = default;
    ~SharedLibrary();
    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;
    SharedLibrary(SharedLibrary&&) noexcept;
    SharedLibrary& operator=(SharedLibrary&&) noexcept;

    [[nodiscard]] static core::Result<SharedLibrary> open(const std::filesystem::path& path);
    [[nodiscard]] void* symbol(const char* name) const;
    void close();
    [[nodiscard]] bool is_open() const noexcept { return handle_ != nullptr; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    void* handle_ = nullptr;
    std::filesystem::path path_;
};

/// Persistent store for enable/disable choices. Implemented over SQLite in the
/// Studio; the default is an in-memory map so headless tools stay simple.
class PluginStateStore {
public:
    virtual ~PluginStateStore() = default;
    virtual bool enabled(const std::string& plugin_id, bool fallback) const = 0;
    virtual void set_enabled(const std::string& plugin_id, bool enabled) = 0;
    virtual void set_error(const std::string& plugin_id, std::string error) = 0;
};

class PluginHost : public std::enable_shared_from_this<PluginHost> {
public:
    struct Options {
        std::vector<std::filesystem::path> plugin_directories;
        /// Directory used for crash markers.
        std::filesystem::path state_directory;
        /// Refuse to load plugins requesting these permissions.
        std::vector<std::string> denied_permissions;
        bool allow_reloading = true;
        /// Absolute project root handed to plugins that need it.
        std::string project_root;
    };

    PluginHost();
    ~PluginHost();
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    void set_options(Options options);
    [[nodiscard]] const Options& options() const noexcept { return options_; }
    void set_state_store(std::shared_ptr<PluginStateStore> store);

    /// Scans plugin directories for manifest.json files (does not load).
    std::vector<PluginInfo> discover();

    /// Loads a discovered plugin by id.
    [[nodiscard]] core::Status load(const std::string& plugin_id);

    /// Loads every discovered, enabled plugin in dependency order.
    [[nodiscard]] core::Status load_all();

    /// Unloads a plugin (calls shutdown first when it is safe).
    [[nodiscard]] core::Status unload(const std::string& plugin_id);

    [[nodiscard]] core::Status enable(const std::string& plugin_id, bool enabled);

    /// Hot reload: unload + load, refused unless the plugin declares itself
    /// reload-safe.
    [[nodiscard]] core::Status reload(const std::string& plugin_id);

    [[nodiscard]] std::vector<PluginInfo> plugins() const;
    [[nodiscard]] PluginInfo plugin(const std::string& plugin_id) const;

    /// Importers offered by loaded plugins (consumed by the bridge).
    [[nodiscard]] std::vector<PluginImporterDescriptor> plugin_importers() const;

    /// Runs a plugin importer. `plugin_id`/`importer_id` identify the owner.
    [[nodiscard]] core::Result<PluginImportOutput> run_plugin_import(
        const std::string& plugin_id, const std::string& importer_id,
        const std::string& asset_id, const std::string& source_path,
        const std::string& relative_path, const std::string& output_path,
        const std::vector<std::pair<std::string, std::string>>& settings);

    /// Exporters registered by loaded plugins, by id.
    [[nodiscard]] std::vector<std::string> exporters() const;

    /// Registered editor panel descriptors (JSON strings).
    [[nodiscard]] std::vector<std::string> editor_panels() const;

    /// Runs a plugin-provided QA rule. Returns the plugin's JSON finding list.
    [[nodiscard]] core::Result<std::string> run_qa_rule(const std::string& rule_name);

    /// Host API handed to plugins (stable for the host's lifetime).
    [[nodiscard]] const bully_host_api* host_api() const;

    /// Number of load failures since construction (used by the UI status bar).
    [[nodiscard]] std::uint32_t failure_count() const noexcept { return failure_count_; }

private:
    struct LoadedPlugin;

    [[nodiscard]] core::Status load_internal(const std::string& plugin_id, bool initialise);
    [[nodiscard]] core::Status check_dependencies(const PluginManifest& manifest) const;
    void register_capabilities(LoadedPlugin& plugin);
    void mark_failure(const std::string& plugin_id, PluginState state, std::string message);

    Options options_;
    std::shared_ptr<PluginStateStore> store_;
    std::vector<PluginInfo> infos_;
    std::map<std::string, std::unique_ptr<LoadedPlugin>> loaded_;
    std::unique_ptr<bully_host_api> host_api_;
    std::vector<std::string> exporter_ids_;
    std::vector<std::pair<std::string, std::string>> panel_descriptors_;  // (plugin id, JSON)
    std::vector<PluginImporterDescriptor> importer_descriptors_;
    std::vector<std::pair<std::string, std::string>> qa_rule_ids_;
    std::string registering_plugin_;
    std::uint32_t failure_count_ = 0;
};

}  // namespace bully::plugins
