#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>

#include "test_data.hpp"
#include "libslic3r/GCodeReader.hpp"

using namespace Slic3r;

// Filament used by the sparse infill of each layer.
static std::map<double, double> infill_per_layer(bool dense)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config_with({
        { "fill_density",         "8%" },
        { "fill_pattern",         "rectilinear" },
        { "infill_dense",         dense },
        { "infill_dense_density", "40%" },
        { "top_solid_layers",     4 },
        { "bottom_solid_layers",  3 },
        { "skirts",               0 },
        { "layer_height",         0.2 },
        { "first_layer_height",   0.2 },
        { "use_relative_e_distances", true },
    });
    std::string gcode = Test::slice({ Test::TestMesh::cube_20x20x20 }, config);

    std::map<double, double> out;
    GCodeReader parser;
    std::string type;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.raw().rfind(";TYPE:", 0) == 0)
            type = line.raw().substr(6);
        // Relative E.
        else if (line.cmd_is("G1") && line.has_e() && line.e() > 0 && line.dist_XY(self) > 0 && type == "Internal infill")
            out[std::round(self.z() * 100.) / 100.] += line.e();
    });
    return out;
}

TEST_CASE("Dense infill below top surfaces", "[DenseInfill]")
{
    std::map<double, double> sparse = infill_per_layer(false);
    std::map<double, double> dense  = infill_per_layer(true);
    REQUIRE(sparse.size() == dense.size());

    size_t denser_layers = 0;
    double top_z = 0.;
    for (const auto &[z, e] : dense) {
        REQUIRE(sparse.count(z) == 1);
        if (e > 2. * sparse[z]) {
            ++ denser_layers;
            top_z = z;
        } else
            REQUIRE(std::abs(e - sparse[z]) < 0.05 * sparse[z] + 0.01);
    }
    // Only the last sparse layer below the top solid layers is denser.
    REQUIRE(denser_layers == 1);
    REQUIRE(top_z == sparse.rbegin()->first);
}
