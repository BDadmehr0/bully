#include "bully/plugins/host.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

#include <dlfcn.h>

#include "bully/core/fs.h"
#include "bully/core/log.h"
#include "bully/core/strings.h"
#include "bully/plugin_api.h"

namespace bully::plugins {

namespace {

bully_str make_str(std::string_view text)
{
    bully_str out;
    out.data = text.data();
    out.size = text.size();
    return out;
}

std::string from_str(bully_str text)
{
    if (text.data == nullptr) return {};
    return std::string(text.data, text.size);
}

/// Default state store: remembers choices for the lifetime of the process.
class MemoryStateStore final : public PluginStateStore {
public:
    bool enabled(const std::string& plugin_id, bool fallback) const override
    {
        const auto found = enabled_.find(plugin_id);
        return found != enabled_.end() ? found->second : fallback;
    }
    void set_enabled(const std::string& plugin_id, bool enabled) override
    {
        enabled_[plugin_id] = enabled;
    }
    void set_error(const std::string& plugin_id, std::string error) override
    {
        errors_[plugin_id] = std::move(error);
    }

private:
    std::map<std::string, bool> enabled_;
    std::map<std::string, std::string> errors_;
};

}  // namespace

std::string_view to_string(PluginState state) noexcept
{
    switch (state) {
        case PluginState::Discovered: return "discovered";
        case PluginState::Loaded: return "loaded";
        case PluginState::Active: return "active";
        case PluginState::Disabled: return "disabled";
        case PluginState::Failed: return "failed";
        case PluginState::Incompatible: return "incompatible";
    }
    return "unknown";
}

// ----------------------------------------------------------- SharedLibrary --

SharedLibrary::~SharedLibrary() { close(); }

SharedLibrary::SharedLibrary(SharedLibrary&& other) noexcept
    : handle_(other.handle_), path_(std::move(other.path_))
{
    other.handle_ = nullptr;
}

SharedLibrary& SharedLibrary::operator=(SharedLibrary&& other) noexcept
{
    if (this != &other) {
        close();
        handle_ = other.handle_;
        path_ = std::move(other.path_);
        other.handle_ = nullptr;
    }
    return *this;
}

core::Result<SharedLibrary> SharedLibrary::open(const std::filesystem::path& path)
{
    core::DiagnosticList diagnostics;
    if (!core::FileSystem::exists(path)) {
        diagnostics.error("BULLY-PLUG-0013", "shared library not found", path.string());
        return core::Result<SharedLibrary>(std::move(diagnostics));
    }
    void* handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* error = dlerror();
        diagnostics.error("BULLY-PLUG-0014",
                          std::string("dlopen failed: ") + (error != nullptr ? error : "unknown"),
                          path.string());
        return core::Result<SharedLibrary>(std::move(diagnostics));
    }
    SharedLibrary library;
    library.handle_ = handle;
    library.path_ = path;
    return library;
}

void* SharedLibrary::symbol(const char* name) const
{
    if (handle_ == nullptr) return nullptr;
    return dlsym(handle_, name);
}

void SharedLibrary::close()
{
    if (handle_ != nullptr) {
        dlclose(handle_);
        handle_ = nullptr;
    }
}

// -------------------------------------------------------------- PluginHost --

struct PluginHost::LoadedPlugin {
    PluginInfo info;
    SharedLibrary library;
    const bully_plugin_api* api = nullptr;
    std::vector<std::string> importers;
    std::vector<std::string> exporters;
    std::vector<std::string> panels;
    std::vector<std::string> qa_rules;
    bool initialized = false;
};

