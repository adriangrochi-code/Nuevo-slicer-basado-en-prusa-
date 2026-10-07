// Tisma: validation of the approximate aerodynamic analysis.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <libslic3r/TriangleMesh.hpp>

#include "tisma_fea/Aero.hpp"

using namespace Slic3r;
using namespace Slic3r::Fea;
using Catch::Approx;

TEST_CASE("Roughness of printed faces", "[Aero]")
{
    // Vertical walls and flat tops: only the rounded edge of the beads.
    CHECK(printed_roughness_ra(0.2, Vec3d(1, 0, 0)) == Approx(0.01));
    CHECK(printed_roughness_ra(0.2, Vec3d(0, 0, 1)) == Approx(0.01));
    // 45° slope: steps of 0.2 * cos 45° = 0.141 mm, Ra = Rz / 4.
    CHECK(printed_roughness_ra(0.2, Vec3d(1, 0, 1)) == Approx(0.25 * 0.2 * std::sqrt(0.5)));
    // Thicker layers, rougher slopes.
    CHECK(printed_roughness_ra(0.3, Vec3d(1, 0, 1)) > printed_roughness_ra(0.1, Vec3d(1, 0, 1)));
    CHECK(printed_roughness_ra(0., Vec3d(1, 0, 1)) == 0.);
}

TEST_CASE("Skin friction correlations", "[Aero]")
{
    const double nu = 1.5e-5;
    // Laminar (Blasius): 0.664 / sqrt(Re_x), Re_x = 10 m/s * 0.05 m / nu = 33333.
    CHECK(local_skin_friction(50., 0., 10., nu) == Approx(0.664 / std::sqrt(10. * 0.05 / nu)));
    // Turbulent smooth beyond Re_x = 5e5.
    CHECK(local_skin_friction(1000., 0., 10., nu) == Approx(0.0592 / std::pow(10. * 1. / nu, 0.2)));
    // Rough enough to trip the flow and rougher than smooth turbulent.
    const double rough = local_skin_friction(1000., 0.5, 10., nu);
    CHECK(rough > local_skin_friction(1000., 0., 10., nu));
    CHECK(rough == Approx(std::pow(2.87 + 1.58 * std::log10(1000. / 0.5), -2.5)));
    CHECK(local_skin_friction(50., 0.5, 10., nu) > local_skin_friction(50., 0., 10., nu));
}

static indexed_triangle_set box(double sx, double sy, double sz)
{
    indexed_triangle_set its = its_make_cube(sx, sy, sz);
    its_translate(its, Vec3f(float(-sx / 2), float(-sy / 2), float(-sz / 2)));
    return its;
}

// Bluff bodies: the drag is almost all pressure drag and changes little with the Reynolds number above ~1000
// (Hoerner, Fluid-Dynamic Drag): disk normal to the flow 1.17, cube face on 1.05.
TEST_CASE("Pressure drag of bluff bodies", "[Aero]")
{
    AeroSetup setup;
    setup.speed         = 10.;
    setup.layer_height  = 0.;
    setup.resolution    = 12;
    setup.flow_throughs = 2.;

    SECTION("square plate normal to the flow") {
        // 40 x 40 x 4 mm plate (a square plate has about the drag of a disk: 1.17 - 1.2).
        const AeroResult r = analyze_aero(box(4., 40., 40.), setup);
        REQUIRE(r.ok);
        INFO("Cd pressure " << r.cd_pressure << ", Re sim " << r.sim_reynolds);
        CHECK(r.cd_pressure > 0.9);
        CHECK(r.cd_pressure < 1.5);
        CHECK(r.frontal_area == Approx(1600.).epsilon(0.15));
        // Stagnation pressure on the front face: Cp close to 1 at the center.
        // Lift by symmetry ~ 0.
        CHECK(r.cl < 0.1);
    }
    SECTION("cube, same result whatever the flow direction") {
        const AeroResult rx = analyze_aero(box(30., 30., 30.), setup);
        AeroSetup sy = setup;
        sy.flow_direction = Vec3d(0., -1., 0.);
        const AeroResult ry = analyze_aero(box(30., 30., 30.), sy);
        REQUIRE(rx.ok);
        REQUIRE(ry.ok);
        INFO("Cd x " << rx.cd_pressure << ", Cd y " << ry.cd_pressure);
        CHECK(rx.cd_pressure > 0.8);
        CHECK(rx.cd_pressure < 1.35);
        CHECK(ry.cd_pressure == Approx(rx.cd_pressure).epsilon(0.05));
        // The force points along the flow.
        CHECK(ry.force.y() < 0.);
        CHECK(std::abs(ry.force.x()) < 0.1 * std::abs(ry.force.y()));
        // Drag in newtons: Cd * 0.5 rho U² A.
        CHECK(rx.drag == Approx(rx.cd * 0.5 * 1.204 * 100. * 900e-6).epsilon(0.05));
    }
}

