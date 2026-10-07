#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>

#include "test_data.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;

static DynamicPrintConfig nonplanar_config(const char *mode)
{
    return DynamicPrintConfig::full_print_config_with({
        { "nonplanar_mode",           mode },
        { "nonplanar_pattern",        "ridges" },
        { "nonplanar_amplitude",      0.5 },
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