PluginHost::PluginHost()
{
    store_ = std::make_shared<MemoryStateStore>();
    auto api = std::make_unique<bully_host_api>();
    std::memset(api.get(), 0, sizeof(bully_host_api));
    api->struct_size = sizeof(bully_host_api);
    api->abi_major = BULLY_PLUGIN_ABI_MAJOR;
    api->abi_minor = BULLY_PLUGIN_ABI_MINOR;
    api->log = +[](void* /*host*/, bully_log_level level, bully_str category, bully_str message) {
        static constexpr core::LogLevel kLevels[] = {core::LogLevel::Trace, core::LogLevel::Debug,
                                                     core::LogLevel::Info, core::LogLevel::Warn,
                                                     core::LogLevel::Error};
        const std::size_t index = level <= BULLY_LOG_ERROR ? static_cast<std::size_t>(level) : 4;
        core::log(kLevels[index], "plugin:" + from_str(category), from_str(message));
    };
    api->alloc = +[](void* /*host*/, std::size_t size) -> void* { return std::malloc(size); };
    api->free = +[](void* /*host*/, void* pointer) { std::free(pointer); };
    api->register_importer = +[](void* host, bully_str importer_id, bully_str display_name,
                                 bully_str version, bully_str extensions,
                                 void* /*user_data*/) -> bully_result {
        auto* self = static_cast<PluginHost*>(host);
        if (self == nullptr || importer_id.data == nullptr) return BULLY_ERR_INVALID_ARGUMENT;
        PluginImporterDescriptor descriptor;
        descriptor.plugin_id = self->registering_plugin_;
        descriptor.importer_id = from_str(importer_id);
        descriptor.display_name = from_str(display_name);
        descriptor.version = from_str(version);
        descriptor.extensions = core::split(from_str(extensions), ',');
        for (auto& extension : descriptor.extensions) extension = core::trim(extension);
        for (auto& existing : self->importer_descriptors_) {
            if (existing.importer_id == descriptor.importer_id &&
                existing.plugin_id == descriptor.plugin_id) {
                existing = descriptor;
                return BULLY_OK;
            }
        }
        self->importer_descriptors_.push_back(std::move(descriptor));
        return BULLY_OK;
    };
    api->register_editor_panel = +[](void* host, bully_str panel_json,
                                     void* /*user_data*/) -> bully_result {
        auto* self = static_cast<PluginHost*>(host);
        if (self == nullptr) return BULLY_ERR_INVALID_ARGUMENT;
        PluginPanelDescriptor descriptor;
        descriptor.plugin_id = self->registering_plugin_;
        descriptor.panel_json = from_str(panel_json);
        self->panel_descriptors_.emplace_back(descriptor.plugin_id, descriptor.panel_json);
        return BULLY_OK;
    };
    api->register_qa_rule = +[](void* host, bully_str rule_json, void* /*user_data*/) -> bully_result {
        auto* self = static_cast<PluginHost*>(host);
        if (self == nullptr || rule_json.data == nullptr) return BULLY_ERR_INVALID_ARGUMENT;
        auto parsed = core::json::parse(from_str(rule_json));
        if (!parsed.has_value()) return BULLY_ERR_PARSE;
        const std::string name = parsed.value().get_string("name");
        if (name.empty()) return BULLY_ERR_INVALID_ARGUMENT;
        self->qa_rule_ids_.emplace_back(self->registering_plugin_, name);
        return BULLY_OK;
    };
    api->read_project_file = +[](void* host, bully_str relative_path,
                                 bully_buf* out) -> bully_result {
        auto* self = static_cast<PluginHost*>(host);
        if (self == nullptr || out == nullptr || self->options_.project_root.empty()) {
            return BULLY_ERR_INVALID_ARGUMENT;
        }
        const auto path = std::filesystem::path(self->options_.project_root) / from_str(relative_path);
        auto bytes = core::FileSystem::read_binary(path);
        if (!bytes) return BULLY_ERR_IO;
        auto* buffer = static_cast<std::uint8_t*>(std::malloc(bytes.value().size()));
        if (buffer == nullptr && !bytes.value().empty()) return BULLY_ERR_INTERNAL;
        std::memcpy(buffer, bytes.value().data(), bytes.value().size());
        out->data = buffer;
        out->size = bytes.value().size();
        out->capacity = bytes.value().size();
        return BULLY_OK;
    };
    api->write_project_file = +[](void* host, bully_str relative_path, const std::uint8_t* data,
                                  std::size_t size) -> bully_result {
        auto* self = static_cast<PluginHost*>(host);
        if (self == nullptr || self->options_.project_root.empty()) return BULLY_ERR_INVALID_ARGUMENT;
        const auto path = std::filesystem::path(self->options_.project_root) / from_str(relative_path);
        if (!core::FileSystem::is_within(path.parent_path().empty() ? path : path.parent_path(),
                                         std::filesystem::path(self->options_.project_root))) {
            return BULLY_ERR_INVALID_ARGUMENT;
        }
        std::vector<std::uint8_t> bytes;
        if (data != nullptr && size > 0) bytes.assign(data, data + size);
        return core::FileSystem::write_binary(path, bytes) ? BULLY_OK : BULLY_ERR_IO;
    };
    api->resolve_path = +[](void* host, bully_str relative_path, bully_str* out) -> bully_result {
        auto* self = static_cast<PluginHost*>(host);
        if (self == nullptr || out == nullptr) return BULLY_ERR_INVALID_ARGUMENT;
        const auto absolute = std::filesystem::path(self->options_.project_root) / from_str(relative_path);
        auto* text = static_cast<char*>(std::malloc(absolute.string().size()));
        if (text == nullptr && !absolute.string().empty()) return BULLY_ERR_INTERNAL;
        std::memcpy(text, absolute.string().data(), absolute.string().size());
        out->data = text;
        out->size = absolute.string().size();
        return BULLY_OK;
    };
    api->host_handle = this;
    host_api_ = std::move(api);
}

