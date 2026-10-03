#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <map>
#include <set>

#include "libslic3r/NonPlanar.hpp"
#include "libslic3r/TriangleMesh.hpp"

using namespace Slic3r;
using namespace Slic3r::NonPlanar;
using Catch::Approx;

static Deformation make(Mode mode, Pattern pattern = Pattern::Egg, bool flat_top = false)
{
    FieldParams f;
    f.mode = mode;
    f.pattern = pattern;
    f.amplitude = 0.6;
    f.wavelength = 16.;
    f.cone_angle_deg = 10.;
    Ramp r;
    r.z_ramp = mode == Mode::Conical ? 15. : 5.;
    if (flat_top)
        r.z_top = 40.;
    return Deformation(f, r);
}

TEST_CASE("Non-planar deformation round trip", "[NonPlanar]")
{
    for (const Deformation &d : { make(Mode::Wave), make(Mode::Wave, Pattern::Ridges), make(Mode::Wave, Pattern::Twisted),
                                  make(Mode::Conical), make(Mode::Wave, Pattern::Egg, true) }) {
        for (double x = -20.; x <= 20.; x += 3.7)
            for (double y = -20.; y <= 20.; y += 4.1)
                for (double z = 0.; z <= 40.; z += 1.3) {
                    const double zs = d.to_slice_z(x, y, z);
                    REQUIRE(d.to_real_z(x, y, zs) == Approx(z).margin(1e-7));
                }
    }
}

TEST_CASE("Non-planar first layers stay flat", "[NonPlanar]")
{
    const Deformation d = make(Mode::Wave);
    for (double z : { 0., 0.2, 0.35, 0.6 })
        for (double x = -10.; x <= 10.; x += 1.)
            REQUIRE(d.to_real_z(x, 0.5 * x, z) == Approx(z).margin(1e-9));
}

TEST_CASE("Non-planar jacobian matches the numeric derivative", "[NonPlanar]")
{
    for (const Deformation &d : { make(Mode::Wave), make(Mode::Wave, Pattern::Twisted), make(Mode::Conical) }) {
        const double x = 3.1, y = -7.4, h = 1e-5;
        for (double z = 0.5; z < 30.; z += 1.1) {
            const double numeric = h / (d.to_slice_z(x, y, z + h) - d.to_slice_z(x, y, z));
            REQUIRE(d.jacobian(x, y, z) == Approx(numeric).epsilon(1e-4));
        }
    }
}

TEST_CASE("Non-planar validation check", "[NonPlanar]")
{
    BoundingBoxf3 bbox(Vec3d(-10., -10., 0.), Vec3d(10., 10., 20.));
    const Deformation::Check ok = make(Mode::Wave).check(bbox);
    REQUIRE(ok.j_min >= J_MIN);
    REQUIRE(ok.j_max <= J_MAX);
    REQUIRE(ok.max_slope_deg == Approx(std::atan(0.6 * 2. * M_PI / 16.) * 180. / M_PI).margin(1.));

    FieldParams f;
    f.mode = Mode::Wave;
    f.amplitude = 1.;
    Ramp short_ramp;
    short_ramp.z_ramp = 1.;
    REQUIRE(Deformation(f, short_ramp).check(bbox).j_max > J_MAX);
}

TEST_CASE("Non-planar mesh subdivision is watertight and bounded", "[NonPlanar]")
{
    indexed_triangle_set its = its_make_cube(20., 20., 20.);
    const double volume_before = its_volume(its);
    subdivide_mesh(its, 1.5);
    std::map<std::pair<int, int>, int> edges;
    double max_len = 0.;
    for (const stl_triangle_vertex_indices &f : its.indices)
        for (int e = 0; e < 3; ++ e) {
            int a = f[e], b = f[(e + 1) % 3];
            max_len = std::max(max_len, double((its.vertices[a] - its.vertices[b]).norm()));
            ++ edges[{ std::min(a, b), std::max(a, b) }];
        }
    for (const auto &[edge, count] : edges)
        REQUIRE(count == 2);
    REQUIRE(max_len <= 1.5 + 1e-5);
    // Single precision vertices: the splitting points are rounded.
    REQUIRE(its_volume(its) == Approx(volume_before).epsilon(1e-4));
}

