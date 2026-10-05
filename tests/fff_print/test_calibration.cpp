#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "test_data.hpp"
#include "libslic3r/GCodeReader.hpp"

using namespace Slic3r;
using Catch::Approx;

// Tisma calibration suite: the value of the test changes every calib_band_height millimeters (here 5 mm on a
// 20 mm cube, 4 steps).
static std::string slice_calibration(const std::string &mode, double start, double end, double step,
                                     const std::string &flavor, int copies = 1)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config_with({
        { "calib_mode",        mode },
        { "calib_start",       start },
        { "calib_end",         end },
        { "calib_step",        step },
        { "calib_band_height", 5. },
        { "gcode_flavor",      flavor },
        { "layer_height",      0.2 },
        { "first_layer_height", 0.2 },
        { "perimeters",        1 },
        { "fill_density",      "0%" },
        { "top_solid_layers",  0 },
        { "bottom_solid_layers", 1 },
        { "skirts",            0 },
        { "use_relative_e_distances", true },
    });
    return copies == 1 ? Test::slice({ Test::TestMesh::cube_20x20x20 }, config) :
                         Test::slice({ Test::TestMesh::cube_20x20x20, Test::TestMesh::cube_20x20x20 }, config);
}

// Expected value of the step containing the height z.
static double expected(double start, double end, double step, double z)
{
    const int    idx = int(std::floor((z - EPSILON) / 5.));
    const double lo  = std::min(start, end), hi = std::max(start, end);
    return std::clamp(start + idx * step, lo, hi);
}

// Values of a numeric parameter of a command (e.g. "S" of M104) in the order they appear.
static std::vector<double> command_values(const std::string &gcode, const std::string &prefix, const std::string &param)
{
    std::vector<double> out;
    size_t pos = 0;
    while ((pos = gcode.find("\n" + prefix, pos)) != std::string::npos) {
        const size_t eol  = gcode.find('\n', pos + 1);
        const std::string line = gcode.substr(pos + 1, eol - pos - 1);
        const size_t p = line.find(param);
        if (p != std::string::npos)
            out.push_back(std::stod(line.substr(p + param.size())));
        pos = eol;
    }
    return out;
}

TEST_CASE("Calibration: temperature tower", "[Calibration]")
{
    const std::string gcode = slice_calibration("temperature", 230., 215., -5., "marlin2");
    // The temperature active at each extrusion is the one of its step.
    double temperature = 0.;
    size_t checked = 0;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.cmd_is("M104")) {
            float s = 0.f;
            if (line.has_value('S', s))
                temperature = s;
        }
        else if (line.cmd_is("G1") && line.has_e() && line.e() > 0 && line.dist_XY(self) > 0) {
            REQUIRE(temperature == Approx(expected(230., 215., -5., self.z())));
            ++ checked;
        }
    });
    REQUIRE(checked > 0);
    const std::vector<double> m117 = command_values(gcode, "M117 Temp ", "Temp ");
    REQUIRE(std::set<double>(m117.begin(), m117.end()) == std::set<double>{ 215., 220., 225., 230. });
}

TEST_CASE("Calibration: pressure advance by firmware", "[Calibration]")
{
    SECTION("Klipper") {
        const std::string gcode = slice_calibration("pressure_advance", 0., 0.06, 0.02, "klipper");
        const std::vector<double> v = command_values(gcode, "SET_PRESSURE_ADVANCE", "ADVANCE=");
        REQUIRE(std::set<double>(v.begin(), v.end()) == std::set<double>{ 0., 0.02, 0.04, 0.06 });
    }
    SECTION("Marlin") {
        const std::string gcode = slice_calibration("pressure_advance", 0., 0.06, 0.02, "marlin2");
        const std::vector<double> v = command_values(gcode, "M900", "K");
        REQUIRE(std::set<double>(v.begin(), v.end()) == std::set<double>{ 0., 0.02, 0.04, 0.06 });
    }
    SECTION("RepRapFirmware") {
        const std::string gcode = slice_calibration("pressure_advance", 0., 0.06, 0.02, "reprapfirmware");
        REQUIRE(gcode.find("\nM572 D0 S0.0600") != std::string::npos);
    }
}

