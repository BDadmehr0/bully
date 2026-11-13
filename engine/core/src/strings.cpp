#include "bully/core/strings.h"

#include <algorithm>
#include <cctype>

#include "bully/core/fs.h"

namespace bully::core {

std::string to_lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string to_upper(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

std::string trim(std::string_view text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return std::string(text.substr(begin, end - begin));
}

bool starts_with_ci(std::string_view text, std::string_view prefix) noexcept
{
    if (prefix.size() > text.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

bool ends_with_ci(std::string_view text, std::string_view suffix) noexcept
{
    if (suffix.size() > text.size()) return false;
    return starts_with_ci(text.substr(text.size() - suffix.size()), suffix);
}

bool equals_ci(std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size() && starts_with_ci(a, b);
}

std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == separator) {
            parts.emplace_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

std::string join(const std::vector<std::string>& parts, std::string_view separator)
{
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) out.append(separator);
        out.append(parts[i]);
    }
    return out;
}

std::string replace_all(std::string text, std::string_view from, std::string_view to)
{
    if (from.empty()) return text;
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
    return text;
}

std::string abbreviate(std::string_view text, std::size_t max_length)
{
    std::string single_line = replace_all(replace_all(std::string(text), "\r\n", " "), "\n", " ");
    if (single_line.size() <= max_length) return single_line;
    if (max_length <= 3) return single_line.substr(0, max_length);
    return single_line.substr(0, max_length - 3) + "...";
}

std::string sanitize_file_name(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        out.push_back(safe ? c : '_');
    }
    while (!out.empty() && out.back() == '.') out.pop_back();
    if (out.empty()) out = "unnamed";
    return out;
}

}  // namespace bully::core

namespace bully::core {

namespace {

int base64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

Result<std::vector<std::uint8_t>> base64_decode(std::string_view text)
{
    DiagnosticList diagnostics;
    std::vector<std::uint8_t> out;
    out.reserve(text.size() * 3 / 4 + 3);
    std::uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        const int value = base64_value(c);
        if (value < 0) {
            diagnostics.error("BULLY-STR-0001", "invalid base64 character in payload");
            return Result<std::vector<std::uint8_t>>(std::move(diagnostics));
        }
        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFFu));
        }
    }
    return out;
}

std::string base64_encode(const std::vector<std::uint8_t>& bytes)
{
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 2 < bytes.size()) {
        const std::uint32_t triple = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                                     (static_cast<std::uint32_t>(bytes[i + 1]) << 8) |
                                     static_cast<std::uint32_t>(bytes[i + 2]);
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3F]);
        out.push_back(kAlphabet[triple & 0x3F]);
        i += 3;
    }
    if (i + 1 == bytes.size()) {
        const std::uint32_t triple = static_cast<std::uint32_t>(bytes[i]) << 16;
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (i + 2 == bytes.size()) {
        const std::uint32_t triple = (static_cast<std::uint32_t>(bytes[i]) << 16) |
                                     (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

}  // namespace bully::core
