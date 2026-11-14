#include "bully/core/uuid.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <string_view>
#include <utility>

#include "bully/core/strings.h"

namespace bully::core {

namespace {

std::atomic<std::uint64_t> g_counter{0x9E3779B97F4A7C15ull};

std::uint64_t splitmix64(std::uint64_t& state)
{
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

std::array<std::uint8_t, 16> hash128(std::string_view name, std::string_view name_space)
{
    // FNV-1a over namespace + name, then two splitmix64 mixes to fill 128 bits.
    std::uint64_t state = 0xCBF29CE484222325ull;
    auto feed = [&state](std::string_view text) {
        for (char raw : text) {
            state ^= static_cast<unsigned char>(raw);
            state *= 0x100000001B3ull;
        }
    };
    feed(name_space);
    feed("\x1f");
    feed(name);
    state = splitmix64(state);
    std::uint64_t high = state;
    std::uint64_t low = splitmix64(state);
    std::array<std::uint8_t, 16> bytes{};
    for (int i = 0; i < 8; ++i) {
        bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((high >> (56 - 8 * i)) & 0xFF);
        bytes[static_cast<std::size_t>(8 + i)] = static_cast<std::uint8_t>((low >> (56 - 8 * i)) & 0xFF);
    }
    // Set version 5 + RFC 4122 variant bits so the value is a legal UUID.
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x50);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);
    return bytes;
}

int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

Uuid Uuid::generate()
{
    std::random_device device;
    std::mt19937_64 engine((static_cast<std::uint64_t>(device()) << 32) ^ device() ^
                           g_counter.fetch_add(1));
    std::uniform_int_distribution<std::uint32_t> dist(0, 255);
    std::array<std::uint8_t, 16> bytes{};
    for (auto& byte : bytes) byte = static_cast<std::uint8_t>(dist(engine));
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);
    return Uuid(bytes);
}

Uuid Uuid::from_name(std::string_view name, std::string_view name_space)
{
    return Uuid(hash128(name, name_space));
}

bool Uuid::try_parse(std::string_view text, Uuid& out)
{
    std::string compact;
    compact.reserve(32);
    for (char c : text) {
        if (c == '-') continue;
        const int value = hex_value(c);
        if (value < 0) return false;
        compact.push_back(static_cast<char>(value));
    }
    if (compact.size() != 32) return false;
    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t i = 0; i < 16; ++i) {
        bytes[i] = static_cast<std::uint8_t>((compact[i * 2] << 4) | compact[i * 2 + 1]);
    }
    out = Uuid(bytes);
    return true;
}

std::string Uuid::to_string() const
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(kHex[bytes_[i] >> 4]);
        out.push_back(kHex[bytes_[i] & 0x0F]);
    }
    return out;
}

bool Uuid::is_nil() const noexcept
{
    return std::all_of(bytes_.begin(), bytes_.end(), [](std::uint8_t b) { return b == 0; });
}

bool StableId::is_valid(std::string_view text) noexcept
{
    if (text.empty() || text.size() > 160) return false;
    bool segment_has_char = false;
    for (char c : text) {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        const bool extra = c == '_' || c == '-';
        if (c == '.') {
            if (!segment_has_char) return false;
            segment_has_char = false;
            continue;
        }
        if (!letter && !extra) return false;
        segment_has_char = true;
    }
    return segment_has_char;
}

bool StableId::try_parse(std::string_view text, StableId& out)
{
    std::string candidate = to_lower(trim(text));
    // Normalise separators and collapse runs of separators/dots.
    std::string normalised;
    normalised.reserve(candidate.size());
    for (char c : candidate) {
        if (c == ' ' || c == '/' || c == '\\') c = '.';
        if (c == '.' && (normalised.empty() || normalised.back() == '.')) continue;
        normalised.push_back(c);
    }
    while (!normalised.empty() && normalised.back() == '.') normalised.pop_back();
    if (!is_valid(normalised)) return false;
    out = StableId(std::move(normalised));
    return true;
}

StableId StableId::from_name(std::string_view name)
{
    StableId id;
    if (!try_parse(name, id)) {
        std::string fallback;
        for (char c : to_lower(name)) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
            fallback.push_back(ok ? c : '.');
        }
        if (!try_parse(fallback, id)) id = StableId("unnamed");
    }
    return id;
}

}  // namespace bully::core
