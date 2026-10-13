#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "test_data.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;
using Catch::Approx;

static DynamicPrintConfig nonplanar_config(const char *mode)
{
    return DynamicPrintConfig::full_print_config_with({
        { "nonplanar_mode",           mode },
        { "nonplanar_pattern",        "ridges" },
        { "nonplanar_amplitude",      0.5 },
        { "nonplanar_fit_head",       false },
        { "nonplanar_wavelength",     20 },
        { "use_relative_e_distances", true },
        { "arc_fitting",              "disabled" },
        { "skirts",                   0 },
        { "layer_height",             0.2 },
        { "first_layer_height",       0.2 },
        { "start_gcode",              "" },
        { "machine_limits_usage",     "ignore" },
    });
}

struct Extrusions
{
    // layer Z of the planar G-code (";Z:" tag) -> Z of the extrusions within the layer
    std::map<double, std::set<double>> z_by_layer;
    double max_z { 0. };
};

static Extrusions analyse(const std::string &gcode)
{
    Extrusions out;
    double layer = 0.;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.comment().rfind("Z:", 0) == 0)
            layer = std::stod(std::string(line.comment().substr(2)));
        if (line.cmd_is("G1") && line.extruding(self) && line.dist_XY(self) > 0) {
            const double z = line.new_Z(self);
            out.z_by_layer[layer].insert(std::round(z * 1000.) / 1000.);
            out.max_z = std::max(out.max_z, z);
        }
    });
    return out;
}

TEST_CASE("Non-planar wave layers", "[NonPlanar]")
{
    const Extrusions planar = analyse(Test::slice({ Test::TestMesh::cube_20x20x20 }, nonplanar_config("disabled")));
    const Extrusions wave   = analyse(Test::slice({ Test::TestMesh::cube_20x20x20 }, nonplanar_config("wave")));

    // Planar: a single Z per layer.
    for (const auto &[layer, zs] : planar.z_by_layer)
        REQUIRE(zs.size() == 1);

    // Non-planar: the first layers stay flat, the upper ones are curved.
    REQUIRE(! wave.z_by_layer.empty());
    REQUIRE(wave.z_by_layer.begin()->second.size() == 1);
    size_t curved = 0;
    for (const auto &[layer, zs] : wave.z_by_layer)
        if (*zs.rbegin() - *zs.begin() > 0.3)
            ++ curved;
    REQUIRE(curved > wave.z_by_layer.size() / 2);

    // The geometry is preserved: nothing above the cube (allowing one layer).
    REQUIRE(wave.max_z <= 20. + 0.25);
    REQUIRE(wave.max_z >= 19.5);
}

TEST_CASE("Non-planar layers are rejected with incompatible settings", "[NonPlanar]")
{
    DynamicPrintConfig config = nonplanar_config("wave");
    config.set_deserialize_strict({ { "use_relative_e_distances", "0" } });
    Print print;
    Model model;
    Test::init_print({ Test::TestMesh::cube_20x20x20 }, print, model, config);
    REQUIRE(! print.validate().empty());
}

TEST_CASE("Non-planar layers are rejected when the print head would hit the part", "[NonPlanar]")
{
    DynamicPrintConfig config = nonplanar_config("wave");
    config.set_deserialize_strict({ { "nonplanar_amplitude", 2 }, { "nonplanar_wavelength", 40 },
                                    { "nonplanar_max_slope", 89 }, { "nonplanar_ramp_height", 15 } });
    {
        Print print;
        Model model;
        Test::init_print({ Test::TestMesh::cube_20x20x20 }, print, model, config);
        const std::string error = print.validate();
        REQUIRE(error.find("print head") != std::string::npos);
    }
    // A head with more room below it.
    config.set_deserialize_strict({ { "nonplanar_head_clearance_height", 6 } });
    {
        Print print;
        Model model;
        Test::init_print({ Test::TestMesh::cube_20x20x20 }, print, model, config);
        REQUIRE(print.validate().find("print head") == std::string::npos);
    }
}

