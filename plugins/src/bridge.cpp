#include "bully/plugins/bridge.h"

#include <functional>

#include "bully/core/log.h"

namespace bully::plugins {

namespace {

class BridgedImporter final : public pipeline::Importer {
public:
    BridgedImporter(std::shared_ptr<PluginHost> host, PluginImporterDescriptor descriptor)
        : host_(std::move(host)), descriptor_(std::move(descriptor))
    {
    }
    BridgedImporter(std::weak_ptr<PluginHost> host, PluginImporterDescriptor descriptor)
        : host_(std::move(host)), descriptor_(std::move(descriptor))
    {
    }

    [[nodiscard]] std::string id() const override { return descriptor_.importer_id; }
    [[nodiscard]] std::string display_name() const override
    {
        return descriptor_.display_name.empty() ? descriptor_.importer_id
                                                : descriptor_.display_name;
    }
    [[nodiscard]] std::string version() const override
    {
        return descriptor_.version.empty() ? "0.0.0" : descriptor_.version;
    }
    [[nodiscard]] std::vector<std::string> extensions() const override
    {
        return descriptor_.extensions;
    }
    [[nodiscard]] pipeline::AssetType produces() const override
    {
        return pipeline::AssetType::Other;
    }

    [[nodiscard]] core::Result<pipeline::ImportResult> import(
        const pipeline::ImportContext& context) override
    {
        auto host = host_.lock();
        if (!host) {
            core::DiagnosticList diagnostics;
            diagnostics.error("BULLY-PLUG-0040",
                              "plugin '" + descriptor_.plugin_id +
                                  "' is no longer loaded; cannot import",
                              context.relative_source_path);
            return core::Result<pipeline::ImportResult>(std::move(diagnostics));
        }
        std::vector<std::pair<std::string, std::string>> settings;
        settings.emplace_back("__importer_version", version());
        for (const auto& setting : context.settings.values) {
            settings.emplace_back(setting.key, setting.value);
        }
        auto result = host->run_plugin_import(descriptor_.plugin_id, descriptor_.importer_id,
                                              context.asset_id, context.source_path,
                                              context.relative_source_path, context.output_path,
                                              settings);
        if (!result) return core::Result<pipeline::ImportResult>(result.diagnostics());
        pipeline::ImportResult imported;
        imported.output_bytes = std::move(result.value().bytes);
        imported.summary = result.value().summary;
        imported.report_json = result.value().report_json;
        imported.asset_type = pipeline::AssetType::Other;
        for (std::uint32_t i = 0; i < result.value().warning_count; ++i) {
            imported.diagnostics.warn("BULLY-PLUG-0041",
                                      "plugin importer reported a warning (#" +
                                          std::to_string(i + 1) + ")",
                                      context.relative_source_path);
        }
        return imported;
    }

private:
    std::weak_ptr<PluginHost> host_;
    PluginImporterDescriptor descriptor_;
};

}  // namespace

std::shared_ptr<pipeline::Importer> make_plugin_importer(std::shared_ptr<PluginHost> host,
                                                        PluginImporterDescriptor descriptor)
{
    return std::make_shared<BridgedImporter>(std::move(host), std::move(descriptor));
}

std::size_t register_plugin_importers(std::shared_ptr<PluginHost> host,
                                      pipeline::ImporterRegistry& registry)
{
    if (!host) return 0;
    // The registry stores factories. They capture a weak_ptr so that an
    // unloaded plugin cannot be resurrected through a stale factory: the
    // bridged importer reports a clear "plugin is no longer loaded" error.
    std::size_t registered = 0;
    for (const auto& descriptor : host->plugin_importers()) {
        const PluginImporterDescriptor captured = descriptor;
        std::weak_ptr<PluginHost> weak_host = host;
        registry.register_importer([weak_host, captured]() {
            return std::make_shared<BridgedImporter>(weak_host.lock(), captured);
        });
        core::log_info("plugins", "registered plugin importer '" + descriptor.importer_id +
                                      "' from '" + descriptor.plugin_id + "'");
        ++registered;
    }
    return registered;
}

SqlitePluginStateStore::SqlitePluginStateStore(project::Database& database) : db_(database) {}

core::Status SqlitePluginStateStore::initialize()
{
    return db_.exec(
        "CREATE TABLE IF NOT EXISTS plugin_state ("
        "plugin_id TEXT PRIMARY KEY, version TEXT NOT NULL DEFAULT '',"
        "enabled INTEGER NOT NULL DEFAULT 1, last_error TEXT NOT NULL DEFAULT '',"
        "updated_unix INTEGER NOT NULL DEFAULT 0);");
}

bool SqlitePluginStateStore::enabled(const std::string& plugin_id, bool fallback) const
{
    auto rows = db_.query("SELECT enabled FROM plugin_state WHERE plugin_id = ?;", {plugin_id});
    if (!rows || rows.value().empty()) return fallback;
    return rows.value().front().at("enabled") != "0";
}

void SqlitePluginStateStore::set_enabled(const std::string& plugin_id, bool enabled)
{
    (void)db_.exec(
        "INSERT INTO plugin_state (plugin_id, enabled, updated_unix) VALUES (?,?,strftime('%s','now')) "
        "ON CONFLICT(plugin_id) DO UPDATE SET enabled=excluded.enabled, "
        "updated_unix=excluded.updated_unix;",
        {plugin_id, project::SqlValue(enabled)});
}

void SqlitePluginStateStore::set_error(const std::string& plugin_id, std::string error)
{
    (void)db_.exec(
        "INSERT INTO plugin_state (plugin_id, last_error, updated_unix) "
        "VALUES (?,?,strftime('%s','now')) "
        "ON CONFLICT(plugin_id) DO UPDATE SET last_error=excluded.last_error, "
        "updated_unix=excluded.updated_unix;",
        {plugin_id, std::move(error)});
}

}  // namespace bully::plugins
