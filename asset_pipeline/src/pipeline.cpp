#include "bully/pipeline/pipeline.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <set>

#include "bully/core/fs.h"
#include "bully/core/log.h"
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

/// True when `path` ends with a compound extension such as ".scene.json".
bool matches_extension(const std::filesystem::path& path, std::string_view extension)
{
    const std::string filename = core::to_lower(path.filename().string());
    const std::string lowered = core::to_lower(extension);
    if (filename.size() < lowered.size()) return false;
    return filename.compare(filename.size() - lowered.size(), lowered.size(), lowered) == 0;
}

}  // namespace

std::string deterministic_asset_id(std::string_view relative_source)
{
    return core::Uuid::from_name(relative_source, "bully.asset").to_string();
}

Pipeline::Pipeline(ProjectLayout layout, project::Database& database,
                   project::AssetRegistry& registry, ImporterRegistry& importers,
                   ImportCache& cache)
    : layout_(std::move(layout)),
      database_(database),
      registry_(registry),
      importers_(importers),
      cache_(cache)
{
}

std::optional<AssetRecord> Pipeline::make_record_for_source(const std::filesystem::path& source) const
{
    const std::string relative = core::FileSystem::relative_to(source, layout_.root());
    auto importer = importers_.for_filename(source.filename().string());
    if (!importer) return std::nullopt;
    const auto absolute = layout_.root() / relative;
    const auto hash = core::FileSystem::hash_file(absolute);
    if (!hash) return std::nullopt;

    AssetRecord record;
    const auto existing = registry_.find_by_source_path(relative);
    record.asset_id = existing ? existing->asset_id : deterministic_asset_id(relative);
    record.name = source.stem().string();
    record.type = importer->produces();
    record.source_path = relative;
    record.settings.importer_id = importer->id();
    record.settings.set("__importer_version", importer->version());
    // Apply the importer's declared defaults; project overrides win.
    const auto schema = importer->settings_schema();
    for (const auto& descriptor : schema) {
        record.settings.set(descriptor.key, descriptor.default_value);
    }
    // The project's retro profile is a project-level default: an importer that
    // exposes a `retro_preset` setting inherits it unless the asset overrides
    // it (per-asset overrides are applied below and therefore take priority).
    const std::string project_preset = project_retro_preset();
    if (!project_preset.empty() && project_preset != "none") {
        const bool supports_preset =
            std::any_of(schema.begin(), schema.end(),
                        [](const ImportSettingDescriptor& descriptor) {
                            return descriptor.key == "retro_preset";
                        });
        if (supports_preset) record.settings.set("retro_preset", project_preset);
    }
    if (existing) {
        for (const auto& setting : existing->settings.values) {
            record.settings.set(setting.key, setting.value);
        }
    }
    record.source_hash = core::FileSystem::hash_to_hex(hash.value());
    record.settings_hash = hash_string_hex(record.settings.to_json().dump(0));
    record.version = importer->version();
    record.modified_unix = core::FileSystem::modified_time(absolute);
    record.status = BuildStatus::Pending;
    record.runtime_path = core::FileSystem::normalize(
        layout_.runtime_output_path(relative, runtime_extension(record.type)).lexically_relative(layout_.root())
            .string());
    return record;
}

std::string Pipeline::project_retro_preset() const
{
    if (!project_preset_loaded_) {
        project_preset_loaded_ = true;
        auto settings = layout_.load();
        if (settings) project_retro_preset_ = settings.value().retro_preset;
    }
    return project_retro_preset_;
}

