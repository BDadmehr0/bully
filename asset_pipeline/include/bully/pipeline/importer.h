// Bully Studio - importer architecture.
//
// Importers are registered, not hardcoded: the pipeline asks the registry
// "who handles .obj?" and never contains a switch on extension. Native
// importers ship with the platform; plugins register more through the plugin
// API, and script importers can be contributed by the automation layer.
//
// Contract:
//   * An importer never writes to the source asset.
//   * An importer reports failures via core::Result; the pipeline converts a
//     failure into a Failed build status with diagnostics - imports never fail
//     silently.
//   * Importers declare their supported extensions and import setting schema so
//     the Studio's Properties panel can render controls automatically.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/pipeline/asset.h"

namespace bully::pipeline {

/// Declares one import setting so UIs can build generic controls.
struct ImportSettingDescriptor {
    enum class Kind : std::uint8_t { Bool, Int, Float, Text, Enum, FilePath, Color };

    std::string key;
    std::string label;
    Kind kind = Kind::Text;
    std::string default_value;
    std::string help;
    std::vector<std::string> enum_values;  // for Kind::Enum
    double minimum = 0.0;
    double maximum = 0.0;
};

/// Factories for import setting descriptors. Using these avoids partially
/// initialised aggregates and keeps importer schemas readable.
namespace settings {
[[nodiscard]] ImportSettingDescriptor boolean(std::string key, std::string label,
                                              bool default_value, std::string help = {});
[[nodiscard]] ImportSettingDescriptor integer(std::string key, std::string label,
                                              std::int64_t default_value, double minimum,
                                              double maximum, std::string help = {});
[[nodiscard]] ImportSettingDescriptor number(std::string key, std::string label,
                                             double default_value, double minimum, double maximum,
                                             std::string help = {});
[[nodiscard]] ImportSettingDescriptor text(std::string key, std::string label,
                                          std::string default_value, std::string help = {});
[[nodiscard]] ImportSettingDescriptor enumeration(std::string key, std::string label,
                                                 std::string default_value,
                                                 std::vector<std::string> values,
                                                 std::string help = {});
[[nodiscard]] ImportSettingDescriptor file_path(std::string key, std::string label,
                                               std::string help = {});
}  // namespace settings

/// Everything an importer needs to produce a runtime asset.
struct ImportContext {
    /// Absolute path of the project root.
    std::string project_root;
    /// Absolute path of the source file (already existence-checked).
    std::string source_path;
    /// Project-relative source path used for provenance inside compiled assets.
    std::string relative_source_path;
    /// Absolute output path for the compiled asset.
    std::string output_path;
    /// Stable asset id assigned by the pipeline.
    std::string asset_id;
    /// Import settings (defaults merged with the project's overrides).
    ImportSettings settings;
    /// Progress callback in [0,1] plus a status message. Optional.
    std::function<void(float, std::string_view)> report_progress = {};
    /// Resolves an authored reference (a source path, a file name or a display
    /// name) to a registered asset id. Importers call this for cross-asset
    /// references - scene entities pointing at a model, missions pointing at
    /// scenes - so the resulting assets carry real dependency edges instead of
    /// free-form strings. Optional: tests and direct importer calls leave it
    /// empty and references stay as authored.
    std::function<std::string(const std::string&)> resolve_asset = {};
};

struct ImportResult {
    /// Bytes to write to `ImportContext::output_path`. Empty means the importer
    /// decided to skip writing (status becomes Skipped) and must explain why.
    std::vector<std::uint8_t> output_bytes;
    /// Asset type inferred by the importer.
    AssetType asset_type = AssetType::Unknown;
    /// Dependencies discovered while importing (textures, clips, source files).
    std::vector<AssetDependency> dependencies;
    /// Human-readable build summary (counts, warnings) shown in the Studio.
    std::string summary;
    /// Structured report for the QA dashboard.
    std::string report_json = "{}";
    /// Warnings that do not fail the import.
    core::DiagnosticList diagnostics;
};

class Importer {
public:
    virtual ~Importer() = default;