TEST_CASE("Non-planar layers fitted to the free angles of the print head", "[NonPlanar]")
{
    DynamicPrintConfig config = nonplanar_config("wave");
    // Ridges rising along X; the head allows 25° along X but only 5° along Y.
    config.set_deserialize_strict({ { "nonplanar_head_free_angle_x", 25 }, { "nonplanar_head_free_angle_y", 5 },
                                    { "nonplanar_head_clearance_height", 3 }, { "nonplanar_ramp_height", 8 } });
    // The amplitude set (0.5 mm) is steeper than 5° along Y.
    config.set_deserialize_strict({ { "nonplanar_angle", 90 } });
    {
        Print print;
        Model model;
        Test::init_print({ Test::TestMesh::cube_20x20x20 }, print, model, config);
        const std::string error = print.validate();
        REQUIRE(error.find("5° along Y") != std::string::npos);
    }
    // Fitted, the same ridges are gentler and accepted; along X they get higher than the 0.5 mm set.
    config.set_deserialize_strict({ { "nonplanar_fit_head", 1 } });
    double height_y = 0.;
    for (double angle : { 90., 0. }) {
        config.set_deserialize_strict({ { "nonplanar_angle", angle } });
        Print print;
        Model model;
        Test::init_print({ Test::TestMesh::cube_20x20x20 }, print, model, config);
        REQUIRE(print.validate().empty());
        const Extrusions e = analyse(Test::gcode(print));
        double height = 0.;
        for (const auto &[layer, zs] : e.z_by_layer)
            height = std::max(height, *zs.rbegin() - *zs.begin());
        if (angle == 90.) {
            height_y = height;
            REQUIRE(height < 2. * 0.5);
        } else
            REQUIRE(height > 2. * 0.5);
    }
    REQUIRE(height_y > 0.3);
}

// Tisma: calibration tests of the non-planar layers (they use their own curved layers).
TEST_CASE("Non-planar calibration: maximum layer slope", "[NonPlanar][Calibration]")
{
    DynamicPrintConfig config = nonplanar_config("disabled");
    // Two steps along X: 10° and 40°, steeper than the maximum slope of the print (20°).
    config.set_deserialize_strict({ { "calib_mode", "nonplanar_slope" }, { "calib_start", 10 }, { "calib_end", 40 },
                                    { "calib_step", 30 }, { "calib_band_height", 10 } });
    Print print;
    Model model;
    Test::init_print({ Test::TestMesh::cube_20x20x20 }, print, model, config);
    REQUIRE(print.validate().empty());
    const Extrusions e = analyse(Test::gcode(print));
    size_t curved = 0;
    for (const auto &[layer, zs] : e.z_by_layer)
        if (*zs.rbegin() - *zs.begin() > 0.5)
            ++ curved;
    REQUIRE(curved > 10);
    REQUIRE(e.max_z <= 20. + 0.25);
}

