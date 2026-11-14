// Model exporter: .bmodel -> Wavefront OBJ (+ MTL).
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>

#include "bully/exporters/exporters.h"

namespace bully::exporters {

namespace {

/// Formats a float the way OBJ consumers expect: fixed notation, no locale
/// dependence, trailing noise trimmed.
std::string number(float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6f", static_cast<double>(value));
    std::string text(buffer);
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    if (text == "-0") return "0";
    return text;
}

}  // namespace

core::Result<ObjExport> encode_obj(const formats::ModelDocument& model)
{
    core::DiagnosticList diagnostics;
    if (model.meshes.empty()) {
        diagnostics.error("BULLY-EXP-0020", "model has no meshes", model.name);
        return core::Result<ObjExport>(std::move(diagnostics));
    }
    ObjExport result;
    std::string& obj = result.obj;
    obj += "# Exported by Bully Studio from " + model.name + "\n";
    obj += "# asset_id " + model.asset_id + "\n";
    std::size_t dropped_lods = 0;
    if (!model.materials.empty()) {
        result.material_library = model.name.empty() ? "materials.mtl" : model.name + ".mtl";
        obj += "mtllib " + result.material_library + "\n";

        std::string& mtl = result.mtl;
        mtl += "# Exported by Bully Studio\n";
        for (const auto& material : model.materials) {
            mtl += "newmtl " + (material.slot_name.empty() ? "slot" : material.slot_name) + "\n";
            mtl += "Kd 1 1 1\n";
            mtl += "Ka 0.2 0.2 0.2\n";
            if (!material.material_asset.empty()) {
                mtl += "# material asset " + material.material_asset + "\n";
            }
            if (!material.shader_preset.empty()) {
                mtl += "# shader preset " + material.shader_preset + "\n";
            }
        }
    }

    // OBJ indices are 1-based and global across the file.
    std::size_t base = 1;
    for (const auto& mesh : model.meshes) {
        // Only LOD0 is exported: lower LODs are derived data and re-exporting
        // them would double the geometry with no way to tell them apart.
        if (mesh.lod != 0) {
            ++dropped_lods;
            continue;
        }
        obj += "\no " + (mesh.name.empty() ? std::string("mesh") : mesh.name) + "\n";
        if (mesh.material_slot != 0xFFFF && mesh.material_slot < model.materials.size()) {
            const auto& material = model.materials[mesh.material_slot];
            obj += "usemtl " + (material.slot_name.empty() ? "slot" : material.slot_name) + "\n";
        }
        for (const auto& vertex : mesh.vertices) {
            obj += "v " + number(vertex.position.x) + " " + number(vertex.position.y) + " " +
                   number(vertex.position.z) + "\n";
        }
        for (const auto& vertex : mesh.vertices) {
            obj += "vt " + number(vertex.uv0.x) + " " + number(vertex.uv0.y) + "\n";
        }
        for (const auto& vertex : mesh.vertices) {
            obj += "vn " + number(vertex.normal.x) + " " + number(vertex.normal.y) + " " +
                   number(vertex.normal.z) + "\n";
        }
        const bool triangles = mesh.indices.size() >= 3 && mesh.indices.size() % 3 == 0;
        if (!triangles) {
            diagnostics.warn("BULLY-EXP-0021",
                             "mesh '" + mesh.name + "' is not triangle-indexed and was written "
                             "as points",
                             model.name);
            for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
                obj += "p " + std::to_string(base + i) + "\n";
            }
            base += mesh.vertices.size();
            continue;
        }
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            obj += "f";
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const std::size_t index = base + mesh.indices[i + corner];
                // v/vt/vn share the index because the compiler splits vertices.
                obj += " " + std::to_string(index) + "/" + std::to_string(index) + "/" +
                       std::to_string(index);
            }
            obj += "\n";
        }
        base += mesh.vertices.size();
    }

    if (dropped_lods > 0) {
        diagnostics.warn("BULLY-EXP-0022",
                         std::to_string(dropped_lods) + " derived LOD mesh(es) were not exported",
                         model.name);
    }
    core::Result<ObjExport> exported(std::move(result));
    exported.attach_diagnostics(std::move(diagnostics));
    return exported;
}

}  // namespace bully::exporters
