#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "libslic3r/SLA/Rotfinder.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

using namespace Slic3r;

// Box from (x0, y0, z0) to (x1, y1, z1).
static indexed_triangle_set box(double x0, double y0, double z0, double x1, double y1, double z1)
{
    indexed_triangle_set its = its_make_cube(x1 - x0, y1 - y0, z1 - z0);
    its_translate(its, Vec3f(float(x0), float(y0), float(z0)));
    return its;
}

// The direction of the Z axis of the object once rotated as found.
static Vec3d rotated_z(const Vec2d &rot)
{
    return (Eigen::AngleAxisd(rot.y(), Vec3d::UnitY()) * Eigen::AngleAxisd(rot.x(), Vec3d::UnitX())) * Vec3d::UnitZ();
}

static ModelObject* add_object(Model &model, const indexed_triangle_set &its)
{
    ModelObject *object = model.add_object();
    object->add_volume(TriangleMesh(its));
    object->add_instance();
    return object;
}

TEST_CASE("FFF orientation puts the overhangs on the bed", "[Rotfinder]")
{
    // A mushroom: a stem of 8 x 8 x 20 mm under a cap of 30 x 30 x 3 mm, all of its underside an overhang.
    indexed_triangle_set mushroom = box(-4., -4., 0., 4., 4., 20.);
    its_merge(mushroom, box(-15., -15., 20., 15., 15., 23.));
    Model model;
    const ModelObject *object = add_object(model, mushroom);
    const Vec2d rot = sla::find_least_fff_support_rotation(*object);
    // Upside down: the cap on the bed, nothing to support.
    CHECK(rotated_z(rot).z() < -0.99);
}

TEST_CASE("FFF orientation lays a bar flat", "[Rotfinder]")
{
    // Standing on its end the bar needs no support either, but it touches the bed with 25 mm2 only.
    Model model;
    const ModelObject *object = add_object(model, box(0., 0., 0., 5., 5., 60.));
    CHECK(std::abs(rotated_z(sla::find_least_fff_support_rotation(*object)).z()) < 0.01);
}

// A cup of 20 x 20 x 20 mm, walls and floor 2 mm thick, open at the top. The boxes overlap: boxes that only touch
// leave gaps between them once turned (rounding), which would vent the cup.
static indexed_triangle_set cup()
{
    indexed_triangle_set its = box(-10., -10., 0., 10., 10., 2.);
    its_merge(its, box(-10., -10., 1., -8., 10., 20.));
    its_merge(its, box(8., -10., 1., 10., 10., 20.));
    its_merge(its, box(-9., -10., 1., 9., -8., 20.));
    its_merge(its, box(-9., 8., 1., 9., 10., 20.));
    return its;
}

TEST_CASE("SLA orientation avoids the suction cups", "[Rotfinder]")
{
    Model model;
    const ModelObject *object = add_object(model, cup());
    SLAFullPrintConfig fullcfg;
    fullcfg.set("layer_height", 0.1);

    SECTION("on supports the cup is not left open towards the film") {
        DynamicPrintConfig cfg;
        cfg.apply(fullcfg);
        const Vec2d rot = sla::find_least_peel_rotation(*object, sla::RotOptimizeParams{}.print_config(&cfg));
        CHECK(rotated_z(rot).z() < 0.5);
    }
    SECTION("on the build plate the cup lies on its side") {
        fullcfg.set("support_object_elevation", 0.);
        DynamicPrintConfig cfg;
        cfg.apply(fullcfg);
        const Vec2d rot = sla::find_least_peel_rotation(*object, sla::RotOptimizeParams{}.print_config(&cfg));
        CHECK(std::abs(rotated_z(rot).z()) < 0.01);
    }
}
