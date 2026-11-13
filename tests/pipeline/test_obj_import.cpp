#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "bully/formats/model.h"
#include "bully/pipeline/builtin_importers.h"
#include "bully/pipeline/importer.h"
#include "fixture.h"
#include "test_support.h"

using namespace bully;
using namespace bully::pipeline;

namespace {

ImportContext make_context(test::TempDir& dir, std::string relative, std::string asset_id)
{
    ImportContext context;
    context.project_root = dir.str();
    context.relative_source_path = relative;
    context.source_path = (dir.path() / relative).string();
    context.output_path = (dir.path() / "built" / "out.bmodel").string();
    context.asset_id = std::move(asset_id);
    return context;
}

}  // namespace

TEST_CASE("OBJ parser handles quads, material libraries and comments", "[pipeline][obj]")
{
    auto parsed = importers::parse_obj(test::cube_obj());
    REQUIRE(parsed.has_value());
    const auto& document = parsed.value();
    REQUIRE(document.meshes.size() == 1);
    // The parser emits one vertex per unique (position, uv, normal) tuple, so
    // the cube expands to more vertices than the 8 positions it declares.
    CHECK(document.meshes[0].positions.size() % 3 == 0);
    CHECK(document.meshes[0].positions.size() / 3 == 20);
    CHECK(document.meshes[0].uvs.size() == document.meshes[0].positions.size() / 3 * 2);
    CHECK(document.meshes[0].indices.size() == 6 * 6);  // six quads -> 12 triangles
    CHECK(document.mtllib == "cube.mtl");
    REQUIRE(document.meshes[0].material_names.size() == 1);
    CHECK(document.meshes[0].material_names[0] == "cube_mat");
}

TEST_CASE("OBJ parser triangulates polygons as a fan", "[pipeline][obj]")
{
    auto parsed = importers::parse_obj(test::relative_index_obj());
    REQUIRE(parsed.has_value());
    const auto& mesh = parsed.value().meshes[0];
    CHECK(mesh.indices.size() == 6);
    CHECK(mesh.indices[0] == 0);
    CHECK(mesh.indices[1] == 1);
    CHECK(mesh.indices[2] == 2);
    CHECK(mesh.indices[3] == 0);
    CHECK(mesh.indices[4] == 2);
    CHECK(mesh.indices[5] == 3);
}

