#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "test_data.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/TriangleMesh.hpp"

using namespace Slic3r;

// A 10x10x10 mm column carrying a 20x10x2 mm slab that sticks out 10 mm on the +X side,
// a cantilever that cannot be bridged.
static TriangleMesh cantilever()
{
    indexed_triangle_set column = its_make_cube(10., 10., 10.);
    indexed_triangle_set slab   = its_make_cube(20., 10., 2.);
    for (stl_vertex &v : slab.vertices)
        v.z() += 10.f;
    its_merge(column, slab);
    return TriangleMesh(std::move(column));
}

struct OverhangStats
{
    double length { 0. };
    double max_x  { -1e10 };
    double min_x  { 1e10 };
};

static OverhangStats overhang_stats(bool arcs)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config_with({
        { "overhang_arcs",                 arcs },
        { "extra_perimeters_on_overhangs", false },
        { "overhangs",                     true },
        { "support_material",              false },
        { "skirts",                        0 },
        { "layer_height",                  0.2 },
        { "first_layer_height",            0.2 },
        { "perimeters",                    2 },
    });
    std::string gcode = Test::slice({ cantilever() }, config);

    OverhangStats stats;
    GCodeReader parser;
    std::string type;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.raw().rfind(";TYPE:", 0) == 0)
            type = line.raw().substr(6);
        else if (line.extruding(self) && line.dist_XY(self) > 0 && type == "Overhang perimeter") {
            stats.length += line.dist_XY(self);
            stats.max_x = std::max(stats.max_x, double(line.new_X(self)));
            stats.min_x = std::min(stats.min_x, double(line.new_X(self)));
        }
    });
    return stats;
}

TEST_CASE("Arc overhangs cover a cantilever with arcs", "[ArcOverhangs]")
{
    OverhangStats planar = overhang_stats(false);
    OverhangStats arcs   = overhang_stats(true);
    // The arcs add a lot of overhang extrusions ...
    REQUIRE(arcs.length > planar.length + 50.);
    // ... reaching far out over the cantilever (10 mm long).
    REQUIRE(arcs.max_x - arcs.min_x > 15.);
}
