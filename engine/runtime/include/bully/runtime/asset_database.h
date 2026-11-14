// Bully Studio runtime - asset database.
//
// The runtime consumes only compiled `.b*` assets. It can read them from two
// places:
//
//   * a **project directory** (`<project>/built/`), which is how the editor and
//     the developer loop work: assets can be rebuilt and re-read without
//     packaging anything,
//   * a **.bpackage**, which is what ships: one file with a table of contents,
//     per-entry CRCs and no loose files on disk.
//
// Both lookups go through the same API and the same typed loaders, so a missing
// asset fails identically in the editor and in a shipped build.
//
// Diagnostic codes: BULLY-RT-0001..0019
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/formats/animation.h"
#include "bully/formats/audio.h"
#include "bully/formats/dialogue.h"
#include "bully/formats/material.h"
#include "bully/formats/mission.h"
#include "bully/formats/model.h"
#include "bully/formats/package.h"
#include "bully/formats/scene.h"
#include "bully/formats/texture.h"

namespace bully::runtime {

/// The resolved asset categories the runtime knows about.
enum class AssetKind : std::uint8_t {
    Model = 0,
    Texture = 1,
    Material = 2,
    Animation = 3,
    Audio = 4,
    Dialogue = 5,
    Scene = 6,
    Mission = 7,
    Unknown = 255,
};

[[nodiscard]] std::string_view to_string(AssetKind kind) noexcept;

/// Classifies an asset from its extension. Unknown extensions stay Unknown so a
/// caller can report them instead of guessing.
[[nodiscard]] AssetKind kind_from_extension(std::string_view extension) noexcept;

/// One resolvable resource. `asset_id` may be empty for a directory mount until
/// the file has been decoded once.
struct AssetEntry {
    std::string asset_id;
    std::string logical_path;  // "models/crate.bmodel"
    AssetKind kind = AssetKind::Unknown;
    std::string mount;         // mount name ("project", package name, ...)
    std::filesystem::path file;  // directory mounts only
    std::uint32_t size = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return !logical_path.empty() && kind != AssetKind::Unknown;
    }
};

struct MountInfo {
    std::string name;
    bool is_package = false;
    std::filesystem::path path;
    std::uint32_t entry_count = 0;
};

class AssetDatabase {
public:
    AssetDatabase();
    ~AssetDatabase();
    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;

    /// Mounts every compiled asset in `built_dir`. Later mounts win over earlier
    /// ones, which is how a DLC package or a hot-reloaded override works.
    core::Status mount_directory(const std::filesystem::path& built_dir,
                                 std::string mount_name = "project");

    /// Mounts a `.bpackage`. Every entry is checked (CRC) before the mount is
    /// accepted, so a corrupt package is refused instead of failing later.
    core::Status mount_package(const std::filesystem::path& package_path);
    core::Status mount_package(const std::filesystem::path& package_path, std::string mount_name);

    void unmount(std::string_view mount_name);

    [[nodiscard]] std::vector<MountInfo> mounts() const;
    [[nodiscard]] std::size_t entry_count() const;

    /// Resolves an asset id (UUID) or a logical path ("models/crate.bmodel").
    [[nodiscard]] std::optional<AssetEntry> find(std::string_view id_or_path) const;
    [[nodiscard]] bool contains(std::string_view id_or_path) const { return find(id_or_path).has_value(); }

    /// Every entry of a kind, ordered by logical path (deterministic).
    [[nodiscard]] std::vector<AssetEntry> entries(AssetKind kind = AssetKind::Unknown) const;

    /// Raw bytes of an asset, CRC-checked for package mounts.
    [[nodiscard]] core::Result<std::vector<std::uint8_t>> read(std::string_view id_or_path) const;

    /// Typed loaders: read, decode, validate and cache. Repeated loads of the
    /// same asset return the same shared document.
    [[nodiscard]] core::Result<std::shared_ptr<const formats::ModelDocument>> load_model(
        std::string_view id_or_path) const;
    [[nodiscard]] core::Result<std::shared_ptr<const formats::TextureDocument>> load_texture(
        std::string_view id_or_path) const;
    [[nodiscard]] core::Result<std::shared_ptr<const formats::MaterialDocument>> load_material(
        std::string_view id_or_path) const;
    [[nodiscard]] core::Result<std::shared_ptr<const formats::AnimationDocument>> load_animation(
        std::string_view id_or_path) const;
    [[nodiscard]] core::Result<std::shared_ptr<const formats::AudioDocument>> load_audio(
        std::string_view id_or_path) const;
    [[nodiscard]] core::Result<std::shared_ptr<const formats::DialogueDocument>> load_dialogue(
        std::string_view id_or_path) const;
    [[nodiscard]] core::Result<std::shared_ptr<const formats::SceneDocument>> load_scene(
        std::string_view id_or_path) const;
    [[nodiscard]] core::Result<std::shared_ptr<const formats::MissionDocument>> load_mission(
        std::string_view id_or_path) const;

    /// Drops every decoded document (used when a developer rebuilds assets in
    /// place). Mounts are kept.
    void clear_caches();

    struct Statistics {
        std::size_t mounts = 0;
        std::size_t entries = 0;
        std::size_t decoded = 0;      // documents currently cached
        std::uint64_t bytes_mounted = 0;
    };
    [[nodiscard]] Statistics statistics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace bully::runtime
