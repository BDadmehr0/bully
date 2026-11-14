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
