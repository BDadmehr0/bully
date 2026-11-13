// bully_asset_cli - headless asset pipeline, QA and packaging tool.
//
// This is the automation surface of Bully Studio: everything the desktop
// application can do to a project's assets is reachable here, so CI, build
// servers and AI agents never need the GUI.
//
// Commands:
//   new <dir> [--template NAME]   create a project from a template
//   list                          list registered importers and formats
//   import <file...>              register + build source assets
//   build [--force] [--dry-run]   build every pending asset
//   info <asset|file>             print the compiled asset summary (JSON)
//   qa [--rule NAME...]           run the QA dashboard headlessly
//   package [--name NAME]         produce a .bpackage + manifest
//   formats                       print the runtime format table
//   verify <file>                 validate a compiled asset file
//   plugins [dir]                 list plugins found in a directory
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "bully/core/fs.h"
#include "bully/core/log.h"
#include "bully/core/strings.h"
#include "bully/formats/registry.h"
#include "bully/pipeline/builtin_importers.h"
#include "bully/pipeline/packager.h"
#include "bully/pipeline/pipeline.h"
#include "bully/pipeline/project_layout.h"
#include "bully/pipeline/qa.h"
#include "bully/plugins/host.h"
#include "bully/project/asset_registry.h"
#include "bully/project/database.h"
#include "bully/templates/project_templates.h"

namespace {

struct Arguments {
    std::string command;
    std::vector<std::string> positional;
    std::vector<std::string> flags;
    std::vector<std::pair<std::string, std::string>> options;