ScanResult Pipeline::scan(const std::vector<std::string>& extensions)
{
    ScanResult result;
    std::vector<std::string> wanted = extensions;
    if (wanted.empty()) {
        wanted = importers_.all_extensions();
        // Compound authoring extensions handled by the data importers.
        for (const auto& importer : importers_.all()) {
            for (const auto& extension : importer->extensions()) {
                if (extension.size() > 5) wanted.push_back(extension);
            }
        }
    }
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());

    std::set<std::string> seen;
    for (const auto& directory : {layout_.source_dir(), layout_.content_dir()}) {
        auto files = core::FileSystem::list_files(directory, true);
        if (!files) {
            if (core::FileSystem::exists(directory)) result.diagnostics.append(files.diagnostics());
            continue;
        }
        for (const auto& file : files.value()) {
            const std::string filename = core::to_lower(file.filename().string());
            if (filename == ".gitkeep") continue;
            bool handled = false;
            for (const auto& extension : wanted) {
                if (matches_extension(file, extension)) {
                    handled = true;
                    break;
                }
            }
            if (!handled) continue;
            auto record = make_record_for_source(file);
            if (!record) continue;
            const auto existing = registry_.find(record->asset_id);
            if (!existing) {
                result.discovered.push_back(*record);
            } else if (existing->source_hash != record->source_hash ||
                       existing->settings_hash != record->settings_hash) {
                record->status = existing->status;
                result.changed.push_back(record->asset_id);
                if (auto status = registry_.upsert(*record); !status) {
                    result.diagnostics.append(status.diagnostics());
                }
                continue;
            } else {
                record->status = existing->status;
                record->runtime_path = existing->runtime_path;
            }
            seen.insert(record->asset_id);
        }
    }
    for (const auto& record : registry_.all()) {
        if (seen.find(record.asset_id) == seen.end()) {
            result.removed.push_back(record.asset_id);
        }
    }
    if (!result.discovered.empty()) {
        auto status = registry_.upsert_all(result.discovered);
        if (!status) result.diagnostics.append(status.diagnostics());
    }
    for (const auto& removed : result.removed) {
        result.diagnostics.info("BULLY-PIPE-0001", "asset '" + removed + "' no longer has a source file");
    }
    return result;
}

core::Result<std::vector<std::string>> Pipeline::build_order(
    const std::vector<std::string>& asset_ids) const
{
    core::DiagnosticList diagnostics;
    std::set<std::string> requested(asset_ids.begin(), asset_ids.end());
    std::vector<std::string> order;
    std::set<std::string> visiting;
    std::set<std::string> done;

    std::function<void(const std::string&)> visit = [&](const std::string& asset_id) {
        if (done.find(asset_id) != done.end()) return;
        if (visiting.find(asset_id) != visiting.end()) {
            diagnostics.error("BULLY-PIPE-0002",
                              "cyclic asset dependency detected at '" + asset_id + "'", asset_id);
            return;
        }
        visiting.insert(asset_id);
        const auto record = registry_.find(asset_id);
        if (!record) {
            diagnostics.warn("BULLY-PIPE-0003", "unknown asset id '" + asset_id + "'", asset_id);
        } else {
            for (const auto& dependency : record->dependencies) {
                if (dependency.asset_id.empty()) continue;
                if (registry_.find(dependency.asset_id)) visit(dependency.asset_id);
            }
        }
        visiting.erase(asset_id);
        if (done.insert(asset_id).second) order.push_back(asset_id);
    };

    for (const auto& asset_id : asset_ids) visit(asset_id);
    if (diagnostics.has_errors()) return core::Result<std::vector<std::string>>(std::move(diagnostics));
    return order;
}

std::vector<std::string> Pipeline::pending_assets() const
{
    std::vector<std::string> pending;
    for (const auto& record : registry_.all()) {
        // Failed assets are retried; a `building` status left behind by a crashed
        // build is treated the same way rather than blocking the asset forever.
        if (record.status == BuildStatus::Failed || record.status == BuildStatus::Building) {
            pending.push_back(record.asset_id);
            continue;
        }
        const auto cache_entry = cache_.find(record.build_key());
        if (!cache_entry) {
            pending.push_back(record.asset_id);
            continue;
        }
        const auto file_hash = core::FileSystem::hash_file(layout_.root() / record.source_path);
        if (!file_hash || core::FileSystem::hash_to_hex(file_hash.value()) != record.source_hash) {
            pending.push_back(record.asset_id);
        }
    }
    return pending;
}

