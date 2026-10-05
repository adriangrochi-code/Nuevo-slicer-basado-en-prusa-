// Tisma: validation of the structural analysis against analytical solutions (phase 5).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>

#include <libslic3r/TriangleMesh.hpp>

#include "tisma_fea/Analysis.hpp"
#include "tisma_fea/Materials.hpp"
#include "tisma_fea/ModelSetup.hpp"

#include <libslic3r/Format/3mf.hpp>
#include <libslic3r/Model.hpp>

#include <boost/filesystem/operations.hpp>

using namespace Slic3r;
using namespace Slic3r::Fea;
using Catch::Approx;

// Triangles of the box whose normal is along the axis with the sign.
static std::vector<int> side(const indexed_triangle_set &its, int axis, bool positive)
{
    std::vector<int> out;
    for (size_t i = 0; i < its.indices.size(); ++ i) {
        const Vec3f n = its_face_normal(its, int(i));
        if (std::abs(n[axis]) > 0.99f && (n[axis] > 0.f) == positive)
            out.push_back(int(i));
    }
    return out;
}

// Bar clamped at x = 0 and pulled or bent by a force on the face x = length.
static Result bar(double length, double width, double height, const Vec3d &force, const std::string &material,
                  double temperature, double voxel, int axis = 0)
{
    Vec3d dims(width, width, width);
    dims[axis] = length;
    if (axis != 2)
        dims.z() = height;
    const indexed_triangle_set its = its_make_cube(dims.x(), dims.y(), dims.z());
    Setup setup;
    setup.material    = material;
    setup.temperature = temperature;
    setup.voxel_size  = voxel;
    setup.tolerance   = 1e-9;
    setup.fixtures.push_back({ side(its, axis, false) });
    Load load;
    load.type      = Load::Type::Faces;
    load.triangles = side(its, axis, true);
    load.force     = force;
    setup.loads.push_back(load);
    return analyze(its, setup);
}

// Displacement along the axis of the voxels at the loaded end.
static double end_displacement(const Result &res, int axis, int component)
{
    double sum = 0.;
    int    n = 0;
    for (size_t e = 0; e < res.voxels.size(); ++ e) {
        const int idx = res.voxels[e];
        const int ijk[3] = { idx % res.size.x(), (idx / res.size.x()) % res.size.y(), idx / (res.size.x() * res.size.y()) };
        if (ijk[axis] == res.size[axis] - 1) {
            sum += res.displacement[e][component];
            ++ n;
        }
    }
    return n ? sum / n : 0.;
}

TEST_CASE("Material table", "[FEA]")
{
    REQUIRE(find_material("pla") != nullptr);
    CHECK(material_for_filament_type("PETG")->key == "PET");
    CHECK(material_for_filament_type("NYLON")->key == "PA");
    CHECK(material_for_filament_type("PA12-CF")->key == "PA-CF");
    CHECK(material_for_filament_type("SCAFF") == nullptr);
    const Material &pla = *find_material("PLA");
    CHECK(temperature_factor(pla, 20.) == Approx(1.));
    CHECK(temperature_factor(pla, pla.max_service_temperature) == Approx(0.5));
    CHECK(temperature_factor(pla, 130.) == Approx(0.02));
    CHECK(out_of_temperature(pla, 130.));
    CHECK(! out_of_temperature(*find_material("PEI"), 130.));
    for (const Material &m : materials()) {
        CHECK(m.E_xy > 0.);
        CHECK(m.E_z > 0.);
        CHECK(m.E_z <= m.E_xy);
        CHECK(m.strength_z <= m.strength_xy);
    }
}