TEST_CASE("OBJ parser reports malformed records as diagnostics", "[pipeline][obj]")
{
    // A malformed vertex record is skipped with a warning, and the parser stays
    // usable: the faces that follow still import.
    auto parsed = importers::parse_obj("v 0 0\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    REQUIRE(parsed.has_value());
    REQUIRE_FALSE(parsed.value().warnings.empty());
    CHECK(parsed.value().meshes.size() == 1);
    CHECK(parsed.value().meshes[0].indices.size() == 3);
}

TEST_CASE("OBJ parser rejects files it cannot turn into geometry", "[pipeline][obj]")
{
    // No vertices at all: a hard error, never an empty model.
    CHECK_FALSE(importers::parse_obj("").has_value());
    // Vertices but no faces: also a hard error, with a specific code.
    auto no_faces = importers::parse_obj("v 0 0 0\nv 1 0 0\n");
    CHECK_FALSE(no_faces.has_value());
    CHECK(no_faces.diagnostics().has_errors());
    // A face referencing a vertex that does not exist is dropped with a warning
    // and the document still reports no usable faces.
    auto out_of_range = importers::parse_obj("v 0 0 0\nf 1 2 3\n");
    CHECK_FALSE(out_of_range.has_value());
}

TEST_CASE("OBJ importer produces a compiled bmodel", "[pipeline][obj]")
{
    test::TempDir dir;
    auto importer = make_obj_importer();
    REQUIRE(importer != nullptr);
    CHECK(importer->extensions() == std::vector<std::string>{".obj"});
    CHECK(importer->produces() == AssetType::Model);

    auto path = dir.path() / "assets/models/crate.obj";
    REQUIRE(core::FileSystem::write_text(path, test::cube_obj()).has_value());
    auto context = make_context(dir, "assets/models/crate.obj", "obj-asset-id");
    auto result = importer->import(context);
    REQUIRE(result.has_value());
    CHECK_FALSE(result.value().output_bytes.empty());
    CHECK(result.value().asset_type == AssetType::Model);

    formats::ModelDocument document;
    auto decoded = document.deserialize(result.value().output_bytes);
    REQUIRE(decoded.has_value());
    const auto& model = decoded.value();
    CHECK(model.asset_id == "obj-asset-id");
    CHECK(model.source_path == "assets/models/crate.obj");
    CHECK(model.source_format == "obj");
    // Provenance records the importer that produced the document, even when the
    // importer is driven directly instead of through the pipeline.
    CHECK(model.importer == importer->id());
    CHECK(model.source_format == "obj");
    REQUIRE(model.meshes.size() == 1);
    CHECK(model.compute_statistics().triangle_count == 12);
    CHECK(model.compute_statistics().vertex_count > 0);
    CHECK(model.validate().has_value());
    CHECK(result.value().summary.find("12") != std::string::npos);
}

TEST_CASE("OBJ importer never writes to the source file", "[pipeline][obj]")
{
    test::TempDir dir;
    const std::string source = "assets/models/keep.obj";
    const auto path = dir.path() / source;
    REQUIRE(core::FileSystem::write_text(path, test::cube_obj()).has_value());
    const auto before = core::FileSystem::hash_file(path).value();
    auto importer = make_obj_importer();
    REQUIRE(importer->import(make_context(dir, source, "id")).has_value());
    CHECK(core::FileSystem::hash_file(path).value() == before);
}

TEST_CASE("OBJ importer honours the scale and up-axis settings", "[pipeline][obj]")
{
    test::TempDir dir;
    REQUIRE(core::FileSystem::write_text(dir.path() / "tri.obj", test::relative_index_obj())
                .has_value());
    auto importer = make_obj_importer();
    auto context = make_context(dir, "tri.obj", "id");
    context.settings.set("scale", "2.0");
    auto scaled = importer->import(context);
    REQUIRE(scaled.has_value());
    formats::ModelDocument scaled_doc;
    auto decoded = scaled_doc.deserialize(scaled.value().output_bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().bounds.max.x == Catch::Approx(2.0f).margin(1e-5));

    context.settings.set("scale", "1.0");
    context.settings.set("up_axis", "z");
    auto rotated = importer->import(context);
    REQUIRE(rotated.has_value());
    formats::ModelDocument rotated_doc;
    auto decoded_rotated = rotated_doc.deserialize(rotated.value().output_bytes);
    REQUIRE(decoded_rotated.has_value());
    // Z-up source: the authored +Y extent becomes -Z after the rotation.
    CHECK(decoded_rotated.value().bounds.max.y == Catch::Approx(0.0f).margin(1e-5));
}

TEST_CASE("OBJ importer reports a missing source instead of an empty model", "[pipeline][obj]")
{
    test::TempDir dir;
    auto importer = make_obj_importer();
    auto result = importer->import(make_context(dir, "missing.obj", "id"));
    CHECK_FALSE(result.has_value());
    CHECK(result.diagnostics().has_errors());
}

TEST_CASE("OBJ importer registers material dependencies", "[pipeline][obj]")
{
    test::TempDir dir;
    REQUIRE(core::FileSystem::write_text(dir.path() / "with_mtl.obj", test::cube_obj()).has_value());
    REQUIRE(core::FileSystem::write_text(dir.path() / "cube.mtl",
                                         "newmtl cube_mat\nKd 0.5 0.5 0.5\n")
                .has_value());
    auto importer = make_obj_importer();
    auto result = importer->import(make_context(dir, "with_mtl.obj", "id"));
    REQUIRE(result.has_value());
    bool saw_mtl = false;
    for (const auto& dependency : result.value().dependencies) {
        if (dependency.reason.find("mtl") != std::string::npos) saw_mtl = true;
    }
    CHECK(saw_mtl);
}

TEST_CASE("Pipeline builds an OBJ asset end to end", "[pipeline][obj][integration]")
{
    test::ProjectFixture fixture("obj-project");
    const std::string source = fixture.write_asset("models/crate.obj", test::cube_obj());
    const auto report = fixture.build_all();
    REQUIRE(report.total == 1);
    CHECK(report.success());
    CHECK(report.built == 1);

    const auto record = fixture.record_for(source);
    REQUIRE(record.has_value());
    CHECK(record->status == BuildStatus::UpToDate);
    CHECK(record->runtime_path.find("built/models/") != std::string::npos);
    CHECK(record->output_hash.size() == 16);

    const auto bytes = fixture.read_compiled(source);
    formats::ModelDocument document;
    auto decoded = document.deserialize(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().asset_id == record->asset_id);
}