// Convex body from its corner points: a box from x = 0 to x = length with an optional pyramid nose upstream.
static indexed_triangle_set nosed_box(double length, double side, double nose)
{
    const float h = float(side / 2);
    indexed_triangle_set its;
    its.vertices = { { 0, -h, -h }, { 0, h, -h }, { 0, h, h }, { 0, -h, h },
                     { float(length), -h, -h }, { float(length), h, -h }, { float(length), h, h }, { float(length), -h, h } };
    // Back and sides.
    its.indices = { { 4, 5, 6 }, { 4, 6, 7 }, { 0, 1, 5 }, { 0, 5, 4 }, { 1, 2, 6 }, { 1, 6, 5 }, { 2, 3, 7 }, { 2, 7, 6 }, { 3, 0, 4 }, { 3, 4, 7 } };
    if (nose > 0.) {
        its.vertices.push_back({ float(-nose), 0, 0 });
        for (int a = 0; a < 4; ++a)
            its.indices.push_back({ 8, a, (a + 1) % 4 });
    } else
        its.indices.insert(its.indices.end(), { { 0, 1, 2 }, { 0, 2, 3 } });
    // Outward normals (the body is convex).
    Vec3f center = Vec3f::Zero();
    for (const Vec3f &v : its.vertices)
        center += v;
    center /= float(its.vertices.size());
    for (Vec3i32 &t : its.indices) {
        const Vec3f n = (its.vertices[t[1]] - its.vertices[t[0]]).cross(its.vertices[t[2]] - its.vertices[t[0]]);
        if (n.dot(its.vertices[t[0]] - center) < 0.f)
            std::swap(t[1], t[2]);
    }
    return its;
}

TEST_CASE("Pointed nose: less pressure drag than a blunt one", "[Aero]")
{
    AeroSetup setup;
    setup.speed         = 10.;
    setup.layer_height  = 0.;
    setup.resolution    = 12;
    setup.flow_throughs = 2.;
    const AeroResult blunt  = analyze_aero(nosed_box(60., 20., 0.), setup);
    const AeroResult nosed  = analyze_aero(nosed_box(60., 20., 30.), setup);
    REQUIRE(blunt.ok);
    REQUIRE(nosed.ok);
    INFO("blunt " << blunt.cd_pressure << ", pointed " << nosed.cd_pressure);
    CHECK(nosed.cd_pressure < 0.8 * blunt.cd_pressure);
    CHECK(nosed.frontal_area == Approx(blunt.frontal_area).epsilon(0.05));
}

TEST_CASE("Roughness of the layers on the skin friction", "[Aero]")
{
    // Friction only (no simulation): a long box along the flow, printed upright or tilted 45° (stepped faces).
    AeroSetup setup;
    setup.speed             = 30.;
    setup.layer_height      = 0.3;
    setup.resolution        = 12;
    setup.simulate_pressure = false;
    const AeroResult upright = analyze_aero(box(120., 20., 20.), setup);
    REQUIRE(upright.ok);
    CHECK(upright.cd_pressure == 0.);
    // Laminar plate (Blasius, mean Cf = 1.328 / sqrt(Re_L)) on the 4 sides: 4 * 120 * 20 / (20 * 20) = 24 times.
    const double re_l = 30. * 0.12 / (1.81e-5 / 1.204);
    CHECK(upright.cd_friction == Approx(24. * 1.328 / std::sqrt(re_l)).epsilon(0.1));
    CHECK(upright.mean_roughness_ra < 20.);

    indexed_triangle_set tilted = box(120., 20., 20.);
    const Eigen::Matrix3f rot = Eigen::AngleAxisf(float(M_PI / 4.), Vec3f::UnitY()).toRotationMatrix();
    for (Vec3f &v : tilted.vertices)
        v = rot * v;
    AeroSetup tilted_setup = setup;
    tilted_setup.flow_direction = rot.cast<double>() * Vec3d::UnitX();
    const AeroResult t = analyze_aero(tilted, tilted_setup);
    REQUIRE(t.ok);
    INFO("Ra " << upright.mean_roughness_ra << " / " << t.mean_roughness_ra << " um, Cd f " << upright.cd_friction << " / " << t.cd_friction);
    CHECK(t.mean_roughness_ra > 2. * upright.mean_roughness_ra);
    // The steps trip the boundary layer: more friction.
    CHECK(t.cd_friction > 1.2 * upright.cd_friction);
    CHECK(t.roughness_friction_share > 0.2);
    // Finer layers, less friction.
    tilted_setup.layer_height = 0.1;
    const AeroResult fine = analyze_aero(tilted, tilted_setup);
    CHECK(fine.cd_friction < t.cd_friction);
}
