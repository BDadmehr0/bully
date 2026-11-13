#include "bully/pipeline/packager.h"

#include <algorithm>
#include <chrono>
#include <ctime>

#include "bully/core/fs.h"
#include "bully/core/strings.h"
#include "bully/core/uuid.h"

namespace bully::pipeline {

namespace {

std::string utc_now()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

bool type_selected(const std::vector<AssetType>& types, AssetType type)
{
    if (types.empty()) return true;
    return std::find(types.begin(), types.end(), type) != types.end();
}

}  // namespace

Packager::Packager(ProjectLayout layout, project::AssetRegistry& registry)
    : layout_(std::move(layout)), registry_(registry)
{
}

PackageResult Packager::build_package(const PackageOptions& options) const
{
    PackageResult result;
    formats::PackageDocument package;
    package.name = options.package_name.empty() ? layout_.root().filename().string()
                                                : options.package_name;
    package.asset_id = core::Uuid::from_name(package.name + "|" + options.target_platform,
                                             "bully.package")
                           .to_string();
    package.target_platform = options.target_platform;
    package.build_profile = options.build_profile;

    struct Item {
        std::string logical_path;
        AssetType type;
        std::string asset_id;
        std::filesystem::path path;
        bool stale;
    };
    std::vector<Item> items;
    for (const auto& record : registry_.all()) {
        if (record.runtime_path.empty()) continue;
        if (!type_selected(options.include_types, record.type)) continue;
        const auto path = layout_.root() / record.runtime_path;
        if (!core::FileSystem::exists(path)) {
            result.diagnostics.warn("BULLY-PKG-0020",
                                    "asset '" + record.asset_id +
                                        "' has no compiled output; rebuild before packaging",
                                    record.source_path);
            continue;
        }
        const bool stale = record.status != BuildStatus::UpToDate;
        if (stale && !options.include_stale) {
            result.diagnostics.warn("BULLY-PKG-0021",
                                    "skipping stale asset '" + record.asset_id + "'",
                                    record.source_path);
            continue;
        }
        if (stale) {
            result.diagnostics.warn("BULLY-PKG-0022",
                                    "packaging stale asset '" + record.asset_id +
                                        "' (status: " + std::string(to_string(record.status)) + ")",
                                    record.source_path);
        }
        Item item;
        item.logical_path = core::FileSystem::relative_to(path, layout_.built_dir());
        item.type = record.type;
        item.asset_id = record.asset_id;
        item.path = path;
        item.stale = stale;
        items.push_back(std::move(item));
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.logical_path < b.logical_path;
    });

    formats::PackageManifest manifest;
    manifest.package_name = package.name;
    manifest.target_platform = package.target_platform;
    manifest.build_profile = package.build_profile;
    manifest.tool_version = core::kPlatformVersion.to_string();
    manifest.deterministic = !options.include_timestamp;
    if (options.include_timestamp) manifest.created_utc = utc_now();

    for (const auto& item : items) {
        auto bytes = core::FileSystem::read_binary(item.path);
        if (!bytes) {
            result.diagnostics.append(bytes.diagnostics());
            continue;
        }
        const auto kind = formats::package_kind_from_extension(item.path.extension().string());
        auto status = package.add(kind, item.logical_path, item.asset_id, bytes.value());
        if (!status) {
            result.diagnostics.append(status.diagnostics());
            continue;
        }
        formats::PackageEntry entry;
        entry.asset_id = item.asset_id;
        entry.kind = kind;
        entry.logical_path = item.logical_path;
        entry.size = static_cast<std::uint32_t>(bytes.value().size());
        entry.crc = formats::crc32(bytes.value().data(), bytes.value().size());
        manifest.entries.push_back(std::move(entry));
        result.total_bytes += bytes.value().size();
    }
    manifest.metadata["asset_count"] = std::to_string(manifest.entries.size());
    manifest.metadata["stale_assets"] =
        std::to_string(std::count_if(items.begin(), items.end(), [](const Item& item) {
            return item.stale;
        }));
    package.manifest_json = manifest.to_json();

    auto validation = package.validate();
    result.diagnostics.append(validation.diagnostics());
    if (!validation && !result.diagnostics.all().empty()) {
        result.success = false;
    }

    auto status = core::FileSystem::create_directories(layout_.packages_dir());
    if (!status) {
        result.diagnostics.append(status.diagnostics());
        return result;
    }
    const auto package_path =
        layout_.packages_dir() / (options.output_name + ".bpackage");
    const auto manifest_path =
        layout_.packages_dir() / (options.output_name + ".manifest.json");
    auto write_status = core::FileSystem::write_binary(package_path, package.serialize());
    if (!write_status) {
        result.diagnostics.append(write_status.diagnostics());
        return result;
    }
    write_status = core::FileSystem::write_text(manifest_path, manifest.to_json() + "\n");
    if (!write_status) {
        result.diagnostics.append(write_status.diagnostics());
        return result;
    }
    result.package_path = package_path.string();
    result.manifest_path = manifest_path.string();
    result.entry_count = static_cast<std::uint32_t>(package.entries.size());
    result.success = !result.diagnostics.has_errors();
    return result;
}

core::Status Packager::write_manifest(const formats::PackageManifest& manifest,
                                      const std::filesystem::path& path) const
{
    return core::FileSystem::write_text(path, manifest.to_json() + "\n");
}

}  // namespace bully::pipeline