TEST_CASE("Bar in tension", "[FEA]")
{
    // delta = F L / (E A): 1000 N on 10 x 10 mm, 50 mm long, PLA E_xy = 3000 MPa -> 0.1667 mm; stress 10 MPa.
    const Result res = bar(50., 10., 10., Vec3d(1000., 0., 0.), "PLA", 23., 1.);
    REQUIRE(res.ok);
    INFO("iterations " << res.iterations);
    const double expected = 1000. * 50. / (3000. * 100.);
    // The clamped end stops the lateral contraction, which stiffens the bar a little.
    CHECK(end_displacement(res, 0, 0) == Approx(expected).epsilon(0.05));
    // Stress far from the ends.
    double vm_mid = 0.;
    int    n = 0;
    for (size_t e = 0; e < res.voxels.size(); ++ e)
        if (res.voxels[e] % res.size.x() == res.size.x() / 2) {
            vm_mid += res.von_mises[e];
            ++ n;
        }
    CHECK(vm_mid / n == Approx(10.).epsilon(0.03));
    CHECK(res.verdict == Verdict::Holds);
}

TEST_CASE("Cantilever converges to the beam theory", "[FEA]")
{
    // delta = F L^3 / (3 E I) + shear: 10 N at the end of a 100 x 10 x 10 mm PLA beam -> 1.333 mm (bending)
    // + F L / (k G A) = 0.011 mm (shear, k = 5/6).
    const double L = 100., b = 10., F = 10., E = 3000., G = E / (2. * 1.35);
    const double I = b * b * b * b / 12.;
    const double expected = F * L * L * L / (3. * E * I) + F * L / (5. / 6. * G * b * b);
    const Result coarse = bar(L, b, b, Vec3d(0., 0., -F), "PLA", 23., 2.5, 0);
    const Result fine   = bar(L, b, b, Vec3d(0., 0., -F), "PLA", 23., 1.25, 0);
    REQUIRE(coarse.ok);
    REQUIRE(fine.ok);
    const double d_coarse = - end_displacement(coarse, 0, 2);
    const double d_fine   = - end_displacement(fine, 0, 2);
    INFO("expected " << expected << " coarse " << d_coarse << " fine " << d_fine);
    // Linear hexahedra are too stiff in bending; the error must shrink with the voxel size.
    CHECK(d_fine > d_coarse);
    CHECK(std::abs(d_fine - expected) < std::abs(d_coarse - expected));
    CHECK(d_fine == Approx(expected).epsilon(0.10));
}

TEST_CASE("Working temperature softens the material", "[FEA]")
{
    const Result cold = bar(40., 10., 10., Vec3d(500., 0., 0.), "PET", 23., 2.);
    const Result warm = bar(40., 10., 10., Vec3d(500., 0., 0.), "PET", 50., 2.);
    REQUIRE(cold.ok);
    REQUIRE(warm.ok);
    const Material &pet = *find_material("PET");
    CHECK(end_displacement(warm, 0, 0) / end_displacement(cold, 0, 0) ==
          Approx(temperature_factor(pet, 23.) / temperature_factor(pet, 50.)).epsilon(1e-3));
}

TEST_CASE("Layers are softer across than along", "[FEA]")
{
    // The same bar along X and along Z: the displacement ratio is E_xy / E_z.
    const Result along  = bar(40., 10., 10., Vec3d(500., 0., 0.), "PA-CF", 23., 2., 0);
    const Result across = bar(40., 10., 10., Vec3d(0., 0., 500.), "PA-CF", 23., 2., 2);
    REQUIRE(along.ok);
    REQUIRE(across.ok);
    const Material &m = *find_material("PA-CF");
    CHECK(end_displacement(across, 2, 2) / end_displacement(along, 0, 0) == Approx(m.E_xy / m.E_z).epsilon(0.05));
}

