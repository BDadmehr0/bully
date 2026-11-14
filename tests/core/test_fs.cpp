#include <catch2/catch_test_macros.hpp>

#include "bully/core/fs.h"
#include "test_support.h"

using namespace bully::core;
namespace test = bully::test;

TEST_CASE("FileSystem writes atomically and reads back", "[core][fs]")
{
    test::TempDir dir;
    const auto path = dir / "nested/dir/file.txt";
    REQUIRE(FileSystem::write_text(path, "hello world").has_value());
    CHECK(FileSystem::exists(path));
    auto text = FileSystem::read_text(path);
    REQUIRE(text.has_value());
    CHECK(text.value() == "hello world");
    CHECK_FALSE(FileSystem::exists(dir / "nested/dir/file.txt.tmp"));
}

TEST_CASE("FileSystem reports missing files with diagnostics", "[core][fs]")
{
    test::TempDir dir;
    auto missing = FileSystem::read_text(dir / "nope.txt");
    CHECK_FALSE(missing.has_value());
    CHECK(missing.diagnostics().has_errors());
}

TEST_CASE("FileSystem hashes are stable", "[core][fs]")
{
    test::TempDir dir;
    dir.write("a.bin", "abc");
    const auto first = FileSystem::hash_file(dir / "a.bin");
    REQUIRE(first.has_value());
    dir.write("b.bin", "abc");
    const auto second = FileSystem::hash_file(dir / "b.bin");
    REQUIRE(second.has_value());
    CHECK(first.value() == second.value());
    dir.write("c.bin", "abd");
    const auto third = FileSystem::hash_file(dir / "c.bin");
    CHECK(third.value() != first.value());
}

TEST_CASE("FileSystem keeps paths inside a root", "[core][fs]")
{
    test::TempDir dir;
    REQUIRE(FileSystem::create_directories(dir / "inside").has_value());
    CHECK(FileSystem::is_within(dir / "inside/file.txt", dir.path()));
    CHECK_FALSE(FileSystem::is_within("/tmp", dir.path()));
}

TEST_CASE("FileSystem lists and filters directories", "[core][fs]")
{
    test::TempDir dir;
    dir.write("a.txt", "1");
    dir.write("sub/b.txt", "2");
    dir.write(".bully-cache/ignored.bin", "3");
    auto files = FileSystem::list_files(dir.path(), true);
    REQUIRE(files.has_value());
    CHECK(files.value().size() == 2);
}

TEST_CASE("FileSystem normalises separators", "[core][fs]")
{
    CHECK(FileSystem::normalize("a\\b\\c") == "a/b/c");
    CHECK(FileSystem::normalize("./a/b") == "a/b");
    CHECK(FileSystem::normalize("a//b") == "a/b");
}

TEST_CASE("FileSystem copy and rename report failures", "[core][fs]")
{
    test::TempDir dir;
    dir.write("one.txt", "data");
    CHECK(FileSystem::copy_file(dir / "one.txt", dir / "two.txt", false).has_value());
    auto missing = FileSystem::copy_file(dir / "absent.txt", dir / "three.txt");
    CHECK_FALSE(missing.has_value());
}
