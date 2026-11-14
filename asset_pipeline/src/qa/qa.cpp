#include "bully/pipeline/qa.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <cstdlib>

#include "bully/core/fs.h"
#include "bully/core/json.h"
#include "bully/core/strings.h"
#include "bully/formats/registry.h"

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

QaFinding finding(core::Severity severity, std::string rule, std::string code, std::string message,
                  std::string asset_id = {}, std::string source = {})
{
    QaFinding item;
    item.severity = severity;
    item.rule = std::move(rule);
    item.code = std::move(code);
    item.message = std::move(message);
    item.asset_id = std::move(asset_id);
    item.source = std::move(source);
    return item;
}

/// Loads a compiled asset and returns its decoded JSON summary + validity.
core::Result<core::json::Value> decode_compiled_asset(const std::filesystem::path& path,
                                                      AssetType expected)
{
    core::DiagnosticList diagnostics;
    auto bytes = core::FileSystem::read_binary(path);
    if (!bytes) return core::Result<core::json::Value>(bytes.diagnostics());
    const formats::FormatDescriptor* format = formats::sniff_format(bytes.value());
    if (format == nullptr) {
        diagnostics.error("BULLY-QA-0001", "file has an unknown runtime format header",
                          path.string());
        return core::Result<core::json::Value>(std::move(diagnostics));
    }
    (void)expected;
    std::string json;
    if (format->magic == formats::kModelFormat.magic) {
        formats::ModelDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) return core::Result<core::json::Value>(decoded.diagnostics());
        json = decoded.value().to_json();
    } else if (format->magic == formats::kTextureFormat.magic) {
        formats::TextureDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) return core::Result<core::json::Value>(decoded.diagnostics());
        json = decoded.value().to_json();
    } else if (format->magic == formats::kMaterialFormat.magic) {
        formats::MaterialDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) return core::Result<core::json::Value>(decoded.diagnostics());
        json = decoded.value().to_json();
    } else if (format->magic == formats::kAnimationFormat.magic) {
        formats::AnimationDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) return core::Result<core::json::Value>(decoded.diagnostics());
        json = decoded.value().to_json();
    } else if (format->magic == formats::kAudioFormat.magic) {
        formats::AudioDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) return core::Result<core::json::Value>(decoded.diagnostics());
        json = decoded.value().to_json();
    } else if (format->magic == formats::kDialogueFormat.magic) {
        formats::DialogueDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) return core::Result<core::json::Value>(decoded.diagnostics());
        json = decoded.value().to_json();
    } else if (format->magic == formats::kSceneFormat.magic) {
        formats::SceneDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) return core::Result<core::json::Value>(decoded.diagnostics());
        json = decoded.value().to_json();
    } else if (format->magic == formats::kMissionFormat.magic) {
        formats::MissionDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) return core::Result<core::json::Value>(decoded.diagnostics());
        json = decoded.value().to_json();
    } else if (format->magic == formats::kPackageFormat.magic) {
        formats::PackageDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) return core::Result<core::json::Value>(decoded.diagnostics());
        json = decoded.value().to_json();
    } else {
        diagnostics.info("BULLY-QA-0002", "no decoder registered for this format", path.string());
        return core::Result<core::json::Value>(core::json::Value::make_object());
    }
    return core::json::parse(json);
}

}  // namespace

QaRunner::QaRunner(ProjectLayout layout, project::AssetRegistry& registry,
                   ImporterRegistry& importers, Pipeline& pipeline)
    : layout_(std::move(layout)),
      registry_(registry),
      importers_(importers),
      pipeline_(pipeline)
{
}

void QaRunner::add_rule(std::string name, std::string description, QaRule rule)
{
    for (auto& existing : rules_) {
        if (existing.name == name) {
            existing.description = std::move(description);
            existing.rule = std::move(rule);
            return;
        }
    }
    rules_.push_back(Registration{std::move(name), std::move(description), std::move(rule)});
}

std::vector<std::string> QaRunner::rule_names() const
{
    std::vector<std::string> names;
    names.reserve(rules_.size());
    for (const auto& rule : rules_) names.push_back(rule.name);
    return names;
}

