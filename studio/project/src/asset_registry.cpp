#include "bully/project/asset_registry.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bully/core/strings.h"

namespace bully::project {

namespace {

AssetRecord record_from_row(const Row& row)
{
    AssetRecord record;
    auto value = [&row](std::string_view key) -> std::string {
        const auto it = row.find(std::string(key));
        return it != row.end() ? it->second : std::string{};
    };
    record.asset_id = value("asset_id");
    record.name = value("name");
    record.type = pipeline::asset_type_from_string(value("type"));
    record.source_path = value("source_path");
    record.runtime_path = value("runtime_path");
    record.version = value("version");
    record.source_hash = value("source_hash");
    record.settings_hash = value("settings_hash");
    record.output_hash = value("output_hash");
    record.status = pipeline::build_status_from_string(value("status"));
    record.last_error = value("last_error");
    record.last_built_unix = std::stoll(value("last_built") .empty() ? "0" : value("last_built"));
    record.modified_unix = std::stoll(value("modified").empty() ? "0" : value("modified"));
    record.metadata_json = value("metadata_json").empty() ? "{}" : value("metadata_json");
    record.settings.importer_id = value("importer_id");
    record.settings.options_json = value("settings_json").empty() ? "{}" : value("settings_json");
    // key=value;key=value encoding keeps the registry schema stable while the
    // importer setting schema is free to evolve per importer.
    const std::string settings_blob = value("settings_values");
    for (const auto& pair : core::split(settings_blob, ';')) {
        if (pair.empty()) continue;
        const auto separator = pair.find('=');
        if (separator == std::string::npos) continue;
        record.settings.values.push_back(
            pipeline::ImportSetting{pair.substr(0, separator), pair.substr(separator + 1)});
    }
    return record;
}

std::string encode_settings(const pipeline::ImportSettings& settings)
{
    std::vector<std::string> parts;
    parts.reserve(settings.values.size());
    for (const auto& setting : settings.values) {
        parts.push_back(setting.key + "=" + setting.value);
    }
    return core::join(parts, ";");
}

constexpr const char* kSelectColumns =
    "asset_id, name, type, source_path, runtime_path, version, source_hash, settings_hash, "
    "output_hash, status, last_error, last_built, modified, metadata_json, importer_id, "
    "settings_json, settings_values";

}  // namespace

AssetRegistry::AssetRegistry(Database& database) : db_(database) {}

core::Status AssetRegistry::initialize_schema()
{
    const std::vector<std::pair<int, std::string>> migrations{
        {1, R"SQL(
CREATE TABLE IF NOT EXISTS assets (
    asset_id       TEXT PRIMARY KEY,
    name           TEXT NOT NULL DEFAULT '',
    type           TEXT NOT NULL DEFAULT 'unknown',
    source_path    TEXT NOT NULL DEFAULT '',
    runtime_path   TEXT NOT NULL DEFAULT '',
    version        TEXT NOT NULL DEFAULT '',
    source_hash    TEXT NOT NULL DEFAULT '',
    settings_hash  TEXT NOT NULL DEFAULT '',
    output_hash    TEXT NOT NULL DEFAULT '',
    status         TEXT NOT NULL DEFAULT 'unknown',
    last_error     TEXT NOT NULL DEFAULT '',
    last_built     INTEGER NOT NULL DEFAULT 0,
    modified       INTEGER NOT NULL DEFAULT 0,
    metadata_json  TEXT NOT NULL DEFAULT '{}',
    importer_id    TEXT NOT NULL DEFAULT '',
    settings_json  TEXT NOT NULL DEFAULT '{}',
    settings_values TEXT NOT NULL DEFAULT ''
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_assets_source ON assets(source_path);
CREATE INDEX IF NOT EXISTS idx_assets_type ON assets(type);
CREATE INDEX IF NOT EXISTS idx_assets_status ON assets(status);

CREATE TABLE IF NOT EXISTS asset_dependencies (
    asset_id       TEXT NOT NULL,
    depends_on     TEXT NOT NULL,
    required       INTEGER NOT NULL DEFAULT 1,
    reason         TEXT NOT NULL DEFAULT '',
    PRIMARY KEY (asset_id, depends_on),
    FOREIGN KEY (asset_id) REFERENCES assets(asset_id) ON DELETE CASCADE
);
CREATE INDEX IF NOT EXISTS idx_deps_target ON asset_dependencies(depends_on);

CREATE TABLE IF NOT EXISTS build_cache (
    build_key      TEXT PRIMARY KEY,
    asset_id       TEXT NOT NULL,
    output_hash    TEXT NOT NULL DEFAULT '',
    output_path    TEXT NOT NULL DEFAULT '',
    created_unix   INTEGER NOT NULL DEFAULT 0,
    tool_version   TEXT NOT NULL DEFAULT ''
);
)SQL"},
        {2, R"SQL(
CREATE TABLE IF NOT EXISTS plugin_state (
    plugin_id      TEXT PRIMARY KEY,
    version        TEXT NOT NULL DEFAULT '',
    enabled        INTEGER NOT NULL DEFAULT 1,
    last_error     TEXT NOT NULL DEFAULT '',
    updated_unix   INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS qa_reports (
    report_id      TEXT PRIMARY KEY,
    created_unix   INTEGER NOT NULL DEFAULT 0,
    kind           TEXT NOT NULL DEFAULT 'full',
    errors         INTEGER NOT NULL DEFAULT 0,
    warnings       INTEGER NOT NULL DEFAULT 0,
    payload_json   TEXT NOT NULL DEFAULT '{}'
);

CREATE TABLE IF NOT EXISTS story_revisions (
    revision_id    TEXT PRIMARY KEY,
    asset_id       TEXT NOT NULL,
    created_unix   INTEGER NOT NULL DEFAULT 0,
    author         TEXT NOT NULL DEFAULT '',
    message        TEXT NOT NULL DEFAULT '',
    payload_json   TEXT NOT NULL DEFAULT '{}'
);
CREATE INDEX IF NOT EXISTS idx_revisions_asset ON story_revisions(asset_id);
)SQL"}};
    return db_.migrate(migrations);
}

core::Status AssetRegistry::upsert(const AssetRecord& record)
{
    core::DiagnosticList diagnostics;
    if (record.asset_id.empty()) {
        diagnostics.error("BULLY-REG-0001", "cannot store an asset without an id", record.source_path);
        return core::Status(std::move(diagnostics));
    }
    Database::Transaction transaction(db_);
    auto status = db_.exec(
        "INSERT INTO assets (asset_id, name, type, source_path, runtime_path, version, source_hash, "
        "settings_hash, output_hash, status, last_error, last_built, modified, metadata_json, "
        "importer_id, settings_json, settings_values) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(asset_id) DO UPDATE SET name=excluded.name, type=excluded.type, "
        "source_path=excluded.source_path, runtime_path=excluded.runtime_path, "
        "version=excluded.version, source_hash=excluded.source_hash, "
        "settings_hash=excluded.settings_hash, output_hash=excluded.output_hash, "
        "status=excluded.status, last_error=excluded.last_error, last_built=excluded.last_built, "
        "modified=excluded.modified, metadata_json=excluded.metadata_json, "
        "importer_id=excluded.importer_id, settings_json=excluded.settings_json, "
        "settings_values=excluded.settings_values",
        {record.asset_id,
         record.name,
         std::string(pipeline::to_string(record.type)),
         record.source_path,
         record.runtime_path,
         record.version,
         record.source_hash,
         record.settings_hash,
         record.output_hash,
         std::string(pipeline::to_string(record.status)),
         record.last_error,
         SqlValue(static_cast<std::int64_t>(record.last_built_unix)),
         SqlValue(static_cast<std::int64_t>(record.modified_unix)),
         record.metadata_json,
         record.settings.importer_id,
         record.settings.options_json,
         encode_settings(record.settings)});
    if (!status) return status;

    auto cleared = db_.exec("DELETE FROM asset_dependencies WHERE asset_id = ?;",
                            {record.asset_id});
    if (!cleared) return cleared;
    for (const auto& dependency : record.dependencies) {
        auto inserted = db_.exec(
            "INSERT OR REPLACE INTO asset_dependencies (asset_id, depends_on, required, reason) "
            "VALUES (?,?,?,?);",
            {record.asset_id, dependency.asset_id, SqlValue(dependency.required), dependency.reason});
        if (!inserted) return inserted;
    }
    transaction.commit();
    return core::Status{};
}

core::Status AssetRegistry::upsert_all(const std::vector<AssetRecord>& records)
{
    Database::Transaction transaction(db_);
    for (const auto& record : records) {
        auto status = upsert(record);
        if (!status) {
            transaction.rollback();
            return status;
        }
    }
    transaction.commit();
    return core::Status{};
}

core::Status AssetRegistry::remove(std::string_view asset_id)
{
    Database::Transaction transaction(db_);
    auto status = db_.exec("DELETE FROM asset_dependencies WHERE asset_id = ?;",
                           {std::string(asset_id)});
    if (!status) return status;
    status = db_.exec("DELETE FROM assets WHERE asset_id = ?;", {std::string(asset_id)});
    if (!status) return status;
    transaction.commit();
    return core::Status{};
}

namespace {

std::vector<AssetRecord> load_records(Database& db, std::string_view where,
                                      const SqlParams& params)
{
    std::string sql = std::string("SELECT ") + kSelectColumns + " FROM assets ";
    if (!where.empty()) sql += std::string("WHERE ") + std::string(where) + " ";
    sql += "ORDER BY name COLLATE NOCASE;";
    auto rows = db.query(sql, params);
    if (!rows) return {};
    std::vector<AssetRecord> records;
    records.reserve(rows.value().size());
    for (const auto& row : rows.value()) {
        AssetRecord record = record_from_row(row);
        auto deps = db.query(
            "SELECT depends_on, required, reason FROM asset_dependencies WHERE asset_id = ?;",
            {record.asset_id});
        if (deps) {
            for (const auto& dep_row : deps.value()) {
                pipeline::AssetDependency dependency;
                dependency.asset_id = dep_row.at("depends_on");
                dependency.required = dep_row.at("required") != "0";
                dependency.reason = dep_row.at("reason");
                record.dependencies.push_back(std::move(dependency));
            }
        }
        records.push_back(std::move(record));
    }
    return records;
}

}  // namespace

std::optional<AssetRecord> AssetRegistry::find(std::string_view asset_id) const
{
    auto records = load_records(db_, "asset_id = ?", {std::string(asset_id)});
    if (records.empty()) return std::nullopt;
    return records.front();
}

std::optional<AssetRecord> AssetRegistry::find_by_source_path(std::string_view path) const
{
    auto records = load_records(db_, "source_path = ?", {std::string(path)});
    if (records.empty()) return std::nullopt;
    return records.front();
}

std::vector<AssetRecord> AssetRegistry::all() const
{
    return load_records(db_, {}, {});
}

std::vector<AssetRecord> AssetRegistry::by_type(AssetType type) const
{
    return load_records(db_, "type = ?", {std::string(pipeline::to_string(type))});
}

std::vector<AssetRecord> AssetRegistry::by_status(BuildStatus status) const
{
    return load_records(db_, "status = ?", {std::string(pipeline::to_string(status))});
}

std::vector<AssetRecord> AssetRegistry::search(std::string_view query) const
{
    const std::string pattern = "%" + core::to_lower(std::string(query)) + "%";
    return load_records(db_,
                        "lower(name) LIKE ? OR lower(source_path) LIKE ? OR lower(asset_id) LIKE ?",
                        {pattern, pattern, pattern});
}

core::Status AssetRegistry::set_status(std::string_view asset_id, BuildStatus status,
                                       std::string error)
{
    return db_.exec("UPDATE assets SET status = ?, last_error = ? WHERE asset_id = ?;",
                    {std::string(pipeline::to_string(status)), std::move(error),
                     std::string(asset_id)});
}

core::Status AssetRegistry::set_output(std::string_view asset_id, std::string runtime_path,
                                       std::string output_hash, std::string version)
{
    return db_.exec(
        "UPDATE assets SET runtime_path = ?, output_hash = ?, version = ?, status = ?, "
        "last_built = strftime('%s','now') WHERE asset_id = ?;",
        {std::move(runtime_path), std::move(output_hash), std::move(version),
         std::string(pipeline::to_string(BuildStatus::UpToDate)), std::string(asset_id)});
}

core::Status AssetRegistry::set_dependencies(
    std::string_view asset_id, const std::vector<pipeline::AssetDependency>& dependencies)
{
    Database::Transaction transaction(db_);
    auto cleared = db_.exec("DELETE FROM asset_dependencies WHERE asset_id = ?;",
                            {std::string(asset_id)});
    if (!cleared) {
        transaction.rollback();
        return cleared;
    }
    for (const auto& dependency : dependencies) {
        if (dependency.asset_id.empty()) continue;
        auto inserted = db_.exec(
            "INSERT OR REPLACE INTO asset_dependencies (asset_id, depends_on, required, reason) "
            "VALUES (?,?,?,?);",
            {std::string(asset_id), dependency.asset_id, SqlValue(dependency.required),
             dependency.reason});
        if (!inserted) {
            transaction.rollback();
            return inserted;
        }
    }
    transaction.commit();
    return core::Status{};
}

std::vector<AssetRecord> AssetRegistry::dependents_of(std::string_view asset_id) const
{
    auto rows = db_.query("SELECT asset_id FROM asset_dependencies WHERE depends_on = ?;",
                          {std::string(asset_id)});
    std::vector<AssetRecord> records;
    if (!rows) return records;
    for (const auto& row : rows.value()) {
        if (auto record = find(row.at("asset_id"))) records.push_back(std::move(record.value()));
    }
    return records;
}

RegistryStatistics AssetRegistry::statistics() const
{
    RegistryStatistics stats;
    stats.total = static_cast<std::uint32_t>(
        db_.query_int("SELECT COUNT(*) FROM assets;", {}, 0));
    stats.up_to_date = static_cast<std::uint32_t>(db_.query_int(
        "SELECT COUNT(*) FROM assets WHERE status = 'up_to_date';", {}, 0));
    stats.pending = static_cast<std::uint32_t>(db_.query_int(
        "SELECT COUNT(*) FROM assets WHERE status IN ('pending','building');", {}, 0));
    stats.failed = static_cast<std::uint32_t>(
        db_.query_int("SELECT COUNT(*) FROM assets WHERE status = 'failed';", {}, 0));
    stats.unknown = stats.total - stats.up_to_date - stats.pending - stats.failed;
    return stats;
}

std::uint32_t AssetRegistry::count() const
{
    return static_cast<std::uint32_t>(db_.query_int("SELECT COUNT(*) FROM assets;", {}, 0));
}

std::vector<std::string> AssetRegistry::missing_source_files(const std::string& project_root) const
{
    std::vector<std::string> missing;
    const std::filesystem::path root(project_root);
    for (const auto& record : all()) {
        if (record.source_path.empty()) continue;
        const std::filesystem::path path = root / record.source_path;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) missing.push_back(record.asset_id);
    }
    return missing;
}

}  // namespace bully::project
