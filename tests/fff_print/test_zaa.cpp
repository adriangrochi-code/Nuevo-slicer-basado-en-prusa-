#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>

#include "test_data.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/ZAntiAliasing.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;

// Block 20 x 20 mm whose top slopes along X from z = 5 to z = 6 (about 2.9°): with 0.2 mm layers the top is printed
// as five stairs.
static TriangleMesh wedge()
{
    indexed_triangle_set its;
    its.vertices = { { 0.f, 0.f, 0.f }, { 20.f, 0.f, 0.f }, { 20.f, 20.f, 0.f }, { 0.f, 20.f, 0.f },
                     { 0.f, 0.f, 5.f }, { 20.f, 0.f, 6.f }, { 20.f, 20.f, 6.f }, { 0.f, 20.f, 5.f } };
    its.indices = { { 0, 2, 1 }, { 0, 3, 2 }, { 4, 5, 6 }, { 4, 6, 7 }, { 0, 1, 5 }, { 0, 5, 4 },
                    { 1, 2, 6 }, { 1, 6, 5 }, { 2, 3, 7 }, { 2, 7, 6 }, { 3, 0, 4 }, { 3, 4, 7 } };
    return TriangleMesh(std::move(its));
}

static DynamicPrintConfig zaa_config(bool enabled)
{
    return DynamicPrintConfig::full_print_config_with({
        { "zaa_enabled",              enabled },
        { "use_relative_e_distances", true },
        { "arc_fitting",              "disabled" },
        { "skirts",                   0 },
        { "layer_height",             0.2 },
        { "first_layer_height",       0.2 },
        { "start_gcode",              "" },
        { "top_solid_layers",         3 },
        { "fill_density",             "0%" },
    });
}

struct TopMoves
{
    // Z of the extrusions of the top infill, by the Z of their layer (";Z:" tag).
    std::map<double, std::set<double>> z_by_layer;
    double filament { 0. };
};

static TopMoves analyse(const std::string &gcode, const DynamicPrintConfig &config)
{
    TopMoves out;
    double layer = 0.;
    bool top = false;
    GCodeReader parser;
    parser.apply_config(config);
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.comment().rfind("Z:", 0) == 0)
            layer = std::stod(std::string(line.comment().substr(2)));
        if (line.comment().rfind("TYPE:", 0) == 0)
            top = line.comment() == "TYPE:Top solid infill";
        if (line.cmd_is("G1") && line.extruding(self)) {
            out.filament += line.dist_E(self);
            if (top && line.dist_XY(self) > 0)
                out.z_by_layer[layer].insert(std::round(line.new_Z(self) * 1000.) / 1000.);
        }
    });
    return out;
}

TEST_CASE("Z anti-aliasing follows a sloped top surface", "[ZAA]")
{
    const TopMoves planar = analyse(Test::slice({ wedge() }, zaa_config(false)), zaa_config(false));
    Print print;
    Model model;
    Test::init_print({ wedge() }, print, model, zaa_config(true));
    REQUIRE(print.validate().empty());
    const TopMoves zaa = analyse(Test::gcode(print), zaa_config(true));

    // Flat layers: the top infill of a layer is at the Z of the layer.
    for (const auto &[layer, zs] : planar.z_by_layer)
        REQUIRE(zs.size() == 1);
    // Anti-aliased: the top infill goes down within its layer (never above it, at most down to 25 % of it).
    bool lowered = false;
    for (const auto &[layer, zs] : zaa.z_by_layer) {
        REQUIRE(*zs.rbegin() <= layer + 1e-3);
        REQUIRE(*zs.begin() >= layer - 0.75 * 0.2 - 1e-3);
        if (zs.size() > 2)
            lowered = true;
    }
    REQUIRE(lowered);
    // Less material: the stairs are no longer filled up to the top of the layer.
    REQUIRE(zaa.filament < planar.filament);
    REQUIRE(zaa.filament > 0.9 * planar.filament);
}

TEST_CASE("Z anti-aliasing keeps absolute extruder distances consistent", "[ZAA]")
{
    DynamicPrintConfig relative = zaa_config(true), absolute = zaa_config(true);
    absolute.set_deserialize_strict({ { "use_relative_e_distances", false } });
    const double e_relative = analyse(Test::slice({ wedge() }, relative), relative).filament;
    const double e_absolute = analyse(Test::slice({ wedge() }, absolute), absolute).filament;
    REQUIRE(std::abs(e_absolute - e_relative) < 0.01 * e_relative);
}

TEST_CASE("Z anti-aliasing is rejected with incompatible settings", "[ZAA]")
{
    for (const auto &[key, value] : std::vector<std::pair<std::string, std::string>>{
             { "arc_fitting", "emit_center" }, { "spiral_vase", "1" }, { "belt_printer", "1" } }) {
        DynamicPrintConfig config = zaa_config(true);
        config.set_deserialize_strict(key, value);
        Print print;
        Model model;
        Test::init_print({ wedge() }, print, model, config);
        REQUIRE(print.validate().find("Z anti-aliasing") != std::string::npos);
    }
}

TEST_CASE("Z anti-aliasing finds the top surface inside a layer", "[ZAA]")
{
    const ZAA::Surface surface(wedge().its);
    // At x = 10 the top is at 5.5.
    REQUIRE(surface.top_in_slab(10., 10., 5.4, 5.6).has_value());
    REQUIRE(std::abs(*surface.top_in_slab(10., 10., 5.4, 5.6) - 5.5) < 1e-4);
    // Below the top: solid all through the layer.
    REQUIRE(! surface.top_in_slab(10., 10., 5.0, 5.2).has_value());
    // Outside of the block.
    REQUIRE(! surface.top_in_slab(30., 10., 5.4, 5.6).has_value());
}

TEST_CASE("Z anti-aliasing works within non-planar layers", "[ZAA][NonPlanar]")
{
    // Waves across the sloped top of the wedge: the anti-aliasing lowers the top moves within the curved layers.
    auto config = [](bool zaa) {
        DynamicPrintConfig c = zaa_config(zaa);
        c.set_deserialize_strict({ { "nonplanar_mode", "wave" }, { "nonplanar_pattern", "ridges" },
                                   { "nonplanar_fit_head", "0" }, { "nonplanar_amplitude", "0.3" },
                                   { "nonplanar_wavelength", "20" }, { "nonplanar_angle", "90" },
                                   { "nonplanar_flat_below", "0.4" }, { "nonplanar_ramp_height", "3" } });
        return c;
    };
    Print print;
    Model model;
    Test::init_print({ wedge() }, print, model, config(true));
    REQUIRE(print.validate().empty());
    const TopMoves curved_zaa = analyse(Test::gcode(print), config(true));
    const TopMoves curved     = analyse(Test::slice({ wedge() }, config(false)), config(false));
    // The top infill is printed in both, with less material where the anti-aliasing lowers it.
    REQUIRE(! curved_zaa.z_by_layer.empty());
    REQUIRE(curved_zaa.filament < curved.filament);
    REQUIRE(curved_zaa.filament > 0.9 * curved.filament);
}