PluginHost::~PluginHost()
{
    // Unload in reverse dependency order: simply iterate in load order reversed.
    std::vector<std::string> order;
    order.reserve(loaded_.size());
    for (const auto& [id, plugin] : loaded_) order.push_back(id);
    std::reverse(order.begin(), order.end());
    for (const auto& id : order) (void)unload(id);
}

void PluginHost::set_options(Options options) { options_ = std::move(options); }

void PluginHost::set_state_store(std::shared_ptr<PluginStateStore> store)
{
    if (store) store_ = std::move(store);
}

const bully_host_api* PluginHost::host_api() const { return host_api_.get(); }

std::vector<PluginInfo> PluginHost::discover()
{
    infos_.clear();
    for (const auto& directory : options_.plugin_directories) {
        auto files = core::FileSystem::list_files(directory, true);
        if (!files) continue;
        for (const auto& file : files.value()) {
            if (file.filename() != "manifest.json") continue;
            auto manifest = PluginManifest::load(file.parent_path());
            PluginInfo info;
            if (!manifest) {
                info.state = PluginState::Failed;
                info.error = manifest.diagnostics().format();
                ++failure_count_;
                core::log_error("plugins", "invalid manifest at " + file.string());
                infos_.push_back(std::move(info));
                continue;
            }
            info.manifest = std::move(manifest.value());
            info.runtime_version = info.manifest.version;
            info.reload_safe = info.manifest.reload_safe;
            info.enabled = store_->enabled(info.manifest.id, info.manifest.enabled_by_default);
            auto status = info.manifest.validate();
            if (!status) {
                info.state = PluginState::Failed;
                info.error = status.diagnostics().format();
                info.manifest.directory = file.parent_path();
                ++failure_count_;
                core::log_error("plugins", "manifest validation failed for " +
                                               info.manifest.id + ": " + info.error);
                infos_.push_back(std::move(info));
                continue;
            }
            info.state = PluginState::Discovered;
            if (!info.enabled) info.state = PluginState::Disabled;
            infos_.push_back(std::move(info));
        }
    }
    std::sort(infos_.begin(), infos_.end(), [](const PluginInfo& a, const PluginInfo& b) {
        return a.manifest.id < b.manifest.id;
    });
    core::log_info("plugins", "discovered " + std::to_string(infos_.size()) + " plugin(s)");
    return infos_;
}

PluginInfo PluginHost::plugin(const std::string& plugin_id) const
{
    for (const auto& info : infos_) {
        if (info.manifest.id == plugin_id) return info;
    }
    PluginInfo missing;
    missing.manifest.id = plugin_id;
    missing.state = PluginState::Discovered;
    missing.error = "plugin '" + plugin_id + "' has not been discovered";
    return missing;
}

