// Tisma: B-Rep origin of the meshes imported from STEP (phase 4): face of every triangle, STEP kept in the 3MF,
// tessellation again with the painting reprojected face by face.
#include <catch2/catch_test_macros.hpp>

#include <set>

#include <boost/filesystem/operations.hpp>

#include "libslic3r/CadSource.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/Format/STEP.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleSelector.hpp"

using namespace Slic3r;

// Cube of 12 triangles, two per side, with the side of each triangle as its face id.
static TriangleMesh cube_with_faces(std::vector<int> &face_ids)
{
    TriangleMesh mesh(its_make_cube(10., 10., 10.));
    face_ids.clear();
    for (const stl_triangle_vertex_indices &tri : mesh.its.indices) {
        const Vec3f n = its_face_normal(mesh.its, tri);
        int axis = 0;
        for (int i = 1; i < 3; ++ i)
            if (std::abs(n[i]) > std::abs(n[axis]))
                axis = i;
        face_ids.push_back(axis * 2 + (n[axis] > 0.f ? 1 : 0));
    }
    return mesh;
}

// Loads the STEP fixture, or returns nullptr when the STEP library cannot be loaded next to the test executable.
static Model *load_fixture(Model &model)
{
    const std::string path = std::string(TEST_DATA_DIR) + "/block_with_hole.step";
    try {
        if (! load_step(path.c_str(), &model, std::make_pair(0.1, 0.5)))
            return nullptr;
    } catch (const std::exception &ex) {
        WARN("STEP library not available, STEP tests skipped: " << ex.what());
        return nullptr;
    }
    return &model;
}

TEST_CASE("Face ids are stored as runs", "[CadSource]")
{
    const std::vector<int> ids { 0, 0, 0, 1, 1, -1, 2, 2, 2, 2, 0 };
    const std::string      str = cad_face_ids_to_string(ids);
    CHECK(str == "0*3 1*2 -1*1 2*4 0*1");
    std::vector<int> back;
    REQUIRE(cad_face_ids_from_string(str, back));
    CHECK(back == ids);
    CHECK(! cad_face_ids_from_string("3*x", back));
    CHECK(! cad_face_ids_from_string("7", back));
}

TEST_CASE("Painting is reprojected by face", "[CadSource]")
{
    std::vector<int> old_ids;
    const TriangleMesh old_mesh = cube_with_faces(old_ids);

    // Face of the first triangle painted completely, one of the two triangles of another face painted.
    TriangleSelector old_sel(old_mesh);
    const int full_face = old_ids[0];
    int       half_face = -1, half_tri = -1;
    for (size_t i = 0; i < old_ids.size(); ++ i) {
        if (old_ids[i] == full_face)
            old_sel.set_facet(int(i), TriangleStateType::ENFORCER);
        else if (half_face == -1) {
            half_face = old_ids[i];
            half_tri  = int(i);
            old_sel.set_facet(int(i), TriangleStateType::BLOCKER);
        }
    }

    // The new tessellation has the triangles in the opposite order.
    indexed_triangle_set new_its = old_mesh.its;
    std::reverse(new_its.indices.begin(), new_its.indices.end());
    std::vector<int> new_ids(old_ids.rbegin(), old_ids.rend());
    const TriangleMesh new_mesh(std::move(new_its));

    TriangleSelector new_sel(new_mesh);
    CadRemapStats    stats;
    cad_remap_painting(old_mesh, old_ids, old_sel.serialize(), new_mesh, new_ids, new_sel, &stats);
    CHECK(stats.faces_exact == 1);
    CHECK(stats.faces_approximate == 1);
    CHECK(stats.triangles_unmatched == 0);

    // Read back the state of every new triangle.
    std::vector<TriangleStateType> state(new_ids.size(), TriangleStateType::NONE);
    for (const TriangleSelector::LeafTriangle &leaf : new_sel.get_leaf_triangles())
        state[leaf.source_triangle] = leaf.state;
    const int n = int(new_ids.size());
    for (int i = 0; i < n; ++ i) {
        const int old_idx = n - 1 - i;
        if (new_ids[i] == full_face)
            CHECK(state[i] == TriangleStateType::ENFORCER);
        else if (old_idx == half_tri)
            CHECK(state[i] == TriangleStateType::BLOCKER);
        else
            CHECK(state[i] == TriangleStateType::NONE);
    }
}

TEST_CASE("STEP import keeps the face of every triangle", "[CadSource]")
{
    Model model;
    if (! load_fixture(model))
        return;
    REQUIRE(model.objects.size() == 1);
    REQUIRE(model.objects.front()->volumes.size() == 1);
    const ModelVolume &volume = *model.objects.front()->volumes.front();
    REQUIRE(volume.has_cad_source());
    const CadSource &cad = *volume.cad_source;
    CHECK(cad.face_count == 7);     // 6 sides of the block and the hole.
    CHECK(cad.brep_valid);
    CHECK(cad.step->name == "block_with_hole.step");
    CHECK(cad.face_ids.size() == volume.mesh().its.indices.size());
    const std::set<int> faces(cad.face_ids.begin(), cad.face_ids.end());
    CHECK(faces.size() == 7);
    CHECK(*faces.begin() == 0);
    CHECK(*faces.rbegin() == 6);
}