TEST_CASE("Non-planar calibration: Z speed grows by steps", "[NonPlanar][Calibration]")
{
    DynamicPrintConfig config = nonplanar_config("disabled");
    config.set_deserialize_strict({ { "calib_mode", "nonplanar_z_speed" }, { "calib_start", 1 }, { "calib_end", 3 },
                                    { "calib_step", 1 }, { "calib_band_height", 3 } });
    Print print;
    Model model;
    Test::init_print({ Test::TestMesh::cube_20x20x20 }, print, model, config);
    REQUIRE(print.validate().empty());
    const std::string gcode = Test::gcode(print);

    // The steps start where the waves are complete: flat layers (0.6 mm) + transition (8 mm).
    const double start = 0.6 + 8.;
    std::vector<double> max_speed(3, 0.);
    // The step is chosen by the height of the planar layer (";Z:"), as the curved layers go up and down.
    double layer = 0.;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.comment().rfind("Z:", 0) == 0)
            layer = std::stod(std::string(line.comment().substr(2)));
        if (! line.cmd_is("G1") || ! line.extruding(self))
            return;
        const double dz   = line.new_Z(self) - self.z();
        const double dist = std::sqrt(sqr(line.new_X(self) - self.x()) + sqr(line.new_Y(self) - self.y()) + sqr(dz));
        if (dist < 1e-6 || std::abs(dz) < 1e-6)
            return;
        const double z_speed = line.new_F(self) / 60. * std::abs(dz) / dist;
        const int band = int(std::floor((layer - start) / 3.));
        if (band >= 0 && band < 3)
            max_speed[band] = std::max(max_speed[band], z_speed);
    });
    for (int band = 0; band < 3; ++ band) {
        INFO("band " << band);
        // The allowed Z speed of the step (+ rounding of the written coordinates and feed rates).
        CHECK(max_speed[band] <= (1. + band) * 1.1);
    }
    // The waves need more than the slowest step: it is limited there.
    CHECK(max_speed[0] > 0.8);
}

// Stem of radius 5 up to z = 10, flaring out at `angle` degrees from the vertical up to z = 15, then a cap 3 mm thick.
static TriangleMesh mushroom(double angle)
{
    const double r_cap = 5. + 5. * std::tan(angle * M_PI / 180.);
    const std::vector<Vec2d> profile { { 5., 0. }, { 5., 10. }, { r_cap, 15. }, { r_cap, 18. } };
    constexpr int segments = 72;
    indexed_triangle_set its;
    const int n = int(profile.size());
    for (int s = 0; s < segments; ++ s) {
        const double a = 2. * M_PI * s / segments;
        for (const Vec2d &p : profile)
            its.vertices.emplace_back(float(p.x() * std::cos(a)), float(p.x() * std::sin(a)), float(p.y()));
    }
    const int bottom = int(its.vertices.size());
    its.vertices.emplace_back(0.f, 0.f, 0.f);
    const int top = int(its.vertices.size());
    its.vertices.emplace_back(0.f, 0.f, float(profile.back().y()));
    for (int s = 0; s < segments; ++ s) {
        const int a = s * n, b = ((s + 1) % segments) * n;
        for (int i = 0; i + 1 < n; ++ i) {
            its.indices.emplace_back(a + i, b + i, b + i + 1);
            its.indices.emplace_back(a + i, b + i + 1, a + i + 1);
        }
        its.indices.emplace_back(bottom, b, a);
        its.indices.emplace_back(top, a + n - 1, b + n - 1);
    }
    return TriangleMesh(std::move(its));
}

// Filament length extruded as support material.
static double support_filament(const std::string &gcode)
{
    double out = 0.;
    bool support = false;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.comment().rfind("TYPE:", 0) == 0)
            support = line.comment().find("Support") != std::string::npos;
        if (support && line.cmd_is("G1") && line.extruding(self))
            out += line.dist_E(self);
    });
    return out;
}

TEST_CASE("Non-planar layers tilted under the overhangs need fewer supports", "[NonPlanar]")
{
    DynamicPrintConfig config = nonplanar_config("disabled");
    config.set_deserialize_strict({ { "support_material", 1 }, { "support_material_threshold", 45 },
                                    { "nonplanar_overhang_threshold", 45 },
                                    { "nonplanar_head_free_angle_x", 25 }, { "nonplanar_head_free_angle_y", 25 },
                                    // A print head that does not limit the slope here.
                                    { "nonplanar_head_clearance_height", 10 }, { "nonplanar_head_clearance_radius", 10 } });
    const double planar = support_filament(Test::slice({ mushroom(55.) }, config));
    REQUIRE(planar > 0.);

    config.set_deserialize_strict({ { "nonplanar_mode", "overhangs" } });
    Print print;
    Model model;
    Test::init_print({ mushroom(55.) }, print, model, config);
    REQUIRE(print.validate().empty());
    const std::string gcode = Test::gcode(print);
    const double tilted = support_filament(gcode);
    // Sheared below the printable angle in the slice space: no supports left.
    REQUIRE(tilted < 0.05 * planar);
    // The layers of the cap are curved, those of the stem below the transition flat.
    const Extrusions e = analyse(gcode);
    bool curved = false;
    for (const auto &[layer, zs] : e.z_by_layer) {
        if (layer < 0.65)
            REQUIRE(zs.size() == 1);
        if (layer > 11. && layer < 15. && *zs.rbegin() - *zs.begin() > 1.)
            curved = true;
    }
    REQUIRE(curved);
}