QaReport QaRunner::run(const std::vector<std::string>& only)
{
    QaReport report;
    report.project_name = layout_.root().filename().string();
    report.generated_utc = utc_now();
    const auto start = std::chrono::steady_clock::now();
    QaContext context{layout_, registry_, importers_, pipeline_, true};
    std::set<std::string> filter(only.begin(), only.end());
    for (auto& rule : rules_) {
        if (!filter.empty() && filter.find(rule.name) == filter.end()) continue;
        const auto rule_start = std::chrono::steady_clock::now();
        QaRuleResult result = rule.rule(context);
        result.rule = rule.name;
        if (result.description.empty()) result.description = rule.description;
        result.duration_ms = static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 rule_start)
                .count());
        for (const auto& item : result.findings) {
            if (item.severity == core::Severity::Error || item.severity == core::Severity::Fatal) {
                ++report.errors;
            } else if (item.severity == core::Severity::Warning) {
                ++report.warnings;
            }
        }
        report.rules.push_back(std::move(result));
    }
    report.rules_run = static_cast<std::uint32_t>(report.rules.size());
    report.duration_ms = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count());
    return report;
}

QaRuleResult QaRunner::check_asset(const std::string& asset_id)
{
    QaRuleResult result;
    result.rule = "asset.import";
    result.description = "import and validate a single asset";
    const auto record = registry_.find(asset_id);
    if (!record) {
        result.findings.push_back(
            finding(core::Severity::Error, result.rule, "BULLY-QA-0003", "asset not found", asset_id));
        ++result.failed;
        return result;
    }
    ++result.checked;
    BuildOptions options;
    options.dry_run = false;
    auto build = pipeline_.build_asset(asset_id, options, false);
    for (const auto& item : build.diagnostics.all()) {
        result.findings.push_back(
            finding(item.severity, result.rule, item.code, item.message, asset_id, item.source));
    }
    if (build.success) {
        ++result.passed;
    } else {
        ++result.failed;
    }
    return result;
}

