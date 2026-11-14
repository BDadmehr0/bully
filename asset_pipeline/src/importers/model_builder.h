// Shared helpers for importers that produce .bmodel documents.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "bully/formats/model.h"
#include "bully/pipeline/importer.h"

namespace bully::pipeline {

/// Intermediate, importer-agnostic mesh used while converting source data.
struct RawMesh {
    std::string name;
    std::vector<formats::ModelVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::string material_name;
};

struct RawModel {
    std::vector<RawMesh> meshes;
    std::vector<formats::MaterialBinding> materials;
    std::vector<formats::AnimationReference> animations;
    std::optional<formats::Skeleton> skeleton;
    std::vector<std::string> warnings;
};

namespace model_builder {

/// Computes per-vertex normals when the source provides none.
void compute_normals(RawMesh& mesh);

/// Recomputes a mesh's AABB.
void update_bounds(formats::ModelMesh& mesh);

/// Real quadric-free vertex welding: merges vertices with identical
/// position/normal/uv within `epsilon` and remaps indices. Returns the number
/// of removed vertices.
std::uint32_t weld_vertices(RawMesh& mesh, float epsilon = 1e-5f);

/// LOD generation by triangle-decimation using vertex clustering:
/// quantise positions to a grid of `cell_size`, collapse coincident clusters,
/// drop degenerate triangles. Produces a genuinely smaller mesh (used by the
/// retro toolkit's LOD pipeline). Returns diagnostics if the mesh is already
/// minimal.
formats::ModelMesh build_lod(const formats::ModelMesh& source, float cell_size, std::uint8_t lod);

/// Converts a RawMesh into a runtime ModelMesh (applying welding and bounds).
formats::ModelMesh finalize_mesh(const RawMesh& raw, std::uint16_t material_slot,
                                 bool skinned, bool weld);

/// Assembles a complete ModelDocument from raw import data.
formats::ModelDocument assemble(const ImportContext& context, RawModel&& raw,
                                const formats::ModelBuildProfile& profile,
                                AssetType asset_type = AssetType::Model);

/// Serialises a document and wraps it into an ImportResult.
ImportResult finish(const formats::ModelDocument& document, core::DiagnosticList diagnostics,
                    std::string summary);

}  // namespace model_builder
}  // namespace bully::pipeline