std::vector<PluginInfo> PluginHost::plugins() const { return infos_; }

core::Status PluginHost::check_dependencies(const PluginManifest& manifest) const
{
    core::DiagnosticList diagnostics;
    for (const auto& dependency : manifest.dependencies) {
        const PluginInfo found = plugin(dependency.plugin_id);
        if (found.manifest.id.empty()) {
            if (!dependency.optional) {
                diagnostics.error("BULLY-PLUG-0015",
                                  "required plugin '" + dependency.plugin_id + "' is not installed",
                                  manifest.id);
            }
            continue;
        }
        if (!dependency.minimum_version.empty() &&
            !version_at_least(found.manifest.version, dependency.minimum_version)) {
            diagnostics.error("BULLY-PLUG-0016",
                              "plugin '" + dependency.plugin_id + "' version " +
                                  found.manifest.version + " is older than required " +
                                  dependency.minimum_version,
                              manifest.id);
        }
    }
    for (const auto& permission : manifest.permissions) {
        if (std::find(options_.denied_permissions.begin(), options_.denied_permissions.end(),
                      core::to_lower(permission)) != options_.denied_permissions.end()) {
            diagnostics.error("BULLY-PLUG-0017",
                              "permission '" + permission + "' is denied by host policy",
                              manifest.id);
        }
    }
    if (diagnostics.has_errors()) return core::Status(std::move(diagnostics));
    return core::Status{};
}

core::Status PluginHost::load(const std::string& plugin_id) { return load_internal(plugin_id, true); }

