// Tisma Slicer: belt printers with a configurable angle of the gantry.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <libslic3r/BeltPrinter.hpp>
#include <libslic3r/Print.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "test_data.hpp"

using namespace Slic3r;
using Catch::Approx;

namespace {

struct Extrusion { Vec3d machine; int layer; };

// The extrusion moves of the printed part (after the first layer change), in the axes of the machine.
std::vector<Extrusion> extrusions(const std::string &gcode)
{
    std::vector<Extrusion> out;
    std::istringstream in(gcode);
    std::string line;
    Vec3d pos = Vec3d::Zero();
    // Absolute E (the default): a move extrudes when E grows and the head moves (not a retraction / deretraction).
    double e = 0.;
    int layer = -1;
    while (std::getline(in, line)) {
        if (line.rfind(";LAYER_CHANGE", 0) == 0)
            ++ layer;
        if (line.rfind("G92 E0", 0) == 0)
            e = 0.;
        if (layer < 0 || line.size() < 3 || line[0] != 'G' || (line[1] != '0' && line[1] != '1') || line[2] != ' ')
            continue;
        const std::string code = line.substr(0, line.find(';'));
        std::istringstream words(code.substr(2));
        std::string w;
        bool moves = false, extrudes = false;
        while (words >> w) {
            const double v = std::atof(w.c_str() + 1);
            if (w[0] >= 'X' && w[0] <= 'Z') {
                pos[w[0] - 'X'] = v;
                moves = true;
            } else if (w[0] == 'E') {
                extrudes = v > e + 1e-6;
                e = v;
            }
        }
        if (moves && extrudes)
            out.push_back({ pos, layer });
    }
    return out;
}

DynamicPrintConfig belt_config(double angle, bool belt_on_z = false)
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "belt_printer", 1 },
        { "belt_angle", angle },
        { "belt_axis_z", belt_on_z },
        { "layer_height", 0.3 },
        { "first_layer_height", 0.3 },
        { "skirts", 1 },             // disabled by the belt mode
        { "support_material", 1 },   // disabled by the belt mode
        { "gcode_comments", 1 },
    });
    return config;
}

} // namespace

TEST_CASE("Belt frame round trip", "[Belt]")
{
    for (double angle : { 30., 45., 60. }) {
        const Belt::Frame frame(angle);
        const Transform3d to_slicing = frame.world_to_slicing();
        CHECK(to_slicing.linear().determinant() == Approx(1.));
        for (const Vec3d &world : { Vec3d(10., 20., 0.), Vec3d(-5., 3., 7.5), Vec3d(0., 0., 30.) }) {
            const Vec3d slicing = to_slicing * world;
            // Across the layers: the normal of the printing planes.
            CHECK(slicing.z() == Approx(frame.layer_normal_world().dot(world)));
            const Vec3d machine = frame.slicing_to_machine(slicing);
            // Z of the machine is along the gantry: on the belt (world z = 0) it is 0.
            CHECK(machine.z() == Approx(world.z() / frame.sin_a));
            const Vec3d back = frame.machine_to_world(machine);
            CHECK((back - world).norm() < 1e-9);
            // With the slicing frame shifted to start at c = 0.
            // The belt starts at 0 at the first layer: it is shifted, the gantry is the same.
            const Belt::Frame shifted(angle, 12.5);
            const Vec3d machine_shifted = shifted.slicing_to_machine(slicing - Vec3d(0., 0., 12.5));
            CHECK(machine_shifted.x() == Approx(machine.x()));
            CHECK(machine_shifted.y() == Approx(machine.y() - 12.5 / frame.sin_a));
            CHECK(machine_shifted.z() == Approx(machine.z()));
            // The belt on Z, the gantry on Y (BlackBelt, CR-30, IR3 V2): the same axes swapped.
            const Belt::Frame on_z(angle, 0., true);
            const Vec3d machine_z = on_z.slicing_to_machine(slicing);
            CHECK(machine_z.y() == Approx(machine.z()));
            CHECK(machine_z.z() == Approx(machine.y()));
            CHECK((on_z.machine_to_world(machine_z) - world).norm() < 1e-9);
        }
    }
}

