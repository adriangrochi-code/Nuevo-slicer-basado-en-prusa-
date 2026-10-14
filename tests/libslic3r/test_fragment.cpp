#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <libslic3r/Fragment.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/TriangleMesh.hpp>

using namespace Slic3r;
using Catch::Approx;

static ModelObject *add_object(Model &model, TriangleMesh mesh)
{
    ModelObject *o = model.add_object();
    o->name        = "part";
    o->add_volume(std::move(mesh));
    o->add_instance();
    o->ensure_on_bed();
    return o;
}

// Volume of the solid parts of the pieces, without the plugs.
static double solid_volume(const ModelObjectPtrs &objects)
{
    double v = 0.;
    for (const ModelObject *o : objects)
        for (const ModelVolume *vol : o->volumes)
            if (vol->is_model_part() && !vol->cut_info.is_connector) {
                TriangleMesh m = vol->mesh();
                m.transform(o->instances.front()->get_matrix() * vol->get_matrix());
                v += m.volume();
            }
    return v;
}

TEST_CASE("Splitting an object larger than the printer", "[Fragment]")
{
    Model model;
    Fragment::Params params;
    params.max_size = Vec3d(250., 210., 220.);

    SECTION("an object that fits is not split") {
        ModelObject *o = add_object(model, make_cube(100., 100., 100.));
        const Fragment::Plan plan = Fragment::plan(*o, 0, params);
        CHECK(plan.error.empty());
        CHECK(!plan.needed());
    }

    SECTION("a long bar: three even pieces with dowels") {
        params.joint = Fragment::Joint::Dowel;
        ModelObject *o = add_object(model, make_cube(600., 60., 40.));
        const Fragment::Plan plan = Fragment::plan(*o, 0, params);
        REQUIRE(plan.error.empty());
        CHECK(plan.pieces == std::array<int, 3>{ 3, 1, 1 });
        REQUIRE(plan.cuts.size() == 2);
        CHECK(plan.cuts[0].position == Approx(200.).margin(10.));
        CHECK(plan.cuts[1].position == Approx(400.).margin(10.));
        for (const Fragment::CutPlane &cut : plan.cuts) {
            CHECK(cut.axis == 0);
            CHECK(cut.connectors.size() >= 1);
            CHECK(cut.sections_without_joint == 0);
            CHECK(cut.area == Approx(60. * 40.).epsilon(0.01));
        }
        Model out;
        const ModelObjectPtrs pieces = Fragment::apply(*o, 0, plan, params, out);
        size_t parts = 0, pins = 0;
        for (const ModelObject *p : pieces) {
            if (p->name == "part - pin") {
                ++pins;
                continue;
            }
            ++parts;
            const BoundingBoxf3 bb = p->instance_bounding_box(0);
            CHECK(bb.size().x() <= 240. + 1e-3);
            // Every piece has the holes of its connectors.
            size_t holes = 0;
            for (const ModelVolume *v : p->volumes)
                holes += v->is_negative_volume() && v->cut_info.is_connector;
            CHECK(holes >= 1);
        }
        CHECK(parts == 3);
        CHECK(pins == plan.joint_count());
        // No material lost or added (the pins, made of connectors, are not counted).
        CHECK(solid_volume(pieces) == Approx(600. * 60. * 40.).epsilon(1e-4));
    }

    SECTION("plugs: the plug is counted in the size of the piece") {
        params.joint = Fragment::Joint::Plug;
        ModelObject *o = add_object(model, make_cube(470., 50., 50.));
        const Fragment::Plan plan = Fragment::plan(*o, 0, params);
        REQUIRE(plan.error.empty());
        // 470 fits in two pieces of 240 but not with a plug of 6 mm sticking out of one of them.
        CHECK(plan.pieces[0] == 3);
        Model out;
        const ModelObjectPtrs pieces = Fragment::apply(*o, 0, plan, params, out);
        REQUIRE(pieces.size() == 3);
        for (const ModelObject *p : pieces)
            CHECK(p->instance_bounding_box(0).size().x() <= 240. + 1e-3);
        size_t plugs = 0;
        for (const ModelObject *p : pieces)
            for (const ModelVolume *v : p->volumes)
                plugs += v->is_model_part() && v->cut_info.is_connector;
        CHECK(plugs == plan.joint_count());
        CHECK(solid_volume(pieces) == Approx(470. * 50. * 50.).epsilon(1e-4));
    }

    SECTION("the cut goes through the narrowest section") {
        // Two blocks joined by a thin neck: the cut is in the neck, not in the middle of a block.
        TriangleMesh mesh = make_cube(150., 80., 80.);
        TriangleMesh neck = make_cube(60., 20., 20.);
        neck.translate(150., 30., 30.);
        TriangleMesh right = make_cube(150., 80., 80.);
        right.translate(210., 0., 0.);
        mesh.merge(neck);
        mesh.merge(right);
        ModelObject *o = add_object(model, mesh);
        const Fragment::Plan plan = Fragment::plan(*o, 0, params);
        REQUIRE(plan.cuts.size() == 1);
        CHECK(plan.cuts[0].position > 150.);
        CHECK(plan.cuts[0].position < 210.);
        CHECK(plan.cuts[0].area == Approx(400.).epsilon(0.01));
        CHECK(plan.cuts[0].connectors.size() == 1);
    }

    SECTION("a large plate: cuts in X and Y, connectors away from the other cut") {
        params.joint = Fragment::Joint::Dowel;
        ModelObject *o = add_object(model, make_cube(400., 300., 20.));
        const Fragment::Plan plan = Fragment::plan(*o, 0, params);
        REQUIRE(plan.error.empty());
        CHECK(plan.pieces == std::array<int, 3>{ 2, 2, 1 });
        const double keep = 0.5 * params.joint_diameter + params.clearance + params.joint_wall;
        for (const Fragment::CutPlane &cut : plan.cuts) {
            const Fragment::CutPlane &other = plan.cuts[cut.axis == 0 ? 1 : 0];
            // One connector at least on each side of the other cut.
            CHECK(cut.connectors.size() >= 2);
            for (const Fragment::Connector &c : cut.connectors) {
                CHECK(c.diameter == params.joint_diameter);
                CHECK(std::abs(c.position[other.axis] - other.position) >= keep - 1e-3);
            }
        }
        Model out;
        const ModelObjectPtrs pieces = Fragment::apply(*o, 0, plan, params, out);
        CHECK(pieces.size() == 4 + plan.joint_count());
        CHECK(solid_volume(pieces) == Approx(400. * 300. * 20.).epsilon(1e-4));
        // The second cut crosses the first one: the pieces stay closed.
        for (const ModelObject *p : pieces)
            for (const ModelVolume *v : p->volumes)
                CHECK(v->mesh().stats().open_edges == 0);
    }

    SECTION("taller than the printer: cut in Z") {
        ModelObject *o = add_object(model, make_cylinder(30., 500.));
        const Fragment::Plan plan = Fragment::plan(*o, 0, params);
        REQUIRE(plan.error.empty());
        CHECK(plan.pieces == std::array<int, 3>{ 1, 1, 3 });
    }

    SECTION("thin walls get thinner connectors") {
        params.joint = Fragment::Joint::Dowel;
        // A hollow box with walls of 8 mm: no room for a pin of 8 mm with 2 mm of wall around, room for 3 mm.
        TriangleMesh outer = make_cube(500., 100., 60.);
        indexed_triangle_set inner = its_make_cube(500. + 20., 84., 44.);
        its_flip_triangles(inner);
        TriangleMesh hole(inner);
        hole.translate(-10., 8., 8.);
        outer.merge(hole);
        ModelObject *o = add_object(model, outer);
        const Fragment::Plan plan = Fragment::plan(*o, 0, params);
        REQUIRE(plan.error.empty());
        REQUIRE(!plan.cuts.empty());
        for (const Fragment::CutPlane &cut : plan.cuts) {
            CHECK(cut.sections_without_joint == 0);
            REQUIRE(!cut.connectors.empty());
            for (const Fragment::Connector &c : cut.connectors)
                CHECK(c.diameter < 8.);
        }
    }
}
