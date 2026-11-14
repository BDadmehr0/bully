// End-to-end headless test: drives the compiled bully_asset_cli over a
// scaffolded project the same way CI would, and verifies that the QA dashboard,
// the build report and packaging all work without the Qt studio.
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include <array>
#include <utility>
#include <cstdio>
#include <string>

#include "bully/formats/model.h"
#include "bully/pipeline/project_layout.h"
#include "bully/templates/project_templates.h"
#include "test_support.h"

#ifndef BULLY_CLI_PATH
#error "BULLY_CLI_PATH must be provided by the test build"
#endif

using namespace bully;

namespace {

struct CommandResult {
    int exit_code = -1;
    std::string output;
};

/// Runs any built executable (CLI, game host) with `arguments`.
CommandResult run_tool(const char* tool, const std::string& arguments,
                       const std::filesystem::path& cwd)
{
    std::string command = std::string("\"") + tool + "\" " + arguments + " 2>&1";
    const std::string changed = "cd \"" + cwd.string() + "\" && " + command;
    CommandResult result;
    FILE* pipe = popen(changed.c_str(), "r");
    REQUIRE(pipe != nullptr);
    std::array<char, 512> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }
    const int status = pclose(pipe);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

CommandResult run(const std::string& arguments, const std::filesystem::path& cwd)
{
    std::string command = std::string("\"") + BULLY_CLI_PATH + "\" " + arguments +
                          " 2>&1";
    const std::string changed = "cd \"" + cwd.string() + "\" && " + command;
    CommandResult result;
    FILE* pipe = popen(changed.c_str(), "r");
    REQUIRE(pipe != nullptr);
    std::array<char, 512> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }
    const int status = pclose(pipe);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

}  // namespace

TEST_CASE("CLI scaffolds, builds, verifies and packages a project headlessly",
          "[headless]")
{
    test::TempDir dir;
    const auto project = dir / "demo";

    const auto created = run("new \"" + project.string() + "\" --template retro-action --name \"Demo\"",
                             dir.path());
    INFO(created.output);
    REQUIRE(created.exit_code == 0);
    CHECK(created.output.find("Created project") != std::string::npos);
    CHECK(core::FileSystem::exists(project / "project.bullyproj"));

    const auto built = run("build", project);
    INFO(built.output);
    CHECK(built.exit_code == 0);
    CHECK(built.output.find("Build succeeded") != std::string::npos);
    CHECK(core::FileSystem::exists(project / "built/models/crate.bmodel"));
    CHECK(core::FileSystem::exists(project / "reports/build-report.md"));

    const auto rebuilt = run("build", project);
    INFO(rebuilt.output);
    CHECK(rebuilt.exit_code == 0);
    CHECK(rebuilt.output.find("cached") != std::string::npos);

    const auto verified = run("verify built/models/crate.bmodel", project);
    INFO(verified.output);
    CHECK(verified.exit_code == 0);
    CHECK(verified.output.find("OK") != std::string::npos);

    const auto info = run("info assets/models/crate.obj", project);
    INFO(info.output);
    CHECK(info.exit_code == 0);
    CHECK(info.output.find("\"format\": \"bmodel\"") != std::string::npos);

    const auto qa = run("qa", project);
    INFO(qa.output);
    // The template ships an unrecorded dialogue line, so the QA dashboard is
    // expected to report it; the command must still render a full report.
    CHECK(qa.output.find("QA report") != std::string::npos);
    CHECK(core::FileSystem::exists(project / "reports/qa-report.json"));

    const auto packaged = run("package --name Demo", project);
    INFO(packaged.output);
    CHECK(packaged.exit_code == 0);
    CHECK(core::FileSystem::exists(project / "packages/Demo.bpackage"));
    CHECK(core::FileSystem::exists(project / "packages/Demo.manifest.json"));
}

TEST_CASE("CLI lists formats, importers and templates", "[headless]")
{
    test::TempDir dir;
    for (const char* command : {"formats", "list", "templates", "help"}) {
        const auto result = run(command, dir.path());
        INFO(command << ": " << result.output);
        CHECK(result.exit_code == 0);
        CHECK_FALSE(result.output.empty());
    }
    const auto formats = run("formats", dir.path());
    for (const char* extension : {".bmodel", ".btexture", ".bmaterial", ".banim", ".baudio",
                                  ".bdialogue", ".bscene", ".bmission", ".bpackage"}) {
        CHECK(formats.output.find(extension) != std::string::npos);
    }
}

