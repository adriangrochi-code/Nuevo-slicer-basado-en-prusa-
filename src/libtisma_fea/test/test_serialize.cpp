// Tisma: results of the structural analysis in a file (.tfea) and the qualities of the analysis.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <sstream>

#include <libslic3r/TriangleMesh.hpp>

#include "tisma_fea/Analysis.hpp"
#include "tisma_fea/Quality.hpp"
#include "tisma_fea/Serialize.hpp"

using namespace Slic3r;
using namespace Slic3r::Fea;
using Catch::Approx;

namespace {

Result small_analysis()
{
    const indexed_triangle_set its = its_make_cube(30., 6., 6.);
    Setup setup;
    setup.voxel_size = 1.5;
    setup.gpu        = false;
    Fixture fixture;
    Load    load;
    load.type  = Load::Type::Faces;
    load.force = Vec3d(0., 0., -50.);
    for (size_t i = 0; i < its.indices.size(); ++ i) {
        const Vec3f n = its_face_normal(its, int(i));
        if (n.x() < -0.99f) fixture.triangles.push_back(int(i));
        if (n.x() > 0.99f)  load.triangles.push_back(int(i));
    }
    setup.fixtures.push_back(fixture);
    setup.loads.push_back(load);
    setup.limits.push_back({ load.triangles, 0.05 });
    return analyze(its, setup);
}

void check_same(const Result &a, const Result &b)
{
    CHECK(a.ok == b.ok);
    CHECK(a.error == b.error);
    CHECK(a.origin == b.origin);
    CHECK(a.h == b.h);
    CHECK(a.size == b.size);
    CHECK(a.voxels == b.voxels);
    CHECK(a.von_mises == b.von_mises);
    CHECK(a.failure_index == b.failure_index);
    CHECK(a.displacement == b.displacement);
    CHECK(a.stress == b.stress);
    CHECK(a.density == b.density);
    CHECK(a.interior == b.interior);
    CHECK(a.mass == b.mass);
    CHECK(a.solid_mass == b.solid_mass);
    CHECK(a.max_displacement == b.max_displacement);
    CHECK(a.max_von_mises == b.max_von_mises);
    CHECK(a.max_failure_index == b.max_failure_index);
    CHECK(a.safety_factor == b.safety_factor);
    CHECK(a.critical_point == b.critical_point);
    CHECK(a.temperature_factor == b.temperature_factor);
    CHECK(a.layer_adhesion_factor == b.layer_adhesion_factor);
    CHECK(a.load_displacement == b.load_displacement);
    CHECK(a.limit_displacement == b.limit_displacement);
    CHECK(a.verdict == b.verdict);
    CHECK(a.alternatives == b.alternatives);
    CHECK(a.iterations == b.iterations);
    CHECK(a.multigrid_levels == b.multigrid_levels);
    CHECK(a.gpu_device == b.gpu_device);
    CHECK(a.layer_orientations == b.layer_orientations);
    CHECK(a.aligned_voxels == b.aligned_voxels);
    CHECK(a.residual == b.residual);
}

void check_same(const indexed_triangle_set &a, const indexed_triangle_set &b)
{
    CHECK(a.vertices == b.vertices);
    CHECK(a.indices == b.indices);
}

ResultFile round_trip(const ResultFile &file)
{
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(write_result_file(ss, file));
    ResultFile back;
    std::string error;
    REQUIRE(read_result_file(ss, back, error));
    CHECK(error.empty());
    CHECK(back.kind == file.kind);
    CHECK(back.info == file.info);
    return back;
}

} // namespace

TEST_CASE("Analysis result file round trip", "[FEA][Serialize]")
{
    ResultFile file;
    file.kind = ResultFile::Kind::Analysis;
    file.analysis = small_analysis();
    REQUIRE(file.analysis.ok);
    REQUIRE(! file.analysis.voxels.empty());
    file.analysis.alternatives = { { "PETG", 2.5 }, { "PC", 4.25 } };
    file.info["material"] = "PLA";
    file.info["quality"]  = "normal";
    const ResultFile back = round_trip(file);
    check_same(back.analysis, file.analysis);
}