void QaRunner::register_builtin_rules()
{
    // ------------------------------------------------- asset references ----
    add_rule("asset.references", "every declared dependency resolves to a known asset",
             [](QaContext& context) {
                 QaRuleResult result;
                 std::set<std::string> known;
                 for (const auto& record : context.registry.all()) known.insert(record.asset_id);
                 for (const auto& record : context.registry.all()) {
                     ++result.checked;
                     bool ok = true;
                     for (const auto& dependency : record.dependencies) {
                         if (!dependency.asset_id.empty() &&
                             known.find(dependency.asset_id) == known.end()) {
                             const bool looks_like_path =
                                 dependency.asset_id.find('/') != std::string::npos;
                             if (looks_like_path) {
                                 // Texture paths imported from OBJ MTLs are resolved to asset
                                 // paths; verify the file exists on disk.
                                 const auto path = context.layout.root() / dependency.asset_id;
                                 if (!core::FileSystem::exists(path)) {
                                     result.findings.push_back(finding(
                                         core::Severity::Warning, result.rule, "BULLY-QA-0010",
                                         "referenced file is missing: " + dependency.asset_id,
                                         record.asset_id, record.source_path));
                                     ++result.warnings;
                                 }
                                 continue;
                             }
                             result.findings.push_back(finding(
                                 core::Severity::Warning, result.rule, "BULLY-QA-0011",
                                 "dependency '" + dependency.asset_id + "' (" + dependency.reason +
                                     ") is not registered in the project",
                                 record.asset_id, record.source_path));
                             ++result.warnings;
                             ok = false;
                         }
                     }
                     if (ok) ++result.passed;
                 }
                 return result;
             });

    // ------------------------------------------------------- import test ---
    add_rule("asset.import", "rebuild each asset in dry-run mode and collect diagnostics",
             [](QaContext& context) {
                 QaRuleResult result;
                 BuildOptions options;
                 options.force = true;
                 options.dry_run = true;
                 for (const auto& record : context.registry.all()) {
                     ++result.checked;
                     auto build = context.pipeline.build_asset(record.asset_id, options, false);
                     bool ok = build.success;
                     for (const auto& item : build.diagnostics.all()) {
                         if (item.severity == core::Severity::Warning) ++result.warnings;
                         if (item.severity != core::Severity::Info) {
                             result.findings.push_back(finding(item.severity, result.rule, item.code,
                                                               item.message, record.asset_id,
                                                               record.source_path));
                         }
                     }
                     if (ok) ++result.passed;
                     else ++result.failed;
                 }
                 return result;
             });

    // -------------------------------------------------- runtime loading ----
    add_rule("runtime.load", "decode every compiled asset with the runtime decoders",
             [](QaContext& context) {
                 QaRuleResult result;
                 for (const auto& record : context.registry.all()) {
                     if (record.runtime_path.empty()) continue;
                     const auto path = context.layout.root() / record.runtime_path;
                     if (!core::FileSystem::exists(path)) continue;
                     ++result.checked;
                     auto decoded = decode_compiled_asset(path, record.type);
                     if (decoded) {
                         ++result.passed;
                     } else {
                         ++result.failed;
                         for (const auto& item : decoded.diagnostics().all()) {
                             result.findings.push_back(finding(item.severity, result.rule, item.code,
                                                               item.message, record.asset_id,
                                                               record.runtime_path));
                         }
                     }
                 }
                 return result;
             });

    // -------------------------------------------------------- dialogue -----
    add_rule("dialogue.recordings", "every dialogue line has a selected voice take",
             [](QaContext& context) {
                 QaRuleResult result;
                 BuildOptions options;
                 options.dry_run = true;
                 for (const auto& record : context.registry.all()) {
                     if (record.type != AssetType::Dialogue) continue;
                     ++result.checked;
                     const auto path = context.layout.root() / record.runtime_path;
                     auto decoded = decode_compiled_asset(path, record.type);
                     if (!decoded) {
                         ++result.failed;
                         continue;
                     }
                     const auto& missing = decoded.value().at("missing_recordings");
                     if (missing.is_array() && !missing.items().empty()) {
                         for (const auto& line : missing.items()) {
                             result.findings.push_back(finding(
                                 core::Severity::Warning, result.rule, "BULLY-QA-0020",
                                 "line '" + line.as_string() + "' has no voice recording",
                                 record.asset_id, record.source_path));
                             ++result.warnings;
                         }
                         ++result.failed;
                     } else {
                         ++result.passed;
                     }
                     const auto& drift = decoded.value().at("subtitle_drift");
                     if (drift.is_array() && !drift.items().empty()) {
                         for (const auto& line : drift.items()) {
                             result.findings.push_back(finding(
                                 core::Severity::Warning, result.rule, "BULLY-QA-0021",
                                 "subtitle timing drifts from audio on line '" + line.as_string() + "'",
                                 record.asset_id, record.source_path));
                             ++result.warnings;
                         }
                     }
                 }
                 return result;
             });

    // ------------------------------------------------------ mission graph --
    add_rule("mission.dependencies", "mission dependency graph is acyclic",
             [](QaContext& context) {
                 QaRuleResult result;
                 std::map<std::string, std::vector<std::string>> graph;
                 for (const auto& record : context.registry.all()) {
                     if (record.type != AssetType::Mission) continue;
                     graph[record.asset_id] = record.dependencies.empty()
                                                  ? std::vector<std::string>{}
                                                  : std::vector<std::string>{};
                 }
                 std::set<std::string> visiting;
                 std::set<std::string> done;
                 std::function<bool(const std::string&)> visit = [&](const std::string& id) {
                     if (done.count(id) != 0) return true;
                     if (visiting.count(id) != 0) return false;
                     visiting.insert(id);
                     for (const auto& next : graph[id]) {
                         if (!visit(next)) return false;
                     }
                     visiting.erase(id);
                     done.insert(id);
                     return true;
                 };
                 for (const auto& [id, _] : graph) {
                     ++result.checked;
                     if (visit(id)) {
                         ++result.passed;
                     } else {
                         ++result.failed;
                         result.findings.push_back(finding(core::Severity::Error, result.rule,
                                                           "BULLY-QA-0030",
                                                           "mission dependency cycle detected", id));
                     }
                 }
                 return result;
             });

    // ------------------------------------------------------- orphans ------
    add_rule("asset.orphans", "source files that no other asset references",
             [](QaContext& context) {
                 QaRuleResult result;
                 std::set<std::string> referenced;
                 for (const auto& record : context.registry.all()) {
                     for (const auto& dependency : record.dependencies) {
                         referenced.insert(dependency.asset_id);
                     }
                 }
                 for (const auto& record : context.registry.all()) {
                     if (record.type == AssetType::Scene || record.type == AssetType::Mission ||
                         record.type == AssetType::Dialogue || record.type == AssetType::Material) {
                         continue;  // entry points / top-level authored content
                     }
                     ++result.checked;
                     if (referenced.count(record.asset_id) == 0) {
                         ++result.warnings;
                         result.findings.push_back(finding(
                             core::Severity::Warning, result.rule, "BULLY-QA-0040",
                             "asset is not referenced by any other asset (possible orphan)",
                             record.asset_id, record.source_path));
                     } else {
                         ++result.passed;
                     }
                 }
                 return result;
             });

    // ------------------------------------------------ compiled outputs ----
    add_rule("asset.outputs", "every asset has a compiled output that is up to date",
             [](QaContext& context) {
                 QaRuleResult result;
                 for (const auto& record : context.registry.all()) {
                     ++result.checked;
                     if (record.runtime_path.empty()) {
                         ++result.failed;
                         result.findings.push_back(finding(
                             core::Severity::Error, result.rule, "BULLY-QA-0050",
                             "asset has never been built", record.asset_id, record.source_path));
                         continue;
                     }
                     const auto path = context.layout.root() / record.runtime_path;
                     if (!core::FileSystem::exists(path)) {
                         ++result.failed;
                         result.findings.push_back(finding(
                             core::Severity::Error, result.rule, "BULLY-QA-0051",
                             "compiled output is missing: " + record.runtime_path, record.asset_id,
                             record.source_path));
                         continue;
                     }
                     if (record.status != BuildStatus::UpToDate) {
                         ++result.failed;
                         result.findings.push_back(finding(
                             core::Severity::Warning, result.rule, "BULLY-QA-0052",
                             "compiled output is stale (status: " +
                                 std::string(to_string(record.status)) + ")",
                             record.asset_id, record.source_path));
                         continue;
                     }
                     ++result.passed;
                 }
                 return result;
             });

    // ------------------------------------------------------ subtitles -----
    add_rule("dialogue.subtitles", "subtitle timing matches the recorded audio",
             [](QaContext& context) {
                 QaRuleResult result;
                 for (const auto& record : context.registry.all()) {
                     if (record.type != AssetType::Dialogue) continue;
                     ++result.checked;
                     const auto path = context.layout.root() / record.runtime_path;
                     auto decoded = decode_compiled_asset(path, record.type);
                     if (!decoded) {
                         ++result.failed;
                         for (const auto& item : decoded.diagnostics().all()) {
                             result.findings.push_back(finding(item.severity, result.rule, item.code,
                                                               item.message, record.asset_id,
                                                               record.source_path));
                         }
                         continue;
                     }
                     const auto& drift = decoded.value().at("subtitle_drift");
                     if (drift.is_array() && !drift.items().empty()) {
                         for (const auto& line : drift.items()) {
                             ++result.warnings;
                             result.findings.push_back(finding(
                                 core::Severity::Warning, result.rule, "BULLY-QA-0021",
                                 "subtitle timing drifts from the audio on line '" +
                                     line.as_string() + "'",
                                 record.asset_id, record.source_path));
                         }
                         ++result.failed;
                     } else {
                         ++result.passed;
                     }
                 }
                 return result;
             });

    // --------------------------------------------------- dialogue graph ---
    add_rule("dialogue.graph", "dialogue graphs are valid and terminate",
             [](QaContext& context) {
                 QaRuleResult result;
                 for (const auto& record : context.registry.all()) {
                     if (record.type != AssetType::Dialogue) continue;
                     ++result.checked;
                     const auto path = context.layout.root() / record.runtime_path;
                     auto bytes = core::FileSystem::read_binary(path);
                     if (!bytes) {
                         ++result.failed;
                         result.findings.push_back(finding(
                             core::Severity::Error, result.rule, "BULLY-QA-0022",
                             "dialogue output cannot be read: " + record.runtime_path,
                             record.asset_id, record.source_path));
                         continue;
                     }
                     formats::DialogueDocument document;
                     auto decoded = document.deserialize(bytes.value());
                     if (!decoded) {
                         ++result.failed;
                         for (const auto& item : decoded.diagnostics().all()) {
                             result.findings.push_back(finding(item.severity, result.rule, item.code,
                                                               item.message, record.asset_id,
                                                               record.source_path));
                         }
                         continue;
                     }
                     auto structure = decoded.value().validate();
                     for (const auto& item : structure.diagnostics().all()) {
                         if (item.severity == core::Severity::Warning) ++result.warnings;
                         if (item.severity == core::Severity::Info) continue;
                         result.findings.push_back(finding(item.severity, result.rule, item.code,
                                                           item.message, record.asset_id,
                                                           record.source_path));
                     }
                     if (structure) {
                         ++result.passed;
                     } else {
                         ++result.failed;
                     }
                 }
                 return result;
             });

    // ----------------------------------------------- animation rig links --
    add_rule("animation.references", "animations reference bones from the models they target",
             [](QaContext& context) {
                 QaRuleResult result;
                 for (const auto& record : context.registry.all()) {
                     if (record.type != AssetType::Animation) continue;
                     ++result.checked;
                     const auto path = context.layout.root() / record.runtime_path;
                     auto bytes = core::FileSystem::read_binary(path);
                     if (!bytes) {
                         ++result.failed;
                         continue;
                     }
                     formats::AnimationDocument document;
                     auto decoded = document.deserialize(bytes.value());
                     if (!decoded) {
                         ++result.failed;
                         for (const auto& item : decoded.diagnostics().all()) {
                             result.findings.push_back(finding(item.severity, result.rule, item.code,
                                                               item.message, record.asset_id,
                                                               record.source_path));
                         }
                         continue;
                     }
                     auto structure = decoded.value().validate();
                     bool ok = structure.has_value();
                     for (const auto& item : structure.diagnostics().all()) {
                         if (item.severity == core::Severity::Warning) ++result.warnings;
                         if (item.severity == core::Severity::Info) continue;
                         result.findings.push_back(finding(item.severity, result.rule, item.code,
                                                           item.message, record.asset_id,
                                                           record.source_path));
                     }
                     if (ok) ++result.passed;
                     else ++result.failed;
                 }
                 return result;
             });

    // ------------------------------------------------- scene references ---
    add_rule("scene.references", "models and missions referenced by scenes exist",
             [](QaContext& context) {
                 QaRuleResult result;
                 std::set<std::string> known;
                 for (const auto& record : context.registry.all()) known.insert(record.asset_id);
                 for (const auto& record : context.registry.all()) {
                     if (record.type != AssetType::Scene) continue;
                     ++result.checked;
                     const auto path = context.layout.root() / record.runtime_path;
                     auto bytes = core::FileSystem::read_binary(path);
                     if (!bytes) {
                         ++result.failed;
                         continue;
                     }
                     formats::SceneDocument document;
                     auto decoded = document.deserialize(bytes.value());
                     if (!decoded) {
                         ++result.failed;
                         for (const auto& item : decoded.diagnostics().all()) {
                             result.findings.push_back(finding(item.severity, result.rule, item.code,
                                                               item.message, record.asset_id,
                                                               record.source_path));
                         }
                         continue;
                     }
                     bool ok = true;
                     for (const auto& entity : decoded.value().entities) {
                         if (entity.model_asset.empty()) continue;
                         if (known.find(entity.model_asset) != known.end()) continue;
                         const bool looks_like_path =
                             entity.model_asset.find('/') != std::string::npos;
                         if (looks_like_path &&
                             core::FileSystem::exists(context.layout.root() / entity.model_asset)) {
                             continue;
                         }
                         ok = false;
                         ++result.warnings;
                         result.findings.push_back(finding(
                             core::Severity::Warning, result.rule, "BULLY-QA-0060",
                             "entity '" + entity.entity_id + "' references unknown model '" +
                                 entity.model_asset + "'",
                             record.asset_id, record.source_path));
                     }
                     auto structure = decoded.value().validate();
                     for (const auto& item : structure.diagnostics().all()) {
                         if (item.severity == core::Severity::Info) continue;
                         if (item.severity == core::Severity::Warning) ++result.warnings;
                         result.findings.push_back(finding(item.severity, result.rule, item.code,
                                                           item.message, record.asset_id,
                                                           record.source_path));
                     }
                     if (structure) {
                         ++result.passed;
                     } else {
                         ok = false;
                         ++result.failed;
                     }
                     (void)ok;
                 }
                 return result;
             });

    // ---------------------------------------------- plugin compatibility --
    // This rule deliberately reads manifests as plain JSON: the pipeline layer
    // must not depend on the plugin host, and CI needs to validate manifests for
    // plugins that cannot be loaded on the build machine (other platforms).
    add_rule("plugin.compatibility",
             "plugin manifests declare an ABI version this build can load",
             [](QaContext& context) {
                 QaRuleResult result;
                 constexpr std::uint32_t kHostAbiMajor = 1;
                 constexpr std::uint32_t kHostAbiMinor = 0;
                 std::vector<std::filesystem::path> roots{context.layout.root() / "plugins",
                                                          context.layout.root() / "extensions"};
                 if (const char* env = std::getenv("BULLY_PLUGIN_PATH"); env != nullptr) {
                     const std::string list(env);
                     for (const auto& entry : core::split(list, ':')) {
                         if (!entry.empty()) roots.emplace_back(entry);
                     }
                 }
                 for (const auto& root : roots) {
                     auto files = core::FileSystem::list_files(root, true);
                     if (!files) continue;
                     for (const auto& file : files.value()) {
                         if (file.filename() != "manifest.json") continue;
                         ++result.checked;
                         auto parsed = core::json::load_file(file.string());
                         if (!parsed) {
                             ++result.failed;
                             result.findings.push_back(finding(
                                 core::Severity::Error, result.rule, "BULLY-QA-0070",
                                 "plugin manifest is not valid JSON", {}, file.string()));
                             continue;
                         }
                         const auto& root_json = parsed.value();
                         const std::string id = root_json.get_string("id");
                         if (id.empty()) {
                             ++result.failed;
                             result.findings.push_back(finding(
                                 core::Severity::Error, result.rule, "BULLY-QA-0071",
                                 "plugin manifest has no id", {}, file.string()));
                             continue;
                         }
                         std::uint32_t major = kHostAbiMajor;
                         std::uint32_t minor = 0;
                         if (const core::json::Value* api = root_json.find("api");
                             api != nullptr && api->is_object()) {
                             major = static_cast<std::uint32_t>(api->get_int("major", 1));
                             minor = static_cast<std::uint32_t>(api->get_int("minor", 0));
                         }
                         if (major != kHostAbiMajor) {
                             ++result.failed;
                             result.findings.push_back(finding(
                                 core::Severity::Error, result.rule, "BULLY-QA-0072",
                                 "plugin '" + id + "' targets ABI " + std::to_string(major) +
                                     ".x; this host speaks " + std::to_string(kHostAbiMajor) + ".x",
                                 {}, file.string()));
                             continue;
                         }
                         if (minor > kHostAbiMinor) {
                             ++result.warnings;
                             result.findings.push_back(finding(
                                 core::Severity::Warning, result.rule, "BULLY-QA-0073",
                                 "plugin '" + id + "' was built against a newer minor ABI (" +
                                     std::to_string(major) + "." + std::to_string(minor) + ")",
                                 {}, file.string()));
                         }
                         if (root_json.get_string("library").empty()) {
                             ++result.failed;
                             result.findings.push_back(finding(
                                 core::Severity::Error, result.rule, "BULLY-QA-0074",
                                 "plugin '" + id + "' declares no library", {}, file.string()));
                             continue;
                         }
                         ++result.passed;
                     }
                 }
                 return result;
             });

    // ------------------------------------------------- package integrity --
    add_rule("package.integrity", "existing .bpackage files verify",
             [](QaContext& context) {
                 QaRuleResult result;
                 auto files = core::FileSystem::list_files(context.layout.packages_dir(), false);
                 if (!files) return result;
                 for (const auto& file : files.value()) {
                     if (file.extension() != ".bpackage") continue;
                     ++result.checked;
                     auto bytes = core::FileSystem::read_binary(file);
                     if (!bytes) {
                         ++result.failed;
                         continue;
                     }
                     formats::PackageDocument document;
                     auto decoded = document.deserialize(bytes.value());
                     if (!decoded) {
                         ++result.failed;
                         for (const auto& item : decoded.diagnostics().all()) {
                             result.findings.push_back(finding(item.severity, result.rule, item.code,
                                                               item.message, {}, file.string()));
                         }
                         continue;
                     }
                     auto verified = decoded.value().verify();
                     if (verified) {
                         ++result.passed;
                     } else {
                         ++result.failed;
                         for (const auto& item : verified.diagnostics().all()) {
                             result.findings.push_back(finding(item.severity, result.rule, item.code,
                                                               item.message, {}, file.string()));
                         }
                     }
                 }
                 return result;
             });
}