TEST_CASE("Tessellate again keeps placement and painting", "[CadSource]")
{
    Model model;
    if (! load_fixture(model))
        return;
    ModelVolume &volume = *model.objects.front()->volumes.front();

    // Move the mesh, as the centering after the import does.
    {
        TriangleMesh moved = volume.mesh();
        moved.translate(-10.f, -10.f, -5.f);
        volume.set_mesh(std::move(moved));
    }
    const BoundingBoxf3 bbox_before = volume.mesh().bounding_box();

    // Paint the whole top face (the triangles with all their vertices at the top).
    const indexed_triangle_set &its = volume.mesh().its;
    const float top_z = float(bbox_before.max.z()) - 0.01f;
    INFO("bbox " << bbox_before.min.transpose() << " / " << bbox_before.max.transpose());
    int top_face = -1;
    for (size_t i = 0; i < its.indices.size(); ++ i)
        if (its.vertices[its.indices[i][0]].z() > top_z && its.vertices[its.indices[i][1]].z() > top_z &&
            its.vertices[its.indices[i][2]].z() > top_z) {
            top_face = volume.cad_source->face_ids[i];
            break;
        }
    REQUIRE(top_face >= 0);
    {
        TriangleSelector sel(volume.mesh());
        for (size_t i = 0; i < its.indices.size(); ++ i)
            if (volume.cad_source->face_ids[i] == top_face)
                sel.set_facet(int(i), TriangleStateType::ENFORCER);
        volume.supported_facets.set(sel);
    }
    const size_t triangles_before = its.indices.size();

    CadRemapStats stats;
    std::string   error;
    REQUIRE(cad_retessellate_volume(volume, 0.01, 0.1, &stats, error));
    CHECK(error.empty());
    CHECK(volume.mesh().its.indices.size() > triangles_before);
    CHECK(volume.has_cad_source());
    CHECK(volume.cad_source->linear_deflection == 0.01);
    CHECK(stats.faces_exact == 1);

    const BoundingBoxf3 bbox_after = volume.mesh().bounding_box();
    CHECK((bbox_after.min - bbox_before.min).norm() < 1e-3);
    CHECK((bbox_after.max - bbox_before.max).norm() < 1e-3);

    // Exactly the triangles of the top face are painted.
    TriangleSelector sel(volume.mesh());
    sel.deserialize(volume.supported_facets.get_data());
    std::vector<TriangleStateType> state(volume.mesh().its.indices.size(), TriangleStateType::NONE);
    for (const TriangleSelector::LeafTriangle &leaf : sel.get_leaf_triangles())
        state[leaf.source_triangle] = leaf.state;
    for (size_t i = 0; i < state.size(); ++ i)
        CHECK((state[i] == TriangleStateType::ENFORCER) == (volume.cad_source->face_ids[i] == top_face));

    // An edited mesh is refused.
    {
        TriangleMesh edited = volume.mesh();
        edited.its.vertices.front() += Vec3f(1.f, 0.f, 0.f);
        volume.set_mesh(std::move(edited));
    }
    CHECK(! cad_retessellate_volume(volume, 0.1, 0.5, nullptr, error));
}

TEST_CASE("STEP and face ids are kept in the 3MF", "[CadSource]")
{
    Model model;
    if (! load_fixture(model))
        return;
    model.add_default_instances();
    const ModelVolume &src = *model.objects.front()->volumes.front();

    const std::string path = (boost::filesystem::temp_directory_path() / "tisma_cad_test.3mf").string();
    REQUIRE(store_3mf(path.c_str(), &model, nullptr, false));

    Model                     loaded;
    DynamicPrintConfig        config;
    ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
    boost::optional<Semver>   version;
    REQUIRE(load_3mf(path.c_str(), config, ctxt, &loaded, false, version));
    boost::filesystem::remove(path);

    REQUIRE(loaded.objects.size() == 1);
    const ModelVolume &dst = *loaded.objects.front()->volumes.front();
    REQUIRE(dst.has_cad_source());
    CHECK(dst.cad_source->face_ids == src.cad_source->face_ids);
    CHECK(dst.cad_source->step->data == src.cad_source->step->data);
    CHECK(dst.cad_source->step->name == src.cad_source->step->name);
    CHECK(dst.cad_source->solid_index == src.cad_source->solid_index);
    CHECK(dst.cad_source->face_count == 7);
}