TEST_CASE("Lightest infill and resin result files round trip", "[FEA][Serialize]")
{
    const Result analysis = small_analysis();
    REQUIRE(analysis.ok);

    ResultFile infill;
    infill.kind = ResultFile::Kind::LightestInfill;
    infill.info["current_mass"] = "12.5";
    OptimizeResult &opt = infill.infill;
    opt.ok = true;
    opt.feasible = true;
    opt.uniform_density = 0.15;
    opt.uniform = analysis;
    RibZone rib;
    rib.mesh = its_make_cube(3., 3., 3.);
    rib.angle_deg = 45.;
    rib.directionality = 0.8;
    rib.cells = 7;
    opt.ribs.push_back(rib);
    opt.density_without_ribs = 0.2;
    opt.zones_found = true;
    opt.base_density = 0.1;
    InfillZone zone;
    zone.mesh = its_make_cube(2., 4., 6.);
    zone.density = 0.4;
    zone.aligned_angle_deg = 30.;
    zone.volume_fraction = true;
    zone.homogenize_cell = 3.;
    opt.zones.push_back(zone);
    opt.zones.push_back(InfillZone());   // NaN angle by default
    opt.zoned = analysis;
    opt.analyses = 17;

    const ResultFile b = round_trip(infill);
    CHECK(b.infill.ok);
    CHECK(b.infill.feasible);
    CHECK(b.infill.uniform_density == 0.15);
    check_same(b.infill.uniform, analysis);
    check_same(b.infill.zoned, analysis);
    REQUIRE(b.infill.ribs.size() == 1);
    check_same(b.infill.ribs[0].mesh, rib.mesh);
    CHECK(b.infill.ribs[0].angle_deg == 45.);
    CHECK(b.infill.ribs[0].directionality == 0.8);
    CHECK(b.infill.ribs[0].cells == 7);
    CHECK(b.infill.density_without_ribs == 0.2);
    CHECK(b.infill.zones_found);
    CHECK(b.infill.base_density == 0.1);
    REQUIRE(b.infill.zones.size() == 2);
    check_same(b.infill.zones[0].mesh, zone.mesh);
    CHECK(b.infill.zones[0].density == 0.4);
    CHECK(b.infill.zones[0].aligned_angle_deg == 30.);
    CHECK(b.infill.zones[0].volume_fraction);
    CHECK(b.infill.zones[0].homogenize_cell == 3.);
    CHECK(std::isnan(b.infill.zones[1].aligned_angle_deg));
    CHECK(b.infill.analyses == 17);

    ResultFile resin;
    resin.kind = ResultFile::Kind::ResinStructure;
    resin.resin.ok = true;
    resin.resin.options.push_back({ 1.5, 0.1, true, analysis });
    resin.resin.options.push_back({ 3., 0., false, Result() });
    resin.resin.solid = analysis;
    resin.resin.best = 0;
    resin.resin.analyses = 9;
    const ResultFile r = round_trip(resin);
    REQUIRE(r.resin.options.size() == 2);
    CHECK(r.resin.options[0].wall_thickness == 1.5);
    CHECK(r.resin.options[0].infill_density == 0.1);
    CHECK(r.resin.options[0].feasible);
    check_same(r.resin.options[0].result, analysis);
    CHECK(! r.resin.options[1].feasible);
    check_same(r.resin.solid, analysis);
    CHECK(r.resin.best == 0);
    CHECK(r.resin.analyses == 9);
}

TEST_CASE("Damaged result files are refused", "[FEA][Serialize]")
{
    ResultFile file;
    file.analysis = small_analysis();
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(write_result_file(ss, file));
    const std::string data = ss.str();
    for (size_t len : { size_t(0), size_t(3), size_t(9), data.size() / 2, data.size() - 1 }) {
        std::stringstream cut(data.substr(0, len), std::ios::in | std::ios::binary);
        ResultFile back;
        std::string error;
        CHECK(! read_result_file(cut, back, error));
        CHECK(! error.empty());
    }
    std::string other = data;
    other[0] = 'X';
    std::stringstream bad(other, std::ios::in | std::ios::binary);
    ResultFile back;
    std::string error;
    CHECK(! read_result_file(bad, back, error));
}

TEST_CASE("Qualities of the analysis", "[FEA][Quality]")
{
    const double volume = 60000.;   // mm³
    bool limited = true;
    CHECK(quality_voxel_size(Quality::Fast, volume, 0.45, MAX_VOXELS_SINGLE, &limited) == Approx(std::cbrt(volume / FAST_VOXELS)));
    CHECK(! limited);
    CHECK(quality_voxel_size(Quality::Normal, volume, 0.45, MAX_VOXELS_SINGLE) == Approx(1.));
    CHECK(quality_voxel_size(Quality::High, volume, 0.45, MAX_VOXELS_SINGLE) == Approx(0.9));
    // Ultra: 0.45 mm voxels would be 658 000 voxels, more than the jobs of many analyses allow.
    CHECK(quality_voxel_size(Quality::Ultra, volume, 0.45, MAX_VOXELS_SINGLE, &limited) == Approx(0.45));
    CHECK(! limited);
    CHECK(quality_voxel_size(Quality::Ultra, volume, 0.45, MAX_VOXELS_MANY, &limited) == Approx(std::cbrt(volume / MAX_VOXELS_MANY)));
    CHECK(limited);
    for (Quality q : { Quality::Fast, Quality::Normal, Quality::High, Quality::Ultra }) {
        Quality parsed = Quality::Fast;
        REQUIRE(parse_quality(quality_name(q), parsed));
        CHECK(parsed == q);
    }
    Quality parsed;
    CHECK(! parse_quality("best", parsed));
}