AssetBuildResult Pipeline::build_asset(std::string_view asset_id, const BuildOptions& options,
                                       bool build_dependencies)
{
    AssetBuildResult result;
    result.asset_id = std::string(asset_id);
    const auto start = std::chrono::steady_clock::now();
    auto finish = [&result, &start]() {
        result.duration_ms = static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 start)
                .count());
    };

    const auto record_opt = registry_.find(asset_id);
    if (!record_opt) {
        result.diagnostics.error("BULLY-PIPE-0004", "asset not found in the registry", result.asset_id);
        finish();
        return result;
    }
    AssetRecord record = *record_opt;
    result.source_path = record.source_path;
    result.asset_type = record.type;

    if (build_dependencies) {
        for (const auto& dependency : record.dependencies) {
            if (dependency.asset_id.empty() || !registry_.find(dependency.asset_id)) continue;
            const auto dependency_record = registry_.find(dependency.asset_id);
            if (dependency_record && dependency_record->status == BuildStatus::UpToDate) continue;
            auto dependency_result = build_asset(dependency.asset_id, options, false);
            if (!dependency_result.success && dependency.required) {
                result.diagnostics.error(
                    "BULLY-PIPE-0005",
                    "required dependency '" + dependency.asset_id + "' (" + dependency.reason +
                        ") failed to build");
                finish();
                (void)registry_.set_status(record.asset_id, BuildStatus::Failed,
                                           "dependency build failed");
                return result;
            }
        }
    }

    const auto source_absolute = layout_.root() / record.source_path;
    const auto file_hash = core::FileSystem::hash_file(source_absolute);
    if (!file_hash) {
        result.diagnostics.error("BULLY-PIPE-0006",
                                 "source file is missing: " + record.source_path, record.source_path);
        (void)registry_.set_status(record.asset_id, BuildStatus::Failed, "source file missing");
        finish();
        return result;
    }
    const std::string current_hash = core::FileSystem::hash_to_hex(file_hash.value());
    if (current_hash != record.source_hash) {
        record.source_hash = current_hash;
    }

    const std::string build_key = record.build_key();
    const bool hash_match = current_hash == record.source_hash;
    (void)hash_match;

    // ---- incremental check ----
    if (!options.force) {
        const auto cached = cache_.find(build_key);
        if (cached) {
            if (!options.dry_run) {
                const auto destination = layout_.root() / record.runtime_path;
                auto status = cache_.materialize(build_key, destination);
                if (!status) {
                    result.diagnostics.append(status.diagnostics());
                } else {
                    (void)registry_.set_output(record.asset_id, record.runtime_path,
                                               cached->output_hash, record.version);
                    result.success = true;
                    result.skipped = true;
                    result.from_cache = true;
                    result.output_path = record.runtime_path;
                    result.output_hash = cached->output_hash;
                    result.summary = "up to date (cache hit)";
                    finish();
                    return result;
                }
            } else {
                result.success = true;
                result.skipped = true;
                result.from_cache = true;
                result.summary = "up to date (dry run)";
                finish();
                return result;
            }
        }
    }

    // ---- resolve the importer ----
    auto importer = importers_.find(record.settings.importer_id);
    if (!importer) {
        importer = importers_.for_filename(
            std::filesystem::path(record.source_path).filename().string());
    }
    if (!importer) {
        result.diagnostics.error(
            "BULLY-PIPE-0007",
            "no importer is registered for '" + record.source_path +
                "'. Install a plugin providing the format or change the asset's importer setting.",
            record.source_path);
        (void)registry_.set_status(record.asset_id, BuildStatus::Failed,
                                   "no importer registered");
        finish();
        return result;
    }
    record.settings.importer_id = importer->id();

    ImportContext context;
    context.project_root = layout_.root().string();
    context.source_path = source_absolute.string();
    context.relative_source_path = record.source_path;
    context.asset_id = record.asset_id;
    context.settings = record.settings;
    context.settings.set("__importer_version", importer->version());
    context.output_path = (layout_.root() / record.runtime_path).string();
    context.report_progress = {};
    // Cross-asset references are authored as names/paths; translate them through
    // the registry so scenes/missions carry real dependency edges.
    context.resolve_asset = [this](const std::string& reference) -> std::string {
        if (reference.empty()) return {};
        // 1. an asset id used verbatim,
        if (auto exact = registry_.find(reference); exact.has_value()) return exact->asset_id;
        // 2. a project-relative source path ("assets/models/crate.obj"),
        const std::string normalized = core::FileSystem::normalize(reference);
        if (auto by_path = registry_.find_by_source_path(normalized); by_path.has_value()) {
            return by_path->asset_id;
        }
        // 3. or a loose reference ("crate", "crate.obj") matched against the
        //    source stem - this is what authors type in the scene editor.
        const std::string stem = std::filesystem::path(normalized).stem().string();
        const std::string lowered = core::to_lower(stem);
        for (const auto& candidate : registry_.all()) {
            if (candidate.source_path.empty()) continue;
            const std::string candidate_stem =
                core::to_lower(std::filesystem::path(candidate.source_path).stem().string());
            if (candidate_stem == lowered || core::to_lower(candidate.name) == lowered) {
                return candidate.asset_id;
            }
        }
        return reference;  // unresolved: keep the authored string so QA can report it
    };

    // A dry run (QA's asset.import rule, `build --dry-run`) must leave the
    // registry untouched: marking an asset Building and then returning without a
    // status update would strand it there and make every later QA run report a
    // stale output.
    if (!options.dry_run) {
        (void)registry_.set_status(record.asset_id, BuildStatus::Building);
    }
    auto import_result = importer->import(context);
    if (!import_result) {
        result.diagnostics.append(import_result.diagnostics());
        std::string message = "import failed";
        for (const auto& item : import_result.diagnostics().all()) {
            if (item.severity == core::Severity::Error) {
                message = item.message;
                break;
            }
        }
        if (!options.dry_run) {
            (void)registry_.set_status(record.asset_id, BuildStatus::Failed, message);
        }
        finish();
        return result;
    }
    ImportResult imported = std::move(import_result.value());
    result.diagnostics.append(imported.diagnostics);
    result.summary = imported.summary;
    result.report_json = imported.report_json;
    result.asset_type = imported.asset_type;
    result.diagnostics.append(imported.diagnostics);

    if (imported.output_bytes.empty()) {
        result.skipped = true;
        result.success = !result.diagnostics.has_errors();
        if (!options.dry_run) {
            (void)registry_.set_status(record.asset_id,
                                       result.success ? BuildStatus::Skipped : BuildStatus::Failed,
                                       "importer produced no output");
        }
        finish();
        return result;
    }

    // ---- persist ----
    const std::string output_hash = hash_hex(imported.output_bytes);
    if (options.dry_run) {
        result.success = !result.diagnostics.has_errors();
        result.output_hash = output_hash;
        result.summary = "[dry run] " + result.summary;
        finish();
        return result;
    }
    if (imported.asset_type != AssetType::Unknown && imported.asset_type != record.type) {
        record.type = imported.asset_type;
    }
    if (record.runtime_path.empty()) {
        record.runtime_path = core::FileSystem::normalize(
            layout_.runtime_output_path(record.source_path, runtime_extension(record.type))
                .lexically_relative(layout_.root())
                .string());
    }
    const auto destination = layout_.root() / record.runtime_path;
    auto write_status = core::FileSystem::write_binary(destination, imported.output_bytes);
    if (!write_status) {
        result.diagnostics.append(write_status.diagnostics());
        (void)registry_.set_status(record.asset_id, BuildStatus::Failed, "cannot write output");
        finish();
        return result;
    }
    // Dependencies reported by the importer replace stale ones, but keep any
    // dependency declared by the project that the importer did not mention.
    if (!imported.dependencies.empty()) {
        std::vector<AssetDependency> merged = imported.dependencies;
        for (const auto& existing : record.dependencies) {
            const bool already = std::any_of(merged.begin(), merged.end(),
                                             [&](const AssetDependency& dependency) {
                                                 return dependency.asset_id == existing.asset_id;
                                             });
            if (!already) merged.push_back(existing);
        }
        record.dependencies = std::move(merged);
        // Persist the graph: without this an importer-discovered reference (a
        // scene pointing at a model, a dialogue pointing at a recording) would
        // only live in memory, so incremental rebuilds would never see it and QA
        // would report the target as an orphan.
        (void)registry_.set_dependencies(record.asset_id, record.dependencies);
    }
    auto cache_status = cache_.store(build_key, record.asset_id, imported.output_bytes,
                                     importer->version());
    if (!cache_status) result.diagnostics.append(cache_status.diagnostics());
    (void)registry_.set_output(record.asset_id, record.runtime_path, output_hash,
                              importer->version());

    result.success = !result.diagnostics.has_errors();
    result.output_path = record.runtime_path;
    result.output_hash = output_hash;
    if (!result.success) {
        (void)registry_.set_status(record.asset_id, BuildStatus::Failed, "build produced errors");
    }
    finish();
    if (hot_reload_ && result.success) hot_reload_(result);
    return result;
}

