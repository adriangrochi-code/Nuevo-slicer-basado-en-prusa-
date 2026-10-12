#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "libslic3r/SLA/SuctionCups.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

using namespace Slic3r;

static ExPolygon square(double half)
{
    ExPolygon out;
    out.contour = Polygon({ scaled(Vec2d(-half, -half)), scaled(Vec2d(half, -half)), scaled(Vec2d(half, half)), scaled(Vec2d(-half, half)) });
    return out;
}

static ExPolygon ring(double outer, double inner)
{
    ExPolygon out = square(outer);
    Polygon hole = square(inner).contour;
    hole.reverse();
    out.holes.emplace_back(hole);
    return out;
}

TEST_CASE("Suction cups are found in the slices", "[SuctionCups]")
{
    // A cup open towards the film: a floor, then walls.
    std::vector<ExPolygons> cup;
    for (int i = 0; i < 5; ++ i)
        cup.push_back({ square(10.) });
    for (int i = 0; i < 20; ++ i)
        cup.push_back({ ring(10., 8.) });

    SECTION("a cup on the build plate") {
        const auto cups = sla::find_suction_cups(cup, true);
        REQUIRE(cups.size() == 1);
        CHECK(cups.front().first_layer == 5);
        CHECK(cups.front().last_layer == 24);
        CHECK(std::abs(cups.front().max_area - 256.) < 1.);
    }
    SECTION("a cup lifted on supports is still closed by its own floor") {
        REQUIRE(sla::find_suction_cups(cup, false).size() == 1);
    }
    SECTION("a drain hole in the floor vents it") {
        for (int i = 0; i < 5; ++ i)
            cup[i] = { ring(10., 2.) };
        CHECK(sla::find_suction_cups(cup, false).empty());
        // On the build plate the drain hole itself is closed by the plate.
        CHECK(sla::find_suction_cups(cup, true).size() == 1);
    }
    SECTION("a drain hole too thin does not vent it") {
        // 1 x 1 mm: resin and air hardly flow through it.
        for (int i = 0; i < 5; ++ i)
            cup[i] = { ring(10., 0.5) };
        CHECK(sla::find_suction_cups(cup, false).size() == 1);
    }
    SECTION("a tube on supports is open below") {
        std::vector<ExPolygons> tube(20, ExPolygons{ ring(10., 8.) });
        CHECK(sla::find_suction_cups(tube, false).empty());
        // Standing on the build plate, the plate closes it.
        CHECK(sla::find_suction_cups(tube, true).size() == 1);
    }
    SECTION("a cup upside down, open towards the build plate, on supports") {
        std::vector<ExPolygons> inverted(cup.rbegin(), cup.rend());
        CHECK(sla::find_suction_cups(inverted, false).empty());
    }
    SECTION("small hollows are ignored") {
        std::vector<ExPolygons> small = { { square(10.) } };
        for (int i = 0; i < 10; ++ i)
            small.push_back({ ring(10., 0.5) });
        CHECK(sla::find_suction_cups(small, true).empty());
    }
}

// Solid of revolution of a closed profile (radius, z) around Z, all radii > 0.
static TriangleMesh revolve(const std::vector<Vec2d> &profile, int segments = 64)
{
    indexed_triangle_set its;
    const int n = int(profile.size());
    for (int s = 0; s < segments; ++ s) {
        const double a = 2. * M_PI * s / segments;
        for (const Vec2d &p : profile)
            its.vertices.emplace_back(float(p.x() * std::cos(a)), float(p.x() * std::sin(a)), float(p.y()));
    }
    for (int s = 0; s < segments; ++ s) {
        const int a = s * n, b = ((s + 1) % segments) * n;
        for (int i = 0; i < n; ++ i) {
            const int j = (i + 1) % n;
            its.indices.emplace_back(a + i, b + i, b + j);
            its.indices.emplace_back(a + i, b + j, a + j);
        }
    }
    TriangleMesh mesh(std::move(its));
    if (mesh.volume() < 0.)
        mesh.flip_triangles();
    return mesh;
}

static std::vector<PrintStateBase::Warning> slice_warnings(const TriangleMesh &mesh, bool supports = false)
{
    Model model;
    ModelObject *object = model.add_object();
    object->name = "cup";
    object->add_volume(mesh);
    object->add_instance();
    SLAFullPrintConfig fullcfg;
    fullcfg.printer_technology.setInt(ptSLA);
    fullcfg.set("supports_enable", supports);
    fullcfg.set("pad_enable", supports);
    fullcfg.set("layer_height", 0.1);
    DynamicPrintConfig cfg;
    cfg.apply(fullcfg);
    SLAPrint print;
    print.set_status_callback([](const PrintBase::SlicingStatus &) {});
    print.apply(model, cfg);
    print.process();
    return print.objects().front()->step_state_with_warnings(slaposObjectSlice).warnings;
}

TEST_CASE("Slicing a resin print warns about suction cups", "[SuctionCups]")
{
    auto has_cup = [](const std::vector<PrintStateBase::Warning> &warnings) {
        for (const PrintStateBase::Warning &w : warnings)
            if (w.message.find("Suction cup") != std::string::npos)
                return true;
        return false;
    };
    // A cup of radius 10 and height 10, floor 2 mm thick, open at the top (towards the film). The profiles keep 0.5 mm
    // off the axis: a channel of 1 mm along it, too thin to vent the cup and too small to be a cup itself.
    const TriangleMesh cup = revolve({ { 0.5, 0. }, { 10., 0. }, { 10., 10. }, { 8., 10. }, { 8., 2. }, { 0.5, 2. } });
    CHECK(has_cup(slice_warnings(cup)));
    // Lifted on supports with a pad: closed by its own floor.
    CHECK(has_cup(slice_warnings(cup, true)));
    // A full cylinder has no hollow.
    CHECK(! has_cup(slice_warnings(revolve({ { 0.5, 0. }, { 10., 0. }, { 10., 10. }, { 0.5, 10. } }))));
}
