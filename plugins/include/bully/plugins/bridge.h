// Bully Studio - plugin importer bridge.
//
// Bridges C-ABI plugin importers into the C++ ImporterRegistry so the pipeline
// treats plugin importers exactly like built-in ones. Adding a format therefore
// requires no host code changes at all.
#pragma once

#include <cstddef>
#include <memory>
#include <vector>
#include <string>

#include "bully/pipeline/importer.h"
#include "bully/plugins/host.h"
#include "bully/project/database.h"

namespace bully::plugins {

/// Wraps one plugin importer as a pipeline::Importer.
std::shared_ptr<pipeline::Importer> make_plugin_importer(std::shared_ptr<PluginHost> host,
                                                        PluginImporterDescriptor descriptor);

/// Registers every plugin importer that is currently loaded.
std::size_t register_plugin_importers(std::shared_ptr<PluginHost> host,
                                      pipeline::ImporterRegistry& registry);

/// SQLite-backed state store so enable/disable choices and errors survive
/// restarts and show up in the Extension Manager.
class SqlitePluginStateStore final : public PluginStateStore {
public:
    explicit SqlitePluginStateStore(project::Database& database);
    [[nodiscard]] core::Status initialize();

    bool enabled(const std::string& plugin_id, bool fallback) const override;
    void set_enabled(const std::string& plugin_id, bool enabled) override;
    void set_error(const std::string& plugin_id, std::string error) override;

private:
    project::Database& db_;
};

}  // namespace bully::plugins