core::Status PluginHost::load_internal(const std::string& plugin_id, bool initialise)
{
    core::DiagnosticList diagnostics;
    auto info_iterator = std::find_if(infos_.begin(), infos_.end(), [&](const PluginInfo& info) {
        return info.manifest.id == plugin_id;
    });
    if (info_iterator == infos_.end()) {
        diagnostics.error("BULLY-PLUG-0018",
                          "plugin '" + plugin_id +
                              "' is not discovered; call discover() with the right directories",
                          plugin_id);
        return core::Status(std::move(diagnostics));
    }
    PluginInfo& info = *info_iterator;
    if (info.state == PluginState::Active) return core::Status{};
    if (!info.enabled) {
        diagnostics.error("BULLY-PLUG-0019", "plugin '" + plugin_id + "' is disabled", plugin_id);
        return core::Status(std::move(diagnostics));
    }

    auto dependency_status = check_dependencies(info.manifest);
    if (!dependency_status) {
        mark_failure(plugin_id, PluginState::Failed, dependency_status.diagnostics().format());
        return dependency_status;
    }
    // Optional dependencies must be loaded first so the plugin can use them.
    for (const auto& dependency : info.manifest.dependencies) {
        if (dependency.optional) continue;
        const PluginInfo dependency_info = plugin(dependency.plugin_id);
        if (dependency_info.state == PluginState::Discovered) {
            auto status = load_internal(dependency.plugin_id, true);
            if (!status) {
                mark_failure(plugin_id, PluginState::Failed,
                             "dependency '" + dependency.plugin_id + "' failed to load");
                return status;
            }
        } else if (dependency_info.state != PluginState::Active) {
            mark_failure(plugin_id, PluginState::Failed,
                         "dependency '" + dependency.plugin_id + "' is not active");
            diagnostics.error("BULLY-PLUG-0020",
                              "dependency '" + dependency.plugin_id + "' is not active", plugin_id);
            return core::Status(std::move(diagnostics));
        }
    }

    // A crash marker left by a previous run keeps the plugin disabled.
    if (!options_.state_directory.empty()) {
        const auto marker = options_.state_directory / (plugin_id + ".crashed");
        if (core::FileSystem::exists(marker)) {
            mark_failure(plugin_id, PluginState::Disabled,
                         "disabled after a previous crash; delete '" + marker.string() +
                             "' to retry loading");
            diagnostics.error("BULLY-PLUG-0021",
                              "plugin '" + plugin_id +
                                  "' was disabled after a crash. Remove the crash marker to retry.",
                              plugin_id);
            return core::Status(std::move(diagnostics));
        }
    }

    const auto library_path = info.manifest.directory / info.manifest.library;
    auto library_result = SharedLibrary::open(library_path);
    if (!library_result) {
        mark_failure(plugin_id, PluginState::Failed, library_result.diagnostics().format());
        return core::Status(library_result.diagnostics());
    }

    auto loaded = std::make_unique<LoadedPlugin>();
    loaded->info = info;
    loaded->library = std::move(library_result.value());

    auto* describe = reinterpret_cast<bully_plugin_api_fn>(
        loaded->library.symbol(BULLY_PLUGIN_EXPORT_SYMBOL));
    if (describe == nullptr) {
        mark_failure(plugin_id, PluginState::Failed,
                     std::string("plugin does not export '") + BULLY_PLUGIN_EXPORT_SYMBOL + "'");
        diagnostics.error("BULLY-PLUG-0022",
                          std::string("plugin does not export '") + BULLY_PLUGIN_EXPORT_SYMBOL + "'",
                          library_path.string());
        return core::Status(std::move(diagnostics));
    }
    const bully_plugin_api* api = nullptr;
    try {
        api = describe();
    } catch (...) {
        mark_failure(plugin_id, PluginState::Failed, "exception in plugin entry point");
        diagnostics.error("BULLY-PLUG-0023", "exception thrown by plugin entry point",
                          library_path.string());
        return core::Status(std::move(diagnostics));
    }
    if (api == nullptr || api->describe == nullptr) {
        mark_failure(plugin_id, PluginState::Failed, "plugin API table is incomplete");
        diagnostics.error("BULLY-PLUG-0024", "plugin returned an incomplete API table",
                          library_path.string());
        return core::Status(std::move(diagnostics));
    }
    loaded->api = api;
    if (api->abi_major != BULLY_PLUGIN_ABI_MAJOR) {
        mark_failure(plugin_id, PluginState::Incompatible,
                     "plugin ABI " + std::to_string(api->abi_major) + "." +
                         std::to_string(api->abi_minor) + " is incompatible with host ABI " +
                         std::to_string(BULLY_PLUGIN_ABI_MAJOR) + "." +
                         std::to_string(BULLY_PLUGIN_ABI_MINOR));
        diagnostics.error("BULLY-PLUG-0025",
                          "plugin ABI major " + std::to_string(api->abi_major) +
                              " does not match host ABI major " +
                              std::to_string(BULLY_PLUGIN_ABI_MAJOR),
                          plugin_id);
        return core::Status(std::move(diagnostics));
    }
    if (api->abi_minor > BULLY_PLUGIN_ABI_MINOR) {
        core::log_warn("plugins", "plugin '" + plugin_id +
                                      "' targets a newer ABI minor; loading anyway");
    }
    const bully_plugin_descriptor* descriptor = nullptr;
    try {
        descriptor = api->describe();
    } catch (...) {
        descriptor = nullptr;
    }
    if (descriptor == nullptr) {
        mark_failure(plugin_id, PluginState::Failed, "plugin descriptor is null");
        diagnostics.error("BULLY-PLUG-0026", "plugin descriptor is null", plugin_id);
        return core::Status(std::move(diagnostics));
    }
    info.runtime_version = from_str(descriptor->version);
    info.runtime_api_major = descriptor->abi_major;
    info.runtime_api_minor = descriptor->abi_minor;
    if (!info.runtime_version.empty() && info.runtime_version != info.manifest.version) {
        core::log_warn("plugins", "plugin '" + plugin_id + "' manifest version " +
                                      info.manifest.version + " differs from runtime version " +
                                      info.runtime_version);
    }
    loaded->exporters.clear();

    registering_plugin_ = plugin_id;
    if (initialise && api->initialize != nullptr) {
        bully_result result = BULLY_ERR_INTERNAL;
        try {
            result = api->initialize(host_api_.get(), nullptr);
        } catch (...) {
            result = BULLY_ERR_INTERNAL;
        }
        if (result != BULLY_OK) {
            mark_failure(plugin_id, PluginState::Failed,
                         "initialize() returned " + std::to_string(static_cast<int>(result)));
            diagnostics.error("BULLY-PLUG-0027",
                              "plugin '" + plugin_id + "' failed to initialize (code " +
                                  std::to_string(static_cast<int>(result)) + ")",
                              plugin_id);
            return core::Status(std::move(diagnostics));
        }
        loaded->initialized = true;
    }
    info.state = PluginState::Active;
    info.error.clear();
    register_capabilities(*loaded);
    core::log_info("plugins", "loaded plugin '" + plugin_id + "' v" + info.runtime_version);
    loaded_.emplace(plugin_id, std::move(loaded));
    return core::Status{};
}