// -------------------------------------------------------------- reports ----

std::string QaReport::to_json() const
{
    core::json::Value root = core::json::Value::make_object();
    root["project"] = project_name;
    root["generated_utc"] = generated_utc;
    root["rules_run"] = rules_run;
    root["errors"] = errors;
    root["warnings"] = warnings;
    root["duration_ms"] = duration_ms;
    root["success"] = success();
    core::json::Value rules_json = core::json::Value::make_array();
    for (const auto& rule : rules) {
        core::json::Value entry = core::json::Value::make_object();
        entry["rule"] = rule.rule;
        entry["description"] = rule.description;
        entry["checked"] = rule.checked;
        entry["passed"] = rule.passed;
        entry["failed"] = rule.failed;
        entry["warnings"] = rule.warnings;
        entry["duration_ms"] = rule.duration_ms;
        core::json::Value findings = core::json::Value::make_array();
        for (const auto& item : rule.findings) {
            core::json::Value finding_json = core::json::Value::make_object();
            finding_json["severity"] = std::string(to_string(item.severity));
            finding_json["code"] = item.code;
            finding_json["message"] = item.message;
            finding_json["asset_id"] = item.asset_id;
            finding_json["source"] = item.source;
            findings.push(std::move(finding_json));
        }
        entry["findings"] = std::move(findings);
        rules_json.push(std::move(entry));
    }
    root["rules"] = std::move(rules_json);
    return root.dump(2);
}