BuildReport Pipeline::build_assets(const std::vector<std::string>& asset_ids,
                                   const BuildOptions& options, ProgressCallback progress)
{
    BuildReport report;
    report.project_name = layout_.root().filename().string();
    report.started_utc = utc_now();
    const auto start = std::chrono::steady_clock::now();
    report.total = static_cast<std::uint32_t>(asset_ids.size());

    auto order_result = build_order(asset_ids);
    std::vector<std::string> order;
    if (order_result) {
        order = std::move(order_result.value());
    } else {
        order = asset_ids;
        for (const auto& item : order_result.diagnostics().all()) {
            core::log_warn("pipeline", item.format());
        }
    }

    std::size_t index = 0;
    for (const auto& asset_id : order) {
        auto result = build_asset(asset_id, options, false);
        if (result.success && result.skipped) {
            ++report.cached;
            if (result.from_cache) ++report.up_to_date;
        } else if (result.success) {
            ++report.built;
        } else if (result.skipped) {
            ++report.skipped;
        } else {
            ++report.failed;
        }
        if (progress) progress(index, order.size(), result);
        ++index;
        report.results.push_back(std::move(result));
        if (report.failed > 0 && options.stop_on_error) break;
    }
    report.duration_ms = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count());
    return report;
}

BuildReport Pipeline::build_all(const BuildOptions& options, ProgressCallback progress)
{
    // Report on every asset so a build report is a complete picture of the
    // project; unchanged assets resolve to a cache hit inside build_asset().
    std::vector<std::string> targets;
    for (const auto& record : registry_.all()) targets.push_back(record.asset_id);
    return build_assets(targets, options, progress);
}

