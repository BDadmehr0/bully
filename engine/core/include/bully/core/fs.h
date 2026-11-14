// Bully Studio - filesystem helpers.
//
// All modules access the disk through this wrapper so that path handling,
// atomic writes and hashing behave identically on every platform. Paths are
// stored as UTF-8 std::string in project metadata; std::filesystem is used
// only for the actual syscalls.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"

namespace bully::core {

namespace fs = std::filesystem;

class FileSystem {
public:
    /// Reads a whole file as bytes. Returns diagnostics (BULLY-IO-*) on failure.
    [[nodiscard]] static Result<std::vector<std::uint8_t>> read_binary(const fs::path& path);

    /// Reads a whole file as UTF-8 text.
    [[nodiscard]] static Result<std::string> read_text(const fs::path& path);

    /// Writes atomically (temp file + rename). Creates parent directories.
    [[nodiscard]] static Status write_binary(const fs::path& path,
                                             const std::vector<std::uint8_t>& bytes);

    [[nodiscard]] static Status write_text(const fs::path& path, std::string_view text);

    /// Atomic write via a "file.tmp-<pid>" sibling; never leaves a truncated
    /// destination behind if the process dies mid-write.
    [[nodiscard]] static Status write_atomic(const fs::path& path,
                                             const std::vector<std::uint8_t>& bytes);

    [[nodiscard]] static bool exists(const fs::path& path) noexcept;
    [[nodiscard]] static bool is_directory(const fs::path& path) noexcept;
    [[nodiscard]] static std::uint64_t file_size(const fs::path& path) noexcept;
    [[nodiscard]] static std::int64_t modified_time(const fs::path& path) noexcept;

    /// Recursively lists regular files under `root`, sorted lexicographically.
    /// Skips the internal cache folder (`.bully-cache`) by default.
    [[nodiscard]] static Result<std::vector<fs::path>> list_files(const fs::path& root,
                                                                 bool recursive = true);

    [[nodiscard]] static Status create_directories(const fs::path& path);
    [[nodiscard]] static Status remove_file(const fs::path& path);
    [[nodiscard]] static Status remove_tree(const fs::path& path);

    /// Copies a file, creating parents. Refuses to overwrite unless requested.
    [[nodiscard]] static Status copy_file(const fs::path& from, const fs::path& to,
                                          bool overwrite = true);

    /// Moves/renames a file, creating parents.
    [[nodiscard]] static Status rename_path(const fs::path& from, const fs::path& to);

    /// Makes `path` relative to `base` when possible, otherwise absolute.
    [[nodiscard]] static std::string relative_to(const fs::path& path, const fs::path& base);

    /// Resolves `maybe_relative` against `base` when it is not absolute.
    [[nodiscard]] static fs::path resolve(const fs::path& base, std::string_view maybe_relative);

    /// Guarantees a path stays inside `root` (defence against "../" escapes in
    /// project-authored import settings).
    [[nodiscard]] static bool is_within(const fs::path& path, const fs::path& root);

    /// FNV-1a 64-bit hash of arbitrary bytes; used for change detection.
    [[nodiscard]] static std::uint64_t hash_bytes(const std::vector<std::uint8_t>& bytes) noexcept;
    [[nodiscard]] static std::optional<std::uint64_t> hash_file(const fs::path& path);

    /// Hex rendering of a 64-bit hash.
    [[nodiscard]] static std::string hash_to_hex(std::uint64_t hash);

    /// Normalises to forward slashes and drops redundant "." segments, so the
    /// same asset referenced from JSON and SQLite compares equal.
    [[nodiscard]] static std::string normalize(std::string_view path);
};

}  // namespace bully::core
