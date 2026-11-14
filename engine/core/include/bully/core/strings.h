// Bully Studio - small string utilities shared by every module.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"

namespace bully::core {

[[nodiscard]] std::string to_lower(std::string_view text);
[[nodiscard]] std::string to_upper(std::string_view text);
[[nodiscard]] std::string trim(std::string_view text);
[[nodiscard]] bool starts_with_ci(std::string_view text, std::string_view prefix) noexcept;
[[nodiscard]] bool ends_with_ci(std::string_view text, std::string_view suffix) noexcept;
[[nodiscard]] bool equals_ci(std::string_view a, std::string_view b) noexcept;

[[nodiscard]] std::vector<std::string> split(std::string_view text, char separator);
[[nodiscard]] std::string join(const std::vector<std::string>& parts, std::string_view separator);
[[nodiscard]] std::string replace_all(std::string text, std::string_view from, std::string_view to);

/// Formats a one-line, length-limited preview of a text (for logs / consoles).
[[nodiscard]] std::string abbreviate(std::string_view text, std::size_t max_length = 80);

/// Decodes standard base64 (with or without padding, ignoring whitespace).
/// Returns diagnostics on invalid input.
[[nodiscard]] Result<std::vector<std::uint8_t>> base64_decode(std::string_view text);
[[nodiscard]] std::string base64_encode(const std::vector<std::uint8_t>& bytes);

/// Percent-encodes characters that are unsafe in file names (used when
/// deriving output paths from asset names).
[[nodiscard]] std::string sanitize_file_name(std::string_view text);

}  // namespace bully::core
