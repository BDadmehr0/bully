// Bully Studio - asset / entity identifiers.
//
// Two id kinds are used across the platform:
//  * Uuid   - random 128-bit ids for assets, dialogue lines, story nodes...
//  * StableId - human-readable, persisted slug (e.g. "chr.missy.bully").
//
// Asset ids are persisted in project metadata (SQLite) and in every compiled
// binary format, so they must never be regenerated once assigned.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace bully::core {

class Uuid {
public:
    Uuid() = default;
    explicit Uuid(std::array<std::uint8_t, 16> bytes) : bytes_(bytes) {}

    /// Cryptographically-strong random v4 id.
    [[nodiscard]] static Uuid generate();

    /// Deterministic v5-style id from a namespace + name. Useful for importers
    /// so repeated imports of the same source path reuse the same asset id.
    [[nodiscard]] static Uuid from_name(std::string_view name,
                                        std::string_view name_space = "bully");

    /// Parse canonical "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" form.
    [[nodiscard]] static bool try_parse(std::string_view text, Uuid& out);

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] bool is_nil() const noexcept;
    [[nodiscard]] const std::array<std::uint8_t, 16>& bytes() const noexcept { return bytes_; }

    [[nodiscard]] auto operator<=>(const Uuid&) const noexcept = default;

private:
    std::array<std::uint8_t, 16> bytes_{};
};

/// A stable, human-readable identifier: lowercase segments separated by '.'.
/// Segments allow [a-z0-9_-]. Examples: "chr.missy", "dlg.mission01.open".
class StableId {
public:
    StableId() = default;
    explicit StableId(std::string value) : value_(std::move(value)) {}

    /// Validates and normalises (lowercase, trims) the input.
    [[nodiscard]] static bool try_parse(std::string_view text, StableId& out);
    [[nodiscard]] static StableId from_name(std::string_view name);
    [[nodiscard]] static bool is_valid(std::string_view text) noexcept;

    [[nodiscard]] const std::string& str() const noexcept { return value_; }
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }
    [[nodiscard]] std::string to_string() const { return value_; }

    [[nodiscard]] auto operator<=>(const StableId&) const noexcept = default;
    [[nodiscard]] bool operator==(const StableId&) const noexcept = default;

private:
    std::string value_;
};

}  // namespace bully::core
