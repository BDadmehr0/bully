#include <catch2/catch_test_macros.hpp>
#include <string>

#include <filesystem>

#include "bully/core/json.h"

using namespace bully::core;
using namespace bully::core::json;

TEST_CASE("JSON round-trips scalars and containers", "[core][json]")
{
    Value root = Value::make_object();
    root["name"] = "Missy";
    root["count"] = 3;
    root["ratio"] = 0.25;
    root["enabled"] = true;
    root["nothing"] = nullptr;
    root["tags"] = Value::make_array();
    root["tags"].push("a");
    root["tags"].push("b");
    root["nested"] = Value::make_object();
    root["nested"]["deep"] = 1;

    const std::string text = root.dump(2);
    auto parsed = parse(text);
    REQUIRE(parsed.has_value());
    const auto& back = parsed.value();
    CHECK(back.get_string("name") == "Missy");
    CHECK(back.get_int("count") == 3);
    CHECK(back.get_number("ratio") == 0.25);
    CHECK(back.get_bool("enabled"));
    CHECK(back.at("nothing").is_null());
    CHECK(back.at("tags").size() == 2);
    CHECK(back.at("tags")[1].as_string() == "b");
    CHECK(back.at("nested").get_int("deep") == 1);
}

TEST_CASE("JSON parser reports malformed input", "[core][json]")
{
    auto missing = parse("{\"a\": }");
    CHECK_FALSE(missing.has_value());
    CHECK(missing.diagnostics().has_errors());

    auto unterminated = parse("{\"a\": 1");
    CHECK_FALSE(unterminated.has_value());

    auto trailing = parse("{} junk");
    CHECK_FALSE(trailing.has_value());

    auto bad_escape = parse("\"\\q\"");
    CHECK_FALSE(bad_escape.has_value());
}

TEST_CASE("JSON handles nested arrays of objects", "[core][json]")
{
    auto parsed = parse(R"([{"id":1},{"id":2,"name":"x"}])");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed.value().size() == 2);
    CHECK(parsed.value()[0].get_int("id") == 1);
    CHECK(parsed.value()[1].get_string("name") == "x");
}

TEST_CASE("JSON unicode escapes decode to UTF-8", "[core][json]")
{
    auto parsed = parse("\"caf\\u00e9\"");
    REQUIRE(parsed.has_value());
    CHECK(parsed.value().as_string() == "café");
}

TEST_CASE("JSON output is deterministic and compact when requested", "[core][json]")
{
    Value object = Value::make_object();
    object["b"] = 2;
    object["a"] = 1;
    // Insertion order is preserved so compiled sidecars stay diff-friendly.
    CHECK(object.dump(0) == "{\"b\":2,\"a\":1}");
    CHECK(object.dump(0) == object.dump(0));
}

TEST_CASE("JSON round-trips through files", "[core][json]")
{
    const auto path = std::filesystem::temp_directory_path() / "bully-json-test.json";
    Value value = Value::make_object();
    value["hello"] = "world";
    REQUIRE(save_file(path.string(), value).has_value());
    auto loaded = load_file(path.string());
    REQUIRE(loaded.has_value());
    CHECK(loaded.value().get_string("hello") == "world");
    std::filesystem::remove(path);
}
