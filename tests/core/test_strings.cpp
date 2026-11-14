#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "bully/core/strings.h"

using namespace bully::core;

TEST_CASE("string helpers normalise case and whitespace", "[core][strings]")
{
    CHECK(to_lower("HeLLo") == "hello");
    CHECK(to_upper("abc") == "ABC");
    CHECK(trim("  padded \n") == "padded");
    CHECK(starts_with_ci("Hello.something", "hello"));
    CHECK(ends_with_ci("file.OBJ", ".obj"));
    CHECK(equals_ci("ABC", "abc"));
    CHECK_FALSE(equals_ci("abc", "abd"));
}

TEST_CASE("split and join are inverse", "[core][strings]")
{
    const auto parts = split("a,b,,c", ',');
    REQUIRE(parts.size() == 4);
    CHECK(parts[2].empty());
    CHECK(join({"a", "b"}, "+") == "a+b");
}

TEST_CASE("replace_all handles empty needles", "[core][strings]")
{
    CHECK(replace_all("a.b.c", ".", "/") == "a/b/c");
    CHECK(replace_all("abc", "", "x") == "abc");
}

TEST_CASE("abbreviate shortens long text", "[core][strings]")
{
    CHECK(abbreviate("short", 10) == "short");
    const std::string long_text(50, 'x');
    const std::string shortened = abbreviate(long_text, 10);
    CHECK(shortened.size() == 10);
    CHECK(shortened.substr(shortened.size() - 3) == "...");
}

TEST_CASE("sanitize_file_name removes unsafe characters", "[core][strings]")
{
    CHECK(sanitize_file_name("My Model!.obj") == "My_Model_.obj");
    CHECK(sanitize_file_name("") == "unnamed");
    CHECK(sanitize_file_name("../../etc/passwd") == ".._.._etc_passwd");
}

TEST_CASE("base64 round-trips", "[core][strings]")
{
    const std::vector<std::uint8_t> bytes{'a', 'b', 'c', 'd', 'e'};
    const std::string encoded = base64_encode(bytes);
    auto decoded = base64_decode(encoded);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value() == bytes);

    auto invalid = base64_decode("!!!not base64!!!");
    CHECK_FALSE(invalid.has_value());
}
