#include "bully/pipeline/import_cache.h"

#include <chrono>

#include "bully/core/fs.h"
#include "bully/pipeline/asset.h"

namespace bully::pipeline {

namespace {

std::int64_t now_unix()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

ImportCache::ImportCache(project::Database& database, std::filesystem::path cache_root)
    : db_(database), cache_root_(std::move(cache_root))
{
}

core::Status ImportCache::initialize()
{
    auto status = core::FileSystem::create_directories(cache_root_ / "artifacts");
    if (!status) return status;
    // The table is normally created by the project schema migration; creating it
    // here as well makes the cache usable from headless builds that only touch
    // the pipeline.
    return db_.exec(
        "CREATE TABLE IF NOT EXISTS build_cache ("
        "build_key TEXT PRIMARY KEY, asset_id TEXT NOT NULL, output_hash TEXT NOT NULL DEFAULT '',"
        "output_path TEXT NOT NULL DEFAULT '', created_unix INTEGER NOT NULL DEFAULT 0,"
        "tool_version TEXT NOT NULL DEFAULT '');");
}

std::filesystem::path ImportCache::artifact_path(std::string_view output_hash) const
{
    return cache_root_ / "artifacts" / (std::string(output_hash) + ".bin");
}

std::optional<CacheEntry> ImportCache::find(std::string_view build_key) const
{
    auto rows = db_.query(
        "SELECT build_key, asset_id, output_hash, output_path, created_unix, tool_version "
        "FROM build_cache WHERE build_key = ?;",
        {std::string(build_key)});
    if (!rows || rows.value().empty()) return std::nullopt;
    const auto& row = rows.value().front();
    CacheEntry entry;
    entry.build_key = row.at("build_key");
    entry.asset_id = row.at("asset_id");
    entry.output_hash = row.at("output_hash");
    entry.output_path = row.at("output_path");
    try {
        entry.created_unix = std::stoll(row.at("created_unix"));
    } catch (...) {
        entry.created_unix = 0;
    }
    entry.tool_version = row.at("tool_version");
    if (!core::FileSystem::exists(entry.output_path)) {
        // Dangling cache entry: treat as a miss so the asset is rebuilt.
        return std::nullopt;
    }
    return entry;
}

core::Status ImportCache::store(std::string_view build_key, std::string_view asset_id,
                                const std::vector<std::uint8_t>& bytes,
                                std::string_view tool_version)
{
    const std::string hash = hash_hex(bytes);
    const auto path = artifact_path(hash);
    if (!core::FileSystem::exists(path)) {
        auto write_status = core::FileSystem::write_binary(path, bytes);
        if (!write_status) return write_status;
    }
    return db_.exec(
        "INSERT INTO build_cache (build_key, asset_id, output_hash, output_path, created_unix, "
        "tool_version) VALUES (?,?,?,?,?,?) "
        "ON CONFLICT(build_key) DO UPDATE SET asset_id=excluded.asset_id, "
        "output_hash=excluded.output_hash, output_path=excluded.output_path, "
        "created_unix=excluded.created_unix, tool_version=excluded.tool_version;",
        {std::string(build_key), std::string(asset_id), hash, path.string(), now_unix(),
         std::string(tool_version)});
}

core::Status ImportCache::materialize(std::string_view build_key,
                                      const std::filesystem::path& destination) const
{
    core::DiagnosticList diagnostics;
    const auto entry = find(build_key);
    if (!entry) {
        diagnostics.error("BULLY-CACHE-0001",
                          "cache miss for build key '" + std::string(build_key) + "'");
        return core::Status(std::move(diagnostics));
    }
    auto bytes = core::FileSystem::read_binary(entry->output_path);
    if (!bytes) return core::Status(bytes.diagnostics());
    if (hash_hex(bytes.value()) != entry->output_hash) {
        diagnostics.error("BULLY-CACHE-0002",
                          "cached artifact for '" + std::string(build_key) +
                              "' is corrupt (hash mismatch)");
        return core::Status(std::move(diagnostics));
    }
    return core::FileSystem::write_binary(destination, bytes.value());
}

core::Status ImportCache::clear()
{
    auto status = core::FileSystem::remove_tree(cache_root_ / "artifacts");
    if (!status) return status;
    status = core::FileSystem::create_directories(cache_root_ / "artifacts");
    if (!status) return status;
    return db_.exec("DELETE FROM build_cache;");
}

std::uint32_t ImportCache::entry_count() const
{
    return static_cast<std::uint32_t>(db_.query_int("SELECT COUNT(*) FROM build_cache;", {}, 0));
}

std::uint64_t ImportCache::total_bytes() const
{
    auto files = core::FileSystem::list_files(cache_root_ / "artifacts", true);
    if (!files) return 0;
    std::uint64_t total = 0;
    for (const auto& file : files.value()) total += core::FileSystem::file_size(file);
    return total;
}

}  // namespace bully::pipeline
