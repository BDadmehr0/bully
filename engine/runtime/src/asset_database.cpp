#include "bully/runtime/asset_database.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <utility>

#include "bully/core/fs.h"
#include "bully/formats/binary.h"
#include "bully/core/log.h"
#include "bully/core/strings.h"

namespace bully::runtime {

namespace {

constexpr std::string_view kLogCategory = "runtime";

std::string normalized_key(std::string_view text)
{
    return core::to_lower(core::FileSystem::normalize(std::string(text)));
}

/// Reads an asset's id without decoding the whole document.
///
/// Every `.b*` format starts with the 32-byte container header followed by an
/// `INFO` chunk whose body begins with the asset id string (see
/// docs/formats/README.md). Directory mounts use this to index assets by id, so
/// a scene that references a model by id resolves without loading it.
std::string sniff_asset_id(const std::filesystem::path& path)
{
    auto handle = core::FileSystem::read_binary(path);
    if (!handle || handle.value().size() < 12 + 8 + 4) return {};
    const auto& bytes = handle.value();
    auto read_u32 = [&](std::size_t offset) {
        return static_cast<std::uint32_t>(bytes[offset]) |
               (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
               (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
               (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    };
    // Header sanity: header_size must be the documented 32 bytes.
    if (read_u32(8) != 32) return {};
    const std::size_t chunk_offset = 32;
    if (bytes.size() < chunk_offset + 8) return {};
    // Compare the integer form: fourcc() packs the name big-endian while the
    // file stores it little-endian, so comparing decoded integers is the only
    // byte-order-independent test. INFO is the chunk every asset format starts
    // with; ENTR is a package's table of contents.
    const std::uint32_t chunk_id = read_u32(chunk_offset);
    if (chunk_id != formats::fourcc("INFO") && chunk_id != formats::fourcc("ENTR")) return {};
    // chunk header (fourcc + size) then the length-prefixed asset id.
    const std::uint32_t id_length = read_u32(chunk_offset + 8);
    if (id_length == 0 || id_length > 256 || bytes.size() < chunk_offset + 12 + id_length) {
        return {};
    }
    return std::string(bytes.begin() + static_cast<std::ptrdiff_t>(chunk_offset + 12),
                       bytes.begin() + static_cast<std::ptrdiff_t>(chunk_offset + 12 + id_length));
}

std::string extension_of(std::string_view path)
{
    const std::string text(path);
    const auto slash = text.find_last_of("/\\");
    const auto dot = text.find_last_of('.');
    if (dot == std::string::npos) return {};
    if (slash != std::string::npos && dot < slash) return {};
    return core::to_lower(text.substr(dot));
}

}  // namespace

std::string_view to_string(AssetKind kind) noexcept
{
    switch (kind) {
        case AssetKind::Model: return "model";
        case AssetKind::Texture: return "texture";
        case AssetKind::Material: return "material";
        case AssetKind::Animation: return "animation";
        case AssetKind::Audio: return "audio";
        case AssetKind::Dialogue: return "dialogue";
        case AssetKind::Scene: return "scene";
        case AssetKind::Mission: return "mission";
        case AssetKind::Unknown: return "unknown";
    }
    return "unknown";
}

AssetKind kind_from_extension(std::string_view extension) noexcept
{
    const std::string ext = core::to_lower(extension);
    if (ext == ".bmodel") return AssetKind::Model;
    if (ext == ".btexture") return AssetKind::Texture;
    if (ext == ".bmaterial") return AssetKind::Material;
    if (ext == ".banim") return AssetKind::Animation;
    if (ext == ".baudio") return AssetKind::Audio;
    if (ext == ".bdialogue") return AssetKind::Dialogue;
    if (ext == ".bscene") return AssetKind::Scene;
    if (ext == ".bmission") return AssetKind::Mission;
    return AssetKind::Unknown;
}

// ------------------------------------------------------------------- impl ---

struct AssetDatabase::Impl {
    struct Mount {
        MountInfo info;
        std::shared_ptr<const formats::PackageDocument> package;  // null for directories
    };

    std::vector<Mount> mounts;
    /// Entries contributed by each mount, in mount order (parallel to `mounts`).
    std::vector<std::map<std::string, AssetEntry>> mount_entries;
    struct RecordedEntry {
        AssetEntry entry;
        std::size_t mount_index = 0;
    };
    /// One record per logical path; later mounts override earlier ones.
    std::map<std::string, RecordedEntry> entries;
    /// Lookup keys (lower-cased asset id and logical path) -> logical path.
    std::map<std::string, std::string> index;

    // decoded document caches, keyed by resolved logical path
    mutable std::map<std::string, std::shared_ptr<const formats::ModelDocument>> models;
    mutable std::map<std::string, std::shared_ptr<const formats::TextureDocument>> textures;
    mutable std::map<std::string, std::shared_ptr<const formats::MaterialDocument>> materials;
    mutable std::map<std::string, std::shared_ptr<const formats::AnimationDocument>> animations;
    mutable std::map<std::string, std::shared_ptr<const formats::AudioDocument>> audio;
    mutable std::map<std::string, std::shared_ptr<const formats::DialogueDocument>> dialogs;
    mutable std::map<std::string, std::shared_ptr<const formats::SceneDocument>> scenes;
    mutable std::map<std::string, std::shared_ptr<const formats::MissionDocument>> missions;

    /// Rebuilds the resolved view after a mount or unmount. `entries` holds one
    /// record per logical path; the index additionally maps asset ids (and the
    /// lower-cased path) onto those records so both lookups work.
    void reindex()
    {
        entries.clear();
        index.clear();
        for (std::size_t i = 0; i < mount_entries.size(); ++i) {
            for (const auto& [key, entry] : mount_entries[i]) {
                const std::string& path = entry.logical_path;
                // Later mounts override earlier ones for the same path, and a
                // duplicate path from one mount (id alias) is ignored.
                const auto existing = entries.find(path);
                if (existing != entries.end() && i < existing->second.mount_index) continue;
                entries[path] = RecordedEntry{entry, i};
                index[key] = path;
                if (!entry.asset_id.empty()) index[normalized_key(entry.asset_id)] = path;
            }
        }
    }
};

AssetDatabase::AssetDatabase() = default;
AssetDatabase::~AssetDatabase() = default;

core::Status AssetDatabase::mount_directory(const std::filesystem::path& built_dir,
                                            std::string mount_name)
{
    if (!impl_) impl_ = std::make_unique<Impl>();
    core::DiagnosticList diagnostics;
    if (!core::FileSystem::exists(built_dir)) {
        diagnostics.error("BULLY-RT-0001", "asset directory does not exist",
                          built_dir.string());
        return core::Status(std::move(diagnostics));
    }
    if (!std::filesystem::is_directory(built_dir)) {
        diagnostics.error("BULLY-RT-0002", "asset mount is not a directory",
                          built_dir.string());
        return core::Status(std::move(diagnostics));
    }

    MountInfo info;
    info.name = std::move(mount_name);
    info.path = built_dir;
    info.is_package = false;

    std::map<std::string, AssetEntry> collected;
    std::uint32_t ignored = 0;
    for (const auto& item : std::filesystem::recursive_directory_iterator(built_dir)) {
        if (!item.is_regular_file()) continue;
        const std::string extension = extension_of(item.path().string());
        const AssetKind kind = kind_from_extension(extension);
        if (kind == AssetKind::Unknown) {
            // Packages nested in a build directory are mounts of their own; every
            // other file (reports, .bmeta, editor debris) is simply not an asset.
            if (extension == ".bpackage") continue;
            ++ignored;
            continue;
        }
        AssetEntry entry;
        entry.logical_path = core::FileSystem::normalize(
            item.path().lexically_relative(built_dir).string());
        entry.kind = kind;
        entry.mount = info.name;
        entry.file = item.path();
        entry.size = static_cast<std::uint32_t>(item.file_size());
        entry.asset_id = sniff_asset_id(item.path());
        if (!entry.asset_id.empty()) {
            collected[normalized_key(entry.asset_id)] = entry;
        }
        entry.asset_id.clear();
        collected[normalized_key(entry.logical_path)] = std::move(entry);
        ++info.entry_count;
    }

    // Replacing an existing mount of the same name keeps mount order stable.
    impl_->mounts.erase(std::remove_if(impl_->mounts.begin(), impl_->mounts.end(),
                                       [&](const Impl::Mount& mount) {
                                           return mount.info.name == info.name;
                                       }),
                        impl_->mounts.end());
    Impl::Mount mount;
    mount.info = std::move(info);
    impl_->mounts.push_back(std::move(mount));
    impl_->mount_entries.push_back(std::move(collected));
    impl_->reindex();

    if (ignored > 0) {
        core::log_debug(kLogCategory, "ignored " + std::to_string(ignored) +
                                          " non-asset file(s) in " + built_dir.string());
    }
    core::log_info(kLogCategory, "mounted " +
                                     std::to_string(impl_->mount_entries.back().size()) +
                                     " asset(s) from " + built_dir.string());
    return core::Status{};
}

core::Status AssetDatabase::mount_package(const std::filesystem::path& package_path)
{
    return mount_package(package_path, package_path.stem().string());
}

core::Status AssetDatabase::mount_package(const std::filesystem::path& package_path,
                                          std::string mount_name)
{
    if (!impl_) impl_ = std::make_unique<Impl>();
    core::DiagnosticList diagnostics;
    auto bytes = core::FileSystem::read_binary(package_path);
    if (!bytes) {
        diagnostics.append(bytes.diagnostics());
        return core::Status(std::move(diagnostics));
    }
    formats::PackageDocument reader;
    auto decoded = reader.deserialize(bytes.value());
    if (!decoded) {
        diagnostics.append(decoded.diagnostics());
        return core::Status(std::move(diagnostics));
    }
    auto package = std::make_shared<const formats::PackageDocument>(std::move(decoded.value()));
    // Integrity is checked once, at mount time: a shipped package is either
    // good or it is not, and finding out at mount is far more debuggable than
    // discovering a corrupt entry three hours into a playthrough.
    auto integrity = package->verify();
    if (!integrity) {
        diagnostics.append(integrity.diagnostics());
        return core::Status(std::move(diagnostics));
    }

    MountInfo info;
    info.name = std::move(mount_name);
    info.path = package_path;
    info.is_package = true;
    info.entry_count = static_cast<std::uint32_t>(package->entries.size());

    std::map<std::string, AssetEntry> collected;
    for (const auto& package_entry : package->entries) {
        AssetEntry entry;
        entry.asset_id = package_entry.asset_id;
        entry.logical_path = core::FileSystem::normalize(package_entry.logical_path);
        entry.kind = kind_from_extension(extension_of(entry.logical_path));
        entry.mount = info.name;
        entry.size = package_entry.size;
        if (!entry.valid()) continue;
        if (!entry.asset_id.empty()) collected[normalized_key(entry.asset_id)] = entry;
        const std::string path_key = normalized_key(entry.logical_path);
        entry.asset_id.clear();
        collected[path_key] = std::move(entry);
    }

    impl_->mounts.erase(std::remove_if(impl_->mounts.begin(), impl_->mounts.end(),
                                       [&](const Impl::Mount& mount) {
                                           return mount.info.name == info.name;
                                       }),
                        impl_->mounts.end());
    Impl::Mount mount;
    mount.info = std::move(info);
    mount.package = package;
    impl_->mounts.push_back(std::move(mount));
    impl_->mount_entries.push_back(std::move(collected));
    impl_->reindex();

    core::log_info(kLogCategory, "mounted package " + package_path.filename().string() + " (" +
                                     std::to_string(impl_->mount_entries.back().size()) +
                                     " entries)");
    return core::Status{};
}

void AssetDatabase::unmount(std::string_view mount_name)
{
    if (!impl_) return;
    for (std::size_t i = 0; i < impl_->mounts.size(); ++i) {
        if (impl_->mounts[i].info.name != mount_name) continue;
        impl_->mounts.erase(impl_->mounts.begin() + static_cast<std::ptrdiff_t>(i));
        impl_->mount_entries.erase(impl_->mount_entries.begin() + static_cast<std::ptrdiff_t>(i));
        impl_->reindex();
        clear_caches();
        return;
    }
}

std::vector<MountInfo> AssetDatabase::mounts() const
{
    std::vector<MountInfo> result;
    if (!impl_) return result;
    result.reserve(impl_->mounts.size());
    for (const auto& mount : impl_->mounts) result.push_back(mount.info);
    return result;
}

std::size_t AssetDatabase::entry_count() const
{
    return impl_ ? impl_->entries.size() : 0;
}

std::optional<AssetEntry> AssetDatabase::find(std::string_view id_or_path) const
{
    if (!impl_ || id_or_path.empty()) return std::nullopt;
    const std::string key = normalized_key(id_or_path);
    const auto found = impl_->index.find(key);
    if (found == impl_->index.end()) return std::nullopt;
    const auto entry = impl_->entries.find(found->second);
    if (entry == impl_->entries.end()) return std::nullopt;
    return entry->second.entry;
}

std::vector<AssetEntry> AssetDatabase::entries(AssetKind kind) const
{
    std::vector<AssetEntry> result;
    if (!impl_) return result;
    for (const auto& [path, recorded] : impl_->entries) {
        (void)path;
        if (kind != AssetKind::Unknown && recorded.entry.kind != kind) continue;
        result.push_back(recorded.entry);
    }
    std::sort(result.begin(), result.end(), [](const AssetEntry& a, const AssetEntry& b) {
        return a.logical_path < b.logical_path;
    });
    return result;
}

core::Result<std::vector<std::uint8_t>> AssetDatabase::read(std::string_view id_or_path) const
{
    core::DiagnosticList diagnostics;
    const auto entry = find(id_or_path);
    if (!entry) {
        diagnostics.error("BULLY-RT-0003", "asset not found in any mount",
                          std::string(id_or_path));
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    for (std::size_t i = 0; i < impl_->mounts.size(); ++i) {
        if (impl_->mounts[i].info.name != entry->mount) continue;
        const auto& mount = impl_->mounts[i];
        if (!mount.package) {
            auto bytes = core::FileSystem::read_binary(entry->file);
            if (!bytes) return core::Result<std::vector<std::uint8_t>>(bytes.diagnostics());
            return bytes.value();
        }
        // Package mount: prefer the asset id (packages may rename logical paths
        // between builds); fall back to the logical path.
        if (!entry->asset_id.empty()) {
            auto extracted = mount.package->extract(entry->asset_id);
            if (extracted) return extracted.value();
        }
        for (const auto& package_entry : mount.package->entries) {
            if (normalized_key(package_entry.logical_path) != normalized_key(entry->logical_path)) {
                continue;
            }
            auto extracted = mount.package->extract(package_entry.asset_id);
            if (!extracted) return core::Result<std::vector<std::uint8_t>>(extracted.diagnostics());
            return extracted.value();
        }
        diagnostics.error("BULLY-RT-0004", "asset is indexed but missing from its package",
                          entry->logical_path);
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    diagnostics.error("BULLY-RT-0005", "asset's mount was removed", entry->logical_path);
    return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
}

namespace {

template <typename Document>
core::Result<std::shared_ptr<const Document>> decode_typed(const std::vector<std::uint8_t>& bytes,
                                                           std::string_view path)
{
    Document reader;
    auto decoded = reader.deserialize(bytes);
    if (!decoded) {
        return core::Result<std::shared_ptr<const Document>>(decoded.diagnostics());
    }
    // Validation warnings do not block loading (a shipped asset with a soft
    // warning must still run), but they are surfaced to the caller's log.
    auto validation = decoded.value().validate();
    for (const auto& item : validation.diagnostics().all()) {
        if (item.severity == core::Severity::Error) {
            core::DiagnosticList diagnostics;
            diagnostics.append(validation.diagnostics());
            return core::Result<std::shared_ptr<const Document>>(std::move(diagnostics));
        }
        core::log_warn(kLogCategory,
                       std::string(path) + ": " + item.code + " " + item.message);
    }
    return std::make_shared<const Document>(std::move(decoded.value()));
}

}  // namespace

#define BULLY_RUNTIME_LOADER(fn, cache_map, Document)                                      \
    core::Result<std::shared_ptr<const Document>> AssetDatabase::fn(std::string_view id) const \
    {                                                                                      \
        if (!impl_) {                                                                      \
            core::DiagnosticList diagnostics;                                              \
            diagnostics.error("BULLY-RT-0006", "no asset database mounts", std::string(id)); \
            return core::Result<std::shared_ptr<const Document>>(std::move(diagnostics));  \
        }                                                                                  \
        const auto entry = find(id);                                                       \
        if (!entry) {                                                                      \
            core::DiagnosticList diagnostics;                                              \
            diagnostics.error("BULLY-RT-0003", "asset not found in any mount",             \
                              std::string(id));                                            \
            return core::Result<std::shared_ptr<const Document>>(std::move(diagnostics));  \
        }                                                                                  \
        const std::string key = normalized_key(entry->logical_path);                       \
        if (const auto cached = impl_->cache_map.find(key); impl_->cache_map.end() != cached) { \
            return cached->second;                                                         \
        }                                                                                  \
        auto bytes = read(entry->logical_path);                                            \
        if (!bytes) {                                                                      \
            return core::Result<std::shared_ptr<const Document>>(bytes.diagnostics());      \
        }                                                                                  \
        auto document = decode_typed<Document>(bytes.value(), entry->logical_path);         \
        if (!document) return document;                                                    \
        impl_->cache_map[key] = document.value();                                          \
        return document.value();                                                            \
    }

BULLY_RUNTIME_LOADER(load_model, models, formats::ModelDocument)
BULLY_RUNTIME_LOADER(load_texture, textures, formats::TextureDocument)
BULLY_RUNTIME_LOADER(load_material, materials, formats::MaterialDocument)
BULLY_RUNTIME_LOADER(load_animation, animations, formats::AnimationDocument)
BULLY_RUNTIME_LOADER(load_audio, audio, formats::AudioDocument)
BULLY_RUNTIME_LOADER(load_dialogue, dialogs, formats::DialogueDocument)
BULLY_RUNTIME_LOADER(load_scene, scenes, formats::SceneDocument)
BULLY_RUNTIME_LOADER(load_mission, missions, formats::MissionDocument)

#undef BULLY_RUNTIME_LOADER

void AssetDatabase::clear_caches()
{
    if (!impl_) return;
    impl_->models.clear();
    impl_->textures.clear();
    impl_->materials.clear();
    impl_->animations.clear();
    impl_->audio.clear();
    impl_->dialogs.clear();
    impl_->scenes.clear();
    impl_->missions.clear();
}

AssetDatabase::Statistics AssetDatabase::statistics() const
{
    Statistics stats;
    if (!impl_) return stats;
    stats.mounts = impl_->mounts.size();
    stats.entries = impl_->entries.size();
    stats.decoded = impl_->models.size() + impl_->textures.size() + impl_->materials.size() +
                    impl_->animations.size() + impl_->audio.size() + impl_->dialogs.size() +
                    impl_->scenes.size() + impl_->missions.size();
    for (const auto& mount : impl_->mounts) {
        if (mount.package) stats.bytes_mounted += mount.package->data.size();
    }
    // Directory mounts report their file sizes; package mounts report the size
    // of the embedded data block, so a mounted package is never counted twice.
    std::set<std::string> counted_mounts;
    for (const auto& [path, recorded] : impl_->entries) {
        (void)path;
        const auto& entry = recorded.entry;
        if (entry.file.empty()) continue;
        if (!counted_mounts.insert(entry.mount).second) continue;
        for (const auto& other : impl_->mount_entries) {
            for (const auto& [other_key, other_entry] : other) {
                (void)other_key;
                if (other_entry.mount != entry.mount || other_entry.file.empty()) continue;
                stats.bytes_mounted += other_entry.size;
            }
        }
    }
    return stats;
}

}  // namespace bully::runtime
