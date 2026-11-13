#include <catch2/catch_test_macros.hpp>

#include "bully/pipeline/qa.h"
#include "fixture.h"
#include "test_support.h"

using namespace bully;
using namespace bully::pipeline;

namespace {

struct QaFixture {
    explicit QaFixture(std::string_view name) : fixture(name)
    {
        runner = std::make_unique<QaRunner>(fixture.layout(), fixture.registry(),
                                            fixture.importers(), fixture.pipeline());
        runner->register_builtin_rules();
    }

    test::ProjectFixture fixture;
    std::unique_ptr<QaRunner> runner;
};

}  // namespace

TEST_CASE("QA runner registers the documented built-in rules", "[pipeline][qa]")
{
    QaFixture qa("qa-rules");
    const auto names = qa.runner->rule_names();
    CHECK(names.size() >= 12);
    for (const char* expected :
         {"asset.import", "asset.references", "asset.orphans", "dialogue.recordings",
          "dialogue.subtitles", "dialogue.graph", "animation.references", "scene.references",
          "mission.dependencies", "plugin.compatibility", "runtime.load", "package.integrity"}) {
        CHECK(std::find(names.begin(), names.end(), expected) != names.end());
    }
}

TEST_CASE("QA passes on a clean project", "[pipeline][qa]")
{
    QaFixture qa("qa-clean");
    qa.fixture.write_asset("models/a.obj", test::cube_obj());
    REQUIRE(qa.fixture.build_all().success());
    const auto report = qa.runner->run();
    CHECK(report.rules_run >= 12);
    CHECK(report.success());
    CHECK(report.errors == 0);
    const std::string json = report.to_json();
    CHECK(json.find("\"success\": true") != std::string::npos);
    CHECK(report.to_markdown().find("QA report") != std::string::npos);
}

TEST_CASE("QA reports missing voice recordings", "[pipeline][qa]")
{
    QaFixture qa("qa-dialogue");
    qa.fixture.write_content("dialogue/intro.dialogue.json", test::dialogue_source_json());
    REQUIRE(qa.fixture.build_all().success());
    const auto report = qa.runner->run({"dialogue.recordings"});
    REQUIRE(report.rules.size() == 1);
    const auto& rule = report.rules[0];
    CHECK(rule.rule == "dialogue.recordings");
    CHECK(rule.checked >= 1);
    CHECK(rule.findings.size() >= 1);
    CHECK_FALSE(rule.findings[0].message.empty());
}

TEST_CASE("QA reports uncompiled assets and missing outputs", "[pipeline][qa]")
{
    QaFixture qa("qa-unbuilt");
    qa.fixture.write_asset("models/a.obj", test::cube_obj());
    REQUIRE(qa.fixture.pipeline().scan().discovered.size() == 1);
    const auto outputs = qa.runner->run({"asset.outputs"});
    REQUIRE(outputs.rules.size() == 1);
    CHECK(outputs.rules[0].failed >= 1);
    CHECK_FALSE(outputs.success());
    CHECK_FALSE(outputs.rules[0].findings.empty());
}

TEST_CASE("QA can run a single named rule and unknown rules are empty", "[pipeline][qa]")
{
    QaFixture qa("qa-single");
    const auto single = qa.runner->run({"asset.orphans"});
    CHECK(single.rules_run == 1);
    const auto unknown = qa.runner->run({"no.such.rule"});
    CHECK(unknown.rules_run == 0);
    CHECK(unknown.success());
}

TEST_CASE("QA accepts plugin-contributed rules", "[pipeline][qa]")
{
    QaFixture qa("qa-plugin");
    qa.runner->add_rule("custom.rule", "A rule registered by a plugin", [](QaContext& context) {
        QaRuleResult result;
        result.rule = "custom.rule";
        result.checked = 1;
        result.passed = 1;
        QaFinding finding;
        finding.severity = core::Severity::Warning;
        finding.rule = "custom.rule";
        finding.code = "CUSTOM-0001";
        finding.message = "plugin rule executed";
        result.findings.push_back(finding);
        result.warnings = 1;
        (void)context;
        return result;
    });
    auto names = qa.runner->rule_names();
    CHECK(std::find(names.begin(), names.end(), "custom.rule") != names.end());
    const auto report = qa.runner->run({"custom.rule"});
    REQUIRE(report.rules.size() == 1);
    CHECK(report.rules[0].findings.size() == 1);
    CHECK(report.warnings == 1);
    CHECK(report.success());
}

TEST_CASE("QA detects missing source files", "[pipeline][qa]")
{
    QaFixture qa("qa-missing");
    const std::string source = qa.fixture.write_asset("models/a.obj", test::cube_obj());
    REQUIRE(qa.fixture.pipeline().scan().discovered.size() == 1);
    REQUIRE(core::FileSystem::remove_file(qa.fixture.layout().root() / source).has_value());
    qa.runner->run();
    const auto missing = qa.fixture.registry().missing_source_files(qa.fixture.layout().root().string());
    CHECK(missing.size() == 1);
}