void PluginHost::register_capabilities(LoadedPlugin& plugin)
{
    // Capability registration happens through the host API callbacks during
    // initialize(); this function records what the descriptor advertises so the
    // Extension Manager can show it even when a plugin registers nothing.
    const bully_plugin_descriptor* descriptor = plugin.api->describe();
    if (descriptor == nullptr) return;
    const std::uint32_t capabilities = descriptor->capabilities;
    if ((capabilities & (1u << BULLY_CAP_EXPORTER)) != 0 && plugin.exporters.empty()) {
        plugin.exporters.push_back(plugin.info.manifest.id + ".default_export");
    }
    for (const auto& exporter : plugin.exporters) exporter_ids_.push_back(exporter);
    for (const auto& panel : plugin.panels) panel_descriptors_.emplace_back(plugin.info.manifest.id, panel);
}

core::Status PluginHost::load_all()
{
    // Enable/disable and dependency ordering are handled by load_internal.
    std::vector<std::string> order;
    for (const auto& info : infos_) {
        if (info.enabled) order.push_back(info.manifest.id);
    }
    std::vector<std::string> failures;
    for (const auto& id : order) {
        auto status = load_internal(id, true);
        if (!status) failures.push_back(id);
    }
    if (!failures.empty()) {
        core::DiagnosticList diagnostics;
        diagnostics.error("BULLY-PLUG-0028",
                          std::to_string(failures.size()) + " plugin(s) failed to load: " +
                              core::join(failures, ", "));
        return core::Status(std::move(diagnostics));
    }
    return core::Status{};
}

core::Status PluginHost::unload(const std::string& plugin_id)
{
    core::DiagnosticList diagnostics;
    auto found = loaded_.find(plugin_id);
    if (found == loaded_.end()) {
        diagnostics.error("BULLY-PLUG-0029", "plugin '" + plugin_id + "' is not loaded", plugin_id);
        return core::Status(std::move(diagnostics));
    }
    // Refuse to unload a plugin that others depend on.
    for (const auto& [other_id, other] : loaded_) {
        if (other_id == plugin_id) continue;
        for (const auto& dependency : other->info.manifest.dependencies) {
            if (dependency.plugin_id == plugin_id && !dependency.optional) {
                diagnostics.error("BULLY-PLUG-0030",
                                  "cannot unload '" + plugin_id + "': '" + other_id +
                                      "' depends on it",
                                  plugin_id);
                return core::Status(std::move(diagnostics));
            }
        }
    }
    auto& plugin = *found->second;
    if (plugin.initialized && plugin.api->shutdown != nullptr) {
        try {
            plugin.api->shutdown();
        } catch (...) {
            core::log_error("plugins", "exception in shutdown() of '" + plugin_id + "'");
        }
    }
    for (const auto& exporter : plugin.exporters) {
        exporter_ids_.erase(std::remove(exporter_ids_.begin(), exporter_ids_.end(), exporter),
                            exporter_ids_.end());
    }
    // Capabilities the plugin registered through the host API must disappear
    // with it, otherwise the Asset Browser would offer an importer whose
    // implementation has been dlclose()d.
    importer_descriptors_.erase(
        std::remove_if(importer_descriptors_.begin(), importer_descriptors_.end(),
                       [&](const PluginImporterDescriptor& descriptor) {
                           return descriptor.plugin_id == plugin_id;
                       }),
        importer_descriptors_.end());
    qa_rule_ids_.erase(std::remove_if(qa_rule_ids_.begin(), qa_rule_ids_.end(),
                                      [&](const std::pair<std::string, std::string>& rule) {
                                          return rule.first == plugin_id;
                                      }),
                       qa_rule_ids_.end());
    panel_descriptors_.erase(
        std::remove_if(panel_descriptors_.begin(), panel_descriptors_.end(),
                       [&](const std::pair<std::string, std::string>& panel) {
                           return panel.first == plugin_id;
                       }),
        panel_descriptors_.end());
    loaded_.erase(found);
    for (auto& info : infos_) {
        if (info.manifest.id == plugin_id) {
            info.state = info.enabled ? PluginState::Discovered : PluginState::Disabled;
        }
    }
    core::log_info("plugins", "unloaded plugin '" + plugin_id + "'");
    return core::Status{};
}

