#include <catch2/catch_test_macros.hpp>

#include <set>

#include "bully/core/uuid.h"

using namespace bully::core;

TEST_CASE("Uuid generates unique non-nil ids", "[core][uuid]")
{
    std::set<std::string> ids;
    for (int i = 0; i < 500; ++i) ids.insert(Uuid::generate().to_string());
    CHECK(ids.size() == 500);
    CHECK_FALSE(Uuid::generate().is_nil());
}

TEST_CASE("Uuid round-trips through text", "[core][uuid]")
{
    const Uuid id = Uuid::generate();
    Uuid parsed;
    REQUIRE(Uuid::try_parse(id.to_string(), parsed));
    CHECK(parsed == id);
    CHECK_FALSE(Uuid::try_parse("not-a-uuid", parsed));
}

TEST_CASE("Uuid::from_name is deterministic", "[core][uuid]")
{
    const Uuid a = Uuid::from_name("assets/models/crate.obj");
    const Uuid b = Uuid::from_name("assets/models/crate.obj");
    const Uuid c = Uuid::from_name("assets/models/other.obj");
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a.to_string().size() == 36);
    // Version 5 + RFC 4122 variant bits.
    CHECK(a.to_string()[14] == '5');
}

TEST_CASE("StableId validates and normalises", "[core][uuid]")
{
    StableId id;
    REQUIRE(StableId::try_parse("Chr.Missy.Bully", id));
    CHECK(id.str() == "chr.missy.bully");
    CHECK_FALSE(StableId::try_parse("", id));
    // Spaces and slashes normalise into segment separators.
    REQUIRE(StableId::try_parse("has space", id));
    CHECK(id.str() == "has.space");
    CHECK_FALSE(StableId::try_parse("has#hash", id));
    CHECK_FALSE(StableId::try_parse("..", id));
    CHECK(StableId::from_name("Missy's Bully").str().find(' ') == std::string::npos);
}
