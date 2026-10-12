// Tisma Slicer (phase 8): the visibility of the seam is cached between G-code exports.
#include <catch2/catch_test_macros.hpp>

#include <libslic3r/GCode/ModelVisibility.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/TriangleMesh.hpp>

#include <chrono>

using namespace Slic3r;

namespace {

ModelInfo::Visibility::Params visibility_params()
{
    ModelInfo::Visibility::Params params;
    params.raycasting_visibility_samples_count  = 3000;
    params.fast_decimation_triangle_count_target = 16000;
    params.sqr_rays_per_sample_point             = 5;
    return params;
}

bool same_visibility(const ModelInfo::Visibility &a, const ModelInfo::Visibility &b)
{
    return a.mesh_samples.positions == b.mesh_samples.positions && a.mesh_samples.normals == b.mesh_samples.normals &&
           a.mesh_samples_visibility == b.mesh_samples_visibility && a.mesh_samples_radius == b.mesh_samples_radius;
}

} // namespace

TEST_CASE("Seam visibility is cached and invalidated", "[Seams][TismaCache]")
{
    Model model;
    ModelObject *object = model.add_object();
    // An L shaped part: the inner corner is partially hidden.
    indexed_triangle_set mesh = its_make_cube(20., 20., 10.);
    indexed_triangle_set tower = its_make_cube(5., 20., 20.);
    its_merge(mesh, tower);
    object->add_volume(TriangleMesh(mesh));
    const Transform3d trafo = Transform3d::Identity();
    const auto no_cancel = []() {};

    const ModelInfo::Visibility first(trafo, object->volumes, visibility_params(), no_cancel);
    const ModelInfo::Visibility second(trafo, object->volumes, visibility_params(), no_cancel);
    CHECK(same_visibility(first, second));
    // The inner corner makes some samples less visible.
    float min_visibility = 1.f;
    for (float v : first.mesh_samples_visibility)
        min_visibility = std::min(min_visibility, v);
    CHECK(min_visibility < 1.f);
    // The cached KD tree works as the computed one.
    CHECK(first.calculate_point_visibility(Vec3f(10.f, 10.f, 10.f)) == second.calculate_point_visibility(Vec3f(10.f, 10.f, 10.f)));

    // Another transformation of the object: computed again.
    const Transform3d scaled = Geometry::scale_transform(Vec3d(2., 1., 1.));
    const ModelInfo::Visibility third(scaled, object->volumes, visibility_params(), no_cancel);
    CHECK(third.mesh_samples_radius != first.mesh_samples_radius);

    // Another mesh: computed again.
    object->volumes.front()->set_mesh(TriangleMesh(its_make_cube(20., 20., 10.)));
    const ModelInfo::Visibility fourth(trafo, object->volumes, visibility_params(), no_cancel);
    CHECK(! same_visibility(first, fourth));
}