core::Status PluginHost::enable(const std::string& plugin_id, bool enabled)
{
    core::DiagnosticList diagnostics;
    auto found = std::find_if(infos_.begin(), infos_.end(), [&](const PluginInfo& info) {
        return info.manifest.id == plugin_id;
    });
    if (found == infos_.end()) {
        diagnostics.error("BULLY-PLUG-0018", "plugin '" + plugin_id + "' is not discovered",
                          plugin_id);
        return core::Status(std::move(diagnostics));
    }
    if (!enabled && loaded_.count(plugin_id) != 0) {
        auto status = unload(plugin_id);
        if (!status) return status;
    }
    found->enabled = enabled;
    store_->set_enabled(plugin_id, enabled);
    if (found->state != PluginState::Failed && found->state != PluginState::Incompatible) {
        found->state = enabled ? PluginState::Discovered : PluginState::Disabled;
    }
    return core::Status{};
}

core::Status PluginHost::reload(const std::string& plugin_id)
{
    core::DiagnosticList diagnostics;
    if (!options_.allow_reloading) {
        diagnostics.error("BULLY-PLUG-0031", "plugin reloading is disabled by host policy",
                          plugin_id);
        return core::Status(std::move(diagnostics));
    }
    auto found = loaded_.find(plugin_id);
    if (found == loaded_.end()) {
        diagnostics.error("BULLY-PLUG-0029", "plugin '" + plugin_id + "' is not loaded", plugin_id);
        return core::Status(std::move(diagnostics));
    }
    if (!found->second->info.manifest.reload_safe) {
        diagnostics.error("BULLY-PLUG-0032",
                          "plugin '" + plugin_id +
                              "' does not declare reload_safe, so it cannot be hot reloaded",
                          plugin_id);
        return core::Status(std::move(diagnostics));
    }
    if (found->second->api->reload != nullptr) {
        bully_result result = BULLY_ERR_INTERNAL;
        try {
            result = found->second->api->reload();
        } catch (...) {
            result = BULLY_ERR_INTERNAL;
        }
        if (result == BULLY_OK) {
            core::log_info("plugins", "plugin '" + plugin_id + "' reloaded in place");
            return core::Status{};
        }
    }
    auto status = unload(plugin_id);
    if (!status) return status;
    return load_internal(plugin_id, true);
}

std::vector<PluginImporterDescriptor> PluginHost::plugin_importers() const
{
    return importer_descriptors_;
}