    [[nodiscard]] bool has_flag(std::string_view flag) const
    {
        return std::find(flags.begin(), flags.end(), flag) != flags.end();
    }
    [[nodiscard]] std::string option(std::string_view name, std::string fallback = {}) const
    {
        for (const auto& [key, value] : options) {
            if (key == name) return value;
        }
        return fallback;
    }
};

void print_usage()
{
    std::cout <<
        "bully_asset_cli " << bully::core::kPlatformVersion.to_string() << "\n"
        "Headless asset pipeline / QA / packaging for Bully Studio.\n\n"
        "Usage: bully_asset_cli <command> [options] [arguments]\n\n"
        "Commands:\n"
        "  new <dir> [--template NAME] [--name NAME]\n"
        "        Create a new project from a template.\n"
        "  list\n"
        "        List registered importers with their extensions.\n"
        "  formats\n"
        "        List the versioned runtime formats.\n"
        "  import <file...> [--project DIR]\n"
        "        Register source files as project assets and build them.\n"
        "  build [--force] [--dry-run] [--project DIR]\n"
        "        Build every pending asset.\n"
        "  info <asset-id|file> [--project DIR]\n"
        "        Print the compiled asset summary as JSON.\n"
        "  verify <file>\n"
        "        Decode and validate a compiled .b* file.\n"
        "  qa [--rule NAME] [--project DIR]\n"
        "        Run the QA dashboard and write reports/qa-report.{json,md}.\n"
        "  package [--name NAME] [--project DIR]\n"
        "        Build a .bpackage plus manifest in packages/.\n"
        "  plugins <dir>\n"
        "        Discover plugins in a directory and print their state.\n"
        "  templates\n"
        "        List the available project templates.\n\n"
        "Global options:\n"
        "  --project DIR   project root (defaults to the current directory)\n"
        "  --quiet         only print errors\n"
        "  --json          machine-readable output where supported\n";
}

bully::core::LogLevel log_level = bully::core::LogLevel::Warn;
bool json_output = false;

struct Session {
    std::unique_ptr<bully::project::Database> database;
    std::unique_ptr<bully::project::AssetRegistry> registry;
    std::unique_ptr<bully::pipeline::ImportCache> cache;
    std::unique_ptr<bully::pipeline::Pipeline> pipeline;
    bully::pipeline::ProjectLayout* layout = nullptr;
    std::unique_ptr<bully::pipeline::ProjectLayout> owned_layout;
};

bool open_project(const std::string& project_dir, Session& session, bool require_manifest = true)
{
    session.layout = nullptr;
    session.owned_layout = std::make_unique<bully::pipeline::ProjectLayout>(
        std::filesystem::path(project_dir).is_absolute()
            ? std::filesystem::path(project_dir)
            : std::filesystem::absolute(project_dir));
    session.layout = session.owned_layout.get();
    if (require_manifest && !bully::pipeline::ProjectLayout::is_project(session.layout->root())) {
        std::cerr << "error: '" << session.layout->root().string()
                  << "' is not a Bully project (no project.bullyproj).\n"
                  << "       Run 'bully_asset_cli new <dir>' first.\n";
        return false;
    }
    auto database = bully::project::Database::open(session.layout->database_path());
    if (!database) {
        std::cerr << database.diagnostics().format();
        return false;
    }
    session.database = std::make_unique<bully::project::Database>(std::move(database.value()));
    session.registry = std::make_unique<bully::project::AssetRegistry>(*session.database);
    auto status = session.registry->initialize_schema();
    if (!status) {
        std::cerr << status.diagnostics().format();
        return false;
    }
    session.cache = std::make_unique<bully::pipeline::ImportCache>(
        *session.database, session.layout->cache_dir());
    status = session.cache->initialize();
    if (!status) {
        std::cerr << status.diagnostics().format();
        return false;
    }
    auto& importers = bully::pipeline::ImporterRegistry::instance();
    importers.register_builtin();
    session.pipeline = std::make_unique<bully::pipeline::Pipeline>(
        *session.layout, *session.database, *session.registry, importers, *session.cache);
    (void)bully::core::FileSystem::create_directories(session.layout->built_dir());
    return true;
}

int command_new(const Arguments& arguments)
{
    if (arguments.positional.empty()) {
        std::cerr << "error: 'new' requires a target directory\n";
        return 2;
    }
    const std::string target = arguments.positional.front();
    const std::string template_name = arguments.option("template", "retro-action");
    auto result = bully::templates::create_project(target, template_name,
                                                  arguments.option("name", {}));
    if (!result) {
        std::cerr << result.diagnostics().format();
        return 1;
    }
    std::cout << "Created project '" << result.value().name << "' at " << target
              << " (template: " << template_name << ")\n";
    return 0;
}

int command_list()
{
    auto& importers = bully::pipeline::ImporterRegistry::instance();
    importers.register_builtin();
    std::cout << "Registered importers (" << importers.all().size() << "):\n";
    for (const auto& importer : importers.all()) {
        std::cout << "  " << importer->id() << "  v" << importer->version() << "  ["
                  << bully::core::join(importer->extensions(), ", ") << "]  - "
                  << importer->display_name() << "\n";
    }
    std::cout << "\nExtensions handled: "
              << bully::core::join(importers.all_extensions(), ", ") << "\n";
    return 0;
}

int command_formats()
{
    std::cout << bully::formats::describe_formats();
    return 0;
}

int command_import(const Arguments& arguments)
{
    Session session;
    if (!open_project(arguments.option("project", "."), session)) return 1;
    if (arguments.positional.empty()) {
        std::cerr << "error: 'import' requires at least one file\n";
        return 2;
    }
    std::vector<std::string> ids;
    for (const auto& path_text : arguments.positional) {
        const auto path = std::filesystem::absolute(path_text);
        if (!bully::core::FileSystem::exists(path)) {
            std::cerr << "error: file not found: " << path_text << "\n";
            return 1;
        }
        const auto relative = bully::core::FileSystem::relative_to(path, session.layout->root());
        session.layout->source_dir();
        // Copy/link into assets/ when the file lives outside the project.
        std::string final_relative = relative;
        if (!bully::core::FileSystem::is_within(path, session.layout->source_dir()) &&
            !bully::core::FileSystem::is_within(path, session.layout->content_dir())) {
            final_relative =
                "assets/imported/" + path.filename().string();
            auto status = bully::core::FileSystem::copy_file(
                path, session.layout->root() / final_relative, false);
            if (!status) {
                std::cerr << status.diagnostics().format();
                return 1;
            }
            std::cout << "copied " << path_text << " -> " << final_relative
                      << " (sources are never modified)\n";
        } else {
            final_relative = bully::core::FileSystem::relative_to(path, session.layout->root());
        }
        const std::string asset_id = bully::pipeline::deterministic_asset_id(final_relative);
        ids.push_back(asset_id);
    }
    auto scan = session.pipeline->scan();
    for (const auto& item : scan.diagnostics.all()) {
        if (item.severity != bully::core::Severity::Info) std::cerr << item.format() << "\n";
    }
    bully::pipeline::BuildOptions options;
    options.detailed_reports = true;
    auto report = session.pipeline->build_assets(ids, options,
                                                 [](std::size_t index, std::size_t total,
                                                    const bully::pipeline::AssetBuildResult& result) {
                                                     std::cout << "[" << (index + 1) << "/" << total
                                                               << "] " << result.source_path << ": "
                                                               << (result.success ? "ok" : "FAILED")
                                                               << " - " << result.summary << "\n";
                                                 });
    for (const auto& result : report.results) {
        for (const auto& item : result.diagnostics.all()) {
            if (item.severity == bully::core::Severity::Warning ||
                item.severity == bully::core::Severity::Error) {
                std::cerr << item.format() << "\n";
            }
        }
    }
    (void)session.pipeline->write_report(report, "import-report");
    return report.success() ? 0 : 1;
}

int command_build(const Arguments& arguments)
{
    Session session;
    if (!open_project(arguments.option("project", "."), session)) return 1;
    auto scan = session.pipeline->scan();
    for (const auto& item : scan.diagnostics.all()) {
        std::cerr << item.format() << "\n";
    }
    bully::pipeline::BuildOptions options;
    options.force = arguments.has_flag("--force");
    options.dry_run = arguments.has_flag("--dry-run");
    auto report = session.pipeline->build_all(
        options, [](std::size_t index, std::size_t total,
                    const bully::pipeline::AssetBuildResult& result) {
            std::cout << "[" << (index + 1) << "/" << total << "] " << result.source_path << ": "
                      << (result.success ? (result.skipped ? "cached" : "built") : "FAILED") << " - "
                      << result.summary << "\n";
        });
    for (const auto& result : report.results) {
        for (const auto& item : result.diagnostics.all()) {
            if (item.severity == bully::core::Severity::Warning ||
                item.severity == bully::core::Severity::Error) {
                std::cerr << item.format() << "\n";
            }
        }
    }
    (void)session.pipeline->write_report(report);
    std::cout << "Build " << (report.success() ? "succeeded" : "FAILED") << ": " << report.built
              << " built, " << report.cached << " cached, " << report.failed << " failed ("
              << report.duration_ms << " ms)\n";
    std::cout << "Report: " << (session.layout->reports_dir() / "build-report.md").string() << "\n";
    return report.success() ? 0 : 1;
}

int command_info(const Arguments& arguments)
{
    if (arguments.positional.empty()) {
        std::cerr << "error: 'info' requires an asset id or file\n";
        return 2;
    }
    const std::string target = arguments.positional.front();
    std::filesystem::path path{target};
    // Resolution order: registry asset id -> registry source path -> a path to
    // a compiled file. Source files are never decoded directly.
    {
        Session session;
        if (open_project(arguments.option("project", "."), session)) {
            std::optional<bully::pipeline::AssetRecord> record = session.registry->find(target);
            if (!record) record = session.registry->find_by_source_path(target);
            if (!record && bully::core::FileSystem::exists(path)) {
                const auto relative =
                    bully::core::FileSystem::relative_to(path, session.layout->root());
                record = session.registry->find_by_source_path(relative);
            }
            if (record) {
                if (record->runtime_path.empty()) {
                    std::cerr << "error: asset '" << target
                              << "' has not been built yet; run 'build' first\n";
                    return 1;
                }
                path = session.layout->root() / record->runtime_path;
            }
        }
    }
    if (!bully::core::FileSystem::exists(path)) {
        std::cerr << "error: asset or file '" << target << "' not found\n";
        return 1;
    }
    auto bytes = bully::core::FileSystem::read_binary(path);
    if (!bytes) {
        std::cerr << bytes.diagnostics().format();
        return 1;
    }
    const auto* format = bully::formats::sniff_format(bytes.value());
    if (format == nullptr) {
        std::cerr << "error: unknown runtime format for " << path.string() << "\n";
        return 1;
    }
    bully::core::Version version;
    bully::formats::sniff_version(bytes.value(), version);
    std::cout << bully::formats::format_by_magic(format->magic)->description << " ("
              << format->extension << " v" << version.major << "." << version.minor << ")\n";
    if (format->magic == bully::formats::kModelFormat.magic) {
        bully::formats::ModelDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) {
            std::cerr << decoded.diagnostics().format();
            return 1;
        }
        std::cout << decoded.value().to_json() << "\n";
    } else if (format->magic == bully::formats::kTextureFormat.magic) {
        bully::formats::TextureDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) {
            std::cerr << decoded.diagnostics().format();
            return 1;
        }
        std::cout << decoded.value().to_json() << "\n";
    } else if (format->magic == bully::formats::kAudioFormat.magic) {
        bully::formats::AudioDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) {
            std::cerr << decoded.diagnostics().format();
            return 1;
        }
        std::cout << decoded.value().to_json() << "\n";
    } else if (format->magic == bully::formats::kDialogueFormat.magic) {
        bully::formats::DialogueDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) {
            std::cerr << decoded.diagnostics().format();
            return 1;
        }
        std::cout << decoded.value().to_json() << "\n";
    } else if (format->magic == bully::formats::kPackageFormat.magic) {
        bully::formats::PackageDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) {
            std::cerr << decoded.diagnostics().format();
            return 1;
        }
        std::cout << decoded.value().to_json() << "\n";
    } else {
        std::cout << "Decoded header only; use 'verify' for a full check.\n";
    }
    return 0;
}

