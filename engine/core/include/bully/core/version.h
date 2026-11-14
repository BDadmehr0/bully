// Bully Studio - core version information.
//
// This header is generated-by-hand (not by CMake) so that every module and
// external plugin can depend on a stable compile-time version contract.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace bully::core {

/// Semantic version, ordered numerically for comparison.
struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    [[nodiscard]] constexpr auto operator<=>(const Version&) const noexcept = default;

    /// Compact numeric encoding: major * 10000 + minor * 100 + patch.
    /// Used by binary formats for cheap compatibility gates.
    [[nodiscard]] constexpr std::uint32_t packed() const noexcept
    {
        return major * 10000u + minor * 100u + patch;
    }

    [[nodiscard]] std::string to_string() const;
    static Version from_string(std::string_view text);
};

/// Platform version (Bully Studio / runtime / studio UI / plugin ABI).
inline constexpr Version kPlatformVersion{0, 1, 0};

/// Plugin ABI version. Plugins declare the ABI major they were built against;
/// the host refuses to load plugins with a different major.
inline constexpr std::uint32_t kPluginAbiMajor = 1;
inline constexpr std::uint32_t kPluginAbiMinor = 0;

}  // namespace bully::core