core::Status Pipeline::write_report(const BuildReport& report, std::string_view file_stem) const
{
    auto status = core::FileSystem::create_directories(layout_.reports_dir());
    if (!status) return status;
    auto json_status = core::FileSystem::write_text(
        layout_.reports_dir() / (std::string(file_stem) + ".json"), report.to_json() + "\n");
    if (!json_status) return json_status;
    return core::FileSystem::write_text(
        layout_.reports_dir() / (std::string(file_stem) + ".md"), report.to_markdown());
}

std::vector<AssetBuildResult> Pipeline::poll_changes()
{
    std::vector<AssetBuildResult> changed;
    for (const auto& record : registry_.all()) {
        const auto absolute = layout_.root() / record.source_path;
        const std::int64_t mtime = core::FileSystem::modified_time(absolute);
        auto found = known_mtimes_.find(record.asset_id);
        if (found == known_mtimes_.end()) {
            known_mtimes_[record.asset_id] = mtime;
            continue;
        }
        if (found->second == mtime) continue;
        found->second = mtime;
        const auto file_hash = core::FileSystem::hash_file(absolute);
        if (!file_hash) continue;
        if (core::FileSystem::hash_to_hex(file_hash.value()) == record.source_hash) continue;
        AssetBuildResult result;
        result.asset_id = record.asset_id;
        result.source_path = record.source_path;
        result.diagnostics.info("BULLY-PIPE-0008",
                                "source changed on disk: " + record.source_path);
        changed.push_back(std::move(result));
    }
    return changed;
}