int command_verify(const Arguments& arguments)
{
    if (arguments.positional.empty()) {
        std::cerr << "error: 'verify' requires a file\n";
        return 2;
    }
    auto bytes = bully::core::FileSystem::read_binary(arguments.positional.front());
    if (!bytes) {
        std::cerr << bytes.diagnostics().format();
        return 1;
    }
    const auto* format = bully::formats::sniff_format(bytes.value());
    if (format == nullptr) {
        std::cerr << "error: not a Bully runtime format\n";
        return 1;
    }
    bully::core::DiagnosticList diagnostics;
    bool ok = true;
    if (format->magic == bully::formats::kModelFormat.magic) {
        bully::formats::ModelDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) {
            diagnostics.append(decoded.diagnostics());
            ok = false;
        } else {
            auto validation = decoded.value().validate();
            diagnostics.append(validation.diagnostics());
            ok = !diagnostics.has_errors();
        }
    } else if (format->magic == bully::formats::kTextureFormat.magic) {
        bully::formats::TextureDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) {
            diagnostics.append(decoded.diagnostics());
            ok = false;
        } else {
            diagnostics.append(decoded.value().validate().diagnostics());
            ok = !diagnostics.has_errors();
        }
    } else if (format->magic == bully::formats::kAudioFormat.magic) {
        bully::formats::AudioDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) {
            diagnostics.append(decoded.diagnostics());
            ok = false;
        } else {
            diagnostics.append(decoded.value().validate().diagnostics());
            ok = !diagnostics.has_errors();
        }
    } else if (format->magic == bully::formats::kDialogueFormat.magic) {
        bully::formats::DialogueDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) {
            diagnostics.append(decoded.diagnostics());
            ok = false;
        } else {
            diagnostics.append(decoded.value().validate().diagnostics());
            ok = !diagnostics.has_errors();
        }
    } else if (format->magic == bully::formats::kSceneFormat.magic) {
        bully::formats::SceneDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) {
            diagnostics.append(decoded.diagnostics());
            ok = false;
        } else {
            diagnostics.append(decoded.value().validate().diagnostics());
            ok = !diagnostics.has_errors();
        }
    } else if (format->magic == bully::formats::kMissionFormat.magic) {
        bully::formats::MissionDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) {
            diagnostics.append(decoded.diagnostics());
            ok = false;
        } else {
            diagnostics.append(decoded.value().validate().diagnostics());
            ok = !diagnostics.has_errors();
        }
    } else if (format->magic == bully::formats::kPackageFormat.magic) {
        bully::formats::PackageDocument document;
        auto decoded = document.deserialize(bytes.value());
        if (!decoded) {
            diagnostics.append(decoded.diagnostics());
            ok = false;
        } else {
            diagnostics.append(decoded.value().validate().diagnostics());
            ok = !diagnostics.has_errors();
        }
    } else {
        bully::core::Version version;
        bully::formats::sniff_version(bytes.value(), version);
        std::cout << "decoded header for " << format->extension << " v" << version.major << "."
                  << version.minor << " (no deep validator for this format yet)\n";
    }
    if (!diagnostics.empty()) std::cout << diagnostics.format();
    std::cout << (ok ? "OK" : "INVALID") << ": " << arguments.positional.front() << "\n";
    return ok ? 0 : 1;
}