TEST_CASE("Calibration: retraction tower", "[Calibration]")
{
    // Two objects, so that there are travels and retractions on every layer.
    const std::string gcode = slice_calibration("retraction", 0.5, 2., 0.5, "marlin2", 2);
    // With relative E, an unretraction is a pure E move; it restores what the retractions removed.
    std::map<int, std::set<double>> unretract_per_step;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.cmd_is("G1") && line.has_e() && line.e() > 0 && !line.has_x() && !line.has_y() && self.z() > 0.21)
            unretract_per_step[int(std::floor((self.z() - EPSILON) / 5.))].insert(std::round(line.e() * 100.) / 100.);
    });
    REQUIRE(unretract_per_step.size() == 4);
    for (const auto &[idx, lengths] : unretract_per_step)
        REQUIRE(lengths == std::set<double>{ 0.5 + 0.5 * idx });
}

TEST_CASE("Calibration: perimeter speed (VFA)", "[Calibration]")
{
    const std::string gcode = slice_calibration("perimeter_speed", 40., 100., 20., "marlin2");
    std::string type;
    size_t checked = 0;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.raw().rfind(";TYPE:", 0) == 0)
            type = line.raw().substr(6);
        else if (line.cmd_is("G1") && line.has_e() && line.e() > 0 && line.dist_XY(self) > 0 &&
                 type == "External perimeter" && self.z() > 0.21) {
            REQUIRE(self.f() == Approx(60. * expected(40., 100., 20., self.z())));
            ++ checked;
        }
    });
    REQUIRE(checked > 0);
}

TEST_CASE("Calibration: maximum volumetric speed", "[Calibration]")
{
    const std::string gcode = slice_calibration("volumetric_speed", 4., 10., 2., "marlin2");
    const double filament_area = 0.25 * M_PI * 1.75 * 1.75;
    std::string type;
    size_t checked = 0;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.raw().rfind(";TYPE:", 0) == 0)
            type = line.raw().substr(6);
        else if (line.cmd_is("G1") && line.has_e() && line.e() > 0 && line.dist_XY(self) > 1. &&
                 type == "External perimeter" && self.z() > 0.21) {
            const double flow = self.f() / 60. * line.e() / line.dist_XY(self) * filament_area;
            REQUIRE(flow == Approx(expected(4., 10., 2., self.z())).epsilon(0.02));
            ++ checked;
        }
    });
    REQUIRE(checked > 0);
}

TEST_CASE("Calibration: acceleration, cornering and input shaping", "[Calibration]")
{
    SECTION("Acceleration") {
        const std::string gcode = slice_calibration("acceleration", 1000., 4000., 1000., "marlin2");
        const std::vector<double> v = command_values(gcode, "M204", "P");
        REQUIRE(std::set<double>(v.begin(), v.end()) == std::set<double>{ 1000., 2000., 3000., 4000. });
    }
    SECTION("Cornering: Marlin jerk and junction deviation") {
        REQUIRE(slice_calibration("cornering", 8., 14., 2., "marlin2").find("\nM205 X14.00 Y14.00") != std::string::npos);
        REQUIRE(slice_calibration("cornering", 0.02, 0.08, 0.02, "marlin2").find("\nM205 J0.080") != std::string::npos);
    }
    SECTION("Cornering: Klipper") {
        const std::string gcode = slice_calibration("cornering", 2., 8., 2., "klipper");
        const std::vector<double> v = command_values(gcode, "SET_VELOCITY_LIMIT", "SQUARE_CORNER_VELOCITY=");
        REQUIRE(std::set<double>(v.begin(), v.end()) == std::set<double>{ 2., 4., 6., 8. });
    }
    SECTION("Input shaping") {
        const std::vector<double> v = command_values(slice_calibration("input_shaping", 20., 50., 10., "marlin2"), "M593", "F");
        REQUIRE(std::set<double>(v.begin(), v.end()) == std::set<double>{ 20., 30., 40., 50. });
    }
}

TEST_CASE("Calibration: disabled by default", "[Calibration]")
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    const std::string gcode = Test::slice({ Test::TestMesh::cube_20x20x20 }, config);
    REQUIRE(gcode.find("; calibration") == std::string::npos);
    REQUIRE(gcode.find("M117") == std::string::npos);
}
