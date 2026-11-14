// Bully Studio - asset identity, types and build state.
//
// Every asset tracked by the pipeline has a stable UUID that survives renames
// and re-imports, a source path (never modified by the pipeline), import
// settings, a dependency list, a content hash, a build status and a runtime
// output path. These fields map 1:1 onto the `assets` table in the project
// database and onto rows in every build report.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/core/json.h"

namespace bully::core::json {}  // fwd: json lives in bully::core::json

namespace bully::pipeline {

/// Broad asset categories. Specific importers live in the registry keyed by
/// extension, so this enum only describes pipeline behaviour.
enum class AssetType : std::uint8_t {
    Unknown = 0,
    Model,
    Texture,
    Material,
    Animation,
    Audio,
    Dialogue,
    Scene,
    Mission,
    Story,
    Font,
    Script,
    Other = 255,
};

[[nodiscard]] std::string_view to_string(AssetType type) noexcept;
[[nodiscard]] AssetType asset_type_from_string(std::string_view text) noexcept;
/// Preferred runtime extension for an asset type (".bmodel", ...).
[[nodiscard]] std::string_view runtime_extension(AssetType type) noexcept;

enum class BuildStatus : std::uint8_t { Unknown = 0, UpToDate, Pending, Building, Failed, Skipped };

[[nodiscard]] std::string_view to_string(BuildStatus status) noexcept;
[[nodiscard]] BuildStatus build_status_from_string(std::string_view text) noexcept;

/// One import setting value typed by the importer's declared schema.
struct ImportSetting {
    std::string key;
    std::string value;  // JSON scalar rendered as text ("true", "0.5", "\"fast\"")
};

/// Per-asset import configuration, persisted in the project database.
struct ImportSettings {
    /// Importer id that owns this asset (e.g. "com.bully.importer.obj").
    std::string importer_id;
    std::vector<ImportSetting> values;
    /// Opaque JSON blob for importer-specific structured settings.
    std::string options_json = "{}";

    [[nodiscard]] std::string get(std::string_view key, std::string fallback = {}) const;
    [[nodiscard]] bool get_bool(std::string_view key, bool fallback = false) const;
    [[nodiscard]] double get_number(std::string_view key, double fallback = 0.0) const;
    void set(std::string key, std::string value);

    [[nodiscard]] core::json::Value to_json() const;
    static ImportSettings from_json(const core::json::Value& value);
};

/// A dependency edge: this asset needs `asset_id` to build/validate.
struct AssetDependency {
    std::string asset_id;
    /// "hard" dependencies block a build; "soft" ones only warn.
    bool required = true;
    std::string reason;
};

struct AssetRecord {
    std::string asset_id;          // stable UUID string
    std::string name;              // display name ("Missy - school uniform")
    AssetType type = AssetType::Unknown;
    std::string source_path;       // project-relative, forward slashes
    std::string runtime_path;      // project-relative output for the built asset
    ImportSettings settings;
    std::vector<AssetDependency> dependencies;
    std::string version;           // importer/exporter version that produced the output
    std::string source_hash;       // hex hash of the source file bytes
    std::string settings_hash;     // hex hash of the serialised import settings
    std::string output_hash;       // hex hash of the compiled artifact
    BuildStatus status = BuildStatus::Unknown;
    std::string last_error;
    std::int64_t last_built_unix = 0;
    std::int64_t modified_unix = 0;
    /// Free-form metadata (tags, author, notes) - JSON object.
    std::string metadata_json = "{}";

    [[nodiscard]] core::json::Value to_json() const;
    static AssetRecord from_json(const core::json::Value& value);

    /// Hash over source hash + settings hash + importer version. Used for
    /// cache lookups and incremental rebuild decisions.
    [[nodiscard]] std::string build_key() const;
};

/// Computes the hex FNV-1a hash of a byte buffer (same primitive the
/// filesystem layer uses, exposed for pipeline callers).
[[nodiscard]] std::string hash_hex(const std::vector<std::uint8_t>& bytes);
[[nodiscard]] std::string hash_string_hex(std::string_view text);

}  // namespace bully::pipeline