int command_qa(const Arguments& arguments)
{
    Session session;
    if (!open_project(arguments.option("project", "."), session)) return 1;
    auto& importers = bully::pipeline::ImporterRegistry::instance();
    bully::pipeline::QaRunner runner(*session.layout, *session.registry, importers,
                                     *session.pipeline);
    runner.register_builtin_rules();
    std::vector<std::string> only;
    for (const auto& [key, value] : arguments.options) {
        if (key == "rule") only.push_back(value);
    }
    auto report = runner.run(only);
    std::cout << report.to_markdown();
    (void)bully::core::FileSystem::create_directories(session.layout->reports_dir());
    (void)bully::core::FileSystem::write_text(session.layout->reports_dir() / "qa-report.json",
                                              report.to_json() + "\n");
    (void)bully::core::FileSystem::write_text(session.layout->reports_dir() / "qa-report.md",
                                              report.to_markdown());
    std::cout << "\nReport written to "
              << (session.layout->reports_dir() / "qa-report.md").string() << "\n";
    return report.success() ? 0 : 1;
}

int command_package(const Arguments& arguments)
{
    Session session;
    if (!open_project(arguments.option("project", "."), session)) return 1;
    bully::pipeline::Packager packager(*session.layout, *session.registry);
    bully::pipeline::PackageOptions options;
    options.package_name = arguments.option("name", session.layout->root().filename().string());
    options.output_name = arguments.option("output", options.package_name);
    auto result = packager.build_package(options);
    for (const auto& item : result.diagnostics.all()) std::cerr << item.format() << "\n";
    if (!result.success) return 1;
    std::cout << "Packaged " << result.entry_count << " assets (" << result.total_bytes
              << " bytes)\n  package: " << result.package_path << "\n  manifest: "
              << result.manifest_path << "\n";
    return 0;
}

