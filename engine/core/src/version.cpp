#include "bully/core/version.h"

#include "bully/core/strings.h"

namespace bully::core {

std::string Version::to_string() const
{
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

Version Version::from_string(std::string_view text)
{
    Version version;
    const auto parts = split(trim(text), '.');
    auto take = [&](std::size_t index) -> std::uint32_t {
        if (index >= parts.size() || parts[index].empty()) return 0;
        try {
            return static_cast<std::uint32_t>(std::stoul(parts[index]));
        } catch (...) {
            return 0;
        }
    };
    version.major = take(0);
    version.minor = take(1);
    version.patch = take(2);
    return version;
}

}  // namespace bully::core