core::Result<PluginImportOutput> PluginHost::run_plugin_import(
    const std::string& plugin_id, const std::string& importer_id, const std::string& asset_id,
    const std::string& source_path, const std::string& relative_path, const std::string& output_path,
    const std::vector<std::pair<std::string, std::string>>& settings)
{
    core::DiagnosticList diagnostics;
    auto found = loaded_.find(plugin_id);
    if (found == loaded_.end()) {
        diagnostics.error("BULLY-PLUG-0035", "plugin '" + plugin_id + "' is not loaded", plugin_id);
        return core::Result<PluginImportOutput>(std::move(diagnostics));
    }
    LoadedPlugin& plugin = *found->second;
    if (plugin.api == nullptr || plugin.api->run_import == nullptr) {
        diagnostics.error("BULLY-PLUG-0036",
                          "plugin '" + plugin_id + "' does not implement run_import", plugin_id);
        return core::Result<PluginImportOutput>(std::move(diagnostics));
    }
    std::vector<bully_kv> key_values;
    key_values.reserve(settings.size());
    for (const auto& [key, value] : settings) {
        key_values.push_back(bully_kv{make_str(key), make_str(value)});
    }
    bully_import_request request{};
    request.asset_id = make_str(asset_id);
    request.source_path = make_str(source_path);
    request.relative_path = make_str(relative_path);
    request.output_path = make_str(output_path);
    request.settings.items = key_values.data();
    request.settings.count = key_values.size();
    request.host = host_api_.get();
    request.host_handle = this;
    bully_import_response response{};
    bully_result result = BULLY_ERR_INTERNAL;
    try {
        result = plugin.api->run_import(make_str(importer_id), &request, &response);
    } catch (...) {
        result = BULLY_ERR_INTERNAL;
    }
    if (result != BULLY_OK) {
        diagnostics.error("BULLY-PLUG-0037",
                          "plugin '" + plugin_id + "' importer '" + importer_id +
                              "' failed with code " + std::to_string(static_cast<int>(result)),
                          plugin_id);
        return core::Result<PluginImportOutput>(std::move(diagnostics));
    }
    PluginImportOutput output;
    if (response.output.data != nullptr && response.output.size > 0) {
        output.bytes.assign(response.output.data, response.output.data + response.output.size);
        std::free(response.output.data);
    }
    output.summary = from_str(response.summary);
    output.report_json = from_str(response.report_json);
    output.warning_count = response.warning_count;
    return output;
}

std::vector<std::string> PluginHost::exporters() const { return exporter_ids_; }
std::vector<std::string> PluginHost::editor_panels() const
{
    std::vector<std::string> panels;
    panels.reserve(panel_descriptors_.size());
    for (const auto& [plugin_id, json] : panel_descriptors_) panels.push_back(json);
    return panels;
}

core::Result<std::string> PluginHost::run_qa_rule(const std::string& rule_name)
{
    core::DiagnosticList diagnostics;
    for (const auto& [id, plugin] : loaded_) {
        if (plugin->api == nullptr || plugin->api->run_qa_rule == nullptr) continue;
        bool owns_rule = std::find(plugin->qa_rules.begin(), plugin->qa_rules.end(), rule_name) !=
                         plugin->qa_rules.end();
        if (!owns_rule) {
            for (const auto& [owner, name] : qa_rule_ids_) {
                if (owner == id && name == rule_name) {
                    owns_rule = true;
                    break;
                }
            }
        }
        if (!owns_rule) continue;
        bully_buf buffer{};
        bully_result result = BULLY_ERR_INTERNAL;
        try {
            result = plugin->api->run_qa_rule(make_str(rule_name),
                                              make_str(options_.project_root), &buffer);
        } catch (...) {
            result = BULLY_ERR_INTERNAL;
        }
        if (result != BULLY_OK) {
            diagnostics.error("BULLY-PLUG-0033",
                              "plugin '" + id + "' failed to run QA rule '" + rule_name + "'",
                              rule_name);
            return core::Result<std::string>(std::move(diagnostics));
        }
        std::string output;
        if (buffer.data != nullptr) {
            output.assign(reinterpret_cast<const char*>(buffer.data), buffer.size);
            std::free(buffer.data);
        }
        return output;
    }
    diagnostics.error("BULLY-PLUG-0034", "no loaded plugin provides QA rule '" + rule_name + "'",
                      rule_name);
    return core::Result<std::string>(std::move(diagnostics));
}

void PluginHost::mark_failure(const std::string& plugin_id, PluginState state, std::string message)
{
    ++failure_count_;
    for (auto& info : infos_) {
        if (info.manifest.id == plugin_id) {
            info.state = state;
            info.error = message;
        }
    }
    if (store_) store_->set_error(plugin_id, message);
    core::log_error("plugins", "plugin '" + plugin_id + "': " + message);
}

}  // namespace bully::plugins