TEST_CASE("Belt G-code transform", "[Belt]")
{
    Belt::GCodeTransform transform(Belt::Frame(45.));
    const std::string out = transform.process("M104 S200\nG1 Z0.3 F600\nG1 X10 Y5 E0.5 ; perimeter\nG1 X20 E1\nG10\n");
    const double s = std::sqrt(0.5);
    std::ostringstream expected;
    char buf[200];
    // The first move gives only the layer: the belt moves, X and the gantry wait for their coordinates.
    std::snprintf(buf, sizeof(buf), "M104 S200\nG1 Y%.3f F600\nG1 X10.000 Z%.3f E0.5 ; perimeter\nG1 X20.000 E1\nG10\n",
                  0.3 / s, -5. + 0.3);
    CHECK(out == std::string(buf));
}

TEST_CASE("Belt printer: the part is printed on the belt", "[Belt]")
{
    for (const auto [angle, belt_on_z] : { std::pair{ 45., false }, std::pair{ 30., false }, std::pair{ 45., true } }) {
        Print print;
        Model model;
        Test::init_print({ Test::TestMesh::cube_20x20x20 }, print, model, belt_config(angle, belt_on_z));
        print.process();
        const std::string gcode = Test::gcode(print);
        // Supports and skirt are disabled on a belt.
        CHECK(print.objects().front()->support_layers().empty());

        const std::vector<Extrusion> moves = extrusions(gcode);
        REQUIRE(moves.size() > 100);
        const Belt::Frame frame(angle, 0., belt_on_z);
        // The machine axis of the belt.
        const int belt = belt_on_z ? 2 : 1;
        BoundingBoxf3 world;
        int    layer = -1;
        double layer_y = 0.;
        double last_y  = -1e10;
        bool   y_fixed_in_layers = true, y_grows = true;
        for (const Extrusion &e : moves) {
            world.merge(frame.machine_to_world(e.machine));
            if (e.layer != layer) {
                // The belt moves between layers, forward.
                y_grows = y_grows && e.machine[belt] > last_y - 1e-6;
                layer   = e.layer;
                layer_y = e.machine[belt];
                last_y  = layer_y;
            } else
                y_fixed_in_layers = y_fixed_in_layers && std::abs(e.machine[belt] - layer_y) < 1e-3;
        }
        CHECK(y_fixed_in_layers);
        CHECK(y_grows);
        // The belt starts at the first layer (the start G-code sets it to 0), wherever the part is on the bed.
        CHECK(moves.front().machine[belt] == Approx(0.3 / frame.sin_a).margin(0.01));
        // No move of the gantry far from the part: the highest point of the cube is 20 mm above the belt.
        double gantry_max = 0.;
        std::istringstream lines(gcode);
        for (std::string line; std::getline(lines, line);)
            if (line.rfind("G1 ", 0) == 0)
                if (const size_t pos = line.find(belt_on_z ? " Y" : " Z"); pos != std::string::npos)
                    gantry_max = std::max(gantry_max, std::atof(line.c_str() + pos + 2));
        CHECK(gantry_max < 20. / frame.sin_a + 5.);
        // The extrusions fill the cube as placed on the belt (within half a line width), nothing below the belt.
        const Vec3d size = world.size();
        INFO("angle " << angle << ", belt on Z " << belt_on_z << ", size " << size.transpose() << ", min " << world.min.transpose());
        CHECK(size.x() == Approx(20.).margin(1.));
        CHECK(size.y() == Approx(20.).margin(1.));
        CHECK(size.z() == Approx(20.).margin(1.));
        CHECK(world.min.z() > -0.05);
        // Layers stacked across the printing planes: the extent of the cube along their normal.
        const double extent = 20. * (frame.sin_a + frame.cos_a);
        CHECK(layer > 0.8 * extent / 0.3);
    }
}