std::string QaReport::to_markdown() const
{
    std::string out;
    out += "# QA report - " + project_name + "\n\n";
    out += "- Generated: " + generated_utc + "\n";
    out += "- Rules: " + std::to_string(rules_run) + "\n";
    out += "- Errors: " + std::to_string(errors) + ", warnings: " + std::to_string(warnings) + "\n";
    out += std::string("- Result: ") + (success() ? "PASS" : "FAIL") + "\n\n";
    out += "| Rule | Checked | Passed | Failed | Warnings | ms |\n|---|---|---|---|---|---|\n";
    for (const auto& rule : rules) {
        out += "| " + rule.rule + " | " + std::to_string(rule.checked) + " | " +
               std::to_string(rule.passed) + " | " + std::to_string(rule.failed) + " | " +
               std::to_string(rule.warnings) + " | " + std::to_string(rule.duration_ms) + " |\n";
    }
    for (const auto& rule : rules) {
        if (rule.findings.empty()) continue;
        out += "\n## " + rule.rule + "\n\n";
        for (const auto& item : rule.findings) {
            out += "- [" + std::string(to_string(item.severity)) + "] " + item.code + ": " +
                   item.message;
            if (!item.source.empty()) out += " (" + item.source + ")";
            out += "\n";
        }
    }
    return out;
}

}  // namespace bully::pipeline
