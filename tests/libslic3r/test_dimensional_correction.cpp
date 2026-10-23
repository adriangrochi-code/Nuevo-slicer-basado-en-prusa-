#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <vector>

#include "libslic3r/DimensionalCorrection.hpp"

using namespace Slic3r;
using Catch::Approx;

TEST_CASE("Dimensional correction: line fit", "[DimensionalCorrection]")
{
    // measured = 0.995 * nominal + 0.3, the third size not measured.
    const LinearFit fit = fit_line({ 10., 30., 50., 80. }, { 10.25, 30.15, 0., 79.9 });
    CHECK(fit.points == 3);
    CHECK(fit.slope == Approx(0.995));
    CHECK(fit.offset == Approx(0.3));
    CHECK(fit.rms == Approx(0.).margin(1e-9));
    // A single size: the offset only.
    const LinearFit one = fit_line({ 10., 30. }, { 10.2, 0. });
    CHECK(one.points == 1);
    CHECK(one.slope == 1.);
    CHECK(one.offset == Approx(0.2));
}

// A printer that shrinks the plastic by s (heights by sz), widens the outer walls by e, narrows the holes by eh, adds
// cz to the heights and spreads the first layer by foot on each side, printing with the given settings (the slicer
// scales by the shrinkage compensation, offsets the sides by xy_size and the holes by xy_hole, and shrinks the first
// layer by the elephant foot compensation).
struct Printer
{
    double s { 0.993 }, sz { 0.996 }, e { 0.24 }, eh { 0.31 }, cz { -0.08 }, foot { 0.18 };

    DimensionalMeasures print(const DimensionalSettings &set) const
    {
        const double k  = 100. / (100. - set.shrinkage_xy_percent);
        const double kz = 100. / (100. - set.shrinkage_z_percent);
        DimensionalMeasures m;
        m.outer_nominal  = { 10., 30., 50., 80. };
        m.hole_nominal   = { 4., 6., 8., 10. };
        m.height_nominal = { 3., 6., 9., 12. };
        for (double n : m.outer_nominal) {
            m.outer_x.push_back(s * (k * n + 2. * set.xy_size) + e);
            m.outer_y.push_back(s * (k * n + 2. * set.xy_size) + e);
        }
        for (double d : m.hole_nominal)
            m.holes.push_back(s * (k * d - 2. * set.xy_size + 2. * set.xy_hole) - eh);
        for (double h : m.height_nominal)
            m.heights.push_back(sz * kz * h + cz);
        m.first_layer_width = m.outer_x.back() + 2. * (foot - set.elephant_foot);
        return m;
    }
};

TEST_CASE("Dimensional correction: the next print is as designed", "[DimensionalCorrection]")
{
    const Printer printer;
    // Printed first with some compensation already set.
    DimensionalSettings first;
    first.shrinkage_xy_percent = 0.3;
    first.xy_size              = -0.05;
    first.xy_hole              = 0.02;
    first.elephant_foot        = 0.1;
    const DimensionalResult result = dimensional_correction(printer.print(first), first);
    REQUIRE(result.xy_ok);
    REQUIRE(result.holes_ok);
    REQUIRE(result.z_ok);
    REQUIRE(result.foot_ok);
    // The constant the first layers add to the heights.
    CHECK(result.z_offset == Approx(printer.cz));
    // Printed again with the corrected settings: every size as designed (the heights but their constant).
    const DimensionalMeasures second = printer.print(result.settings);
    for (size_t i = 0; i < second.outer_nominal.size(); ++ i) {
        CHECK(second.outer_x[i] == Approx(second.outer_nominal[i]).margin(1e-9));
        CHECK(second.outer_y[i] == Approx(second.outer_nominal[i]).margin(1e-9));
    }
    for (size_t i = 0; i < second.hole_nominal.size(); ++ i)
        CHECK(second.holes[i] == Approx(second.hole_nominal[i]).margin(1e-9));
    for (size_t i = 0; i < second.height_nominal.size(); ++ i)
        CHECK(second.heights[i] - printer.cz == Approx(second.height_nominal[i]).margin(1e-9));
    CHECK(second.first_layer_width == Approx(second.outer_x.back()).margin(1e-9));
}

TEST_CASE("Dimensional correction: missing measures keep the settings", "[DimensionalCorrection]")
{
    DimensionalSettings set;
    set.xy_size = 0.1;
    set.xy_hole = 0.2;
    DimensionalMeasures m;
    m.outer_nominal = { 10., 30. };
    m.outer_x       = { 10.1, 0. };
    m.outer_y       = { 10.1, 30.1 };
    const DimensionalResult result = dimensional_correction(m, set);
    CHECK(! result.xy_ok);
    CHECK(! result.holes_ok);
    CHECK(! result.z_ok);
    CHECK(result.settings.xy_size == 0.1);
    CHECK(result.settings.xy_hole == 0.2);
    CHECK(result.settings.shrinkage_xy_percent == 0.);
}
