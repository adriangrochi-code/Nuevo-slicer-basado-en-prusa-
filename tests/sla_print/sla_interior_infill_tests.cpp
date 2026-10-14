// Tisma Slicer: struts inside the cavity of a hollowed resin part (src/libslic3r/SLA/InteriorInfill.hpp).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/SLA/InteriorInfill.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

using namespace Slic3r;
using Catch::Approx;

TEST_CASE("Interior infill struts support themselves", "[SLA][InteriorInfill]")
{
    // Vertical or at 45 degrees: every strut stands on the layers below.
    for (const Vec3d &d : sla::infill_directions()) {
        CHECK(d.norm() == Approx(1.));
        CHECK(d.z() >= std::sqrt(0.5) - 1e-9);
    }
    // density = 5 (width / cell)^2.
    CHECK(sla::infill_strut_width(0.2, 10.) == Approx(2.));
    CHECK(sla::infill_strut_width(0., 10.) == 0.);
}

TEST_CASE("Interior infill is removed from the cavity", "[SLA][InteriorInfill]")
{
    indexed_triangle_set cavity = its_make_cube(40., 40., 40.);
    const double volume = its_volume(cavity);
    // 20 % of struts, 8 mm cell: 1.8 mm struts.
    REQUIRE(sla::subtract_infill(cavity, 0.2, 8., 2.f));
    const double left = its_volume(cavity);
    INFO("cavity " << volume << " left " << left);
    // The struts fill about the density (less: they cross each other); the cavity stays closed (positive volume).
    CHECK(left > 0.80 * volume);
    CHECK(left < 0.90 * volume);
    // Nothing to remove.
    indexed_triangle_set unchanged = its_make_cube(40., 40., 40.);
    CHECK(! sla::subtract_infill(unchanged, 0., 8., 2.f));
}

static double sliced_area(double infill)
{
    Model model;
    ModelObject *object = model.add_object();
    object->name = "block";
    object->add_volume(TriangleMesh(its_make_cube(30., 30., 30.)));
    object->add_instance();
    SLAFullPrintConfig fullcfg;
    fullcfg.printer_technology.setInt(ptSLA);
    fullcfg.set("supports_enable", false);
    fullcfg.set("pad_enable", false);
    fullcfg.set("layer_height", 0.5);
    fullcfg.set("hollowing_enable", true);
    fullcfg.set("hollowing_min_thickness", 2.);
    fullcfg.hollowing_infill_density.value = infill;
    DynamicPrintConfig cfg;
    cfg.apply(fullcfg);
    SLAPrint print;
    print.set_status_callback([](const PrintBase::SlicingStatus &) {});
    print.apply(model, cfg);
    print.process();
    double area = 0.;
    for (const SLAPrintObject::SliceRecord &record : print.objects().front()->get_slice_index())
        for (const ExPolygon &e : record.get_slice(soModel))
            area += e.area() * SCALING_FACTOR * SCALING_FACTOR;
    return area;
}

TEST_CASE("Interior infill of a hollowed resin print", "[SLA][InteriorInfill]")
{
    const double empty = sliced_area(0.);
    const double filled = sliced_area(20.);
    INFO("sliced area: hollow " << empty << ", with 20 % infill " << filled);
    // The struts are printed: more material in the slices than the shell alone.
    CHECK(filled > 1.15 * empty);
}
