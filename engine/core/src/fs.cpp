#include "bully/core/fs.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace bully::core {

namespace {

DiagnosticList io_error(const fs::path& path, std::string code, std::string message)
{
    DiagnosticList diagnostics;
    diagnostics.error(std::move(code), std::move(message), path.string());
    return diagnostics;
}

}  // namespace

Result<std::vector<std::uint8_t>> FileSystem::read_binary(const fs::path& path)
{
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        return Result<std::vector<std::uint8_t>>(
            io_error(path, "BULLY-IO-0001", "file does not exist or is not a regular file"));
    }
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return Result<std::vector<std::uint8_t>>(
            io_error(path, "BULLY-IO-0002", "failed to open file for reading"));
    }
    const std::streamoff size = stream.tellg();
    if (size < 0) {
        return Result<std::vector<std::uint8_t>>(
            io_error(path, "BULLY-IO-0003", "failed to determine file size"));
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (size > 0) {
        stream.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!stream) {
            return Result<std::vector<std::uint8_t>>(
                io_error(path, "BULLY-IO-0004", "short read while loading file"));
        }
    }
    return bytes;
}

Result<std::string> FileSystem::read_text(const fs::path& path)
{
    auto bytes = read_binary(path);
    if (!bytes) return Result<std::string>(bytes.diagnostics());
    return std::string(bytes.value().begin(), bytes.value().end());
}

Status FileSystem::create_directories(const fs::path& path)
{
    if (path.empty()) return Status{};
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec && !fs::is_directory(path)) {
        return Status(io_error(path, "BULLY-IO-0005", "failed to create directory: " + ec.message()));
    }
    return Status{};
}

Status FileSystem::write_atomic(const fs::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::error_code ec;
    if (path.has_parent_path()) {
        const auto parent_status = create_directories(path.parent_path());
        if (!parent_status) return parent_status;
    }
    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return Status(io_error(path, "BULLY-IO-0006", "failed to open temporary file for writing"));
        }
        if (!bytes.empty()) {
            stream.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }
        stream.flush();
        if (!stream) {
            stream.close();
            fs::remove(temp, ec);
            return Status(io_error(path, "BULLY-IO-0007", "failed to write file contents"));
        }
    }
    fs::rename(temp, path, ec);
    if (ec) {
        // Cross-device or locked destination: fall back to copy then remove.
        std::error_code copy_ec;
        fs::copy_file(temp, path, fs::copy_options::overwrite_existing, copy_ec);
        fs::remove(temp, ec);
        if (copy_ec) {
            return Status(io_error(path, "BULLY-IO-0008",
                                   "failed to commit file: " + copy_ec.message()));
        }
    }
    return Status{};
}

Status FileSystem::write_binary(const fs::path& path, const std::vector<std::uint8_t>& bytes)
{
    return write_atomic(path, bytes);
}

Status FileSystem::write_text(const fs::path& path, std::string_view text)
{
    return write_atomic(path, std::vector<std::uint8_t>(text.begin(), text.end()));
}

bool FileSystem::exists(const fs::path& path) noexcept
{
    std::error_code ec;
    return fs::exists(path, ec);
}

bool FileSystem::is_directory(const fs::path& path) noexcept
{
    std::error_code ec;
    return fs::is_directory(path, ec);
}

std::uint64_t FileSystem::file_size(const fs::path& path) noexcept
{
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    return ec ? 0ull : static_cast<std::uint64_t>(size);
}

std::int64_t FileSystem::modified_time(const fs::path& path) noexcept
{
    std::error_code ec;
    const auto time = fs::last_write_time(path, ec);
    if (ec) return 0;
    return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
}

Result<std::vector<fs::path>> FileSystem::list_files(const fs::path& root, bool recursive)
{
    std::error_code ec;
    if (!fs::exists(root, ec)) {
        return Result<std::vector<fs::path>>(
            io_error(root, "BULLY-IO-0001", "root directory does not exist"));
    }
    std::vector<fs::path> files;
    auto collect = [&files](const fs::directory_entry& entry) {
        std::error_code file_ec;
        if (entry.is_regular_file(file_ec)) files.push_back(entry.path());
    };
    if (recursive) {
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        for (; it != end && !ec; it.increment(ec)) {
            if (it->is_directory()) {
                const auto name = it->path().filename().string();
                if (name == ".bully-cache" || name == ".git" || name == "node_modules") {
                    it.disable_recursion_pending();
                }
                continue;
            }
            collect(*it);
        }
    } else {
        fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        fs::directory_iterator end;
        for (; it != end && !ec; it.increment(ec)) collect(*it);
    }
    if (ec) {
        return Result<std::vector<fs::path>>(
            io_error(root, "BULLY-IO-0009", "error while scanning directory: " + ec.message()));
    }
    std::sort(files.begin(), files.end());
    return files;
}