TEST_CASE("Non-planar layers tilted under the overhangs stay within the print head clearance", "[NonPlanar]")
{
    // The default print head (3 mm of clearance 15 mm around the nozzle) allows only about 10°.
    DynamicPrintConfig config = nonplanar_config("disabled");
    config.set_deserialize_strict({ { "support_material", 1 }, { "support_material_threshold", 45 },
                                    { "nonplanar_head_free_angle_x", 25 }, { "nonplanar_head_free_angle_y", 25 } });
    auto supports = [&config](double angle, const char *mode) {
        config.set_deserialize_strict({ { "nonplanar_mode", mode } });
        Print print;
        Model model;
        Test::init_print({ mushroom(angle) }, print, model, config);
        REQUIRE(print.validate().empty());
        return support_filament(Test::gcode(print));
    };
    // Enough to bring a 48° flare below 45°: no supports left.
    const double planar48 = supports(48., "disabled");
    REQUIRE(planar48 > 0.);
    REQUIRE(supports(48., "overhangs") < 0.05 * planar48);
    // Not enough for a 55° flare: left flat, the supports are not made taller.
    REQUIRE(supports(55., "overhangs") == Approx(supports(55., "disabled")).epsilon(0.01));
}

// Layers rising 0.3 mm per mm along X over a part of 20 x 20 x 20 mm, as the structural analysis would store them.
static EngineeringLayerField tilted_layers(double slope)
{
    EngineeringLayerField field;
    field.origin    = Vec3d(-2., -2., -2.);
    field.cell      = 2.;
    field.nx = field.ny = field.nz = 14;
    field.part_size = Vec3d(20., 20., 20.);
    for (int k = 0; k < field.nz; ++ k)
        for (int j = 0; j < field.ny; ++ j)
            for (int i = 0; i < field.nx; ++ i)
                field.offset.push_back(float(slope * (field.origin.x() + i * field.cell - 10.)));
    return field;
}

TEST_CASE("Non-planar layers following the stresses of the analysis", "[NonPlanar][StressLayers]")
{
    DynamicPrintConfig config = nonplanar_config("stress");
    config.set_deserialize_strict({ { "nonplanar_max_slope", "30" } });
    Print print;
    Model model;
    Test::init_print({ Test::TestMesh::cube_20x20x20 }, print, model, config);

    SECTION("not computed yet") {
        REQUIRE(print.validate().find("compute them first") != std::string::npos);
    }
    SECTION("computed for another part") {
        EngineeringLayerField field = tilted_layers(0.3);
        field.part_size = Vec3d(30., 20., 20.);
        model.objects.front()->engineering.layer_field = field;
        print.apply(model, config);
        REQUIRE(print.validate().find("the part changed") != std::string::npos);
    }
    SECTION("the layers rise along X") {
        const Extrusions flat = analyse(Test::gcode(print));
        model.objects.front()->engineering.layer_field = tilted_layers(0.3);
        print.apply(model, config);
        REQUIRE(print.validate().empty());
        const Extrusions curved = analyse(Test::gcode(print));
        // Flat without the field (the stresses mode without layers keeps the layers flat).
        for (const auto &[layer, zs] : flat.z_by_layer)
            REQUIRE(zs.size() == 1);
        // In the middle of the part the layers are tilted (the transitions above the first layers reduce the slope
        // to keep the layer thickness).
        const auto &middle = curved.z_by_layer.lower_bound(12.)->second;
        CHECK(*middle.rbegin() - *middle.begin() > 0.5);
    }
}