int command_plugins(const Arguments& arguments)
{
    if (arguments.positional.empty()) {
        std::cerr << "error: 'plugins' requires a directory\n";
        return 2;
    }
    bully::plugins::PluginHost host;
    bully::plugins::PluginHost::Options options;
    options.plugin_directories.push_back(arguments.positional.front());
    host.set_options(options);
    host.discover();
    const auto plugins = host.plugins();
    if (plugins.empty()) {
        std::cout << "no plugins found in " << arguments.positional.front() << "\n";
        return 0;
    }
    for (const auto& plugin : plugins) {
        std::cout << plugin.manifest.id << " v" << plugin.manifest.version << " ["
                  << bully::plugins::to_string(plugin.state) << "] api "
                  << plugin.manifest.api_major << "." << plugin.manifest.api_minor << " "
                  << plugin.manifest.name << "\n";
        if (!plugin.error.empty()) std::cout << "  error: " << plugin.error << "\n";
    }
    return 0;
}

int command_templates()
{
    std::cout << "Available project templates:\n";
    for (const auto& info : bully::templates::template_catalog()) {
        std::cout << "  " << info.name << "  - " << info.description << "\n";
    }
    return 0;
}

Arguments parse(int argc, char** argv)
{
    Arguments arguments;
    for (int i = 1; i < argc; ++i) {
        const std::string token = argv[i];
        if (token.rfind("--", 0) == 0) {
            if (token == "--quiet") {
                log_level = bully::core::LogLevel::Error;
                arguments.flags.push_back(token);
            } else if (token == "--json") {
                json_output = true;
                arguments.flags.push_back(token);
            } else if (token == "--force" || token == "--dry-run" || token == "--verbose") {
                if (token == "--verbose") log_level = bully::core::LogLevel::Debug;
                arguments.flags.push_back(token);
            } else if (i + 1 < argc) {
                arguments.options.emplace_back(token.substr(2), argv[++i]);
            } else {
                arguments.flags.push_back(token);
            }
        } else if (arguments.command.empty()) {
            arguments.command = token;
        } else {
            arguments.positional.push_back(token);
        }
    }
    return arguments;
}

}  // namespace

int main(int argc, char** argv)
{
    const Arguments arguments = parse(argc, argv);
    bully::core::Logger::instance().set_min_level(log_level);
    if (arguments.has_flag("--verbose")) {
        (void)bully::core::Logger::instance().add_sink(bully::core::Logger::stderr_sink());
    }
    if (arguments.command.empty() || arguments.command == "help" ||
        arguments.command == "--help") {
        print_usage();
        return arguments.command.empty() ? 2 : 0;
    }
    const auto& command = arguments.command;
    if (command == "new") return command_new(arguments);
    if (command == "list") return command_list();
    if (command == "formats") return command_formats();
    if (command == "import") return command_import(arguments);
    if (command == "build") return command_build(arguments);
    if (command == "info") return command_info(arguments);
    if (command == "verify") return command_verify(arguments);
    if (command == "qa") return command_qa(arguments);
    if (command == "package") return command_package(arguments);
    if (command == "plugins") return command_plugins(arguments);
    if (command == "templates") return command_templates();
    std::cerr << "error: unknown command '" << command << "'\n\n";
    print_usage();
    return 2;
}