TEST_CASE("CLI fails clearly outside a project directory", "[headless]")
{
    test::TempDir dir;
    const auto result = run("build", dir.path());
    INFO(result.output);
    CHECK(result.exit_code != 0);
    CHECK(result.output.find("not a Bully project") != std::string::npos);
}

TEST_CASE("CLI verifies corrupted assets as invalid", "[headless]")
{
    test::TempDir dir;
    const auto project = dir / "corrupt";
    REQUIRE(run("new \"" + project.string() + "\"", dir.path()).exit_code == 0);
    REQUIRE(run("build", project).exit_code == 0);

    const auto path = project / "built/models/crate.bmodel";
    auto bytes = core::FileSystem::read_binary(path);
    REQUIRE(bytes.has_value());
    bytes.value().back() ^= 0xFF;
    REQUIRE(core::FileSystem::write_binary(path, bytes.value()).has_value());

    const auto verified = run("verify built/models/crate.bmodel", project);
    INFO(verified.output);
    CHECK(verified.exit_code != 0);
    CHECK(verified.output.find("INVALID") != std::string::npos);
}

TEST_CASE("CLI packaging run from the project root names files sensibly", "[headless]")
{
    // Regression: `bully_asset_cli package` executed *inside* a project (the
    // documented workflow) used to produce "packages/..bpackage" because the
    // project name defaulted to the directory name of the current directory,
    // which is ".". It also silently packaged zero assets.
    test::TempDir dir;
    const auto project = dir / "named-project";
    REQUIRE(run("new \"" + project.string() + "\" --name \"Bully Demo\"", dir.path()).exit_code == 0);

    // Packaging before building must fail, not write an empty archive.
    const auto premature = run("package", project);
    INFO(premature.output);
    CHECK(premature.exit_code == 1);
    CHECK(premature.output.find("BULLY-PKG-0010") != std::string::npos);
    for (const auto& entry : std::filesystem::directory_iterator(project / "packages")) {
        CHECK(entry.path().extension() != ".bpackage");
    }

    REQUIRE(run("build", project).exit_code == 0);
    const auto packaged = run("package", project);
    INFO(packaged.output);
    CHECK(packaged.exit_code == 0);
    CHECK(packaged.output.find("Packaged 0 assets") == std::string::npos);

    // The template ships a .gitkeep placeholder in packages/; only the real
    // artefacts are subject to the naming rule.
    for (const auto& entry : std::filesystem::directory_iterator(project / "packages")) {
        const auto name = entry.path().filename().string();
        if (name == ".gitkeep") continue;
        INFO(name);
        CHECK(name.rfind("..", 0) != 0);
        CHECK(name.find('.') != 0);
    }
    CHECK(std::filesystem::file_size(project / "packages/Bully-Demo.bpackage") > 0);
    // Reported paths are project-relative, never "./packages".
    CHECK(packaged.output.find("./") == std::string::npos);
}