TEST_CASE("Verdicts", "[FEA]")
{
    // 130 °C: PLA is out of temperature and materials for high temperature are proposed.
    const Result hot = bar(40., 10., 10., Vec3d(500., 0., 0.), "PLA", 130., 2.);
    REQUIRE(hot.ok);
    CHECK(hot.verdict == Verdict::OutOfTemperature);
    bool pei = false;
    for (const auto &[key, sf] : hot.alternatives) {
        CHECK(! out_of_temperature(*find_material(key), 130.));
        pei |= key == "PEI";
    }
    CHECK(pei);

    // 6000 N on 100 mm² is 60 MPa: more than the strength of PLA.
    const Result broken = bar(40., 10., 10., Vec3d(6000., 0., 0.), "PLA", 23., 2.);
    REQUIRE(broken.ok);
    CHECK(broken.verdict == Verdict::OutOfLoad);
    CHECK(broken.safety_factor < 1.);

    // Pulling across the layers is limited by the layer adhesion.
    const Result layers = bar(40., 10., 10., Vec3d(0., 0., 3500.), "PLA", 23., 2., 2);
    REQUIRE(layers.ok);
    CHECK(layers.verdict == Verdict::OutOfLoad);

    const Result tpu = bar(40., 10., 10., Vec3d(10., 0., 0.), "FLEX", 23., 2.);
    REQUIRE(tpu.ok);
    CHECK(tpu.verdict == Verdict::NotApplicable);
}

TEST_CASE("Errors of the setup", "[FEA]")
{
    const indexed_triangle_set its = its_make_cube(10., 10., 10.);
    Setup setup;
    Load load;
    load.point = Vec3d(10., 5., 5.);
    load.force = Vec3d(0., 0., -10.);
    setup.loads.push_back(load);
    CHECK(! analyze(its, setup).ok);         // no fixtures
    setup.fixtures.push_back({ side(its, 0, false) });
    setup.material = "UNOBTAINIUM";
    CHECK(! analyze(its, setup).ok);
    setup.material = "PLA";
    setup.voxel_size = 1.;
    const Result res = analyze(its, setup);
    REQUIRE(res.ok);
    CHECK(res.voxels.size() == 1000);
    int calls = 0;
    const Result stopped = analyze(its, setup, [&]() { return ++ calls > 1; });
    CHECK(! stopped.ok);
}

TEST_CASE("Size of a real analysis", "[.][FEA_benchmark]")
{
    // About 60000 voxels, as the default of the GUI.
    const indexed_triangle_set its = its_make_cube(120., 30., 25.);
    Setup setup;
    setup.fixtures.push_back({ side(its, 0, false) });
    Load load;
    load.point = Vec3d(120., 15., 25.);
    load.force = Vec3d(0., 0., -50.);
    setup.loads.push_back(load);
    const auto t0 = std::chrono::steady_clock::now();
    const Result res = analyze(its, setup);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    REQUIRE(res.ok);
    WARN("voxels " << res.voxels.size() << " h " << res.h << " iterations " << res.iterations << " time " << seconds << " s"
         << " max displacement " << res.max_displacement << " safety " << res.safety_factor);
}

// Object with a 60 x 10 x 10 mm bar, held at x = 0, with a point load at the other end.
static Model bar_model()
{
    Model model;
    ModelObject *object = model.add_object();
    object->name = "bar";
    ModelVolume *volume = object->add_volume(TriangleMesh(its_make_cube(60., 10., 10.)));
    volume->name = "bar";
    object->add_instance();
    EngineeringRegion fixed;
    fixed.volume    = 0;
    fixed.triangles = side(volume->mesh().its, 0, false);
    object->engineering.fixtures.push_back(fixed);
    EngineeringLoad load;
    load.name   = "end";
    load.volume = 0;
    // The mesh of the volume is centered when it is added: the end is at x = 30 and the top at z = 5.
    load.point  = Vec3d(30., 0., 5.);
    load.force  = Vec3d(0., 0., -20.);
    object->engineering.loads.push_back(load);
    object->engineering.temperature = 40.;
    return model;
}

TEST_CASE("Engineering setup is kept in the 3MF", "[FEA]")
{
    Model model = bar_model();
    model.objects.front()->engineering.material = "PET";
    const std::string path = (boost::filesystem::temp_directory_path() / "tisma_engineering_test.3mf").string();
    REQUIRE(store_3mf(path.c_str(), &model, nullptr, false));
    Model                     loaded;
    DynamicPrintConfig        config;
    ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
    boost::optional<Semver>   version;
    REQUIRE(load_3mf(path.c_str(), config, ctxt, &loaded, false, version));
    boost::filesystem::remove(path);
    REQUIRE(loaded.objects.size() == 1);
    const EngineeringSetup &a = model.objects.front()->engineering;
    const EngineeringSetup &b = loaded.objects.front()->engineering;
    CHECK(b.material == "PET");
    CHECK(b.temperature == Approx(40.));
    CHECK(b.fixtures == a.fixtures);
    REQUIRE(b.loads.size() == 1);
    CHECK(b.loads.front().name == "end");
    CHECK((b.loads.front().point - a.loads.front().point).norm() < 1e-6);
    CHECK((b.loads.front().force - a.loads.front().force).norm() < 1e-6);
}