    /// Stable, reverse-DNS id, e.g. "com.bully.importer.wavefront_obj".
    [[nodiscard]] virtual std::string id() const = 0;
    [[nodiscard]] virtual std::string display_name() const = 0;
    [[nodiscard]] virtual std::string version() const = 0;
    /// Lowercase extensions including the dot: {".obj", ".mtl"}.
    [[nodiscard]] virtual std::vector<std::string> extensions() const = 0;
    [[nodiscard]] virtual AssetType produces() const = 0;
    [[nodiscard]] virtual std::vector<ImportSettingDescriptor> settings_schema() const
    {
        return {};
    }

    /// Performs the import. Implementations must be deterministic for identical
    /// inputs (the cache relies on it).
    [[nodiscard]] virtual core::Result<ImportResult> import(const ImportContext& context) = 0;

    /// Optional fast validation that does not build anything (used by the QA
    /// dashboard and by hot-reload pre-checks).
    [[nodiscard]] virtual core::Status validate_source(const ImportContext& context)
    {
        return import(context).has_value() ? core::Status{}
                                           : core::Status(core::DiagnosticList{});
    }
};

using ImporterFactory = std::function<std::shared_ptr<Importer>()>;

class ImporterRegistry {
public:
    static ImporterRegistry& instance();

    /// Registers a factory. Returns false (and logs) when another importer
    /// already claims the same id.
    bool register_importer(ImporterFactory factory);
    /// Convenience for native importers.
    bool add(std::shared_ptr<Importer> importer);
    bool unregister(std::string_view importer_id);

    [[nodiscard]] std::vector<std::shared_ptr<Importer>> all() const;
    [[nodiscard]] std::shared_ptr<Importer> find(std::string_view importer_id) const;
    /// First importer that claims `extension` (with or without leading dot).
    [[nodiscard]] std::shared_ptr<Importer> for_extension(std::string_view extension) const;
    /// Matches a whole file name against every claimed extension, longest
    /// suffix first, so compound extensions such as ".dialogue.json" win over
    /// ".json".
    [[nodiscard]] std::shared_ptr<Importer> for_filename(std::string_view filename) const;
    /// All extensions claimed by any importer (sorted, deduplicated).
    [[nodiscard]] std::vector<std::string> all_extensions() const;

    /// Registers the importers shipped with Bully Studio.
    void register_builtin();
    /// Drops every registration (used by tests).
    void clear();

private:
    ImporterRegistry() = default;
    std::vector<ImporterFactory> factories_;
};

/// Helpers shared by importer implementations.
namespace importers {

/// Parses a Wavefront OBJ file into a simple in-memory mesh representation.
/// This is the real parser used by the OBJ importer (positions, normals, UVs,
/// faces with negative indices and polygon triangulation).
struct ObjMesh {
    std::string name;
    std::vector<float> positions;  // x,y,z triples
    std::vector<float> normals;    // x,y,z triples
    std::vector<float> uvs;        // u,v pairs
    std::vector<std::uint32_t> indices;
    std::vector<std::string> material_names;
    std::uint32_t face_count = 0;
};

struct ObjMaterial {
    std::string name;
    std::string diffuse_map;
    std::string normal_map;
    float diffuse[3] = {0.8f, 0.8f, 0.8f};
    float specular[3] = {0.0f, 0.0f, 0.0f};
    float alpha = 1.0f;
    float shininess = 0.0f;
};

struct ObjDocument {
    std::vector<ObjMesh> meshes;
    std::vector<ObjMaterial> materials;
    std::string mtllib;
    std::vector<std::string> warnings;
};

/// Parses OBJ text. Returns diagnostics with line numbers on malformed input.
[[nodiscard]] core::Result<ObjDocument> parse_obj(std::string_view text);
/// Parses a .mtl file.
[[nodiscard]] core::Result<std::vector<ObjMaterial>> parse_mtl(std::string_view text);

}  // namespace importers

}  // namespace bully::pipeline
