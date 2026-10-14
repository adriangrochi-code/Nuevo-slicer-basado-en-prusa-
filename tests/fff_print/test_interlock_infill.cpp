#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>

#include "test_data.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;

static TriangleMesh block()
{
    return TriangleMesh(its_make_cube(30., 30., 6.));
}

static DynamicPrintConfig interlock_config(bool enabled)
{
    return DynamicPrintConfig::full_print_config_with({
        { "interlock_infill",           enabled },
        { "interlock_infill_amplitude", "50%" },
        { "interlock_infill_wavelength", 10. },
        { "use_relative_e_distances",   true },
        { "arc_fitting",                "disabled" },
        { "skirts",                     0 },
        { "layer_height",               0.2 },
        { "first_layer_height",         0.2 },
        { "start_gcode",                "" },
        { "bottom_solid_layers",        3 },
        { "top_solid_layers",           3 },
        { "fill_density",               "20%" },
        { "fill_pattern",               "rectilinear" },
    });
}

struct Moves
{
    // Z of the extrusions by the Z of their layer (";Z:" tag), for the sparse infill and for everything else.
    std::map<double, std::set<double>> infill, other;
    double filament { 0. };
};

static Moves analyse(const std::string &gcode, const DynamicPrintConfig &config)
{
    Moves out;
    double layer = 0.;
    bool infill = false;
    GCodeReader parser;
    parser.apply_config(config);
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.comment().rfind("Z:", 0) == 0)
            layer = std::stod(std::string(line.comment().substr(2)));
        if (line.comment().rfind("TYPE:", 0) == 0)
            infill = line.comment() == "TYPE:Internal infill";
        if (line.cmd_is("G1") && line.extruding(self)) {
            out.filament += line.dist_E(self);
            if (line.dist_XY(self) > 0)
                (infill ? out.infill : out.other)[layer].insert(std::round(line.new_Z(self) * 1000.) / 1000.);
        }
    });
    return out;
}

TEST_CASE("Interlocking infill puts the sparse infill on waves", "[Interlock]")
{
    const Moves planar = analyse(Test::slice({ block() }, interlock_config(false)), interlock_config(false));
    Print print;
    Model model;
    Test::init_print({ block() }, print, model, interlock_config(true));
    REQUIRE(print.validate().empty());
    const Moves waved = analyse(Test::gcode(print), interlock_config(true));

    REQUIRE(! waved.infill.empty());
    REQUIRE(waved.infill.size() == planar.infill.size());
    // Within the amplitude (50 % of 0.2 mm) around the layer, and the full amplitude somewhere.
    double spread = 0.;
    for (const auto &[layer, zs] : waved.infill) {
        REQUIRE(*zs.begin() >= layer - 0.1 - 1e-3);
        REQUIRE(*zs.rbegin() <= layer + 0.1 + 1e-3);
        spread = std::max(spread, *zs.rbegin() - *zs.begin());
    }
    REQUIRE(spread > 0.15);
    // Flat next to the solid layers: the first and the last layer of sparse infill.
    REQUIRE(waved.infill.begin()->second.size() == 1);
    REQUIRE(waved.infill.rbegin()->second.size() == 1);
    // At most a fifth of the layer height more from one layer to the next: the second layer stays within 0.04 mm.
    const auto second = std::next(waved.infill.begin());
    REQUIRE(*second->second.begin() >= second->first - 0.04 - 1e-3);
    REQUIRE(*second->second.rbegin() <= second->first + 0.04 + 1e-3);
    // Everything else at the Z of its layer.
    for (const auto &[layer, zs] : waved.other)
        REQUIRE(zs == std::set<double>{ layer });
    // The same material, spread over thicker and thinner places.
    REQUIRE(std::abs(waved.filament - planar.filament) < 0.01 * planar.filament);
}

TEST_CASE("Interlocking infill keeps absolute extruder distances consistent", "[Interlock]")
{
    DynamicPrintConfig relative = interlock_config(true), absolute = interlock_config(true);
    absolute.set_deserialize_strict({ { "use_relative_e_distances", false } });
    const double e_relative = analyse(Test::slice({ block() }, relative), relative).filament;
    const double e_absolute = analyse(Test::slice({ block() }, absolute), absolute).filament;
    REQUIRE(std::abs(e_absolute - e_relative) < 0.005 * e_relative);
}

TEST_CASE("Interlocking infill is rejected with incompatible settings", "[Interlock]")
{
    for (const auto &[key, value] : std::vector<std::pair<std::string, std::string>>{
             { "arc_fitting", "emit_center" }, { "spiral_vase", "1" }, { "nonplanar_mode", "wave" } }) {
        DynamicPrintConfig config = interlock_config(true);
        config.set_deserialize_strict(key, value);
        Print print;
        Model model;
        Test::init_print({ block() }, print, model, config);
        REQUIRE(print.validate().find("Interlocking infill") != std::string::npos);
    }
}