TEST_CASE("Analysis of an object of the model", "[FEA]")
{
    Model model = bar_model();
    Fea::ModelAnalysisInput input;
    std::string error;
    // The material comes from the filament type.
    REQUIRE(build_analysis_input(*model.objects.front(), 0, "PETG", input, error));
    CHECK(input.material == "PET");
    input.setup.voxel_size = 2.;
    const Result flat = analyze(input.mesh, input.setup);
    INFO(flat.error);
    REQUIRE(flat.ok);
    CHECK(flat.max_displacement > 0.);

    // Standing the bar up (rotated 90 degrees about Y) bends it across the layers... no: the bending is now in the
    // plane of the layers, where the material is the same, but the load stays vertical (print coordinates), so it
    // pulls along the bar: much smaller displacement.
    model.objects.front()->instances.front()->set_rotation(Vec3d(0., -0.5 * M_PI, 0.));
    REQUIRE(build_analysis_input(*model.objects.front(), 0, "PETG", input, error));
    input.setup.voxel_size = 2.;
    const Result standing = analyze(input.mesh, input.setup);
    REQUIRE(standing.ok);
    CHECK(standing.max_displacement < 0.2 * flat.max_displacement);

    // Unknown filament types need a chosen material.
    CHECK(! build_analysis_input(*model.objects.front(), 0, "SCAFF", input, error));
    CHECK(! error.empty());
    // A region on a volume which does not exist.
    model.objects.front()->engineering.fixtures.front().volume = 3;
    CHECK(! build_analysis_input(*model.objects.front(), 0, "PLA", input, error));
}

TEST_CASE("Deformation limit", "[FEA]")
{
    Model model = bar_model();
    model.objects.front()->engineering.temperature = 23.;
    Fea::ModelAnalysisInput input;
    std::string error;
    REQUIRE(build_analysis_input(*model.objects.front(), 0, "PLA", input, error));
    input.setup.voxel_size = 2.;
    const Result free = analyze(input.mesh, input.setup);
    REQUIRE(free.ok);
    REQUIRE(free.load_displacement.size() == 1);
    const double d = free.load_displacement.front();
    CHECK(d > 0.);
    CHECK(free.verdict == Verdict::Holds);

    // A limit above the displacement is met.
    model.objects.front()->engineering.loads.front().max_displacement = 2. * d;
    REQUIRE(build_analysis_input(*model.objects.front(), 0, "PLA", input, error));
    input.setup.voxel_size = 2.;
    CHECK(analyze(input.mesh, input.setup).verdict == Verdict::Holds);

    // Half the displacement, given as % of the length of the bar (60 mm): too flexible; only stiffer materials
    // are proposed.
    model.objects.front()->engineering.loads.front().max_displacement = 0.;
    model.objects.front()->engineering.loads.front().max_displacement_percent = 100. * 0.5 * d / 60.;
    REQUIRE(build_analysis_input(*model.objects.front(), 0, "PLA", input, error));
    CHECK(input.setup.loads.front().max_displacement == Approx(0.5 * d));
    input.setup.voxel_size = 2.;
    const Result stiff = analyze(input.mesh, input.setup);
    REQUIRE(stiff.ok);
    CHECK(stiff.verdict == Verdict::TooFlexible);
    REQUIRE(! stiff.alternatives.empty());
    for (const auto &[key, sf] : stiff.alternatives)
        CHECK(find_material(key)->E_xy >= 2. * find_material("PLA")->E_xy);
}
