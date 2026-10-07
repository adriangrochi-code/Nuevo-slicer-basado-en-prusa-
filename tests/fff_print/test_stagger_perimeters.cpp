#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <map>
#include <vector>

#include "test_data.hpp"
#include "libslic3r/GCodeReader.hpp"

using namespace Slic3r;
using Catch::Approx;

namespace {

// Extrusion per mm of every perimeter move, by Z (in tenths of the layer height) and by type.
struct Moves {
    // Z rounded to 0.01 mm -> extrusion per mm of each move.
    std::map<double, std::vector<double>> internal;
    std::map<double, std::vector<double>> external;
};

Moves perimeter_moves(bool stagger)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config_with({
        { "perimeters",               3 },
        { "stagger_perimeters",       stagger },
        { "fill_density",             "15%" },
        { "top_solid_layers",         3 },
        { "bottom_solid_layers",      3 },
        { "skirts",                   0 },
        { "layer_height",             0.2 },
        { "first_layer_height",       0.2 },
        { "perimeter_extrusion_width",       0.45 },
        { "external_perimeter_extrusion_width", 0.45 },
        { "first_layer_extrusion_width",     0.45 },
        { "use_relative_e_distances", true },
        { "retract_lift",             "0" },
    });
    const std::string gcode = Test::slice({ Test::TestMesh::cube_20x20x20 }, config);

    Moves out;
    std::string type;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.raw().rfind(";TYPE:", 0) == 0)
            type = line.raw().substr(6);
        else if (line.cmd_is("G1") && line.has_e() && line.e() > 0 && line.dist_XY(self) > 1.) {
            const double z = std::round(self.z() * 100.) / 100.;
            if (type == "Perimeter")
                out.internal[z].push_back(line.e() / line.dist_XY(self));
            else if (type == "External perimeter")
                out.external[z].push_back(line.e() / line.dist_XY(self));
        }
    });
    return out;
}

// Z between two layers (layer height 0.2 mm).
bool half_layer(double z) { return std::abs(std::fmod(z + 1e-6, 0.2) - 0.1) < 0.01; }

double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

TEST_CASE("Staggered perimeters", "[StaggerPerimeters]")
{
    const Moves plain     = perimeter_moves(false);
    const Moves staggered = perimeter_moves(true);

    SECTION("Without the option every perimeter is at the layer height") {
        for (const auto &[z, e] : plain.internal)
            CHECK(! half_layer(z));
    }
    SECTION("The external perimeter is never moved") {
        for (const auto &[z, e] : staggered.external)
            CHECK(! half_layer(z));
        CHECK(staggered.external.size() == plain.external.size());
    }
    SECTION("Every second internal perimeter is half a layer higher on the layers below the top") {
        size_t shifted = 0;
        for (const auto &[z, e] : staggered.internal)
            if (half_layer(z)) {
                ++ shifted;
                // The bead fills the same height as the others in the middle of the wall.
                if (z > 1. && z < 19.)
                    CHECK(median(e) == Approx(median(plain.internal.at(std::round((z - 0.1) * 100.) / 100.))).epsilon(0.05));
            }
        // All the layers but the last one (99 of 100).
        CHECK(shifted == 99);
    }
    SECTION("First layer: 1.5 times the flow; last layer: back to the layer height with half the flow") {
        // First layer: the staggered perimeter goes from the bed up to half a layer above the first layer.
        REQUIRE(staggered.internal.count(0.3) == 1);
        const double first_plain = median(plain.internal.at(0.2));
        CHECK(median(staggered.internal.at(0.3)) == Approx(1.5 * first_plain).epsilon(0.05));
        // Last layer: two flows at the same Z, the second internal perimeter (full) and the first one (half).
        const auto &top = staggered.internal.at(20.);
        const double hi = *std::max_element(top.begin(), top.end());
        const double lo = *std::min_element(top.begin(), top.end());
        CHECK(lo == Approx(0.5 * hi).epsilon(0.08));
    }
}
