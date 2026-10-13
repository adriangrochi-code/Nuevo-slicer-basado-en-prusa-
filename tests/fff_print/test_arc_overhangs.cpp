#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

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
    // ... reaching far out over the cantilever (the column is 10 mm wide, the cantilever 10 mm long).
    REQUIRE(arcs.max_x - arcs.min_x > 9.5);
}

// Length of the overhang perimeters by feed rate (mm/min).
static std::map<int, double> overhang_feeds(const std::string &gcode)
{
    std::map<int, double> out;
    GCodeReader parser;
    std::string type;
    parser.parse_buffer(gcode, [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.raw().rfind(";TYPE:", 0) == 0)
            type = line.raw().substr(6);
        else if (line.extruding(self) && line.dist_XY(self) > 0 && type == "Overhang perimeter")
            out[int(std::round(line.new_F(self)))] += line.dist_XY(self);
    });
    return out;
}

static std::string slice_cantilever(double arc_speed, double max_distance)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config_with({
        { "overhang_arcs",                 true },
        { "overhang_arcs_speed",           arc_speed },
        { "overhang_arcs_max_distance",    max_distance },
        { "extra_perimeters_on_overhangs", false },
        { "overhangs",                     true },
        { "support_material",              false },
        { "skirts",                        0 },
        { "layer_height",                  0.2 },
        { "first_layer_height",            0.2 },
        { "perimeters",                    2 },
        { "bridge_speed",                  30 },
    });
    return Test::slice({ cantilever() }, config);
}

TEST_CASE("Arc overhangs are printed at their own speed", "[ArcOverhangs]")
{
    // 4 mm/s: most of the overhang is printed at 240 mm/min.
    std::map<int, double> feeds = overhang_feeds(slice_cantilever(4., 0.));
    double total = 0.;
    for (const auto &[f, l] : feeds)
        total += l;
    CHECK(feeds[240] > 0.5 * total);
    // Zero: the bridge speed.
    feeds = overhang_feeds(slice_cantilever(0., 0.));
    CHECK(feeds.count(240) == 0);
    CHECK(feeds[1800] > 0.);
}

TEST_CASE("Arc overhangs stop at the maximum distance", "[ArcOverhangs]")
{
    // The arcs grow from the column (X 0..10) over the 10 mm cantilever; limited to 5 mm they cover less of it.
    auto arc_extent = [](double max_distance) {
        double max_x = -1e10, min_x = 1e10, length = 0.;
        GCodeReader parser;
        std::string type;
        parser.parse_buffer(slice_cantilever(4., max_distance), [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
            if (line.raw().rfind(";TYPE:", 0) == 0)
                type = line.raw().substr(6);
            else if (line.extruding(self) && line.dist_XY(self) > 0 && type == "Overhang perimeter" &&
                     std::abs(line.new_F(self) - 240.f) < 1.f) {
                length += line.dist_XY(self);
                max_x = std::max(max_x, double(line.new_X(self)));
                min_x = std::min(min_x, double(line.new_X(self)));
            }
        });
        return std::make_pair(max_x - min_x, length);
    };
    const auto [full_extent, full_length]       = arc_extent(0.);
    const auto [limited_extent, limited_length] = arc_extent(5.);
    CHECK(full_extent > 9.5);
    CHECK(limited_extent < 6.);
    CHECK(limited_length < 0.7 * full_length);
}