// Block 40 x 40 mm whose top rises along X from z = 20 to z = 24 (5.7°).
static TriangleMesh sloped_block()
{
    indexed_triangle_set its;
    its.vertices = { { 0.f, 0.f, 0.f }, { 40.f, 0.f, 0.f }, { 40.f, 40.f, 0.f }, { 0.f, 40.f, 0.f },
                     { 0.f, 0.f, 20.f }, { 40.f, 0.f, 24.f }, { 40.f, 40.f, 24.f }, { 0.f, 40.f, 20.f } };
    its.indices = { { 0, 2, 1 }, { 0, 3, 2 }, { 4, 5, 6 }, { 4, 6, 7 }, { 0, 1, 5 }, { 0, 5, 4 },
                    { 1, 2, 6 }, { 1, 6, 5 }, { 2, 3, 7 }, { 2, 7, 6 }, { 3, 0, 4 }, { 3, 4, 7 } };
    return TriangleMesh(std::move(its));
}

// Spread (5 % to 95 %) of the highest extrusion of each 1 mm cell around the sloped top of sloped_block(), the
// extrusions sampled every 0.25 mm.
static double top_spread(const std::string &gcode)
{
    std::map<std::pair<int, int>, double> top;
    double x_min = 1e9, x_max = -1e9, y_min = 1e9;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.cmd_is("G1") && line.extruding(self) && line.dist_XY(self) > 0) {
            const Vec3d a(self.x(), self.y(), self.z()), b(line.new_X(self), line.new_Y(self), line.new_Z(self));
            x_min = std::min(x_min, b.x());
            x_max = std::max(x_max, b.x());
            y_min = std::min(y_min, b.y());
            const int n = std::max(1, int((b - a).head<2>().norm() / 0.25));
            for (int i = 0; i <= n; ++ i) {
                const Vec3d p = a + (b - a) * (double(i) / n);
                auto [it, added] = top.try_emplace({ int(std::floor(p.x())), int(std::floor(p.y())) }, p.z());
                it->second = std::max(it->second, p.z());
            }
        }
    });
    std::vector<double> d;
    for (const auto &[cell, z] : top)
        if (cell.first > x_min + 3 && cell.first < x_max - 4 && cell.second > y_min + 3 && cell.second < y_min + 36)
            d.push_back(z - (20. + 4. * (cell.first + 0.5 - x_min) / (x_max - x_min)));
    std::sort(d.begin(), d.end());
    REQUIRE(d.size() > 500);
    return d[d.size() * 19 / 20] - d[d.size() / 20];
}

TEST_CASE("Non-planar layers following the top surfaces", "[NonPlanar][Surface]")
{
    auto config = [](const char *mode) {
        DynamicPrintConfig c = nonplanar_config(mode);
        c.set_deserialize_strict({ { "nonplanar_max_slope", "20" }, { "top_solid_layers", "4" }, { "fill_density", "15%" } });
        return c;
    };
    const double flat    = top_spread(Test::slice({ sloped_block() }, config("disabled")));
    Print print;
    Model model;
    Test::init_print({ sloped_block() }, print, model, config("surface"));
    REQUIRE(print.validate().empty());
    const double surface = top_spread(Test::gcode(print));
    WARN("Spread of the top: flat " << flat << " mm, following the top " << surface << " mm");
    // Flat layers leave stairs of a layer height every 2 mm along X; the upper layers parallel to the top leave few.
    CHECK(surface < 0.6 * flat);
}