// ---------------------------------------------------------------- report ----

std::string BuildReport::to_json() const
{
    core::json::Value root = core::json::Value::make_object();
    root["project"] = project_name;
    root["started_utc"] = started_utc;
    root["total"] = total;
    root["built"] = built;
    root["cached"] = cached;
    root["up_to_date"] = up_to_date;
    root["failed"] = failed;
    root["skipped"] = skipped;
    root["duration_ms"] = duration_ms;
    root["success"] = success();
    core::json::Value list = core::json::Value::make_array();
    for (const auto& result : results) {
        core::json::Value entry = core::json::Value::make_object();
        entry["asset_id"] = result.asset_id;
        entry["source_path"] = result.source_path;
        entry["type"] = std::string(to_string(result.asset_type));
        entry["success"] = result.success;
        entry["skipped"] = result.skipped;
        entry["from_cache"] = result.from_cache;
        entry["duration_ms"] = result.duration_ms;
        entry["summary"] = result.summary;
        entry["output_path"] = result.output_path;
        entry["output_hash"] = result.output_hash;
        core::json::Value diagnostics = core::json::Value::make_array();
        for (const auto& item : result.diagnostics.all()) {
            core::json::Value diagnostic = core::json::Value::make_object();
            diagnostic["severity"] = std::string(to_string(item.severity));
            diagnostic["code"] = item.code;
            diagnostic["message"] = item.message;
            diagnostic["source"] = item.source;
            diagnostics.push(std::move(diagnostic));
        }
        entry["diagnostics"] = std::move(diagnostics);
        list.push(std::move(entry));
    }
    root["assets"] = std::move(list);
    return root.dump(2);
}

std::string BuildReport::to_markdown() const
{
    std::string out;
    out += "# Build report - " + project_name + "\n\n";
    out += "- Started: " + started_utc + "\n";
    out += "- Duration: " + std::to_string(duration_ms) + " ms\n";
    out += "- Assets: " + std::to_string(total) + " (built " + std::to_string(built) + ", cached " +
           std::to_string(cached) + ", failed " + std::to_string(failed) + ", skipped " +
           std::to_string(skipped) + ")\n";
    out += std::string("- Result: ") + (success() ? "PASS" : "FAIL") + "\n\n";
    out += "| Asset | Type | Result | Notes |\n|---|---|---|---|\n";
    for (const auto& result : results) {
        std::string status = result.success ? (result.skipped ? "cached" : "built") : "FAILED";
        out += "| `" + result.source_path + "` | " + std::string(to_string(result.asset_type)) +
               " | " + status + " | " + core::abbreviate(result.summary, 90) + " |\n";
    }
    std::size_t error_count = 0;
    for (const auto& result : results) error_count += result.diagnostics.all().size();
    if (error_count > 0) {
        out += "\n## Diagnostics\n\n";
        for (const auto& result : results) {
            for (const auto& item : result.diagnostics.all()) {
                out += "- " + item.format() + "\n";
            }
        }
    }
    return out;
}

}  // namespace bully::pipeline