TEST_CASE("CLI exports compiled assets back to editable source files", "[headless]")
{
    test::TempDir dir;
    const auto project = dir / "exports";
    REQUIRE(run("new \"" + project.string() + "\"", dir.path()).exit_code == 0);
    REQUIRE(run("build", project).exit_code == 0);

    const auto listed = run("exporters", project);
    INFO(listed.output);
    CHECK(listed.exit_code == 0);
    for (const char* extension : {".png", ".wav", ".obj", ".scene.json", ".mission.json",
                                  ".dialogue.json", ".banim.json", ".bmaterial.json"}) {
        CHECK(listed.output.find(extension) != std::string::npos);
    }

    // Relative destinations resolve inside the project root.
    for (const auto& [source, destination] :
         std::array<std::pair<const char*, const char*>, 4>{{
             {"built/models/crate.bmodel", "exported/crate.obj"},
             {"built/scenes/schoolyard.scene.bscene", "exported/schoolyard.scene.json"},
             {"built/missions/m_first_day.mission.bmission",
              "exported/m_first_day.mission.json"},
             {"built/dialogue/intro.dialogue.bdialogue",
              "exported/intro.dialogue.json"}}}) {
        const auto result = run(std::string("export ") + source + " " + destination, project);
        INFO(source << ": " << result.output);
        CHECK(result.exit_code == 0);
        CHECK(std::filesystem::exists(project / destination));
        CHECK(std::filesystem::file_size(project / destination) > 0);
    }

    // The exported source JSON must itself be re-importable, which is the whole
    // point of a lossless-ish round trip back into the authoring formats.
    const auto reimported = run("import exported/schoolyard.scene.json", project);
    INFO(reimported.output);
    CHECK(reimported.exit_code == 0);

    const auto missing = run("export built/models/nope.bmodel exported/nope.obj", project);
    INFO(missing.output);
    CHECK(missing.exit_code == 1);
}

#ifdef BULLY_GAME_PATH
TEST_CASE("Game host runs a built project headless and reports usage errors", "[headless]")
{
    test::TempDir dir;
    const auto project = dir / "playable";
    REQUIRE(run("new \"" + project.string() + "\" --template retro-action", dir.path()).exit_code == 0);
    REQUIRE(run("build", project).exit_code == 0);

    // --help is a request, not a usage error.
    const auto help = run_tool(BULLY_GAME_PATH, "--help", dir.path());
    INFO(help.output);
    CHECK(help.exit_code == 0);
    CHECK(help.output.find("--headless") != std::string::npos);

    // --diagnose works without a project and explains the video state.
    const auto diagnose = run_tool(BULLY_GAME_PATH, "--diagnose", dir.path());
    INFO(diagnose.output);
    CHECK((diagnose.exit_code == 0 || diagnose.exit_code == 1));
    CHECK(diagnose.output.find("video diagnostics") != std::string::npos);

    // Missing / wrong kind of argument is a usage error (exit 2), with a hint.
    const auto missing = run_tool(BULLY_GAME_PATH, "does-not-exist", dir.path());
    INFO(missing.output);
    CHECK(missing.exit_code == 2);
    CHECK(missing.output.find("not a project directory") != std::string::npos);

    // A real run: the same fixed-step loop the windowed host uses.
    const auto played = run_tool(BULLY_GAME_PATH,
                                 "\"" + project.string() + "\" --headless --frames 30",
                                 dir.path());
    INFO(played.output);
    CHECK(played.exit_code == 0);
    CHECK(played.output.find("fixed steps:       30") != std::string::npos);

    // A project directory is expected, but a manifest path is accepted too
    // (make run used to pass the .bullyproj file and fail).
    const auto via_manifest = run_tool(BULLY_GAME_PATH,
                                       "\"" + (project / "project.bullyproj").string() +
                                           "\" --headless --frames 5",
                                       dir.path());
    INFO(via_manifest.output);
    CHECK(via_manifest.exit_code == 0);

    // A project with no compiled assets must fail with an actionable message.
    const auto unbuilt = dir / "unbuilt";
    REQUIRE(run("new \"" + unbuilt.string() + "\"", dir.path()).exit_code == 0);
    const auto refused = run_tool(BULLY_GAME_PATH, "\"" + unbuilt.string() + "\" --headless",
                                  dir.path());
    INFO(refused.output);
    CHECK(refused.exit_code == 2);
    CHECK(refused.output.find("build the project first") != std::string::npos);
}
#endif

TEST_CASE("Compiled template assets decode with the public format API", "[headless]")
{
    test::TempDir dir;
    const auto project = dir / "decode";
    REQUIRE(templates::create_project(project, "retro-action", "Decode").has_value());
    REQUIRE(run("build", project).exit_code == 0);

    auto bytes = core::FileSystem::read_binary(project / "built/models/crate.bmodel");
    REQUIRE(bytes.has_value());
    formats::ModelDocument document;
    auto decoded = document.deserialize(bytes.value());
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().compute_statistics().triangle_count == 12);
    CHECK(decoded.value().profile.preset_name == "ps2");
}