Status FileSystem::remove_file(const fs::path& path)
{
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) return Status(io_error(path, "BULLY-IO-0010", "failed to remove file: " + ec.message()));
    return Status{};
}

Status FileSystem::remove_tree(const fs::path& path)
{
    std::error_code ec;
    fs::remove_all(path, ec);
    if (ec) return Status(io_error(path, "BULLY-IO-0011", "failed to remove directory: " + ec.message()));
    return Status{};
}

Status FileSystem::copy_file(const fs::path& from, const fs::path& to, bool overwrite)
{
    std::error_code ec;
    if (to.has_parent_path()) {
        const auto parent_status = create_directories(to.parent_path());
        if (!parent_status) return parent_status;
    }
    auto options = fs::copy_options::none;
    if (overwrite) options |= fs::copy_options::overwrite_existing;
    fs::copy_file(from, to, options, ec);
    if (ec) {
        return Status(io_error(from, "BULLY-IO-0012",
                               "failed to copy to '" + to.string() + "': " + ec.message()));
    }
    return Status{};
}

Status FileSystem::rename_path(const fs::path& from, const fs::path& to)
{
    std::error_code ec;
    if (to.has_parent_path()) {
        const auto parent_status = create_directories(to.parent_path());
        if (!parent_status) return parent_status;
    }
    fs::rename(from, to, ec);
    if (ec) {
        return Status(io_error(from, "BULLY-IO-0013",
                               "failed to rename to '" + to.string() + "': " + ec.message()));
    }
    return Status{};
}

std::string FileSystem::relative_to(const fs::path& path, const fs::path& base)
{
    std::error_code ec;
    const auto relative = fs::relative(path, base, ec);
    if (ec) return normalize(path.string());
    return normalize(relative.string());
}

fs::path FileSystem::resolve(const fs::path& base, std::string_view maybe_relative)
{
    const fs::path candidate{std::string(maybe_relative)};
    if (candidate.is_absolute()) return candidate;
    return (base / candidate).lexically_normal();
}

bool FileSystem::is_within(const fs::path& path, const fs::path& root)
{
    std::error_code ec;
    const auto normal_root = fs::weakly_canonical(root, ec);
    if (ec) return false;
    const auto normal_path = fs::weakly_canonical(path, ec);
    if (ec) return false;
    auto root_it = normal_root.begin();
    auto path_it = normal_path.begin();
    for (; root_it != normal_root.end(); ++root_it, ++path_it) {
        if (path_it == normal_path.end() || *root_it != *path_it) return false;
    }
    return true;
}

std::uint64_t FileSystem::hash_bytes(const std::vector<std::uint8_t>& bytes) noexcept
{
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 0x100000001B3ull;
    }
    return hash;
}

std::optional<std::uint64_t> FileSystem::hash_file(const fs::path& path)
{
    auto bytes = read_binary(path);
    if (!bytes) return std::nullopt;
    return hash_bytes(bytes.value());
}

std::string FileSystem::hash_to_hex(std::uint64_t hash)
{
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

std::string FileSystem::normalize(std::string_view path)
{
    std::string out;
    out.reserve(path.size());
    for (char c : path) {
        if (c == '\\') {
            out.push_back('/');
            continue;
        }
        if (c == '/' && !out.empty() && out.back() == '/') continue;
        out.push_back(c);
    }
    // Collapse "/./" and leading "./".
    while (out.rfind("./", 0) == 0) out.erase(0, 2);
    const std::string dot_segment = "/./";
    std::size_t found = 0;
    while ((found = out.find(dot_segment, found)) != std::string::npos) out.erase(found, 2);
    if (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}

}  // namespace bully::core
