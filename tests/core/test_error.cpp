#include <catch2/catch_test_macros.hpp>

#include "bully/core/error.h"

using namespace bully::core;

TEST_CASE("DiagnosticList tracks severities", "[core][error]")
{
    DiagnosticList diagnostics;
    CHECK(diagnostics.empty());
    diagnostics.info("CODE-1", "info");
    diagnostics.warn("CODE-2", "warn");
    CHECK_FALSE(diagnostics.has_errors());
    diagnostics.error("CODE-3", "bad");
    CHECK(diagnostics.has_errors());
    CHECK(diagnostics.count(Severity::Error) == 1);
    CHECK(diagnostics.count(Severity::Warning) == 1);
    CHECK_FALSE(diagnostics.has_fatal());
    diagnostics.add(Severity::Fatal, "CODE-4", "dead");
    CHECK(diagnostics.has_fatal());
    CHECK(diagnostics.error_count() == 2);
}

TEST_CASE("Diagnostic::format is greppable", "[core][error]")
{
    Diagnostic diagnostic{Severity::Error, "BULLY-IO-0001", "missing file", "a/b.c", 12};
    CHECK(diagnostic.format() == "[error] BULLY-IO-0001: missing file (a/b.c:12)");
}

TEST_CASE("Result carries either a value or diagnostics", "[core][error]")
{
    Result<int> good{42};
    CHECK(good.has_value());
    CHECK(good.value() == 42);

    DiagnosticList diagnostics;
    diagnostics.error("X", "failed");
    Result<int> bad{std::move(diagnostics)};
    CHECK_FALSE(bad.has_value());
    CHECK(bad.diagnostics().has_errors());

    Status ok{};
    CHECK(ok.has_value());

    DiagnosticList fatal;
    fatal.add(Severity::Fatal, "Y", "boom");
    Status failure{std::move(fatal)};
    CHECK_FALSE(failure.has_value());
    CHECK(failure.diagnostics().has_fatal());
}

TEST_CASE("DiagnosticList append keeps order", "[core][error]")
{
    DiagnosticList first;
    first.error("A", "one");
    DiagnosticList second;
    second.warn("B", "two");
    first.append(second);
    REQUIRE(first.all().size() == 2);
    CHECK(first.all()[0].code == "A");
    CHECK(first.all()[1].code == "B");
}