static std::vector<Vec3d> extrusion_points(const std::string &gcode, double *e_total = nullptr)
{
    std::vector<Vec3d> out;
    double x = 0., y = 0., z = 0., e = 0.;
    std::istringstream in(gcode);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("G1", 0) != 0)
            continue;
        double de = 0.;
        for (const char axis : { 'X', 'Y', 'Z', 'E' }) {
            const size_t p = line.find(std::string(" ") + axis);
            if (p == std::string::npos || line.find(';') < p)
                continue;
            const double v = std::stod(line.substr(p + 2));
            if (axis == 'X') x = v;
            if (axis == 'Y') y = v;
            if (axis == 'Z') z = v;
            if (axis == 'E') de = v;
        }
        e += de;
        if (de > 0.)
            out.emplace_back(x, y, z);
    }
    if (e_total)
        *e_total = e;
    return out;
}

TEST_CASE("Non-planar G-code filter", "[NonPlanar]")
{
    const std::string planar =
        "G90\nM83\nG1 Z10 F600\nG1 X0 Y4 F3000\n;TYPE:Perimeter\nG1 X40 Y4 E2 F1800\nG1 E-0.8 F2400\n";

    SECTION("disabled field keeps the geometry and does not split moves") {
        const Deformation d(FieldParams{}, Ramp{});
        GCodeFilter filter(d, GCodeFilterParams{});
        double e = 0.;
        const std::vector<Vec3d> pts = extrusion_points(filter.process_layer(planar), &e);
        REQUIRE(pts.size() == 1);
        REQUIRE(pts.front().z() == Approx(10.));
        REQUIRE(e == Approx(2. - 0.8));
    }
    SECTION("wave layers follow the surface and scale the extrusion") {
        const Deformation d = make(Mode::Wave, Pattern::Ridges);
        GCodeFilterParams params;
        params.seg_len = 0.25;
        GCodeFilter filter(d, params);
        double e = 0.;
        const std::string out = filter.process_layer(planar);
        const std::vector<Vec3d> pts = extrusion_points(out, &e);
        REQUIRE(pts.size() > 10);
        double zmin = 1e10, zmax = -1e10;
        for (const Vec3d &p : pts) {
            zmin = std::min(zmin, p.z());
            zmax = std::max(zmax, p.z());
            REQUIRE(p.z() == Approx(d.to_real_z(p.x(), p.y(), 10.)).margin(0.002));
        }
        REQUIRE(zmax - zmin > 1.);
        // The retraction is not scaled.
        REQUIRE(out.find("G1 E-0.8") != std::string::npos);
    }
    SECTION("the Z axis limit is respected") {
        const Deformation d = make(Mode::Wave, Pattern::Ridges);
        GCodeFilterParams params;
        params.z_max_speed = 5.;
        GCodeFilter filter(d, params);
        const std::string out = filter.process_layer(planar);
        REQUIRE(filter.stats().z_limited > 0);
        double x = 0, y = 4, z = 10, f = 0;
        std::istringstream in(out);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("G1 X", 0) != 0)
                continue;
            double nx = x, ny = y, nz = z;
            std::sscanf(line.c_str(), "G1 X%lf Y%lf Z%lf", &nx, &ny, &nz);
            if (const size_t p = line.find(" F"); p != std::string::npos)
                f = std::stod(line.substr(p + 2));
            const double l3 = std::sqrt((nx - x) * (nx - x) + (ny - y) * (ny - y) + (nz - z) * (nz - z));
            if (l3 > 0.)
                REQUIRE(std::abs(nz - z) / l3 * f / 60. <= 5. * 1.01);
            x = nx; y = ny; z = nz;
        }
    }
}
